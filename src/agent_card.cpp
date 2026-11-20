// Shared parts of the model-driven workers (docs/COMPONENTS.md cleanup step 4): the fixed system block, the work card, the numbered draft view and the
// parser of whole-file replies. The attempt loop is in llm_agent.cpp, the canvas loop in canvas_agent.cpp, forked sessions in fork_agent.cpp.
#include "agent_impl.hpp"

#include "sub0squad/harness.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/stance.hpp"
#include "sub0platform/file.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

namespace sub0squad::agent {

namespace fs = std::filesystem;

std::string harnessBrief(const Plan& plan)
{
    const Harness* h = findHarness(plan.harness);
    if (!h) return {};
    // A plan that carries its own interface is told what the harness provides without any project's files named (the default brief names the calculator's).
    if (!plan.scaffold.empty() && h->ownScaffoldBrief) return h->ownScaffoldBrief();
    return h->brief ? h->brief() : std::string{};
}

std::string slurp(const fs::path& p) { return sub0platform::readFileText(p); }
bool writeFile(const fs::path& p, const std::string& text) { return sub0platform::writeFile(p, text).has_value(); }
std::string arg(const std::vector<std::string>& a, const std::string& key, const std::string& d)
{
    for (std::size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == "--" + key) return a[i + 1];
    return d;
}
std::string clip(const std::string& s, std::size_t n) { return s.size() <= n ? s : s.substr(0, n) + "\n... (truncated)"; }

double deliverableTokens(const EngineProfile& engine, const fs::path& out, double tokensOut)
{
    std::string text;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(out, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) text += slurp(it->path());
    if (text.empty()) return 0.0;
    bool estimated = false;
    return std::min(tokensOut, static_cast<double>(countTokens(engine, text, estimated)));
}

// An earlier draft as the worker sees it: every line numbered (so an edit can name lines), whole when it fits, else FOLDED: signatures and structure visible,
// long function bodies collapsed to one line that names the folded range, the regions around the lines the gate named left open (src/outline.cpp). The worker
// asks for folded lines with a READ request. Never cut off in the middle of code: a worker cannot edit what it cannot see.
std::string draftView(const std::string& path, const std::string& text, const std::string& gate, std::size_t budget)
{
    std::vector<int> focus;
    static const std::regex ref(R"([\w./+-]+\.[a-z]+:(\d+))");
    for (std::sregex_iterator it(gate.begin(), gate.end(), ref), end; it != end; ++it) focus.push_back(std::atoi((*it)[1].str().c_str()));
    // A failing test names no line, only itself (`FAIL 13 - trailing_operator_throws`): its definition in this file is a focus too, so a tester sees the
    // tests it must correct (tier 0 write-tests).
    static const std::regex failing(R"(FAIL \d+ - (\w+))");
    for (std::sregex_iterator it(gate.begin(), gate.end(), failing), end; it != end; ++it) {
        const std::string name = (*it)[1].str();
        for (const std::string& def : {"TEST(" + name + ")", "def " + name + "("}) {
            const auto at = text.find(def);
            // The line after the definition's own: inside its body, not also "just past the end" of the test before it.
            if (at != std::string::npos) { focus.push_back(2 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n'))); break; }
        }
    }
    return foldedView(text, isPythonPath(path), focus, budget);
}


std::string systemBlock(const std::string& persona, const std::string& brief, const std::string& stance)
{
    // Persona-neutral text first (the same for every item, so the server's prompt cache shares it), the stance and persona last (stance.hpp).
    std::string s = stance == "specialist"
                        ? "Reply with the complete content of every file you are asked for, in exactly this format (a CONCERN block, described below, may come first; no other "
                          "explanations, no markdown fences):\n"
                        : "Reply with the complete content of every file you are asked for, in exactly this format and nothing else (no explanations, no markdown fences):\n";
    s += "=== FILE: <path> ===\n<file content>\n=== END FILE ===\n";
    if (!brief.empty()) s += brief + "\n";
    return s + stanceText(stance, persona, WorkerShape::files);
}

// Names of the tests a test file defines: test('name' ... / it("name" ...
std::string testNames(const std::string& source)
{
    std::string out;
    int n = 0;
    for (std::size_t pos = 0; pos < source.size();) {
        std::size_t hit = std::string::npos;
        for (const char* key : {"test(", "it("}) { const auto h = source.find(key, pos); if (h != std::string::npos && h < hit) hit = h; }
        if (hit == std::string::npos) break;
        std::size_t q = source.find_first_of("'\"`", hit);
        if (q == std::string::npos || q - hit > 12) { pos = hit + 3; continue; }
        const auto e = source.find(source[q], q + 1);
        if (e == std::string::npos) break;
        out += "- " + source.substr(q + 1, e - q - 1) + "\n";
        ++n;
        pos = e + 1;
    }
    return n ? out : std::string{};
}

// Parse "=== FILE: path ===" blocks; tolerate a missing END and markdown fences around the content.
Parsed parseReply(const std::string& reply, const std::set<std::string>& allowed)
{
    Parsed p;
    std::size_t pos = 0;
    std::size_t consumed = 0;
    for (;;) {
        const auto start = reply.find("=== FILE:", pos);
        if (start == std::string::npos) break;
        const auto nameEnd = reply.find("===", start + 9);
        if (nameEnd == std::string::npos) break;
        std::string path = reply.substr(start + 9, nameEnd - start - 9);
        path.erase(0, path.find_first_not_of(" \t"));
        path.erase(path.find_last_not_of(" \t\r\n") + 1);
        std::size_t bodyStart = reply.find('\n', nameEnd);
        if (bodyStart == std::string::npos) break;
        ++bodyStart;
        std::size_t end = reply.find("=== END FILE ===", bodyStart);
        const std::size_t nextFile = reply.find("=== FILE:", bodyStart);
        std::size_t after;
        if (end == std::string::npos || (nextFile != std::string::npos && nextFile < end)) { end = nextFile == std::string::npos ? reply.size() : nextFile; after = end; }
        else after = end + std::string{"=== END FILE ==="}.size();
        std::string body = reply.substr(bodyStart, end - bodyStart);
        if (body.rfind("```", 0) == 0) { const auto nl = body.find('\n'); body = nl == std::string::npos ? "" : body.substr(nl + 1); }  // opening fence
        for (const std::string tail : {"\n```\n", "\n```", "```\n", "```"}) {
            if (body.size() >= tail.size() && body.compare(body.size() - tail.size(), tail.size(), tail) == 0) { body.erase(body.size() - tail.size()); body.push_back('\n'); break; }
        }
        if (!body.empty() && body.back() != '\n') body.push_back('\n');
        consumed += after - start;
        if (allowed.count(path) && !body.empty()) { p.files[path] = body; p.fileChars += body.size(); }
        else p.rejectedChars += body.size();
        pos = after;
    }
    p.proseChars = reply.size() > consumed ? reply.size() - consumed : 0;
    return p;
}

std::set<std::string> closure(const Plan& plan, const std::string& id)
{
    std::set<std::string> seen;
    std::vector<std::string> work{id};
    while (!work.empty()) {
        const WorkItem* it = plan.find(work.back());
        work.pop_back();
        if (!it) continue;
        for (const auto& d : it->deps) if (seen.insert(d).second) work.push_back(d);
    }
    return seen;
}

const char* const kEditSyntax =
    "Reply with EDITS, not the whole files. The earlier draft is shown with line numbers (`N| text`; the numbers are not part of the file); a long draft is FOLDED: function bodies collapse to `... (N lines folded: a-b)`. To see folded lines, reply with ONLY read requests (no edits yet): `=== READ: <path> LINES a-b ===` or `=== READ: <path> SYMBOL name ===`, one per line; you are shown the lines and then reply with your edits. Prefer LINES edits, which name the lines to replace:\n"
    "=== EDIT: <path> LINES 345-347 ===\n<the new text for those lines; empty to delete them>\n=== END EDIT ===\n"
    "(LINES 345 is one line; AFTER 120 inserts after line 120. Numbers are those of the draft shown, and several edits may be given: they are applied together.) Or a search block, which must match the current text exactly once:\n"
    "=== EDIT: <path> ===\n<<<<<<< SEARCH\n<exact lines to replace>\n=======\n<new lines>\n>>>>>>> REPLACE\n=== END EDIT ===\n"
    "A worked example. The draft shows\n  41|     return \"draft\";\nand line 41 must return the label ready. The whole reply is:\n"
    "=== EDIT: src/labels.cpp LINES 41 ===\n    return \"ready\";\n=== END EDIT ===\n"
    "Never copy the file into a SEARCH block. Rewrite the whole file (in the usual === FILE: <path> === form) only when most of it changes.";

// The work card as the worker reads it: the task, the files, the context, the contract, the acceptance list, the provided tests of accepted dependencies
// (whole, or by name with the `index` treatment) and the notes. Counts the tests it showed.
std::string buildCard(const fs::path& dir, const Plan& plan, const WorkItem& itemRef, const std::string& itemId, const std::string& treatment, long& testCount)
{
    const WorkItem* item = &itemRef;
    std::ostringstream card;
    card << "Task: " << item->goal << "\nFiles to write: ";
    for (std::size_t i = 0; i < item->files.size(); ++i) card << (i ? ", " : "") << item->files[i];
    card << "\n\nContext: " << item->card.context << "\n\nContract:\n" << item->card.contract << "\n";
    if (!item->card.acceptance.empty()) { card << "\nAcceptance:\n"; for (const auto& x : item->card.acceptance) card << "- " << x << "\n"; }
    if (!item->card.examples.empty()) { card << "\nWorked examples (facts the contract commits to: the tests and the code must both agree with them; if a test or an example looks wrong, say so):\n"; for (const auto& x : item->card.examples) card << "- " << x << "\n"; }
    // The contracts of the developer items this one is built on: the program must know how the library reports an error (the first full clean-slate run: the
    // engine returns NaN for invalid input, the app's card only said "return 1 on error", and no attempt of the app passed its error test).
    for (const auto& depId : item->deps)
        if (const WorkItem* dep = plan.find(depId); dep && dep->persona == "developer" && !dep->card.contract.empty() && item->persona == "developer")
            card << "\nYou build on '" << dep->id << "' (files " << (dep->files.empty() ? std::string{} : dep->files[0]) << "). What it promises, which you rely on:\n" << dep->card.contract << "\n";
    for (const auto& inc : item->card.include) {
        std::string content;
        for (const auto& dep : closure(plan, itemId)) { const fs::path f = dir / "items" / dep / "accepted" / inc; if (fs::exists(f)) { content = slurp(f); break; } }
        if (content.empty())   // a file of the plan's own scaffold (the project's header) or a test its author gave: shown as it was planned
            for (const auto* list : {&plan.scaffold, &plan.given})
                for (const auto& f : *list) if (content.empty() && f.path == inc) content = f.content;
        if (content.empty()) continue;
        const std::string allNames = testNames(content);
        testCount += std::count(allNames.begin(), allNames.end(), '\n');
        const std::string names = treatment == "index" ? allNames : std::string{};
        if (!names.empty()) card << "\nThe provided tests in " << inc << " are named as follows (the file itself is not shown; implement the contract so tests with these names pass):\n" << names;
        else card << "\nProvided file " << inc << ":\n=== FILE: " << inc << " ===\n" << clip(content, 7000) << "\n=== END FILE ===\n";
    }
    if (!item->card.notes.empty()) { card << "\nNotes:\n"; for (const auto& x : item->card.notes) card << "- " << x << "\n"; }
    return card.str();
}

}  // namespace sub0squad::agent
