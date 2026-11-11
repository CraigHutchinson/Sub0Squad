# Building blocks: validating the means of working from the ground up

Status: proposal, 2026-11-11. Owner direction: step back, treat the way the squad works as a stack of building blocks, **validate each from the bottom**, converge
and integrate into an improved architecture. The first real 9B run showed why: failures arrived from every layer at once (a plan defect, a workflow rule, a
scheduling stall), so nobody could say which layer was sound.

## The method

For each block, bottom to top: state its **contract** (inputs, outputs, what it promises), its **invariants** (checkable without a model), its **characterisation**
(measured behaviour with the real 9B on recorded cases), and its **bar** (the number that says "sound enough to build on"). A block is not used by the one above until it
meets its bar. Failures found above are pushed down to the block that should have prevented them and become fixtures there (INTERVENTIONS.md).

| Block | Contract | Invariants (no model needed) | Characterisation (real 9B, recorded) | Bar | Today |
|---|---|---|---|---|---|
| **0 Engine** | prompt → completion, per-slot cache, seed | health; slot count; throughput per slot; same seed, same short reply | decode tok/s vs slots, prefill, cache hit | stable throughput; no OOM at the planned slot count | measured; seeded determinism holds for short replies only |
| **1 Call** | system + card → parsed reply (files / edits / plan) | the parser accepts every recorded reply form; rejects with a located reason | share of 9B replies parsed first time, by format | ≥95 % of recorded replies parse or are repaired mechanically | tolerant parsers built; no corpus measurement |
| **2 Check** | files → pass/fail + located diagnostic | syntax, allowlist, counts, stubs, oracle: each gives the same verdict twice, and names file/line/values | does the diagnostic let a developer fix it? (replay with the diagnostic) | every failure class has a diagnostic naming where | syntax + expected/actual built; oracle opt-in |
| **3 Attempt** | card → accepted files or a located failure, bounded tokens | repair never loses a better earlier draft; terminates | pass rate per attempt for a leaf item, by size | ≥70 % of leaf items (≤1k tokens) pass within the repair turns | drafts carried, continuation built; rate unmeasured |
| **4 Node ladder** | attempts → accepted, or a classified failure | the decision table is total and pure (unit-tested) | how often each rung fires and succeeds | no rung loops; spent ladder → a named cause | tested; strategies built |
| **5 Division** | item → validated parts (a plan fragment) | **validator**: every file delivered; every developer part has a tester writing its gate file; no part graded by the parent's whole test; ids unique; acyclic | architect output valid first time? after completion? | ≥90 % valid after the validator's completions | completion for two defects; no full validator |
| **6 Workflow graph** | plan → run to done/failed | a failed branch never cancels another; failure names the first cause; every item reaches a terminal state | wall time vs serial; idle slots | no cancelled-victim failures; idle slots used | rule 13 built; attribution open |
| **7 Management** | stuck item/run → a strategy | overseer/briefing deterministic on fixed inputs | which strategy rescues which signature | rescues ≥ half of ladder-spent items | built; unmeasured |
| **8 Governance** | decisions needing the owner | blocking requests hold scope; never lose a request | n/a | owner asked only about intent/permissions | built |

## Order of work (each step ends with a number, not a feeling)

1. **Build the corpus.** Turn run-v5 `w-000001` and the archived runs into fixtures: for each recorded call (prompt, reply, gate output) a case, keyed by block. No new
   model calls. This is the validation substrate for blocks 1–5 and is cheap.
2. **Block 1 and 2 from the corpus:** parse every recorded reply, run every recorded file set through the checks; list what fails to parse and which diagnostics are unlocated.
   Bar: the numbers in the table. Fix at this level only.
3. **Block 5 validator** (rows 10 and 12 unified), run over every recorded division, accept or reject before any job starts; replay the architect only on rejects.
4. **Block 3 on leaf items with the 9B**, isolated (`replay`), several seeds: the real pass rate per item size. This is the one measurement everything above depends on; if
   leaf items do not reach the bar, the division granularity (block 5) must change, not the ladder.
5. **Blocks 6, 4**: failure attribution, structural-job priority, then a whole run; the run is now a *test of the integration*, not the first test of anything.
6. **Mending ledger** (`mends.json`) wired into the gate/escalation path from step 2 onwards, so each block's failures are classified as they appear.

## The three critical ideals, and how each is held to a bar (owner, 2026-11-11)

Delegation, parallelism and the V model are not features to be added at the end; every block above is judged by whether it serves them.

| Ideal | Meaning here | Blocks that carry it | Bar (measured, not asserted) | Today, honestly |
|---|---|---|---|---|
| **Delegation** | Work goes to the smallest competent worker with a complete card; managers divide, workers do; a request for help (debugger, tester, architect, QA) is answered by another worker, not by the owner | 3 attempt, 5 division, 7 management, 8 governance | Every item's card is sufficient on its own (a worker never needs the parent's conversation); a delegated request is answered by a worker; owner asked only about intent and permissions | cards, strategies, overseer and requests built; forked sessions (`llm-fork`) built but unvalidated; the debugger worker is only designed |
| **Parallelism** | Independent work runs side by side on idle slots; division is chosen for parallelism as well as size; idle capacity runs QA and dreamer work | 0 engine, 5 division, 6 graph | Share of slot-time busy during a run; critical path vs serial time (plan-lint reports it); no cancelled-victim waits | slots and affinity built; plan-lint reports width; divisions are driven by size, not yet by free slots; structural jobs can stall behind leaf work (catalogue row 19) |
| **V model** | Top-down: requirements → architecture → parts, each level with its own test written first; bottom-up: parts integrate under the tests of the level above, failures sent down to the part | 5 division, 6 graph, 2 check | Every divided item has a coordinator whose integration test passes over the merged parts; a failure at integration names the part; no part graded by the parent's whole test | coordinator, per-round integration gate and `sendDown` built and run (run 1 divided `ui-js` and `ui-page` that way); the engine division has not yet completed on the 9B; requirements level is not modelled |

**What this changes in the order of work:** step 3 (the division validator) enforces the V (a tester for every developer part, the parent's tests stay with the coordinator) and
delegation (a self-sufficient card per part); step 4 adds a measure of parallel yield (slot-time busy, critical path actual vs planned); step 5 gives structural jobs priority
so parallel leaf work cannot starve the divisions the V depends on.

## Architecture direction this points to
- **Validate before spend:** plans, divisions, replies and cards are checked mechanically before a model or a long job is used on them.
- **Failures are classified data**, not text: signature → class → mend, with the ledger feeding the briefing and the portal.
- **Blocks are separately runnable** (replay already does this for one node; the same for parse, check, division, workflow rules), so any layer can be tested without the ones above.
- **Integration runs prove integration only:** a full run is the last test, not the first.
