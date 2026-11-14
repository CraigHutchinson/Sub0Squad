#include "sub0squad/blocks.hpp"

#include "agent_impl.hpp"
#include "json_util.hpp"
#include "sub0squad/canvas.hpp"
#include "sub0squad/edits.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/sandbox.hpp"
#include "sub0squad/scenario.hpp"
#include "sub0squad/turn_control.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <random>
#include <sstream>
#include <thread>

namespace sub0squad {

namespace fs = std::filesystem;
using namespace agent;

namespace {

std::vector<std::string> splitLines(const std::string& t)
{
    std::vector<std::string> v;
    std::istringstream in(t);
    for (std::string l; std::getline(in, l);) v.push_back(l);
    return v;
}
std::string joinLines(const std::vector<std::string>& v) { std::string s; for (const auto& l : v) s += l + "\n"; return s; }

}  // namespace

// ---- block 1 ---------------------------------------------------------------------------------------------------------------------------------------
bool parseReplyCase(const std::string& text, ReplyCase& c, std::string& error)
{
    c = {};
    std::string kind, arg, body;
    bool inSection = false;
    const auto close = [&] {
        if (!inSection) return;
        if (kind == "base") c.base[arg] = body;
        else if (kind == "want") c.want[arg] = body;
        else if (kind == "reply") { if (!body.empty()) body.pop_back(); if (arg == "cut" && !body.empty() && body.back() == '\\') body.pop_back(); c.replies.push_back(body); }
        body.clear();
    };
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("--- ", 0) == 0) {
            close();
            std::istringstream h(line.substr(4));
            kind.clear(); arg.clear();
            h >> kind >> arg;
            if (kind != "base" && kind != "want" && kind != "reply") { error = "unknown section '" + kind + "'"; return false; }
            if (kind != "reply" && arg.empty()) { error = "a " + kind + " section names no path"; return false; }
            inSection = true;
            continue;
        }
        if (!inSection) {
            if (line.rfind("# ", 0) != 0) continue;
            const auto colon = line.find(": ");
            if (colon == std::string::npos) continue;
            const std::string key = line.substr(2, colon - 2), value = line.substr(colon + 2);
            if (key == "name") c.name = value;
            else if (key == "expect-error") c.expectError = value;
            else if (key == "expect-reads") c.expectReads = std::atoi(value.c_str());
            else if (key == "classic") c.classic = value == "yes";
            else if (key == "allowed") { std::istringstream a(value); for (std::string p; a >> p;) c.allowed.push_back(p); }
            continue;
        }
        body += line + "\n";
    }
    close();
    if (c.replies.empty()) { error = "the case has no reply"; return false; }
    if (c.expectError.empty() && c.expectReads < 0 && c.want.empty()) { error = "the case states no outcome (want, expect-error or expect-reads)"; return false; }
    return true;
}

std::string runReplyCase(const ReplyCase& c)
{
    std::set<std::string> allowed(c.allowed.begin(), c.allowed.end());
    if (allowed.empty()) { for (const auto& [p, t] : c.base) allowed.insert(p); for (const auto& [p, t] : c.want) allowed.insert(p); }
    std::string text;
    for (std::size_t i = 0; i < c.replies.size(); ++i) text = i == 0 ? c.replies[i] : text + continuationText(text, c.replies[i]);
    Canvas canvas;
    for (const auto& [p, t] : c.base) canvas.addFile(p, t);
    const CanvasReply parsed = parseCanvasReply(text);
    if (c.expectReads >= 0) {
        if (parsed.hasEdits()) return "expected only read requests, but the reply holds an edit";
        if (static_cast<int>(parsed.reads.size()) != c.expectReads) return "expected " + std::to_string(c.expectReads) + " read request(s), found " + std::to_string(parsed.reads.size());
        return {};
    }
    Canvas scratch = canvas;
    std::string refusal = parsed.hasEdits() || !parsed.error.empty() ? applyCanvasReply(scratch, parsed, allowed, "case") : std::string{"the reply holds no edit"};
    if (!c.expectError.empty()) {
        if (refusal.empty()) return "expected a refusal containing '" + c.expectError + "', but the reply applied";
        if (refusal.find(c.expectError) == std::string::npos) return "refused with '" + refusal + "', expected a message containing '" + c.expectError + "'";
        return {};   // the scratch copy is discarded: nothing changed, by construction of the commit rule
    }
    if (!refusal.empty()) return "refused: " + refusal;
    for (const auto& [p, want] : c.want) {
        if (!scratch.has(p)) return "'" + p + "' was not written";
        if (scratch.text(p) != want) {
            const auto a = splitLines(scratch.text(p)), b = splitLines(want);
            std::size_t i = 0;
            while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
            return "'" + p + "' differs from the intended text at line " + std::to_string(i + 1) + ": got '" + (i < a.size() ? a[i] : std::string{"<end>"}) + "', want '" + (i < b.size() ? b[i] : std::string{"<end>"}) + "'";
        }
    }
    if (c.classic) {   // the classic applier must agree on the forms it supports
        std::string err;
        const auto classic = applyEdits(text, c.base, allowed, err);
        if (!classic) return "the classic applier refused it: " + err;
        for (const auto& [p, want] : c.want) if (classic->count(p) == 0 || classic->at(p) != want) return "the classic applier gives a different '" + p + "'";
    }
    return {};
}

