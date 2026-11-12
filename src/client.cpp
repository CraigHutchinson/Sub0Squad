#include "sub0squad/client.hpp"

#include "json_util.hpp"
#include "sub0squad/daemon.hpp"
#include "sub0squad/frame.hpp"
#include "sub0squad/plan_lint.hpp"
#include "sub0squad/portal.hpp"
#include "sub0squad/qa.hpp"
#include "sub0squad/recall.hpp"
#include "sub0squad/replay.hpp"
#include "sub0squad/job_store.hpp"
#include "sub0squad/journal.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/status.hpp"
#include "sub0squad/triage.hpp"
#include "sub0squad/workflow.hpp"
#include "sub0squad/workflow_status.hpp"
#include "sub0squad/unix_socket.hpp"

#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sub0squad {

ControlClient::~ControlClient() { if (fd_ >= 0) ::close(fd_); }

std::expected<ControlClient, ControlClient::ConnectError> ControlClient::connect(const RuntimePaths& paths)
{
    auto r = connectUnix(paths.controlSocket());
    if (r.fd < 0) return std::unexpected(ConnectError{r.error, r.noDaemon});
    ControlClient c;
    c.fd_ = r.fd;
    return c;
}

void ControlClient::setReceiveTimeout(int seconds)
{
    timeval tv{seconds, 0};
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

std::expected<std::string, std::string> ControlClient::request(std::string_view cmd, std::string_view extra)
{
    std::string body = "{\"v\":1,\"cmd\":\"" + std::string{cmd} + "\"";
    if (!extra.empty()) { body += ","; body += extra; }
    body += "}";
    if (auto w = writeFrameBlocking(fd_, body); !w) return std::unexpected(w.error());
    return readFrameBlocking(fd_);
}

namespace {

constexpr const char* kUsage =
    "usage: sub0squad <command> [options]\n"
    "  serve [--max-jobs N] [--mem-shed-mib N]   run the daemon (foreground); sheds jobs below N MiB available memory (default 1536, 0 = off)\n"
    "  submit [opts] -- <cmd> [args...]     submit a process job; prints its id\n"
    "         --cwd DIR  --wall SEC  --memory-mib N (refused at submit if the host lacks it)  --grace SEC  --tool NAME  --tier stub|local  --env K=V  --lease RESOURCE=CAP (at most CAP running jobs hold RESOURCE)  --service (a server: not counted against --max-jobs)\n"
    "         --resume-arg ARG (repeatable: the command `resume` runs)  --state-file PATH --state-schema NAME\n"
    "         --spec FILE (a job-spec-1 JSON file instead of a command)\n"
    "  status [ID] [--json]                 jobs and progress (works without the daemon, from the journal)\n"
    "  logs ID [--stderr] [--follow] [--run N]\n"
    "  cancel ID | resume ID | wait ID [--timeout SEC] | ping | stop\n"
    "  do \"<task in words>\" [project options]   triage first, then run: a known blueprint starts at once, otherwise a needs-plan.md handoff is written\n"
    "  project submit --task calculator-basic|calculator-scientific|calculator-web|calculator-cpp|calculator-py [--sandbox DIR] [--inject ITEM[:N]]... [--max-local N]\n"
    "         [--agent mock|llm] [--engine NAME=http://127.0.0.1:PORT/v1[,model=..][,ctx=N][,slots=N]]... [--engine-for PERSONA=NAME]... [--max-tokens N] [--treatment baseline|index|planfirst] [--executor session|canvas|fork] [--repair-turns N] [--size-budget TOKENS] [--fallback-engine NAME] [--ask]\n"
    "  project plan ID --file plan.json     supply the plan a workflow is awaiting (the deferred architect's answer)\n"
    "  request list [WF] | raise WF --kind K [--scope a,b] [--blocking] [--title T] [--detail D] [--option O]... | answer WF RID [--resolution retry|stop] [--answer TEXT]\n"
    "                                       the inbox: questions, clarifications, requirements, blockers, proposals, escalations\n"
    "  replay WF NODE [--seeds 1,2,3] [--think] [--decompose]   run one node again in isolation, several seeds side by side (minutes, not a whole workflow)\n"
    "  qa DIR|WORKFLOW                      quality findings over sources: duplicated code, the same helper declared twice, very long functions\n"
    "  recall WF words... [--item NODE] [--limit N]   search what a workflow already did: conversations, replies, gate outputs, briefings\n"
    "  plan-lint FILE [--tps N]             audit a plan: width, critical path, rough time, serial tails\n"
    "  portal [--port N] [--engine-url URL]  read-only status portal on 127.0.0.1 (default 8780): the work graph, tokens per node, daemon jobs\n"
    "  guard [--memory-mib N] [--reserve-mib R] [--disk-path P --disk-mib N]   explicit host guards (exit 1 when refused)\n"
    "  fetch-model --url https://huggingface.co/... --out FILE [--sha256 H]   guarded, resumable, verified download (run it as a job)\n"
    "  blueprint NAME                       print a built-in plan-2 (calculator-web, calculator-cpp, calculator-py)\n"
    "  project status [ID] [--json] | project cancel ID | project wait ID [--timeout SEC]\n"
    "  serve also takes --frontier off|stub (default stub: the escalation path runs, nothing is sent)\n"
    "  --selftest | --version\n";

RuntimePaths pathsOrDie() { return RuntimePaths::fromEnvironment(); }

std::string slurpFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---- JSON reply helpers --------------------------------------------------------------------------------------------
struct Reply { bool ok = false; std::string error; std::string raw; };

Reply parseReply(const std::string& text)
{
    Reply r;
    r.raw = text;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                if (k == "ok") r.ok = f.value().get_bool();
                else if (k == "error") r.error = json::toString(f.value());
            }
        });
    } catch (const simdjson::simdjson_error& e) { r.error = std::string{"unreadable reply: "} + e.what(); }
    return r;
}

