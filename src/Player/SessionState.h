#ifndef SCMP_SESSION_STATE_H
#define SCMP_SESSION_STATE_H

#include <string>

namespace scmp
{
struct SessionState
{
    std::string view, mode, theme, folder, selection, track, filter;
    double elapsed = 0;
    bool playing = false;
    std::string library_setting, theme_setting;
    double volume = 1.0;
    bool shuffle = false;
    int repeat = 0;
    std::string layout;
};

SessionState read_session(const std::string &path);
bool save_session(const std::string &path, const SessionState &state);
}

#endif
