#include "Writer/StudioView.h"
#include "Writer/TerminalText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace
{
struct GraphArea
{
    int x;
    int y;
    int width;
    int height;
};

GraphArea graph_area(int width, int height, bool inset = true)
{
    if (!inset) return {0, 0, width, height};
    const int x = width >= 88 ? 3 : 0;
    const int y = height >= 20 ? 1 : 0;
    return {x, y, width - x * 2, height - y * 2};
}

std::string effective_scene(const std::string &theme)
{
    return theme == "auto" ? scmp::themes().front().id : theme;
}

std::string scene_label(const std::string &theme)
{
    const auto &catalog = scmp::themes();
    const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const scmp::Theme &item) {
        return theme == item.id;
    });
    return found == catalog.end() ? "" : found->label;
}

int rgb_color(uint32_t rgb, double level)
{
    const int r = static_cast<int>(((rgb >> 16) & 0xff) * level);
    const int g = static_cast<int>(((rgb >> 8) & 0xff) * level);
    const int b = static_cast<int>((rgb & 0xff) * level);
    static const int steps[] = {0, 95, 135, 175, 215, 255};
    const auto cube = [](int value) {
        return static_cast<int>(std::min_element(steps, steps + 6, [&](int first, int second) {
            return std::abs(first - value) < std::abs(second - value);
        }) - steps);
    };
    const int ri = cube(r), gi = cube(g), bi = cube(b);
    const int gray = std::max(0, std::min(23, ((r + g + b) / 3 - 3) / 10));
    const int value = 8 + gray * 10;
    const auto square = [](int x) { return x * x; };
    const int gray_error = square(r - value) + square(g - value) + square(b - value);
    const int cube_error = square(r - steps[ri]) + square(g - steps[gi]) + square(b - steps[bi]);
    const bool low_chroma = std::max({r, g, b}) - std::min({r, g, b}) < 28;
    const bool neutral_cube = ri == gi && gi == bi;
    return gray_error < cube_error && (low_chroma || neutral_cube)
        ? 232 + gray : 16 + 36 * ri + 6 * gi + bi;
}

int wallpaper_color(uint32_t rgb)
{
    const double r = (rgb >> 16) & 0xff;
    const double g = (rgb >> 8) & 0xff;
    const double b = rgb & 0xff;
    const double luminance = .2126 * r + .7152 * g + .0722 * b;
    // Add saturation around the existing luminance and bound all three channels together.
    const double dark = luminance - std::min({r, g, b});
    const double light = std::max({r, g, b}) - luminance;
    double saturation = 1.30;
    if (dark > 0) saturation = std::min(saturation, luminance / dark);
    if (light > 0) saturation = std::min(saturation, (255 - luminance) / light);
    const auto channel = [&](double value) {
        return static_cast<uint32_t>(std::round(luminance + (value - luminance) * saturation));
    };
    return rgb_color((channel(r) << 16) | (channel(g) << 8) | channel(b), 1.0);
}

uint32_t ansi_rgb(int index)
{
    if (index >= 232)
    {
        return static_cast<uint32_t>(8 + (index - 232) * 10) * 0x010101;
    }
    const int cube = std::max(0, index - 16);
    static const uint32_t values[] = {0, 95, 135, 175, 215, 255};
    return (values[cube / 36] << 16) | (values[(cube / 6) % 6] << 8) | values[cube % 6];
}

void pixel(WINDOW *window, int row, int col, int color, wchar_t glyph = L'█')
{
    const wchar_t cell[] = {glyph, L'\0'};
    wattrset(window, color >= 0 && color < COLORS && color < COLOR_PAIRS ? COLOR_PAIR(color) : A_DIM);
    mvwaddnwstr(window, row, col, cell, 1);
}

uint32_t sample_image(const scmp::PixelImage &image, double x, double y,
                      double view_aspect)
{
    const double aspect = static_cast<double>(image.width) / image.height;
    if (aspect > view_aspect)
    {
        x = 0.5 + (x - 0.5) * view_aspect / aspect;
    }
    else
    {
        y = 0.5 + (y - 0.5) * aspect / view_aspect;
    }
    const int ix = std::min(image.width - 1, static_cast<int>(x * image.width));
    const int iy = std::min(image.height - 1, static_cast<int>(y * image.height));
    return image.pixels[static_cast<size_t>(iy) * image.width + ix];
}

