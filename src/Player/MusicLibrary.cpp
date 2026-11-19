#include "Player/MusicLibrary.h"

#include "Domain/ScmpException.h"
#include "Utils/Utils.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <sys/stat.h>

namespace
{
bool audio_file(const std::string &name)
{
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos)
    {
        return false;
    }
    auto extension = scmp::Utils::lowercase(name.substr(dot + 1));
    return extension == "mp3" || extension == "m4a" || extension == "aac" ||
           extension == "flac" || extension == "wav" || extension == "aiff" ||
           extension == "aif" || extension == "caf" || extension == "mp4" ||
           extension == "wave" || extension == "aifc" || extension == "opus";
}
}

std::string scmp::expand_path(const std::string &path)
{
    if (path == "~")
    {
        return Utils::get_home_directory();
    }
    if (path.compare(0, 2, "~/") == 0)
    {
        return Utils::get_home_directory() + path.substr(2);
    }
    return path;
}

std::vector<scmp::MusicTrack> scmp::scan_music_directory(const std::string &directory)
{
    const auto path = expand_path(directory);
    using Directory = std::unique_ptr<DIR, decltype(&closedir)>;
    Directory root(opendir(path.c_str()), closedir);
    if (!root)
    {
        throw ScmpException("Could not open music folder %s (%s)", path.c_str(), std::strerror(errno));
    }

    std::vector<MusicTrack> tracks;
    std::vector<std::string> folders{path};
    while (!folders.empty())
    {
        auto folder = std::move(folders.back());
        folders.pop_back();
        Directory entries(folder == path ? root.release() : opendir(folder.c_str()), closedir);
        if (!entries)
        {
            continue;
        }
        while (auto *entry = readdir(entries.get()))
        {
            const std::string name(entry->d_name);
            if (name.front() == '.')
            {
                continue;
            }
            const auto file = folder + (folder.back() == '/' ? "" : "/") + name;
            struct stat info{};
            if (lstat(file.c_str(), &info) != 0)
            {
                continue;
            }
            if (S_ISDIR(info.st_mode))
            {
                folders.push_back(file);
            }
            else if (audio_file(name))
            {
                if (S_ISLNK(info.st_mode) && stat(file.c_str(), &info) != 0)
                {
                    continue;
                }
                if (S_ISREG(info.st_mode))
                {
                    const auto relative = folder == path ? "" : folder.substr(path.size() + (path.back() == '/' ? 0 : 1));
                    tracks.push_back({file, name.substr(0, name.find_last_of('.')), relative});
                }
            }
        }
    }

    std::sort(tracks.begin(), tracks.end(), [](const MusicTrack &left, const MusicTrack &right) {
        const auto first = Utils::lowercase(left.path);
        const auto second = Utils::lowercase(right.path);
        return first == second ? left.path < right.path : first < second;
    });
    return tracks;
}
