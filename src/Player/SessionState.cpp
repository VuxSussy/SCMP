#include "Player/SessionState.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <unistd.h>
#include <vector>

scmp::SessionState scmp::read_session(const std::string &path)
{
    std::ifstream input(path);
    SessionState state;
    int version = 0;
    if (!(input >> version) || version != 1 ||
        !(input >> std::quoted(state.view) >> std::quoted(state.mode) >> std::quoted(state.theme)
                >> std::quoted(state.folder) >> std::quoted(state.selection) >> std::quoted(state.track)
                >> std::quoted(state.filter) >> state.elapsed >> state.playing
                >> std::quoted(state.library_setting) >> std::quoted(state.theme_setting)) ||
        !std::isfinite(state.elapsed) || state.elapsed < 0)
    {
        return {};
    }
    return state;
}

bool scmp::save_session(const std::string &path, const SessionState &state)
{
    const auto pattern = path + ".XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    const int fd = mkstemp(temporary.data());
    if (fd < 0) return false;
    close(fd);
    std::ofstream output(temporary.data());
    output << 1 << '\n' << std::quoted(state.view) << '\n' << std::quoted(state.mode) << '\n'
           << std::quoted(state.theme) << '\n' << std::quoted(state.folder) << '\n'
           << std::quoted(state.selection) << '\n' << std::quoted(state.track) << '\n'
           << std::quoted(state.filter) << '\n' << std::setprecision(17) << state.elapsed << ' '
           << state.playing << '\n' << std::quoted(state.library_setting) << '\n'
           << std::quoted(state.theme_setting) << '\n';
    output.close();
    if (output && std::rename(temporary.data(), path.c_str()) == 0) return true;
    std::remove(temporary.data());
    return false;
}
