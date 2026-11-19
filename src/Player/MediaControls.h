#ifndef SCMP_MEDIA_CONTROLS_H
#define SCMP_MEDIA_CONTROLS_H

#include "Writer/PixelImage.h"

#include <memory>
#include <string>
#include <vector>

namespace scmp
{
struct NowPlaying
{
    std::string track;
    std::string title;
    std::string artist;
    std::string album;
    const PixelImage *cover = nullptr;
    double duration = 0;
    double elapsed = 0;
    bool playing = false;
};

struct MediaCommand
{
    enum Kind
    {
        Toggle,
        Play,
        Pause,
        Next,
        Previous,
        SeekTo,
        SkipForward,
        SkipBackward
    } kind;
    double position = 0;
};

// Media keys, headphone buttons and the system Now Playing widget.
// On platforms without an integration every call is a no-op.
class MediaControls
{
  public:
    MediaControls();
    ~MediaControls();
    MediaControls(const MediaControls &) = delete;
    MediaControls &operator=(const MediaControls &) = delete;

    std::vector<MediaCommand> poll();
    // Pass nullptr when nothing is loaded.
    void update(const NowPlaying *state);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}

#endif
