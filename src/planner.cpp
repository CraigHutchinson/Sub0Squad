#include "sub0squad/planner.hpp"

#include "agent_impl.hpp"
#include "json_util.hpp"
#include "sub0squad/engine.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/plan_checks.hpp"
#include "sub0squad/plan_lint.hpp"
#include "sub0squad/stance.hpp"
#include "sub0squad/harness_common.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;

namespace {

using harnessutil::slurp;
using harnessutil::writeFile;
std::string arg(const std::vector<std::string>& a, const std::string& key, const std::string& d = {})
{
    for (std::size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == "--" + key) return a[i + 1];
    return d;
}

}  // namespace

std::vector<PlanFile> extractFileBlocks(const std::string& reply, std::string& rest)
{
    // `=== FILE: path ===` ... `=== END FILE ===`: source code written as plain text, not as an escaped JSON string (a small model corrupts the escaping).
    std::vector<PlanFile> files;
    rest.clear();
    std::size_t at = 0;
    for (;;) {
        const auto open = reply.find("=== FILE: ", at);
        if (open == std::string::npos) { rest += reply.substr(at); break; }
        rest += reply.substr(at, open - at);
        const auto nameEnd = reply.find(" ===", open + 10);
        const auto bodyStart = nameEnd == std::string::npos ? std::string::npos : reply.find('\n', nameEnd);
        if (bodyStart == std::string::npos) { rest += reply.substr(open); break; }
        const auto close = reply.find("=== END FILE ===", bodyStart);
        const std::string body = reply.substr(bodyStart + 1, (close == std::string::npos ? reply.size() : close) - bodyStart - 1);
        files.push_back({reply.substr(open + 10, nameEnd - open - 10), body});
        if (close == std::string::npos) break;
        at = close + 16;
    }
    return files;
}

std::string extractJsonObject(const std::string& reply)
{
    const auto first = reply.find('{');
    const auto last = reply.rfind('}');
    if (first == std::string::npos || last == std::string::npos || last < first) return {};
    return reply.substr(first, last - first + 1);
}

std::string repairJson(const std::string& reply)
{
    const auto first = reply.find('{');
    if (first == std::string::npos) return {};
    std::string out;
    std::vector<char> stack;
    bool inString = false;
    for (std::size_t i = first; i < reply.size(); ++i) {
        const char c = reply[i];
        if (inString) {
            if (c == '\\' && i + 1 < reply.size()) { out += c; out += reply[++i]; continue; }
            if (c == '\n') { out += "\\n"; continue; }
            if (c == '"') {   // a quote closes the string only before , : ] } or the end; any other quote is part of the text
                std::size_t j = i + 1;
                while (j < reply.size() && (reply[j] == ' ' || reply[j] == '\n' || reply[j] == '\t' || reply[j] == '\r')) ++j;
                bool closes = j >= reply.size() || reply[j] == ':' || reply[j] == ']' || reply[j] == '}';
                if (!closes && reply[j] == ',') {   // a comma closes the string only when a JSON value or key follows it: `run("ARG1", ...)` is text
                    std::size_t k = j + 1;
                    while (k < reply.size() && (reply[k] == ' ' || reply[k] == '\n' || reply[k] == '\t' || reply[k] == '\r')) ++k;
                    closes = k >= reply.size() || std::string("\"{[-0123456789tfn]}").find(reply[k]) != std::string::npos;
                    if (closes && !stack.empty() && stack.back() == '{' && k < reply.size() && reply[k] == '"') {   // in an object a value is followed by a KEY: a string and then a colon
                        std::size_t e = k + 1;
                        while (e < reply.size() && reply[e] != '"' && reply[e] != '\n') e += reply[e] == '\\' ? 2 : 1;
                        std::size_t after = e + 1;
                        while (after < reply.size() && (reply[after] == ' ' || reply[after] == '\n' || reply[after] == '\t')) ++after;
                        closes = after < reply.size() && reply[after] == ':';
                    }
                }
                if (closes) { inString = false; out += c; } else out += "\\\"";
                continue;
            }
            out += c;
            continue;
        }
        if (c == '"') { inString = true; out += c; continue; }
        if (c == '{' || c == '[') { stack.push_back(c); out += c; continue; }
        if (c == '}' || c == ']') {
            const char want = c == '}' ? '{' : '[';
            if (std::find(stack.begin(), stack.end(), want) == stack.end()) continue;   // a closer nothing opened
            while (stack.back() != want) { out += stack.back() == '{' ? '}' : ']'; stack.pop_back(); }   // a closer a level too early: close what is open
            stack.pop_back();
            out += c;
            if (stack.empty()) return out;   // the object is complete; whatever follows is not part of it
            continue;
        }
        out += c;
    }
    if (inString) out += '"';
    while (!stack.empty()) { out += stack.back() == '{' ? '}' : ']'; stack.pop_back(); }
    return out;
}

