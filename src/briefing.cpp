#include "sub0squad/briefing.hpp"

namespace sub0squad {

namespace {
std::string clip(const std::string& s, std::size_t n) { return s.size() <= n ? s : s.substr(0, n) + "\n[... cut]"; }
}  // namespace

std::string buildBriefing(const BriefingInput& in)
{
    std::string o = "# Briefing for " + in.request + ": " + in.title + "\n\n";
    o += "You are asked to decide about node `" + in.item + "` (" + in.persona + ") of workflow `" + in.workflow + "`. Everything needed is below; nothing else is assumed.\n\n";
    o += "## The request from the owner\n\n" + in.task + "\n\n";
    o += "## The node\n\n**" + in.itemTitle + "**: " + in.goal + "\n\nFiles it must deliver:";
    for (const auto& f : in.files) o += " `" + f + "`";
    o += "\n\nContract:\n\n" + clip(in.contract, 2500) + "\n";
    if (!in.acceptance.empty()) { o += "\nAcceptance:\n"; for (const auto& a : in.acceptance) o += "- " + a + "\n"; }
    o += "\n## How the harness behaves here\n\n" + in.harnessFacts + "\n";
    o += "\n## Evidence (recent gate outputs, oldest first)\n\n";
    if (in.gates.empty()) o += "(none: the attempts never reached the gate)\n";
    for (std::size_t i = 0; i < in.gates.size(); ++i) o += "Attempt output " + std::to_string(i + 1) + " of " + std::to_string(in.gates.size()) + ":\n```\n" + clip(in.gates[i], 900) + "\n```\n";
    if (!in.agentNote.empty()) o += "\nWhat the worker reported:\n```\n" + clip(in.agentNote, 400) + "\n```\n";
    o += "\n## What was already tried\n\n";
    if (in.history.empty()) o += "(nothing recorded)\n";
    for (const auto& h : in.history) o += "- " + h + "\n";
    o += "\n## The overseer's analysis\n\nClass: **" + in.triage + "**. " + in.analysis + "\n";
    o += "\n## The choices\n\n";
    for (const auto& c : in.options) o += "- " + c + "\n";
    o += "- Give guidance in your own words: it becomes the feedback of the node's next attempt.\n- Change the requirement or the node's contract: say what, and which nodes it concerns.\n";
    return o;
}

}  // namespace sub0squad
