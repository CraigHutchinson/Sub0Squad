#include "sub0squad/clarify.hpp"

#include "json_util.hpp"
#include "sub0squad/triage.hpp"
#include "sub0squad/platform_file.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sub0squad {

namespace {


void writeTopics(json::Writer& w, const std::vector<ClarifyTopic>& topics)
{
    w.beginArray();
    for (const auto& t : topics) w.beginObject().kv("key", t.key).kv("value", t.value).kv("source", t.source).kv("note", t.note).endObject();
    w.endArray();
}

std::vector<ClarifyTopic> readTopics(simdjson::ondemand::array array)
{
    std::vector<ClarifyTopic> out;
    for (auto element : array) {
        ClarifyTopic t;
        for (auto f : element.get_object()) {
            const std::string_view k = f.unescaped_key();
            auto v = f.value();
            if (k == "key") t.key = json::toString(v);
            else if (k == "value") t.value = json::toString(v);
            else if (k == "source") t.source = json::toString(v);
            else if (k == "note") t.note = json::toString(v);
        }
        out.push_back(std::move(t));
    }
    return out;
}

/// The platforms a calculator request may be built for: the value the owner says, the blueprint it means, and how the question describes it.
struct Platform { const char* value; const char* task; const char* label; };
const Platform kPlatforms[] = {
    {"cpp", "calculator-cpp", "C++ with CMake and ctest (the current default)"},
    {"python", "calculator-py", "Python with unittest"},
    {"web", "calculator-web", "a static web page with node tests"},
};

const Platform* platformOfTask(const std::string& task)
{
    for (const auto& p : kPlatforms) if (task == p.task) return &p;
    return nullptr;
}

}  // namespace

std::string ClarifyFacts::toJson() const
{
    json::Writer w;
    w.beginObject().kv("schema", "clarify-input-1").kv("taskText", taskText).kv("task", task).kv("agent", agent).kv("executor", executor).kvBool("acceptDefaults", acceptDefaults);
    w.key("remembered").beginObject();
    for (const auto& [k, v] : remembered) w.kv(k, v);
    w.endObject().endObject();
    return w.take();
}

std::expected<ClarifyFacts, std::string> ClarifyFacts::fromJson(std::string_view text)
{
    ClarifyFacts f;
    bool schema = false;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto field : obj) {
                const std::string_view k = field.unescaped_key();
                auto v = field.value();
                if (k == "schema") schema = json::toString(v) == "clarify-input-1";
                else if (k == "taskText") f.taskText = json::toString(v);
                else if (k == "task") f.task = json::toString(v);
                else if (k == "agent") f.agent = json::toString(v);
                else if (k == "executor") f.executor = json::toString(v);
                else if (k == "acceptDefaults") f.acceptDefaults = v.get_bool();
                else if (k == "remembered") { for (auto r : v.get_object()) { const std::string key{std::string_view{r.unescaped_key()}}; f.remembered[key] = json::toString(r.value()); } }
            }
        });
    } catch (const simdjson::simdjson_error& e) { return std::unexpected(std::string{"bad clarify input: "} + e.what()); }
    if (!schema) return std::unexpected("clarify input needs \"schema\": \"clarify-input-1\"");
    return f;
}

std::string ClarifyPlan::toJson() const
{
    json::Writer w;
    w.beginObject().kv("schema", "clarify-1").kv("task", task).key("questions").beginArray();
    for (const auto& q : questions) {
        w.beginObject().kv("key", q.key).kv("question", q.question).key("values").beginArray();
        for (const auto& v : q.values) w.str(v);
        w.endArray().key("labels").beginArray();
        for (const auto& l : q.labels) w.str(l);
        w.endArray().endObject();
    }
    w.endArray().key("settled");
    writeTopics(w, settled);
    w.endObject();
    return w.take();
}

std::expected<ClarifyPlan, std::string> ClarifyPlan::fromJson(std::string_view text)
{
    ClarifyPlan p;
    bool schema = false;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto field : obj) {
                const std::string_view k = field.unescaped_key();
                auto v = field.value();
                if (k == "schema") schema = json::toString(v) == "clarify-1";
                else if (k == "task") p.task = json::toString(v);
                else if (k == "settled") p.settled = readTopics(v.get_array());
                else if (k == "questions") {
                    for (auto element : v.get_array()) {
                        ClarifyQuestion q;
                        for (auto f : element.get_object()) {
                            const std::string_view qk = f.unescaped_key();
                            auto qv = f.value();
                            if (qk == "key") q.key = json::toString(qv);
                            else if (qk == "question") q.question = json::toString(qv);
                            else if (qk == "values") { for (auto x : qv.get_array()) q.values.push_back(json::toString(x.value())); }
                            else if (qk == "labels") { for (auto x : qv.get_array()) q.labels.push_back(json::toString(x.value())); }
                        }
                        p.questions.push_back(std::move(q));
                    }
                }
            }
        });
    } catch (const simdjson::simdjson_error& e) { return std::unexpected(std::string{"bad questions file: "} + e.what()); }
    if (!schema) return std::unexpected("a questions file needs \"schema\": \"clarify-1\"");
    for (const auto& q : p.questions) if (q.key.empty() || q.values.empty() || q.labels.size() != q.values.size()) return std::unexpected("question '" + q.key + "' needs a key, at least one value, and a label per value");
    return p;
}