void draw_cover(WINDOW *surface, int row, int x, int width, int height,
                const scmp::PixelImage &art)
{
    for (int cy = 0; cy < height; ++cy)
    {
        for (int cx = 0; cx < width; ++cx)
        {
            const auto u = (cx + 0.5) / width;
            const auto v = (cy + 0.5) / height;
            if (!art.empty())
            {
                const auto color = sample_image(art, u, v, static_cast<double>(width) / (height * 2));
                const auto brightness = ((color >> 16) & 0xff) + ((color >> 8) & 0xff) + (color & 0xff);
                constexpr wchar_t shades[] = L" .:*#";
                pixel(surface, row + cy, x + cx, rgb_color(color, 1), COLORS >= 256 ? L'█' : shades[brightness / 160]);
                continue;
            }
            const double radius = std::hypot(u - 0.5, v - 0.5);
            const int color = radius < 0.09 ? 117 : radius < 0.16 ? 67 :
                              radius < 0.38 ? (static_cast<int>(radius * 32) % 2 ? 239 : 236) : 17;
            const auto glyph = COLORS >= 256 ? L'█' : radius < 0.09 ? L'+' : radius < 0.38 ? L'o' : L'·';
            pixel(surface, row + cy, x + cx, color, glyph);
        }
    }
    wattrset(surface, A_NORMAL);
}

int scene_color(const std::string &scene, int col, int row, int width, int height)
{
    const double x = (col + 0.5) / width;
    const double y = (row + 0.5) / height;
    const uint32_t grain = (static_cast<uint32_t>(col) * 374761393u) ^
                           (static_cast<uint32_t>(row) * 668265263u);
    const bool star = (grain ^ (grain >> 13)) % 79 == 0;
    if (scene == "night-drive")
    {
        const double sun = std::hypot((x - 0.52) * width / (2.0 * height), y - 0.31);
        if (y > 0.57)
        {
            const double road = std::abs(x - 0.52);
            const double spread = (y - 0.57) * 0.76;
            if (std::abs(road - spread) < 0.012) return 60;
            if (road < spread)
                return road < 0.008 && row % 3 == 0 ? 97 : 235;
            return row % 3 == 0 ? 54 : 17;
        }
        if (y > 0.41 + 0.13 * (grain >> 6 & 7) / 7.0 && std::abs(x - 0.52) > 0.20)
        {
            return star ? 103 : 235;
        }
        if (sun < 0.24 && (y < 0.32 || row % 3 != 0))
        {
            return y < 0.26 ? 126 : y < 0.36 ? 90 : 54;
        }
        return star ? 61 : (y < 0.23 ? 17 : y < 0.45 ? 53 : 54);
    }
    if (scene == "alpine")
    {
        const double peaks = 0.54 - 0.28 * std::max({0.0, 1 - std::abs(x - 0.27) * 4.8,
                                                          1 - std::abs(x - 0.73) * 4.0});
        if (y > 0.66) return (row + (grain % 3)) % 5 == 0 ? 24 : 17;
        if (y > 0.57 - (std::max(0.0, 1 - std::abs(x - 0.50) * 3.5) * 0.12)) return 23;
        if (y > peaks) return y < peaks + 0.08 ? 67 : 24;
        return star ? 67 : (y < 0.23 ? 17 : 18);
    }
    if (scene == "orbit")
    {
        const double dx = (x - 0.73) * width / (2.0 * height);
        const double dy = y - 0.51;
        const double planet = std::hypot(dx, dy);
        const double ring = std::hypot(dx, (dy + 0.26 * dx) * 3.2);
        if (ring > 0.27 && ring < 0.40 && (planet > 0.20 || dy > 0))
            return ring > 0.34 ? 59 : 60;
        if (planet < 0.20)
            return dx - dy < -0.05 ? 60 : (dx - dy < 0.07 ? 54 : 17);
        return star ? 103 : (y < 0.34 ? 234 : 17);
    }
    return -1;
}

std::string timestamp(double seconds)
{
    const auto time = static_cast<unsigned>(std::max(0.0, std::min(359999.0, seconds)));
    char text[24];
    std::snprintf(text, sizeof(text), "%u:%02u", time / 60, time % 60);
    return text;
}
}