// A JSON text, tolerant of slips, laid out one element per line and indented: a line number then names a place, and a repair is an edit of a few lines.
static std::string layoutJson(const std::string& s)
{
    std::string out;
    int depth = 0;
    bool inString = false;
    const auto newline = [&] { out += '\n'; out.append(static_cast<std::size_t>(std::max(depth, 0)) * 2, ' '); };
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (inString) {
            out += c;
            if (c == '\\' && i + 1 < s.size()) out += s[++i];
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; out += c; continue; }
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') continue;
        if (c == '{' || c == '[') {
            std::size_t j = i + 1;
            while (j < s.size() && (s[j] == ' ' || s[j] == '\n' || s[j] == '\t' || s[j] == '\r')) ++j;
            if (j < s.size() && (s[j] == '}' || s[j] == ']')) { out += c; out += s[j]; i = j; continue; }   // an empty object or list
            out += c; ++depth; newline();
        } else if (c == '}' || c == ']') { --depth; newline(); out += c; }
        else if (c == ',') { out += c; newline(); }
        else if (c == ':') out += ": ";
        else out += c;
    }
    return out;
}

std::string prosePlannerPrompt(int slots)
{
    std::string harnesses;
    for (const auto& name : harnessNames()) harnesses += "- " + name + "\n";
    std::string s;
    s += "You are the PLANNING LEAD of a small software squad. Think about WHAT to build and how to split it, and write the plan in plain prose. Do NOT write JSON, code or file contents: a formatter turns your prose into the exact plan form afterwards, and a different worker writes each file.\n\n";
    s += "Project kinds the squad can build (name the one that fits, or the one the request names):\n" + harnesses + "\n";
    s += "Write these parts, with these headings, briefly (under about 450 words in all):\n";
    s += "PROJECT: a short name and the kind.\n";
    s += "SPLIT: why this split. The work is test-first: for each independent deliverable (a library; a command-line program that uses it) a TESTER item writes tests from the contract, then a DEVELOPER item implements it and is graded by those tests. Few items, each with one purpose.\n";
    s += "For each item: ITEM <id> (tester or developer): its goal in one sentence; the file it writes (tests/NAME... or src/NAME...); the items it depends on.\n";
    s += "INTERFACE: the project's header in words (the names, parameter and result types, and how an error is reported) and which source files need a stub.\n";
    s += "CONTRACT, per deliverable: exact behaviour in prose, nothing left open (names, types, errors, argument order of anything a test calls).\n";
    s += "EXAMPLES, per deliverable: at least 4 lines of the form `input = exact result`, worked out CAREFULLY (both the tests and the code are checked against them).\n";
    s += "ACCEPTANCE, per item: the observable checks that say it is done.\n";
    if (slots > 0) s += "\nTHE ENGINE runs " + std::to_string(slots) + " requests at once. Items take unknown, different times, so plan for more independent items than slots when the work really is independent (never pad with invented parts), so a slot that frees finds work waiting.\n";
    return s;
}

