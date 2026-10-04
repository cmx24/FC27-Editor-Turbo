#!/usr/bin/env bash
# Build + Lua suite + native suite (run inside MSYS2: bash scripts/msys.sh "bash scripts/check_all.sh"). Prints one summary line per step.
# CHECK_LOG_DIR (default /tmp) holds the three logs: give each worktree its own when several checks run at once,
# otherwise one run's summary can read another run's log.
set -u
cd "$(dirname "$0")/.."
L="${CHECK_LOG_DIR:-/tmp}"
mkdir -p "$L"
bash turbogui/scripts/build_win.sh > "$L/check_build.log" 2>&1; b=$?
echo "BUILD exit=$b warnings=$(grep -c -i 'warning:' "$L/check_build.log")"; [ $b -ne 0 ] && grep -i -E "error" "$L/check_build.log" | head -15
bash turbo/tests/run_tests.sh > "$L/check_lua.log" 2>&1
grep -E "^  FAIL" "$L/check_lua.log" | head -15; echo "LUA $(grep TOTAL "$L/check_lua.log")"
bash turbogui/tests/native/run_native.sh > "$L/check_native.log" 2>&1; n=$?
grep -E "^  FAIL|Segmentation" "$L/check_native.log" | head -15; echo "NATIVE exit=$n $(grep '^RESULT' "$L/check_native.log" | tail -1)"
