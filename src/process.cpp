#include "sub0squad/process.hpp"

#include "json_util.hpp"
#include "sub0squad/predicate.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;

namespace {

/// One parameter of a stage: its dotted path inside the stage and where it lives. The table is the single description of the file format: JSON writing, JSON
/// reading, `set` and the list of paths all come from it, so a new parameter is one line here and cannot be written but not read.
struct Field
{
    std::string path;
    bool* b = nullptr;
    int* i = nullptr;
    std::string* s = nullptr;
    std::vector<std::string>* list = nullptr;
};

std::vector<Field> fieldsOf(Stage& st)
{
    std::vector<Field> f;
    const auto flag = [&](const char* path, bool& v) { f.push_back({path, &v, nullptr, nullptr, nullptr}); };
    const auto num = [&](const char* path, int& v) { f.push_back({path, nullptr, &v, nullptr, nullptr}); };
    const auto text = [&](const char* path, std::string& v) { f.push_back({path, nullptr, nullptr, &v, nullptr}); };
    const auto list = [&](const char* path, std::vector<std::string>& v) { f.push_back({path, nullptr, nullptr, nullptr, &v}); };
    text("entry", st.entry);
    text("exit", st.exit);
    if (st.kind == StageKind::job) {
        text("job.step", st.job.step);
        text("job.label", st.job.label);
        text("job.tool", st.job.tool);
        list("job.args", st.job.args);
        text("job.tier", st.job.tier);
        num("job.wallSeconds", st.job.wallSeconds);
        num("job.retries", st.job.retries);
        text("job.accept", st.job.accept);
    } else {
        text("leaf.executor.developer", st.leaf.developerExecutor);
        text("leaf.executor.tester", st.leaf.testerExecutor);
        flag("leaf.allow.decompose", st.leaf.allowDecompose);
        flag("leaf.allow.sections", st.leaf.allowSections);
        flag("leaf.allow.reviseTests", st.leaf.allowReviseTests);
        flag("leaf.allow.rearchitect", st.leaf.allowRearchitect);
        flag("leaf.allow.escalate", st.leaf.allowEscalate);
        num("leaf.wallSeconds.agentLlm", st.leaf.agentWallLlmSeconds);
        num("leaf.wallSeconds.agentOther", st.leaf.agentWallOtherSeconds);
        num("leaf.wallSeconds.gate", st.leaf.gateWallSeconds);
        num("leaf.wallSeconds.architectLlm", st.leaf.architectWallLlmSeconds);
        num("leaf.wallSeconds.architectOther", st.leaf.architectWallOtherSeconds);
        flag("leaf.contractSections", st.leaf.contractSections);
        flag("leaf.mends.enabled", st.leaf.mendsEnabled);
        list("leaf.mends.disabled", st.leaf.mendsDisabled);
        num("ascent.maxRoundTrips", st.ascent.maxRoundTrips);
    }
    return f;
}

std::vector<std::string> split(const std::string& path)
{
    std::vector<std::string> v;
    std::istringstream in(path);
    for (std::string part; std::getline(in, part, '.');) v.push_back(part);
    return v;
}

bool validName(const std::string& n)
{
    if (n.empty() || n.size() > 40) return false;
    for (const char c : n) if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-')) return false;
    return n.front() != '-';
}

struct Scalar { enum class Kind { boolean, integer, text, list } kind = Kind::text; bool b = false; std::int64_t i = 0; std::string s; std::vector<std::string> list; };
using Found = std::vector<std::pair<std::string, Scalar>>;

/// Every value of a JSON object with its dotted path: an object nests, an array must hold strings, anything else is an error.
void walk(simdjson::ondemand::object obj, const std::string& prefix, Found& out)
{
    for (auto f : obj) {
        const std::string key{std::string_view{f.unescaped_key()}};
        auto v = f.value();
        const std::string path = prefix.empty() ? key : prefix + "." + key;
        Scalar sc;
        switch (v.type().value()) {
            case simdjson::ondemand::json_type::object: walk(v.get_object(), path, out); continue;
            case simdjson::ondemand::json_type::boolean: sc.kind = Scalar::Kind::boolean; sc.b = v.get_bool(); break;
            case simdjson::ondemand::json_type::number: sc.kind = Scalar::Kind::integer; sc.i = v.get_int64(); break;
            case simdjson::ondemand::json_type::string: sc.kind = Scalar::Kind::text; sc.s = json::toString(v); break;
            case simdjson::ondemand::json_type::array:
                sc.kind = Scalar::Kind::list;
                for (auto x : v.get_array()) {
                    auto e = x.value();
                    if (e.type().value() != simdjson::ondemand::json_type::string) throw std::runtime_error("'" + path + "' must be a list of strings");
                    sc.list.push_back(json::toString(e));
                }
                break;
            default: throw std::runtime_error("'" + path + "' must be a boolean, a number, a string, a list of strings or an object");
        }
        out.emplace_back(path, std::move(sc));
    }
}

bool validExecutor(const std::string& e, bool tester) { return e == "session" || e == "canvas" || (!tester && e == "fork"); }

std::string range(int v, int lo, int hi, const std::string& what) { return v < lo || v > hi ? what + " must be " + std::to_string(lo) + ".." + std::to_string(hi) + ", not " + std::to_string(v) : std::string{}; }

/// "" or what is wrong with one argument's `${...}` placeholders.
std::string badPlaceholder(const std::string& arg)
{
    for (std::size_t at = arg.find("${"); at != std::string::npos; at = arg.find("${", at + 2)) {
        const std::size_t close = arg.find('}', at);
        const std::string name = close == std::string::npos ? arg.substr(at) : arg.substr(at, close - at + 1);
        if (name != "${task}" && name != "${sandbox}" && name != "${workflow}") return "'" + name + "' is not a placeholder (${task}, ${sandbox} and ${workflow} are)";
    }
    return {};
}

std::string validateStage(const Stage& st, const std::string& where)
{
    if (st.kind == StageKind::job) {
        const JobParams& j = st.job;
        if (!validName(j.step)) return where + "job.step '" + j.step + "' must be 1 to 40 characters of a-z, 0-9 and '-'";
        for (const char* taken : {"agent", "gate", "decompose", "rearchitect"}) if (j.step == taken) return where + "job.step '" + j.step + "' is the step of an item's job: pick another name";
        if (j.label.empty() || j.label.size() > 40) return where + "job.label must be 1 to 40 characters";
        for (const char c : j.label) if (!std::isprint(static_cast<unsigned char>(c)) || c == '"' || c == '\\') return where + "job.label must be plain printable text";
        const auto& tools = stageTools();
        if (std::none_of(tools.begin(), tools.end(), [&](const StageTool& t) { return j.tool == t.name; })) {
            std::string known;
            for (const auto& t : tools) known += (known.empty() ? "" : ", ") + std::string{t.name};
            return where + "job.tool '" + j.tool + "' is not a registered stage tool (" + known + ")";
        }
        if (j.args.size() > 16) return where + "job.args has more than 16 arguments";
        for (const auto& a : j.args) {
            if (a.size() > 200) return where + "job.args: an argument is longer than 200 characters";
            if (const std::string bad = badPlaceholder(a); !bad.empty()) return where + "job.args: " + bad;
        }
        if (j.tier != "stub" && j.tier != "local") return where + "job.tier '" + j.tier + "' must be stub or local";
        if (j.accept != "" && j.accept != "plan") return where + "job.accept '" + j.accept + "' must be \"\" or \"plan\"";
        for (const std::string& bad : {range(j.wallSeconds, 10, 86400, where + "job.wallSeconds"), range(j.retries, 0, 5, where + "job.retries")}) if (!bad.empty()) return bad;
    } else {
        const LeafParams& l = st.leaf;
        if (l.mendsDisabled.size() > 32) return where + "leaf.mends.disabled has more than 32 names";
        for (const auto& n : l.mendsDisabled) if (!validName(n)) return where + "leaf.mends.disabled: '" + n + "' is not a mend name (a-z, 0-9 and '-')";
        if (!validExecutor(l.developerExecutor, false)) return where + "leaf.executor.developer '" + l.developerExecutor + "' must be session, canvas or fork";
        if (!validExecutor(l.testerExecutor, true)) return where + "leaf.executor.tester '" + l.testerExecutor + "' must be session or canvas (a test file has no sections to fork)";
        for (const std::string& bad : {range(l.agentWallLlmSeconds, 60, 604800, where + "leaf.wallSeconds.agentLlm"), range(l.agentWallOtherSeconds, 10, 3600, where + "leaf.wallSeconds.agentOther"),
                                       range(l.gateWallSeconds, 10, 3600, where + "leaf.wallSeconds.gate"), range(l.architectWallLlmSeconds, 60, 86400, where + "leaf.wallSeconds.architectLlm"),
                                       range(l.architectWallOtherSeconds, 10, 3600, where + "leaf.wallSeconds.architectOther"), range(st.ascent.maxRoundTrips, 1, 10, where + "ascent.maxRoundTrips")})
            if (!bad.empty()) return bad;
    }
    return {};
}

void writeStage(json::Writer& w, const Stage& stage)
{
    Stage copy = stage;
    w.beginObject().kv("name", stage.name).kv("kind", stage.kind == StageKind::job ? "job" : "items");
    std::vector<std::string> open;   // the objects currently open, as path components
    for (const Field& f : fieldsOf(copy)) {
        auto parts = split(f.path);
        const std::string leafKey = parts.back();
        parts.pop_back();
        std::size_t common = 0;
        while (common < open.size() && common < parts.size() && open[common] == parts[common]) ++common;
        while (open.size() > common) { w.endObject(); open.pop_back(); }
        for (std::size_t k = common; k < parts.size(); ++k) { w.key(parts[k]).beginObject(); open.push_back(parts[k]); }
        if (f.b) w.kvBool(leafKey, *f.b);
        else if (f.i) w.kv(leafKey, *f.i);
        else if (f.s) w.kv(leafKey, *f.s);
        else { w.key(leafKey).beginArray(); for (const auto& x : *f.list) w.str(x); w.endArray(); }
    }
    while (!open.empty()) { w.endObject(); open.pop_back(); }
    w.endObject();
}

/// Put one found value into the stage; "" or what is wrong. `where` prefixes the message ("stage 'build': ").
std::string assign(Stage& st, const std::string& path, const Scalar& v, const std::string& where)
{
    auto fields = fieldsOf(st);
    const auto it = std::find_if(fields.begin(), fields.end(), [&](const Field& f) { return f.path == path; });
    if (it == fields.end()) {
        std::string all;
        for (const auto& f : fields) all += (all.empty() ? "" : ", ") + f.path;
        return where + "unknown key '" + path + "' (a stage of kind " + (st.kind == StageKind::job ? "job" : "items") + " has: " + all + ")";
    }
    if (it->b && v.kind != Scalar::Kind::boolean) return where + "'" + path + "' must be true or false";
    if (it->i && v.kind != Scalar::Kind::integer) return where + "'" + path + "' must be a number";
    if (it->s && v.kind != Scalar::Kind::text) return where + "'" + path + "' must be a string";
    if (it->list && v.kind != Scalar::Kind::list) return where + "'" + path + "' must be a list of strings";
    if (it->i && (v.i < -1000000 || v.i > 1000000)) return where + "'" + path + "' is out of range";
    if (it->b) *it->b = v.b; else if (it->i) *it->i = static_cast<int>(v.i); else if (it->s) *it->s = v.s; else *it->list = v.list;
    return {};
}

/// What a stage means when its file leaves a field out.
void applyDefaults(Stage& st)
{
    if (st.kind == StageKind::job) {
        if (st.job.label.empty()) st.job.label = st.job.step;
        if (st.exit.empty()) st.exit = "job(" + st.job.step + ").succeeded";
    } else if (st.exit.empty()) st.exit = "items.all(accepted)";
}

Stage jobStage(const char* name, const char* entry, const char* exit, JobParams job)
{
    Stage s;
    s.name = name; s.kind = StageKind::job; s.entry = entry; s.exit = exit; s.job = std::move(job);
    return s;
}

}  // namespace

