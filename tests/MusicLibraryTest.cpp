#include <fstream>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Player/MusicLibrary.h"
#include "Domain/ScmpException.h"

TEST(MusicLibraryTest, FindsLocalAudioInSubfoldersWithoutFollowingDirectoryCycles)
{
    char temporary[] = "/tmp/scmp-library-test.XXXXXX";
    ASSERT_NE(mkdtemp(temporary), nullptr);
    const std::string root(temporary);
    const std::string album = root + "/Album name";
    const std::string hidden = root + "/.skip";
    ASSERT_EQ(mkdir(album.c_str(), 0700), 0);
    ASSERT_EQ(mkdir(hidden.c_str(), 0700), 0);
    ASSERT_EQ(symlink(root.c_str(), (album + "/cycle").c_str()), 0);
    const std::vector<std::string> files{album + "/Bài nhạc.MP3", root + "/a.wav", root + "/notes.txt",
                                         hidden + "/hidden.flac"};
    for (const auto &file : files)
    {
        std::ofstream output(file);
        output << "fixture";
    }

    const auto tracks = scmp::scan_music_directory(root + "/");
    EXPECT_EQ(tracks.size(), 2u);
    if (tracks.size() == 2)
    {
        EXPECT_EQ(tracks[0].title, "a");
        EXPECT_EQ(tracks[1].title, "Bài nhạc");
        EXPECT_EQ(tracks[1].folder, "Album name");
    }

    for (const auto &file : files)
    {
        unlink(file.c_str());
    }
    unlink((album + "/cycle").c_str());
    rmdir(album.c_str());
    rmdir(hidden.c_str());
    rmdir(root.c_str());
    EXPECT_THROW(scmp::scan_music_directory(root), scmp::ScmpException);
}
