#!/usr/bin/env bash
# Runs the Turbo offline test suite (Lua 5.4) against the real FC 27 Live Editor Lua libraries.
# Needs: lua5.4, and the Live Editor "lua/libs" folder copied to ../le27/libs
set -u
cd "$(dirname "$0")"
[ -f ../le27/libs/v1/live_editor.lua ] || { echo "Live Editor's Lua libs are missing: copy <Live Editor>\\lua\\libs to turbo/le27/libs (see turbo/le27/README.txt)"; exit 2; }
pass=0; fail=0
for t in t*.lua; do
  out=$(timeout 300 lua5.4 "$t" 2>&1); code=$?
  echo "$out" | grep -E "^t[0-9]|FAIL|RESULT"
  r=$(echo "$out" | grep RESULT | sed -E 's/RESULT ([0-9]+) passed, ([0-9]+) failed/\1 \2/')
  p=${r% *}; f=${r#* }
  pass=$((pass + ${p:-0})); fail=$((fail + ${f:-1}))
  [ $code -ne 0 ] && [ "${f:-1}" = "0" ] && fail=$((fail+1)) && echo "  $t exited with $code"
done
echo "TOTAL: $pass passed, $fail failed"
[ $fail -eq 0 ]
