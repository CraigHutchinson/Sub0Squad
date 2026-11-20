#!/usr/bin/env bash
# Run a model tier above tier 0, only on a build whose tier 0 gate passed on the same engine (docs/BLOCK_PROVING_GROUNDS.md, "Model tiers and the tier 0 gate").
#   validation/tiers/run_tier.sh <repo> <tier: 1|2> <engine spec> [--force "why"]
# TIER_WAIT_SECONDS waits, writes the run report and stops its daemon. TIER_OUT selects a fresh run directory.
#     tier 1: one worker builds the whole calculator on the canvas (validation/tiers/t1-plan.json, process "single": no division, no forked sections)
#     tier 2: the same plan with forked sections (executor fork, sections allowed): the skeleton, parallel sections, the harness's assembly
# Without a passing gate the tier is refused (exit 3) and says why: a tier above a failing tier 0 measures the failure tier 0 already shows. --force runs it
# anyway (an explicit request) and records the reason in the run's home (forced.txt), so the result is never mistaken for a gated one.
set -u
repo=$(cd "$1" && pwd); tier=$2; engine=$3; shift 3
force=""; [ "${1:-}" = "--force" ] && force=${2:?"--force needs a reason"}
. "$repo/validation/tiers/lib.sh"
exe="${T0_EXE:-$repo/out/build/mingw/sub0squad.exe}"
gate_dir="${T0_OUT:-$repo/out/runs/t0}"
name=${engine%%=*}
case "$tier" in
  1) process=single; executor=canvas ;;
  2) process=forked; executor=fork ;;
  *) echo "tier $tier is not defined (1: single worker, 2: forked sections)"; exit 2 ;;
esac
if [ -f "$gate_dir/cost-mode.txt" ] && [ "$(cat "$gate_dir/cost-mode.txt")" = measure ]; then
  gate=2
  verdict="MEASUREMENT ONLY"
else
  T0_OUT="$(winpath "$gate_dir")" python -I "$repo/validation/tiers/gate_t0.py" "$(winpath "$repo")" --engine "$name" --exe "$(winpath "$exe")" >/dev/null; gate=$?
  case "$gate" in 0) verdict=PASS ;; 1) verdict=FAIL ;; 2) verdict=INCOMPLETE ;; *) verdict="UNREADABLE (gate exit $gate)" ;; esac
fi
if [ "$gate" -ne 0 ]; then
  if [ -z "$force" ]; then
    report=gate.md
    [ "$verdict" != "MEASUREMENT ONLY" ] || report=gate-measure.md
    echo "tier $tier refused: the tier 0 gate is $verdict for this build on engine '$name' ($gate_dir/$report)."
    echo "Run tier 0 first: validation/tiers/run_t0.sh $repo '$engine' 3   (or pass --force \"why\" to run tier $tier anyway)"
    exit 3
  fi
  echo "tier $tier FORCED past a $verdict tier 0 gate: $force"
fi
stamp=$(date +%Y%m%d-%H%M%S)
home=${TIER_OUT:-$repo/out/runs/t$tier/$name-$stamp}
case "$home" in
  [a-zA-Z]:*) home=$(cygpath -u "$home") ;;
  /*) ;;
  *) home="$repo/$home" ;;
esac
mkdir -p -- "$(dirname "$home")" || exit 5
if ! mkdir -- "$home"; then
  echo "tier $tier: choose a fresh TIER_OUT ($home)" >&2
  exit 5
fi
[ -n "$force" ] && printf 'forced past a %s tier 0 gate: %s\n' "$verdict" "$force" >"$home/forced.txt"
[ "$verdict" = "MEASUREMENT ONLY" ] || cp "$gate_dir/gate.json" "$home/t0-gate.json" 2>/dev/null
if ! start_squad "$exe" "$home" "$engine" "$executor" "$process" "$repo/validation/tiers/t1-plan.json" "tier $tier proving run"; then
  echo "tier $tier workflow startup failed at $home" >&2
  "$exe" down >/dev/null 2>&1
  exit 5
fi
echo "tier $tier running in $home (gate: $verdict${force:+, forced})"
echo "watch: python validation/tiers/watch_run.py '$(winpath "$home")' --exe '$(winpath "$exe")' --wait 30"
if [ -n "${TIER_WAIT_SECONDS:-}" ]; then
  state=$(wait_workflow "$exe" "$TIER_WAIT_SECONDS")
  "$exe" down >/dev/null 2>&1
  python -I "$repo/validation/tiers/gate_t0.py" "$(winpath "$repo")" --engine "$name" --report-run "$(winpath "$home")" >"$home/gate-result.json"
  result=$?
  echo "tier $tier state=$state; report: $home/gate-result.json (exit $result)"
  if [ "$state" != succeeded ]; then exit 1; fi
  exit "$result"
fi
