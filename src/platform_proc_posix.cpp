// POSIX (Linux) backend of platform_proc.hpp: /proc for identity and the tree, readlink for the program's path. The parsers are in platform_proc.cpp.
#include "sub0squad/platform_proc.hpp"

#include <dirent.h>
#include <signal.h>
#include <unistd.h>

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace sub0squad::platform {

namespace {

std::optional<std::string> slurp(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::optional<ProcStat> readStat(ProcessId id)
{
    auto text = slurp("/proc/" + std::to_string(id) + "/stat");
    if (!text) return std::nullopt;
    return parseProcStat(*text);
}

}  // namespace

std::filesystem::path selfExecutable()
{
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    return n > 0 ? std::filesystem::path(std::string(buf, static_cast<std::size_t>(n))) : std::filesystem::path{};
}

ProcessId currentProcess() { return static_cast<ProcessId>(::getpid()); }

std::optional<std::uint64_t> processStart(ProcessId id)
{
    auto st = readStat(id);
    if (!st) return std::nullopt;
    return st->start;
}

bool isAlive(ProcessId id, std::uint64_t expectedStart)
{
    auto st = readStat(id);
    return st && st->state != 'Z' && st->state != 'X' && st->start == expectedStart;
}

std::vector<ProcessId> descendants(ProcessId root)
{
    std::map<ProcessId, std::vector<ProcessId>> children;
    if (DIR* d = ::opendir("/proc")) {
        while (const dirent* e = ::readdir(d)) {
            const std::string_view name = e->d_name;
            ProcessId n = 0;
            const auto [end, ec] = std::from_chars(name.data(), name.data() + name.size(), n);
            if (ec != std::errc{} || end != name.data() + name.size() || n <= 0) continue;
            if (auto st = readStat(n)) children[st->parent].push_back(n);
        }
        ::closedir(d);
    }
    std::vector<ProcessId> out;
    std::vector<ProcessId> work{root};
    while (!work.empty()) {
        const ProcessId p = work.back();
        work.pop_back();
        if (auto it = children.find(p); it != children.end())
            for (ProcessId c : it->second) { out.push_back(c); work.push_back(c); }
    }
    return out;
}

std::optional<std::vector<std::string>> commandLine(ProcessId id)
{
    auto text = slurp("/proc/" + std::to_string(id) + "/cmdline");
    if (!text) return std::nullopt;
    return parseCmdline(*text);
}

bool terminateProcess(ProcessId id) { return id > 0 && ::kill(static_cast<pid_t>(id), SIGTERM) == 0; }

bool isExecutable(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && ::access(path.c_str(), X_OK) == 0;
}

std::filesystem::path findOnPath(const std::string& name)
{
    if (name.find('/') != std::string::npos) return isExecutable(name) ? std::filesystem::path(name) : std::filesystem::path{};
    const char* path = std::getenv("PATH");
    std::istringstream in(path ? path : "");
    for (std::string dir; std::getline(in, dir, ':');) {
        if (dir.empty()) continue;
        const std::filesystem::path p = std::filesystem::path{dir} / name;
        if (isExecutable(p)) return p;
    }
    return {};
}

void setEnvironment(const std::string& name, const std::string& value) { ::setenv(name.c_str(), value.c_str(), 1); }
void unsetEnvironment(const std::string& name) { ::unsetenv(name.c_str()); }

}  // namespace sub0squad::platform
