#include "Domain/ScmpException.h"

#include <cstdarg>

scmp::ScmpException::ScmpException(const ScmpException &other) noexcept
{
    this->m_message = other.m_message;
}

scmp::ScmpException::ScmpException(const char *format, ...) noexcept
{
    char buffer[1024];

    std::va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    m_message = std::string{buffer};
}

scmp::ScmpException::~ScmpException() noexcept
{
}
