#include "Writer/AlbumArt.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sys/ioctl.h>
#include <unistd.h>
#ifdef NCURSESW
#include <ncursesw/ncurses.h>
#else
#include <ncurses.h>
#endif

namespace
{
const char base64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const std::string &data)
{
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3)
    {
        const auto byte = [&](size_t at) { return at < data.size() ? static_cast<uint8_t>(data[at]) : 0u; };
        const uint32_t chunk = (byte(i) << 16) | (byte(i + 1) << 8) | byte(i + 2);
        out += base64_alphabet[(chunk >> 18) & 63];
        out += base64_alphabet[(chunk >> 12) & 63];
        out += i + 1 < data.size() ? base64_alphabet[(chunk >> 6) & 63] : '=';
        out += i + 2 < data.size() ? base64_alphabet[chunk & 63] : '=';
    }
    return out;
}

uint32_t crc32(const std::string &data, size_t offset, size_t length)
{
    static uint32_t table[256];
    static bool ready = false;
    if (!ready)
    {
        for (uint32_t n = 0; n < 256; ++n)
        {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xffffffffu;
    for (size_t i = offset; i < offset + length; ++i)
        crc = table[(crc ^ static_cast<uint8_t>(data[i])) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

void put32(std::string &out, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) out += static_cast<char>((value >> shift) & 0xff);
}

void chunk(std::string &out, const char *type, const std::string &body)
{
    put32(out, static_cast<uint32_t>(body.size()));
    const auto start = out.size();
    out += type;
    out += body;
    put32(out, crc32(out, start, out.size() - start));
}

scmp::PixelImage resize(const scmp::PixelImage &image, int width, int height)
{
    // Box filter: average every source pixel that falls inside each target pixel.
    scmp::PixelImage out;
    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y)
    {
        const int y0 = y * image.height / height, y1 = std::max(y0 + 1, (y + 1) * image.height / height);
        for (int x = 0; x < width; ++x)
        {
            const int x0 = x * image.width / width, x1 = std::max(x0 + 1, (x + 1) * image.width / width);
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; ++sy)
            {
                for (int sx = x0; sx < x1; ++sx)
                {
                    const auto pixel = image.pixels[static_cast<size_t>(sy) * image.width + sx];
                    r += (pixel >> 16) & 0xff;
                    g += (pixel >> 8) & 0xff;
                    b += pixel & 0xff;
                    ++n;
                }
            }
            out.pixels[static_cast<size_t>(y) * width + x] = 0xff000000u | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    return out;
}

bool inside_multiplexer()
{
    return std::getenv("TMUX") || std::getenv("STY");
}

bool contains(const char *text, const char *needle)
{
    return text && std::strstr(text, needle);
}

// Terminal cell size in pixels, from the kernel when the terminal reports it.
std::pair<int, int> cell_pixels()
{
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col && size.ws_row && size.ws_xpixel && size.ws_ypixel)
    {
        return {size.ws_xpixel / size.ws_col, size.ws_ypixel / size.ws_row};
    }
    return {10, 20};
}
}

scmp::ImageProtocol scmp::detect_image_protocol()
{
    const char *forced = std::getenv("SCMP_IMAGE_PROTOCOL");
    if (forced && *forced)
    {
        const std::string name(forced);
        if (name == "kitty") return ImageProtocol::Kitty;
        if (name == "iterm") return ImageProtocol::ITerm;
        if (name == "sixel") return ImageProtocol::Sixel;
        return ImageProtocol::None;
    }
    if (!isatty(STDOUT_FILENO) || inside_multiplexer()) return ImageProtocol::None;
    const char *term = std::getenv("TERM");
    const char *program = std::getenv("TERM_PROGRAM");
    if ((term && std::strcmp(term, "xterm-kitty") == 0) || contains(program, "ghostty") ||
        contains(term, "ghostty"))
    {
        return ImageProtocol::Kitty;
    }
    if (contains(program, "iTerm.app") || contains(program, "WezTerm") || std::getenv("ITERM_SESSION_ID"))
    {
        return ImageProtocol::ITerm;
    }
    if (contains(term, "foot") || contains(term, "mlterm") || contains(term, "contour") ||
        contains(program, "Konsole") || std::getenv("KONSOLE_VERSION"))
    {
        return ImageProtocol::Sixel;
    }
    return ImageProtocol::None;
}

std::string scmp::encode_png(const PixelImage &image)
{
    std::string raw;
    raw.reserve(static_cast<size_t>(image.height) * (image.width * 3 + 1));
    for (int y = 0; y < image.height; ++y)
    {
        raw += '\0';
        for (int x = 0; x < image.width; ++x)
        {
            const auto pixel = image.pixels[static_cast<size_t>(y) * image.width + x];
            raw += static_cast<char>((pixel >> 16) & 0xff);
            raw += static_cast<char>((pixel >> 8) & 0xff);
            raw += static_cast<char>(pixel & 0xff);
        }
    }
    // zlib stream of uncompressed deflate blocks.
    std::string zlib("\x78\x01", 2);
    for (size_t offset = 0; offset < raw.size() || offset == 0; offset += 65535)
    {
        const auto length = std::min<size_t>(65535, raw.size() - offset);
        const bool last = offset + length >= raw.size();
        zlib += static_cast<char>(last ? 1 : 0);
        zlib += static_cast<char>(length & 0xff);
        zlib += static_cast<char>((length >> 8) & 0xff);
        zlib += static_cast<char>(~length & 0xff);
        zlib += static_cast<char>((~length >> 8) & 0xff);
        zlib.append(raw, offset, length);
        if (last) break;
    }
    uint32_t a = 1, b = 0;
    for (const auto c : raw)
    {
        a = (a + static_cast<uint8_t>(c)) % 65521;
        b = (b + a) % 65521;
    }
    put32(zlib, (b << 16) | a);

    std::string png("\x89PNG\r\n\x1a\n", 8);
    std::string header;
    put32(header, static_cast<uint32_t>(image.width));
    put32(header, static_cast<uint32_t>(image.height));
    header += std::string("\x08\x02\x00\x00\x00", 5);
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", zlib);
    chunk(png, "IEND", "");
    return png;
}

std::string scmp::encode_sixel(const PixelImage &image, int width, int height)
{
    if (image.empty() || width <= 0 || height <= 0) return {};
    // Quantize to a 6x6x6 cube plus a 24-step gray ramp (240 registers) with Floyd-Steinberg dithering.
    static const int cube[] = {0, 95, 135, 175, 215, 255};
    const auto palette_rgb = [](int index, int channel) {
        if (index >= 216) return 8 + (index - 216) * 10;
        const int digit = channel == 0 ? index / 36 : channel == 1 ? (index / 6) % 6 : index % 6;
        return cube[digit];
    };
    const auto nearest = [&](int r, int g, int b) {
        const auto step = [](int value) {
            int best = 0;
            for (int i = 1; i < 6; ++i) if (std::abs(cube[i] - value) < std::abs(cube[best] - value)) best = i;
            return best;
        };
        const int color = 36 * step(r) + 6 * step(g) + step(b);
        const int gray = 216 + std::max(0, std::min(23, ((r + g + b) / 3 - 3) / 10));
        const auto error = [&](int index) {
            const int dr = r - palette_rgb(index, 0), dg = g - palette_rgb(index, 1), db = b - palette_rgb(index, 2);
            return dr * dr + dg * dg + db * db;
        };
        return error(gray) < error(color) ? gray : color;
    };
    std::vector<int> work(static_cast<size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y)
    {
        const int sy = std::min(image.height - 1, y * image.height / height);
        for (int x = 0; x < width; ++x)
        {
            const int sx = std::min(image.width - 1, x * image.width / width);
            const auto pixel = image.pixels[static_cast<size_t>(sy) * image.width + sx];
            auto *cell = &work[(static_cast<size_t>(y) * width + x) * 3];
            cell[0] = (pixel >> 16) & 0xff;
            cell[1] = (pixel >> 8) & 0xff;
            cell[2] = pixel & 0xff;
        }
    }
    std::vector<uint8_t> indices(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            auto *cell = &work[(static_cast<size_t>(y) * width + x) * 3];
            const int r = std::max(0, std::min(255, cell[0]));
            const int g = std::max(0, std::min(255, cell[1]));
            const int b = std::max(0, std::min(255, cell[2]));
            const int index = nearest(r, g, b);
            indices[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(index);
            const int error[] = {r - palette_rgb(index, 0), g - palette_rgb(index, 1), b - palette_rgb(index, 2)};
            const auto spread = [&](int dx, int dy, int weight) {
                if (x + dx < 0 || x + dx >= width || y + dy >= height) return;
                auto *next = &work[(static_cast<size_t>(y + dy) * width + x + dx) * 3];
                for (int c = 0; c < 3; ++c) next[c] += error[c] * weight / 16;
            };
            spread(1, 0, 7);
            spread(-1, 1, 3);
            spread(0, 1, 5);
            spread(1, 1, 1);
        }
    }
    std::string out = "\033P0;1;0q\"1;1;" + std::to_string(width) + ";" + std::to_string(height);
    std::vector<bool> used(240, false);
    for (const auto index : indices) used[index] = true;
    for (int i = 0; i < 240; ++i)
    {
        if (!used[static_cast<size_t>(i)]) continue;
        out += "#" + std::to_string(i) + ";2";
        for (int channel = 0; channel < 3; ++channel)
            out += ";" + std::to_string(palette_rgb(i, channel) * 100 / 255);
    }
    std::vector<char> bits(static_cast<size_t>(width));
    for (int band = 0; band < height; band += 6)
    {
        std::map<int, bool> colors;
        for (int y = band; y < std::min(height, band + 6); ++y)
            for (int x = 0; x < width; ++x) colors[indices[static_cast<size_t>(y) * width + x]] = true;
        bool first = true;
        for (const auto &color : colors)
        {
            for (int x = 0; x < width; ++x)
            {
                int mask = 0;
                for (int row = 0; row < 6 && band + row < height; ++row)
                {
                    if (indices[static_cast<size_t>(band + row) * width + x] == color.first) mask |= 1 << row;
                }
                bits[static_cast<size_t>(x)] = static_cast<char>(63 + mask);
            }
            if (!first) out += '$';
            first = false;
            out += "#" + std::to_string(color.first);
            for (int x = 0; x < width;)
            {
                int run = 1;
                while (x + run < width && bits[static_cast<size_t>(x + run)] == bits[static_cast<size_t>(x)]) ++run;
                if (run > 3) out += "!" + std::to_string(run) + bits[static_cast<size_t>(x)];
                else out.append(static_cast<size_t>(run), bits[static_cast<size_t>(x)]);
                x += run;
            }
        }
        out += '-';
    }
    out += "\033\\";
    return out;
}

scmp::AlbumArt::AlbumArt() : m_protocol(detect_image_protocol())
{
}

scmp::AlbumArt::~AlbumArt()
{
    if (m_protocol == ImageProtocol::Kitty) clear();
}

bool scmp::AlbumArt::place(const PixelImage &image, const std::string &path,
                          int row, int col, int width, int height)
{
    if (m_protocol == ImageProtocol::None || image.empty()) return false;
    m_image = &image;
    m_path = path;
    m_area = {row, col, width, height};
    return true;
}

void scmp::AlbumArt::clear()
{
    if (m_displayed_path.empty()) return;
    if (m_protocol == ImageProtocol::Kitty)
    {
        std::printf("\033_Ga=d,d=I,i=%u,q=2\033\\", static_cast<unsigned>(getpid()));
        std::fflush(stdout);
    }
    else if (!isendwin())
    {
        // iTerm2 and sixel images live in the cells. Repainting those lines replaces them.
        wredrawln(stdscr, m_displayed_area[0], m_displayed_area[3]);
        wrefresh(stdscr);
    }
    m_displayed_path.clear();
}

void scmp::AlbumArt::draw()
{
    if (!m_image || m_path != m_displayed_path || m_area != m_displayed_area) clear();
    if (!m_image || !m_displayed_path.empty()) return;

    std::printf("\0337\033[%d;%dH", m_area[0] + 1, m_area[1] + 1);
    if (m_protocol == ImageProtocol::Kitty)
    {
        // Each 24-bit RGB pixel encodes to exactly four Base64 characters.
        std::string encoded;
        encoded.reserve(m_image->pixels.size() * 4);
        for (const auto pixel : m_image->pixels)
        {
            for (int shift = 18; shift >= 0; shift -= 6) encoded += base64_alphabet[(pixel >> shift) & 63];
        }
        for (size_t offset = 0; offset < encoded.size(); offset += 4096)
        {
            const auto count = std::min<size_t>(4096, encoded.size() - offset);
            const int more = offset + count < encoded.size();
            if (offset == 0)
            {
                std::printf("\033_Ga=T,f=24,s=%d,v=%d,c=%d,r=%d,i=%u,C=1,q=2,m=%d;",
                            m_image->width, m_image->height, m_area[2], m_area[3],
                            static_cast<unsigned>(getpid()), more);
            }
            else
            {
                std::printf("\033_Gm=%d;", more);
            }
            std::fwrite(encoded.data() + offset, 1, count, stdout);
            std::fputs("\033\\", stdout);
        }
    }
    else if (m_protocol == ImageProtocol::ITerm)
    {
        // Send only as many pixels as the cells can show. Large payloads make iTerm2 ask before displaying.
        const auto cell = cell_pixels();
        const int side = std::max(1, std::min({m_image->width, m_image->height, 192,
                                               std::max(m_area[2] * cell.first, m_area[3] * cell.second)}));
        const auto png = encode_png(resize(*m_image, side, side));
        std::printf("\033]1337;File=inline=1;size=%zu;width=%d;height=%d;preserveAspectRatio=1:",
                    png.size(), m_area[2], m_area[3]);
        const auto encoded = base64(png);
        std::fwrite(encoded.data(), 1, encoded.size(), stdout);
        std::fputs("\a", stdout);
    }
    else
    {
        const auto cell = cell_pixels();
        // Keep the cover square within the cell area.
        const int side = std::min(m_area[2] * cell.first, m_area[3] * cell.second);
        const auto sixel = encode_sixel(resize(*m_image, side, side), side, side);
        std::fwrite(sixel.data(), 1, sixel.size(), stdout);
    }
    std::fputs("\0338", stdout);
    std::fflush(stdout);
    m_displayed_path = m_path;
    m_displayed_area = m_area;
}
