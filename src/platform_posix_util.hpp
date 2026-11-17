#pragma once
/// @file platform_posix_util.hpp
/// Private to the POSIX backends of the platform library (docs/PLATFORM.md): the error from errno, and how a waited-for child ended.

#include "sub0squad/platform_error.hpp"
#include "sub0squad/platform_process.hpp"

#include <sys/wait.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace sub0squad::platform::posix {

/// A PlatformError for `operation` from errno (or `code`): "cannot <operation>: <the system's reason>".
[[nodiscard]] inline PlatformError sysError(std::string operation, int code = errno)
{
    std::string message = "cannot " + operation + ": " + std::strerror(code);
    return PlatformError{std::move(operation), code, std::move(message)};
}

/// How a child ended, from the status waitpid gave.
[[nodiscard]] inline ExitStatus exitStatusOf(int raw)
{
    ExitStatus s;
    if (WIFEXITED(raw)) s.code = WEXITSTATUS(raw);
    else if (WIFSIGNALED(raw)) s.signal = WTERMSIG(raw);
    return s;
}

}  // namespace sub0squad::platform::posix
