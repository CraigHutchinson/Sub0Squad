# Shared by the tier scripts (source it): start a squad in its own home and hand it a plan, the same way for every tier.

# A path as the native program wants it (Windows form under Git Bash; unchanged elsewhere).
winpath() { cygpath -w "$1" 2>/dev/null || echo "$1"; }

# The processes the tiers run under. single: one worker, one item (no division, no forked sections). forked: one item, its file written in forked sections.
add_process() {   # <exe> <single|forked>
  case "$2" in
    single) "$1" process add single --set build.leaf.allow.decompose=false --set build.leaf.allow.sections=false --describe "one worker, one item: no division, no forked sections" ;;
    forked) "$1" process add forked --set build.leaf.allow.decompose=false --describe "one item, its file written in forked sections" ;;
    # t0: the single process with the ladder's later rungs off (owner, 2026-11-18): tier 0 tests a targeted step, so a run stops at its attempts
    # (run_t0.sh: --max-local, default 1) instead of grinding through card rewrites and strategies into a long failure.
    t0) "$1" process add t0 --set build.leaf.allow.decompose=false --set build.leaf.allow.sections=false --set build.leaf.allow.rearchitect=false \
          --set build.leaf.allow.thinkFirst=false --set build.leaf.allow.escalate=false --set build.leaf.allow.strategies=false --describe "tier 0: one worker, one item, no ladder past the local attempts" ;;
    *) echo "unknown process '$2'" >&2; return 2 ;;
  esac >/dev/null 2>&1
}

# Start a squad in <home> (frontier off: nothing leaves the machine), start a workflow that waits for a plan, and give it <plan>. Leaves
# SUB0SQUAD_HOME exported so the caller can watch it and bring it down. The task text names no known project, so the workflow waits for the plan.
start_squad() {   # <exe> <home> <engine spec> <executor> <process> <plan.json> <task> [more `do` options]
  local exe=$1 home=$2 engine=$3 executor=$4 process=$5 plan=$6 task=$7
  shift 7
  mkdir -p "$home"
  export SUB0SQUAD_HOME=$(winpath "$home")
  "$exe" up --frontier off >/dev/null 2>&1
  add_process "$exe" "$process" || return 2
  (cd "$home" && "$exe" do "$task" --agent llm --engine "$engine" --executor "$executor" --process "$process" "$@" >/dev/null 2>&1)
  sleep 2
  "$exe" project plan w-000001 --file "$(winpath "$plan")" >/dev/null 2>&1
}

# Wait for w-000001 to end (cancelled after <limit> seconds); prints its state.
wait_workflow() {   # <exe> <limit seconds>
  local start=$(date +%s)
  until "$1" project status w-000001 2>&1 | head -1 | grep -qiE "succeeded|failed|cancelled"; do
    sleep 10
    [ $(( $(date +%s) - start )) -gt "$2" ] && { "$1" project cancel w-000001 >/dev/null 2>&1; break; }
  done
  "$1" project status w-000001 2>&1 | head -1 | awk '{print $3}'
}
