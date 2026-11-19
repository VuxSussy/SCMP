#include "Writer/AlbumArt.h"
#include <gtest/gtest.h>
#include <cstdlib>

namespace
{
scmp::PixelImage gradient(int width, int height)
{
    scmp::PixelImage image;
    image.width = width;
    image.height = height;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.pixels.push_back(0xff000000u | (static_cast<uint32_t>(x * 255 / width) << 16) |
                                   (static_cast<uint32_t>(y * 255 / height) << 8) | 0x80u);
    return image;
}

uint32_t big_endian(const std::string &data, size_t offset)
{
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) value = (value << 8) | static_cast<uint8_t>(data[offset + i]);
    return value;
}
}

TEST(AlbumArtTest, EncodesPngHeaderAndDimensions)
{
    const auto png = scmp::encode_png(gradient(300, 200));
    ASSERT_GT(png.size(), 33u);
    EXPECT_EQ(png.substr(0, 8), std::string("\x89PNG\r\n\x1a\n", 8));
    EXPECT_EQ(png.substr(12, 4), "IHDR");
    EXPECT_EQ(big_endian(png, 16), 300u);
    EXPECT_EQ(big_endian(png, 20), 200u);
    EXPECT_EQ(png.substr(png.size() - 8, 4), "IEND");
    // RGB rows plus filter bytes are stored without compression.
    EXPECT_GT(png.size(), 300u * 200u * 3u);
}

TEST(AlbumArtTest, EncodesSixelFrame)
{
    const auto sixel = scmp::encode_sixel(gradient(40, 40), 24, 18);
    ASSERT_FALSE(sixel.empty());
    EXPECT_EQ(sixel.substr(0, 9), "\033P0;1;0q\"");
    EXPECT_NE(sixel.find("\"1;1;24;18"), std::string::npos);
    EXPECT_EQ(sixel.substr(sixel.size() - 2), "\033\\");
    // 18 rows make three bands of six.
    size_t bands = 0;
    for (const auto c : sixel) bands += c == '-';
    EXPECT_EQ(bands, 3u);
    EXPECT_TRUE(scmp::encode_sixel(scmp::PixelImage{}, 10, 10).empty());
}

TEST(AlbumArtTest, HonorsForcedProtocol)
{
    setenv("SCMP_IMAGE_PROTOCOL", "sixel", 1);
    EXPECT_EQ(scmp::detect_image_protocol(), scmp::ImageProtocol::Sixel);
    setenv("SCMP_IMAGE_PROTOCOL", "iterm", 1);
    EXPECT_EQ(scmp::detect_image_protocol(), scmp::ImageProtocol::ITerm);
    setenv("SCMP_IMAGE_PROTOCOL", "none", 1);
    EXPECT_EQ(scmp::detect_image_protocol(), scmp::ImageProtocol::None);
    unsetenv("SCMP_IMAGE_PROTOCOL");
}
