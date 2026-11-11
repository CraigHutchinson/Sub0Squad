/// @file daemon_governance.cpp
/// Daemon: governance: guidance, the first responder, raising and answering requests.

#include "daemon_impl.hpp"

namespace sub0squad {

void Daemon::Impl::applyGuidance(const WorkflowRecord& wf, const std::vector<std::string>& scope, const std::string& text, bool voidAccepted, const std::string& source)
{
    if (!wf.plan || text.empty()) return;
    const std::filesystem::path sandbox{wf.spec.sandboxDir};
    std::vector<std::string> items = scope;
    if (items.empty()) for (const auto& it : wf.plan->items) if (it.persona != "integrator") items.push_back(it.id);
    for (const auto& id : items) {
        const WorkItem* it = wf.plan->find(id);
        if (!it || it->persona == "integrator") continue;
        std::error_code ec;
        std::filesystem::create_directories(sandbox / "items" / id, ec);
        { std::ofstream o(sandbox / "items" / id / "dispute.txt", std::ios::trunc); o << source << " about your work:\n" << text << "\nTake it into account and keep what already works.\n"; }
        if (voidAccepted && std::filesystem::exists(sandbox / "items" / id / "accepted.json")) {
            int attempt = 1;
            for (const auto& j : store.jobs()) if (j.spec.workflow == wf.id && j.spec.item == id) attempt = std::max<int>(attempt, static_cast<int>(j.spec.attempt));
            json::Writer w;
            w.beginObject().kv("action", "disputed").key("reasons").beginArray().str(source + " changed what '" + id + "' must deliver: it is redone").endArray()
                .key("inputs").beginObject().kv("item", id).kv("attempt", attempt).endObject().endObject();
            record("workflowDecision", wfbody::decision(wf.id, id, attempt, "disputed", "local", w.take()));
        }
    }
}

void Daemon::Impl::firstResponder(const std::string& wfId, const std::string& rid)
{
    const WorkflowRecord* wf = wfStore.find(wfId);
    if (!wf || !wf->plan) return;
    const RequestRecord* req = nullptr;
    for (const auto& r : wf->requests) if (r.id == rid) req = &r;
    if (!req || req->scope.size() != 1) return;
    const std::string item = req->scope[0];
    const WorkItem* wi = wf->plan->find(item);
    if (!wi) return;
    const std::filesystem::path sandbox{wf->spec.sandboxDir};
    OverseerInput in;
    in.persona = wi->persona;
    for (int k = std::max(1, req->attempt - 3); k <= req->attempt; ++k) {
        std::ifstream f(sandbox / "items" / item / ("attempt-" + std::to_string(k)) / "gate.txt");
        std::stringstream ss;
        ss << f.rdbuf();
        if (!ss.str().empty()) in.gates.push_back(ss.str());
    }
    const JobRecord* lastAgent = nullptr;
    for (const auto& j : store.jobs()) if (j.spec.workflow == wfId && j.spec.item == item && j.spec.role == "agent" && (!lastAgent || j.spec.attempt >= lastAgent->spec.attempt)) lastAgent = &j;
    if (lastAgent) {
        std::ifstream f(opts.paths.runDir(lastAgent->id, lastAgent->run) / "stderr.log");
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string t = ss.str();
        in.agentNote = t.size() > 300 ? t.substr(t.size() - 300) : t;
    }
    const OverseerVerdict v = analyseEscalation(in);
    {   // the harness-expert's briefing: everything a decider needs, in one file next to the node's files
        BriefingInput b;
        b.workflow = wfId; b.request = rid; b.kind = req->kind; b.title = req->title; b.task = wf->spec.taskText.empty() ? wf->spec.task : wf->spec.taskText;
        b.item = item; b.persona = wi->persona; b.itemTitle = wi->title; b.goal = wi->goal; b.files = wi->files; b.contract = wi->card.contract; b.acceptance = wi->card.acceptance;
        const Harness* h = findHarness(wf->plan->harness);
        b.harnessFacts = h && h->facts ? h->facts() : std::string{"(no harness facts for this project type)"};
        b.gates = in.gates; b.agentNote = in.agentNote; b.lastConversation = recallLastConversation(sandbox, item); b.triage = v.triage; b.analysis = v.analysis; b.options = req->options;
        for (const auto& dd : wf->decisions) if (dd.item == item) b.history.push_back("attempt " + std::to_string(dd.attempt) + ": " + dd.action + (dd.tier.empty() ? "" : " (" + dd.tier + ")"));
        std::error_code ec;
        std::filesystem::create_directories(sandbox / "items" / item, ec);
        std::ofstream o(sandbox / "items" / item / ("briefing-" + rid + ".md"), std::ios::trunc);
        o << buildBriefing(b);
    }
    record("workflowRequestAnalysed", wfbody::requestAnalysed(wfId, rid, v.triage, v.analysis));
    logf("workflow %s: overseer on %s: %s (%s)", wfId.c_str(), rid.c_str(), v.triage.c_str(), v.analysis.c_str());
    if (v.triage != "guidance") return;
    int autos = 0;
    for (const auto& r : wf->requests) if (r.scope.size() == 1 && r.scope[0] == item && r.answeredBy == "overseer") ++autos;
    if (autos >= 2) return;
    answerRequest(wfId, rid, v.guidance, "retry", "overseer");
}

std::string Daemon::Impl::answerRequest(const std::string& wfId, const std::string& rid, const std::string& answer, const std::string& resolution, const std::string& by)
{
    const WorkflowRecord* wf = wfStore.find(wfId);
    if (!wf) return "no such workflow: " + wfId;
    const RequestRecord* req = nullptr;
    for (const auto& r : wf->requests) if (r.id == rid) req = &r;
    if (!req) return "no such request: " + rid;
    if (req->status != "open") return "request " + rid + " is already answered";
    if (answer.size() > 4000) return "answer is too long";
    if (resolution != "" && resolution != "retry" && resolution != "stop") return "resolution must be retry or stop";
    const RequestRecord copy = *req;
    record("workflowRequestAnswered", wfbody::requestAnswered(wfId, rid, answer, resolution, by));
    if (const WorkflowRecord* w2 = wfStore.find(wfId); w2 && resolution != "stop")
        applyGuidance(*w2, copy.scope, answer, copy.kind != "escalation", by == "overseer" ? "Guidance from the overseer" : "Guidance from the owner");
    return {};
}

std::string Daemon::Impl::raiseRequest(const std::string& wfId, RequestRecord r)
{
    const WorkflowRecord* wf = wfStore.find(wfId);
    if (!wf) return {};
    r.id = "r-" + std::to_string(wf->requests.size() + 1);
    record("workflowRequestRaised", wfbody::requestRaised(wfId, r));
    logf("workflow %s: %s request %s raised by %s: %s%s", wfId.c_str(), r.kind.c_str(), r.id.c_str(), r.from.c_str(), r.title.c_str(), r.blocking ? " (blocking)" : "");
    if (r.kind == "escalation" && r.from == "engine") firstResponder(wfId, r.id);
    return r.id;
}

std::string Daemon::Impl::doRequestRaise(const std::string& wfId, const std::string& specJson)
{
    const WorkflowRecord* wf = wfStore.find(wfId);
    if (!wf) return errorReply("no such workflow: " + wfId);
    RequestRecord r;
    try {
        json::withObject(specJson, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "kind") r.kind = json::toString(v);
                else if (k == "from") r.from = json::toString(v);
                else if (k == "title") r.title = json::toString(v);
                else if (k == "detail") r.detail = json::toString(v);
                else if (k == "blocking") r.blocking = v.get_bool();
                else if (k == "scope") { for (auto x : v.get_array()) r.scope.push_back(json::toString(x.value())); }
                else if (k == "options") { for (auto x : v.get_array()) r.options.push_back(json::toString(x.value())); }
            }
        });
    } catch (const simdjson::simdjson_error& e) { return errorReply(std::string{"bad request: "} + e.what()); }
    static const std::set<std::string> kinds{"question", "clarification", "requirement", "blocker", "proposal", "escalation"};
    if (!kinds.count(r.kind)) return errorReply("kind must be one of question, clarification, requirement, blocker, proposal, escalation");
    if (r.title.empty() && r.detail.empty()) return errorReply("a request needs a title or a detail");
    if (r.title.size() > 300 || r.detail.size() > 4000) return errorReply("request text is too long");
    if (r.from.empty()) r.from = "user";
    if (wf->state != WorkflowState::running && wf->state != WorkflowState::awaitingPlan) return errorReply("workflow " + wfId + " is " + toString(wf->state));
    if (r.title.empty()) r.title = r.detail.substr(0, 80);
    for (const auto& s : r.scope) if (!wf->plan || !wf->plan->find(s)) return errorReply("unknown node in scope: " + s);
    const std::string rid = raiseRequest(wfId, r);
    // The owner stating a requirement or clarification is a decision already made: it takes effect now.
    if (r.from == "user" && (r.kind == "requirement" || r.kind == "clarification")) {
        const std::string text = r.detail.empty() ? r.title : r.detail;
        record("workflowRequestAnswered", wfbody::requestAnswered(wfId, rid, text, "", "user"));
        if (const WorkflowRecord* w2 = wfStore.find(wfId)) applyGuidance(*w2, r.scope, text, true, "A requirement from the owner");
    }
    advanceWorkflows();
    json::Writer w;
    w.beginObject().kvBool("ok", true).kv("id", wfId).kv("rid", rid).endObject();
    return w.take();
}

std::string Daemon::Impl::doRequestAnswer(const std::string& wfId, const std::string& specJson)
{
    const WorkflowRecord* wf = wfStore.find(wfId);
    if (!wf) return errorReply("no such workflow: " + wfId);
    std::string rid, answer, resolution, by = "user";
    try {
        json::withObject(specJson, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "rid") rid = json::toString(v);
                else if (k == "answer") answer = json::toString(v);
                else if (k == "resolution") resolution = json::toString(v);
                else if (k == "by") by = json::toString(v);
            }
        });
    } catch (const simdjson::simdjson_error& e) { return errorReply(std::string{"bad answer: "} + e.what()); }
    if (const std::string err = answerRequest(wfId, rid, answer, resolution, by); !err.empty()) return errorReply(err);
    advanceWorkflows();
    json::Writer w;
    w.beginObject().kvBool("ok", true).kv("id", wfId).kv("rid", rid).endObject();
    return w.take();
}

}  // namespace sub0squad
