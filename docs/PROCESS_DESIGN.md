# Process design: processes as data over the V, delegation and parallelism; the process controller

Status: DESIGN, revision 2, 2026-11-11. Owner of the design: this document. It supersedes the first draft after an audit of that draft against
`src/workflow.cpp` (section 1). Builds on DESIGN_AUDIT.md section 3 (four registries), BUILDING_BLOCKS.md (co-evolving structure and leaves, up-transfer,
versioned contracts, the three ideals), FEEDBACK_LOOPS.md section 4 (the bar), INTERVENTIONS.md (the ledger) and WORK_GRAPH.md sections 7, 13, 14 (the V,
requests). Nothing here is built except the harness registry (`harness.hpp`, `harness_registry.cpp`). Every new capability is off by default; the owner
decides intent and permissions only; no frontier tier without an explicit owner switch; progress is never discarded; extension is files and jobs, no plugin ABI.

## 0. Decisions in one table

| Decision | Choice | Why (and what changed from the draft) |
|---|---|---|
| What a process controls | which **stages** exist, how each stage's items come to be (plan, templates, a producer role), each item's bar, ladder and exhaustion chain, which dependency state lets a dependant start (`accepted` or `drafted`), when a stage is revisited, who answers which request kind | the rigidity is there (audit findings 1, 5, 6, 10) |
| What stays code | **two item executors**, selected by the item's role, not by the stage: the *leaf* executor (attempt, gate, ladder, exhaustion chain) and the *ascent* executor (integration rounds, send-down). Everything they do differently per process is a parameter struct | the draft's four "flows" do not match the code: `workflow.cpp` selects by `persona == "integrator"` (line 425), and `single` is the leaf executor with an empty ladder (section 1.1) |
| Stage state | **derived** from items, decisions and jobs on every call; the journal carries decisions (reopen, timebox, amendment), never derived facts | the draft journaled `workflowStageEntered/Exited` as authority, which breaks when a dispute voids an accepted item of a "done" stage (section 1.3) |
| Time | enters `advance()` only as an explicit `now` in the snapshot; a timebox exit is a journaled decision, so it is evaluated once | the draft's `stage.X.elapsed` read a clock inside a pure function (section 1.4) |
| Predicates | total, side-effect free, over a snapshot built from the journal only; no file-system facts; empty `all()` is false | `artifacts.exists` let a worker steer the process by writing a file and could not be replayed (section 1.4) |
| Items created by a stage | a new `Plan::extended` (append, namespaced ids, validated), not `Plan::amended` | `amended` needs a real parent, two or more items, file coverage and, on `web-static`, manufactures a coordinator (section 1.2) |
| The V | first-class: levels are the item tree (root, coordinators, leaves); every level has a contract version, an ascent and both transfer directions; leaves may be **drafts** that dependants can start on | BUILDING_BLOCKS, owner direction |
| Delegation | data: a process says which role answers each request kind; the owner is only ever the answer for `requirement`, `clarification`, `permission`; the validator refuses anything else | WORK_GRAPH section 20 |
| Controller | a role that picks from a **catalogue** (stages, bars, ladders already in the registry) and never authors a stage object; rules by default; model off by default; three rejections disable it per workflow | a 9B authoring a stage object is the weakest link (section 7) |
| First slice | `Process` and parameter structs, the standard process as a built-in constant and as `standard@1.json`, `advance()` reading its constants from it, `WorkflowAction::toJson()`, golden action logs of every scenario | behaviour-identical, 14 tests green, and it records the oracle every later step is compared against (section 9) |

## 1. Audit of the draft against `src/workflow.cpp`

What the draft said, what the code says, and what this revision does about it. Line numbers refer to `src/workflow.cpp` at commit 80dc36e.

### 1.1 "Four flows keep the per-item micro-engine as code": wrong axis, and too thin

| Code | Fact | Consequence for the model |
|---|---|---|
| 425 `if (v.item->persona == "integrator")` | the executor is chosen by the **item's role**, inside one loop over the plan; a coordinator and its parts sit in the same plan, same "stage" | `flow` is not a stage property. A stage sets *defaults* for its items; the executor is `role.agentless ? ascent : leaf` |
| 497-502 `restore`, `strategy`, `defer` | after `policy.decide` returns `fail`, a fixed chain runs: restore the pre-dispute acceptance (if `restorable`), else a manager strategy (`think`, then `engine` when `fallbackEngine` is set), else defer to the owner (only with `wf.spec.ask`), else fail | the draft's `allowDivide`/`allowEscalate` booleans map rungs to `fail`, which then **triggers the chain**: a spike with `allowEscalate: false` would still think, switch engine and ask the owner. The chain must be a parameter too: `exhaustion: {restore, strategies, defer}` |
| 293-299 `strategyOrder(wf)` | strategies depend on `WorkflowSpec::fallbackEngine`, a workflow switch, not a process field | the standard process's parameters are *resolved* per workflow: `strategies = strategyOrder(wf)`, `defer = wf.spec.ask`. The draft's `autonomy: ask-intent` would have silently removed `--ask` deferral (an `escalation` is not an intent question) |
| 429 `kMaxRoundTrips = 3`, 436-437 `sends` counts every `sendDown` decision of the item | the round-trip budget is per item for its whole life, not per visit | `AscentParams::maxRoundTrips`; a revisit does not reset it unless the process says so (`resetOnRevisit`, default false) |
| 454-475 the three attempt-like job kinds `agent`, `decompose`, `rearchitect` and their follow-ups (gate, amendPlan, next attempt) | `JobSpec::role` is the **job kind** (`planner|agent|gate|decompose|rearchitect|integrator`), not the persona | two vocabularies must stay apart: **role** (the persona registry entry, today `WorkItem::persona`) and **step** (`JobSpec::role`). The ledger, portal and `viewOf` (255-265) read step strings; they do not change |
| 561-567 `failedItem` | the workflow fails only when `!anyActive && out.empty()`: a **global** rule over every item of every stage and every action of this pass | any stage bookkeeping action in `out` would postpone the failure by one pass and change the journal. Stage bookkeeping must not be an action (it is derived, section 2.2), and the fail rule stays global, not per stage |
| 388-409 the planner | two tries, then "planner failed twice"; `loadPlan` is an action the daemon performs (file read, validation) | a **producer** with `retries: 1` and `accept: loadPlan`; exact |
| 570-583 the integrator | starts when `allAccepted && !anyOpenBlocking`, a job with no item, finishes the workflow on its exit | the draft rendered it as an item template "not inserted in the plan when `files` is empty and `tool` is set": an item that is not an item, which `itemStatuses()` and every `items[...]` predicate would then have to special-case. Here it is what it is: the **root ascent** of the V, a stage of kind `ascend` whose one job is the harness's integrator (section 2.4) |
| 245-272 `viewOf` | acceptance is voided by a `disputed` decision with a higher attempt; `restorable` remembers the earlier pass; both are recomputed every call | item state is derived from decisions; stage state must be derived the same way or the two disagree (1.3) |

