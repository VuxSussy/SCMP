#include "Writer/AlbumArt.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

scmp::AlbumArt::AlbumArt()
{
    const auto *term = std::getenv("TERM");
    m_supported = isatty(STDOUT_FILENO) && !std::getenv("TMUX") &&
        term && std::strcmp(term, "xterm-kitty") == 0;
}

scmp::AlbumArt::~AlbumArt()
{
    clear();
}

bool scmp::AlbumArt::place(const PixelImage &image, const std::string &path,
                          int row, int col, int width, int height)
{
    if (!m_supported || image.empty()) return false;
    m_image = &image;
    m_path = path;
    m_area = {row, col, width, height};
    return true;
}

void scmp::AlbumArt::clear()
{
    if (m_displayed_path.empty()) return;
    std::printf("\033_Ga=d,d=I,i=%u,q=2\033\\", static_cast<unsigned>(getpid()));
    std::fflush(stdout);
    m_displayed_path.clear();
}

void scmp::AlbumArt::draw()
{
    if (!m_image || m_path != m_displayed_path || m_area != m_displayed_area) clear();
    if (!m_image || !m_displayed_path.empty()) return;

    // Each 24-bit RGB pixel encodes to exactly four Base64 characters.
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(m_image->pixels.size() * 4);
    for (const auto pixel : m_image->pixels)
    {
        for (int shift = 18; shift >= 0; shift -= 6) encoded += alphabet[(pixel >> shift) & 63];
    }

    std::printf("\0337\033[%d;%dH", m_area[0] + 1, m_area[1] + 1);
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
    std::fputs("\0338", stdout);
    std::fflush(stdout);
    m_displayed_path = m_path;
    m_displayed_area = m_area;
}
