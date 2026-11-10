# Plan, act, iterate per work item; delegation; the work graph and its information graph

Status: DESIGN, 2026-11-10. Owner direction after the first real runs of the calculator sandbox. Nothing here is built yet except where marked.

## 1. Why (evidence from the real runs)

- The `engine` item (one file, a lexer, parser, evaluator and function table) was too big for the model's per-call budget and speed. The first
  serial run reached 64 of 67 tests, regressed on a rewrite, and the ladder then decomposed it only *after* two failures, seven items and 85k
  tokens later. The decision to divide came from failure, not from looking at the size of the task.
- Concurrency (engine-slot leases, built and pushed) cannot help while the plan is one serial item; the modular plan made four items
  parallel, and then **memory** became the limit (section 5).
- A person (or this session) wrote the better plan by hand. The squad should be able to make that call itself.

## 2. Plan, act, iterate inside every work item

Each item runs a small loop instead of one blind generation:

1. **Plan (cheap, no code).** One short call returns a plan record: the units of work it sees (functions or modules with signatures), a size
   estimate in completion tokens, the risks, and a verdict `implement | split | delegate`. The harness also computes its own size estimate from the
   card (contract length, number of tests, files) and the engine's measured decode speed, so a verdict is checked against data, not trusted.
2. **Act.** `implement` runs the existing generate and repair turns. `split` and `delegate` do not generate the whole item: they create
   children (section 3) and the item becomes a *coordinator* that waits for them.
3. **Iterate.** After a failed attempt the loop measures again: size of the best code so far, failing fraction, tokens spent. The verdict
   can change (a near pass repairs; a stuck, large item splits; a stuck, small item goes to the tester dispute path). This replaces the fixed
   ladder order with a decision on measured state; the ladder stays as the guard-rail ceiling (attempt budget).

Rule of thumb for the size check: if the estimated completion exceeds a fraction of the card's token budget, or exceeds N minutes at the
engine's measured speed with a safety margin, the verdict must be `split`. The plan record, the estimate and the observed cost are written to
the ledger so the estimate can be calibrated.

## 3. Strong delegation rights

A worker may do more than answer. With a reply in a structured plan format it can:

- **write a skeleton**: the module with exported signatures, types and `TODO` bodies that throw, accepted by the harness as a real artifact;
- **delegate a function**: create a child item for one signature (`name`, `signature`, `contract`, `tests it must pass`), with the skeleton as the
  child's included file; the child can only touch its own declared file or region;
- **rearchitect and respond back**: a child (or the integrator) can return a *proposal* against the parent's contract ("this signature cannot
  be met; change it to X"); the parent re-plans, which counts against the attempt budget and is recorded, like the test dispute path.
- **sibling coordination** only through the graph (accepted artifacts and interfaces), never by editing each other's files.

Limits stay in the daemon: depth, total items, file allowlists per child, cards under the token budget, and the owner's quotas. Delegation is
data validated by the daemon, never commands (the same rule as plans today).

## 4. Two graphs

1. **The work graph (the process).** The DAG of items with parent and child edges (an item is a coordinator over children), dependency edges,
   owners (personas and tiers), states and decisions. Today: `plan-2` plus `amended` fragments (depth 2), staged pipelines.
2. **The information graph (the product and its knowledge).** Nodes are *artifacts and facts attached to work*: the contracts and signatures,
   accepted files, tests and their names, gate results, plan records and estimates, disputes, decisions with their inputs, metrics, and
   rationale text. Edges say *what was derived from what* (this function satisfies that signature; this failure disputed that test; this card
   was built from those artifacts). It is the shareable, inspectable record: the portal shows it, another squad or person can read a subgraph,
   and a worker's card is a *query* over it (the included files and notes are today a hand-built slice of it).

Both graphs live in the journal as typed records (offline readable, level-triggered, restart safe). The information graph needs one new record
family (`artifact-1`, `edge-1`) and a content store keyed by hash; the work graph already has its records. Sharing and collaboration then mean
exporting a subgraph with its provenance, and importing one as read-only context or as a delegated sub-project.

## 5. Capacity is memory as well as slots (measured 2026-11-10)

With four concurrent engine requests the Qwen3.6 snap server grew to 25.4 GB resident on a 29.6 GiB host and the machine swapped (8 GB swap
fully used, decode 0.6 tok/s instead of 7, a 55 minute run that did little). Slot count alone is therefore the wrong cap. The engine profile
needs a *memory cost per concurrent sequence* (KV cache per token times the context the prompts reach) and the lease must be limited by
`min(slots, floor((available - weights - reserve) / kvPerSequence))`, measured or declared, and checked by the same explicit guard used for
`submit --memory-mib`. Until then use `slots=2` for this snap and keep prompts short. (Open: how much of the growth is KV cache and how much is
prompt-processing buffers; the unified 262144-token KV of the snap is not ours to size, which is the case for our own `llama-server` start.)

## 6. Build order

1. Plan record and the size verdict for one item (harness estimate plus a short model call), `split` creating children through the existing
   `amended` path; ledger fields for estimate versus actual.
2. Skeleton and single-signature delegation (child item with a declared region); validator rules.
3. Memory-aware lease: engine profile gets `kvMiBPerSeq` and `weightsMiB`; guard before a lease is granted; status shows why a job waits.
4. Information graph records and the portal view; export of a subgraph.
5. Replace the fixed ladder order with decisions on measured state (keep the budget).
