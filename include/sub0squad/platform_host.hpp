#pragma once
/// @file platform_host.hpp
/// Facts about the host (docs/PLATFORM.md, module platform_host): memory, processor time, load, free disk and the first GPU. The POSIX backend
/// reads /proc, statvfs and /sys/class/drm; the parsers are pure so they are tested on fixed text.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace sub0squad::platform {

/// Memory in MiB. `availableMiB` is what a new process can be given without swapping (page cache counts as available).
struct MemoryFacts
{
    std::int64_t totalMiB = 0;
    std::int64_t availableMiB = 0;
    std::int64_t swapTotalMiB = 0;
    std::int64_t swapFreeMiB = 0;
    /// How much more may be committed before the system refuses (Linux: CommitLimit - Committed_AS; Windows: the commit limit). Only a hard
    /// limit under strict overcommit on Linux, but a real one on Windows, so it is reported for the guards to read.
    std::optional<std::int64_t> commitHeadroomMiB{};
};
[[nodiscard]] MemoryFacts memory();

/// Cumulative processor time across all processors, in the host's own units: compare two samples, busy over total.
struct CpuTimes
{
    double busy = 0;
    double total = 0;
};
[[nodiscard]] std::optional<CpuTimes> cpuTimes();

/// The one-minute load average; nullopt where the host has none (Windows).
[[nodiscard]] std::optional<double> loadAverage();

/// Free space of the filesystem holding `path` (its nearest existing parent), in MiB; nullopt if unknown.
[[nodiscard]] std::optional<std::int64_t> freeDiskMiB(const std::filesystem::path& path);

/// The first GPU that reports its use (Linux: an amdgpu card under /sys/class/drm). Sizes in MiB.
struct GpuFacts
{
    double busyPercent = 0;
    double vramUsedMiB = 0, vramTotalMiB = 0;
    double gttUsedMiB = 0, gttTotalMiB = 0;
};
[[nodiscard]] std::optional<GpuFacts> gpu();

/// Parse the text of /proc/meminfo (keys in kB); missing keys stay 0, a missing commit pair leaves `commitHeadroomMiB` empty.
[[nodiscard]] MemoryFacts parseMeminfo(std::string_view text);
/// Parse the first ("cpu") line of /proc/stat; busy is everything except idle and iowait.
[[nodiscard]] std::optional<CpuTimes> parseCpuTimes(std::string_view text);

}  // namespace sub0squad::platform
