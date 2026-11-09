#include "sub0squad/progress.hpp"

#include "json_util.hpp"

#include <sys/stat.h>
#include <fstream>

namespace sub0squad {

Progress readProgress(const std::filesystem::path& runDir)
{
    Progress p;
    const auto path = runDir / "events.ndjson";
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0 || st.st_size == 0) return p;
    p.lastEventAt = static_cast<double>(st.st_mtim.tv_sec) + static_cast<double>(st.st_mtim.tv_nsec) / 1e9;
    constexpr std::streamoff kTail = 256 * 1024;
    std::ifstream in(path, std::ios::binary);
    std::streamoff start = st.st_size > kTail ? st.st_size - kTail : 0;
    in.seekg(start);
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t pos = 0;
    if (start > 0) { const auto nl = data.find('\n'); pos = nl == std::string::npos ? data.size() : nl + 1; }  // first line is partial
    while (pos < data.size()) {
        auto nl = data.find('\n', pos);
        if (nl == std::string::npos) break;
        const std::string_view line{data.data() + pos, nl - pos};
        pos = nl + 1;
        ++p.eventLines;
        try {
            std::string type, phase, text, name;
            std::int64_t done = 0, total = 0;
            double value = 0;
            json::withObject(line, [&](simdjson::ondemand::object& obj) {
                for (auto f : obj) {
                    const std::string_view k = f.unescaped_key();
                    auto v = f.value();
                    if (k == "type") type = json::toString(v);
                    else if (k == "done") done = v.get_int64();
                    else if (k == "total") total = v.get_int64();
                    else if (k == "phase") phase = json::toString(v);
                    else if (k == "text") text = json::toString(v);
                    else if (k == "name") name = json::toString(v);
                    else if (k == "value") value = v.get_double();
                }
            });
            if (type == "progress") { p.hasProgress = true; p.done = done; p.total = total; p.phase = phase; }
            else if (type == "metric" && !name.empty()) p.metrics[name] = value;
            else if (type == "note") p.lastNote = text;
        } catch (const simdjson::simdjson_error&) {
            // a malformed event line is skipped, never fatal
        }
    }
    return p;
}

Progress progressFromOptimizationState(std::string_view text)
{
    Progress p;
    p.adapterSchema = "optimization-run-1";
    try {
        std::int64_t attempts = 0, maxAttempts = 0, noProgress = 0, recoveries = 0;
        std::string status;
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "status") status = json::toString(v);
                else if (k == "no_progress") noProgress = v.get_int64();
                else if (k == "recoveries") recoveries = v.get_int64();
                else if (k == "attempts") { for (auto a : v.get_array()) { (void)a; ++attempts; } }
                else if (k == "item") {
                    for (auto g : v.get_object()) if (json::keyOf(g) == "max_attempts") maxAttempts = g.value().get_int64();
                }
            }
        });
        p.hasProgress = true;
        p.done = attempts;
        p.total = maxAttempts;
        p.phase = status;
        p.adapterStatus = status;
        p.metrics["attempts"] = static_cast<double>(attempts);
        p.metrics["no_progress"] = static_cast<double>(noProgress);
        p.metrics["recoveries"] = static_cast<double>(recoveries);
    } catch (const simdjson::simdjson_error&) {
        p.hasProgress = false;
        p.adapterStatus = "unreadable";
    }
    return p;
}

Progress readProgress(const std::filesystem::path& runDir, const ProgressAdapter& adapter, const std::string& cwd)
{
    Progress p = readProgress(runDir);
    if (adapter.type != "state-file" || adapter.schema != "optimization-run-1" || adapter.path.empty()) return p;
    std::filesystem::path path{adapter.path};
    if (path.is_relative() && !cwd.empty()) path = std::filesystem::path{cwd} / path;
    std::ifstream in(path, std::ios::binary);
    if (!in) return p;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Progress a = progressFromOptimizationState(text);
    p.adapterStatus = a.adapterStatus;
    p.adapterSchema = a.adapterSchema;
    if (!p.hasProgress) { p.hasProgress = a.hasProgress; p.done = a.done; p.total = a.total; p.phase = a.phase; }
    for (const auto& [name, v] : a.metrics) p.metrics.emplace(name, v);  // events win on a name clash
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) {
        const double mt = static_cast<double>(st.st_mtim.tv_sec) + static_cast<double>(st.st_mtim.tv_nsec) / 1e9;
        if (mt > p.lastEventAt) p.lastEventAt = mt;  // the tool's own record counts as liveness
    }
    return p;
}

}  // namespace sub0squad
