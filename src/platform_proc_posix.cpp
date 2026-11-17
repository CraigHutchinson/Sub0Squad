// POSIX (Linux) backend of platform_proc.hpp: /proc for identity and the tree, readlink for the program's path. The parsers are in platform_proc.cpp.
#include "sub0squad/platform_proc.hpp"

#include <dirent.h>
#include <signal.h>
#include <sys/prctl.h>
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

std::optional<ProcessUsage> processUsage(ProcessId id)
{
    // /proc/PID/stat: utime and stime are fields 14 and 15 (the 12th and 13th after the state, read after the last ')').
    auto stat = slurp("/proc/" + std::to_string(id) + "/stat");
    if (!stat) return std::nullopt;
    const auto close = stat->rfind(')');
    if (close == std::string::npos || close + 2 > stat->size()) return std::nullopt;
    std::istringstream rest(stat->substr(close + 2));
    std::string tok;
    for (int i = 0; i < 11; ++i) rest >> tok;
    long long utime = 0, stime = 0;
    if (!(rest >> utime >> stime)) return std::nullopt;
    ProcessUsage u;
    u.cpuSeconds = static_cast<double>(utime + stime) / static_cast<double>(::sysconf(_SC_CLK_TCK));
    if (auto status = slurp("/proc/" + std::to_string(id) + "/status")) {
        std::uint64_t total = 0;
        bool any = false;
        for (const char* key : {"voluntary_ctxt_switches:", "nonvoluntary_ctxt_switches:"})
            if (auto p = status->find(key); p != std::string::npos) {
                total += std::strtoull(status->c_str() + p + std::char_traits<char>::length(key), nullptr, 10);
                any = true;
            }
        if (any) u.contextSwitches = total;
    }
    return u;
}

std::vector<ProcessId> allProcesses()
{
    std::vector<ProcessId> out;
    if (DIR* d = ::opendir("/proc")) {
        while (const dirent* e = ::readdir(d)) {
            const std::string_view name = e->d_name;
            ProcessId n = 0;
            const auto [end, ec] = std::from_chars(name.data(), name.data() + name.size(), n);
            if (ec == std::errc{} && end == name.data() + name.size() && n > 0) out.push_back(n);
        }
        ::closedir(d);
    }
    return out;
}

std::vector<ProcessId> descendants(ProcessId root)
{
    std::map<ProcessId, std::vector<ProcessId>> children;
    for (const ProcessId n : allProcesses())
        if (auto st = readStat(n)) children[st->parent].push_back(n);
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
bool killProcess(ProcessId id) { return id > 0 && ::kill(static_cast<pid_t>(id), SIGKILL) == 0; }

void becomeSubreaper() { ::prctl(PR_SET_CHILD_SUBREAPER, 1); }
void ignoreBrokenPipes() { ::signal(SIGPIPE, SIG_IGN); }

void useBinaryStandardStreams() {}
void ignoreStopRequests()
{
    ::signal(SIGTERM, SIG_IGN);
    ::signal(SIGINT, SIG_IGN);
}
void setProcessName(const std::string& name) { ::prctl(PR_SET_NAME, name.c_str(), 0, 0, 0); }
void crash()
{
    ::signal(SIGSEGV, SIG_DFL);
    ::raise(SIGSEGV);
    ::_exit(128 + SIGSEGV);   // only when the signal was blocked
}

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
