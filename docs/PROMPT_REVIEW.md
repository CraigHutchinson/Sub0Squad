# Prompt review: what the prompts cost, and where the tokens should go

Status: review, 2026-11-10. Measured on this host (Ryzen CPU, one DRAM channel) with the Ornith 9B Q4_K_M on our own llama-server, 8 slots, and the
recorded prompts and replies of two real runs (the 35B parallel run, and the 9B V run in progress). Numbers are from files and calls, not estimates,
except where marked.

## 1. What a call looks like today

| Part | Tokens | Notes |
|---|---|---|
| System block, tester | 167 to 222 | persona sentence, "write only the files", the reply format, the harness brief |
| System block, developer | 162 to 217 | the same with the developer sentence |
| System block, architect | 251 to 338 | role, the plan-2 syntax, rules |
| User part, tester | median 312, max 1,586 | the card (task, files, context, contract, acceptance, notes) |
| User part, developer | median 1,005, max 2,817 | the card plus the **whole accepted test file** (up to about 1,700); a retry adds the best earlier files and the gate failures |
| Repair turn (inside an attempt) | previous prompt + the whole reply + about 400 of feedback | the conversation keeps every earlier reply |
| Architect correction | previous prompt + the whole JSON reply (about 2,000) + the error | resent for each of up to two corrections |

## 2. The cost model that decides what matters

Prompt tokens are cheap here, output tokens are not:

