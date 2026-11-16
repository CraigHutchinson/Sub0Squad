// platform_proc.hpp, the part every operating system shares: the pure parsers of /proc text (tested on fixed input everywhere).
#include "sub0squad/platform_proc.hpp"

#include <sstream>

namespace sub0squad::platform {

std::optional<ProcStat> parseProcStat(std::string_view text)
{
    const auto close = text.rfind(')');
    if (close == std::string_view::npos || close + 2 >= text.size()) return std::nullopt;
    std::istringstream rest{std::string(text.substr(close + 2))};
    // after comm: state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime cutime cstime priority nice threads itrealvalue starttime
    ProcStat st;
    std::string skip;
    if (!(rest >> st.state >> st.parent)) return std::nullopt;
    for (int i = 0; i < 17; ++i)
        if (!(rest >> skip)) return std::nullopt;
    if (!(rest >> st.start)) return std::nullopt;
    return st;
}

std::vector<std::string> parseCmdline(std::string_view text)
{
    std::vector<std::string> args;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto nul = text.find('\0', at);
        const auto end = nul == std::string_view::npos ? text.size() : nul;
        args.emplace_back(text.substr(at, end - at));
        at = end + 1;
    }
    return args;
}

}  // namespace sub0squad::platform
