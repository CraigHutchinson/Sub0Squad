// Forked sections on the canvas (include/sub0squad/fork.hpp). The design in one place:
//   primer  -> the skeleton (every function declared, each body one statement that raises `not implemented: <name>`), checked by the harness precheck,
//              becomes REVISION 1 of the canvas;
//   section -> one stubbed function, owned by one session. Every session gets the same prefix (system block, card, the numbered skeleton), so a server
//              that keeps a prompt cache prefills it once, and replies with ONE transaction: its own function, whole;
//   commit  -> the coordinator (this function) applies each transaction against revision 1; the canvas rebases it through the commits of the other
//              sections (line numbers shift), refuses an edit outside the section's own function, and commits only what passes the precheck;
//   merge   -> the item's gate judges the merged file; a failure goes back to the sections it names, each in a fresh small session that sees the current
//              revision with its own function open and the others folded.
// Throughput by default: sections run side by side up to the idle slots (planParallel); one at a time needs a stated reason; memory pressure sheds forks.
#include "sub0squad/fork.hpp"

#include "agent_impl.hpp"
#include "json_util.hpp"
#include "sub0squad/canvas.hpp"
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
#include <mutex>
#include <sstream>
#include <thread>

namespace sub0squad {

namespace fs = std::filesystem;
using namespace agent;

namespace {

using Clock = std::chrono::steady_clock;
double since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

/// Whether `text` holds `word` as a whole identifier (a test failure that names a function).
bool namesWord(const std::string& text, const std::string& word)
{
    const auto isId = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
        if ((at == 0 || !isId(text[at - 1])) && (at + word.size() >= text.size() || !isId(text[at + word.size()]))) return true;
    return false;
}

std::string leaf(const std::string& name) { const auto c = name.find_last_of(":."); return c == std::string::npos ? name : name.substr(c + 1); }

std::string forkSystemBlock(const std::string& persona, const std::string& brief)
{
    std::string s = "You write ONE function of a file that several workers are writing at the same time. You are shown the file's skeleton with line numbers (`N| text`; the numbers are not "
                    "part of the file): every function is declared, and a function that is not written yet has a body that only raises `not implemented: <name>`. "
                    "Reply with exactly one block and nothing else (no explanations, no markdown fences):\n"
                    "=== EDIT: <path> SYMBOL <function name> ===\n<your whole function: its signature line exactly as in the skeleton, then its body, with the skeleton's indentation>\n=== END EDIT ===\n"
                    "Write only your own function. Use the other functions only through the signatures in the skeleton; do not write, change or repeat them.\n";
    if (!brief.empty()) s += brief + "\n";
    s += "You are the " + persona + " on a small software squad.\n";
    return s;
}

const char* const kPrimerAsk =
    "\nWrite the SKELETON first, with no implementation yet. For every file to write give the complete file: its imports, its constants, and every function or method declared with its exact "
    "signature and a one-line comment saying what it does. The body of each function is ONE statement that raises an error whose message is exactly `not implemented: <name>`, where <name> is "
    "the function's name (JavaScript: throw new Error('not implemented: name'); C++: throw std::logic_error(\"not implemented: name\"); with unused parameters left unnamed so it compiles "
    "without warnings; Python: raise NotImplementedError(\"not implemented: name\")). Constructors and one-line helpers may be written in full. "
    "The skeleton must compile or parse as it stands. Reply in the file format: === FILE: <path> === ... === END FILE ===";

}  // namespace

std::string ForkResult::toJson() const
{
    json::Writer w;
    w.beginObject().kvBool("passed", passed).kv("exitCode", exitCode).kv("width", width).kv("widthReason", widthReason).kvBool("shed", shed).kv("rounds", rounds)
        .kv("conflicts", conflicts).kv("lostEdits", lostEdits).kv("refused", refused)
        .kv("skeletonTokens", static_cast<std::int64_t>(skeletonTokens)).kv("sectionTokens", static_cast<std::int64_t>(sectionTokens)).kv("integrationTokens", static_cast<std::int64_t>(integrationTokens))
        .kv("wallSec", wallSec).kv("sectionWallSec", sectionWallSec).kv("idleShare", idleShare).kv("error", error).key("sections").beginArray();
    for (const auto& s : sections)
        w.beginObject().kv("name", s.name).kv("file", s.file).kv("symbol", s.symbol).kv("startSec", s.startSec).kv("endSec", s.endSec)
            .kv("promptTokens", static_cast<std::int64_t>(s.promptTokens)).kv("completionTokens", static_cast<std::int64_t>(s.completionTokens)).kv("calls", s.calls).kv("refused", s.refused).kvBool("committed", s.committed).endObject();
    w.endArray().endObject();
    return w.take();
}

ForkResult runCanvasFork(const ForkOptions& o)
{
    ForkResult res;
    const auto t0 = Clock::now();
    auto plan = Plan::fromJson(slurp(o.dir / "plan.json"));
    if (!plan || !plan->find(o.itemId)) { res.exitCode = 2; res.error = "cannot load the plan or the item"; return res; }
    WorkItem item = *plan->find(o.itemId);
    (void)readCardOverride(o.dir / "items" / o.itemId / "card-override.json", item.card);
    const Harness* harness = findHarness(plan->harness);
    const fs::path attemptDir = o.dir / "items" / o.itemId / ("attempt-" + std::to_string(o.attempt)), out = attemptDir / "out";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);
    const std::set<std::string> allowed(item.files.begin(), item.files.end());
    long testCount = 0;
    const std::string card = buildCard(o.dir, *plan, item, o.itemId, "baseline", testCount);
    const ChatFn chat = o.chat ? o.chat : ChatFn{[](const EngineProfile& e, const std::vector<ChatMessage>& m, int maxTokens, int timeoutSec) { return chatCompleteMessages(e, m, maxTokens, timeoutSec); }};
    const auto seeded = [&](const std::string& label) { EngineProfile e = o.engine; e.seed = turnSeed(o.engine.seed, o.itemId + "#" + std::to_string(o.attempt) + "#" + label); return e; };
    const auto finish = [&](int code) {
        res.exitCode = code;
        res.passed = code == 0;
        res.wallSec = since(t0);
        (void)writeFile(attemptDir / "fork.json", res.toJson() + "\n");
        return res;
    };

