#!/bin/bash
# usage: gate.sh <calc.cpp to test>  -> builds in hk/ws, runs test_eval, prints the harness-style result
set -u
cd "$(dirname "$0")/ws"
cp "$1" src/calc.cpp
mkdir -p build && cd build
(cmake .. -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build . -j2 --target calc 2>&1 | grep -E 'error|warning' -A3 | head -40) > cc.txt 2>&1
if grep -q 'error' cc.txt; then echo "GATE FAIL: the build failed:"; cat cc.txt; exit 1; fi
cmake --build . -j2 2>&1 | grep -E 'error' -A3 | head -20
timeout 20 ./test_eval 2>&1 | head -60
