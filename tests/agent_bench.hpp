#pragma once
// A bench for agent tests: a Python calculator workflow directory (the cheapest real toolchain: python3 only) with the tester's item accepted, so the
// developer item `dev-engine` (one file, calc/engine.py) can be attempted by an agent whose "model" is tests/fake_engine.hpp. The reference engine and its
// one-line defect (modulo computed as a division) come from the harness's own mock templates.
#include "check.hpp"
#include "fake_engine.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_py.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/sandbox.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace bench {

namespace fs = std::filesystem;

inline const char* kFile = "calc/engine.py";
inline const char* kItem = "dev-engine";

inline std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
inline void put(const fs::path& p, const std::string& t) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << t; }
inline std::vector<std::string> lines(const std::string& t) { std::vector<std::string> v; std::istringstream in(t); for (std::string l; std::getline(in, l);) v.push_back(l); return v; }
/// The 1-based number of the first line containing `needle` (0 when absent).
inline int lineOf(const std::string& text, const std::string& needle)
{
    const auto v = lines(text);
    for (std::size_t i = 0; i < v.size(); ++i) if (v[i].find(needle) != std::string::npos) return static_cast<int>(i) + 1;
    return 0;
}
inline std::string reference() { return sub0squad::findHarness("py-unittest")->mockFile(kFile, false); }
inline std::string defective() { return sub0squad::findHarness("py-unittest")->mockFile(kFile, true); }

/// The reference as a DECLARED skeleton (what a primer replies): each function in `names` becomes one `@@ signature` line; `functions` receives each
/// one's reference text. `python` selects the language of the fold detection.
inline std::string skeletonOf(const std::string& ref, const std::vector<std::string>& names, std::map<std::string, std::string>& functions, bool python = true)
{
    auto v = lines(ref);
    const auto regions = sub0squad::foldRegions(ref, python, true);
    for (auto r = regions.rbegin(); r != regions.rend(); ++r) {   // bottom up, so earlier line numbers stay valid
        if (std::find(names.begin(), names.end(), r->name) == names.end() || functions.count(r->name)) continue;
        std::string text;
        for (int k = r->first; k <= r->bodyTo; ++k) text += v[static_cast<std::size_t>(k) - 1] + "\n";
        functions[r->name] = text;
        const std::string sig = v[static_cast<std::size_t>(r->first) - 1];
        const auto ind = sig.find_first_not_of(" \t");
        std::string decl = sig.substr(ind);
        if (python) decl.erase(decl.rfind(':'));
        v.erase(v.begin() + (r->first - 1), v.begin() + r->bodyTo);
        v.insert(v.begin() + (r->first - 1), sig.substr(0, ind) + "@@ " + decl);
    }
    std::string out;
    for (const auto& l : v) out += l + "\n";
    return out;
}
inline std::string skeletonBlock(const std::string& path, const std::string& declared) { return "=== SKELETON: " + path + " ===\n" + declared + "=== END SKELETON ===\n"; }
/// The names of the functions of `ref` worth a section of their own (four lines or more, not a constructor).
inline std::vector<std::string> sectionNames(const std::string& ref, bool python = true)
{
    std::vector<std::string> names;
    for (const auto& r : sub0squad::foldRegions(ref, python, true)) if (!r.name.empty() && r.name.rfind("__", 0) != 0 && r.bodyTo - r.first >= 3) names.push_back(r.name);
    return names;
}

/// A fresh workflow directory with the plan written and `test-engine` accepted.
inline fs::path workflow(const char* tag)
{
    const fs::path d = makeTempDir(tag);
    CHECK_EQ(sub0squad::runMockPlanner({"--task", "calculator-py", "--out", (d / "plan.json").string()}), 0);
    CHECK_EQ(sub0squad::runMockAgent({"--workflow-dir", d.string(), "--item", "test-engine", "--tier", "local", "--attempt", "1", "--delay-ms", "0"}), 0);
    CHECK_EQ(sub0squad::runPyGate({"--workflow-dir", d.string(), "--item", "test-engine", "--attempt", "1"}), 0);
    return d;
}

/// Record a failed earlier attempt of dev-engine holding `text` (its gate is run, so gate.txt is the real failure text).
inline void failedAttempt(const fs::path& d, int attempt, const std::string& text)
{
    put(d / "items" / kItem / ("attempt-" + std::to_string(attempt)) / "out" / kFile, text);
    CHECK(sub0squad::runPyGate({"--workflow-dir", d.string(), "--item", kItem, "--attempt", std::to_string(attempt), "--no-publish", "1"}) != 0);
}

/// The arguments of an `llm-agent` job for dev-engine against a fake engine.
inline std::vector<std::string> agentArgs(const fs::path& d, const fake::Engine& engine, int attempt, int repairTurns = 2, std::vector<std::string> extra = {})
{
    std::vector<std::string> a = {"--workflow-dir", d.string(), "--item", kItem, "--tier", "local", "--attempt", std::to_string(attempt), "--engine", engine.profile(),
                                  "--max-tokens", "3000", "--timeout", "30", "--repair-turns", std::to_string(repairTurns)};
    a.insert(a.end(), extra.begin(), extra.end());
    return a;
}
inline fs::path attemptDir(const fs::path& d, int attempt) { return d / "items" / kItem / ("attempt-" + std::to_string(attempt)); }
inline std::string fileBlock(const std::string& text) { return std::string{"=== FILE: "} + kFile + " ===\n" + text + "=== END FILE ===\n"; }

}  // namespace bench
