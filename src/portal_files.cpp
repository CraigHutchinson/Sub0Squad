// The portal's view of a workflow's files (portal_impl.hpp): read only, and never outside the sandbox or a job's run directory.
#include "portal_impl.hpp"

namespace sub0squad::portal {

// ---- workflow packages: files of a workflow's sandbox, read only ----------------------------------------------------------------
std::string sandboxOf(Live& L, const std::string& wf)
{
    std::string text;
    { std::lock_guard lock(L.m); text = L.stateJson; }
    try {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded{text};
        auto doc = parser.iterate(padded);
        for (auto top : doc.get_object()) {
            if (top.unescaped_key().value() != "workflows") continue;
            for (auto inner : top.value().get_object()) {
                if (inner.unescaped_key().value() != "workflows") continue;
                for (auto w : inner.value().get_array()) {
                    std::string id, sandbox;
                    for (auto f : w.get_object()) {
                        const std::string_view k = f.unescaped_key();
                        if (k == "id") id = std::string{std::string_view{f.value().get_string()}};
                        else if (k == "sandbox") sandbox = std::string{std::string_view{f.value().get_string()}};
                    }
                    if (id == wf) return sandbox;
                }
            }
        }
    } catch (const simdjson::simdjson_error&) {}
    return {};
}

std::string urlDecode(const std::string& s)
{
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) { o += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)); i += 2; }
        else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}

std::string queryParam(const std::string& target, const std::string& name)
{
    const auto q = target.find('?');
    if (q == std::string::npos) return {};
    const std::string key = name + "=";
    for (std::size_t pos = q + 1; pos < target.size();) {
        const auto amp = target.find('&', pos);
        const std::string part = target.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (part.rfind(key, 0) == 0) return urlDecode(part.substr(key.size()));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
}

// A path inside the sandbox: relative, no "..", and after resolving symbolic links still under the sandbox root.
bool insideSandbox(const fs::path& root, const std::string& rel, fs::path& out)
{
    if (rel.empty() || rel[0] == '/' || rel.find("..") != std::string::npos || rel.find('\0') != std::string::npos) return false;
    std::error_code ec;
    const fs::path base = fs::weakly_canonical(root, ec);
    const fs::path full = fs::weakly_canonical(base / rel, ec);
    if (ec) return false;
    const std::string b = base.string() + "/", f = full.string() + "/";
    if (f.compare(0, b.size(), b) != 0) return false;
    out = full;
    return true;
}

const char* mimeOf(const std::string& path)
{
    const auto dot = path.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : path.substr(dot);
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "text/javascript; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".json") return "application/json";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".wasm") return "application/wasm";
    return "text/plain; charset=utf-8";
}

// A job's own files (its output and error streams, events, exit record) live beside the workflows, not inside one: `@jobs/j-000006/run-1/stdout.log`.
bool jobFilePath(const fs::path& sandbox, const std::string& rel, fs::path& out)
{
    if (rel.rfind("@jobs/", 0) != 0) return false;
    std::vector<std::string> parts;
    std::stringstream ss(rel.substr(6));
    for (std::string x; std::getline(ss, x, '/');) parts.push_back(x);
    const auto digitsAfter = [](const std::string& s, const char* prefix) {
        const std::string pre = prefix;
        return s.size() > pre.size() && s.compare(0, pre.size(), pre) == 0 && std::all_of(s.begin() + static_cast<long>(pre.size()), s.end(), [](unsigned char c) { return std::isdigit(c); });
    };
    static const std::set<std::string> files{"stdout.log", "stderr.log", "events.ndjson", "exit.json", "spec.json", "shim.json"};
    if (parts.size() != 3 || !digitsAfter(parts[0], "j-") || !digitsAfter(parts[1], "run-") || !files.count(parts[2])) return false;
    std::error_code ec;
    const fs::path jobs = fs::weakly_canonical(sandbox.parent_path().parent_path() / "jobs", ec);
    if (ec) return false;
    out = jobs / parts[0] / parts[1] / parts[2];
    return true;
}

// Everything worth opening in a sandbox, without the duplicated per-attempt workspaces (`ws/`): path and size.
std::string filesJson(const fs::path& root)
{
    json::Writer w;
    w.beginObject().kv("sandbox", root.string());
    std::string deliverables;
    { std::ifstream in(root / "deliverables.json"); std::ostringstream o; o << in.rdbuf(); deliverables = o.str(); }
    if (!deliverables.empty()) w.kvRaw("deliverables", deliverables); else w.key("deliverables").null();
    w.key("files").beginArray();
    std::error_code ec;
    int n = 0;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end && n < 3000; it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (it->is_directory(ec) && (name == "ws" || name == "node_modules" || name == ".git")) { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec)) continue;
        w.beginObject().kv("p", fs::relative(it->path(), root).generic_string()).kv("s", static_cast<std::int64_t>(it->file_size(ec))).endObject();
        ++n;
    }
    w.endArray().endObject();
    return w.take();
}

}  // namespace sub0squad::portal
