#pragma once
/// @file daemon.hpp
/// `sub0squad serve`: one reactor thread (control socket, shim pidfds, a wake-up eventfd, signals) that owns the journal and the
/// job store. Each running job is a one-node Sub0Pipeline graph whose deferred job completes when its shim exits, so scheduling
/// stays Sub0Pipeline's and the reactor never blocks. Idle cost is a blocked `epoll_wait`: no timers, no polling.
///
/// Jobs are owned by their shims, not by the daemon: stopping or killing the daemon leaves them running, and the next daemon
/// adopts them (or marks them `lost` if the shim is gone without an outcome).

#include "sub0squad/paths.hpp"
#include "sub0squad/policy.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace sub0squad {

struct DaemonOptions
{
    RuntimePaths paths;
    std::filesystem::path exe;  ///< the multi-call sub0squad binary used to start shims (normally this process)
    int maxJobs = 2;            ///< the only M1 concurrency limit; replaced by arbiter leases in M2
    int memShedMiB = 1536;      ///< below this much available memory the daemon sheds work (newest job first, servers last) and admits nothing new; 0 = off
    FrontierMode frontier = FrontierMode::stub;  ///< owner setting: `off` forbids escalation, `stub` runs it without sending anything
};

class Daemon
{
public:
    /// Take the instance lock, replay the journal, reconcile unfinished jobs against the disk, bind the control socket.
    [[nodiscard]] static std::expected<std::unique_ptr<Daemon>, std::string> create(DaemonOptions options);
    ~Daemon();
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    /// Run until SIGTERM/SIGINT or a `shutdown` request. Leaves running jobs running. Returns the process exit code.
    [[nodiscard]] int run();

private:
    struct Impl;
    explicit Daemon(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace sub0squad