std::string fmtDuration(double sec)
{
    char buf[32];
    if (sec < 90) std::snprintf(buf, sizeof buf, "%.0fs", sec);
    else if (sec < 5400) std::snprintf(buf, sizeof buf, "%.0fm%02.0fs", sec / 60, static_cast<double>(static_cast<long>(sec) % 60));
    else std::snprintf(buf, sizeof buf, "%.1fh", sec / 3600);
    return buf;
}

void printJobs(const std::string& text)
{
    std::printf("%-9s %-10s %-5s %-14s %-8s %s\n", "ID", "STATE", "RUN", "PROGRESS", "ELAPSED", "NOTE");
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                if (json::keyOf(f) != "jobs") continue;
                for (auto jobV : f.value().get_array()) {
                    std::string id, state, note, phase;
                    std::int64_t run = 1, done = -1, total = 0;
                    double elapsed = 0;
                    for (auto jf : jobV.get_object()) {
                        const std::string_view k = jf.unescaped_key();
                        auto v = jf.value();
                        if (k == "id") id = json::toString(v);
                        else if (k == "state") state = json::toString(v);
                        else if (k == "note") note = json::toString(v);
                        else if (k == "run") run = v.get_int64();
                        else if (k == "elapsedSec") elapsed = v.get_double();
                        else if (k == "progress") {
                            if (v.type() == simdjson::ondemand::json_type::object)
                                for (auto pf : v.get_object()) {
                                    const std::string_view pk = pf.unescaped_key();
                                    if (pk == "done") done = pf.value().get_int64();
                                    else if (pk == "total") total = pf.value().get_int64();
                                    else if (pk == "phase") phase = json::toString(pf.value());
                                }
                        }
                    }
                    const std::string prog = done >= 0 ? std::to_string(done) + "/" + std::to_string(total) + (phase.empty() ? "" : " " + phase) : "-";
                    std::printf("%-9s %-10s %-5lld %-14s %-8s %s\n", id.c_str(), state.c_str(), static_cast<long long>(run), prog.c_str(), fmtDuration(elapsed).c_str(), note.c_str());
                }
            }
        });
    } catch (const simdjson::simdjson_error& e) { std::fprintf(stderr, "cannot read status reply: %s\n", e.what()); }
}

// A reply for status either from the daemon or, when none runs, from the journal and the files the shims left.
std::expected<std::string, std::string> statusAnyway(const RuntimePaths& paths, const std::string& id, bool& offline)
{
    offline = false;
    auto client = ControlClient::connect(paths);
    if (client) {
        std::string extra = id.empty() ? "" : "\"id\":\"" + id + "\"";
        return client->request("status", extra);
    }
    if (!client.error().noDaemon) return std::unexpected(client.error().message);
    offline = true;
    auto replay = Journal::replay(paths.journalDir());
    if (!replay) return std::unexpected(replay.error());
    JobStore store;
    for (const auto& rec : replay->records) store.apply(rec);
    return statusJson(paths, store, id, "journal");
}

struct JobSnapshot { std::string state; int run = 1; bool found = false; };

JobSnapshot lookup(const RuntimePaths& paths, const std::string& id)
{
    JobSnapshot s;
    auto replay = Journal::replay(paths.journalDir());
    if (!replay) return s;
    JobStore store;
    for (const auto& rec : replay->records) store.apply(rec);
    if (const JobRecord* j = store.find(id)) {
        s.found = true;
        s.run = j->run;
        s.state = std::string{toString(reconcile(paths, *j).state)};
    }
    return s;
}

int cmdLogs(const RuntimePaths& paths, int argc, char** argv)
{
    if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
    const std::string id = argv[0];
    bool err = false, follow = false;
    int runOverride = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--stderr") err = true;
        else if (a == "--follow" || a == "-f") follow = true;
        else if (a == "--run" && i + 1 < argc) runOverride = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "logs: unknown option %s\n", a.c_str()); return 2; }
    }
    JobSnapshot snap = lookup(paths, id);
    if (!snap.found) { std::fprintf(stderr, "no such job: %s\n", id.c_str()); return 1; }
    const int run = runOverride ? runOverride : snap.run;
    const std::string path = (paths.runDir(id, run) / (err ? "stderr.log" : "stdout.log")).string();
    std::streamoff offset = 0;
    const int ino = ::inotify_init1(IN_CLOEXEC);
    if (follow && ino >= 0) ::inotify_add_watch(ino, paths.runDir(id, run).c_str(), IN_MODIFY | IN_CREATE | IN_CLOSE_WRITE);
    for (;;) {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            in.seekg(offset);
            char buf[8192];
            while (in.read(buf, sizeof buf) || in.gcount() > 0) {
                std::fwrite(buf, 1, static_cast<std::size_t>(in.gcount()), stdout);
                offset += in.gcount();
            }
            std::fflush(stdout);
        }
        if (!follow) break;
        if (isTerminal(*jobStateFromString(lookup(paths, id).state))) {  // final drain after the job ended
            std::ifstream last(path, std::ios::binary);
            last.seekg(offset);
            char buf[8192];
            while (last.read(buf, sizeof buf) || last.gcount() > 0) std::fwrite(buf, 1, static_cast<std::size_t>(last.gcount()), stdout);
            break;
        }
        pollfd p{ino, POLLIN, 0};
        if (ino >= 0 && ::poll(&p, 1, 1000) > 0) { char junk[4096]; (void)!::read(ino, junk, sizeof junk); }
        else if (ino < 0) ::usleep(500000);
    }
    if (ino >= 0) ::close(ino);
    return 0;
}