namespace
{
// Ranger-inspired palette with 8-color fallbacks.
int ink(int color256, int basic)
{
    if (COLORS >= 256) return COLOR_PAIR(color256);
    return COLORS > basic ? COLOR_PAIR(basic) : A_NORMAL;
}
int green() { return ink(114, COLOR_GREEN); }
int blue() { return ink(75, COLOR_BLUE); }
int magenta() { return ink(176, COLOR_MAGENTA); }
int cyan() { return ink(80, COLOR_CYAN); }
int yellow() { return ink(221, COLOR_YELLOW); }
int red() { return ink(203, COLOR_RED); }
int gray() { return COLORS >= 256 ? COLOR_PAIR(245) : A_DIM; }

size_t display_width(const std::string &text)
{
    std::mbstate_t state{};
    size_t cells = 0;
    for (size_t offset = 0; offset < text.size();)
    {
        wchar_t character = L' ';
        auto consumed = std::mbrtowc(&character, text.data() + offset, text.size() - offset, &state);
        if (consumed == 0 || consumed == static_cast<size_t>(-1) || consumed == static_cast<size_t>(-2))
        {
            state = {};
            consumed = 1;
        }
        cells += static_cast<size_t>(std::max(0, wcwidth(character)));
        offset += consumed;
    }
    return cells;
}

std::string home_relative(const std::string &path)
{
    const char *home = std::getenv("HOME");
    if (home && *home && path.compare(0, std::strlen(home), home) == 0) return "~" + path.substr(std::strlen(home));
    return path;
}

// Ranger's scroll indicator: All, Top, Bot or a percentage.
std::string scroll_label(size_t first, size_t shown, size_t total)
{
    if (shown >= total) return "All";
    if (first == 0) return "Top";
    if (first + shown >= total) return "Bot";
    return std::to_string(first * 100 / (total - shown)) + "%";
}
}

void scmp::StudioView::fill_bar(int row, int x, int width, int background)
{
    if (width <= 0) return;
    m_writer.set_colors(stdscr, -1, background);
    mvwhline(stdscr, row, x, ' ', width);
    wattrset(stdscr, A_NORMAL);
}

int scmp::StudioView::draw_segments(int row, int x, int width, const std::vector<Segment> &segments)
{
    int column = x;
    for (const auto &segment : segments)
    {
        const int cells = static_cast<int>(display_width(segment.text));
        if (column - x + cells > width) break;
        draw_label(stdscr, row, column, x + width - column, segment.text, segment.style);
        column += cells;
    }
    return column - x;
}

void scmp::StudioView::draw_hints(int row, int x, int width, const std::vector<std::pair<std::string, std::string>> &hints,
                                  int key_style)
{
    std::vector<Segment> segments;
    for (size_t i = 0; i < hints.size(); ++i)
    {
        // Each hint must fit whole, so measure it before adding it.
        const auto gap = i ? std::string("   ") : std::string();
        std::vector<Segment> next = segments;
        next.push_back({gap + hints[i].first, key_style});
        next.push_back({" " + hints[i].second, gray()});
        size_t cells = 0;
        for (const auto &segment : next) cells += display_width(segment.text);
        if (static_cast<int>(cells) > width) break;
        segments = std::move(next);
    }
    draw_segments(row, x, width, segments);
}

scmp::StudioView::StudioView(const WorkspaceSettings &settings, NcursesWriter &writer)
    : m_writer(writer), m_theme(settings.theme), m_theme_motion(settings.theme_motion)
{
    if (settings.background != "none" && !settings.background.empty())
    {
#ifdef _OS_OSX
        m_background = load_pixel_image(expand_path(settings.background));
#endif
        m_show_background = !m_background.empty();
        if (m_background.empty())
        {
            m_background_error = "Could not load background image";
        }
    }
}

void scmp::StudioView::cycle_theme()
{
    const auto current = effective_scene(m_theme);
    if (m_show_background)
    {
        m_show_background = false;
        m_theme = themes().front().id;
        return;
    }
    const auto &catalog = themes();
    auto found = std::find_if(catalog.begin(), catalog.end(), [&](const Theme &scene) { return current == scene.id; });
    const bool wrap = found == catalog.end() || ++found == catalog.end();
    m_show_background = wrap && !m_background.empty();
    m_theme = wrap ? catalog.front().id : found->id;
    m_background_error.clear();
}

bool scmp::StudioView::choose_theme(const volatile std::sig_atomic_t &interrupted)
{
    const auto &catalog = themes();
    const auto current = effective_scene(m_theme);
    const size_t count = catalog.size() + (m_background.empty() ? 0 : 1);
    const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const Theme &theme) {
        return current == theme.id;
    });
    size_t selected = m_show_background ? catalog.size()
        : found == catalog.end() ? 0 : static_cast<size_t>(found - catalog.begin());
    timeout(100);
    while (!interrupted)
    {
        erase();
        draw_text(stdscr, 0, 2, COLS - 4, "Wallpaper", A_BOLD);
        const size_t rows = static_cast<size_t>(std::max(1, LINES - 4));
        const size_t first = selected >= rows ? selected - rows + 1 : 0;
        for (size_t i = first; i < count && i - first < rows; ++i)
        {
            const auto *label = i == catalog.size() ? "Custom background"
                : *catalog[i].label ? catalog[i].label : "None";
            draw_text(stdscr, 2 + static_cast<int>(i - first), 2, COLS - 4, label,
                      i == selected ? A_REVERSE : A_NORMAL);
        }
        draw_text(stdscr, LINES - 1, 2, COLS - 4, "j/k select   Enter apply   Esc back", A_DIM);
        refresh();
        const int key = getch();
        if (key == 27 || key == 'q') return false;
        if ((key == KEY_UP || key == 'k') && selected > 0) --selected;
        if ((key == KEY_DOWN || key == 'j') && selected + 1 < count) ++selected;
        if (key == KEY_ENTER || key == '\n' || key == '\r')
        {
            m_show_background = selected == catalog.size();
            if (!m_show_background) m_theme = catalog[selected].id;
            m_background_error.clear();
            return true;
        }
    }
    return false;
}

