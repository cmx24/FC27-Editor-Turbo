#!/usr/bin/env bash
# x1_run.sh - gate X1 (docs/TACTICS_OVERHAUL_DECISION.md 4.1): does the engine read the tuning table (width_in), and when?
# Coordinator only (brief Part C: LTEST, never saved, NO to the team-sheet prompt, never Escape). Agents run only --mock.
#
#   bash x1_run.sh hub   [--value 0.6]
#        In the LTEST hub, AFTER Competitions > Gameplay tuning has located (log "locator attrib.attackinglinelengthwidth found").
#        Read-only checks (x1_write check: build, identity, value = FC 27's 1.0; chain_check.py if present; 96 signature bytes at
#        0x141077070), then the B1 precondition: x1_write set 0.6 (identity re-check, owner rule, read-back) + marker B1.
#        PASS -> the game stays up for the Tactical View kick-off (overlay off, TV seq, then `x1_run.sh match` at once).
#        Any other exit -> the game is killed.
#   bash x1_run.sh match [--value 0.6] [--since-ko S] [--win S] [--mode inproc|seq|pause]
#        Right after the Tactical View kick-off (S = real seconds since the kick-off, default 30). Value still 0.6 (else the hub write
#        did not reach the match), match_measure --check (20 s), then B1 W s | revert | A1 W s | set | B2 W s | revert | A2 W s
#        (W = --win, default 90 s, shortened to fit before about 445 s after the kick-off; 5 s 'x' after every write), final check
#        1.0, KILL, mm_stats --x1 -> "OUTCOME 1/2/3" (decision table 4.1) + VERDICT.
#        mode inproc (default, LIVE-PROVEN 2026-10-13 run 3 as x1_cont.py): `x1_write.py schedule` = the sampler and the three writes
#          in ONE process (writes between two samples; no second dev2 client, so no lock starvation).
#        mode seq (live-proven, run 2): one sampler run per window, writes between the runs; costs a ~21 s re-locate per window.
#        mode pause (OFFLINE-PROVEN ONLY, never run live): one continuous sampler paused through a file while a separate x1_write
#          process takes the dev lock.
#        The game is killed on EVERY exit path (trap); the knob is reverted first when the value is not FC 27's.
#   bash x1_run.sh analyse RUN_DIR [more mm_stats args]   mm_stats --x1 again on a finished run (offline)
#   --mock (before the phase): the whole flow offline over a synthetic engine (harness.MockMem; X1_MOCK_ENGINE live|once|none|b1only,
#        X1_TIME_SCALE, X1_MOCK_LATENCY). The kill is NOT sent in mock mode (it is recorded in $X1_DIR/mock/killed.txt).
# Refuses (exit 3, nothing sent, nothing killed) unless TOOLS\GAME_STATE.txt names a live state: while it says CLOSED the game on this
# PC may be Cassio's own, so a refusal never kills.
# Files: $X1_DIR (default TO\x1): win.txt (marker), pause, <stamp>_<phase>.log, <stamp>_run* (positions.csv, x1_stats.txt/json).
# Exit: 0 PASS (outcome 1 or 2), 1 FAIL, 3 INCONCLUSIVE.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-python}"
MOCK=0
[ "${1:-}" = "--mock" ] && { MOCK=1; shift; }
PHASE="${1:-}"; shift || true
VALUE=0.6; SINCE_KO=30; WIN=90; MODE=inproc; SETTLE=5; KO_END="${X1_KO_END:-445}"; WRITE_S=6
while [ $# -gt 0 ]; do
  case "$1" in
    --value) VALUE="$2"; shift 2 ;;
    --since-ko) SINCE_KO="$2"; shift 2 ;;
    --win) WIN="$2"; shift 2 ;;
    --mode) MODE="$2"; shift 2 ;;
    *) break ;;
  esac