const std::vector<StageTool>& stageTools()
{
    static const std::vector<StageTool> tools = {
        {"mock-planner", "write the blueprint plan of a known task to the path after --out", false},
        {"harness:integrate", "the integration command of the plan's harness: merge the accepted items into <sandbox>/project and run the whole suite", true},
        {"qa", "quality findings over a source tree: duplicated code, the same helper declared twice, very long functions", false},
        {"plan-lint", "audit a plan file: width, critical path, rough time, serial tails", false},
    };
    return tools;
}

const Stage* Process::stage(std::string_view n) const
{
    for (const auto& s : stages) if (s.name == n) return &s;
    return nullptr;
}

const Stage* Process::itemsStage() const
{
    for (const auto& s : stages) if (s.kind == StageKind::items) return &s;
    return nullptr;
}

bool Process::producesPlan() const
{
    return std::any_of(stages.begin(), stages.end(), [](const Stage& s) { return s.kind == StageKind::job && s.job.accept == "plan"; });
}

std::string Process::validate() const
{
    if (!validName(name)) return "process name '" + name + "' must be 1 to 40 characters of a-z, 0-9 and '-', not starting with '-'";
    if (version < 1 || version > 1000) return "process version must be 1..1000";
    if (stages.empty()) return "a process needs at least one stage";
    if (stages.size() > 12) return "a process has at most 12 stages";
    Vocabulary vocabulary;
    for (const auto& s : stages) if (s.kind == StageKind::job) vocabulary.steps.push_back(s.job.step);
    int itemStages = 0;
    for (std::size_t k = 0; k < stages.size(); ++k) {
        const Stage& s = stages[k];
        if (!validName(s.name)) return "stage " + std::to_string(k + 1) + ": the name '" + s.name + "' must be 1 to 40 characters of a-z, 0-9 and '-'";
        const std::string where = "stage '" + s.name + "': ";
        for (std::size_t e = 0; e < k; ++e) {
            if (stages[e].name == s.name) return where + "the name is used twice";
            if (s.kind == StageKind::job && stages[e].kind == StageKind::job && stages[e].job.step == s.job.step) return where + "job.step '" + s.job.step + "' is also the step of stage '" + stages[e].name + "'";
        }
        if (s.kind == StageKind::items && ++itemStages > 1) return where + "a process has at most one stage of kind items (a plan's items do not say which stage they belong to)";
        if (const std::string bad = validateStage(s, where); !bad.empty()) return bad;
        if (auto p = Predicate::parse(s.entry, vocabulary); !p) return where + "entry \"" + s.entry + "\": " + p.error();
        if (auto p = Predicate::parse(s.exit, vocabulary); !p) return where + "exit \"" + s.exit + "\": " + p.error();
        vocabulary.stages.push_back(s.name);
    }
    return {};
}