int simpleCommand(const RuntimePaths& paths, const char* cmd, const std::string& id, bool printJson)
{
    auto client = ControlClient::connect(paths);
    if (!client) { std::fprintf(stderr, "%s%s\n", client.error().message.c_str(), client.error().noDaemon ? " (is `sub0squad serve` running?)" : ""); return 3; }
    auto reply = client->request(cmd, id.empty() ? "" : "\"id\":\"" + id + "\"");
    if (!reply) { std::fprintf(stderr, "%s\n", reply.error().c_str()); return 1; }
    const Reply r = parseReply(*reply);
    if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
    if (printJson || std::string_view{cmd} == "ping") std::printf("%s\n", reply->c_str());
    else std::printf("%s: ok\n", cmd);
    return 0;
}

int cmdSubmit(const RuntimePaths& paths, int argc, char** argv)
{
    JobSpec spec;
    std::string specFile;
    int i = 0;
    for (; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--") { ++i; break; }
        else if (a == "--cwd") spec.cwd = val();
        else if (a == "--wall") spec.wallSeconds = std::atoll(val().c_str());
        else if (a == "--memory-mib") spec.memoryMiB = std::atoll(val().c_str());
        else if (a == "--service") spec.service = true;
        else if (a == "--lease") { const std::string v = val(); const auto eq = v.rfind('='); if (eq == std::string::npos || eq == 0) { std::fprintf(stderr, "--lease wants RESOURCE=CAP\n"); return 2; } spec.leaseResource = v.substr(0, eq); spec.leaseCap = std::max(1, std::atoi(v.c_str() + eq + 1)); }
        else if (a == "--grace") spec.graceSeconds = std::atoll(val().c_str());
        else if (a == "--tool") spec.tool = val();
        else if (a == "--tier") spec.tier = val();
        else if (a == "--env") { std::string kv = val(); auto eq = kv.find('='); if (eq != std::string::npos) spec.env[kv.substr(0, eq)] = kv.substr(eq + 1); }
        else if (a == "--resume-arg") spec.resumeArgv.push_back(val());
        else if (a == "--state-file") { spec.adapter.type = "state-file"; spec.adapter.path = val(); }
        else if (a == "--state-schema") spec.adapter.schema = val();
        else if (a == "--spec") specFile = val();
        else { std::fprintf(stderr, "submit: unknown option %s (put the command after --)\n", a.c_str()); return 2; }
    }
    if (!specFile.empty()) {
        auto parsed = JobSpec::fromJson(slurpFile(specFile));
        if (!parsed) { std::fprintf(stderr, "submit: %s\n", parsed.error().c_str()); return 2; }
        spec = *parsed;
    } else {
        for (; i < argc; ++i) spec.argv.emplace_back(argv[i]);
    }
    if (spec.cwd.empty()) { char buf[4096]; if (::getcwd(buf, sizeof buf)) spec.cwd = buf; }
    auto client = ControlClient::connect(paths);
    if (!client) { std::fprintf(stderr, "%s%s\n", client.error().message.c_str(), client.error().noDaemon ? " (is `sub0squad serve` running?)" : ""); return 3; }
    auto reply = client->request("submit", "\"spec\":" + spec.toJson());
    if (!reply) { std::fprintf(stderr, "%s\n", reply.error().c_str()); return 1; }
    const Reply r = parseReply(*reply);
    if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
    try {
        json::withObject(*reply, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) if (json::keyOf(f) == "id") std::printf("%s\n", json::toString(f.value()).c_str());
        });
    } catch (const simdjson::simdjson_error&) { return 1; }
    return 0;
}

int cmdWait(const RuntimePaths& paths, int argc, char** argv)
{
    if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
    const std::string id = argv[0];
    int timeout = 0;
    for (int i = 1; i + 1 < argc; ++i) if (std::string_view{argv[i]} == "--timeout") timeout = std::atoi(argv[i + 1]);
    auto client = ControlClient::connect(paths);
    if (!client) {  // no daemon: report what the journal and shim records say right now
        bool offline = false;
        auto st = statusAnyway(paths, id, offline);
        if (st) printJobs(*st);
        std::fprintf(stderr, "daemon not running: cannot wait\n");
        return 3;
    }
    if (timeout > 0) client->setReceiveTimeout(timeout);
    auto reply = client->request("wait", "\"id\":\"" + id + "\"");
    if (!reply) {
        const bool timedOut = timeout > 0 && reply.error().find("Resource temporarily unavailable") != std::string::npos;
        std::fprintf(stderr, "%s\n", timedOut ? "timed out waiting for the job to finish (it keeps running)" : reply.error().c_str());
        return timedOut ? 124 : 1;
    }
    const Reply r = parseReply(*reply);
    if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
    printJobs("{\"jobs\":[" + [&] {
        std::string out;
        try { json::withObject(*reply, [&](simdjson::ondemand::object& obj) { for (auto f : obj) if (json::keyOf(f) == "job") { std::string_view raw = f.value().raw_json(); out.assign(raw); } }); }
        catch (const simdjson::simdjson_error&) {}
        return out;
    }() + "]}");
    return reply->find("\"state\":\"succeeded\"") != std::string::npos ? 0 : 1;
}


std::string workflowStatusAnyway(const RuntimePaths& paths, const std::string& id, bool& offline)
{
    offline = false;
    auto client = ControlClient::connect(paths);
    if (client) {
        auto r = client->request("workflowStatus", id.empty() ? "" : "\"id\":\"" + id + "\"");
        return r ? *r : std::string{};
    }
    offline = true;
    auto replay = Journal::replay(paths.journalDir());
    if (!replay) return {};
    JobStore jobs;
    WorkflowStore wfs;
    for (const auto& rec : replay->records) { jobs.apply(rec); wfs.apply(rec); }
    return workflowStatusJson(paths, wfs, jobs, id, "journal");
}