done
TO="${TURBO_OUTPUT_DIR:-C:/FC 27 Live Editor/turbo_output}"
X1_DIR="${X1_DIR:-$TO/x1}"
mkdir -p "$X1_DIR"
M="$X1_DIR/win.txt"; PAUSE="$X1_DIR/pause"; STOP="$X1_DIR/stop"
STAMP="$(date +%Y%m%d-%H%M%S)"
SCALE=1; XW=(); MM=(); MSA=()
if [ "$MOCK" = 1 ]; then
  SCALE="${X1_TIME_SCALE:-0.01}"; export X1_TIME_SCALE="$SCALE"
  MD="$X1_DIR/mock"; mkdir -p "$MD"
  [ -f "$MD/mem.json" ] || "$PY" "$HERE/harness.py" mock-x1 "$MD" || exit 3
  export TURBO_MOCK_MEM="$MD/mem.json"
  XW=(--mock --log "$MD/turbo_gui.log" --state "$MD/x1_write_state.json" --wlog "$MD/x1_write.log" --off "$MD/x1_write_off.txt")
  MM=(--mock)
  MSA=(--perm "${X1_PERM:-400}")
  WRITE_S=$(awk -v k="$SCALE" 'BEGIN{printf "%d", 6 + 0.8 / k}')  # a tool start costs about 0.5 s REAL, i.e. 0.5/k simulated s
fi

say() { echo "[x1 $(date +%H:%M:%S)] $*"; }
# sleep in simulated seconds (real = s x SCALE)
ssleep() { sleep "$(awk -v s="$1" -v k="$SCALE" 'BEGIN{v=s*k; if (v<0) v=0; printf "%.3f", v}')"; }
T0="$(date +%s.%N)"
vnow() { awk -v t0="$T0" -v k="$SCALE" -v n="$(date +%s.%N)" 'BEGIN{printf "%d", (n-t0)/k}'; }
marker() { "$PY" "$HERE/harness.py" marker "$M" "$1"; }
xw() { "$PY" "$HERE/x1_write.py" "$@" "${XW[@]}"; }
game_running() {
  [ "$MOCK" = 1 ] && return 0
  MSYS_NO_PATHCONV=1 tasklist /FI "IMAGENAME eq FC27.exe" /NH 2>/dev/null | grep -qi "FC27.exe"
}
kill_game() {
  if [ "$MOCK" = 1 ]; then
    echo "killed $(date +%H:%M:%S) phase $PHASE" >> "$MD/killed.txt"; say "mock: game kill recorded (no kill sent)"; return 0
  fi
  "$PY" "$HERE/harness.py" state --write > /dev/null || { say "GAME_STATE is no longer a coordinator session (re-read before the kill): the game is NOT killed"; return 0; }
  MSYS_NO_PATHCONV=1 taskkill /IM FC27.exe /F > /dev/null 2>&1 \
    || powershell -NoProfile -Command "Stop-Process -Name FC27 -Force" > /dev/null 2>&1
  rmdir "$TO/turbo_dev_client.lock" 2> /dev/null
  say "game killed (post-flight C.8: GAME_STATE CLOSED, md5 of CmMgrC*, remove this session's opt-ins)"
}
verdict() {  # $1 = PASS|FAIL|INCONCLUSIVE, rest = reason
  local v="$1"; shift; [ $# -gt 0 ] && say "$*"; echo "VERDICT $v (x1_run $PHASE)"
  case "$v" in PASS) RC=0 ;; FAIL) RC=1 ;; *) RC=3 ;; esac
}

# ------------------------------------------------------------------------------------------------------------------ analyse
if [ "$PHASE" = "analyse" ]; then
  RUN="${1:?analyse RUN_DIR}"; shift
  "$PY" "$HERE/mm_stats.py" --x1 --csv "$RUN" "${MSA[@]}" "$@"; exit $?
fi
[ "$PHASE" = "hub" ] || [ "$PHASE" = "match" ] || { sed -n '2,30p' "$0"; exit 2; }

# ------------------------------------------------------------------------------------------------------------------ refusals
if [ "$MOCK" != 1 ]; then
  "$PY" "$HERE/harness.py" state --write || { echo "VERDICT INCONCLUSIVE (x1_run $PHASE: refused, nothing sent, nothing killed)"; exit 3; }
  game_running || { echo "FC27.exe is not running: nothing to do"; echo "VERDICT INCONCLUSIVE (x1_run $PHASE)"; exit 3; }
fi

LOG="$X1_DIR/${STAMP}_$PHASE.log"
exec > >(tee -a "$LOG") 2>&1
RC=3; MPID=""; CHILD=""; KEEP_GAME=0
rm -f "$PAUSE" "$STOP"
cleanup() {
  [ -n "${WDOG:-}" ] && kill "$WDOG" 2> /dev/null
  [ -n "$CHILD" ] && kill "$CHILD" 2> /dev/null
  [ -n "$MPID" ] && kill "$MPID" 2> /dev/null
  rm -f "$PAUSE" "$STOP"
  # kill FIRST, no dev call before it (review minor 8): a written value dies with the process; kill_game re-reads GAME_STATE (minor 6)
  [ "$KEEP_GAME" != 1 ] && kill_game
  say "log: $LOG"
  exit "$RC"
}
# long python steps run in the background and are waited for: a TERM (watchdog, Ctrl-C) runs the trap at once (review minor 7)
bgwait() { "$@" & CHILD=$!; wait "$CHILD"; local rc=$?; CHILD=""; return $rc; }
trap cleanup EXIT
trap 'say "interrupted"; RC=3; exit' INT TERM
say "phase $PHASE value $VALUE mode $MODE mock $MOCK (scale $SCALE) dir $X1_DIR"