void scmp::StudioView::restore_theme(const std::string &theme)
{
    if (theme == "custom")
    {
        m_show_background = !m_background.empty();
    }
    else if (std::any_of(themes().begin(), themes().end(), [&](const Theme &item) { return theme == item.id; }))
    {
        m_show_background = false;
        m_theme = theme;
    }
}

WINDOW *scmp::StudioView::begin_frame(const MusicSession &music, ViewMode view, const std::string &effect,
                                     const char *input, const std::string &status)
{
    erase();
    m_album_art.begin_frame();
    m_stage_visible = false;
    const int rows = LINES;
    const int width = COLS;
    if (rows < 12 || width < 30)
    {
        draw_label(stdscr, 0, 0, width, "Enlarge the terminal to use scmp.");
        return nullptr;
    }
    const bool local = music.loaded();
    const bool stage = view == ViewMode::Visualizer;
    m_full_stage = stage;
    const int header = 3;
    const int bottom = rows - 5;
    constexpr int stage_x = 2;
    const int content_width = width - 4;
    const int content_height = bottom - header;
    const auto scene = effective_scene(m_theme);
    const auto theme = m_show_background ? "Custom background" : scene_label(scene);
    // The wallpaper fills the whole window. Behind the playlist it is dimmed so titles stay readable.
    const bool wide = !stage && width >= 96;
    const int list_width = std::min(76, content_width * 2 / 5);
    const int graph_height = !stage && !wide && content_height >= 9 ? std::min(11, std::max(5, content_height / 3)) : 0;
    const int list_height = content_height - graph_height - (graph_height ? 1 : 0);
    m_dim = stage ? std::array<int, 4>{0, 0, 0, 0}
          : wide ? std::array<int, 4>{0, header, list_width + 4, content_height}
                 : std::array<int, 4>{0, header, width, list_height};
    draw_background(scene, stage_x, header, bottom, stage);

    const int accent = COLORS >= 256 ? COLOR_PAIR(117) | A_BOLD :
                       COLORS > COLOR_CYAN ? COLOR_PAIR(COLOR_CYAN) | A_BOLD : A_BOLD;
    for (int row = 0; row < header; ++row) mvwhline(stdscr, row, 0, ' ', width);
    for (int row = bottom; row < rows; ++row) mvwhline(stdscr, row, 0, ' ', width);
    draw_label(stdscr, 0, 2, 14, " 1 Visualizer ", stage ? A_REVERSE | A_BOLD : A_DIM);
    draw_label(stdscr, 0, 17, 10, " 2 Player ", stage ? A_DIM : A_REVERSE | A_BOLD);
    if (width >= 36)
    {
        const auto brand = stage && !theme.empty() && width >= 56 ? "SCMP  " + theme : "SCMP";
        const int x = width - 2 - static_cast<int>(brand.size());
        draw_label(stdscr, 0, x, width - x - 1, brand, A_DIM);
    }
    {
        const char *user = std::getenv("USER");
        std::vector<Segment> path{{std::string(user && *user ? user : "you") + "@scmp ", green() | A_BOLD}};
        if (stage) path.push_back({input, blue() | A_BOLD});
        else if (!local) path.push_back({"no folder open", blue() | A_BOLD});
        else
        {
            path.push_back({home_relative(music.directory()) + "/", blue() | A_BOLD});
            if (!music.visible_tracks().empty())
            {
                const auto &file = music.tracks()[music.selected()].path;
                path.push_back({file.substr(file.find_last_of('/') + 1), A_BOLD});
            }
        }
        // Long paths keep their start, like ranger's title bar.
        int column = 2;
        for (const auto &segment : path)
        {
            if (column >= width - 2) break;
            draw_label(stdscr, 1, column, width - 2 - column, segment.text, segment.style);
            column += static_cast<int>(display_width(segment.text));
        }
    }
    const std::wstring rule(content_width, L'─');
    m_writer.write_over(stdscr, 2, 2, rule, A_DIM);
    m_writer.write_over(stdscr, bottom, 2, rule, A_DIM);

    GraphArea canvas{stage_x, header, content_width, content_height};
    if (wide)
    {
        draw_playlist(music, stage_x, list_width, header, content_height);
        for (int row = header; row < bottom; ++row)
        {
            m_writer.write_over(stdscr, row, list_width + 3, L"│", A_DIM);
        }
        canvas = {list_width + 6, header, content_width - list_width - 4, content_height};
    }
    else if (!stage)
    {
        draw_playlist(music, stage_x, content_width, header, list_height);
        canvas = {stage_x, bottom - graph_height, content_width, graph_height};
    }

    const auto *player = music.player();
    const bool has_track = player && player->duration() > 0;
    const auto position = has_track ? player->elapsed() : 0.0;
    const auto duration = has_track ? player->duration() : 0.0;
    if (has_track)
    {
        // Right: modes, volume and time as colored segments. Left: state, title and artist.
        std::vector<Segment> right;
        if (music.shuffle()) right.push_back({"shuffle  ", magenta() | A_BOLD});
        if (music.repeat() != RepeatMode::Off)
            right.push_back({music.repeat() == RepeatMode::One ? "repeat:one  " : "repeat  ", magenta() | A_BOLD});
        if (width >= 60)
            right.push_back({"vol " + std::to_string(static_cast<int>(std::lround(music.volume() * 100))) + "%  ", yellow()});
        right.push_back({timestamp(position) + " / " + timestamp(duration), cyan()});
        int right_width = 0;
        for (const auto &segment : right) right_width += static_cast<int>(display_width(segment.text));
        const int time_x = width - 2 - right_width;
        draw_segments(bottom + 1, time_x, right_width, right);
        const auto &details = music.playing_details();
        std::vector<Segment> left{
            {player->playing() ? "▶ " : player->finished() ? "■ " : "Ⅱ ", green() | A_BOLD},
            {music.now_playing(), A_BOLD}};
        if (!details.artist.empty()) left.push_back({" · " + details.artist, gray()});
        // The title may be cut, but the artist only shows when it fits whole.
        const int title_room = time_x - 5;
        draw_label(stdscr, bottom + 1, 2, 2, left[0].text, left[0].style);
        draw_label(stdscr, bottom + 1, 4, title_room - 2, left[1].text, left[1].style);
        const int used = 2 + static_cast<int>(display_width(left[1].text));
        if (left.size() > 2 && used + static_cast<int>(display_width(left[2].text)) <= title_room)
            draw_label(stdscr, bottom + 1, 2 + used, title_room - used, left[2].text, left[2].style);
        const int fill = std::max(0, std::min(content_width, static_cast<int>(content_width * position / duration)));
        m_writer.write_over(stdscr, bottom + 2, 2, std::wstring(content_width, L'─'), gray());
        if (fill > 0)
        {
            m_writer.write_over(stdscr, bottom + 2, 2, std::wstring(static_cast<size_t>(fill), L'━'), green());
        }
    }

    const auto &message = stage && !status.empty() ? status :
                          !music.error().empty() ? music.error() : m_background_error;
    using Hints = std::vector<std::pair<std::string, std::string>>;
    Hints controls;
    if (!stage)
        controls = {{"Enter", "play"}, {"Space", "pause"}, {"←/→", "seek"}, {"+/-", "volume"},
                    {"n/N", "next/prev"}, {"z", "shuffle"}, {"R", "repeat"}, {"/", "filter"}, {"v", "effect"}};
    else if (effect == "lava")
        controls = {{"mouse/arrows", "interact"}, {"v", "effect"}, {"T", "wallpaper"}};
    else if (has_track)
        controls = {{"v", "effect"}, {"T", "wallpaper"}, {"Space", "pause"}, {"←/→", "seek"}, {"+/-", "volume"},
                    {"n/N", "next/prev"}};
    else
        controls = {{"v", "effect"}, {"T", "wallpaper"}, {"r", "reconnect"}};
    if (!message.empty() && !has_track)
    {
        draw_label(stdscr, bottom + 1, 2, content_width, message, (music.error().empty() ? yellow() : red()) | A_BOLD);
    }
    else if (!has_track && !stage)
    {
        draw_label(stdscr, bottom + 1, 2, content_width, music.visible_tracks().empty() ?
                   "-- no music --  press o to open a folder" : "-- stopped --  Enter plays the selection", gray());
    }
    if (!message.empty() && has_track)
        draw_label(stdscr, rows - 2, 2, content_width, message, (music.error().empty() ? yellow() : red()) | A_BOLD);
    else
        draw_hints(rows - 2, 2, content_width, controls, yellow() | A_BOLD);

    // Last line: app commands on the left, ranger's position counter on the right.
    std::string position_label;
    if (!stage && m_list_position[1] > 0)
    {
        position_label = std::to_string(m_list_position[0]) + "/" + std::to_string(m_list_position[1]) + "  " +
                         scroll_label(m_list_position[2], m_list_position[3], m_list_position[1]);
    }
    const int position_width = static_cast<int>(display_width(position_label));
    if (position_width)
        draw_label(stdscr, rows - 1, width - 2 - position_width, position_width, position_label, gray());
    draw_hints(rows - 1, 2, content_width - position_width - 3,
               {{"?", "help"}, {"q", "quit"}, {"o", "folder"}, {"i", "Spotify"}, {"Tab", "view"},
                {"s", "settings"}, {"p", "plugins"}}, cyan() | A_BOLD);

    if (canvas.height < 4)
    {
        m_visualizer.reset();
        m_stage.reset();
        return nullptr;
    }
    if (!m_stage || getmaxx(m_stage.get()) != canvas.width || getmaxy(m_stage.get()) != canvas.height ||
        getbegx(m_stage.get()) != canvas.x || getbegy(m_stage.get()) != canvas.y)
    {
        m_visualizer.reset();
        m_stage.reset(derwin(stdscr, canvas.height, canvas.width, canvas.y, canvas.x));
    }
    m_stage_visible = m_stage != nullptr;
    return m_stage.get();
}

