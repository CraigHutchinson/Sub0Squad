// Process as data (docs/PROCESS_DESIGN.md section 13): a process is an ordered list of stages that is read, written, validated and registered. Pinned here:
// the file format (every parameter of every stage written, unknown keys refused, types, ranges, tools, placeholders and predicates checked when the file is
// LOADED), the registry (a built-in equals its file under registry/processes, a user's file is found by name, a file that lies about its name is refused),
// `set` and `removeStage` (all or nothing), and a workflow spec that carries its process through the journal. That the ENGINE obeys a process is in
// test_workflow (the goldens prove the standard one changes nothing; further scenarios drive processes with other stages).
#include "check.hpp"
#include "sub0squad/process.hpp"
#include "sub0squad/workflow.hpp"

#include <fstream>
#include <sstream>

using namespace sub0squad;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
static bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
static std::string errorOf(const std::string& json) { auto p = Process::fromJson(json); return p ? std::string{"(parsed)"} : p.error(); }
/// A process file with the given stages (JSON objects, comma separated).
static std::string file(const std::string& stages) { return "{\"schema\":\"process-1\",\"name\":\"x\",\"stages\":[" + stages + "]}"; }
static const char* kPlan = "{\"name\":\"plan\",\"kind\":\"job\",\"exit\":\"plan.present\",\"job\":{\"step\":\"planner\",\"tool\":\"mock-planner\",\"accept\":\"plan\"}}";
static const char* kBuild = "{\"name\":\"build\",\"kind\":\"items\",\"entry\":\"stage.plan.done\"}";

