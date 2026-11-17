#!/usr/bin/env bash
# Run a model tier above tier 0, only on a build whose tier 0 gate passed on the same engine (docs/BLOCK_PROVING_GROUNDS.md, "Model tiers and the tier 0 gate").
#   validation/tiers/run_tier.sh <repo> <tier: 1|2> <engine spec> [--force "why"]
#     tier 1: one worker builds the whole calculator on the canvas (validation/tiers/t1-plan.json, process "single": no division, no forked sections)
#     tier 2: the same plan with forked sections (executor fork, sections allowed): the skeleton, parallel sections, the harness's assembly
# Without a passing gate the tier is refused (exit 3) and says why: a tier above a failing tier 0 measures the failure tier 0 already shows. --force runs it
# anyway (an explicit request) and records the reason in the run's home (forced.txt), so the result is never mistaken for a gated one.
set -u
repo=$(cd "$1" && pwd); tier=$2; engine=$3; shift 3
force=""; [ "${1:-}" = "--force" ] && force=${2:?"--force needs a reason"}
. "$repo/validation/tiers/lib.sh"
exe="$repo/out/build/mingw/sub0squad.exe"
name=${engine%%=*}
case "$tier" in
  1) process=single; executor=canvas ;;
  2) process=forked; executor=fork ;;
  *) echo "tier $tier is not defined (1: single worker, 2: forked sections)"; exit 2 ;;
esac
python -I "$repo/validation/tiers/gate_t0.py" "$(winpath "$repo")" --engine "$name" >/dev/null; gate=$?
case "$gate" in 0) verdict=PASS ;; 1) verdict=FAIL ;; 2) verdict=INCOMPLETE ;; *) verdict="UNREADABLE (gate exit $gate)" ;; esac
if [ "$gate" -ne 0 ]; then
  if [ -z "$force" ]; then
    echo "tier $tier refused: the tier 0 gate is $verdict for this build on engine '$name' (out/runs/t0/gate.md)."
    echo "Run tier 0 first: validation/tiers/run_t0.sh $repo '$engine' 3   (or pass --force \"why\" to run tier $tier anyway)"
    exit 3
  fi
  echo "tier $tier FORCED past a $verdict tier 0 gate: $force"
fi
stamp=$(date +%Y%m%d-%H%M%S)
home="$repo/out/runs/t$tier/$name-$stamp"; mkdir -p "$home"
[ -n "$force" ] && printf 'forced past a %s tier 0 gate: %s\n' "$verdict" "$force" >"$home/forced.txt"
cp "$repo/out/runs/t0/gate.json" "$home/t0-gate.json" 2>/dev/null
start_squad "$exe" "$home" "$engine" "$executor" "$process" "$repo/validation/tiers/t1-plan.json" "tier $tier proving run"
echo "tier $tier running in $home (gate: $verdict${force:+, forced}); watch: SUB0SQUAD_HOME='$SUB0SQUAD_HOME' $exe project status w-000001"
