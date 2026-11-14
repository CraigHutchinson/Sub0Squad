#pragma once
/// @file attempt_failure.hpp
/// The record of why one attempt of a work item failed, kept in the attempt's own directory (catalogue row 72). Plain file helpers with no dependency on
/// harnesses or agents, so the status (domain layer), the gates, the agents and the daemon all use the same record.

#include <filesystem>
#include <string>

namespace sub0squad {

/// WHY an attempt failed (catalogue row 72: a tester failed five attempts and nothing in its directory said why). `attempt-N/failure.json` is written by
/// whoever knows: the agent on a failing exit (`role` "agent"; `kind` engine | format | oversize | skeleton | budget | sections-missing | compile |
/// compile-outside-sections | tests), the gate job (`role` "gate", `kind` gate) and, when a job ended without either, the daemon (`kind` exit, with the
/// exit record's note). `reason` is the evidence in a few lines. The status shows the latest attempt's record as the item's `lastFailure`.
void recordAttemptFailure(const std::filesystem::path& attemptDir, const std::string& role, const std::string& kind, const std::string& reason);
/// Remove the record (the attempt went on to pass).
void clearAttemptFailure(const std::filesystem::path& attemptDir);
/// The record as JSON text, "" when the attempt has none.
[[nodiscard]] std::string attemptFailureJson(const std::filesystem::path& attemptDir);

}  // namespace sub0squad
