#include "sub0squad/workflow.hpp"

#include "json_util.hpp"

#include "sub0squad/harness.hpp"
#include "sub0squad/mends.hpp"
#include "sub0squad/predicate.hpp"
#include <algorithm>
#include <set>
#include <cstdio>

namespace sub0squad {

const char* toString(WorkflowState s)
{
    switch (s) { case WorkflowState::running: return "running"; case WorkflowState::awaitingPlan: return "awaiting-plan"; case WorkflowState::succeeded: return "succeeded"; case WorkflowState::failed: return "failed"; case WorkflowState::cancelled: return "cancelled"; }
    return "?";
}

std::string WorkflowSpec::toJson() const
{
    json::Writer w;
    w.beginObject().kv("task", task).kv("taskText", taskText).kv("triage", triage).kv("sandbox", sandboxDir).kv("maxLocalAttempts", maxLocalAttempts).kv("agent", agent).kv("treatment", treatment).kv("executor", executor).kvRaw("process", process.toJson()).kv("repairTurns", repairTurns).kv("sizeBudget", sizeBudget).kv("maxTokens", maxTokens).kvBool("ask", ask).kv("fallbackEngine", fallbackEngine).key("inject").beginArray();
    for (const auto& i : inject) w.str(i);
    w.endArray().key("engines").beginArray();
    for (const auto& e : engines) w.str(e);
    w.endArray().key("route").beginObject();
    for (const auto& [persona, name] : route) w.kv(persona, name);
    w.endObject();
    if (!mends.empty()) w.kvRaw("mends", mends);
    w.endObject();
    return w.take();
}

std::expected<WorkflowSpec, std::string> WorkflowSpec::fromJson(std::string_view text)
{
    WorkflowSpec s;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "task") s.task = json::toString(v);
                else if (k == "taskText") s.taskText = json::toString(v);
                else if (k == "triage") s.triage = json::toString(v);
                else if (k == "sandbox") s.sandboxDir = json::toString(v);
                else if (k == "maxLocalAttempts") s.maxLocalAttempts = static_cast<int>(v.get_int64());
                else if (k == "inject") { for (auto x : v.get_array()) s.inject.push_back(json::toString(x.value())); }
                else if (k == "agent") s.agent = json::toString(v);
                else if (k == "treatment") s.treatment = json::toString(v);
                else if (k == "executor") s.executor = json::toString(v);
                else if (k == "process") {
                    std::string_view r = v.raw_json();
                    auto p = Process::fromJson(std::string_view{r});
                    if (!p) throw std::runtime_error(p.error());
                    s.process = std::move(*p);
                }
                else if (k == "repairTurns") s.repairTurns = static_cast<int>(v.get_int64());
                else if (k == "mends") { std::string_view r = v.raw_json(); s.mends.assign(r); if (auto t = parseMends(s.mends); !t) throw std::runtime_error(t.error()); }
                else if (k == "sizeBudget") s.sizeBudget = static_cast<int>(v.get_int64());
                else if (k == "maxTokens") s.maxTokens = static_cast<int>(v.get_int64());
                else if (k == "ask") s.ask = v.get_bool();
                else if (k == "fallbackEngine") s.fallbackEngine = json::toString(v);
                else if (k == "engines") { for (auto x : v.get_array()) s.engines.push_back(json::toString(x.value())); }
                else if (k == "route") { for (auto r : v.get_object()) { std::string persona{std::string_view{r.unescaped_key()}}; s.route[persona] = json::toString(r.value()); } }
            }
        });
    } catch (const simdjson::simdjson_error& e) { return std::unexpected(std::string{"bad workflow spec: "} + e.what()); }
    catch (const std::runtime_error& e) { return std::unexpected(std::string{"bad workflow spec: "} + e.what()); }   // the process it carries is invalid
    if (s.task.empty()) return std::unexpected("workflow needs a task");
    if (s.taskText.size() > 4000) return std::unexpected("task text is too long");
    if (!s.sandboxDir.empty() && s.sandboxDir[0] != '/') return std::unexpected("workflow sandbox must be an absolute directory");  // empty: the daemon chooses
    if (s.maxLocalAttempts < 1 || s.maxLocalAttempts > 5) return std::unexpected("maxLocalAttempts must be 1..5");
    if (s.agent != "mock" && s.agent != "llm") return std::unexpected("agent must be mock or llm");
    std::set<std::string> names;
    for (const auto& e : s.engines) { auto p = EngineProfile::parse(e); if (!p) return std::unexpected("engine '" + e + "': " + p.error()); names.insert(p->name); }
    if (s.agent == "llm" && s.engines.empty()) return std::unexpected("agent llm needs at least one --engine");
    if (!s.fallbackEngine.empty() && !names.count(s.fallbackEngine)) return std::unexpected("fallback engine '" + s.fallbackEngine + "' names an unknown engine");
    for (const auto& [persona, name] : s.route) if (!names.count(name)) return std::unexpected("route " + persona + "=" + name + " names an unknown engine");
    if (s.treatment != "baseline" && s.treatment != "index" && s.treatment != "planfirst") return std::unexpected("treatment must be baseline, index or planfirst");
    if (!s.executor.empty() && s.executor != "session" && s.executor != "canvas" && s.executor != "fork") return std::unexpected("executor must be session, canvas or fork");
    if (s.maxTokens < 256 || s.maxTokens > 32768) return std::unexpected("maxTokens must be 256..32768");
    return s;
}

std::optional<EngineProfile> WorkflowSpec::engineFor(const std::string& persona) const
{
    if (engines.empty()) return std::nullopt;
    std::string want;
    if (auto it = route.find(persona); it != route.end()) want = it->second;
    for (const auto& e : engines) { auto p = EngineProfile::parse(e); if (p && (want.empty() || p->name == want)) return std::optional<EngineProfile>{*p}; }
    return std::nullopt;
}

