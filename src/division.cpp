#include "sub0squad/division.hpp"

#include <algorithm>
#include <set>

namespace sub0squad {

std::vector<std::string> completeDivision(Plan& fragment, const WorkItem& parent)
{
    std::set<std::string> covered;
    for (const auto& f : fragment.items) for (const auto& file : f.files) covered.insert(file);
    std::vector<std::string> added;
    for (const auto& file : parent.files) {
        if (covered.count(file) || file.rfind("tests/", 0) == 0) continue;
        const auto slash = file.rfind('/'), dot = file.rfind('.');
        const std::string stem = file.substr(slash == std::string::npos ? 0 : slash + 1, dot == std::string::npos || dot < slash ? std::string::npos : dot - (slash == std::string::npos ? 0 : slash + 1));
        const WorkItem* tester = nullptr;
        for (const auto& f : fragment.items)
            if (f.persona == "tester") for (const auto& t : f.files) if (t == "tests/" + stem + ".test.mjs") tester = &f;
        if (!tester) continue;
        WorkItem d;
        d.id = "impl-" + stem;
        while (std::any_of(fragment.items.begin(), fragment.items.end(), [&](const WorkItem& x) { return x.id == d.id; })) d.id += "x";
        d.title = "Implement " + file;
        d.persona = "developer";
        d.goal = "Implement " + file + " so that the provided tests pass";
        d.files = {file};
        d.deps = {tester->id};
        d.complexity = 2;
        d.card.context = "Part of the division of '" + parent.id + "': it delivers " + file + ". The tests below are its specification.";
        d.card.contract = "Implement " + file + " so that every test in " + tester->files.front() + " passes: the tests define its exports, signatures and behaviour. Plain ES module, no dependencies.";
        d.card.acceptance = {"every test in " + tester->files.front() + " passes", "plain ES module, no dependencies"};
        d.card.include = {tester->files.front()};
        d.card.notes = {"Output only " + file};
        d.gate.kind = "node-test";
        d.gate.tests = {tester->files.front()};
        d.gate.minTests = std::max(3, tester->gate.minTests);
        d.gate.mustFailOnStub = false;
        const std::string testerId = tester->id, newId = d.id, testFile = tester->files.front();   // copies: pushing below may move the vector's storage
        for (auto& f : fragment.items)
            if (f.persona == "developer" && std::find(f.deps.begin(), f.deps.end(), testerId) != f.deps.end() && std::find(f.deps.begin(), f.deps.end(), newId) == f.deps.end()) f.deps.push_back(newId);
        fragment.items.push_back(std::move(d));
        added.push_back(file + " (part '" + newId + "', graded by " + testFile + ")");
    }
    return added;
}

}  // namespace sub0squad
