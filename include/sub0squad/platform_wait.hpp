#pragma once
/// @file platform_wait.hpp
/// Waiting for several things at once (docs/PLATFORM.md, module platform_wait): sockets becoming readable or writable, processes exiting, a
/// wake-up from another thread, and a request to stop this process, each reported with the caller's tag. POSIX: epoll, pidfd, eventfd and a
/// signalfd for SIGTERM and SIGINT. Windows: one event for every socket (WSAEventSelect) checked with WSAPoll, process handles, an event for the
/// wake-up, and a named event for the stop request (platform_proc's terminateProcess sets it), all in WaitForMultipleObjects (at most 60 processes).

#include "sub0squad/platform_error.hpp"
#include "sub0squad/platform_net.hpp"
#include "sub0squad/platform_process.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <vector>

namespace sub0squad::platform {

/// What a wait saw.
struct WaitEvent
{
    enum class Kind { socket, exited, woken, stopRequested };
    Kind kind = Kind::woken;
    std::uint64_t tag = 0;   ///< the tag the source was watched with
    bool readable = false;   ///< socket: data, a connection to accept, or the end
    bool writable = false;   ///< socket: there is room to write
    bool hangup = false;     ///< socket: the peer is gone or the socket failed
    std::optional<ExitStatus> status{};   ///< exited: how it ended, when the system says (POSIX: only for a child, which the wait reaped)
};

/// One waiter per loop. Move-only. Only wake() may be called from another thread.
class Waiter
{
public:
    Waiter();
    Waiter(Waiter&&) noexcept;
    Waiter& operator=(Waiter&&) noexcept;
    ~Waiter();

    [[nodiscard]] static std::expected<Waiter, PlatformError> create();

    /// Watch a socket (not owned: unwatch it before closing it). Watching it again changes what is watched.
    [[nodiscard]] std::expected<void, PlatformError> watchSocket(NativeSocket socket, bool read, bool write, std::uint64_t tag);
    void unwatchSocket(NativeSocket socket);

    /// Watch a process for its exit (any process this user may see, not only a child; a child is also reaped, its status in the event, so a Child
    /// object of it learns nothing more from wait()). False when it is already gone.
    [[nodiscard]] bool watchExit(ProcessId id, std::uint64_t tag);
    void unwatchExit(ProcessId id);

    /// Report stop requests to this process (POSIX: SIGTERM and SIGINT, which no longer end it; Windows: terminateProcess from another
    /// process, and Ctrl+C or Ctrl+Break on a console).
    [[nodiscard]] std::expected<void, PlatformError> watchStopRequests(std::uint64_t tag);

    /// Make a wait in progress (or the next one) return a `woken` event. Safe from any thread.
    void wake();

    /// Wait until something happens or `timeout` passes (none: as long as it takes); empty when the time passed. Sources stay watched, except a
    /// process that exited.
    [[nodiscard]] std::expected<std::vector<WaitEvent>, PlatformError> wait(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    struct State;   ///< the backend's

private:
    explicit Waiter(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

}  // namespace sub0squad::platform