namespace wfbody {
std::string submitted(const std::string& id, const WorkflowSpec& spec) { json::Writer w; w.beginObject().kv("id", id).kvRaw("spec", spec.toJson()).endObject(); return w.take(); }
std::string planned(const std::string& id, const Plan& plan) { json::Writer w; w.beginObject().kv("id", id).kvRaw("plan", plan.toJson()).endObject(); return w.take(); }
std::string decision(const std::string& id, const std::string& item, int attempt, const std::string& action, const std::string& tier, const std::string& decisionJson)
{
    json::Writer w;
    w.beginObject().kv("id", id).kv("item", item).kv("attempt", attempt).kv("action", action).kv("tier", tier).kvRaw("decision", decisionJson).endObject();
    return w.take();
}
std::string awaitingPlan(const std::string& id, const std::string& packetPath) { json::Writer w; w.beginObject().kv("id", id).kv("note", packetPath).endObject(); return w.take(); }
std::string planAmended(const std::string& id, const std::string& replaced, const Plan& plan) { json::Writer w; w.beginObject().kv("id", id).kv("item", replaced).kvRaw("plan", plan.toJson()).endObject(); return w.take(); }
std::string amendRejected(const std::string& id, const std::string& item, int attempt, const std::string& reason) { json::Writer w; w.beginObject().kv("id", id).kv("item", item).kv("attempt", attempt).kv("note", reason).endObject(); return w.take(); }
std::string requestRaised(const std::string& id, const RequestRecord& r)
{
    json::Writer w;
    w.beginObject().kv("id", id).kv("rid", r.id).kv("kind", r.kind).kv("from", r.from).kv("title", r.title).kv("detail", r.detail).kv("attempt", r.attempt).kvBool("blocking", r.blocking);
    w.key("scope").beginArray();
    for (const auto& s : r.scope) w.str(s);
    w.endArray().key("options").beginArray();
    for (const auto& o : r.options) w.str(o);
    w.endArray().endObject();
    return w.take();
}
std::string requestAnalysed(const std::string& id, const std::string& rid, const std::string& triage, const std::string& analysis)
{
    json::Writer w;
    w.beginObject().kv("id", id).kv("rid", rid).kv("triage", triage).kv("analysis", analysis).endObject();
    return w.take();
}
std::string requestAnswered(const std::string& id, const std::string& rid, const std::string& answer, const std::string& resolution, const std::string& by)
{
    json::Writer w;
    w.beginObject().kv("id", id).kv("rid", rid).kv("answer", answer).kv("resolution", resolution).kv("by", by).endObject();
    return w.take();
}
std::string cancelRequested(const std::string& id) { json::Writer w; w.beginObject().kv("id", id).endObject(); return w.take(); }
std::string finished(const std::string& id, WorkflowState state, const std::string& note) { json::Writer w; w.beginObject().kv("id", id).kv("state", toString(state)).kv("note", note).endObject(); return w.take(); }
}  // namespace wfbody

const WorkflowRecord* WorkflowStore::find(std::string_view id) const
{
    for (const auto& w : workflows_) if (w.id == id) return &w;
    return nullptr;
}

std::string WorkflowStore::nextId() const
{
    unsigned long long maxN = 0;
    for (const auto& w : workflows_) { unsigned long long n = 0; if (std::sscanf(w.id.c_str(), "w-%llu", &n) == 1 && n > maxN) maxN = n; }
    char buf[32];
    std::snprintf(buf, sizeof buf, "w-%06llu", maxN + 1);
    return buf;
}

void WorkflowStore::apply(const JournalRecord& rec)
{
    if (rec.type.rfind("workflow", 0) != 0) return;
    std::string id, state, note, item, action, tier, specJson, planJson, decisionJson;
    RequestRecord rq;
    std::string answer, resolution, by, triage, analysis;
    std::int64_t attempt = 0;
    try {
        json::withObject(rec.body, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "id") id = json::toString(v);
                else if (k == "state") state = json::toString(v);
                else if (k == "note") note = json::toString(v);
                else if (k == "item") item = json::toString(v);
                else if (k == "action") action = json::toString(v);
                else if (k == "tier") tier = json::toString(v);
                else if (k == "attempt") attempt = v.get_int64();
                else if (k == "spec") { std::string_view r = v.raw_json(); specJson.assign(r); }
                else if (k == "plan") { std::string_view r = v.raw_json(); planJson.assign(r); }
                else if (k == "decision") { std::string_view r = v.raw_json(); decisionJson.assign(r); }
                else if (k == "rid") rq.id = json::toString(v);
                else if (k == "kind") rq.kind = json::toString(v);
                else if (k == "from") rq.from = json::toString(v);
                else if (k == "title") rq.title = json::toString(v);
                else if (k == "detail") rq.detail = json::toString(v);
                else if (k == "blocking") rq.blocking = v.get_bool();
                else if (k == "scope") { for (auto x : v.get_array()) rq.scope.push_back(json::toString(x.value())); }
                else if (k == "options") { for (auto x : v.get_array()) rq.options.push_back(json::toString(x.value())); }
                else if (k == "answer") answer = json::toString(v);
                else if (k == "resolution") resolution = json::toString(v);
                else if (k == "by") by = json::toString(v);
                else if (k == "triage") triage = json::toString(v);
                else if (k == "analysis") analysis = json::toString(v);
            }
        });
    } catch (const simdjson::simdjson_error&) { return; }
    if (rec.type == "workflowSubmitted") {
        auto spec = WorkflowSpec::fromJson(specJson);
        if (!spec) return;
        WorkflowRecord w;
        w.id = id; w.spec = std::move(*spec); w.submittedAt = rec.ts;
        workflows_.push_back(std::move(w));
        return;
    }
    WorkflowRecord* w = nullptr;
    for (auto& x : workflows_) if (x.id == id) w = &x;
    if (!w) return;
    if (rec.type == "workflowPlanned") { if (auto p = Plan::fromJson(planJson)) { w->plan = std::move(*p); if (w->state == WorkflowState::awaitingPlan) w->state = WorkflowState::running; } }
    else if (rec.type == "workflowPlanAmended") { if (auto p = Plan::fromJson(planJson)) { w->plan = std::move(*p); w->amendedItems.push_back(item); } }
    else if (rec.type == "workflowAmendRejected") w->rejectedAmends.insert(item + "#" + std::to_string(attempt));
    else if (rec.type == "workflowAwaitingPlan") { w->state = WorkflowState::awaitingPlan; w->note = "needs a plan: see " + note; }
    else if (rec.type == "workflowDecision") w->decisions.push_back({item, static_cast<int>(attempt), action, tier, decisionJson});
    else if (rec.type == "workflowRequestRaised") { rq.attempt = static_cast<int>(attempt); rq.raisedAt = rec.ts; w->requests.push_back(std::move(rq)); }
    else if (rec.type == "workflowRequestAnswered") {
        for (auto& r : w->requests)
            if (r.id == rq.id) { r.status = "answered"; r.answer = answer; r.resolution = resolution; r.answeredBy = by; r.answeredAt = rec.ts; }
    }
    else if (rec.type == "workflowRequestAnalysed") { for (auto& r : w->requests) if (r.id == rq.id) { r.triage = triage; r.analysis = analysis; } }
    else if (rec.type == "workflowCancelRequested") w->cancelRequested = true;
    else if (rec.type == "workflowFinished") {
        w->state = state == "succeeded" ? WorkflowState::succeeded : state == "cancelled" ? WorkflowState::cancelled : WorkflowState::failed;
        w->note = note; w->finishedAt = rec.ts;
    }
}

const char* kindName(WorkflowAction::Kind k)
{
    switch (k) {
        case WorkflowAction::Kind::startJob: return "startJob";
        case WorkflowAction::Kind::loadPlan: return "loadPlan";
        case WorkflowAction::Kind::recordDecision: return "recordDecision";
        case WorkflowAction::Kind::cancelJob: return "cancelJob";
        case WorkflowAction::Kind::finish: return "finish";
        case WorkflowAction::Kind::amendPlan: return "amendPlan";
        case WorkflowAction::Kind::writeDispute: return "writeDispute";
        case WorkflowAction::Kind::sendDown: return "sendDown";
        case WorkflowAction::Kind::raiseRequest: return "raiseRequest";
        case WorkflowAction::Kind::setStrategy: return "setStrategy";
    }
    return "?";
}

