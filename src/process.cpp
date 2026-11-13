#include "sub0squad/process.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;

namespace {

/// One parameter of a process: its dotted path and where it lives. The table is the single description of the file format: JSON writing, JSON reading,
/// `set` and the list of paths all come from it, so a new parameter is one line here and cannot be written but not read.
struct Field
{
    std::string path;
    bool* b = nullptr;
    int* i = nullptr;
    std::string* s = nullptr;
};

std::vector<Field> fieldsOf(Process& p)
{
    std::vector<Field> f;
    const auto flag = [&](const char* path, bool& v) { f.push_back({path, &v, nullptr, nullptr}); };
    const auto num = [&](const char* path, int& v) { f.push_back({path, nullptr, &v, nullptr}); };
    const auto text = [&](const char* path, std::string& v) { f.push_back({path, nullptr, nullptr, &v}); };
    num("planner.wallSeconds", p.planner.wallSeconds);
    num("planner.retries", p.planner.retries);
    text("leaf.executor.developer", p.leaf.developerExecutor);
    text("leaf.executor.tester", p.leaf.testerExecutor);
    flag("leaf.allow.decompose", p.leaf.allowDecompose);
    flag("leaf.allow.sections", p.leaf.allowSections);
    flag("leaf.allow.reviseTests", p.leaf.allowReviseTests);
    flag("leaf.allow.rearchitect", p.leaf.allowRearchitect);
    flag("leaf.allow.escalate", p.leaf.allowEscalate);
    num("leaf.wallSeconds.agentLlm", p.leaf.agentWallLlmSeconds);
    num("leaf.wallSeconds.agentOther", p.leaf.agentWallOtherSeconds);
    num("leaf.wallSeconds.gate", p.leaf.gateWallSeconds);
    num("leaf.wallSeconds.architectLlm", p.leaf.architectWallLlmSeconds);
    num("leaf.wallSeconds.architectOther", p.leaf.architectWallOtherSeconds);
    num("ascent.maxRoundTrips", p.ascent.maxRoundTrips);
    num("integrate.wallSeconds", p.integrate.wallSeconds);
    return f;
}

std::vector<std::string> split(const std::string& path)
{
    std::vector<std::string> v;
    std::istringstream in(path);
    for (std::string part; std::getline(in, part, '.');) v.push_back(part);
    return v;
}

bool validName(const std::string& n)
{
    if (n.empty() || n.size() > 40) return false;
    for (const char c : n) if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-')) return false;
    return n.front() != '-';
}

struct Scalar { enum class Kind { boolean, integer, text } kind = Kind::text; bool b = false; std::int64_t i = 0; std::string s; };

/// Every scalar of a JSON object, with its dotted path (an object nests, anything else is an error).
void walk(simdjson::ondemand::object obj, const std::string& prefix, std::vector<std::pair<std::string, Scalar>>& out)
{
    for (auto f : obj) {
        const std::string key{std::string_view{f.unescaped_key()}};
        auto v = f.value();
        const std::string path = prefix.empty() ? key : prefix + "." + key;
        Scalar sc;
        switch (v.type().value()) {
            case simdjson::ondemand::json_type::object: walk(v.get_object(), path, out); continue;
            case simdjson::ondemand::json_type::boolean: sc.kind = Scalar::Kind::boolean; sc.b = v.get_bool(); break;
            case simdjson::ondemand::json_type::number: sc.kind = Scalar::Kind::integer; sc.i = v.get_int64(); break;
            case simdjson::ondemand::json_type::string: sc.kind = Scalar::Kind::text; sc.s = json::toString(v); break;
            default: throw std::runtime_error("'" + path + "' must be a boolean, a number, a string or an object");
        }
        out.emplace_back(path, std::move(sc));
    }
}

bool validExecutor(const std::string& e, bool tester) { return e == "session" || e == "canvas" || (!tester && e == "fork"); }

}  // namespace

