#!/usr/bin/env bash
# Build + Lua suite + native suite + Python tool self-tests (run inside MSYS2: bash scripts/msys.sh "bash scripts/check_all.sh"). Prints one summary line per step.
# CHECK_LOG_DIR (default turbogui/build/check_logs of this worktree) holds the three logs, so checks in several worktrees can run
# at once without one run's summary reading another run's log. The Lua and native suites run side by side (they share no files;
# CHECK_SERIAL=1 runs them one after the other). Exit code: 0 only when the build and both suites are green.
set -u
cd "$(dirname "$0")/.."
L="${CHECK_LOG_DIR:-$PWD/turbogui/build/check_logs}"
mkdir -p "$L"
where="$L"; command -v cygpath >/dev/null 2>&1 && where="$(cygpath -w "$L")"
echo "logs: $where"
bash turbogui/scripts/build_win.sh > "$L/check_build.log" 2>&1; b=$?
echo "BUILD exit=$b warnings=$(grep -c -i 'warning:' "$L/check_build.log")"; [ $b -ne 0 ] && grep -i -E "error" "$L/check_build.log" | head -15
if [ "${CHECK_SERIAL:-0}" = "1" ]; then
  bash turbo/tests/run_tests.sh > "$L/check_lua.log" 2>&1; l=$?
else
  bash turbo/tests/run_tests.sh > "$L/check_lua.log" 2>&1 &
  lua_pid=$!
fi
bash turbogui/tests/native/run_native.sh > "$L/check_native.log" 2>&1; n=$?
[ "${CHECK_SERIAL:-0}" = "1" ] || { wait "$lua_pid"; l=$?; }
grep -E "^  FAIL|no RESULT line" "$L/check_lua.log" | head -15; echo "LUA exit=$l $(grep TOTAL "$L/check_lua.log" || echo 'no TOTAL line: see check_lua.log')"
grep -E "^  FAIL|^CRASH|Segmentation|^run_native.sh: " "$L/check_native.log" | head -15
echo "NATIVE exit=$n $(grep '^RESULT' "$L/check_native.log" | tail -1)"
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
  # tools/selftest_all.py = the proof harness (WP-C): every tools/*.py --self-test on mocks + the x1_run.sh end-to-end (mock only;
  # it needs this bash: TOOLS_BASH). It never talks to the game.
  command -v cygpath > /dev/null 2>&1 && export TOOLS_BASH="$(cygpath -w "$BASH")"
  for t in turbo/tools/import_callname_masters turbo/tools/replicate_language_structure tools/selftest_all; do
    tn="$(basename "$t")"  # not n: n holds the NATIVE exit code
    $py "$t.py" --self-test > "$L/check_py_$tn.log" 2>&1; e=$?
    grep -E "^  FAIL|Traceback" "$L/check_py_$tn.log" | head -5
    r="$r $tn=$(grep -o 'SELF-TEST [A-Z]*' "$L/check_py_$tn.log" | tail -1 | cut -d' ' -f2)(exit $e, $(grep -c '^  ok' "$L/check_py_$tn.log") ok)"
  done
  echo "PY$r"
fi
# 2.1 asset catalog: the fast standalone runners (g++ only, synthetic data; turbogui/tests/catalog)
c=0; cr=""
for t in run run_callnames run_game_heads run_gear run_pages run_kitswap run_lualoader run_faces3d run_face_tile; do
  bash "turbogui/tests/catalog/$t.sh" > "$L/check_catalog_$t.log" 2>&1 || c=1
  cr="$cr $t=$(grep -E -o '[0-9]+ (passed|checks)' "$L/check_catalog_$t.log" | tail -1 | cut -d' ' -f1)/$(grep -E -o '[0-9]+ failed' "$L/check_catalog_$t.log" | tail -1 | cut -d' ' -f1)"
done
echo "CATALOG exit=$c$cr (ok/failed)"
# 2.22 navigation capability check (tools/nav_capability_check.py): every function the DEFAULT navigation of v2.14.0 reached must still
# be reachable by default (generic names make no call edge; a page in both registries counts its NEW function; the "Use the old pages"
# toggle does not count), minus tools/nav_capability_allow.txt. Symbol scan, no build, no game. Fails check_all when anything is lost;
# NAVCAP_BASE=<ref> changes the baseline, NAVCAP_ADVISORY=1 reports without failing (a track that cannot fix a loss of another track).
pyn=""
for pc in "${PYTHON:-}" python3 python "py -3"; do
  [ -n "$pc" ] && $pc -c "import sys; sys.exit(0)" > /dev/null 2>&1 && { pyn="$pc"; break; }
done
nv=0
if [ -z "$pyn" ]; then
  echo "NAVCAP skipped: no Python found (set PYTHON=)"
else
  $pyn -I tools/nav_capability_check.py --self-test > "$L/check_navcap_self.log" 2>&1; nvs=$?
  $pyn -I tools/nav_capability_check.py --base "${NAVCAP_BASE:-v2.14.0}" > "$L/check_navcap.log" 2>&1; nv=$?
  echo "NAVCAP exit=$nv self-test=$nvs base=${NAVCAP_BASE:-v2.14.0} $(grep '^RESULT' "$L/check_navcap.log" | tail -1) $(grep -E -o '^LOST BY DEFAULT \([0-9]+' "$L/check_navcap.log" | tr -d '(')  (list: $L/check_navcap.log)"
  [ $nvs -ne 0 ] && nv=1
  [ "${NAVCAP_ADVISORY:-0}" = "1" ] && nv=0
fi
# 4.0 S1 one-component lint (turbogui/tests/lint_one_component.py): a page that uses the page template (src/ui/ui_page.h, a function that
# constructs a page::Frame) has no raw BeginTable (registry form tbl:: only), no own undo store, no classic / old / new label; the tree-wide
# raw-table / own-undo / cutover-label lists are ADVISORY (counts only in the line below; list: python turbogui/tests/lint_one_component.py --list).
oc=0
if [ -z "$pyn" ]; then
  echo "LINT-ONE skipped: no Python found (set PYTHON=)"
else
  $pyn -I turbogui/tests/lint_one_component.py --self-test > "$L/check_lint_one_self.log" 2>&1; ocs=$?
  $pyn -I turbogui/tests/lint_one_component.py . > "$L/check_lint_one.log" 2>&1; oc=$?
  echo "LINT-ONE exit=$oc self-test=$ocs $(grep '^ADVISORY' "$L/check_lint_one.log" | tail -1 | cut -c1-200)  (log: $L/check_lint_one.log)"
  [ $ocs -ne 0 ] && oc=1
fi
[ $b -eq 0 ] && [ $l -eq 0 ] && [ $n -eq 0 ] && [ $c -eq 0 ] && [ $nv -eq 0 ] && [ $oc -eq 0 ]
