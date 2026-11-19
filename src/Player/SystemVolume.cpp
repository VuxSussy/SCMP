#include "Player/SystemVolume.h"

#ifndef _OS_OSX
// Linux keeps the player's own gain. MacSystemVolume.mm implements these on macOS.
double scmp::system_volume()
{
    return -1;
}

bool scmp::set_system_volume(double)
{
    return false;
}
#endif
