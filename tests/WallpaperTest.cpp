#include <gtest/gtest.h>
#include "Writer/Wallpaper.h"

#ifdef _OS_OSX
TEST(WallpaperTest, RendersEveryImportedSceneWithAStillFrame)
{
    const char *scenes[] = {"moonlit", "cloud-drift", "honu-lagoon", "koi-pond",
                           "jellyfish", "lantern-night", "fireflies", "neon-rain", "sakura-dusk",
                           "snow-cabin", "star-voyage"};
    scmp::Wallpaper wallpaper;
    for (const auto *scene : scenes)
    {
        SCOPED_TRACE(scene);
        const auto &frame = wallpaper.frame(scene, 38, 24, false);
        EXPECT_EQ(frame.size(), 38u * 24u);
        if (!frame.empty())
        {
            const auto *cached = frame.data();
            EXPECT_NE(frame.front().background, 0u);
            EXPECT_EQ(wallpaper.frame(scene, 38, 24, false).data(), cached);
        }
    }
}
#endif

TEST(WallpaperTest, DrawsAnimatedPixelScenes)
{
    const char *scenes[] = {"cat-window", "torii", "sunset", "day-drive", "lighthouse", "rainy-city"};
    for (const auto *scene : scenes)
    {
        SCOPED_TRACE(scene);
        std::vector<uint32_t> still, later;
        ASSERT_TRUE(scmp::render_scene(scene, 120, 64, 0.0, still));
        ASSERT_TRUE(scmp::render_scene(scene, 120, 64, 2.5, later));
        EXPECT_EQ(still.size(), 120u * 64u);
        EXPECT_NE(still, later);
        // Tiny terminals must not crash.
        std::vector<uint32_t> tiny;
        EXPECT_TRUE(scmp::render_scene(scene, 3, 2, 1.0, tiny));
    }
}

TEST(WallpaperTest, KeepsStaticScenesAndRejectsUnknownOnes)
{
    std::vector<uint32_t> pixels;
    EXPECT_TRUE(scmp::render_scene("night-drive", 40, 20, 0.0, pixels));
    EXPECT_FALSE(scmp::render_scene("moonlit", 40, 20, 0.0, pixels));
    EXPECT_FALSE(scmp::render_scene("clawd", 40, 20, 0.0, pixels));
}
