#pragma once
/// @file overseer.hpp
/// The first responder to an escalation: before the owner is asked, the failure evidence is analysed and the request is classed as
/// `guidance` (a mechanical cause with actionable advice: answer it and let the node try again), `fatal` (an environment or tooling fault no
/// worker can fix: say so plainly) or `owner` (nothing mechanical found: this needs a decision). Rule based and deterministic; a model-backed
/// overseer would slot in behind the same verdict. See docs/WORK_GRAPH.md section 17.

#include <string>
#include <vector>

namespace sub0squad {

struct OverseerInput
{
    std::string persona;                ///< tester | developer
    std::vector<std::string> gates;     ///< gate outputs of the item's recent attempts, oldest first
    std::string agentNote;              ///< what the last attempt's agent or job reported when it did not reach the gate (exit text, error)
};

struct OverseerVerdict
{
    std::string triage;    ///< guidance | fatal | owner
    std::string analysis;  ///< the diagnosis in a sentence or two, for the owner and the log
    std::string guidance;  ///< triage == guidance: what to tell the node (becomes the feedback of its next attempt)
};

[[nodiscard]] OverseerVerdict analyseEscalation(const OverseerInput& in);

}  // namespace sub0squad
