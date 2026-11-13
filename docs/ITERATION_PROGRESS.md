# Iteration progress: running log of the Fable iteration

Branch `worktree-agent-a507f5df083b20ce3`, from tag `parked-v1`. Newest state first. Every "passes" below was observed in `out/build/fable`, run sequentially under a
process cap; nothing here has run on a real model.

## Second pass (2026-11-13), newest first

* **Rows 44, 45, 46 from run-v8 (the first real use of the fork executor) are built and tested without a model.** 44: only a developer's item is divided
  (`RulePolicy`, `Plan::amended`). 46: the primer declares, the harness completes the skeleton (`expandSkeleton`), checked as a partial file; bounded at the
  server, aborted past 120 lines, an attempt wall budget. 45: the choice is SECTIONS on compiled platforms (`Harness::divideBySections`, `Action::sections`);
  the CONTRACT stage is recorded as the answer for projects that need new modules and is not built. Design and reasons: `docs/CANVAS.md`, "After the first
  real run". Tests: `test_fork` (now 8 scenarios, one on the C++ calculator blueprint through the real C++ gate), `test_blocks`, `test_workflow`.
* **Every plan names a registered harness**: `Plan::fromJson` refuses one that does not; `test_workflow`'s scenarios run on `web-static`; the division
  scenario is rewritten for the coordinator semantics, the only one left.
* Next, in order: process as data (golden action logs first), scenarios and `replay` plumbing, the open catalogue rows and the daemon end-to-end test for the
  canvas and fork executors, components cleanup, the stale documents.

## Where it stood after the first pass (2026-11-12)

* `ctest`: **22 of 22 pass** (the last full sequential run, 133 s); `cmake -P cmake/CheckLayers.cmake` passes with no tolerated violations.
* `sub0squad block run`: block 1 proven (24 of 24 cases); blocks 5 and 6 partly proven (their model-free part); the rest by ctest, needing a model, or not built.

## Done (each with tests; see the commits)

| what | where | proven by |
|---|---|---|
| Build repaired after the `parked-v1` tag emptied `Version.hpp` (catalogue row 38) | `cmake/git-semver.sh`, `cmake/dependencies.cmake` | the configure's fatal check |
| Three tests that could not fail now can; a check refuses that pattern (row 39); `test_e2e` lease check reads a snapshot (row 42) | `cmake/CheckTests.cmake`, `tests/` | ctest `test_reporting`, `test_e2e` |
| A recorded-reply fixture harness for agents; `llm_agent` split | `tests/fake_engine.hpp`, `tests/agent_bench.hpp`, `src/agent_card.cpp` | `test_agent` |
| Canvas slice 0: replies are transactions, the precheck gates each commit, the conversation carries no files, a worse revision is reverted | `src/canvas.cpp`, `src/canvas_agent.cpp`, `Harness::precheck` | `test_canvas_agent` (11 behaviours) |
| Forked sections on the canvas, throughput by default, ownership, integration rounds | `src/fork.cpp`, `src/throughput.cpp` | `test_fork` (5 scenarios) |
| Turn control for every executor (rows 34 to 36): early abort on the stream, cut-off edit continuation, overlap trimming, bounded reads, a worked LINES example | `src/turn_control.cpp`, `src/engine.cpp` (a stream can be cancelled), `src/llm_agent.cpp` | `test_agent`, `test_canvas_agent`, `test_blocks` |
| Block proving grounds without a model: reply cases (1), outline (4), the canvas against a model (5), the parallel-sections harness on a slot-model engine (6); the `block` command and its archive | `src/blocks.cpp`, `tests/data/replies/` | `test_blocks` |
| Executors as a named choice: `--executor session, canvas or fork` on `do`, `project submit`, `scenario run` | `WorkflowSpec.executor` | compile and the suites above; **no workflow-level test with a model-driven executor** |
| The header-only sandbox path removed; a plan that runs must name a registered harness | `src/sandbox.cpp`, `Plan::notRunnable` | `test_project`, `test_workflow` |
| First use: `doctor`, `up`, `down`, blueprints start by task name, the README sample | `src/first_use.cpp`, `README.md` | `test_readme` runs the README's commands |

## Not done (in the brief's order), and why

* **Item 1, process as data** (and with it "every workflow is a registered process", and the README's second sample that adds a workflow): not started. It needs the
  golden action logs of `advance()` first; that is the next thing to build. The three executors and the block command are the first entries its registry should hold.
* (Done in the second pass: the engine's harness-less branch is gone.)
* **Item 3, scenarios:** only `--executor` was added. The six "next scenarios", `scenario compare` and archiving are not built.
* **Item 4, open catalogue rows:** 14, 19, 20, 22 (the shed check), 25/26 and 31 are untouched.
* **Item 5:** `test_workflow.cpp`, `client.cpp` and `portal.cpp` are not split; one library per layer is not done.
* **Items 6 to 10** (division validator, debugger worker, cross-file outline and SEARCH request, reporter and register readers, portal items): not started.
* **Blocks 7 to 10** of `BLOCK_PROVING_GROUNDS.md` (seam scenario, division, golden decision logs, process as data): not built.
* `replay` does not pass `--parallel` or `--sequential-reason` to `llm-fork` (needed for measurement 3 in `BLOCK_PROVING_GROUNDS.md`).
* A workflow through the daemon with the `fork` or `canvas` executor and a fake engine is not tested end to end.
* `docs/DESIGN.md`, `M1_PLAN.md`, `PROCESS_DESIGN.md`, `SCENARIOS.md` and `WORK_GRAPH.md` section 23 were not revised; section 23 still describes the removed
  JavaScript-only fork (the current design is `docs/CANVAS.md`, "As built").

## Open questions for the owner

1. Which executor is the default once measured (`session` today)? And may the session loop be deleted when the canvas reaches the block 3 bar?
2. A plain "write a calculator" with no platform named now goes to the C++ blueprint (it went to the removed header-only one). Is C++ the right default?
3. `docs/BLOCK_PROVING_GROUNDS.md` exists on `main` and, edited, on this branch (it was copied here to be updated, as asked): the merge is an add/add of that file.
   `docs/INTERVENTIONS.md` rows here start at 38 because row 37 is on `main`.
4. The tag `parked-v1` breaks the build of the tagged commit wherever Version.cmake regenerates (including `out/build/dev`); the fix is on this branch only.
5. The project name; the frontier tier stays off or stub (nothing was changed there).
