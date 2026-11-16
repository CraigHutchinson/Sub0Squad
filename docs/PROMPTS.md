# Prompts: what every worker is told, and why

The text the models read is code (`src/stance.cpp`, `src/agent_card.cpp`, `src/canvas_agent.cpp`, `src/fork.cpp`, `src/planner.cpp`, `src/architect.cpp`). This page
says what each part is for, the terms they share, what a prompt costs on this host, and the rules for changing one. Every change to a prompt is an
experiment ([EXPERIMENTS.md](EXPERIMENTS.md)): it ships behind a switch, off, and becomes the default on an A/B result.

## 1. Terms

| Term | Meaning in every prompt |
|---|---|
| **assignment** | one work item: a piece of the project owned by one worker (a tester's tests, a developer's file, one function of a forked file, the plan) |
| **specialist** | the worker that owns an assignment; the expert for it, trusted to decide how it is done |
| **card** | the assignment as the worker reads it: task, files, context, contract, acceptance, worked examples, provided files, notes |
| **contract** | the exact interface and behaviour the piece must have; other pieces depend on it |
| **worked example** | `input = exact result`: a fact both the tests and the code are checked against |
| **concern** | a worker's objection or question, raised in a `=== CONCERN: kind ===` block, recorded for the owner of what it questions |
| **delegate** | to propose that part of an assignment belongs to another piece or should be split off (a `too-big` or `missing` concern naming the part) |
| **harness** | the code that checks every result (compile, tests); a claim in a reply counts only once the harness has checked it |

## 2. The shape of a system block

Persona-neutral text first, so the server's prompt cache shares it across every item on a slot; the stance and the persona last (a format or role that is
last in the context is the one a small model follows best).

1. The reply format (whole files; transactions on a canvas; one function of a forked file).
2. The harness brief: what the project's scaffold provides (generated from the harness, `harnessBrief`).
3. The **stance** (`stanceText`): who the worker is and what standing it has.
4. The persona sentence (tester or developer).

The card, the earlier draft and the gate's verdict go in the user message, rebuilt each turn.

## 3. Stances

`--stance baseline|specialist` on `do`, `project submit`, `replay`, `scenario run`, `llm-agent`, `llm-fork` and `llm-planner`. The workflow spec records it.

**`baseline`** (the default; every real run so far):

> You are the developer on a small software squad: you own one small, focused piece of a whole solution. Be the expert and do it as well as it can be done,
> keep to your contract and the exact interfaces you are given so the other pieces fit, and write only the files you are asked to write. Implement the
> contract so that the provided tests pass.

