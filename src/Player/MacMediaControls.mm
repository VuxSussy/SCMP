#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>
#import <MediaPlayer/MediaPlayer.h>

#include "Player/MediaControls.h"

#include <cmath>
#include <mutex>

namespace
{
NSString *to_ns(const std::string &text)
{
    return [NSString stringWithUTF8String:text.c_str()] ?: @"";
}

NSImage *to_image(const scmp::PixelImage &image)
{
    if (image.empty()) return nil;
    auto color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(image.pixels.data()),
                             static_cast<CFIndex>(image.pixels.size() * sizeof(uint32_t)));
    auto provider = CGDataProviderCreateWithCFData(data);
    auto cg = CGImageCreate(image.width, image.height, 8, 32, image.width * sizeof(uint32_t), color_space,
                            kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, provider, nullptr,
                            false, kCGRenderingIntentDefault);
    NSImage *result = cg ? [[NSImage alloc] initWithCGImage:cg size:NSMakeSize(image.width, image.height)] : nil;
    if (cg) CGImageRelease(cg);
    CGDataProviderRelease(provider);
    CFRelease(data);
    CGColorSpaceRelease(color_space);
    return result;
}
}

struct scmp::MediaControls::Impl
{
    std::mutex lock;
    std::vector<MediaCommand> pending;
    std::vector<id> targets;
    std::string track;
    bool playing = false;
    bool published = false;
    double elapsed = 0;
    CFAbsoluteTime published_at = 0;

    void add(MPRemoteCommand *command, MediaCommand::Kind kind)
    {
        command.enabled = YES;
        id target = [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
            MediaCommand item{kind, 0};
            if ([event isKindOfClass:[MPChangePlaybackPositionCommandEvent class]])
            {
                item.position = static_cast<MPChangePlaybackPositionCommandEvent *>(event).positionTime;
            }
            std::lock_guard<std::mutex> guard(lock);
            pending.push_back(item);
            return MPRemoteCommandHandlerStatusSuccess;
        }];
        targets.push_back(target);
    }
};

scmp::MediaControls::MediaControls() : m_impl(std::make_unique<Impl>())
{
    @autoreleasepool
    {
        // Now Playing only routes media keys to processes with an application object. Stay out of the Dock.
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        auto *center = [MPRemoteCommandCenter sharedCommandCenter];
        m_impl->add(center.togglePlayPauseCommand, MediaCommand::Toggle);
        m_impl->add(center.playCommand, MediaCommand::Play);
        m_impl->add(center.pauseCommand, MediaCommand::Pause);
        m_impl->add(center.nextTrackCommand, MediaCommand::Next);
        m_impl->add(center.previousTrackCommand, MediaCommand::Previous);
        m_impl->add(center.changePlaybackPositionCommand, MediaCommand::SeekTo);
        center.skipForwardCommand.preferredIntervals = @[@10];
        center.skipBackwardCommand.preferredIntervals = @[@10];
        m_impl->add(center.skipForwardCommand, MediaCommand::SkipForward);
        m_impl->add(center.skipBackwardCommand, MediaCommand::SkipBackward);
    }
}

scmp::MediaControls::~MediaControls()
{
    @autoreleasepool
    {
        auto *center = [MPRemoteCommandCenter sharedCommandCenter];
        for (MPRemoteCommand *command in @[center.togglePlayPauseCommand, center.playCommand, center.pauseCommand,
                                           center.nextTrackCommand, center.previousTrackCommand,
                                           center.changePlaybackPositionCommand, center.skipForwardCommand,
                                           center.skipBackwardCommand])
        {
            [command removeTarget:nil];
        }
        auto *info = [MPNowPlayingInfoCenter defaultCenter];
        info.nowPlayingInfo = nil;
        info.playbackState = MPNowPlayingPlaybackStateStopped;
    }
}

std::vector<scmp::MediaCommand> scmp::MediaControls::poll()
{
    // The terminal loop owns the main thread, so let queued remote commands run here.
    while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true) == kCFRunLoopRunHandledSource)
    {
    }
    std::lock_guard<std::mutex> guard(m_impl->lock);
    std::vector<MediaCommand> commands;
    commands.swap(m_impl->pending);
    return commands;
}

void scmp::MediaControls::update(const NowPlaying *state)
{
    @autoreleasepool
    {
        auto *info = [MPNowPlayingInfoCenter defaultCenter];
        if (!state)
        {
            if (m_impl->published)
            {
                info.nowPlayingInfo = nil;
                info.playbackState = MPNowPlayingPlaybackStateStopped;
                m_impl->published = false;
                m_impl->track.clear();
            }
            return;
        }
        // Republish only on a change. The widget extrapolates the position from the playback rate.
        const auto now = CFAbsoluteTimeGetCurrent();
        const auto expected = m_impl->elapsed + (m_impl->playing ? now - m_impl->published_at : 0);
        const bool changed = !m_impl->published || state->track != m_impl->track ||
                             state->playing != m_impl->playing || std::fabs(state->elapsed - expected) > 1.5;
        if (!changed) return;

        NSMutableDictionary *details = [NSMutableDictionary dictionary];
        details[MPMediaItemPropertyTitle] = to_ns(state->title);
        if (!state->artist.empty()) details[MPMediaItemPropertyArtist] = to_ns(state->artist);
        if (!state->album.empty()) details[MPMediaItemPropertyAlbumTitle] = to_ns(state->album);
        details[MPMediaItemPropertyPlaybackDuration] = @(state->duration);
        details[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(state->elapsed);
        details[MPNowPlayingInfoPropertyPlaybackRate] = @(state->playing ? 1.0 : 0.0);
        details[MPNowPlayingInfoPropertyMediaType] = @(MPNowPlayingInfoMediaTypeAudio);
        if (state->cover && !state->cover->empty())
        {
            NSImage *image = to_image(*state->cover);
            if (image)
            {
                details[MPMediaItemPropertyArtwork] = [[MPMediaItemArtwork alloc]
                    initWithBoundsSize:image.size requestHandler:^NSImage *(CGSize) { return image; }];
            }
        }
        info.nowPlayingInfo = details;
        info.playbackState = state->playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
        m_impl->published = true;
        m_impl->track = state->track;
        m_impl->playing = state->playing;
        m_impl->elapsed = state->elapsed;
        m_impl->published_at = now;
    }
}