std::string plannerSystemPrompt(int slots, const std::string& stance)
{
    std::string harnesses;
    for (const auto& name : harnessNames()) {
        const Harness* h = findHarness(name);
        if (!h) continue;
        harnesses += "- \"" + name + "\" (gate kind \"" + h->gateKind + "\", test files end in \"" + h->testSuffix + "\"): " + (h->ownScaffoldBrief ? h->ownScaffoldBrief() : h->brief ? h->brief() : std::string{}) + "\n";
    }
    std::string s;
    if (stance == "specialist")
        s += "You are the PLANNER of this project: a dedicated, specialised assignment. You turn a request into a plan the squad can build and verify. Every item you write "
             "becomes a dedicated, specialised assignment for one worker, who owns it, is the expert for it, and may contest your contract, examples or tests with evidence. "
             "The workers are small local models and each sees ONLY its own work card, so every card must be exact, self-sufficient and short: what the piece is for (one "
             "line of why in the context), its exact interface, and the worked facts it is checked against. ";
    else
        s += "You are the PLANNER of a small software squad. You turn a request into a plan the squad can build and verify. The workers are small local models: each sees ONLY its own work card, so every card must be exact and short. ";
    s += "Reply with ONE JSON object in `plan-2` form, then the scaffold file blocks described below, and nothing else (no markdown fences, no commentary).\n\n";
    s += "PROJECT HARNESSES (set \"harness\" to the one that fits the request; if the request names a platform, use it):\n" + harnesses + "\n";
    s += "THE FORM (angle brackets are placeholders):\n";
    s += "{\"schema\":\"plan-2\",\"task\":\"<the request>\",\"harness\":\"<harness name>\",\"project\":\"<short-name>\",\"reasons\":[\"<why this split>\"],\n";
    s += " \"items\":[{\"id\":\"test-<area>\",\"title\":\"..\",\"persona\":\"tester\",\"goal\":\"one sentence\",\"files\":[\"tests/<area><test suffix>\"],\"deps\":[],\"complexity\":2,\n";
    s += "   \"card\":{\"context\":\"1-2 sentences\",\"contract\":\"the exact interface and behaviour\",\"acceptance\":[\"observable checks\"],\"include\":[],\"notes\":[],\"examples\":[\"<input> = <exact result>\"]},\n";
    s += "   \"gate\":{\"kind\":\"<gate kind>\",\"tests\":[\"tests/<area><test suffix>\"],\"minTests\":5,\"mustFailOnStub\":true}},\n";
    s += "  {\"id\":\"dev-<area>\",\"title\":\"..\",\"persona\":\"developer\",\"goal\":\"one sentence\",\"files\":[\"src/<file>.cpp\"],\"deps\":[\"test-<area>\"],\"complexity\":3,\n";
    s += "   \"card\":{\"context\":\"..\",\"contract\":\"..\",\"acceptance\":[\"every test in the tester's file passes\"],\"include\":[\"tests/<area><test suffix>\"],\"notes\":[],\"examples\":[\"<the same facts>\"]},\n";
    s += "   \"gate\":{\"kind\":\"<gate kind>\",\"tests\":[\"tests/<area><test suffix>\"],\"minTests\":5,\"mustFailOnStub\":false}}]}\n";
    s += "AFTER the JSON object, write each scaffold file as a plain file block (NOT inside the JSON):\n=== FILE: include/<name>/<name>.hpp ===\n<the project's interface: declarations only>\n=== END FILE ===\n=== FILE: src/<file>.cpp ===\n<a STUB: defines every declared function, returns a neutral value, never throws>\n=== END FILE ===\n\n";
    if (slots > 0) {
        s += "THE ENGINE: it runs " + std::to_string(slots) + " requests at once, one per slot, and the goal is to keep every slot busy. Items take different, unknown amounts of time, so some finish early: a slot that frees must find a ready item waiting. ";
        s += "Plan for MORE independent work than slots (about " + std::to_string(slots) + " items that can start at once when the work really is independent), never fewer; but never pad with invented parts, and keep every reply small (a plan of more than 6 items is cut off and refused). Each item must fit one slot's context window and about an hour.\n\n";
    }
    s += "RULES\n";
    s += "- Test-first. A tester item (persona \"tester\", no deps) writes the tests from the contract; a developer item depends on it, lists its test file in card.include and gate.tests, and is graded by tests it did not write. Testers write only under tests/; developers never write tests; no two items edit the same file.\n";
    s += "- The interface comes first. For a native (C++) project the PLAN carries the project's own interface as scaffold FILE blocks: a header that declares what the developers implement, and a STUB for every source file a developer writes (every declared function defined, returning a neutral value such as 0, an empty string or NaN, and never throwing, so the tester's tests fail against it instead of crashing). The stubs must compile with -Wall -Wextra -Werror (a parameter a stub does not use needs a cast to void). The harness supplies the build file and the test helpers (CMakeLists.txt, tests/helpers/); do not write those.\n";
    s += "- Worked examples. card.examples (at least 3 on every tester and developer item; the same facts on both) are lines of the form `input = exact result`, worked out CAREFULLY by you: they are facts both the tests and the code are checked against, so a wrong example breaks the project. Prefer examples that exercise precedence, edge cases and errors, not only the easy case.\n";
    s += "- Be exact in the contract: names, parameter types, result types, how an error is reported, the order of arguments of anything a test calls. Anything the contract leaves open will be guessed differently by the tester and the developer.\n";
    s += "- Few items, each with one purpose. Make ONE tester item and ONE developer item per independent deliverable (a library; a command-line program that uses it): a calculator is an engine item pair and a command-line app item pair, four items (more only where the work is really independent). NEVER make items of a file's internal parts (a lexer, a parser, a \"misc\" item): the developer divides its own file into sections, and two items may not edit the same file. At most 8 items.\n";
    s += "- Keep the whole reply SMALL: under about 2500 tokens. One-line examples, short contracts, no filler. A reply that is cut off is refused. Test files are named tests/NAME followed by the test suffix of the harness.\n";
    s += "- Item ids are lowercase letters, digits, - and _, and say the role and area: test-NAME for a tester, dev-NAME for a developer. At most 32 items; each work card at most about 1500 tokens.\n";
    s += "- Every gate is declarative (a gate kind and test files); a plan never contains commands. A tester's gate has mustFailOnStub true and minTests of 3 up to the number of worked examples (a small model writes about one test per example, and a gate that asks for more can never pass); a developer's has mustFailOnStub false.\n";
    return s;
}

