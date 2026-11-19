#ifndef _SCMP_MPD_AUDIO_SOURCE_H
#define _SCMP_MPD_AUDIO_SOURCE_H

#include <fstream>

#include "Source/AudioSource.h"
#include "Domain/Settings.h"

namespace scmp
{

class MpdAudioSource : public scmp::AudioSource
{
  public:
    explicit MpdAudioSource(const scmp::Settings *const settings);

    ~MpdAudioSource() override;

    bool read(pcm_stereo_sample *buffer, const uint32_t buffer_size) override;

  private:
    const scmp::Settings *const m_settings;

    int32_t m_mpd_fifo_fd;

    bool open_mpd_fifo();
};
}

#endif
