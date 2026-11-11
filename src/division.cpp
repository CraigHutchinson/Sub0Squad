#include "sub0squad/division.hpp"

#include <algorithm>
#include <set>

#include "sub0squad/harness.hpp"

namespace sub0squad {

std::vector<std::string> completeDivision(Plan& fragment, const WorkItem& parent)
{
    const Harness* harness = findHarness(fragment.harness);
    const std::string gateKind = harness ? harness->gateKind : "node-test", testSuffix = harness ? harness->testSuffix : ".test.mjs", style = harness ? harness->deliverableStyle : "plain ES module, no dependencies";
    std::set<std::string> covered;
    for (const auto& f : fragment.items) for (const auto& file : f.files) covered.insert(file);
    std::vector<std::string> added;
    for (const auto& file : parent.files) {
        if (covered.count(file) || file.rfind("tests/", 0) == 0) continue;
        const auto slash = file.rfind('/'), dot = file.rfind('.');
        const std::string stem = file.substr(slash == std::string::npos ? 0 : slash + 1, dot == std::string::npos || dot < slash ? std::string::npos : dot - (slash == std::string::npos ? 0 : slash + 1));
        const WorkItem* tester = nullptr;
        for (const auto& f : fragment.items)
            if (f.persona == "tester") for (const auto& t : f.files) if (t == "tests/" + stem + testSuffix) tester = &f;
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
        d.card.contract = "Implement " + file + " so that every test in " + tester->files.front() + " passes: the tests define its exports, signatures and behaviour. " + style + ".";
        d.card.acceptance = {"every test in " + tester->files.front() + " passes", style};
        d.card.include = {tester->files.front()};
        d.card.notes = {"Output only " + file};
        d.gate.kind = gateKind;
        d.gate.tests = {tester->files.front()};
        d.gate.minTests = std::max(3, tester->gate.minTests);
        d.gate.mustFailOnStub = false;
        const std::string testerId = tester->id, newId = d.id, testFile = tester->files.front();   // copies: pushing below may move the vector's storage
        for (auto& f : fragment.items)
            if (f.persona == "developer" && std::find(f.deps.begin(), f.deps.end(), testerId) != f.deps.end() && std::find(f.deps.begin(), f.deps.end(), newId) == f.deps.end()) f.deps.push_back(newId);
        fragment.items.push_back(std::move(d));
        added.push_back(file + " (part '" + newId + "', graded by " + testFile + ")");
    }
    // The opposite gap (the first real 9B run, 'ui-js.ui-dom'): a developer part graded by a test file that no item writes, so its gate can never run.
    // Give it a tester of its own, built from the part's own card; the part then waits for it.
    std::set<std::string> written, inherited(parent.gate.tests.begin(), parent.gate.tests.end());
    for (const auto& f : fragment.items) for (const auto& file : f.files) written.insert(file);
    const std::size_t count = fragment.items.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (fragment.items[i].persona != "developer") continue;
        const std::vector<std::string> wanted = fragment.items[i].gate.tests;   // a copy: the vector below grows
        for (const std::string& testFile : wanted) {
            if (written.count(testFile) || inherited.count(testFile)) continue;
            const WorkItem dev = fragment.items[i];   // copy: pushing below may move the vector's storage
            WorkItem t;
            t.id = dev.id + "-tests";
            while (std::any_of(fragment.items.begin(), fragment.items.end(), [&](const WorkItem& x) { return x.id == t.id; })) t.id += "x";
            t.title = "Tests for " + dev.title;
            t.persona = "tester";
            t.goal = "Write the tests in " + testFile + " that specify: " + dev.goal;
            t.files = {testFile};
            t.deps = dev.deps;
            t.complexity = 2;
            t.card.context = "Tests for the part '" + dev.id + "' of the division of '" + parent.id + "'. Write only " + testFile + ".";
            t.card.contract = dev.card.contract;
            t.card.acceptance = {"at least 4 focused tests of the contract in " + testFile, "the tests fail against the stub"};
            t.card.notes = {"Output only " + testFile};
            t.gate.kind = gateKind;
            t.gate.tests = {testFile};
            t.gate.minTests = 4;
            t.gate.mustFailOnStub = true;
            const std::string newId = t.id;
            fragment.items[i].deps.push_back(newId);
            fragment.items.push_back(std::move(t));
            written.insert(testFile);
            added.push_back("tester '" + newId + "' for " + testFile + " (graded part '" + dev.id + "')");
        }
    }
    return added;
}

}  // namespace sub0squad
