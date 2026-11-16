#pragma once
/// @file platform_error.hpp
/// The one error type of the platform library (docs/PLATFORM.md): what was attempted, on what, and why it failed, so no caller reads errno or a
/// Windows error code itself.

#include <string>

namespace sub0squad::platform {

/// A failed operating-system operation.
struct PlatformError
{
    std::string operation{};   ///< what was attempted, in words ("start 'g++'")
    int code = 0;              ///< the native error code (errno on POSIX), 0 when there is none
    std::string message{};     ///< the whole sentence for a person: the operation, the object and the system's reason

    /// The message, for printing.
    [[nodiscard]] const std::string& what() const { return message; }
};

}  // namespace sub0squad::platform
