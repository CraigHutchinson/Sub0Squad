// Forked sections on the canvas (include/sub0squad/fork.hpp). The design in one place:
//   declare  -> a primer session declares the structure (`@@ signature` lines, no bodies); the harness completes it into the skeleton (expandSkeleton),
//               which must pass the partial compile check to become REVISION 1 of the canvas. A later attempt RESUMES the earlier attempt's canvas
//               instead (accepted sections kept), unless that attempt had its full chance.
//   write    -> one stubbed function is a section, owned by one session. Sessions share a prefix (system block, card, the numbered skeleton) and run side
//               by side up to the idle slots, never pinned to the job's one lease slot. A reply supplies a body; the SIGNATURE is the skeleton's, put back
//               by the harness. The coordinator commits it against the revision shown (the canvas rebases), after the partial compile check; a refusal
//               shows the section its own text with the located error.
//   assemble -> findings are ROUTED, never a whole-file round: a section never accepted is retried alone; a function nobody calls is removed by the
//               harness (no model call); a compile error goes to the owner of the function it is in; only a file that compiles strictly is judged by the
//               item's tests, and a test failure goes to the sections it names.
//   budget   -> a wall budget that grows with the number of sections; when it is spent no session starts, the failure reason says so, and the next
//               attempt carries on from this canvas.
// Throughput by default: sections run side by side up to the idle slots (planParallel); one at a time needs a stated reason; memory pressure sheds forks.
#include "sub0squad/fork.hpp"

#include "agent_impl.hpp"
#include "json_util.hpp"
#include "sub0squad/canvas.hpp"
#include "sub0squad/contract.hpp"
#include "sub0squad/harness_common.hpp"
#include "sub0squad/mends.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/host_guard.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/sandbox.hpp"
#include "sub0squad/trace.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace sub0squad {

namespace fs = std::filesystem;
using namespace agent;

namespace {

using Clock = std::chrono::steady_clock;
double since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

std::vector<std::string> linesOf(const std::string& text)
{
    std::vector<std::string> v;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) { if (!l.empty() && l.back() == '\r') l.pop_back(); v.push_back(l); }
    return v;
}

/// Whether `text` holds `word` as a whole identifier (a test failure that names a function).
bool namesWord(const std::string& text, const std::string& word)
{
    const auto isId = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
        if ((at == 0 || !isId(text[at - 1])) && (at + word.size() >= text.size() || !isId(text[at + word.size()]))) return true;
    return false;
}

std::string leaf(const std::string& name) { const auto c = name.find_last_of(":."); return c == std::string::npos ? name : name.substr(c + 1); }
std::string trimmed(const std::string& l) { const auto a = l.find_first_not_of(" \t"); return a == std::string::npos ? std::string{} : l.substr(a, l.find_last_not_of(" \t") - a + 1); }

/// The function-like region `symbol` names: the one of exactly that name; else one whose qualified name ends with it (`Parser::parse` in
/// `calc::Parser::parse`); else, for an unqualified symbol, the first whose last name is it. A member and a free function may share a last name (row 71).
const FoldRegion* regionOf(const std::vector<FoldRegion>& regions, const std::string& symbol)
{
    for (const auto& r : regions) if (r.name == symbol) return &r;
    for (const auto& r : regions) if (r.name.size() > symbol.size() + 2 && r.name.compare(r.name.size() - symbol.size() - 2, 2, "::") == 0 && r.name.compare(r.name.size() - symbol.size(), symbol.size(), symbol) == 0) return &r;
    if (symbol.find(':') == std::string::npos) for (const auto& r : regions) if (leaf(r.name) == symbol) return &r;
    return nullptr;
}

/// The lines of the region `symbol` names in `text` ("" when there is none).
std::string functionText(const std::string& text, const std::string& path, const std::string& symbol)
{
    const auto regions = foldRegions(text, isPythonPath(path), true);
    const FoldRegion* r = regionOf(regions, symbol);
    if (!r) return {};
    const auto v = linesOf(text);
    std::string fn;
    for (int k = r->first; k <= r->bodyTo && k <= static_cast<int>(v.size()); ++k) fn += v[static_cast<std::size_t>(k) - 1] + "\n";
    return fn;
}

/// Tests failed and tests run, from a gate's text (`GATE FAIL: 2 of 22 tests failed`, `GATE PASS: ... (22 tests, ...`); {-1, -1} when it says neither
/// (the file did not build, or nothing ran).
std::pair<int, int> testCounts(const std::string& gateText)
{
    if (gateText.rfind("GATE FAIL: ", 0) == 0 && std::isdigit(static_cast<unsigned char>(gateText[11]))) {
        const auto of = gateText.find(" of ", 11);
        if (of != std::string::npos && gateText.find(" tests failed", of) != std::string::npos) return {std::atoi(gateText.c_str() + 11), std::atoi(gateText.c_str() + of + 4)};
    }
    if (gateText.rfind("GATE PASS: ", 0) == 0) if (const auto open = gateText.find('('); open != std::string::npos) return {0, std::atoi(gateText.c_str() + open + 1)};
    return {-1, -1};
}

std::string forkSystemBlock(const std::string& persona, const std::string& brief)
{
    std::string s = "You write ONE function of a file that several workers are writing at the same time. You are shown the file with line numbers (`N| text`; the numbers are not "
                    "part of the file): every function is declared, and a function that is not written yet has a body that only raises `not implemented: <name>`. "
                    "Reply with exactly one block and nothing else (no explanations, no markdown fences):\n"
                    "=== EDIT: <path> SYMBOL <function name> ===\n<your function: its declaration, then its body>\n=== END EDIT ===\n"
                    "Rules. The declaration is fixed: the harness keeps the one in the file whatever you write, so do not change the name, the parameters or the return type. "
                    "Write only your own function, complete, returning a value on every path. Call only what the file declares (the other functions, by their signatures) and the "
                    "standard library; if you need a helper of your own, write it INSIDE your function (a lambda or a nested function), never as another function. "
                    "Do not write, change or repeat any other function.\n";
    if (!brief.empty()) s += brief + "\n";
    s += "You are the " + persona + " on a small software squad.\n";
    return s;
}

const char* const kPrimerAsk =
    "\nDeclare the STRUCTURE of each file only. Other workers write the function bodies afterwards, one function each, at the same time. For every file to write, reply with:\n"
    "=== SKELETON: <path> ===\n"
    "<the file's includes or imports, constants, and type or class declarations, written normally>\n"
    "@@ <the signature of ONE function or method, on one line, with NO body>\n"
    "=== END SKELETON ===\n"
    "Put one `@@` line where each function or method belongs (a method: inside its class, indented like the class's other members). Write NO function bodies at all: the harness "
    "gives every `@@` line a placeholder body and compiles the result. Declare only functions that will be CALLED: every one must be used by another or be part of the file's contract. "
    "Split the work into functions of at most about 40 lines each. Keep each file's skeleton under 80 lines. Nothing else in the reply (no explanations, no markdown fences).\n"
    "Example (C++):\n=== SKELETON: src/shapes.cpp ===\n#include \"shapes.hpp\"\n#include <cmath>\n\nnamespace shapes {\n\n@@ static double square(double x)\n@@ double circleArea(double radius)\n@@ double hypotenuse(double a, double b)\n\n}  // namespace shapes\n=== END SKELETON ===\n"
    "Example (Python):\n=== SKELETON: shapes/core.py ===\nimport math\n\n\nclass Circle:\n    def __init__(self, radius):\n        self.radius = radius\n\n    @@ def area(self)\n    @@ def scaled(self, factor)\n\n\n@@ def hypotenuse(a, b)\n=== END SKELETON ===\n";

/// The files of a skeleton reply: `=== SKELETON: path ===` blocks (a `=== FILE: path ===` block is read the same way), each up to its END line or the next block.
std::map<std::string, std::string> parseSkeletonReply(const std::string& reply, const std::set<std::string>& allowed)
{
    std::map<std::string, std::string> files;
    std::istringstream in(reply);
    std::string path, body;
    const auto close = [&] { if (!path.empty() && allowed.count(path) && !body.empty()) files[path] = body; path.clear(); body.clear(); };
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const bool head = line.rfind("=== SKELETON:", 0) == 0 || line.rfind("=== FILE:", 0) == 0;
        if (head) {
            close();
            path = line.substr(line.find(':') + 1);
            if (const auto e = path.rfind("==="); e != std::string::npos) path.erase(e);
            path = trimmed(path);
            continue;
        }
        if (line.rfind("=== END", 0) == 0) { close(); continue; }
        if (path.empty() || line.rfind("```", 0) == 0) continue;
        body += line + "\n";
    }
    close();
    return files;
}

/// The lines `from..to` (1-based, inclusive) of `text`, numbered as the views are.
std::string numbered(const std::string& text, int from, int to)
{
    const auto v = linesOf(text);
    std::string out;
    for (int k = std::max(1, from); k <= to && k <= static_cast<int>(v.size()); ++k) { char n[16]; std::snprintf(n, sizeof n, "%4d| ", k); out += std::string(n) + v[static_cast<std::size_t>(k) - 1] + "\n"; }
    return out;
}

}  // namespace

