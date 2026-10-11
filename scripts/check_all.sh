#!/usr/bin/env bash
# Build + Lua suite + native suite + Python tool self-tests (run inside MSYS2: bash scripts/msys.sh "bash scripts/check_all.sh"). Prints one summary line per step.
# CHECK_LOG_DIR (default /tmp) holds the logs: give each worktree its own when several checks run at once,
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
# Python tools' self-tests on synthetic data (callname masters importer, language pipeline): a Windows Python with
# openpyxl (PYTHON=..., else python3 / python / py -3). MSYS2's login shell drops APPDATA, where pip --user put openpyxl.
[ -z "${APPDATA:-}" ] && [ -d "/c/Users/${USER:-}/AppData/Roaming" ] && export APPDATA="$(cygpath -w "/c/Users/$USER/AppData/Roaming")"
py=""
for c in "${PYTHON:-}" python3 python "py -3"; do
  [ -n "$c" ] && $c -c "import openpyxl" > /dev/null 2>&1 && { py="$c"; break; }
done
if [ -z "$py" ]; then
  echo "PY skipped: no Python with openpyxl found (set PYTHON=)"
else
  r=""
  for t in import_callname_masters replicate_language_structure; do
    $py "turbo/tools/$t.py" --self-test > "$L/check_py_$t.log" 2>&1; e=$?
    grep -E "^  FAIL|Traceback" "$L/check_py_$t.log" | head -5
    r="$r $t=$(grep -o 'SELF-TEST [A-Z]*' "$L/check_py_$t.log" | tail -1 | cut -d' ' -f2)(exit $e, $(grep -c '^  ok' "$L/check_py_$t.log") ok)"
  done
  echo "PY$r"
fi
