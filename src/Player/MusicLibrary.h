#ifndef SCMP_MUSIC_LIBRARY_H
#define SCMP_MUSIC_LIBRARY_H

#include <string>
#include <vector>

namespace scmp
{
struct MusicTrack
{
    std::string path;
    std::string title;
    std::string folder;
};

std::string expand_path(const std::string &path);
std::vector<MusicTrack> scan_music_directory(const std::string &directory);
}

#endif
