#include "Player/MediaControls.h"

#ifndef _OS_OSX
struct scmp::MediaControls::Impl
{
};

scmp::MediaControls::MediaControls() = default;
scmp::MediaControls::~MediaControls() = default;

std::vector<scmp::MediaCommand> scmp::MediaControls::poll()
{
    return {};
}

void scmp::MediaControls::update(const NowPlaying *)
{
}
#endif
