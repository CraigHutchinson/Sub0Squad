#include "sub0squad/throughput.hpp"

#include <algorithm>
#include <limits>

namespace sub0squad {

ParallelPlan planParallel(int sections, int idleSlots, int requested, const std::string& sequentialReason, long availableMiB, long memFloorMiB)
{
    ParallelPlan p;
    if (sections <= 1) { p.parallel = 1; p.reason = "one section"; return p; }
    if (!sequentialReason.empty()) { p.parallel = 1; p.reason = "sequential: " + sequentialReason; return p; }
    if (memFloorMiB > 0 && availableMiB >= 0 && availableMiB < memFloorMiB) {
        p.parallel = 1;
        p.shed = true;
        p.reason = "shed: memory pressure (" + std::to_string(availableMiB) + " MiB available, floor " + std::to_string(memFloorMiB) + " MiB): forks are shed first";
        return p;
    }
    const int width = requested > 0 ? requested : std::max(1, idleSlots);
    p.parallel = std::clamp(width, 1, sections);
    p.reason = requested > 0 ? "requested width " + std::to_string(requested)
             : p.parallel == sections ? "every section at once (" + std::to_string(idleSlots) + " idle slots)"
                                      : "limited by " + std::to_string(std::max(1, idleSlots)) + " idle slot(s)";
    return p;
}

double SlotModel::perSequenceTps(int n) const
{
    if (n <= 1 || aggregateScale.empty()) return singleTps;
    const std::size_t i = std::min(static_cast<std::size_t>(n), aggregateScale.size()) - 1;
    return singleTps * aggregateScale[i] / static_cast<double>(n);
}

ThroughputRun accountSpans(std::vector<JobSpan> spans, int width, long completionTokens)
{
    ThroughputRun r;
    r.spans = spans;
    if (spans.empty()) return r;
    width = std::max(1, width);
    double first = std::numeric_limits<double>::max(), last = 0, lastStart = 0;
    for (const auto& s : spans) { first = std::min(first, s.start); last = std::max(last, s.end); lastStart = std::max(lastStart, s.start); }
    r.wallSec = last - first;
    // Sweep the period in which a job was still waiting to start (first start to last start): slot-time not in use there is idle.
    std::vector<std::pair<double, int>> events;
    for (const auto& s : spans) { events.emplace_back(s.start, 1); events.emplace_back(s.end, -1); }
    std::sort(events.begin(), events.end());
    int active = 0;
    double at = first;
    for (const auto& [t, d] : events) {
        const double until = std::min(t, lastStart);
        if (until > at) { r.idleSlotSec += (until - at) * static_cast<double>(std::max(0, width - active)); at = until; }
        active += d;
    }
    const double waiting = lastStart - first;
    r.idleShare = waiting > 0 ? r.idleSlotSec / (waiting * static_cast<double>(width)) : 0.0;
    r.aggregateTps = r.wallSec > 0 ? static_cast<double>(completionTokens) / r.wallSec : 0.0;
    return r;
}

ThroughputRun simulate(const SlotModel& model, const std::vector<SectionJob>& jobs, int width)
{
    width = std::clamp(width, 1, std::max(1, model.slots));
    struct Live { std::size_t job; double prefillLeft, decodeLeft, start; };
    std::vector<Live> live;
    std::vector<JobSpan> spans(jobs.size());
    std::size_t next = 0;
    double now = 0;
    long tokens = 0;
    for (const auto& j : jobs) tokens += j.completionTokens;
    const auto admit = [&] {
        while (next < jobs.size() && static_cast<int>(live.size()) < width) {
            const auto& j = jobs[next];
            const double prefill = model.prefillTps > 0 ? static_cast<double>(std::max(0L, j.promptTokens - j.cachedTokens)) / model.prefillTps : 0.0;
            live.push_back({next, prefill, static_cast<double>(j.completionTokens), now});
            spans[next] = {j.name, now, now};
            ++next;
        }
    };
    admit();
    while (!live.empty()) {
        int decoding = 0;
        for (const auto& l : live) if (l.prefillLeft <= 0) ++decoding;
        const double tps = model.perSequenceTps(std::max(1, decoding));
        // The time to the next event: a prefill ending or a decode ending.
        double step = std::numeric_limits<double>::max();
        for (const auto& l : live) step = std::min(step, l.prefillLeft > 0 ? l.prefillLeft : (tps > 0 ? l.decodeLeft / tps : 0.0));
        if (!(step > 0) && tps <= 0) break;   // an engine that decodes nothing: stop rather than spin
        now += step;
        for (auto& l : live) { if (l.prefillLeft > 0) l.prefillLeft -= step; else l.decodeLeft -= step * tps; }
        for (auto it = live.begin(); it != live.end();) {
            if (it->prefillLeft <= 1e-12 && it->decodeLeft <= 1e-9) { spans[it->job].end = now; it = live.erase(it); }
            else { if (it->prefillLeft < 1e-12) it->prefillLeft = 0; ++it; }
        }
        admit();
    }
    return accountSpans(spans, width, tokens);
}

ParallelMetrics parallelMetrics(const SlotModel& model, const SectionJob& skeleton, const std::vector<SectionJob>& sections, const SectionJob& integration, int width)
{
    ParallelMetrics m;
    m.width = std::clamp(width, 1, std::max(1, model.slots));
    m.singleTps = model.singleTps;
    const double fixed = simulate(model, {skeleton}, 1).wallSec + simulate(model, {integration}, 1).wallSec;   // the same either way: one session each
    const ThroughputRun seq = simulate(model, sections, 1), par = simulate(model, sections, m.width);
    m.speedUp = fixed + par.wallSec > 0 ? (fixed + seq.wallSec) / (fixed + par.wallSec) : 0.0;
    m.idleShare = par.idleShare;
    m.aggregateTps = par.aggregateTps;
    long coordination = skeleton.promptTokens + skeleton.completionTokens + integration.promptTokens + integration.completionTokens, all = coordination;
    for (const auto& s : sections) all += s.promptTokens + s.completionTokens;
    m.coordinationShare = all > 0 ? static_cast<double>(coordination) / static_cast<double>(all) : 0.0;
    return m;
}

}  // namespace sub0squad
