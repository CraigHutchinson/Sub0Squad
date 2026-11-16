// Windows backend of platform_host.hpp: GlobalMemoryStatusEx, GetSystemTimes and std::filesystem::space. No load average and, for now, no GPU
// figures (the amdgpu sysfs files have no Windows twin; docs/PLATFORM.md). The parsers of /proc text are in platform_host.cpp.
#include "sub0squad/platform_host.hpp"

#include "platform_win32_util.hpp"

#include <algorithm>

namespace sub0squad::platform {

namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kMiB = 1024 * 1024;

std::int64_t mib(DWORDLONG bytes) { return static_cast<std::int64_t>(bytes / kMiB); }

}  // namespace

MemoryFacts memory()
{
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    if (!::GlobalMemoryStatusEx(&m)) return {};
    // The "page file" figures are the commit limit and what is left of it: physical memory plus the paging files. The paging-file share of
    // the limit stands in for swap.
    const DWORDLONG swapTotal = m.ullTotalPageFile > m.ullTotalPhys ? m.ullTotalPageFile - m.ullTotalPhys : 0;
    return MemoryFacts{.totalMiB = mib(m.ullTotalPhys),
                       .availableMiB = mib(m.ullAvailPhys),
                       .swapTotalMiB = mib(swapTotal),
                       .swapFreeMiB = std::min(mib(swapTotal), mib(m.ullAvailPageFile)),
                       .commitHeadroomMiB = mib(m.ullAvailPageFile)};
}

std::optional<CpuTimes> cpuTimes()
{
    FILETIME idle{}, kernel{}, user{};
    if (!::GetSystemTimes(&idle, &kernel, &user)) return std::nullopt;
    const double total = static_cast<double>(win32::ticks(kernel) + win32::ticks(user));   // kernel time includes idle time
    return CpuTimes{.busy = total - static_cast<double>(win32::ticks(idle)), .total = total};
}

std::optional<double> loadAverage() { return std::nullopt; }

std::optional<std::int64_t> freeDiskMiB(const fs::path& path)
{
    std::error_code ec;
    fs::path p = fs::absolute(path, ec);
    while (!p.empty() && !fs::exists(p, ec)) {
        if (p == p.parent_path()) return std::nullopt;
        p = p.parent_path();
    }
    if (p.empty()) return std::nullopt;
    const auto s = fs::space(p, ec);
    if (ec) return std::nullopt;
    return static_cast<std::int64_t>(s.available / kMiB);
}

std::optional<GpuFacts> gpu() { return std::nullopt; }

}  // namespace sub0squad::platform
