// Host side of layout plugins (plugin API v2), included by StudioView.cpp so it shares its helpers.
// scmp paints the wallpaper, hands the plugin a snapshot of the library and playback with drawing
// callbacks, then draws the visualizer where the plugin reserved room for it.

namespace
{
uint32_t average_cover_color(const scmp::PixelImage &image)
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

// The longest prefix of text that fits in max_width cells, and its width.
std::pair<std::string, int> clip_text(const std::string &text, int max_width)
{
    std::mbstate_t state{};
    int cells = 0;
    size_t offset = 0;
    while (offset < text.size())
    {
        wchar_t character = L' ';
        auto consumed = std::mbrtowc(&character, text.data() + offset, text.size() - offset, &state);
        if (consumed == 0 || consumed == static_cast<size_t>(-1) || consumed == static_cast<size_t>(-2))
        {
            state = {};
            consumed = 1;
        }
        const int width = std::max(0, wcwidth(character));
        if (cells + width > max_width) break;
        cells += width;
        offset += consumed;
    }
    return {text.substr(0, offset), cells};
}

int curses_attributes(int attrs)
{
    int style = A_NORMAL;
    if (attrs & SCMP_ATTR_BOLD) style |= A_BOLD;
    if (attrs & SCMP_ATTR_DIM) style |= A_DIM;
    if (attrs & SCMP_ATTR_REVERSE) style |= A_REVERSE;
    if (attrs & SCMP_ATTR_UNDERLINE) style |= A_UNDERLINE;
    return style;
}

int plugin_key_code(int key)
{
    switch (key)
    {
    case KEY_UP: return SCMP_KEY_UP;
    case KEY_DOWN: return SCMP_KEY_DOWN;
    case KEY_LEFT: return SCMP_KEY_LEFT;
    case KEY_RIGHT: return SCMP_KEY_RIGHT;
    case KEY_ENTER:
    case '\n':
    case '\r': return SCMP_KEY_ENTER;
    case 27: return SCMP_KEY_ESCAPE;
    case KEY_PPAGE: return SCMP_KEY_PAGE_UP;
    case KEY_NPAGE: return SCMP_KEY_PAGE_DOWN;
    case KEY_HOME: return SCMP_KEY_HOME;
    case KEY_END: return SCMP_KEY_END;
    default: return key >= 32 && key < 127 ? key : -1;
    }
}
}

bool scmp::StudioView::uses_plugin(const ScmpPluginV2 *layout) const
{
    return m_use_plugin && layout && layout->layout_draw && COLS >= std::max(30, layout->min_width) &&
           LINES >= std::max(12, layout->min_height);
}

std::vector<scmp::StudioView::LayoutCommand> scmp::StudioView::take_commands()
{
    std::vector<LayoutCommand> commands;
    commands.swap(m_commands);
    return commands;
}

ScmpLayout scmp::StudioView::snapshot(const MusicSession &music, const std::string &status, bool drawing)
{
    m_layout_music = &music;
    m_layout_drawing = drawing;
    m_layout_images = 0;
    m_layout_visualizer = {0, 0, 0, 0};

    const auto &tracks = music.tracks();
    m_layout_tags.resize(tracks.size());
    m_layout_tracks.resize(tracks.size());
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        m_layout_tags[i] = music.tags(i);
        const auto &tags = m_layout_tags[i];
        m_layout_tracks[i] = {tracks[i].path.c_str(), tags.title.c_str(), tags.artist.c_str(), tags.album.c_str(),
                              tags.duration, static_cast<uint32_t>(tracks[i].root)};
    }
    m_layout_visible.assign(music.visible_tracks().begin(), music.visible_tracks().end());
    m_layout_playlists.clear();
    for (const auto &playlist : music.playlists())
    {
        m_layout_playlists.push_back({playlist.name.c_str(), playlist.directory.c_str(),
                                      static_cast<uint32_t>(playlist.tracks)});
    }
    const char *user = std::getenv("USER");
    m_layout_user = user && *user ? user : "you";
    m_layout_search = music.query();
    m_layout_message = !music.error().empty() ? music.error() : !status.empty() ? status : m_background_error;

    const auto *player = music.player();
    const bool has_track = player && player->duration() > 0;
    ScmpLayout layout{};
    layout.host = this;
    layout.width = COLS;
    layout.height = LINES;
    layout.time_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    layout.frame = m_layout_frame++;
    layout.user = m_layout_user.c_str();
    layout.track_count = static_cast<uint32_t>(m_layout_tracks.size());
    layout.tracks = m_layout_tracks.data();
    layout.visible_count = static_cast<uint32_t>(m_layout_visible.size());
    layout.visible = m_layout_visible.data();
    layout.selected = music.selected() < tracks.size() && !m_layout_visible.empty()
        ? static_cast<uint32_t>(music.selected()) : SCMP_NONE;
    layout.playing_track = has_track && music.active() < tracks.size() ? static_cast<uint32_t>(music.active()) : SCMP_NONE;
    layout.playlist_count = static_cast<uint32_t>(m_layout_playlists.size());
    layout.playlists = m_layout_playlists.data();
    layout.playlist = static_cast<uint32_t>(music.playlist());
    layout.search = m_layout_search.c_str();
    layout.has_track = has_track;
    layout.playing = has_track && player->playing();
    layout.shuffle = music.shuffle();
    layout.repeat = static_cast<int>(music.repeat());
    layout.elapsed = has_track ? player->elapsed() : 0;
    layout.duration = has_track ? player->duration() : 0;
    layout.volume = music.volume();
    layout.message = m_layout_message.c_str();
    layout.text = &StudioView::layout_text;
    layout.text_width = &StudioView::layout_text_width;
    layout.fill = &StudioView::layout_fill;
    layout.image = &StudioView::layout_image;
    layout.image_color = &StudioView::layout_image_color;
    layout.color = &StudioView::layout_color;
    layout.visualizer = &StudioView::layout_visualizer;
    layout.command = &StudioView::layout_command;
    return layout;
}

