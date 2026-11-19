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