- **Prefill** about 160 tok/s (2,996 tokens took 18.8 s). **Decode** 3 to 5 tok/s per stream (5.2 alone, about 3 under the V run's load).
  One output token costs the time of 30 to 50 prompt tokens.
- The **server's prompt cache** works per slot on a common prefix: the same 2,996-token prompt again on the same slot took **0.6 s** (2,992 cached);
  the same prefix with a new ending (a retry) **4.5 s** (2,480 cached, 523 processed); on a different slot **19.2 s**, cold.

So a 200-token system block costs about 1.2 s cold and nothing when cached: **the system prompt is not where the time goes.** Time goes to output
tokens, to prefill that missed the cache, and to attempts that were wasted.

## 3. Findings, by what they cost

1. **Repair turns re-emit whole files.** Over the repair turns of two runs, 12,899 output tokens were written and only about **19 % were lines that
   actually changed**. The other 81 % (about 10,400 tokens, roughly 35 minutes of single-stream decode) was unchanged text written again. This is the
   largest waste found. (Some repair turns were identical to the turn before: 0 to 2 % changed; the stagnation stop catches those after they are paid for.)
2. **A division costs more than most items it divides.** An architect job averages about 2,100 tokens in and **2,700 out** on the 9B (10 to 15 minutes
   at 3 to 5 tok/s). The plan phase now splits an item whenever its estimate is over the budget, and the budget follows idle capacity down to 400 tokens;
   the live V run divided `ui-page` and `engine.evaluator` (estimates of a few hundred to about a thousand tokens) at that price. Dividing only pays when the
   serial work saved exceeds the division itself plus the parts' own tests and the integration. As built, the budget rule is a size rule, not a cost rule.
3. **Retries miss the cache.** A node's retry goes to whichever slot is lowest and free, so the 1,000 to 2,800 tokens of card and tests it shares with
   its earlier attempt are usually prefilled again (19 s instead of 4.5 s for 3,000 tokens).
4. **The conversation grows.** Each repair turn keeps every earlier reply (each a full copy of the files) and every earlier gate output, so the
   third turn carries three copies of the files. Slots here hold 8,192 tokens; a large item can run out of room, and the growth is paid in prefill
   (cache helps only for the unchanged prefix).
5. **The harness brief in the system block is stale and wrong.** It says the stubs "throw 'not implemented'" (they now return undefined or do nothing,
   which is what makes error tests fail against them), names three of the six stubs and omits `style.css`. Wrong text costs tokens and misleads.
6. **The shared prefix is cut short.** The system block starts "You are the tester/developer on a small software squad", so two personas share only
   the first few tokens; everything after the persona sentence (the reply format and the harness brief, about 120 tokens) is identical but sits behind
   a difference. Persona-neutral text first, the persona sentence last, would share it across all items on a slot.
7. **Duplication inside the card.** `Output only X` in the notes repeats `Files to write`; `Context` often restates the goal; a part's contract
   repeats the parent's interface text. Small each, (about 40 to 100 tokens per card), but every card has some.
8. **The whole test file is included on the first attempt.** It is the specification and worth its 1,000 to 1,700 tokens, and prefill is cheap, so this
   is *not* a priority; the `index` treatment (names only) risks losing the exact expectations (a model-written test that expected positions of 2, 5, 7).
   Keep the file; on a retry include only the failing tests plus the names of the rest.
9. **Thinking is off** for these workers and the reasoning count is zero, so no tokens go there. Keep it off by default.

## 4. Recommendations, by expected saving

1. **Repair by edits.** On repair turns ask for edits instead of whole files: `=== EDIT: path ===` with search and replace blocks (a few lines each),
   applied by the harness, falling back to a full file when an edit does not apply. Expected: about 80 % fewer repair output tokens (a repair turn of
   1,500 tokens becomes about 300). Same for the architect: a correction returns only the corrected items, merged by id.
2. **Slot affinity** (built, in the second build): a node's next job goes back to its previous slot when it is free (`chooseLeaseSlot`), so a retry's
   shared prefix is a cache hit. The conversation trace now records how many prompt tokens came from the cache, so the saving is visible.
3. **A cost rule for dividing.** Divide when `estimate x (1 - 1/parallelism) > divisionCost + integrationCost`, with the division cost taken from the
   measured architect output (about 2,700 on the 9B) and the parallelism from idle slots; keep the size rule only as a ceiling for what one attempt can
   hold. The V experiment forces divisions on purpose; the default should not.
4. **A rolling conversation window.** Keep the system block, the card, the latest reply and the latest feedback; drop the older replies and gate texts.
   Context stays flat; the dropped text was a stale copy of the files.
5. **Tidy the system block.** Generate the harness brief from the scaffold list and the stub behaviour (one source of truth, `webHarnessFacts` already
   states it), cut it to what a worker needs, put persona-neutral text first and the persona sentence last, drop notes that restate the file list.
6. **Skills, fetched when needed** (the owner's idea; section 5).
7. Measure before and after with seeded runs: tokens by kind (system, card, feedback, replies), cached tokens, time per turn, and acceptance.

## 5. A skill-like mechanism: known behaviour, content fetched when needed

Idea: the worker knows *that* a capability exists and what it is for; the exact syntax comes into its context fresh, at the point it is used (for the
final report, the exact reply format). Read against the measurements, the saving in tokens is small (the reply format is about 60 tokens, 0.4 s of
prefill, and cached after the first call), but the idea is right for three other reasons: the number of formats is growing (files, edits, plan JSON,
requests, reports, deliverables), a format that is last in the context is the one a small model follows best, and a format not needed in a turn should not
be in it (a repair turn does not need the file format; a plain worker never needs the plan syntax).

Design, in the order I would build it:

1. **A skill registry**: small named text blocks (`format.files`, `format.edits`, `format.plan`, `format.request`, `harness.web`, `format.report`) kept as
   data, each with a one-line summary and the version of the harness it matches (which also ends the staleness in finding 5).
2. **Just-in-time injection by the harness, where it knows the phase**: the first turn gets `format.files` appended at the *end* of the user message
   (after the card, so the shared prefix is persona- and format-neutral); a repair turn gets `format.edits`; an architect turn gets `format.plan`; the
   reporter step gets `format.report`. No model round trip, nothing in the system block but a line naming what exists.
3. **On request for optional capabilities**: the system block lists only names and one-line triggers (`ask: raise a question or a blocker`,
   `recall: search what earlier attempts did`, `interface: the exact signature of a part you use`). A worker replies `NEED <name>`; the harness answers with
   the skill's text in the next turn (cheap with the cache: the turn's prompt is almost all cached and the answer a handful of tokens). Whether a 9B
   asks reliably is unmeasured; the fallback is the harness injecting it when the card says the capability applies.
4. **Tolerant parsing** as the cheapest skill of all: accept the reply forms a model naturally produces (a fenced block headed by a file name, a comment
   naming the path) so the strict syntax need not be taught to every worker on every turn.

## 6. What was not found

The card text itself is not bloated (tester cards 200 to 330 tokens, developer 900 to 1,000 with the tests), thinking is not eating tokens, and the system
block's size is not the problem. The wins are structural: fewer output tokens per repair, fewer wasted divisions, and cache-friendly ordering and
placement.