std::string WorkflowAction::toJson() const
{
    json::Writer w;
    w.beginObject().kv("kind", kindName(kind));
    switch (kind) {
        case Kind::startJob: w.kvRaw("job", job.toJson()); break;
        case Kind::loadPlan: break;
        case Kind::recordDecision:
            w.kv("item", item).kv("attempt", attempt).kv("action", decisionAction).kv("tier", decisionTier);
            if (!decisionJson.empty()) w.kvRaw("decision", decisionJson);
            break;
        case Kind::cancelJob: w.kv("jobId", jobId); break;
        case Kind::finish: w.kv("state", toString(finalState)).kv("note", note); break;
        case Kind::amendPlan: w.kv("item", item).kv("attempt", attempt); break;
        case Kind::writeDispute: w.kv("item", item).kv("from", fromItem).kv("attempt", attempt); break;
        case Kind::sendDown: w.kv("item", item).kv("attempt", attempt); break;
        case Kind::raiseRequest:
            w.kv("requestKind", request.kind).kv("from", request.from).kv("title", request.title).kv("detail", request.detail).kv("attempt", request.attempt).kvBool("blocking", request.blocking).key("scope").beginArray();
            for (const auto& s : request.scope) w.str(s);
            w.endArray().key("options").beginArray();
            for (const auto& o : request.options) w.str(o);
            w.endArray();
            break;
        case Kind::setStrategy: w.kv("item", item).kv("strategy", note); break;
    }
    w.endObject();
    return w.take();
}

// ---- engine ---------------------------------------------------------------------------------------------------------

namespace {

struct Mine { std::vector<const JobRecord*> all; };

Mine jobsOf(const WorkflowRecord& wf, const JobStore& store)
{
    Mine m;
    for (const auto& j : store.jobs()) if (j.spec.workflow == wf.id) m.all.push_back(&j);
    return m;
}

JobSpec baseJob(const WorkflowRecord& wf, const WorkflowSettings& st, const std::string& role, const std::string& tool, const std::string& tier, std::int64_t wall)
{
    JobSpec s;
    s.cwd = wf.spec.sandboxDir;
    s.tool = tool;
    s.tier = tier;
    s.owner = "workflow:" + wf.id;
    s.workflow = wf.id;
    s.role = role;
    s.wallSeconds = wall;
    s.graceSeconds = 2;
    s.argv = {st.exe};
    return s;
}

bool terminalJob(const JobRecord* j) { return isTerminal(j->state); }

struct ItemView
{
    const WorkItem* item = nullptr;
    std::vector<const JobRecord*> attempts, gates;  ///< attempt-like jobs (agent, decompose, rearchitect) and gate jobs
    bool accepted = false, active = false;
    bool restorable = false;  ///< an earlier attempt passed before a dispute voided it: its accepted output is still on disk
    int maxAttempt = 0;
    int localAttempts = 0, frontierAttempts = 0;
    bool decomposeTried = false, rearchitectTried = false;
    std::string latestTier;
    const JobRecord* latest = nullptr;              ///< the attempt-like job with the highest attempt number
};

ItemView viewOf(const WorkItem& it, const Mine& mine, const WorkflowRecord& wf)
{
    ItemView v;
    v.item = &it;
    int disputedAt = 0;
    for (const auto& d : wf.decisions) if (d.item == it.id && d.action == "disputed") disputedAt = std::max(disputedAt, d.attempt);
    for (const JobRecord* j : mine.all) {
        if (j->spec.item != it.id) continue;
        if (!terminalJob(j)) v.active = true;
        const std::string& role = j->spec.role;
        if (role == "agent" || role == "decompose" || role == "rearchitect") {
            v.attempts.push_back(j);
            v.maxAttempt = std::max<int>(v.maxAttempt, static_cast<int>(j->spec.attempt));
            if (role == "agent") { if (j->spec.tier == "frontier-stub") ++v.frontierAttempts; else ++v.localAttempts; }
            if (role == "decompose") v.decomposeTried = true;
            if (role == "rearchitect") v.rearchitectTried = true;
            if (!v.latest || j->spec.attempt >= v.latest->spec.attempt) { v.latest = j; v.latestTier = j->spec.tier; }
        } else if (role == "gate") {
            v.gates.push_back(j);
            if (j->state == JobState::succeeded && j->spec.attempt > disputedAt) v.accepted = true;  // a dispute voids earlier acceptances
            else if (j->state == JobState::succeeded) v.restorable = true;
        }
    }
    // A disputed item whose re-run could not pass keeps its earlier accepted version: the dispute ended with the decision "restored".
    if (!v.accepted && v.restorable)
        for (const auto& d : wf.decisions) if (d.item == it.id && d.action == "restored" && d.attempt > disputedAt) v.accepted = true;
    return v;
}

// An open blocking request holds the nodes in its scope (every node when the scope is empty): nothing new starts for them.
bool blockedByRequest(const WorkflowRecord& wf, const std::string& item)
{
    for (const auto& r : wf.requests)
        if (r.status == "open" && r.blocking && (r.scope.empty() || std::find(r.scope.begin(), r.scope.end(), item) != r.scope.end())) return true;
    return false;
}
bool anyOpenBlocking(const WorkflowRecord& wf)
{
    for (const auto& r : wf.requests) if (r.status == "open" && r.blocking) return true;
    return false;
}
const RequestRecord* escalationFor(const WorkflowRecord& wf, const std::string& item, int attempt)
{
    for (const auto& r : wf.requests) if (r.kind == "escalation" && r.attempt == attempt && r.scope.size() == 1 && r.scope[0] == item) return &r;
    return nullptr;
}

// The manager's strategies, tried in order when an item's ladder is spent, before anything reaches the owner: think (reasoning on, a bigger
// budget), then a stronger local engine when one is configured. The owner decides intent and permissions, never how to solve a task.
std::vector<std::string> strategyOrder(const WorkflowRecord& wf)
{
    std::vector<std::string> o{"think"};
    if (!wf.spec.fallbackEngine.empty()) o.push_back("engine");
    return o;
}
int strategiesUsed(const WorkflowRecord& wf, const std::string& item)
{
    int n = 0;
    for (const auto& d : wf.decisions) if (d.item == item && d.action == "strategy") ++n;
    return n;
}
bool hasStrategy(const WorkflowRecord& wf, const std::string& item, const char* name)
{
    for (const auto& d : wf.decisions) if (d.item == item && d.action == "strategy" && d.tier == name) return true;
    return false;
}

const DecisionRecord* decisionFor(const WorkflowRecord& wf, const std::string& item, int attempt)
{
    for (const auto& d : wf.decisions) if (d.item == item && d.attempt == attempt) return &d;
    return nullptr;
}

/// How an item of this persona is executed: the spec's override if it has one (a tester's `fork` is the canvas), else the process's choice.
std::string executorFor(const WorkflowSpec& spec, const LeafParams& leaf, const std::string& persona)
{
    const bool tester = persona == "tester";
    if (!spec.executor.empty()) return tester && spec.executor == "fork" ? std::string{"canvas"} : spec.executor;
    return tester ? leaf.testerExecutor : leaf.developerExecutor;
}

/// The parameters of the stage that runs the plan's items (the standard values when the process has no such stage: nothing then runs an item).
const Stage& itemsStageOf(const WorkflowRecord& wf)
{
    static const Stage standard = [] { Stage s; s.kind = StageKind::items; return s; }();
    const Stage* st = wf.spec.process.itemsStage();
    return st ? *st : standard;
}

/// The mend a failed gate reported in its result (`{"code":N, ..., "result":{"mend":{...}}}`: the gate looked its first error up in the mends table).
std::optional<MendMatch> mendIn(const std::string& text, const char* outer)
{
    std::string raw;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                if (json::keyOf(f) != outer) continue;
                if (std::string_view{outer} == "mend") { std::string_view r = f.value().raw_json(); raw.assign(r); continue; }
                for (auto g : f.value().get_object()) if (json::keyOf(g) == "mend") { std::string_view r = g.value().raw_json(); raw.assign(r); }
            }
        });
    } catch (const simdjson::simdjson_error&) { return std::nullopt; }
    return raw.empty() ? std::nullopt : MendMatch::fromJson(raw);
}