std::string ClarifyPlan::requestTitle() const
{
    std::string keys;
    for (const auto& q : questions) keys += (keys.empty() ? "" : ", ") + q.key;
    return "Before planning: " + keys + "? (answer `go` for the defaults)";
}

std::string ClarifyPlan::requestDetail() const
{
    std::string d = "The request leaves " + std::string{questions.size() == 1 ? "this" : "these"} + " open. Answer `go` to take the default of each, or `key=value` (several, separated by spaces) to choose.\n";
    for (const auto& q : questions) {
        d += "\n" + q.key + ": " + q.question + "\n";
        for (std::size_t i = 0; i < q.values.size(); ++i) d += "  " + q.key + "=" + q.values[i] + "  " + q.labels[i] + (i == 0 ? " [default]" : "") + "\n";
    }
    return d;
}

std::vector<std::string> ClarifyPlan::requestOptions() const
{
    std::string defaults;
    for (const auto& q : questions) defaults += (defaults.empty() ? "" : ", ") + q.key + " " + q.values.front();
    std::vector<std::string> options{"go (" + defaults + ")"};
    for (const auto& q : questions) for (std::size_t i = 1; i < q.values.size(); ++i) options.push_back(q.key + "=" + q.values[i]);
    return options;
}

std::string Brief::toJson() const
{
    json::Writer w;
    w.beginObject().kv("schema", "brief-1").kv("task", task).kv("executor", executor).key("topics");
    writeTopics(w, topics);
    w.endObject();
    return w.take();
}

std::map<std::string, std::string> Brief::toRemember() const
{
    std::map<std::string, std::string> out;
    for (const auto& t : topics) if (t.source == "answer" || t.source == "default") out[t.key] = t.value;
    return out;
}

ClarifyPlan clarify(const ClarifyFacts& facts)
{
    ClarifyPlan plan;
    plan.task = facts.task;
    const auto remembered = [&](const char* key, std::initializer_list<const char*> valid) -> std::string {
        const auto it = facts.remembered.find(key);
        if (it == facts.remembered.end()) return {};
        for (const char* v : valid) if (it->second == v) return it->second;
        return {};   // a value the record holds that is no longer a choice is not an answer
    };
    // ---- platform: which blueprint the planner loads ----
    const Platform* named = platformOfTask(facts.task);
    if (!named) {
        plan.settled.push_back({"platform", "", "not-asked", "the task '" + facts.task + "' has no blueprint family to choose from: its plan comes from the plan handoff (needs-plan.md), which names the harness"});
    } else {
        const bool assumed = !facts.taskText.empty() && triage(facts.taskText).platformAssumed;
        if (!assumed) plan.settled.push_back({"platform", named->value, "request", facts.taskText.empty() ? "the task was named: " + facts.task : "the request names the platform"});
        else if (const std::string r = remembered("platform", {"cpp", "python", "web"}); !r.empty()) plan.settled.push_back({"platform", r, "remembered", "answered before for this project"});
        else if (facts.acceptDefaults) plan.settled.push_back({"platform", kPlatforms[0].value, "default", "the owner asked for the defaults"});
        else {
            ClarifyQuestion q;
            q.key = "platform";
            q.question = "The request names no platform. Which one is the calculator built for?";
            for (const auto& p : kPlatforms) { q.values.push_back(p.value); q.labels.push_back(p.label); }
            plan.questions.push_back(std::move(q));
        }
    }
    // ---- executor: how a model writes an item ----
    if (!facts.executor.empty()) plan.settled.push_back({"executor", facts.executor, "request", "given with the request (--executor)"});
    else if (facts.agent != "llm") plan.settled.push_back({"executor", "", "harness", "the " + facts.agent + " agent writes reference files: an executor is a way of driving a model"});
    else if (const std::string r = remembered("executor", {"session", "canvas", "fork"}); !r.empty()) plan.settled.push_back({"executor", r, "remembered", "answered before for this project"});
    else if (facts.acceptDefaults) plan.settled.push_back({"executor", "session", "default", "the owner asked for the defaults"});
    else {
        ClarifyQuestion q;
        q.key = "executor";
        q.question = "How should the model write each item?";
        q.values = {"session", "canvas", "fork"};
        q.labels = {"one conversation that carries the files (the current default)", "replies are checked transactions on a revisioned document", "a skeleton, then one function per session, side by side"};
        plan.questions.push_back(std::move(q));
    }
    // ---- topics no answer could change in this run: said, with where they ARE decided, and not asked ----
    plan.settled.push_back({"scope", "", "not-asked", "a blueprint fixes the scope and size; another scope needs a plan written for it (submit without a blueprint and supply the plan)"});
    plan.settled.push_back({"engine", "", "not-asked", "the engines were given with the request (--engine, --route): the first is the default, and a persona may be routed to another"});
    plan.settled.push_back({"bar", "", "not-asked", "done is what the process says: every gate and every later stage; a higher bar is another process, chosen at submission (--process reviewed)"});
    plan.settled.push_back({"frontier", "", "not-asked", "the daemon's setting (off unless it was started otherwise); a question never changes it"});
    return plan;
}

