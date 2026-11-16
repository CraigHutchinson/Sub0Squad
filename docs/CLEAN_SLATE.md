# Clean slate: a sandbox project with no pre-built plan and no reference inside the harness

Status: 2026-11-15. Owner direction (this is the next priority, ahead of more baseline campaigns). Design only; nothing below is built except the validation-suite split.

## 1. The principle

Everything the harness runs on must be something a **real project** could supply: a request in words, answers to clarifying questions, and what the squad itself produces. Two things we have been leaning on are not that:

* **A hand-written plan** (`docs/calculator-cpp.plan.json`, the mock planner's blueprints). Every real run so far started from one. The planner role, the one that turns a bare request into items, contracts and tests, has never run. Failures that begin in the plan (underspecified or wrong contracts, an unusable decomposition) were fixed in the blueprint by us and so never happened to the squad.
* **A reference implementation** (the mock agent's correct files; the C++ oracle). A real project has none. An oracle in the gate made the process look better than it is and leaked the answer to the tester (see `docs/WORKER_PROBE.md`).

The harness therefore contains neither as a dependency. Blueprints stay only as **mock-agent fixtures for fast machinery tests**; they never start a real-model run.

## 2. The split: harness versus validation suite

| | harness (what a real project gets) | validation suite (`validation/`, only we have it) |
|---|---|---|
| input | the request in words, owner answers | the sandbox spec: a bare prompt, never shown as a plan |
| plan | written by the squad's planner (a model), reviewed by the squad | none; the plan is an OUTPUT to be graded |
| tests | written by testers, checked by expectation QA and independent readings | a reference acceptance suite the squad never sees |
| judgement of success | the squad's own gates and QA | `validation/grade_run.py`: reference acceptance on the product, tests-versus-reference on the squad's tests, plan quality review |
| reference implementation | none | `validation/reference/` |

Built now: the oracle is out of the harness; `validation/grade_run.py` grades an archived run (tests-vs-reference and product-vs-reference); the seed 1000 wrong-literal tests are a fixture in `validation/fixtures/`. First reading of the archived seed 1000 run: the accepted engine tests agree with the reference, the accepted app tests do not (the `--screen` frame's padding), and the product fails the reference acceptance.

## 3. The clean-slate sandbox project

* **The prompt:** `write a scientific calculator in C++` (and `... as a web page`, and the Python variant), unchanged, **with no blueprint**. Triage must not find one: the known-blueprint path is disabled for real-model runs; the request goes to the planning stage.
* **Planning stage** (exists in part: `--process clarified` asks what the request leaves open once): a **planner** job (the 35B, frontier off) turns request plus answers into a plan-2 document. The plan is validated by `Plan::fromJson` and the plan checks; a plan the validator refuses goes back to the planner with the validator's messages (a repair turn), the same loop a developer has with a compiler.
* **Plan review** (a distinct role, ideally a second sampling or a smaller prompt): checks the plan for the failures the campaign found: contracts that contradict their own examples, a test card that names no argument order, a division finer than the model can finish in the wall budget (seeds 1001 and 1002: 19 member sections), items with no acceptance, missing files. Findings are issues raised to the planner (CONCERN channel of `docs/CONTEXT_AND_PLANNING.md`), answered with an amendment or a reason.
* **Expectation QA** (backlog item of the same date): the explicit gate step that judges tests against the contract, by independent reading (a second tester, or the QA persona working the contract's own examples), not against a reference.
* **Golden examples in the contract:** the planner is asked to put worked examples into the contract (`2 + 3 * 4 = 14`). They are checkable data a real project's author can write, and the cheapest available check on a tester's literals.
* **Grading:** after each run `validation/grade_run.py` produces the independent verdicts; the campaign report adds them next to the squad's own result. The squad never sees the reference; the harness never reads it.

## 4. What is needed (build order)

1. **The planner role**: a prompt, the plan-2 schema as the reply format, validator-driven repair, the plan written to the workflow (`plan.json`), journaled like any job; no blueprint lookup for real-model runs (`--blueprint` stays an explicit opt-in for machinery tests).
2. **Plan checks** in the validator for the mechanical failures already seen (section count against budget; every tester card states the check argument order or the harness lint removes the need; every contract has at least N examples; every developer item depends on a tester).
3. **The plan review role** and the issue loop (needs the CONCERN channel; a rule-based keeper first).
4. **Expectation QA** as a gate step.
5. **Campaign driver**: `--no-blueprint` as the default, the grader run after every run, the report columns for plan validity, plan repairs, review findings and the reference verdicts.
6. **A clean-slate baseline campaign** (20 seeds): the number the whole project is judged by. The earlier baseline (3 runs with a hand-written plan) is kept as the contrast: `out/campaign/baseline-prefix`.

## 5. Risks

* A 35B planner may produce unusable plans most of the time; that is a finding, not a reason to bring the blueprint back. The levers are the prompt, the repair loop, the review and the plan checks.
* Without a reference in the loop, wrong tests are caught only by the squad's own checks; the first campaigns may fail more often than the blueprint runs. They measure the real process.
* The grader's reference is itself ours and could be wrong; its tests are the same ones the mock agent passes, run in CI, and a disagreement is read by a person before it is believed.

## 6. Refinements (owner direction, 2026-11-15, while the first probes ran)

**6.1 A larger context.** The first real planner probes ran into the slot, not the model: the engine runs `-c 32768 -np 4`, so each slot holds 8192 tokens, and a planner reply of 5,000 tokens plus the prompt and one repair turn overflowed it (probe 2). Today's answer is to make the planner small (a reply cap, a size check, no repeated refused reply). The structural answer is to let roles have different windows: a planning role is one long conversation, a section writer is many short ones. Options, to be measured on this host (the 35B uses about 23 of 30 GB; the KV cost per token has not been measured, only guessed, so the first step is to read it from the engine's start log):
* fewer, larger slots for the planning phases (`-np 2` at 16384 each, or one 32768 slot) with the width restored for the execution phases; the engine is restarted between phases by the campaign driver, or two engine services share the memory budget (the second a small model);
* a larger total (`-c 65536`) if the KV memory allows, with the same four slots at 16384;
* a per-role window in the engine profile (`ctx=` per route), so a role is only given what its context honestly needs.
The planner and every other role must read the window from the profile and budget against it (the planner already counts the previous reply; the rest do their own trimming).

**6.2 The planner knows the slot goal.** A plan is a shape: width, depth, item size. The planner is told what the engine can run at once (`slots`, the window per slot, the measured tokens per second per request) and that the goal is to keep every slot busy: so independent deliverables are independent items with no shared files, the critical path is short, and no item is bigger than one slot's window and the wall budget. `plan-lint` (width, critical path, rough minutes) already measures this; its numbers go back to the planner as part of the repair messages, not only as warnings.

**6.3 Planning in phases, continuously.** One planning step is the wrong unit. The shape that fits how development actually goes:
* **Architecture and design phase** (once, at the start, then whenever the design changes): L0 intent, L1 architecture and rationale, L2 contracts (`docs/CONTEXT_AND_PLANNING.md` section 3). The output is the design document, the interfaces (the plan's scaffold: headers) and the decomposition into deliverables; it needs the long window, so it runs wide-context and narrow.
* **Execution planning** (many times): from the current design and the current state, the next items: their cards, tests, order and width for the slots available NOW. It runs as development proceeds: after each accepted item or a failure the plan keeper may add, split, merge or reorder items (`Plan::extended`, the contract amendment of `docs/PROCESS_DESIGN.md`), and every change goes through the consolidation step (change log, regenerate what is derivable, mark dependents stale).
* The two phases are one continuous loop with different cadence and different windows: design changes are rare and expensive, execution planning is frequent and cheap. The process is data (`planned` today is plan, build, integrate): the target is `design -> plan next wave -> build -> integrate -> review -> plan next wave ...`, a stage list with a loop condition, run until the acceptance is met or the budget is.
* This is also where wrong tests and thin contracts get corrected: a concern raised against the design reopens the design phase for that entry only.

**Order of work.** (1) finish and record the first probes with the single-phase planner (running now); (2) measure KV memory and decide the window plan; (3) pass `slots` and the window to the planner and feed `plan-lint` into its repair messages; (4) split the planner into design and wave-planning roles on the `planned` process; (5) the plan review and expectation QA; (6) the loop stage kind.

## 7. First probe (the planner on the real 35B-A3B, 2026-11-15)

Method: `sub0squad llm-planner` with the request "write a calculator in C++", 4 slots at 8192 tokens each, at most 4 turns. No blueprint, no reference.

| Batch | Seeds | Valid | Notes |
|---|---|---|---|
| probes 1 to 4 (cpp, web, py) | single runs | cpp: 1 of 3 valid after fixes to the harness; web 1 of 1; py 1 of 1 | probe 1 timed out (loaded machine), probe 2 hit the token cap and the slot size |
| cpp-5 | 1, 2, 3 | 2 of 3 | seed 1 copied a stand-in message from the repair history |
| cpp-6 (repair protocol fixed: a long refused reply is dropped and the request is re-asked with the reasons) | 1 to 5 | 3 of 5 (seed 3 in 1 turn, seed 5 in 3, seed 2 in 4) | seeds 1 and 4 never got valid JSON out of the `card` of the CLI tester item in 4 turns |

What the repair loop caught (all real, all mechanical): a missing stub for `src/main.cpp`, a stub of more than 30 lines (an implementation, not a stub), a scaffold that did not compile (`-Werror` unused parameters), a missing header, and malformed JSON in one item's `card`.

Plan shape: always 4 items, an engine pair and an app pair. The widest level is 2, so on 4 slots the lint reports 33 to 41 % fill, a speedup of 1.3x to 1.7x, and a backfill rate of 0 %. The warning fires every time. The planner is told the slot count but does not act on it yet; the lint is recorded, not fed back into repair turns.

Plan quality, honestly: the checks make the plans well formed and compilable, not good. Seen so far: hedged contracts, fragile or wrong worked examples (for example `10 / 3 = 3.3333333333333335`, `10 / 0 = Error`), and an over-large last item. Nothing here has been run through the squad yet; no plan from this probe has been built into a project.

Failure classes: (1) JSON structure errors in a deeply nested `card` (2 of 5): a small model loses track of braces; a flatter plan form or a per-item reply would avoid it. (2) Scaffold stubs that do not compile under `-Werror`: caught and repaired in the same run. (3) Under-allocation of ready work: not caught by any check.

Not verified: a build from any of these plans, the success rate over a larger sample (5 seeds is a rough figure), and whether feeding the lint into the repair turn raises fill.
