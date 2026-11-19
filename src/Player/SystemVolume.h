#ifndef SCMP_SYSTEM_VOLUME_H
#define SCMP_SYSTEM_VOLUME_H

namespace scmp
{
// Volume of the default output device from 0 to 1, or a negative value when the device has no
// adjustable volume or the platform does not expose one. A muted device reports 0.
double system_volume();
// Sets the default output device's volume and unmutes it above 0. Returns false if the device
// has no adjustable volume.
bool set_system_volume(double volume);
}

#endif
