# Context and planning: who holds the whole picture, and how agents pull what they need

Status: design and hypotheses, 2026-11-15 (owner direction). Nothing here is built except where a row says so. It is the thinking behind the experiments of [EXPERIMENTS.md](EXPERIMENTS.md) and uses the measures of `docs/METRICS.md`.

## 1. The hypothesis

Many failures of an agentic build begin before any code is written: in the **plan**. A planner or architect has the whole picture when it writes the plan and then hands each worker a narrow slice. That produces a class of failure:

* **P1 underspecified**: the card omits something the worker needs (an export name, a helper, an edge case, a format).
* **P2 specified wrongly**: the card or contract states something that is false, contradictory or infeasible.
* **P3 context lost on change**: something changes (a contract is amended, a test is disputed, a part is divided) and the consolidated picture is not rebuilt, so cards, tests and code drift apart.
* **P4 no way to question**: a worker that sees the problem has no channel to question the design, so it works around it (hacks, silent assumptions) or fails; the "why" is not available to it, so it cannot make the right local decision.
* **P5 patterns repeat**: a planner's habit (a decomposition shape, a missing header, a vague contract) reproduces in every run, and each worker inherits it.
* **P6 narrow context hides rationale**: even a correct, complete card gives the *what* without the *why*, so a worker cannot judge whether a test, a signature or a constraint is a mistake or intended.

The fix we want is not "write better cards" alone. It is to make developers **experts with standing**: they know the whole project well enough to push back, they are *forced to question and pull context* before they build, and there is one
role that holds the whole picture and a defined way to consolidate it when anything changes.

## 2. What the real runs already show (so far, evidence for and against)

* **For P1 / P2 (the plan or contract is wrong or thin).** Run-v8: the architect, asked to divide a *tester* item, invented a whole calculator project (row 44); the divided parts had no header contracts, so every part tester failed (row 45). Run-v10: the fork's section list omitted
  the contract's own entry point (row 59). Run-v11: the skeleton declared a class whose members no section covered (row 71). Run-v12: a helper's const-iterator signature, written into the skeleton by the model, made the entry point uncompilable three times (row 74). Row 25: a test imported
  an export no contract named. Row 26: page tests that checked text with a strict regex the contract never stated.
* **For P4 (no channel to question).** Row 26 and the interface-mismatch rows: the worker retried the leaf for hours; nobody could say "this test contradicts the contract". The only escalation was the ladder's `reviseTests`, which is the harness guessing, not the worker arguing.
* **Against, or untested.** In every real run so far the *plan* was a hand-written blueprint, so the planner role itself (a model planning from a bare prompt) has NOT been run at all. What we have evidence on is the **architect's decomposition** and the **skeleton**, both model-made plans of parts. So the strongest honest statement is: model-made *sub-plans* fail in the
  ways P1, P2 and P4 predict; whether a model-made *top-level plan* does is the open experiment.

## 3. The project knowledge base, in levels

