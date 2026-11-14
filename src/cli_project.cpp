// The workflow commands of the command line: do, project (submit, plan, status, cancel, wait) and request (the inbox).
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
                                std::string iid, title, status, tier, builtBy, whyKind, whyRole, why; std::int64_t attempts = 0, whyAttempt = 0;
                                for (auto g : iv.get_object()) {
                                    const std::string_view gk = g.unescaped_key();
                                    auto gv = g.value();
                                    if (gk == "id") iid = json::toString(gv); else if (gk == "title") title = json::toString(gv);
                                    else if (gk == "status") status = json::toString(gv); else if (gk == "tier") tier = json::toString(gv);
                                    else if (gk == "attempts") attempts = gv.get_int64();
                                    else if (gk == "builtBy") builtBy = json::toString(gv);
                                    else if (gk == "lastFailureAttempt") whyAttempt = gv.get_int64();
                                    else if (gk == "lastFailure") {   // why the latest failed attempt failed (its agent, its gate or the daemon recorded it)
                                        for (auto f : gv.get_object()) {
                                            const std::string_view fk = f.unescaped_key();
                                            if (fk == "role") whyRole = json::toString(f.value()); else if (fk == "kind") whyKind = json::toString(f.value()); else if (fk == "reason") why = json::toString(f.value());
                                        }
                                    }
                                }
                                char buf[256];
                                std::snprintf(buf, sizeof buf, "  %-11s %-10s attempts %lld  last tier %-13s %s%s", iid.c_str(), status.c_str(), static_cast<long long>(attempts), tier.empty() ? "-" : tier.c_str(), title.c_str(),
                                              builtBy == "stub" ? "  [NOT model-built: frontier stub]" : builtBy == "mock" ? "  [mock agent]" : "");
                                items.push_back(buf);
                                if (!why.empty()) items.push_back("              attempt " + std::to_string(whyAttempt) + " failed in its " + whyRole + " (" + whyKind + "): " + why.substr(0, why.find('\n')).substr(0, 180));
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
        else if (a == "--mends") {
            const std::string file = val();
            std::string text = slurpFile(file);
            while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
            if (auto t = parseMends(text); !t) { std::fprintf(stderr, "--mends %s: %s\n", file.c_str(), text.empty() ? "cannot read the file" : t.error().c_str()); return 2; }
            spec.mends = text;
        }
        else if (a == "--treatment") spec.treatment = val();
        else if (a == "--executor") spec.executor = val();
        else if (a == "--process") {
            auto p = findProcess(val(), RuntimePaths::fromEnvironment().stateDir / "processes");
            if (!p) { std::fprintf(stderr, "--process: %s\n", p.error().c_str()); return 2; }
            spec.process = std::move(*p);
        }
        else if (a == "--repair-turns") spec.repairTurns = std::atoi(val().c_str());
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    return 0;
}

int cmdDo(const RuntimePaths& paths, int argc, char** argv)
{
    const bool allowMismatch = takeAllowMismatch(argc, argv);
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
    auto reply = client->request("workflowSubmit", submitFields(allowMismatch) + "\"spec\":" + spec.toJson());
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
        const bool allowMismatch = takeAllowMismatch(argc, argv);
        WorkflowSpec spec;
        if (int rc = parseWorkflowFlags(argc, argv, 1, spec)) return rc;
        if (!spec.sandboxDir.empty() && spec.sandboxDir[0] != '/') { char b[4096]; if (::getcwd(b, sizeof b)) spec.sandboxDir = std::string{b} + "/" + spec.sandboxDir; }
        auto client = ControlClient::connect(paths);
        if (!client) { std::fprintf(stderr, "%s%s\n", client.error().message.c_str(), client.error().noDaemon ? " (is `sub0squad serve` running?)" : ""); return 3; }
        auto reply = client->request("workflowSubmit", submitFields(allowMismatch) + "\"spec\":" + spec.toJson());
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

}  // namespace sub0squad::cli
