// The planning iteration (clarify.hpp; docs/BACKLOG.md "Getting started"): what a request leaves open is asked ONCE through the inbox, the defaults
// first, and remembered; what the request or the harness already says is never asked.
//   rules     the pure part: which topics a request settles and which become questions; one answer settles them; the brief; the project's record
//   daemon    the `clarified` process through a real daemon with the mock agent (no model): a vague request shows one blocking clarification and the
//             planner does not start; a wrong answer is refused and the request stays open; `platform=python` makes the workflow build the PYTHON
//             calculator; the answer is remembered, so the next vague request asks nothing; an answered request asks nothing; `--defaults` asks
//             nothing and builds the default; `go` is one answer.
#include "e2e.hpp"
#include "sub0squad/clarify.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/triage.hpp"

using namespace sub0squad;
using namespace e2e;
namespace fs = std::filesystem;

static bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
static ClarifyTopic topic(const std::vector<ClarifyTopic>& v, const char* key) { for (const auto& t : v) if (t.key == key) return t; return {}; }

/// The one platform fact triage needs from a harness, without the harnesses layer: a harness that claims a task which names its language.
static bool claimsCpp(const std::string& t) { return t.find("c++") != std::string::npos; }
static bool claimsPython(const std::string& t) { return t.find("python") != std::string::npos; }
static const Harness kCpp = [] { Harness h; h.name = "cpp-cmake"; h.claimsTask = &claimsCpp; return h; }();
static const Harness kPy = [] { Harness h; h.name = "py-unittest"; h.claimsTask = &claimsPython; return h; }();

