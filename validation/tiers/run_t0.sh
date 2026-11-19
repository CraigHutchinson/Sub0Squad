#!/usr/bin/env bash
# Run tier 0 cases on a model, one fresh squad home per run, then judge them (gate_t0.py writes out/runs/t0/gate.md and gate.json).
#   [START=n] validation/tiers/run_t0.sh <repo> <engine spec> <repeats> [case ... | @smoke]      (no case: all; runs are numbered from START, default 1)
# @smoke runs the smoke set (make_t0.py SMOKE: one fast case per kind of edit) and judges it with gate_t0.py --set smoke: smoke.md, never the gate.
# Each run: its own home out/runs/t0/<case>/run-N, the "t0" process (lib.sh: single, no ladder past the local attempts), the canvas executor, the
# case's plan (make_t0.py), T0_ATTEMPTS local attempts (default 1: a run stops at its first failed attempt); then `down`.
# T0_SEED_START: optional base seed, incremented by run number so each case replays the same three seeds.
# T0_DO_ARGS: more `do` options for every run (e.g. "--max-tokens 7000": a reply budget with room to think; the thinking cap is 60 % of it).
# out/runs/t0/summary.md gains one row per run as it ends (a progress log; the gate judges the run directories, not this table).
set -u
plan_only=false
if [ "${1:-}" = --plan ]; then plan_only=true; shift; fi
repo=$(cd "$1" && pwd); engine=$2; repeats=$3; shift 3
T0_DIR=${T0_OUT:-$repo/out/runs/t0}
case "$T0_DIR" in
    [a-zA-Z]:*) T0_DIR=$(cygpath -u "$T0_DIR") ;;
    /*) ;;
    *) T0_DIR="$repo/$T0_DIR" ;;
esac
. "$repo/validation/tiers/lib.sh"
exe="${T0_EXE:-$repo/out/build/mingw/sub0squad.exe}"   # T0_EXE: another build (the release exe may be held by a running portal)
if $plan_only; then
    case "${1:-}" in
      @*) python -I "$repo/validation/tiers/make_t0.py" "$repo" --list "${1#@}"; exit $? ;;
      '') python -I "$repo/validation/tiers/make_t0.py" "$repo" --list; exit $? ;;
      *) printf '%s\n' "$@"; exit 0 ;;
    esac
fi
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
export T0_OUT=$(winpath "$T0_DIR")
python -I "$repo/validation/tiers/make_t0.py" "$repo" $cases >/dev/null || exit $?
sum="$T0_DIR/summary.md"
[ -f "$sum" ] || printf '| case | run | result | attempts | turns | out tokens | seconds | commits | staged | refused | reads | aborted | thinking stops |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n' >"$sum"
for c in $cases; do
  for r in $(seq "${START:-1}" $(( ${START:-1} + repeats - 1 ))); do
    home="$T0_DIR/$c/run-$r"
    if ! mkdir "$home"; then echo "run_t0: existing evidence at $home" >&2; exit 4; fi
    run_engine="$engine"
    if [ -n "${T0_SEED_START:-}" ]; then
      run_engine="$engine,seed=$((T0_SEED_START + r - 1))"
    fi
    if ! start_squad "$exe" "$home" "$run_engine" canvas t0 "$T0_DIR/$c/plan.json" "tier zero spike: $c" --max-local "${T0_ATTEMPTS:-1}" ${T0_DO_ARGS:-}; then
      echo "run_t0: workflow startup failed at $home" >&2
      "$exe" down >/dev/null 2>&1
      exit 5
    fi
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
python -I "$repo/validation/tiers/gate_t0.py" "$(winpath "$repo")" --engine "${engine%%=*}" --exe "$(winpath "$exe")" "${set_args[@]}" >/dev/null; echo "tier 0 $name: see $T0_DIR/$name.md (exit $?)"
