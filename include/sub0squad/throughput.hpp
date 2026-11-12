#pragma once
/// @file throughput.hpp
/// Throughput by default (docs/BLOCK_PROVING_GROUNDS.md, principle 2): work that divides into independent sections is generated in parallel up to the
/// idle slots; a sequential path needs a stated reason; under memory pressure forks are the first thing shed. This unit holds the rule as a pure function,
/// the model of an engine's slots (single-stream decode speed and how aggregate decode scales with concurrent sequences), a deterministic simulation of a
/// set of sections on that engine, and the metrics of the parallel-sections proving ground (block 6): speed-up, utilisation, coordination cost.

#include <string>
#include <vector>

namespace sub0squad {

/// How many sections to generate at once, and why.
struct ParallelPlan
{
    int parallel = 1;
    std::string reason;   ///< why it is not more (a stated sequential reason, memory pressure, the number of idle slots, the number of sections)
    bool shed = false;    ///< memory pressure reduced it
};
/// The rule. `requested` > 0 fixes the width (an experiment); otherwise it is the smaller of `sections` and `idleSlots`. A non-empty `sequentialReason`
/// gives 1 (sections that are coupled: one needs another's result). `availableMiB` below `memFloorMiB` (> 0) gives 1: forks are shed first.
[[nodiscard]] ParallelPlan planParallel(int sections, int idleSlots, int requested, const std::string& sequentialReason, long availableMiB, long memFloorMiB);

/// An engine as the scheduler sees it: slots, the decode speed of one sequence alone, and the aggregate decode speed relative to it when n sequences
/// decode at once (`aggregateScale[n-1]`; a mixture-of-experts model on a CPU reads its weights once per step for all sequences, so the aggregate rises
/// with n until compute binds). The default curve is a PLACEHOLDER shape, not a measurement of any model: the 35B's curve is to be measured.
struct SlotModel
{
    int slots = 4;
    double singleTps = 10.0;                                   ///< decode tokens per second of one sequence alone
    std::vector<double> aggregateScale = {1.0, 1.7, 2.3, 2.8};  ///< aggregate decode at n concurrent sequences, relative to one
    double prefillTps = 300.0;                                 ///< prompt tokens per second (not shared between slots)
    /// Decode tokens per second of EACH of `n` concurrent sequences.
    [[nodiscard]] double perSequenceTps(int n) const;
};

/// One generation request: its prompt (of which `cachedTokens` need no prefill) and its reply.
struct SectionJob { std::string name; long promptTokens = 0, cachedTokens = 0, completionTokens = 0; };

/// When a job ran, in seconds from the start.
struct JobSpan { std::string name; double start = 0, end = 0; };

/// The outcome of running jobs on an engine at a given width.
struct ThroughputRun
{
    double wallSec = 0;          ///< first start to last end
    double idleSlotSec = 0;      ///< slot-time left idle (of the `width` slots in use) while a job was still waiting to start
    double idleShare = 0;        ///< idleSlotSec over width x the time during which a job was waiting
    double aggregateTps = 0;     ///< completion tokens over the wall time
    std::vector<JobSpan> spans;
};
/// Deterministic simulation: `jobs` are started in order on `width` slots (never more than the model's), each prefills alone and decodes at the
/// per-sequence speed of however many are decoding at that moment.
[[nodiscard]] ThroughputRun simulate(const SlotModel& model, const std::vector<SectionJob>& jobs, int width);
/// The same idle-slot accounting over spans that were MEASURED (a real or mock run): idle slot-time while a job had not started yet.
[[nodiscard]] ThroughputRun accountSpans(std::vector<JobSpan> spans, int width, long completionTokens);

/// The block 6 metrics of one fixed task: sequential against parallel.
struct ParallelMetrics
{
    int width = 1;
    double speedUp = 0;           ///< sequential wall time over parallel wall time, both including the skeleton and the integration
    double idleShare = 0;         ///< of the parallel run
    double coordinationShare = 0; ///< tokens spent on the skeleton and the integration over all tokens of the parallel run
    double aggregateTps = 0, singleTps = 0;
};
/// Sections after a skeleton and before an integration step, sequentially (width 1) and at `width`: wall time to the merged file each way.
[[nodiscard]] ParallelMetrics parallelMetrics(const SlotModel& model, const SectionJob& skeleton, const std::vector<SectionJob>& sections, const SectionJob& integration, int width);

}  // namespace sub0squad
