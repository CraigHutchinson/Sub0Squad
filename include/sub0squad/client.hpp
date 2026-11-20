#pragma once
/// @file client.hpp
/// Blocking client for the control socket, plus the command line (`sub0squad submit|status|logs|cancel|wait|resume|serve|...`).

#include "sub0squad/paths.hpp"
#include "sub0platform/net.hpp"

#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace sub0squad {

/// One connection to the daemon. Requests are JSON objects carrying `"v":1`.
class ControlClient
{
public:
    ControlClient() = default;
    ControlClient(const ControlClient&) = delete;
    ControlClient& operator=(const ControlClient&) = delete;
    ControlClient(ControlClient&&) noexcept = default;
    ControlClient& operator=(ControlClient&&) noexcept = default;
    ~ControlClient() = default;

    struct ConnectError { std::string message; bool noDaemon = false; };
    [[nodiscard]] static std::expected<ControlClient, ConnectError> connect(const RuntimePaths& paths);

    /// Send `{"v":1,"cmd":...,<extra>}` (extra is the inside of a JSON object, or empty) and return the reply frame. A reply that does not come
    /// within the receive timeout fails with a message that starts "timed out" and says the daemon may be hung.
    [[nodiscard]] std::expected<std::string, std::string> request(std::string_view cmd, std::string_view extraFields = {});

    /// The daemon answers every request but `wait` at once, so a reply later than this means it is hung: 30 s, or SUB0SQUAD_REPLY_TIMEOUT seconds.
    [[nodiscard]] static int defaultReceiveSeconds();

    /// Bound every later blocking read to `seconds` (0 = unbounded, for `wait`). Until called, the bound is defaultReceiveSeconds().
    void setReceiveTimeout(int seconds);

private:
    sub0platform::SocketStream stream_;
    std::optional<std::chrono::milliseconds> receiveTimeout_;
};

/// Entry point for every command except the hidden `exec-shim` and `mock-worker`. Returns the process exit code.
[[nodiscard]] int runCli(int argc, char** argv);

}  // namespace sub0squad
