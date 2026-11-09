#include "sub0squad/job_store.hpp"

#include "json_util.hpp"

#include <cstdio>

namespace sub0squad {

std::string_view toString(JobState s)
{
    switch (s) {
        case JobState::queued: return "queued";
        case JobState::running: return "running";
        case JobState::cancelling: return "cancelling";
        case JobState::succeeded: return "succeeded";
        case JobState::failed: return "failed";
        case JobState::cancelled: return "cancelled";
        case JobState::lost: return "lost";
    }
    return "?";
}

std::optional<JobState> jobStateFromString(std::string_view s)
{
    for (auto st : {JobState::queued, JobState::running, JobState::cancelling, JobState::succeeded, JobState::failed, JobState::cancelled, JobState::lost})
        if (toString(st) == s) return st;
    return std::nullopt;
}

namespace journalbody {
std::string submitted(const std::string& id, const JobSpec& spec)
{
    json::Writer w;
    w.beginObject().kv("id", id).kvRaw("spec", spec.toJson()).endObject();
    return w.take();
}
std::string started(const std::string& id, int run, std::int64_t shimPid, std::uint64_t shimStartTicks)
{
    json::Writer w;
    w.beginObject().kv("id", id).kv("run", run).kv("shimPid", shimPid).kv("shimStart", static_cast<std::int64_t>(shimStartTicks)).endObject();
    return w.take();
}
std::string cancelRequested(const std::string& id, int run)
{
    json::Writer w;
    w.beginObject().kv("id", id).kv("run", run).endObject();
    return w.take();
}
std::string finished(const std::string& id, int run, JobState state, std::string_view exitJson, std::string_view note)
{
    json::Writer w;
    w.beginObject().kv("id", id).kv("run", run).kv("state", toString(state)).kv("note", note);
    if (!exitJson.empty()) w.kvRaw("exit", exitJson);
    w.endObject();
    return w.take();
}
std::string resumed(const std::string& id, int run)
{
    json::Writer w;
    w.beginObject().kv("id", id).kv("run", run).endObject();
    return w.take();
}
}  // namespace journalbody

const JobRecord* JobStore::find(std::string_view id) const
{
    for (const auto& j : jobs_) if (j.id == id) return &j;
    return nullptr;
}
JobRecord* JobStore::find(std::string_view id)
{
    for (auto& j : jobs_) if (j.id == id) return &j;
    return nullptr;
}

std::string JobStore::nextId() const
{
    unsigned long long maxN = 0;
    for (const auto& j : jobs_) {
        unsigned long long n = 0;
        if (std::sscanf(j.id.c_str(), "j-%llu", &n) == 1 && n > maxN) maxN = n;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "j-%06llu", maxN + 1);
    return buf;
}

void JobStore::apply(const JournalRecord& rec)
{
    if (rec.type != "jobSubmitted" && rec.type != "jobStarted" && rec.type != "jobCancelRequested" && rec.type != "jobFinished" &&
        rec.type != "jobResumed")
        return;
    std::string id, state, note, exitJson, specJson;
    std::int64_t run = 1, shimPid = 0, shimStart = 0;
    try {
        json::withObject(rec.body, [&](simdjson::ondemand::object& obj) {
            for (auto field : obj) {
                const std::string_view key = field.unescaped_key();
                auto value = field.value();
                if (key == "id") id = json::toString(value);
                else if (key == "run") run = value.get_int64();
                else if (key == "shimPid") shimPid = value.get_int64();
                else if (key == "shimStart") shimStart = value.get_int64();
                else if (key == "state") state = json::toString(value);
                else if (key == "note") note = json::toString(value);
                else if (key == "exit") { std::string_view raw = value.raw_json(); exitJson.assign(raw); }
                else if (key == "spec") { std::string_view raw = value.raw_json(); specJson.assign(raw); }
            }
        });
    } catch (const simdjson::simdjson_error&) {
        return;  // a record we cannot read changes nothing
    }
    if (rec.type == "jobSubmitted") {
        auto spec = JobSpec::fromJson(specJson);
        if (!spec) return;
        JobRecord j;
        j.id = id;
        j.spec = std::move(*spec);
        j.submittedAt = rec.ts;
        jobs_.push_back(std::move(j));
        return;
    }
    JobRecord* j = find(id);
    if (!j) return;
    if (rec.type == "jobStarted") {
        j->run = static_cast<int>(run);
        j->state = JobState::running;
        j->shimPid = shimPid;
        j->shimStartTicks = static_cast<std::uint64_t>(shimStart);
        j->startedAt = rec.ts;
    } else if (rec.type == "jobCancelRequested") {
        if (!isTerminal(j->state)) j->state = j->state == JobState::queued ? JobState::cancelled : JobState::cancelling;
        if (j->state == JobState::cancelled) { j->finishedAt = rec.ts; j->note = "cancelled while queued"; }
    } else if (rec.type == "jobFinished") {
        if (auto st = jobStateFromString(state)) j->state = *st;
        j->exitJson = exitJson;
        j->note = note;
        j->finishedAt = rec.ts;
    } else if (rec.type == "jobResumed") {
        j->run = static_cast<int>(run);
        j->state = JobState::queued;
        j->shimPid = 0;
        j->shimStartTicks = 0;
        j->exitJson.clear();
        j->note.clear();
        j->finishedAt = 0;
    }
}

}  // namespace sub0squad
