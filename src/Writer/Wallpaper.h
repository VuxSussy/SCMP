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
