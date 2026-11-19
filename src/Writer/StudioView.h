#ifndef SCMP_STUDIO_VIEW_H
#define SCMP_STUDIO_VIEW_H

#include "Player/MusicSession.h"
#include "Writer/NcursesWriter.h"
#include "Writer/AlbumArt.h"
#include "Writer/Wallpaper.h"
#include <array>
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
    WINDOW *begin_frame(const MusicSession &music, ViewMode view, const std::string &effect,
                        const char *input, const std::string &status);
    WINDOW *visualizer_surface();
    void draw_visualizer_frame(const std::string &mode, const char *signal, bool audible);
    void finish_frame();
    void clear_cover()
    {
        m_album_art.clear();
        m_now_art.clear();
        m_bar_art.clear();
    }
    // Classic layout or the Spotify-inspired one, switched with L.
    void toggle_layout();
    void restore_layout(const std::string &layout);
    std::string layout_name() const { return m_layout == Layout::Spotify ? "spotify" : "classic"; }
    bool sidebar_focus() const { return m_sidebar_focus; }
    void set_sidebar_focus(bool focus);
    void sidebar_move(int offset);
    // The filter for the selected library item: empty for the whole playlist, else an artist.
    std::string sidebar_choice() const;
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
    void fill_rect(int x, int y, int width, int height, int background);
    WINDOW *begin_spotify_frame(const MusicSession &music, const std::string &effect, const std::string &status);
    void refresh_library(const MusicSession &music);
    void draw_spotify_top(const MusicSession &music);
    void draw_spotify_sidebar(const MusicSession &music, int x, int width, int top, int height);
    void draw_spotify_main(const MusicSession &music, int x, int width, int top, int height);
    void draw_spotify_now(const MusicSession &music, const TrackDetails &shown, int x, int width, int top,
                          int cover_height);
    void draw_spotify_bar(const MusicSession &music, const std::string &status);
    void draw_label(WINDOW *surface, int row, int column, int width, const std::string &text,
                    int style = A_NORMAL);

    NcursesWriter &m_writer;
    AlbumArt m_album_art{0};
    AlbumArt m_now_art{1};
    AlbumArt m_bar_art{2};
    enum class Layout
    {
        Classic,
        Spotify
    };
    Layout m_layout = Layout::Classic;
    bool m_sidebar_focus = false;
    size_t m_sidebar_index = 0;
    std::vector<std::pair<std::string, size_t>> m_artists;
    double m_library_length = 0;
    size_t m_library_tracks = static_cast<size_t>(-1);
    size_t m_library_tick = 0;
    size_t m_tick = 0;
    std::string m_header_key;
    uint32_t m_header_rgb = 0x3a2a6a;
    std::string m_theme;
    std::string m_background_error;
    PixelImage m_background;
    bool m_theme_motion;
    bool m_show_background = false;
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
