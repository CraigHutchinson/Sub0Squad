#include "sub0squad/triage.hpp"

#include "json_util.hpp"
#include "sub0squad/harness.hpp"

#include <algorithm>
#include <cctype>

namespace sub0squad {

namespace {
bool has(const std::string& text, std::initializer_list<const char*> words)
{
    for (const char* w : words) if (text.find(w) != std::string::npos) return true;
    return false;
}
}  // namespace

bool isKnownTask(const std::string& t) { return t == "calculator-basic" || t == "calculator-scientific" || t == "calculator-web"; }

std::string TriageResult::toJson() const
{
    json::Writer w;
    w.beginObject().kv("schema", "triage-1").kv("projectType", projectType).kv("blueprint", blueprint).kv("complexity", complexity).kvBool("split", split);
    w.key("personas").beginArray();
    for (const auto& p : personas) w.str(p);
    w.endArray().kv("architect", architect).kv("cardBudgetTokens", cardBudgetTokens).key("reasons").beginArray();
    for (const auto& r : reasons) w.str(r);
    w.endArray().endObject();
    return w.take();
}

TriageResult triage(const std::string& original)
{
    std::string t = original;
    for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    TriageResult r;
    const bool web = has(t, {"web", "html", "browser", "website", "page", "javascript", "frontend"});
    const bool calc = has(t, {"calculator", "calc"});
    const bool scientific = has(t, {"scientific", "trig", "sin", "log"});
    r.projectType = web ? "web-static" : calc ? "cpp-header" : "unknown";
    r.reasons.push_back(web ? "mentions a web page or browser: web-static project harness (static server, node test runner, fake DOM)" : calc ? "a calculator with no web wording: header-only C++ sandbox project" : "no known project type in the task text");
    int c = 1 + static_cast<int>(std::count(t.begin(), t.end(), ' ') / 12);
    for (const char* feature : {"scientific", "matrix", "plot", "graph", "units", "history", "database", "login", "network", "multi", "mobile"}) if (t.find(feature) != std::string::npos) ++c;
    r.complexity = std::min(5, std::max(1, c));
    if (calc && web) { r.blueprint = "calculator-web"; r.personas = {"tester", "developer"}; r.split = true; }
    else if (calc && scientific) { r.blueprint = "calculator-scientific"; r.personas = {"developer"}; r.split = true; }
    else if (calc) { r.blueprint = "calculator-basic"; r.personas = {"developer"}; r.split = false; }
    if (!r.blueprint.empty()) {
        r.architect = "not needed";
        r.reasons.push_back("a known blueprint ('" + r.blueprint + "') covers this task: no architect, no project manager, no frontier");
        r.reasons.push_back(r.split ? "blueprint splits the work into independent items" : "blueprint is a single item: splitting would cost more than it saves");
    } else {
        r.architect = "deferred";
        r.personas = {"tester", "developer"};
        r.split = true;
        r.reasons.push_back("no blueprint: an architect must write the plan; DEFERRED as a file handoff (needs-plan.md), nothing is sent anywhere");
    }
    return r;
}

std::string needsPlanPacket(const std::string& taskText, const TriageResult& t, const std::string& workflowId)
{
    std::string s;
    s += "# Plan needed for workflow " + workflowId + "\n\n";
    s += "You are asked to act as the **architect** (a frontier model or a person). Triage found no blueprint for this task, so the squad needs a plan.\n";
    s += "Answer with ONE file in `plan-2` JSON and supply it with `sub0squad project plan " + workflowId + " --file plan.json`.\n\n";
    s += "## Task\n\n" + taskText + "\n\n## Triage\n\n```json\n" + t.toJson() + "\n```\n\n";
    s += "## What the squad can do\n\n- Personas: `tester` (writes only under tests/), `developer` (writes implementation files, never tests).\n";
    s += "- Project harness `web-static` (the only one today): " + (findHarness("web-static") && findHarness("web-static")->brief ? findHarness("web-static")->brief() : std::string{}) + "\n";
    s += "- Gates are declarative: `{ \"kind\": \"node-test\", \"tests\": [...], \"minTests\": N, \"mustFailOnStub\": bool }`. A plan never contains commands.\n";
    s += "- Workers are SMALL local models: keep every item tiny (one to three files), test-first (a tester item with no dependencies, then the developer item that depends on it and `include`s the test file).\n\n";
    s += "## plan-2 syntax\n\n```json\n{ \"schema\": \"plan-2\", \"task\": \"...\", \"harness\": \"web-static\", \"project\": \"name\", \"reasons\": [\"why this split\"],\n";
    s += "  \"items\": [ { \"id\": \"short-id\", \"title\": \"...\", \"persona\": \"tester|developer\", \"goal\": \"one sentence\", \"files\": [\"relative/paths\"], \"deps\": [\"ids\"], \"complexity\": 1,\n";
    s += "    \"card\": { \"context\": \"1-2 sentences\", \"contract\": \"the interface, exact\", \"acceptance\": [\"observable checks\"], \"include\": [\"files from dependencies to inline\"], \"notes\": [] },\n";
    s += "    \"gate\": { \"kind\": \"node-test\", \"tests\": [\"tests/x.test.mjs\"], \"minTests\": 6, \"mustFailOnStub\": true } } ] }\n```\n\n";
    s += "## Rules the daemon enforces\n\n- Item ids are `[a-z0-9_-]`; dependencies exist and are acyclic; editable files are disjoint, relative, no `..`.\n";
    s += "- Each work card must be at most about " + std::to_string(t.cardBudgetTokens) + " tokens (length/4): that is the context a small model receives. Be exact and brief.\n";
    s += "- At most 32 items. The tester gate requires most tests to FAIL against the harness stubs, so tests must really exercise the contract.\n";
    return s;
}

}  // namespace sub0squad