void scmp::StudioView::draw_playlist(const MusicSession &music, int x, int width, int top, int height)
{
    const auto &visible = music.visible_tracks();
    if (!visible.empty() && height >= 8)
    {
        const int preview_height = std::min({14, std::max(3, height / 4), (width - 16) / 2});
        draw_track_preview(music, x, width, top, preview_height);
        m_writer.write_over(stdscr, top + preview_height, x, std::wstring(width, L'─'), A_DIM);
        top += preview_height + 1;
        height -= preview_height + 1;
    }
    const auto &tracks = music.tracks();
    const auto count = music.query().empty() ? std::to_string(tracks.size())
        : std::to_string(visible.size()) + "/" + std::to_string(tracks.size());
    const auto tally = count + (tracks.size() == 1 ? " track" : " tracks");
    draw_label(stdscr, top, x, width - static_cast<int>(tally.size()) - 1, "Playlist", blue() | A_BOLD);
    draw_label(stdscr, top, x + width - static_cast<int>(tally.size()),
               static_cast<int>(tally.size()), tally, gray());
    const int digits = static_cast<int>(std::to_string(tracks.size()).size());
    // Columns: marker and number, title, artist (when wide enough), duration.
    const int title_x = x + digits + 5;
    const int time_width = width >= 40 ? 6 : 0;
    const int rest = width - (title_x - x) - time_width;
    const int artist_width = width >= 52 ? std::min(26, rest * 2 / 5) : 0;
    const int title_width = rest - (artist_width ? artist_width + 2 : 0);
    const int artist_x = title_x + title_width + 2;
    const int time_x = x + width - time_width;
    if (music.query().empty())
    {
        draw_label(stdscr, top + 1, x, digits + 2, std::string(static_cast<size_t>(digits + 1), ' ') + "#", A_DIM);
        draw_label(stdscr, top + 1, title_x, title_width, "Title", gray());
        if (artist_width) draw_label(stdscr, top + 1, artist_x, artist_width, "Artist", gray());
        if (time_width) draw_label(stdscr, top + 1, time_x + 1, time_width - 1, " Time", gray());
    }
    else
    {
        draw_label(stdscr, top + 1, x, width, "Filter  " + music.query(), A_DIM);
    }
    if (visible.empty())
    {
        m_list_position = {};
        draw_label(stdscr, top + 2, x, width, tracks.empty() ?
            music.loaded() ? "No playable files in this folder." : "Press i for Spotify or o for a folder."
            : "No matches. Esc clears the filter.", A_NORMAL);
        return;
    }

    const int first_row = top + 2;
    const int list_rows = std::max(1, height - 2);
    const size_t capacity = static_cast<size_t>(list_rows);
    const auto selection = music.selected_row();
    m_queue_start = std::min(m_queue_start, visible.size() > capacity ? visible.size() - capacity : 0);
    if (selection < m_queue_start)
    {
        m_queue_start = selection;
    }
    else if (selection - m_queue_start >= capacity)
    {
        m_queue_start = selection - capacity + 1;
    }
    m_list_position = {selection + 1, visible.size(), m_queue_start, capacity};

    for (size_t row = m_queue_start; row < visible.size() && row - m_queue_start < capacity; ++row)
    {
        const auto index = visible[row];
        const int y = first_row + static_cast<int>(row - m_queue_start);
        const bool selected = row == selection;
        const bool playing = music.active() == index;
        int style = playing ? green() | A_BOLD : A_NORMAL;
        if (selected)
        {
            // A solid bar like ranger's cursor line.
            fill_bar(y, x, width, COLORS >= 256 ? (playing ? 114 : 176) : COLOR_MAGENTA);
            style = (COLORS >= 256 ? COLOR_PAIR(16) : A_REVERSE) | A_BOLD;
        }
        const auto number = std::to_string(index + 1);
        const auto tags = music.tags(index);
        const auto prefix = (playing ? "▶ " : "  ") + std::string(digits - number.size(), ' ') + number;
        draw_label(stdscr, y, x, title_x - x, prefix, style);
        draw_label(stdscr, y, title_x, title_width, tags.title, style);
        if (artist_width) draw_label(stdscr, y, artist_x, artist_width, tags.artist, selected ? style : cyan());
        if (time_width && tags.duration > 0)
        {
            const auto length = timestamp(tags.duration);
            const int column = x + width - static_cast<int>(length.size());
            draw_label(stdscr, y, column, static_cast<int>(length.size()), length, selected ? style : gray());
        }
    }
}