FixtureReport runReplyFixtures(const fs::path& dir)
{
    FixtureReport r;
    std::error_code ec;
    std::vector<fs::path> files;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); ++it) if (it->path().extension() == ".case") files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        ++r.cases;
        ReplyCase c;
        std::string error;
        if (!parseReplyCase(slurp(f), c, error)) { ++r.failed; r.problems.push_back(f.filename().string() + ": unreadable case: " + error); continue; }
        if (const std::string bad = runReplyCase(c); !bad.empty()) { ++r.failed; r.problems.push_back(f.filename().string() + ": " + bad); }
    }
    return r;
}

// ---- block 5 ---------------------------------------------------------------------------------------------------------------------------------------
namespace {

struct ModelOp { CanvasOp::Kind kind; int from, to; std::vector<std::string> text; };   // replaceLines [from,to], or insertAfter from

/// The independent model: operations in the numbering of `base`, applied together from the bottom up. Returns false when they are invalid or overlap.
bool modelApply(std::vector<std::string>& lines, std::vector<ModelOp> ops)
{
    const int n = static_cast<int>(lines.size());
    struct Span { int lo, hi; std::vector<std::string> text; };   // replaces the half-open 0-based range [lo, hi)
    std::vector<Span> spans;
    for (const auto& op : ops) {
        if (op.kind == CanvasOp::Kind::insertAfter) { if (op.from < 0 || op.from > n) return false; spans.push_back({op.from, op.from, op.text}); }
        else { if (op.from < 1 || op.to < op.from || op.to > n) return false; spans.push_back({op.from - 1, op.to, op.text}); }
    }
    std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.lo < b.lo || (a.lo == b.lo && a.hi < b.hi); });
    for (std::size_t i = 1; i < spans.size(); ++i) {
        const bool prevInsert = spans[i - 1].lo == spans[i - 1].hi, curInsert = spans[i].lo == spans[i].hi;
        // the canvas's rule, restated independently: an insertion is the empty span just before line lo+1; two spans may not share a position
        const int prevEnd = prevInsert ? spans[i - 1].lo : spans[i - 1].hi;          // 1-based last line touched (an insertion "touches" up to lo)
        const int curStart = spans[i].lo + 1;                                        // 1-based first line touched
        if (curStart <= prevEnd) return false;
        (void)curInsert;
    }
    for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
        lines.erase(lines.begin() + it->lo, lines.begin() + it->hi);
        lines.insert(lines.begin() + it->lo, it->text.begin(), it->text.end());
    }
    return true;
}

Transaction toTransaction(const std::string& path, int base, const std::vector<ModelOp>& ops)
{
    Transaction tx{path, base, "model", {}};
    for (const auto& op : ops) tx.ops.push_back({op.kind, op.from, op.kind == CanvasOp::Kind::insertAfter ? 0 : op.to, {}, joinLines(op.text)});
    return tx;
}

