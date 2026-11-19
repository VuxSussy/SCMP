#ifndef SCMP_STUDIO_VIEW_H
#define SCMP_STUDIO_VIEW_H

#include "Player/MusicSession.h"
#include "Writer/NcursesWriter.h"
#include "Writer/AlbumArt.h"
#include "Writer/Wallpaper.h"
#include <scmp/plugin.h>
#include <array>
#include <chrono>
#include <csignal>
#include <memory>
#include <utility>
#include <vector>

namespace scmp
{
class StudioView
{
  public:
    StudioView(const WorkspaceSettings &settings, NcursesWriter &writer);
    void cycle_theme();
    bool choose_theme(const volatile std::sig_atomic_t &interrupted);
    // layout is the enabled layout plugin, or nullptr. It draws the Player when the plugin layout is on.
    WINDOW *begin_frame(const MusicSession &music, ViewMode view, const std::string &effect,
                        const char *input, const std::string &status, const ScmpPluginV2 *layout = nullptr);
    WINDOW *visualizer_surface();
    void draw_visualizer_frame(const std::string &mode, const char *signal, bool audible);
    void finish_frame();
    void clear_cover()
    {
        m_album_art.clear();
        m_now_art.clear();
        m_bar_art.clear();
    }
    // Classic layout or the layout plugin, switched with L.
    void toggle_layout() { m_use_plugin = !m_use_plugin; clear_cover(); }
    void restore_layout(const std::string &layout) { m_use_plugin = layout != "classic"; }
    std::string layout_name() const { return m_use_plugin ? "plugin" : "classic"; }
    bool uses_plugin(const ScmpPluginV2 *layout) const;
    // Offers a key to the layout plugin. Returns true when the plugin handled it.
    bool plugin_key(const MusicSession &music, const ScmpPluginV2 *layout, int key);
    struct LayoutCommand
    {
        int command;
        double value;
        std::string text;
    };
    // Commands the layout plugin sent since the last call.
    std::vector<LayoutCommand> take_commands();
    bool show_help(const volatile std::sig_atomic_t &interrupted);
    std::string theme_choice() const { return m_show_background ? "custom" : m_theme; }
    void restore_theme(const std::string &theme);

  private:
    struct DeleteWindow
    {
        void operator()(WINDOW *window) const { delwin(window); }
    };
    void draw_playlist(const MusicSession &music, int x, int width, int top, int height);
    void draw_track_preview(const MusicSession &music, int x, int width, int top, int height);
    void draw_background(const std::string &scene, int stage_x, int top, int bottom, bool stage_visible);
    struct Segment
    {
        // Accepts both int and chtype attributes without narrowing errors.
        Segment(std::string value, long long attributes) : text(std::move(value)), style(static_cast<int>(attributes)) {}
        std::string text;
        int style;
    };
    int draw_segments(int row, int x, int width, const std::vector<Segment> &segments);
    void draw_hints(int row, int x, int width, const std::vector<std::pair<std::string, std::string>> &hints,
                    int key_style);
    void fill_bar(int row, int x, int width, int background);
    WINDOW *begin_plugin_frame(const MusicSession &music, const ScmpPluginV2 *layout, const std::string &status);
    ScmpLayout snapshot(const MusicSession &music, const std::string &status, bool drawing);
    static int layout_text(ScmpLayout *layout, int x, int y, int max_width, const char *utf8, int fg, int bg,
                           int attrs);
    static int layout_text_width(ScmpLayout *layout, const char *utf8);
    static void layout_fill(ScmpLayout *layout, int x, int y, int width, int height, int bg);
    static void layout_image(ScmpLayout *layout, int which, int x, int y, int width, int height);
    static uint32_t layout_image_color(ScmpLayout *layout, int which);
    static int layout_color(ScmpLayout *layout, uint32_t rgb);
    static void layout_visualizer(ScmpLayout *layout, int x, int y, int width, int height);
    static void layout_command(ScmpLayout *layout, int command, double value, const char *text);
    void draw_label(WINDOW *surface, int row, int column, int width, const std::string &text,
                    int style = A_NORMAL);

    NcursesWriter &m_writer;
    AlbumArt m_album_art{0};
    AlbumArt m_now_art{1};
    AlbumArt m_bar_art{2};
    // Layout plugin state. The snapshot vectors own the strings the plugin reads during a call.
    bool m_use_plugin = true;
    bool m_layout_drawing = false;
    const MusicSession *m_layout_music = nullptr;
    std::vector<TrackTags> m_layout_tags;
    std::vector<ScmpTrack> m_layout_tracks;
    std::vector<uint32_t> m_layout_visible;
    std::vector<ScmpPlaylist> m_layout_playlists;
    std::string m_layout_user, m_layout_search, m_layout_message;
    std::array<int, 4> m_layout_visualizer{};
    int m_layout_images = 0;
    uint64_t m_layout_frame = 0;
    std::vector<LayoutCommand> m_commands;
    std::string m_theme;
    std::string m_background_error;
    PixelImage m_background;
    bool m_theme_motion;
    bool m_show_background = false;
    std::vector<uint32_t> m_pixels;
    std::chrono::steady_clock::time_point m_scene_started = std::chrono::steady_clock::now();
    std::unique_ptr<WINDOW, DeleteWindow> m_stage;
    std::unique_ptr<WINDOW, DeleteWindow> m_visualizer;
#ifdef _OS_OSX
    Wallpaper m_wallpaper;
#endif
    size_t m_queue_start = 0;
    bool m_stage_visible = false;
    bool m_full_stage = false;
    // Wallpaper area dimmed behind the playlist: x, y, width, height.
    std::array<int, 4> m_dim{};
    // Selected row (1-based), visible tracks, first shown row and rows shown, for the position counter.
    std::array<size_t, 4> m_list_position{};
};
}

#endif