std::string Process::validate() const
{
    if (!validName(name)) return "process name '" + name + "' must be 1 to 40 characters of a-z, 0-9 and '-', not starting with '-'";
    if (version < 1 || version > 1000) return "process version must be 1..1000";
    if (!validExecutor(leaf.developerExecutor, false)) return "leaf.executor.developer '" + leaf.developerExecutor + "' must be session, canvas or fork";
    if (!validExecutor(leaf.testerExecutor, true)) return "leaf.executor.tester '" + leaf.testerExecutor + "' must be session or canvas (a test file has no sections to fork)";
    const auto range = [](int v, int lo, int hi, const char* what) { return v < lo || v > hi ? std::string{what} + " must be " + std::to_string(lo) + ".." + std::to_string(hi) + ", not " + std::to_string(v) : std::string{}; };
    for (const std::string& bad : {range(planner.wallSeconds, 10, 3600, "planner.wallSeconds"), range(planner.retries, 0, 5, "planner.retries"),
                                   range(leaf.agentWallLlmSeconds, 60, 604800, "leaf.wallSeconds.agentLlm"), range(leaf.agentWallOtherSeconds, 10, 3600, "leaf.wallSeconds.agentOther"),
                                   range(leaf.gateWallSeconds, 10, 3600, "leaf.wallSeconds.gate"), range(leaf.architectWallLlmSeconds, 60, 86400, "leaf.wallSeconds.architectLlm"),
                                   range(leaf.architectWallOtherSeconds, 10, 3600, "leaf.wallSeconds.architectOther"), range(ascent.maxRoundTrips, 1, 10, "ascent.maxRoundTrips"),
                                   range(integrate.wallSeconds, 10, 86400, "integrate.wallSeconds")})
        if (!bad.empty()) return bad;
    return {};
}

std::string Process::toJson() const
{
    Process copy = *this;
    json::Writer w;
    w.beginObject().kv("schema", "process-1").kv("name", name).kv("version", version).kv("description", description);
    std::vector<std::string> open;   // the objects currently open, as path components
    for (const Field& f : fieldsOf(copy)) {
        auto parts = split(f.path);
        const std::string leafKey = parts.back();
        parts.pop_back();
        std::size_t common = 0;
        while (common < open.size() && common < parts.size() && open[common] == parts[common]) ++common;
        while (open.size() > common) { w.endObject(); open.pop_back(); }
        for (std::size_t k = common; k < parts.size(); ++k) { w.key(parts[k]).beginObject(); open.push_back(parts[k]); }
        if (f.b) w.kvBool(leafKey, *f.b); else if (f.i) w.kv(leafKey, *f.i); else w.kv(leafKey, *f.s);
    }
    while (!open.empty()) { w.endObject(); open.pop_back(); }
    w.endObject();
    return w.take();
}

std::expected<Process, std::string> Process::fromJson(std::string_view text)
{
    Process p;
    std::vector<std::pair<std::string, Scalar>> found;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) { walk(obj, "", found); });
    } catch (const simdjson::simdjson_error& e) { return std::unexpected(std::string{"bad process: "} + e.what()); }
    catch (const std::runtime_error& e) { return std::unexpected(std::string{"bad process: "} + e.what()); }
    p = Process{};
    bool schemaSeen = false;
    auto fields = fieldsOf(p);
    for (const auto& [path, v] : found) {
        if (path == "schema") { if (v.s != "process-1") return std::unexpected("unsupported process schema '" + v.s + "' (process-1 is the only one)"); schemaSeen = true; continue; }
        if (path == "name") { p.name = v.s; continue; }
        if (path == "version") { if (v.kind != Scalar::Kind::integer) return std::unexpected("'version' must be a number"); p.version = static_cast<int>(v.i); continue; }
        if (path == "description") { p.description = v.s; continue; }
        const auto it = std::find_if(fields.begin(), fields.end(), [&](const Field& f) { return f.path == path; });
        if (it == fields.end()) return std::unexpected("unknown key '" + path + "' (a process has: " + [] { std::string all; for (const auto& x : Process::parameterPaths()) all += (all.empty() ? "" : ", ") + x; return all; }() + ")");
        if (it->b && v.kind != Scalar::Kind::boolean) return std::unexpected("'" + path + "' must be true or false");
        if (it->i && v.kind != Scalar::Kind::integer) return std::unexpected("'" + path + "' must be a number");
        if (it->s && v.kind != Scalar::Kind::text) return std::unexpected("'" + path + "' must be a string");
        if (it->b) *it->b = v.b; else if (it->i) *it->i = static_cast<int>(v.i); else *it->s = v.s;
    }
    if (!schemaSeen) return std::unexpected("a process file needs \"schema\": \"process-1\"");
    if (const std::string bad = p.validate(); !bad.empty()) return std::unexpected(bad);
    return p;
}