std::string toReply(const std::string& path, const std::vector<ModelOp>& ops)
{
    std::string r;
    for (const auto& op : ops) {
        r += "=== EDIT: " + path + (op.kind == CanvasOp::Kind::insertAfter ? " AFTER " + std::to_string(op.from) : " LINES " + std::to_string(op.from) + "-" + std::to_string(op.to)) + " ===\n";
        r += joinLines(op.text) + "=== END EDIT ===\n";
    }
    return r;
}

}  // namespace

ModelCheck canvasModelCheck(int cases, unsigned seed)
{
    ModelCheck out;
    std::mt19937 rng(seed);
    const auto pick = [&](int lo, int hi) { return lo + static_cast<int>(rng() % static_cast<unsigned>(hi - lo + 1)); };
    int label = 0;
    const auto fresh = [&](int n) { std::vector<std::string> v; for (int i = 0; i < n; ++i) v.push_back("line " + std::to_string(++label)); return v; };
    const auto randomOps = [&](int count, int lineCount) {
        std::vector<ModelOp> ops;
        for (int i = 0; i < count; ++i) {
            if (pick(0, 3) == 0) ops.push_back({CanvasOp::Kind::insertAfter, pick(0, lineCount), 0, fresh(pick(1, 3))});
            else { const int a = pick(1, std::max(1, lineCount)), b = std::min(lineCount, a + pick(0, 3)); ops.push_back({CanvasOp::Kind::replaceLines, a, std::max(a, b), fresh(pick(0, 3))}); }
        }
        return ops;
    };
    const auto fail = [&](int c, int step, const std::string& what) {
        ++out.failures;
        if (out.firstFailure.empty()) out.firstFailure = "case " + std::to_string(c) + " (seed " + std::to_string(seed) + "), step " + std::to_string(step) + ": " + what;
    };
    for (int c = 0; c < cases; ++c) {
        ++out.cases;
        const std::string path = "f.txt";
        Canvas canvas;
        std::vector<std::vector<std::string>> history = {fresh(pick(4, 30))};   // history[i] is the model's text at revision i + 1
        canvas.addFile(path, joinLines(history[0]));
        const int steps = pick(3, 9);
        bool bad = false;
        for (int s = 0; s < steps && !bad; ++s) {
            ++out.steps;
            const int current = static_cast<int>(history.size());
            const int kind = pick(0, 9);
            if (kind == 0 && current > 1) {   // revert to an earlier revision
                const int to = pick(1, current - 1);
                const TxResult r = canvas.revertTo(path, to, "model");
                history.push_back(history[static_cast<std::size_t>(to) - 1]);
                if (!r.ok || r.revision != current + 1) { fail(c, s, "revert was refused"); bad = true; }
            } else if (kind == 1 && current > 1) {
                // A STALE transaction, made against the previous revision. Expected: the same operations applied in the model on top of what the newer
                // revision did, when they do not touch its lines; otherwise a refusal that changes nothing. The model decides by replaying both together.
                const auto ops = randomOps(pick(1, 2), static_cast<int>(history[static_cast<std::size_t>(current) - 2].size()));
                const TxResult r = canvas.apply(toTransaction(path, current - 1, ops));
                if (r.ok) {
                    // Soundness of a rebase: the lines the stale edit wrote must all be present, in order, in the new text, and the revision count grew by one.
                    const auto now = splitLines(canvas.text(path));
                    std::vector<std::string> wrote;
                    {   // in file order
                        auto sorted = ops;
                        std::sort(sorted.begin(), sorted.end(), [](const ModelOp& a, const ModelOp& b) { return a.from < b.from; });
                        for (const auto& op : sorted) wrote.insert(wrote.end(), op.text.begin(), op.text.end());
                    }
                    std::size_t at = 0;
                    for (const auto& l : now) if (at < wrote.size() && l == wrote[at]) ++at;
                    if (at != wrote.size()) { fail(c, s, "a rebased transaction lost lines it wrote"); bad = true; }
                    history.push_back(now);
                } else if (canvas.text(path) != joinLines(history.back()) || canvas.revision(path) != current) { fail(c, s, "a refused stale transaction changed the file"); bad = true; }
            } else if (kind == 2) {   // a malformed transaction: out of range; must be refused whole even though its first operation is valid
                auto ops = randomOps(1, static_cast<int>(history.back().size()));
                ops.push_back({CanvasOp::Kind::replaceLines, static_cast<int>(history.back().size()) + 2, static_cast<int>(history.back().size()) + 3, fresh(1)});
                const TxResult r = canvas.apply(toTransaction(path, current, ops));
                if (r.ok || canvas.text(path) != joinLines(history.back()) || canvas.revision(path) != current) { fail(c, s, "an out-of-range transaction was not refused whole"); bad = true; }
            } else {   // an up-to-date transaction of one to three operations; half of them go through the reply text
                const auto ops = randomOps(pick(1, 3), static_cast<int>(history.back().size()));
                auto model = history.back();
                const bool valid = modelApply(model, ops);
                Transaction tx = toTransaction(path, current, ops);
                if (pick(0, 1) == 0) {
                    const CanvasReply parsed = parseCanvasReply(toReply(path, ops));
                    if (!parsed.error.empty() || parsed.transactions.size() != 1) { fail(c, s, "the reply form did not parse: " + parsed.error); bad = true; break; }
                    tx = parsed.transactions[0];
                    tx.baseRevision = current;
                }
                const TxResult r = canvas.apply(tx);
                if (r.ok != valid) { fail(c, s, std::string{"the canvas "} + (r.ok ? "accepted" : "refused (" + r.error + ")") + " what the model " + (valid ? "accepts" : "refuses")); bad = true; }
                else if (valid) { history.push_back(model); if (canvas.text(path) != joinLines(model)) { fail(c, s, "the text differs from the model"); bad = true; } }
                else if (canvas.text(path) != joinLines(history.back()) || canvas.revision(path) != current) { fail(c, s, "a refused transaction changed the file"); bad = true; }
            }
            if (!bad && (canvas.revision(path) != static_cast<int>(history.size()) || canvas.text(path) != joinLines(history.back()))) { fail(c, s, "revision count or text out of step with the model"); bad = true; }
        }
        for (std::size_t i = 0; i < history.size() && !bad; ++i)   // every revision is kept
            if (canvas.textAt(path, static_cast<int>(i) + 1) != joinLines(history[i])) { fail(c, steps, "revision " + std::to_string(i + 1) + " is not retrievable"); bad = true; }
    }
    return out;
}