void scmp::StudioView::draw_track_preview(const MusicSession &music, int x, int width, int top, int height)
{
    const auto &details = music.details();
    const int art_width = height * 2;
    if (!m_album_art.place(details.cover, music.tracks()[music.selected()].path, top, x, art_width, height))
        draw_cover(stdscr, top, x, art_width, height, details.cover);

    const int label_x = x + art_width + 2;
    const int label_width = width - art_width - 2;
    draw_label(stdscr, top, label_x, label_width, details.title, A_BOLD);
    draw_label(stdscr, top + 1, label_x, label_width, details.artist, A_DIM);
    const auto &album = details.album.empty() ? music.tracks()[music.selected()].folder : details.album;
    if (album != details.title)
    {
        draw_label(stdscr, top + 2, label_x, label_width, album, A_DIM);
    }
}

void scmp::StudioView::draw_label(WINDOW *surface, int row, int column, int width,
                                 const std::string &text, int style)
{
    draw_text(surface, row, column, width, text, style, &m_writer);
}

WINDOW *scmp::StudioView::visualizer_surface()
{
    auto *stage = m_stage.get();
    const auto area = graph_area(getmaxx(stage), getmaxy(stage), m_full_stage);
    if (area.width < 16 || area.height < 4)
    {
        return stage;
    }
    if (!m_visualizer)
    {
        m_visualizer.reset(derwin(stage, area.height - 2, area.width - 4, area.y + 1, area.x + 2));
    }
    return m_visualizer ? m_visualizer.get() : stage;
}

