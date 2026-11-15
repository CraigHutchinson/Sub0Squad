# How a plan (and other structured work) should be written: options, prior art, recommendation

Status: design, 2026-11-15, from the owner's direction after the first end-to-end runs. Evidence: the planner on the 35B-A3B produced a valid plan in about 40 to 60 % of
runs; nearly every failure was FORMAT (a stray quote, a bracket short, a missing stub), not planning. See `docs/CLEAN_SLATE.md` section 7 and the runs
`w-000005` to `w-000010`.

## 1. What goes wrong today

One reply carries two disciplines at once: *what to build* (the split, the contracts, the examples) and *exact syntax* (nested JSON with escaped quotes, plus C++ stubs
that must compile under `-Werror`). A small model at 3 to 5 tokens a second spends its budget on both, cannot see its own syntax errors, and a failed reply is rewritten whole.
Format slips are the dominant failure, and they are cheap to fix mechanically if they are found in isolation.

## 2. Prior art (what others found; vendor claims marked)

| Source | Finding | Relevance |
|---|---|---|
| Tam et al., "Let Me Speak Freely?" (arXiv 2408.02442) | Strict format restriction lowers reasoning quality; the stricter, the worse; a two-step "answer in prose, then convert to the schema" keeps the unrestricted quality. Tested on GPT-3.5-class models, simple 2-field schemas. | Supports dividing planning from formatting. Not shown for our models: measure. |
| BAML, "schema-aligned parsing" (boundaryml.com; vendor claims, own benchmark) | Do not demand strict JSON: parse what the model wrote against the declared schema (trailing commas, unquoted strings, text around the object, near-miss enum values), with a compact TypeScript-like schema in the prompt instead of JSON Schema. Works on any model. | Our `repairJson` is a hand-made subset. A schema-guided tolerant parser is the general form. |
| TOON (project's own benchmark, no independent replication found) | A compact tabular notation of the JSON data model, about 40 % fewer tokens, accuracy about equal; best on uniform arrays of objects, not on deeply nested data; some local runtimes are faster on compact JSON. | Our plan is nested and non-uniform: small gain expected. A flat form for items (a table) may fit. |
| Grammar-constrained decoding: llama.cpp GBNF and `json_schema`, XGrammar, Outlines (arXiv 2411.15100, 2501.10868) | The decoder cannot emit invalid syntax; reported speed-ups as the structure tokens are forced; coverage of JSON Schema features differs by engine. It guarantees structure, not correctness, and the "speak freely" result says it can hurt the content. | We run llama-server, which accepts a grammar or schema per request. Our engine call does not pass one yet. Candidate for the FORMAT step only. |
| Owner's own finding | Thinking off is more reliable and faster for JSON. | The planner already runs with thinking off (`EngineProfile::thinking` defaults to false); keep it off for every format step. |

## 3. Options for the plan

| # | Option | Pros | Cons |
|---|---|---|---|
| A | **Today**: one reply, JSON plus FILE blocks, mends, repair by edits | Built; the edit repair removes the rewrite cost | One reply still mixes planning with syntax; first-reply failures remain; JSON is the least forgiving notation for a small model |
| B | **Two steps, one discipline each**: (1) PLAN IN PROSE: a model writes the split, contracts and examples in a simple outline (headings, bullet lines, `input = result` examples), thinking off. (2) FORMAT: a second call turns the outline into `plan-2`, with a grammar or schema constraint if available, and its failures are repaired by edits at the line the error names | Each call has one job and a small output; the format step is mechanical and can use a smaller or constrained model; a format failure never loses the planning; matches the discipline-boundary principle | Two calls (the prose step is short: a few hundred tokens); the outline form needs a parser; information can be lost between the steps (mitigate: the formatter must not invent, a check compares counts of items and examples) |
| C | **Plan in a lenient notation** (YAML-like or a line-per-fact text), parsed by our own tolerant parser into the plan | One call; no quote or bracket slips possible in the notation; the parser reports the line of any slip | A new format the models must learn from the prompt; our parser is code to own and prove |
| D | **Grammar-constrained JSON** for the whole reply | Syntax cannot fail | May lower plan quality (Tam et al.); a nested grammar with long free strings is slow to constrain on CPU; does not stop semantic slips (a missing stub, a wrong example) |
| E | **TOON for the items table** | Fewer tokens | Nested cards do not fit; gain is small on our shape |
| F | **Section by section**: the planner writes the skeleton (item ids, one-line goals, dependencies) first, then each card is its own small call (parallel on idle slots) | Small replies; cards run in parallel on 4 slots; a bad card is redone alone; the same shape as the fork executor | More calls; cross-card consistency (names, the header) needs a final check |

## 4. Recommendation

B then F, with D only on the format step and BAML-style tolerant parsing in the parser:

1. **Prose plan** (thinking off, short): item ids, a line per goal, dependencies, then per item the contract in prose and the worked examples as `input = result`.
2. **Format step** (a discrete task): turn each item's prose into its `card` object, one call per item, run in parallel on idle slots (option F). The format call may carry a grammar or schema where the engine supports it. A slip is repaired by an edit at the line the error names; nothing is regenerated.
3. **Scaffold step** (another discipline): the header and stubs are written by a developer-persona call from the contracts, and the plan checks and the `-Werror` compile check judge it; stubs that only ignore parameters are mended mechanically (done).
4. **Tolerant parsing**: keep `repairJson` and the mends; grow them into a schema-guided parser (names near-misses, drops trailing text, coerces numbers and lists) instead of one more regex per slip.
5. **Measure before adopting**: each step is a block with a fixture and a number (valid-first-try rate, repair edits per plan, tokens per plan, wall time), compared with option A on the same seeds. Grammar constraint is adopted only if it does not lower the plan's content quality on the same judge.

This divides at discipline boundaries, so a smaller or specialised model, or an MoE with a narrow job, can take the format and scaffold steps, which are mechanical and checkable.

## 5. The same idea for the rest of the workflow

- Tester and developer replies already use edit transactions; the open work is to make the *first* reply small (the section split) and to persist what a worker knew (the planner document is now kept under `planner/doc/turn-N/`).
- A "formatter" role is a generic building block: free-form worker output in, a checked artefact out, repaired by edits. It serves the plan, the gate report, the work-card rewrite (`rearchitect`) and the manager's decisions.

## 6. Not decided (owner)

- Whether a second, smaller model may serve the format step (frontier stays off; this is a local model).
- Whether to add grammar or schema passing to the engine call (a change to the engine request, local only).
