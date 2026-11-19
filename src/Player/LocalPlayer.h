#ifndef SCMP_LOCAL_PLAYER_H
#define SCMP_LOCAL_PLAYER_H

#include "Player/MusicLibrary.h"
#include "Source/AudioSource.h"
#include "Writer/PixelImage.h"
#include <memory>

namespace scmp
{
struct TrackTags
{
    std::string title;
    std::string artist;
    std::string album;
    double duration = 0;
};

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
    virtual void play(const std::string &path, double position = 0, bool start = true) = 0;
    virtual void toggle_pause() = 0;
    virtual bool playing() const = 0;
    virtual bool finished() const = 0;
    virtual double elapsed() const = 0;
    virtual double duration() const = 0;
    // Linear gain from 0 to 1. The value carries over to the next track.
    virtual void set_volume(double volume) = 0;
    virtual double volume() const = 0;
};

std::unique_ptr<LocalPlayer> make_local_player();
TrackDetails load_track_details(const MusicTrack &track);
// Title, artist and duration only, without decoding cover art.
TrackTags load_track_tags(const MusicTrack &track);
}

#endif
