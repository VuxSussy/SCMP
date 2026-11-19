#include "Writer/StudioView.h"
#include "Writer/TerminalText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

void scmp::StudioView::choose_theme(const volatile std::sig_atomic_t &interrupted)
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
        if (key == 27 || key == 'q') return;
        if ((key == KEY_UP || key == 'k') && selected > 0) --selected;
        if ((key == KEY_DOWN || key == 'j') && selected + 1 < count) ++selected;
        if (key == KEY_ENTER || key == '\n' || key == '\r')
        {
            m_show_background = selected == catalog.size();
            if (!m_show_background) m_theme = catalog[selected].id;
            m_background_error.clear();
            return;
        }
    }
}

WINDOW *scmp::StudioView::begin_frame(const MusicSession &music, ViewMode view, const std::string &effect,
                                     const char *input, const std::string &status)
{
    erase();
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
    draw_label(stdscr, 1, 2, content_width, stage ? std::string(input) :
              local ? music.directory() : "Music library", A_DIM);
    const std::wstring rule(content_width, L'─');
    m_writer.write_over(stdscr, 2, 2, rule, A_DIM);
    m_writer.write_over(stdscr, bottom, 2, rule, A_DIM);

    GraphArea canvas{stage_x, header, content_width, content_height};
    if (!stage && width >= 96)
    {
        const int list_width = std::min(76, content_width * 2 / 5);
        const bool show_details = content_height >= 22 && !music.visible_tracks().empty();
        const int details_height = show_details ? 11 : 0;
        draw_playlist(music, stage_x, list_width, header, content_height, !show_details);
        for (int row = header; row < bottom; ++row)
        {
            m_writer.write_over(stdscr, row, list_width + 3, L"│", A_DIM);
        }
        canvas = {list_width + 6, header + details_height,
                  content_width - list_width - 4, content_height - details_height};
        if (show_details)
        {
            draw_track_preview(music, canvas.x, canvas.width, header, details_height);
        }
    }
    else if (!stage)
    {
        const int graph_height = content_height >= 9 ? std::min(11, std::max(5, content_height / 3)) : 0;
        const int list_height = content_height - graph_height - (graph_height ? 1 : 0);
        draw_playlist(music, stage_x, content_width, header, list_height, true);
        canvas = {stage_x, bottom - graph_height, content_width, graph_height};
    }

    const auto *player = music.player();
    const bool has_track = player && player->duration() > 0;
    const auto position = has_track ? player->elapsed() : 0.0;
    const auto duration = has_track ? player->duration() : 0.0;
    const auto playback = has_track
        ? (player->playing() ? "▶ " : player->finished() ? "■ " : "Ⅱ ") + music.now_playing() : "";
    if (has_track)
    {
        const auto elapsed = timestamp(position) + " / " + timestamp(duration);
        const int time_x = width - 2 - static_cast<int>(elapsed.size());
        draw_label(stdscr, bottom + 1, 2, time_x - 3, playback, accent);
        draw_label(stdscr, bottom + 1, time_x, width - time_x - 1, elapsed, A_DIM);
        const int fill = std::max(0, std::min(content_width, static_cast<int>(content_width * position / duration)));
        m_writer.write_over(stdscr, bottom + 2, 2, std::wstring(content_width, L'╌'), A_DIM);
        if (fill > 0)
        {
            m_writer.write_over(stdscr, bottom + 2, 2, rule.substr(0, static_cast<size_t>(fill)), accent);
        }
    }

    const auto &message = stage && !status.empty() ? status :
                          !music.error().empty() ? music.error() : m_background_error;
    std::string controls;
    if (!stage)
    {
        controls = width >= 67 ? "Enter play   Space pause   / filter   v effect   n/N next/prev" : "Enter play   / filter   v effect";
    }
    else if (effect == "lava")
    {
        controls = width >= 46 ? "Mouse/arrows interact   v effect" : "Arrows interact   v effect";
    }
    else if (has_track)
    {
        controls = width >= 56 ? "v effect   T wallpaper   Space pause   n/N next/prev" : "v effect  Space pause";
    }
    else
    {
        controls = width >= 46 ? "v effect   T wallpaper   r reconnect" : "v effect  T theme  r audio";
    }
    if (!message.empty() && !has_track)
    {
        draw_label(stdscr, bottom + 1, 2, content_width, message, A_BOLD);
    }
    else if (!has_track && !stage)
    {
        draw_label(stdscr, bottom + 1, 2, content_width, music.visible_tracks().empty() ?
                   "Choose a music source" : "Select a track with ↑/↓", A_DIM);
    }
    draw_label(stdscr, rows - 2, 2, content_width,
               !message.empty() && has_track ? message : controls,
               !message.empty() && has_track ? A_BOLD : A_DIM);
    draw_label(stdscr, rows - 1, 2, content_width,
               width < 57 ? "i Spotify o Folder q Quit" :
               width < 76 ? "i Spotify   o Folder   Tab View   s Settings   q Quit" :
                            "i Spotify   o Folder   Tab View   s Settings   p Plugins   q Quit", accent);

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

void scmp::StudioView::draw_playlist(const MusicSession &music, int x, int width, int top, int height,
                                    bool compact_preview)
{
    const auto &visible = music.visible_tracks();
    const auto &tracks = music.tracks();
    const auto count = music.query().empty() ? std::to_string(tracks.size())
        : std::to_string(visible.size()) + "/" + std::to_string(tracks.size());
    const auto tally = count + (tracks.size() == 1 ? " track" : " tracks");
    draw_label(stdscr, top, x, width - static_cast<int>(tally.size()) - 1, "Playlist", A_BOLD);
    draw_label(stdscr, top, x + width - static_cast<int>(tally.size()),
               static_cast<int>(tally.size()), tally, A_DIM);
    const int digits = static_cast<int>(std::to_string(tracks.size()).size());
    draw_label(stdscr, top + 1, x, width,
              music.query().empty() ? std::string(digits + 1, ' ') + "#   Track" : "Filter  " + music.query(), A_DIM);
    if (visible.empty())
    {
        draw_label(stdscr, top + 2, x, width, tracks.empty() ?
            music.loaded() ? "No playable files in this folder." : "Press i for Spotify or o for a folder."
            : "No matches. Esc clears the filter.", A_NORMAL);
        return;
    }

    const int preview_height = compact_preview && height >= 12 ? std::min(5, height / 4) : 0;
    const int first_row = top + 2;
    const int list_rows = std::max(1, height - 2 - (preview_height ? preview_height + 1 : 0));
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

    for (size_t row = m_queue_start; row < visible.size() && row - m_queue_start < capacity; ++row)
    {
        const auto index = visible[row];
        const auto &track = tracks[index];
        const int y = first_row + static_cast<int>(row - m_queue_start);
        const bool selected = row == selection;
        const bool playing = music.active() == index;
        const int style = selected ? A_REVERSE : playing ? A_BOLD : A_NORMAL;
        if (selected)
        {
            m_writer.write_over(stdscr, y, x, std::wstring(width, L' '), A_REVERSE);
        }
        const auto number = std::to_string(index + 1);
        const auto &title = selected ? music.details().title : track.title;
        const auto prefix = (playing ? "▶ " : "  ") + std::string(digits - number.size(), ' ') + number + "   ";
        draw_label(stdscr, y, x, width, prefix + title, style);
    }

    if (preview_height)
    {
        const int preview_y = top + height - preview_height;
        m_writer.write_over(stdscr, preview_y - 1, x, std::wstring(width, L'─'), A_DIM);
        const int image_width = preview_height * 2;
        const auto &details = music.details();
        draw_cover(stdscr, preview_y, x, image_width, preview_height, details.cover);
        const int label_x = x + image_width + 2;
        const int label_width = width - image_width - 2;
        draw_label(stdscr, preview_y, label_x, label_width, details.title, A_BOLD);
        draw_label(stdscr, preview_y + 1, label_x, label_width, details.artist, A_DIM);
        draw_label(stdscr, preview_y + 2, label_x, label_width,
                  details.album.empty() ? tracks[music.selected()].folder : details.album, A_DIM);
        draw_label(stdscr, preview_y + 3, label_x, label_width,
                  std::to_string(selection + 1) + " / " + std::to_string(visible.size()), A_DIM);
    }
}

void scmp::StudioView::draw_track_preview(const MusicSession &music, int x, int width, int top, int height)
{
    const auto &details = music.details();
    draw_label(stdscr, top, x, width, "Selected track", A_BOLD);
    const int art_height = std::min({10, width / 2, height - 6});
    draw_cover(stdscr, top + 2, x + (width - art_height * 2) / 2, art_height * 2, art_height, details.cover);
    const int label = top + art_height + 3;
    draw_label(stdscr, label, x, width, details.title, A_BOLD);
    draw_label(stdscr, label + 1, x, width, details.artist, A_DIM);
    const auto &album = details.album.empty() ? music.tracks()[music.selected()].folder : details.album;
    if (album != details.title)
    {
        draw_label(stdscr, label + 2, x, width, album, A_DIM);
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
                    const double light = veil(x * 2, y);
                    const int fg = COLORS >= 256 ? rgb_color(ink & 0xffffff, light) : COLOR_CYAN;
                    const int bg = COLORS >= 256 ? rgb_color(cell.background, light) : -1;
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
            const double light = veil(x, y);
            const auto rgb = m_show_background
                ? sample_image(m_background, (x + 0.5) / cols, (y + 0.5) / rows,
                               static_cast<double>(cols) / (2 * rows))
                : ansi_rgb(scene_color(scene, x, y, cols, rows));
            const int color = rgb_color(rgb, light * (m_show_background ? .85 : 1.0));
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
                const int density = 1 + color % 3;
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
}