std::string Process::toJson() const
{
    json::Writer w;
    w.beginObject().kv("schema", "process-1").kv("name", name).kv("version", version).kv("description", description).key("stages").beginArray();
    for (const auto& s : stages) writeStage(w, s);
    w.endArray().endObject();
    return w.take();
}

std::expected<Process, std::string> Process::fromJson(std::string_view text)
{
    Process p;
    bool schemaSeen = false;
    std::string problem;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string key{std::string_view{f.unescaped_key()}};
                auto v = f.value();
                if (key == "schema") {
                    const std::string schema = json::toString(v);
                    if (schema != "process-1") throw std::runtime_error("unsupported process schema '" + schema + "' (process-1 is the only one)");
                    schemaSeen = true;
                } else if (key == "name") p.name = json::toString(v);
                else if (key == "version") p.version = static_cast<int>(v.get_int64());
                else if (key == "description") p.description = json::toString(v);
                else if (key == "stages") {
                    for (auto element : v.get_array()) {
                        Found found;
                        walk(element.get_object(), "", found);
                        Stage st;
                        std::string kind;
                        for (const auto& [path, value] : found) {
                            if (path == "name") st.name = value.s;
                            else if (path == "kind") kind = value.s;
                        }
                        const std::string where = "stage '" + st.name + "': ";
                        if (kind != "job" && kind != "items") throw std::runtime_error(where + "kind '" + kind + "' must be job or items");
                        st.kind = kind == "job" ? StageKind::job : StageKind::items;
                        st.entry = "always";
                        for (const auto& [path, value] : found) {
                            if (path == "name" || path == "kind") continue;
                            if (const std::string bad = assign(st, path, value, where); !bad.empty()) throw std::runtime_error(bad);
                        }
                        applyDefaults(st);
                        p.stages.push_back(std::move(st));
                    }
                } else throw std::runtime_error("unknown key '" + key + "' (a process has: schema, name, version, description, stages)");
            }
        });
    } catch (const simdjson::simdjson_error& e) { return std::unexpected(std::string{"bad process: "} + e.what()); }
    catch (const std::runtime_error& e) { return std::unexpected(std::string{"bad process: "} + e.what()); }
    if (!schemaSeen) return std::unexpected("a process file needs \"schema\": \"process-1\"");
    if (const std::string bad = p.validate(); !bad.empty()) return std::unexpected(bad);
    return p;
}

