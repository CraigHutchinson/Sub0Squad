/// @file daemon_workflow.cpp
/// Daemon: executing workflow actions, advancing workflows, submitting, planning and cancelling them.

#include "daemon_impl.hpp"

namespace sub0squad {

void Daemon::Impl::executeWorkflowAction(const std::string& wfId, const WorkflowAction& a)
{
    switch (a.kind) {
        case WorkflowAction::Kind::startJob: {
            const std::string id = store.nextId();
            record("jobSubmitted", journalbody::submitted(id, a.job));
            logf("workflow %s: queued %s job %s (item '%s', attempt %d, tier %s)", wfId.c_str(), a.job.role.c_str(), id.c_str(), a.job.item.c_str(), static_cast<int>(a.job.attempt), a.job.tier.c_str());
            break;
        }
        case WorkflowAction::Kind::recordDecision:
            record("workflowDecision", wfbody::decision(wfId, a.item, a.attempt, a.decisionAction, a.decisionTier, a.decisionJson));
            logf("workflow %s: decision for '%s' attempt %d: %s %s", wfId.c_str(), a.item.c_str(), a.attempt, a.decisionAction.c_str(), a.decisionTier.c_str());
            break;
        case WorkflowAction::Kind::cancelJob: (void)doCancel(a.jobId); break;
        case WorkflowAction::Kind::loadPlan: {
            const WorkflowRecord* wf = wfStore.find(wfId);
            if (!wf) break;
            std::ifstream in(std::filesystem::path{wf->spec.sandboxDir} / "plan.json");
            std::stringstream ss;
            ss << in.rdbuf();
            auto plan = Plan::fromJson(ss.str());
            if (plan) {
                record("workflowPlanned", wfbody::planned(wfId, *plan));
                logf("workflow %s: plan accepted, %zu item(s)", wfId.c_str(), plan->items.size());
            } else {
                record("workflowFinished", wfbody::finished(wfId, WorkflowState::failed, "plan rejected: " + plan.error()));
                logf("workflow %s: plan rejected: %s", wfId.c_str(), plan.error().c_str());
            }
            break;
        }
        case WorkflowAction::Kind::amendPlan: {
            const WorkflowRecord* wf = wfStore.find(wfId);
            if (!wf || !wf->plan) break;
            const std::filesystem::path sandbox{wf->spec.sandboxDir};
            const std::filesystem::path frag = sandbox / "items" / a.item / ("attempt-" + std::to_string(a.attempt)) / "fragment.json";
            std::ifstream in(frag);
            std::stringstream ss;
            ss << in.rdbuf();
            std::string reason;
            auto fragment = Plan::fromJson(ss.str());
            if (!fragment) reason = "fragment invalid: " + fragment.error();
            else if (auto merged = Plan::amended(*wf->plan, a.item, *fragment)) {
                { std::ofstream o(sandbox / "plan.json", std::ios::trunc); o << merged->toJson(); }
                const std::size_t before = wf->plan->items.size();
                record("workflowPlanAmended", wfbody::planAmended(wfId, a.item, *merged));
                logf("workflow %s: item '%s' divided into %zu items", wfId.c_str(), a.item.c_str(), merged->items.size() - before + 1);
                break;
            } else reason = merged.error();
            record("workflowAmendRejected", wfbody::amendRejected(wfId, a.item, a.attempt, reason));
            logf("workflow %s: division of '%s' rejected: %s", wfId.c_str(), a.item.c_str(), reason.c_str());
            break;
        }
        case WorkflowAction::Kind::writeDispute: {
            const WorkflowRecord* wf = wfStore.find(wfId);
            if (!wf) break;
            const std::filesystem::path sandbox{wf->spec.sandboxDir};
            std::ifstream in(sandbox / "items" / a.fromItem / ("attempt-" + std::to_string(a.attempt)) / "gate.txt");
            std::stringstream ss;
            ss << in.rdbuf();
            std::ofstream o(sandbox / "items" / a.item / "dispute.txt", std::ios::trunc);
            o << "Item '" << a.fromItem << "' is graded by your tests and failed them even after several attempts. Some of your tests may contradict the contract. Failures reported:\n"
              << ss.str() << "Check each failing test against the contract and fix the tests that are wrong; keep the ones that are right.\n";
            break;
        }
        case WorkflowAction::Kind::raiseRequest: raiseRequest(wfId, a.request); break;
        case WorkflowAction::Kind::setStrategy: {
            // what the manager decided for this item, read by its next attempt: reasoning on and a bigger budget; the engine switch is made when the job is built
            const WorkflowRecord* wf = wfStore.find(wfId);
            if (!wf) break;
            bool think = a.note == "think", engine = a.note == "engine";
            for (const auto& dd : wf->decisions) if (dd.item == a.item && dd.action == "strategy") { think = think || dd.tier == "think"; engine = engine || dd.tier == "engine"; }
            const std::filesystem::path f = std::filesystem::path{wf->spec.sandboxDir} / "items" / a.item / "strategy.json";
            std::error_code ec;
            std::filesystem::create_directories(f.parent_path(), ec);
            std::ofstream o(f, std::ios::trunc);
            o << "{\"think\":" << (think || engine ? "true" : "false") << ",\"engine\":" << (engine ? "true" : "false") << ",\"latest\":\"" << a.note << "\"}\n";
            logf("workflow %s: manager strategy '%s' for '%s'", wfId.c_str(), a.note.c_str(), a.item.c_str());
            break;
        }
        case WorkflowAction::Kind::sendDown: {
            // Pick the part the failed integration points at (a part whose file names appear in the failure; otherwise the most complex one),
            // give it the failure as its feedback, and void its acceptance so it is redone and the coordinator integrates again.
            const WorkflowRecord* wf = wfStore.find(wfId);
            if (!wf || !wf->plan) break;
            const std::filesystem::path sandbox{wf->spec.sandboxDir};
            std::ifstream in(sandbox / "items" / a.item / ("attempt-" + std::to_string(a.attempt)) / "gate.txt");
            std::stringstream ss;
            ss << in.rdbuf();
            const std::string failure = ss.str();
            const WorkItem* coordinatorItem = wf->plan->find(a.item);
            const WorkItem* pick = nullptr;
            int bestScore = -1;
            for (const auto& d : coordinatorItem ? coordinatorItem->deps : std::vector<std::string>{}) {
                for (const auto& it : wf->plan->items) {   // the part and every part below it (a part may itself have been divided)
                    if (it.persona != "developer" || (it.id != d && it.id.rfind(d + ".", 0) != 0)) continue;
                    int score = 0;
                    for (const auto& f : it.files) {
                        if (failure.find(f) != std::string::npos) score += 10;
                        const auto slash = f.rfind('/');
                        if (failure.find(slash == std::string::npos ? f : f.substr(slash + 1)) != std::string::npos) score += 5;
                    }
                    score = score * 10 + it.complexity;
                    if (score > bestScore) { bestScore = score; pick = &it; }
                }
            }
            if (!pick) break;
            int childAttempt = 1;
            for (const auto& j : store.jobs()) if (j.spec.workflow == wfId && j.spec.item == pick->id) childAttempt = std::max<int>(childAttempt, static_cast<int>(j.spec.attempt));
            std::ofstream o(sandbox / "items" / pick->id / "dispute.txt", std::ios::trunc);
            o << "Your part of '" << a.item << "' passed its own tests, but the integration of '" << a.item << "' (its tests over all the parts together) failed. Failures reported:\n"
              << failure << "Fix your part so the combined result passes; keep what already works.\n";
            o.close();
            json::Writer w;
            w.beginObject().kv("action", "disputed").key("reasons").beginArray().str("integration of '" + a.item + "' failed and points at '" + pick->id + "': it is redone").endArray()
                .key("inputs").beginObject().kv("item", pick->id).kv("attempt", childAttempt).kv("from", a.item).endObject().endObject();
            record("workflowDecision", wfbody::decision(wfId, pick->id, childAttempt, "disputed", "local", w.take()));
            logf("workflow %s: integration of '%s' failed: sent down to '%s' (attempt %d redone)", wfId.c_str(), a.item.c_str(), pick->id.c_str(), childAttempt + 1);
            break;
        }
        case WorkflowAction::Kind::finish:
            record("workflowFinished", wfbody::finished(wfId, a.finalState, a.note));
            logf("workflow %s finished: %s (%s)", wfId.c_str(), toString(a.finalState), a.note.c_str());
            break;
    }
}

void Daemon::Impl::advanceWorkflows()
{
    if (inAdvance || stopping) return;
    inAdvance = true;
    WorkflowSettings settings;
    settings.exe = opts.exe.string();
    settings.frontier = opts.frontier;
    for (int round = 0; round < 16; ++round) {
        bool acted = false;
        std::vector<std::string> ids;
        for (const auto& wf : wfStore.workflows()) if (wf.state == WorkflowState::running) ids.push_back(wf.id);
        for (const auto& id : ids) {
            const WorkflowRecord* wf = wfStore.find(id);
            if (!wf || wf->state != WorkflowState::running) continue;
            const auto actions = advance(*wf, store, settings, policy);  // copies what it needs: the store is mutated below
            for (const auto& a : actions) { executeWorkflowAction(id, a); acted = true; }
        }
        if (!acted) break;
    }
    inAdvance = false;
    scheduleQueued();
}

std::string Daemon::Impl::doWorkflowSubmit(const std::string& specJson)
{
    if (!journalOk) return errorReply("journal is failing: not accepting new work");
    auto spec = WorkflowSpec::fromJson(specJson);
    if (!spec) return errorReply(spec.error());
    const std::string id = wfStore.nextId();
    if (spec->sandboxDir.empty()) spec->sandboxDir = std::filesystem::absolute(opts.paths.workflowsDir() / id).string();
    std::error_code ec;
    std::filesystem::create_directories(spec->sandboxDir, ec);
    if (ec) return errorReply("cannot create the sandbox " + spec->sandboxDir + ": " + ec.message());
    record("workflowSubmitted", wfbody::submitted(id, *spec));
    logf("workflow %s submitted: task '%s', sandbox %s", id.c_str(), spec->task.c_str(), spec->sandboxDir.c_str());
    if (!isKnownTask(spec->task)) {  // no blueprint: the architect role is deferred as a file handoff
        TriageResult t = triage(spec->taskText.empty() ? spec->task : spec->taskText);
        const std::filesystem::path packet = std::filesystem::path{spec->sandboxDir} / "needs-plan.md";
        std::ofstream(packet) << needsPlanPacket(spec->taskText.empty() ? spec->task : spec->taskText, t, id);
        record("workflowAwaitingPlan", wfbody::awaitingPlan(id, packet.string()));
        logf("workflow %s awaits a plan: %s", id.c_str(), packet.c_str());
    }
    advanceWorkflows();
    json::Writer w;
    w.beginObject().kvBool("ok", true).kv("id", id).kv("sandbox", spec->sandboxDir).endObject();
    return w.take();
}

std::string Daemon::Impl::doWorkflowPlan(const std::string& id, const std::string& planJson)
{
    const WorkflowRecord* wf = wfStore.find(id);
    if (!wf) return errorReply("no such workflow: " + id);
    if (wf->state != WorkflowState::awaitingPlan) return errorReply("workflow " + id + " is " + toString(wf->state) + ", not awaiting a plan");
    auto plan = Plan::fromJson(planJson);
    if (!plan) return errorReply("plan rejected: " + plan.error());
    { std::ofstream o(std::filesystem::path{wf->spec.sandboxDir} / "plan.json", std::ios::trunc); o << plan->toJson(); }
    record("workflowPlanned", wfbody::planned(id, *plan));
    logf("workflow %s: plan supplied, %zu item(s)", id.c_str(), plan->items.size());
    advanceWorkflows();
    json::Writer w;
    w.beginObject().kvBool("ok", true).kv("id", id).kv("items", static_cast<std::int64_t>(plan->items.size())).endObject();
    return w.take();
}

std::string Daemon::Impl::doWorkflowCancel(const std::string& id)
{
    const WorkflowRecord* wf = wfStore.find(id);
    if (!wf) return errorReply("no such workflow: " + id);
    if (wf->state != WorkflowState::running && wf->state != WorkflowState::awaitingPlan) return errorReply("workflow " + id + " is already " + toString(wf->state));
    if (wf->state == WorkflowState::awaitingPlan) {
        record("workflowFinished", wfbody::finished(id, WorkflowState::cancelled, "cancelled while awaiting a plan"));
        json::Writer w;
        w.beginObject().kvBool("ok", true).kv("id", id).endObject();
        return w.take();
    }
    record("workflowCancelRequested", wfbody::cancelRequested(id));
    advanceWorkflows();
    json::Writer w;
    w.beginObject().kvBool("ok", true).kv("id", id).endObject();
    return w.take();
}

}  // namespace sub0squad