    // ---- 1. the primer: the skeleton, which must pass the precheck to become revision 1 ----
    Canvas canvas;
    const std::string primerSystem = systemBlock(item.persona, harnessBrief(plan->harness));
    std::vector<ChatMessage> primer = {{"system", primerSystem}, {"user", card + kPrimerAsk}};
    const fs::path primerTrace = attemptDir / "conversation-skeleton.md";
    traceBegin(primerTrace, "Skeleton session of " + o.itemId, o.engine, o.maxTokens);
    for (int tryNo = 0; tryNo < 2; ++tryNo) {
        traceSent(primerTrace, tryNo, primer, tryNo == 0 ? 0 : primer.size() - 2, -1);
        auto r = chat(seeded("skeleton" + std::to_string(tryNo)), primer, o.maxTokens, o.timeoutSec);
        if (!r) { res.error = "the skeleton call failed: " + r.error(); return finish(2); }
        traceReply(primerTrace, tryNo, *r);
        res.skeletonTokens += r->promptTokens + r->completionTokens;
        const Parsed parsed = parseReply(r->content, allowed);
        Canvas candidate;
        for (const auto& [path, text] : parsed.files) candidate.addFile(path, text);
        std::string bad = parsed.files.empty() ? std::string{"the reply held none of the files"} : precheckCanvas(candidate, harness, o.dir, o.itemId, o.attempt, attemptDir);
        if (bad.empty()) { canvas = std::move(candidate); break; }
        traceVerdict(primerTrace, tryNo, "the skeleton was refused: " + bad);
        std::printf("fork skeleton refused: %s\n", bad.substr(0, 200).c_str());
        if (tryNo == 1) { res.error = "no usable skeleton: " + bad; return finish(2); }
        primer.push_back({"assistant", "[your skeleton is not repeated here]"});
        primer.push_back({"user", "That skeleton cannot be used: " + clip(bad, 1200) + "\nWrite the skeleton again so that it compiles or parses as it stands." + kPrimerAsk});
    }
    persistCanvas(canvas, attemptDir);