void printWorkflows(const std::string& text)
{
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                if (json::keyOf(f) != "workflows") continue;
                for (auto wv : f.value().get_array()) {
                    std::string id, task, state, note, sandbox;
                    std::int64_t jobs = 0, active = 0;
                    double share = 0, workRatio = 0, acceptedWork = 0, rework = 0, totalTok = 0, overhead = 0, harnessBytes = 0;
                    std::vector<std::string> engines;
                    std::vector<std::string> reasons, items, decisions, tiers;
                    for (auto wf : wv.get_object()) {
                        const std::string_view k = wf.unescaped_key();
                        auto v = wf.value();
                        if (k == "id") id = json::toString(v);
                        else if (k == "task") task = json::toString(v);
                        else if (k == "state") state = json::toString(v);
                        else if (k == "note") note = json::toString(v);
                        else if (k == "sandbox") sandbox = json::toString(v);
                        else if (k == "jobs") jobs = v.get_int64();
                        else if (k == "jobsActive") active = v.get_int64();
                        else if (k == "reasons") { for (auto r : v.get_array()) reasons.push_back(json::toString(r.value())); }
                        else if (k == "items") {
                            for (auto iv : v.get_array()) {
                                std::string iid, title, status, tier; std::int64_t attempts = 0;
                                for (auto g : iv.get_object()) {
                                    const std::string_view gk = g.unescaped_key();
                                    auto gv = g.value();
                                    if (gk == "id") iid = json::toString(gv); else if (gk == "title") title = json::toString(gv);
                                    else if (gk == "status") status = json::toString(gv); else if (gk == "tier") tier = json::toString(gv);
                                    else if (gk == "attempts") attempts = gv.get_int64();
                                }
                                char buf[256];
                                std::snprintf(buf, sizeof buf, "  %-11s %-10s attempts %lld  last tier %-13s %s", iid.c_str(), status.c_str(), static_cast<long long>(attempts), tier.empty() ? "-" : tier.c_str(), title.c_str());
                                items.push_back(buf);
                            }
                        } else if (k == "decisions") {
                            for (auto dv : v.get_array()) {
                                std::string item, action, tier, why; std::int64_t attempt = 0;
                                for (auto g : dv.get_object()) {
                                    const std::string_view gk = g.unescaped_key();
                                    auto gv = g.value();
                                    if (gk == "action") action = json::toString(gv); else if (gk == "tier") tier = json::toString(gv);
                                    else if (gk == "reasons") { for (auto r : gv.get_array()) why = json::toString(r.value()); }  // the last reason is the deciding one
                                    else if (gk == "inputs") { for (auto h : gv.get_object()) { const std::string_view hk = h.unescaped_key(); if (hk == "item") item = json::toString(h.value()); else if (hk == "attempt") attempt = h.value().get_int64(); } }
                                }
                                decisions.push_back("  " + item + " attempt " + std::to_string(attempt) + ": " + action + (tier.empty() ? "" : " -> " + tier) + "  (" + why + ")");
                            }
                        } else if (k == "ledger") {
                            for (auto lf : v.get_object()) {
                                const std::string_view lk = lf.unescaped_key();
                                if (lk == "frontierTokenShare") share = lf.value().get_double();
                                else if (lk == "workRatio") workRatio = lf.value().get_double();
                                else if (lk == "acceptedWorkTokens") acceptedWork = lf.value().get_double();
                                else if (lk == "reworkTokens") rework = lf.value().get_double();
                                else if (lk == "totalTokens") totalTok = lf.value().get_double();
                                else if (lk == "promptOverheadTokens") overhead = lf.value().get_double();
                                else if (lk == "harnessBytes") harnessBytes = lf.value().get_double();
                                else if (lk == "byEngine") {
                                    for (auto en : lf.value().get_object()) {
                                        std::string name{std::string_view{en.unescaped_key()}};
                                        std::int64_t attempts = 0; double tin = 0, tout = 0, tps = 0;
                                        for (auto m : en.value().get_object()) {
                                            const std::string_view mk = m.unescaped_key();
                                            if (mk == "attempts") attempts = m.value().get_int64(); else if (mk == "tokensIn") tin = m.value().get_double();
                                            else if (mk == "tokensOut") tout = m.value().get_double(); else if (mk == "decodeTps") tps = m.value().get_double();
                                        }
                                        char buf[200];
                                        std::snprintf(buf, sizeof buf, "  engine %-14s %3lld attempt(s)  tokens in %.0f out %.0f  decode %.1f tok/s", name.c_str(), static_cast<long long>(attempts), tin, tout, tps);
                                        engines.emplace_back(buf);
                                    }
                                }
                                else if (lk == "byTier") {
                                    for (auto t : lf.value().get_object()) {
                                        std::string name{std::string_view{t.unescaped_key()}};
                                        std::int64_t attempts = 0; double tin = 0, tout = 0, cost = 0;
                                        for (auto m : t.value().get_object()) {
                                            const std::string_view mk = m.unescaped_key();
                                            if (mk == "attempts") attempts = m.value().get_int64(); else if (mk == "tokensIn") tin = m.value().get_double();
                                            else if (mk == "tokensOut") tout = m.value().get_double(); else if (mk == "costUnits") cost = m.value().get_double();
                                        }
                                        char buf[200];
                                        std::snprintf(buf, sizeof buf, "  %-14s %3lld job(s)  tokens in %.0f out %.0f  notional cost %.2f", name.c_str(), static_cast<long long>(attempts), tin, tout, cost);
                                        tiers.emplace_back(buf);
                                    }
                                }
                            }
                        }
                    }
                    std::printf("%s  %s  %s%s%s\n  sandbox %s   jobs %lld (%lld active)\n", id.c_str(), task.c_str(), state.c_str(), note.empty() ? "" : "  - ", note.c_str(), sandbox.c_str(),
                                static_cast<long long>(jobs), static_cast<long long>(active));
                    for (const auto& r : reasons) std::printf("  plan: %s\n", r.c_str());
                    if (!items.empty()) std::printf("items:\n");
                    for (const auto& i : items) std::printf("%s\n", i.c_str());
                    if (!decisions.empty()) std::printf("decisions:\n");
                    for (const auto& d : decisions) std::printf("%s\n", d.c_str());
                    if (!tiers.empty()) std::printf("ledger (notional, nothing was sent anywhere): frontier token share %.1f %%\n", share * 100.0);
                    for (const auto& t : tiers) std::printf("%s\n", t.c_str());
                    for (const auto& e : engines) std::printf("%s\n", e.c_str());
                    if (totalTok > 0)
                        std::printf("work vs harness (estimates): work ratio %.1f %% (accepted deliverable tokens %.0f of %.0f total), rework %.0f, prompt overhead %.0f, harness supplied %.0f bytes\n",
                                    workRatio * 100.0, acceptedWork, totalTok, rework, overhead, harnessBytes);
                }
            }
        });
    } catch (const simdjson::simdjson_error& e) { std::fprintf(stderr, "cannot read workflow status: %s\n", e.what()); }
}

