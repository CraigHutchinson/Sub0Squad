# Tool surface: operations, usage and embedding (design, 2026-11-25)

Design only; nothing here is built. Status of the measurements it relies on: [T0_ITERATIVE_SANDBOX.md](T0_ITERATIVE_SANDBOX.md), "Tool-use probe".

## 1. Why

The canvas worker is given a set of forms (FILE, SYMBOL, SEARCH/REPLACE, AFTER, READ, and FIND TOOL in the need arm). Three questions are mixed up in the
current prompt, and the probe cannot yet separate them:

* **What can the worker do?** The operations. Independent of any wording.
* **When and how should it use each one?** The usage guidance: which operation fits the state, its preconditions and limits.
* **How is that told to the model?** The embedding: a text protocol in the system prompt (today) or the chat template's native tool calling.

The 2026-11-25 plain-arm probe shows the cost of mixing them. Most failures are framing failures (READ requests run together on one line, or without `===`
framing), which is an embedding problem, not a question of which operations exist. The model also emitted its own tool-call token
(`<|content_invoke_tool_json|>`), so it has a trained calling convention that the text protocol does not use.

## 2. Layer 0: the operations

### 2.1 The reduced set

Five operations. Each is a verb with one kind of address or mode; the rest is parameters.

| Operation | Effect | Address or mode | Body or parameters | Guard the harness enforces |
|---|---|---|---|---|
| `view` | read, no change | a file; a line range; a symbol; a handle from `search` with `context n` lines around it | `mode: text` (default) or `outline` (functions and folded spans) | ranges and handles lie inside the file; a folded span is shown as folded until it is viewed |
| `search` | locate, no change | scope `content` or `paths`; a pattern | `mode: literal`, `regex` or `identifier`; case; result cap | a regex that does not compile is refused with its position; results are capped and say so; each hit carries a handle |
| `edit` | change part of a file, or write a whole file | a line range; a symbol; a position (after line n); a `match` (pattern, mode, scope, **expected count**); a whole file | the replacement (empty deletes) | a symbol must exist or be added; a match is written only if its count equals the expected count; a whole file replaces only a stub, or creates a missing file |
| `rename` | move a file, keeping its content whole | from; to | none | the source exists and the target does not; deletion is `rename` into `.bin/<original path>`, which is recoverable |
| `request` | ask for a capability none of the above provides | a capability description | none | recorded only; it changes nothing |

Expected count is the safety rule for every bulk change: an `edit` by `match` states how many occurrences it expects, and writes nothing otherwise. A
unique-match SEARCH/REPLACE is the case where the count is one, so it is the same operation, not a separate form.

### 2.2 The reduction pass

The rule: an operation is removed when it can be expressed as an address or mode of an operation that stays, and the guard it needs is the same. Each
removal keeps its guard, so nothing the harness checks is lost.

| Removed (earlier catalogue) | Now expressed as | Guard kept |
|---|---|---|
| `create` | `edit` with a whole-file address | replaces only a stub, or creates a missing file |
| `replace` | `edit` with a `match` address, mode `literal` or `regex`, and an expected count | the count must match |
| `rename-symbol` | `edit` with a `match` address, mode `identifier`, scope the tree, and an expected count | whole identifiers only, count checked; "the new name is unused" is a usage precondition (search first) and is caught by the compiler gate if violated |
| `find-files` | `search` with scope `paths` | result cap |
| `outline` | `view` with `mode: outline` | the folded spans it reports are the ones `view` shows folded |
| `view-around` | `view` with a handle and `context n` | the window lies inside the file |
| `delete-file` | `rename` into the bin | recoverable by renaming back |
| `move-file` | `rename` | the target does not exist; content is moved, not copied |

Kept, because no other operation can express them without losing a guard or an effect: `view` (reads have no effect, so no other verb carries them),
`search` (it returns the handles that `edit` and `view` accept, which a plain read cannot), `edit` (the only change), `rename` (a move keeps content whole,
which an `edit` cannot do across files), and `request` (it is the only signal for a capability that is missing).

### 2.3 Trade-off, for the probe to decide

Fewer operations give the model fewer verbs to choose among, but each `edit` address now has its own parameters. Whether that is easier for a model
than separate verbs is an empirical question, and the one the catalogue scenarios answer: the scenarios hold the task fixed and vary only the
operation the model picks. The cut-off in the probe's T2 tier ("the operation the task calls for") is the measure. If `edit` with a `match` address
fails T2 where a separate `replace` would pass, the reduction is wrong at that point, and the operation goes back.

## 3. Layer 1: usage (the embellishment)

Usage is guidance on top of the base set. It has four parts per operation, and each part is testable with the existing probe:

1. **When** to choose it, stated from the state the worker sees (a whole function is wrong or missing: `edit` by name; a few lines are wrong: `edit` by
   match; the file is missing or a stub: `create`). This is the T2 "selected" tier.
