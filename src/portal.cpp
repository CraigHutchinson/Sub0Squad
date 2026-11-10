#include "sub0squad/portal.hpp"

#include "json_util.hpp"
#include "sub0squad/engine.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
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
};

struct EngineLive
{
    bool ok = false;
    std::string error;
    std::map<int, SlotLive> slots;
    Ring decode, prefill, active;
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
    double sumDecode = 0, sumPrefill = 0;
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
            s.decodeTps = dt > 0 ? dDec / dt : 0;
            s.prefillTps = dt > 0 ? dPre / dt : 0;
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
    e.decode.push(sumDecode);
    e.prefill.push(sumPrefill);
    e.active.push(active);
    e.lastT = t;
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

void respond(int fd, int code, const char* type, const std::string& body)
{
    const char* text = code == 200 ? "OK" : code == 404 ? "Not Found" : "Method Not Allowed";
    sendAll(fd, "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n" + body);
}

}  // namespace

int servePortal(int port, const std::function<std::string()>& state, const std::vector<std::string>& extraEngines)
{
    const int srv = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (srv < 0) { std::perror("portal: socket"); return 1; }
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);  // loopback only: the portal is never reachable from another machine
    if (::bind(srv, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(srv, 16) != 0) { std::perror("portal: bind/listen"); ::close(srv); return 1; }

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
            for (const auto& b : bases) sampleEngine(live, b);
            std::this_thread::sleep_until(next);
        }
    });
    std::fprintf(stderr, "portal: http://127.0.0.1:%d/  (read-only, loopback only; Ctrl-C to stop)\n", port);
    for (;;) {
        const int c = ::accept4(srv, nullptr, nullptr, SOCK_CLOEXEC);
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
        if (req.rfind("GET ", 0) != 0) respond(c, 405, "text/plain", "read-only portal: GET only\n");
        else {
            const std::string target = req.substr(4, req.find(' ', 4) - 4);
            const std::string path = target.substr(0, target.find('?'));
            if (path == "/" || path == "/index.html") respond(c, 200, "text/html; charset=utf-8", portalPage());
            else if (path == "/api/state") { std::string cached; { std::lock_guard lock(live.m); cached = live.stateJson; } respond(c, 200, "application/json", cached); }
            else if (path == "/api/live") {
                std::size_t n = 120;
                if (const auto q = target.find("n="); q != std::string::npos) n = std::clamp<long>(std::atol(target.c_str() + q + 2), 10, static_cast<long>(kKeep));
                respond(c, 200, "application/json", liveJson(live, n));
            }
            else if (path == "/healthz") respond(c, 200, "text/plain", "ok\n");
            else respond(c, 404, "text/plain", "not found\n");
        }
        ::close(c);
    }
}

}  // namespace sub0squad
