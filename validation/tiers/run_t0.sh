#!/usr/bin/env bash
# Run tier 0 cases on a model, one fresh squad home per run, then judge them (gate_t0.py writes out/runs/t0/gate.md and gate.json).
#   [START=n] validation/tiers/run_t0.sh <repo> <engine spec> <repeats> [case ... | @smoke]      (no case: all; runs are numbered from START, default 1)
# @smoke runs the smoke set (make_t0.py SMOKE: one fast case per kind of edit) and judges it with gate_t0.py --set smoke: smoke.md, never the gate.
# Each run: its own home out/runs/t0/<case>/run-N, the "t0" process (lib.sh: single, no ladder past the local attempts), the canvas executor, the
# case's plan (make_t0.py), T0_ATTEMPTS local attempts (default 1: a run stops at its first failed attempt); then `down`.
# out/runs/t0/summary.md gains one row per run as it ends (a progress log; the gate judges the run directories, not this table).
set -u
repo=$(cd "$1" && pwd); engine=$2; repeats=$3; shift 3
. "$repo/validation/tiers/lib.sh"
exe="$repo/out/build/mingw/sub0squad.exe"
# One loop at a time (2026-11-18: a stopped caller left its loop running, and three loops wrote into out/runs/t0, wiping each other's homes).
lock="$repo/out/runs/t0.lock"
mkdir -p "$repo/out/runs"
if ! mkdir "$lock" 2>/dev/null; then
  if [ -f "$lock/pid" ] && kill -0 "$(cat "$lock/pid")" 2>/dev/null; then echo "run_t0: another loop is running (pid $(cat "$lock/pid"), $lock); stop it first" >&2; exit 4; fi
  rm -rf "$lock"; mkdir "$lock"   # left by a loop that is gone
fi
echo $$ >"$lock/pid"
trap 'rm -rf "$lock"' EXIT
set_args=()
setname=""
case "${1:-}" in @*) setname=${1#@}; shift; set -- $(python -I "$repo/validation/tiers/make_t0.py" "$repo" --list "$setname" | awk '{print $1}'); set_args=(--set "$setname" --min-runs 1);; esac
cases=${*:-$(python -I "$repo/validation/tiers/make_t0.py" "$repo" --list | awk '{print $1}')}
python -I "$repo/validation/tiers/make_t0.py" "$repo" $cases >/dev/null
sum="$repo/out/runs/t0/summary.md"
[ -f "$sum" ] || printf '| case | run | result | attempts | turns | out tokens | seconds | commits | staged | refused | reads | aborted | thinking stops |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n' >"$sum"
for c in $cases; do
  for r in $(seq "${START:-1}" $(( ${START:-1} + repeats - 1 ))); do
    home="$repo/out/runs/t0/$c/run-$r"; rm -rf "$home"
    start_squad "$exe" "$home" "$engine" canvas t0 "$repo/out/runs/t0/$c/plan.json" "tier zero spike: $c" --max-local "${T0_ATTEMPTS:-1}"
    state=$(wait_workflow "$exe" 3600)
    "$exe" down >/dev/null 2>&1
    items="$home/state/workflows/w-000001/items/app"
    row=$(python -I - "$(winpath "$items")" <<'EOF'
import json, pathlib, sys
tot = dict(turns=0, tokensOut=0, seconds=0.0, commits=0, staged=0, refused=0, reads=0, aborted=0, thinkingStops=0)
attempts = sorted(pathlib.Path(sys.argv[1]).glob("attempt-*"))
for a in attempts:
    try: j = json.loads((a / "agent.json").read_text())
    except Exception: continue
    cv = j.get("canvas", {})
    for k in ("turns", "tokensOut", "seconds"): tot[k] += j.get(k, 0)
    for k in ("commits", "staged", "refused", "reads", "aborted", "thinkingStops"): tot[k] += cv.get(k, 0)
print(len(attempts), "|", " | ".join(str(round(v) if isinstance(v, float) else v) for v in tot.values()))
EOF
)
    printf '| %s | %s | %s | %s |\n' "$c" "$r" "$state" "$row" | tee -a "$sum"
  done
done
name=gate; [ -n "$setname" ] && name=$setname
python -I "$repo/validation/tiers/gate_t0.py" "$(winpath "$repo")" --engine "${engine%%=*}" "${set_args[@]}" >/dev/null; echo "tier 0 $name: see $repo/out/runs/t0/$name.md (exit $?)"
