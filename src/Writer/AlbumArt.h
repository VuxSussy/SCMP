#ifndef SCMP_ALBUM_ART_H
#define SCMP_ALBUM_ART_H

#include "Writer/PixelImage.h"
#include <array>
#include <string>

namespace scmp
{
class AlbumArt
{
  public:
    AlbumArt();
    ~AlbumArt();
    void begin_frame() { m_image = nullptr; }
    bool place(const PixelImage &image, const std::string &path, int row, int col, int width, int height);
    void draw();
    void clear();

  private:
    bool m_supported = false;
    const PixelImage *m_image = nullptr;
    std::array<int, 4> m_area{}, m_displayed_area{};
    std::string m_path, m_displayed_path;
};
}

#endif
