# Status

One page for the owner and for whoever works on this next. Updated 2026-11-16. Sub0Squad is a working title.

**Where it is.** The machinery is built and proven without a model: the daemon and its durable jobs, workflows as processes of stages, three project
harnesses (C++ with CMake, Python, a static web page), three executors (session, canvas, forked sections), the escalation ladder with self-mending, the
planner role, the portal and the wizard. **No real model has completed a project yet.** There is no release and no production code: the work now is the
proving sandbox on a local llama.cpp server, optimising and A/B-verifying each part ([EXPERIMENTS.md](EXPERIMENTS.md)).

**Next.** Experiment E1, the specialist stance and the contest channel: built 2026-11-16 (`--stance specialist`, the CONCERN block, `replay --accepted`),
waiting for an engine run. Then the window plan (E2) and the planner arms (E3 to E5). The queue, with hypotheses and commands, is
[EXPERIMENTS.md](EXPERIMENTS.md) section 8.
Resuming on a new machine: [RESUME.md](RESUME.md) has the priority order and the start commands.

## 1. Proven without a model (and the test that proves it)

| Capability | Proof (ctest name) |
|---|---|
| Durable jobs: journal, job store, shim, supervisor, resume, host guards, shedding under memory pressure | test_journal, test_job_store, test_job_spec, test_shim, test_idle, test_host_guard, test_e2e |
| The workflow engine: a process is a list of stages with entry and exit predicates; the decision ladder; goldens of every scenario | test_workflow, test_workflow_stages, test_workflow_mends, test_process, test_predicate, test_policy |
| Three project harnesses: scaffold, gate, integrator, blueprints, with real builds and test runs | test_platforms, test_project, test_plan |
| The mock path end to end, exactly as the README says | test_readme |
| Executors `session`, `canvas`, `fork` driven by recorded replies, and through a real daemon | test_agent, test_canvas_agent, test_fork, test_executors |
| Turn control: early abort, continuation, overlap trimming, bounded reads | test_stream, test_agent |
| Self-healing: the mends table, `add-section`, `silence-unused`, a process's own table | test_mends, test_workflow_mends, test_heal |
| Near pass kept, best state never lost, class members as sections, thrash control, first-cause attribution | test_nearpass, test_thrash, test_mends |
| The planner from a bare request: validation, plan checks, repair as edits, JSON mends | test_planner |
| The planning stage that asks once and remembers (`--process clarified`) | test_clarify |
| Stances and the CONCERN channel: the exact baseline wording, the parser, recording, acknowledgement | test_stance, test_agent |
| `sub0squad wizard` from recorded keystrokes | test_wizard |
| The portal page; the archive; the model-free block proofs; the scenario fixtures | test_portal, test_portal_page (needs node), test_archive, test_blocks, test_scenarios |
| Layering: includes go downward and no layer calls upward (one library per layer); only the platform library uses the operating system (with a shrinking list of files not yet moved) | layering, link_platform ... link_presentation |
| The platform library's process launcher: exit codes, signals, capture, timeouts, group kill, no leaked descriptors | test_platform_process |
| The platform library's process identity and host facts: `/proc` parsers on fixed text, this process, a child's tree, memory, processor time, disk | test_platform_proc, test_platform_host |
| The platform library's files: binary whole-file reads and writes, atomic replace, owner-only appends with sync and truncate, descriptor I/O | test_platform_file |
| The platform library's sockets and instance lock: loopback round trip, read and accept deadlines, end of stream, two listeners, a held port, the local socket and its peer, a lock a second taker cannot get | test_platform_net |
| The platform library's waits and the shim's process pieces: a timeout, a wake from another thread, socket readiness and hang-up, a child's exit with its status, a stop request to this process and to a child, piped stdout and stderr, the event pipe, a replaced environment | test_platform_wait |

## 2. On the real model

Five whole-project runs (v8 to v12) and three baseline-campaign seeds, all from a hand-written plan, none finished; eight planner probes from a bare request.
Results and causes: [EXPERIMENTS.md](EXPERIMENTS.md) section 6. Built and proven without a model but **not yet seen on the model**: the mends for
undefined contract symbols, undefined members, unused entities; contract and member sections; the near-pass keep; `think` for a stuck section; the diagnostics
table; thrash detection; the first-cause gate rule; the planning stage with an llm agent; the stances.

## 3. Build and test

```sh
. scripts/dev-env.sh                                   # every shell that configures or builds (sets the CPM overrides on the dev host)
cmake -S . -B out/build/dev -G Ninja && nice cmake --build out/build/dev -j2
ulimit -u $(( $(ps -u $USER -L --no-headers | wc -l) + 400 )); ctest --test-dir out/build/dev -j1
```

Tests are in two tiers by ctest label ([DEV_SETUP.md](DEV_SETUP.md) "Test tiers", `cmake/TestTiers.cmake`): `ctest -L smoke` is a few fast
tests from every area (structure, platform, kernel, workflow, harness, agents, portal, e2e) for every change and the smoke CI job; the full
suite runs at milestones, releases, a `ci:full` pull request and the weekly CI run.
The full suite is about 50 tests and 6 to 12 minutes sequentially. clang 21 is the reference compiler; GCC 15 builds too (two flow-based GCC
diagnostics are warnings, not errors: `CMakeLists.txt`). The program runs on Linux and natively on Windows (MinGW-w64): the platform layer has Windows backends, and the daemon, shims, jobs, workflows and the
end-to-end tests run natively ([PLATFORM.md](PLATFORM.md) section 6). On a Windows host use WSL with the checkout
cloned inside WSL, or `.gitattributes` keeping the shell scripts LF. `cmake -DSRC_DIR=$PWD -P cmake/CheckLayers.cmake` is the layer check by hand.

