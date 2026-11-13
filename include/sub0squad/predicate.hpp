#pragma once
/// @file predicate.hpp
/// The predicate language of a process (docs/PROCESS_DESIGN.md section 3): when a stage is entered and when it is done are expressions over a SNAPSHOT of
/// journal facts. The language is closed and total: no loops, no functions of the user's, no arithmetic, no clock and no file system, so a process file cannot
/// make the engine loop, cannot be steered by a worker writing a file, and evaluates the same on a replay. Everything is checked when the process is LOADED
/// (an unknown name, a stage that is not earlier in the list, a comparison of a truth value with a number), so evaluating a parsed predicate never fails.
///
///   expr  := and ("||" and)*          and := unary ("&&" unary)*          unary := "!" unary | cmp
///   cmp   := atom (("==" | "!=" | "<" | "<=" | ">" | ">=") atom)?         atom  := INT | "true" | "false" | "(" expr ")" | path
///
/// The whole vocabulary:
///   always                                   true
///   plan.present                             the workflow has a plan
///   stage.NAME.done | .active | .failed      the derived state of a stage EARLIER in the process (never the stage itself or a later one: no cycles)
///   items.all(STATE) | items.any(STATE)      over the plan's items; STATE is pending | running | accepted | failed; `all` over no items is FALSE
///   items.count(STATE) | items.count()       a number, to compare with a literal
///   job(STEP).succeeded | .failed | .active  the latest item-less job of a stage's step (how a job stage is observed)
///   job(STEP).tries                          how many such jobs were submitted
///   requests.openBlocking                    an open blocking request exists

#include <expected>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// The facts a predicate reads. The engine builds one per pass from the workflow record and its jobs; a test builds one by hand.
struct Snapshot
{
    struct StageFacts { bool done = false, active = false, failed = false; };
    struct JobFacts { int tries = 0; bool succeeded = false, failed = false, active = false; };   ///< of the LATEST job of the step
    bool planPresent = false;
    bool openBlocking = false;
    int pending = 0, running = 0, accepted = 0, failed = 0;   ///< the plan's items by state
    std::map<std::string, StageFacts, std::less<>> stages;    ///< only stages already derived this pass (the earlier ones)
    std::map<std::string, JobFacts, std::less<>> jobs;        ///< by step; a step with no job has no entry (all false, 0 tries)
};

/// The names a predicate may use, known when the process is loaded.
struct Vocabulary
{
    std::vector<std::string> stages;   ///< the stages EARLIER than the one the predicate belongs to
    std::vector<std::string> steps;    ///< the step of every job stage of the process
};

/// A parsed, type-checked predicate. Cheap to copy.
class Predicate
{
public:
    /// Parse and check `text` against `vocabulary`. The error names the column (1-based) and what is wrong.
    [[nodiscard]] static std::expected<Predicate, std::string> parse(std::string_view text, const Vocabulary& vocabulary);
    /// The value over `snapshot`. Total: a name the snapshot lacks reads as false or 0.
    [[nodiscard]] bool eval(const Snapshot& snapshot) const;
    /// Why the predicate is false: the text of the first atom (in reading order) whose value makes it false, e.g. `items.all(accepted)` or
    /// `!requests.openBlocking`. "" when the predicate is true.
    [[nodiscard]] std::string firstFalse(const Snapshot& snapshot) const;
    [[nodiscard]] const std::string& text() const { return text_; }

    struct Node;

private:
    std::string text_;
    std::shared_ptr<const Node> root_;
};

}  // namespace sub0squad
