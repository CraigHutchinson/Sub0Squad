# Resume here (2026-11-23)

The tier work is **paused** with the tree clean and pushed. This page is where it stands and what comes next; the session brief to start from is
[sessions/tier-work-resume.md](sessions/tier-work-resume.md). The owner's goal is unchanged: prove the harness builds projects from a bare request
with local models. The Qwen3.6-35B-A3B is the minimum bar, and nothing leaves the machine (frontier stays OFF). The previous versions of this page
(2026-11-16 and 2026-11-17: the clean-slate path, the scientific request, the parallelism bar) are in git history ([ARCHIVE.md](ARCHIVE.md)).

## 1. The proving ladder

Each rung is run only once the rung below passes; a larger project waits until the calculator is achievable.

| Rung | What one run is | Where it stands (2026-11-23) |
|---|---|---|
| **Hello world** (new lowest bar) | `sub0squad do "Write a hello world app in C++"` through the planned process | First run failed every attempt on a harness defect, now fixed (section 2). Not yet re-run. |
| **T0** | One worker, one seeded edit on the reference calculator (`validation/tiers/run_t0.sh`, `gate_t0.py`) | Not passed. Last full cohorts: 73/78 within budget (legacy visible-range arm, `fd4d8eb`) and 69/78 (default think-plan-act, `7ec8209`). The failures are in the whole-engine cases: wrong-approach and skeleton 1/3, write-feature 2/3. |
| **T1** | One worker builds the whole calculator against the owner's tests (`run_tier.sh <repo> 1`) | Closed: it needs a T0 PASS. Forced runs are diagnostic only ([T0_LAYERS.md](T0_LAYERS.md) 2.4). |
| **T2** | The same plan with forked sections | Closed behind T1. |
| **Clean slate** | The scientific calculator from the request alone (planner, testers, developers, integration) | The 2026-11-20 run (`out/runs/calculator-e2e-20261120-161815`) built and passed 16 tests, but it shows the two design gaps in section 3. |

**T0 in detail.** The run defaults to the **simple** profile: the fixture plan, one Canvas worker, think-plan-act (`SUB0SQUAD_THINKPLAN=read-handoff`),
no ladder past the worker's turns. `T0_PROCESS_PROFILE=legacy` replays the broader worker policy; the two profiles never share an output directory.
The simple profile has **not yet had a full cohort**, and its token calibration is separate from the legacy one. Commands, the isolated operator
session and the watch tool: [DEV_SETUP.md](DEV_SETUP.md#isolated-t0-and-t1-operator-session). The design of the tree, the modes, the token
accounting, the edit-address shootout and the golden retry paths: [T0_LAYERS.md](T0_LAYERS.md). The history of the 2026-11-18 iteration (early stop,
half-written cut-off hint, undeclared-member/-many, the thinking A/B): [BLOCK_PROVING_GROUNDS.md](BLOCK_PROVING_GROUNDS.md).

**What thinking showed (2026-11-18/19).** Before then, every T0 and T1 run had thinking off. With it on, undeclared-member and undeclared-many went
10/10 in 2 or 3 turns, against 1/5 and 5/5 without. But the 35B drafts the whole code inside its reasoning, so on larger edits the thinking ran to its
cap and was thrown away. That is why think-plan-act is now the default: hidden thinking is followed by a visible plan with no code, then an act turn with
thinking off. The open arms (bounded options, a short thinking budget, presence 1.5, compact repair context, scratch checks) are in the
[canvas reasoning brief](sessions/canvas-reasoning-workflow.md).

## 2. Fixed on 2026-11-23: the harness assumed a library

The C++ harness's CMakeLists declared a `calc` library from every `src/*.cpp` except `main.cpp`. A hello world has only `main.cpp`, so configure
failed ("No SOURCES given to target: calc"). The tester could not change the harness's file, so all five attempts of its one item failed. Now an
app-only project gets an INTERFACE library (`src/cpp_templates.cpp`). I checked it by hand on that run's workspace: the tester's test fails on the stub
and passes on a real hello world. No unit test covers it yet (BACKLOG).

## 3. The design direction the owner set on 2026-11-23

Recorded in [BACKLOG.md](BACKLOG.md), "Now: the proving ladder and reasoned workflow decisions":
1. **A reasoning or evidence step in each workflow decision**, not a hard limit. The planned process's `--min-speedup 2.0` bar sent the calculator's
   planner round 8 turns and still ended at 1.31x, and the same bar would split a hello world. A threshold becomes evidence for a recorded decision.
2. **A test plan before tests.** In that run the CLI tester was accepted with 3 tests and nothing asked whether more were needed. Plans per area (code,
   tests, interfaces, and others the agents raise) are living documents, revised as work lands and checked before the project is accepted.
3. **Hello world first, then the calculator, then larger projects.**

Earlier direction still in force: no pre-built plan or reference in the harness ([CLEAN_SLATE.md](CLEAN_SLATE.md)); repairs are edits, not
regenerations; every change to how the squad works is an A/B on the same seeds ([EXPERIMENTS.md](EXPERIMENTS.md)).

## 4. Priority order for the next session

1. **Hello world end to end.** Add a test that configures an app-only workspace, run the request through `do`, and grade it. Fix what it finds the same
   way: a defect in the harness is fixed in the harness, and the model is never asked to work round it.
2. **The reasoned decision step for planning size**, so hello world is not forced into parallel items. Then the test-plan step for testers.
3. **A simple-profile T0 cohort** on a fresh output directory, with the calibration kept apart. Work the whole-engine cases through the cases they
   require (write-engine, replace-function), using the canvas reasoning brief's arms.
4. **T1** once T0 passes, then T2. Then the 9B on T0.
5. The tracked workstreams in [BACKLOG.md](BACKLOG.md) (issues 2 to 7): the manager-led second attempt, workflow extension seams, prompt
   restatement, unary-minus proving grounds, the calculator receiving case.

## 5. Engine and host

The 35B (`Qwen3.6-35B-A3B-APEX-I-Compact.gguf`) runs on `llama-server` on port 8092, alias `qwen35`. As run on 2026-11-23: `-c 131072 -np 4`,
`--reasoning-budget 2048`, `--jinja`, flash attention, q8_0 KV cache, `--sleep-idle-seconds 900`, and sampling defaults of temperature 0.6, top-p 0.95,
top-k 20, presence 0. Model-specific guidance (the card's sampling, `enable_thinking`, templates) belongs to Sub0LlmEngine (`docs/MODEL_BEHAVIOR.md`).
Another session owns tuning the llama configuration. A 9B is downloaded for T0 once the 35B passes.

## 6. Rules that still bind

The owner's standing rules are in [STATUS.md](STATUS.md) section 6, and that list is complete. In short: STOP AND ASK before downloads or installs,
credentials, the final project name, enabling frontier, and changes to Sub0Pipeline or Sub0Pub. Commit as you go and confirm before pushing. One
CPU-heavy job at a time. Report only what was observed. An experiment switch reaches a job only when `src/shim.cpp` passes it through (a lesson from
2026-11-18). One `run_t0.sh` loop at a time (it takes a lock).
