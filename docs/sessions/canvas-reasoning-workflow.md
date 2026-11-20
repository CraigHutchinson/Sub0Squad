# Follow-up session for the canvas reasoning workflow

Status is open as of 2026-11-19. The initial live screen and request replay are in [Matched Windows screen and request replay](../BLOCK_PROVING_GROUNDS.md#matched-windows-screen-and-request-replay-2026-11-19).

A worker should reach a concrete plan, carry decisions forward when an edit fails, and test an alternative against the current canvas before choosing to commit it. The Tier 0 replace-function case is the first consumer.

## Default workflow and gate reporting

Canvas defaults to an explicit think, plan and act sequence. The engine profile controls hidden thinking. A visible plan carries the decision into the act turn, which runs with thinking off. A valid transaction or read provided directly can still be used. An incomplete plan does not masquerade as an edit. Use SUB0SQUAD_THINKPLAN=off for a direct control. The options arm remains experimental.

T0 reports first- and second-pass KPIs at both the candidate check and the behavior gate. Candidate checks include compiler failures. Behavior checks count edits that reached the item test gate. Reads, plans, refused transactions and starting checks advance neither ordinal. Gate acceptance is bounded by the configured edit-and-test turn budget. The T0 process disables additional manager near-pass attempts so a fresh worker cannot silently extend that budget. The report indexes failing runs and keeps their original gate and conversation files. The T1 runner can use the exact same executable and T0 evidence directory, and its waiting mode records both ordinals for the single workflow.

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

## Precedence repair probe

The default workflow completed the 78-run Windows T0 matrix on build 7ec8209 with 69 in-budget passes. Two more workflows recovered on a fresh attempt above the declared limit. T1 admission was refused. The gate and the failed run index are in out/runs/t0-think-plan-act-7ec8209. The follow-up pinned replay stopped at the attempt limit and passed write-feature on the first candidate after the Sub0Canvas footer fix.

The wrong-approach candidate evaluated -2^2 as 4. Its instrumented trace showed parseUnary returning -2 at cursor 2 before parsePower read the exponent. The scratch repair evaluated the power before applying the sign. It passed all 16 owner assertions and preserved signed exponents, right associativity and explicit grouping. The runtime witness and before-and-after snapshots are under the run's diagnostics directory.

Full-context control, dry-run and trace-fed prompts failed to close a usable plan on this candidate. A compact prompt supplied the three pertinent functions, the failing assertion and three related expressions. In the first matched seed pair, the compact control plan and dry-run plan each led to a passing first edit. The direct edit still failed the original assertion. The trace-fed plan exhausted its token limit. The standalone probe applies each proposed edit and compares the original owner suite plus four independent expressions.

On a second seed pair, both the compact-plan and direct-edit controls exhausted 3,000 edit tokens in incomplete replies. Restricting the edit to existing parser calls produced an applicable 169-token reply. It changed the power base to postfix parsing but left signed operands disconnected. Adding a parser-progress invariant still left the same behavior failures. The second pair establishes a separate check for a complete grammar change: enter signed operands through unary parsing, parse the power base without a leading sign, and allow signed exponents while preserving right associativity. The prompt variants remain diagnostic.

### What the worker could see

The archived wrong-approach seed 5101 ran with hidden thinking disabled. Its visible plan on turn 6 identified the precedence symptom and speculated about both call orders. The failing assertion was in the prompt and the relevant function bodies were folded. On turn 7 the worker requested six symbols, and turn 8 contained all five pertinent call lines from the revision that failed. A second read returned more source; a third was refused at the existing limit. The later plans pursued modulo, factorial and trailing operators, whose owner assertions were already passing. On turns 8 and 10 the user message asked for an edit after the read while the system message selected a fresh plan-only turn.

The compact replay showed that the source and assertion can support a passing patch. Making the same evidence more explicit did not resolve the second seed: a matched edit request with the verified call map and the observed -2 return at cursor 2 still exhausted 3,000 tokens in an unfinished edit. The exact request and response are under diagnostics/precedence-prompt-second-seed/caller-evidence in the T0 run. The earlier controlled issue-read replay also found no first-edit acceptance gain from delivering the full trace on this fixture.

The next bounded comparison should keep the failing assertion and revision visible through a source read, carry the current plan into one edit turn, and issue a single instruction for that turn. Show the implicated callers and callees in a compact window. Keep the one-line runtime observation available through the issue, with the full stack and values retrievable when asked. Measure whether each requested region arrived, whether the reply became an applicable edit, which owner assertions changed, and how many calls went to re-reading or re-planning. Use the archived post-read sequence as the control.

### Read-to-edit experiment

The default canvas workflow carries the current plan and the pre-read verdict into the edit turn after a read. The source read stays in the prompt and the act request runs with hidden thinking off. Set SUB0SQUAD_THINKPLAN=steps to replay the earlier workflow. The read-handoff value explicitly selects the default.

A Windows replay on build ad6fd25 used the same archived arithmetic candidate, scripted visible plan and native symbol read in each arm. The model wrote every candidate edit. Both seeds ran with a 1,600-token reply limit and up to two tested edits in one worker attempt. The comparison excludes the two scripted calls from the model-call counts.

| Seed | Steps | Read handoff |
| --- | --- | --- |
| 1911 | First-edit pass, 4 model calls, 76.5 s | First-edit pass, 1 model call, 24.5 s |
| 2407 | Test program crashed on the second edit, 4 model calls, 79.1 s | First-edit pass, 1 model call, 25.0 s |

All three accepted candidates passed the 12-expression matrix. The failed control created a parser call cycle that could return to the same input cursor without consuming a character. The original diagnostics and compiled candidates are retained under out/runs/t0-read-handoff-ad6fd25. This replay measures the handoff after a read has been made.

In the unscripted seed 2407 run, both arms produced identical model requests, made two calls, and passed on the first tested edit. Neither requested a read, so this pair exercised the ordinary plan and edit path. Unscripted evidence is under out/runs/t0-read-handoff-natural-ad6fd25.

### T0 failure fixes

A developing item whose seeded implementation fails at least three quarters of at least eight owner tests can restructure immediately. When a candidate reduces the failures to one, narrow the scope back to edits and ask the worker to trace the single failing input and preserve a nearby passing case. If a candidate crashes before reporting a verdict, keep the best tested revision and show the worker the crash from the rejected candidate. These rules keep the repair on the best available canvas revision.

The tester prompt asks for contract-backed expectations and directs the worker to inspect existing implementation for behavior the contract leaves open. The cut-off file prompt directs the worker to preserve the completed prefix, finish the open scopes, and match the public entry point to its header. The T0 fixtures, owner tests and token ceilings stay fixed for the matched failure replays.

A refused transaction keeps the current revision's sole failing assertion in view when space permits. Early replays used a 640-token cap for visible plans. The follow-up uses the ordinary reply token budget for planning and aims for about 150 words on the current step. The actor can expand detail where the next edit needs it and record later work and dependencies. A clipped plan hands the available text to the act turn without consuming a tested edit. The carried plan is bounded by the view budget so requested source retains room. Read verdicts place the next action above the source so a long read cannot hide the read-limit instruction.

The nine original failure seeds replayed on build f560796. Three passed the behavior gate within budget, all on their first behavior check. Two passed on their first candidate, and half-written passed on its second candidate after a compile failure. The two first-candidate passes had complete usage below the warnings. The half-written run reported 43,479 PP and 5,651 TG, with usage missing for three canceled calls.

| Case | Seeds | Replay outcome |
| --- | --- | --- |
| write-feature | 5101 | Passed, 21,415 PP and 1,468 TG |
| write-tests | 5102 | Passed, 5,782 PP and 850 TG |
| half-written | 5103 | Passed, cost evidence incomplete |
| wrong-approach | 5101, 5102, 5103 | Hard PP stop before a behavior check, hard TG stop at 15/16, then compile failure on the third seed |
| skeleton | 5101, 5102, 5103 | 15/16, 11/16, 15/16. Seeds 5101 and 5103 missed unary-minus precedence, while seed 5102 failed five function calls |

The matched replay, per-run conversations, and staged candidates are under out/runs/t0-failure-replays-f560796. This is a failure cohort. The full 78-run result and T1 admission require a separate full tier run.

Build 5640103 replayed three remaining failures and one passing control. The fix-boundary control passed on its first candidate and behavior check. Skeleton seed 5102 improved from 11 to 15 passing owner checks. Wrong-approach seed 5103 moved from a compile failure to 15 passing checks. Both still failed unary-minus precedence. Skeleton seed 5101 passed 14 of 16 and missed right associativity as well as unary-minus precedence. Both bounded plans and repeated reads appear in the trace. All four runs stayed below their token warnings and reported complete usage. The follow-up evidence is under out/runs/t0-failure-followup-5640103.

### Folded source syntax experiment

The standard view shows the numbered signature, then a marker such as `... (17 lines folded: 45-61; power)`. The worker can expand it with `=== READ: src/calc.cpp SYMBOL power ===` or `=== READ: src/calc.cpp LINES 45-61 ===`. The code under a fold still exists in the current revision.

SUB0SQUAD_FOLDS=compact shows `... B5 ...` under the same signature. SUB0SQUAD_FOLDS=compact-count shows `...(B5@5lines)...` and gives the hidden line count. In either variant, the worker sends `READ B5` on its own line, or several such lines in one reply. Each label resolves to the exact hidden range in the current prompt. Labels can change when the view reflows. The same read allowance, edit protocol, owner checks and token ceilings apply in all three arms. When the allowance is exhausted, the harness replaces the read instructions with the next available action.

Compare paired seeds on behavior and candidate pass ordinals, PP and TG, number of read requests answered or refused, and whether the first edit after a read used the expanded source. Check marker copies in edits and unknown labels in replies. Use a numbered trace to tell a read-selection error from a bad repair made with adequate source.

The Windows qwen35 comparison used skeleton seed 5102. All three runs stayed below the token warnings and reported complete usage.

| Display | Behavior | Read replies answered | PP | TG |
| --- | --- | ---: | ---: | ---: |
| `... (N lines folded: a-b; name)` | 16/16, second candidate and behavior check | 2 | 34,713 | 4,223 |
| `... B5 ...` | 15/16 | 0 | 32,666 | 5,074 |
| `...(B5@5lines)...` | 15/16 | 0 | 31,299 | 4,933 |

The standard worker read the parser bodies, repeated an existing implementation, then repaired unary-minus precedence after the refusal kept that assertion visible. Both compact workers edited without requesting expansion. The native tests verified hidden-range retrieval for both displays, rejection of an unknown label and option propagation through the daemon. The initial attempted compact run repeated the standard result because the worker shim filtered the switch. The reported compact runs used a build that forwards the switch and validated the marker in the actual source view. Evidence is under out/runs/t0-fold-compare-b25f9f5/standard and out/runs/t0-fold-compare-a46b2cf.

Keep the standard display as the default. The comparison used one seed and changed both the marker and the read command. A follow-up can keep the read command fixed and vary only the marker, then measure whether requested reads and accepted repairs improve across seeds.



### Workflow extension seams

Backlog a run-scoped extension contract for composing individual workflows and the prompts they send. Start from two consumers: the experiment runner needs
to swap one instruction in a paired run, and a workflow provider needs to add, replace or remove a capability together with its instructions. Derive the
initial insertion points from the actual think, plan, read, edit and recovery transitions. Name each slot, its stage, and the context it receives. A
provider can contribute a bounded fragment or replace a named instruction. Order contributions deterministically and reject conflicting replacements.
Keep optional capability instructions paired with the registered parser or executor, so removing a capability also removes its example and fallback
guidance. Preserve required stage, policy and budget instructions at each insertion point.

Use a versioned run manifest to identify the base workflow, provider, configured slots, enabled capabilities and the full rendered prompt hashes. Archive
the sent messages with the trace and expose a before/after prompt diff for experiment review. Keep workflow configuration local to the run so concurrent
experiments and the default workflow remain independent. Owners' fixtures and private acceptance checks stay outside provider control.

The first receiving slice should be a small experiment provider that adds one compact-read capability, replaces its usage instruction, and can remove
both together. Verify the default prompt is byte-identical when the extension is absent, the selected workflow exposes the capability in the intended
stages only, and the trace records changes through plan, read and edit. Run a paired replay with a real read, verify the edit reached the existing
checker and record the token difference. Add broader provider loading and cross-workflow propagation when a second consumer demonstrates a need.

### Earlier experiment prompt audit

Review the sent system message in the per-turn conversation. The archived compact-fold run's prompt.txt contains the base standard-fold system text.
Its conversation.md records the actual compact marker and READ B5 instructions after the per-turn rewrite. The worker received the intended format.
The old turn also carried a generic demand for a transaction and a later plan-only directive. The two compact results therefore screen the combined
marker, read command and stage guidance on a single seed. Keep the standard default and reopen the compact comparison with one unambiguous stage
directive. First vary display with the read command fixed, then isolate the command and the stage guidance in separate paired rounds. Verify whether
each requested read arrived and whether the next candidate used it.

The archived precedence replay also showed an edit request colliding with a fresh plan-only directive. Before treating added context as ineffective,
represent post-read stage and evidence delivery as separate factors. Use the archived cases and exact owner checks, verify the sent system and user
messages agree on the next action, and log first/second candidate and behavior passes, readable evidence, prefill, generation, and retries. The existing
read-handoff results are bounded screens, including the unscripted pair that never reached a read. Keep upcoming comparisons in the experiment
method's three-round protocol.

### Precedence crash replay

A final standard-display replay used build a46b2cf and the same budgets. Skeleton seed 5101 first failed right associativity and unary-minus precedence. Its next tested edit fixed right associativity. Wrong-approach seed 5103 reached the same single precedence failure. Both then crashed on a proposed repair and restored the revision that passed 15 of 16 tests. Skeleton reported 37,560 PP and 4,925 TG. Wrong-approach reported 43,437 PP and 5,287 TG, with one canceled call missing usage. Neither run crossed an observed warning threshold.

A separate GDB run used the exact crashing source revisions and the harness's debug compile flags. On `-2^2`, skeleton revision 4 alternated between `factor()` at line 77 and `power()` at line 83 with cursor 0. Wrong-approach revision 3 alternated between `parse_unary()` at line 259 and `parse_power()` at line 263 with cursor 1. The top frame of the second crash was in the C++ library. The parser frames and cursor located the call cycle in the candidate. Both restored revisions returned 4 for the same input, where the test expects -4.

The gate reports, source revisions, SHA-256 manifests and bounded backtraces are under out/runs/t0-precedence-final-a46b2cf. These two traces give the canvas receiving slice a concrete lookup in each direction. Starting with the gate crash, show the candidate revision and the recurring parser call sites. Starting with either call site on that revision, find the crashing test executable and the input used to reproduce it. The earlier unary-minus assertion provides the input for this replay. The original crashing gate did not identify an individual assertion.

### Gate preflight and prompt fairness

The near-pass repair check asks the worker to run the failing input through its proposed code and verify that any call cycle consumes input or advances state toward a stopping condition. Apply it after the developer has produced a compiling edit with one measured failure. The check uses the task's evidence and travels with the edit request after reads and refusals.

The preflight found a calculator division example in the shared edit protocol. It overlapped the missing-check case, so the batch on 987cb01 was stopped and its results were retained separately. Shared SEARCH and LINES examples now change a label in an unrelated file. The generic C++ CLI examples demonstrate argument quoting with an unrelated command. The T0 case definitions and T1 owner tests remain frozen. The T0 judge also requires every declared deliverable to exist in the integrated project before counting a model pass.

The independent preflight rebuilt the fixed seeds and three reference controls. The compiler or owner tests rejected all 25 implementation seeds. The three reference controls passed 22, 19 and 19 checks. The remaining case asks the worker to write tests against a working implementation. An archived generated suite passed all 16 checks on that implementation and rejected the version with inverse-trig degree conversion removed. These checks establish that the frozen cases distinguish those defects. The accepted model build still has to pass the full T0 gate. A different task and broader independent checks remain separate controls for generalization. Preflight logs are under out/runs/t0-fairness-preflight-20261120.

The frozen full run on 5e0a981 completed all 78 trials. It recorded 62 behavior passes, 49 first-candidate passes and 12 second-candidate passes. The gate found 12 hard token failures and four runs with incomplete cost evidence. T1 refused the failed report. A runner interruption after 50 trials was recovered on the same pinned executable. The attempt that ran without its live monitor was archived outside the scored directories and replayed with its original seed. The archived report and token policy are under out/runs/t0-full-5e0a981.

## Token cost gate

The T0 workload KPIs are PP (reported prompt tokens) and TG (reported generated tokens), summed across calls and attempts. The report identifies requests with missing usage. Keep model, tokenizer and cache settings fixed when comparing counts. The qwen35 calibration has caching disabled. System-dependent prefill or time-to-first-token measurements remain in the per-run JSON diagnostics. The token gate uses counts alone.

The qwen35 policy is frozen in [t0-token-budget.json](../../validation/tiers/t0-token-budget.json). It uses the highest observed costs among the 69 in-budget behavior passes on Windows build 7ec8209, separately for each case and metric. The warning threshold adds 25%, with minimum allowances of 2,048 PP and 256 TG tokens. The initial campaign used a hard ceiling with 50% headroom and minimum allowances of 4,096 PP and 512 TG tokens. The follow-up raises hard headroom provisionally to 125%, with minimum allowances of 8,192 PP and 1,024 TG. The baseline and warning allowances are unchanged. Percentage allowances round up. Cases without a matching passing baseline use provisional suite limits. The suite maxima include observed lower bounds from three runs with interrupted usage. Recalibrate explicitly when changing the model profile or when a wider passing cohort is available.

| Threshold | Fallback PP | Fallback TG | Effect |
| --- | ---: | ---: | --- |
| Warning | 49,010 | 8,044 | Report the overrun and continue the run |
| Original hard ceiling | 58,812 | 9,653 | Applied to the first full run |
| Provisional hard ceiling | 88,218 | 14,479 | Fail the cost gate and cancel a live T0 workflow |

The runner polls completed-call usage across attempts once a second and saves a cost-stop record when the observed total exceeds either hard cap. A request can already be in flight at cancellation. The existing per-reply and wall-clock limits still bound that gap. Missing usage beneath the hard limit leaves the cost verdict incomplete. Behavior and pass ordinals remain separate. T1 admission uses the T0 verdict, and the T1 single-run report exposes PP and TG without applying the T0 case limits.

The revised 78-run backtest found seven hard cost failures, one warning with complete usage, and four runs with incomplete usage. One of the incomplete runs also crossed the warning threshold. The native Windows replay with a scripted engine verified early cancellation. Evidence is in out/runs/t0-token-budget-backtest-7ec8209 and out/runs/t0-token-hard-stop-native. Run the focused checks with python -B -m unittest discover -s validation/tiers -p test_gate_t0.py.

## Extra turns when a repair is making progress

Keep first-pass acceptance as a KPI. Compare the fixed N-turn gate with a separate experiment that declares a hard N+1 limit before the run. At the end of N, grant one additional tested edit on the same canvas when recent owner checks show a meaningful improvement and the worker can name the next local edit and its check. Record ordinary passes, extension grants, extension passes and denied requests. Keep the best tested revision and the total model-call count, including calls that produced no candidate.

Qualifying evidence is a strict reduction in the failing owner assertions on a comparable check, or a repaired build that exposes a new, actionable failure. A different error string or a smaller diff alone is insufficient. If the last check repeated the same assertion, require evidence for a different repair, such as a bounded scratch check that changes the failing behavior and preserves nearby passing cases. Evaluate eligibility using only evidence recorded before the extension. Stop at the hard cap or when the evidence points back to the same attempted fix.

The archived write-tests run reduced two failing assertions to one, repeated the last failure, then passed on a fresh worker attempt. The skeleton seed 5103 recovered from compilation errors and later passed, but its intermediate changes only changed the symptom of the same precedence assertion. Wrong-approach seed 5103 spent three attempts on that assertion and briefly regressed from one failure to five. Skeleton seed 5101 repeated the same failing value across two attempts. These cases are controls for comparing the extension with the fixed budget. The late passes show that recovery was possible. They do not show that one additional tested edit would have been enough.

## Runtime evidence on the canvas

The C++ trace spike recorded failing and passing stack traces, a cursor watch and a 12-input matrix on the archived arithmetic candidate. Evidence stays in out/runs/t0-trace-spike-20261119. The Sub0Canvas runtime evidence brief (docs/sessions/canvas-runtime-evidence.md in that repository) places the first receiving slice here. Start with a trace reference on the failing issue and retrieve a bounded detail on request. Preserve the source revision that ran even after the canvas changes. The gate remains the initial view. The [on-demand C++ replay](../BLOCK_PROVING_GROUNDS.md#on-demand-trace-retrieval-in-the-c-repair) verified retrieval and revision binding in the spike. It found no acceptance gain from adding trace detail to this arithmetic candidate.

## Asynchronous scratch checks

Start with one speculative candidate in a Tier 0 work item. Apply a normal transaction to an isolated canvas snapshot and enqueue the existing compile/test gate. Return a candidate ID and base revision immediately. The existing precheck uses a shared candidate directory within an attempt. A scratch check needs an isolated path and a separate verdict destination before overlap is safe. A later turn can ask for status and get queued, running, passed, failed or stale with bounded diagnostics. Promotion requires explicit selection, a passed check and a matching base revision. Run ordinary acceptance checks on promotion. Bound concurrent checks, cancel with the attempt and discard scratch artifacts when the attempt ends.

The worker sees the candidate, evidence and promoted edit as distinct steps. Deliver feedback at a tool or turn boundary and measure overlap with independent generation. Feedback injected into generation already underway would need a different inference mechanism. Prototype the exchange with thinking off and on using the same checks. Add a Sub0Canvas API only if the worker exposes a product-neutral requirement the current snapshot and transaction APIs cannot meet.

## Receiving gates

Prove revision isolation, stale result rejection, cancellation and selection with deterministic fixtures and a real compiler on replace-function. Then compare acceptance, time to accepted edit, repeated reasoning and concurrent checking on the shared engine during an owner-coordinated run. Tier 1 waits for the full Tier 0 gate.