/// How many times a mend was applied to an item (`name` empty: any mend). Mends are decisions, so this is read from the journal.
int mendsApplied(const WorkflowRecord& wf, const std::string& item, const std::string& name = {})
{
    int n = 0;
    for (const auto& d : wf.decisions) if (d.item == item && d.action == "mend" && (name.empty() || d.tier == name)) ++n;
    return n;
}

JobSpec agentJob(const WorkflowRecord& wf, const WorkflowSettings& st, const WorkItem& it, int attempt, const std::string& tier)
{
    auto engine = wf.spec.engineFor(it.persona);
    if (!wf.spec.fallbackEngine.empty() && hasStrategy(wf, it.id, "engine"))   // the manager moved this item to the stronger engine
        for (const auto& e : wf.spec.engines) if (auto p = EngineProfile::parse(e); p && p->name == wf.spec.fallbackEngine) engine = *p;
    const bool llm = wf.spec.agent == "llm" && engine.has_value();
    const LeafParams& leaf = itemsStageOf(wf).leaf;
    JobSpec s = baseJob(wf, st, "agent", llm ? "llm-agent" : "mock-agent", tier, llm && tier == "local" ? leaf.agentWallLlmSeconds : leaf.agentWallOtherSeconds);
    s.item = it.id;
    s.attempt = attempt;
    // A mend (mends.hpp): the attempt IS the mend's action on what the attempt before it left. `silence-unused` is an edit by the harness with no model.
    const DecisionRecord* before = attempt > 1 ? decisionFor(wf, it.id, attempt - 1) : nullptr;
    const std::optional<MendMatch> mend = before && before->action == "mend" ? mendIn(before->json, "mend") : std::nullopt;
    if (mend && mend->action == "silence-unused") {
        s.tool = "mend-apply";
        s.argv = {st.exe, "mend-apply", "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--attempt", std::to_string(attempt), "--action", mend->action, "--file", mend->file,
                  "--line", std::to_string(mend->line), "--column", std::to_string(mend->column), "--symbol", mend->symbol};
        s.wallSeconds = leaf.agentWallOtherSeconds;
        return s;
    }
    if (llm) { s.leaseResource = engine->endpoint; s.leaseCap = std::max(1, engine->maxConcurrent); }
    if (llm) {
        s.argv = {st.exe, "llm-agent", "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--tier", tier, "--attempt", std::to_string(attempt), "--engine", engine->toArg(),
                  "--max-tokens", std::to_string(wf.spec.maxTokens), "--timeout", "1700", "--treatment", wf.spec.treatment, "--repair-turns", std::to_string(wf.spec.repairTurns), "--size-budget", std::to_string(wf.spec.sizeBudget)};
        const bool sections = before && before->action == "sections";   // the ladder chose forked sections for this attempt
        const std::string executor = executorFor(wf.spec, leaf, it.persona);
        if ((executor == "fork" || sections || mend) && tier == "local" && it.persona != "tester") {   // a developer's item as forked sections; the gate job judges the merged files
            s.argv = {st.exe, "llm-fork", "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--attempt", std::to_string(attempt), "--engine", engine->toArg(),
                      "--max-tokens", std::to_string(wf.spec.maxTokens), "--timeout", "1700", "--repair-turns", std::to_string(wf.spec.repairTurns), "--agent-exit", "1"};
            if (!leaf.contractSections) s.argv.insert(s.argv.end(), {"--contract-sections", "0"});
            if (mend) s.argv.insert(s.argv.end(), {"--mend-section", mend->symbol, "--mend-file", mend->file});   // add-section: only the missing function is written
        }
        else if (executor != "session") s.argv.insert(s.argv.end(), {"--executor", "canvas"});
    } else {
        s.argv = {st.exe, "mock-agent", "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--tier", tier, "--attempt", std::to_string(attempt)};
        for (const auto& i : wf.spec.inject) { s.argv.push_back("--inject"); s.argv.push_back(i); }
    }
    return s;
}

JobSpec gateJob(const WorkflowRecord& wf, const WorkflowSettings& st, const WorkItem& it, int attempt, const std::string& tier)
{
    const Harness* harness = wf.plan ? findHarness(wf.plan->harness) : nullptr;
    const char* gateCommand = harness ? harness->gateCommand : "";   // a plan always names a registered harness (Plan::fromJson refuses one that does not)
    JobSpec s = baseJob(wf, st, "gate", gateCommand, tier, itemsStageOf(wf).leaf.gateWallSeconds);
    s.item = it.id;
    s.attempt = attempt;
    s.argv = {st.exe, gateCommand, "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--attempt", std::to_string(attempt)};
    return s;
}

JobSpec architectJob(const WorkflowRecord& wf, const WorkflowSettings& st, const WorkItem& it, int attempt, const char* mode)
{
    const auto engine = wf.spec.engineFor("architect");
    const bool llm = wf.spec.agent == "llm" && engine.has_value();
    JobSpec s = baseJob(wf, st, mode, llm ? "llm-architect" : "mock-architect", "local", llm ? itemsStageOf(wf).leaf.architectWallLlmSeconds : itemsStageOf(wf).leaf.architectWallOtherSeconds);  // three turns of up to 1700 s each: the first real timeout killed a correction mid-turn
    s.item = it.id;
    s.attempt = attempt;
    s.argv = {st.exe, llm ? "llm-architect" : "mock-architect", "--mode", mode, "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--attempt", std::to_string(attempt)};
    if (llm) {
        s.leaseResource = engine->endpoint; s.leaseCap = std::max(1, engine->maxConcurrent);
        s.argv.push_back("--engine"); s.argv.push_back(engine->toArg()); s.argv.push_back("--timeout"); s.argv.push_back("1700");
    }
    return s;
}

