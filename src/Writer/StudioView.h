#ifndef SCMP_STUDIO_VIEW_H
#define SCMP_STUDIO_VIEW_H

#include "Player/MusicSession.h"
#include "Writer/NcursesWriter.h"
#include "Writer/Wallpaper.h"
#include <csignal>
#include <memory>

namespace scmp
{
class StudioView
{
  public:
    StudioView(const WorkspaceSettings &settings, NcursesWriter &writer);
    void cycle_theme();
    void choose_theme(const volatile std::sig_atomic_t &interrupted);
    WINDOW *begin_frame(const MusicSession &music, ViewMode view, const std::string &effect,
                        const char *input, const std::string &status);
    WINDOW *visualizer_surface();
    void draw_visualizer_frame(const std::string &mode, const char *signal, bool audible);
    void finish_frame();

  private:
    struct DeleteWindow
    {
        void operator()(WINDOW *window) const { delwin(window); }
    };
    void draw_playlist(const MusicSession &music, int x, int width, int top, int height,
                       bool compact_preview);
    void draw_track_preview(const MusicSession &music, int x, int width, int top, int height);
    void draw_background(const std::string &scene, int stage_x, int top, int bottom, bool stage_visible);
    void draw_label(WINDOW *surface, int row, int column, int width, const std::string &text,
                    int style = A_NORMAL);

    NcursesWriter &m_writer;
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
};
}

#endif
