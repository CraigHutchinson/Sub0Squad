// The job commands of the command line: submit, status, logs, wait, and the one-request commands.
#include "cli_impl.hpp"

#include "sub0squad/client.hpp"
#include "cmd_archive.hpp"
#include "cmd_process.hpp"
#include "sub0squad/mends.hpp"
#include "sub0squad/build_id.hpp"
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

namespace sub0squad::cli {

std::string slurpFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---- JSON reply helpers --------------------------------------------------------------------------------------------
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

/// What every submit tells the daemon about its sender (catalogue row 20): this program's build id, and whether a different daemon build is accepted.
std::string submitFields(bool allowBuildMismatch)
{
    json::Writer w;
    w.beginObject().kv("build", buildId()).kvBool("allowBuildMismatch", allowBuildMismatch).endObject();
    std::string fields = w.take();
    return fields.substr(1, fields.size() - 2) + ",";   // the two members, to splice into a request
}

/// Take `--allow-build-mismatch` out of a workflow command's arguments (it is about the submit, not the workflow); true when it was there.
bool takeAllowMismatch(int& argc, char** argv)
{
    bool found = false;
    int kept = 0;
    for (int i = 0; i < argc; ++i) { if (std::string_view{argv[i]} == "--allow-build-mismatch") found = true; else argv[kept++] = argv[i]; }
    argc = kept;
    return found;
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
    bool allowMismatch = false;
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
        else if (a == "--allow-build-mismatch") allowMismatch = true;
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
    auto reply = client->request("submit", submitFields(allowMismatch) + "\"spec\":" + spec.toJson());
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

int cmdStatus(const RuntimePaths& paths, int argc, char** argv)
{
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

}  // namespace sub0squad::cli