WorkflowAction start(JobSpec spec) { WorkflowAction a; a.kind = WorkflowAction::Kind::startJob; a.job = std::move(spec); return a; }
WorkflowAction finish(WorkflowState s, std::string note) { WorkflowAction a; a.kind = WorkflowAction::Kind::finish; a.finalState = s; a.note = std::move(note); return a; }

}  // namespace

std::string firstCause(const WorkflowRecord& wf)
{
    for (const auto& d : wf.decisions) if (d.action == "fail" && (!wf.plan || wf.plan->find(d.item))) return d.item;
    return {};
}

std::vector<std::string> laterFailures(const WorkflowRecord& wf)
{
    const std::string first = firstCause(wf);
    std::vector<std::string> later;
    for (const auto& d : wf.decisions)
        if (d.action == "fail" && d.item != first && (!wf.plan || wf.plan->find(d.item)) && std::find(later.begin(), later.end(), d.item) == later.end()) later.push_back(d.item);
    return later;
}

namespace {

/// An item's state as the status output and the predicates read it.
const char* stateOf(const ItemView& v, const WorkflowRecord& wf)
{
    int attempts = v.maxAttempt;
    if (v.item->persona == "integrator") for (const JobRecord* x : v.gates) attempts = std::max<int>(attempts, static_cast<int>(x->spec.attempt));
    const DecisionRecord* d = attempts ? decisionFor(wf, v.item->id, attempts) : nullptr;
    return v.accepted ? "accepted" : v.active ? "running" : (d && d->action == "fail") ? "failed" : "pending";
}

/// One stage as derived this pass. Nothing here is stored: it is a function of the workflow record and its jobs (docs/PROCESS_DESIGN.md 2.2), so a dispute
/// that voids an accepted item re-opens the stage it belongs to and closes the ones that waited for it, with no bookkeeping to repair.
struct StageView
{
    const Stage* stage = nullptr;
    bool entered = false, done = false, failed = false;
    std::string waitingOn;   ///< the first atom that keeps it from being entered, or from being done
};

struct Derived
{
    Snapshot snapshot;
    std::vector<StageView> stages;
};

Derived derive(const WorkflowRecord& wf, const Mine& mine, const std::vector<ItemView>& views)
{
    Derived d;
    Snapshot& snap = d.snapshot;
    snap.planPresent = wf.plan.has_value();
    snap.openBlocking = anyOpenBlocking(wf);
    for (const ItemView& v : views) {
        const std::string_view state = stateOf(v, wf);
        ++(state == "accepted" ? snap.accepted : state == "running" ? snap.running : state == "failed" ? snap.failed : snap.pending);
    }
    Vocabulary vocabulary;
    for (const Stage& stage : wf.spec.process.stages) if (stage.kind == StageKind::job) vocabulary.steps.push_back(stage.job.step);
    for (const JobRecord* j : mine.all) {
        if (!j->spec.item.empty() || std::find(vocabulary.steps.begin(), vocabulary.steps.end(), j->spec.role) == vocabulary.steps.end()) continue;
        Snapshot::JobFacts& f = snap.jobs[j->spec.role];   // jobs are in submission order: the last one seen is the latest
        ++f.tries;
        f.active = !terminalJob(j);
        f.succeeded = j->state == JobState::succeeded;
        f.failed = terminalJob(j) && !f.succeeded;
    }
    for (const Stage& stage : wf.spec.process.stages) {
        StageView sv;
        sv.stage = &stage;
        const auto entry = Predicate::parse(stage.entry, vocabulary), exit = Predicate::parse(stage.exit, vocabulary);   // checked when the process was loaded
        sv.entered = entry && entry->eval(snap);
        sv.done = sv.entered && exit && exit->eval(snap);
        if (!sv.entered) sv.waitingOn = entry ? entry->firstFalse(snap) : entry.error();
        else if (!sv.done) sv.waitingOn = exit ? exit->firstFalse(snap) : exit.error();
        if (stage.kind == StageKind::job) { const auto it = snap.jobs.find(stage.job.step); sv.failed = !sv.done && it != snap.jobs.end() && it->second.failed && it->second.tries >= 1 + stage.job.retries; }
        else sv.failed = sv.entered && snap.failed > 0;
        snap.stages[stage.name] = {sv.done, sv.entered && !sv.done, sv.failed};
        vocabulary.stages.push_back(stage.name);
        d.stages.push_back(std::move(sv));
    }
    return d;
}

std::string substituted(std::string arg, const WorkflowRecord& wf)
{
    const std::pair<const char*, const std::string*> names[] = {{"${task}", &wf.spec.task}, {"${sandbox}", &wf.spec.sandboxDir}, {"${workflow}", &wf.id}};
    for (const auto& [name, value] : names)
        for (std::size_t at = arg.find(name); at != std::string::npos; at = arg.find(name, at + value->size())) arg.replace(at, std::string_view{name}.size(), *value);
    return arg;
}

/// "twice", as the engine has always said it.
std::string times(int n) { return n == 2 ? "twice" : std::to_string(n) + " times"; }

/// A stage of kind `job` that is entered and not done: start its job, wait for it, accept what it wrote, run it again, or fail the workflow.
/// Returns true when the workflow was finished (nothing else may be decided this pass).
bool advanceJobStage(const WorkflowRecord& wf, const Mine& mine, const WorkflowSettings& st, const Stage& stage, const Snapshot& snap, std::vector<WorkflowAction>& out)
{
    const JobParams& p = stage.job;
    std::string command = p.tool;
    if (p.tool == "harness:integrate") {
        const Harness* harness = wf.plan ? findHarness(wf.plan->harness) : nullptr;
        if (!harness) { out.push_back(finish(WorkflowState::failed, "stage '" + stage.name + "' cannot run: its tool is the plan's harness, and there is no plan")); return true; }
        command = harness->integrateCommand;
    }
    const auto it = snap.jobs.find(p.step);
    const Snapshot::JobFacts facts = it == snap.jobs.end() ? Snapshot::JobFacts{} : it->second;
    const auto startIt = [&] {
        JobSpec s = baseJob(wf, st, p.step, command, p.tier, p.wallSeconds);
        s.argv = {st.exe, command};
        for (const auto& a : p.args) s.argv.push_back(substituted(a, wf));
        out.push_back(start(std::move(s)));
    };
    if (facts.tries == 0) startIt();
    else if (facts.active) {}   // wait
    else if (facts.succeeded) {
        // The job is done and the stage is not: either its output is still to be accepted, or the stage's exit asks for more than the job (a stall the
        // status shows, with the atom that is false).
        if (p.accept == "plan" && !wf.plan) { WorkflowAction a; a.kind = WorkflowAction::Kind::loadPlan; out.push_back(std::move(a)); }
    }
    else if (facts.tries < 1 + p.retries) startIt();
    else {
        std::string note;
        for (const JobRecord* j : mine.all) if (j->spec.item.empty() && j->spec.role == p.step) note = j->note;   // the latest one's
        out.push_back(finish(WorkflowState::failed, facts.tries == 1 ? p.label + " failed: " + note : p.label + " failed " + times(facts.tries)));
        return true;
    }
    return false;
}

/// What a finished workflow achieved, from its stages: the standard process reads "all items accepted; integration passed".
std::string successNote(const Process& process)
{
    std::string note;
    for (const Stage& s : process.stages) {
        if (s.kind == StageKind::job && s.job.accept == "plan") continue;   // that there was a plan goes without saying
        note += (note.empty() ? "" : "; ") + (s.kind == StageKind::items ? std::string{"all items accepted"} : s.job.label + " passed");
    }
    return note.empty() ? "every stage is done" : note;
}

/// The stage of kind `items`: every item of the plan under its executor (a coordinator ascends, anything else is a leaf), in plan order.
/// Returns true when `advance` must return what is in `out` now (an item failed: see the end of the function).
bool advanceItems(const WorkflowRecord& wf, const std::vector<ItemView>& views, const WorkflowSettings& st, const IDecisionPolicy& policy, bool anyActive, std::vector<WorkflowAction>& out)
{
    const Plan& plan = *wf.plan;
    std::string failedItem;
    auto isAccepted = [&](const std::string& id) { for (const auto& v : views) if (v.item->id == id) return v.accepted; return false; };
    for (const ItemView& v : views) {
        if (v.accepted) continue;
        if (v.active) continue;
        if (!std::all_of(v.item->deps.begin(), v.item->deps.end(), isAccepted)) continue;
        if (blockedByRequest(wf, v.item->id)) continue;  // waiting for the owner or the architect: nothing new starts for this node
        if (v.item->persona == "integrator") {
            // The V's ascent. A coordinator has no agent step: when its parts are accepted its own tests run over them (a gate per round). A
            // failing round is sent down to the part the failure points at (the daemon picks it and writes the feedback), the part is redone,
            // and the coordinator integrates again, up to kMaxRoundTrips times; then the item fails with the evidence.
            const int kMaxRoundTrips = itemsStageOf(wf).ascent.maxRoundTrips;
            int round = 0;
            const JobRecord* g = nullptr;
            for (const JobRecord* x : v.gates) if (x->spec.attempt >= round) { round = static_cast<int>(x->spec.attempt); g = x; }
            if (!g) { out.push_back(start(gateJob(wf, st, *v.item, 1, "local"))); continue; }
            const DecisionRecord* cd = decisionFor(wf, v.item->id, round);
            if (!cd) {
                int sends = 0;
                for (const auto& dd : wf.decisions) if (dd.item == v.item->id && dd.action == "sendDown") ++sends;
                const bool giveUp = sends >= kMaxRoundTrips;
                WorkflowAction rec;
                rec.kind = WorkflowAction::Kind::recordDecision;
                rec.item = v.item->id; rec.attempt = round; rec.decisionTier = "local";
                rec.decisionAction = giveUp ? "fail" : "sendDown";
                rec.decisionJson = std::string{"{\"action\":\""} + rec.decisionAction + "\",\"reasons\":[\"integration of '" + v.item->id + "' failed in round " + std::to_string(round) + "\",\"" +
                                   (giveUp ? "the round-trip budget (" + std::to_string(kMaxRoundTrips) + ") is spent: the item fails with the evidence" : "send the failure down to the part it points at, redo that part, integrate again") +
                                   "\"],\"inputs\":{\"item\":\"" + v.item->id + "\",\"attempt\":" + std::to_string(round) + ",\"sends\":" + std::to_string(sends) + "}}";
                out.push_back(std::move(rec));
                if (giveUp) failedItem = v.item->id;
                else { WorkflowAction sd; sd.kind = WorkflowAction::Kind::sendDown; sd.item = v.item->id; sd.attempt = round; out.push_back(std::move(sd)); }
            } else if (cd->action == "sendDown") {
                out.push_back(start(gateJob(wf, st, *v.item, round + 1, "local")));  // the redone part is accepted again: integrate once more
            } else failedItem = v.item->id;
            continue;
        }
        if (v.maxAttempt == 0) { out.push_back(start(agentJob(wf, st, *v.item, 1, "local"))); continue; }
        const int n = v.maxAttempt;
        const JobRecord* latest = v.latest;
        const DecisionRecord* d = decisionFor(wf, v.item->id, n);
        const bool disputed = d && d->action == "disputed";
        bool failed = false;
        const std::string& role = latest->spec.role;
        if (role == "agent" && !disputed) {
            const JobRecord* gate = nullptr;
            for (const JobRecord* g : v.gates) if (g->spec.attempt == n) gate = g;
            if (latest->state == JobState::succeeded && !gate) { out.push_back(start(gateJob(wf, st, *v.item, n, v.latestTier))); continue; }
            failed = true;
        } else if (role == "decompose" && !disputed) {
            if (latest->state == JobState::succeeded && !wf.rejectedAmends.count(v.item->id + "#" + std::to_string(n))) {
                WorkflowAction a; a.kind = WorkflowAction::Kind::amendPlan; a.item = v.item->id; a.attempt = n; out.push_back(std::move(a));
                continue;
            }
            failed = true;
        } else if (role == "rearchitect" && !disputed) {
            if (latest->state == JobState::succeeded && !d) { out.push_back(start(agentJob(wf, st, *v.item, n + 1, "local"))); continue; }
            failed = true;
        }
        // A failed gate that reported a mend: the harness repairs the cause itself. The decision `mend` is journaled; the next attempt is the mend's action and
        // spends no rung of the ladder. The same signature is mended at most `maxPerItem` times for an item; after that the ladder below decides.
        if (!d && failed && !disputed && role == "agent" && v.item->persona == "developer") {
            const JobRecord* gate = nullptr;
            for (const JobRecord* g : v.gates) if (g->spec.attempt == n) gate = g;
            const LeafParams& leaf = itemsStageOf(wf).leaf;
            const auto mend = gate && gate->state != JobState::succeeded && leaf.mendsEnabled ? mendIn(gate->exitJson, "result") : std::nullopt;
            const bool runnable = mend && (mend->action == "silence-unused" || (wf.spec.agent == "llm" && !wf.spec.engines.empty()));
            if (runnable && std::find(leaf.mendsDisabled.begin(), leaf.mendsDisabled.end(), mend->name) == leaf.mendsDisabled.end() && mendsApplied(wf, v.item->id, mend->name) < mend->maxPerItem) {
                const int applied = mendsApplied(wf, v.item->id, mend->name) + 1;
                json::Writer w;
                w.beginObject().kv("action", "mend").kv("signature", mend->name).kvRaw("mend", mend->toJson()).key("reasons").beginArray()
                    .str("the gate of '" + v.item->id + "' attempt " + std::to_string(n) + " failed with a known mechanical error: " + mend->error)
                    .str("mend '" + mend->name + "' (" + std::to_string(applied) + " of at most " + std::to_string(mend->maxPerItem) + " for this item): the harness applies '" + mend->action + "' to `" + mend->symbol + "` itself; no rung of the ladder is spent")
                    .endArray().key("inputs").beginObject().kv("item", v.item->id).kv("attempt", n).kv("applied", applied).endObject().endObject();
                WorkflowAction rec;
                rec.kind = WorkflowAction::Kind::recordDecision;
                rec.item = v.item->id; rec.attempt = n; rec.decisionAction = "mend"; rec.decisionTier = mend->name; rec.decisionJson = w.take();
                WorkflowRecord shadow = wf;   // the next attempt's job is built from the decision just recorded
                shadow.decisions.push_back({v.item->id, n, "mend", mend->name, rec.decisionJson});
                out.push_back(std::move(rec));
                out.push_back(start(agentJob(shadow, st, *v.item, n + 1, "local")));
                continue;
            }
        }
        if (!d && (failed || disputed)) {
            DecisionContext ctx;
            ctx.workflow = wf.id; ctx.item = v.item->id; ctx.attempt = n;
            const int mended = mendsApplied(wf, v.item->id);   // a mend's attempt is the harness's work: it counts against no budget of the ladder
            ctx.localAttempts = v.localAttempts - mended; ctx.frontierAttempts = v.frontierAttempts;
            ctx.failedRole = role == "agent" ? (latest->state != JobState::succeeded ? "agent" : "gate") : role;
            ctx.failure = std::string{toString(latest->state)} + (latest->note.empty() ? "" : ": " + latest->note);
            ctx.complexity = v.item->complexity;
            ctx.frontier = st.frontier;
            ctx.maxLocalAttempts = wf.spec.maxLocalAttempts;
            ctx.depth = static_cast<int>(std::count(v.item->id.begin(), v.item->id.end(), '.'));
            ctx.totalAttempts = n - mended;
            ctx.splittable = v.item->files.size() > 1 || v.item->complexity >= 2;
            for (const JobRecord* g : v.gates) if (g->spec.attempt == n && g->exitJson.find("\"code\":3") != std::string::npos) ctx.nearPass = true;
            for (const JobRecord* g : v.gates) if (g->spec.attempt == n && g->exitJson.find("\"code\":5") != std::string::npos) ctx.repeatedFailure = true;
            for (const JobRecord* g : v.gates) if (g->spec.attempt == n && g->exitJson.find("\"code\":6") != std::string::npos) ctx.interfaceGap = true;
            if (role == "agent" && latest->exitJson.find("\"code\":4") != std::string::npos) ctx.oversize = true;
            if (const Harness* hs = wf.plan ? findHarness(wf.plan->harness) : nullptr; hs && hs->mockFile) { ctx.frontierHasReference = !v.item->files.empty(); for (const auto& f : v.item->files) if (hs->mockFile(f, false).empty()) ctx.frontierHasReference = false; }
            ctx.persona = v.item->persona;
            const LeafParams& leaf = itemsStageOf(wf).leaf;
            ctx.allowDecompose = leaf.allowDecompose; ctx.allowSections = leaf.allowSections; ctx.allowReviseTests = leaf.allowReviseTests;
            ctx.allowRearchitect = leaf.allowRearchitect; ctx.allowEscalate = leaf.allowEscalate;
            if (const Harness* hs = findHarness(plan.harness)) ctx.divideBySections = hs->divideBySections;
            for (const auto& dd : wf.decisions) if (dd.item == v.item->id && dd.action == "sections") ctx.sectionsTried = true;
            ctx.decomposeTried = v.decomposeTried;
            ctx.rearchitectTried = v.rearchitectTried;
            for (const auto& dd : wf.decisions) if (dd.item == v.item->id && dd.action == "reviseTests") ctx.reviseTried = true;
            const WorkItem* tester = nullptr;
            for (const auto& depId : v.item->deps) { const WorkItem* dep = plan.find(depId); if (dep && dep->persona == "tester") { tester = dep; ctx.hasTesterDep = true; break; } }
            Decision dec = policy.decide(ctx);
            const bool restore = dec.action == Action::fail && v.restorable;  // fall back to the accepted version from before the dispute
            const auto order = strategyOrder(wf);
            const int usedStrategies = strategiesUsed(wf, v.item->id);
            const bool strategy = dec.action == Action::fail && !restore && usedStrategies < static_cast<int>(order.size());  // the manager acts before anyone is asked
            const bool defer = dec.action == Action::fail && !restore && !strategy && wf.spec.ask;  // governance on and every strategy tried: ask the owner instead of failing
            WorkflowAction rec;
            rec.kind = WorkflowAction::Kind::recordDecision;
            rec.item = v.item->id; rec.attempt = n; rec.decisionAction = restore ? "restored" : strategy ? "strategy" : defer ? "deferred" : toString(dec.action); rec.decisionTier = strategy ? order[static_cast<std::size_t>(usedStrategies)] : dec.tier;
            if (restore) dec.reasons.push_back("the re-run could not pass: the version accepted before the dispute is restored and the work continues with it");
            if (strategy) dec.reasons.push_back("the ladder is spent: the manager applies strategy '" + order[static_cast<std::size_t>(usedStrategies)] + "' (" + std::to_string(usedStrategies + 1) + " of " + std::to_string(order.size()) + ") and tries again");
            if (defer) dec.reasons.push_back("the ladder is exhausted: deferred to the owner (a blocking request), who can guide another attempt or stop");
            rec.decisionJson = toJson(ctx, dec);
            out.push_back(std::move(rec));
            switch (dec.action) {
                case Action::fail:
                    if (strategy) {
                        WorkflowAction ss;
                        ss.kind = WorkflowAction::Kind::setStrategy;
                        ss.item = v.item->id; ss.note = order[static_cast<std::size_t>(usedStrategies)];
                        out.push_back(std::move(ss));
                        // the strategy file is written before the job starts, and the engine choice for "engine" is read from the decision just recorded
                        WorkflowRecord shadow = wf;
                        shadow.decisions.push_back({v.item->id, n, "strategy", order[static_cast<std::size_t>(usedStrategies)], ""});
                        out.push_back(start(agentJob(shadow, st, *v.item, n + 1, "local")));
                    } else if (defer) {
                        WorkflowAction rq;
                        rq.kind = WorkflowAction::Kind::raiseRequest;
                        rq.request.kind = "escalation"; rq.request.from = "engine"; rq.request.attempt = n; rq.request.blocking = true;
                        rq.request.scope = {v.item->id};
                        rq.request.title = "'" + v.item->id + "' could not be completed";
                        rq.request.detail = "Every rung of the ladder was tried (" + std::to_string(ctx.totalAttempts) + " attempts). Last failure: " + ctx.failure + ". " + (dec.reasons.empty() ? std::string{} : dec.reasons.front() + ".");
                        rq.request.options = {"retry with my guidance", "stop the workflow"};
                        out.push_back(std::move(rq));
                    } else if (!restore) failedItem = v.item->id;
                    break;
                case Action::retry: case Action::escalate: out.push_back(start(agentJob(wf, st, *v.item, n + 1, dec.tier))); break;
                case Action::sections: {   // the executor of the next attempt is read from the decision just recorded
                    WorkflowRecord shadow = wf;
                    shadow.decisions.push_back({v.item->id, n, "sections", "local", ""});
                    out.push_back(start(agentJob(shadow, st, *v.item, n + 1, "local")));
                    break;
                }
                case Action::decompose: out.push_back(start(architectJob(wf, st, *v.item, n + 1, "decompose"))); break;
                case Action::rearchitect: out.push_back(start(architectJob(wf, st, *v.item, n + 1, "rearchitect"))); break;
                case Action::reviseTests: {
                    const ItemView* tv = nullptr;
                    for (const auto& x : views) if (tester && x.item->id == tester->id) tv = &x;
                    WorkflowAction w1; w1.kind = WorkflowAction::Kind::writeDispute; w1.item = tester->id; w1.fromItem = v.item->id; w1.attempt = n; out.push_back(std::move(w1));
                    WorkflowAction w2;
                    w2.kind = WorkflowAction::Kind::recordDecision; w2.item = tester->id; w2.attempt = tv ? tv->maxAttempt : 1; w2.decisionAction = "disputed";
                    w2.decisionJson = "{\"action\":\"disputed\",\"reasons\":[\"the tests of '" + tester->id + "' are disputed by a failing attempt of '" + v.item->id + "'\"],\"inputs\":{\"item\":\"" + tester->id + "\",\"attempt\":" + std::to_string(w2.attempt) + "}}";
                    out.push_back(std::move(w2));
                    break;
                }
            }
            continue;
        }
        if (!d) continue;
        // A decision is recorded; act on it if its follow-up job has not been submitted yet.
        if (d->action == "fail") failedItem = v.item->id;
        else if (d->action == "strategy" || d->action == "mend") out.push_back(start(agentJob(wf, st, *v.item, n + 1, "local")));
        else if (d->action == "deferred") {
            const RequestRecord* rq = escalationFor(wf, v.item->id, n);
            if (rq && rq->status == "answered") { if (rq->resolution == "stop") failedItem = v.item->id; else out.push_back(start(agentJob(wf, st, *v.item, n + 1, "local"))); }
        }
        else if (d->action == "decompose") out.push_back(start(architectJob(wf, st, *v.item, n + 1, "decompose")));
        else if (d->action == "rearchitect") out.push_back(start(architectJob(wf, st, *v.item, n + 1, "rearchitect")));
        else out.push_back(start(agentJob(wf, st, *v.item, n + 1, d->tier.empty() ? "local" : d->tier)));  // retry, escalate, reviseTests, disputed
    }
    if (!failedItem.empty()) {
        // Never discard progress (owner rule): an item that ran out of options stops its own branch, not the others. Jobs in flight and work this pass
        // started carry on; the workflow fails once nothing else can move (everything that depended on the failed item stays pending).
        if (anyActive || !out.empty()) return true;
        if (!anyActive) {
            // Name the FIRST cause (row 14), not whichever item the loop above reached last, and say what failed after it.
            const std::string cause = firstCause(wf).empty() ? failedItem : firstCause(wf);
            std::string note = "item '" + cause + "' failed after the policy ran out of options";
            const auto later = laterFailures(wf);
            if (!later.empty()) { note += " (it failed first; later failures:"; for (const auto& l : later) note += " '" + l + "'"; note += ")"; }
            out.push_back(finish(WorkflowState::failed, note));
        }
        return true;
    }
    return false;
}

}  // namespace