2. **Preconditions** the worker must check first (a folded function is viewed before it is edited). Scenario `folded-function` tests it.
3. **Limits** that the harness enforces (a match is at most about 15 lines and must occur once; a `create` may not replace a real file). Tier T3.
4. **One worked example** per operation, neutral in content so the model does not copy it (the 2026-11-20 edit-address study found a one-line example
   steered the worker to one-line replacements).

The usage text is generated from the same table as the operations (one source of truth), so a limit can never drift from its check. The model's own
wording, from the knowledge probe's self-prompt request, is an alternative usage text to be measured, not a replacement for the table.

## 4. Layer 2: embedding (the system prompt)

Two renderers take the same operation table and produce a worker-facing surface. The scorer grades the canonical operation after parsing, so one
scenario set grades both.

| | A. Harness text protocol (current) | B. Native tool calling |
|---|---|---|
| How the model calls | `=== EDIT: path SYMBOL name ===` blocks in the reply | a tool call in the model's trained format, arguments as JSON against a schema |
| Delivered by | the system prompt text | the chat template's `tools` field (llama.cpp with `--jinja`) |
| Parsing | our parser; framing failures are a parse-level T3 kind (`unframed`) | the server's parser; a malformed call is rejected by schema |
| Dependencies | none beyond the engine | the server must render tools into the template and return tool calls |
| Observed | framing failures dominate the 2026-11-25 probe; the model also emits its native tool token | not yet measured here |
| Portability | works with any model that follows text | depends on each model's trained format |

Decision rule for the owner: the text protocol stays the default until native calling is measured on the same scenarios. Native calling becomes an
arm, not a replacement, and is off by default (the family rule: new capability off by default).

Measurements common to both arms, from the existing tiers:

* **T1 invoked**: a call in an offered operation.
* **T2 selected**: the operation the task calls for.
* **T3 well-formed**: the arguments are valid for their address kind and fit the source. For A this is the current checker; for B it is the schema plus
  the same semantic checks.
* **Framing**: reported separately. Under B it is a schema rejection, not a text failure, so the two arms are not compared on framing alone.

## 5. Plan

1. **Knowledge probe** (running, 2026-11-25): does the model name the forms with and without them in its system prompt, and what does its own system
   prompt for this task contain? Its answers test whether the wording or the model is the limit. Result to be recorded here.

   **Result (2026-11-25, Inkling 2 Mini, 3 samples, [summary](D:/Craig/GitHub/Sub0Squad/out/runs/t0-tools-know/20261125T223755Z/summary.json), [replies](D:/Craig/GitHub/Sub0Squad/out/runs/t0-tools-know/20261125T223755Z/replies)):

   * **With the forms in the system prompt**, the model names the expected form in 16 of 21 answers, and its answers use the framed header in most
     of them. Its misses name a neighbouring form (a FIND TOOL for a read, a READ LINES for an edit), not an invented one.
   * **With no forms shown**, it names none of them. Its answers describe generic tools (`read_file`, `propose_edit`) and never the `=== … ===`
     framing, so the framed syntax is learned from the prompt, not from prior training.
   * **Its own system prompt** (the self-prompt request, 3 samples) is written for a generic tool API (`read_file`, `list_files`, `propose_edit`,
     `apply_edit`, `comment`, `request_review`), not for our forms.

   Reading: the model follows the forms when they are shown, so the framing failures in the 2026-11-25 probe are about how reliably it writes the
   framing, not a failure to understand it. Its unprompted prior is a generic function-calling style. That points to native tool calling (option B
   in section 4) as the arm to measure, and to the names of the operations being a free choice the owner can make without changing behaviour much.
2. **Canonical operation table** in code: the five operations in section 2.1, with their address kinds and guards. The text renderer and the scorer both read it.
3. **Native arm** (`--encoding native` in the probe): the same scenarios, the same canonical checks, the tools passed through the chat template. Three
   samples per scenario, as now.
4. **Usage variants**: the table's text against the model's self-written text, same scenarios, same arm.
5. **Catalogue scenarios**: one scenario per operation's address or mode, each with a reference reply and a failing control. For `edit` by `match`
   the control gives the wrong expected count; for `identifier` it renames a substring inside another identifier; for `search` it sends an invalid
   regex; for `rename` it targets an existing path, and a deletion scenario checks that the file sits under the bin path. For `view` with `outline` the
   control reads a span the outline reports as folded.
6. **Adoption**: change the production canvas prompt only when an arm wins on T2 and T3 without more failures on T1. Record the comparison in
   [INTERVENTIONS.md](INTERVENTIONS.md) with the numbers.

## 6. Open questions for the owner

1. Is the engine's chat template able to take a `tools` list? The shared server reports the model "Inkling 2 Mini" with `--jinja`-style tool support not
   yet confirmed; the native arm needs that checked, read only, before any run.
2. Should `request` (FIND TOOL) have an effect, such as queueing a tool for review, or stay a text signal that the harness only records? Today it is
   recorded only; automated tool addition is not built (frontier off).
3. Is five operations with rich addresses the right grain for the product, or do models do better with more, narrower verbs (section 2.3)? A
   larger set costs probe time and a usage paragraph per operation. Should `view` and `edit` be split by address kind for models that handle many small tools better?
