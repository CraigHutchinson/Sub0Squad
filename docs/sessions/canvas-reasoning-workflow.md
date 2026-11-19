# Follow-up session for the canvas reasoning workflow

Status is open as of 2026-11-19. The initial live screen and request replay are in [Matched Windows screen and request replay](../BLOCK_PROVING_GROUNDS.md#matched-windows-screen-and-request-replay-2026-11-19).

A worker should reach a concrete plan, carry decisions forward when an edit fails, and test an alternative against the current canvas before choosing to commit it. The Tier 0 replace-function case is the first consumer.

## Default workflow and gate reporting

Canvas defaults to an explicit think, plan and act sequence. The engine profile controls hidden thinking. A visible plan carries the decision into the act turn, which runs with thinking off. A valid transaction or read provided directly can still be used. An incomplete plan does not masquerade as an edit. Use SUB0SQUAD_THINKPLAN=off for a direct control. The options arm remains experimental.

T0 reports first- and second-tested-edit passes as separate KPIs. Reads, plans, rejected edits and starting checks do not advance the tested-edit ordinal. Gate acceptance is still bounded by the configured edit-and-test turn budget. The report indexes failing runs and keeps their original gate and conversation files. The T1 runner can use the exact same executable and T0 evidence directory, and its waiting mode records the single workflow's tested-edit ordinal.

## Bounded options and sampling

Compare SUB0SQUAD_THINKPLAN=options with steps on the same pinned executable, model artifact, fixture seeds, SUB0SQUAD_FIXSITE=1 and max-tokens=7000. Include half-written, replace-function and undeclared-member. Count completed plans, applied transactions, accepted runs, reasoning stops, tokens and wall time. Measure thinking separately from total completion tokens. Compare presence=0 against presence=1.5 in its own paired arm, holding the other precise-coding settings fixed.

Sub0LlmEngine owns model and backend guidance in docs/MODEL_BEHAVIOR.md. Squad already sends enable_thinking per request. Start with the embedded template. Evaluate a custom harness template if the embedded template cannot express a required phase or tool exchange.

For an owner-coordinated run from Git Bash, use the same fresh build for both arms. The output prefix must be unused.

```bash
eng='qwen35=http://127.0.0.1:8092/v1,model=qwen35,ctx=16384,slots=4,thinking=1'
export T0_EXE="$PWD/out/build/mingw-dev/sub0squad.exe"
export SUB0SQUAD_FIXSITE=1
export T0_DO_ARGS='--max-tokens 7000'
for arm in steps options; do
    T0_OUT="out/runs/t0-options-compare-$arm" SUB0SQUAD_THINKPLAN="$arm" \
        bash validation/tiers/run_t0.sh . "$eng" 3 half-written replace-function undeclared-member
done
```

The matched pilot found a fixture scope error and no acceptance gain from the options wording. Keep the plan and act experience focused on a short, executable decision. The installed backend can enforce a short thinking budget, and the same model can write a visible plan with thinking disabled. Those are the next two paired workflow arms on the corrected fixture. Use the same visible plan layout, sampling and seed in both. In a separate arm, ask for the first permitted edit and a dry-run of the highest-risk contract example. The corrected arithmetic fixture supplies a concrete receiving check. Its options run still parsed -2^2 as 4. The two cues improved the replayed plan, and the dry-run variant exceeded the word limit. The act replay still reached 7/8 tests. Once the plan dropped out, subsequent requests and replies were identical. The compact retention replay also reached 7/8 but used two more turns. Keep the next comparison tied to the candidate's actual behavior. For this fixture, have the worker inspect the failing expression against the parser it wrote, preserve the useful part of that diagnosis, and test a small repair against the existing gate. Measure accepted runs, valid plan markers, distinct alternatives, format resets and generation cost. Avoid treating cancelled streams as zero-cost calls. Compare elapsed time and captured reasoning bytes when usage is missing. Leave the 1.5 penalty experimental.

## Compact repair context

Use a plan reply containing the cause, chosen edit and brief alternatives with trade-offs. An act turn receives the chosen edit. On failure, give the next decision turn the prior choice, its verdict and alternatives that still apply. Keep the record bounded and tied to the canvas revision. The full reasoning remains in the local trace. The canvas currently creates a fresh pair of system and user messages each turn, so enabling the template flag alone cannot deliver earlier reasoning. Reuse the structured plan and its revision-tagged verdict in the next decision turn before adding a separate summarizer. Test a completed plan followed by a rejected edit and a successful alternative. Test stopped thinking before a plan exists separately, using the existing carried tail as the control. Measure acceptance, prompt size and repeated work before enabling historical raw reasoning via preserve_thinking.

## Runtime evidence on the canvas

The C++ trace spike recorded failing and passing stack traces, a cursor watch and a 12-input matrix on the archived arithmetic candidate. Evidence stays in out/runs/t0-trace-spike-20261119. The Sub0Canvas runtime evidence brief (docs/sessions/canvas-runtime-evidence.md in that repository) places the first receiving slice here. Start with a trace reference on the failing issue and retrieve a bounded detail on request. Preserve the source revision that ran even after the canvas changes. The gate remains the initial view. The [on-demand C++ replay](../BLOCK_PROVING_GROUNDS.md#on-demand-trace-retrieval-in-the-c-repair) verified retrieval and revision binding in the spike. It found no acceptance gain from adding trace detail to this arithmetic candidate.

## Asynchronous scratch checks

Start with one speculative candidate in a Tier 0 work item. Apply a normal transaction to an isolated canvas snapshot and enqueue the existing compile/test gate. Return a candidate ID and base revision immediately. The existing precheck uses a shared candidate directory within an attempt. A scratch check needs an isolated path and a separate verdict destination before overlap is safe. A later turn can ask for status and get queued, running, passed, failed or stale with bounded diagnostics. Promotion requires explicit selection, a passed check and a matching base revision. Run ordinary acceptance checks on promotion. Bound concurrent checks, cancel with the attempt and discard scratch artifacts when the attempt ends.

The worker sees the candidate, evidence and promoted edit as distinct steps. Deliver feedback at a tool or turn boundary and measure overlap with independent generation. Feedback injected into generation already underway would need a different inference mechanism. Prototype the exchange with thinking off and on using the same checks. Add a Sub0Canvas API only if the worker exposes a product-neutral requirement the current snapshot and transaction APIs cannot meet.

## Receiving gates

Prove revision isolation, stale result rejection, cancellation and selection with deterministic fixtures and a real compiler on replace-function. Then compare acceptance, time to accepted edit, repeated reasoning and concurrent checking on the shared engine during an owner-coordinated run. Tier 1 waits for the full Tier 0 gate.
