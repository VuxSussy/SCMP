#include "Player/SystemVolume.h"

#include <AudioToolbox/AudioServices.h>
#include <CoreAudio/CoreAudio.h>
#include <algorithm>

namespace
{
AudioObjectPropertyAddress output_property(AudioObjectPropertySelector selector)
{
    return {selector, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain};
}

AudioDeviceID default_output()
{
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    const AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultOutputDevice,
                                             kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device) != noErr)
    {
        return kAudioObjectUnknown;
    }
    return device;
}

// The virtual main volume is the value the menu bar slider and the volume keys change.
bool settable(AudioDeviceID device, const AudioObjectPropertyAddress &address)
{
    Boolean writable = false;
    return device != kAudioObjectUnknown && AudioObjectHasProperty(device, &address) &&
           AudioObjectIsPropertySettable(device, &address, &writable) == noErr && writable;
}

bool muted(AudioDeviceID device)
{
    const auto address = output_property(kAudioDevicePropertyMute);
    UInt32 value = 0;
    UInt32 size = sizeof(value);
    return AudioObjectHasProperty(device, &address) &&
           AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) == noErr && value;
}
}

double scmp::system_volume()
{
    const auto device = default_output();
    const auto address = output_property(kAudioHardwareServiceDeviceProperty_VirtualMainVolume);
    if (!settable(device, address)) return -1;
    Float32 value = 0;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) != noErr) return -1;
    return muted(device) ? 0 : std::max(0.0, std::min(1.0, static_cast<double>(value)));
}

bool scmp::set_system_volume(double volume)
{
    const auto device = default_output();
    const auto address = output_property(kAudioHardwareServiceDeviceProperty_VirtualMainVolume);
    if (!settable(device, address)) return false;
    Float32 value = static_cast<Float32>(std::max(0.0, std::min(1.0, volume)));
    if (AudioObjectSetPropertyData(device, &address, 0, nullptr, sizeof(value), &value) != noErr) return false;
    const auto mute = output_property(kAudioDevicePropertyMute);
    if (value > 0 && muted(device) && settable(device, mute))
    {
        UInt32 off = 0;
        AudioObjectSetPropertyData(device, &mute, 0, nullptr, sizeof(off), &off);
    }
    return true;
}