## 4. Running on the model

The engine is started by the owner as a contained user service; see [EXPERIMENTS.md](EXPERIMENTS.md) section 2 for the profile and section 3 for the tools.
A whole project:

```sh
sub0squad doctor --engine $ENGINE
sub0squad up --frontier off
sub0squad do "write a scientific calculator in C++" --agent llm --engine $ENGINE --executor fork --max-tokens 3500 --repair-turns 2 [--stance specialist]
sub0squad archive --label NAME                         # after the run, daemon stopped
```

## 5. Open questions for the owner

1. **The project name** (still the working title).
2. **The frontier tier** stays `off` or `stub`; nothing has ever been sent off the machine. Enabling a real one is the owner's decision.
3. **The wizard and the engine:** may the build fetch and build llama.cpp, and may the wizard start a contained engine service and fetch a model?
4. **Planning questions:** should "the bar for done" and "which engine" become questions? They need the process and the engines to be replaceable after
   submission ([PROCESS_DESIGN.md](PROCESS_DESIGN.md) section 15).
5. **Remembered answers:** is "one record of answers per state directory" the right meaning of "the project"?
6. **Diagnostics table:** grow it only from real runs, or seed it with common compiler diagnostics a 35B trips on?
7. **`leaf.thrashControl`, `leaf.memberSections`, `leaf.allow.thinkFirst` are ON by default** although the rule is "new capability off by default": fixes
   decided by the lead, each with a switch to reproduce the defect. Confirm or reverse.
8. **Plan format:** may a second, smaller local model serve the format step, and may the engine call pass a grammar or JSON schema
   ([CLEAN_SLATE.md](CLEAN_SLATE.md) section 8)?
9. The questions of [METRICS.md](METRICS.md) section 10 (the oracle, whether a healed run counts as success, wall time).

## 6. Standing rules

* **No legacy.** There is no production version: remove dead code, compatibility shims and retired documents (git keeps them: [ARCHIVE.md](ARCHIVE.md)).
* **Style:** C++23, camelCase, Doxygen on every interface, `#pragma once`, `[[nodiscard]]`, `-Wall -Wextra -Werror` clean; `std::format`/`std::print`, not `printf` ([DECISIONS.md](DECISIONS.md) 25). New capability off by default.
* **Self-mending:** every manual fix is a missing rule: catalogue it in [INTERVENTIONS.md](INTERVENTIONS.md) with a detector, a mend and a regression fixture.
* **Report faithfully:** a failing test is reported with its output; what was not verified is said.
* **Stop and ask:** the final name; enabling any frontier tier; changes to Sub0Pipeline or Sub0Pub; downloads; installing software; credentials.
* **Nothing leaves the machine:** local models only (frontier off); no uploading of code or data. This repository is public, so it never names or
  links a private repository (the pre-push gate checks, [DEV_SETUP.md](DEV_SETUP.md)).
* **Commit as you go** to `main` (fetch, rebase, build, run the tests first), as the owner's GitHub identity already set in the clone, with a
  `Co-Authored-By` trailer for the agent. **Confirm with the owner before pushing**; never force-push or skip hooks. In a checkout another session
  shares, commit only your own files and never stash, reset or switch branches (use a worktree). When the owner says "all work", include other
  agents' uncommitted files.
* **Clean up as you go** (standing permission, 2026-11-17): once work is merged, superseded or obsolete, delete its branches (local and remote),
  worktrees and stale folders (old run data under `out/`) without asking, after checking that it really is merged or superseded. Retire one-off
  handoff notes and legacy documents once integrated. "Never throw work away" protects unique, unmerged work only.
* **CI:** never dispatch the full tier after a routine push. Verify locally (the pre-push gate runs the quick tier; `ctest` for full), then check
  only the smoke run. Full runs are for releases, version bumps and milestones; the one exception is a change only another platform can prove
  (a Windows Server runner's behaviour): batch such changes and dispatch once ([DEV_SETUP.md](DEV_SETUP.md)).
* **Follow-up work** that belongs in its own session is written as a brief in [sessions/](sessions/README.md) (write, pick up, close with an
  `## Outcome`, move to *Done*), listed there and pointed at from [BACKLOG.md](BACKLOG.md). Not left in a chat or a session card, which other
  devices cannot open.
* **The shared engine:** `llama-server` on port 8092 is shared by every session; never stop, restart or retune it, and never start a second server on
  its port. Coordinate tier 0 runs with the session running them.
* **The canvas library** is [Sub0Canvas](https://github.com/CraigHutchinson/Sub0Canvas) (that spelling in prose and URLs; its code identifiers,
  `sub0canvas::`, `CPM_sub0canvas_SOURCE`, stay lower-case). Changes to it are made there first, tagged, and arrive here as a pull request bumping
  the pin (its `docs/INTEGRATION.md`).
* **This list is complete.** Everything needed to work here is in this repository; nothing depends on notes kept outside it (an assistant's local
  memory, a chat, a session card). When the owner gives a new standing instruction, add it here in the same change.

## 7. Host safety (the dev host ran out of memory twice)

* Builds at `-j2`, one at a time; never while a real run needs the machine unless told it is idle. The 35B needs about 23 GB of the host's 30.
* Every test or command that starts processes runs under a process cap and a timeout (the `ulimit` line above). Nothing re-executes `/proc/self/exe`
  (catalogue row 32: a fork bomb); a program path is passed in and verified.
* An agent never starts, stops or restarts an engine, a daemon, a portal or a service that is not its own test's; no real-model runs by agents.
* Do not edit a source file while the compiler is reading it (the compile dies with SIGBUS).
* In an agent's shell tool a dollar-brace reference in a heredoc may be expanded to nothing: write such files with the file tool.
