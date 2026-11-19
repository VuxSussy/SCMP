#ifndef SCMP_TERMINAL_TEXT_H
#define SCMP_TERMINAL_TEXT_H

#include "Writer/NcursesWriter.h"
#include <csignal>

namespace scmp
{
void draw_text(WINDOW *window, int row, int column, int width,
               const std::string &text, int style = A_NORMAL, NcursesWriter *writer = nullptr);
bool prompt_text(std::string &value, const char *title, const char *hint,
                 const volatile std::sig_atomic_t &interrupted);
}

#endif
