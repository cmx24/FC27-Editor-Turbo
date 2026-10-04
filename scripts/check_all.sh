#!/usr/bin/env bash
# Build + Lua suite + native suite (run inside MSYS2: bash scripts/msys.sh "bash scripts/check_all.sh"). Prints one summary line per step.
set -u
cd "$(dirname "$0")/.."
bash turbogui/scripts/build_win.sh > /tmp/check_build.log 2>&1; b=$?
echo "BUILD exit=$b warnings=$(grep -c -i 'warning:' /tmp/check_build.log)"; [ $b -ne 0 ] && grep -i -E "error" /tmp/check_build.log | head -15
bash turbo/tests/run_tests.sh > /tmp/check_lua.log 2>&1
grep -E "^  FAIL" /tmp/check_lua.log | head -15; echo "LUA $(grep TOTAL /tmp/check_lua.log)"
bash turbogui/tests/native/run_native.sh > /tmp/check_native.log 2>&1; n=$?
grep -E "^  FAIL|Segmentation" /tmp/check_native.log | head -15; echo "NATIVE exit=$n $(grep '^RESULT' /tmp/check_native.log | tail -1)"
