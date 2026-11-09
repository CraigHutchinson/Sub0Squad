#pragma once
/// @file client.hpp
/// Blocking client for the control socket, plus the command line (`sub0squad submit|status|logs|cancel|wait|resume|serve|...`).

#include "sub0squad/paths.hpp"

#include <expected>
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
    ControlClient(ControlClient&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    ~ControlClient();

    struct ConnectError { std::string message; bool noDaemon = false; };
    [[nodiscard]] static std::expected<ControlClient, ConnectError> connect(const RuntimePaths& paths);

    /// Send `{"v":1,"cmd":...,<extra>}` (extra is the inside of a JSON object, or empty) and return the reply frame.
    [[nodiscard]] std::expected<std::string, std::string> request(std::string_view cmd, std::string_view extraFields = {});

    /// Bound every later blocking read to `seconds` (0 = unbounded). A timed-out request fails with "timed out".
    void setReceiveTimeout(int seconds);

private:
    int fd_ = -1;
};

/// Entry point for every command except the hidden `exec-shim` and `mock-worker`. Returns the process exit code.
[[nodiscard]] int runCli(int argc, char** argv);

}  // namespace sub0squad