int scmp::StudioView::layout_text(ScmpLayout *layout, int x, int y, int max_width, const char *utf8, int fg, int bg,
                                  int attrs)
{
    auto &view = *static_cast<StudioView *>(layout->host);
    if (!utf8 || max_width <= 0 || y < 0 || y >= LINES || x < 0 || x >= COLS) return 0;
    const auto clipped = clip_text(utf8, std::min(max_width, COLS - x));
    if (!view.m_layout_drawing || clipped.second == 0) return clipped.second;
    if (bg != SCMP_COLOR_KEEP) view.fill_bar(y, x, clipped.second, bg);
    const int color = fg >= 0 && fg < COLORS && fg < COLOR_PAIRS ? COLOR_PAIR(fg) : 0;
    view.draw_label(stdscr, y, x, clipped.second, clipped.first, color | curses_attributes(attrs));
    return clipped.second;
}

int scmp::StudioView::layout_text_width(ScmpLayout *, const char *utf8)
{
    return utf8 ? static_cast<int>(display_width(utf8)) : 0;
}

void scmp::StudioView::layout_fill(ScmpLayout *layout, int x, int y, int width, int height, int bg)
{
    auto &view = *static_cast<StudioView *>(layout->host);
    if (!view.m_layout_drawing) return;
    const int left = std::max(0, x), right = std::min(COLS, x + width);
    for (int row = std::max(0, y); row < std::min(LINES, y + height); ++row) view.fill_bar(row, left, right - left, bg);
}

void scmp::StudioView::layout_image(ScmpLayout *layout, int which, int x, int y, int width, int height)
{
    auto &view = *static_cast<StudioView *>(layout->host);
    if (!view.m_layout_drawing || width <= 0 || height <= 0) return;
    const auto &music = *view.m_layout_music;
    const auto *player = music.player();
    const bool playing = which == SCMP_IMAGE_PLAYING && player && player->duration() > 0 &&
                         music.active() < music.tracks().size();
    if (!playing && music.visible_tracks().empty()) return;
    const auto &details = playing ? music.playing_details() : music.details();
    const auto &path = music.tracks()[playing ? music.active() : music.selected()].path;
    AlbumArt *slots[] = {&view.m_album_art, &view.m_now_art, &view.m_bar_art};
    const int slot = view.m_layout_images++;
    if (slot >= 3 || !slots[slot]->place(details.cover, path, y, x, width, height))
    {
        draw_cover(stdscr, y, x, width, height, details.cover);
    }
}

uint32_t scmp::StudioView::layout_image_color(ScmpLayout *layout, int which)
{
    auto &view = *static_cast<StudioView *>(layout->host);
    const auto &music = *view.m_layout_music;
    const auto *player = music.player();
    const bool playing = which == SCMP_IMAGE_PLAYING && player && player->duration() > 0;
    return average_cover_color(playing ? music.playing_details().cover : music.details().cover);
}

int scmp::StudioView::layout_color(ScmpLayout *, uint32_t rgb)
{
    return rgb_color(rgb & 0xffffff, 1);
}

void scmp::StudioView::layout_visualizer(ScmpLayout *layout, int x, int y, int width, int height)
{
    auto &view = *static_cast<StudioView *>(layout->host);
    if (!view.m_layout_drawing) return;
    view.m_layout_visualizer = {x, y, width, height};
}

void scmp::StudioView::layout_command(ScmpLayout *layout, int command, double value, const char *text)
{
    auto &view = *static_cast<StudioView *>(layout->host);
    view.m_commands.push_back({command, value, text ? text : ""});
}

bool scmp::StudioView::plugin_key(const MusicSession &music, const ScmpPluginV2 *layout, int key)
{
    if (!uses_plugin(layout) || !layout->layout_key) return false;
    const int code = plugin_key_code(key);
    if (code < 0) return false;
    auto state = snapshot(music, "", false);
    return layout->layout_key(&state, code) != 0;
}

WINDOW *scmp::StudioView::begin_plugin_frame(const MusicSession &music, const ScmpPluginV2 *layout,
                                             const std::string &status)
{
    m_full_stage = false;
    m_dim = {0, 0, 0, 0};
    draw_background(effective_scene(m_theme), 2, 1, LINES - 1, false);
    auto state = snapshot(music, status, true);
    layout->layout_draw(&state);
    m_layout_drawing = false;

    const auto &area = m_layout_visualizer;
    const int x = std::max(0, area[0]), y = std::max(0, area[1]);
    const int width = std::min(COLS - x, area[2]), height = std::min(LINES - y, area[3]);
    if (width < 8 || height < 4)
    {
        m_visualizer.reset();
        m_stage.reset();
        return nullptr;
    }
    if (!m_stage || getmaxx(m_stage.get()) != width || getmaxy(m_stage.get()) != height ||
        getbegx(m_stage.get()) != x || getbegy(m_stage.get()) != y)
    {
        m_visualizer.reset();
        m_stage.reset(derwin(stdscr, height, width, y, x));
    }
    m_stage_visible = m_stage != nullptr;
    return m_stage.get();
}
