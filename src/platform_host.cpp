// platform_host.hpp, the part every operating system shares: the pure parsers of /proc text (tested on fixed input everywhere).
#include "sub0squad/platform_host.hpp"

#include <cstdlib>
#include <map>
#include <sstream>
#include <string>

namespace sub0squad::platform {

MemoryFacts parseMeminfo(std::string_view text)
{
    std::map<std::string, long long, std::less<>> kb;
    std::istringstream in{std::string(text)};
    for (std::string line; std::getline(in, line);) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        kb[line.substr(0, colon)] = std::atoll(line.c_str() + colon + 1);
    }
    const auto mib = [&](std::string_view key) -> std::int64_t { auto it = kb.find(key); return it == kb.end() ? 0 : it->second / 1024; };
    MemoryFacts m{.totalMiB = mib("MemTotal"), .availableMiB = mib("MemAvailable"), .swapTotalMiB = mib("SwapTotal"), .swapFreeMiB = mib("SwapFree")};
    if (kb.contains("CommitLimit") && kb.contains("Committed_AS")) m.commitHeadroomMiB = mib("CommitLimit") - mib("Committed_AS");
    return m;
}

std::optional<CpuTimes> parseCpuTimes(std::string_view text)
{
    std::istringstream in{std::string(text)};
    std::string tag;
    double user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    if (!(in >> tag >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal) || tag != "cpu") return std::nullopt;
    const double total = user + nice + sys + idle + iowait + irq + softirq + steal;
    return CpuTimes{.busy = total - idle - iowait, .total = total};
}

}  // namespace sub0squad::platform