# ------------------------------------------------------------------------------------------------------------------ hub
if [ "$PHASE" = "hub" ]; then
  if ! xw check --expect 1.0; then verdict FAIL "identity / value check failed (expected FC 27's 1.0): no write"; exit; fi
  if [ -f "$HERE/chain_check.py" ]; then
    say "chain_check.py (read-only, <= 60 s, informative for WP-A1)"
    timeout 90 "$PY" "$HERE/chain_check.py" "${MM[@]}" > "$X1_DIR/${STAMP}_chain_check.log" 2>&1
    say "chain_check exit $? - $(grep -a -E 'VERDICT|PASS|FAIL' "$X1_DIR/${STAMP}_chain_check.log" | tail -1)"
  fi
  if [ "$MOCK" != 1 ]; then
    "$PY" "$HERE/dev2.py" '{"op":"bytes","addr":"0x141077070","len":96}' > "$X1_DIR/${STAMP}_sig_141077070.json" 2>&1
    say "signature material (96 bytes at 0x141077070): $X1_DIR/${STAMP}_sig_141077070.json"
  fi
  if ! xw set "$VALUE" --allow-write --marker "$M" --label B1; then verdict FAIL "the B1 write failed (rolled back if read-back mismatched)"; exit; fi
  KEEP_GAME=1
  verdict PASS "width_in = $VALUE written in the hub and read back; marker B1. Next: overlay off, Tactical View, then at once:"
  say "   bash \"$HERE/x1_run.sh\" match --since-ko <seconds since kick-off>"
  exit
fi

# ------------------------------------------------------------------------------------------------------------------ match
# (KO_END - SINCE_KO) simulated seconds from now = about 445 s after kick-off: 35 s before the half-time whistle of 8-minute halves
DEADLINE=$((KO_END - SINCE_KO))
# the watchdog must not hold this script's output pipe (an orphaned sleep would keep a caller's pipeline open)
( exec > /dev/null 2>&1; sleep "$(awk -v s="${X1_WATCHDOG_S:-$((DEADLINE + 25))}" -v k="$SCALE" 'BEGIN{printf "%.3f", s*k}')"
  echo "[x1 $(date +%H:%M:%S)] watchdog: the deadline passed, TERM sent" >> "$LOG"; kill -TERM $$ ) &
WDOG=$!
if ! xw check --expect "$VALUE"; then
  verdict INCONCLUSIVE "the hub value $VALUE is not in place after kick-off (reset at match setup, or the hub step did not run)"; exit
fi
RUN="$X1_DIR/${STAMP}_run"
say "check: match_measure --check (20 s)"
if ! bgwait timeout "$(awk -v k="$SCALE" 'BEGIN{printf "%d", 150*k+30}')" "$PY" "$HERE/match_measure.py" --check -v --label x1check \
     --marker "$M" --run-dir "${RUN}_check" "${MM[@]}" > "$X1_DIR/${STAMP}_check.log" 2>&1; then
  tail -3 "$X1_DIR/${STAMP}_check.log"; verdict FAIL "match_measure --check failed (route C / 22 players / moving)"; exit
fi
tail -2 "$X1_DIR/${STAMP}_check.log"
LEFT=$((DEADLINE - $(vnow) - 15))
case "$MODE" in  # time the writes / re-locates cost on top of the four windows
  inproc) OVH=9 ;;                                                         # 3 in-process writes, about 3 s each
  pause) OVH=$((3 * (SETTLE + WRITE_S))) ;;                                # 3 tool starts + settle
  seq) OVH=$((3 * (SETTLE + WRITE_S) + 4 * ${X1_LOCATE_S:-25})) ;;         # + a re-locate per window (about 21 s on 10-13)
  *) verdict INCONCLUSIVE "unknown --mode $MODE (inproc | seq | pause)"; exit ;;
