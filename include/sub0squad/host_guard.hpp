#pragma once
/// @file host_guard.hpp
/// Explicit resource guards (the host facts come from sub0platform/host.hpp): free memory and free disk are checked BEFORE work is admitted, so a model load or a
/// download refuses with a clear message instead of thrashing the host. This is the M1 stand-in for the arbiter's memory budget (DESIGN.md section 5 (archived: docs/ARCHIVE.md)).

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "sub0platform/lease.hpp"

namespace sub0squad {

struct HostMemory { std::int64_t totalMiB = 0, availableMiB = 0; };
/// Total and available memory (sub0platform::memory) (what can be given to a new process without swapping; page cache counts as available).
[[nodiscard]] HostMemory readHostMemory();
/// Free space of the filesystem holding `path` (its nearest existing parent), in MiB; -1 if unknown.
[[nodiscard]] std::int64_t freeDiskMiB(const std::filesystem::path& path);

/// Refuse when `needMiB + reserveMiB` is more than the available memory. The message names the numbers and the largest resident processes' owner is NOT guessed.
[[nodiscard]] std::expected<void, std::string> requireMemory(std::int64_t needMiB, std::int64_t reserveMiB = 2048);
[[nodiscard]] std::expected<void, std::string> requireDisk(const std::filesystem::path& path, std::int64_t needMiB, std::int64_t reserveMiB = 1024);

/// Attempts admission under the ADR 0005 machine lease without waiting.
/// @param mode How the job may share the machine.
/// @param job What the shim is starting, for the owner record.
/// @return The lease to hold through teardown, or a diagnostic naming the available holder information.
[[nodiscard]] std::expected<sub0platform::MachineLease, std::string> requireMachineLease(sub0platform::LeaseMode mode, std::string_view job);

/// `sub0squad guard [--memory-mib N] [--reserve-mib R] [--disk-path P --disk-mib N]`: print the host facts and exit 1 when a guard refuses.
[[nodiscard]] int runGuard(const std::vector<std::string>& args);
/// `sub0squad fetch-model --url U --out FILE [--sha256 H] [--yes-bytes-over N]`: guarded, resumable, verified download (curl does the transfer
/// because TLS is not built in yet: Sub0Llm #100). For Hugging Face the size and sha256 come from `X-Linked-Size` and `X-Linked-ETag`.
[[nodiscard]] int runFetchModel(const std::vector<std::string>& args);

}  // namespace sub0squad