    // ---- 2. the sections: every stub marker inside a function ----
    for (const auto& path : canvas.paths()) {
        const std::string& text = canvas.text(path);
        const auto regions = foldRegions(text, isPythonPath(path), true);
        std::istringstream in(text);
        int ln = 0;
        for (std::string line; std::getline(in, line);) {
            ++ln;
            const auto m = line.find("not implemented:");
            if (m == std::string::npos) continue;
            std::string name = line.substr(m + 16);
            name.erase(0, name.find_first_not_of(" \t"));
            name = name.substr(0, name.find_first_of("'\"` )"));
            const FoldRegion* reg = nullptr;
            for (const auto& r : regions) if (ln >= r.first && ln <= r.bodyTo) reg = &r;
            if (name.empty() || !reg || reg->name.empty()) { std::printf("note: the stub at %s:%d is not inside a named function; it stays as written\n", path.c_str(), ln); continue; }
            if (std::any_of(res.sections.begin(), res.sections.end(), [&](const ForkSection& s) { return s.file == path && s.symbol == leaf(reg->name); })) continue;
            ForkSection s;
            s.name = name; s.file = path; s.symbol = leaf(reg->name);
            res.sections.push_back(std::move(s));
        }
    }
    std::printf("fork skeleton: %zu file(s), %zu section(s)\n", canvas.paths().size(), res.sections.size());
    const auto gate = [&](std::string& gateText) {
        fs::remove_all(out, ec);
        fs::create_directories(out, ec);
        for (const auto& [path, text] : canvas.files()) (void)writeFile(out / path, text);
        const int rc = harness && harness->gate ? harness->gate({"--workflow-dir", o.dir.string(), "--item", o.itemId, "--attempt", std::to_string(o.attempt), "--no-publish", "1"}) : 1;
        gateText = slurp(attemptDir / "gate.txt");
        return rc == 0;
    };
    std::string gateText;
    if (res.sections.empty()) { std::printf("no stubs in the skeleton: nothing to delegate\n"); return finish(gate(gateText) ? 0 : 1); }

    // ---- 3. one session per section, side by side up to the idle slots ----
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

    std::mutex mu;                                   // guards the canvas and the result counters
    const std::string system = forkSystemBlock(item.persona, harnessBrief(plan->harness));
    std::string sectionList;
    for (const auto& s : res.sections) sectionList += "- " + s.name + " (" + s.file + ")\n";
    // The shared prefix: identical for every section, so it is prefilled once by a server that reuses its prompt cache.
    std::string skeletonView = card + "\nThe skeleton:\n";
    for (const auto& path : canvas.paths()) skeletonView += "=== CANVAS: " + path + " (revision 1) ===\n" + foldedView(canvas.text(path), isPythonPath(path), {}, 1u << 30) + "=== END CANVAS ===\n";
    skeletonView += "The functions being written, each by one worker:\n" + sectionList;
    std::map<std::string, std::string> committedText;   // section name -> the function text it committed

