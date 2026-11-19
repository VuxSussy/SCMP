#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

#include "Player/LocalPlayer.h"
#include "Domain/ScmpException.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>

namespace
{
NSURL *file_url(const std::string &path)
{
    return [NSURL fileURLWithFileSystemRepresentation:path.c_str() isDirectory:NO relativeToURL:nil];
}

std::string from_ns(NSString *value)
{
    const char *text = value.UTF8String;
    return text ? text : "";
}

scmp::PixelImage decode_image(CGImageSourceRef source)
{
    if (!source)
    {
        return {};
    }
    NSDictionary *options = @{
        (__bridge NSString *)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
        (__bridge NSString *)kCGImageSourceCreateThumbnailWithTransform: @YES,
        (__bridge NSString *)kCGImageSourceThumbnailMaxPixelSize: @384,
    };
    CGImageRef image = CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options);
    CFRelease(source);
    if (!image)
    {
        return {};
    }
    scmp::PixelImage output;
    output.width = static_cast<int>(CGImageGetWidth(image));
    output.height = static_cast<int>(CGImageGetHeight(image));
    output.pixels.resize(static_cast<size_t>(output.width) * output.height);
    auto color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    auto context = CGBitmapContextCreate(output.pixels.data(), output.width, output.height, 8,
        output.width * sizeof(uint32_t), color_space, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    if (context)
    {
        CGContextDrawImage(context, CGRectMake(0, 0, output.width, output.height), image);
        CGContextRelease(context);
    }
    CGColorSpaceRelease(color_space);
    CGImageRelease(image);
    return context ? std::move(output) : scmp::PixelImage{};
}

scmp::PixelImage flac_cover(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    char magic[4];
    if (!file.read(magic, sizeof(magic)) || std::string(magic, sizeof(magic)) != "fLaC")
    {
        return {};
    }

    // The FLAC PICTURE block prefixes the media type, description and image with big-endian lengths.
    scmp::PixelImage fallback;
    unsigned char header[4];
    while (file.read(reinterpret_cast<char *>(header), sizeof(header)))
    {
        const size_t size = (static_cast<size_t>(header[1]) << 16) |
                            (static_cast<size_t>(header[2]) << 8) | header[3];
        if ((header[0] & 0x7f) == 6)
        {
            std::vector<uint8_t> block(size);
            if (!file.read(reinterpret_cast<char *>(block.data()), size))
            {
                break;
            }
            size_t offset = 0;
            auto integer = [&](uint32_t &value) {
                if (size - offset < 4) return false;
                value = (static_cast<uint32_t>(block[offset]) << 24) |
                        (static_cast<uint32_t>(block[offset + 1]) << 16) |
                        (static_cast<uint32_t>(block[offset + 2]) << 8) | block[offset + 3];
                offset += 4;
                return true;
            };
            auto skip = [&](size_t count) {
                if (count > size - offset) return false;
                offset += count;
                return true;
            };
            uint32_t type = 0;
            uint32_t length = 0;
            if (integer(type) && integer(length) && skip(length) &&
                integer(length) && skip(length) && skip(16) &&
                integer(length) && length > 0 && length <= size - offset &&
                (fallback.empty() || type == 3))
            {
                NSData *data = [NSData dataWithBytes:block.data() + offset length:length];
                auto picture = decode_image(CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr));
                if (!picture.empty())
                {
                    if (type == 3) return picture;
                    fallback = std::move(picture);
                }
            }
        }
        else
        {
            file.seekg(size, std::ios::cur);
        }
        if (header[0] & 0x80)
        {
            break;
        }
    }
    return fallback;
}

struct SampleBuffer
{
    std::mutex lock;
    std::vector<scmp::pcm_stereo_sample> frames = std::vector<scmp::pcm_stereo_sample>(scmp::LocalPlayer::sample_rate);
    size_t write = 0;
    size_t available = 0;
    std::atomic<bool> ended{false};

    void capture(AVAudioPCMBuffer *data)
    {
        const auto channels = data.floatChannelData;
        if (!channels || data.format.channelCount < 2)
        {
            return;
        }
        std::unique_lock<std::mutex> guard(lock, std::try_to_lock);
        if (!guard.owns_lock())
        {
            return;
        }
        const auto stride = data.stride;
        const auto count = data.frameLength;
        const auto pcm = [](float value) -> int16_t {
            return static_cast<int16_t>(std::max(-1.0f, std::min(1.0f, value)) * 32767.0f);
        };
        for (size_t i = 0; i < count; ++i)
        {
            frames[write] = {pcm(channels[0][i * stride]), pcm(channels[1][i * stride])};
            if (++write == frames.size())
            {
                write = 0;
            }
        }
        available = std::min(frames.size(), available + count);
    }

