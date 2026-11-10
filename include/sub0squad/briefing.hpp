#pragma once
/// @file briefing.hpp
/// The harness-expert's output: a compact briefing that collates what a decider (the owner, an architect or project-manager model, or a frontier
/// model reached by a file handoff) needs to decide quickly: the situation, the node's card, how the harness behaves here, the evidence, what
/// was already tried, the overseer's analysis and the choices. Markdown, written next to the node's files and shown in the portal.

#include <string>
#include <vector>

namespace sub0squad {

struct BriefingInput
{
    std::string workflow, request, kind, title;     ///< ids and what is being asked
    std::string task;                                ///< the owner's words
    std::string item, persona, itemTitle, goal;
    std::vector<std::string> files;
    std::string contract;
    std::vector<std::string> acceptance;
    std::string harnessFacts;                        ///< how the harness behaves (scaffold, stubs, gates, thresholds, formats)
    std::vector<std::string> gates;                  ///< recent gate outputs, oldest first
    std::string agentNote;
    std::string lastConversation;                    ///< the tail of the node's latest conversation trace (what the worker was last sent and answered)
    std::vector<std::string> history;                ///< what the ladder did, one line per decision
    std::string triage, analysis;                    ///< the overseer's verdict
    std::vector<std::string> options;
};

[[nodiscard]] std::string buildBriefing(const BriefingInput& in);

}  // namespace sub0squad
