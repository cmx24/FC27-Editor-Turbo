#!/usr/bin/env bash
# s_cleanup_check.sh - the 2.4/cleanup in-game check, log part (read-only: it reads turbo_gui.log, nothing else; no game, no dev service).
#
# usage: bash tools/s_cleanup_check.sh [turbo_gui.log]      (default: C:/FC 27 Live Editor/turbo_output/turbo_gui.log)
#        bash tools/s_cleanup_check.sh --self-test         (offline, on mock logs in a temp folder)
#
# turbo_gui.log is opened in append mode (win/dllmain.cpp), so it holds every earlier session: the check reads ONLY the lines after the
# LAST "Turbo GUI loading in process" line (this game session). Run it after the coordinator did, in that session's hub:
#   Competitions > Match setup > "Gameplay switches for the next matches": Weather = 3, Set; then Clear on the same row.
# Pass criterion (all three):
#   1. no "game call gamevar set GAMEPLAY_CUSTOMIZATION/<name>" line except the four kept injury names (_USER / _CPUAI): channel A is gone;
#   2. "game call gamevar set OVERRIDE/WEATHER = 3: ok": a kept variable still passes the host's name gate (gv::settable) and is set;
#   3. "game call gamevar clear OVERRIDE/WEATHER: ok": and it is cleared again (the next match is not changed).
# Prints PASS / FAIL / INCONCLUSIVE and exits 0 / 1 / 3 (3 = no log or no session start line).
set -u

check_log() {
  local log="$1"
  if [ ! -f "$log" ]; then echo "INCONCLUSIVE: no log at $log"; return 3; fi
  local start
  start="$(grep -a -n "Turbo GUI loading in process" "$log" | tail -1 | cut -d: -f1)"
  if [ -z "$start" ]; then echo "INCONCLUSIVE: no 'Turbo GUI loading in process' line in $log"; return 3; fi
  local session
  session="$(tail -n +"$start" "$log")"
  echo "session from line $start: $(printf '%s\n' "$session" | head -1 | cut -c1-80)"
  local fail=0
  local stray
  stray="$(printf '%s\n' "$session" | grep -a "game call gamevar set GAMEPLAY_CUSTOMIZATION/" |
           grep -a -v -E "game call gamevar set GAMEPLAY_CUSTOMIZATION/INJURY_(FREQUENCY|SEVERITY)_(USER|CPUAI) =")"
  if [ -n "$stray" ]; then
    echo "FAIL 1: a removed GAMEPLAY_CUSTOMIZATION variable was set in this session:"; printf '%s\n' "$stray" | head -5; fail=1
  else
    echo "ok 1: no channel A variable set in this session"
  fi
  if printf '%s\n' "$session" | grep -a -q "game call gamevar set OVERRIDE/WEATHER = 3: ok"; then
    echo "ok 2: OVERRIDE/WEATHER = 3 set (a kept variable passes the host's gate)"
  else
    echo "FAIL 2: no 'game call gamevar set OVERRIDE/WEATHER = 3: ok' in this session (step not done, or refused: see the toast)"; fail=1
  fi
  if printf '%s\n' "$session" | grep -a -q "game call gamevar clear OVERRIDE/WEATHER: ok"; then
    echo "ok 3: OVERRIDE/WEATHER cleared again"
  else
    echo "FAIL 3: no 'game call gamevar clear OVERRIDE/WEATHER: ok' in this session (press Clear on the Weather row)"; fail=1
  fi
  if [ $fail -eq 0 ]; then echo "PASS"; return 0; fi
  echo "FAIL"; return 1
}

self_test() {
  local d; d="$(mktemp -d)"
  local old='2026-10-11 22:27:15.736  game call gamevar set GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_CPU = 0: ok'
  local start1='2026-10-11 22:00:00.000  Turbo GUI loading in process 111'
  local start2='2026-10-14 10:00:00.000  Turbo GUI loading in process 222'
  local set_w='2026-10-14 10:05:00.000  game call gamevar set OVERRIDE/WEATHER = 3: ok'
  local clr_w='2026-10-14 10:05:10.000  game call gamevar clear OVERRIDE/WEATHER: ok'
  local inj='2026-10-14 10:05:20.000  game call gamevar set GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI = 60: ok'
  local new_a='2026-10-14 10:06:00.000  game call gamevar set GAMEPLAY_CUSTOMIZATION/POS_LINE_WIDTH_USER = 90: ok'
  printf '%s\n' "$start1" "$old" "$start2" "$set_w" "$inj" "$clr_w" > "$d/pass.log"           # old session's channel A line is ignored
  printf '%s\n' "$start1" "$start2" "$set_w" "$new_a" "$clr_w" > "$d/channel_a.log"           # a channel A set in THIS session
  printf '%s\n' "$start1" "$old" "$start2" "$inj" > "$d/no_weather.log"                       # the positive step was not done
  printf '%s\n' "$start2" "$set_w" > "$d/no_clear.log"                                       # set but not cleared
  printf '%s\n' "$old" "$set_w" "$clr_w" > "$d/no_start.log"                                 # no session start line
  local n=0 bad=0 rc
  for case in "pass.log 0" "channel_a.log 1" "no_weather.log 1" "no_clear.log 1" "no_start.log 3" "missing.log 3"; do
    set -- $case
    check_log "$d/$1" > "$d/out.txt" 2>&1; rc=$?
    n=$((n + 1))
    if [ "$rc" -ne "$2" ]; then bad=$((bad + 1)); echo "  FAIL $1: exit $rc, expected $2"; sed 's/^/    /' "$d/out.txt"; else echo "  ok $1 -> exit $rc"; fi
  done
  rm -rf "$d"
  echo "SELF-TEST $([ $bad -eq 0 ] && echo PASSED || echo FAILED) ($n cases, $bad failed)"
  [ $bad -eq 0 ]
}

if [ "${1:-}" = "--self-test" ]; then self_test; exit $?; fi
check_log "${1:-/c/FC 27 Live Editor/turbo_output/turbo_gui.log}"
exit $?
