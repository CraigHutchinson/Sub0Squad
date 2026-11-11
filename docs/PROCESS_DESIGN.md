# Process design: roles, checks and processes as data; the agent-driven process controller

Status: DESIGN, 2026-11-11. Builds on DESIGN_AUDIT.md section 3 (four registries, processes as stages over the same engine), BUILDING_BLOCKS.md
(co-evolving structure and leaves, up-transfer, versioned contracts), FEEDBACK_LOOPS.md section 4 (the bar) and INTERVENTIONS.md (the ledger). Nothing here is
built except the harness registry (`harness.hpp`, `harness_registry.cpp`). Every new capability is off by default; the owner decides intent and permissions only.

## 0. Decisions in one table

| Decision | Choice | Why |
|---|---|---|
| What a process controls | **which stages exist, which items each stage creates, each item's bar, ladder and capacity, and when a stage is revisited** | that is where today's rigidity is (audit findings 1, 5, 6) |
| What stays code | the **per-item micro-engine** (attempt → gate → decision → coordinator rounds), parameterised by data (`flow`, `ladder`, `bar`, budgets) | it is block 3/4 of BUILDING_BLOCKS, already pure and unit-tested; making it data from one example is premature generality |
| Purity | `advance()` stays `(record, jobs, settings, policy, process) → actions`; stage state is **derived from journal events**, never held in memory | restart-safe, replayable, idempotent, as today |
| Predicates | a tiny total expression language over a fixed **snapshot**; unknown names fail at load, never at run time | safe for human and controller authors alike; `process explain` prints every value |
| Entry provenance | the journal carries the **full text** of every process, amendment and contract version (as `workflowPlanned` carries the plan) | replay never depends on the registry directory |
| Controller | a role whose only output is a **process amendment** validated by the same validator as a human-written process; rule-based by default, model-backed off by default, disabled for the workflow after three rejected amendments | a weak 9B degrades to "the process as written", never to chaos |
| Extension | files in a directory plus commands run as ordinary jobs; no plugin ABI | audit section 3.1; jobs inherit leases, memory limits, OOM priority and the shim |

## 1. Registries: format, loading, validation

### 1.1 Common rules

```
<repo>/registry/{roles,checks,processes}/<name>@<version>.json     in-tree defaults (embedded at build time, also loadable from disk)
<stateDir>/registry/{roles,checks,processes}/<name>@<version>.json  the owner's extensions (RuntimePaths::registryDir())
<sandboxDir>/registry/...                                           per-workflow overrides (an experiment, a replay)
```

| Rule | Detail |
|---|---|
| Identity | `name@version`; `name` is `[a-z][a-z0-9-]{1,31}`, `version` a positive integer; the file name must match the fields inside |
| Precedence | sandbox > state > built-in for the *same* `name@version`; shadowing a built-in is allowed but logged and journaled (`workflowRegistryUsed` records the sha) |
| Latest | a reference without `@version` resolves to the highest version across layers; a workflow records the resolved version |
| Embedding | CMake reads `registry/**/*.json` into `src/registry_defaults.cpp` (string literals, like `web_templates.cpp`); a test checks `embedded == on disk` |
| Validation | each file: JSON parses (simdjson, position in the error), `schema` matches the directory, fields typed and in range; then cross-references (process → roles, checks, harness; role → engine route key; check → harness) resolved after all files are read |
| Error reporting | `registry check` prints one line per problem: `<file>:<json-path>: <message>`; a bad file is skipped, the rest load; the daemon refuses to **start a workflow** that references a bad or missing entry, never refuses to start itself |
| Immutability | a loaded entry never changes under a running workflow: the workflow's copy is in its journal |
| CLI | `sub0squad registry list [roles|checks|processes]`, `registry check [DIR]`, `registry show NAME[@V]`, `process explain WF` |

Code: `include/sub0squad/registry.hpp` (layer 2: `template <class Entry> class Registry { [[nodiscard]] std::expected<const Entry*, std::string> find(std::string_view ref) const; ... }`),
`src/registry.cpp`, `src/registry_defaults.cpp` (layer 3, generated). Entry structs are plain data with `fromJson`/`toJson`/`validate`, like `Plan`.

### 1.2 Role (`role-1`)

```json
{ "schema": "role-1", "name": "developer", "version": 1,
  "description": "implements one focused piece against its contract and tests",
  "system": "You own one focused piece. Be the expert on it. Keep to the interfaces.",
  "formats": ["files", "edits"],                 // reply formats the agent may use (skills the parser accepts)
  "writes": { "allow": ["src/**", "**"], "deny": ["tests/**"] },   // plan validator: which paths this role may own
  "engineRoute": "developer",                    // key into WorkflowSpec::route; falls back to the first engine
  "tool": "llm-agent", "mockTool": "mock-agent", // the commands run as jobs; a new role may name any command
  "agentless": false,                            // true: the role has no agent step (today's integrator/coordinator)
  "defaultBar": "web-leaf",                      // a named bar (section 1.3) the harness may override
  "defaultCapacity": "normal",                   // reserved | normal | idle
  "requestable": ["debug", "tester", "architect"], // requests a worker of this role may raise (answered by other roles)
  "thinking": false, "wallSeconds": 10800 }
```

