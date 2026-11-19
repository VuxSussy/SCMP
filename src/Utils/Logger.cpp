#include <cstdarg>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include "Utils/Logger.h"
#include "Utils/Utils.h"

namespace
{
const static uint64_t k_max_log_line = 4096;
FILE *log_file = nullptr;
const static std::vector<const char *> log_level_names{"DEBUG", "INFO", "WARN",
                                                       "ERROR"};
}

scmp::Logger::Logger()
{
}

void scmp::Logger::initialize(const std::string log_location)
{
    std::string log_path{Utils::get_home_directory()};
    log_path.append(log_location);

    log_file = std::fopen(log_path.c_str(), "w");
}

void scmp::Logger::uninitialize()
{
    if (log_file)
    {
        std::fclose(log_file);
        log_file = nullptr;
    }
}

void scmp::Logger::log(scmp::LogLevel level, const char *file, uint16_t line,
                      const char *format, ...)
{
    if (!log_file)
    {
        return;
    }
    char message_text_buf[k_max_log_line];
    std::va_list args;
    va_start(args, format);
    vsnprintf(message_text_buf, k_max_log_line, format, args);
    fprintf(
        log_file, "%s:%d %s %s\n", file, line,
        log_level_names[std::min(static_cast<uint8_t>(level),
                                 static_cast<uint8_t>(scmp::LogLevel::ERROR))],
        message_text_buf);
}

scmp::Logger::~Logger()
{
}
