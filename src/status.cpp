#include "sub0squad/status.hpp"

#include "json_util.hpp"
#include "sub0squad/process_tree.hpp"
#include "sub0squad/progress.hpp"

#include <chrono>

namespace sub0squad {

namespace {
double wallNow() { using namespace std::chrono; return duration<double>(system_clock::now().time_since_epoch()).count(); }
}  // namespace

JobState stateFromExit(const ExitRecord& exit, std::string& note, bool cancelRequested)
{
    const bool clean = exit.teardown == "verified";
    if (!clean) note = "teardown left survivors";
    if (exit.outcome == "cancelled") {
        if (!clean) return JobState::failed;
        note = "cancelled";
        return JobState::cancelled;
    }
    if (exit.outcome == "timeout") { note = "wall time budget exceeded" + std::string{clean ? "" : "; teardown left survivors"}; return JobState::failed; }
    if (exit.outcome == "startFailed") { note = "could not start"; return JobState::failed; }
    if (exit.signal != 0) { note = "killed by signal " + std::to_string(exit.signal) + (clean ? "" : "; teardown left survivors"); return cancelRequested ? JobState::cancelled : JobState::failed; }
    if (exit.code == 0) { if (!clean) return JobState::failed; note = "exit 0"; return JobState::succeeded; }
    note = "exit code " + std::to_string(exit.code) + (clean ? "" : "; teardown left survivors");
    return JobState::failed;
}

Effective reconcile(const RuntimePaths& paths, const JobRecord& job)
{
    Effective e;
    e.state = job.state;
    e.note = job.note;
    e.exitJson = job.exitJson;
    if (job.state != JobState::running && job.state != JobState::cancelling) return e;
    const auto runDir = paths.runDir(job.id, job.run);
    if (auto exit = readExitRecord(runDir)) {
        e.state = stateFromExit(*exit, e.note, job.state == JobState::cancelling);
        e.exitJson = exit->raw;
        e.fromExitRecord = true;
        return e;
    }
    e.shimAlive = job.shimPid > 0 && proc::isAlive(static_cast<pid_t>(job.shimPid), job.shimStartTicks);
    if (!e.shimAlive) {
        // The shim is gone and left no outcome: never report `running`.
        const auto pr = readProgress(runDir, job.spec.adapter, job.spec.cwd);
        e.state = JobState::lost;
        e.note = "supervisor vanished without an exit record";
        if (pr.hasProgress) e.note += "; last progress " + std::to_string(pr.done) + "/" + std::to_string(pr.total);
    }
    return e;
}

std::string jobJson(const RuntimePaths& paths, const JobRecord& job, const Effective& eff)
{
    const double now = wallNow();
    json::Writer w;
    w.beginObject().kv("id", job.id).kv("state", toString(eff.state)).kv("run", job.run).kv("tool", job.spec.tool).kv("tier", job.spec.tier)
        .kv("owner", job.spec.owner).kv("note", eff.note);
    w.kv("submittedAt", job.submittedAt).kv("startedAt", job.startedAt).kv("finishedAt", job.finishedAt);
    const double end = isTerminal(eff.state) && job.finishedAt > 0 ? job.finishedAt : now;
    w.kv("elapsedSec", job.startedAt > 0 ? end - job.startedAt : 0.0);
    w.key("argv").beginArray();
    for (const auto& a : job.spec.argv) w.str(a);
    w.endArray();
    w.kv("resumable", !job.spec.resumeArgv.empty());
    w.kv("shimPid", static_cast<std::int64_t>(job.shimPid));
    if (job.startedAt > 0) {
        const auto pr = readProgress(paths.runDir(job.id, job.run), job.spec.adapter, job.spec.cwd);
        w.key("progress");
        if (pr.hasProgress) w.beginObject().kv("done", pr.done).kv("total", pr.total).kv("phase", pr.phase).endObject();
        else w.null();
        w.kv("lastEventAt", pr.lastEventAt);
        w.kv("lastEventAgeSec", pr.lastEventAt > 0 ? now - pr.lastEventAt : -1.0);
        w.kv("lastNote", pr.lastNote);
        if (!pr.adapterSchema.empty()) w.key("adapter").beginObject().kv("schema", pr.adapterSchema).kv("status", pr.adapterStatus).endObject();
        w.key("metrics").beginObject();
        for (const auto& [name, v] : pr.metrics) w.kv(name, v);
        w.endObject();
    }
    if (!eff.exitJson.empty()) w.kvRaw("exit", eff.exitJson);
    w.endObject();
    return w.take();
}

std::string statusJson(const RuntimePaths& paths, const JobStore& store, const std::string& onlyId, const char* source)
{
    json::Writer w;
    w.beginObject().kvBool("ok", true).kv("asOf", wallNow()).kv("source", source).key("jobs").beginArray();
    for (const auto& job : store.jobs()) {
        if (!onlyId.empty() && job.id != onlyId) continue;
        w.raw(jobJson(paths, job, reconcile(paths, job)));
    }
    w.endArray().endObject();
    return w.take();
}

}  // namespace sub0squad
