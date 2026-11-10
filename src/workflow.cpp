#include "sub0squad/workflow.hpp"

#include "json_util.hpp"

#include "sub0squad/harness_web.hpp"
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
    w.beginObject().kv("task", task).kv("taskText", taskText).kv("triage", triage).kv("sandbox", sandboxDir).kv("maxLocalAttempts", maxLocalAttempts).kv("agent", agent).kv("treatment", treatment).kv("repairTurns", repairTurns).kv("sizeBudget", sizeBudget).kv("maxTokens", maxTokens).kvBool("ask", ask).key("inject").beginArray();
    for (const auto& i : inject) w.str(i);
    w.endArray().key("engines").beginArray();
    for (const auto& e : engines) w.str(e);
    w.endArray().key("route").beginObject();
    for (const auto& [persona, name] : route) w.kv(persona, name);
    w.endObject().endObject();
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
                else if (k == "repairTurns") s.repairTurns = static_cast<int>(v.get_int64());
                else if (k == "sizeBudget") s.sizeBudget = static_cast<int>(v.get_int64());
                else if (k == "maxTokens") s.maxTokens = static_cast<int>(v.get_int64());
                else if (k == "ask") s.ask = v.get_bool();
                else if (k == "engines") { for (auto x : v.get_array()) s.engines.push_back(json::toString(x.value())); }
                else if (k == "route") { for (auto r : v.get_object()) { std::string persona{std::string_view{r.unescaped_key()}}; s.route[persona] = json::toString(r.value()); } }
            }
        });
    } catch (const simdjson::simdjson_error& e) { return std::unexpected(std::string{"bad workflow spec: "} + e.what()); }
    if (s.task.empty()) return std::unexpected("workflow needs a task");
    if (s.taskText.size() > 4000) return std::unexpected("task text is too long");
    if (!s.sandboxDir.empty() && s.sandboxDir[0] != '/') return std::unexpected("workflow sandbox must be an absolute directory");  // empty: the daemon chooses
    if (s.maxLocalAttempts < 1 || s.maxLocalAttempts > 5) return std::unexpected("maxLocalAttempts must be 1..5");
    if (s.agent != "mock" && s.agent != "llm") return std::unexpected("agent must be mock or llm");
    std::set<std::string> names;
    for (const auto& e : s.engines) { auto p = EngineProfile::parse(e); if (!p) return std::unexpected("engine '" + e + "': " + p.error()); names.insert(p->name); }
    if (s.agent == "llm" && s.engines.empty()) return std::unexpected("agent llm needs at least one --engine");
    for (const auto& [persona, name] : s.route) if (!names.count(name)) return std::unexpected("route " + persona + "=" + name + " names an unknown engine");
    if (s.treatment != "baseline" && s.treatment != "index" && s.treatment != "planfirst") return std::unexpected("treatment must be baseline, index or planfirst");
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

// ---- engine ---------------------------------------------------------------------------------------------------------

