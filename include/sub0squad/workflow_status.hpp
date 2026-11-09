#pragma once
/// @file workflow_status.hpp
/// Workflow view for clients: state, plan and the planner's reasons, per-item status, every decision with its inputs, and the ledger
/// summary (local versus frontier share). Works with the daemon down, from the journal and the ledger file.

#include "sub0squad/ledger.hpp"
#include "sub0squad/paths.hpp"
#include "sub0squad/workflow.hpp"

#include <string>

namespace sub0squad {

[[nodiscard]] std::string workflowStatusJson(const RuntimePaths& paths, const WorkflowStore& workflows, const JobStore& jobs,
                                             const std::string& onlyId, const char* source);

}  // namespace sub0squad
