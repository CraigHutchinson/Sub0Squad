#include "sub0squad/workflow.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <cstdio>

namespace sub0squad {

const char* toString(WorkflowState s)
{
    switch (s) { case WorkflowState::running: return "running"; case WorkflowState::succeeded: return "succeeded"; case WorkflowState::failed: return "failed"; case WorkflowState::cancelled: return "cancelled"; }
    return "?";
}

std::string WorkflowSpec::toJson() const
{
    json::Writer w;
    w.beginObject().kv("task", task).kv("sandbox", sandboxDir).kv("maxLocalAttempts", maxLocalAttempts).key("inject").beginArray();
    for (const auto& i : inject) w.str(i);
    w.endArray().endObject();
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
                else if (k == "sandbox") s.sandboxDir = json::toString(v);
                else if (k == "maxLocalAttempts") s.maxLocalAttempts = static_cast<int>(v.get_int64());
                else if (k == "inject") { for (auto x : v.get_array()) s.inject.push_back(json::toString(x.value())); }
            }
        });
    } catch (const simdjson::simdjson_error& e) { return std::unexpected(std::string{"bad workflow spec: "} + e.what()); }
    if (s.task.empty()) return std::unexpected("workflow needs a task");
    if (!s.sandboxDir.empty() && s.sandboxDir[0] != '/') return std::unexpected("workflow sandbox must be an absolute directory");  // empty: the daemon chooses
    if (s.maxLocalAttempts < 1 || s.maxLocalAttempts > 5) return std::unexpected("maxLocalAttempts must be 1..5");
    return s;
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
    if (rec.type == "workflowPlanned") { if (auto p = Plan::fromJson(planJson)) w->plan = std::move(*p); }
    else if (rec.type == "workflowDecision") w->decisions.push_back({item, static_cast<int>(attempt), action, tier, decisionJson});
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
    std::vector<const JobRecord*> agents, gates;
    bool accepted = false, active = false;
    int maxAttempt = 0;
    int localAttempts = 0, frontierAttempts = 0;
    std::string latestTier;
    const JobRecord* latestAgent = nullptr;
};

ItemView viewOf(const WorkItem& it, const Mine& mine)
{
    ItemView v;
    v.item = &it;
    for (const JobRecord* j : mine.all) {
        if (j->spec.item != it.id) continue;
        if (!terminalJob(j)) v.active = true;
        if (j->spec.role == "agent") {
            v.agents.push_back(j);
            v.maxAttempt = std::max<int>(v.maxAttempt, static_cast<int>(j->spec.attempt));
            if (j->spec.tier == "frontier-stub") ++v.frontierAttempts; else ++v.localAttempts;
            if (!v.latestAgent || j->spec.attempt >= v.latestAgent->spec.attempt) { v.latestAgent = j; v.latestTier = j->spec.tier; }
        } else if (j->spec.role == "gate") {
            v.gates.push_back(j);
            if (j->state == JobState::succeeded) v.accepted = true;
        }
    }
    return v;
}

const DecisionRecord* decisionFor(const WorkflowRecord& wf, const std::string& item, int attempt)
{
    for (const auto& d : wf.decisions) if (d.item == item && d.attempt == attempt) return &d;
    return nullptr;
}

JobSpec agentJob(const WorkflowRecord& wf, const WorkflowSettings& st, const WorkItem& it, int attempt, const std::string& tier)
{
    JobSpec s = baseJob(wf, st, "agent", "mock-agent", tier.c_str(), 120);
    s.item = it.id;
    s.attempt = attempt;
    s.argv = {st.exe, "mock-agent", "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--tier", tier, "--attempt", std::to_string(attempt)};
    for (const auto& i : wf.spec.inject) { s.argv.push_back("--inject"); s.argv.push_back(i); }
    return s;
}

