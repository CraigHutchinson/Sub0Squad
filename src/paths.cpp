#include "sub0squad/paths.hpp"

#include <sys/stat.h>
#include <cstdlib>
#include <system_error>

namespace sub0squad {

RuntimePaths RuntimePaths::fromEnvironment()
{
    namespace fs = std::filesystem;
    RuntimePaths p;
    if (const char* home = std::getenv("SUB0SQUAD_HOME"); home && *home) {
        p.stateDir = fs::path{home} / "state";
        p.runtimeDir = fs::path{home} / "run";
        return p;
    }
    const char* xdgState = std::getenv("XDG_STATE_HOME");
    const char* userHome = std::getenv("HOME");
    fs::path base = (xdgState && *xdgState) ? fs::path{xdgState} : fs::path{userHome ? userHome : "/tmp"} / ".local" / "state";
    p.stateDir = base / "sub0squad";
    const char* xdgRun = std::getenv("XDG_RUNTIME_DIR");
    p.runtimeDir = (xdgRun && *xdgRun) ? fs::path{xdgRun} / "sub0squad" : p.stateDir / "run";
    return p;
}

std::string RuntimePaths::ensureDirectories() const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& d : {stateDir, journalDir(), jobsDir(), runtimeDir}) {
        fs::create_directories(d, ec);
        if (ec) return "cannot create " + d.string() + ": " + ec.message();
    }
    if (::chmod(runtimeDir.c_str(), 0700) != 0) return "cannot chmod " + runtimeDir.string();
    return {};
}

}  // namespace sub0squad