std::vector<WorkflowAction> advance(const WorkflowRecord& wf, const JobStore& store, const WorkflowSettings& st, const IDecisionPolicy& policy)
{
    std::vector<WorkflowAction> out;
    if (wf.state != WorkflowState::running) return out;
    const Mine mine = jobsOf(wf, store);
    bool anyActive = false;
    for (const JobRecord* j : mine.all) if (!terminalJob(j)) anyActive = true;

    if (wf.cancelRequested) {
        for (const JobRecord* j : mine.all) if (!terminalJob(j) && j->state != JobState::cancelling) {
            WorkflowAction a; a.kind = WorkflowAction::Kind::cancelJob; a.jobId = j->id; out.push_back(std::move(a));
        }
        if (!anyActive) out.push_back(finish(WorkflowState::cancelled, "cancelled by request"));
        return out;
    }

    // The process: its stages in order, each derived from the record, each doing what its kind says. No stage, step or tool is named here.
    std::vector<ItemView> views;
    if (wf.plan) for (const auto& it : wf.plan->items) views.push_back(viewOf(it, mine, wf));
    const Derived derived = derive(wf, mine, views);
    bool allDone = true;
    for (const StageView& sv : derived.stages) {
        allDone = allDone && sv.done;
        if (!sv.entered) continue;
        if (sv.stage->kind == StageKind::job) { if (!sv.done && advanceJobStage(wf, mine, st, *sv.stage, derived.snapshot, out)) return out; }
        else if (wf.plan && advanceItems(wf, views, st, policy, anyActive, out)) return out;
    }
    if (allDone) out.push_back(finish(WorkflowState::succeeded, successNote(wf.spec.process)));
    return out;
}

