#include "sub0squad/portal.hpp"

#include "json_util.hpp"
#include "sub0squad/engine.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>

namespace sub0squad {

namespace {

namespace fs = std::filesystem;

// ---- sampled series -----------------------------------------------------------------------------------------------------------
constexpr std::size_t kKeep = 900;  // 15 minutes at 1 Hz

struct Ring
{
    std::deque<double> v;
    void push(double x) { v.push_back(x); if (v.size() > kKeep) v.pop_front(); }
    void write(json::Writer& w, std::size_t n) const
    {
        w.beginArray();
        for (std::size_t i = v.size() > n ? v.size() - n : 0; i < v.size(); ++i) w.num(v[i]);
        w.endArray();
    }
};

struct SlotLive
{
    bool processing = false;
    long promptTokens = 0, promptProcessed = 0, decoded = 0, remain = -1, idTask = -1;
    long prevDecoded = 0, prevProcessed = 0, prevTask = -2;
    double decodeTps = 0, prefillTps = 0;
    Ring decodeSeries, prefillSeries;
    std::deque<std::array<double, 3>> window;  ///< recent samples (seconds, tokens decoded, prompt tokens processed): the rate is a sum over the window
};

// /slots counts whole tokens, so one second at 5 to 8 tok/s reads as 5, 6, 7 or 8: the rate is taken over the last five samples instead,
// which resolves a fifth of a token per second.
constexpr std::size_t kRateWindow = 5;
void rates(std::deque<std::array<double, 3>>& win, double dt, double dec, double pre, double& decTps, double& preTps)
{
    win.push_back({dt, dec, pre});
    while (win.size() > kRateWindow) win.pop_front();
    double t = 0, d = 0, p = 0;
    for (const auto& w : win) { t += w[0]; d += w[1]; p += w[2]; }
    decTps = t > 0 ? d / t : 0;
    preTps = t > 0 ? p / t : 0;
}

struct ModelInfo
{
    bool have = false;
    double fetchedAt = 0;
    std::string alias, file, path, ftype, build;
    long long nParams = 0, sizeBytes = 0, nCtxTrain = 0;
    int totalSlots = 0;
};

struct EngineLive
{
    ModelInfo model;
    bool ok = false;
    std::string error;
    std::map<int, SlotLive> slots;
    Ring decode, prefill, active;
    std::deque<std::array<double, 3>> window;
    double lastT = 0;
};

struct Live
{
    std::mutex m;
    Ring cpu, mem, swap, load, gpu, vram, gtt;
    double memTotalMiB = 0, swapTotalMiB = 0, vramTotalMiB = 0, gttTotalMiB = 0;
    bool hasGpu = false;
    std::map<std::string, EngineLive> engines;
    std::set<std::string> bases;
    std::set<std::string> extra;
    std::string stateJson = "{\"workflows\":{\"workflows\":[]},\"jobs\":{\"jobs\":[]}}";  ///< refreshed by the sampler so a slow daemon never stalls the HTTP loop
};

double nowSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string slurp(const fs::path& p)
{
    std::ifstream in(p);
    std::ostringstream o;
    o << in.rdbuf();
    return o.str();
}

bool cpuTimes(double& busy, double& total)
{
    std::istringstream in(slurp("/proc/stat"));
    std::string tag;
    double user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    if (!(in >> tag >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal) || tag != "cpu") return false;
    total = user + nice + sys + idle + iowait + irq + softirq + steal;
    busy = total - idle - iowait;
    return true;
}

double meminfoKb(const std::string& text, const char* key)
{
    const auto p = text.find(key);
    return p == std::string::npos ? 0.0 : std::atof(text.c_str() + p + std::strlen(key));
}

double sysfsNum(const fs::path& p) { const std::string t = slurp(p); return t.empty() ? -1.0 : std::atof(t.c_str()); }

void sampleSystem(Live& L, double& prevBusy, double& prevTotal)
{
    double busy = 0, total = 0;
    double cpu = 0;
    if (cpuTimes(busy, total) && prevTotal > 0 && total > prevTotal) cpu = 100.0 * (busy - prevBusy) / (total - prevTotal);
    prevBusy = busy;
    prevTotal = total;
    const std::string mi = slurp("/proc/meminfo");
    const double memTotal = meminfoKb(mi, "MemTotal:") / 1024.0, memAvail = meminfoKb(mi, "MemAvailable:") / 1024.0;
    const double swapTotal = meminfoKb(mi, "SwapTotal:") / 1024.0, swapFree = meminfoKb(mi, "SwapFree:") / 1024.0;
    const double load = std::atof(slurp("/proc/loadavg").c_str());
    double gpu = -1, vram = -1, gtt = -1, vramTotal = 0, gttTotal = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/sys/class/drm", ec)) {
        const fs::path d = e.path() / "device";
        if (e.path().filename().string().find('-') != std::string::npos || !fs::exists(d / "gpu_busy_percent", ec)) continue;
        gpu = sysfsNum(d / "gpu_busy_percent");
        vram = sysfsNum(d / "mem_info_vram_used") / 1048576.0;
        gtt = sysfsNum(d / "mem_info_gtt_used") / 1048576.0;
        vramTotal = sysfsNum(d / "mem_info_vram_total") / 1048576.0;
        gttTotal = sysfsNum(d / "mem_info_gtt_total") / 1048576.0;
        break;
    }
    std::lock_guard lock(L.m);
    L.cpu.push(cpu);
    L.mem.push(memTotal > 0 ? 100.0 * (memTotal - memAvail) / memTotal : 0);
    L.swap.push(swapTotal - swapFree);
    L.load.push(load);
    L.memTotalMiB = memTotal;
    L.swapTotalMiB = swapTotal;
    L.hasGpu = gpu >= 0;
    if (L.hasGpu) { L.gpu.push(gpu); L.vram.push(vram); L.gtt.push(gtt); L.vramTotalMiB = vramTotal; L.gttTotalMiB = gttTotal; }
}

// llama-server `/slots`: per slot processing flag, prompt tokens and how many were processed (prefill progress), tokens decoded so far.
void sampleEngine(Live& L, const std::string& base)
{
    const auto resp = httpRequest("GET", base + "/slots", "", 2);
    const double t = nowSec();
    std::lock_guard lock(L.m);
    EngineLive& e = L.engines[base];
    if (!resp || resp->status != 200) { e.ok = false; e.error = resp ? "HTTP " + std::to_string(resp->status) : resp.error(); return; }
    e.ok = true;
    e.error.clear();
    const double dt = e.lastT > 0 ? t - e.lastT : 0;
    double sumDecode = 0, sumPrefill = 0, rawDec = 0, rawPre = 0;
    int active = 0;
    try {
        simdjson::ondemand::parser parser;
        simdjson::padded_string text{resp->body};
        auto doc = parser.iterate(text);
        for (auto slotVal : doc.get_array()) {
            auto o = slotVal.get_object();
            int id = -1;
            SlotLive n;
            for (auto f : o) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "id") id = static_cast<int>(v.get_int64());
                else if (k == "is_processing") n.processing = v.get_bool();
                else if (k == "id_task") { auto r = v.get_int64(); if (!r.error()) n.idTask = r.value(); }
                else if (k == "n_prompt_tokens") n.promptTokens = static_cast<long>(v.get_int64());
                else if (k == "n_prompt_tokens_processed") n.promptProcessed = static_cast<long>(v.get_int64());
                else if (k == "next_token") {
                    for (auto nt : v.get_array()) {
                        for (auto g : nt.get_object()) {
                            const std::string_view kk = g.unescaped_key();
                            if (kk == "n_decoded") n.decoded = static_cast<long>(g.value().get_int64());
                            else if (kk == "n_remain") n.remain = static_cast<long>(g.value().get_int64());
                        }
                        break;
                    }
                }
            }
            if (id < 0) continue;
            SlotLive& s = e.slots[id];
            const bool sameTask = s.prevTask == n.idTask;
            const long dDec = sameTask ? std::max(0L, n.decoded - s.prevDecoded) : (n.processing ? n.decoded : 0);
            const long dPre = sameTask ? std::max(0L, n.promptProcessed - s.prevProcessed) : (n.processing ? n.promptProcessed : 0);
            if (dt > 0) rates(s.window, dt, static_cast<double>(dDec), static_cast<double>(dPre), s.decodeTps, s.prefillTps);
            else { s.decodeTps = 0; s.prefillTps = 0; }
            rawDec += static_cast<double>(dDec);
            rawPre += static_cast<double>(dPre);
            s.decodeSeries.push(s.decodeTps);
            s.prefillSeries.push(s.prefillTps);
            s.processing = n.processing;
            s.promptTokens = n.promptTokens;
            s.promptProcessed = n.promptProcessed;
            s.decoded = n.decoded;
            s.remain = n.remain;
            s.idTask = n.idTask;
            s.prevDecoded = n.decoded;
            s.prevProcessed = n.promptProcessed;
            s.prevTask = n.idTask;
            sumDecode += s.decodeTps;
            sumPrefill += s.prefillTps;
            if (n.processing) ++active;
        }
    } catch (const simdjson::simdjson_error& err) { e.ok = false; e.error = err.what(); return; }
    if (dt > 0) rates(e.window, dt, rawDec, rawPre, sumDecode, sumPrefill);
    e.decode.push(sumDecode);
    e.prefill.push(sumPrefill);
    e.active.push(active);
    e.lastT = t;
}

