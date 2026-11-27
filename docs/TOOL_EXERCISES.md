# Running the tool-call exercises

These local experiments measure whether a model emits the calls in the five-operation design in [TOOL_SURFACE.md](TOOL_SURFACE.md). They do not run
the emitted calls or change project files. The JSON exercises currently cover one address per operation, not the full design. The model server is
shared: check with its owner before using it, and do not restart or retune it. All requests below are serial.

Run from the Sub0Squad repository root in PowerShell. Python 3 is required. This example uses the owner's endpoint and model; replace both with the
actual server and model. Every output directory must be new. The timestamp makes the example safe to paste again.

```powershell
$runId = Get-Date -Format 'yyyyMMdd-HHmmss'
$outDir = "out/runs/tool-exercises-$runId"
$modelName = 'Inkling 2 Mini'
$endpoint = 'http://127.0.0.1:8092/v1/chat/completions'

# Check the server's total slots and current activity. A busy slot is not a reservation.
Invoke-RestMethod http://127.0.0.1:8092/props | Select-Object model_alias, total_slots
Invoke-RestMethod http://127.0.0.1:8092/slots | Select-Object id, is_processing, state

# Run matched seeds against the baseline and the revised prompt.
python -B validation/tiers/run_t0_contract.py --out "$outDir/baseline" --model $modelName --url $endpoint --samples 3 --seed 9201 --max-tokens 1024
python -B validation/tiers/run_t0_contract.py --out "$outDir/v2" --model $modelName --url $endpoint --samples 3 --seed 9201 --max-tokens 1024 --prompt validation/tiers/tool_prompt_v2.txt

# Inspect the paired KPI and the full requests, raw responses, finish reasons and token usage.
python -B validation/tiers/compare_t0_contract.py "$outDir/baseline/manifest.json" "$outDir/v2/manifest.json"
Get-Content "$outDir/v2/manifest.json" -Raw | ConvertFrom-Json | Select-Object status, kpi, results
```

The comparator refuses incomplete runs or different models, seeds, sampling settings, tasks or expected answers. It reports exact matches, paired
improvements and regressions, and seeds where every operation passed. Three seeds are a diagnostic, not a statistically strong comparison. Increase
`--samples` equally in both arms. `--heldout` selects a second fixed set; pass it to **both** arms. These cases were authored during this session,
after inspecting earlier results: they are a stress set, not an independent blind evaluation. Use fresh, independently authored cases for adoption.

An incomplete request or output-limit response is retained in the manifest and does not pass. Start a new output directory to retry. Prompt files can
be supplied with `--prompt`; record the reason for each revision and compare on the same seeds. Results are under ignored `out/` and are local to this
machine: archive or transfer them explicitly when evidence must be retained. The current checker requires one exact JSON object; it checks syntax,
operation and exact arguments, **not** effect, execution, safety or the complete design. A successful example does not authorize adopting a prompt.

For the exploratory model-authored-prompt sequence (draft, embed our definitions, self-check, bounded revision and sample calls):

```powershell
python -B validation/tiers/t0_self_format.py --out "$outDir/self-format" --model $modelName --url $endpoint --samples 3 --max-tokens 4096
python -B validation/tiers/t0_self_format_check.py "$outDir/self-format/manifest.json"
```

The self-check is the model's own claim; the separate checker only recognizes strict JSON after a native marker or literal keyword-call syntax. It
does not repair malformed output, execute it, or declare other syntax wrong. Read the manifest and inspect unparsed replies. To replay a complete
candidate from an earlier manifest on direct examples, add `--prompt-from <manifest.json>` and choose a **new** output directory; this skips
generation and self-check, and does not itself establish that the candidate was accepted.

Run the model-free scorer controls before changing the evaluator:

```powershell
Push-Location validation/tiers
python -B -m unittest test_t0_contract test_t0_tools test_t0_self_format
Pop-Location
```