// ---- block 6 ---------------------------------------------------------------------------------------------------------------------------------------
ChatFn slotModelEngine(std::function<std::string(const std::vector<ChatMessage>&)> answer, SlotModel model, double msPerSimSecond)
{
    auto inFlight = std::make_shared<std::atomic<int>>(0);
    return [answer = std::move(answer), model, msPerSimSecond, inFlight](const EngineProfile&, const std::vector<ChatMessage>& messages, int maxTokens, int, const StreamGuard&) -> std::expected<ChatResult, std::string> {
        ChatResult r;
        r.content = answer(messages);
        std::size_t promptChars = 0;
        for (const auto& m : messages) promptChars += m.content.size();
        r.promptTokens = static_cast<long>(promptChars / 4 + 1);
        r.completionTokens = std::min<long>(static_cast<long>(r.content.size() / 4 + 1), maxTokens);
        r.finishReason = "stop";
        const int n = ++*inFlight;
        const double prefill = model.prefillTps > 0 ? static_cast<double>(r.promptTokens) / model.prefillTps : 0.0;
        const double decode = static_cast<double>(r.completionTokens) / std::max(1e-9, model.perSequenceTps(n));
        r.promptMs = prefill * 1000.0;
        r.predictedMs = decode * 1000.0;
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>((prefill + decode) * msPerSimSecond));
        --*inFlight;
        return r;
    };
}