bool workflowTerminal(const std::string& statusText, const std::string& id, std::string& state)
{
    state.clear();
    const std::string key = "\"id\":\"" + id + "\"";
    const auto at = statusText.find(key);
    if (at == std::string::npos) return false;
    const auto sp = statusText.find("\"state\":\"", at);
    if (sp == std::string::npos) return false;
    const auto start = sp + 9;
    state = statusText.substr(start, statusText.find('"', start) - start);
    return state != "running";
}


int parseWorkflowFlags(int argc, char** argv, int from, WorkflowSpec& spec)
{
    for (int i = from; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--task") spec.task = val();
        else if (a == "--sandbox") spec.sandboxDir = val();
        else if (a == "--inject") spec.inject.push_back(val());
        else if (a == "--max-local") spec.maxLocalAttempts = std::atoi(val().c_str());
        else if (a == "--agent") spec.agent = val();
        else if (a == "--engine") {
            auto p = EngineProfile::parse(val());
            if (!p) { std::fprintf(stderr, "--engine: %s\n", p.error().c_str()); return 2; }
            spec.engines.push_back(p->toArg());
        } else if (a == "--engine-for") { std::string kv = val(); const auto eq = kv.find('='); if (eq == std::string::npos) { std::fprintf(stderr, "--engine-for takes PERSONA=ENGINE\n"); return 2; } spec.route[kv.substr(0, eq)] = kv.substr(eq + 1); }
        else if (a == "--size-budget") spec.sizeBudget = std::atoi(val().c_str());
        else if (a == "--ask") spec.ask = true;
        else if (a == "--fallback-engine") spec.fallbackEngine = val();
        else if (a == "--max-tokens") spec.maxTokens = std::atoi(val().c_str());
        else if (a == "--treatment") spec.treatment = val();
        else if (a == "--executor") spec.executor = val();
        else if (a == "--repair-turns") spec.repairTurns = std::atoi(val().c_str());
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    return 0;
}

int cmdDo(const RuntimePaths& paths, int argc, char** argv)
{
    if (argc < 1 || argv[0][0] == '-') { std::fputs(kUsage, stderr); return 2; }
    const std::string text = argv[0];
    WorkflowSpec spec;
    if (int rc = parseWorkflowFlags(argc, argv, 1, spec)) return rc;
    const TriageResult t = triage(text);
    spec.task = t.blueprint.empty() ? "custom" : t.blueprint;
    spec.taskText = text;
    spec.triage = t.toJson();
    std::printf("triage: %s project, complexity %d, %s, architect: %s\n", t.projectType.c_str(), t.complexity, t.split ? "split into items" : "one item", t.architect.c_str());
    for (const auto& r : t.reasons) std::printf("  - %s\n", r.c_str());
    if (!spec.sandboxDir.empty() && spec.sandboxDir[0] != '/') { char b[4096]; if (::getcwd(b, sizeof b)) spec.sandboxDir = std::string{b} + "/" + spec.sandboxDir; }
    auto client = ControlClient::connect(paths);
    if (!client) { std::fprintf(stderr, "%s%s\n", client.error().message.c_str(), client.error().noDaemon ? " (is `sub0squad serve` running?)" : ""); return 3; }
    auto reply = client->request("workflowSubmit", "\"spec\":" + spec.toJson());
    if (!reply) { std::fprintf(stderr, "%s\n", reply.error().c_str()); return 1; }
    const Reply r = parseReply(*reply);
    if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
    try { json::withObject(*reply, [&](simdjson::ondemand::object& obj) { for (auto f : obj) if (json::keyOf(f) == "id") std::printf("workflow %s\n", json::toString(f.value()).c_str()); }); }
    catch (const simdjson::simdjson_error&) { return 1; }
    if (t.blueprint.empty()) std::printf("awaiting a plan: see needs-plan.md in the workflow sandbox, then `sub0squad project plan <id> --file plan.json`\n");
    return 0;
}

// `sub0squad request list|raise|answer`: the inbox of a workflow (questions, clarifications, requirements, blockers, proposals, escalations).
int cmdRequest(const RuntimePaths& paths, int argc, char** argv)
{
    if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
    const std::string sub = argv[0];
    if (sub == "list") {
        bool offline = false;
        const std::string text = workflowStatusAnyway(paths, argc > 1 ? argv[1] : "", offline);
        try {
            json::withObject(text, [&](simdjson::ondemand::object& obj) {
                for (auto f : obj) {
                    if (json::keyOf(f) != "workflows") continue;
                    for (auto w : f.value().get_array()) {
                        for (auto wf : w.get_object()) {
                            if (json::keyOf(wf) != "requests") continue;
                            for (auto r : wf.value().get_array()) {
                                std::string id, kind, from, title, status, answer;
                                bool blocking = false;
                                for (auto rf : r.get_object()) {
                                    const auto k = json::keyOf(rf);
                                    if (k == "id") id = json::toString(rf.value());
                                    else if (k == "kind") kind = json::toString(rf.value());
                                    else if (k == "from") from = json::toString(rf.value());
                                    else if (k == "title") title = json::toString(rf.value());
                                    else if (k == "status") status = json::toString(rf.value());
                                    else if (k == "answer") answer = json::toString(rf.value());
                                    else if (k == "blocking") blocking = rf.value().get_bool();
                                }
                                std::printf("%-5s %-8s %-13s %-8s%s  %s%s\n", id.c_str(), status.c_str(), kind.c_str(), from.c_str(), blocking ? " (blocking)" : "", title.c_str(), answer.empty() ? "" : ("  => " + answer).c_str());
                            }
                        }
                    }
                }
            });
        } catch (const simdjson::simdjson_error&) { std::fprintf(stderr, "cannot read requests\n"); return 1; }
        return 0;
    }
    if ((sub == "raise" && argc >= 2) || (sub == "answer" && argc >= 3)) {
        const std::string wf = argv[1];
        json::Writer w;
        w.beginObject();
        std::string kind = "question", from = "user", title, detail, answer, resolution;
        std::vector<std::string> scope, options;
        bool blocking = false;
        const int first = sub == "raise" ? 2 : 3;
        for (int i = first; i < argc; ++i) {
            const std::string a = argv[i];
            auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
            if (a == "--kind") kind = val(); else if (a == "--from") from = val(); else if (a == "--title") title = val(); else if (a == "--detail") detail = val();
            else if (a == "--answer") answer = val(); else if (a == "--resolution") resolution = val(); else if (a == "--blocking") blocking = true;
            else if (a == "--option") options.push_back(val());
            else if (a == "--scope") { std::stringstream ss(val()); for (std::string x; std::getline(ss, x, ',');) if (!x.empty()) scope.push_back(x); }
        }
        if (sub == "raise") {
            w.kv("kind", kind).kv("from", from).kv("title", title).kv("detail", detail).kvBool("blocking", blocking);
            w.key("scope").beginArray();
            for (const auto& s : scope) w.str(s);
            w.endArray().key("options").beginArray();
            for (const auto& o : options) w.str(o);
            w.endArray();
        } else w.kv("rid", argv[2]).kv("answer", answer).kv("resolution", resolution).kv("by", "user");
        w.endObject();
        auto client = ControlClient::connect(paths);
        if (!client) { std::fprintf(stderr, "%s\n", client.error().message.c_str()); return 3; }
        auto reply = client->request(sub == "raise" ? "requestRaise" : "requestAnswer", "\"id\":\"" + wf + "\",\"spec\":" + w.take());
        if (!reply) { std::fprintf(stderr, "%s\n", reply.error().c_str()); return 1; }
        const Reply r = parseReply(*reply);
        if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
        std::printf("%s\n", reply->c_str());
        return 0;
    }
    std::fputs(kUsage, stderr);
    return 2;
}

int cmdProject(const RuntimePaths& paths, int argc, char** argv)
{
    if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
    const std::string sub = argv[0];
    if (sub == "submit") {
        WorkflowSpec spec;
        if (int rc = parseWorkflowFlags(argc, argv, 1, spec)) return rc;
        if (!spec.sandboxDir.empty() && spec.sandboxDir[0] != '/') { char b[4096]; if (::getcwd(b, sizeof b)) spec.sandboxDir = std::string{b} + "/" + spec.sandboxDir; }
        auto client = ControlClient::connect(paths);
        if (!client) { std::fprintf(stderr, "%s%s\n", client.error().message.c_str(), client.error().noDaemon ? " (is `sub0squad serve` running?)" : ""); return 3; }
        auto reply = client->request("workflowSubmit", "\"spec\":" + spec.toJson());
        if (!reply) { std::fprintf(stderr, "%s\n", reply.error().c_str()); return 1; }
        const Reply r = parseReply(*reply);
        if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
        try { json::withObject(*reply, [&](simdjson::ondemand::object& obj) { for (auto f : obj) if (json::keyOf(f) == "id") std::printf("%s\n", json::toString(f.value()).c_str()); }); }
        catch (const simdjson::simdjson_error&) { return 1; }
        return 0;
    }
    if (sub == "status") {
        std::string id;
        bool jsonOut = false;
        for (int i = 1; i < argc; ++i) { if (std::string_view{argv[i]} == "--json") jsonOut = true; else id = argv[i]; }
        bool offline = false;
        const std::string text = workflowStatusAnyway(paths, id, offline);
        if (text.empty()) { std::fprintf(stderr, "cannot read workflow status\n"); return 1; }
        if (jsonOut) std::printf("%s\n", text.c_str());
        else { if (offline) std::fprintf(stderr, "daemon not running: showing the journal (as of now)\n"); printWorkflows(text); }
        return 0;
    }
    if (sub == "cancel") {
        if (argc < 2) { std::fputs(kUsage, stderr); return 2; }
        return simpleCommand(paths, "workflowCancel", argv[1], true);
    }
    if (sub == "plan") {
        std::string id, file;
        for (int i = 1; i < argc; ++i) { if (std::string_view{argv[i]} == "--file" && i + 1 < argc) file = argv[++i]; else id = argv[i]; }
        if (id.empty() || file.empty()) { std::fputs(kUsage, stderr); return 2; }
        const std::string planText = slurpFile(file);
        if (planText.empty()) { std::fprintf(stderr, "cannot read %s\n", file.c_str()); return 2; }
        auto client = ControlClient::connect(paths);
        if (!client) { std::fprintf(stderr, "%s\n", client.error().message.c_str()); return 3; }
        auto reply = client->request("workflowPlan", "\"id\":\"" + id + "\",\"spec\":" + planText);
        if (!reply) { std::fprintf(stderr, "%s\n", reply.error().c_str()); return 1; }
        const Reply r = parseReply(*reply);
        if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
        std::printf("plan accepted\n");
        return 0;
    }
    if (sub == "wait") {
        if (argc < 2) { std::fputs(kUsage, stderr); return 2; }
        const std::string id = argv[1];
        int timeout = 0;
        for (int i = 2; i + 1 < argc; ++i) if (std::string_view{argv[i]} == "--timeout") timeout = std::atoi(argv[i + 1]);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout > 0 ? timeout : 1 << 20);
        for (;;) {
            bool offline = false;
            const std::string text = workflowStatusAnyway(paths, id, offline);
            std::string state;
            if (!text.empty() && workflowTerminal(text, id, state)) { printWorkflows(text); return state == "succeeded" ? 0 : 1; }
            if (std::chrono::steady_clock::now() >= deadline) { std::fprintf(stderr, "timed out waiting for the workflow (it keeps running)\n"); return 124; }
            ::usleep(300000);
        }
    }
    std::fputs(kUsage, stderr);
    return 2;
}

}  // namespace

