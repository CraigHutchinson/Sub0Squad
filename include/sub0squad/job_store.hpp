#pragma once
/// @file job_store.hpp
/// Job records and their state machine, rebuilt from the journal. Pure data and rules: no I/O, no processes.

#include "sub0squad/job_spec.hpp"
#include "sub0squad/journal.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// `cancelling` is the state between a cancel request and the shim's verified outcome.
enum class JobState { queued, running, cancelling, succeeded, failed, cancelled, lost };

[[nodiscard]] std::string_view toString(JobState s);
[[nodiscard]] std::optional<JobState> jobStateFromString(std::string_view s);
[[nodiscard]] constexpr bool isTerminal(JobState s)
{
    return s == JobState::succeeded || s == JobState::failed || s == JobState::cancelled || s == JobState::lost;
}

/// One job (identified by id; `run` counts attempts, a resume starts a new run).
struct JobRecord
{
    std::string id;
    JobSpec spec;
    int run = 1;
    JobState state = JobState::queued;
    std::int64_t shimPid = 0;          ///< 0 until started
    std::uint64_t shimStartTicks = 0;  ///< /proc starttime of the shim (pid-reuse defence)
    double submittedAt = 0, startedAt = 0, finishedAt = 0;  ///< wall-clock UTC seconds
    std::string exitJson;              ///< raw `exit-1` object from the shim, when known
    std::string note;                  ///< why a state was reached (e.g. "shim vanished without an exit record")
};

/// Journal record bodies (`d`), built here so the writer and the reader cannot drift apart.
namespace journalbody {
[[nodiscard]] std::string submitted(const std::string& id, const JobSpec& spec);
[[nodiscard]] std::string started(const std::string& id, int run, std::int64_t shimPid, std::uint64_t shimStartTicks);
[[nodiscard]] std::string cancelRequested(const std::string& id, int run);
[[nodiscard]] std::string finished(const std::string& id, int run, JobState state, std::string_view exitJson, std::string_view note);
[[nodiscard]] std::string resumed(const std::string& id, int run);
}  // namespace journalbody

class JobStore
{
public:
    /// Apply one journal record (replay or live). Unknown record types are ignored (forward compatibility).
    void apply(const JournalRecord& rec);

    [[nodiscard]] const std::vector<JobRecord>& jobs() const { return jobs_; }
    [[nodiscard]] const JobRecord* find(std::string_view id) const;
    [[nodiscard]] JobRecord* find(std::string_view id);
    /// Next unused id, `j-000001` style.
    [[nodiscard]] std::string nextId() const;

private:
    std::vector<JobRecord> jobs_;
};

}  // namespace sub0squad
