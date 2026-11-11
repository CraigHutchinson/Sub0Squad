# Holistic design audit (2026-11-11)

Scope: the whole squad as it is in the tree (about 11,000 lines, `src/` and `include/`), judged against the owner's direction: **flexible and organic workflow, with preset
working modes and processes, extensible by extension; today's workflow is the default process, but a process could be chosen and steered by an agent from the use case,
progress and decisions** (example given: spike every job first, then design, plan, productionise). Evidence is from the code and from run 1 on the 9B; claims about code are
checked against the files named, not inferred.

## 1. What is sound and should be kept

| Strength | Where | Why it matters for extension |
|---|---|---|
| **Journal-1 NDJSON as the single state authority**; status works without the daemon | `journal`, `job_store`, `daemon` | any process can be rebuilt from events; a new process type adds events, not a new store |
| **Level-triggered, pure `advance()`** over a snapshot: state in, actions out | `workflow.cpp` | processes can be tested offline and replayed; a new stage is a new rule over the same snapshot |
| **Decision seam as data**: `DecisionContext` → `Decision` with reasons and inputs | `policy.hpp/.cpp` | the ladder is a swappable policy, already audited and replayable |
| **Everything is a job**: process jobs with specs, leases, shim, resumability | `job_spec`, `shim`, `daemon` | an extension can be *any command* speaking a job spec; no plugin ABI is needed |
| **Plan as data (plan-2)** with items, personas, cards, gates, deps, coordinators | `plan.cpp` | a process can emit plans; plans can be amended (`Plan::amended`) |
| **Engine profiles per persona, routing, strategies** | `WorkflowSpec::route`, strategies | model choice is already a dial |
| **Replay/isolation, recall, QA, trace** | `replay.cpp`, `recall.cpp`, `qa.cpp`, `trace.cpp` | the observation tools needed to develop new processes safely exist |

## 2. Where the design is rigid (findings, with evidence)

1. **The process is hard-wired into `advance()`.** Order is fixed: plan → (tester → developer) per item → gate → ladder → coordinator rounds → integrator. There is no
   notion of a *stage* or *phase* that is data. A spike-first process cannot be expressed; it would be another branch in `workflow.cpp` (602 lines already carrying the V,
   strategies, requests and governance).
2. **The harness is selected by string comparison.** `"web-static"` appears in 8 source files (`llm_agent` 5×, `triage` 4×, `plan` 3×, `workflow` 3×, `architect`, `sandbox`,
   `daemon`, `harness_web`, `web_templates`). A second harness (C++, document, data analysis) means touching every site. A harness should be a **registry entry** supplying: scaffold,
   stubs, brief, facts, gate/check ladder, integrator, allowlist rules, tolerant-format hints.
3. **Personas are a closed set** (`tester`, `developer`, `integrator`; 16 `persona ==` sites). A spiker, a designer, a debugger, a QA head, a reporter would each be a new `if`.
   A persona should be data: system sentence, allowed outputs/format skills, default engine, default bar, tools it may request.
4. **Gate kinds and checks are hard-coded** (`node-test`, the harness gate function). The bar model (FEEDBACK_LOOPS.md section 4) is the replacement: checks as registered units
   composed per node.
5. **The ladder is one fixed escalation shape** (retry → decompose → reviseTests → rearchitect → frontier-stub → strategies). Modes want different ladders (a spike stage should *not*
   escalate or divide; production should insist on a bar before accepting).
6. **Planning is a single front step** (a plan supplied or an architect job), not a continuing activity. The owner's idea of structure and leaves co-evolving, and of agent-driven
   process, needs planning to be revisitable at any time from any node.
7. **Large files carry unrelated concerns** (`daemon.cpp` 1,060: lease, shed, requests, strategies, briefings; `client.cpp` 840; `llm_agent.cpp` 646: card, drafts, fork, plan phase).
   These are the places extension would otherwise be bolted on. Splitting by block (BUILDING_BLOCKS.md) before extending is cheaper than after.
8. **Self-description is partial**: `webHarnessFacts` exists for one harness; process/persona/policy facts for the briefing and the portal are not data. Anything new must describe itself or
   the expert/briefing and the owner cannot reason about it.
9. **Capacity is a first-class concern only at the engine** (slots, affinity, shed). Background roles (QA, dreamer, debugger) and speculative work need a *capacity class* in the
   scheduler (guaranteed, best-effort-when-idle, reserved-for-structure); today leases are one flat pool.
10. **Governance is built, but modes of working with the owner are not** (autonomy level per mode: ask on everything vs ask only about intent).

## 3. The extension architecture proposed

Principle: **extension by data first, by process second, by code last**, and the default process is itself expressed in the same form, so the default is just one preset.

### 3.1 Four registries (each entry self-describing)

