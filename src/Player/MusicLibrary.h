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
    // Index of the library folder (playlist) the track came from.
    size_t root = 0;
};

// A library folder shown as a playlist.
struct Playlist
{
    std::string name;
    std::string directory;
    size_t tracks = 0;
};

std::string expand_path(const std::string &path);
std::vector<MusicTrack> scan_music_directory(const std::string &directory);
// music.directory holds one folder or several separated by commas; "none" means no folder.
std::vector<std::string> split_folders(const std::string &setting);
std::string join_folders(const std::vector<std::string> &folders);
}

#endif