int runLlmPlanner(const std::vector<std::string>& args)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const fs::path dir = arg(args, "workflow-dir");
    const std::string request = arg(args, "request");
    const int maxTurns = std::atoi(arg(args, "max-turns", "4").c_str());
    const int timeoutSec = std::atoi(arg(args, "timeout", "3300").c_str());   // one engine call; a small model on a CPU writes a plan at a few tokens a second
    const int maxTokensArg = std::atoi(arg(args, "max-tokens", "0").c_str());
    const int wallSec = std::atoi(arg(args, "wall", "7000").c_str());
    // The parallelism bar (owner: iterate the plan until the nodes can keep the slots busy): a plan that passes every check but whose expected speedup on the
    // engine's slots (plan-lint's Monte Carlo over unknown item sizes) is below this goes back to the planner like any other problem. 0 = no bar.
    const double minSpeedup = std::atof(arg(args, "min-speedup", "0").c_str());
    std::string bestPlanJson, bestLintText;
    double bestSpeedup = -1, lastSpeedup = 0;
    bool barMet = minSpeedup <= 0;           // the whole job: no turn is started that cannot finish inside it
    const auto started = std::chrono::steady_clock::now();
    if (dir.empty() || request.empty()) { std::fprintf(stderr, "llm-planner: --workflow-dir and --request are required\n"); return 64; }
    const std::string stance = arg(args, "stance", "baseline");   // stance.hpp: an arm of docs/EXPERIMENTS.md E1 for the planner
    if (!isKnownStance(stance)) { std::fprintf(stderr, "llm-planner: --stance must be baseline or specialist\n"); return 64; }
    // A plan the owner supplied (`do --plan-file`): not re-planned. It still goes through the mends and the plan's checks, so a plan from any source is held to the same bar.
    if (fs::exists(dir / "plan.supplied")) {
        auto supplied = Plan::fromJson(slurp(dir / "plan.json"));
        if (!supplied) { std::fprintf(stderr, "llm-planner: the supplied plan is refused: %s\n", supplied.error().c_str()); return 1; }
        const std::vector<std::string> suppliedMends = mendPlan(*supplied);
        for (const auto& m : suppliedMends) std::fprintf(stderr, "llm-planner: supplied plan mended: %s\n", m.c_str());
        for (const auto& problem : checkPlan(*supplied)) std::fprintf(stderr, "llm-planner: supplied plan: %s\n", problem.c_str());
        if (auto ok = supplied->validate(); !ok) { std::fprintf(stderr, "llm-planner: the supplied plan is refused: %s\n", ok.error().c_str()); return 1; }
        if (const Harness* h = findHarness(supplied->harness); h && h->checkPlanScaffold)
            if (const std::string bad = h->checkPlanScaffold(supplied->toJson()); !bad.empty()) { std::fprintf(stderr, "llm-planner: the supplied plan is refused: %s\n", bad.c_str()); return 1; }
        if (!writeFile(dir / "plan.json", supplied->toJson())) return 1;
        {   // the record the portal reads: no turns, and what was mended on the way in
            json::Writer w;
            w.beginObject().kv("schema", "planner-1").kv("request", request).kvBool("valid", true).kv("turns", static_cast<std::int64_t>(0)).kvBool("supplied", true).key("mends").beginArray();
            for (const auto& m : suppliedMends) w.beginObject().kv("turn", static_cast<std::int64_t>(0)).kv("text", m).endObject();
            w.endArray().endObject();
            writeFile(dir / "planner.json", w.take() + "\n");
        }
        std::printf("using the supplied plan: %zu item(s)\n", supplied->items.size());
        return 0;
    }
    auto engine = EngineProfile::parse(arg(args, "engine"));
    if (!engine) { std::fprintf(stderr, "llm-planner: %s\n", engine.error().c_str()); return 64; }
    // A reply may use up to 45 % of a slot's window (3.7k of 8192: what the small window allowed; 7k of 16384), unless `--max-tokens` says otherwise.
    const int maxTokens = maxTokensArg > 0 ? maxTokensArg : std::clamp(engine->contextTokens * 45 / 100, 2000, 7000);

    // What the owner settled when the planning stage asked (clarify): the platform and the executor are data in the sandbox.
    std::string answers = slurp(dir / "brief.json");
    std::string user = "Request: " + request + "\n";
    if (!answers.empty()) user += "\nThe owner's answers to the planning questions (settled; follow them):\n" + answers + "\n";
    // Two disciplines, two steps (docs/CLEAN_SLATE.md section 8): the planning lead writes the plan in PROSE, a formatter turns the prose into the plan form. The
    // prose is kept on disk (planner/prose.md): it is the layer a formatting or a building problem is raised to.
    std::string prose;
    if (std::find(args.begin(), args.end(), "--prose") != args.end()) {
        events::progress(0, 1, "writing the plan in prose");
        const std::vector<ChatMessage> proseMessages = {{"system", prosePlannerPrompt(std::atoi(arg(args, "slots", std::to_string(engine->maxConcurrent)).c_str()))}, {"user", user + "\nWrite the plan in prose."}};
        auto lead = chatCompleteMessages(*engine, proseMessages, 1800, std::min(timeoutSec, wallSec), {});
        if (!lead) { std::fprintf(stderr, "llm-planner: the prose step failed: %s\n", lead.error().c_str()); return 2; }
        prose = lead->content;
        writeFile(dir / "planner" / "prose.md", prose + "\n");
        if (prose.size() < 200) { std::fprintf(stderr, "llm-planner: the prose plan is too short (%zu characters)\n", prose.size()); return 1; }
        user += "\nThe planning lead has decided the split, the contracts and the examples, in prose, below. Turn them into the plan form: keep every decision, example and name, and invent nothing.\n\n--- the lead's plan ---\n" + prose + "\n--- end ---\n";
    }
    user += "\nWrite the plan.";

    const int slots = std::atoi(arg(args, "slots", std::to_string(engine->maxConcurrent)).c_str());
    std::vector<ChatMessage> messages = {{"system", plannerSystemPrompt(slots, stance)}, {"user", user}};
    long promptTokens = 0, completionTokens = 0;
    std::string lintText;
    std::vector<std::vector<std::string>> problemsByTurn;
    std::vector<std::string> kindByTurn;                       ///< how each turn's reply was taken: "plan" (a first or whole plan), "edit" (edits to the document), "unreadable"
    std::vector<std::pair<int, std::string>> mendsMade;        ///< the mechanical mends, by turn: shown by the portal, and the evidence of what the model did not have to fix
    std::string conversation = "# Planner conversation\n\nEngine `" + engine->name + "`, run seed " + std::to_string(engine->seed) + ".\n\n## System\n\n```\n" + messages[0].content + "\n```\n\n## User\n\n" + user + "\n";
    const auto record = [&](bool valid, int turns) {
        json::Writer w;
        w.beginObject().kv("schema", "planner-1").kv("request", request).kvBool("valid", valid).kv("turns", static_cast<std::int64_t>(turns))
            .kv("promptTokens", static_cast<std::int64_t>(promptTokens)).kv("completionTokens", static_cast<std::int64_t>(completionTokens)).key("problems").beginArray();
        for (const auto& turn : problemsByTurn) { w.beginArray(); for (const auto& p : turn) w.str(p); w.endArray(); }
        w.endArray().key("kinds").beginArray();
        for (const auto& k : kindByTurn) w.str(k);
        w.endArray().key("mends").beginArray();
        for (const auto& m : mendsMade) w.beginObject().kv("turn", static_cast<std::int64_t>(m.first)).kv("text", m.second).endObject();
        w.endArray().kvBool("prose", !prose.empty()).kv("slots", static_cast<std::int64_t>(slots)).kv("minSpeedup", minSpeedup).kv("speedup", lastSpeedup).kvBool("barMet", barMet).kv("lint", lintText).endObject();
        writeFile(dir / "planner.json", w.take() + "\n");
        writeFile(dir / "planner" / "conversation.md", conversation);
    };

    // The plan as a DOCUMENT (docs/BACKLOG.md "Repairs as edits"): once a first reply exists, a repair is a few edits to it, not a second plan. The document is
    // `plan.json` laid out one field per line (so a line number names a place) plus one file per scaffold file. The model is shown the numbered document and the
    // problems, and answers with EDIT transactions; nothing it got right is rewritten, and no history is carried (the conversation is rebuilt from the document).
    std::map<std::string, std::string> doc;
    const auto numbered = [](const std::string& text) {
        std::string out;
        std::istringstream in(text);
        std::string line;
        for (int n = 1; std::getline(in, line); ++n) out += std::to_string(n) + "| " + line + "\n";
        return out;
    };
    // The document as the model last wrote it, from a reply's pieces: the plan JSON (mended when strict parsing fails) with the scaffold taken out into files.
    const auto toDocument = [&](const std::string& rawJson, const std::string& fullReplyBody, std::vector<PlanFile> files) {
        doc.clear();
        std::string jsonText = rawJson;
        if (auto parsed = Plan::fromJson(jsonText)) {
            for (const auto& f : parsed->scaffold) if (std::none_of(files.begin(), files.end(), [&](const PlanFile& g) { return g.path == f.path; })) files.push_back(f);
            parsed->scaffold.clear();
            jsonText = parsed->toJson();
        } else if (const std::string fixed = repairJson(fullReplyBody); !fixed.empty()) jsonText = fixed;
        doc["plan.json"] = layoutJson(jsonText) + "\n";
        for (const auto& f : files) doc[f.path] = f.content;
    };
    // What the model is shown on a repair turn: every file that matters to the problems whole and numbered, the rest by name.
    const auto documentView = [&](const std::vector<std::string>& problems) {
        std::string joined;
        for (const auto& p : problems) joined += p + "\n";
        std::string view;
        for (const auto& [path, text] : doc) {
            const bool mentioned = path == "plan.json" || joined.find(path) != std::string::npos;
            if (mentioned || text.size() < 700) view += "=== " + path + " ===\n" + numbered(text) + "=== END " + path + " ===\n";
            else view += "=== " + path + " === (" + std::to_string(std::count(text.begin(), text.end(), '\n') + 1) + " lines, not shown: no problem names it)\n";
        }
        return view;
    };
    bool haveDocument = false;
    std::vector<std::string> lastProblems;

    for (int turn = 1; turn <= maxTurns; ++turn) {
        const int left = wallSec - static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count());
        if (left < 300) { std::fprintf(stderr, "llm-planner: %d s of the wall budget left, too little for another turn\n", left); break; }
        events::progress(turn - 1, maxTurns, turn == 1 ? "planning" : "repairing the plan");
        if (haveDocument) {   // a repair turn: the document and the problems only, answered with edits (small replies)
            std::string fix = "Request: " + request + "\n\nYou wrote the plan below (as files; `N| ` is a line number, not part of the file). It was refused for these reasons:\n";
            for (const auto& p : lastProblems) fix += "- " + p + "\n";
            fix += "\nFix exactly these problems with the SMALLEST edits; do not rewrite what is right. A missing bracket is an insert at that line. A new file (for example a header) is a === FILE: path === block.\n\n" + documentView(lastProblems) + "\n" + agent::kEditSyntax + "\n";
            messages = {{"system", plannerSystemPrompt(slots, stance)}, {"user", fix}};
            conversation += "\n## Turn " + std::to_string(turn) + " (the harness: the document and the problems)\n\n" + fix.substr(0, fix.find("=== plan.json ===")) + "...\n";
        }
        // The reply streams into planner/live.txt so a watcher can read the plan as it is written; the file goes when the turn ends.
        const fs::path live = dir / "planner" / "live.txt";
        std::error_code ec;
        fs::create_directories(live.parent_path(), ec);
        std::ofstream liveOut(live, std::ios::binary | std::ios::trunc);
        auto reply = chatCompleteMessages(*engine, messages, haveDocument ? std::min(maxTokens, 3500) : maxTokens, std::min(timeoutSec, left), [&](std::string_view content, std::string_view) { liveOut << content << std::flush; });
        liveOut.close();
        fs::remove(live, ec);
        if (!reply) { std::fprintf(stderr, "llm-planner: engine call failed: %s\n", reply.error().c_str()); record(false, turn - 1); return 2; }
        promptTokens += reply->promptTokens; completionTokens += reply->completionTokens;
        writeFile(dir / "planner" / ("turn-" + std::to_string(turn) + ".txt"), reply->content);
        conversation += "\n## Turn " + std::to_string(turn) + " (the planner)\n\n```\n" + reply->content + "\n```\n";

        std::vector<std::string> problems;
        std::string withoutBlocks;
        std::vector<PlanFile> blocks = extractFileBlocks(reply->content, withoutBlocks);
        std::string json = extractJsonObject(withoutBlocks);
        bool edited = false;
        if (haveDocument && withoutBlocks.find("\"schema\"") == std::string::npos) {   // not a second plan: the edits apply to the document
            std::set<std::string> allowed;
            for (const auto& [path, text] : doc) allowed.insert(path);
            for (const auto& b : blocks) allowed.insert(b.path);
            if (auto next = applyReplyText(reply->content, doc, allowed, "planner")) {
                doc = std::move(*next);
                edited = true;
            } else {
                problems = lastProblems;   // the edits changed nothing, so the original problems stand
                problems.insert(problems.begin(), "your edits were not applied (nothing changed): " + next.error());
            }
        }
        if (edited) {   // rebuild the plan from the edited document
            json = doc["plan.json"];
            blocks.clear();
            for (const auto& [path, text] : doc) if (path != "plan.json") blocks.push_back({path, text});
            withoutBlocks = json;
        } else if (!haveDocument || problems.empty()) {   // a first reply, or a whole new plan: the old document is replaced
            if (reply->finishReason == "length") problems.push_back("the reply was cut off at the token limit: make the plan smaller (fewer or shorter items and cards) and reply with the complete JSON object");
            else if (json.empty()) problems.push_back("the reply holds no JSON object: reply with ONE plan-2 JSON object and nothing else");
        }
        if (problems.empty()) {
            auto plan = Plan::fromJson(json);
            if (!plan)
                if (const std::string fixed = repairJson(withoutBlocks); !fixed.empty())
                    if (auto again = Plan::fromJson(fixed)) {
                        plan = std::move(again);
                        std::fprintf(stderr, "llm-planner: turn %d: the JSON was mended (quotes escaped, brackets closed)\n", turn);
                    }
            if (!plan) problems.push_back("the plan is refused: " + plan.error() + (plan.error().find("not valid JSON") != std::string::npos
                    ? ". Check the JSON: every string on ONE line with no double quote inside it (use single quotes), commas between all fields and items, 'reasons' is a list of short sentences only, and every item (id, title, persona, goal, files, deps, complexity, card, gate) goes inside the 'items' list" : ""));
            else {
                if (plan->task.empty()) plan->task = request;
                if (edited) plan->scaffold.clear();   // the document's files are the scaffold
                for (const auto& b : blocks) {   // a block replaces a scaffold file of the same path in the JSON, or adds one
                    auto same = std::find_if(plan->scaffold.begin(), plan->scaffold.end(), [&](const PlanFile& f) { return f.path == b.path; });
                    if (same != plan->scaffold.end()) same->content = b.content; else plan->scaffold.push_back(b);
                }
                for (const auto& m : mendPlan(*plan)) { std::fprintf(stderr, "llm-planner: turn %d: mended: %s\n", turn, m.c_str()); mendsMade.push_back({turn, m}); }
                if (auto ok = plan->validate(); !ok) problems.push_back("the plan is refused: " + ok.error());
                // given tests are the owner's (Plan::given): a planner that wrote them would be grading its own plan
                if (!plan->given.empty()) problems.push_back("the plan is refused: 'given' is for the owner's acceptance tests; the squad's tests come from tester items (leave 'given' out)");
                if (problems.empty()) problems = checkPlan(*plan);
                if (problems.empty())
                    if (const Harness* h = findHarness(plan->harness); h && h->checkPlanScaffold)
                        if (const std::string bad = h->checkPlanScaffold(plan->toJson()); !bad.empty()) problems.push_back(bad);
                PlanLint barLint;
                if (problems.empty()) {
                    barLint = lintPlan(*plan, 5.0, slots);
                    lastSpeedup = barLint.expectedSpeedup;
                    if (minSpeedup > 0 && barLint.expectedSpeedup + 1e-9 < minSpeedup) {
                        if (barLint.expectedSpeedup > bestSpeedup) { bestSpeedup = barLint.expectedSpeedup; bestPlanJson = plan->toJson(); bestLintText = barLint.toText(); }
                        char num[400];
                        std::snprintf(num, sizeof num, "the plan is valid but too serial: its expected speedup on %d slots is %.2fx and the bar is %.2fx (at most %d items can be ready at once; no schedule can beat %.2fx)", slots, barLint.expectedSpeedup, minSpeedup, barLint.maxWidth, barLint.boundSpeedup);
                        std::string why = num;
                        why += ". Raise it by splitting the work into more INDEPENDENT deliverables: modules that share no files and depend on nothing but their own tester item, each with its own tester item and developer item, so that several items can start at once and a slot that frees finds work waiting. Do not pad with invented parts; do not split one file's internals into items. Keep every card short";
                        problems.push_back(why);
                        std::fprintf(stderr, "llm-planner: turn %d: valid, but %.2fx is below the %.2fx bar\n", turn, barLint.expectedSpeedup, minSpeedup);
                    }
                }
                if (problems.empty()) {
                    problemsByTurn.push_back({});
                    kindByTurn.push_back(edited ? "edit" : "plan");
                    barMet = true;
                    const PlanLint& lint = barLint;
                    lintText = lint.toText();
                    if (!writeFile(dir / "plan.json", plan->toJson())) { std::fprintf(stderr, "llm-planner: cannot write plan.json\n"); return 1; }
                    events::metric("plan.items", static_cast<double>(plan->items.size()));
                    events::metric("plan.turns", static_cast<double>(turn));
                    events::progress(maxTurns, maxTurns, "planned");
                    record(true, turn);
                    std::printf("%s", lintText.c_str());
                    std::printf("planned %zu item(s) for '%s' on harness %s in %d turn(s)\n", plan->items.size(), request.c_str(), plan->harness.c_str(), turn);
                    return 0;
                }
                Plan bare = *plan;   // the mended plan is the document to repair: its JSON laid out, its scaffold as files
                bare.scaffold.clear();
                doc.clear();
                doc["plan.json"] = layoutJson(bare.toJson()) + "\n";
                for (const auto& f : plan->scaffold) doc[f.path] = f.content;
                haveDocument = true;
            }
            if (!plan && !edited) { toDocument(json, withoutBlocks, blocks); haveDocument = true; }   // not even a plan: repair the raw text
        }
        if (!haveDocument) {   // nothing to edit (no JSON at all, or cut off): the request is asked again with the reasons
            std::string again = user + "\n\nAn earlier reply was refused for these reasons; write the COMPLETE plan again, fixing them:\n";
            for (const auto& p : problems) again += "- " + p + "\n";
            messages = {{"system", plannerSystemPrompt(slots, stance)}, {"user", again}};
        }
        // The document is kept on disk, every turn: the planner can be resumed from it, and a reader sees what each repair changed.
        for (const auto& [path, text] : doc) writeFile(dir / "planner" / "doc" / ("turn-" + std::to_string(turn)) / path, text);
        lastProblems = problems;
        kindByTurn.push_back(edited ? "edit" : json.empty() ? "unreadable" : "plan");
        problemsByTurn.push_back(problems);
        conversation += "\n## Turn " + std::to_string(turn) + " (the harness)\n\n";
        for (const auto& p : problems) conversation += "- " + p + "\n";
        std::fprintf(stderr, "llm-planner: turn %d refused: %zu problem(s); first: %s\n", turn, problems.size(), problems.front().substr(0, 200).c_str());
    }
    if (!bestPlanJson.empty()) {   // valid plans exist but none reached the bar: the best one stands, and the record says the bar was not met
        if (!writeFile(dir / "plan.json", bestPlanJson)) return 1;
        lintText = bestLintText;
        lastSpeedup = bestSpeedup;
        record(true, static_cast<int>(problemsByTurn.size()));
        std::printf("%s", lintText.c_str());
        std::printf("planned with the BAR NOT MET: best expected speedup %.2fx, bar %.2fx, after %zu turn(s)\n", bestSpeedup, minSpeedup, problemsByTurn.size());
        return 0;
    }
    record(false, static_cast<int>(problemsByTurn.size()));
    std::fprintf(stderr, "llm-planner: no valid plan in %zu turn(s)\n", problemsByTurn.size());
    return 1;
}

}  // namespace sub0squad
