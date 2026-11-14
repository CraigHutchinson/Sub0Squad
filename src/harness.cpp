#include "sub0squad/harness.hpp"

namespace sub0squad {

namespace {

/// The registered entries, in registration order (the order is the order of `harnessNames()` and of the blueprint search).
std::vector<const Harness*>& entries()
{
    static std::vector<const Harness*> all;
    return all;
}

}  // namespace

void registerHarness(const Harness& harness)
{
    for (const Harness* h : entries()) if (std::string_view{h->name} == harness.name) return;
    entries().push_back(&harness);
}

const Harness* findHarness(std::string_view name)
{
    for (const Harness* h : entries()) if (name == h->name) return h;
    return nullptr;
}

bool knownGateKind(std::string_view kind)
{
    for (const Harness* h : entries()) if (kind == h->gateKind) return true;
    return false;
}

const Harness* harnessWithBlueprint(const std::string& name)
{
    for (const Harness* h : entries()) if (h->blueprint && !h->blueprint(name).empty()) return h;
    return nullptr;
}

std::vector<std::string> harnessNames()
{
    std::vector<std::string> out;
    for (const Harness* h : entries()) out.push_back(h->name);
    return out;
}

}  // namespace sub0squad