void scmp::StudioView::draw_visualizer_frame(const std::string &mode, const char *signal, bool audible)
{
    auto *stage = m_stage.get();
    const auto area = graph_area(getmaxx(stage), getmaxy(stage), m_full_stage);
    const int x = area.x;
    const int y = area.y;
    const int width = area.width;
    const int height = area.height;
    const int trim = COLORS >= 256 ? COLOR_PAIR(110) :
                     COLORS > COLOR_CYAN ? COLOR_PAIR(COLOR_CYAN) : A_BOLD;
    const std::wstring horizontal(width - 2, L'─');
    m_writer.write_over(stage, y, x, L"╭" + horizontal + L"╮", trim);
    m_writer.write_over(stage, y + height - 1, x, L"╰" + horizontal + L"╯", trim);
    for (int row = y + 1; row < y + height - 1; ++row)
    {
        m_writer.write_over(stage, row, x, L"│", trim);
        m_writer.write_over(stage, row, x + width - 1, L"│", trim);
    }
    const int status_col = x + width - 3 - static_cast<int>(std::strlen(signal));
    const int accent = audible ? (COLORS >= 256 ? COLOR_PAIR(87) : A_BOLD) : A_DIM;
    const auto title = !m_full_stage && width >= 44 ? "Visualizer / " + mode : mode;
    draw_label(stage, y, x + 2, std::max(1, status_col - x - 3), " " + title + " ", A_BOLD);
    draw_label(stage, y, status_col, x + width - status_col - 1, " " + std::string(signal) + " ", accent);
}