    bool read(scmp::pcm_stereo_sample *buffer, size_t count)
    {
        std::unique_lock<std::mutex> guard(lock, std::try_to_lock);
        if (!guard.owns_lock() || available == 0)
        {
            return false;
        }
        const auto frames_read = std::min(count, available);
        const auto first = (write + frames.size() - frames_read) % frames.size();
        const auto before_wrap = std::min(frames_read, frames.size() - first);
        std::copy_n(frames.data() + first, before_wrap, buffer);
        std::copy_n(frames.data(), frames_read - before_wrap, buffer + before_wrap);
        std::fill(buffer + frames_read, buffer + count, scmp::pcm_stereo_sample{});
        available = 0;
        return true;
    }
};

struct Playback
{
    AVAudioEngine *engine = [[AVAudioEngine alloc] init];
    AVAudioPlayerNode *player = [[AVAudioPlayerNode alloc] init];
    AVAudioMixerNode *mixer = [[AVAudioMixerNode alloc] init];
    AVAudioFile *file = nil;
    std::shared_ptr<SampleBuffer> samples = std::make_shared<SampleBuffer>();
    double length = 0;
    double paused_at = 0;
    double offset = 0;
    bool tap = false;

    ~Playback()
    {
        @autoreleasepool
        {
            [player stop];
            [engine stop];
            if (tap)
            {
                [mixer removeTapOnBus:0];
            }
        }
    }
};

class MacPlayer final : public scmp::LocalPlayer
{
  public:
    void play(const std::string &path, double position, bool start) override
    {
        @autoreleasepool
        {
            auto next = std::make_unique<Playback>();
            NSError *error = nil;
            next->file = [[AVAudioFile alloc] initForReading:file_url(path) error:&error];
            if (!next->file || next->file.length <= 0)
            {
                throw scmp::ScmpException("Could not play %s (%s)", path.c_str(),
                    error ? error.localizedDescription.UTF8String : "empty audio file");
            }
            auto input = next->file.processingFormat;
            auto format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:sample_rate channels:2];
            next->length = next->file.length / input.sampleRate;

            [next->engine attachNode:next->player];
            [next->engine attachNode:next->mixer];
            [next->engine connect:next->player to:next->mixer format:input];
            [next->engine connect:next->mixer to:next->engine.mainMixerNode format:format];
            const auto samples = next->samples;
            [next->mixer installTapOnBus:0 bufferSize:512 format:format
                block:^(AVAudioPCMBuffer *buffer, AVAudioTime *) {
                    samples->capture(buffer);
                }];
            next->tap = true;
            const auto last_frame = next->file.length - 1;
            const auto first_frame = static_cast<AVAudioFramePosition>(
                std::min(static_cast<double>(last_frame), position * input.sampleRate));
            next->offset = next->paused_at = first_frame / input.sampleRate;
            const auto completion = ^(AVAudioPlayerNodeCompletionCallbackType) {
                samples->ended.store(true, std::memory_order_release);
            };
            if (first_frame == 0)
            {
                [next->player scheduleFile:next->file atTime:nil
                    completionCallbackType:AVAudioPlayerNodeCompletionDataPlayedBack completionHandler:completion];
            }
            else
            {
                [next->player scheduleSegment:next->file startingFrame:first_frame
                    frameCount:static_cast<AVAudioFrameCount>(next->file.length - first_frame) atTime:nil
                    completionCallbackType:AVAudioPlayerNodeCompletionDataPlayedBack completionHandler:completion];
            }
            if (![next->engine startAndReturnError:&error])
            {
                throw scmp::ScmpException("Could not start audio output (%s)", error.localizedDescription.UTF8String);
            }
            next->player.volume = static_cast<float>(m_volume);
            m_playback = std::move(next);
            if (start) [m_playback->player play];
        }
    }

    void toggle_pause() override
    {
        @autoreleasepool
        {
            if (!m_playback || finished())
            {
                return;
            }
            if (m_playback->player.isPlaying)
            {
                m_playback->paused_at = elapsed();
                [m_playback->player pause];
            }
            else
            {
                [m_playback->player play];
            }
        }
    }

    bool playing() const override
    {
        return m_playback && !finished() && m_playback->player.isPlaying;
    }

    bool finished() const override
    {
        return m_playback && m_playback->samples->ended.load(std::memory_order_acquire);
    }

    double elapsed() const override
    {
        @autoreleasepool
        {
            if (!playing())
            {
                return finished() ? duration() : m_playback ? m_playback->paused_at : 0;
            }
            auto *last = m_playback->player.lastRenderTime;
            auto *time = last ? [m_playback->player playerTimeForNodeTime:last] : nil;
            return time && time.sampleRate > 0
                ? std::max(m_playback->offset, std::min(duration(), m_playback->offset + time.sampleTime / time.sampleRate))
                : m_playback->paused_at;
        }
    }

    double duration() const override
    {
        return m_playback ? m_playback->length : 0;
    }

    bool read(scmp::pcm_stereo_sample *buffer, uint32_t count) override
    {
        return playing() && m_playback->samples->read(buffer, count);
    }

    void set_volume(double volume) override
    {
        m_volume = std::max(0.0, std::min(1.0, volume));
        if (m_playback) m_playback->player.volume = static_cast<float>(m_volume);
    }

    double volume() const override
    {
        return m_volume;
    }

  private:
    std::unique_ptr<Playback> m_playback;
    double m_volume = 1.0;
};
}