All project knowledge lives in one layered store (the same structure as the knowledge register of [WORK_GRAPH.md](https://github.com/CraigHutchinson/Sub0Squad/blob/96a17c925fb82feff086fc5106bc0a79915be3e8/docs/WORK_GRAPH.md) (archived) section 19, scoped to a project), addressable by level so that an agent pulls only as deep as it needs:

| level | what it holds | who writes it | machine-checkable? |
|---|---|---|---|
| **L0 intent** | the request as given, the owner's answers to clarifying questions, the acceptance bar, constraints (platform, style, limits) | owner, planning stage | the answers are data |
| **L1 architecture and rationale** | the design in prose with the WHY: decisions, alternatives rejected, risks, glossary of names | planner / architect | no (reviewed) |
| **L2 contracts** | interfaces, signatures, file ownership, invariants, error behaviour, golden examples | planner; the harness generates what it can (headers, stubs) | yes: headers compile, schemas validate |
| **L3 plan and state** | the items, dependencies, who owns what, status, the **change log** (every amendment with its reason and who asked) | planner / plan keeper; the daemon records state | partly (plan validation) |
| **L4 code map** | outlines, symbol index, callers and callees, test index (folded views) | derived by the harness | yes (derived) |
| **L5 code and evidence** | full files and canvas revisions, gate outputs, conversations, traces | the workers and the harness | yes |

Every entry has an id, level, scope, text, evidence pointers, author, revision and `supersedes`. The harness keeps L4 and L5 itself; the planner owns L0 to L3 and is accountable for them being consistent.

## 4. Pull-based context: agents are made to question

Today the worker is *pushed* a card chosen by someone else. The alternative is a worker that starts thin and **pulls**:

1. **A thin card**: the task, the files it owns, the gate, and pointers into the knowledge base (level and topic names), not the content.
2. **Pull requests** in the reply grammar the worker already uses: `=== CONTEXT: L1 architecture ===`, `=== CONTEXT: L2 calc::evaluate ===`, `=== READ: path LINES a-b ===` (L4 and L5, built). The harness answers with that entry (bounded, with its rationale and evidence pointers). Pulls cost no repair turn (as reads do today).
3. **A forced first turn ("spec check")**: before writing anything the worker must reply with a short list: what it assumes, what it needs to know, and what looks wrong or missing in its card or tests (100 to 250 tokens). The harness answers the needs from the knowledge base. This is the cheap equivalent of an engineer reading the ticket and the design doc before touching code. It is an arm of the experiment, not a given (section 6): it costs tokens and must pay for itself.
4. **Questions and concerns have a typed channel** (section 5), so a worker is never stuck choosing between hacking around a defect and failing.
5. **Everything pulled is recorded**: what was asked, what level, whether it changed the work. That is the data for "was this context needed?".

## 5. Standing to push back, and consolidating the picture when things change

**Roles.** The **plan keeper** (the planner or architect role) owns L0 to L3 and is the one place that holds the whole picture. Workers own their section and their evidence. Everyone else is a reader.

**The channel.** A worker raises `=== CONCERN: <kind> | <text> | evidence ===` with kind one of: *underspecified* (needs more), *contradictory* (two statements conflict: a test and the contract, two contracts), *wrong* (false or infeasible), *missing contract* (a name or interface nobody owns), *better idea* (a proposal). It becomes a request record in the inbox (the existing `request` machinery) addressed to the plan keeper, with the worker's evidence attached
(the failing line, the two conflicting texts). The keeper answers: **accept** (an amendment), **modify**, or **reject with a reason**; the reply is delivered to the worker as context, and the worker continues in its own section on the settled spec. A worker whose concern is rejected has the reason in front of it, which is the "why" it lacked.

**Consolidation on change.** An accepted amendment goes through one step, not ad hoc edits:
1. the keeper records the change in the **L3 change log** (what, why, who asked, which levels);
2. the harness **regenerates what is derivable** from L2: contract headers, stubs, skeleton, the affected cards, the L4 map;
3. every item that depends on the changed entry is marked **stale** with the reason (the V model's up and down transfer, `docs/PROCESS_DESIGN.md`), and re-planned or re-run only as far as needed;
4. the workers who raised or hold related concerns are told the outcome.
This is the "someone has the full picture and rebuilds the context when things change" that is missing today (the contract amendment is designed, not built).

**Developers as experts.** The prompts and the stage structure say so plainly: you own your section and its quality; check the spec against the contract and the tests before you build; if they disagree, raise a concern; do not code around a defect in silence; say what you learned about the design. The habits of `docs/METRICS.md` section 4a (verifying, suggesting, designing) are scored for it.

## 6. The experiments: how much context, in what shape (A/B on failing cases and over long runs)

Context is a set of knobs, each worth an A/B:

| knob | arms |
|---|---|
| card size | minimal (task, files, gate) / standard (today) / rich (adds the L1 overview and rationale) |
| project overview | none / one paragraph / the L1 document |
| rationale ("why") | omitted / included for contracts and tests |
| tests shown | whole / index of names / failing assertions only |
| context mode | pushed (today) / pushed + pull allowed / thin card + forced spec-check turn |
| pushback channel | off / on (with a keeper that answers) |
| knowledge levels exposed | L2 only / L1 and L2 / all |
| view of code | whole / folded with reads |
| keeper | rule-based / the model (35B) / a stronger model when allowed (never frontier without the owner's switch) |

**Method.** (1) The campaign ([EXPERIMENTS.md](EXPERIMENTS.md)) gives the baseline: many seeds of today's configuration, with the failure classes. (2) Failing cases become **scenarios** (`docs/SCENARIOS.md`): the exact failing item with its recorded state, so an arm can be tried on it in minutes with the same seeds and the confidence rule of `scenario compare`.
(3) Promising arms are run as whole campaigns (20 seeds) and compared on the KPIs of `docs/METRICS.md`: first-pass rate, oracle pass rate, tokens per quality point, convergence and thrash, plus the context-specific ones below. (4) The plan-quality question is tested directly by running the **planner** (a model planning from the bare prompt, with and without the pull and pushback machinery) against the hand-written blueprint as the control, judged by the same downstream outcomes.

**Context-specific measures.** Context tokens provided against pulled (what was needed vs offered); questions asked, and the **useful-question rate** (a question that changed the work or avoided an error, judged by whether the answered attempt passed where the same seed without it failed); concerns raised, accepted, rejected, and the outcome after each; stale items created by amendments and the tokens to repair them; plan-defect rate (failures whose first cause is in L1 to L3, from the failure taxonomy of [EXPERIMENTS.md](EXPERIMENTS.md)).

## 7. Risks and what would change our mind

* **More context can hurt a small model** (distraction, a full window): the arms test it; the thin-plus-pull design is partly a bet that pulling beats pushing for small windows.
* **The forced spec-check turn may be tokens with no return**: if the useful-question rate is low it is dropped or made conditional (only after a failure).
* **The keeper can be wrong too**: a rejected concern with a bad reason hardens a defect; the concern and its outcome are kept so a later failure can be traced to it, and an owner can overrule.
* **Consolidation cost**: regenerating on every change could cost more than it saves; measure stale-item repair tokens against the failures avoided.
* If the baseline campaign shows failures are mostly execution (assembly, semantic slips) and not plan-class, the planning and context work moves down the priority list; the taxonomy is what lets us see that.

## 8. Build order

1. The campaign driver and the failure taxonomy ([EXPERIMENTS.md](EXPERIMENTS.md)): the baseline and the evidence.
2. The metrics accounting (`docs/METRICS.md` slice 0), so context and token measures are trustworthy.
3. `CONTEXT` pull requests over the existing knowledge-register files and recall (levels L0 to L5 as a directory layout in the workflow sandbox with an index); L4 and L5 already exist as outlines, reads and traces.
4. The `CONCERN` channel: request kind, keeper job (rule-based first), answer delivery, the L3 change log.
5. Consolidation on change: regenerate derivable context, mark dependents stale (needs `Plan::extended` and the contract amendment of `docs/PROCESS_DESIGN.md`).
6. The spec-check turn and the thin card as selectable context modes; then the A/B campaigns.
7. The project knowledge base as the **sandbox knowledge store** project ([KNOWLEDGE_STORE_SCOPE.md](https://github.com/CraigHutchinson/Sub0Squad/blob/96a17c925fb82feff086fc5106bc0a79915be3e8/docs/KNOWLEDGE_STORE_SCOPE.md) (archived)) is the test-bed for the storage layer itself; it stays a separate sandbox project, comparable to this component, until a decision is made to adopt it.
