#include "cmd_archive.hpp"

#include "sub0squad/client.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace sub0squad {

namespace fs = std::filesystem;

int runArchiveCommand(const std::vector<std::string>& args, const RuntimePaths& paths)
{
    std::string label;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--label" && i + 1 < args.size()) label = args[++i];
        else { std::fprintf(stderr, "archive [--label TEXT]: unknown option '%s'\n", args[i].c_str()); return 2; }
    }
    for (const char c : label)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) { std::fprintf(stderr, "archive: a label is letters, digits, '-' and '_' (it becomes part of a directory name)\n"); return 2; }
    if (label.size() > 40) { std::fprintf(stderr, "archive: a label is at most 40 characters\n"); return 2; }
    if (auto client = ControlClient::connect(paths)) {
        std::fprintf(stderr, "archive: the daemon is running and has the journal open: stop it first (`sub0squad down`), then archive\n");
        return 1;
    }
    std::error_code ec;
    if (!fs::is_directory(paths.stateDir, ec) || fs::is_empty(paths.stateDir, ec)) { std::printf("nothing to archive: %s is empty\n", paths.stateDir.c_str()); return 0; }
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    ::gmtime_r(&now, &utc);
    std::strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%SZ", &utc);
    const fs::path root = paths.stateDir.parent_path() / "archive";
    fs::path target = root / (std::string{stamp} + (label.empty() ? "" : "-" + label));
    for (int n = 2; fs::exists(target, ec) && n < 100; ++n) target = root / (std::string{stamp} + (label.empty() ? "" : "-" + label) + "-" + std::to_string(n));   // two archives in one second
    fs::create_directories(target, ec);
    if (ec) { std::fprintf(stderr, "archive: cannot create %s: %s\n", target.c_str(), ec.message().c_str()); return 1; }
    fs::rename(paths.stateDir, target / "state", ec);
    if (ec) { std::fprintf(stderr, "archive: cannot move %s to %s: %s (nothing was changed)\n", paths.stateDir.c_str(), (target / "state").c_str(), ec.message().c_str()); fs::remove(target, ec); return 1; }
    fs::create_directories(paths.stateDir, ec);
    if (ec) { std::fprintf(stderr, "archive: moved the state to %s but cannot create a new %s: %s\n", target.c_str(), paths.stateDir.c_str(), ec.message().c_str()); return 1; }
    const fs::path processes = target / "state" / "processes";
    if (fs::is_directory(processes, ec)) {
        fs::copy(processes, paths.stateDir / "processes", fs::copy_options::recursive, ec);
        if (ec) std::fprintf(stderr, "archive: could not carry the registered processes over (%s); they are in %s\n", ec.message().c_str(), processes.c_str());
    }
    std::printf("archived to %s\n%s is new and empty%s\n", target.c_str(), paths.stateDir.c_str(), fs::is_directory(paths.stateDir / "processes", ec) ? " (your registered processes were kept)" : "");
    return 0;
}

}  // namespace sub0squad