JobSpec gateJob(const WorkflowRecord& wf, const WorkflowSettings& st, const WorkItem& it, int attempt, const std::string& tier)
{
    JobSpec s = baseJob(wf, st, "gate", "sandbox-gate", tier.c_str(), 120);
    s.item = it.id;
    s.attempt = attempt;
    s.argv = {st.exe, "sandbox-gate", "--workflow-dir", wf.spec.sandboxDir, "--item", it.id, "--attempt", std::to_string(attempt)};
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
    for (const auto& it : plan.items) views.push_back(viewOf(it, mine));
    auto accepted = [&](const std::string& id) { for (const auto& v : views) if (v.item->id == id) return v.accepted; return false; };

    bool allAccepted = true;
    std::string failedItem;
    for (const ItemView& v : views) {
        if (v.accepted) continue;
        allAccepted = false;
        if (v.active) continue;
        if (!std::all_of(v.item->deps.begin(), v.item->deps.end(), accepted)) continue;
        if (v.maxAttempt == 0) { out.push_back(start(agentJob(wf, st, *v.item, 1, "local"))); continue; }
        const int n = v.maxAttempt;
        const JobRecord* agent = v.latestAgent;
        const JobRecord* gate = nullptr;
        for (const JobRecord* g : v.gates) if (g->spec.attempt == n) gate = g;
        if (agent->state == JobState::succeeded && !gate) { out.push_back(start(gateJob(wf, st, *v.item, n, v.latestTier))); continue; }
        // The attempt failed (agent or gate): there must be a recorded decision.
        const DecisionRecord* d = decisionFor(wf, v.item->id, n);
        if (!d) {
            DecisionContext ctx;
            ctx.workflow = wf.id; ctx.item = v.item->id; ctx.attempt = n;
            ctx.localAttempts = v.localAttempts; ctx.frontierAttempts = v.frontierAttempts;
            ctx.failedRole = agent->state != JobState::succeeded ? "agent" : "gate";
            const JobRecord* failed = agent->state != JobState::succeeded ? agent : gate;
            ctx.failure = std::string{toString(failed->state)} + (failed->note.empty() ? "" : ": " + failed->note);
            ctx.complexity = v.item->complexity;
            ctx.frontier = st.frontier;
            ctx.maxLocalAttempts = wf.spec.maxLocalAttempts;
            const Decision dec = policy.decide(ctx);
            WorkflowAction a;
            a.kind = WorkflowAction::Kind::recordDecision;
            a.item = v.item->id; a.attempt = n; a.decisionAction = toString(dec.action); a.decisionTier = dec.tier; a.decisionJson = toJson(ctx, dec);
            out.push_back(std::move(a));
            if (dec.action == Action::fail) failedItem = v.item->id;
            else out.push_back(start(agentJob(wf, st, *v.item, n + 1, dec.tier)));
            continue;
        }
        if (d->action == "fail") failedItem = v.item->id;
        else out.push_back(start(agentJob(wf, st, *v.item, n + 1, d->tier)));  // decision recorded but the retry job not yet submitted
    }
    if (!failedItem.empty()) {
        cancelActive();
        if (!anyActive) out.push_back(finish(WorkflowState::failed, "item '" + failedItem + "' failed after the policy ran out of options"));
        return out;
    }

    // 3. Integrator.
    if (allAccepted) {
        const JobRecord* integ = nullptr;
        for (const JobRecord* j : mine.all) if (j->spec.role == "integrator") integ = j;
        if (!integ) {
            JobSpec s = baseJob(wf, st, "integrator", "sandbox-integrate", "stub", 300);
            s.argv = {st.exe, "sandbox-integrate", "--workflow-dir", wf.spec.sandboxDir};
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
        const ItemView v = viewOf(it, mine);
        ItemStatus s;
        s.id = it.id; s.title = it.title; s.attempts = v.maxAttempt; s.tier = v.latestTier;
        const DecisionRecord* d = v.maxAttempt ? decisionFor(wf, it.id, v.maxAttempt) : nullptr;
        s.status = v.accepted ? "accepted" : v.active ? "running" : (d && d->action == "fail") ? "failed" : "pending";
        r.push_back(std::move(s));
    }
    return r;
}

}  // namespace sub0squad
