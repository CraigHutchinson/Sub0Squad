// POSIX (Linux) backend of platform_host.hpp: /proc/meminfo, /proc/stat, /proc/loadavg, statvfs and /sys/class/drm.
#include "sub0squad/platform_host.hpp"

#include <sys/statvfs.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace sub0squad::platform {

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream o;
    o << in.rdbuf();
    return o.str();
}

double sysfsNumber(const fs::path& p)
{
    const std::string t = slurp(p);
    return t.empty() ? -1.0 : std::atof(t.c_str());
}

}  // namespace

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

MemoryFacts memory() { return parseMeminfo(slurp("/proc/meminfo")); }

std::optional<CpuTimes> cpuTimes() { return parseCpuTimes(slurp("/proc/stat")); }

std::optional<double> loadAverage()
{
    const std::string t = slurp("/proc/loadavg");
    if (t.empty()) return std::nullopt;
    return std::atof(t.c_str());
}

std::optional<std::int64_t> freeDiskMiB(const fs::path& path)
{
    std::error_code ec;
    fs::path p = fs::absolute(path, ec);
    while (!p.empty() && !fs::exists(p, ec)) {
        if (p == p.parent_path()) return std::nullopt;
        p = p.parent_path();
    }
    struct statvfs sv{};
    if (p.empty() || ::statvfs(p.c_str(), &sv) != 0) return std::nullopt;
    return static_cast<std::int64_t>(sv.f_bavail) * static_cast<std::int64_t>(sv.f_frsize) / (1024 * 1024);
}

std::optional<GpuFacts> gpu()
{
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/sys/class/drm", ec)) {
        const fs::path d = e.path() / "device";
        if (e.path().filename().string().find('-') != std::string::npos || !fs::exists(d / "gpu_busy_percent", ec)) continue;
        return GpuFacts{.busyPercent = sysfsNumber(d / "gpu_busy_percent"),
                        .vramUsedMiB = sysfsNumber(d / "mem_info_vram_used") / 1048576.0,
                        .vramTotalMiB = sysfsNumber(d / "mem_info_vram_total") / 1048576.0,
                        .gttUsedMiB = sysfsNumber(d / "mem_info_gtt_used") / 1048576.0,
                        .gttTotalMiB = sysfsNumber(d / "mem_info_gtt_total") / 1048576.0};
    }
    return std::nullopt;
}

}  // namespace sub0squad::platform
