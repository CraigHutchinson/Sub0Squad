#include "sub0squad/process_tree.hpp"

#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace sub0squad::proc {

namespace {
struct Stat { pid_t ppid = 0; char state = '?'; std::uint64_t start = 0; };

std::optional<Stat> readStat(pid_t pid)
{
    std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string s = ss.str();
    const auto close = s.rfind(')');  // comm may contain spaces and parentheses: parse after the last ')'
    if (close == std::string::npos || close + 2 >= s.size()) return std::nullopt;
    std::istringstream rest(s.substr(close + 2));
    Stat st;
    std::string tok;
    // fields after comm: state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime cutime cstime priority nice threads itrealvalue starttime
    rest >> st.state >> st.ppid;
    for (int i = 0; i < 17 && (rest >> tok); ++i) {}
    rest >> st.start;
    return st;
}
}  // namespace

std::optional<std::uint64_t> startTicks(pid_t pid)
{
    auto st = readStat(pid);
    if (!st) return std::nullopt;
    return st->start;
}

bool isAlive(pid_t pid, std::uint64_t expectedStartTicks)
{
    auto st = readStat(pid);
    return st && st->state != 'Z' && st->state != 'X' && st->start == expectedStartTicks;
}

std::vector<pid_t> descendantsOf(pid_t root)
{
    std::map<pid_t, std::vector<pid_t>> children;
    if (DIR* d = ::opendir("/proc")) {
        while (const dirent* e = ::readdir(d)) {
            char* end = nullptr;
            const long n = std::strtol(e->d_name, &end, 10);
            if (*end != '\0' || n <= 0) continue;
            if (auto st = readStat(static_cast<pid_t>(n))) children[st->ppid].push_back(static_cast<pid_t>(n));
        }
        ::closedir(d);
    }
    std::vector<pid_t> out;
    std::vector<pid_t> work{root};
    while (!work.empty()) {
        const pid_t p = work.back();
        work.pop_back();
        if (auto it = children.find(p); it != children.end())
            for (pid_t c : it->second) { out.push_back(c); work.push_back(c); }
    }
    return out;
}

}  // namespace sub0squad::proc
