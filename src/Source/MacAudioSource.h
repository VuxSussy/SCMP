#ifndef SCMP_MAC_AUDIO_SOURCE_H
#define SCMP_MAC_AUDIO_SOURCE_H

#include <memory>
#include "Source/AudioSource.h"

namespace scmp
{
std::unique_ptr<AudioSource> make_mac_audio_source(uint32_t &sample_rate);
}

#endif
