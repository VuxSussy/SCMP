// Spotify-inspired layout for the player view, included by StudioView.cpp so it shares its helpers.
// Dark panels on black, a library sidebar, a playlist header tinted by the cover, a two-line track
// table, a now-playing panel with the visualizer, and a player bar with transport controls.

namespace
{
constexpr int k_black = 16;
constexpr int k_panel = 233;
constexpr int k_hover = 236;
constexpr int k_pill = 235;
constexpr int k_text = 255;
constexpr int k_muted = 249;
constexpr int k_faint = 242;
constexpr int k_green = 35;
constexpr int k_green_text = 41;

int fg(int color) { return COLORS >= 256 ? COLOR_PAIR(color) : A_NORMAL; }

uint32_t average_color(const scmp::PixelImage &image)
{
    if (image.empty()) return 0x3a2a6a;
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (size_t i = 0; i < image.pixels.size(); i += 7)
    {
        const auto pixel = image.pixels[i];
        r += (pixel >> 16) & 0xff;
        g += (pixel >> 8) & 0xff;
        b += pixel & 0xff;
        ++n;
    }
    return static_cast<uint32_t>(((r / n) << 16) | ((g / n) << 8) | (b / n));
}

// Blend toward the panel color: t = 1 keeps the cover color, t = 0 is the panel.
uint32_t blend(uint32_t color, double t)
{
    const double panel = 0x12;
    const auto channel = [&](int shift) {
        const double value = (color >> shift) & 0xff;
        return static_cast<uint32_t>(panel + (value - panel) * t);
    };
    return (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

int tile_color(const std::string &name)
{
    static const int palette[] = {60, 61, 96, 131, 66, 95, 132, 67, 97, 130};
    uint32_t hash = 2166136261u;
    for (const auto c : name) hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
    return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
}

std::string first_letter(const std::string &name)
{
    if (name.empty()) return "?";
    std::mbstate_t state{};
    wchar_t character = L'?';
    const auto consumed = std::mbrtowc(&character, name.data(), name.size(), &state);
    if (consumed == 0 || consumed > name.size()) return name.substr(0, 1);
    return name.substr(0, consumed);
}

std::string total_length(double seconds)
{
    const auto minutes = static_cast<long>(seconds / 60);
    if (minutes >= 60) return "about " + std::to_string(minutes / 60) + " hr " + std::to_string(minutes % 60) + " min";
    return std::to_string(minutes) + " min " + std::to_string(static_cast<long>(seconds) % 60) + " sec";
}

std::vector<std::string> split_artists(const std::string &artists)
{
    std::vector<std::string> names;
    size_t start = 0;
    while (start <= artists.size())
    {
        auto end = artists.find_first_of("/,;&", start);
        if (end == std::string::npos) end = artists.size();
        auto name = artists.substr(start, end - start);
        const auto first = name.find_first_not_of(' ');
        const auto last = name.find_last_not_of(' ');
        if (first != std::string::npos) names.push_back(name.substr(first, last - first + 1));
        start = end + 1;
    }
    // Names like "W/N" use the separator themselves: keep the whole string when a piece is one letter.
    if (std::any_of(names.begin(), names.end(), [](const std::string &name) { return display_width(name) <= 1; }))
    {
        const auto first = artists.find_first_not_of(' ');
        return first == std::string::npos ? std::vector<std::string>{} : std::vector<std::string>{artists.substr(first)};
    }
    return names;
}
}

void scmp::StudioView::toggle_layout()
{
    m_layout = m_layout == Layout::Classic ? Layout::Spotify : Layout::Classic;
    m_sidebar_focus = false;
    clear_cover();
}

void scmp::StudioView::restore_layout(const std::string &layout)
{
    m_layout = layout == "spotify" ? Layout::Spotify : Layout::Classic;
}

void scmp::StudioView::set_sidebar_focus(bool focus)
{
    m_sidebar_focus = focus && m_layout == Layout::Spotify;
}

void scmp::StudioView::sidebar_move(int offset)
{
    const auto count = static_cast<int64_t>(m_artists.size()) + 1;
    m_sidebar_index = static_cast<size_t>(std::max<int64_t>(0, std::min<int64_t>(
        count - 1, static_cast<int64_t>(m_sidebar_index) + offset)));
}

std::string scmp::StudioView::sidebar_choice() const
{
    return m_sidebar_index == 0 || m_sidebar_index > m_artists.size() ? "" : m_artists[m_sidebar_index - 1].first;
}

void scmp::StudioView::refresh_library(const MusicSession &music)
{
    // Artist counts and the playlist length change as tags arrive; refresh them a few times a second.
    if (m_library_tick++ % 8 != 0 && m_library_tracks == music.tracks().size()) return;
    m_library_tracks = music.tracks().size();
    std::map<std::string, size_t> counts;
    m_library_length = 0;
    for (size_t i = 0; i < music.tracks().size(); ++i)
    {
        const auto tags = music.tags(i);
        m_library_length += tags.duration;
        for (const auto &name : split_artists(tags.artist)) ++counts[name];
    }
    m_artists.assign(counts.begin(), counts.end());
    std::stable_sort(m_artists.begin(), m_artists.end(), [](const std::pair<std::string, size_t> &a,
                                                            const std::pair<std::string, size_t> &b) {
        return a.second > b.second;
    });
    m_sidebar_index = std::min(m_sidebar_index, m_artists.size());
}

void scmp::StudioView::fill_rect(int x, int y, int width, int height, int background)
{
    for (int row = y; row < y + height; ++row) fill_bar(row, x, width, background);
}

WINDOW *scmp::StudioView::begin_spotify_frame(const MusicSession &music, const std::string &effect,
                                              const std::string &status)
{
    static_cast<void>(effect);
    const int rows = LINES;
    const int width = COLS;
    ++m_tick;
    refresh_library(music);

    // Geometry: top bar, three panels, a gap row, then a three-row player bar.
    const int top = 1;
    const int panel_height = rows - 5;
    const bool sidebar = width >= 90;
    const bool now_panel = width >= 120;
    const int sidebar_width = sidebar ? std::min(32, std::max(24, width / 5)) : 0;
    const int now_width = now_panel ? std::min(44, std::max(32, width / 4)) : 0;
    const int main_x = 1 + (sidebar ? sidebar_width + 1 : 0);
    const int main_width = width - main_x - 1 - (now_panel ? now_width + 1 : 0);
    const int now_x = main_x + main_width + 1;

    // Now-playing panel: cover, title, artist, then the visualizer below.
    const auto *player = music.player();
    const bool has_track = player && player->duration() > 0;
    const auto &shown = has_track ? music.playing_details() : music.details();
    const int now_cover_height = now_panel ? std::min((now_width - 4) / 2, std::max(4, panel_height / 3)) : 0;
    const int now_info_rows = 2 + now_cover_height + 4;
    GraphArea canvas{0, 0, 0, 0};
    if (now_panel && panel_height - now_info_rows >= 7)
    {
        canvas = {now_x + 1, top + now_info_rows, now_width - 2, panel_height - now_info_rows - 1};
    }

    // Wallpaper only shows through the visualizer; everything else is solid like Spotify's panels.
    m_dim = {0, 0, 0, 0};
    draw_background(effective_scene(m_theme), 2, top, top + panel_height, false);
    for (int row = 0; row < rows; ++row)
    {
        if (canvas.height > 0 && row >= canvas.y && row < canvas.y + canvas.height)
        {
            fill_bar(row, 0, canvas.x, k_black);
            fill_bar(row, canvas.x + canvas.width, width - canvas.x - canvas.width, k_black);
        }
        else
        {
            fill_bar(row, 0, width, k_black);
        }
    }
    const auto panel = [&](int x, int w) {
        for (int row = top; row < top + panel_height; ++row)
        {
            if (canvas.height > 0 && x == now_x && row >= canvas.y && row < canvas.y + canvas.height)
            {
                fill_bar(row, x, 1, k_panel);
                fill_bar(row, canvas.x + canvas.width, x + w - canvas.x - canvas.width, k_panel);
                continue;
            }
            fill_bar(row, x, w, k_panel);
        }
    };
    if (sidebar) panel(1, sidebar_width);
    panel(main_x, main_width);
    if (now_panel) panel(now_x, now_width);

    draw_spotify_top(music);
    if (sidebar) draw_spotify_sidebar(music, 1, sidebar_width, top, panel_height);
    draw_spotify_main(music, main_x, main_width, top, panel_height);
    if (now_panel) draw_spotify_now(music, shown, now_x, now_width, top, now_cover_height);
    draw_spotify_bar(music, status);

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

void scmp::StudioView::draw_spotify_top(const MusicSession &music)
{
    const int width = COLS;
    draw_label(stdscr, 0, 2, 2, "♫", fg(k_green_text) | A_BOLD);
    draw_label(stdscr, 0, 4, 5, "scmp", fg(k_text) | A_BOLD);

    // Search pill centered between the app name and the tabs, showing the filter.
    const int tabs_x = width - 2 - 23;
    const int room = tabs_x - 2 - 12;
    const int pill_width = std::min({48, width / 3, room});
    const int pill_x = 12 + (room - pill_width) / 2;
    if (pill_width >= 16)
    {
        fill_bar(0, pill_x, pill_width, k_pill);
        const auto &query = music.query();
        draw_label(stdscr, 0, pill_x + 2, 2, "⌕", fg(k_muted));
        draw_label(stdscr, 0, pill_x + 4, pill_width - 6, query.empty() ? "What do you want to play?   /" : query,
                   fg(query.empty() ? k_faint : k_text) | (query.empty() ? 0 : A_BOLD));
    }
    const std::string tabs[] = {"1 Visualizer", "2 Player"};
    int x = tabs_x;
    draw_label(stdscr, 0, x, 12, tabs[0], fg(k_muted));
    x += 15;
    draw_label(stdscr, 0, x, 8, tabs[1], fg(k_text) | A_BOLD);
}

void scmp::StudioView::draw_spotify_sidebar(const MusicSession &music, int x, int width, int top, int height)
{
    draw_label(stdscr, top + 1, x + 2, width - 4, "▤  Your Library", fg(m_sidebar_focus ? k_text : k_muted) | A_BOLD);
    const bool chips = width >= 24;
    if (chips)
    {
        fill_bar(top + 3, x + 2, 11, k_pill);
        draw_label(stdscr, top + 3, x + 3, 9, "Playlists", fg(k_text));
        fill_bar(top + 3, x + 14, 9, k_pill);
        draw_label(stdscr, top + 3, x + 15, 7, "Artists", fg(k_text));
    }
    const int list_top = top + (chips ? 5 : 3);
    const int capacity = std::max(1, (height - (list_top - top)) / 3);
    const auto active = music.query();
    const size_t count = m_artists.size() + 1;
    size_t first = m_sidebar_index >= static_cast<size_t>(capacity) ? m_sidebar_index - capacity + 1 : 0;
    for (size_t i = first; i < count && static_cast<int>(i - first) < capacity; ++i)
    {
        const int y = list_top + static_cast<int>(i - first) * 3;
        const bool playlist = i == 0;
        const auto name = playlist ? music.directory().substr(music.directory().find_last_of('/') + 1)
                                   : m_artists[i - 1].first;
        const bool current = playlist ? active.empty() : active == name;
        if (m_sidebar_focus && i == m_sidebar_index)
        {
            fill_bar(y, x, width, k_hover);
            fill_bar(y + 1, x, width, k_hover);
        }
        // Square tiles for playlists, a softer two-tone tile for artists.
        const int tile = playlist ? rgb_color(m_header_rgb, 1) : tile_color(name);
        fill_bar(y, x + 2, 4, tile);
        fill_bar(y + 1, x + 2, 4, tile);
        draw_label(stdscr, y, x + 3, 2, playlist ? "♫" : first_letter(name), fg(k_text) | A_BOLD);
        auto subtitle = playlist ? "Playlist · " + std::to_string(music.tracks().size()) + " songs"
                                 : "Artist · " + std::to_string(m_artists[i - 1].second) + " songs";
        // Drop the unit rather than cut a word when the sidebar is narrow.
        if (static_cast<int>(display_width(subtitle)) > width - 10) subtitle.resize(subtitle.size() - 6);
        draw_label(stdscr, y, x + 8, width - 10, name.empty() ? "Library" : name,
                   fg(current ? k_green_text : k_text) | A_BOLD);
        draw_label(stdscr, y + 1, x + 8, width - 10, subtitle, fg(k_muted));
    }
}

void scmp::StudioView::draw_spotify_main(const MusicSession &music, int x, int width, int top, int height)
{
    const auto *player = music.player();
    const bool has_track = player && player->duration() > 0;
    const auto &cover_source = has_track ? music.playing_details() : music.details();
    const auto key = has_track ? music.now_playing() : music.details().title;
    if (key != m_header_key)
    {
        m_header_key = key;
        m_header_rgb = average_color(cover_source.cover);
    }

    // Header tinted by the cover color, fading into the panel.
    const int header = std::min(10, std::max(6, height / 3));
    for (int row = 0; row < header + 2; ++row)
    {
        const double t = 1.0 - static_cast<double>(row) / (header + 2);
        fill_bar(top + row, x, width, rgb_color(blend(m_header_rgb, 0.25 + 0.75 * t), 1));
    }
    if (!music.loaded())
    {
        draw_label(stdscr, top + 2, x + 3, width - 6, "Nothing here yet", fg(k_text) | A_BOLD);
        draw_label(stdscr, top + 3, x + 3, width - 6, "Press o to open a music folder or i to import a playlist.",
                   fg(k_text));
        return;
    }
    const int art_height = header - 2;
    const int art_width = art_height * 2;
    const auto &path = music.visible_tracks().empty() ? std::string() : music.tracks()[music.selected()].path;
    if (!m_album_art.place(cover_source.cover, (has_track ? "now:" : "sel:") + path, top + 1, x + 3, art_width,
                           art_height))
    {
        draw_cover(stdscr, top + 1, x + 3, art_width, art_height, cover_source.cover);
    }
    const int text_x = x + art_width + 6;
    const int text_width = width - art_width - 8;
    const auto folder = music.directory().substr(music.directory().find_last_of('/') + 1);
    const auto query = music.query();
    draw_label(stdscr, top + header - 5, text_x, text_width, query.empty() ? "Playlist" : "Filtered", fg(k_text));
    // Spaced capitals stand in for Spotify's large title type.
    std::string title;
    for (const auto c : (query.empty() ? folder : query))
    {
        title += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        if (static_cast<unsigned char>(c) < 0x80) title += ' ';
    }
    draw_label(stdscr, top + header - 4, text_x, text_width, title, fg(k_text) | A_BOLD);
    const char *user = std::getenv("USER");
    const auto count = music.visible_tracks().size();
    const auto summary = std::string(user && *user ? user : "you") + " · " + std::to_string(count) +
                         (count == 1 ? " song" : " songs") +
                         (query.empty() && m_library_length > 0 ? ", " + total_length(m_library_length) : "");
    draw_label(stdscr, top + header - 2, text_x, text_width, summary, fg(k_text));

    // Action row: big play button, shuffle, repeat.
    const int actions = top + header + 1;
    const bool playing = has_track && player->playing();
    fill_bar(actions, x + 3, 5, k_green);
    draw_label(stdscr, actions, x + 5, 2, playing ? "Ⅱ" : "▶", fg(k_black) | A_BOLD);
    draw_label(stdscr, actions, x + 11, 2, "⇄", fg(music.shuffle() ? k_green_text : k_muted) | A_BOLD);
    draw_label(stdscr, actions, x + 15, 3, music.repeat() == RepeatMode::One ? "↻1" : "↻",
               fg(music.repeat() != RepeatMode::Off ? k_green_text : k_muted) | A_BOLD);
    draw_label(stdscr, actions, x + width - 14, 12, "Enter plays", fg(k_faint));

    // Track table.
    const int table = actions + 2;
    const int digits = static_cast<int>(std::to_string(music.tracks().size()).size());
    const int number_x = x + 2;
    const int title_x = number_x + digits + 3;
    const int time_width = 6;
    const int album_width = width >= 70 ? std::min(30, width * 3 / 10) : 0;
    const int album_x = x + width - time_width - album_width - 2;
    const int title_width = (album_width ? album_x - 2 : x + width - time_width - 2) - title_x;
    draw_label(stdscr, table, number_x, digits + 1, std::string(static_cast<size_t>(digits - 1), ' ') + "#", fg(k_muted));
    draw_label(stdscr, table, title_x, title_width, "Title", fg(k_muted));
    if (album_width) draw_label(stdscr, table, album_x, album_width, "Album", fg(k_muted));
    draw_label(stdscr, table, x + width - 6, 4, "◷", fg(k_muted));
    m_writer.write_over(stdscr, table + 1, x + 2, std::wstring(static_cast<size_t>(width - 4), L'─'), fg(239));

    const auto &visible = music.visible_tracks();
    const int list_top = table + 2;
    const int list_rows = top + height - list_top - 1;
    if (visible.empty())
    {
        draw_label(stdscr, list_top, title_x, title_width, "No songs match. Esc clears the search.", fg(k_muted));
        return;
    }
    const bool stacked = list_rows / 2 >= 5;
    const int row_height = stacked ? 2 : 1;
    const size_t capacity = static_cast<size_t>(std::max(1, list_rows / row_height));
    const auto selection = music.selected_row();
    m_queue_start = std::min(m_queue_start, visible.size() > capacity ? visible.size() - capacity : 0);
    if (selection < m_queue_start) m_queue_start = selection;
    else if (selection - m_queue_start >= capacity) m_queue_start = selection - capacity + 1;
    m_list_position = {selection + 1, visible.size(), m_queue_start, capacity};

    // Equalizer frames for the playing track's number column.
    static const char *const bars[] = {"▂▅▃", "▅▃▇", "▇▂▅", "▃▇▂", "▆▄▂", "▂▆▇"};
    for (size_t row = m_queue_start; row < visible.size() && row - m_queue_start < capacity; ++row)
    {
        const auto index = visible[row];
        const int y = list_top + static_cast<int>(row - m_queue_start) * row_height;
        const bool selected = row == selection && !m_sidebar_focus;
        const bool current = music.active() == index;
        if (selected)
        {
            fill_bar(y, x + 1, width - 2, k_hover);
            if (stacked) fill_bar(y + 1, x + 1, width - 2, k_hover);
        }
        const auto tags = music.tags(index);
        const auto number = std::to_string(index + 1);
        if (current && playing)
            draw_label(stdscr, y, number_x, digits + 1, bars[(m_tick / 3) % 6], fg(k_green_text));
        else
            draw_label(stdscr, y, number_x + digits - static_cast<int>(number.size()), digits, number,
                       fg(current ? k_green_text : k_muted));
        draw_label(stdscr, y, title_x, title_width, tags.title, fg(current ? k_green_text : k_text) | (stacked ? 0 : A_BOLD));
        if (stacked) draw_label(stdscr, y + 1, title_x, title_width, tags.artist, fg(k_muted));
        if (album_width)
        {
            const auto album = tags.album.empty() ? music.tracks()[index].folder : tags.album;
            draw_label(stdscr, y, album_x, album_width, album, fg(k_muted));
        }
        if (tags.duration > 0)
        {
            const auto length = timestamp(tags.duration);
            draw_label(stdscr, y, x + width - 2 - static_cast<int>(length.size()), static_cast<int>(length.size()), length,
                       fg(k_muted));
        }
    }
}

void scmp::StudioView::draw_spotify_now(const MusicSession &music, const TrackDetails &shown, int x, int width,
                                         int top, int cover_height)
{
    const auto *player = music.player();
    const bool has_track = player && player->duration() > 0;
    const auto folder = music.directory().substr(music.directory().find_last_of('/') + 1);
    draw_label(stdscr, top + 1, x + 2, width - 4, has_track ? folder : "Selected", fg(k_text) | A_BOLD);
    const int cover_width = cover_height * 2;
    const int cover_x = x + (width - cover_width) / 2;
    const auto path = has_track ? music.tracks()[music.active()].path
                                : music.visible_tracks().empty() ? "" : music.tracks()[music.selected()].path;
    if (!path.empty() && !m_now_art.place(shown.cover, path, top + 3, cover_x, cover_width, cover_height))
        draw_cover(stdscr, top + 3, cover_x, cover_width, cover_height, shown.cover);
    const int text = top + 3 + cover_height + 1;
    draw_label(stdscr, text, x + 2, width - 4, shown.title, fg(k_text) | A_BOLD);
    draw_label(stdscr, text + 1, x + 2, width - 4, shown.artist, fg(k_muted));
}

void scmp::StudioView::draw_spotify_bar(const MusicSession &music, const std::string &status)
{
    const int rows = LINES;
    const int width = COLS;
    const int y = rows - 3;
    const auto *player = music.player();
    const bool has_track = player && player->duration() > 0;
    const auto position = has_track ? player->elapsed() : 0.0;
    const auto duration = has_track ? player->duration() : 0.0;
    const bool playing = has_track && player->playing();

    // Left: small cover, title and artist of the playing track.
    const int left_width = std::min(40, width * 3 / 10);
    if (has_track)
    {
        const auto &details = music.playing_details();
        if (!m_bar_art.place(details.cover, music.tracks()[music.active()].path, y, 2, 6, 3))
            draw_cover(stdscr, y, 2, 6, 3, details.cover);
        draw_label(stdscr, y, 10, left_width - 10, music.now_playing(), fg(k_text) | A_BOLD);
        draw_label(stdscr, y + 1, 10, left_width - 10, details.artist, fg(k_muted));
    }

    // Center: transport controls and the progress bar.
    const int center_width = std::min(70, width - 2 * left_width);
    const int center_x = (width - center_width) / 2;
    const int controls_x = width / 2 - 13;
    draw_label(stdscr, y, controls_x, 2, "⇄", fg(music.shuffle() ? k_green_text : k_muted) | A_BOLD);
    draw_label(stdscr, y, controls_x + 6, 2, "|◀", fg(k_muted) | A_BOLD);
    fill_bar(y, controls_x + 11, 4, k_text);
    draw_label(stdscr, y, controls_x + 12, 2, playing ? "Ⅱ" : "▶", fg(k_black) | A_BOLD);
    draw_label(stdscr, y, controls_x + 19, 2, "▶|", fg(k_muted) | A_BOLD);
    draw_label(stdscr, y, controls_x + 24, 3, music.repeat() == RepeatMode::One ? "↻1" : "↻",
               fg(music.repeat() != RepeatMode::Off ? k_green_text : k_muted) | A_BOLD);
    if (center_width >= 24)
    {
        const auto elapsed = timestamp(position);
        const auto total = has_track ? timestamp(duration) : "-:--";
        const int bar_x = center_x + static_cast<int>(elapsed.size()) + 2;
        const int bar_width = center_width - static_cast<int>(elapsed.size() + total.size()) - 4;
        draw_label(stdscr, y + 1, center_x, static_cast<int>(elapsed.size()), elapsed, fg(k_muted));
        draw_label(stdscr, y + 1, bar_x + bar_width + 2, static_cast<int>(total.size()), total, fg(k_muted));
        m_writer.write_over(stdscr, y + 1, bar_x, std::wstring(static_cast<size_t>(bar_width), L'━'), fg(239));
        const int fill = has_track ? std::max(0, std::min(bar_width, static_cast<int>(bar_width * position / duration))) : 0;
        if (fill > 0) m_writer.write_over(stdscr, y + 1, bar_x, std::wstring(static_cast<size_t>(fill), L'━'), fg(k_text));
        if (has_track) m_writer.write_over(stdscr, y + 1, bar_x + std::min(fill, bar_width - 1), L"●", fg(k_text));
    }

    // Right: volume.
    if (width >= 90)
    {
        const int volume_width = 12;
        const int vx = width - volume_width - 8;
        draw_label(stdscr, y + 1, vx - 4, 3, "vol", fg(k_muted));
        const int level = static_cast<int>(std::lround(music.volume() * volume_width));
        m_writer.write_over(stdscr, y + 1, vx, std::wstring(static_cast<size_t>(volume_width), L'━'), fg(239));
        if (level > 0) m_writer.write_over(stdscr, y + 1, vx, std::wstring(static_cast<size_t>(level), L'━'), fg(k_text));
        draw_label(stdscr, y + 1, vx + volume_width + 1, 5,
                   std::to_string(static_cast<int>(std::lround(music.volume() * 100))) + "%", fg(k_muted));
    }

    // Bottom row: messages, or quiet hints.
    const auto &message = !music.error().empty() ? music.error() : !status.empty() ? status : m_background_error;
    if (!message.empty())
    {
        draw_label(stdscr, rows - 1, left_width + 2, width - left_width - 4, message,
                   (music.error().empty() ? yellow() : red()) | A_BOLD);
        return;
    }
    // Hints sit under the progress bar, clear of the cover on the left.
    const int hints_x = std::max(left_width + 2, center_x);
    draw_hints(rows - 1, hints_x, width - hints_x - 2,
               {{"?", "help"}, {"L", "classic"}, {"h/l", "library/songs"}, {"/", "search"}, {"←/→", "seek"},
                {"+/-", "volume"}, {"q", "quit"}}, fg(k_muted) | A_BOLD);
}
