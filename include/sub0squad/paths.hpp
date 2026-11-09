#pragma once
/// @file paths.hpp
/// Where the daemon keeps its state and its control socket. Linux backend of the future IRuntimePaths seam
/// (docs/DESIGN.md section 20); nothing else in the tree decides a location.

#include <filesystem>
#include <string>

namespace sub0squad {

/// All locations derived from one root. `SUB0SQUAD_HOME` (tests, side-by-side instances) puts everything under one directory;
/// otherwise state follows `XDG_STATE_HOME` and the control socket `XDG_RUNTIME_DIR`.
struct RuntimePaths
{
    std::filesystem::path stateDir;    ///< journal, jobs, ledger
    std::filesystem::path runtimeDir;  ///< control socket, instance lock (0700)

    /// Resolve from the environment. Never creates anything.
    [[nodiscard]] static RuntimePaths fromEnvironment();

    [[nodiscard]] std::filesystem::path controlSocket() const { return runtimeDir / "control.sock"; }
    [[nodiscard]] std::filesystem::path instanceLock() const { return runtimeDir / "daemon.lock"; }
    [[nodiscard]] std::filesystem::path ledgerFile() const { return stateDir / "ledger.ndjson"; }
    [[nodiscard]] std::filesystem::path workflowsDir() const { return stateDir / "workflows"; }
    [[nodiscard]] std::filesystem::path journalDir() const { return stateDir / "journal"; }
    [[nodiscard]] std::filesystem::path jobsDir() const { return stateDir / "jobs"; }
    [[nodiscard]] std::filesystem::path jobDir(const std::string& id) const { return jobsDir() / id; }
    [[nodiscard]] std::filesystem::path runDir(const std::string& id, int run) const
    {
        return jobDir(id) / ("run-" + std::to_string(run));
    }

    /// Create the directories (runtimeDir with mode 0700). Returns an error text, empty on success.
    [[nodiscard]] std::string ensureDirectories() const;
};

}  // namespace sub0squad
