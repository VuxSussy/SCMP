#ifndef SCMP_SETTINGS_MENU_H
#define SCMP_SETTINGS_MENU_H

#include <csignal>
#include <string>

namespace scmp
{
// Uses the active ncurses screen and returns true after saving.
bool edit_settings(const std::string &config_path, const volatile std::sig_atomic_t &interrupted);
}

#endif