scmp::PixelImage scmp::load_pixel_image(const std::string &path)
{
    @autoreleasepool
    {
        return decode_image(CGImageSourceCreateWithURL((__bridge CFURLRef)file_url(path), nullptr));
    }
}

scmp::TrackTags scmp::load_track_tags(const MusicTrack &track)
{
    TrackTags tags;
    @autoreleasepool
    {
        auto asset = [AVURLAsset URLAssetWithURL:file_url(track.path) options:nil];
        for (AVMetadataItem *item in asset.commonMetadata)
        {
            if ([item.commonKey isEqualToString:AVMetadataCommonKeyTitle] && item.stringValue.length)
            {
                tags.title = from_ns(item.stringValue);
            }
            else if ([item.commonKey isEqualToString:AVMetadataCommonKeyArtist] && item.stringValue.length)
            {
                tags.artist = from_ns(item.stringValue);
            }
            else if ([item.commonKey isEqualToString:AVMetadataCommonKeyAlbumName] && item.stringValue.length)
            {
                tags.album = from_ns(item.stringValue);
            }
        }
        auto file = [[AVAudioFile alloc] initForReading:file_url(track.path) error:nil];
        if (file && file.processingFormat.sampleRate > 0)
        {
            tags.duration = file.length / file.processingFormat.sampleRate;
        }
    }
    return tags;
}

scmp::TrackDetails scmp::load_track_details(const MusicTrack &track)
{
    TrackDetails details;
    details.title = track.title;
    @autoreleasepool
    {
        auto asset = [AVURLAsset URLAssetWithURL:file_url(track.path) options:nil];
        for (AVMetadataItem *item in asset.commonMetadata)
        {
            if ([item.commonKey isEqualToString:AVMetadataCommonKeyTitle] && item.stringValue.length)
            {
                details.title = from_ns(item.stringValue);
            }
            else if ([item.commonKey isEqualToString:AVMetadataCommonKeyArtist])
            {
                details.artist = from_ns(item.stringValue);
            }
            else if ([item.commonKey isEqualToString:AVMetadataCommonKeyAlbumName])
            {
                details.album = from_ns(item.stringValue);
            }
            else if ([item.commonKey isEqualToString:AVMetadataCommonKeyArtwork] && item.dataValue)
            {
                details.cover = decode_image(CGImageSourceCreateWithData((__bridge CFDataRef)item.dataValue, nullptr));
            }
        }
        if (details.cover.empty() && [file_url(track.path).pathExtension caseInsensitiveCompare:@"flac"] == NSOrderedSame)
        {
            details.cover = flac_cover(track.path);
        }
    }
    if (details.cover.empty())
    {
        const auto folder = track.path.substr(0, track.path.find_last_of('/') + 1);
        for (const auto *name : {"cover", "folder", "front", "album"})
        {
            for (const auto *extension : {".jpg", ".jpeg", ".png"})
            {
                details.cover = load_pixel_image(folder + name + extension);
                if (!details.cover.empty())
                {
                    break;
                }
            }
            if (!details.cover.empty())
            {
                break;
            }
        }
    }
    return details;
}

std::unique_ptr<scmp::LocalPlayer> scmp::make_local_player()
{
    return std::make_unique<MacPlayer>();
}
