#include "sub0squad/workflow_status.hpp"

#include "json_util.hpp"
#include "sub0squad/harness_common.hpp"

#include <filesystem>

#include <chrono>

namespace sub0squad {

std::string workflowStatusJson(const RuntimePaths& paths, const WorkflowStore& workflows, const JobStore& jobs, const std::string& onlyId, const char* source)
{
    using namespace std::chrono;
    const double now = duration<double>(system_clock::now().time_since_epoch()).count();
    const auto ledger = readLedger(paths.ledgerFile());
    json::Writer w;
    w.beginObject().kvBool("ok", true).kv("asOf", now).kv("source", source).key("workflows").beginArray();
    for (const auto& wf : workflows.workflows()) {
        if (!onlyId.empty() && wf.id != onlyId) continue;
        w.beginObject().kv("id", wf.id).kv("task", wf.spec.task).kv("taskText", wf.spec.taskText).kv("seed", "").kv("state", toString(wf.state)).kv("note", wf.note).kv("sandbox", wf.spec.sandboxDir)
            .kv("firstCause", firstCause(wf))
            .kv("process", wf.spec.process.name + "@" + std::to_string(wf.spec.process.version)).kv("submittedAt", wf.submittedAt).kv("finishedAt", wf.finishedAt);
        w.key("engines").beginArray();
        for (const auto& e : wf.spec.engines) w.str(e);
        w.endArray();
        w.key("requests").beginArray();
        for (const auto& r : wf.requests) {
            w.beginObject().kv("id", r.id).kv("kind", r.kind).kv("from", r.from).kv("title", r.title).kv("detail", r.detail).kv("attempt", r.attempt).kvBool("blocking", r.blocking)
                .kv("status", r.status).kv("triage", r.triage).kv("analysis", r.analysis).kv("answer", r.answer).kv("resolution", r.resolution).kv("answeredBy", r.answeredBy).kv("raisedAt", r.raisedAt).kv("answeredAt", r.answeredAt);
            w.key("scope").beginArray();
            for (const auto& x : r.scope) w.str(x);
            w.endArray().key("options").beginArray();
            for (const auto& x : r.options) w.str(x);
            w.endArray().endObject();
        }
        w.endArray();
        w.key("reasons").beginArray();
        if (wf.plan) for (const auto& r : wf.plan->reasons) w.str(r);
        w.endArray();
        int jobsTotal = 0, jobsActive = 0;
        for (const auto& j : jobs.jobs()) if (j.spec.workflow == wf.id) { ++jobsTotal; if (!isTerminal(j.state)) ++jobsActive; }
        w.kv("jobs", jobsTotal).kv("jobsActive", jobsActive);
        w.key("stages").beginArray();   // the process's stages as derived now: which is active, and the condition a waiting one waits on
        for (const auto& st : stageStatuses(wf, jobs)) w.beginObject().kv("name", st.name).kv("kind", st.kind).kv("state", st.state).kv("waitingOn", st.waitingOn).endObject();
        w.endArray();
        w.key("items").beginArray();
        for (const auto& it : itemStatuses(wf, jobs)) {
            const WorkItem* pi = wf.plan ? wf.plan->find(it.id) : nullptr;
            double tin = 0, tout = 0, dur = 0;
            std::string engine;
            for (const auto& r : ledger) if (r.workflow == wf.id && r.item == it.id) { tin += r.tokensIn; tout += r.tokensOut; dur += r.durationSec; if (!r.engine.empty()) engine = r.engine; }
            const auto dot = it.id.rfind('.');
            w.beginObject().kv("id", it.id).kv("title", it.title).kv("status", it.status).kv("attempts", it.attempts).kv("tier", it.tier)
                .kv("builtBy", it.status != "accepted" ? std::string{} : it.tier == "frontier-stub" ? std::string{"stub"} : pi && pi->persona == "integrator" ? std::string{"parts"} : wf.spec.agent == "mock" ? std::string{"mock"} : std::string{"model"})
                .kv("persona", pi ? pi->persona : std::string{}).kv("parent", dot == std::string::npos ? std::string{} : it.id.substr(0, dot))
                .kv("tokensIn", tin).kv("tokensOut", tout).kv("durationSec", dur).kv("engine", engine);
            // Mends the harness applied to this item (mends.hpp): `mends` as they were decided; `healed` once the item is accepted after them.
            w.key("mends").beginArray();
            for (const auto& d : wf.decisions) if (d.item == it.id && d.action == "mend") w.str(d.tier);
            // Why the latest attempt failed (catalogue row 72): the record its agent, its gate or the daemon left in the attempt's directory.
            // It is the latest attempt that HAS a record: while attempt N+1 runs, what an observer wants is why attempt N failed.
            std::string why;
            int whyAttempt = 0;
            if (it.status != "accepted")
                for (int n = it.attempts; n >= 1 && why.empty(); --n) { why = harnessutil::attemptFailureJson(std::filesystem::path{wf.spec.sandboxDir} / "items" / it.id / ("attempt-" + std::to_string(n))); whyAttempt = n; }
            if (!why.empty()) w.endArray().kv("lastFailureAttempt", whyAttempt).kvRaw("lastFailure", why).key("healed").beginArray();
            else w.endArray().key("healed").beginArray();
            if (it.status == "accepted") for (const auto& d : wf.decisions) if (d.item == it.id && d.action == "mend") w.str(d.tier);
            w.endArray();
            w.key("deps").beginArray();
            if (pi) for (const auto& d : pi->deps) w.str(d);
            w.endArray().endObject();
        }
        w.endArray().key("decisions").beginArray();
        for (const auto& d : wf.decisions) w.raw(d.json);
        w.endArray();
        const LedgerSummary s = summarise(ledger, wf.id);
        w.key("ledger").beginObject().key("byTier").beginObject();
        for (const auto& [tier, t] : s.byTier)
            w.key(tier).beginObject().kv("attempts", t.attempts).kv("tokensIn", t.tokensIn).kv("tokensOut", t.tokensOut).kv("costUnits", t.costUnits).kv("durationSec", t.durationSec).endObject();
        w.endObject().key("byEngine").beginObject();
        for (const auto& [name, e] : s.byEngine)
            w.key(name).beginObject().kv("attempts", e.attempts).kv("tokensIn", e.tokensIn).kv("tokensOut", e.tokensOut)
                .kv("decodeTps", e.decodeSamples ? e.decodeTpsSum / e.decodeSamples : 0.0).endObject();
        w.endObject().kv("frontierTokenShare", s.frontierTokenShare).kv("workRatio", s.workRatio).kv("totalTokens", s.totalTokens)
            .kv("acceptedWorkTokens", s.acceptedWorkTokens).kv("reworkTokens", s.reworkTokens).kv("promptOverheadTokens", s.promptOverheadTokens)
            .kv("harnessBytes", s.harnessBytes).kvBool("stub", true).endObject();
        w.endObject();
    }
    w.endArray().endObject();
    return w.take();
}

}  // namespace sub0squad