// Which model a llama-server runs: `/props` (alias, file, quantisation, build, slots) and `/v1/models` meta (parameters, size, training context).
void fetchModel(Live& L, const std::string& base)
{
    ModelInfo m;
    if (auto r = httpRequest("GET", base + "/props", "", 2); r && r->status == 200) {
        try {
            simdjson::ondemand::parser parser;
            simdjson::padded_string text{r->body};
            auto doc = parser.iterate(text);
            for (auto f : doc.get_object()) {
                const std::string_view k = f.unescaped_key();
                if (k == "model_alias") m.alias = std::string{std::string_view{f.value().get_string()}};
                else if (k == "model_path") { m.path = std::string{std::string_view{f.value().get_string()}}; m.file = fs::path(m.path).filename().string(); }
                else if (k == "model_ftype") m.ftype = std::string{std::string_view{f.value().get_string()}};
                else if (k == "build_info") m.build = std::string{std::string_view{f.value().get_string()}};
                else if (k == "total_slots") m.totalSlots = static_cast<int>(f.value().get_int64());
            }
            m.have = true;
        } catch (const simdjson::simdjson_error&) {}
    }
    if (auto r = httpRequest("GET", base + "/v1/models", "", 2); r && r->status == 200) {
        try {
            simdjson::ondemand::parser parser;
            simdjson::padded_string text{r->body};
            auto doc = parser.iterate(text);
            for (auto top : doc.get_object()) {
                if (top.unescaped_key().value() != "data") continue;
                for (auto entry : top.value().get_array()) {
                    for (auto f : entry.get_object()) {
                        if (f.unescaped_key().value() != "meta") continue;
                        for (auto mf : f.value().get_object()) {
                            const std::string_view k = mf.unescaped_key();
                            if (k == "n_params") m.nParams = mf.value().get_int64();
                            else if (k == "size") m.sizeBytes = mf.value().get_int64();
                            else if (k == "n_ctx_train") m.nCtxTrain = mf.value().get_int64();
                        }
                    }
                    break;
                }
            }
        } catch (const simdjson::simdjson_error&) {}
    }
    m.fetchedAt = nowSec();
    std::lock_guard lock(L.m);
    L.engines[base].model = m;
}

