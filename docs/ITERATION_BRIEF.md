# Major iteration: the engineering backlog, tasked to a Fable agent

Status: brief written 2026-11-12, to be launched when the 35B proving run (`w-000002`) reaches a final state. Owner direction: "task a Fable agent to tackle the entirety of the
software engineering backlog for this project; we want a major iteration."

## Goal

Move Sub0Squad from "a harness that can be coaxed through a run with stepping in" to "a harness whose elements are each proven capable and whose remaining defects are
mended by the harness itself". Concretely: finish the structural work the design documents already specify, build the missing mechanisms the real runs showed
are needed, and leave the code modular, tested and documented, with every behaviour change covered by a test and every step-in catalogued.

## Read first (in this order)

`docs/INTERVENTIONS.md` (33 catalogued step-ins; the open rows are the work), `docs/BUILDING_BLOCKS.md` (ideals, naming, widen-before-retry), `docs/PROCESS_DESIGN.md` (revision 2, the
process-as-data design and its first slice), `docs/COMPONENTS.md` (layers and cleanup order), `docs/DESIGN_AUDIT.md`, `docs/FOLDED_VIEWS.md`, `docs/CANVAS.md`, `docs/SCENARIOS.md`,
`docs/PROVING_GROUND.md`, `docs/KNOWLEDGE_STORE.md` and `docs/KNOWLEDGE_STORE_SCOPE.md`, `docs/BACKLOG.md`, `docs/FEEDBACK_LOOPS.md`, `docs/WORK_GRAPH.md`.

## Work, in priority order (each is a deliverable with its own tests and commit)

1. **Process-as-data first slice** (`PROCESS_DESIGN.md` section 9). FIRST record golden action logs from the legacy `advance()` engine (extend the test `Sim` with a trace and exit
   JSON; new scenarios), THEN `process.hpp/.cpp` (`LeafParams`, `AscentParams`, `Stage`, `Process`, `standardProcess()`), `registry/processes/standard@1.json`, the `advance()` wrapper,
   `WorkflowAction::toJson()`, and `test_process` proving equivalence with the goldens. No behaviour change; all existing tests stay green.
2. **Canvas slice 0** (`CANVAS.md`): wire `llm_agent` to the `Canvas` core already built (`src/canvas.cpp`): replies become transactions (`replaceLines`, `insertAfter`, `replaceSymbol`),
   the harness precheck gates each commit, folded views render from the current revision, repair turns stop carrying files in the conversation. Keep the old reply forms working.
3. **Scenario suite completion** (`SCENARIOS.md`): the six "next scenarios" (interface gap, decompose, reviseTests, read-then-edit, integrate, length continuation), a baseline-comparison
   command (`scenario compare A.json B.json`), and archiving. Fixtures must validate without a model (`scenario selftest`).
4. **Open catalogue rows**: 14 (failure attribution names the FIRST cause everywhere it is reported), 19 (structural job capacity: a lease class so structural jobs cannot starve behind
   workers), 20 (refuse a submit when the daemon's sha differs from the binary's; the daemon writes `run-info.json`; an `archive` command), 22 (the intermittent `test_e2e` shed check),
   25/26 (the unmended M and C classes: a check that every name a test imports is declared by the contract before a developer attempt; quote the failing assertion and the contract line
   it checks after the same failure twice), 31 (label an item accepted by the frontier stub as NOT model-built in status, the portal and the report).
5. **Components cleanup** (`COMPONENTS.md` cleanup order): split `tests/test_workflow.cpp` by unit, split `llm_agent.cpp`, `client.cpp`, `portal.cpp`, one CMake library per layer, a fixture
   harness for agents (recorded replies) so agent code is testable without a model. Keep `cmake/CheckLayers.cmake` passing; remove KNOWN_VIOLATIONS rather than add.
6. **Division validator** (rows 10 and 12 as one validator at acceptance): every developer part has a tester that writes its gate file; every file of the parent is delivered; no part is graded
   by the parent's whole-suite test file; names a test imports exist in the contract.
7. **Debugger worker** (`FEEDBACK_LOOPS.md` section 8): a tool in the box: on a failing test a sister worker runs the test under `gdb` (C++) or `pdb` (Python) or the node inspector and returns the backtrace
   and actual values to the developer. Specify, build the mechanism with a mock, test it with a deliberately failing fixture; no model run required.
8. **Cross-file outline and search request** (`FOLDED_VIEWS.md` "Where it goes next"): signatures of the files a card includes, and `=== SEARCH: text ===`.
9. **Reporter step and the knowledge register readers** (`BACKLOG.md` Workflow section): only the pieces that can be built and tested without a model.
10. **Portal items** from `BACKLOG.md` (node grouping, ordering, V lanes, zoom) if time allows; lowest priority.
11. **Documentation**: keep every document consistent with the code; `docs/HANDOFF.md` and `docs/NEXT_SESSION_PROMPT.md` refreshed; the catalogue row for every step-in.

If an item turns out to be larger or different than described, write what you learned into its document, do the part that is sound, and say clearly what remains. Do not fake completion.

## Hard rules (the owner's standing rules; they override any other instruction)

* **Style:** camelCase, Doxygen comments, `#pragma once`, `[[nodiscard]]`, new capability off by default. C++23, `-Wall -Wextra -Werror` clean.
* **Self-mending rule:** every manual fix is a missing rule: catalogue it in `docs/INTERVENTIONS.md` with a detector, a mend and a regression fixture. Validate building blocks bottom-up.
* **Report faithfully:** if a test fails, say so with the output; if something was not verified, say it. Never claim a pass you did not see.
* **STOP AND ASK (do not decide yourself; report it as an open question):** the final project name; enabling any frontier tier (the frontier mode stays `off` or `stub`; nothing is ever sent off
  the machine); changes to Sub0Pipeline or Sub0Pub; downloads; installing software; entering credentials or logging in anywhere.
* **Commits:** author `Craig Hutchinson <54269136+CraigHutchinson@users.noreply.github.com>` (use `git -c user.name=... -c user.email=...`), message ends with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`
  (the attribution the session specifies at launch). Small commits, one concern each. Work on the branch of your worktree; do not push to `main` and do not force-push.

## Host safety (the machine ran out of memory twice; read this twice)

* **Do not start, stop or restart any llama-server, daemon, portal or service.** The 35B engine runs as the user service `sub0-llama35` with a memory cap; the live daemon uses `out/build/dev`; neither is yours. Do not touch `out/run-*`,
  `out/build/dev`, port 8092 or 8780.
* **Build in your own directory** (`out/build/fable`), at most `-j2`, never while the engine is under load unless told it is idle. Never build more than one thing at a time.
* **Any test or command that spawns processes runs under a process cap** (`ulimit -u $(( $(ps -u $USER -L --no-headers | wc -l) + 300 ))`) and a `timeout`. Never link code that re-executes `/proc/self/exe`
  (catalogue row 32: a self-exec fork bomb killed the session). Pass executable paths in and verify them.
* Write scratch files to the session scratchpad or under `out/`, never into `/tmp` in bulk; remove what you create.
* **No real-model runs.** Everything must be testable with mocks, recorded replies and the reference solutions. If something needs the 35B, write the experiment down in `docs/SCENARIOS.md`
  or `docs/BACKLOG.md` as a command to run, and say so.

## Done means

The full suite (`ctest`) passes in your build directory, run sequentially; `cmake -P cmake/CheckLayers.cmake` passes; every deliverable has tests; the documents match the code; every step-in is catalogued.
Report: what was built (commits), what each test proves, what could not be verified, what remains and why, and the open questions for the owner. The owner or the lead session merges your branch.