std::expected<Brief, std::string> resolveBrief(const ClarifyPlan& plan, const std::string& answer)
{
    std::map<std::string, std::string> chosen;
    std::string text = answer;
    std::replace(text.begin(), text.end(), ',', ' ');
    std::istringstream in(text);
    std::vector<std::string> words;
    for (std::string w; in >> w;) { std::transform(w.begin(), w.end(), w.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); words.push_back(w); }
    const auto say = [&] {
        std::string s = "answer `go`";
        for (const auto& q : plan.questions) for (const auto& v : q.values) s += ", `" + q.key + "=" + v + "`";
        return s;
    };
    // `go`, alone or as the first word of the option as it is listed ("go (platform cpp, executor session)"), takes every default.
    const bool go = words.empty() || words.front() == "go" || words.front() == "defaults" || words.front() == "default";
    if (!go) {
        for (const auto& w : words) {
            const auto eq = w.find('=');
            std::string key = eq == std::string::npos ? std::string{} : w.substr(0, eq);
            const std::string value = eq == std::string::npos ? w : w.substr(eq + 1);
            if (key.empty()) {   // a bare value: fine when exactly one question has it
                int hits = 0;
                for (const auto& q : plan.questions) if (std::find(q.values.begin(), q.values.end(), value) != q.values.end()) { ++hits; key = q.key; }
                if (hits != 1) return std::unexpected("'" + w + "' is not an answer here: " + say());
            }
            const auto q = std::find_if(plan.questions.begin(), plan.questions.end(), [&](const ClarifyQuestion& x) { return x.key == key; });
            if (q == plan.questions.end()) return std::unexpected("'" + key + "' is not one of the questions: " + say());
            if (std::find(q->values.begin(), q->values.end(), value) == q->values.end()) return std::unexpected("'" + value + "' is not a choice for " + key + ": " + say());
            chosen[key] = value;
        }
    }
    Brief b;
    b.task = plan.task;
    b.topics = plan.settled;
    for (const auto& q : plan.questions) {
        const auto it = chosen.find(q.key);
        b.topics.push_back({q.key, it == chosen.end() ? q.values.front() : it->second, it == chosen.end() ? "default" : "answer", it == chosen.end() ? "the default, accepted by the owner's answer" : "the owner's answer"});
    }
    for (const auto& t : b.topics) {
        if (t.key == "platform" && !t.value.empty()) { for (const auto& p : kPlatforms) if (t.value == p.value) b.task = p.task; }
        if (t.key == "executor" && t.source != "harness") b.executor = t.value;
    }
    return b;
}

std::map<std::string, std::string> parseProjectAnswers(std::string_view text)
{
    std::map<std::string, std::string> out;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto field : obj) {
                if (json::keyOf(field) != "answers") continue;
                for (auto r : field.value().get_object()) { const std::string key{std::string_view{r.unescaped_key()}}; out[key] = json::toString(r.value()); }
            }
        });
    } catch (const simdjson::simdjson_error&) { return {}; }
    return out;
}

std::string projectAnswersJson(const std::map<std::string, std::string>& answers)
{
    json::Writer w;
    w.beginObject().kv("schema", "project-answers-1").key("answers").beginObject();
    for (const auto& [k, v] : answers) w.kv(k, v);
    w.endObject().endObject();
    return w.take();
}

int runPlanQuestions(const std::vector<std::string>& args)
{
    std::filesystem::path dir;
    for (std::size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == "--workflow-dir") dir = args[i + 1];
    if (dir.empty()) { std::fprintf(stderr, "plan-questions: --workflow-dir D\n"); return 64; }
    const auto facts = ClarifyFacts::fromJson(platform::readFileText(dir / "clarify-input.json"));
    if (!facts) { std::fprintf(stderr, "plan-questions: %s\n", facts.error().c_str()); return 1; }
    const ClarifyPlan plan = clarify(*facts);
    if (!platform::writeFile(dir / "questions.json", plan.toJson() + "\n")) { std::fprintf(stderr, "plan-questions: cannot write questions.json\n"); return 1; }
    for (const auto& t : plan.settled) std::printf("%-9s %-10s %s%s%s\n", t.key.c_str(), t.source.c_str(), t.value.c_str(), t.value.empty() ? "" : ": ", t.note.c_str());
    for (const auto& q : plan.questions) std::printf("%-9s ASK        %s (default: %s)\n", q.key.c_str(), q.question.c_str(), q.values.front().c_str());
    return 0;
}

}  // namespace sub0squad
