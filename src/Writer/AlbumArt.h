#ifndef SCMP_ALBUM_ART_H
#define SCMP_ALBUM_ART_H

#include "Writer/PixelImage.h"
#include <array>
#include <string>

namespace scmp
{
enum class ImageProtocol
{
    None,
    Kitty,
    ITerm,
    Sixel
};

// Picks an inline image protocol from SCMP_IMAGE_PROTOCOL, TERM and TERM_PROGRAM.
ImageProtocol detect_image_protocol();
// Minimal PNG (stored deflate) used for the iTerm2 protocol. Exposed for tests.
std::string encode_png(const PixelImage &image);
// DEC sixel sequence for the image scaled to width x height pixels. Exposed for tests.
std::string encode_sixel(const PixelImage &image, int width, int height);

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
    ImageProtocol m_protocol = ImageProtocol::None;
    const PixelImage *m_image = nullptr;
    std::array<int, 4> m_area{}, m_displayed_area{};
    std::string m_path, m_displayed_path;
};
}

#endif