Conclusion: the micro-engine is **two executors with a parameter struct each**, and `single` is `leaf` with `ladder: none, exhaustion: {restore: false, strategies: [], defer: false}`. The draft's `coordinator` flow is the ascent executor, selected by role, as today.

### 1.2 "Templates enter the plan through `amendPlan`": false

`Plan::amended` (plan.cpp 214-285) requires the parent to exist (`no item to replace`), at least two fragment items, that the fragment covers the parent's files,
prefixes ids with the parent's, and on a harness with `coordinators` replaces the parent by an integrator item carrying the parent's gate tests. A virtual parent
`_stage.spike` would need files and a gate to be valid at all, and would leave a coordinator behind. Stage items need a new, simpler operation
(`Plan::extended`, section 2.3) and two validator changes the draft did not list: `Plan::validate` line 175 (closed persona set) and lines 178-180 (a harness
project item needs a `node-test` gate with test files): a spike item with a `runs-only` bar is invalid today.

### 1.3 Journaled stage state disagrees with derived item state

Acceptance is voided after the fact by `disputed` decisions (a send-down from a coordinator, a guidance answer, a tester dispute). If `workflowStageExited` were
authoritative, a stage would stay "done" while one of its items is no longer accepted, and the next stage's entry predicate (`stage.build.done`) would hold
while `items[stage=build].all(accepted)` is false. The code today recomputes `allAccepted` every pass and the integrator simply does not start. So: **stage
state is a pure function of the snapshot**; only decisions are journaled (section 2.2).

### 1.4 The predicate language was unsafe in two places and wrong in one

- `stage.<name>.elapsed` and `timeboxSeconds` put the wall clock inside `advance()`: calling it twice would not return the same actions (the idempotence test),
  and a replay at another time would exit stages differently. Fix: `now` is a field of the snapshot the daemon fills from its clock and the replayer fills from
  the record's `ts`; a timebox exit is a journaled decision (`stageTimebox`) so every later pass derives it from the record (section 3.3).
- `artifacts.exists(path)` depended on a directory listing that is not in the journal (no replay) and let any worker advance the process by writing a file,
  bypassing its gate. Removed. Process control reads **accepted or recorded** facts only: gate results are in the job's `exitJson`, which the journal carries
  (`jobFinished.exit`), so `gate(item).code` and `checks(item).passed("syntax")` are derivable.
- `items[...].all(accepted)` over an empty set is vacuously true: a stage whose items are created by an action this pass has no items in the snapshot, so its
  exit would hold in the same pass. Fix: a stage's exit is evaluated only once its items for the current visit exist, and `all()` over nothing is `false` (3.2).

### 1.5 Where the draft's equivalence test could not see a divergence

| Blind spot | Why | Fix in section 5 |
|---|---|---|
| gate exit codes 3, 4, 5 (`nearPass`, `oversize`, `repeatedFailure`) | `Sim::finishJob` passes an empty `exitJson`; no scenario sets them | `Sim::finishJob(id, state, exitJson)`; three scenarios added before anything changes |
| action fields | `WorkflowAction` has no comparison; "the lists are equal" is undefined | `WorkflowAction::toJson()`; compare serialised actions in order |
| one pass vs the daemon's fixpoint | `advanceWorkflows` loops up to 16 rounds; a design that emits one stage action per pass changes the per-pass lists without changing the journal | compare **journals** after driving both engines to quiescence with the same job outcomes, and compare the per-pass lists too |
| daemon-side actions (`sendDown` target choice, `writeDispute`, `setStrategy`, `amendPlan`, `loadPlan`) | they are outside `advance()`; the Sim stubs `sendDown` as "always d2" | unchanged by this design; `test_e2e` shadow mode covers them once; the Sim's stubs are declared as such |
| `itemStatuses()` | a second consumer of `viewOf`; the portal shows it | compared alongside the actions |
| anything new (drafts, revisits, producers) | by construction has no legacy oracle | stated as unproven in section 11; tested by scenario, not by equivalence |

## 2. The model

### 2.1 Process, stage, item, executor

```
Process  = name, version, description, autonomy, answers, budgets, controller?, stages[]
Stage    = name, kind (descend | leaf | ascend | none), entry, exit, items (plan | templates | producer), defaults for its items
           (role, bar, ladder, exhaustion, capacity, depsReady), revisit?, timebox?, onFail
Item     = a WorkItem (plan-3: + stage, + contractVersion, + gate.bar); its role selects the executor
Executor = leaf (role.agentless == false)  | ascent (role.agentless == true)
```

Stage kinds say what a stage contributes to the V and nothing about control flow: `descend` produces structure (a plan, a design, a division); `leaf` does
work under the bar; `ascend` integrates a level (coordinators, the root integrator); `none` has no items (a controller-only or owner-gate stage).

**Executor parameters** (what today's constants and switches become; the standard process resolves to today's values exactly):

| Struct | Field | Today's value | Source line |
|---|---|---|---|
| `LeafParams` | `ladder` (policy name or `none`) | `rules-1` | `policy.decide` 497 |
| | `allowDivide`, `allowEscalate`, `allowReviseTests`, `allowRearchitect` | all true | `switch (dec.action)` 511-546 |
| | `exhaustion.restore` | true | 498 |
| | `exhaustion.strategies` | `strategyOrder(wf)` | 293-299 |
| | `exhaustion.defer` | `wf.spec.ask` | 502 |
| | `maxLocalAttempts` | `wf.spec.maxLocalAttempts` | 484 |
| | `agentWallSeconds` (llm, local) / (other) | 10800 / 120 | 324 |
| | `gateWallSeconds` | 120 | 342 |
| | `architectWallSeconds` (llm / other) | 5400 / 120 | 353 |
| | `depsReady` | `accepted` | 423 |
| | `drafts` | false | (new, 2.4) |
| `AscentParams` | `maxRoundTrips` | 3 | 429 |
| | `roundWhen` (`all` or `every(n)`) | `all` | 423 (deps accepted) |
| | `resetOnRevisit` | false | 436 |
| | `gateWallSeconds` | 120 | 342 |
| `ProducerParams` | `step`, `tool`, `tier`, `wallSeconds`, `retries`, `accept` | `planner`, `mock-planner`, `stub`, 60, 1, `loadPlan` | 394-406 |
| `RootAscentParams` | `tool` (harness `integrateCommand`), `tier`, `wallSeconds`, `requires` | harness, `stub`, 300, `!requests.openBlocking` | 574-577, 570 |

A disallowed rung maps the policy's `Decision` to `fail` **before** the exhaustion chain, in `advance()`, so `RulePolicy` and its tests are untouched; the recorded
decision keeps the policy's reasons and adds "rung X is not allowed in stage S".

### 2.2 Purity: everything is derived, decisions are journaled