namespace {

/// The reference Python engine as a DECLARED skeleton (what a primer replies): every function of four lines or more becomes one `@@ signature` line;
/// `functions` receives the reference text of each.
std::string pythonSkeleton(const std::string& ref, std::map<std::string, std::string>& functions)
{
    const auto lines = splitLines(ref);
    std::vector<bool> drop(lines.size(), false);
    std::map<std::size_t, std::string> declAt;
    for (const auto& r : foldRegions(ref, true, true)) {
        if (r.name.empty() || r.name.rfind("__", 0) == 0 || r.bodyTo - r.first < 3 || functions.count(r.name)) continue;
        std::string text;
        for (int k = r.first; k <= r.bodyTo; ++k) { text += lines[static_cast<std::size_t>(k) - 1] + "\n"; drop[static_cast<std::size_t>(k) - 1] = true; }
        functions[r.name] = text;
        const std::string& sig = lines[static_cast<std::size_t>(r.first) - 1];
        const auto ind = sig.find_first_not_of(" \t");
        declAt[static_cast<std::size_t>(r.first) - 1] = sig.substr(0, ind) + "@@ " + sig.substr(ind, sig.rfind(':') - ind);
    }
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (const auto d = declAt.find(i); d != declAt.end()) out += d->second + "\n";
        if (!drop[i]) out += lines[i] + "\n";
    }
    return out;
}

}  // namespace

ParallelProof parallelSectionsProof(const fs::path& workDir, int width, const SlotModel& model)
{
    ParallelProof p;
    const Harness* h = findHarness("py-unittest");
    if (!h || !h->mockFile || !h->gate) { p.error = "the py-unittest harness is not registered"; return p; }
    const std::string file = "calc/engine.py", ref = h->mockFile(file, false);
    std::map<std::string, std::string> functions;
    const std::string skeleton = pythonSkeleton(ref, functions);
    if (functions.size() < 4) { p.error = "the reference has too few functions to fork"; return p; }
    const auto answer = [&](const std::vector<ChatMessage>& m) -> std::string {
        const std::string& ask = m.back().content;
        if (ask.find("Declare the STRUCTURE") != std::string::npos) return "=== SKELETON: " + file + " ===\n" + skeleton + "=== END SKELETON ===\n";
        const auto own = ask.rfind("You own `");
        if (own == std::string::npos) return "I do not know what to write.";
        const std::string name = ask.substr(own + 9, ask.find('`', own + 9) - own - 9);
        const auto f = functions.find(name);
        return f == functions.end() ? std::string{"I do not know that function."} : "=== EDIT: " + file + " SYMBOL " + name + " ===\n" + f->second + "=== END EDIT ===\n";
    };
    ForkResult results[2];
    std::string merged[2];
    for (int run = 0; run < 2; ++run) {
        const fs::path dir = workDir / (run == 0 ? "sequential" : "parallel");
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        if (runMockPlanner({"--task", "calculator-py", "--out", (dir / "plan.json").string()}) != 0) { p.error = "cannot write the plan"; return p; }
        if (runMockAgent({"--workflow-dir", dir.string(), "--item", "test-engine", "--tier", "local", "--attempt", "1", "--delay-ms", "0"}) != 0 ||
            h->gate({"--workflow-dir", dir.string(), "--item", "test-engine", "--attempt", "1"}) != 0) { p.error = "the tester fixture did not pass its gate"; return p; }
        ForkOptions o;
        o.dir = dir; o.itemId = "dev-engine"; o.attempt = 1; o.rounds = 0;
        o.engine.name = "slot-model"; o.engine.maxConcurrent = model.slots; o.engine.seed = 7;
        o.chat = slotModelEngine(answer, model, 2.0);
        o.idleSlots = [&] { return model.slots; };
        o.availableMiB = [] { return 1L << 20; };
        if (run == 0) o.sequentialReason = "the proving ground's sequential baseline"; else o.parallel = width;
        results[run] = runCanvasFork(o);
        merged[run] = slurp(dir / "items" / "dev-engine" / "attempt-1" / "out" / file);
    }
    p.ran = true;
    p.sections = static_cast<int>(results[1].sections.size());
    p.width = results[1].width;
    p.sequentialPassed = results[0].passed && results[0].width == 1;
    p.parallelPassed = results[1].passed;
    p.identical = !merged[0].empty() && merged[0] == merged[1] && merged[1] == ref;
    p.conflicts = results[0].conflicts + results[1].conflicts;
    p.lostEdits = results[0].lostEdits + results[1].lostEdits;
    p.idleShare = results[1].idleShare;
    std::vector<SectionJob> jobs;
    long prefix = 0;
    for (const auto& s : results[1].sections) { jobs.push_back({s.name, s.promptTokens, 0, s.completionTokens}); prefix = prefix == 0 ? s.promptTokens : std::min(prefix, s.promptTokens); }
    for (std::size_t i = 1; i < jobs.size(); ++i) jobs[i].cachedTokens = prefix * 9 / 10;   // the shared prefix (system, card, skeleton) is prefilled once
    const long skeletonTokens = results[1].skeletonTokens;
    p.model = parallelMetrics(model, {"skeleton", skeletonTokens * 2 / 3, 0, skeletonTokens / 3}, jobs, {"integration", 0, 0, 0}, p.width);
    return p;
}