std::expected<void, std::string> Process::set(std::string_view pathIn, std::string_view value)
{
    const std::string path{pathIn};
    Process trial = *this;
    if (path == "description") trial.description = std::string{value};
    else {
        auto fields = fieldsOf(trial);
        const auto it = std::find_if(fields.begin(), fields.end(), [&](const Field& f) { return f.path == path; });
        if (it == fields.end()) { std::string all; for (const auto& x : parameterPaths()) all += (all.empty() ? "" : ", ") + x; return std::unexpected("no parameter '" + path + "' (a process has: " + all + ")"); }
        if (it->b) {
            if (value != "true" && value != "false") return std::unexpected("'" + path + "' must be true or false, not '" + std::string{value} + "'");
            *it->b = value == "true";
        } else if (it->i) {
            const std::string v{value};
            char* end = nullptr;
            const long n = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || *end != '\0') return std::unexpected("'" + path + "' must be a whole number, not '" + v + "'");
            *it->i = static_cast<int>(n);
        } else *it->s = std::string{value};
    }
    if (const std::string bad = trial.validate(); !bad.empty()) return std::unexpected(bad);
    *this = std::move(trial);
    return {};
}

std::vector<std::string> Process::parameterPaths()
{
    Process p;
    std::vector<std::string> v;
    for (const Field& f : fieldsOf(p)) v.push_back(f.path);
    return v;
}

const Process& standardProcess()
{
    static const Process p = [] {
        Process x;
        x.name = "standard";
        x.version = 1;
        x.description = "Plan, build every item under the escalation ladder (retry, divide, send the tests back, rewrite the card, escalate), integrate. Today's behaviour.";
        return x;
    }();
    return p;
}

const Process& sectionsProcess()
{
    static const Process p = [] {
        Process x;
        x.name = "sections";
        x.version = 1;
        x.description = "Developers' items are written as forked sections from the first attempt, testers' on the canvas; a failing item is never divided into modules. For compiled platforms and for work that is long rather than wide.";
        x.leaf.developerExecutor = "fork";
        x.leaf.testerExecutor = "canvas";
        x.leaf.allowDecompose = false;
        return x;
    }();
    return p;
}

const std::vector<const Process*>& builtinProcesses()
{
    static const std::vector<const Process*> all = {&standardProcess(), &sectionsProcess()};
    return all;
}

std::expected<Process, std::string> findProcess(std::string_view name, const fs::path& userDir)
{
    for (const Process* p : builtinProcesses()) if (name == p->name) return *p;
    if (!validName(std::string{name})) return std::unexpected("'" + std::string{name} + "' is not a process name (a-z, 0-9 and '-')");
    if (!userDir.empty()) {
        const fs::path file = userDir / (std::string{name} + ".json");
        std::ifstream in(file, std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            auto p = Process::fromJson(ss.str());
            if (!p) return std::unexpected(file.string() + ": " + p.error());
            if (p->name != name) return std::unexpected(file.string() + ": the file holds a process named '" + p->name + "', not '" + std::string{name} + "'");
            return p;
        }
    }
    std::string known;
    for (const auto& n : processNames(userDir)) known += (known.empty() ? "" : ", ") + n;
    return std::unexpected("no process named '" + std::string{name} + "' (known: " + known + ")");
}

std::vector<std::string> processNames(const fs::path& userDir)
{
    std::vector<std::string> names;
    for (const Process* p : builtinProcesses()) names.push_back(p->name);
    std::vector<std::string> mine;
    std::error_code ec;
    if (!userDir.empty())
        for (auto it = fs::directory_iterator(userDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (it->path().extension() == ".json" && validName(it->path().stem().string()) &&
                std::find(names.begin(), names.end(), it->path().stem().string()) == names.end()) mine.push_back(it->path().stem().string());
    std::sort(mine.begin(), mine.end());
    names.insert(names.end(), mine.begin(), mine.end());
    return names;
}

}  // namespace sub0squad