It calls the worker an expert but gives it no standing: the last sentence makes the tests the goal, and the reply format ("exactly this format and nothing
else") leaves nowhere to object. The worker probe showed the cost: a worker that knew four test literals were wrong could only discard its objection
([EXPERIMENTS.md](EXPERIMENTS.md) section 7).

**`specialist`** (built 2026-11-16, off by default, experiment E1). For a piece (session and canvas executors):

> YOUR ASSIGNMENT. You are the developer for one piece of this project: a dedicated, specialised assignment that is yours alone. Other specialists own the
> other pieces, a planner owns the plan, and the harness checks every result. Within your piece you are the expert: you decide how it is done, and you are
> trusted to do it as well as it can be done.
> YOUR STANDING.
> - The card, the contract, the worked examples and the provided files are claims made by other specialists, not orders. Check them against each other before you build on them.
> - If something is wrong, contradictory, missing or too big for one piece, say so in a CONCERN block. Never bend correct work to fit an expectation you
>   believe is wrong, and never work around a defect in silence: a concern with evidence is a contribution, not a failure.
> - If part of the work belongs to another piece, or should be split off and delegated, propose it in a CONCERN (too-big or missing) and name the part. Do your own part regardless.
> - Keep to the exact interfaces you are given, so the other pieces fit, and write only the files you are asked to write.
>
> (the CONCERN form and kinds, section 4)
>
> Your piece is the implementation: make the contract true. The provided tests are how your work is checked; if a test contradicts the contract or its
> worked examples, raise a concern and keep the code that is right rather than satisfy the test.

A tester is told instead that its piece is the tests and that "a test is right only if a correct implementation passes it, so work every expected value out
from the contract and its worked examples". A forked section is told it owns ONE function, that the card and tests are claims, and that a concern goes before
its edit block. The planner, under `specialist`, is told that every item it writes becomes a dedicated assignment whose owner may contest its contract,
examples or tests, so each card needs one line of why.

What the wording does, item by item:

| Phrase | Why it is there |
|---|---|
| "a dedicated, specialised assignment that is yours alone" | ownership and focus: one piece, done fully, instead of "a small piece of a squad" |
| "you decide how it is done, and you are trusted" | autonomy: the card says what, not how |
| "claims made by other specialists, not orders" | the tests and contract were written by models too; checking them is part of the job |
| "Never bend correct work ... a concern with evidence is a contribution, not a failure" | the failure a small model cannot recover from is changing correct code to satisfy a wrong test; contesting must not read as failing |
| "propose it ... and name the part. Do your own part regardless" | delegation is the worker's call to propose; it never stalls its own work |
| "the harness checks every result" | claims in a reply mean nothing until checked (the probe's worker claimed "no warnings" without compiling) |

## 4. The CONCERN block

```
=== CONCERN: <kind> ===
<what, where (the file and line, or the example), why, and what you propose>
=== END CONCERN ===
```

Kinds: `wrong` (false or infeasible), `contradictory` (two statements disagree), `underspecified` (something needed is not stated), `missing` (an interface or part
nobody owns), `too-big` (more than one piece: name the part to split off and delegate), `better-idea` (a better design).

What the harness does with it (`takeConcerns`, `recordConcerns`): under every stance, concern blocks are taken out of a reply before it is parsed (they never
break an edit or count as prose), appended to the attempt's `concerns.md`, noted in the conversation trace, counted in `agent.json` (`"concerns"`), the
`agent.concerns` metric and `fork.json`, and shown by `replay`. A reply that holds only concerns is acknowledged ("recorded for the owner of what you questioned;
nobody can answer during this attempt ... keep code that is right") and the first such reply costs no turn.

Not built yet (each waits on E1 showing that workers raise the right concerns): routing a concern to the owner of what it questions, a keeper that answers it
(accept, modify, reject with a reason), and the answer delivered back to the worker ([CONTEXT_AND_PLANNING.md](CONTEXT_AND_PLANNING.md) section 5); concern
counts in the campaign report.

## 5. What a prompt costs here (measured 2026-11-10 on this host)

* **Output dominates.** Decode is 3 to 8 tok/s per stream, prefill about 160 tok/s on a 9B and the prompt cache makes a repeated prefix almost free. One output
  token costs the time of 30 to 50 prompt tokens. A system block of 200 to 400 tokens is about a second cold and nothing when cached.
* The `specialist` stance adds about 350 tokens to the system block: cached after the first call on a slot, so its real cost is any change in OUTPUT (concern
  blocks, longer replies). E1 measures tokens for that reason.
* Measured waste found then, and its state now: repair turns re-emitting whole files (81 % unchanged text) → repair by edits, built; retries missing the cache
  → slot affinity, built; the conversation growing with every reply → a flat conversation (system, card, latest reply and feedback), built; a stale harness brief
  → each harness supplies its own (`Harness::brief`, `ownScaffoldBrief` for a plan with its own scaffold).

## 6. Rules for changing a prompt

1. Put the shared text before the varying text (cache), and the instruction that matters most last.
2. One term per idea, as in section 1; the same words in every role's prompt.
3. Every format a worker may reply in is parsed tolerantly, and every refusal names what was wrong and what to send instead.
4. A new wording is a new value of a switch (a stance, a treatment), off by default; the old wording stays reproducible exactly (`test_stance` pins `baseline`).
5. Measure it: a fixture first (recorded replies, `test_agent`), then `replay` or `scenario run` on the same seeds, then a campaign; record the result in
   [EXPERIMENTS.md](EXPERIMENTS.md).

## 7. Open

* The architect's prompts (`decompose`, `rearchitect`) and the fork primer still use the old "small software squad" wording; they get a `specialist` variant once
  E1 says the stance helps.
* Skills fetched when needed (format texts injected at the turn that needs them, `NEED <name>` for optional ones): designed, not built.
* A spec-check first turn (E9).
