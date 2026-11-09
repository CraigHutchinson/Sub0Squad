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

}  // namespace sub0squad
