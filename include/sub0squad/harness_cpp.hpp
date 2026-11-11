#pragma once
/// @file harness_cpp.hpp
/// The `cpp-cmake` project harness (docs/PROVING_GROUND.md section 5): a native C++20 project built with CMake and Ninja. The harness supplies the
/// CMake file, the contract header, a tiny TAP-printing test framework and stubs, so an agent writes only deliverable files. Its gate checks the
/// allowlist, then `-fsyntax-only` of every written file, then builds only the gate's test targets and judges the TAP report; its integrator builds the
/// merged project under AddressSanitizer and UBSan, runs every test and the app, and stores the app's text frame as the capture.

#include "sub0squad/harness_common.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace sub0squad {

[[nodiscard]] const std::vector<HarnessFile>& cppScaffold();
[[nodiscard]] std::size_t cppScaffoldBytes();
[[nodiscard]] std::string cppHarnessBrief();
[[nodiscard]] std::string cppHarnessFacts();
/// Mock deliverable for `path` (correct, or with the defect that makes its item fail its gate); empty if unknown or the defect no longer matches.
[[nodiscard]] std::string cppMockFile(const std::string& path, bool buggy);
[[nodiscard]] std::string cppMockBugTarget(const std::string& item);
/// Whether lowercase task text names a native C++ project.
[[nodiscard]] bool cppClaimsTask(const std::string& lowercaseTask);
/// The calculator-cpp blueprint as plan-2 JSON (empty for an unknown name).
[[nodiscard]] std::string cppBlueprintJson(const std::string& name);

/// `cpp-gate --workflow-dir D --item I --attempt N [--no-publish 1]`
[[nodiscard]] int runCppGate(const std::vector<std::string>& args);
/// `cpp-integrate --workflow-dir D`
[[nodiscard]] int runCppIntegrate(const std::vector<std::string>& args);

}  // namespace sub0squad