// Loopback llama-server bases named in text (the workflow engine profiles): http://127.0.0.1:PORT or http://localhost:PORT.
void discover(Live& L, const std::string& text)
{
    for (const char* host : {"http://127.0.0.1:", "http://localhost:"}) {
        for (std::size_t pos = 0; (pos = text.find(host, pos)) != std::string::npos;) {
            pos += std::strlen(host);
            std::size_t e = pos;
            while (e < text.size() && std::isdigit(static_cast<unsigned char>(text[e]))) ++e;
            if (e > pos) { std::lock_guard lock(L.m); L.bases.insert(std::string{"http://127.0.0.1:"} + text.substr(pos, e - pos)); }
            pos = e;
        }
    }
}

std::string liveJson(Live& L, std::size_t n)
{
    std::lock_guard lock(L.m);
    json::Writer w;
    w.beginObject().kv("keep", static_cast<std::int64_t>(kKeep));
    w.key("system").beginObject().kv("memTotalMiB", L.memTotalMiB).kv("swapTotalMiB", L.swapTotalMiB).kvBool("hasGpu", L.hasGpu)
        .kv("vramTotalMiB", L.vramTotalMiB).kv("gttTotalMiB", L.gttTotalMiB);
    const std::pair<const char*, const Ring*> sys[] = {{"cpu", &L.cpu}, {"mem", &L.mem}, {"swapMiB", &L.swap}, {"load", &L.load}, {"gpu", &L.gpu}, {"vramMiB", &L.vram}, {"gttMiB", &L.gtt}};
    for (const auto& [name, ring] : sys) { w.key(name); ring->write(w, n); }
    w.endObject();
    w.key("engines").beginArray();
    for (const auto& [base, e] : L.engines) {
        w.beginObject().kv("base", base).kvBool("ok", e.ok).kv("error", e.error);
        w.key("model").beginObject().kv("alias", e.model.alias).kv("file", e.model.file).kv("path", e.model.path).kv("ftype", e.model.ftype).kv("build", e.model.build)
            .kv("nParams", static_cast<std::int64_t>(e.model.nParams)).kv("sizeBytes", static_cast<std::int64_t>(e.model.sizeBytes))
            .kv("nCtxTrain", static_cast<std::int64_t>(e.model.nCtxTrain)).kv("totalSlots", e.model.totalSlots).endObject();
        w.key("decodeTps"); e.decode.write(w, n);
        w.key("prefillTps"); e.prefill.write(w, n);
        w.key("active"); e.active.write(w, n);
        w.key("slots").beginArray();
        for (const auto& [id, s] : e.slots) {
            w.beginObject().kv("id", id).kvBool("processing", s.processing).kv("promptTokens", static_cast<std::int64_t>(s.promptTokens))
                .kv("promptProcessed", static_cast<std::int64_t>(s.promptProcessed)).kv("decoded", static_cast<std::int64_t>(s.decoded))
                .kv("remain", static_cast<std::int64_t>(s.remain)).kv("decodeTps", s.decodeTps).kv("prefillTps", s.prefillTps);
            w.key("decodeSeries"); s.decodeSeries.write(w, n);
            w.key("prefillSeries"); s.prefillSeries.write(w, n);
            w.endObject();
        }
        w.endArray().endObject();
    }
    w.endArray().endObject();
    return w.take();
}

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