// ---- pure helpers, exported for their tests ---------------------------------------------------------------------------------------------------------
std::string applySectionSignature(const std::string& textIn, const std::string& signature, const std::string& indent, const std::string& path)
{
    std::string text = textIn;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    if (isPythonPath(path)) {
        const auto v = linesOf(text);
        // The model's own `def` line, when it wrote one: everything up to the line that ends it is replaced by the skeleton's declaration, and the body
        // is moved to the skeleton's indentation.
        std::size_t def = v.size(), bodyFrom = 0;
        for (std::size_t i = 0; i < v.size(); ++i) { const std::string t = trimmed(v[i]); if (t.rfind("def ", 0) == 0 || t.rfind("async def ", 0) == 0) { def = i; break; } if (!t.empty() && t[0] != '@' && t[0] != '#') break; }
        std::string modelIndent;
        if (def < v.size()) {
            modelIndent = v[def].substr(0, v[def].find_first_not_of(" \t"));
            bodyFrom = def + 1;
            for (std::size_t j = def; j < v.size(); ++j) { const std::string t = trimmed(v[j]); if (!t.empty() && t.back() == ':') { bodyFrom = j + 1; break; } }
        } else {   // a body alone: its least indentation stands for the function's own, one level in
            std::size_t least = std::string::npos;
            for (const auto& l : v) { const auto a = l.find_first_not_of(" \t"); if (a != std::string::npos) least = std::min(least, a); }
            if (least == std::string::npos) least = 0;
            modelIndent = least >= 4 ? std::string(least - 4, ' ') : std::string{};
            if (least < 4) { std::string out = indent + signature + ":\n"; for (const auto& l : v) out += (trimmed(l).empty() ? std::string{} : indent + "    " + l.substr(std::min(least, l.size()))) + "\n"; return out; }
        }
        std::string out = indent + signature + ":\n";
        for (std::size_t i = bodyFrom; i < v.size(); ++i) {
            if (trimmed(v[i]).empty()) { out += "\n"; continue; }
            out += (v[i].rfind(modelIndent, 0) == 0 ? indent + v[i].substr(modelIndent.size()) : indent + "    " + trimmed(v[i])) + "\n";
        }
        return out;
    }
    const bool script = path.size() > 3 && (path.compare(path.size() - 3, 3, ".js") == 0 || path.compare(path.size() - 4, 4, ".mjs") == 0);
    const auto brace = text.find('{');
    // Whether what precedes the first brace is the model's own declaration (it holds a parameter list); if not, the reply is a body alone.
    const bool declared = brace != std::string::npos && text.substr(0, brace).find('(') != std::string::npos;
    if (declared && !script) {
        // A constructor's member-initializer list is part of what the section writes: it is kept (the declaration before it is still the skeleton's).
        int level = 0;
        std::size_t close = std::string::npos;
        for (std::size_t k = text.find('('); k < text.size(); ++k) { if (text[k] == '(') ++level; else if (text[k] == ')' && --level == 0) { close = k; break; } }
        const auto next = close == std::string::npos ? std::string::npos : text.find_first_not_of(" \t\r\n", close + 1);
        const auto sigParen = signature.find('(');
        const std::string qualifiedName = sigParen == std::string::npos ? std::string{} : signature.substr(0, sigParen);
        const auto sep = qualifiedName.rfind("::");
        const bool constructor = sep != std::string::npos && sep >= 1 && qualifiedName.substr(sep + 2) == leaf(qualifiedName.substr(0, sep)) && qualifiedName.find(' ') == std::string::npos;
        if (constructor && next != std::string::npos && text[next] == ':' && (next + 1 >= text.size() || text[next + 1] != ':')) return indent + signature + "\n" + indent + "    " + text.substr(next) + "\n";
    }
    std::string body = declared ? text.substr(brace) : (brace != std::string::npos && trimmed(text.substr(0, brace)).empty() ? text.substr(brace) : "{\n" + text + "\n" + indent + "}");
    return script ? indent + signature + " " + body + "\n" : indent + signature + "\n" + indent + body + "\n";
}

std::map<std::string, std::string> sectionsOwningFailure(const std::string& gateText, const std::vector<std::pair<std::string, std::string>>& sections)
{
    std::map<std::string, std::string> owners;
    std::vector<std::string> failures;
    for (const auto& l : linesOf(gateText)) if (l.find("FAIL") != std::string::npos || l.find("not ok") != std::string::npos || l.find("xception") != std::string::npos || l.find("Error") != std::string::npos) failures.push_back(l);
    for (const auto& [name, text] : sections) {
        std::set<std::string> literals;
        for (const auto& l : linesOf(text)) {
            if (trimmed(l).rfind("#include", 0) == 0) continue;
            for (std::size_t i = 0; i < l.size(); ++i) {
                if (l[i] != '"' && l[i] != '\'') continue;
                const char quote = l[i];
                std::string lit;
                std::size_t j = i + 1;
                for (; j < l.size() && l[j] != quote; ++j) { if (l[j] == '\\' && j + 1 < l.size()) ++j; lit += l[j]; }
                if (j < l.size() && lit.size() >= 8) literals.insert(lit);
                i = j;
            }
        }
        for (const auto& f : failures)
            for (const auto& lit : literals) if (f.find(lit) != std::string::npos) { owners[name] += f + "\n"; break; }
    }
    return owners;
}

std::string failingTestSources(const std::string& gateText, const std::vector<std::pair<std::string, std::string>>& testFiles)
{
    // The names of the failing tests: `FAIL 6 - name: ...` (the C++ runner), `not ok 6 - name` (TAP), `FAIL: name` / `FAILED file::name` (Python runners).
    std::vector<std::string> names;
    for (const auto& l : linesOf(gateText)) {
        const std::string t = trimmed(l);
        std::size_t at = std::string::npos;
        if (t.rfind("FAIL ", 0) == 0 || t.rfind("not ok ", 0) == 0) { at = t.find(" - "); if (at != std::string::npos) at += 3; }
        else if (t.rfind("FAIL: ", 0) == 0) at = 6;
        else if (t.rfind("FAILED ", 0) == 0) { at = t.rfind("::"); at = at == std::string::npos ? 7 : at + 2; }
        if (at == std::string::npos || at >= t.size()) continue;
        std::size_t e = at;
        while (e < t.size() && (std::isalnum(static_cast<unsigned char>(t[e])) || t[e] == '_')) ++e;
        if (e > at && std::find(names.begin(), names.end(), t.substr(at, e - at)) == names.end()) names.push_back(t.substr(at, e - at));
    }
    std::string out;
    for (const auto& name : names) {
        for (const auto& [path, text] : testFiles) {
            const auto v = linesOf(text);
            const bool python = isPythonPath(path);
            for (std::size_t i = 0; i < v.size(); ++i) {
                const std::string t = trimmed(v[i]);
                const bool head = namesWord(v[i], name) && (python ? t.rfind("def ", 0) == 0 : (t.rfind("TEST", 0) == 0 || t.rfind("test(", 0) == 0 || t.rfind("it(", 0) == 0 || t.find("void ") != std::string::npos));
                if (!head) continue;
                std::string block;
                int depth = 0, lines = 0;
                const auto indent = v[i].find_first_not_of(" \t");
                for (std::size_t k = i; k < v.size() && lines < 16; ++k, ++lines) {
                    if (python && k > i && !trimmed(v[k]).empty() && v[k].find_first_not_of(" \t") <= indent) break;
                    block += v[k] + "\n";
                    for (const char c : v[k]) { if (c == '{') ++depth; else if (c == '}') --depth; }
                    if (!python && depth <= 0 && v[k].find_first_of("{}") != std::string::npos) break;
                }
                if (out.size() + block.size() <= 1600) out += "--- " + path + ", " + name + "\n" + block;
                break;
            }
        }
    }
    return out.empty() ? out : "The failing tests as they are written:\n" + out;
}

std::vector<CompileFinding> compileFindings(const std::string& output, const std::string& path)
{
    std::vector<CompileFinding> found;
    const auto v = linesOf(output);
    const auto located = [&](const std::string& l) -> int {
        const auto at = l.find(path + ":");
        if (at == std::string::npos) return 0;
        const std::size_t d = at + path.size() + 1;
        return d < l.size() && std::isdigit(static_cast<unsigned char>(l[d])) ? std::atoi(l.c_str() + d) : 0;
    };
    for (std::size_t i = 0; i < v.size(); ++i) {
        const int line = located(v[i]);
        if (line <= 0 || v[i].find("note:") != std::string::npos) continue;
        CompileFinding f;
        f.line = line;
        std::string msg = v[i].substr(v[i].find(path + ":"));
        f.text = msg + "\n";
        for (std::size_t j = i + 1; j < v.size() && j <= i + 2 && located(v[j]) == 0; ++j) f.text += v[j] + "\n";
        f.unusedFunction = msg.find("defined but not used") != std::string::npos || msg.find("unused function") != std::string::npos;
        found.push_back(std::move(f));
    }
    return found;
}

