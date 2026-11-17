#pragma once
/// @file bench_platform.hpp
/// The platform-layer pairs of Sub0Squad_Bench (benchmarks/bench_platform.cpp): each primitive beside a hand-written twin of direct system calls.
#include "bench_runner.hpp"

#include <filesystem>

/// The argument that makes the benchmark executable exit at once: the child the spawn pair starts.
inline constexpr const char* kBenchExitNow = "--exit-now";

/// Run the platform pairs. `self` is this executable (started with kBenchExitNow by the spawn pair); `root` is a scratch directory it may fill.
void runPlatformBenchmarks(sub0bench::BenchRunner& runner, const std::filesystem::path& self, const std::filesystem::path& root);
