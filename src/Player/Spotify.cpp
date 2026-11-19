#include "Player/Spotify.h"
#include "Domain/ScmpException.h"
#include "Player/MusicSession.h"
#include "Utils/Utils.h"
#include "Writer/TerminalText.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits.h>
#include <spawn.h>
#include <sys/wait.h>

#ifdef _OS_OSX
#include <mach-o/dyld.h>
#endif

extern char **environ;

namespace
{
std::string spotify_program()
{
#ifdef _OS_OSX
    char executable[PATH_MAX];
    uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) != 0)
    {
        throw scmp::ScmpException("Could not locate the scmp executable.");
    }
#else
    const char *executable = "/proc/self/exe";
#endif
    char resolved[PATH_MAX];
    if (!realpath(executable, resolved))
    {
        throw scmp::ScmpException("Could not locate the scmp executable: %s", std::strerror(errno));
    }
    const std::string path(resolved);
    return path.substr(0, path.find_last_of('/') + 1) + "scmp-spotify";
}
}

int scmp::run_spotify(const std::vector<std::string> &arguments)
{
    const auto program = spotify_program();
    std::vector<char *> argv{const_cast<char *>(program.c_str())};
    for (const auto &argument : arguments)
    {
        argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);

    pid_t child;
    const auto error = posix_spawn(&child, program.c_str(), nullptr, nullptr, argv.data(), environ);
    if (error)
    {
        throw ScmpException("Could not start Spotify helper %s (%s). Run make or reinstall scmp.",
                           program.c_str(), std::strerror(error));
    }
    int status;
    while (waitpid(child, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            throw ScmpException("Could not wait for Spotify helper: %s", std::strerror(errno));
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

void scmp::import_spotify(MusicSession &music, const volatile std::sig_atomic_t &interrupted)
{
    std::string url;
    if (!prompt_text(url, "Import Spotify playlist", "Paste a public playlist link.", interrupted) || url.empty())
    {
        return;
    }
    // Only use the playlist ID for the local folder. The helper validates the entire link.
    const auto marker = url.find("playlist/");
    const auto start = marker != std::string::npos ? marker + 9 :
                       url.compare(0, 17, "spotify:playlist:") == 0 ? 17 : std::string::npos;
    const auto id = start == std::string::npos ? "" : url.substr(start, 22);
    if (id.size() != 22 || id.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") !=
                              std::string::npos)
    {
        music.set_error("Use a public Spotify playlist link. Press i to try again.");
        return;
    }
    auto folder = Utils::get_home_directory() + "Music/Spotify/" + id;
    if (!prompt_text(folder, "Download folder", "Enter uses this folder. Edit it to save somewhere else.",
                     interrupted) || folder.empty())
    {
        return;
    }
    endwin();
    try
    {
        const auto status = run_spotify({"import", url, "--download", "--output", folder});
        if (!interrupted && (status == 0 || status == 3))
        {
            music.open(folder);
            if (status == 3)
            {
                music.set_error("Some tracks failed to download. Press i to retry.");
            }
        }
        else if (!interrupted)
        {
            music.set_error("Spotify download failed. Press i to try again.");
            std::cout << "\nPress Enter to return to Player." << std::endl;
            std::string line;
            std::getline(std::cin, line);
        }
    }
    catch (const std::exception &error)
    {
        music.set_error(error.what());
    }
    clearok(stdscr, TRUE);
    refresh();
}
