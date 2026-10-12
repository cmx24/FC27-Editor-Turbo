#!/usr/bin/env bash
# Runs the Turbo offline test suite (Lua 5.4) against the real FC 27 Live Editor Lua libraries.
# Needs: lua5.4, and the Live Editor "lua/libs" folder copied to ../le27/libs
# Runs may overlap (several worktrees, or one worktree twice): every run makes its Live Editor folders in its own temp
# folder (TURBO_TEST_TMP; default a new folder under TMPDIR or /tmp, removed at the end) and shares nothing else.
set -u
cd "$(dirname "$0")"
[ -f ../le27/libs/v1/live_editor.lua ] || { echo "Live Editor's Lua libs are missing: copy <Live Editor>\\lua\\libs to turbo/le27/libs (see turbo/le27/README.txt)"; exit 2; }
if [ -z "${TURBO_TEST_TMP:-}" ]; then
  TURBO_TEST_TMP="$(mktemp -d "${TMPDIR:-/tmp}/turbo_lua_tests.XXXXXX")" || { echo "run_tests.sh: could not make a temp folder"; exit 2; }
  trap 'rm -rf "$TURBO_TEST_TMP"' EXIT
fi
export TURBO_TEST_TMP
pass=0; fail=0
for t in t*.lua; do
  out=$(timeout 300 "${LUA:-lua5.4}" "$t" 2>&1); code=$?
  echo "$out" | grep -E "^t[0-9]|FAIL|RESULT"
  r=$(echo "$out" | grep RESULT | sed -E 's/RESULT ([0-9]+) passed, ([0-9]+) failed/\1 \2/')
  p=${r% *}; f=${r#* }
  [ -z "$r" ] && echo "  $t: no RESULT line (exit $code); last lines:" && echo "$out" | tail -n 5 | sed 's/^/    /'
  pass=$((pass + ${p:-0})); fail=$((fail + ${f:-1}))
  [ $code -ne 0 ] && [ "${f:-1}" = "0" ] && fail=$((fail+1)) && echo "  $t exited with $code"
done
echo "TOTAL: $pass passed, $fail failed"
[ $fail -eq 0 ]
