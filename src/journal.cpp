#include "sub0squad/journal.hpp"

#include "json_util.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sub0squad {

namespace {

constexpr const char* kSegment = "journal-000001.ndjson";

double wallNow()
{
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

bool parseLine(std::string_view line, JournalRecord& out)
{
    try {
        bool haveSeq = false, haveType = false, haveBody = false;
        json::withObject(line, [&](simdjson::ondemand::object& obj) {
            for (auto field : obj) {
                const std::string_view key = field.unescaped_key();
                auto value = field.value();
                if (key == "seq") { out.seq = value.get_uint64(); haveSeq = true; }
                else if (key == "ts") { out.ts = value.get_double(); }
                else if (key == "type") { out.type = json::toString(value); haveType = true; }
                else if (key == "d") { std::string_view raw = value.raw_json(); out.body.assign(raw); haveBody = true; }
            }
        });
        return haveSeq && haveType && haveBody;
    } catch (const simdjson::simdjson_error&) {
        return false;
    }
}

}  // namespace

std::expected<JournalReplay, std::string> Journal::replay(const std::filesystem::path& dir)
{
    JournalReplay out;
    const auto path = dir / kSegment;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (!std::filesystem::exists(path)) return out;  // no journal yet
        return std::unexpected("cannot read " + path.string());
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string data = ss.str();
    std::size_t pos = 0;
    std::uint64_t expected = 1;
    while (pos < data.size()) {
        const std::size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) { out.tornTail = true; break; }  // partial final line
        std::string_view line{data.data() + pos, nl - pos};
        JournalRecord rec;
        const bool good = parseLine(line, rec) && rec.seq == expected;
        if (!good) {
            if (nl + 1 == data.size()) { out.tornTail = true; break; }  // bad final line: treat as torn
            return std::unexpected("journal corrupt before its end: line starting at byte " + std::to_string(pos) + " in " + path.string());
        }
        out.records.push_back(std::move(rec));
        ++expected;
        pos = nl + 1;
        out.validBytes = pos;
    }
    return out;
}

std::expected<Journal, std::string> Journal::open(const std::filesystem::path& dir)
{
    auto replayed = replay(dir);
    if (!replayed) return std::unexpected(replayed.error());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / kSegment;
    auto file = sub0platform::AppendFile::open(path);
    if (!file) return std::unexpected("cannot open " + path.string() + ": " + file.error().message);
    if (replayed->tornTail)
        if (auto cut = file->truncate(replayed->validBytes); !cut) return std::unexpected("cannot truncate torn tail of " + path.string() + ": " + cut.error().message);
    Journal j;
    j.file_ = std::move(*file);
    j.records_ = std::move(replayed->records);
    j.nextSeq_ = j.records_.empty() ? 1 : j.records_.back().seq + 1;
    return j;
}

std::expected<std::uint64_t, std::string> Journal::append(std::string_view type, std::string_view bodyJson, bool durable)
{
    if (!file_.isOpen()) return std::unexpected("journal not open");
    const std::uint64_t seq = nextSeq_;
    json::Writer w;
    const double ts = wallNow();
    w.beginObject().key("seq").num(static_cast<std::int64_t>(seq)).key("ts").num(ts).kv("type", type).kvRaw("d", bodyJson).endObject();
    std::string line{w.view()};
    line.push_back('\n');
    if (auto w = file_.append(line); !w) return std::unexpected("journal write failed: " + w.error().message);
    if (durable)
        if (auto s = file_.sync(); !s) return std::unexpected("journal fdatasync failed: " + s.error().message);
    JournalRecord rec;
    rec.seq = seq;
    rec.ts = ts;
    rec.type = std::string{type};
    rec.body = std::string{bodyJson};
    records_.push_back(std::move(rec));
    ++nextSeq_;
    return seq;
}

}  // namespace sub0squad