// ---- HTTP -----------------------------------------------------------------------------------------------------------------------
void sendAll(int fd, const std::string& s)
{
    std::size_t off = 0;
    while (off < s.size()) {
        const ssize_t n = ::send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return;
        off += static_cast<std::size_t>(n);
    }
}

void respond(int fd, int code, const char* type, const std::string& body, const char* extraHeaders = "")
{
    const char* text = code == 200 ? "OK" : code == 404 ? "Not Found" : "Method Not Allowed";
    sendAll(fd, "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n" + extraHeaders + "Connection: close\r\n\r\n" + body);
}

}  // namespace

namespace {
std::string randomToken()
{
    unsigned char b[16] = {};
    if (std::FILE* f = std::fopen("/dev/urandom", "rb")) { (void)!std::fread(b, 1, sizeof b, f); std::fclose(f); }
    static const char* hex = "0123456789abcdef";
    std::string t;
    for (unsigned char c : b) { t += hex[c >> 4]; t += hex[c & 15]; }
    return t;
}
std::string headerValue(const std::string& req, const std::string& name)  // case-insensitive, first occurrence
{
    std::string lower = req;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    const std::string key = "\r\n" + name + ":";
    const auto p = lower.find(key);
    if (p == std::string::npos) return {};
    auto s = p + key.size();
    while (s < req.size() && req[s] == ' ') ++s;
    return req.substr(s, req.find("\r\n", s) - s);
}
}  // namespace

