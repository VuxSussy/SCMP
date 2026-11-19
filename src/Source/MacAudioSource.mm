#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>

#include "Source/MacAudioSource.h"
#include "Domain/ScmpException.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace
{
void check_status(OSStatus status, const char *operation)
{
    if (status != noErr)
    {
        throw scmp::ScmpException("Core Audio could not %s (OSStatus %d). Check "
                                "System Settings > Privacy & Security > Screen & System Audio Recording.",
                                operation, status);
    }
}

class API_AVAILABLE(macos(14.2)) MacAudioSource final : public scmp::AudioSource
{
  public:
    void start(uint32_t &sample_rate)
    {
        @autoreleasepool
        {
            CATapDescription *tap = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
            tap.name = @"SCMP";
            tap.privateTap = YES;
            tap.muteBehavior = CATapUnmuted;
            check_status(AudioHardwareCreateProcessTap(tap, &m_tap), "create the system audio tap");

            AudioStreamBasicDescription format{};
            UInt32 size = sizeof(format);
            const AudioObjectPropertyAddress address{kAudioTapPropertyFormat,
                kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
            check_status(AudioObjectGetPropertyData(m_tap, &address, 0, nullptr, &size, &format),
                         "read the tap format");
            if (format.mFormatID != kAudioFormatLinearPCM ||
                !(format.mFormatFlags & kAudioFormatFlagIsFloat) ||
                (format.mFormatFlags & kAudioFormatFlagIsBigEndian) ||
                format.mBitsPerChannel != 32 || format.mChannelsPerFrame != 2 ||
                format.mSampleRate < 8000 || format.mSampleRate > 384000)
            {
                throw scmp::ScmpException("Unsupported Core Audio stereo tap format");
            }

            NSDictionary *device = @{
                @kAudioAggregateDeviceNameKey: @"SCMP",
                @kAudioAggregateDeviceUIDKey: [NSUUID UUID].UUIDString,
                @kAudioAggregateDeviceIsPrivateKey: @YES,
                @kAudioAggregateDeviceTapListKey: @[@{
                    @kAudioSubTapUIDKey: tap.UUID.UUIDString,
                    @kAudioSubTapDriftCompensationKey: @YES
                }]
            };
            check_status(AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)device, &m_device),
                         "create the private capture device");
            sample_rate = static_cast<uint32_t>(format.mSampleRate);
            m_frames.resize(sample_rate);
            check_status(AudioDeviceCreateIOProcID(m_device, capture, this, &m_io), "set up the capture callback");
            check_status(AudioDeviceStart(m_device, m_io), "start system audio capture");
            m_started = true;
        }
    }

    ~MacAudioSource() override
    {
        if (m_started) AudioDeviceStop(m_device, m_io);
        if (m_io) AudioDeviceDestroyIOProcID(m_device, m_io);
        if (m_device != kAudioObjectUnknown) AudioHardwareDestroyAggregateDevice(m_device);
        if (m_tap != kAudioObjectUnknown) AudioHardwareDestroyProcessTap(m_tap);
    }

    bool read(scmp::pcm_stereo_sample *buffer, uint32_t count) override
    {
        std::unique_lock<std::mutex> lock(m_lock, std::try_to_lock);
        if (!lock.owns_lock() || m_available < count)
        {
            return false;
        }
        const size_t start = (m_write + m_frames.size() - count) % m_frames.size();
        const size_t first = std::min<size_t>(count, m_frames.size() - start);
        std::copy_n(m_frames.data() + start, first, buffer);
        std::copy_n(m_frames.data(), count - first, buffer + first);
        m_available = 0;
        return true;
    }

  private:
    static int16_t pcm16(float sample)
    {
        return static_cast<int16_t>(std::max(-1.0f, std::min(1.0f, sample)) * 32767.0f);
    }

    static OSStatus capture(AudioObjectID, const AudioTimeStamp *, const AudioBufferList *input,
                            const AudioTimeStamp *, AudioBufferList *, const AudioTimeStamp *, void *context)
    {
        if (!input || input->mNumberBuffers == 0 || !input->mBuffers[0].mData)
        {
            return noErr;
        }
        const auto &first = input->mBuffers[0];
        const auto *left = static_cast<const float *>(first.mData);
        const float *right = nullptr;
        size_t frames = first.mDataByteSize / sizeof(float);
        const size_t stride = first.mNumberChannels;
        if (input->mNumberBuffers == 1 && stride == 2)
        {
            right = left + 1;
            frames /= 2;
        }
        else if (input->mNumberBuffers == 2 && stride == 1 &&
                 input->mBuffers[1].mNumberChannels == 1 && input->mBuffers[1].mData)
        {
            right = static_cast<const float *>(input->mBuffers[1].mData);
            frames = std::min<size_t>(frames, input->mBuffers[1].mDataByteSize / sizeof(float));
        }
        if (!right)
        {
            return noErr;
        }

        auto &source = *static_cast<MacAudioSource *>(context);
        std::unique_lock<std::mutex> lock(source.m_lock, std::try_to_lock);
        if (!lock.owns_lock())
        {
            return noErr;
        }
        for (size_t i = 0; i < frames; ++i)
        {
            source.m_frames[source.m_write] = {pcm16(left[i * stride]), pcm16(right[i * stride])};
            if (++source.m_write == source.m_frames.size())
            {
                source.m_write = 0;
            }
        }
        source.m_available = std::min(source.m_available + frames, source.m_frames.size());
        return noErr;
    }

    AudioObjectID m_tap{kAudioObjectUnknown};
    AudioObjectID m_device{kAudioObjectUnknown};
    AudioDeviceIOProcID m_io{nullptr};
    bool m_started{false};
    std::mutex m_lock;
    std::vector<scmp::pcm_stereo_sample> m_frames;
    size_t m_write{0};
    size_t m_available{0};
};
}

std::unique_ptr<scmp::AudioSource> scmp::make_mac_audio_source(uint32_t &sample_rate)
{
    if (@available(macOS 14.2, *))
    {
        auto source = std::make_unique<MacAudioSource>();
        source->start(sample_rate);
        return source;
    }
    throw ScmpException("Core Audio system capture requires macOS 14.2 or later");
}
