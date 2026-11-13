// Process as data (docs/PROCESS_DESIGN.md): the parameters of the engine's stages are a value that is read, written, validated and registered. Pinned here:
// the file format (every parameter written, unknown keys refused, types and ranges checked), the registry (a built-in equals its file under registry/processes,
// a user's file is found by name, a file that lies about its name is refused), `set` (all or nothing), and a workflow spec that carries its process through
// the journal. That the ENGINE obeys a process is in test_workflow (the goldens prove the standard one changes nothing; further scenarios drive the others).
#include "check.hpp"
#include "sub0squad/process.hpp"
#include "sub0squad/workflow.hpp"

#include <fstream>
#include <sstream>

using namespace sub0squad;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
static bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
static std::string errorOf(const std::string& json) { auto p = Process::fromJson(json); return p ? std::string{} : p.error(); }

int main()
{
    // The built-ins are valid, distinct, and round-trip through their own JSON.
    CHECK(standardProcess().validate().empty() && sectionsProcess().validate().empty());
    CHECK_EQ(builtinProcesses().size(), static_cast<std::size_t>(2));
    CHECK(standardProcess().name != sectionsProcess().name);
    for (const Process* p : builtinProcesses()) {
        auto back = Process::fromJson(p->toJson());
        CHECK(back.has_value() && *back == *p);
        if (back) CHECK_EQ(back->toJson(), p->toJson());
    }
    // Every parameter is in the JSON, so a file says what runs; and the list of paths is the list of keys.
    {
        const std::string json = standardProcess().toJson();
        for (const auto& path : Process::parameterPaths()) {
            const std::string key = path.substr(path.rfind('.') + 1);
            CHECK(contains(json, ("\"" + key + "\":").c_str()));
        }
        CHECK_EQ(Process::parameterPaths().size(), static_cast<std::size_t>(16));
        CHECK(contains(json, "\"schema\":\"process-1\"") && contains(json, "\"executor\":{\"developer\":\"session\",\"tester\":\"session\"}"));
    }
    // The registry: each built-in has a file `registry/processes/<name>@<version>.json` that equals the code, byte for byte (so editing one without the other fails here).
    for (const Process* p : builtinProcesses()) {
        const fs::path file = fs::path{SUB0SQUAD_REGISTRY_DIR} / "processes" / (p->name + "@" + std::to_string(p->version) + ".json");
        CHECK(fs::exists(file));
        CHECK_EQ(slurp(file), p->toJson() + "\n");
        auto parsed = Process::fromJson(slurp(file));
        CHECK(parsed.has_value() && *parsed == *p);
    }
    // The standard process has the values the engine used before processes were data (the goldens hold the engine to them; this holds the numbers).
    {
        const Process& s = standardProcess();
        CHECK(s.planner.wallSeconds == 60 && s.planner.retries == 1 && s.leaf.agentWallLlmSeconds == 3 * 3600 && s.leaf.agentWallOtherSeconds == 120 && s.leaf.gateWallSeconds == 120);
        CHECK(s.leaf.architectWallLlmSeconds == 5400 && s.leaf.architectWallOtherSeconds == 120 && s.ascent.maxRoundTrips == 3 && s.integrate.wallSeconds == 300);
        CHECK(s.leaf.developerExecutor == "session" && s.leaf.testerExecutor == "session");
        CHECK(s.leaf.allowDecompose && s.leaf.allowSections && s.leaf.allowReviseTests && s.leaf.allowRearchitect && s.leaf.allowEscalate);
    }
    // Reading: what is wrong is named.
    {
        const std::string base = standardProcess().toJson();
        CHECK(contains(errorOf("{\"name\":\"x\"}").c_str(), "needs \"schema\": \"process-1\""));
        CHECK(contains(errorOf("{\"schema\":\"process-2\"}").c_str(), "unsupported process schema 'process-2'"));
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"x\",\"leaf\":{\"allow\":{\"decompse\":false}}}").c_str(), "unknown key 'leaf.allow.decompse'"));   // a typo is not the default
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"x\",\"ascent\":{\"maxRoundTrips\":\"three\"}}").c_str(), "'ascent.maxRoundTrips' must be a number"));
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"x\",\"leaf\":{\"allow\":{\"escalate\":1}}}").c_str(), "'leaf.allow.escalate' must be true or false"));
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"x\",\"leaf\":{\"executor\":{\"developer\":\"swarm\"}}}").c_str(), "must be session, canvas or fork"));
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"x\",\"leaf\":{\"executor\":{\"tester\":\"fork\"}}}").c_str(), "a test file has no sections to fork"));
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"x\",\"ascent\":{\"maxRoundTrips\":0}}").c_str(), "ascent.maxRoundTrips must be 1..10, not 0"));
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"Bad Name\"}").c_str(), "must be 1 to 40 characters"));
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"x\",\"planner\":[1]}").c_str(), "bad process"));
        CHECK(contains(errorOf("not json").c_str(), "bad process"));
        auto minimal = Process::fromJson("{\"schema\":\"process-1\",\"name\":\"tiny\"}");
        CHECK(minimal.has_value() && minimal->name == "tiny" && minimal->leaf == standardProcess().leaf);   // a file may name only what it changes
    }
    // set(): by dotted path, typed, and all or nothing.
    {
        Process p = standardProcess();
        CHECK(p.set("leaf.executor.developer", "fork").has_value() && p.leaf.developerExecutor == "fork");
        CHECK(p.set("leaf.allow.decompose", "false").has_value() && !p.leaf.allowDecompose);
        CHECK(p.set("ascent.maxRoundTrips", "5").has_value() && p.ascent.maxRoundTrips == 5);
        CHECK(p.set("description", "my words").has_value() && p.description == "my words");
        const Process before = p;
        CHECK(contains(p.set("leaf.allow.nothing", "true").error().c_str(), "no parameter 'leaf.allow.nothing'"));
        CHECK(contains(p.set("leaf.allow.escalate", "maybe").error().c_str(), "must be true or false"));
        CHECK(contains(p.set("ascent.maxRoundTrips", "many").error().c_str(), "must be a whole number"));
        CHECK(contains(p.set("ascent.maxRoundTrips", "99").error().c_str(), "must be 1..10"));
        CHECK(contains(p.set("leaf.executor.tester", "fork").error().c_str(), "no sections to fork"));
        CHECK(p == before);                                                      // none of the refused sets changed anything
    }
    // The registry finds built-ins and the user's files; a file must say its own name; the error lists what exists.
    {
        const fs::path dir = makeTempDir("process-registry");
        CHECK(findProcess("standard", dir).has_value() && findProcess("sections", {}).has_value());
        Process mine = standardProcess();
        mine.name = "mine";
        mine.leaf.allowEscalate = false;
        { std::ofstream(dir / "mine.json") << mine.toJson() << "\n"; }
        auto found = findProcess("mine", dir);
        CHECK(found.has_value() && *found == mine);
        { std::ofstream(dir / "liar.json") << mine.toJson() << "\n"; }
        CHECK(contains(findProcess("liar", dir).error().c_str(), "holds a process named 'mine', not 'liar'"));
        { std::ofstream(dir / "broken.json") << "{\"schema\":\"process-1\",\"name\":\"broken\",\"leaf\":{\"wat\":1}}"; }
        CHECK(contains(findProcess("broken", dir).error().c_str(), "unknown key 'leaf.wat'") && contains(findProcess("broken", dir).error().c_str(), "broken.json"));
        const auto none = findProcess("ghost", dir);
        CHECK(!none.has_value() && contains(none.error().c_str(), "no process named 'ghost'") && contains(none.error().c_str(), "standard, sections"));
        CHECK(!findProcess("../etc/passwd", dir).has_value());                  // a name is never a path
        const auto names = processNames(dir);
        CHECK(names.size() >= 5 && names[0] == "standard" && names[1] == "sections");   // built-ins first, then the user's, sorted
        CHECK(names[2] == "broken" && names[3] == "liar" && names[4] == "mine");
        removeTree(dir);
    }
    // A workflow spec carries its process through the journal: written whole, read whole; a spec without one runs the standard process; a bad one is refused.
    {
        WorkflowSpec spec;
        spec.task = "calculator-cpp";
        spec.sandboxDir = "/sb";
        spec.process = sectionsProcess();
        auto back = WorkflowSpec::fromJson(spec.toJson());
        CHECK(back.has_value() && back->process == sectionsProcess() && back->executor.empty());
        WorkflowSpec plain;
        plain.task = "t";
        const std::string text = plain.toJson();
        CHECK(contains(text, ",\"process\":{\"schema\":\"process-1\""));
        const std::string legacy = "{\"task\":\"t\"}";   // a spec written before processes existed
        auto old = WorkflowSpec::fromJson(legacy);
        CHECK(old.has_value() && old->process == standardProcess());
        std::string bad = text;
        bad.replace(bad.find("\"maxRoundTrips\":3"), 17, "\"maxRoundTrips\":0");
        CHECK(!WorkflowSpec::fromJson(bad).has_value());
        spec.executor = "teleport";
        CHECK(!WorkflowSpec::fromJson(spec.toJson()).has_value());
    }
    return finish();
}