int runCli(int argc, char** argv)
{
    if (argc < 2) { std::fputs(kUsage, stderr); return 2; }
    const std::string cmd = argv[1];
    const RuntimePaths paths = pathsOrDie();
    if (cmd == "serve") {
        DaemonOptions opts;
        opts.paths = paths;
        char self[4096];
        const ssize_t n = ::readlink("/proc/self/exe", self, sizeof self - 1);
        opts.exe = n > 0 ? std::string(self, static_cast<std::size_t>(n)) : std::string{argv[0]};
        for (int i = 2; i + 1 < argc; ++i) {
            if (std::string_view{argv[i]} == "--mem-shed-mib") opts.memShedMiB = std::max(0, std::atoi(argv[i + 1]));
            if (std::string_view{argv[i]} == "--max-jobs") opts.maxJobs = std::max(1, std::atoi(argv[i + 1]));
            if (std::string_view{argv[i]} == "--frontier") {
                const std::string_view m = argv[i + 1];
                if (m == "off") opts.frontier = FrontierMode::off;
                else if (m == "stub") opts.frontier = FrontierMode::stub;
                else { std::fprintf(stderr, "serve: --frontier takes off or stub (there is no live mode in this build)\n"); return 2; }
            }
        }
        auto daemon = Daemon::create(std::move(opts));
        if (!daemon) { std::fprintf(stderr, "sub0squad serve: %s\n", daemon.error().c_str()); return 1; }
        return (*daemon)->run();
    }
    if (cmd == "submit") return cmdSubmit(paths, argc - 2, argv + 2);
    if (cmd == "status") {
        std::string id;
        bool jsonOut = false;
        for (int i = 2; i < argc; ++i) { if (std::string_view{argv[i]} == "--json") jsonOut = true; else id = argv[i]; }
        bool offline = false;
        auto st = statusAnyway(paths, id, offline);
        if (!st) { std::fprintf(stderr, "%s\n", st.error().c_str()); return 1; }
        if (jsonOut) std::printf("%s\n", st->c_str());
        else { if (offline) std::fprintf(stderr, "daemon not running: showing the journal and shim records (as of now)\n"); printJobs(*st); }
        return 0;
    }
    if (cmd == "plan-lint") {  // audit a plan's parallelism before running it: sub0squad plan-lint FILE [--tps N]
        if (argc < 3) { std::fputs(kUsage, stderr); return 2; }
        double tps = 5.0;
        for (int i = 3; i + 1 < argc; ++i) if (std::string_view{argv[i]} == "--tps") tps = std::atof(argv[i + 1]);
        const std::string text = slurpFile(argv[2]);
        auto plan = Plan::fromJson(text);
        if (!plan) { std::fprintf(stderr, "plan rejected: %s\n", plan.error().c_str()); return 1; }
        std::fputs(lintPlan(*plan, tps).toText().c_str(), stdout);
        return 0;
    }
    if (cmd == "portal") {
        int port = 8780;
        std::vector<std::string> engines;
        for (int i = 2; i + 1 < argc; ++i) {
            if (std::string_view{argv[i]} == "--port") port = std::atoi(argv[i + 1]);
            else if (std::string_view{argv[i]} == "--engine-url") engines.push_back(argv[i + 1]);
        }
        return servePortal(port, [&paths] {
            bool offline = false;
            const std::string wf = workflowStatusAnyway(paths, "", offline);
            auto jobs = statusAnyway(paths, "", offline);
            return "{\"workflows\":" + (wf.empty() ? std::string{"{\"workflows\":[]}"} : wf) + ",\"jobs\":" + (jobs ? *jobs : std::string{"{\"jobs\":[]}"}) + "}";
       }, engines, [&paths](const std::string& command, const std::string& workflow, const std::string& spec) -> std::string {
            auto client = ControlClient::connect(paths);
            if (!client) return "{\"ok\":false,\"error\":\"the daemon is not running\"}";
            auto reply = client->request(command, "\"id\":\"" + workflow + "\",\"spec\":" + spec);
            return reply ? *reply : "{\"ok\":false,\"error\":\"" + reply.error() + "\"}";
        });
    }
    if (cmd == "do") return cmdDo(paths, argc - 2, argv + 2);
    if (cmd == "blueprint") { if (argc < 3) { std::fputs(kUsage, stderr); return 2; } const Harness* bh = harnessWithBlueprint(argv[2]); const std::string b = bh ? bh->blueprint(argv[2]) : std::string{}; if (b.empty()) { std::fprintf(stderr, "no blueprint '%s'\n", argv[2]); return 1; } std::printf("%s", b.c_str()); return 0; }
    if (cmd == "project") return cmdProject(paths, argc - 2, argv + 2);
    if (cmd == "request") return cmdRequest(paths, argc - 2, argv + 2);
    if (cmd == "replay") {  // sub0squad replay WF NODE [--seeds 1,2,3] [--think] [--decompose] [--engine PROFILE] [--max-tokens N] [--repair-turns N]
        if (argc < 4) { std::fputs(kUsage, stderr); return 2; }
        sub0squad::ReplayOptions ro;
        ro.node = argv[3];
        for (int i = 4; i < argc; ++i) {
            const std::string a = argv[i];
            auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
            if (a == "--seeds") { ro.seeds.clear(); std::stringstream ss(val()); for (std::string x; std::getline(ss, x, ',');) if (!x.empty()) ro.seeds.push_back(std::atol(x.c_str())); }
            else if (a == "--think") ro.think = true;
            else if (a == "--decompose") ro.decompose = true;
            else if (a == "--fork") ro.fork = true;
            else if (a == "--engine") ro.engineProfile = val();
            else if (a == "--max-tokens") ro.maxTokens = std::atoi(val().c_str());
            else if (a == "--repair-turns") ro.repairTurns = std::atoi(val().c_str());
            else if (a == "--treatment") ro.treatment = val();
        }
        if (ro.seeds.empty()) ro.seeds = {1};
        bool offline = false;
        const std::string text = workflowStatusAnyway(paths, argv[2], offline);
        std::string firstEngine;
        try {
            json::withObject(text, [&](simdjson::ondemand::object& obj) {
                for (auto f : obj) {
                    if (json::keyOf(f) != "workflows") continue;
                    for (auto w : f.value().get_array()) for (auto wf : w.get_object()) {
                        const auto k = json::keyOf(wf);
                        if (k == "sandbox") ro.sandbox = json::toString(wf.value());
                        else if (k == "engines") { for (auto e : wf.value().get_array()) if (firstEngine.empty()) firstEngine = json::toString(e.value()); }
                    }
                }
            });
        } catch (const simdjson::simdjson_error&) {}
        if (ro.sandbox.empty()) { std::fprintf(stderr, "no such workflow: %s\n", argv[2]); return 2; }
        if (ro.engineProfile.empty()) ro.engineProfile = firstEngine;
        {
            char self[4096];
            const ssize_t n = ::readlink("/proc/self/exe", self, sizeof self - 1);
            ro.exe = n > 0 ? std::string(self, static_cast<std::size_t>(n)) : std::string{argv[0]};
        }
        return sub0squad::runReplay(ro);
    }
    if (cmd == "qa") {  // sub0squad qa DIR | WORKFLOW: duplication and size findings over a source tree or a workflow's accepted outputs
        if (argc < 3) { std::fputs(kUsage, stderr); return 2; }
        std::filesystem::path root = argv[2];
        std::error_code qec;
        if (!std::filesystem::is_directory(root, qec)) {
            bool offline = false;
            const std::string text = workflowStatusAnyway(paths, argv[2], offline);
            std::string sandbox;
            try { json::withObject(text, [&](simdjson::ondemand::object& obj) { for (auto f : obj) { if (json::keyOf(f) != "workflows") continue; for (auto w : f.value().get_array()) for (auto wf : w.get_object()) if (json::keyOf(wf) == "sandbox") sandbox = json::toString(wf.value()); } }); } catch (const simdjson::simdjson_error&) {}
            if (sandbox.empty()) { std::fprintf(stderr, "no such directory or workflow: %s\n", argv[2]); return 2; }
            root = qaCollect(sandbox, std::filesystem::path(sandbox) / "qa-tree");
        }
        const auto findings = qaScan(root);
        std::fputs(qaText(findings).c_str(), stdout);
        return 0;
    }
    if (cmd == "recall") {  // sub0squad recall WF words... [--item NODE] [--limit N]: what the workflow already did (conversations, gates, briefings)
        if (argc < 4) { std::fputs(kUsage, stderr); return 2; }
        std::string query, item;
        int limit = 20;
        for (int i = 3; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--item" && i + 1 < argc) item = argv[++i];
            else if (a == "--limit" && i + 1 < argc) limit = std::atoi(argv[++i]);
            else query += (query.empty() ? "" : " ") + a;
        }
        bool offline = false;
        const std::string text = workflowStatusAnyway(paths, argv[2], offline);
        std::string sandbox;
        try {
            json::withObject(text, [&](simdjson::ondemand::object& obj) {
                for (auto f : obj) {
                    if (json::keyOf(f) != "workflows") continue;
                    for (auto w : f.value().get_array()) for (auto wf : w.get_object()) if (json::keyOf(wf) == "sandbox") sandbox = json::toString(wf.value());
                }
            });
        } catch (const simdjson::simdjson_error&) {}
        if (sandbox.empty()) { std::fprintf(stderr, "no such workflow: %s\n", argv[2]); return 1; }
        const auto hits = recallSearch(sandbox, query, limit, item);
        for (const auto& h : hits) std::printf("%-12s %s:%d  %s\n", h.kind.c_str(), h.path.c_str(), h.line, h.text.c_str());
        if (hits.empty()) std::printf("nothing matched\n");
        return 0;
    }
    if (cmd == "logs") return cmdLogs(paths, argc - 2, argv + 2);
    if (cmd == "wait") return cmdWait(paths, argc - 2, argv + 2);
    if (cmd == "cancel" || cmd == "resume") {
        if (argc < 3) { std::fputs(kUsage, stderr); return 2; }
        return simpleCommand(paths, cmd.c_str(), argv[2], true);
    }
    if (cmd == "ping") return simpleCommand(paths, "ping", "", true);
    if (cmd == "stop") return simpleCommand(paths, "shutdown", "", false);
    std::fputs(kUsage, stderr);
    return 2;
}

}  // namespace sub0squad
