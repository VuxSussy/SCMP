#include <gtest/gtest.h>
#include "Writer/Wallpaper.h"

#ifdef _OS_OSX
TEST(WallpaperTest, RendersEveryImportedSceneWithAStillFrame)
{
    const char *scenes[] = {"moonlit", "clawd", "cloud-drift", "honu-lagoon", "koi-pond",
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
