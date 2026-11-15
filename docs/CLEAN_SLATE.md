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

* **The prompt:** `write a calculator in C++` (and the web and Python variants), unchanged, **with no blueprint**. Triage must not find one: the known-blueprint path is disabled for real-model runs; the request goes to the planning stage.
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