namespace {

struct Mine { std::vector<const JobRecord*> all; };

Mine jobsOf(const WorkflowRecord& wf, const JobStore& store)
{
    Mine m;
    for (const auto& j : store.jobs()) if (j.spec.workflow == wf.id) m.all.push_back(&j);
    return m;
}

JobSpec baseJob(const WorkflowRecord& wf, const WorkflowSettings& st, const char* role, const char* tool, const char* tier, std::int64_t wall)
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

const DecisionRecord* decisionFor(const WorkflowRecord& wf, const std::string& item, int attempt)
{
    for (const auto& d : wf.decisions) if (d.item == item && d.attempt == attempt) return &d;
    return nullptr;
}

JobSpec agentJob(const WorkflowRecord& wf, const WorkflowSettings& st, const WorkItem& it, int attempt, const std::string& tier)
{
    const auto engine = wf.spec.engineFor(it.persona);
    const bool llm = wf.spec.agent == "llm" && engine.has_value();
    JobSpec s = baseJob(wf, st, "agent", llm ? "llm-agent" : "mock-agent", tier.c_str(), llm && tier == "local" ? 3 * 3600 : 120);
    s.item = it.id;
    s.attempt = attempt;
    if (llm) { s.leaseResource = engine->endpoint; s.leaseCap = std::max(1, engine->maxConcurrent); }
    if (llm) {
        s.argv = {st.exe, "llm-agent", "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--tier", tier, "--attempt", std::to_string(attempt), "--engine", engine->toArg(),
                  "--max-tokens", std::to_string(wf.spec.maxTokens), "--timeout", "1700", "--treatment", wf.spec.treatment, "--repair-turns", std::to_string(wf.spec.repairTurns), "--size-budget", std::to_string(wf.spec.sizeBudget)};
    } else {
        s.argv = {st.exe, "mock-agent", "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--tier", tier, "--attempt", std::to_string(attempt)};
        for (const auto& i : wf.spec.inject) { s.argv.push_back("--inject"); s.argv.push_back(i); }
    }
    return s;
}

JobSpec gateJob(const WorkflowRecord& wf, const WorkflowSettings& st, const WorkItem& it, int attempt, const std::string& tier)
{
    const bool web = wf.plan && wf.plan->harness == "web-static";
    JobSpec s = baseJob(wf, st, "gate", web ? "harness-gate" : "sandbox-gate", tier.c_str(), 120);
    s.item = it.id;
    s.attempt = attempt;
    s.argv = {st.exe, web ? "harness-gate" : "sandbox-gate", "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--attempt", std::to_string(attempt)};
    return s;
}

JobSpec architectJob(const WorkflowRecord& wf, const WorkflowSettings& st, const WorkItem& it, int attempt, const char* mode)
{
    const auto engine = wf.spec.engineFor("architect");
    const bool llm = wf.spec.agent == "llm" && engine.has_value();
    JobSpec s = baseJob(wf, st, mode, llm ? "llm-architect" : "mock-architect", "local", llm ? 1800 : 120);
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

std::vector<WorkflowAction> advance(const WorkflowRecord& wf, const JobStore& store, const WorkflowSettings& st, const IDecisionPolicy& policy)
{
    std::vector<WorkflowAction> out;
    if (wf.state != WorkflowState::running) return out;
    const Mine mine = jobsOf(wf, store);
    bool anyActive = false;
    for (const JobRecord* j : mine.all) if (!terminalJob(j)) anyActive = true;

    auto cancelActive = [&] {
        for (const JobRecord* j : mine.all) if (!terminalJob(j) && j->state != JobState::cancelling) {
            WorkflowAction a; a.kind = WorkflowAction::Kind::cancelJob; a.jobId = j->id; out.push_back(std::move(a));
        }
    };
    if (wf.cancelRequested) {
        cancelActive();
        if (!anyActive) out.push_back(finish(WorkflowState::cancelled, "cancelled by request"));
        return out;
    }

    // 1. Planner.
    if (!wf.plan) {
        int plannerJobs = 0;
        const JobRecord* last = nullptr;
        for (const JobRecord* j : mine.all) if (j->spec.role == "planner") { ++plannerJobs; last = j; }
        if (plannerJobs == 0) {
            JobSpec s = baseJob(wf, st, "planner", "mock-planner", "stub", 60);
            s.argv = {st.exe, "mock-planner", "--task", wf.spec.task, "--out", wf.spec.sandboxDir + "/plan.json"};
            out.push_back(start(std::move(s)));
        } else if (!terminalJob(last)) {
            // wait
        } else if (last->state == JobState::succeeded) {
            WorkflowAction a; a.kind = WorkflowAction::Kind::loadPlan; out.push_back(std::move(a));
        } else if (plannerJobs < 2) {
            JobSpec s = baseJob(wf, st, "planner", "mock-planner", "stub", 60);
            s.argv = {st.exe, "mock-planner", "--task", wf.spec.task, "--out", wf.spec.sandboxDir + "/plan.json"};
            out.push_back(start(std::move(s)));
        } else {
            out.push_back(finish(WorkflowState::failed, "planner failed twice"));
        }
        return out;
    }

    // 2. Items.
    const Plan& plan = *wf.plan;
    std::vector<ItemView> views;
    for (const auto& it : plan.items) views.push_back(viewOf(it, mine, wf));

    bool allAccepted = true;
    std::string failedItem;
    auto isAccepted = [&](const std::string& id) { for (const auto& v : views) if (v.item->id == id) return v.accepted; return false; };
    for (const ItemView& v : views) {
        if (v.accepted) continue;
        allAccepted = false;
        if (v.active) continue;
        if (!std::all_of(v.item->deps.begin(), v.item->deps.end(), isAccepted)) continue;
        if (blockedByRequest(wf, v.item->id)) continue;  // waiting for the owner or the architect: nothing new starts for this node
        if (v.item->persona == "integrator") {
            // The V's ascent. A coordinator has no agent step: when its parts are accepted its own tests run over them (a gate per round). A
            // failing round is sent down to the part the failure points at (the daemon picks it and writes the feedback), the part is redone,
            // and the coordinator integrates again, up to kMaxRoundTrips times; then the item fails with the evidence.
            constexpr int kMaxRoundTrips = 3;
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
        if (!d && (failed || disputed)) {
            DecisionContext ctx;
            ctx.workflow = wf.id; ctx.item = v.item->id; ctx.attempt = n;
            ctx.localAttempts = v.localAttempts; ctx.frontierAttempts = v.frontierAttempts;
            ctx.failedRole = role == "agent" ? (latest->state != JobState::succeeded ? "agent" : "gate") : role;
            ctx.failure = std::string{toString(latest->state)} + (latest->note.empty() ? "" : ": " + latest->note);
            ctx.complexity = v.item->complexity;
            ctx.frontier = st.frontier;
            ctx.maxLocalAttempts = wf.spec.maxLocalAttempts;
            ctx.depth = static_cast<int>(std::count(v.item->id.begin(), v.item->id.end(), '.'));
            ctx.totalAttempts = n;
            ctx.splittable = v.item->files.size() > 1 || v.item->complexity >= 2;
            for (const JobRecord* g : v.gates) if (g->spec.attempt == n && g->exitJson.find("\"code\":3") != std::string::npos) ctx.nearPass = true;
            for (const JobRecord* g : v.gates) if (g->spec.attempt == n && g->exitJson.find("\"code\":5") != std::string::npos) ctx.repeatedFailure = true;
            if (role == "agent" && latest->exitJson.find("\"code\":4") != std::string::npos) ctx.oversize = true;
            if (wf.plan && wf.plan->harness == "web-static") { ctx.frontierHasReference = !v.item->files.empty(); for (const auto& f : v.item->files) if (webMockFile(f, false).empty()) ctx.frontierHasReference = false; }
            ctx.decomposeTried = v.decomposeTried;
            ctx.rearchitectTried = v.rearchitectTried;
            for (const auto& dd : wf.decisions) if (dd.item == v.item->id && dd.action == "reviseTests") ctx.reviseTried = true;
            const WorkItem* tester = nullptr;
            for (const auto& depId : v.item->deps) { const WorkItem* dep = plan.find(depId); if (dep && dep->persona == "tester") { tester = dep; ctx.hasTesterDep = true; break; } }
            Decision dec = policy.decide(ctx);
            const bool restore = dec.action == Action::fail && v.restorable;  // fall back to the accepted version from before the dispute
            const bool defer = dec.action == Action::fail && !restore && wf.spec.ask;  // governance on: ask the owner instead of failing the workflow
            WorkflowAction rec;
            rec.kind = WorkflowAction::Kind::recordDecision;
            rec.item = v.item->id; rec.attempt = n; rec.decisionAction = restore ? "restored" : defer ? "deferred" : toString(dec.action); rec.decisionTier = dec.tier;
            if (restore) dec.reasons.push_back("the re-run could not pass: the version accepted before the dispute is restored and the work continues with it");
            if (defer) dec.reasons.push_back("the ladder is exhausted: deferred to the owner (a blocking request), who can guide another attempt or stop");
            rec.decisionJson = toJson(ctx, dec);
            out.push_back(std::move(rec));
            switch (dec.action) {
                case Action::fail:
                    if (defer) {
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
        else if (d->action == "deferred") {
            const RequestRecord* rq = escalationFor(wf, v.item->id, n);
            if (rq && rq->status == "answered") { if (rq->resolution == "stop") failedItem = v.item->id; else out.push_back(start(agentJob(wf, st, *v.item, n + 1, "local"))); }
        }
        else if (d->action == "decompose") out.push_back(start(architectJob(wf, st, *v.item, n + 1, "decompose")));
        else if (d->action == "rearchitect") out.push_back(start(architectJob(wf, st, *v.item, n + 1, "rearchitect")));
        else out.push_back(start(agentJob(wf, st, *v.item, n + 1, d->tier.empty() ? "local" : d->tier)));  // retry, escalate, reviseTests, disputed
    }
    if (!failedItem.empty()) {
        cancelActive();
        if (!anyActive) out.push_back(finish(WorkflowState::failed, "item '" + failedItem + "' failed after the policy ran out of options"));
        return out;
    }

    // 3. Integrator.
    if (allAccepted && !anyOpenBlocking(wf)) {
        const JobRecord* integ = nullptr;
        for (const JobRecord* j : mine.all) if (j->spec.role == "integrator") integ = j;
        if (!integ) {
            const bool web = plan.harness == "web-static";
            JobSpec s = baseJob(wf, st, "integrator", web ? "harness-integrate" : "sandbox-integrate", "stub", 300);
            s.argv = {st.exe, web ? "harness-integrate" : "sandbox-integrate", "--workflow-dir", wf.spec.sandboxDir};
            out.push_back(start(std::move(s)));
        } else if (terminalJob(integ)) {
            out.push_back(integ->state == JobState::succeeded ? finish(WorkflowState::succeeded, "all items accepted; integration passed")
                                                              : finish(WorkflowState::failed, "integration failed: " + integ->note));
        }
    }
    return out;
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
        const DecisionRecord* d = s.attempts ? decisionFor(wf, it.id, s.attempts) : nullptr;
        s.status = v.accepted ? "accepted" : v.active ? "running" : (d && d->action == "fail") ? "failed" : "pending";
        r.push_back(std::move(s));
    }
    return r;
}

}  // namespace sub0squad
