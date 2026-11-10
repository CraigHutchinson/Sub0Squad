#include "sub0squad/overseer.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace sub0squad {

namespace {

bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

std::set<std::string> failLines(const std::string& g)
{
    std::set<std::string> s;
    std::istringstream in(g);
    for (std::string l; std::getline(in, l);) if (l.rfind("FAIL ", 0) == 0) s.insert(l);
    return s;
}

std::string firstRunnerLine(const std::string& g)
{
    std::istringstream in(g);
    bool seen = false;
    for (std::string l; std::getline(in, l);) {
        if (l.find("The runner reported") != std::string::npos) { seen = true; continue; }
        if (seen && !l.empty()) { const auto b = l.find_first_not_of(' '); return b == std::string::npos ? l : l.substr(b); }
    }
    return {};
}

}  // namespace

OverseerVerdict analyseEscalation(const OverseerInput& in)
{
    OverseerVerdict v;
    v.triage = "owner";
    // 1. An environment fault: the worker never got a chance. Retrying cannot help; the owner needs to know the cause, not to choose.
    for (const char* sign : {"engine call failed", "Connection refused", "cannot connect", "connection to the engine", "No such file or directory: node", "node: command not found"})
        if (has(in.agentNote, sign)) {
            v.triage = "fatal";
            v.analysis = "The engine or the tooling was unreachable (\"" + in.agentNote.substr(0, 160) + "\"). No worker can fix this: start or repair it, then answer retry.";
            return v;
        }
    const std::string last = in.gates.empty() ? std::string{} : in.gates.back();
    const bool repeated = in.gates.size() >= 2 && failLines(last) == failLines(in.gates[in.gates.size() - 2]) && !failLines(last).empty();
    // 2. A test file that fails while loading: one "failed test", and no message in the old output. A top-level read or a bad import.
    if ((has(last, "test(s) ran, at least") && (has(last, "'test failed'") || has(last, "fails while loading"))) || has(last, "SyntaxError") || has(last, "Cannot find module")) {
        const std::string why = firstRunnerLine(last);
        v.triage = "guidance";
        v.analysis = "The file fails while loading, before any test runs" + (why.empty() ? std::string{} : " (" + why + ")") + ".";
        v.guidance = "Your file fails while loading, so no test ran at all" + (why.empty() ? std::string{} : ": " + why) + ". Do not read files or call the code under test at the top level of the file: do every read and call inside the test bodies, and expect a file to be missing (check with existsSync inside the test). Check every import path and every name you import.";
        return v;
    }
    // 3. A tester whose tests pass against the stub: they check nothing real.
    if (has(last, "tests fail against the stub")) {
        v.triage = "guidance";
        v.analysis = "Most of the tests pass against the stub, so they do not exercise the contract.";
        v.guidance = "Too many of your tests pass against the stub, so they test nothing real. The stub does nothing and returns undefined. Make every test assert a specific value or a specific thrown error message that only a correct implementation produces; an error test must check the message, not just that something throws.";
        return v;
    }
    // 4. Files: the wrong ones, or none.
    if (has(last, "is outside the item's editable files")) {
        v.triage = "guidance";
        v.analysis = "The attempt wrote a file it may not write.";
        v.guidance = "You wrote a file outside the files you were given. Write only the files named in the task, each in the form === FILE: path === ... === END FILE ===.";
        return v;
    }
    if (has(last, "wrote nothing") || has(last, "is missing or empty")) {
        v.triage = "guidance";
        v.analysis = "The reply did not contain the required files in the required format.";
        v.guidance = "Reply with the complete content of every file you were asked for, each in exactly this form and nothing else: === FILE: path === then the content then === END FILE ===.";
        return v;
    }
    // 5. The same tests failing for the same reasons after several attempts: a misreading or a wrong test. A decision, not a mechanical cause.
    if (repeated) {
        v.analysis = "The same tests failed with the same messages across attempts: either the contract is being misread or those tests contradict it. Nothing mechanical was found; the manager chooses a strategy (think harder, a stronger engine, a different division). This is not a question for the owner.";
        return v;
    }
    v.analysis = "No mechanical cause was found in the failure evidence (" + std::to_string(in.gates.size()) + " recent gate outputs); the manager chooses a strategy.";
    return v;
}

}  // namespace sub0squad