std::string ForkResult::toJson() const
{
    json::Writer w;
    w.beginObject().kvBool("passed", passed).kv("exitCode", exitCode).kv("failure", failure).kv("resumedFrom", resumedFrom).kv("width", width).kv("widthReason", widthReason).kvBool("shed", shed)
        .kvBool("budgetSpent", budgetSpent).kv("budgetSec", budgetSec).kv("skeletonTries", skeletonTries).kv("rounds", rounds).kv("targeted", targeted).kv("mechanicalMends", mechanicalMends).kv("contractSections", contractSections).kv("memberSections", memberSections).kv("routedByMessage", routedByMessage)
        .kv("testsTotal", testsTotal).kv("testsFailed", testsFailed).kvBool("nearPass", nearPass).kv("reverted", reverted)
        .kv("conflicts", conflicts).kv("lostEdits", lostEdits).kv("refused", refused)
        .kv("skeletonTokens", static_cast<std::int64_t>(skeletonTokens)).kv("sectionTokens", static_cast<std::int64_t>(sectionTokens)).kv("integrationTokens", static_cast<std::int64_t>(integrationTokens))
        .kv("wallSec", wallSec).kv("sectionWallSec", sectionWallSec).kv("idleShare", idleShare).kv("error", error).key("sections").beginArray();
    for (const auto& s : sections)
        w.beginObject().kv("name", s.name).kv("file", s.file).kv("symbol", s.symbol).kv("signature", s.signature).kv("indent", s.indent).kv("startSec", s.startSec).kv("endSec", s.endSec)
            .kv("promptTokens", static_cast<std::int64_t>(s.promptTokens)).kv("completionTokens", static_cast<std::int64_t>(s.completionTokens)).kv("calls", s.calls).kv("refused", s.refused)
            .kvBool("committed", s.committed).kvBool("removed", s.removed).kv("lastRefusal", s.lastRefusal.substr(0, 400)).endObject();
    w.endArray().endObject();
    return w.take();
}

namespace {

/// What an earlier attempt left for this one to continue: its sections and why it stopped ("" when there is nothing to resume).
struct Earlier { std::string failure; int testsTotal = -1, testsFailed = -1; std::vector<ForkSection> sections; };
Earlier readEarlier(const fs::path& forkJson)
{
    Earlier e;
    const std::string text = slurp(forkJson);
    if (text.empty()) return e;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "failure") e.failure = json::toString(v);
                else if (k == "testsTotal") e.testsTotal = static_cast<int>(v.get_int64());
                else if (k == "testsFailed") e.testsFailed = static_cast<int>(v.get_int64());
                else if (k == "sections") {
                    for (auto sv : v.get_array()) {
                        ForkSection s;
                        for (auto sf : sv.get_object()) {
                            const std::string_view sk = sf.unescaped_key();
                            auto val = sf.value();
                            if (sk == "name") s.name = json::toString(val);
                            else if (sk == "file") s.file = json::toString(val);
                            else if (sk == "symbol") s.symbol = json::toString(val);
                            else if (sk == "signature") s.signature = json::toString(val);
                            else if (sk == "indent") s.indent = json::toString(val);
                            else if (sk == "removed") s.removed = val.get_bool();
                        }
                        e.sections.push_back(std::move(s));
                    }
                }
            }
        });
    } catch (const simdjson::simdjson_error&) { return {}; }
    return e;
}

}  // namespace

