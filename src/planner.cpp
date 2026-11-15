#include "sub0squad/planner.hpp"

#include "json_util.hpp"
#include "sub0squad/engine.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/plan_checks.hpp"
#include "sub0squad/plan_lint.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
bool writeFile(const fs::path& p, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o << text;
    return static_cast<bool>(o);
}
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

std::string plannerSystemPrompt(int slots)
{
    std::string harnesses;
    for (const auto& name : harnessNames()) {
        const Harness* h = findHarness(name);
        if (!h) continue;
        harnesses += "- \"" + name + "\" (gate kind \"" + h->gateKind + "\", test files end in \"" + h->testSuffix + "\"): " + (h->ownScaffoldBrief ? h->ownScaffoldBrief() : h->brief ? h->brief() : std::string{}) + "\n";
    }
    std::string s;
    s += "You are the PLANNER of a small software squad. You turn a request into a plan the squad can build and verify. The workers are small local models: each sees ONLY its own work card, so every card must be exact and short. ";
    s += "Reply with ONE JSON object in `plan-2` form, then the scaffold file blocks described below, and nothing else (no markdown fences, no commentary).\n\n";
    s += "PROJECT HARNESSES (set \"harness\" to the one that fits the request; if the request names a platform, use it):\n" + harnesses + "\n";
    s += "THE FORM (angle brackets are placeholders):\n";
    s += "{\"schema\":\"plan-2\",\"task\":\"<the request>\",\"harness\":\"<harness name>\",\"project\":\"<short-name>\",\"reasons\":[\"<why this split>\"],\n";
    s += " \"items\":[{\"id\":\"test-<area>\",\"title\":\"..\",\"persona\":\"tester\",\"goal\":\"one sentence\",\"files\":[\"tests/<area><test suffix>\"],\"deps\":[],\"complexity\":2,\n";
    s += "   \"card\":{\"context\":\"1-2 sentences\",\"contract\":\"the exact interface and behaviour\",\"acceptance\":[\"observable checks\"],\"include\":[],\"notes\":[],\"examples\":[\"<input> = <exact result>\"]},\n";
    s += "   \"gate\":{\"kind\":\"<gate kind>\",\"tests\":[\"tests/<area><test suffix>\"],\"minTests\":8,\"mustFailOnStub\":true}},\n";
    s += "  {\"id\":\"dev-<area>\",\"title\":\"..\",\"persona\":\"developer\",\"goal\":\"one sentence\",\"files\":[\"src/<file>.cpp\"],\"deps\":[\"test-<area>\"],\"complexity\":3,\n";
    s += "   \"card\":{\"context\":\"..\",\"contract\":\"..\",\"acceptance\":[\"every test in the tester's file passes\"],\"include\":[\"tests/<area><test suffix>\"],\"notes\":[],\"examples\":[\"<the same facts>\"]},\n";
    s += "   \"gate\":{\"kind\":\"<gate kind>\",\"tests\":[\"tests/<area><test suffix>\"],\"minTests\":8,\"mustFailOnStub\":false}}]}\n";
    s += "AFTER the JSON object, write each scaffold file as a plain file block (NOT inside the JSON):\n=== FILE: include/<name>/<name>.hpp ===\n<the project's interface: declarations only>\n=== END FILE ===\n=== FILE: src/<file>.cpp ===\n<a STUB: defines every declared function, returns a neutral value, never throws>\n=== END FILE ===\n\n";
    if (slots > 0) {
        s += "THE ENGINE: it runs " + std::to_string(slots) + " requests at once, one per slot, and the goal is to keep every slot busy. Items take different, unknown amounts of time, so some finish early: a slot that frees must find a ready item waiting. ";
        s += "Plan for MORE independent work than slots (about " + std::to_string(slots + slots / 2) + " items that can start at once when the work really is independent), never fewer; but never pad with invented parts. Each item must fit one slot's context window and about an hour.\n\n";
    }
    s += "RULES\n";
    s += "- Test-first. A tester item (persona \"tester\", no deps) writes the tests from the contract; a developer item depends on it, lists its test file in card.include and gate.tests, and is graded by tests it did not write. Testers write only under tests/; developers never write tests; no two items edit the same file.\n";
    s += "- The interface comes first. For a native (C++) project the PLAN carries the project's own interface as scaffold FILE blocks: a header that declares what the developers implement, and a STUB for every source file a developer writes (every declared function defined, returning a neutral value such as 0, an empty string or NaN, and never throwing, so the tester's tests fail against it instead of crashing). The stubs must compile with -Wall -Wextra -Werror (a parameter a stub does not use needs a cast to void). The harness supplies the build file and the test helpers (CMakeLists.txt, tests/helpers/); do not write those.\n";
    s += "- Worked examples. card.examples (at least 3 on every tester and developer item; the same facts on both) are lines of the form `input = exact result`, worked out CAREFULLY by you: they are facts both the tests and the code are checked against, so a wrong example breaks the project. Prefer examples that exercise precedence, edge cases and errors, not only the easy case.\n";
    s += "- Be exact in the contract: names, parameter types, result types, how an error is reported, the order of arguments of anything a test calls. Anything the contract leaves open will be guessed differently by the tester and the developer.\n";
    s += "- Few items, each with one purpose. Make ONE tester item and ONE developer item per independent deliverable (a library; a command-line program that uses it): a calculator is an engine item pair and a command-line app item pair, four items (more only where the work is really independent). NEVER make items of a file's internal parts (a lexer, a parser, a \"misc\" item): the developer divides its own file into sections, and two items may not edit the same file. At most 8 items.\n";
    s += "- Keep the whole reply SMALL: under about 2500 tokens. One-line examples, short contracts, no filler. A reply that is cut off is refused. Test files are named tests/NAME followed by the test suffix of the harness.\n";
    s += "- Item ids are lowercase letters, digits, - and _, and say the role and area: test-NAME for a tester, dev-NAME for a developer. At most 32 items; each work card at most about 1500 tokens.\n";
    s += "- Every gate is declarative (a gate kind and test files); a plan never contains commands. A tester's gate has mustFailOnStub true and minTests of at least 5; a developer's has mustFailOnStub false.\n";
    return s;
}