std::vector<StageStatus> stageStatuses(const WorkflowRecord& wf, const JobStore& store)
{
    const Mine mine = jobsOf(wf, store);
    std::vector<ItemView> views;
    if (wf.plan) for (const auto& it : wf.plan->items) views.push_back(viewOf(it, mine, wf));
    std::vector<StageStatus> r;
    for (const StageView& sv : derive(wf, mine, views).stages)
        r.push_back({sv.stage->name, sv.stage->kind == StageKind::job ? "job" : "items", sv.done ? "done" : sv.failed ? "failed" : sv.entered ? "active" : "waiting", sv.waitingOn});
    return r;
}

std::vector<ItemStatus> itemStatuses(const WorkflowRecord& wf, const JobStore& store)
{
    std::vector<ItemStatus> r;
    if (!wf.plan) return r;
    const Mine mine = jobsOf(wf, store);
    for (const auto& it : wf.plan->items) {
        const ItemView v = viewOf(it, mine, wf);
        ItemStatus s;
        s.id = it.id; s.title = it.title; s.attempts = v.maxAttempt; s.tier = v.latestTier;
        if (it.persona == "integrator") { for (const JobRecord* x : v.gates) s.attempts = std::max<int>(s.attempts, static_cast<int>(x->spec.attempt)); s.tier = v.gates.empty() ? "" : "local"; }
        s.status = stateOf(v, wf);
        r.push_back(std::move(s));
    }
    return r;
}

}  // namespace sub0squad
