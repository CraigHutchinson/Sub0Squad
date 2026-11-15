#pragma once
/// @file wizard.hpp
/// `sub0squad wizard`: guided first use in one console (docs/BACKLOG.md "Getting started"). A checklist of what a first project needs (binary, memory,
/// ports, a model, daemon, portal, a project), each item's state taken from the `doctor` checks, the next step offered with its default, and every
/// action an EXISTING command of this program run as a child (`up`, `do`, `request answer`, `project wait`, `down`).
///
/// Bounds, which are the point:
///   * nothing is started, stopped or submitted without a yes (interactive), `--yes` (every prompt takes its default) or a line of `--script FILE`
///     (recorded keystrokes, one answer per line); when a script runs out of answers the wizard STOPS, it does not fall back to defaults;
///   * it never downloads, installs, or changes a setting; it does not start a model server: it uses one that is already running, or the mock agent;
///   * the daemon it starts runs with the frontier tier OFF;
///   * an engine address that is not this machine is refused unless the owner confirms it, because prompts and code would leave the machine;
///   * the model may only EXPLAIN (the notes of the checklist), on request, and what it says is shown as text: nothing it says is run.
///
/// It is not a registered process: it runs before a daemon exists, and its steps are prompts in a console, not journaled jobs (docs/PROCESS_DESIGN.md
/// section 16 says what that costs). The project it starts IS one: it is submitted under the `clarified` process, and the wizard is the console in which
/// that process's planning question is shown and answered.

#include <string>
#include <vector>

namespace sub0squad {

/// `wizard [--yes] [--script FILE] [--engine NAME=URL] [--portal-port N] [--no-portal]`. `exe` is this program's path (as for `up`). Exit 0 when the
/// flow ended as the owner chose (also when they declined a step), 1 when something must be fixed first or a step failed, 64 on bad usage.
[[nodiscard]] int runWizard(const std::vector<std::string>& args, const std::string& exe, const std::string& version);

/// Whether an engine endpoint is on this machine (`127.0.0.0/8`, `localhost`, `[::1]`): the wizard's test for "nothing leaves the machine".
[[nodiscard]] bool isLoopbackEndpoint(const std::string& url);

}  // namespace sub0squad
