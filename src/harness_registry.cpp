#include "sub0squad/harness.hpp"

#include "sub0squad/harness_cpp.hpp"
#include "sub0squad/harness_py.hpp"
#include "sub0squad/harness_web.hpp"

namespace sub0squad {

namespace {

const Harness kWebStatic = {
    "web-static", "harness-gate", "harness-integrate", true,
    &webHarnessBrief, &webHarnessFacts, &webScaffoldBytes, &runHarnessGate, &webMockFile, &webMockBugTarget,
    "node-test", ".test.mjs", "plain ES module, no dependencies", &blueprintJson, nullptr,
};

const Harness kCppCmake = {
    "cpp-cmake", "cpp-gate", "cpp-integrate", true,
    &cppHarnessBrief, &cppHarnessFacts, &cppScaffoldBytes, &runCppGate, &cppMockFile, &cppMockBugTarget,
    "ctest", ".test.cpp", "C++20, standard library only, no warnings", &cppBlueprintJson, &cppClaimsTask,
};

const Harness kPyUnittest = {
    "py-unittest", "py-gate", "py-integrate", true,
    &pyHarnessBrief, &pyHarnessFacts, &pyScaffoldBytes, &runPyGate, &pyMockFile, &pyMockBugTarget,
    "unittest", "_test.py", "Python 3, standard library only", &pyBlueprintJson, &pyClaimsTask,
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
