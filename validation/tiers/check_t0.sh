#!/usr/bin/env bash
# Check every tier 0 case without a model: its seeded workspace must fail the way the case says (a compile error for compile-*, failing tests otherwise),
# and the reference must pass the same tests. Usage: validation/tiers/check_t0.sh <repo> <instruments dir: a cpp-cmake workspace's CMakeLists.txt and tests/helpers>
set -u
repo=$1; inst=$2; work=${3:-$repo/out/runs/t0/check}
cases=${CASES:-$(python -I "$repo/validation/tiers/make_t0.py" "$repo" --list | awk '{print $1}')}
build() {   # build a workspace and run its tests: prints "compile-error" or "N failing of M"
    local ws=$1; shift
    cmake -S "$ws" -B "$ws/build" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null 2>&1 || { echo "configure-error"; return; }
    cmake --build "$ws/build" >"$ws/build.log" 2>&1 || { echo "compile-error: $(grep -m1 -E 'error:' "$ws/build.log" | sed 's/.*error: //' | cut -c1-70)"; return; }
    local out; out=$(cd "$ws/build" && ctest -V 2>&1 | sed -E 's/^[0-9]+: //')
    local fails; fails=$(printf '%s\n' "$out" | grep -cE '^not ok')
    local total; total=$(printf '%s\n' "$out" | grep -cE '^(not )?ok')
    echo "$fails failing of $total: $(printf '%s\n' "$out" | grep -E '^not ok' | sed -E 's/not ok [0-9]+ - //' | tr '\n' ' ' | cut -c1-90)"
}
for c in $cases; do
    ws="$work/$c"; rm -rf "$ws"; mkdir -p "$ws"
    cp -r "$inst/CMakeLists.txt" "$inst/tests" "$ws/" 2>/dev/null; rm -f "$ws"/tests/*.test.cpp
    python -I "$repo/validation/tiers/make_t0.py" "$repo" --check "$c" "$ws" >/dev/null
    printf '%-18s seeded: %s\n' "$c" "$(build "$ws")"
done
for c in two-files write-feature repeat-trig; do   # the reference must pass every test a case gives (the CLI tests, and the factorial tests write-feature adds)
    ws="$work/reference-$c"; rm -rf "$ws"; mkdir -p "$ws"
    cp -r "$inst/CMakeLists.txt" "$inst/tests" "$ws/"; rm -f "$ws"/tests/*.test.cpp
    python -I "$repo/validation/tiers/make_t0.py" "$repo" --check "$c" "$ws" >/dev/null
    cp "$repo/validation/reference/calculator-cpp/src/"*.cpp "$ws/src/"
    printf '%-18s %s\n' "reference ($c)" "$(build "$ws")"
done