| Registry | An entry is | Replaces |
|---|---|---|
| **Harness** | scaffold, stubs, brief and facts, check ladder, integrator, allowlist, format hints | the `"web-static"` string tests |
| **Role** (persona) | system sentence, accepted reply formats (skills), default engine route, default bar, requestable tools | the closed persona set |
| **Check** (bar unit) | name, cost, level (gate/advisory), runner command, diagnostic parser | the hard-coded gate function |
| **Process** | stages, entry/exit criteria, item templates per stage, ladder policy, autonomy level, capacity classes | the fixed order in `advance()` |

An entry is a JSON file (versioned) plus, optionally, **commands** that implement it (a check or a harness step is a command that speaks a job spec; the daemon already runs those). So
extension needs no plugin ABI or dynamic loading: it is files in a directory the daemon reads, and jobs. In-tree entries (the web harness, the standard process) use the same format, and
a test checks that the standard process expressed as data produces the same plan/run as today.

### 3.2 A process is stages over the same engine

```
process:  name, description, autonomy (ask-all | ask-intent | none), ladder (policy name)
stage:    name, entry (predicate over the snapshot), produces (item templates), bar (checks), exit (predicate), may-revisit (bool), capacity-class
```

**Standard** (today): `plan → build(V) → integrate`. **Spike-first** (the owner's example), for every job:

| Stage | Produces | Bar | Notes |
|---|---|---|---|
| spike | throwaway prototype and a findings note (what works, interfaces discovered, risks, rough size) | runs; findings present | time-boxed, no escalation, no division; output feeds design, never shipped |
| design | contracts and a division informed by the spike | contracts consistent (the division validator) | may revisit when a build leaf proposes an amendment (up-transfer) |
| plan | the V plan: tests first, parts, coordinators | plan-lint, validator | capacity reserved for structural jobs |
| build | parts as drafts, integrated in rounds | the full bar ladder, drafts allowed | parallel across idle slots |
| productionise | hardening: sanitizers/perf/docs/lint/QA fixes | stricter bar (FEEDBACK_LOOPS) | idle-time checks become gates here |

Other presets that fall out: **hotfix** (reproduce → fix → bar), **research** (spike only, report out), **refactor** (characterise → change → compare), **review** (QA head only).

### 3.3 Agent-driven process

A **process controller** is a role (a manager-level agent, on a stronger engine when available) that, at defined moments (stage exit, a ladder spent, an amendment, a request), may:
choose or switch the process for a node or subtree, insert a stage (a spike where uncertainty is high), revisit a stage, change the bar, or ask the owner (only about intent
and permissions). Its authority is data (the process's `autonomy`), its decisions are recorded like any other decision (`decisions` with reasons and inputs), and every change of process is a journal event so
replay shows it. Rule: **a controller proposes by writing a process-amendment through the same validator as a human-written one**; an invalid amendment never takes effect.

### 3.4 Capacity classes in the scheduler
`reserved` (structural/integration), `normal` (leaf work), `idle` (QA, dreamer, debugger, speculative spikes). Slots and memory shed already exist; this adds the class to the lease choice
(`chooseLeaseSlot`) and the shed order (idle first).

## 4. Migration, smallest safe steps first (each keeps the 13 tests green)

1. **Harness registry** (finding 2): one struct of function pointers/data for web-static; replace the string tests. Pure refactor, tests prove equivalence.
2. **Role table** (finding 3): persona → system sentence/format/engine/bar as data; the current three become entries.
3. **Check registry and the bar** (finding 4), with the web gate expressed as a bar of registered checks; `syntax`, `tests`, `oracle`, `qa` become entries.
4. **Division validator** (BUILDING_BLOCKS step 3), the first check that serves any process.
5. **Process as data**: express *Standard* as stages; make `advance()` stage-driven for new stage kinds while the old path remains until equivalence is shown; then add *Spike-first* as the second preset and run it on a tiny task.
6. **Capacity classes**, then **the process controller** with `autonomy` levels; start with `ask-intent` and a single power (insert a spike).
7. Split `daemon.cpp`/`llm_agent.cpp` along the block lines as each registry lands, not before.

## 5. Risks and what would change my mind
- **Premature generality.** One harness and one process exist; a registry designed from one example may be wrong. Mitigation: step 1 and 2 are refactors with equivalence tests; the second process (spike-first) is the real test of the abstraction, and the second harness (C++) the other.
- **Data-driven processes are harder to debug than code.** Mitigation: replay already isolates nodes; add a `process explain` view (which stage, which predicate held) in the portal.
- **An agentic controller on a 9B may be unreliable.** Start with the controller proposing and the standard validator gating, on a stronger engine for that role only; measure amendments accepted/rejected.
- Not audited here: the security surface of extension commands (they are jobs, so they inherit leases, memory limits, OOM priority and the shim; trust and signing of extension files are open).
- Not verified: the line counts and string-site counts above are from the current tree; the claim that the standard process can be expressed as stages without changing behaviour is a hypothesis step 5 must prove.
