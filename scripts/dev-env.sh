# Source this file (". scripts/dev-env.sh") to point the build at the sibling Sub0* repositories checked out next to this one.
# Nothing here is a machine-specific path: the sibling directory defaults to this repository's parent, and SUB0_GITHUB_DIR_OVERRIDE wins if set.
_s0_root="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
# In a git worktree the siblings are next to the MAIN checkout, not next to the worktree: the parent of the git common directory is the main checkout.
_s0_main="$(cd "$_s0_root" && cd "$(git rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)"
[ -n "$_s0_main" ] || _s0_main="$_s0_root"
export SUB0_GITHUB_DIR="${SUB0_GITHUB_DIR_OVERRIDE:-$(dirname "$_s0_main")}"
for _n in Sub0Pipeline Sub0Pub Sub0Log Sub0ECS Sub0Math Sub0MemPage Sub0TieredCache Sub0HexGrid Sub0Physics; do
  if [ -d "$SUB0_GITHUB_DIR/$_n" ]; then
    export "CPM_${_n}_SOURCE=$SUB0_GITHUB_DIR/$_n"
  fi
done
unset _s0_root _s0_main _n
