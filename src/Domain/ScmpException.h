#ifndef _SCMP_SCMP_EXCEPTION_H
#define _SCMP_SCMP_EXCEPTION_H

#include <string>
#include <exception>
#include <stdexcept>

namespace scmp
{

class ScmpException : public std::exception
{
  public:
    explicit ScmpException(const char *format, ...) noexcept;

    ScmpException(const ScmpException &other) noexcept;

    ~ScmpException() noexcept override;

    const char *what() const noexcept override
    {
        return m_message.c_str();
    }

  private:
    std::string m_message;
};
}

#endif