ForkResult runCanvasFork(const ForkOptions& o)
{
    ForkResult res;
    const auto t0 = Clock::now();
    auto plan = Plan::fromJson(slurp(o.dir / "plan.json"));
    if (!plan || !plan->find(o.itemId)) { res.exitCode = 2; res.failure = "skeleton"; res.error = "cannot load the plan or the item"; return res; }
    WorkItem item = *plan->find(o.itemId);
    (void)readCardOverride(o.dir / "items" / o.itemId / "card-override.json", item.card);
    const Harness* harness = findHarness(plan->harness);
    const fs::path itemDir = o.dir / "items" / o.itemId, attemptDir = itemDir / ("attempt-" + std::to_string(o.attempt)), out = attemptDir / "out";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);
    const std::set<std::string> allowed(item.files.begin(), item.files.end());
    long testCount = 0;
    const std::string card = buildCard(o.dir, *plan, item, o.itemId, "baseline", testCount);
    const ChatFn chat = o.chat ? o.chat : ChatFn{[](const EngineProfile& e, const std::vector<ChatMessage>& m, int maxTokens, int timeoutSec, const StreamGuard& guard) { return chatCompleteMessages(e, m, maxTokens, timeoutSec, {}, guard); }};
    // Several requests run at once, so none is pinned to the job's one lease slot: pinned, they queue behind each other (docs/INTERVENTIONS.md row 47).
    const auto seeded = [&](const std::string& label) { EngineProfile e = o.engine; e.slot = EngineProfile::kAnySlot; e.seed = turnSeed(o.engine.seed, o.itemId + "#" + std::to_string(o.attempt) + "#" + label); return e; };

    Canvas canvas;
    std::mutex mu;                                   // guards the canvas and the result counters
    // The wall budget: a base plus an allowance per section (known once the skeleton is). Every call is bounded by what is left; none starts after it is spent.
    std::atomic<int> budget{o.wallBudgetSec};
    const auto remainingSec = [&]() -> int { return o.wallBudgetSec <= 0 ? o.timeoutSec : std::max(0, std::min(o.timeoutSec, budget.load() - static_cast<int>(since(t0)))); };
    /// Run the item's gate over the canvas as it stands (the attempt's `out` is rewritten from it); the gate's text comes back in `gateText`.
    const auto gate = [&](std::string& gateText) {
        fs::remove_all(out, ec);
        fs::create_directories(out, ec);
        for (const auto& [path, text] : canvas.files()) (void)writeFile(out / path, text);
        const int rc = harness && harness->gate ? harness->gate({"--workflow-dir", o.dir.string(), "--item", o.itemId, "--attempt", std::to_string(o.attempt), "--no-publish", "1"}) : 1;
        gateText = slurp(attemptDir / "gate.txt");
        return rc == 0;
    };
    // The best state the tests have judged in this attempt (catalogue row 70): a round that leaves the file worse, or not compiling, is undone, and an
    // attempt never ends on a state worse than the best it had (which is at least the best of every earlier attempt, since it started from that).
    struct Best { Canvas canvas; int failed = -1, total = -1; std::string gateText; std::vector<bool> removed; };
    std::optional<Best> best;
    const auto keepBest = [&](int failed, int total, const std::string& gateText) {
        Best b{canvas, failed, total, gateText, {}};
        for (const auto& sec : res.sections) b.removed.push_back(sec.removed);
        best = std::move(b);
    };
    const auto restoreBest = [&](const std::string& why) {
        canvas = best->canvas;
        for (std::size_t i = 0; i < res.sections.size() && i < best->removed.size(); ++i) res.sections[i].removed = best->removed[i];
        persistCanvas(canvas, attemptDir);
        ++res.reverted;
        std::printf("assembly: %s: back to the best state (%d of %d tests failing)\n", why.c_str(), best->failed, best->total);
        events::note("assembly: back to the best state (" + std::to_string(best->failed) + " of " + std::to_string(best->total) + " failing)");
    };
    const auto finish = [&](int code, const std::string& failure) {
        if (code != 0 && best) {
            if (canvas.files() != best->canvas.files()) restoreBest("the attempt ends on a worse state than it had");
            std::string text;
            (void)gate(text);   // `out` and gate.txt describe what stands
            res.testsFailed = best->failed;
            res.testsTotal = best->total;
            res.nearPass = best->total >= 10 && best->failed * 100 <= best->total * 15;   // the gate's own near-pass rule (exit 3)
            if (res.nearPass && code == 1) code = 3;
        }
        res.exitCode = code;
        res.passed = code == 0;
        res.failure = failure;
        res.budgetSec = budget.load();
        res.wallSec = since(t0);
        for (const auto& [path, text] : canvas.files()) (void)writeFile(out / path, text);   // what stands, always: the next attempt continues from it
        (void)writeFile(attemptDir / "fork.json", res.toJson() + "\n");
        if (code == 0) harnessutil::clearAttemptFailure(attemptDir);
        else harnessutil::recordAttemptFailure(attemptDir, "agent", failure, res.error + (res.testsTotal > 0 ? " (best state: " + std::to_string(res.testsFailed) + " of " + std::to_string(res.testsTotal) + " tests failing)" : std::string{}));
        return res;
    };

    // The contract of each file (contract.hpp): the functions the harness's own stub defines. They are sections whatever the model declares.
    std::map<std::string, std::vector<ContractFunction>> contract;
    for (const auto& f : item.files) if (auto c = contractOf(harness, f); !c.empty()) contract[f] = std::move(c);
    /// Add `signature` to the canvas as a stubbed section of `path` (a harness transaction, a new revision), with `restored` functions put back before it.
    const auto addSection = [&](const std::string& path, const std::string& name, const std::string& signature, const std::string& scope, const std::string& brief, const std::vector<std::string>& restored, bool member = false) {
        if (!canvas.replaceText(path, withStubFunction(canvas.text(path), path, signature, scope, restored), "harness").ok) return false;
        ForkSection s;
        s.name = name; s.file = path; s.symbol = name; s.brief = brief;
        const auto v = linesOf(canvas.text(path));
        const auto regions = foldRegions(canvas.text(path), isPythonPath(path), true);
        if (const FoldRegion* r = regionOf(regions, name)) { const std::string& decl = v[static_cast<std::size_t>(r->first) - 1]; s.indent = decl.substr(0, decl.find_first_not_of(" \t")); s.signature = trimmed(decl); s.symbol = r->name; }
        while (!s.signature.empty() && (s.signature.back() == '{' || s.signature.back() == ':' || s.signature.back() == ' ')) s.signature.pop_back();
        res.sections.erase(std::remove_if(res.sections.begin(), res.sections.end(), [&](const ForkSection& e) { return e.file == path && (e.symbol == s.symbol || e.name == name); }), res.sections.end());
        res.sections.push_back(std::move(s));
        ++(member ? res.memberSections : res.contractSections);
        std::printf("%s: `%s` was not in %s: added by the harness as a section\n", member ? "class member" : "contract", name.c_str(), path.c_str());
        events::note("the harness added the missing function " + name + " as a section");
        return true;
    };
    /// Every contract function the canvas does not define becomes a section (row 59): a model's section list cannot leave the entry point out.
    const auto ensureContract = [&](const std::string& brief) {
        for (const auto& [path, fns] : contract) if (canvas.has(path)) for (const auto& f : undefinedContract(canvas.text(path), path, fns)) (void)addSection(path, f.name, f.signature, f.scope, brief, {});
    };

    /// Every member function a class of the file declares and nothing defines becomes a section too (row 71): the skeleton declared it, so the file owes it.
    const auto ensureMembers = [&](const std::string& brief) {
        for (const auto& path : canvas.paths()) for (const auto& m : undefinedMembers(canvas.text(path), path)) {
            if (std::any_of(res.sections.begin(), res.sections.end(), [&](const ForkSection& e) { return e.file == path && e.name == m.qualified() && !e.removed; })) continue;   // an overload: one section holds the name
            (void)addSection(path, m.qualified(), m.signature, m.scope, brief, {}, true);
        }
    };

    // ---- 1. the document: the earlier attempt's canvas when it stopped short, else a declared skeleton completed by the harness ----
    if (!o.mendSection.empty() && o.attempt > 1) {
        // The mend `add-section`: the previous attempt's files as they stand, whatever it failed on; nothing accepted is written again.
        const fs::path before = itemDir / ("attempt-" + std::to_string(o.attempt - 1));
        const Earlier e = readEarlier(before / "fork.json");
        for (const auto& f : item.files) if (const std::string text = slurp(before / "out" / f); !text.empty()) canvas.addFile(f, text);
        const std::string path = canvas.has(o.mendFile) ? o.mendFile : canvas.paths().empty() ? std::string{} : canvas.paths().front();
        std::string name = leaf(o.mendSection);
        std::string signature, scope;
        bool member = false;
        if (!path.empty()) {
            // A member a class of the file declares (`calc::Parser::parse`): its out-of-class definition, with the class kept in the section's name.
            for (const auto& m : undefinedMembers(canvas.text(path), path)) {
                const std::string q = m.qualified();
                const bool named = o.mendSection == q || (o.mendSection.size() > q.size() + 2 && o.mendSection.compare(o.mendSection.size() - q.size() - 2, 2, "::") == 0 && o.mendSection.compare(o.mendSection.size() - q.size(), q.size(), q) == 0);
                if (named && signature.empty()) { signature = m.signature; scope = m.scope; name = q; member = true; }
            }
            if (signature.empty()) if (const auto c = contract.find(path); c != contract.end()) for (const auto& f : c->second) if (f.name == name) { signature = f.signature; scope = f.scope; }
            if (signature.empty()) { signature = prototypeOf(canvas.text(path), name); scope = prototypeScope(canvas.text(path), name); }
        }
        if (signature.empty()) { res.error = "mend add-section: `" + o.mendSection + "` is neither a contract function nor declared in the file, so it has no signature to add"; return finish(2, "skeleton"); }
        res.sections = e.sections;
        res.resumedFrom = o.attempt - 1;
        // Sections an earlier assembly removed because nobody called them: with a new caller they may be needed, so their last committed text comes back
        // (from the canvas history the attempt kept); assembly removes them again if they are still uncalled.
        std::vector<std::string> restored;
        std::string flat = path;
        for (char& c : flat) if (c == '/') c = '_';
        for (auto& s : res.sections) {
            if (!s.removed || s.file != path) continue;
            for (int rev = 400; rev >= 1; --rev) {
                const std::string old = slurp(before / "canvas" / (flat + ".r" + std::to_string(rev)));
                if (old.empty()) continue;
                const std::string fn = functionText(old, path, s.symbol);
                if (fn.empty() || fn.find("not implemented: " + s.name) != std::string::npos) continue;
                restored.push_back(fn);
                s.removed = false;
                std::printf("mend: `%s` (removed earlier as uncalled) is put back\n", s.symbol.c_str());
                break;
            }
        }
        if (!restored.empty() && !isPythonPath(path)) {   // they may call each other in any order: their prototypes come back first, as the skeleton had them
            std::string prototypes;
            for (const auto& fn : restored) { const std::string first = fn.substr(0, fn.find('\n')); if (!first.empty() && first[0] != ' ' && first[0] != '\t') prototypes += trimmed(first) + ";\n"; }
            if (!prototypes.empty()) restored.insert(restored.begin(), prototypes);
        }
        const std::string brief = "This function was MISSING from the file: the harness added its declaration. The build failed without it:\n" + clip(firstErrorLine(slurp(before / "gate.txt")), 400);
        if (!addSection(path, name, signature, scope, brief, restored, member)) { res.error = "mend add-section: the canvas refused the new section"; return finish(2, "skeleton"); }
        ensureMembers("This member function is DECLARED by its class in the file and nothing defined it, so the build failed. The harness added its definition as a stub.");   // one mend covers every such member
        std::printf("mend add-section: `%s` in %s; %zu earlier section(s) kept\n", name.c_str(), path.c_str(), res.sections.size() - 1);
    } else if (!o.fresh && o.attempt > 1) {
        // The BEST earlier attempt first (row 70): the one whose file passed the largest share of its tests, when that is at least half. Whatever came
        // between (a rewritten card, revised tests, a strategy), this attempt starts from it and never from a worse state. Failing that, the attempt
        // before this one when it stopped short (out of budget, a section missing, a compile error in a section).
        int from = 0, bestScore = -1;
        Earlier e;
        for (int n = 1; n < o.attempt; ++n) {
            Earlier x = readEarlier(itemDir / ("attempt-" + std::to_string(n)) / "fork.json");
            if (x.sections.empty() || x.testsTotal <= 0 || x.testsFailed < 0 || x.testsFailed * 2 > x.testsTotal) continue;
            const int score = (x.testsTotal - x.testsFailed) * 10000 / x.testsTotal;
            if (score >= bestScore) { bestScore = score; from = n; e = std::move(x); }
        }
        const bool byTests = from != 0;
        if (!from) {
            e = readEarlier(itemDir / ("attempt-" + std::to_string(o.attempt - 1)) / "fork.json");
            if (!e.sections.empty() && (e.failure == "budget" || e.failure == "sections-missing" || e.failure == "compile")) from = o.attempt - 1;
        }
        if (from) {
            const fs::path before = itemDir / ("attempt-" + std::to_string(from));
            Canvas candidate;
            for (const auto& f : item.files) if (const std::string text = slurp(before / "out" / f); !text.empty()) candidate.addFile(f, text);
            if (!candidate.paths().empty() && precheckCanvas(candidate, harness, o.dir, o.itemId, o.attempt, attemptDir, true).empty()) {
                canvas = std::move(candidate);
                res.sections = e.sections;
                res.resumedFrom = from;
                const std::string why = byTests ? "the best so far: " + std::to_string(e.testsTotal - e.testsFailed) + " of " + std::to_string(e.testsTotal) + " tests passed" : e.failure;
                std::printf("fork: resuming the canvas of attempt %d (%s): %zu section(s)\n", from, why.c_str(), res.sections.size());
                events::note("fork resumes attempt " + std::to_string(from) + " (" + why + ")");
                if (o.contractSections) ensureContract("This function was MISSING from the file: the tests call it and nothing defined it. The harness added its declaration.");
                if (o.contractSections) ensureMembers("This member function is DECLARED by its class in the file and nothing defined it. The harness added its definition as a stub.");
            }
        }
    }
    if (canvas.paths().empty()) {
        const std::string primerSystem = "You plan the structure of source files for a small software squad. " + harnessBrief(plan->harness) + "\n";
        std::string contractNote;
        if (o.contractSections)
            for (const auto& [path, fns] : contract) for (const auto& f : fns) contractNote += "  " + path + ": " + f.signature + "\n";
        if (!contractNote.empty()) contractNote = "\nThe harness itself declares these contract functions as sections, with exactly these signatures (you may place their `@@` line; you need not):\n" + contractNote + "Declare the HELPER functions they need.\n";
        std::vector<ChatMessage> primer = {{"system", primerSystem}, {"user", card + kPrimerAsk + contractNote}};
        const fs::path primerTrace = attemptDir / "conversation-skeleton.md";
        traceBegin(primerTrace, "Skeleton session of " + o.itemId, o.engine, std::min(o.maxTokens, o.skeletonMaxTokens));
        for (int tryNo = 0; tryNo < 2; ++tryNo) {
            if (remainingSec() <= 0) { res.budgetSpent = true; res.error = "the attempt's wall budget (" + std::to_string(budget.load()) + " s) was spent before a skeleton was accepted"; return finish(2, "budget"); }
            ++res.skeletonTries;
            traceSent(primerTrace, tryNo, primer, tryNo == 0 ? 0 : primer.size() - 2, -1);
            // A skeleton is short by definition: the reply is bounded at the server, and cancelled while it streams if it runs on.
            const StreamGuard tooLong = [&](const std::string& soFar) { return o.skeletonMaxLines > 0 && std::count(soFar.begin(), soFar.end(), '\n') > o.skeletonMaxLines; };
            auto r = chat(seeded("skeleton" + std::to_string(tryNo)), primer, std::min(o.maxTokens, o.skeletonMaxTokens), remainingSec(), tooLong);
            if (!r) { res.error = "the skeleton call failed: " + r.error(); return finish(2, remainingSec() <= 0 ? "budget" : "skeleton"); }
            traceReply(primerTrace, tryNo, *r);
            res.skeletonTokens += r->promptTokens + r->completionTokens;
            std::string bad;
            Canvas candidate;
            if (r->finishReason == "aborted") bad = "it ran past " + std::to_string(o.skeletonMaxLines) + " lines and was stopped: a skeleton holds declarations and `@@` signature lines only, with NO function bodies";
            else if (r->finishReason == "length") bad = "it was cut off at the token limit: a skeleton holds declarations and `@@` signature lines only, with NO function bodies";
            else {
                const auto declared = parseSkeletonReply(r->content, allowed);
                if (declared.empty()) bad = "the reply held no === SKELETON: <path> === block for the files to write";
                for (const auto& [path, modelText] : declared) {
                    if (!bad.empty()) break;
                    // The contract first (row 59): each contract function is declared with the contract's signature, whether or not the model listed it.
                    const auto fns = o.contractSections && contract.count(path) ? contract.at(path) : std::vector<ContractFunction>{};
                    const std::string withContract = fns.empty() ? modelText : withContractDeclarations(modelText, path, fns);
                    // Then the members (row 71): a class that declares a member function owes its definition, so each one gets a `@@` line of its own.
                    const std::string text = o.contractSections ? withMemberDeclarations(withContract, path) : withContract;
                    const auto marks = [](const std::string& t) { int n = 0; for (std::size_t at = t.find("@@"); at != std::string::npos; at = t.find("@@", at + 2)) ++n; return n; };
                    if (marks(withContract) > marks(modelText)) { res.contractSections += marks(withContract) - marks(modelText); std::printf("contract: the skeleton of %s left %d contract function(s) out; the harness declared them\n", path.c_str(), marks(withContract) - marks(modelText)); }
                    if (marks(text) > marks(withContract)) { res.memberSections += marks(text) - marks(withContract); std::printf("class members: the skeleton of %s declares %d member function(s) with no section; the harness made each one a section\n", path.c_str(), marks(text) - marks(withContract)); }
                    if (const auto twice = o.contractSections ? overloadedMembers(text, path) : std::vector<std::string>{}; !twice.empty()) { bad = path + ": the class member `" + twice.front() + "` is declared more than once (overloads); every function is written by one worker under its own name, so give each a different name"; break; }
                    if (text.find("@@") == std::string::npos) { candidate.addFile(path, text); continue; }   // a file with no function to delegate is taken as written
                    const Skeleton sk = expandSkeleton(text, path);
                    if (!sk.error.empty()) bad = path + ": " + sk.error;
                    else candidate.addFile(path, sk.text);
                    // The skeleton check: a skeleton that leaves a contract function undefined is refused (it cannot happen after the step above; kept as the check).
                    if (bad.empty() && !fns.empty()) if (const auto missing = undefinedContract(sk.text, path, fns); !missing.empty()) bad = path + ": the skeleton leaves the contract function `" + missing.front().name + "` undefined (" + missing.front().signature + ")";
                    // And a class whose member has no section is refused: nobody would write it, and the link fails (row 71).
                    if (bad.empty() && o.contractSections) if (const auto missing = undefinedMembers(sk.text, path); !missing.empty()) bad = path + ": the class `" + missing.front().className + "` declares the member function `" + missing.front().name + "` and no section defines it; write `@@ " + missing.front().signature + "` after the class";
                }
                if (bad.empty()) bad = precheckCanvas(candidate, harness, o.dir, o.itemId, o.attempt, attemptDir, true);
            }
            if (bad.empty()) { canvas = std::move(candidate); break; }
            traceVerdict(primerTrace, tryNo, "the skeleton was refused: " + bad);
            std::printf("fork skeleton refused: %s\n", bad.substr(0, 200).c_str());
            events::note("skeleton refused: " + bad.substr(0, 120));
            if (tryNo == 1) { res.error = "no usable skeleton: " + bad; return finish(2, "skeleton"); }
            primer.push_back({"assistant", "[your previous reply is not repeated here]"});
            primer.push_back({"user", "That reply cannot be used: " + clip(bad, 1200) + "\nReply again with the structure only." + kPrimerAsk});
        }
        // The sections: every stub marker inside a named function, with the declaration the skeleton gives it.
        for (const auto& path : canvas.paths()) {
            const std::string& text = canvas.text(path);
            const auto v = linesOf(text);
            const auto regions = foldRegions(text, isPythonPath(path), true);
            for (std::size_t i = 0; i < v.size(); ++i) {
                const auto m = v[i].find("not implemented:");
                if (m == std::string::npos) continue;
                std::string name = v[i].substr(m + 16);
                name.erase(0, name.find_first_not_of(" \t"));
                name = name.substr(0, name.find_first_of("'\"` )"));
                const FoldRegion* reg = nullptr;
                for (const auto& r : regions) if (static_cast<int>(i) + 1 >= r.first && static_cast<int>(i) + 1 <= r.bodyTo) reg = &r;
                if (name.empty() || !reg || reg->name.empty()) { std::printf("note: the stub at %s:%zu is not inside a named function; it stays as written\n", path.c_str(), i + 1); continue; }
                if (std::any_of(res.sections.begin(), res.sections.end(), [&](const ForkSection& s) { return s.file == path && s.symbol == reg->name; })) continue;
                ForkSection s;
                s.name = name; s.file = path; s.symbol = reg->name;
                const std::string& decl = v[static_cast<std::size_t>(reg->first) - 1];
                s.indent = decl.substr(0, decl.find_first_not_of(" \t"));
                s.signature = trimmed(decl);
                while (!s.signature.empty() && (s.signature.back() == '{' || s.signature.back() == ':' || s.signature.back() == ' ')) s.signature.pop_back();
                res.sections.push_back(std::move(s));
            }
        }
    }
    persistCanvas(canvas, attemptDir);
    if (o.wallBudgetSec > 0) budget = o.wallBudgetSec + o.wallPerSectionSec * static_cast<int>(res.sections.size());
    std::printf("fork document: %zu file(s), %zu section(s), wall budget %d s%s\n", canvas.paths().size(), res.sections.size(), budget.load(), res.resumedFrom ? " (resumed)" : "");

    std::string gateText;
    if (res.sections.empty()) { std::printf("no stubs in the skeleton: nothing to delegate\n"); const bool ok = gate(gateText); return finish(ok ? 0 : 1, ok ? "" : "tests"); }

    // ---- 2. the width: side by side up to the idle slots ----
    const auto idleSlots = [&]() -> int {
        if (o.idleSlots) return o.idleSlots();
        if (const auto sc = engineSlots(o.engine)) return sc->idle;
        return std::max(1, o.engine.maxConcurrent);
    };
    const auto availableMiB = [&]() -> long { return o.availableMiB ? o.availableMiB() : static_cast<long>(readHostMemory().availableMiB); };
    const ParallelPlan width = planParallel(static_cast<int>(res.sections.size()), idleSlots(), o.parallel, o.sequentialReason, o.memFloorMiB > 0 ? availableMiB() : -1, o.memFloorMiB);
    res.width = width.parallel;
    res.widthReason = width.reason;
    res.shed = width.shed;
    std::printf("fork width: %d of %zu section(s) at once (%s)\n", res.width, res.sections.size(), width.reason.c_str());
    events::note("forked sections: " + std::to_string(res.sections.size()) + ", " + std::to_string(res.width) + " at once (" + width.reason + ")");

    const std::string system = forkSystemBlock(item.persona, harnessBrief(plan->harness));
    std::map<std::string, std::string> committedText;   // section name -> the function text it committed
    /// A stub's marker is still in the file: the section was never written.
    const auto unwritten = [&](const ForkSection& s) {
        if (s.removed || !canvas.has(s.file)) return false;
        const std::string& text = canvas.text(s.file);
        const std::string marker = "not implemented: " + s.name;   // the whole name: `evaluate` is not unwritten because `evaluateTokens` is
        for (std::size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + 1)) if (at + marker.size() >= text.size() || text[at + marker.size()] == '"' || text[at + marker.size()] == '\'') return true;
        return false;
    };
    const auto sectionAt = [&](const std::string& file, int line) -> ForkSection* {
        const auto regions = foldRegions(canvas.text(file), isPythonPath(file), true);
        for (const auto& r : regions) {
            if (line < r.first || line > r.bodyTo) continue;
            for (auto& s : res.sections) if (s.file == file && !s.removed && regionOf(regions, s.symbol) == &r) return &s;
        }
        return nullptr;
    };

    // One section's session: ask, read the reply as its function, put the skeleton's declaration back, commit through the coordinator. `shown` is the
    // revision of each file the prompt showed.
    const auto runSection = [&](ForkSection& s, std::vector<ChatMessage> convo, int base, const std::string& tag, long& phaseTokens) {
        std::string flat = s.name;
        for (char& c : flat) if (c == '.' || c == '/' || c == ':') c = '_';
        const fs::path trace = attemptDir / ("conversation-" + flat + ".md");
        if (!fs::exists(trace)) traceBegin(trace, "Section " + s.name + " of " + o.itemId, o.engine, o.maxTokens);
        for (int tryNo = 0; tryNo < 3; ++tryNo) {
            if (remainingSec() <= 0) { std::lock_guard<std::mutex> l(mu); res.budgetSpent = true; return; }
            traceSent(trace, tryNo, convo, tryNo == 0 ? 0 : convo.size() - 2, -1);
            auto r = chat(seeded(tag + ":" + s.name + ":" + std::to_string(tryNo)), convo, o.maxTokens, remainingSec(), {});
            ++s.calls;
            if (!r) { traceVerdict(trace, tryNo, "the engine call failed: " + r.error()); if (remainingSec() <= 0) { std::lock_guard<std::mutex> l(mu); res.budgetSpent = true; } return; }
            traceReply(trace, tryNo, *r);
            s.promptTokens += r->promptTokens; s.completionTokens += r->completionTokens;
            { std::lock_guard<std::mutex> l(mu); phaseTokens += r->promptTokens + r->completionTokens; }
            // The reply as ONE function: a replaceSymbol block on the section's own function, or the function alone. Its declaration is then replaced by
            // the skeleton's (run-v9: sections dropped a `Class::` qualifier or changed parameters, and each such slip broke the file for all).
            CanvasReply cr = parseCanvasReply(r->content);
            std::string refusal = cr.error, written;
            if (refusal.empty() && !cr.hasEdits()) {
                written = r->content;
                if (written.rfind("```", 0) == 0) { const auto nl = written.find('\n'); written = nl == std::string::npos ? "" : written.substr(nl + 1); }
                if (const auto fence = written.rfind("```"); fence != std::string::npos && written.find_first_not_of(" \t\r\n", fence + 3) == std::string::npos) written.erase(fence);
                if (trimmed(written).empty() || written.find("=== READ:") != std::string::npos) refusal = "the reply held no === EDIT: " + s.file + " SYMBOL " + s.symbol + " === block";
            } else if (refusal.empty()) {
                if (!cr.wholeFiles.empty() || !cr.searchEdits.empty()) refusal = "you own only `" + s.symbol + "`: reply with one === EDIT: " + s.file + " SYMBOL " + s.symbol + " === block, not a file or a search edit";
                int ops = 0;
                for (const auto& t : cr.transactions) for (const auto& op : t.ops) {
                    if (!refusal.empty()) break;
                    ++ops;
                    if (t.path != s.file || op.kind != CanvasOp::Kind::replaceSymbol || leaf(op.symbol) != leaf(s.symbol)) {
                        std::string holder;
                        for (const auto& other : res.sections) if (other.file == t.path && op.kind == CanvasOp::Kind::replaceSymbol && leaf(other.symbol) == leaf(op.symbol)) holder = other.name;
                        refusal = "you own only `" + s.symbol + "` in " + s.file + (holder.empty() ? std::string{": that edit is outside it"} : ": `" + leaf(op.symbol) + "` is held by the worker writing " + holder) + ". Reply with your own function only";
                    } else written = op.text;
                }
                if (refusal.empty() && ops != 1) refusal = "reply with exactly one block for `" + s.symbol + "`";
            }
            std::string shownBack;
            if (refusal.empty()) {
                const std::string function = applySectionSignature(written, s.signature, s.indent, s.file);
                Transaction tx{s.file, base, s.name, {{CanvasOp::Kind::replaceSymbol, 0, 0, s.symbol, function}}};
                std::lock_guard<std::mutex> l(mu);
                if (base > canvas.revision(s.file)) tx.baseRevision = canvas.revision(s.file);
                Canvas scratch = canvas;
                const TxResult applied = scratch.apply(tx);
                if (!applied.ok) { refusal = applied.error; if (applied.error.find("were changed by") != std::string::npos) ++res.conflicts; }
                else {
                    // A commit is refused only for errors INSIDE the section's own function. An error elsewhere in the file is somebody else's (it can
                    // even surface late: a function whose callee was still a stub looked complete to the compiler until the callee was written), and
                    // blocking this section for it would spend its retries on a defect it cannot touch (run-v9: `evaluate`, refused six times). Such
                    // findings are routed to their owners in assembly.
                    const std::string bad = precheckCanvas(scratch, harness, o.dir, o.itemId, o.attempt, attemptDir, true);
                    const auto found = compileFindings(bad, s.file);
                    std::string mine;
                    int from = 0, to = 0;
                    const auto scratchRegions = foldRegions(scratch.text(s.file), isPythonPath(s.file), true);
                    if (const FoldRegion* reg = regionOf(scratchRegions, s.symbol)) { from = reg->first; to = reg->bodyTo; }
                    for (const auto& f : found) if (f.line >= from && f.line <= to) mine += f.text;
                    const bool blocked = !bad.empty() && (found.empty() || !mine.empty() || from == 0);
                    if (blocked) {
                        // The section sees what it wrote, as it would stand in the file, with the file's line numbers the errors use (run-v9: a section
                        // told only "your reply is not repeated" could not see what to change).
                        refusal = "your function does not pass the check, so it was not kept:\n" + clip(mine.empty() ? bad : mine, 1200);
                        if (from > 0) shownBack = "Your function as you wrote it, with the line numbers the errors use:\n" + numbered(scratch.text(s.file), from, std::min(to, from + 150));
                    } else {
                        canvas = std::move(scratch);
                        persistCanvas(canvas, attemptDir);
                        committedText[s.name] = function;
                        s.committed = true;
                        s.lastRefusal.clear();
                        traceVerdict(trace, tryNo, "committed as revision " + std::to_string(applied.revision) + " of " + s.file + (bad.empty() ? std::string{} : " (errors elsewhere in the file are for their owners)"));
                        return;
                    }
                }
            }
            ++s.refused;
            s.lastRefusal = refusal;
            { std::lock_guard<std::mutex> l(mu); ++res.refused; }
            traceVerdict(trace, tryNo, "refused: " + refusal);
            std::printf("section %s: refused: %s\n", s.name.c_str(), refusal.substr(0, 160).c_str());
            convo.push_back({"assistant", "[your previous reply is shown below where it matters]"});
            convo.push_back({"user", "Your reply was REFUSED and nothing changed: " + refusal + "\n" + shownBack + "Reply again with the corrected function:\n=== EDIT: " + s.file + " SYMBOL " + s.symbol + " ===\n<your function>\n=== END EDIT ==="});
        }
    };
    /// Run sessions for `who`, each with the conversation `make` builds for it, at the attempt's width (shed to one under memory pressure).
    const auto runPool = [&](const std::vector<ForkSection*>& who, const std::function<std::vector<ChatMessage>(ForkSection&)>& make, const std::string& tag, long& phaseTokens, bool timeSpans) {
        std::map<std::string, int> shown;
        for (const auto& path : canvas.paths()) shown[path] = canvas.revision(path);
        const auto tPool = Clock::now();
        std::atomic<std::size_t> next{0};
        std::atomic<int> limit{std::min<int>(res.width, static_cast<int>(who.size()))};
        std::vector<std::thread> pool;
        for (int wkr = 0; wkr < limit.load(); ++wkr) {
            pool.emplace_back([&, wkr] {
                for (;;) {
                    if (wkr >= limit.load()) return;   // shed: this worker stops taking sections; the first one carries on alone
                    if (wkr > 0 && o.memFloorMiB > 0 && availableMiB() < o.memFloorMiB) {
                        std::lock_guard<std::mutex> l(mu);
                        if (limit.load() > 1) { limit = 1; res.shed = true; res.widthReason += "; shed to one at a time under memory pressure"; events::note("forks shed: memory pressure"); }
                        return;
                    }
                    if (remainingSec() <= 0) { std::lock_guard<std::mutex> l(mu); res.budgetSpent = true; return; }
                    const std::size_t i = next.fetch_add(1);
                    if (i >= who.size()) return;
                    ForkSection& s = *who[i];
                    std::vector<ChatMessage> convo;
                    { std::lock_guard<std::mutex> l(mu); convo = make(s); }
                    if (timeSpans) s.startSec = since(tPool);
                    runSection(s, std::move(convo), shown[s.file], tag, phaseTokens);
                    if (timeSpans) s.endSec = since(tPool);
                }
            });
        }
        for (auto& t : pool) t.join();
    };

    // The shared prefix of a writing phase: the card and the file as it stood when the phase began, IDENTICAL for every section of the phase (a section
    // is written against the declarations, not against whichever neighbours happened to finish first), so a server that keeps a prompt cache prefills
    // it once per slot.
    const auto writePrefix = [&] {
        std::string list;
        for (const auto& x : res.sections) if (!x.removed) list += "- " + x.name + " (" + x.file + ")\n";
        std::string view = card + "\nThe file as it stands:\n";
        for (const auto& path : canvas.paths()) view += "=== CANVAS: " + path + " (revision " + std::to_string(canvas.revision(path)) + ") ===\n" + foldedView(canvas.text(path), isPythonPath(path), {}, 1u << 30) + "=== END CANVAS ===\n";
        return view + "The functions being written, each by one worker:\n" + list;
    };

    // ---- 3. assembly: write what is unwritten, then converge by routed findings ----
    const auto tSections = Clock::now();
    bool firstPhase = true;
    for (int round = 0;; ++round) {
        res.rounds = round;
        // (a) sections that were never written: only they get a session (accepted ones are never re-run).
        std::vector<ForkSection*> missing;
        for (auto& s : res.sections) if (unwritten(s)) missing.push_back(&s);
        if (!missing.empty()) {
            if (!firstPhase) res.targeted += static_cast<int>(missing.size());
            const std::string prefix = writePrefix();
            const auto writePrompt = [&](ForkSection& s) -> std::vector<ChatMessage> {
                return {{"system", system}, {"user", prefix + "\nYou own `" + s.symbol + "` in " + s.file + "." + (s.brief.empty() ? std::string{} : " " + s.brief) + (s.lastRefusal.empty() ? std::string{} : " Your earlier reply was refused: " + clip(s.lastRefusal, 500)) +
                                                         " Reply with:\n=== EDIT: " + s.file + " SYMBOL " + s.symbol + " ===\n<your function>\n=== END EDIT ==="}};
            };
            runPool(missing, writePrompt, firstPhase ? "section" : "rewrite" + std::to_string(round), firstPhase ? res.sectionTokens : res.integrationTokens, firstPhase);
        }
        if (firstPhase) {
            firstPhase = false;
            res.sectionWallSec = since(tSections);
            std::vector<JobSpan> spans;
            long tokens = 0;
            for (const auto* s : missing) { spans.push_back({s->name, s->startSec, s->endSec}); tokens += s->completionTokens; }
            res.idleShare = accountSpans(spans, res.width, tokens).idleShare;
            // No edit may be lost in the merge: every committed function is in the merged text.
            for (const auto& s : res.sections) {
                if (!s.committed || s.removed) continue;
                std::string want = committedText[s.name];
                while (!want.empty() && (want.back() == '\n' || want.back() == '\r')) want.pop_back();
                if (canvas.text(s.file).find(want) == std::string::npos) ++res.lostEdits;
            }
            for (const auto& s : res.sections) std::printf("section %s: %ld prompt, %ld completion tokens, %d call(s), %s\n", s.name.c_str(), s.promptTokens, s.completionTokens, s.calls, unwritten(s) ? "NOT written" : "committed");
        }
        std::string still;
        for (const auto& s : res.sections) if (unwritten(s)) still += (still.empty() ? "" : ", ") + s.name;
        if (!still.empty()) {
            if (res.budgetSpent || remainingSec() <= 0) { res.budgetSpent = true; res.error = "the wall budget (" + std::to_string(budget.load()) + " s) was spent with sections unwritten: " + still; return finish(1, "budget"); }
            if (round >= o.rounds) { res.error = "sections never accepted: " + still; return finish(1, "sections-missing"); }
            continue;
        }
        // (b) the whole file under the STRICT check. A function nobody calls is removed by the harness, with its prototype: the section that wrote it
        // could only delete it, so no session is spent on it; if it WAS needed, the tests say so next.
        std::string strict = precheckCanvas(canvas, harness, o.dir, o.itemId, o.attempt, attemptDir, false);
        for (int mend = 0; mend < 16 && !strict.empty(); ++mend) {
            bool removedOne = false;
            for (const auto& path : canvas.paths()) {
                if (removedOne) break;
                for (const auto& f : compileFindings(strict, path)) {
                    if (!f.unusedFunction) continue;
                    const std::string& text = canvas.text(path);
                    const FoldRegion* reg = nullptr;
                    const auto regions = foldRegions(text, isPythonPath(path), true);
                    for (const auto& r : regions) if (f.line >= r.first && f.line <= r.bodyTo) reg = &r;
                    if (!reg) continue;
                    Transaction tx{path, canvas.revision(path), "harness", {{CanvasOp::Kind::replaceLines, reg->first, reg->bodyTo, {}, {}}}};
                    ForkSection* owner = sectionAt(path, f.line);
                    if (owner) {   // the prototype the harness wrote for it goes too
                        const auto v = linesOf(text);
                        for (std::size_t i = 0; i < v.size(); ++i) if (trimmed(v[i]) == owner->signature + ";" && (static_cast<int>(i) + 1 < reg->first || static_cast<int>(i) + 1 > reg->bodyTo)) { tx.ops.push_back({CanvasOp::Kind::replaceLines, static_cast<int>(i) + 1, static_cast<int>(i) + 1, {}, {}}); break; }
                    }
                    if (!canvas.apply(tx).ok) continue;
                    if (owner) owner->removed = true;
                    ++res.mechanicalMends;
                    removedOne = true;
                    std::printf("assembly: `%s` is called by nobody: removed by the harness\n", reg->name.c_str());
                    events::note("assembly: removed the uncalled function " + reg->name);
                    break;
                }
            }
            if (!removedOne) break;
            persistCanvas(canvas, attemptDir);
            strict = precheckCanvas(canvas, harness, o.dir, o.itemId, o.attempt, attemptDir, false);
        }
        // A name the compiler calls unused is marked `[[maybe_unused]]` by the harness (the mend `silence-unused`, mends.hpp): run-v10 sent one such error to
        // its section three times and the model did not clear it once. No session is spent on it.
        for (int mend = 0; mend < 8 && !strict.empty(); ++mend) {
            const auto m = matchMend(builtinMends(), strict);
            if (!m || m->action != "silence-unused" || !canvas.has(m->file)) break;
            const auto mended = silenceUnused(canvas.text(m->file), m->file, m->line, m->column, m->symbol);
            if (!mended || !canvas.replaceText(m->file, *mended, "harness").ok) break;
            ++res.mechanicalMends;
            persistCanvas(canvas, attemptDir);
            std::printf("assembly: `%s` at %s:%d is unused: marked [[maybe_unused]] by the harness\n", m->symbol.c_str(), m->file.c_str(), m->line);
            events::note("assembly: marked the unused name " + m->symbol + " [[maybe_unused]]");
            strict = precheckCanvas(canvas, harness, o.dir, o.itemId, o.attempt, attemptDir, false);
        }
        bool undone = false;
        if (!strict.empty() && best) {   // the round's edits broke a file the tests had already judged: they are undone, never carried on (row 70)
            restoreBest("the round's edits do not compile");
            undone = true;
            strict.clear();
        }
        if (!strict.empty()) {
            // A compile error goes to the owner of the function it is in, with only its own errors and its own function open.
            std::map<ForkSection*, std::string> findings;
            std::string outside;
            for (const auto& path : canvas.paths())
                for (const auto& f : compileFindings(strict, path)) { if (ForkSection* s = sectionAt(path, f.line)) findings[s] += f.text; else outside += f.text; }
            (void)writeFile(attemptDir / "gate.txt", "GATE FAIL: the assembled file does not compile:\n" + strict + "\n");
            if (round >= o.rounds || remainingSec() <= 0) {
                res.budgetSpent = res.budgetSpent || remainingSec() <= 0;
                res.error = "the assembled file does not compile: " + clip(strict, 600);
                return finish(1, remainingSec() <= 0 ? "budget" : findings.empty() ? "compile-outside-sections" : "compile");
            }
            if (findings.empty()) { res.error = "the assembled file does not compile, and the errors are outside every section (in the declared structure): " + clip(outside.empty() ? strict : outside, 600); return finish(1, "compile-outside-sections"); }
            std::vector<ForkSection*> owners;
            for (auto& [s, text] : findings) owners.push_back(s);
            res.targeted += static_cast<int>(owners.size());
            std::printf("assembly round %d: %zu function(s) hold compile errors; each goes to its owner\n", round + 1, owners.size());
            runPool(owners, [&](ForkSection& s) -> std::vector<ChatMessage> {
                const std::string view = "=== CANVAS: " + s.file + " (revision " + std::to_string(canvas.revision(s.file)) + ") ===\n" + sectionView(canvas.text(s.file), isPythonPath(s.file), {s.symbol}) + "=== END CANVAS ===\n";
                return {{"system", system}, {"user", "The file was assembled from functions written separately. It does not compile yet, and these errors are inside YOUR function `" + s.symbol + "`:\n" + clip(findings[&s], 1500) + "\n" + view +
                                                         "Your function is shown open; the others are folded to their declarations. Fix exactly these errors. Reply with:\n=== EDIT: " + s.file + " SYMBOL " + s.symbol + " ===\n<your function>\n=== END EDIT ==="}};
            }, "compile" + std::to_string(round), res.integrationTokens, false);
            continue;
        }
        // (c) it compiles strictly: the item's tests judge it.
        if (!undone) {
            if (gate(gateText)) {
                res.testsFailed = 0;
                res.testsTotal = testCounts(gateText).second;
                std::printf("RESULT: PASS after %d assembly round(s); width %d, %d refused, %d removed by the harness\n", round, res.width, res.refused, res.mechanicalMends);
                return finish(0, "");
            }
            const auto [failed, total] = testCounts(gateText);
            // The gate failed on something the mends table knows (a symbol nobody defined: the link, not a test): no section can repair that by being
            // asked again. The attempt ends here; the engine records the mend and the next attempt is its action.
            if (failed < 0 && !best && fs::exists(attemptDir / "mend.json")) { res.error = "the gate failed with a known mechanical error, which the harness mends: " + clip(firstErrorLine(gateText), 300); return finish(1, "mendable"); }
            if (best && (failed < 0 || failed > best->failed)) { restoreBest("the round left " + (failed < 0 ? std::string{"a file the tests cannot judge"} : std::to_string(failed) + " tests failing")); undone = true; }
            else if (failed >= 0) keepBest(failed, total, gateText);
        }
        if (undone) gateText = best->gateText;
        if (round >= o.rounds || remainingSec() <= 0) { res.budgetSpent = res.budgetSpent || remainingSec() <= 0; res.error = "the assembled file compiles but its tests fail"; return finish(1, remainingSec() <= 0 ? "budget" : "tests"); }
        // A failing test goes to the sections it names; when it names none (or naming them did not help), to all.
        // First by MESSAGE (row 64): a test that fails with a message one function produces goes to that function, in any round, with the failure quoted.
        std::vector<std::pair<std::string, std::string>> texts;
        for (const auto& s : res.sections) {
            if (s.removed || !canvas.has(s.file)) continue;
            texts.emplace_back(s.name, functionText(canvas.text(s.file), s.file, s.symbol));
        }
        const std::map<std::string, std::string> byMessage = sectionsOwningFailure(gateText, texts);
        std::vector<ForkSection*> implicated;
        for (auto& s : res.sections) if (!s.removed && (byMessage.count(s.name) || (round == 0 && namesWord(gateText, s.symbol)))) implicated.push_back(&s);
        res.routedByMessage += static_cast<int>(byMessage.size());
        if (implicated.empty()) for (auto& s : res.sections) if (!s.removed) implicated.push_back(&s);
        res.targeted += static_cast<int>(implicated.size());
        // The failing tests as they are written (rows 25/26): the assertion is quoted, not only its name.
        std::vector<std::pair<std::string, std::string>> testFiles;
        for (const auto& t : item.gate.tests) if (const std::string text = slurp(attemptDir / "ws" / t); !text.empty()) testFiles.emplace_back(t, text);
        const std::string quotedTests = failingTestSources(gateText, testFiles);
        std::printf("assembly round %d: the tests failed; %zu section(s) go back\n", round + 1, implicated.size());
        runPool(implicated, [&](ForkSection& s) -> std::vector<ChatMessage> {
            const std::string view = "=== CANVAS: " + s.file + " (revision " + std::to_string(canvas.revision(s.file)) + ") ===\n" + sectionView(canvas.text(s.file), isPythonPath(s.file), {s.symbol}) + "=== END CANVAS ===\n";
            return {{"system", system}, {"user", card + "\nThe functions were written separately and merged. The merged file was checked by running the tests and failed:\n" + clip(gateText, 1500) + "\n" + quotedTests + view +
                                                     (byMessage.count(s.name) ? "These failures carry a message that YOUR function produces, so the path that fails runs through it:\n" + clip(byMessage.at(s.name), 600) : std::string{}) +
                                                     "You own `" + s.symbol + "` in " + s.file + " (shown open; the others are folded). It may be at fault. Reply with the corrected function:\n=== EDIT: " + s.file + " SYMBOL " + s.symbol + " ===\n<your function>\n=== END EDIT ==="}};
        }, "tests" + std::to_string(round), res.integrationTokens, false);
    }
}

