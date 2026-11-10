#pragma once
/// @file harness_web.hpp
/// The `web-static` project harness (docs/SQUAD_UX_AND_HARNESS.md section 4): code that creates the workspace, scaffolding and stubs
/// itself so an agent only writes the deliverable files, plus the gate and the integrator for web projects. Also the mock deliverables
/// (correct and deliberately broken) and the hand-written calculator-web blueprint, which stands in for a frontier architect's plan.

#include <cstddef>
#include <string>
#include <vector>

namespace sub0squad {

/// One file the harness provides itself.
struct HarnessFile { std::string path; std::string content; };

/// Scaffold and stubs of the web-static harness (package.json, serve.mjs, tests/helpers/dom-stub.mjs, stub src/calc.js, src/ui.js, index.html).
[[nodiscard]] const std::vector<HarnessFile>& webScaffold();
/// Total bytes of scaffold and stubs: work the harness did, so an agent did not have to.
[[nodiscard]] std::size_t webScaffoldBytes();
/// Short description of the harness for the fixed system block (what exists, how it is tested).
[[nodiscard]] std::string webHarnessBrief();

/// Mock deliverable for `path` (correct, or with the one defect that makes its item fail its gate); empty if unknown or the defect no longer matches.
[[nodiscard]] std::string webMockFile(const std::string& path, bool buggy);
[[nodiscard]] std::string webMockBugTarget(const std::string& item);
/// What an expert (the overseer, an architect, the owner) needs to know about how the web harness behaves: scaffold, stubs, gates, thresholds, formats.
[[nodiscard]] std::string webHarnessFacts();

/// The calculator-web blueprint as plan-2 JSON (empty for an unknown name).
[[nodiscard]] std::string blueprintJson(const std::string& name);

/// The feedback a worker gets from a `node --test` TAP report: each leaf failure with its message and, when the runner gives them, the expected and
/// actual values and the assertion's file (an empty string when nothing failed).
[[nodiscard]] std::string webFailureExcerpt(const std::string& tapOutput);

/// `harness-gate --workflow-dir D --item I --attempt N`
[[nodiscard]] int runHarnessGate(const std::vector<std::string>& args);
/// `harness-integrate --workflow-dir D`
[[nodiscard]] int runHarnessIntegrate(const std::vector<std::string>& args);

}  // namespace sub0squad