int main(int, char** argv)
{
    registerHarness(kCpp);   // this program links the domain layer only: it registers the two facts triage reads
    registerHarness(kPy);
    {   // ---- rules ----
        ClarifyFacts vague;
        vague.taskText = "write a calculator"; vague.task = "calculator-cpp"; vague.agent = "llm";
        const ClarifyPlan open = clarify(vague);
        CHECK_EQ(open.questions.size(), static_cast<std::size_t>(2));
        if (open.questions.size() == 2) {
            CHECK(open.questions[0].key == "platform" && open.questions[0].values == std::vector<std::string>({"cpp", "python", "web"}));   // the default first
            CHECK(open.questions[1].key == "executor" && open.questions[1].values.front() == "session");
        }
        // every topic of the specification is accounted for: asked, or settled with a source and a reason
        for (const char* key : {"scope", "engine", "bar", "frontier"}) { const ClarifyTopic t = topic(open.settled, key); CHECK(t.source == "not-asked" && !t.note.empty()); }
        CHECK(has(open.requestTitle(), "platform, executor") && has(open.requestTitle(), "`go`"));
        CHECK(has(open.requestDetail(), "platform=cpp  C++ with CMake and ctest (the current default) [default]") && has(open.requestDetail(), "executor=fork"));
        const std::vector<std::string> options = open.requestOptions();
        CHECK_EQ(options.front(), std::string("go (platform cpp, executor session)"));
        // the plan survives its file
        const auto again = ClarifyPlan::fromJson(open.toJson());
        CHECK(again && again->toJson() == open.toJson());
        // one answer: `go` (also as the option is listed) takes every default
        for (const char* answer : {"go", "", "GO", "go (platform cpp, executor session)", "defaults"}) {
            const auto b = resolveBrief(open, answer);
            CHECK(b && b->task == "calculator-cpp" && b->executor == "session");
            if (b) { CHECK(topic(b->topics, "platform").source == "default"); CHECK_EQ(b->toRemember().size(), static_cast<std::size_t>(2)); }
        }
        // some chosen, the rest defaulted; a bare value only one question has
        const auto py = resolveBrief(open, "platform=python");
        CHECK(py && py->task == "calculator-py" && py->executor == "session" && topic(py->topics, "platform").source == "answer" && topic(py->topics, "executor").source == "default");
        const auto both = resolveBrief(open, "web, executor=fork");
        CHECK(both && both->task == "calculator-web" && both->executor == "fork");
        // an answer that is not one is an error that says what can be said
        const auto bad = resolveBrief(open, "platform=cobol");
        CHECK(!bad && has(bad.error(), "'cobol' is not a choice for platform") && has(bad.error(), "`platform=python`"));
        CHECK(!resolveBrief(open, "colour=blue") && !resolveBrief(open, "banana"));
        // never ask what the request says: a platform in the words, an executor in the flags
        ClarifyFacts said = vague;
        said.taskText = "write a calculator in Python"; said.task = "calculator-py"; said.executor = "canvas";
        const ClarifyPlan none = clarify(said);
        CHECK(none.questions.empty() && topic(none.settled, "platform").source == "request" && topic(none.settled, "platform").value == "python" && topic(none.settled, "executor").source == "request");
        const auto direct = resolveBrief(none, "");
        CHECK(direct && direct->task == "calculator-py" && direct->executor == "canvas" && direct->toRemember().empty());   // nothing was answered here: nothing to remember
        // never ask what the harness says: the mock agent has no executor
        ClarifyFacts mock = vague;
        mock.agent = "mock";
        const ClarifyPlan one = clarify(mock);
        CHECK(one.questions.size() == 1 && one.questions[0].key == "platform" && topic(one.settled, "executor").source == "harness");
        const auto mockBrief = resolveBrief(one, "go");
        CHECK(mockBrief && mockBrief->executor.empty());
        // a task named directly, and a task with no blueprint family
        ClarifyFacts named; named.task = "calculator-web";
        CHECK(clarify(named).questions.empty() && topic(clarify(named).settled, "platform").value == "web");
        ClarifyFacts custom; custom.task = "custom"; custom.taskText = "build a unit converter page";
        CHECK(clarify(custom).questions.empty() && topic(clarify(custom).settled, "platform").source == "not-asked");
        // asked once, then remembered; a remembered value that is not a choice is not an answer; --defaults asks nothing
        ClarifyFacts second = vague;
        second.remembered = {{"platform", "python"}, {"executor", "teletype"}};
        const ClarifyPlan remembered = clarify(second);
        CHECK(remembered.questions.size() == 1 && remembered.questions[0].key == "executor" && topic(remembered.settled, "platform").source == "remembered");
        const auto rb = resolveBrief(remembered, "go");
        CHECK(rb && rb->task == "calculator-py");
        ClarifyFacts defaults = vague;
        defaults.acceptDefaults = true;
        CHECK(clarify(defaults).questions.empty() && topic(clarify(defaults).settled, "platform").source == "default");
        // the facts and the record survive their files
        const auto facts = ClarifyFacts::fromJson(second.toJson());
        CHECK(facts && facts->taskText == second.taskText && facts->remembered == second.remembered && facts->agent == "llm");
        CHECK(parseProjectAnswers(projectAnswersJson({{"platform", "web"}})) == (std::map<std::string, std::string>{{"platform", "web"}}));
        CHECK(parseProjectAnswers("not json").empty());
        // triage says when it assumed the platform
        CHECK(triage("write a calculator").platformAssumed && !triage("write a calculator in C++").platformAssumed && !triage("write a calculator in python").platformAssumed);
    }
    {   // ---- through a real daemon, the mock agent ----
        const std::string exe = argv[1];
        Squad sq(exe, "clarify");
        sq.startDaemon(4, {"--frontier", "off"});
        const auto submitted = [&](const Result& r) { const auto at = r.out.find("workflow w-"); return at == std::string::npos ? std::string{} : r.out.substr(at + 9, 8); };
        const auto state = [&](const std::string& wf) { const std::string s = sq.cli({"project", "status", wf, "--json"}).out; const auto at = s.find("\"state\":\""); return at == std::string::npos ? std::string{} : s.substr(at + 9, s.find('"', at + 9) - at - 9); };
        const auto finished = [&](const std::string& wf) { for (int i = 0; i < 1500; ++i) { if (state(wf) != "running") break; sleepMs(100); } return state(wf); };
        const auto requestOpen = [&](const std::string& wf) { for (int i = 0; i < 300; ++i) { if (has(sq.cli({"request", "list", wf}).out, "clarification")) return true; sleepMs(50); } return false; };
        const fs::path workflows = sq.home() / "state" / "workflows";

        // 1. A vague request: one blocking clarification from the planner; nothing is planned while it is open.
        const std::string w1 = submitted(sq.cli({"do", "write a calculator", "--process", "clarified"}));
        CHECK_EQ(w1, std::string("w-000001"));
        CHECK(requestOpen(w1));
        const std::string inbox = sq.cli({"request", "list", w1}).out;
        CHECK(has(inbox, "r-1") && has(inbox, "open") && has(inbox, "planner") && has(inbox, "(blocking)") && has(inbox, "Before planning: platform? (answer `go` for the defaults)"));
        const std::string asked = slurp(workflows / w1 / "questions.json");
        CHECK(has(asked, "\"key\":\"platform\"") && !has(asked, "\"key\":\"executor\",\"question\""));   // the mock agent has no executor to ask about
        sleepMs(500);
        CHECK(!fs::exists(workflows / w1 / "plan.json") && state(w1) == "running");
        // a wrong answer is refused and nothing changes
        const Result wrong = sq.cli({"request", "answer", w1, "r-1", "--answer", "platform=cobol"});
        CHECK(wrong.exitCode != 0);   // (the reason goes to stderr; the rules above check its text)
        CHECK(has(sq.cli({"request", "list", w1}).out, "open"));
        // the answer: Python. The workflow builds the Python calculator.
        CHECK_EQ(sq.cli({"request", "answer", w1, "r-1", "--answer", "platform=python"}).exitCode, 0);
        CHECK_EQ(finished(w1), std::string("succeeded"));
        const std::string brief1 = slurp(workflows / w1 / "brief.json");
        CHECK(has(brief1, "\"schema\":\"brief-1\",\"task\":\"calculator-py\"") && has(brief1, "\"key\":\"platform\",\"value\":\"python\",\"source\":\"answer\""));
        CHECK(has(slurp(workflows / w1 / "plan.json"), "\"harness\":\"py-unittest\""));
        CHECK(fs::exists(workflows / w1 / "project" / "calc.py") || fs::exists(workflows / w1 / "project" / "src" / "calc.py") || has(sq.cli({"project", "status", w1, "--json"}).out, "integration passed"));
        // ... and remembers it
        CHECK_EQ(sq.cli({"project", "answers"}).out, std::string("platform=python\n"));

        // 2. The same vague request again: asked once, so nothing is asked now; it is Python again.
        const std::string w2 = submitted(sq.cli({"do", "write a calculator", "--process", "clarified"}));
        CHECK_EQ(finished(w2), std::string("succeeded"));
        CHECK(!has(sq.cli({"request", "list", w2}).out, "clarification"));
        CHECK(has(slurp(workflows / w2 / "brief.json"), "\"key\":\"platform\",\"value\":\"python\",\"source\":\"remembered\""));
        CHECK(has(slurp(workflows / w2 / "plan.json"), "\"harness\":\"py-unittest\""));

        // 3. An answered request (the words name the platform): nothing is asked, whatever is remembered.
        const std::string w3 = submitted(sq.cli({"do", "write a calculator as a web page", "--process", "clarified"}));
        CHECK_EQ(finished(w3), std::string("succeeded"));
        CHECK(!has(sq.cli({"request", "list", w3}).out, "clarification"));
        CHECK(has(slurp(workflows / w3 / "brief.json"), "\"key\":\"platform\",\"value\":\"web\",\"source\":\"request\"") && has(slurp(workflows / w3 / "plan.json"), "\"harness\":\"web-static\""));

        // 4. Forgotten, then the defaults path: no answer is needed and the default (C++) is built.
        CHECK_EQ(sq.cli({"project", "answers", "--forget"}).exitCode, 0);
        CHECK_EQ(sq.cli({"project", "answers"}).out, std::string("no answers are remembered\n"));
        const std::string w4 = submitted(sq.cli({"do", "write a calculator", "--process", "clarified", "--defaults"}));
        CHECK_EQ(finished(w4), std::string("succeeded"));
        CHECK(!has(sq.cli({"request", "list", w4}).out, "clarification"));
        CHECK(has(slurp(workflows / w4 / "brief.json"), "\"key\":\"platform\",\"value\":\"cpp\",\"source\":\"default\"") && has(slurp(workflows / w4 / "plan.json"), "\"harness\":\"cpp-cmake\""));

        // 5. "Just go" is ONE answer; the default it accepted is remembered like any other answer.
        CHECK_EQ(sq.cli({"project", "answers", "--forget"}).exitCode, 0);
        const std::string w5 = submitted(sq.cli({"do", "write a calculator", "--process", "clarified"}));
        CHECK(requestOpen(w5));
        CHECK_EQ(sq.cli({"request", "answer", w5, "r-1", "--answer", "go"}).exitCode, 0);
        CHECK_EQ(finished(w5), std::string("succeeded"));
        CHECK(has(slurp(workflows / w5 / "plan.json"), "\"harness\":\"cpp-cmake\""));
        CHECK_EQ(sq.cli({"project", "answers"}).out, std::string("platform=cpp\n"));

        // 6. The standard process is untouched: no planning stage, no question, no brief.
        const std::string w6 = submitted(sq.cli({"do", "write a calculator in Python"}));
        CHECK_EQ(finished(w6), std::string("succeeded"));
        CHECK(!fs::exists(workflows / w6 / "brief.json") && !fs::exists(workflows / w6 / "clarify-input.json"));
        sq.stopDaemon();
    }
    return finish();
}
