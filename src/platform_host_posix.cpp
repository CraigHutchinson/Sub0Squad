// POSIX (Linux) backend of platform_host.hpp: /proc/meminfo, /proc/stat, /proc/loadavg, statvfs and /sys/class/drm. The parsers are in platform_host.cpp.
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
