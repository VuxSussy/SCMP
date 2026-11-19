#ifndef SCMP_WALLPAPER_H
#define SCMP_WALLPAPER_H

#include <cstdint>
#include <string>
#include <vector>

namespace scmp
{
struct Theme
{
    const char *id;
    const char *label;
};

const std::vector<Theme> &themes();
const std::string &theme_choices();

// Renders a native pixel scene as width x height RGB pixels at the given time.
// Returns false for scenes that are not drawn natively.
bool render_scene(const std::string &scene, int width, int height, double seconds, std::vector<uint32_t> &pixels);
uint32_t ansi_rgb(int index);

#ifdef _OS_OSX
struct WallpaperCell
{
    uint32_t background;
    uint32_t foreground_and_glyph;
};

class Wallpaper
{
  public:
    Wallpaper() = default;
    ~Wallpaper();
    Wallpaper(const Wallpaper &) = delete;
    Wallpaper &operator=(const Wallpaper &) = delete;

    const std::vector<WallpaperCell> &frame(const std::string &scene, int cols, int rows, bool moving);

  private:
    void *m_context = nullptr;
    std::vector<WallpaperCell> m_cells;
    std::string m_scene;
    int m_cols = 0;
    int m_rows = 0;
    double m_started = 0;
    int64_t m_tick = -1;
};
#endif
}

#endif