std::expected<void, std::string> Process::set(std::string_view pathIn, std::string_view value)
{
    const std::string path{pathIn};
    Process trial = *this;
    if (path == "description") trial.description = std::string{value};
    else {
        const auto dot = path.find('.');
        const std::string stageName = path.substr(0, dot);
        const std::string rest = dot == std::string::npos ? std::string{} : path.substr(dot + 1);
        Stage* st = nullptr;
        for (auto& s : trial.stages) if (s.name == stageName) st = &s;
        const auto unknown = [&] {
            std::string all;
            for (const auto& x : parameterPaths()) all += (all.empty() ? "" : ", ") + x;
            return std::unexpected("no parameter '" + path + "' (this process has: " + all + ")");
        };
        if (!st) return unknown();
        auto fields = fieldsOf(*st);
        const auto it = std::find_if(fields.begin(), fields.end(), [&](const Field& f) { return f.path == rest && !f.list; });
        if (it == fields.end()) return unknown();
        if (it->b) {
            if (value != "true" && value != "false") return std::unexpected("'" + path + "' must be true or false, not '" + std::string{value} + "'");
            *it->b = value == "true";
        } else if (it->i) {
            const std::string v{value};
            char* end = nullptr;
            const long n = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || *end != '\0' || n < -1000000 || n > 1000000) return std::unexpected("'" + path + "' must be a whole number, not '" + v + "'");
            *it->i = static_cast<int>(n);
        } else *it->s = std::string{value};
    }
    if (const std::string bad = trial.validate(); !bad.empty()) return std::unexpected(bad);
    *this = std::move(trial);
    return {};
}