// ---- the report --------------------------------------------------------------------------------------------------------------------------------
namespace {

struct BlockDef { int id; const char* name; const char* how; const char* command; };
const BlockDef kBlocks[] = {
    {0, "engine transport", "by ctest", "ctest -R test_stream"},
    {1, "reply formats", "here", "sub0squad block run --block 1 --fixtures tests/data/replies"},
    {2, "gates and judges", "gates", "sub0squad block run --block 2 --exe PATH-OF-SUB0SQUAD   (or: sub0squad scenario selftest)"},
    {3, "single-section capability", "needs model", "sub0squad scenario run --engine ENGINE --trials 10 --out out/scenarios/NAME"},
    {4, "folded views and reads", "by ctest", "ctest -R 'test_workflow|test_canvas_agent'   (the read-then-edit scenario needs a model: sub0squad scenario run --only read-then-edit, not built)"},
    {5, "canvas transactions", "here", "sub0squad block run --block 5"},
    {6, "parallel sections", "here", "sub0squad block run --block 6   (model-free harness; the real measurement: docs/BLOCK_PROVING_GROUNDS.md, block 6 commands)"},
    {7, "section integration", "not built", "the seam scenario (docs/BLOCK_PROVING_GROUNDS.md)"},
    {8, "division", "not built", "the division validator over known divisions"},
    {9, "decision policy and the ladder", "by ctest", "ctest -R test_workflow   (golden logs: every decision of 33 scenarios is in tests/data/actions and compared byte for byte)"},
    {10, "process as data", "by ctest", "ctest -R 'test_predicate|test_process|test_workflow'   (stages as data; the goldens of the engine before the refactor are reproduced: docs/PROCESS_DESIGN.md section 13)"},
    {11, "the whole on a platform", "needs model", "a workflow run with --frontier off on the 35B"},
    {12, "the whole at scale", "needs model", "docs/KNOWLEDGE_STORE_SCOPE.md"},
};

std::string fmt(const char* f, double a, double b = 0, double c = 0) { char buf[200]; std::snprintf(buf, sizeof buf, f, a, b, c); return buf; }

}  // namespace