Built-in roles: `tester` (writes only `tests/**`; bar adds `mustFailOnStub`), `developer`, `integrator` (`agentless`, the V coordinator), `architect`
(`tool: llm-architect`, modes `decompose|rearchitect|revise-contract|design`), `spiker` (writes `spike/**`, formats `files`, bar `runs-only`, capacity
`normal`, no ladder), `designer` (writes `design/**`, output a contract set), `controller` (section 5), `qa`, `debugger`, `reporter` (capacity `idle`).
`WorkItem::persona` becomes a role reference; `Plan::validate` asks the role registry for `writes` instead of its two hard-coded rules (plan.cpp line 175-190).

### 1.3 Check (`check-1`) and bars

```json
{ "schema": "check-1", "name": "node-tests", "version": 1,
  "description": "node --test over the item's test files; counts, failures with expected/actual",
  "level": "gate",                               // default level: gate | advisory (a bar may override)
  "cost": "seconds",                             // ms | seconds | minutes: orders the ladder, cheapest first
  "applies": { "harness": ["web-static"] },      // empty: any harness
  "argv": ["{exe}", "harness-check", "--check", "node-tests", "--workflow-dir", "{workflowDir}", "--item", "{item}", "--attempt", "{attempt}"],
  "params": { "of": "paths", "minTests": "int", "mustFailOnStub": "bool" },   // what a bar entry may pass; typed, validated
  "exit": { "0": "pass", "1": "fail", "3": "nearPass", "5": "repeatedFailure" },   // the engine's signals, unchanged from today
  "result": "checks.ndjson",                     // where the runner writes {check, level, passed, diagnostic, evidence}
  "wallSeconds": 120, "capacity": "normal" }
```

A **bar** is an ordered list of check references with overrides, named in a registry file (`bars/` lives inside `checks/` as `bar-1`) or inline in a stage or item:

```json
{ "schema": "bar-1", "name": "web-leaf", "version": 1,
  "checks": [ { "check": "node-syntax" }, { "check": "node-tests", "of": "${item.gate.tests}", "minTests": "${item.gate.minTests}" },
              { "check": "oracle", "level": "advisory", "when": "settings.oracleDir != ''" }, { "check": "qa", "level": "advisory", "capacity": "idle" } ] }
```

The bar runner (`harness-check --bar NAME`, one job) runs gates in cost order, stops at the first failing gate, runs advisories after, writes
`items/<id>/attempt-N/checks.ndjson`, and exits with the first gate's signal. **For equivalence, the first bar `web-gate` is one check whose `argv` is
today's `harness-gate`**: the registry wraps the existing function before anything is re-expressed. `GateSpec` keeps its fields; `GateSpec::bar` (a bar
reference, optional) is added; when absent the harness's default bar applies, which for `web-static` is `web-gate`.

### 1.4 Process (`process-1`)

```json
{ "schema": "process-1", "name": "<name>", "version": 1, "description": "...",
  "autonomy": "ask-intent",                      // ask-all | ask-intent | none  (section 2.6)
  "ladder": "rules-1",                           // default IDecisionPolicy name for item flows
  "controller": { "role": "controller", "engine": "rules", "at": ["stageExit", "ladderSpent", "amendment"], "maxAmendments": 4 },   // optional
  "budgets": { "maxItems": 32, "maxDepth": 2, "maxStageVisits": 3, "wallSeconds": 0 },
  "stages": [ <stage>, ... ] }

<stage> = {
  "name": "build",
  "entry": "stage.plan.done",                    // predicate (section 2.2); the stage is entered when it holds and the stage is not active/done
  "exit":  "items[stage=build].all(accepted) && !requests.openBlocking",
  "items": { "source": "plan" } | { "templates": [ <itemTemplate>, ... ] },
  "flow": "leafV",                               // leafV | single | coordinator | none (section 2.4)
  "ladder": "rules-1", "allowDivide": true, "allowEscalate": true, "maxRoundTrips": 3,
  "bar": "web-leaf",                             // default bar of the stage's items
  "capacity": "normal",                          // default capacity class of the stage's items
  "revisit": { "max": 2, "on": "contracts.revisedSince(stage.design)", "reopen": ["design"] },   // optional
  "onFail": "stopBranch" | "failWorkflow" | "continue",
  "timeboxSeconds": 0 }

<itemTemplate> = {
  "id": "spike", "title": "Spike: ${task.name}", "role": "spiker", "files": ["spike/**"], "deps": [],
  "complexity": 2, "gate": { "bar": "runs-only" }, "capacity": "normal",
  "card": { "context": "${task.text}", "contract": "@design/contracts.md", "acceptance": ["runs", "findings.md present"], "include": ["@stage.spike.outputs"] } }
```

