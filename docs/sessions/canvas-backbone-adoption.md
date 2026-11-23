# Follow-up session: adopt Sub0Canvas as the squad's backend (backbone migration, phase 1)

Status: open (written 2026-11-23). Backlog: [layered document, the backbone](../BACKLOG.md#next-what-the-experiments-need). Design: Sub0Canvas
`docs/PLAN.md` (phase 1, section 5 "Across repositories"), `docs/BACKBONE.md` section 8 (how the squad's pieces become layers and adapters),
`docs/INTEGRATION.md` (how a change reaches this repository).

## Goal

Make Sub0Canvas the one home of the canvas backend this repository uses: the reply applier and turn control move out of Sub0Squad, the pin moves to
a release tag, and the squad's copies are deleted. This is the first step of Sub0Canvas's phase 1 and the base every later backbone slice lands on
(B0's format revision, then the per-attempt store). Behaviour must not regress on the squad's tests or on T0. Do not build backbone mechanisms
(events, inboxes, claims) here: they are Sub0Canvas slices and arrive as pin bumps.

## What is known (checked 2026-11-23)

- **Both trees are clean on `main` and pushed.** Sub0Canvas: `ctest --preset full` 8/8, `npm test` 40/40. Sub0Squad: `ctest` 47/47.
- **The pin** is the commit `fcaa76c` in `cmake/dependencies.cmake`, past the last tag `v0.2.1`. Sub0Canvas has 41 commits since `v0.2.1`; the
  library ones since the pin only switch its tests to Sub0Platform's shared helper.
- **Sub0Squad still carries its copies**, which Sub0Canvas took over on 2026-11-18 (its RESUME, "Known gaps"):
  - `include/sub0squad/turn_control.hpp` and `src/turn_control.cpp` (Canvas: `lib/include/sub0canvas/turn_control.hpp`);
  - `applyCanvasReply` and `applyAsTransactions` in `src/canvas_agent.cpp` (declared in `src/agent_impl.hpp`; Canvas: `applyReply` and
    `applyReplyText` in `canvas.hpp`). Used by `canvas_agent.cpp`, `blocks.cpp`, `llm_agent.cpp` and `tests/test_blocks.cpp`.
- **The copies have drifted: the squad's are newer.** Since the move the squad added, in turn control:
  - `Runaway::detail` (what repeated), used for the thinking-loop carry-over (`canvas_agent.cpp` around line 812, `daemon_governance.cpp:129`);
  - `endsInsideBlock(text)`, the half-written cut-off check (`canvas_agent.cpp:398`).
  These go to Sub0Canvas first, or the squad loses them. Diff again before starting: other T0 work may have touched them since.
- **One intended behaviour change.** Canvas's `applyReply` makes all-or-nothing atomic (a refusal leaves the canvas exactly as it was, whole files
  written earlier in the reply included). The squad's copy can leave whole files written before a later refusal. The partial path matches.
- **Sub0Canvas's owner decisions** (its `PLAN.md` section 3): O1, the v0.3.0 release, was approved 2026-11-23; O2-O13 are still open. This step needs
  only O1.

## Steps

1. **Sub0Canvas: upstream the drift.** Add `Runaway::detail` and `endsInsideBlock` to `turn_control`, each with a test in `test_turn_control` and a
   requirement row (R-TURN in `docs/REQUIREMENTS.md`). Diff both copies of `turn_control` and the reply applier once more, until nothing remains
   that only the squad has.
2. **Release v0.3.0** (Sub0Canvas `PLAN.md` O1). **The owner approved it on 2026-11-23**, once step 1 is in and both suites are green. Push the tag; the
   full CI tier runs on it, and it must pass before the pin moves.
3. **Sub0Squad: adopt.** Bump the pin to the tag. Include `sub0canvas/turn_control.hpp`, replace `applyCanvasReply` / `applyAsTransactions` with
   `applyReply` / `applyReplyText`, and delete the copies and their declarations. Keep `test_blocks`' recorded replies running against the new pin.
   Drop squad tests only where Sub0Canvas's `test_replies` / `test_turn_control` now hold the same case.
4. **Check for regressions.** Run `ctest` (quick, then full) and the pre-push gate. Then run a short T0 run on the simple profile, on cases that
   exercise refusals and cut-offs (`half-written`, `undeclared-member`; `validation/tiers/run_t0.sh`, one loop at a time), and compare with the
   last cohort. Any difference is the atomicity change or a bug: record which.
5. **Then the next slices, each its own pin bump:** B0's format revision (Sub0Canvas `docs/sessions/b0-format-catch-up.md`: the producer here
   writes item `id`, `column`, `layerMeta`, per-turn events), then the per-attempt store (O10, after the owner answers).

## Done means

Sub0Squad pins a Sub0Canvas release tag and holds no copy of turn control or the reply applier (`grep -rn applyCanvasReply src include` is empty).
Both suites are green, and a T0 check on the new pin matches the last cohort or its differences are explained. Sub0Canvas's RESUME no longer
lists the copies as a known gap. This brief has an Outcome.

## Environment and constraints

Windows host. Sub0Squad: MinGW dev tree `out/build/mingw-dev`; the pre-push hook builds `out/build/default`, and a running daemon or portal exe
locks it. Sub0Canvas: `cmake --preset default -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`, `ctest --preset quick|full`,
`npm test`. Both: `export CPM_SOURCE_CACHE='D:\Craig\GitHub\Sub0Squad\out\cpm-cache'`. A change to the library is made in Sub0Canvas first, then
the pin is bumped here. The owner's rules: [STATUS.md](../STATUS.md) section 6 and Sub0Canvas `AGENTS.md` ("Rules"). In particular: commit as
you go, confirm before pushing or tagging, and never skip hooks.
