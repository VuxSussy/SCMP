#ifndef SCMP_LOCAL_PLAYER_H
#define SCMP_LOCAL_PLAYER_H

#include "Player/MusicLibrary.h"
#include "Source/AudioSource.h"
#include "Writer/PixelImage.h"
#include <memory>

namespace scmp
{
struct TrackDetails
{
    std::string title;
    std::string artist;
    std::string album;
    PixelImage cover;
};

class LocalPlayer : public AudioSource
{
  public:
    static constexpr uint32_t sample_rate = 48000;
    virtual void play(const std::string &path) = 0;
    virtual void toggle_pause() = 0;
    virtual bool playing() const = 0;
    virtual bool finished() const = 0;
    virtual double elapsed() const = 0;
    virtual double duration() const = 0;
};

#ifdef _OS_OSX
std::unique_ptr<LocalPlayer> make_local_player();
TrackDetails load_track_details(const MusicTrack &track);
#endif
}

#endif
