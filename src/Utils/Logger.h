#ifndef _SCMP_LOGGER_H
#define _SCMP_LOGGER_H

#include "Domain/Settings.h"

/**
 * SCMP_LOG is defined as a macro to force the log level check to happen at
 * compile time so that the expression is not evaluated if the log level
 * is turned off. For example,
 *      SCMP_LOG( LogLevel::DEBUG,
 *          some_really_expensive_debug_function() )
 * In this case the, if the log level is ERROR, then the function will not be
 * called.
 */
#define SCMP_LOG(log_level, message, args...)                                   \
    {                                                                          \
        if (scmp::Logger::level() <= (log_level))                               \
        {                                                                      \
            scmp::Logger::log(log_level, __FILE__, __LINE__, message, ##args);  \
        }                                                                      \
    }

namespace scmp
{

enum class LogLevel
{
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger
{
  public:
    static void log(scmp::LogLevel level, const char *file, uint16_t line,
                    const char *format, ...);

    constexpr static scmp::LogLevel level()
    {
#ifdef SCMP_LOG_DEBUG
        return LogLevel::DEBUG;
#endif
        return LogLevel::ERROR;
    }

    static void initialize(const std::string log_location);

    static void uninitialize();

  private:
    explicit Logger();

    virtual ~Logger();
};
}

#endif