std::expected<void, std::string> Process::removeStage(std::string_view stageName)
{
    Process trial = *this;
    const auto it = std::find_if(trial.stages.begin(), trial.stages.end(), [&](const Stage& s) { return s.name == stageName; });
    if (it == trial.stages.end()) return std::unexpected("no stage '" + std::string{stageName} + "'");
    trial.stages.erase(it);
    if (const std::string bad = trial.validate(); !bad.empty()) return std::unexpected("without stage '" + std::string{stageName} + "': " + bad);
    *this = std::move(trial);
    return {};
}

std::vector<std::string> Process::parameterPaths() const
{
    std::vector<std::string> v;
    for (Stage st : stages)
        for (const Field& f : fieldsOf(st)) if (!f.list) v.push_back(st.name + "." + f.path);
    return v;
}

const Process& standardProcess()
{
    static const Process p = [] {
        Process x;
        x.name = "standard";
        x.version = 1;
        x.description = "Plan, build every item under the escalation ladder (retry, divide, send the tests back, rewrite the card, escalate), integrate.";
        x.stages.push_back(jobStage("plan", "always", "plan.present", {"planner", "planner", "mock-planner", {"--task", "${task}", "--out", "${sandbox}/plan.json"}, "stub", 60, 1, "plan"}));
        Stage build;
        build.name = "build"; build.kind = StageKind::items; build.entry = "stage.plan.done"; build.exit = "items.all(accepted)";
        x.stages.push_back(build);
        x.stages.push_back(jobStage("integrate", "stage.build.done && !requests.openBlocking", "job(integrator).succeeded", {"integrator", "integration", "harness:integrate", {"--workflow-dir", "${sandbox}"}, "stub", 300, 0, ""}));
        return x;
    }();
    return p;
}