int runMendApply(const std::vector<std::string>& args)
{
    const fs::path dir = arg(args, "workflow-dir");
    const std::string itemId = arg(args, "item"), action = arg(args, "action"), file = arg(args, "file"), symbol = arg(args, "symbol");
    const int attempt = std::atoi(arg(args, "attempt", "0").c_str()), line = std::atoi(arg(args, "line", "0").c_str()), column = std::atoi(arg(args, "column", "0").c_str());
    if (dir.empty() || itemId.empty() || attempt < 2 || file.empty() || file[0] == '/' || file.find("..") != std::string::npos) { std::fprintf(stderr, "mend-apply: --workflow-dir D --item I --attempt N (2 or more) --action A --file F --line L --column C --symbol S\n"); return 64; }
    if (action != "silence-unused") { std::fprintf(stderr, "mend-apply: '%s' is not an action the harness applies without a model (silence-unused is)\n", action.c_str()); return 64; }
    const fs::path itemDir = dir / "items" / itemId, before = itemDir / ("attempt-" + std::to_string(attempt - 1)), now = itemDir / ("attempt-" + std::to_string(attempt));
    const std::string text = slurp(before / "out" / file);
    if (text.empty()) { std::fprintf(stderr, "mend-apply: attempt %d left no %s\n", attempt - 1, file.c_str()); return 1; }
    const auto mended = silenceUnused(text, file, line, column, symbol);
    if (!mended) { std::fprintf(stderr, "mend-apply: `%s` is not declared at %s:%d in a way the harness can mark unused\n", symbol.c_str(), file.c_str(), line); return 1; }
    std::error_code ec;
    fs::remove_all(now / "out", ec);
    fs::create_directories(now / "out", ec);
    fs::copy(before / "out", now / "out", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);   // everything the attempt before wrote is kept
    if (ec || !writeFile(now / "out" / file, *mended)) { std::fprintf(stderr, "mend-apply: cannot write attempt %d\n", attempt); return 1; }
    if (fs::exists(before / "fork.json")) fs::copy_file(before / "fork.json", now / "fork.json", fs::copy_options::overwrite_existing, ec);   // a later fork attempt resumes from here as from the attempt before
    (void)writeFile(now / "mend.txt", "silence-unused: `" + symbol + "` at " + file + ":" + std::to_string(line) + " marked [[maybe_unused]] by the harness; nothing else was changed\n");
    std::printf("mend silence-unused: `%s` at %s:%d marked [[maybe_unused]]\n", symbol.c_str(), file.c_str(), line);
    return 0;
}