int servePortal(int port, const std::function<std::string()>& state, const std::vector<std::string>& extraEngines, const PortalAct& act)
{
    const std::string token = randomToken();
    const int srv = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (srv < 0) { std::perror("portal: socket"); return 1; }
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);  // loopback only: the portal is never reachable from another machine
    if (::bind(srv, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(srv, 16) != 0) { std::perror("portal: bind/listen"); ::close(srv); return 1; }

    // Generated sites are served from a second loopback port, a different origin from the portal: a page the models wrote cannot read the
    // portal's API (no CORS headers there), and its own responses forbid any network access (CSP connect-src 'none', default-src to itself).
    const int siteSrv = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    {
        int one2 = 1;
        ::setsockopt(siteSrv, SOL_SOCKET, SO_REUSEADDR, &one2, sizeof one2);
        sockaddr_in b{};
        b.sin_family = AF_INET;
        b.sin_port = htons(static_cast<std::uint16_t>(port + 1));
        ::inet_pton(AF_INET, "127.0.0.1", &b.sin_addr);
        if (siteSrv < 0 || ::bind(siteSrv, reinterpret_cast<sockaddr*>(&b), sizeof b) != 0 || ::listen(siteSrv, 16) != 0) { std::perror("portal: site port"); return 1; }
    }
    Live live;
    for (const auto& b : extraEngines) live.bases.insert(b);
    std::jthread sampler([&live, &state](std::stop_token st) {
        double prevBusy = 0, prevTotal = 0;
        int tick = 0;
        while (!st.stop_requested()) {
            const auto next = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            sampleSystem(live, prevBusy, prevTotal);
            if (tick++ % 2 == 0) {
                std::string fresh = state();  // may take a while when the host is thrashing: only this thread waits for it
                discover(live, fresh);
                std::lock_guard lock(live.m);
                live.stateJson = std::move(fresh);
            }
            std::set<std::string> bases;
            { std::lock_guard lock(live.m); bases = live.bases; }
            for (const auto& b : bases) {
                bool need = false;
                { std::lock_guard lock(live.m); const auto& m = live.engines[b].model; need = !m.have || nowSec() - m.fetchedAt > 60; }
                if (need) fetchModel(live, b);
                sampleEngine(live, b);
            }
            std::this_thread::sleep_until(next);
        }
    });
    std::fprintf(stderr, "portal: http://127.0.0.1:%d/  (read-only, loopback only; Ctrl-C to stop)\n", port);
    for (;;) {
        pollfd pf[2] = {{srv, POLLIN, 0}, {siteSrv, POLLIN, 0}};
        if (::poll(pf, 2, -1) <= 0) continue;
        const bool onSite = !(pf[0].revents & POLLIN);
        const int c = ::accept4(onSite ? siteSrv : srv, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0) continue;
        timeval tv{2, 0};
        ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        std::string req;
        char buf[2048];
        while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) break;
            req.append(buf, static_cast<std::size_t>(n));
        }
        if (req.rfind("POST ", 0) == 0) {  // the owner's inbox: same-origin, tokened, small
            const std::string target = req.substr(5, req.find(' ', 5) - 5);
            const auto headEnd = req.find("\r\n\r\n");
            const std::size_t want = static_cast<std::size_t>(std::clamp<long>(std::atol(headerValue(req, "content-length").c_str()), 0, 65536));
            while (headEnd != std::string::npos && req.size() < headEnd + 4 + want) {
                const ssize_t n = ::recv(c, buf, sizeof buf, 0);
                if (n <= 0) break;
                req.append(buf, static_cast<std::size_t>(n));
            }
            const std::string origin = headerValue(req, "origin"), host = headerValue(req, "host");
            const std::string self1 = "127.0.0.1:" + std::to_string(port), self2 = "localhost:" + std::to_string(port);
            const bool sameOrigin = (origin.empty() || origin == "http://" + self1 || origin == "http://" + self2) && (host == self1 || host == self2);
            if (!act || onSite || (target != "/api/raise" && target != "/api/answer")) respond(c, 404, "text/plain", "not found\n");
            else if (!sameOrigin || headerValue(req, "x-portal-token") != token) respond(c, 403, "text/plain", "forbidden\n");
            else {
                std::string wf, spec;
                try {
                    simdjson::ondemand::parser parser;
                    simdjson::padded_string body{req.substr(headEnd + 4)};
                    auto doc = parser.iterate(body);
                    for (auto f : doc.get_object()) {
                        const std::string_view k = f.unescaped_key();
                        if (k == "wf") wf = std::string{std::string_view{f.value().get_string()}};
                        else if (k == "spec") spec = std::string{f.value().raw_json().value()};
                    }
                } catch (const simdjson::simdjson_error&) {}
                const bool okId = !wf.empty() && wf.size() < 20 && std::all_of(wf.begin(), wf.end(), [](unsigned char ch) { return std::isalnum(ch) || ch == '-'; });
                if (!okId || spec.empty()) respond(c, 400, "application/json", "{\"ok\":false,\"error\":\"bad request\"}");
                else respond(c, 200, "application/json", act(target == "/api/raise" ? "requestRaise" : "requestAnswer", wf, spec));
            }
        }
        else if (req.rfind("GET ", 0) != 0) respond(c, 405, "text/plain", "read-only portal: GET and the two inbox POSTs only\n");
        else {
            const std::string target = req.substr(4, req.find(' ', 4) - 4);
            const std::string path = target.substr(0, target.find('?'));
            if (onSite && path.rfind("/site/", 0) != 0) respond(c, 404, "text/plain", "this port serves /site/<workflow>/ only\n");
            else if (path == "/" || path == "/index.html") {
                std::string page = portalPage();
                const std::string ph = "__PORTAL_TOKEN__";
                for (auto at = page.find(ph); at != std::string::npos; at = page.find(ph, at + token.size())) page.replace(at, ph.size(), token);
                respond(c, 200, "text/html; charset=utf-8", page);
            }
            else if (path == "/api/state") { std::string cached; { std::lock_guard lock(live.m); cached = live.stateJson; } respond(c, 200, "application/json", cached); }
            else if (path == "/api/live") {
                std::size_t n = 120;
                if (const auto q = target.find("n="); q != std::string::npos) n = std::clamp<long>(std::atol(target.c_str() + q + 2), 10, static_cast<long>(kKeep));
                respond(c, 200, "application/json", liveJson(live, n));
            }
            else if (path == "/api/files") {  // the packages of one workflow: its files and its deliverables manifest
                const std::string root = sandboxOf(live, queryParam(target, "wf"));
                if (root.empty()) respond(c, 404, "text/plain", "unknown workflow\n"); else respond(c, 200, "application/json", filesJson(root));
            }
            else if (path == "/api/file") {  // one file as plain text (the request, a prompt, a reply, a gate output, an artifact), capped
                const std::string root = sandboxOf(live, queryParam(target, "wf"));
                fs::path full;
                if (root.empty() || !(jobFilePath(root, queryParam(target, "p"), full) || insideSandbox(root, queryParam(target, "p"), full)) || !fs::is_regular_file(full)) respond(c, 404, "text/plain", "not found\n");
                else { std::string body = slurp(full); const bool cut = body.size() > 400000; if (cut) body.resize(400000); respond(c, 200, "text/plain; charset=utf-8", body + (cut ? "\n[... cut at 400 KB]\n" : "")); }
            }
            else if (onSite && path.rfind("/site/", 0) != 0) respond(c, 404, "text/plain", "this port serves /site/<workflow>/ only\n");
            else if (onSite) {  // the integrated project served as a site, in an opaque sandboxed origin with no network
                const std::string rest = urlDecode(path.substr(6));
                const auto slash = rest.find('/');
                const std::string wf = rest.substr(0, slash);
                std::string rel = slash == std::string::npos ? "" : rest.substr(slash + 1);
                if (rel.empty() || rel.back() == '/') rel += "index.html";
                const std::string root = sandboxOf(live, wf);
                fs::path full;
                if (root.empty() || !insideSandbox(fs::path(root) / "project", rel, full) || !fs::is_regular_file(full)) respond(c, 404, "text/plain", "not found\n");
                else respond(c, 200, mimeOf(full.string()), slurp(full), ("Content-Security-Policy: default-src http://127.0.0.1:" + std::to_string(port + 1) + " 'unsafe-inline'; connect-src 'none'\r\n").c_str());
            }
            else if (path == "/healthz") respond(c, 200, "text/plain", "ok\n");
            else respond(c, 404, "text/plain", "not found\n");
        }
        ::close(c);
    }
}

}  // namespace sub0squad