int runLlmPlanner(const std::vector<std::string>& args)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const fs::path dir = arg(args, "workflow-dir");
    const std::string request = arg(args, "request");
    const int maxTurns = std::atoi(arg(args, "max-turns", "4").c_str());
    const int timeoutSec = std::atoi(arg(args, "timeout", "3300").c_str());   // one engine call; a small model on a CPU writes a plan at a few tokens a second
    const int maxTokens = std::atoi(arg(args, "max-tokens", "3500").c_str());
    const int wallSec = std::atoi(arg(args, "wall", "7000").c_str());           // the whole job: no turn is started that cannot finish inside it
    const auto started = std::chrono::steady_clock::now();
    if (dir.empty() || request.empty()) { std::fprintf(stderr, "llm-planner: --workflow-dir and --request are required\n"); return 64; }
    auto engine = EngineProfile::parse(arg(args, "engine"));
    if (!engine) { std::fprintf(stderr, "llm-planner: %s\n", engine.error().c_str()); return 64; }

    // What the owner settled when the planning stage asked (clarify): the platform and the executor are data in the sandbox.
    std::string answers = slurp(dir / "brief.json");
    std::string user = "Request: " + request + "\n";
    if (!answers.empty()) user += "\nThe owner's answers to the planning questions (settled; follow them):\n" + answers + "\n";
    user += "\nWrite the plan.";

    const int slots = std::atoi(arg(args, "slots", std::to_string(engine->maxConcurrent)).c_str());
    std::vector<ChatMessage> messages = {{"system", plannerSystemPrompt(slots)}, {"user", user}};
    long promptTokens = 0, completionTokens = 0;
    std::string lintText;
    std::vector<std::vector<std::string>> problemsByTurn;
    std::string conversation = "# Planner conversation\n\nEngine `" + engine->name + "`, run seed " + std::to_string(engine->seed) + ".\n\n## System\n\n```\n" + messages[0].content + "\n```\n\n## User\n\n" + user + "\n";
    const auto record = [&](bool valid, int turns) {
        json::Writer w;
        w.beginObject().kv("schema", "planner-1").kv("request", request).kvBool("valid", valid).kv("turns", static_cast<std::int64_t>(turns))
            .kv("promptTokens", static_cast<std::int64_t>(promptTokens)).kv("completionTokens", static_cast<std::int64_t>(completionTokens)).key("problems").beginArray();
        for (const auto& turn : problemsByTurn) { w.beginArray(); for (const auto& p : turn) w.str(p); w.endArray(); }
        w.endArray().kv("lint", lintText).endObject();
        writeFile(dir / "planner.json", w.take() + "\n");
        writeFile(dir / "planner" / "conversation.md", conversation);
    };

    for (int turn = 1; turn <= maxTurns; ++turn) {
        const int left = wallSec - static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count());
        if (left < 300) { std::fprintf(stderr, "llm-planner: %d s of the wall budget left, too little for another turn\n", left); break; }
        events::progress(turn - 1, maxTurns, turn == 1 ? "planning" : "repairing the plan");
        // The reply streams into planner/live.txt so a watcher can read the plan as it is written; the file goes when the turn ends.
        const fs::path live = dir / "planner" / "live.txt";
        std::error_code ec;
        fs::create_directories(live.parent_path(), ec);
        std::ofstream liveOut(live, std::ios::binary | std::ios::trunc);
        auto reply = chatCompleteMessages(*engine, messages, maxTokens, std::min(timeoutSec, left), [&](std::string_view content, std::string_view) { liveOut << content << std::flush; });
        liveOut.close();
        fs::remove(live, ec);
        if (!reply) { std::fprintf(stderr, "llm-planner: engine call failed: %s\n", reply.error().c_str()); record(false, turn - 1); return 2; }
        promptTokens += reply->promptTokens; completionTokens += reply->completionTokens;
        writeFile(dir / "planner" / ("turn-" + std::to_string(turn) + ".txt"), reply->content);
        conversation += "\n## Turn " + std::to_string(turn) + " (the planner)\n\n```\n" + reply->content + "\n```\n";
        std::vector<std::string> problems;
        std::string withoutBlocks;
        const std::vector<PlanFile> blocks = extractFileBlocks(reply->content, withoutBlocks);
        const std::string json = extractJsonObject(withoutBlocks);
        if (reply->finishReason == "length") problems.push_back("the reply was cut off at the token limit: make the plan smaller (fewer or shorter items and cards) and reply with the complete JSON object");
        else if (json.empty()) problems.push_back("the reply holds no JSON object: reply with ONE plan-2 JSON object and nothing else");
        if (problems.empty()) {
            auto plan = Plan::fromJson(json);
            if (!plan) problems.push_back("the plan is refused: " + plan.error());
            else {
                if (plan->task.empty()) plan->task = request;
                for (const auto& b : blocks) {   // a block replaces a scaffold file of the same path in the JSON, or adds one
                    auto same = std::find_if(plan->scaffold.begin(), plan->scaffold.end(), [&](const PlanFile& f) { return f.path == b.path; });
                    if (same != plan->scaffold.end()) same->content = b.content; else plan->scaffold.push_back(b);
                }
                if (auto ok = plan->validate(); !ok) problems.push_back("the plan is refused: " + ok.error());
                if (problems.empty()) problems = checkPlan(*plan);
                if (problems.empty())
                    if (const Harness* h = findHarness(plan->harness); h && h->checkPlanScaffold)
                        if (const std::string bad = h->checkPlanScaffold(plan->toJson()); !bad.empty()) problems.push_back(bad);
                if (problems.empty()) {
                    problemsByTurn.push_back({});
                    const PlanLint lint = lintPlan(*plan, 5.0, slots);
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
            }
        }
        problemsByTurn.push_back(problems);
        std::string fix = "The plan was refused. Fix exactly these problems and reply with the COMPLETE corrected plan (one JSON object, nothing else):\n";
        for (const auto& p : problems) fix += "- " + p + "\n";
        conversation += "\n## Turn " + std::to_string(turn) + " (the harness)\n\n" + fix;
        std::fprintf(stderr, "llm-planner: turn %d refused: %zu problem(s); first: %s\n", turn, problems.size(), problems.front().substr(0, 200).c_str());
        // A long refused reply is not repeated: a small model's slot holds a few thousand tokens (the first real probe overflowed it on the third turn), and a
        // bloated plan is better rewritten than patched. It is also not replaced by a stand-in message: the model copies its own history (the placeholder came
        // back as the next reply). The request is simply asked again with the reasons attached.
        if (reply->content.size() > 3000) {
            messages = {{"system", plannerSystemPrompt(slots)}, {"user", user + "\n\nAn earlier attempt was refused (it is not repeated here) for these reasons; write the COMPLETE plan again, fixing them:\n" + fix.substr(fix.find('\n') + 1)}};
        } else {
            messages.push_back({"assistant", reply->content});
            messages.push_back({"user", fix});
        }
    }
    record(false, static_cast<int>(problemsByTurn.size()));
    std::fprintf(stderr, "llm-planner: no valid plan in %zu turn(s)\n", problemsByTurn.size());
    return 1;
}

}  // namespace sub0squad