    // One section's session: ask, read the reply as a transaction on its own function, commit through the coordinator. `base` is the revision it was shown.
    const auto runSection = [&](ForkSection& s, std::vector<ChatMessage> convo, int base, const std::string& tag, long& phaseTokens) {
        std::string flat = s.name;
        for (char& c : flat) if (c == '.' || c == '/' || c == ':') c = '_';
        const fs::path trace = attemptDir / ("conversation-" + flat + ".md");
        if (tag == "section") traceBegin(trace, "Section " + s.name + " of " + o.itemId, o.engine, o.maxTokens);
        for (int tryNo = 0; tryNo < 3; ++tryNo) {
            traceSent(trace, tryNo, convo, tryNo == 0 ? 0 : convo.size() - 2, -1);
            auto r = chat(seeded(tag + ":" + s.name + ":" + std::to_string(tryNo)), convo, o.maxTokens, o.timeoutSec);
            ++s.calls;
            if (!r) { traceVerdict(trace, tryNo, "the engine call failed: " + r.error()); return; }
            traceReply(trace, tryNo, *r);
            s.promptTokens += r->promptTokens; s.completionTokens += r->completionTokens;
            { std::lock_guard<std::mutex> l(mu); phaseTokens += r->promptTokens + r->completionTokens; }
            // The reply as a transaction: one replaceSymbol on the section's own function. A bare function (no block) is accepted when it starts with the name.
            CanvasReply cr = parseCanvasReply(r->content);
            std::string refusal = cr.error;
            Transaction tx{s.file, base, s.name, {}};
            if (refusal.empty() && !cr.hasEdits()) {
                std::string body = r->content;
                if (body.rfind("```", 0) == 0) { const auto nl = body.find('\n'); body = nl == std::string::npos ? "" : body.substr(nl + 1); }
                if (const auto fence = body.rfind("```"); fence != std::string::npos && body.find_first_not_of(" \t\r\n", fence + 3) == std::string::npos) body.erase(fence);
                const auto firstLine = body.substr(0, body.find('\n'));
                if (firstLine.find(s.symbol) != std::string::npos && firstLine.find('(') != std::string::npos) tx.ops.push_back({CanvasOp::Kind::replaceSymbol, 0, 0, s.symbol, body});
                else refusal = "the reply held no === EDIT: " + s.file + " SYMBOL " + s.symbol + " === block";
            } else if (refusal.empty()) {
                if (!cr.wholeFiles.empty() || !cr.searchEdits.empty()) refusal = "you own only `" + s.symbol + "`: reply with one === EDIT: " + s.file + " SYMBOL " + s.symbol + " === block, not a file or a search edit";
                for (const auto& t : cr.transactions) for (const auto& op : t.ops) {
                    if (!refusal.empty()) break;
                    if (t.path != s.file || op.kind != CanvasOp::Kind::replaceSymbol || leaf(op.symbol) != s.symbol) {
                        std::string holder;
                        for (const auto& other : res.sections) if (other.file == t.path && op.kind == CanvasOp::Kind::replaceSymbol && other.symbol == leaf(op.symbol)) holder = other.name;
                        refusal = "you own only `" + s.symbol + "` in " + s.file + (holder.empty() ? std::string{": that edit is outside it"} : ": `" + leaf(op.symbol) + "` is held by the worker writing " + holder) + ". Reply with your own function only";
                    } else tx.ops.push_back(op);
                }
                if (refusal.empty() && tx.ops.size() != 1) refusal = "reply with exactly one block for `" + s.symbol + "`";
            }
            if (refusal.empty()) {
                std::lock_guard<std::mutex> l(mu);
                Canvas scratch = canvas;
                const TxResult applied = scratch.apply(tx);
                if (!applied.ok) { refusal = applied.error; if (applied.error.find("were changed by") != std::string::npos) ++res.conflicts; }
                else if (const std::string bad = precheckCanvas(scratch, harness, o.dir, o.itemId, o.attempt, attemptDir); !bad.empty()) refusal = "your function does not pass the check, so it was not kept:\n" + clip(bad, 1000);
                else {
                    canvas = std::move(scratch);
                    persistCanvas(canvas, attemptDir);
                    committedText[s.name] = tx.ops[0].text;
                    s.committed = true;
                    traceVerdict(trace, tryNo, "committed as revision " + std::to_string(applied.revision) + " of " + s.file);
                    return;
                }
            }
            ++s.refused;
            { std::lock_guard<std::mutex> l(mu); ++res.refused; }
            traceVerdict(trace, tryNo, "refused: " + refusal);
            std::printf("section %s: refused: %s\n", s.name.c_str(), refusal.substr(0, 160).c_str());
            convo.push_back({"assistant", "[your previous reply is not repeated]"});
            convo.push_back({"user", "Your reply was REFUSED and nothing changed: " + refusal + ".\nReply again with exactly:\n=== EDIT: " + s.file + " SYMBOL " + s.symbol + " ===\n<your whole function>\n=== END EDIT ==="});
        }
    };

    const auto tSections = Clock::now();
    {
        std::atomic<std::size_t> next{0};
        std::atomic<int> limit{res.width};
        std::vector<std::thread> pool;
        for (int wkr = 0; wkr < res.width; ++wkr) {
            pool.emplace_back([&, wkr] {
                for (;;) {
                    if (wkr >= limit.load()) return;   // shed: this worker stops taking sections; the first one carries on alone
                    if (wkr > 0 && o.memFloorMiB > 0 && availableMiB() < o.memFloorMiB) {
                        std::lock_guard<std::mutex> l(mu);
                        if (limit.load() > 1) { limit = 1; res.shed = true; res.widthReason += "; shed to one at a time under memory pressure"; events::note("forks shed: memory pressure"); }
                        return;
                    }
                    const std::size_t i = next.fetch_add(1);
                    if (i >= res.sections.size()) return;
                    ForkSection& s = res.sections[i];
                    s.startSec = since(tSections);
                    runSection(s, {{"system", system}, {"user", skeletonView + "\nYou own `" + s.symbol + "` in " + s.file + ". Reply with:\n=== EDIT: " + s.file + " SYMBOL " + s.symbol + " ===\n<your whole function>\n=== END EDIT ==="}}, 1, "section", res.sectionTokens);
                    s.endSec = since(tSections);
                }
            });
        }
        for (auto& t : pool) t.join();
    }
    res.sectionWallSec = since(tSections);
    {
        std::vector<JobSpan> spans;
        long tokens = 0;
        for (const auto& s : res.sections) { spans.push_back({s.name, s.startSec, s.endSec}); tokens += s.completionTokens; }
        res.idleShare = accountSpans(spans, res.width, tokens).idleShare;
    }
    // No edit may be lost in the merge: every committed function is in the merged text, and no committed section still holds its stub.
    for (const auto& s : res.sections) {
        if (!s.committed) continue;
        const std::string& merged = canvas.text(s.file);
        std::string want = committedText[s.name];
        while (!want.empty() && (want.back() == '\n' || want.back() == '\r')) want.pop_back();
        if (merged.find(want) == std::string::npos) ++res.lostEdits;
    }
    for (const auto& s : res.sections) std::printf("section %s: %ld prompt, %ld completion tokens, %d call(s), %s\n", s.name.c_str(), s.promptTokens, s.completionTokens, s.calls, s.committed ? "committed" : "NOT committed");