void scmp::StudioView::draw_background(const std::string &scene, int stage_x, int top, int bottom, bool stage_visible)
{
    if (!m_show_background && scene_label(scene).empty())
    {
        return;
    }
    auto *surface = stdscr;
    const int cols = COLS;
    const int rows = LINES;
    const auto graph = graph_area(cols - stage_x - 2, bottom - top);
    constexpr double dim_level = .75;
    const auto dimmed = [&](int x, int y) {
        return x >= m_dim[0] && x < m_dim[0] + m_dim[2] && y >= m_dim[1] && y < m_dim[1] + m_dim[3];
    };
    const auto veil = [&](int x, int y) {
        if (!stage_visible || y < top || y >= bottom || x < stage_x || x >= cols - stage_x)
        {
            return stage_visible ? .60 : .42;
        }
        const int gx = x - stage_x - graph.x;
        const int gy = y - top - graph.y;
        return gx >= 0 && gx < graph.width && gy >= 0 && gy < graph.height ? .62 : 1.0;
    };
#ifdef _OS_OSX
    if (!m_show_background && scene != "night-drive" && scene != "alpine" && scene != "orbit")
    {
        // Two terminal columns approximate one square wallpaper cell.
        const int cells = (cols + 1) / 2;
        const auto &frame = m_wallpaper.frame(scene, cells, rows, m_theme_motion);
        if (!frame.empty())
        {
            for (int y = 0; y < rows; ++y)
            {
                for (int x = 0; x < cells; ++x)
                {
                    const auto &cell = frame[static_cast<size_t>(y) * cells + x];
                    const auto ink = cell.foreground_and_glyph;
                    const bool dim = dimmed(x * 2, y);
                    const double light = dim ? .45 : veil(x * 2, y);
                    const int fg = COLORS >= 256 ? rgb_color(ink & 0xffffff, light) : COLOR_CYAN;
                    const int bg = COLORS >= 256 ? dim ? rgb_color(ansi_rgb(wallpaper_color(cell.background)), dim_level)
                                                       : wallpaper_color(cell.background) : -1;
                    const char tile[] = {light < .65 ? ' ' : static_cast<char>(ink >> 24), ' ', '\0'};
                    m_writer.set_colors(surface, fg, bg);
                    mvwaddnstr(surface, y, x * 2, tile, std::min(2, cols - x * 2));
                }
            }
            wattrset(surface, A_NORMAL);
        }
        return;
    }
#endif
    for (int y = 0; y < rows; ++y)
    {
        for (int x = 0; x < cols; ++x)
        {
            const auto rgb = m_show_background
                ? sample_image(m_background, (x + 0.5) / cols, (y + 0.5) / rows,
                               static_cast<double>(cols) / (2 * rows))
                : ansi_rgb(scene_color(scene, x, y, cols, rows));
            const int color = dimmed(x, y) ? rgb_color(ansi_rgb(wallpaper_color(rgb)), dim_level) : wallpaper_color(rgb);
            if (COLORS >= 256)
            {
                m_writer.set_colors(surface, color, color);
                mvwaddch(surface, y, x, ' ');
            }
            else
            {
                const int basic = scene == "night-drive" ? COLOR_MAGENTA : COLOR_BLUE;
                const uint32_t pattern = (static_cast<uint32_t>(x) * 55503u) ^
                                         (static_cast<uint32_t>(y) * 17431u);
                const int density = 1 + rgb_color(rgb, veil(x, y)) % 3;
                if (static_cast<int>(pattern % 5) < density)
                {
                    pixel(surface, y, x, basic, L'·');
                }
            }
        }
    }
    wattrset(surface, A_NORMAL);
}

void scmp::StudioView::finish_frame()
{
    if (m_stage_visible)
    {
        touchwin(stdscr);
    }
    wnoutrefresh(stdscr);
    doupdate();
    m_album_art.draw();
}

bool scmp::StudioView::show_help(const volatile std::sig_atomic_t &interrupted)
{
    static const char *const keys[][2] = {
        {"", "Player"},
        {"j/k ↑/↓", "move selection"},
        {"Enter", "play selection"},
        {"Space", "pause or resume"},
        {"←/→", "seek 5 seconds"},
        {"+/-", "volume"},
        {"n / N", "next / previous track"},
        {"z", "shuffle"},
        {"R", "repeat off / all / one"},
        {"/", "filter, Esc clears"},
        {"C", "jump to the playing track"},
        {"g / G", "first / last track"},
        {"", ""},
        {"", "Everywhere"},
        {"1 2 Tab", "visualizer / player"},
        {"v", "next visualizer effect"},
        {"t / T", "next wallpaper / wallpaper picker"},
        {"o", "open a music folder"},
        {"i", "import a Spotify playlist"},
        {"s / p", "settings / plugins"},
        {"q", "quit"},
    };
    timeout(100);
    while (!interrupted)
    {
        erase();
        draw_label(stdscr, 0, 0, COLS, "scmp keys", green() | A_BOLD);
        int row = 2;
        for (const auto &entry : keys)
        {
            if (row >= LINES - 1) break;
            if (!*entry[0])
            {
                draw_label(stdscr, row++, 2, COLS - 4, entry[1], blue() | A_BOLD);
                continue;
            }
            draw_label(stdscr, row, 4, 12, entry[0], yellow() | A_BOLD);
            draw_label(stdscr, row++, 18, COLS - 20, entry[1], A_NORMAL);
        }
        draw_label(stdscr, LINES - 1, 0, COLS, "press any key to return", gray());
        refresh();
        if (getch() != ERR) return true;
    }
    return false;
}
