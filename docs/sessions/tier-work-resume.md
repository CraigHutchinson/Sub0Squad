# Follow-up session: resume the tier work (hello world, then T0 to T2)

Status: open (written 2026-11-23). Backlog: [the proving ladder and reasoned workflow decisions](../BACKLOG.md#now-the-proving-ladder-and-reasoned-workflow-decisions-owner-2026-11-23).
Evidence: [RESUME.md](../RESUME.md), [T0_LAYERS.md](../T0_LAYERS.md), [BLOCK_PROVING_GROUNDS.md](../BLOCK_PROVING_GROUNDS.md), the
[canvas reasoning brief](canvas-reasoning-workflow.md).

## Goal

Take the proving ladder up from its lowest rung. First make "Write a hello world app in C++" pass end to end through the planned process. Then make the
planner's sizing a reasoned decision instead of a fixed bar. Then measure T0 on the simple profile and work its failing cases until the gate passes,
which opens T1. Do not start larger projects until the calculator passes. Do not tune the llama configuration: another session owns it.

## What is known

- **The ladder** and where each rung stands: [RESUME.md](../RESUME.md) section 1. T0 is not passed: the best full cohorts were 73/78 (legacy
  visible-range, `fd4d8eb`) and 69/78 (think-plan-act, `7ec8209`). The failures are in wrong-approach, skeleton (1/3) and write-feature (2/3). T1 is
  closed.
- **Hello world** failed on 2026-11-23 because the C++ harness required library sources. That is fixed in `src/cpp_templates.cpp` (an INTERFACE library
  for an app-only project) and checked by hand, but no unit test covers it and the request has not been re-run. The failed run's home was
  `D:\tmp\.local\state\sub0squad`, with a daemon from `out/build/default` that may still be running. That isn't this session's: leave it unless the
  owner says otherwise.
- **The sizing bar**: the planned process passes `--min-speedup 2.0` to the planner (`registry/processes/planned@1.json`, `src/process.cpp`). The
  2026-11-20 calculator planner spent 8 turns on it and ended at 1.31x (`out/runs/calculator-e2e-20261120-161815/workflow/planner.json`).
- **Tests without a plan**: the same run accepted a CLI test file with 3 tests and never revisited it.
- **Thinking**: the 35B writes code inside its reasoning, which is why think-plan-act is the default. The measured arms and the open ones are in the
  canvas reasoning brief.

## Steps

1. Add a test that configures and builds an app-only cpp-cmake workspace (only `src/main.cpp`, one test), so the harness fix is held.
2. Run "Write a hello world app in C++" through `sub0squad do ... --agent llm --executor fork`, in an isolated home with the dev build. Grade it, and
   fix each harness defect it finds in the harness. If the planner splits it into parallel items it is too small for, that is step 3's evidence.
3. Replace the planner's hard speedup bar with a decision step that states its evidence (the plan's size, the lint's slot fill) and its reasoning,
   and records them in `planner.json`; a small project may stay serial. Then design the tester's test-plan step (BACKLOG) before building it.
4. Run a simple-profile T0 cohort on a fresh `T0_OUT` (DEV_SETUP, the isolated operator session). Work the failing cases through the cases they
   require, one A/B at a time on the same seeds.
5. On a T0 PASS, run T1, then T2, then T0 on the 9B.

## Done means

Hello world passes end to end and is graded, with the harness fix held by a test. The planner's sizing decision is recorded with its evidence. A
simple-profile T0 cohort's `gate.md` reads PASS, or the failing cases each have a recorded cause and the next A/B. RESUME and this brief's Outcome say
where it got to.

## Environment and constraints

Windows host; MinGW-w64 dev tree `out/build/mingw-dev`, release `out/build/mingw`; `export CPM_SOURCE_CACHE='D:\Craig\GitHub\Sub0Squad\out\cpm-cache'`.
The engine is the 35B on `llama-server` port 8092 ([RESUME.md](../RESUME.md) section 5). The owner's standing rules are
[STATUS.md](../STATUS.md) section 6: ask before downloads or installs, nothing leaves the machine (frontier off), commit as you go and confirm before
pushing, ask before changes to Sub0Pipeline or Sub0Pub. A `run_t0.sh` loop takes a lock, so run one at a time. An experiment switch must be passed
through `src/shim.cpp`, or the job never sees it.
