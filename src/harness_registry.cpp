#include "sub0squad/harness.hpp"

#include "sub0squad/harness_cpp.hpp"
#include "sub0squad/harness_py.hpp"
#include "sub0squad/harness_web.hpp"

namespace sub0squad {

namespace {

template <const std::vector<HarnessFile>& (*Scaffold)()>
std::string fileOf(const std::string& path) { for (const auto& f : Scaffold()) if (f.path == path) return f.content; return {}; }
template <const std::vector<HarnessFile>& (*Scaffold)()>
std::vector<std::string> pathsOf() { std::vector<std::string> v; for (const auto& f : Scaffold()) v.push_back(f.path); return v; }

const Harness kWebStatic = {
    "web-static", "harness-gate", "harness-integrate",
    &webHarnessBrief, &webHarnessFacts, &webScaffoldBytes, &runHarnessGate, &webMockFile, &webMockBugTarget,
    "node-test", ".test.mjs", "plain ES module, no dependencies", &blueprintJson, nullptr, &runWebPrecheck, false,
    &fileOf<&webScaffold>, &pathsOf<&webScaffold>,
};

const Harness kCppCmake = {
    "cpp-cmake", "cpp-gate", "cpp-integrate",
    &cppHarnessBrief, &cppHarnessFacts, &cppScaffoldBytes, &runCppGate, &cppMockFile, &cppMockBugTarget,
    "ctest", ".test.cpp", "C++20, standard library only, no warnings", &cppBlueprintJson, &cppClaimsTask, &runCppPrecheck, true,
    &fileOf<&cppScaffold>, &pathsOf<&cppScaffold>,
};

const Harness kPyUnittest = {
    "py-unittest", "py-gate", "py-integrate",
    &pyHarnessBrief, &pyHarnessFacts, &pyScaffoldBytes, &runPyGate, &pyMockFile, &pyMockBugTarget,
    "unittest", "_test.py", "Python 3, standard library only", &pyBlueprintJson, &pyClaimsTask, &runPyPrecheck, false,
    &fileOf<&pyScaffold>, &pathsOf<&pyScaffold>,
};

const Harness* const kAll[] = {&kWebStatic, &kCppCmake, &kPyUnittest};

}  // namespace

const Harness* findHarness(std::string_view name)
{
    for (const Harness* h : kAll) if (name == h->name) return h;
    return nullptr;
}

bool knownGateKind(std::string_view kind)
{
    for (const Harness* h : kAll) if (kind == h->gateKind) return true;
    return false;
}

const Harness* harnessWithBlueprint(const std::string& name)
{
    for (const Harness* h : kAll) if (h->blueprint && !h->blueprint(name).empty()) return h;
    return nullptr;
}

std::vector<std::string> harnessNames()
{
    std::vector<std::string> out;
    for (const Harness* h : kAll) out.push_back(h->name);
    return out;
}

}  // namespace sub0squad
