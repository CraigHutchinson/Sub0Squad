#pragma once
/// @file cli_impl.hpp
/// The command line's private header: what its translation units share. `cli.cpp` is the dispatch and the usage text; `cli_jobs.cpp` the job commands
/// (submit, status, logs, wait, cancel, resume, ping, check, stop); `cli_project.cpp` the workflow commands (do, project, request); `cli_tools.cpp` the
/// commands that are tools around a run (serve, portal, replay, qa, recall, plan-lint). `client.cpp` is only the connection to the daemon.

#include "sub0squad/paths.hpp"

#include <expected>
#include <string>

namespace sub0squad::cli {

/// The usage text printed for an unknown or incomplete command.
extern const char* const kUsage;

/// A daemon reply reduced to what every command needs: whether it is ok, the error if not, and the text.
struct Reply { bool ok = false; std::string error; std::string raw; };
[[nodiscard]] Reply parseReply(const std::string& text);
/// A whole file as a string (empty when it cannot be read).
[[nodiscard]] std::string slurpFile(const std::string& path);

/// Job status from the daemon, or from the journal and the shim records when no daemon answers (`offline` is then true).
[[nodiscard]] std::expected<std::string, std::string> statusAnyway(const RuntimePaths& paths, const std::string& id, bool& offline);
/// Workflow status the same way; empty when neither source has it.
[[nodiscard]] std::string workflowStatusAnyway(const RuntimePaths& paths, const std::string& id, bool& offline);
/// The fields every submit carries: this program's build id, and whether a mismatch with the daemon's is allowed.
[[nodiscard]] std::string submitFields(bool allowBuildMismatch);
/// Take `--allow-build-mismatch` out of a command's arguments; true when it was there.
bool takeAllowMismatch(int& argc, char** argv);
/// One request with an optional id; prints the reply (as JSON when `printJson`) and returns the exit code.
[[nodiscard]] int simpleCommand(const RuntimePaths& paths, const char* cmd, const std::string& id, bool printJson);

// Job commands (cli_jobs.cpp). `argc`/`argv` start after the command word.
[[nodiscard]] int cmdSubmit(const RuntimePaths& paths, int argc, char** argv);
[[nodiscard]] int cmdLogs(const RuntimePaths& paths, int argc, char** argv);
[[nodiscard]] int cmdWait(const RuntimePaths& paths, int argc, char** argv);
/// `status [ID] [--json]`; takes the whole command line.
[[nodiscard]] int cmdStatus(const RuntimePaths& paths, int argc, char** argv);

// Workflow commands (cli_project.cpp). `argc`/`argv` start after the command word.
[[nodiscard]] int cmdDo(const RuntimePaths& paths, int argc, char** argv);
[[nodiscard]] int cmdRequest(const RuntimePaths& paths, int argc, char** argv);
[[nodiscard]] int cmdProject(const RuntimePaths& paths, int argc, char** argv);

// Tools around a run (cli_tools.cpp). These take the whole command line (argv[1] is the command word).
[[nodiscard]] int cmdServe(const RuntimePaths& paths, int argc, char** argv);
[[nodiscard]] int cmdPortal(const RuntimePaths& paths, int argc, char** argv);
[[nodiscard]] int cmdReplay(const RuntimePaths& paths, int argc, char** argv);
[[nodiscard]] int cmdQa(const RuntimePaths& paths, int argc, char** argv);
[[nodiscard]] int cmdRecall(const RuntimePaths& paths, int argc, char** argv);
[[nodiscard]] int cmdPlanLint(const RuntimePaths& paths, int argc, char** argv);

}  // namespace sub0squad::cli
