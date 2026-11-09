#pragma once
/// @file status.hpp
/// What a job looks like to a client, derived from the journal's record plus what the shim recorded on disk. The same code
/// serves the live daemon and the CLI when no daemon runs, and it never reports `running` for a run whose shim is gone.

#include "sub0squad/job_store.hpp"
#include "sub0squad/paths.hpp"
#include "sub0squad/supervisor.hpp"

#include <string>

namespace sub0squad {

/// The effective state of a job given the disk, without writing anything.
struct Effective
{
    JobState state = JobState::queued;
    std::string note;
    std::string exitJson;
    bool fromExitRecord = false;  ///< the state comes from an exit.json the journal has not recorded yet
    bool shimAlive = false;
};

/// Map a shim's outcome to a job state and a note. Teardown survivors always make a run `failed`, never a silent success.
[[nodiscard]] JobState stateFromExit(const ExitRecord& exit, std::string& note, bool cancelRequested);

/// Reconcile one job against the disk: running/cancelling jobs are checked against exit.json and the shim's identity.
[[nodiscard]] Effective reconcile(const RuntimePaths& paths, const JobRecord& job);

/// JSON object for one job. `source` is "live" (daemon answering) or "journal" (daemon down).
[[nodiscard]] std::string jobJson(const RuntimePaths& paths, const JobRecord& job, const Effective& eff);

/// `{"ok":true,"asOf":...,"source":...,"jobs":[...]}` for all jobs, or just `onlyId`.
[[nodiscard]] std::string statusJson(const RuntimePaths& paths, const JobStore& store, const std::string& onlyId, const char* source);

}  // namespace sub0squad
