#include "Player/SessionState.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdlib>
#include <unistd.h>

TEST(SessionStateTest, RestoresPathsAndPlaybackAndRejectsTruncatedState)
{
    char path[] = "/tmp/scmp-session-test.XXXXXX";
    const int fd = mkstemp(path);
    ASSERT_GE(fd, 0);
    close(fd);
    scmp::SessionState state{"player", "lava", "koi-pond", "/Music/My Albums",
                             "/Music/My Albums/1.mp3", "/Music/My Albums/2.mp3", "part \"two\"",
                             17.125, true, "none", "auto"};
    EXPECT_TRUE(scmp::save_session(path, state));
    const auto restored = scmp::read_session(path);
    EXPECT_EQ(restored.view, state.view);
    EXPECT_EQ(restored.mode, state.mode);
    EXPECT_EQ(restored.theme, state.theme);
    EXPECT_EQ(restored.folder, state.folder);
    EXPECT_EQ(restored.selection, state.selection);
    EXPECT_EQ(restored.track, state.track);
    EXPECT_EQ(restored.filter, state.filter);
    EXPECT_EQ(restored.elapsed, state.elapsed);
    EXPECT_TRUE(restored.playing);
    EXPECT_EQ(restored.library_setting, state.library_setting);
    EXPECT_EQ(restored.theme_setting, state.theme_setting);
    std::ofstream(path) << "1\n\"player\"";
    EXPECT_TRUE(scmp::read_session(path).view.empty());
    unlink(path);
}

TEST(SessionStateTest, KeepsVolumeShuffleRepeatAndReadsVersionOne)
{
    char path[] = "/tmp/scmp-session-test.XXXXXX";
    const int fd = mkstemp(path);
    ASSERT_GE(fd, 0);
    close(fd);
    scmp::SessionState state;
    state.view = "player";
    state.volume = 0.35;
    state.shuffle = true;
    state.repeat = 2;
    EXPECT_TRUE(scmp::save_session(path, state));
    auto restored = scmp::read_session(path);
    EXPECT_DOUBLE_EQ(restored.volume, 0.35);
    EXPECT_TRUE(restored.shuffle);
    EXPECT_EQ(restored.repeat, 2);

    // Files written before volume and modes existed still load with defaults.
    std::ofstream(path) << "1\n\"player\"\n\"lava\"\n\"auto\"\n\"\"\n\"\"\n\"\"\n\"\"\n0 0\n\"\"\n\"\"\n";
    restored = scmp::read_session(path);
    EXPECT_EQ(restored.view, "player");
    EXPECT_DOUBLE_EQ(restored.volume, 1.0);
    EXPECT_FALSE(restored.shuffle);
    EXPECT_EQ(restored.repeat, 0);
    unlink(path);
}
