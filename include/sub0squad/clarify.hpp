#pragma once
/// @file clarify.hpp
/// The planning iteration (docs/BACKLOG.md "Getting started", docs/PROCESS_DESIGN.md section 15): before any work starts, the few questions whose
/// answers CHANGE THE OUTCOME are put to the owner through the inbox, once, with the current default as the first option, so "just go" is one answer.
///
/// Pure rules over data. A request is looked at topic by topic (platform, executor, scope, engine, the bar for done, frontier), and each topic is settled
/// by the first source that has it:
///
///   request      the owner's words or flags already say it (a platform named in the text, `--executor`): never asked
///   harness      the harness or the agent decides it (the mock agent writes reference files: an executor means nothing to it): never asked
///   remembered   an earlier answer in the project's record (`<state>/project-answers.json`): asked once, then remembered
///   default      the owner asked for the defaults (`--defaults`): nothing is asked
///   not-asked    no answer could change this run (the topic's note says why and where it IS decided): a question that cannot change the outcome
///                is noise, so it is not put
///   (open)       otherwise the topic becomes a QUESTION; its first value is the default
///
/// All open questions go out as ONE blocking clarification request, and one answer settles them: `go` takes every default, `key=value ...` (or a bare
/// value that only one question has) sets some and defaults the rest. The result is the BRIEF: what the workflow will run with, and where each value
/// came from. The engine applies it (the task, and so the blueprint the planner loads; the executor) and the daemon remembers what was answered.
///
/// As a stage of a process this is a `job` stage whose tool is `plan-questions` and whose `accept` is `brief` (the `clarified` process).

#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// What is known about a request before it is planned.
struct ClarifyFacts
{
    std::string taskText;                          ///< the owner's words ("" when the task was named directly)
    std::string task;                              ///< the task name triage or the owner chose
    std::string agent = "mock";                    ///< mock | llm
    std::string executor;                          ///< the workflow's executor override, "" when the owner gave none
    bool acceptDefaults = false;                   ///< the owner asked for the defaults: ask nothing
    std::map<std::string, std::string> remembered; ///< topic -> value, from the project's record
    [[nodiscard]] std::string toJson() const;
    [[nodiscard]] static std::expected<ClarifyFacts, std::string> fromJson(std::string_view text);
};

/// One open question. `values[0]` is the current default; `labels[i]` says what `values[i]` means.
struct ClarifyQuestion
{
    std::string key, question;
    std::vector<std::string> values, labels;
};

/// One topic and how it was settled.
struct ClarifyTopic
{
    std::string key, value;
    std::string source;                            ///< request | harness | remembered | default | answer | not-asked
    std::string note;                              ///< why (for `not-asked`: where the topic is decided instead)
};

/// What a request leaves open and what it does not.
struct ClarifyPlan
{
    std::string task;                              ///< the task as submitted
    std::vector<ClarifyQuestion> questions;        ///< to be asked, in order
    std::vector<ClarifyTopic> settled;             ///< everything else, each with its source
    [[nodiscard]] std::string toJson() const;      ///< `clarify-1`
    [[nodiscard]] static std::expected<ClarifyPlan, std::string> fromJson(std::string_view text);
    /// The one request that carries every question: its title, its detail (each question with its options) and its options (`go` first).
    [[nodiscard]] std::string requestTitle() const;
    [[nodiscard]] std::string requestDetail() const;
    [[nodiscard]] std::vector<std::string> requestOptions() const;
};

/// What the workflow runs with.
struct Brief
{
    std::string task;                              ///< the task after the answers (the blueprint the planner loads)
    std::string executor;                          ///< "" when the process decides
    std::vector<ClarifyTopic> topics;              ///< every topic with its value and source
    [[nodiscard]] std::string toJson() const;      ///< `brief-1`
    /// The topics whose value the owner chose or accepted here (source `answer` or `default` of an ASKED question): what the project's record keeps.
    [[nodiscard]] std::map<std::string, std::string> toRemember() const;
};

/// Look at a request: which topics it settles and which become questions.
[[nodiscard]] ClarifyPlan clarify(const ClarifyFacts& facts);
/// Settle `plan` with the owner's `answer` ("" when there was nothing to ask). An answer that names an unknown topic or value is an error that lists
/// what can be said.
[[nodiscard]] std::expected<Brief, std::string> resolveBrief(const ClarifyPlan& plan, const std::string& answer);

/// The project's record of answers (`project-answers-1`): topic -> value. A missing or unreadable file is an empty record.
[[nodiscard]] std::map<std::string, std::string> parseProjectAnswers(std::string_view json);
[[nodiscard]] std::string projectAnswersJson(const std::map<std::string, std::string>& answers);

/// `plan-questions --workflow-dir D`: read `D/clarify-input.json` (the facts, written by the daemon), write `D/questions.json` (the clarify plan) and
/// print one line per topic. Exit 0; 1 when the input cannot be read.
[[nodiscard]] int runPlanQuestions(const std::vector<std::string>& args);

}  // namespace sub0squad
