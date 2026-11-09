# Sandbox harness: a project-scope task delegated into work items (mock agents, real gates)

Status: DESIGN + first implementation, 2026-11-09. Purpose: exercise the whole delegation loop of DESIGN sections 6, 8a and 16
(planner, fan-out, per-item gates, integrator, decision policy, escalation, ledger) **before** any real model is attached, with
work that has a real, checkable outcome.

## The scenario

Task given to the squad: **"write a scientific calculator"** (`project submit --task calculator-scientific`). Nobody tells the squad how
to split it. An internal **decision** does:

1. The **planner** (a job kind; here `mock-planner`, later a model planner) turns the task into a validated `plan-1`: work items
   with a goal, an editable-file allowlist, dependencies and a complexity estimate. Small tasks (`calculator-basic`) come back as
   **one** item (no split); the scientific one as six items: `tokenizer`, `parser`, `functions`, `evaluator`, `cli`, `tests`.
   The decision "split or not" is the planner's, recorded as a decision record with its reasons.
2. The daemon **validates the plan as data** (ids unique, dependencies exist and are acyclic, editable files disjoint, relative
   paths only, at most 32 items) and never executes it on trust. A rejected plan fails the workflow with the reason.
3. **Fan-out** (staged pipelines, DESIGN section 11): every item whose dependencies are accepted becomes an ordinary squad job
   (`role: agent`), running in parallel up to the daemon's job limit. The mock agent writes real C++ files into
   `items/<id>/out/`.
4. A separate **gate** job (`role: gate`) per item compiles and runs that item's own unit test: the trusted evaluator decides
   acceptance, never the agent's claim. A gate failure or an agent failure goes to the **decision policy**.
5. The **decision policy** (`IDecisionPolicy`, rule based) answers `retry | escalate | fail` with reasons and its inputs, recorded in
   the journal: retry on the local tier up to `maxLocalAttempts` (default 2), then **escalate to `frontier-stub`** if the frontier
   mode allows, else fail. `frontier-stub` runs the full path (decision, permission check, ledger entry with notional cost) and
   sends nothing anywhere; there is no network code in the tree. The frontier mode is a daemon setting (`--frontier off|stub`,
   default `stub` in development); `live` does not exist yet and is only ever enabled by the owner in the portal (DESIGN section 16).
6. When every item is accepted the **integrator** job merges the item outputs into one project directory, builds it, runs the
   combined test program and a smoke command. Only then is the workflow `succeeded`. Nothing is pushed or merged anywhere.
7. Every job that ends writes a **ledger** record (`ledger-1`): workflow, item, role, attempt, tier, `stub` flag, outcome, duration,
   notional tokens and cost, so local-versus-frontier share is measurable (DESIGN sections 15, 16).

## Why mock agents still produce real code

Gates must be able to fail for real reasons. The mock agent holds correct and deliberately broken variants of each module
(compile error or wrong result). Failure injection (`--inject tokenizer:1`) makes the local tier fail the first N attempts of an item,
so the retry path, the escalation path and the "never silently degrade" rules are tested by outcomes of a real compiler run, not by
flags. The `frontier-stub` tier always produces the correct variant (a stand-in for "the stronger model got it right").

## Level-triggered engine (restart safe)

The workflow engine is a pure function from (workflow record, plan, the jobs labelled with the workflow, settings) to a list of
actions (`startJob`, `loadPlan`, `recordDecision`, `finish`). It is re-evaluated after every job finish and at daemon start, so a
daemon restart or crash needs no special recovery: the next evaluation derives the same next steps from the journal. Jobs carry
`workflow`, `item`, `role` and `attempt` labels in their `job-spec-1`; decisions are journal records keyed by (item, attempt) so a
decision is made exactly once.

## Where real engines plug in

The same plan, gates, policy and ledger apply unchanged when `mock-agent` is replaced by a worker that drives llama.cpp, Sub0Llm or
Strata (M5), or by a model planner. `frontier-stub` becomes a real provider only in M7 by the owner's explicit switch. A task
more expansive than a calculator (units, statistics, history, plotting) is a bigger plan, not new machinery; the templates here stop
at the scientific calculator because a template cannot grow without a model.

## Files

`include/sub0squad/plan.hpp` (plan-1), `policy.hpp` (decisions), `workflow.hpp` (store + engine), `ledger.hpp`, `sandbox.hpp`
(planner, agent, gate, integrator, calculator templates). Subcommands (hidden, used as job commands): `mock-planner`, `mock-agent`,
`sandbox-gate`, `sandbox-integrate`. Owner commands: `sub0squad project submit|status|cancel`.
