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

## 2. Layer 0: the operations (the base and its catalogue)

### 2.1 Core: the minimal complete set

| Operation | Effect | Address (one of) | Body |
|---|---|---|---|
| `view` | read, no change | a file; a line range; a named function | none |
| `edit` | change part of an existing file | a line range; a named function; a unique text match; a position after line n | the replacement (empty deletes) |
| `create` | make a file, or replace a stub | the path | the complete file |
| `request` | ask for a capability none of the above provides | a capability description | none |

These four are complete for the task space: any read is `view`, any change to a file is `create` or `edit`, and any capability they lack is `request`.
The current forms map onto them exactly: FILE = `create`; SYMBOL, SEARCH/REPLACE, AFTER and LINES = `edit` with four address kinds; READ SYMBOL and READ LINES
= `view` with two address kinds; FIND TOOL = `request`.

### 2.2 Codified extensions

Codify an operation when it passes all three tests: it is common in real tasks, the harness can guard it deterministically, and composing it from the core
costs many turns or risks collateral change. Anything that fails a test stays a composition of core operations.

| Operation | Family | Parameters | Guard the harness enforces | Composed from the core as | Status |
|---|---|---|---|---|---|
| `search` | locate | pattern; scope (file, files, tree); mode `literal` or `regex`; case; whole-word; result cap | a bad regex is a refusal, not a guess; results are capped and say so | many `view`s and reads | codified |
| `outline` | locate | path | none; reports the spans the folded view uses | `view` of every range | codified |
| `find-files` | locate | glob within the tree | result cap | none without a listing | codified |
| `view-around` | read | a search hit (a handle from `search`); lines before and after | the window lies inside the file | `search`, then `view` | codified |
| `replace` | change | pattern; replacement; mode `literal` or `regex`; scope; **expected count** | the count found must equal the expected count, or nothing is written | one `edit` per occurrence, a turn each | codified |
| `rename-symbol` | change | old name; new name; scope | whole identifiers only; the new name must be unused in scope; the count is checked | `replace` with word boundaries, unchecked | codified |
| `rename-file` | structure | from; to | the source exists; the target does not exist; content is moved whole, not copied | `create` plus a delete, which loses content if copied by hand | codified |

Delete is not a separate operation. It is `rename-file` into the harness's bin path (`.bin/<original path>`, a reserved location the worker cannot
create files in directly), so a deletion is recoverable by renaming back. The bin keeps the original path, so a second deletion of the same name
is refused until the first is cleared.
| `move-block`, `extract-function`, `reorder` | change | varies | varies | `view`, `create`, `edit`, `delete` in sequence | composed (not codified until a case needs it atomically) |

Notes that follow from the rule:

* **Regex is a mode, not an operation.** `search` and `replace` take `mode: literal | regex`. A regex that does not compile is refused with its position.
* **Every bulk change names its expected count.** `replace` and `rename-symbol` refuse to write when the count differs. This makes collateral change explicit
  in the request, and the probe can check it at T3, the same way it checks a SEARCH text that must occur once.
* **Delete is a move to the bin.** `rename-file` to `.bin/<original path>` is a deletion the harness can undo by renaming back. Nothing is
  lost silently, so there is no separate delete operation to guard.
* **`search` returns handles.** A hit carries the path, line, revision and a handle that `edit` and `view-around` accept, so a change can address a hit
  without copying its text. That is the cheapest address the 2026-11-20 study measured, and it needs no new edit form.
* **`outline` is the folded rule's source.** The "read before edit" precondition (section 3) needs the worker to know what is folded; `outline` states it.

### 2.3 Grouping

Five families, read in this order by the usage text: **locate** (`search`, `outline`, `find-files`), **read** (`view`, `view-around`), **change**
(`edit`, `replace`, `rename-symbol`), **structure** (`create`, `rename-file`), **request** (`request`). Each family has at most three
operations, so the surface stays small enough to describe in one short list.

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
2. **Canonical operation table** in code: the core four and the codified catalogue (section 2.2), with their parameters and guards. The text renderer and the scorer both read it.
3. **Native arm** (`--encoding native` in the probe): the same scenarios, the same canonical checks, the tools passed through the chat template. Three
   samples per scenario, as now.
4. **Usage variants**: the table's text against the model's self-written text, same scenarios, same arm.
5. **Catalogue scenarios**: one scenario per codified operation, each with a reference reply and a failing control. For `replace` the control
   gives the wrong expected count; for `rename-symbol` it renames a substring inside another identifier; for `search` it sends an invalid regex;
   for `rename-file` it targets an existing path, and a deletion scenario checks that the file sits under the bin path.
6. **Adoption**: change the production canvas prompt only when an arm wins on T2 and T3 without more failures on T1. Record the comparison in
   [INTERVENTIONS.md](INTERVENTIONS.md) with the numbers.

## 6. Open questions for the owner

1. Is the engine's chat template able to take a `tools` list? The shared server reports the model "Inkling 2 Mini" with `--jinja`-style tool support not
   yet confirmed; the native arm needs that checked, read only, before any run.
2. Should `request` (FIND TOOL) have an effect, such as queueing a tool for review, or stay a text signal that the harness only records? Today it is
   recorded only; automated tool addition is not built (frontier off).
3. Is the codified catalogue the right size for the product? Each codified operation adds a scenario and a usage paragraph, so a larger set costs
   probe time. Should `view` and `edit` be split by address kind for models that handle many small tools better?
