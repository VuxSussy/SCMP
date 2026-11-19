#ifndef _SCMP_AUDIO_SOURCE_H
#define _SCMP_AUDIO_SOURCE_H

#include <cstdint>
#include "Domain/ScmpTypes.h"

namespace scmp
{

class AudioSource
{
  public:
    explicit AudioSource();

    virtual ~AudioSource();

    /**
     * Reads "buffer_size" frames of the audio stream into "buffer"
     */
    virtual bool read(pcm_stereo_sample *buffer, uint32_t buffer_size) = 0;
};
}

#endif