`advanceProcess(wf, jobs, settings, policy, process, now)` is a pure function. Its snapshot:

| Fact | Derived from |
|---|---|
| items of a stage | `WorkItem::stage` (absent: the first stage with `items: plan`) |
| item state: `pending | running | accepted | failed | drafted | stale` | `viewOf()` as today, plus: `drafted` when the latest gate's `exitJson` says the syntax rung passed and the bar failed (2.4); `stale` from a `staleSince` decision (section 8) |
| stage `created(visit)` | items with `stage == s` and `visit == k` exist in the plan (templates), or `plan.present` (plan source), or the producer's job for visit k succeeded (producer) |
| stage `active` | `created && !done && !failed` |
| stage `done` | `created && eval(exit)` |
| stage `failed` | an item of the stage is `failed` and `onFail == failWorkflow`, or a producer's retries are spent |
| stage `visits` | 1 + the count of `stageReopen` decisions for the stage |
| stage `enteredAt` | the `ts` of the record that created the stage's items for this visit (the plan record, the extension record or the producer's job submission); for the first stage, the workflow's `submittedAt` |
| stage `timeboxed` | a `stageTimebox` decision for (stage, visit) |
| contract versions | `contractRevised` decisions per item (section 8) |
| controller state | `processAmend` decisions (accepted / rejected / disabled) |

Decisions (journaled `workflowDecision` with `item` = `_stage/<name>` for stage-level ones) are the only new journal facts: `stageReopen`, `stageTimebox`,
`processAmend`, `contractRevise`, `staleSince`. The existing `workflowDecision` record is reused so `WorkflowStore::apply` changes by one branch and replay
needs no new reader. `workflowProcessChosen` (the full process JSON) and `workflowPlanExtended` (the full plan after a stage added items) are the two new
record types, both carrying full text as `workflowPlanned` does, so replay never reads the registry directory.

Idempotence holds as today: every action's precondition is a journal fact that executing the action creates (a job, a decision, a plan record). Two calls before
the actions are applied return the same list. **The pass structure does not change**: one loop over the plan's items in plan order, with the stage's parameters
looked up per item; stage entry creates items (an `extendPlan` or `startJob` action) and stage exit is a derivation, not an action. The global fail rule (1.1)
keeps its exact form: `if (!failedItem.empty()) { if (anyActive || !out.empty()) return out; finish(failed) }`.

**Termination**: every action creates a fact that cannot be created twice (a job id, a decision keyed by item and attempt, a visit number), and every such
counter is bounded (`maxTotalAttempts`, `maxRoundTrips`, `revisit.max`, `budgets.maxStageVisits`, `budgets.maxItems`, `controller.maxAmendments`). So a process
file cannot make the engine loop; the worst case is a stage whose exit never holds, which is a stall the owner sees (`process explain` names the atom that is false).

### 2.3 Items created by a stage: `Plan::extended`

`[[nodiscard]] static std::expected<Plan, std::string> Plan::extended(const Plan& base, std::string_view stage, int visit, const std::vector<WorkItem>& items)`:
ids become `<stage>.<id>` (visit 1) or `<stage>.<id>.v<visit>`; `stage` and `visit` are set on each item; deps may name any accepted item of the base or a
sibling template; files must be disjoint with every item of the base that is not `stale`; `validate()` runs on the result with the process's `budgets.maxItems`
(so `Plan::fromJson` and `validate` take the limit as a parameter; today's constant 32 stays the default). The daemon applies it as the action `extendPlan`
and journals `workflowPlanExtended {id, stage, visit, plan}`. A revisit never deletes earlier items: they are marked `stale` (decision) and their outputs stay
under `items/<id>/` as drafts for the new visit's items to `include`.

Validator changes this needs (plan-3, backward compatible with plan-1 and plan-2):

- `persona` is validated against the role registry (`writes` allow/deny per role replaces lines 187-191); the three built-in roles keep today's rules exactly.
- `gate.bar` (optional) is an alternative to `gate.kind == node-test`: a harness project item needs one of the two (line 180). A bar reference must resolve.
- `stage`, `visit`, `contractVersion` are optional fields with defaults (absent, 1, 1).

### 2.4 The V as structure: levels, drafts, two transfer directions

The V is the **item tree**, not a stage. Dotted ids are the tree (`eng`, `eng.parser`), a divided item's coordinator is its ascent, the final integrator is the root's
ascent, and a `design` stage (when the process has one) is the root's descent. Each level has a **contract** (a `WorkCard::contract` for an item; `design/contracts.md`
for the root) with a version. What is first-class in the stage model:

| Ideal | In the model | Measured by |
|---|---|---|
| **Descent** | `descend` stages (plan, design, division via `decompose`) create items with contracts; a division is validated before any part runs (the division validator is the `design` stage's bar and the `amendPlan` acceptance check) | share of divisions valid first time |
| **Leaves as drafts** | an attempt whose syntax rung passed but whose bar failed is `drafted`; its files are kept as `items/<id>/drafts/v<n>/` and are what the next attempt starts from (built: the best-draft carry); new: a stage with `depsReady: drafted` lets a dependant start on a draft, with the card marking the included files as drafts | parts started on drafts; rework when the draft changed |
| **Ascent** | coordinators run rounds; `roundWhen: every(n)` starts a round when n parts have landed (as accepted or, with drafts on, as drafts) instead of waiting for all | rounds to integrate; time to first integration failure |
| **Down transfer** | `sendDown` (built): the failure goes to the part it implicates as a dispute with feedback | send-downs that hit the right part |
| **Up transfer** | a `proposal` request from a leaf against its parent's contract, answered by the parent's architect (section 8): the contract is revised and versioned, dependants go `stale` with their drafts kept | proposals accepted; parts kept as drafts vs restarted |
| **Reaching the top** | a revision at the root reopens the `design` stage (`revisit.on: contracts.revisedSince(stage.design)`), bounded by `revisit.max` | design revisits per workflow |

`depsReady` and `roundWhen` are the two engine changes behind "parallelism from drafts": line 423's `all_of(deps, isAccepted)` becomes `all_of(deps, isReady)` where
`isReady` is `accepted` in the standard process and `accepted || drafted` where a stage says so. A dependant that started on a draft is marked `staleSince` when
the draft's item is later accepted with different file contents (the daemon compares hashes when it records acceptance) and gets the final files as feedback on
its next attempt; it is not restarted.

### 2.5 Delegation as data

```json
"answers": { "debug": "debugger", "tester": "tester", "proposal": "architect", "escalation": "manager",
             "requirement": "owner", "clarification": "owner", "permission": "owner" }
```

Every request kind has one answering role; `owner` is legal only for the three intent kinds, and the validator refuses a process that maps anything else to the
owner, whatever its `autonomy`. `manager` is the built-in rule role that applies strategies (today's lines 497-531), so `--ask` deferral is the `escalation` kind
reaching the owner **after** the manager: `exhaustion.defer` is honoured only when `answers.escalation` is `owner`-after-`manager`, which the standard process
writes as `"escalation": "manager,owner"` (a chain). Producers are delegation of structure: the stage names the role that writes its items. A role's
`requestable` lists the kinds its workers may raise (a `NEED debug` line in a reply becomes a request only if the role allows it).

`autonomy` then means only how much the owner is **told**: `ask-all` turns every stage exit and amendment into a blocking `question` first; `ask-intent` (default)
asks only the three intent kinds; `none` answers intent kinds with "proceed on the written task" and records that it did.

### 2.6 Parallelism

Items of a stage run on dependency readiness, as today. Stages may overlap: two stages are active together whenever both predicates hold (legal, untested;
the first presets are sequential). Capacity classes (`reserved | normal | idle`) are a `JobSpec::capacityClass` field, resolved item > stage > role, honoured by
the scheduler only behind `--capacity-classes`: `reserved` keeps one daemon slot and one engine lease slot that others cannot take; `idle` starts only when no
`normal`/`reserved` job is ready and is shed first. Until the switch is on every class behaves as `normal`. The parallel yield of a process is a report
figure (slot-time busy, critical path actual vs plan-lint's estimate), not a control input, until a measurement says the controller should read it.

## 3. The predicate language

### 3.1 Grammar

```
expr   := or ;   or := and ("||" and)* ;   and := unary ("&&" unary)* ;   unary := "!" unary | cmp
cmp    := atom (("==" | "!=" | "<" | "<=" | ">" | ">=") atom)?
atom   := INT | STRING | "true" | "false" | "(" expr ")" | path
path   := segment ("[" filters "]")? ("." segment)*       e.g. items[stage=build,role=developer].count(accepted), job(integrator).succeeded
segment:= IDENT ("(" args? ")")? ;   filters := IDENT "=" (IDENT | STRING) ("," filters)* ;   args := atom ("," atom)*
```

Total, side-effect free, no loops, no user functions, no arithmetic (a count compares with a literal). Types are checked at load: an unknown path, filter key,
aggregate, state name or a comparison between different types is a registry error naming the file, the stage and the column.

### 3.2 Vocabulary (the whole of it)

| Path | Type | Meaning |
|---|---|---|
| `always` | bool | true |
| `plan.present` | bool | `wf.plan` loaded |
| `stage.<name>.created / .active / .done / .failed / .timeboxed` | bool | derived as in 2.2 |
| `stage.<name>.visits` | int | 1 + reopen decisions |
| `stage.<name>.elapsed` | int | `now - enteredAt`, seconds (3.3) |
| `stage.self.*` | | the stage the predicate belongs to (templates stay reusable) |
| `items[<filters>].all(<state>) / .any(<state>) / .count(<state>) / .count()` | bool / int | filters `stage`, `role`, `id`, `prefix`, `kind`; states `pending`, `running`, `accepted`, `failed`, `drafted`, `stale`, `ladderSpent`; **`all()` over no items is false** |
| `job(<step>).succeeded / .failed / .active` | bool | the latest item-less job of that step (`planner`, `integrator`, `controller`): how the root ascent and a producer are observed |
| `gate(<item>).code` | int | the exit code of the item's latest gate job (from `exitJson`) |
| `checks(<item>).passed(<check>)` | bool | the named check passed in the latest bar run (from `exitJson`, once bars exist) |
| `requests.openBlocking` | bool | any open blocking request |
| `requests.open(<kind>)` | int | open requests of a kind |
| `contracts.revisedSince(stage.<name>)` | bool | a `contractRevise` decision later than the stage's `enteredAt` |
| `controller.verdict(<string>)` | bool | the latest accepted amendment carried this verdict tag |
| `process.amendments / .rejected` | int | counts |
| `settings.ask / .frontier / .agent / .fallbackEngine` | bool / string | workflow switches (read-only) |

Removed from the draft: `artifacts.exists` (1.4), `settings.oracleDir` (an environment variable, not a journal fact).

### 3.3 Time

`now` is a double in the snapshot. The daemon sets it from its clock; the replayer and the equivalence test set it from the record being applied. A stage with a
`timebox` emits the decision `stageTimebox` once `elapsed > timebox` and nothing else depends on `now`; every later pass reads the decision. The idempotence test
passes `now` twice unchanged; a second test advances `now` and checks that the only difference is the timebox decision.

`Snapshot` is built once per call from `(wf, jobs, settings, now)`, rendered to JSON for the controller and for `sub0squad process explain WF`, which prints every
atom of every stage predicate with its value and the first false atom of a stage that is not done.

Code: `include/sub0squad/predicate.hpp` (layer 2): `[[nodiscard]] std::expected<Predicate, std::string> Predicate::parse(std::string_view, const Vocabulary&)`,
`[[nodiscard]] Value Predicate::eval(const Snapshot&) const`, `[[nodiscard]] std::vector<AtomValue> Predicate::explain(const Snapshot&) const`.

## 4. Registries and formats

### 4.1 Common rules (kept from the draft)

```
<repo>/registry/{roles,checks,bars,stages,processes}/<name>@<version>.json   in-tree defaults, embedded at build time (src/registry_defaults.cpp, generated)
<stateDir>/registry/...                                                        the owner's extensions
<sandboxDir>/registry/...                                                      per-workflow overrides (an experiment, a replay)
```

| Rule | Detail |
|---|---|
| Identity | `name@version`; `name` is `[a-z][a-z0-9-]{1,31}`, `version` a positive integer; file name matches the fields |
| Precedence | sandbox > state > built-in for the same `name@version`; shadowing a built-in is logged and journaled (`workflowRegistryUsed` with sha256) |
| Latest | a reference without `@version` resolves to the highest version across layers; the workflow records the resolved one |
| Validation | parse (simdjson, position in the error), schema matches the directory, fields typed and in range; cross-references after all files are read; predicates parsed against the vocabulary |
| Errors | `registry check` prints `<file>:<json-path>: <message>`; a bad file is skipped; the daemon refuses to **start a workflow** that references a bad entry, never refuses to start itself |
| Immutability | a running workflow uses the copy in its journal |
| CLI | `registry list|check|show`, `process explain WF`, `process diff WF` (the process as chosen vs as amended) |

Code: `include/sub0squad/registry.hpp` (layer 2): `template <class Entry> class Registry { [[nodiscard]] std::expected<const Entry*, std::string> find(std::string_view ref) const; [[nodiscard]] std::vector<Problem> load(const std::filesystem::path& dir, Source); }`.
Entries are plain data with `fromJson`/`toJson`/`validate`, like `Plan`.

### 4.2 Role (`role-1`)

```json
{ "schema": "role-1", "name": "developer", "version": 1,
  "description": "implements one focused piece against its contract and tests",
  "system": "You own one focused piece. Be the expert on it. Keep to the interfaces.",
  "formats": ["files", "edits"],
  "writes": { "allow": ["**"], "deny": ["tests/**"] },
  "engineRoute": "developer",
  "step": "agent", "tool": "llm-agent", "mockTool": "mock-agent",
  "agentless": false,
  "defaultBar": "web-gate", "defaultCapacity": "normal",
  "requestable": ["debug", "proposal"],
  "wallSeconds": { "llm": 10800, "other": 120 } }
```

Built-in roles and today's rules: `tester` (`writes: {allow: ["tests/**"]}`, bar adds `mustFailOnStub`), `developer` (above), `integrator` (`agentless: true`, the
ascent executor), `planner` (producer, `tool: mock-planner`), `architect` (`tool: llm-architect`, modes `decompose | rearchitect | revise-contract | design`),
`manager` (rules: the strategies), `controller` (section 7). New roles for presets: `spiker` (writes `spike/**`, bar `runs-only`), `designer` (writes `design/**`),
`reporter` (capacity `idle`), `qa`, `debugger`. `WorkItem::persona` stays the field name (plan-2 compatibility) and is validated against this registry.

### 4.3 Check (`check-1`) and bar (`bar-1`)

As in the draft, with one correction: a bar runner's exit JSON is the engine's input, so it must carry what `viewOf` and the predicates read:
`{"code": <signal>, "checks": [{"check", "level", "passed", "diagnostic"}], "draft": <bool>}` where `draft` is true when the first gate (syntax) passed.
The first bar `web-gate` is one check whose `argv` is today's `harness-gate`, so step 2 of the migration re-expresses nothing. `GateSpec::bar` is optional; absent
means the harness's default bar, which for `web-static` is `web-gate`.

### 4.4 Stage catalogue (`stage-1`) and process (`process-1`)

A stage file is a stage object with a name, so a process may reference it (`"stages": ["spike@1", {...inline...}]`) and the controller may insert it by name:

```json
{ "schema": "stage-1", "name": "spike", "version": 1, "kind": "leaf",
  "entry": "always", "exit": "items[stage=self].all(accepted) || stage.self.timeboxed",
  "timebox": 2700, "onFail": "continue",
  "defaults": { "role": "spiker", "bar": "runs-only", "ladder": "none", "exhaustion": { "restore": false, "strategies": [], "defer": false },
                "capacity": "normal", "depsReady": "accepted" },
  "items": { "templates": [ { "id": "proto", "files": ["spike/**"], "complexity": 2,
               "card": { "context": "${task.text}", "contract": "Throwaway prototype. Deliver spike/findings.md: what works, interfaces discovered, risks, rough size per part.",
                         "acceptance": ["the prototype runs", "spike/findings.md names interfaces, risks and sizes"] } } ] } }
```

```json
{ "schema": "process-1", "name": "<name>", "version": 1, "description": "...",
  "autonomy": "ask-intent",
  "answers": { "escalation": "manager,owner", "proposal": "architect", "debug": "debugger", "requirement": "owner", "clarification": "owner", "permission": "owner" },
  "budgets": { "maxItems": 32, "maxDepth": 2, "maxStageVisits": 3, "maxContractRevisions": 3 },
  "controller": { "role": "controller", "engine": "rules", "at": ["stageExit", "ladderSpent"], "maxAmendments": 4 },
  "stages": [ "<stage ref>" | <stage object>, ... ] }
```

Templates allow `${task.name}`, `${task.text}`, `${stage.visit}`, `${item.id}` substitution and `@path` includes; no expressions. `items: {"source": "plan"}` takes the
plan's items; `items: {"producer": {...}}` runs a job whose output is a plan fragment (validated by `Plan::fromJson`, completed by `completeDivision`, checked by
the division validator) that `Plan::extended` appends.

## 5. The standard process as data, and the equivalence test

```json
{ "schema": "process-1", "name": "standard", "version": 1, "description": "today's behaviour: plan, build the V, integrate",
  "autonomy": "ask-intent",
  "answers": { "escalation": "manager,owner", "requirement": "owner", "clarification": "owner", "permission": "owner" },
  "budgets": { "maxItems": 32, "maxDepth": 2 },
  "stages": [
    { "name": "plan", "kind": "descend", "entry": "always", "exit": "plan.present", "onFail": "failWorkflow",
      "items": { "producer": { "role": "planner", "tool": "mock-planner", "tier": "stub", "wallSeconds": 60, "retries": 1, "accept": "loadPlan" } } },
    { "name": "build", "kind": "leaf", "entry": "stage.plan.done", "exit": "items[stage=self].all(accepted) && !requests.openBlocking",
      "items": { "source": "plan" }, "onFail": "stopBranch",
      "defaults": { "ladder": "rules-1", "exhaustion": { "restore": true, "strategies": "workflow", "defer": "workflow" }, "depsReady": "accepted", "bar": "web-gate" } },
    { "name": "integrate", "kind": "ascend", "entry": "stage.build.done", "exit": "job(integrator).succeeded", "onFail": "failWorkflow",
      "items": { "rootAscent": { "tool": "harness", "tier": "stub", "wallSeconds": 300 } } } ] }
```

`"strategies": "workflow"` and `"defer": "workflow"` mean "resolve from the workflow's switches" (`strategyOrder(wf)`, `wf.spec.ask`), which is what keeps the
standard process identical for every existing scenario including the strategies and governance ones. `onFail: stopBranch` is rule 13 in its global form (2.2).

**Equivalence test** (`tests/test_process_equivalence.cpp`):

1. **Golden action logs, recorded first.** Before the engine changes, every scenario in `test_workflow.cpp` is driven through `Sim` with a `trace` that appends
   each pass's actions as JSON (`WorkflowAction::toJson()`) and each `itemStatuses()` to `tests/data/actions/<scenario>.ndjson`. Three scenarios are added to cover
   the blind spots in 1.5 (a near pass, a repeated failure, an oversize verdict) using `Sim::finishJob(id, state, exitJson)`. The goldens are the oracle for every
   later step, including after the legacy path is deleted.
2. **Same snapshot, both engines.** While both exist, `Sim::stepBoth()` calls `advance()` and `advanceProcess(standard)` and asserts equal serialised lists in order,
   then drives the scenario to quiescence (as `advanceWorkflows` does) and asserts equal journals.
3. **Recorded journals.** `tests/data/journals/*.ndjson` (run-v5 `w-000001` and the archived mock runs, sandbox paths rewritten) are applied record by record;
   after each record both engines are called with `now = rec.ts` and compared. This covers orderings the scenarios never produced and the real gate exit codes.
4. **Shadow mode.** `--process-engine shadow` runs both in the daemon, applies the legacy actions and logs divergences; `test_e2e` runs one mock workflow in
   shadow and asserts zero. One real 9B run in shadow mode is the last gate before the legacy path is removed.

What the test still cannot prove: that a process **other than standard** behaves as its author intends. Those are scenario tests (section 10, step 7 onward).

## 6. Presets

### 6.1 Spike-first (`spike-first@1`)

```json
{ "schema": "process-1", "name": "spike-first", "version": 1,
  "description": "spike every job first, then design, plan, build as drafts integrated in rounds, productionise",
  "autonomy": "ask-intent",
  "answers": { "escalation": "manager,owner", "proposal": "architect", "debug": "debugger", "requirement": "owner", "clarification": "owner", "permission": "owner" },
  "controller": { "role": "controller", "engine": "rules", "at": ["stageExit", "ladderSpent"], "maxAmendments": 4 },
  "budgets": { "maxItems": 48, "maxDepth": 2, "maxStageVisits": 3, "maxContractRevisions": 3 },
  "stages": [
    "spike@1",
    { "name": "design", "kind": "descend", "entry": "stage.spike.done", "exit": "items[stage=self].all(accepted)",
      "defaults": { "role": "designer", "bar": "division-validator", "ladder": "rules-1", "allowDivide": false, "allowEscalate": false, "capacity": "reserved" },
      "revisit": { "max": 2, "on": "contracts.revisedSince(stage.design)" },
      "items": { "templates": [ { "id": "contracts", "files": ["design/**"], "complexity": 3,
                   "card": { "context": "${task.text}", "include": ["@spike/findings.md"],
                             "contract": "Write design/contracts.md: modules, exact signatures, files per part, which parts are independent.",
                             "acceptance": ["every interface named in findings.md is decided", "parts are disjoint by file"] } } ] } },
    { "name": "plan", "kind": "descend", "entry": "stage.design.done", "exit": "plan.present", "onFail": "failWorkflow",
      "items": { "producer": { "role": "architect", "mode": "design", "include": ["@design/contracts.md", "@spike/findings.md"], "retries": 1, "accept": "loadPlan", "bar": "plan-lint" } } },
    { "name": "build", "kind": "leaf", "entry": "stage.plan.done", "exit": "items[stage=self].all(accepted) && !requests.openBlocking",
      "items": { "source": "plan" }, "onFail": "stopBranch",
      "defaults": { "ladder": "rules-1", "exhaustion": { "restore": true, "strategies": "workflow", "defer": "workflow" }, "bar": "web-leaf",
                    "drafts": true, "depsReady": "drafted", "roundWhen": "every(2)" } },
    { "name": "integrate", "kind": "ascend", "entry": "stage.build.done", "exit": "job(integrator).succeeded", "onFail": "failWorkflow",
      "items": { "rootAscent": { "tool": "harness", "tier": "stub", "wallSeconds": 300 } } },
    { "name": "productionise", "kind": "leaf", "entry": "stage.integrate.done", "exit": "items[stage=self].all(accepted)",
      "defaults": { "role": "developer", "bar": "web-production", "ladder": "rules-1", "allowDivide": false, "allowEscalate": false },
      "items": { "templates": [ { "id": "harden", "files": ["src/**"], "card": { "context": "All parts are accepted. Make the result production-grade.", "include": ["@qa.txt", "@report.md"],
                                   "acceptance": ["the full suite passes", "no QA finding above 'low' remains", "lint clean"] } },
                                 { "id": "report", "role": "reporter", "files": ["report.md", "deliverables.json"], "deps": ["productionise.harden"], "capacity": "idle", "gate": { "bar": "manifest" } } ] } } ] }
```

Notes: `spike@1` is the catalogue stage of 4.4 (timeboxed, `onFail: continue`: a spike that did not run still exits, and design proceeds with whatever
`findings.md` says; `spike/**` is excluded by the integrator's allowlist so nothing from it ships). `depsReady: drafted` and `roundWhen: every(2)` are the
parallelism levers of 2.4. New bars: `runs-only`, `division-validator`, `plan-lint`, `web-leaf`, `web-production` (syntax, tests, lint gate, QA gate at severity
high, oracle gate when configured), `manifest`. `budgets.maxItems: 48` is why `Plan::validate` takes the limit as a parameter.

### 6.2 Sketches

| Process | Stages (entry → exit) | What differs | Autonomy |
|---|---|---|---|
| `hotfix` | `reproduce` (tester; exit `checks(reproduce.repro).passed("fails-on-current")`) → `fix` (developer, `allowDivide: false`, bar `web-leaf` plus the repro test) → `bar` (bar `web-production`) | no spike, no design; `allowReviseTests` on | ask-intent |
| `research` | `spike@1` (timebox 1 h) → `report` (reporter, idle, bar `manifest`) | nothing integrated; succeeds on the report | none |
| `refactor` | `characterise` (tester; bar `tests-pass-on-current`) → `change` (developer, bar `web-leaf` plus the characterisation tests, `allowDivide: true`) → `compare` (bar `web-production` plus `perf` against the pre-change baseline) | QA findings of the current tree are the `change` card's acceptance list | ask-intent |
| `review` | `qa` only (role qa, idle, bar `manifest`; findings as non-blocking `proposal` requests) | `writes` empty | none |

## 7. The process controller

| Aspect | Design |
|---|---|
| What it is | a role (`controller@1`): `tool: rule-controller` by default; `llm-controller` only when the process says `engine: "model"` **and** the owner passed `--controller model`; route key `controller` so it can sit on the strongest local engine |
| When | at the moments the process lists (`stageExit`, `ladderSpent`, `request`); started by `advance()` as an ordinary job (`step: controller`, capacity `reserved`, wall 20 min); at most one in flight per workflow; a stage exit that lists the controller waits for its job to be terminal (derived: the job for `(stage, visit)` exists and is terminal), and a timed-out or failed job equals `noop` |
| What it sees | the snapshot JSON (section 3), the current process, the plan, the ledger tail, the knowledge register when built, and the **catalogue**: the names of every stage, bar and ladder in the registry |
| What it may output | one file `items/_process/amendment-<n>.json` (`process-amendment-1`); nothing else it writes is read |
| Powers (closed set) | `insertStage` (**a catalogue reference** `stage@v`, before or after a named stage; never an inline stage object), `revisitStage` (name, reason), `setBar` (stage or item, a catalogue bar), `setLadder` (stage, catalogue policy name, the four `allow*` flags), `ask` (an intent-kind request the autonomy allows), `noop` (with a reason, recorded) |
| Cannot | remove a stage, void accepted work, raise `autonomy`, raise any budget, touch `FrontierMode` or `fallbackEngine`, add roles, checks or stages to the registry, exceed `maxAmendments` |

Validation applies the process validator to the **result**: the daemon applies the amendment to a copy of the current process, runs `Process::validate` (the same
path as a file from disk), and only then journals the decision `processAmend {op, by, reason, evidence}` with the new process text (`workflowProcessChosen` with
`by: controller`). A failed validation journals `processAmend {decision: rejected, reason}` and the transition proceeds as the process wrote it.

Degrading safely on a weak 9B: the controller is advisory to the schedule (a wall-time overrun is a `noop`); output that does not parse or names an op outside
the set is a rejection; **three rejections disable the controller for the workflow** (`processAmend {decision: disabled}`); the rule controller (default) has three
rules, each a predicate plus a fixed catalogue amendment (a spike whose `findings.md` lists more than N unknown interfaces inserts `spike@1` again; a `build`
ladder spent on two items of the same parent revisits `design`; `requests.open(proposal) > 0` for longer than an hour asks a `clarification`); the model
controller receives the rule controller's suggestion as its default and must give a reason to differ; the ledger counts accepted/rejected per engine.

## 8. Contract amendments: up-transfer and versioned contracts

`WorkItem::contractVersion` (plan-3, default 1) and `attempt-N/built-against.json` (`{item, contractVersion}`), written by the agent at the start of every attempt.

| Step | Who | Mechanism |
|---|---|---|
| Propose | a leaf (reply block `PROPOSE contract <parent>: <change>`, parsed like `NEED`, allowed only if the role's `requestable` has `proposal`), the coordinator's integration gate (a failure the attribution cannot pin on one part), the QA role, or the controller | a `proposal` request, blocking for the proposer only; body: the change as text plus the signatures it touches |
| Decide | `answers.proposal` (the architect: `llm-architect --mode revise-contract`, capacity `reserved`) for the parent of the proposer; the design stage's item for the root; the owner only under `ask-all` or when the architect answers `requirement` (intent changed) | the job writes `items/<parent>/contract-v<n+1>.json` `{contract, affected: [ids], rationale}`; the daemon validates (files still covered, parts disjoint, the division validator) and journals the decision `contractRevise {item, version}` with the text |
| Mark stale | the daemon, from `affected` plus every item whose `card.include` names an affected item's files | decision `staleSince {item, contractVersion}` per item: acceptance voided (as a dispute), outputs copied to `items/<id>/drafts/v<old>/`; the next attempt's card carries the new contract, the old draft and the diff |
| Keep drafts | always | the ledger records `rework.keptDraft`; nothing is deleted |
| Revisit design | the process | `revisit.on: contracts.revisedSince(stage.design)` |
| Budget | per parent | `budgets.maxContractRevisions` (default 3); the next proposal becomes a `rearchitect` of the parent through the ladder |

Bar (BUILDING_BLOCKS): share of revisions originating from a leaf, parts kept as drafts vs restarted, rounds to integrate; computed from the decisions above by
`sub0squad report`.

## 9. The first implementable slice (behaviour-identical, 14 tests green, one test added)

Goal: make the process a **value** the engine reads, record the oracle for everything that follows, and change no journal record of any existing scenario.

**Files**

| File | Content |
|---|---|
| `include/sub0squad/process.hpp` (layer 2; `LAYER2` in `cmake/CheckLayers.cmake` gains `process`) | `struct Exhaustion { bool restore = true; std::optional<std::vector<std::string>> strategies; std::optional<bool> defer; }` (nullopt = resolve from the workflow); `struct LeafParams { std::string ladder = "rules-1"; bool allowDivide = true, allowEscalate = true, allowReviseTests = true, allowRearchitect = true; Exhaustion exhaustion; std::string depsReady = "accepted"; bool drafts = false; std::int64_t agentWallLlm = 10800, agentWallOther = 120, gateWall = 120, architectWallLlm = 5400, architectWallOther = 120; }`; `struct AscentParams { int maxRoundTrips = 3; std::string roundWhen = "all"; bool resetOnRevisit = false; std::int64_t gateWall = 120; }`; `struct ProducerParams { std::string role = "planner", tool = "mock-planner", tier = "stub", accept = "loadPlan"; std::int64_t wallSeconds = 60; int retries = 1; }`; `struct RootAscentParams { std::string tier = "stub"; std::int64_t wallSeconds = 300; }`; `enum class StageKind { descend, leaf, ascend, none }`; `struct Stage { std::string name; StageKind kind; std::string entry, exit; std::string onFail; ... }`; `struct Process { std::string name; int version; std::string autonomy; std::map<std::string, std::string> answers; Budgets budgets; std::vector<Stage> stages; [[nodiscard]] std::string toJson() const; [[nodiscard]] static std::expected<Process, std::string> fromJson(std::string_view); [[nodiscard]] std::expected<void, std::string> validate() const; }`; `[[nodiscard]] const Process& standardProcess();` All Doxygen-commented, `#pragma once`, camelCase. |
| `src/process.cpp` | `standardProcess()` as a function-local static built in code with exactly the values of section 2.1; `toJson`/`fromJson` (`process-1`, simdjson, tolerant like `Plan`); `validate` (names, kinds, `answers` owner rule, budgets in range, **entry/exit stored as strings and not parsed yet**: the predicate parser is slice 2) |
| `registry/processes/standard@1.json` | the JSON of section 5 |
| `include/sub0squad/workflow.hpp` | `WorkflowAction::toJson()`; `advanceProcess(const WorkflowRecord&, const JobStore&, const WorkflowSettings&, const IDecisionPolicy&, const Process&)`; `advance()` keeps its signature and becomes `return advanceProcess(wf, jobs, settings, policy, standardProcess());` |
| `src/workflow.cpp` | the body of `advance()` moves into `advanceProcess()` unchanged except that `kMaxRoundTrips`, the planner's tool/tier/wall/retries, the integrator's tier/wall, the agent/gate/architect walls and the `strategies`/`defer` resolution read from `process.stages[...]` (looked up once by `StageKind`: the one `descend` producer, the one `leaf` stage, the one `ascend` root). No stage loop, no predicates, no new actions, no new journal records |
| `tests/test_process.cpp` (`sub0squad_add_test(test_process)`) | (a) `standardProcess().validate()` holds; (b) `Process::fromJson(standardProcess().toJson())` equals `standardProcess()` field by field; (c) `registry/processes/standard@1.json` parses to a `Process` equal to `standardProcess()` (the file-equals-code test); (d) every `answers` entry that names `owner` is an intent kind, and a process mapping `escalation` to `owner` alone is rejected; (e) `WorkflowAction::toJson()` round-trips through `JobSpec::fromJson` for `startJob` and names every field of the other kinds |
| `tests/test_workflow.cpp` | `Sim` gains `trace` (a vector of JSON lines, one per pass: the actions and `itemStatuses()`) and `finishJob(id, state, exitJson = "")`; three scenarios added (near pass → retry; repeated failure with a tester dep → reviseTests; oversize → decompose), driven by `exitJson` `{"code":3}`, `{"code":5}`, `{"code":4}`; a flag `SUB0SQUAD_WRITE_GOLDENS=1` writes `tests/data/actions/<scenario>.ndjson`, otherwise each scenario's trace is compared with its golden. The goldens are generated once by the implementer from the legacy engine **before** `advanceProcess` exists, committed, and never regenerated without a stated reason |

**What proves it**: the 14 existing tests unchanged and green (`test_workflow` in particular: every scenario, the idempotence check, the strategies and governance
loops), `test_process` green, and the goldens equal under the refactored engine (they were recorded under the unrefactored one). `layering` stays green because
`process.hpp` includes nothing above layer 2. No daemon change, no CLI change, no journal change; `src/daemon.cpp` is not touched (it calls `advance()`).

**What it buys**: every later step (predicates, stage derivation, `Plan::extended`, bars, drafts) is compared against goldens recorded from the engine as it is
today, which is the one thing that cannot be done after the fact.

## 10. Migration after the slice

Each step is one commit that keeps every test green and adds the test named. Layers: `registry`, `process`, `predicate` at 2; `registry_defaults`, `controller`
(rules) at 3; `llm-controller` at 4.

| # | Step | Test | Off switch |
|---|---|---|---|
| 1 | section 9 | `test_process`, goldens | none (identical) |
| 2 | `predicate.hpp`: parser, evaluator, vocabulary, `Snapshot` with `now`, `explain`; `Process::validate` parses entry/exit | `test_predicate`: a table of expressions, every vocabulary path, every load error, empty-`all()` is false, `now` only via `elapsed` | pure addition |
| 3 | stage derivation inside `advanceProcess` (2.2) with the standard process's predicates evaluated for real; `depsReady` honoured (still `accepted`) | goldens unchanged; `test_process_equivalence` parts 1-2 | `--process-engine legacy` keeps `advance()` as the slice-1 body until step 6 |
| 4 | `Registry<T>`, `role-1` with the current personas, `Plan::validate` reads `writes` from the role, `registry check|list|show`, embedded defaults | `test_registry`: load, precedence, every error line, `embedded == disk`; plan fixtures validate unchanged | none (refactor) |
| 5 | `check-1`, `bar-1`, `harness-check --bar`, `web-gate` wraps `harness-gate`, `GateSpec::bar`, exit JSON with `draft` | `test_checks`: `web-gate` exit codes equal `harness-gate` on recorded gate fixtures | bar absent = today |
| 6 | `--process-engine shadow` in the daemon, `process explain`; equivalence parts 3-4; one real 9B run in shadow; then the default flips to `data` and the legacy body is deleted | `test_e2e` shadow: zero divergences; goldens still equal | revert the commit |
| 7 | `Plan::extended`, `workflowPlanExtended`, `extendPlan` action, templates, `stage-1` catalogue, `WorkItem::stage/visit`; `Plan::fromJson(limit)` | scenario: a template stage creates items, a revisit marks them stale and keeps outputs | `--process` defaults to `standard` |
| 8 | drafts: `drafted` state from exit JSON, `depsReady: drafted`, `roundWhen: every(n)`, `staleSince` when a draft's final differs | scenario: a dependant starts on a draft, is marked stale when the draft changes, keeps its work | stage field absent = today |
| 9 | capacity classes in the lease and the shed order | `test_idle`: reserved starts ahead of normal; idle never starts with normal ready; shed order | `--capacity-classes` |
| 10 | `spike-first@1`, roles `spiker`/`designer`/`reporter`, the new bars | scenario: spike → design → plan → build → integrate → productionise with mock agents; a timebox exit; `now` advanced by the test | `--process` |
| 11 | versioned contracts, `PROPOSE`, `revise-contract`, `staleSince`, `answers` enforcement | scenario: a leaf proposes, the architect revises, two dependants go stale, drafts kept, design revisited once | `--contract-proposals` |
| 12 | rule controller, amendment validator, catalogue-only `insertStage` | scenario: each rule fires once; an invalid amendment is rejected and the process continues; three rejections disable it | `--controller off` (default) |
| 13 | `llm-controller` | replay on recorded `w-000001`: amendments accepted/rejected per seed | `--controller model` |
| 14 | `hotfix`, `research`, `refactor`, `review`; split `daemon.cpp`/`llm_agent.cpp` along the registry seams as each lands | one mock scenario per preset | — |

## 11. What is unproven, and the measurement that settles each point

| Claim | Status | Measurement that settles it |
|---|---|---|
| The standard process as data is behaviour-identical | unproven until step 6 | goldens equal through steps 1-6; equivalence parts 3-4 at zero divergences; one real 9B shadow run with zero divergences. A divergence that needs a special case for `standard` inside `advanceProcess` falsifies processes-as-data for this engine; the fallback is stages as a prefix/suffix around today's `advance()` |
| Two executors with parameter structs cover every path in `workflow.cpp` | argued line by line in 1.1 and 2.1, not yet compiled | slice 1 compiles with no path left outside the two structs; if a third executor appears during step 3, the claim was wrong and the stage model needs an executor registry |
| Stage state derived, not journaled, is enough for the portal and `explain` | unproven | `process explain` on the recorded journals names a stage and a first false atom for every record; the portal needs no extra record |
| `now` as an input keeps idempotence and replay | unproven | the idempotence test with `now` fixed; the replay test with `now = rec.ts` reproduces the timebox decisions of a scenario that was journaled with a different clock |
| The predicate vocabulary is enough for real processes | unproven; the draft's `artifacts.exists` was removed | the second and third presets (hotfix, refactor) are written with the vocabulary of 3.2 and no new atom; if either needs more than `checks(item).passed`, the next atom is `checks(item).value(name)` and not a language |
| Drafts and `depsReady: drafted` reduce wall time without adding rework | unproven | on the same tiny task, `standard` vs `spike-first` with mock agents then the 9B: wall time, slot-time busy, number of `staleSince` decisions, attempts per item; the lever stays off unless wall time falls and rework (stale attempts) stays under one per dependant |
| `roundWhen: every(2)` finds integration failures earlier | unproven | rounds to first integration failure and total rounds, same task, `all` vs `every(2)` |
| Spike-first reduces rework on this model class | unproven; the owner's hypothesis | same task, three seeds each: attempts per accepted item, contract revisions, time to first accepted leaf; if spike-first is not better on two of three, the preset defaults to `research` then `standard` |
| Up-transfer proposals are useful from a leaf | unproven | share of leaf proposals the architect accepts over the recorded runs replayed with `PROPOSE` enabled; under half means the coordinator's integration gate should be the only proposer |
| The rule controller carries most of the value; the 9B controller is fit | unproven | step 13: amendments accepted / proposed per engine over the recorded runs; under half accepted keeps the model controller off |
| Capacity classes interact safely with the memory shed and lease code | unproven | `test_idle` scenarios of step 9; a live run with `--capacity-classes` and the memory figure injected |
| `Plan::extended` and 48-item processes stay within the plan parser's limits and the card budget | unproven | spike-first on a real plan: item count, card tokens, validation time |
| Two stages active at once is sound | legal in the model, untested | a scenario with overlapping `build` and `productionise`; the first presets stay sequential until it passes |
| The cost of building the snapshot every pass | unmeasured | `Sub0Squad_Bench` gains a case: `advanceProcess` on the recorded `w-000001` snapshot; the budget is the current `advance()` cost times two |