int main()
{
    // The built-ins are valid, distinct, and round-trip through their own JSON.
    CHECK_EQ(builtinProcesses().size(), static_cast<std::size_t>(3));
    for (const Process* p : builtinProcesses()) {
        CHECK_EQ(p->validate(), std::string(""));
        auto back = Process::fromJson(p->toJson());
        CHECK(back.has_value() && *back == *p);
        if (back) CHECK_EQ(back->toJson(), p->toJson());
        for (const Process* q : builtinProcesses()) CHECK(p == q || p->name != q->name);
    }
    // The registry: each built-in has a file `registry/processes/<name>@<version>.json` that equals the code, byte for byte (so editing one without the other fails here).
    for (const Process* p : builtinProcesses()) {
        const fs::path path = fs::path{SUB0SQUAD_REGISTRY_DIR} / "processes" / (p->name + "@" + std::to_string(p->version) + ".json");
        CHECK(fs::exists(path));
        CHECK_EQ(slurp(path), p->toJson() + "\n");
        auto parsed = Process::fromJson(slurp(path));
        CHECK(parsed.has_value() && *parsed == *p);
    }
    // The standard process IS what the engine used to hard-code: three stages, their order, conditions, tools and numbers (the goldens hold the engine to it).
    {
        const Process& s = standardProcess();
        CHECK_EQ(s.stages.size(), static_cast<std::size_t>(3));
        const Stage* plan = s.stage("plan");
        const Stage* build = s.stage("build");
        const Stage* integrate = s.stage("integrate");
        CHECK(plan && build && integrate && s.stage("review") == nullptr && s.itemsStage() == build && s.producesPlan());
        if (plan && build && integrate) {
            CHECK(plan->kind == StageKind::job && plan->entry == "always" && plan->exit == "plan.present");
            CHECK(plan->job.step == "planner" && plan->job.tool == "mock-planner" && plan->job.tier == "stub" && plan->job.wallSeconds == 60 && plan->job.retries == 1 && plan->job.accept == "plan");
            CHECK(plan->job.args == (std::vector<std::string>{"--task", "${task}", "--out", "${sandbox}/plan.json"}));
            CHECK(build->kind == StageKind::items && build->entry == "stage.plan.done" && build->exit == "items.all(accepted)");
            const LeafParams& l = build->leaf;
            CHECK(l.developerExecutor == "session" && l.testerExecutor == "session" && l.allowDecompose && l.allowSections && l.allowReviseTests && l.allowRearchitect && l.allowEscalate);
            CHECK(l.agentWallLlmSeconds == 3 * 3600 && l.agentWallOtherSeconds == 120 && l.gateWallSeconds == 120 && l.architectWallLlmSeconds == 5400 && l.architectWallOtherSeconds == 120 && build->ascent.maxRoundTrips == 3);
            CHECK(integrate->kind == StageKind::job && integrate->entry == "stage.build.done && !requests.openBlocking" && integrate->exit == "job(integrator).succeeded");
            CHECK(integrate->job.step == "integrator" && integrate->job.label == "integration" && integrate->job.tool == "harness:integrate" && integrate->job.wallSeconds == 300 && integrate->job.retries == 0 && integrate->job.accept.empty());
        }
        // the other built-ins are the standard stages with a difference each: other parameters, one more stage
        const Stage* sectionsBuild = sectionsProcess().itemsStage();
        CHECK(sectionsBuild && sectionsBuild->leaf.developerExecutor == "fork" && sectionsBuild->leaf.testerExecutor == "canvas" && !sectionsBuild->leaf.allowDecompose);
        CHECK_EQ(reviewedProcess().stages.size(), static_cast<std::size_t>(4));
        const Stage* review = reviewedProcess().stage("review");
        CHECK(review && review->job.tool == "qa" && review->entry == "stage.integrate.done" && review->exit == "job(qa).succeeded");
    }
    // Every parameter is in the JSON, so a file says what runs; and every settable path names a key that is there.
    {
        const Process& s = standardProcess();
        const std::string json = s.toJson();
        const auto paths = s.parameterPaths();
        CHECK_EQ(paths.size(), static_cast<std::size_t>(9 + 15 + 9));   // a job stage: entry, exit and 7 of the job (its args are a list: edit the file); the items stage: entry, exit, 12 of the leaf, 1 of the ascent
        for (const auto& path : paths) CHECK(contains(json, ("\"" + path.substr(path.rfind('.') + 1) + "\":").c_str()));
        CHECK(paths.front() == "plan.entry" && paths.back() == "integrate.job.accept");
        CHECK(contains(json, "\"schema\":\"process-1\"") && contains(json, "\"executor\":{\"developer\":\"session\",\"tester\":\"session\"}"));
        CHECK(contains(json, "\"args\":[\"--task\",\"${task}\",\"--out\",\"${sandbox}/plan.json\"]"));
    }
    // The registered stage tools: a closed list; a stage cannot name anything else.
    {
        bool planner = false, integrate = false, qa = false;
        for (const StageTool& t : stageTools()) { planner = planner || std::string{t.name} == "mock-planner"; integrate = integrate || (std::string{t.name} == "harness:integrate" && t.needsPlan); qa = qa || std::string{t.name} == "qa"; }
        CHECK(planner && integrate && qa);
    }
    // Reading: a file may leave out what it does not change (defaults), and what is wrong is named with its stage.
    {
        auto minimal = Process::fromJson(file(std::string{kPlan} + "," + kBuild));
        CHECK(minimal.has_value());
        if (minimal) {
            CHECK(minimal->stages.size() == 2 && minimal->stages[0].entry == "always" && minimal->stages[0].job.label == "planner" && minimal->stages[0].job.wallSeconds == 120);
            CHECK(minimal->stages[1].exit == "items.all(accepted)" && minimal->stages[1].leaf == standardProcess().itemsStage()->leaf);
            CHECK(minimal->itemsStage() == &minimal->stages[1] && minimal->producesPlan());
        }
        auto jobOnly = Process::fromJson(file("{\"name\":\"lint\",\"kind\":\"job\",\"job\":{\"step\":\"lint\",\"tool\":\"plan-lint\",\"args\":[\"${sandbox}/plan.json\"]}}"));
        CHECK(jobOnly.has_value());
        if (jobOnly) CHECK(jobOnly->stages[0].exit == "job(lint).succeeded" && jobOnly->itemsStage() == nullptr && !jobOnly->producesPlan());   // a process need not plan or have items at all
        CHECK(contains(errorOf("{\"name\":\"x\"}"), "needs \"schema\": \"process-1\""));
        CHECK(contains(errorOf("{\"schema\":\"process-2\"}"), "unsupported process schema 'process-2'"));
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"x\"}"), "needs at least one stage"));
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"x\",\"leaf\":{}}"), "unknown key 'leaf' (a process has: schema, name, version, description, stages)"));
        CHECK(contains(errorOf("{\"schema\":\"process-1\",\"name\":\"Bad Name\",\"stages\":[" + std::string{kPlan} + "]}"), "must be 1 to 40 characters"));
        CHECK(contains(errorOf("not json"), "bad process"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"loop\"}")), "stage 'a': kind 'loop' must be job or items"));
        CHECK(contains(errorOf(file("{\"name\":\"build\",\"kind\":\"items\",\"leaf\":{\"allow\":{\"decompse\":false}}}")), "stage 'build': unknown key 'leaf.allow.decompse'"));   // a typo is not the default
        CHECK(contains(errorOf(file("{\"name\":\"build\",\"kind\":\"items\",\"job\":{\"step\":\"s\"}}")), "unknown key 'job.step' (a stage of kind items has:"));                 // a job's field on an items stage
        CHECK(contains(errorOf(file("{\"name\":\"build\",\"kind\":\"items\",\"ascent\":{\"maxRoundTrips\":\"three\"}}")), "'ascent.maxRoundTrips' must be a number"));
        CHECK(contains(errorOf(file("{\"name\":\"build\",\"kind\":\"items\",\"leaf\":{\"allow\":{\"escalate\":1}}}")), "'leaf.allow.escalate' must be true or false"));
        CHECK(contains(errorOf(file("{\"name\":\"build\",\"kind\":\"items\",\"leaf\":{\"executor\":{\"developer\":\"swarm\"}}}")), "must be session, canvas or fork"));
        CHECK(contains(errorOf(file("{\"name\":\"build\",\"kind\":\"items\",\"leaf\":{\"executor\":{\"tester\":\"fork\"}}}")), "a test file has no sections to fork"));
        CHECK(contains(errorOf(file("{\"name\":\"build\",\"kind\":\"items\",\"ascent\":{\"maxRoundTrips\":0}}")), "stage 'build': ascent.maxRoundTrips must be 1..10, not 0"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"items\"},{\"name\":\"b\",\"kind\":\"items\"}")), "stage 'b': a process has at most one stage of kind items"));
        CHECK(contains(errorOf(file(std::string{kPlan} + "," + kPlan)), "stage 'plan': the name is used twice"));
        CHECK(contains(errorOf(file(std::string{kPlan} + ",{\"name\":\"again\",\"kind\":\"job\",\"job\":{\"step\":\"planner\",\"tool\":\"qa\"}}")), "job.step 'planner' is also the step of stage 'plan'"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"job\",\"job\":{\"step\":\"gate\",\"tool\":\"qa\"}}")), "is the step of an item's job"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"job\",\"job\":{\"step\":\"s\",\"tool\":\"rm\"}}")), "job.tool 'rm' is not a registered stage tool (mock-planner, harness:integrate, qa, plan-lint)"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"job\",\"job\":{\"step\":\"s\",\"tool\":\"qa\",\"args\":[\"${home}/x\"]}}")), "'${home}' is not a placeholder"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"job\",\"job\":{\"step\":\"s\",\"tool\":\"qa\",\"args\":[1]}}")), "'job.args' must be a list of strings"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"job\",\"job\":{\"step\":\"s\",\"tool\":\"qa\",\"args\":\"--x\"}}")), "'job.args' must be a list of strings"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"job\",\"job\":{\"step\":\"s\",\"tool\":\"qa\",\"retries\":9}}")), "stage 'a': job.retries must be 0..5, not 9"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"job\",\"job\":{\"step\":\"s\",\"tool\":\"qa\",\"accept\":\"everything\"}}")), "job.accept 'everything' must be"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"job\",\"job\":{\"step\":\"s\",\"tool\":\"qa\",\"tier\":\"frontier\"}}")), "job.tier 'frontier' must be stub or local"));   // a process cannot switch a frontier tier on
        // predicates are parsed when the file is loaded: the stage, which of the two, the text and the column
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"items\",\"entry\":\"stage.a.done\"}")), "stage 'a': entry \"stage.a.done\": column 7: 'a' is not an earlier stage"));        // not itself
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"items\",\"entry\":\"stage.b.done\"},{\"name\":\"b\",\"kind\":\"job\",\"job\":{\"step\":\"s\",\"tool\":\"qa\"}}")), "'b' is not an earlier stage"));   // not a later one: no cycles
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"items\",\"exit\":\"items.all(finished)\"}")), "stage 'a': exit \"items.all(finished)\": column 11: 'finished' is not an item state"));
        CHECK(contains(errorOf(file("{\"name\":\"a\",\"kind\":\"items\",\"exit\":\"job(planner).succeeded\"}")), "'planner' is not the step of a job stage"));
        std::string many;
        for (int k = 0; k < 13; ++k) many += std::string{k ? "," : ""} + "{\"name\":\"s" + std::to_string(k) + "\",\"kind\":\"job\",\"job\":{\"step\":\"t" + std::to_string(k) + "\",\"tool\":\"qa\"}}";
        CHECK(contains(errorOf(file(many)), "at most 12 stages"));
    }
    // set(): `STAGE.PARAMETER`, typed, and all or nothing (the whole process is validated, predicates included).
    {
        Process p = standardProcess();
        CHECK(p.set("build.leaf.executor.developer", "fork").has_value() && p.itemsStage()->leaf.developerExecutor == "fork");
        CHECK(p.set("build.leaf.allow.decompose", "false").has_value() && !p.itemsStage()->leaf.allowDecompose);
        CHECK(p.set("build.ascent.maxRoundTrips", "5").has_value() && p.itemsStage()->ascent.maxRoundTrips == 5);
        CHECK(p.set("plan.job.retries", "0").has_value() && p.stage("plan")->job.retries == 0);
        CHECK(p.set("integrate.entry", "stage.build.done").has_value() && p.stage("integrate")->entry == "stage.build.done");
        CHECK(p.set("description", "my words").has_value() && p.description == "my words");
        const Process before = p;
        CHECK(contains(p.set("build.leaf.allow.nothing", "true").error(), "no parameter 'build.leaf.allow.nothing'"));
        CHECK(contains(p.set("review.job.retries", "1").error(), "no parameter 'review.job.retries'"));                   // no such stage here
        CHECK(contains(p.set("plan.leaf.allow.escalate", "false").error(), "no parameter"));                              // a leaf's field on a job stage
        CHECK(contains(p.set("plan.job.args", "x").error(), "no parameter"));                                             // lists are edited in the file
        CHECK(contains(p.set("build.leaf.allow.escalate", "maybe").error(), "must be true or false"));
        CHECK(contains(p.set("build.ascent.maxRoundTrips", "many").error(), "must be a whole number"));
        CHECK(contains(p.set("build.ascent.maxRoundTrips", "99").error(), "must be 1..10"));
        CHECK(contains(p.set("build.leaf.executor.tester", "fork").error(), "no sections to fork"));
        CHECK(contains(p.set("integrate.entry", "stage.review.done").error(), "'review' is not an earlier stage"));
        CHECK(contains(p.set("integrate.job.tool", "sh").error(), "not a registered stage tool"));
        CHECK(contains(p.set("integrate.job.step", "planner").error(), "is also the step of stage 'plan'"));
        CHECK(p == before);                                                      // none of the refused sets changed anything
    }
    // removeStage(): a stage nothing reads can go; one that a later stage reads cannot (the error is the reader's predicate).
    {
        Process p = standardProcess();
        const Process before = p;
        CHECK(contains(p.removeStage("build").error(), "without stage 'build': stage 'integrate': entry") && p == before);
        CHECK(contains(p.removeStage("nowhere").error(), "no stage 'nowhere'") && p == before);
        CHECK(p.removeStage("integrate").has_value() && p.stages.size() == 2 && p.stage("integrate") == nullptr);
        Process r = reviewedProcess();
        CHECK(r.removeStage("review").has_value());
        r.name = "standard"; r.description = standardProcess().description;
        CHECK(r == standardProcess());                                            // `reviewed` is the standard process and one stage
    }
    // The registry finds built-ins and the user's files; a file must say its own name; the error lists what exists.
    {
        const fs::path dir = makeTempDir("process-registry");
        CHECK(findProcess("standard", dir).has_value() && findProcess("sections", {}).has_value() && findProcess("reviewed", {}).has_value());
        Process mine = standardProcess();
        mine.name = "mine";
        CHECK(mine.set("build.leaf.allow.escalate", "false").has_value());
        { std::ofstream(dir / "mine.json") << mine.toJson() << "\n"; }
        auto found = findProcess("mine", dir);
        CHECK(found.has_value() && *found == mine);
        { std::ofstream(dir / "liar.json") << mine.toJson() << "\n"; }
        CHECK(contains(findProcess("liar", dir).error(), "holds a process named 'mine', not 'liar'"));
        { std::ofstream(dir / "broken.json") << "{\"schema\":\"process-1\",\"name\":\"broken\",\"stages\":[{\"name\":\"build\",\"kind\":\"items\",\"leaf\":{\"wat\":1}}]}"; }
        CHECK(contains(findProcess("broken", dir).error(), "unknown key 'leaf.wat'") && contains(findProcess("broken", dir).error(), "broken.json"));
        const auto none = findProcess("ghost", dir);
        CHECK(!none.has_value() && contains(none.error(), "no process named 'ghost'") && contains(none.error(), "standard, sections, reviewed"));
        CHECK(!findProcess("../etc/passwd", dir).has_value());                  // a name is never a path
        const auto names = processNames(dir);
        CHECK(names.size() == 6 && names[0] == "standard" && names[1] == "sections" && names[2] == "reviewed");   // built-ins first, then the user's, sorted
        if (names.size() == 6) CHECK(names[3] == "broken" && names[4] == "liar" && names[5] == "mine");
        removeTree(dir);
    }
    // A workflow spec carries its process through the journal: written whole, read whole; a spec without one runs the standard process; a bad one is refused.
    {
        WorkflowSpec spec;
        spec.task = "calculator-cpp";
        spec.sandboxDir = "/sb";
        spec.process = reviewedProcess();
        auto back = WorkflowSpec::fromJson(spec.toJson());
        CHECK(back.has_value() && back->process == reviewedProcess() && back->executor.empty());
        WorkflowSpec plain;
        plain.task = "t";
        const std::string text = plain.toJson();
        CHECK(contains(text, ",\"process\":{\"schema\":\"process-1\""));
        auto bare = WorkflowSpec::fromJson("{\"task\":\"t\"}");   // a spec that names no process
        CHECK(bare.has_value() && bare->process == standardProcess());
        std::string bad = text;
        bad.replace(bad.find("\"maxRoundTrips\":3"), 17, "\"maxRoundTrips\":0");
        CHECK(!WorkflowSpec::fromJson(bad).has_value());
        std::string cyclic = text;
        cyclic.replace(cyclic.find("\"entry\":\"always\""), 16, "\"entry\":\"stage.build.done\"");
        CHECK(!WorkflowSpec::fromJson(cyclic).has_value());                      // the daemon refuses a process whose first stage waits for a later one
        spec.executor = "teleport";
        CHECK(!WorkflowSpec::fromJson(spec.toJson()).has_value());
    }
    return finish();
}
