#include "sub0squad/harness.hpp"

#include "sub0squad/harness_web.hpp"

namespace sub0squad {

namespace {

const Harness kWebStatic = {
    "web-static", "harness-gate", "harness-integrate", true,
    &webHarnessBrief, &webHarnessFacts, &webScaffoldBytes, &runHarnessGate, &webMockFile, &webMockBugTarget,
};

const Harness* const kAll[] = {&kWebStatic};

}  // namespace

const Harness* findHarness(std::string_view name)
{
    for (const Harness* h : kAll) if (name == h->name) return h;
    return nullptr;
}

std::vector<std::string> harnessNames()
{
    std::vector<std::string> out;
    for (const Harness* h : kAll) out.push_back(h->name);
    return out;
}

}  // namespace sub0squad
