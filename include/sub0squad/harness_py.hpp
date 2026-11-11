#pragma once
/// @file harness_py.hpp
/// The `py-unittest` project harness (docs/PROVING_GROUND.md section 1, third platform): Python 3, standard library only, tests are `unittest` classes in
/// `tests/NAME_test.py` run by a harness-supplied runner that prints the same TAP report as the other harnesses. Its gate byte-compiles every written file
/// (file, line, message), runs the gate's test files and judges the report with the shared flow; its integrator runs everything in Python's development
/// mode with warnings as errors, runs the app and stores its text frame as the capture.

#include "sub0squad/harness_common.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace sub0squad {

[[nodiscard]] const std::vector<HarnessFile>& pyScaffold();
[[nodiscard]] std::size_t pyScaffoldBytes();
[[nodiscard]] std::string pyHarnessBrief();
[[nodiscard]] std::string pyHarnessFacts();
[[nodiscard]] std::string pyMockFile(const std::string& path, bool buggy);
[[nodiscard]] std::string pyMockBugTarget(const std::string& item);
[[nodiscard]] bool pyClaimsTask(const std::string& lowercaseTask);
[[nodiscard]] std::string pyBlueprintJson(const std::string& name);

/// `py-gate --workflow-dir D --item I --attempt N [--no-publish 1]`
[[nodiscard]] int runPyGate(const std::vector<std::string>& args);
/// `py-integrate --workflow-dir D`
[[nodiscard]] int runPyIntegrate(const std::vector<std::string>& args);

}  // namespace sub0squad