esac
W=$(( (LEFT - OVH) / 4 )); [ "$W" -gt "$WIN" ] && W=$WIN
if [ "$W" -lt 45 ]; then verdict INCONCLUSIVE "only $LEFT s left before the half-time margin: windows would be $W s (< 45 s)"; exit; fi
say "windows $W s (requested $WIN), $LEFT s left before the half-time margin"
write_step() {  # $1 = set|revert, $2 = next window label
  local args=("$1"); [ "$1" = set ] && args+=("$VALUE")
  [ "$MODE" = pause ] && : > "$PAUSE"
  if ! xw "${args[@]}" --allow-write --marker "$M" --transition x; then
    rm -f "$PAUSE"; verdict FAIL "the $1 write before $2 failed"; exit
  fi
  rm -f "$PAUSE"
  ssleep "$SETTLE"
  marker "$2"
}
measure_bg() {  # $1 = seconds, $2 = run dir
  rm -f "$STOP"
  "$PY" "$HERE/match_measure.py" --seconds "$1" --label x1 --marker "$M" --pause "$PAUSE" --stop "$STOP" --run-dir "$2" "${MM[@]}" \
    > "$2.log" 2>&1 &
  MPID=$!
}
wait_measure() {
  local t=0
  while kill -0 "$MPID" 2> /dev/null && [ "$t" -lt 600 ]; do sleep 0.2; t=$((t + 1)); done
  wait "$MPID"; local rc=$?; MPID=""; return $rc
}
if [ "$MODE" = inproc ]; then
  say "in-process schedule: x1_write.py schedule (sampler + 3 writes in ONE process; live-proven pattern)"
  bgwait "$PY" "$HERE/x1_write.py" schedule "$VALUE" --allow-write --win "$W" --run-dir "$RUN" "${XW[@]}" > "$RUN.log" 2>&1
  src=$?
  grep -a -E "^schedule|-> (A1|B2|A2)|read back|stopped|written:|VERDICT" "$RUN.log" | cut -c1-170
  [ "$src" = 0 ] || { verdict FAIL "the in-process schedule failed (exit $src, see $RUN.log)"; exit; }
  WINS=(--csv "$RUN/positions.csv")
elif [ "$MODE" = seq ]; then
  for step in "B1:" "A1:revert" "B2:set" "A2:revert"; do
    w="${step%%:*}"; act="${step#*:}"
    [ -n "$act" ] && write_step "$act" "$w"
    marker "$w"; measure_bg "$W" "${RUN}_$w"; wait_measure
    [ -s "${RUN}_$w/positions.csv" ] || { verdict FAIL "window $w: the sampler wrote no positions (see ${RUN}_$w.log)"; exit; }
  done
  WINS=(--win "B1=${RUN}_B1/positions.csv" --win "A1=${RUN}_A1/positions.csv" --win "B2=${RUN}_B2/positions.csv" --win "A2=${RUN}_A2/positions.csv")
else
  measure_bg "$((4 * W + 3 * (SETTLE + WRITE_S) + 30))" "$RUN"   # upper bound; the stop file ends it after A2
  ssleep "$W"; write_step revert A1
  ssleep "$W"; write_step set B2
  ssleep "$W"; write_step revert A2
  ssleep "$W"; marker end; : > "$STOP"
  wait_measure; say "sampler exit $?: $(grep -a -E 'written|paused|resumed' "$RUN.log" | tail -2 | tr '\n' ' ')"
  WINS=(--csv "$RUN/positions.csv")
fi
kill "$WDOG" 2> /dev/null
xw check --expect 1.0 || say "WARNING: the final check did not read 1.0 (the value dies with the kill below)"
kill_game; KEEP_GAME=1  # killed here, right after the last window; the analysis needs no game
BASE=()
[ -n "${X1_BASELINE:-}" ] && BASE=(--x1-baseline "$X1_BASELINE")
"$PY" "$HERE/mm_stats.py" --x1 "${WINS[@]}" --json "$RUN.x1_stats.json" "${BASE[@]}" "${MSA[@]}" | tee "$RUN.x1_stats.txt"
case "${PIPESTATUS[0]}" in 0) RC=0 ;; 1) RC=1 ;; *) RC=3 ;; esac
say "$(grep -E '^OUTCOME' "$RUN.x1_stats.txt")"
say "evidence: $RUN.x1_stats.txt, $RUN.x1_stats.json, $LOG, x1_write.log"
echo "VERDICT $(grep -E '^VERDICT' "$RUN.x1_stats.txt" | awk '{print $2}') (x1_run match)"
exit