    // ---- 4. the merged file under the item's gate; a failure goes back to the sections it names ----
    bool ok = gate(gateText);
    while (!ok && res.rounds < o.rounds) {
        ++res.rounds;
        std::vector<ForkSection*> implicated;
        // The first round goes to the sections the failure names (and any that never committed); when it names none, or naming them did not help, to all.
        if (res.rounds == 1) for (auto& s : res.sections) if (!s.committed || namesWord(gateText, s.symbol)) implicated.push_back(&s);
        if (implicated.empty()) for (auto& s : res.sections) implicated.push_back(&s);
        std::printf("round %d: the merged file failed; %zu section(s) go back\n", res.rounds, implicated.size());
        // Each goes back in a fresh small session: the card, the failure, and the current revision with its own function open and the others folded.
        std::map<std::string, int> shown;
        std::map<std::string, std::string> shownText;
        for (const auto& path : canvas.paths()) { shown[path] = canvas.revision(path); shownText[path] = canvas.text(path); }
        std::atomic<std::size_t> next{0};
        std::vector<std::thread> pool;
        const int w = std::min<int>(res.width, static_cast<int>(implicated.size()));
        for (int wkr = 0; wkr < w; ++wkr) {
            pool.emplace_back([&] {
                for (;;) {
                    const std::size_t i = next.fetch_add(1);
                    if (i >= implicated.size()) return;
                    ForkSection& s = *implicated[i];
                    const std::string view = "=== CANVAS: " + s.file + " (revision " + std::to_string(shown[s.file]) + ") ===\n" + sectionView(shownText[s.file], isPythonPath(s.file), {s.symbol}) + "=== END CANVAS ===\n";
                    runSection(s, {{"system", system}, {"user", card + "\nThe functions were written separately and merged. The merged file was checked by running the tests and failed:\n" + clip(gateText, 1500) + "\n" + view +
                                                                    "You own `" + s.symbol + "` in " + s.file + " (shown open; the others are folded). It may be at fault. Reply with the corrected function:\n=== EDIT: " + s.file + " SYMBOL " + s.symbol + " ===\n<your whole function>\n=== END EDIT ==="}},
                               shown[s.file], "round" + std::to_string(res.rounds), res.integrationTokens);
                }
            });
        }
        for (auto& t : pool) t.join();
        ok = gate(gateText);
    }
    std::printf("RESULT: %s after %d round(s); width %d, %d refused, %d conflict(s), %d lost edit(s)\n", ok ? "PASS" : "FAIL", res.rounds, res.width, res.refused, res.conflicts, res.lostEdits);
    return finish(ok ? 0 : 1);
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
    auto profile = EngineProfile::parse(arg(args, "engine"));
    if (!profile) { std::fprintf(stderr, "llm-fork: %s\n", profile.error().c_str()); return 64; }
    o.engine = *profile;
    const ForkResult r = runCanvasFork(o);
    if (!r.error.empty()) std::fprintf(stderr, "llm-fork: %s\n", r.error.c_str());
    // As a workflow's agent job the attempt succeeds when it wrote its files: the gate job is the judge, as for every executor.
    if (arg(args, "agent-exit", "0") == "1") return r.exitCode == 2 ? 2 : 0;
    return r.exitCode;
}

}  // namespace sub0squad
