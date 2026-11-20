// The stance of a worker's system block and the CONCERN channel (include/sub0squad/stance.hpp, docs/PROMPTS.md).
#include "sub0squad/stance.hpp"
#include "sub0platform/file.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>

namespace sub0squad {

bool isKnownStance(std::string_view stance) { return stance == "baseline" || stance == "specialist"; }

const std::vector<std::string>& concernKinds()
{
    static const std::vector<std::string> kinds{"wrong", "contradictory", "underspecified", "missing", "too-big", "better-idea"};
    return kinds;
}

std::string concernSyntax(std::string_view stance)
{
    if (stance != "specialist") return {};
    return "A CONCERN, when you have one (before your work, or alone when you cannot go on):\n"
           "=== CONCERN: <kind> ===\n<what, where (the file and line, or the example), why, and what you propose>\n=== END CONCERN ===\n"
           "Kinds: wrong (false or infeasible), contradictory (two statements disagree, such as a test and the contract), underspecified (something you need is not "
           "stated), missing (an interface or part nobody owns), too-big (more than one piece: name the part to split off and delegate), better-idea (a better design). "
           "A concern is recorded for the owner of what you question; nobody answers it during this attempt, so after raising it carry on with everything you can do correctly.\n";
}

namespace {

std::string personaSentence(std::string_view stance, std::string_view persona)
{
    if (stance != "specialist") return persona == "tester" ? "Write tests for the contract; do not write the implementation.\n" : "Implement the contract so that the provided tests pass.\n";
    if (persona == "tester")
        return "Your piece is the tests: state the contract exactly, including its errors and edge cases, and do not write the implementation. A test is right only if a correct "
               "implementation passes it, so work every expected value out from the contract and its worked examples.\n";
    return "Your piece is the implementation: make the contract true. The provided tests are how your work is checked; if a test contradicts the contract or its worked "
           "examples, raise a concern and keep the code that is right rather than satisfy the test.\n";
}

}  // namespace

std::string stanceText(std::string_view stance, std::string_view persona, WorkerShape shape)
{
    const std::string who{persona};
    if (stance != "specialist") {   // exactly the wording of every run before stances existed
        if (shape == WorkerShape::section) return "You are the " + who + " on a small software squad.\n";
        return "You are the " + who + " on a small software squad: you own one small, focused piece of a whole solution. Be the expert and do it as well as it can be done, "
               "keep to your contract and the exact interfaces you are given so the other pieces fit, and " + std::string{shape == WorkerShape::canvas ? "change" : "write"} +
               " only the files you are asked to write. " + personaSentence(stance, persona);
    }
    if (shape == WorkerShape::section)
        return "YOUR ASSIGNMENT. You are the " + who + " for ONE function of this file: a dedicated, specialised assignment that is yours alone. Other specialists write the "
               "other functions at the same time, and the harness checks the merged file. Within your function you are the expert: you decide how it is done.\n"
               "YOUR STANDING. The card, the contract and the tests are claims made by other specialists, not orders. If one is wrong, contradictory, or lacks what "
               "you need, say so in a CONCERN block before your edit block, and still write your function as the contract means it. Never bend correct code to fit "
               "an expectation you believe is wrong.\n" + concernSyntax(stance);
    return "YOUR ASSIGNMENT. You are the " + who + " for one piece of this project: a dedicated, specialised assignment that is yours alone. Other specialists own the "
           "other pieces, a planner owns the plan, and the harness checks every result. Within your piece you are the expert: you decide how it is done, and you are "
           "trusted to do it as well as it can be done.\n"
           "YOUR STANDING.\n"
           "- The card, the contract, the worked examples and the provided files are claims made by other specialists, not orders. Check them against each other "
           "before you build on them.\n"
           "- If something is wrong, contradictory, missing or too big for one piece, say so in a CONCERN block. Never bend correct work to fit an expectation you "
           "believe is wrong, and never work around a defect in silence: a concern with evidence is a contribution, not a failure.\n"
           "- If part of the work belongs to another piece, or should be split off and delegated, propose it in a CONCERN (too-big or missing) and name the part. "
           "Do your own part regardless.\n"
           "- Keep to the exact interfaces you are given, so the other pieces fit, and " + std::string{shape == WorkerShape::canvas ? "change" : "write"} +
           " only the files you are asked to write.\n" + concernSyntax(stance) + personaSentence(stance, persona);
}

std::vector<Concern> takeConcerns(std::string& reply)
{
    static constexpr std::string_view head = "=== CONCERN:", tail = "=== END CONCERN ===";
    std::vector<Concern> found;
    std::string kept;
    std::size_t pos = 0;
    while (pos < reply.size()) {
        std::size_t at = reply.find(head, pos);
        while (at != std::string::npos && at > 0 && reply[at - 1] != '\n') at = reply.find(head, at + 1);   // only at the start of a line
        if (at == std::string::npos) { kept += reply.substr(pos); break; }
        kept += reply.substr(pos, at - pos);
        const std::size_t lineEnd = std::min(reply.find('\n', at), reply.size());
        std::string kind = reply.substr(at + head.size(), lineEnd - at - head.size());
        if (const auto close = kind.find("==="); close != std::string::npos) kind.erase(close);
        kind.erase(0, kind.find_first_not_of(" \t"));
        kind.erase(kind.find_last_not_of(" \t\r") + 1);
        std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // The body runs to the END line, else to the next line that opens another block, else to the end of the reply.
        std::size_t bodyFrom = lineEnd < reply.size() ? lineEnd + 1 : reply.size();
        std::size_t bodyTo = reply.size(), next = reply.size();
        for (std::size_t line = bodyFrom; line < reply.size();) {
            const std::size_t e = std::min(reply.find('\n', line), reply.size());
            const std::string_view l{reply.data() + line, e - line};
            if (l.substr(0, tail.size()) == tail) { bodyTo = line; next = e < reply.size() ? e + 1 : e; break; }
            if (l.substr(0, 4) == "=== ") { bodyTo = line; next = line; break; }
            line = e + 1;
        }
        std::string text = reply.substr(bodyFrom, bodyTo - bodyFrom);
        text.erase(0, text.find_first_not_of(" \t\r\n"));
        text.erase(text.find_last_not_of(" \t\r\n") + 1);
        if (!kind.empty() || !text.empty()) found.push_back({kind, text});
        pos = next;
    }
    if (!found.empty()) reply = kept;
    return found;
}

bool recordConcerns(const std::filesystem::path& attemptDir, std::string_view who, int turn, const std::vector<Concern>& concerns)
{
    if (concerns.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(attemptDir, ec);
    std::string text;
    for (const auto& c : concerns) text += std::format("## CONCERN: {}  ({}, turn {})\n\n{}\n\n", c.kind.empty() ? "(no kind)" : c.kind, who, turn, c.text);
    return sub0platform::appendToFile(attemptDir / "concerns.md", text).has_value();
}

std::string concernAcknowledgement(const std::vector<Concern>& concerns)
{
    std::string kinds;
    for (const auto& c : concerns) kinds += (kinds.empty() ? "" : ", ") + (c.kind.empty() ? std::string{"unnamed"} : c.kind);
    return "Your concern" + std::string{concerns.size() == 1 ? "" : "s"} + " (" + kinds + ") " + (concerns.size() == 1 ? "is" : "are") +
           " recorded for the owner of what you questioned; nobody can answer during this attempt. Now do your piece as well as it can be done: reply with your work in "
           "the required form. Where you questioned an expectation, follow the contract and its worked examples as you understand them, and keep code that is right even "
           "if a test you questioned then fails.\n";
}

}  // namespace sub0squad
