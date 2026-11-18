#include "portal_work.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <system_error>

namespace sub0squad::portal {
namespace {

namespace fs = std::filesystem;
using simdjson::dom::element;
using simdjson::dom::parser;

constexpr std::size_t maxJsonBytes = 2 * 1024 * 1024;
constexpr std::size_t maxHomes = 16;
constexpr std::size_t maxRows = 300;

[[nodiscard]] std::string readBounded(const fs::path& path, std::size_t limit = maxJsonBytes)
{
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return {};
    const auto size = fs::file_size(path, ec);
    if (ec || size > limit) return {};
    std::ifstream input(path, std::ios::binary);
    std::string body(static_cast<std::size_t>(size), '\0');
    if (!input.read(body.data(), static_cast<std::streamsize>(body.size())) || input.peek() != std::char_traits<char>::eof()) return {};
    return body;
}

[[nodiscard]] std::string text(element value, std::string_view key, std::string_view fallback = {})
{
    std::string_view result;
    if (value[key].get(result)) return std::string{fallback};
    return std::string{result.substr(0, 2000)};
}

[[nodiscard]] std::int64_t count(element value, std::string_view key)
{
    std::int64_t result{};
    return value[key].get(result) ? 0 : std::max<std::int64_t>(result, 0);
}

[[nodiscard]] bool flag(element value, std::string_view key)
{
    bool result{};
    return !value[key].get(result) && result;
}

[[nodiscard]] bool validId(std::string_view id)
{
    return !id.empty() && id.size() <= 96 && std::ranges::all_of(id, [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
    });
}

[[nodiscard]] bool privatePath(const fs::path& path)
{
    return std::ranges::any_of(path, [](const auto& part) {
        auto lowercase = part.string();
        for (auto& ch : lowercase) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        return lowercase == "checker" || lowercase == "private";
    });
}

[[nodiscard]] std::optional<fs::path> registeredPath(const fs::path& parent, const std::string& value)
{
    if (value.empty()) return std::nullopt;
    std::error_code ec;
    auto resolved = fs::weakly_canonical(parent / fs::path{value}, ec);
    if (ec || privatePath(parent / fs::path{value}) || privatePath(resolved)) return std::nullopt;
    return resolved;
}

[[nodiscard]] std::optional<fs::path> registeredFile(const fs::path& parent, element spec, std::string_view field)
{
    const auto root = registeredPath(parent, text(spec, "root"));
    const fs::path relative{text(spec, field)};
    if (!root || relative.has_root_path() || privatePath(relative) || std::ranges::find(relative, fs::path{".."}) != relative.end()) return std::nullopt;
    const auto resolved = registeredPath(*root, relative.generic_string());
    if (!resolved) return std::nullopt;
    const auto inside = resolved->lexically_relative(*root);
    if (inside.empty() || *inside.begin() == "..") return std::nullopt;
    return resolved;
}

[[nodiscard]] std::optional<fs::path> reportPath(const fs::path& parent, element spec)
{
    const auto resolved = registeredFile(parent, spec, "file");
    if (!resolved) return std::nullopt;
    static constexpr std::array allowed{"gate.json", "smoke.json", "scenario-results.json", "summary.json", "manifest.json", "verdict.json", "report.md"};
    if (std::ranges::find(allowed, resolved->filename().string()) == allowed.end()) return std::nullopt;
    return resolved;
}

void row(json::Writer& writer, std::string_view project, std::string_view id, std::string_view kind,
         std::string_view title, std::string_view lifecycle, std::string_view verdict, std::string_view freshness)
{
    writer.beginObject().kv("project", project).kv("id", id).kv("kind", kind).kv("title", title)
        .kv("lifecycle", lifecycle).kv("verdict", verdict).kv("freshness", freshness);
}

void workflowRows(json::Writer& writer, const std::string& data, std::string_view project, std::string_view source, std::size_t& rows, std::string_view classification = {})
{
    parser parse;
    element status;
    if (parse.parse(data).get(status)) return;
    element wfStatus;
    if (status["workflows"].get(wfStatus)) return;
    const auto origin = text(wfStatus, "source");
    simdjson::dom::array workflows;
    if (wfStatus["workflows"].get(workflows)) return;
    for (auto workflow : workflows) {
        if (rows >= maxRows) break;
        const auto id = text(workflow, "id");
        if (!validId(id)) continue;
        const auto state = text(workflow, "state");
        const bool terminal = state == "succeeded" || state == "failed" || state == "cancelled";
        const bool waiting = state == "waiting" || state == "paused" || (state == "running" && count(workflow, "jobsActive") == 0);
        const bool active = state == "running" && count(workflow, "jobsActive") > 0;
        const char* lifecycle = terminal ? "finished" : active && origin == "live" ? "running" : waiting ? "waiting" : active ? "last observed running" : "queued";
        const char* verdict = state == "succeeded" ? "pass" : state == "failed" ? "fail" : state == "cancelled" ? "cancelled" : "";
        row(writer, project, id, "workflow", text(workflow, "taskText", text(workflow, "task", id)), lifecycle, verdict,
            origin == "live" ? "live" : terminal ? "archive" : "offline");
        const auto firstCause = text(workflow, "firstCause");
        writer.kv("source", source).kv("workflow", id).kv("note", firstCause.empty() ? text(workflow, "note") : firstCause);
        if (!classification.empty()) writer.kv("classification", classification);
        double asOf{};
        if (!wfStatus["asOf"].get(asOf)) writer.kv("asOf", asOf);
        writer.key("stages").beginArray();
        simdjson::dom::array stages;
        if (!workflow["stages"].get(stages)) for (auto stage : stages) {
            const auto stageState = text(stage, "state");
            if (stageState == "active" || stageState == "waiting")
                writer.beginObject().kv("name", text(stage, "name")).kv("state", stageState).kv("waitingOn", text(stage, "waitingOn")).endObject();
        }
        writer.endArray().endObject();
        ++rows;
    }
}

void reportRow(json::Writer& writer, const fs::path& parent, element spec, std::size_t& rows)
{
    if (rows >= maxRows) return;
    const auto id = text(spec, "id");
    const auto project = text(spec, "project");
    const auto path = reportPath(parent, spec);
    if (!validId(id) || !validId(project) || !path) return;
    const auto kind = text(spec, "kind", "report");
    const auto title = text(spec, "title", id);
    const auto body = readBounded(*path);
    parser parse;
    element report;
    if (body.empty() || parse.parse(body).get(report)) {
        row(writer, project, id, kind, title, "waiting", "incomplete", "unavailable");
        writer.endObject();
        ++rows;
        return;
    }
    auto verdict = text(report, "verdict", text(report, "public", ""));
    simdjson::dom::array scenarios;
    const bool scenarioReport = text(report, "schema") == "scenario-results-1" && !report["results"].get(scenarios);
    if (scenarioReport && verdict.empty()) {
        verdict = scenarios.size() ? "pass" : "incomplete";
        for (auto scenario : scenarios) if (!flag(scenario, "reliable")) verdict = "not proven";
    }
    row(writer, project, id, kind, title, "finished", verdict, "recorded");
    writer.kv("report", id).kv("tier", text(spec, "tier")).kv("build", text(report, "build")).kv("engine", text(report, "engine"))
        .kv("classification", text(spec, "classification", text(report, "schema"))).kv("note", text(report, "detail"));
    simdjson::dom::object cases;
    if (!report["cases"].get(cases)) {
        writer.key("cases").beginArray();
        std::int64_t completed = 0, passed = 0, planned = 0;
        const auto minimum = count(report, "minRuns");
        std::size_t shown = 0;
        for (const auto& [name, entry] : cases) {
            const auto runs = count(entry, "runs");
            const auto successes = count(entry, "passed");
            completed += runs;
            passed += successes;
            planned += minimum;
            if (shown++ < 100) writer.beginObject().kv("name", name).kv("state", text(entry, "state"))
                .kv("completed", runs).kv("passed", successes).endObject();
        }
        writer.endArray().kv("planned", planned).kv("completed", completed).kv("passed", passed);
    } else if (scenarioReport) {
        writer.key("cases").beginArray();
        std::int64_t completed = 0, passed = 0;
        std::size_t shown = 0;
        for (auto scenario : scenarios) {
            const auto runs = count(scenario, "trials"), successes = count(scenario, "passed");
            completed += runs;
            passed += successes;
            if (shown++ < 100) writer.beginObject().kv("name", text(scenario, "platform") + "/" + text(scenario, "element"))
                .kv("state", flag(scenario, "reliable") ? "reliable" : "not proven").kv("completed", runs).kv("passed", successes).endObject();
        }
        writer.endArray().kv("completed", completed).kv("passed", passed);
    }
    writer.endObject();
    ++rows;
}

void sandboxRows(json::Writer& writer, json::Writer& snapshots, const fs::path& parent, element spec, std::size_t& rows,
                 const std::function<std::string(const fs::path&)>& readHome)
{
    const auto root = registeredPath(parent, text(spec, "root"));
    const auto project = text(spec, "project"), campaign = text(spec, "id");
    if (!root || !validId(project) || !validId(campaign) || campaign.size() > 24) return;
    std::error_code ec;
    std::vector<fs::path> trials;
    std::size_t scanned = 0;
    for (fs::directory_iterator it(*root, ec), end; !ec && it != end && scanned++ < 2500; it.increment(ec)) {
        const auto name = it->path().filename().string();
        const auto status = it->symlink_status(ec);
        if (!ec && fs::is_directory(status) && name.size() >= 24 && validId(name) && name[8] == 'T') trials.push_back(it->path());
    }
    std::ranges::sort(trials, std::greater{});
    int checkedHomes = 0;
    for (const auto& trial : trials) {
        if (rows >= maxRows) break;
        parser manifestParser, verdictParser;
        element manifest, result;
        std::error_code manifestError;
        if (!fs::is_regular_file(fs::symlink_status(trial / "manifest.json", manifestError)) ||
            manifestParser.parse(readBounded(trial / "manifest.json", 128 * 1024)).get(manifest)) continue;
        const auto verdictPath = trial / "verdict.json";
        std::error_code statusError;
        const bool terminal = fs::is_regular_file(fs::symlink_status(verdictPath, statusError)) &&
            !verdictParser.parse(readBounded(verdictPath, 128 * 1024)).get(result);
        const auto mode = text(manifest, "mode", "trial");
        const auto id = trial.filename().string();
        const bool staged = !terminal && text(manifest, "state") == "staged";
        std::string channel;
        if (!terminal && !staged && checkedHomes < 4 && fs::is_directory(trial / "home/run", statusError)) {
            ++checkedHomes;
            parser statusParser;
            element homeState, workflows;
            const auto status = readHome(trial / "home");
            if (!statusParser.parse(status).get(homeState) && !homeState["workflows"].get(workflows) &&
                text(workflows, "source") == "live") {
                simdjson::dom::array entries;
                if (!workflows["workflows"].get(entries)) for (auto entry : entries)
                    if (text(entry, "state") == "running" && count(entry, "jobsActive") > 0) {
                        channel = campaign + "--" + id;
                        snapshots.key(channel).beginObject().kvRaw("state", status).kv("home", (trial / "home").generic_string()).endObject();
                        break;
                    }
            }
        }
        const bool active = !channel.empty();
        row(writer, project, id, "sandbox", mode + " " + id, terminal ? "finished" : staged ? "staged" : active ? "running" : "unconfirmed",
            terminal ? text(result, "public", text(result, "verdict", "indeterminate")) : "", terminal || staged ? "recorded" : active ? "live" : "unconfirmed");
        auto tier = text(spec, "tier", "T0");
        if (const auto number = count(manifest, "tier")) tier = "T" + std::to_string(number);
        if (active) writer.kv("source", channel).kv("workflow", "w-000001");
        if (terminal) writer.kv("report", campaign + "--" + id);
        if (staged) writer.kv("report", campaign + "--" + id).kv("reportLabel", "Open run spec");
        std::string note = terminal ? text(result, "detail") : staged ? "Prepared run spec" : active ? "Owning daemon reports activity" : "Execution has not been verified.";
        if (staged) {
            parser admissionParser;
            element admission;
            if (!admissionParser.parse(readBounded(trial / "admission.json", 8192)).get(admission))
                note = text(admission, "detail", note);
        }
        writer.kv("tier", tier).kv("classification", mode == "preflight" ? "scripted" : "diagnostic")
            .kv("note", note)
            .kv("tester", terminal ? text(result, "tester") : "").kv("parent", text(manifest, "parent")).endObject();
        ++rows;
    }
}

} // namespace

std::string workOverview(const fs::path& registry, std::string localState,
    const std::function<std::string(const fs::path&)>& readHome)
{
    constexpr std::string_view emptyState = R"({"workflows":{"workflows":[]},"jobs":{"jobs":[]}})";
    if (localState.empty() || localState.back() != '}') localState = emptyState;
    localState.pop_back();
    parser parse;
    element config;
    if (parse.parse(readBounded(registry, 256 * 1024)).get(config))
        return localState + R"(,"workOverview":{"error":"Work registry is unavailable or invalid","rows":[]}})";

    const auto parent = registry.parent_path();
    json::Writer overview, snapshots;
    overview.beginObject().key("rows").beginArray();
    snapshots.beginObject();
    std::size_t rows = 0;
    std::set<std::string> knownSources;
    workflowRows(overview, localState + '}', text(config, "localProject", "Local"), "", rows);

    simdjson::dom::array sources;
    if (!config["homes"].get(sources)) for (auto spec : sources) {
        if (knownSources.size() >= maxHomes) break;
        const auto id = text(spec, "id"), project = text(spec, "project");
        const auto home = registeredPath(parent, text(spec, "home"));
        if (!validId(id) || !validId(project) || !home || !knownSources.insert(id).second) continue;
        const auto data = readHome(*home);
        if (!data.empty()) {
            snapshots.key(id).beginObject().kvRaw("state", data).kv("home", home->generic_string()).endObject();
            const bool forced = !readBounded(*home / "forced.txt", 8192).empty();
            workflowRows(overview, data, project, id, rows, forced ? "forced" : "");
        } else if (rows < maxRows) {
            row(overview, project, id, "source", text(spec, "title", id), "waiting", "", "offline");
            overview.kv("note", "No live or recorded status is available.").endObject();
            ++rows;
        }
    }
    simdjson::dom::array work;
    if (!config["work"].get(work)) for (auto spec : work) {
        if (rows >= maxRows) break;
        const auto id = text(spec, "id"), project = text(spec, "project"), state = text(spec, "state", "backlog");
        if (!validId(id) || !validId(project) || (state != "staged" && state != "backlog")) continue;
        row(overview, project, id, text(spec, "kind", "backlog"), text(spec, "title", id), state, "", "recorded");
        if (const auto brief = registeredFile(parent, spec, "brief"); brief && brief->extension() == ".md")
            overview.kv("report", "brief--" + project + "--" + id).kv("reportLabel", "Open brief");
        overview.kv("tier", text(spec, "tier")).kv("note", text(spec, "note")).kv("brief", text(spec, "brief"))
            .kv("owner", text(spec, "owner")).key("dependsOn").beginArray();
        simdjson::dom::array dependencies;
        if (!spec["dependsOn"].get(dependencies)) for (auto dependency : dependencies) {
            std::string_view item;
            if (!dependency.get(item) && validId(item)) overview.str(item);
        }
        overview.endArray().endObject();
        ++rows;
    }
    if (!config["reports"].get(sources)) for (auto spec : sources) reportRow(overview, parent, spec, rows);
    if (!config["sandboxCampaigns"].get(sources)) for (auto spec : sources) sandboxRows(overview, snapshots, parent, spec, rows, readHome);
    overview.endArray().kvBool("limited", rows >= maxRows).endObject();
    snapshots.endObject();
    return localState + ",\"workOverview\":" + overview.take() + ",\"workSources\":" + snapshots.take() + "}";
}

std::string selectWorkSource(const std::string& state, const std::string& source)
{
    if (source.empty()) return state;
    if (!validId(source)) return {};
    parser parse;
    element doc, entry;
    if (parse.parse(state).get(doc)) return {};
    const auto registered = doc["workSources"][source];
    if (registered["state"].get(entry)) return {};
    std::string_view home;
    if (registered["home"].get(home)) return {};
    auto selected = simdjson::to_string(entry);
    if (selected.empty() || selected.back() != '}') return {};
    selected.pop_back();
    json::Writer tail;
    tail.beginObject().kv("workHome", home).endObject();
    return selected + ',' + tail.take().substr(1);
}

std::string readWorkReport(const fs::path& registry, const std::string& id)
{
    if (!validId(id)) return {};
    parser parse;
    element config;
    simdjson::dom::array reports;
    if (parse.parse(readBounded(registry, 256 * 1024)).get(config)) return {};
    if (!config["work"].get(reports)) for (auto spec : reports) {
        if ("brief--" + text(spec, "project") + "--" + text(spec, "id") != id) continue;
        const auto path = registeredFile(registry.parent_path(), spec, "brief");
        return path && path->extension() == ".md" ? readBounded(*path, 400000) : std::string{};
    }
    if (!config["reports"].get(reports)) for (auto spec : reports) if (text(spec, "id") == id) {
        const auto path = reportPath(registry.parent_path(), spec);
        return path ? readBounded(*path, 400000) : std::string{};
    }
    if (!config["sandboxCampaigns"].get(reports)) for (auto spec : reports) {
        const auto prefix = text(spec, "id") + "--";
        if (!id.starts_with(prefix)) continue;
        const auto trial = id.substr(prefix.size());
        if (trial.size() < 24 || trial[8] != 'T' || !validId(trial)) return {};
        const auto root = registeredPath(registry.parent_path(), text(spec, "root"));
        if (!root) return {};
        std::error_code ec;
        const auto dir = *root / trial;
        if (!fs::is_directory(fs::symlink_status(dir, ec))) return {};
        const auto file = dir / "verdict.json";
        if (fs::is_regular_file(fs::symlink_status(file, ec))) return readBounded(file, 400000);
        const auto manifest = dir / "manifest.json";
        if (!fs::is_regular_file(fs::symlink_status(manifest, ec))) return {};
        return readBounded(manifest, 400000);
    }
    return {};
}

} // namespace sub0squad::portal
