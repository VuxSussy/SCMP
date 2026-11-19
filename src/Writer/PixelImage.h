#ifndef SCMP_PIXEL_IMAGE_H
#define SCMP_PIXEL_IMAGE_H

#include <cstdint>
#include <string>
#include <vector>

namespace scmp
{
struct PixelImage
{
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
    bool empty() const { return pixels.empty(); }
};

PixelImage load_pixel_image(const std::string &path);
}

#endif