Templates allow only `${...}` substitution from a fixed vocabulary (`task.name`, `task.text`, `stage.<s>.visit`, `item.id`) and `@path` references to
artifacts; no expressions. A template may also name a **producer**: `"producer": { "role": "architect", "mode": "design" }`, meaning the stage's items are
whatever a plan fragment written by that job contains (validated by `Plan::fromJson`, completed by `completeDivision`, checked by the division validator).

## 2. How a process is evaluated

### 2.1 Purity and derivation

`advance(wf, jobs, settings, policy, process)` is unchanged in shape. Everything a stage needs is derived from the snapshot each call:

| Fact | Derived from |
|---|---|
| stage active / done / visit count | `workflowStageEntered` / `workflowStageExited` records in `wf.stages` (a new `std::vector<StageRecord>` on `WorkflowRecord`) |
| which items belong to a stage | `WorkItem::stage` (plan-3 field; absent = the process's first stage with `items.source: plan`) |
| item state | `viewOf()` as today (jobs, decisions, disputes) |
| contract versions | `workflowContractRevised` records (`wf.contracts`) |
| controller state | `workflowProcessAmended` / `workflowAmendmentRejected` records (`wf.amendments`) |

Algorithm per call (after the cancel check, which is unchanged):

```
1. for each stage s in order:
     if !active(s) && !done(s) && eval(s.entry):            → enterStage(s, visit 1)                  (one action; return after it: level-triggered)
     if done(s) && s.revisit && visits(s) < s.revisit.max && eval(s.revisit.on) → enterStage(s, visit+1) (reopening named items as disputes)
2. for each active stage s:
     if items not yet created for this visit               → createItems(s)  (plan source: loadPlan; templates: amendPlan with the templated fragment; producer: start the producer job, then amendPlan)
     for each item of s (dependency-ready, not blocked):   → itemStep(item, s.flow, s.ladder, s.bar, ...)   (the existing per-item code, parameterised)
     if eval(s.exit)                                        → exitStage(s, "done"); if s.controller moment "stageExit" → startJob(controller)
     if s.timeboxSeconds && elapsed(s) > timebox            → exitStage(s, "timebox")
3. if every stage done, or a stage with onFail=failWorkflow failed and nothing is active → finish
```

Idempotence holds because each action's precondition is a journal fact that the action creates; calling `advance()` twice before the daemon applies the
actions returns the same list (the existing test "idempotence" is extended to stage actions). Order of stages in the file is the tie-break order of
evaluation; two stages may be active at once (`build` and `productionise` overlapping is legal if their predicates say so).

### 2.2 The predicate language

Total, side-effect free, bounded (no loops, no recursion, no user functions). Grammar:

```
expr     := or ;  or := and ("||" and)* ;  and := not ("&&" not)* ;  not := "!" not | cmp
cmp      := term (("==" | "!=" | "<" | "<=" | ">" | ">=") term)?
term     := INT | STRING | "true" | "false" | path
path     := IDENT ("." IDENT | "[" filter "]" | "(" args ")")*        e.g. items[stage=build,role=developer].count(accepted)
filter   := IDENT "=" (IDENT | STRING) ("," filter)*
args     := (term ("," term)*)?
```

The snapshot vocabulary (validated at load: an unknown path, filter key, aggregate or argument type is a registry error):

| Path | Type | Meaning |
|---|---|---|
| `always` | bool | true |
| `plan.present` | bool | `wf.plan` loaded |
| `stage.<name>.active / .done / .visits / .failed` | bool / int | derived as in 2.1 |
| `stage.<name>.elapsed` | int | seconds since the latest entry |
| `items[<filters>].all(<state>) / .any(<state>) / .count(<state>) / .count()` | bool / int | filters: `stage`, `role`, `id`, `prefix`; states: `accepted`, `failed`, `pending`, `running`, `stale`, `draft` |
| `items[...].ladderSpent()` | bool | any item whose decision ladder and strategies are spent |
| `requests.openBlocking` / `requests.open(<kind>)` | bool / int | the inbox |
| `contracts.revisedSince(stage.<name>)` | bool | a `workflowContractRevised` record after the stage's latest entry |
| `artifacts.exists(<path>)` | bool | a file under the sandbox (the daemon passes a listing in the snapshot; the evaluator does no I/O) |
| `controller.verdict(<string>)` | bool | the latest accepted amendment carried this verdict tag |
| `settings.<key>` | string/int/bool | `oracleDir`, `frontier`, `ask`, `agent` |
| `process.amendments / .rejected` | int | counts |

`Snapshot` is a struct built once per `advance()` call from `(wf, jobs, settings)` and rendered to JSON for the controller and for `process explain`.
Evaluator: `include/sub0squad/predicate.hpp`, `[[nodiscard]] std::expected<Predicate, std::string> Predicate::parse(std::string_view)`,
`[[nodiscard]] Value Predicate::eval(const Snapshot&) const`; both pure, unit-tested on a table of expressions.

### 2.3 Item templates, bars, capacity, autonomy

- **Items created by a stage** enter the plan through the existing `amendPlan` path as a fragment whose root is a virtual parent named after the stage
  (`_stage.spike`), so `Plan::amended`, `completeDivision` and the division validator run on them. Ids are `<stage>.<template id>` on visit 1 and
  `<stage>.<template id>.v<visit>` on revisits; a reopened item keeps its id and is voided like a dispute (its outputs stay as drafts).
- **Bar** resolution: item `gate.bar` > stage `bar` > role `defaultBar` > harness default. A bar reference that resolves to nothing is a load error.
- **Capacity class** (new `JobSpec::capacityClass`, `job-spec-1` gains an optional field defaulting to `normal`): resolution item > stage > role.
  Scheduler: `reserved` jobs have one slot of the daemon's `maxJobs` and one engine lease slot that `normal`/`idle` cannot take; `idle` starts only when
  no `normal`/`reserved` job is ready and is shed first (memory shed order: idle, normal, reserved, servers). Off by default: until `--capacity-classes` is
  passed every class behaves as `normal`.
- **Autonomy** (process field, overridable by `do --autonomy`): `ask-all` raises a blocking `question` to the owner at every stage exit and before any
  amendment; `ask-intent` (default) asks only on `requirement`, `clarification` and `permission` requests, everything else is answered by roles;
  `none` never asks and fails with evidence. A frontier tier is never enabled by a process or an amendment: `FrontierMode` stays a daemon option.

### 2.4 Item flows (code, parameterised)

| Flow | Behaviour | Parameters honoured |
|---|---|---|
| `leafV` | today: attempt → gate → `policy.decide` ladder → strategies → defer or fail; coordinator rounds for divided items | `ladder`, `allowDivide`, `allowEscalate`, `maxRoundTrips`, `bar`, `maxLocalAttempts` |
| `single` | one attempt, one bar run; on failure the item is `failed` with evidence, no ladder, no division; drafts kept | `bar`, `timeboxSeconds` |
| `coordinator` | the ascent only (gate rounds, send-down) for an agentless item | `maxRoundTrips`, `bar` |
| `none` | the stage has no items (a controller-only or owner-gate stage) | — |

`leafV` with `allowDivide=false` makes `Action::decompose` map to `fail` in the ladder wrapper; `allowEscalate=false` does the same for `escalate`. Both are
applied in `advance()` on the `Decision`, so `RulePolicy` stays untouched and its tests stay valid.

### 2.5 Journal events (all new; `WorkflowStore::apply` learns each; replay shows every process change)

| Event | Body | Written when |
|---|---|---|
| `workflowProcessChosen` | `{id, process: <full JSON>, by: owner|controller|default, reason}` | at submit (default or `--process`), and on a `switchProcess` amendment |
| `workflowRegistryUsed` | `{id, kind, name, version, sha256, source}` | each entry the workflow resolves |
| `workflowStageEntered` | `{id, stage, visit, reason, predicate, values}` | `enterStage` action; `values` is the evaluated atoms, so replay can show why |
| `workflowStageExited` | `{id, stage, visit, verdict: done|timebox|failed, predicate, values}` | `exitStage` action |
| `workflowProcessAmended` | `{id, amendment: <full JSON>, by, decision}` | an amendment passed validation |
| `workflowAmendmentRejected` | `{id, amendment, reason}` | an amendment failed validation |
| `workflowContractProposed` | `{id, rid, from, against, version, change, reason}` | up-transfer (section 6) |
| `workflowContractRevised` | `{id, item, version, contract, affected: [...], by}` | the coordinator's architect or the owner accepted it |
| `workflowItemStale` | `{id, item, contractVersion, keptAs: "drafts/v<n>"}` | a dependent is marked stale |

`workflowDecision` gains two action values: `processAmend` (the controller's decision, `tier` = the amendment op) and `contractRevise`.

## 3. The standard process as data, and the equivalence test

```json
{ "schema": "process-1", "name": "standard", "version": 1, "description": "today's behaviour: plan, build the V, integrate",
  "autonomy": "ask-intent", "ladder": "rules-1",
  "budgets": { "maxItems": 32, "maxDepth": 2 },
  "stages": [
    { "name": "plan", "entry": "always", "exit": "plan.present", "flow": "none",
      "items": { "producer": { "role": "planner", "tool": "mock-planner", "retries": 1 } }, "onFail": "failWorkflow" },
    { "name": "build", "entry": "stage.plan.done", "exit": "items[stage=build].all(accepted) && !requests.openBlocking",
      "items": { "source": "plan" }, "flow": "leafV", "ladder": "rules-1", "allowDivide": true, "allowEscalate": true, "maxRoundTrips": 3,
      "bar": "web-gate", "capacity": "normal", "onFail": "stopBranch" },
    { "name": "integrate", "entry": "stage.build.done", "exit": "items[stage=integrate].all(accepted)", "flow": "single",
      "items": { "templates": [ { "id": "integrator", "role": "integrator", "tool": "harness-integrate", "files": [], "bar": "integration" } ] },
      "onFail": "failWorkflow" } ] }
```

Two details keep this identical to today: `onFail: stopBranch` encodes rule 13 (a failed item stops its branch; the workflow fails when nothing can move),
and the `integrate` stage's single item is the integrator job (today started directly by `advance()` step 3; `role: integrator` with `tool` set is a
job, not a plan item: the template is rendered as a job spec, not inserted in the plan, when `files` is empty and `tool` is set).

**Equivalence test** (`tests/test_process_equivalence.cpp`, added to `sub0squad_add_test`):

1. **Scenario replay.** `Sim` (test_workflow.cpp) gains `stepBoth()`: it calls legacy `advance()` and `advanceProcess(standard)` on the same snapshot and
   asserts the action lists are equal after dropping `enterStage`/`exitStage` actions and the `stage` field of created items. Every existing scenario
   in `test_workflow.cpp` (dependency order, gates, retry, decompose, revise, rearchitect, escalate, strategies, deferral, dispute, restore, send-down,
   cancel, idempotence, blocking requests) runs through `stepBoth()`.
2. **Recorded journals.** `tests/data/journals/*.ndjson` (run-v5 `w-000001` and the archived mock runs, with sandbox paths rewritten) are replayed record
   by record; after each record both engines are called and compared. This covers orderings the scenarios never produced.
3. **Journal text.** The daemon in `--process-engine shadow` mode runs both, applies the legacy actions, and logs any divergence; `test_e2e` runs one mock
   workflow in shadow mode and asserts zero divergences.

The legacy path is removed only when 1-3 pass and one real 9B run in shadow mode reports no divergence.

## 4. Spike-first, and the other presets

### 4.1 Spike-first (`spike-first@1`), the owner's example

```json
{ "schema": "process-1", "name": "spike-first", "version": 1,
  "description": "spike every job first, then design, plan, build as drafts integrated in rounds, productionise",
  "autonomy": "ask-intent", "ladder": "rules-1",
  "controller": { "role": "controller", "engine": "rules", "at": ["stageExit", "ladderSpent", "amendment"], "maxAmendments": 4 },
  "budgets": { "maxItems": 48, "maxDepth": 2, "maxStageVisits": 3 },
  "stages": [
    { "name": "spike", "entry": "always", "exit": "items[stage=spike].all(accepted) || stage.spike.elapsed > 2700",
      "flow": "single", "timeboxSeconds": 2700, "allowDivide": false, "allowEscalate": false, "capacity": "normal", "onFail": "continue",
      "items": { "templates": [ { "id": "proto", "role": "spiker", "files": ["spike/**"], "complexity": 2, "gate": { "bar": "runs-only" },
                   "card": { "context": "${task.text}", "contract": "Throwaway prototype. Deliver spike/findings.md: what works, interfaces discovered, risks, rough size per part.",
                             "acceptance": ["the prototype runs", "spike/findings.md exists and names interfaces, risks and sizes"] } } ] } },
    { "name": "design", "entry": "stage.spike.done", "exit": "items[stage=design].all(accepted)",
      "flow": "leafV", "allowDivide": false, "allowEscalate": false, "capacity": "reserved", "bar": "division-validator",
      "revisit": { "max": 2, "on": "contracts.revisedSince(stage.design)", "reopen": ["design.contracts"] },
      "items": { "templates": [ { "id": "contracts", "role": "designer", "files": ["design/**"], "complexity": 3,
                   "card": { "context": "${task.text}", "include": ["@spike/findings.md"], "contract": "Write design/contracts.md: modules, exact signatures, files per part, which parts are independent.",
                             "acceptance": ["every interface named in findings.md is decided", "parts are disjoint by file"] } } ] } },
    { "name": "plan", "entry": "stage.design.done", "exit": "plan.present && items[stage=plan].all(accepted)",
      "flow": "none", "capacity": "reserved", "bar": "plan-lint",
      "items": { "producer": { "role": "architect", "mode": "design", "include": ["@design/contracts.md", "@spike/findings.md"] } } },
    { "name": "build", "entry": "stage.plan.done", "exit": "items[stage=build].all(accepted) && !requests.openBlocking",
      "items": { "source": "plan" }, "flow": "leafV", "ladder": "rules-1", "allowDivide": true, "allowEscalate": true, "maxRoundTrips": 3,
      "bar": "web-leaf", "capacity": "normal", "drafts": true, "integrateEvery": 2, "onFail": "stopBranch" },
    { "name": "productionise", "entry": "stage.build.done", "exit": "items[stage=productionise].all(accepted)",
      "flow": "leafV", "allowDivide": false, "allowEscalate": false, "bar": "web-production", "capacity": "normal",
      "items": { "templates": [ { "id": "harden", "role": "developer", "files": ["src/**"], "deps": [], "gate": { "bar": "web-production" },
                   "card": { "context": "All parts are accepted. Make the result production-grade.", "include": ["@qa.txt", "@report.md"],
                             "acceptance": ["the full suite passes", "no QA finding above 'low' remains", "lint clean"] } },
                 { "id": "report", "role": "reporter", "files": ["report.md", "deliverables.json"], "deps": ["productionise.harden"], "capacity": "idle", "gate": { "bar": "manifest" } } ] } } ] }
```

Notes that make it work: `drafts: true` on `build` tells `leafV` that a `single`-style partial output (a file set that parses but fails tests) is kept as
`items/<id>/drafts/v<n>/` and handed to dependants through `card.include` as a draft marked so; `integrateEvery: 2` starts a coordinator round every time two
parts land rather than waiting for all (the round-trip budget applies per round). The `design` stage's bar is the division validator (BUILDING_BLOCKS step 3):
contracts consistent, parts disjoint, a tester per developer part. The spike's `onFail: continue` means a spike that did not run still exits the stage
(timebox) and design proceeds with whatever `findings.md` says; nothing from the spike is ever shipped (`spike/**` is excluded by the integrator's allowlist).
Bars `runs-only`, `division-validator`, `plan-lint`, `web-production` (syntax, tests, lint gate, qa gate at severity high, oracle gate when configured), `manifest`
are new bar files; `web-production` is the first place idle-time advisories become gates.

### 4.2 Sketches

| Process | Stages (entry → exit) | Flows and bars | Autonomy |
|---|---|---|---|
| `hotfix` | `reproduce` (always → a failing test exists: `artifacts.exists("tests/repro.test.mjs")`, role tester, bar `tests-fail-on-current`) → `fix` (developer, `leafV`, `allowDivide: false`, bar `web-leaf` + the repro test) → `bar` (`single`, bar `web-production`) | no spike, no design; the ladder's `reviseTests` is on (a wrong repro is the common fault) | ask-intent |
| `research` | `spike` (as above, timebox 1 h, `onFail: continue`) → `report` (reporter, capacity idle, bar `manifest`: `findings.md` and a deliverables entry) | nothing integrated; the workflow succeeds on the report | none |
| `refactor` | `characterise` (tester writes tests that pass on the current tree: bar `tests-pass-on-current`) → `change` (developer, `leafV`, bar `web-leaf` + the characterisation tests, `allowDivide: true`) → `compare` (`single`, bar `web-production` + `perf` gate against the pre-change baseline) | QA findings of the current tree are the `change` card's acceptance list | ask-intent |
| `review` | `qa` only (`single`, role qa, capacity idle, bar `manifest`; produces findings as `proposal` requests, non-blocking) | no code changes; `writes` empty | none |

## 5. The process controller

| Aspect | Design |
|---|---|
| What it is | a **role** (`controller@1`): `tool: rule-controller` by default, `llm-controller` when the process says `engine: "model"` **and** the owner passed `--controller model` (off by default); route key `controller` so it can sit on the strongest local engine |
| When it runs | at the moments the process lists (`stageExit`, `ladderSpent`, `amendment`, `request`); started by `advance()` as an ordinary job with `role: controller`, `capacity: reserved`, wall 20 min; at most one in flight per workflow |
| What it sees | the snapshot JSON (section 2.2), the current process, the plan, the ledger tail, the knowledge register (when built), the registry's process and bar names |
| What it may output | **one** file `items/_process/amendment-<n>.json` (`process-amendment-1`); nothing else it writes is read |
| Powers (closed set) | `insertStage` (a full `<stage>` object, before or after a named stage), `revisitStage` (name, reason), `setBar` (stage or item, bar ref), `setLadder` (stage, policy name, `allowDivide`/`allowEscalate`), `switchProcess` (for a named subtree: a process ref; the subtree's accepted items are kept), `ask` (a `question`/`clarification`/`permission` request, only kinds the autonomy level allows), `noop` (with a reason, which is also recorded) |
| Cannot | remove a stage that has accepted items, void accepted work, raise `autonomy`, change `budgets` upward, touch `FrontierMode`, add roles or checks (those are files the owner writes), amend more than `maxAmendments` times |

Amendment format:

```json
{ "schema": "process-amendment-1", "workflow": "w-000001", "at": "stageExit:spike", "op": "insertStage",
  "after": "spike", "stage": { "name": "spike-2", "entry": "stage.spike.done", "exit": "...", "flow": "single", "items": { "templates": [ ... ] } },
  "reason": "findings.md names two unknown interfaces (storage, rendering); uncertainty is high", "evidence": ["spike/findings.md:12-19"],
  "verdict": "uncertain" }
```

Validation is the process validator applied to the **result**: the daemon applies the amendment to a copy of the current process, runs `Process::validate`
(same code path as a file from disk: schema, references, predicates parse against the vocabulary, budgets not exceeded, the cannot-list above), and only
then journals `workflowProcessAmended` plus a `workflowDecision {action: processAmend, tier: <op>}` with the controller's reasons and evidence as
`inputs`. A failed validation journals `workflowAmendmentRejected` with the first problem, and the stage transition proceeds **as the process wrote it**.

Degrading safely on a weak 9B:

1. The controller is advisory to the schedule: `advance()` never waits for it beyond its wall time; a timed-out or failed controller job equals `noop`.
2. Output that does not parse, or parses to an op outside the closed set, is a rejection; **three rejections disable the controller for that workflow**
   (`workflowDecision {action: processAmend, tier: disabled}`), after which the process runs exactly as written. The ledger counts accepted/rejected per
   engine so the owner can see whether a model is fit for the role.
3. The rule-based controller (default) has three rules, each a predicate from section 2.2 plus a fixed amendment: a spike whose `findings.md` lists more
   than N unknown interfaces inserts a second spike; a `build` ladder spent on two items of the same parent revisits `design` with the evidence; a
   stage with `requests.open(proposal) > 0` for longer than an hour asks the owner a `clarification` (ask-intent allows it).
4. The model-backed controller receives the rule controller's suggestion as its default and must give a reason to differ; its reply format is a single
   JSON object (the tolerant plan parser is reused), and a `noop` costs nothing.
5. Autonomy caps it: under `ask-all` every amendment becomes a `question` to the owner before it takes effect; under `none` an `ask` op is rejected.

## 6. Contract amendments: up-transfer and versioned contracts

Contracts live in cards (`WorkCard::contract`). Versioning adds `WorkItem::contractVersion` (plan-3, default 1) and `attempt-N/built-against.json`
(`{item, contractVersion}`) written by the agent at the start of every attempt.

| Step | Who | Mechanism |
|---|---|---|
| Propose | a leaf worker (reply block `PROPOSE contract <parent>: <change>` parsed like `NEED`), the coordinator's integration gate (a failure the attribution cannot pin on one part), the QA role, or the controller | a `proposal` request, non-blocking for siblings, blocking for the proposer only; `workflowContractProposed` with the change as text plus the signatures it touches |
| Decide | the **coordinator's architect** (`llm-architect --mode revise-contract`, capacity `reserved`) when the parent is a coordinator; the root's architect for top-level items; the owner only under `ask-all` or when the change alters intent (the architect answers `requirement` to say so) | the architect job writes `items/<parent>/contract-v<n+1>.json` `{contract, affected: [ids], rationale}`; the daemon validates (the parent's files still covered, parts still disjoint, the division validator) and journals `workflowContractRevised` |
| Mark stale | the daemon, from `affected` plus every item whose `card.include` names an affected item's files | `workflowItemStale` per item: acceptance voided (as a dispute), outputs copied to `items/<id>/drafts/v<old>/`, the next attempt's card carries the new contract, the old draft and the diff of the contract as feedback |
| Keep drafts | always | the draft is the next attempt's starting point ("keep what is right, fix the rest"), never deleted; the ledger records `rework.keptDraft` |
| Revisit design | the process | `revisit.on: contracts.revisedSince(stage.design)` reopens the design item with the revision as feedback, bounded by `revisit.max` |
| Budget | per parent | `maxContractRevisions` (default 3, a process budget); the fourth proposal escalates to the ladder's `rearchitect` on the parent |

Bar (BUILDING_BLOCKS): share of revisions that originate from a leaf, parts kept as drafts versus restarted, rounds to integrate; all three are
computed from the events above by `sub0squad report`.

## 7. Migration plan, risks, and what would change my mind

Each step is a separate commit that keeps the 13 tests green and adds the test named. Layers: `registry`, `process`, `predicate` at layer 2;
`registry_defaults`, `controller` (rules) at layer 3; `llm-controller` at layer 4.

| # | Step | Test that proves it | Off switch |
|---|---|---|---|
| 1 | `Registry<T>` loader, `registry check|list|show`, embedded defaults, `role-1` with the three current personas; `Plan::validate` reads `writes` from the role | `test_registry`: load, precedence, every error line; `embedded == disk`; plan validation unchanged on the existing fixtures | none needed (pure refactor) |
| 2 | `check-1`, `bar-1`, `harness-check --bar`; `web-gate` wraps `harness-gate`; `GateSpec::bar` optional | `test_checks`: the result shape; `web-gate` exit codes equal `harness-gate` on the recorded gate fixtures | bar absent = today |
| 3 | `predicate.hpp`: parser, evaluator, vocabulary, `Snapshot` builder | `test_predicate`: a table of expressions, every vocabulary path, every error | pure addition |
| 4 | `process-1` structs, validator, `standard@1` file; `advanceProcess()` beside `advance()`; stage events in `WorkflowStore` | `test_process_equivalence` parts 1 and 2 | `--process-engine legacy` (default) |
| 5 | daemon `--process-engine shadow`; `process explain` | `test_e2e` shadow run: zero divergences; one real 9B run in shadow | shadow is opt-in |
| 6 | flip the default to `data`; delete the legacy path in `advance()` after one more green shadow run | the whole suite; equivalence test now compares against recorded legacy action logs | revert the commit |
| 7 | capacity classes in the lease and the shed order | `test_idle`: a reserved job starts ahead of normal; idle never starts with a normal job ready; shed order | `--capacity-classes` |
| 8 | `spike-first@1`, roles `spiker`/`designer`/`reporter`, bars `runs-only`/`division-validator`/`web-production`; drafts and `integrateEvery` in `leafV` | `test_workflow` scenario: spike → design → plan → build → productionise with mock agents; a draft handed on; a timebox exit | `--process` defaults to `standard` |
| 9 | versioned contracts: `contractVersion`, `PROPOSE`, `revise-contract` mode, stale marking, drafts kept | scenario: a leaf proposes, the architect revises, two dependants go stale, their drafts are kept, design is revisited once | `--contract-proposals` |
| 10 | rule controller with its three rules; amendment validator; the journal and decision records | scenario: each rule fires once; an invalid amendment is rejected and the process continues; three rejections disable it | `--controller off` (default) |
| 11 | `llm-controller` | replay on the recorded `w-000001`: amendments accepted/rejected per seed, reported | `--controller model` |
| 12 | `hotfix`, `research`, `refactor`, `review` files; split `daemon.cpp`/`llm_agent.cpp` along the registry seams as each lands | one mock scenario per preset | — |

**Risks and honest unknowns**

- *The equivalence hypothesis is unproven.* Three things in today's `advance()` are hard to express as stage data and are therefore kept in code (section 2.4):
  the strategies order, the deferral request, and the restore-after-dispute rule. If the equivalence test shows a divergence that needs a fourth code
  path, the stage model is too thin and `leafV` needs a sub-flow table rather than booleans; that would be found at step 4, before anything is removed.
- *Predicates may be too weak for real processes.* The vocabulary has no arithmetic over artifacts (e.g. "findings.md names more than two unknowns"); the
  rule controller reads files for that. If the second preset needs more than two such rules, the vocabulary should gain `artifacts.count(path, pattern)`
  rather than growing into a language.
- *Templates created through `amendPlan` carry the plan's limits* (32 items, depth 2, 1500-token cards). Spike-first with productionise and report items may
  cross 32 on a real project; `budgets.maxItems` is per process for that reason, and `Plan::fromJson` must take the limit as a parameter (today a constant).
- *Two active stages at once* is legal in the model and untested in practice; the first presets keep stages sequential.
- *The controller on a 9B* is the weakest link; the design assumes the rule controller carries most of the value. Measured at step 11: if fewer than half of
  the model's amendments pass validation over the recorded runs, the model controller stays off and the effort goes to rules.
- *Not verified*: `--idle` scheduling interactions with the existing memory shed and lease code (step 7 is where it is tested); the cost of reading the
  sandbox listing for `artifacts.exists` on every `advance()` (cache per pass; measure).

**What would change my mind**

- If the equivalence test cannot be made to pass without special-casing the standard process inside `advanceProcess()`, processes-as-data is the wrong
  abstraction for this engine and the alternative is a smaller change: stages as a prefix/suffix around today's `advance()` (spike and design before, productionise
  after), with the middle untouched.
- If spike-first on a tiny task (step 8, mock agents, then one 9B run) does not reduce rework compared with `standard` on the same task, the spike stage is a
  cost, not an investment, for this model class, and the preset should default to `research` plus `standard` instead.
- If up-transfer proposals are mostly rejected by the architect (step 9 measured), the leaf is not the right proposer and the coordinator's integration gate
  should be the only source of contract revisions.