int runLlmFork(const std::vector<std::string>& args)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    ForkOptions o;
    o.dir = arg(args, "workflow-dir");
    o.itemId = arg(args, "item");
    o.attempt = std::atoi(arg(args, "attempt", "1").c_str());
    o.maxTokens = std::atoi(arg(args, "max-tokens", "3500").c_str());
    o.timeoutSec = std::atoi(arg(args, "timeout", "3600").c_str());
    o.rounds = std::max(0, std::atoi(arg(args, "repair-turns", "2").c_str()));
    o.parallel = std::atoi(arg(args, "parallel", "0").c_str());
    o.sequentialReason = arg(args, "sequential-reason");
    o.memFloorMiB = std::atol(arg(args, "mem-floor-mib", "0").c_str());
    o.wallBudgetSec = std::atoi(arg(args, "attempt-wall", "900").c_str());
    o.wallPerSectionSec = std::atoi(arg(args, "section-wall", "150").c_str());
    o.fresh = arg(args, "fresh", "0") == "1";
    o.contractSections = arg(args, "contract-sections", "1") != "0";
    o.mendSection = arg(args, "mend-section");
    o.mendFile = arg(args, "mend-file");
    auto profile = EngineProfile::parse(arg(args, "engine"));
    if (!profile) { std::fprintf(stderr, "llm-fork: %s\n", profile.error().c_str()); return 64; }
    o.engine = *profile;
    // The manager's `think` strategy for this item (strategy.json, written by the daemon): reasoning on and room for it. Without this a fork after the
    // strategy was the same fork again (catalogue row 73).
    if (slurp(o.dir / "items" / o.itemId / "strategy.json").find("\"think\":true") != std::string::npos) {
        o.engine.thinking = true;
        o.maxTokens = std::max(o.maxTokens, 6000);
        o.timeoutSec = std::max(o.timeoutSec, 3600);
        std::printf("manager strategy: thinking on, %d token budget\n", o.maxTokens);
    }
    const ForkResult r = runCanvasFork(o);
    if (!r.error.empty()) std::fprintf(stderr, "llm-fork: %s (%s)\n", r.error.c_str(), r.failure.c_str());
    // As a workflow's agent job the attempt succeeds when it wrote its files: the gate job is the judge, as for every executor (a near pass is the
    // gate's exit 3 there; alone, the fork's own exit code is 3).
    if (arg(args, "agent-exit", "0") == "1") return r.exitCode == 2 ? 2 : 0;
    return r.exitCode;
}

}  // namespace sub0squad