const Process& sectionsProcess()
{
    static const Process p = [] {
        Process x = standardProcess();
        x.name = "sections";
        x.description = "The standard stages, with developers' items written as forked sections from the first attempt, testers' on the canvas, and no division into modules. For compiled platforms and for work that is long rather than wide.";
        Stage& build = x.stages[1];
        build.leaf.developerExecutor = "fork";
        build.leaf.testerExecutor = "canvas";
        build.leaf.allowDecompose = false;
        return x;
    }();
    return p;
}

const Process& reviewedProcess()
{
    static const Process p = [] {
        Process x = standardProcess();
        x.name = "reviewed";
        x.description = "The standard stages, then a review: the quality scan (duplicated code, a helper declared twice, very long functions) runs over the integrated project and its findings are the stage's log.";
        x.stages.push_back(jobStage("review", "stage.integrate.done", "job(qa).succeeded", {"qa", "review", "qa", {"${sandbox}/project"}, "stub", 120, 0, ""}));
        return x;
    }();
    return p;
}

const std::vector<const Process*>& builtinProcesses()
{
    static const std::vector<const Process*> all = {&standardProcess(), &sectionsProcess(), &reviewedProcess()};
    return all;
}

std::expected<Process, std::string> findProcess(std::string_view name, const fs::path& userDir)
{
    for (const Process* p : builtinProcesses()) if (name == p->name) return *p;
    if (!validName(std::string{name})) return std::unexpected("'" + std::string{name} + "' is not a process name (a-z, 0-9 and '-')");
    if (!userDir.empty()) {
        const fs::path file = userDir / (std::string{name} + ".json");
        std::ifstream in(file, std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            auto p = Process::fromJson(ss.str());
            if (!p) return std::unexpected(file.string() + ": " + p.error());
            if (p->name != name) return std::unexpected(file.string() + ": the file holds a process named '" + p->name + "', not '" + std::string{name} + "'");
            return p;
        }
    }
    std::string known;
    for (const auto& n : processNames(userDir)) known += (known.empty() ? "" : ", ") + n;
    return std::unexpected("no process named '" + std::string{name} + "' (known: " + known + ")");
}

std::vector<std::string> processNames(const fs::path& userDir)
{
    std::vector<std::string> names;
    for (const Process* p : builtinProcesses()) names.push_back(p->name);
    std::vector<std::string> mine;
    std::error_code ec;
    if (!userDir.empty())
        for (auto it = fs::directory_iterator(userDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (it->path().extension() == ".json" && validName(it->path().stem().string()) &&
                std::find(names.begin(), names.end(), it->path().stem().string()) == names.end()) mine.push_back(it->path().stem().string());
    std::sort(mine.begin(), mine.end());
    names.insert(names.end(), mine.begin(), mine.end());
    return names;
}

}  // namespace sub0squad
