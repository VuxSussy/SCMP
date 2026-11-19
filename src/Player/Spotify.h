#ifndef SCMP_SPOTIFY_H
#define SCMP_SPOTIFY_H

#include <csignal>
#include <string>
#include <vector>

namespace scmp
{
int run_spotify(const std::vector<std::string> &arguments);
class MusicSession;
void import_spotify(MusicSession &music, const volatile std::sig_atomic_t &interrupted);
}

#endif