std::vector<BlockState> runBlocks(const BlockOptions& o)
{
    std::vector<BlockState> out;
    for (const auto& b : kBlocks) {
        if (o.only >= 0 && o.only != b.id) continue;
        BlockState s;
        s.id = b.id; s.name = b.name; s.command = b.command;
        const std::string how = b.how;
        if (how == "by ctest" || how == "needs model" || how == "not built") { s.state = how; s.evidence = how == "by ctest" ? "proven by the test suite, not re-run by this command" : ""; }
        else if (b.id == 1) {
            if (o.fixtures.empty() || !fs::exists(o.fixtures)) { s.state = "not run"; s.evidence = "no fixture directory (--fixtures)"; }
            else {
                const FixtureReport r = runReplyFixtures(o.fixtures);
                s.state = r.cases > 0 && r.failed == 0 ? "proven" : "failed";
                s.evidence = std::to_string(r.cases - r.failed) + " of " + std::to_string(r.cases) + " recorded-reply cases give the intended result";
                for (const auto& p : r.problems) s.evidence += "; " + p;
            }
        } else if (b.id == 2) {
            if (o.exe.empty()) { s.state = "not run"; s.evidence = "runs the real toolchains (about a minute): pass --exe PATH-OF-SUB0SQUAD"; }
            else {
                const auto problems = scenarioSelfTest({}, o.exe);
                s.state = problems.empty() ? "proven" : "failed";
                s.evidence = problems.empty() ? std::to_string(scenarioCatalogue().size()) + " of " + std::to_string(scenarioCatalogue().size()) + " fixtures valid (reference passes, starting state fails)" : std::to_string(problems.size()) + " problem(s): " + problems.front();
            }
        } else if (b.id == 5) {
            const ModelCheck m = canvasModelCheck(o.modelCases, 20261112u);
            s.state = m.failures == 0 && m.cases > 0 ? "partly proven" : "failed";
            s.evidence = std::to_string(m.cases - m.failures) + " of " + std::to_string(m.cases) + " random cases (" + std::to_string(m.steps) + " steps) equal the model" + (m.firstFailure.empty() ? std::string{} : "; first: " + m.firstFailure) +
                         "; the repair scenarios as transactions need a model";
        } else if (b.id == 6) {
            if (o.workDir.empty()) { s.state = "not run"; s.evidence = "no work directory"; }
            else {
                const ParallelProof p = parallelSectionsProof(o.workDir, 4);
                s.state = p.proven() ? "partly proven" : "failed";
                s.evidence = !p.ran ? p.error : "the harness, model-free: " + std::to_string(p.sections) + " sections at width " + std::to_string(p.width) + ", parity " + (p.identical ? "identical files" : "FILES DIFFER") +
                                                    ", " + std::to_string(p.conflicts) + " conflicts, " + std::to_string(p.lostEdits) + " lost edits, idle share " + fmt("%.2f", p.idleShare) +
                                                    fmt("; on the placeholder slot model speed-up %.2fx, coordination %.0f %% of tokens", p.model.speedUp, p.model.coordinationShare * 100.0) +
                                                    " (arithmetic, not a measurement: the real speed-up and parity need the 35B)";
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

int runBlockCommand(const std::vector<std::string>& args, const std::string& build)
{
    if (args.empty() || (args[0] != "list" && args[0] != "run")) { std::fprintf(stderr, "block: list | run [--block N] [--fixtures DIR] [--out DIR] [--cases N] [--exe PATH-OF-SUB0SQUAD]\n"); return 64; }
    if (args[0] == "list") {
        for (const auto& b : kBlocks) std::printf("%2d  %-32s %-12s %s\n", b.id, b.name, std::string(b.how) == "here" || std::string(b.how) == "gates" ? "block run" : b.how, b.command);
        return 0;
    }
    BlockOptions o;
    o.only = std::atoi(arg(args, "block", "-1").c_str());
    o.fixtures = arg(args, "fixtures", "tests/data/replies");
    o.modelCases = std::max(1, std::atoi(arg(args, "cases", "1000").c_str()));
    const fs::path outDir = arg(args, "out", "out/scenarios");
    o.workDir = outDir / "block-work";
    o.exe = arg(args, "exe");   // block 2 runs the gate commands as processes: the binary is named explicitly and verified, never assumed
    const auto states = runBlocks(o);
    json::Writer w;
    w.beginObject().kv("schema", "block-results-1").kv("build", build).key("blocks").beginArray();
    int failed = 0;
    std::printf("%-3s %-32s %-12s %s\n", "#", "block", "state", "evidence");
    for (const auto& s : states) {
        std::printf("%-3d %-32s %-12s %s\n", s.id, s.name.c_str(), s.state.c_str(), s.evidence.empty() ? s.command.c_str() : s.evidence.c_str());
        w.beginObject().kv("id", s.id).kv("name", s.name).kv("state", s.state).kv("evidence", s.evidence).kv("command", s.command).endObject();
        if (s.state == "failed") ++failed;
    }
    w.endArray().endObject();
    std::error_code ec;
    fs::remove_all(o.workDir, ec);
    (void)writeFile(outDir / "block-results.json", w.take() + "\n");
    std::printf("\nbuild %s; results in %s\n", build.c_str(), (outDir / "block-results.json").c_str());
    return failed ? 1 : 0;
}

}  // namespace sub0squad
