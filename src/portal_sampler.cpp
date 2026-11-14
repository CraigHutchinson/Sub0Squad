// The portal's sampler (portal_impl.hpp): the host and each engine, once a second, into bounded series; `liveJson` renders them.
#include "portal_impl.hpp"

namespace sub0squad::portal {

void rates(std::deque<std::array<double, 3>>& win, double dt, double dec, double pre, double& decTps, double& preTps)
{
    win.push_back({dt, dec, pre});
    while (win.size() > kRateWindow) win.pop_front();
    double t = 0, d = 0, p = 0;
    for (const auto& w : win) { t += w[0]; d += w[1]; p += w[2]; }
    decTps = t > 0 ? d / t : 0;
    preTps = t > 0 ? p / t : 0;
}

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

}  // namespace sub0squad::portal
