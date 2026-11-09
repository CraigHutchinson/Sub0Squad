#include "sub0squad/ledger.hpp"

#include "json_util.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <set>

namespace sub0squad {

double notionalCost(const std::string& tier, double tokensIn, double tokensOut)
{
    if (tier != "frontier-stub") return 0.0;
    return (tokensIn * 3.0 + tokensOut * 15.0) / 1000.0;  // notional units per 1000 tokens
}

std::string toJson(const LedgerRecord& r)
{
    json::Writer w;
    w.beginObject().kv("schema", "ledger-1").kv("ts", r.ts).kv("workflow", r.workflow).kv("item", r.item).kv("role", r.role).kv("job", r.job)
        .kv("run", r.run).kv("attempt", r.attempt).kv("tier", r.tier).kvBool("stub", r.stub).kv("outcome", r.outcome)
        .kv("durationSec", r.durationSec).kv("tokensIn", r.tokensIn).kv("tokensOut", r.tokensOut).kv("costUnits", r.costUnits)
        .kv("engine", r.engine).kv("tokensSystem", r.tokensSystem).kv("tokensCard", r.tokensCard).kv("tokensFeedback", r.tokensFeedback)
        .kv("tokensWork", r.tokensWork).kv("harnessBytes", r.harnessBytes).kv("decodeTps", r.decodeTps).endObject();
    return w.take();
}

bool appendLedger(const std::filesystem::path& file, const LedgerRecord& r)
{
    const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    std::string line = toJson(r);
    line.push_back('\n');
    const ssize_t n = ::write(fd, line.data(), line.size());
    ::close(fd);
    return n == static_cast<ssize_t>(line.size());
}

std::vector<LedgerRecord> readLedger(const std::filesystem::path& file)
{
    std::vector<LedgerRecord> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        LedgerRecord r;
        try {
            json::withObject(line, [&](simdjson::ondemand::object& obj) {
                for (auto f : obj) {
                    const std::string_view k = f.unescaped_key();
                    auto v = f.value();
                    if (k == "ts") r.ts = v.get_double();
                    else if (k == "workflow") r.workflow = json::toString(v);
                    else if (k == "item") r.item = json::toString(v);
                    else if (k == "role") r.role = json::toString(v);
                    else if (k == "job") r.job = json::toString(v);
                    else if (k == "tier") r.tier = json::toString(v);
                    else if (k == "outcome") r.outcome = json::toString(v);
                    else if (k == "run") r.run = static_cast<int>(v.get_int64());
                    else if (k == "attempt") r.attempt = static_cast<int>(v.get_int64());
                    else if (k == "stub") r.stub = v.get_bool();
                    else if (k == "durationSec") r.durationSec = v.get_double();
                    else if (k == "tokensIn") r.tokensIn = v.get_double();
                    else if (k == "tokensOut") r.tokensOut = v.get_double();
                    else if (k == "costUnits") r.costUnits = v.get_double();
                    else if (k == "engine") r.engine = json::toString(v);
                    else if (k == "tokensSystem") r.tokensSystem = v.get_double();
                    else if (k == "tokensCard") r.tokensCard = v.get_double();
                    else if (k == "tokensFeedback") r.tokensFeedback = v.get_double();
                    else if (k == "tokensWork") r.tokensWork = v.get_double();
                    else if (k == "harnessBytes") r.harnessBytes = v.get_double();
                    else if (k == "decodeTps") r.decodeTps = v.get_double();
                }
            });
            out.push_back(std::move(r));
        } catch (const simdjson::simdjson_error&) {}  // a torn last line is ignored
    }
    return out;
}

LedgerSummary summarise(const std::vector<LedgerRecord>& records, const std::string& workflow)
{
    LedgerSummary s;
    double all = 0, frontier = 0;
    std::set<std::string> gatePassed;  // "item#attempt"
    for (const auto& r : records)
        if ((workflow.empty() || r.workflow == workflow) && r.role == "gate" && r.outcome == "succeeded") gatePassed.insert(r.item + "#" + std::to_string(r.attempt));
    for (const auto& r : records) {
        if (!workflow.empty() && r.workflow != workflow) continue;
        auto& t = s.byTier[r.tier];
        ++t.attempts;
        t.tokensIn += r.tokensIn; t.tokensOut += r.tokensOut; t.costUnits += r.costUnits; t.durationSec += r.durationSec;
        all += r.tokensIn + r.tokensOut;
        if (r.tier == "frontier-stub") frontier += r.tokensIn + r.tokensOut;
        if (r.role != "agent" && r.role != "decompose" && r.role != "rearchitect") continue;  // architect jobs are overhead in the work ratio
        s.totalTokens += r.tokensIn + r.tokensOut;
        if (r.role != "agent") continue;
        const bool accepted = gatePassed.count(r.item + "#" + std::to_string(r.attempt)) > 0;
        (accepted ? s.acceptedWorkTokens : s.reworkTokens) += r.tokensWork;
        s.promptOverheadTokens += r.tokensSystem + r.tokensFeedback;
        s.harnessBytes = std::max(s.harnessBytes, r.harnessBytes);
        if (!r.engine.empty()) {
            auto& e = s.byEngine[r.engine];
            ++e.attempts; e.tokensIn += r.tokensIn; e.tokensOut += r.tokensOut;
            if (r.decodeTps > 0) { e.decodeTpsSum += r.decodeTps; ++e.decodeSamples; }
        }
    }
    s.frontierTokenShare = all > 0 ? frontier / all : 0.0;
    s.workRatio = s.totalTokens > 0 ? s.acceptedWorkTokens / s.totalTokens : 0.0;
    return s;
}

}  // namespace sub0squad
