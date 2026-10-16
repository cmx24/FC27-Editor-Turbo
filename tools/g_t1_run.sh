#!/usr/bin/env bash
# g_t1_run.sh - gate G-T1 (PLAN_tactics_settings.md section 2): do EXTREME length_in / width_in values change the team shape on the pitch?
# Coordinator only (LTEST, never saved, NO to the team-sheet prompt, never Escape). Agents run only --mock.
#
#   bash g_t1_run.sh hub [--length 0.4] [--width 0.5]
#        In the LTEST hub, AFTER Competitions > Gameplay tuning has located. Read-only checks (g_t1_write check --expect-vanilla), then the
#        B precondition: both extremes written (identity, owner rule, read-back, rollback of the first on a failure) + marker B.
#        PASS -> the game stays up for the Tactical View kick-off, then `g_t1_run.sh match` at once. Any other exit kills the game.
#   bash g_t1_run.sh match [--length 0.4] [--width 0.5] [--since-ko S] [--win 60] [--vanilla length=M,width=M]
#        Right after the kick-off. Extremes still in place (else the hub write did not reach the match), match_measure --check (20 s), then
#        B W s | restore | A W s in ONE process (g_t1_write.py schedule), final check (vanilla), KILL, mm_stats --gt1 -> OUTCOME + VERDICT.
#        --vanilla (or env G_T1_VANILLA) = the vanilla with-ball means from TEST_24's control windows; without it the analysis is INCONCLUSIVE.
#   bash g_t1_run.sh analyse RUN_DIR [--vanilla ...]    mm_stats --gt1 again on a finished run (offline)
#   --mock (before the phase): the whole flow offline over a synthetic engine (X1_TIME_SCALE, X1_MOCK_ENGINE live|once|none|b1only,
#        X1_MOCK_LATENCY). The kill is NOT sent in mock mode (recorded in $G_T1_DIR/mock/killed.txt). The synthetic engine reacts to
#        the width knob only, so a mock run ends INCONCLUSIVE (length never compresses): it proves the flow, not the effect.
# Refuses (exit 3, nothing sent, nothing killed) unless TOOLS\GAME_STATE.txt names a live state (while CLOSED the game may be Cassio's).
# Files: $G_T1_DIR (default TO\g_t1): win.txt (marker), <stamp>_<phase>.log, <stamp>_run* (positions.csv, gt1_stats.txt/json).
# Exit: 0 PASS (PASS-live or PASS-setup), 1 FAIL, 3 INCONCLUSIVE.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-python}"
MOCK=0
[ "${1:-}" = "--mock" ] && { MOCK=1; shift; }
PHASE="${1:-}"; shift || true
LENGTH=0.4; WIDTH=0.5; SINCE_KO=30; WIN=60; SETTLE=5; KO_END="${X1_KO_END:-445}"; VANILLA="${G_T1_VANILLA:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    --length) LENGTH="$2"; shift 2 ;;
    --width) WIDTH="$2"; shift 2 ;;
    --since-ko) SINCE_KO="$2"; shift 2 ;;
    --win) WIN="$2"; shift 2 ;;
    --vanilla) VANILLA="$2"; shift 2 ;;
    *) break ;;
  esac
done
TO="${TURBO_OUTPUT_DIR:-C:/FC 27 Live Editor/turbo_output}"
G_DIR="${G_T1_DIR:-$TO/g_t1}"
mkdir -p "$G_DIR"
M="$G_DIR/win.txt"
STAMP="$(date +%Y%m%d-%H%M%S)"
SCALE=1; XW=(); MM=(); MSA=()
if [ "$MOCK" = 1 ]; then
  SCALE="${X1_TIME_SCALE:-0.01}"; export X1_TIME_SCALE="$SCALE"
  MD="$G_DIR/mock"; mkdir -p "$MD"
  [ -f "$MD/mem.json" ] || "$PY" "$HERE/g_t1_write.py" mock-world "$MD" > /dev/null || exit 3
  export TURBO_MOCK_MEM="$MD/mem.json"
  XW=(--mock --log "$MD/turbo_gui.log" --state-base "$MD/g_t1_write_state" --wlog "$MD/g_t1_write.log" --off "$MD/g_t1_write_off.txt")
  MM=(--mock)
fi

say() { echo "[g_t1 $(date +%H:%M:%S)] $*"; }
ssleep() { sleep "$(awk -v s="$1" -v k="$SCALE" 'BEGIN{v=s*k; if (v<0) v=0; printf "%.3f", v}')"; }
T0="$(date +%s.%N)"
vnow() { awk -v t0="$T0" -v k="$SCALE" -v n="$(date +%s.%N)" 'BEGIN{printf "%d", (n-t0)/k}'; }
gw() { "$PY" "$HERE/g_t1_write.py" "$@" "${XW[@]}"; }
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
  say "game killed (post-flight: GAME_STATE CLOSED, md5 of CmMgrC* and settings, postlaunch_teardown.ps1)"
}
verdict() {  # $1 = PASS|FAIL|INCONCLUSIVE, rest = reason
  local v="$1"; shift; [ $# -gt 0 ] && say "$*"; echo "VERDICT $v (g_t1_run $PHASE)"
  case "$v" in PASS) RC=0 ;; FAIL) RC=1 ;; *) RC=3 ;; esac
}
analyse() {  # $1 = positions.csv path or run dir, $2 = output prefix
  local VA=()
  [ -n "$VANILLA" ] && VA=(--gt1-vanilla "$VANILLA")
  "$PY" "$HERE/mm_stats.py" --gt1 --csv "$1" --json "$2.gt1_stats.json" "${VA[@]}" | tee "$2.gt1_stats.txt"
  case "${PIPESTATUS[0]}" in 0) return 0 ;; 1) return 1 ;; *) return 3 ;; esac
}

if [ "$PHASE" = "analyse" ]; then
  RUN="${1:?analyse RUN_DIR}"; shift
  analyse "$RUN" "$RUN"; exit $?
fi
[ "$PHASE" = "hub" ] || [ "$PHASE" = "match" ] || { sed -n '2,24p' "$0"; exit 2; }

if [ "$MOCK" != 1 ]; then
  "$PY" "$HERE/harness.py" state --write || { echo "VERDICT INCONCLUSIVE (g_t1_run $PHASE: refused, nothing sent, nothing killed)"; exit 3; }
  game_running || { echo "FC27.exe is not running: nothing to do"; echo "VERDICT INCONCLUSIVE (g_t1_run $PHASE)"; exit 3; }
fi

LOG="$G_DIR/${STAMP}_$PHASE.log"
exec > >(tee -a "$LOG") 2>&1
RC=3; CHILD=""; KEEP_GAME=0
cleanup() {
  [ -n "${WDOG:-}" ] && kill "$WDOG" 2> /dev/null
  [ -n "$CHILD" ] && kill "$CHILD" 2> /dev/null
  # kill FIRST, no dev call before it: a written value dies with the process
  [ "$KEEP_GAME" != 1 ] && kill_game
  say "log: $LOG"
  exit "$RC"
}
bgwait() { "$@" & CHILD=$!; wait "$CHILD"; local rc=$?; CHILD=""; return $rc; }
trap cleanup EXIT
trap 'say "interrupted"; RC=3; exit' INT TERM
say "phase $PHASE length $LENGTH width $WIDTH mock $MOCK (scale $SCALE) dir $G_DIR"

# ------------------------------------------------------------------------------------------------------------------ hub
if [ "$PHASE" = "hub" ]; then
  if ! gw check --expect-vanilla; then verdict FAIL "identity / value check failed (expected 0.84 / 1.0): no write"; exit; fi
  if ! gw set --length "$LENGTH" --width "$WIDTH" --allow-write --marker "$M" --label B; then
    verdict FAIL "the B write failed (the first knob is rolled back when the second fails)"; exit
  fi
  KEEP_GAME=1
  verdict PASS "length $LENGTH width $WIDTH written in the hub and read back; marker B. Next: overlay off, Tactical View, then at once:"
  say "   bash \"$HERE/g_t1_run.sh\" match --since-ko <seconds since kick-off> --vanilla length=<m>,width=<m>"
  exit
fi

# ------------------------------------------------------------------------------------------------------------------ match
DEADLINE=$((KO_END - SINCE_KO))
( exec > /dev/null 2>&1; sleep "$(awk -v s="${X1_WATCHDOG_S:-$((DEADLINE + 25))}" -v k="$SCALE" 'BEGIN{printf "%.3f", s*k}')"
  echo "[g_t1 $(date +%H:%M:%S)] watchdog: the deadline passed, TERM sent" >> "$LOG"; kill -TERM $$ ) &
WDOG=$!
if ! gw check; then verdict INCONCLUSIVE "identity check failed after kick-off"; exit; fi
RUN="$G_DIR/${STAMP}_run"
say "check: match_measure --check (20 s)"
if ! bgwait timeout "$(awk -v k="$SCALE" 'BEGIN{printf "%d", 150*k+30}')" "$PY" "$HERE/match_measure.py" --check -v --label gt1check \
     --marker "$M" --run-dir "${RUN}_check" "${MM[@]}" > "$G_DIR/${STAMP}_check.log" 2>&1; then
  tail -3 "$G_DIR/${STAMP}_check.log"; verdict FAIL "match_measure --check failed (route C / 22 players / moving)"; exit
fi
tail -2 "$G_DIR/${STAMP}_check.log"
LEFT=$((DEADLINE - $(vnow) - 15))
OVH=9   # one mid-match restore (two writes, about 3 s each) + margin
W=$(( (LEFT - OVH) / 2 )); [ "$W" -gt "$WIN" ] && W=$WIN
if [ "$W" -lt 45 ]; then verdict INCONCLUSIVE "only $LEFT s left before the half-time margin: windows would be $W s (< 45 s)"; exit; fi
say "windows $W s (requested $WIN), $LEFT s left before the half-time margin"
say "in-process schedule: g_t1_write.py schedule (sampler + the restore in ONE process; the x1 live-proven pattern)"
bgwait "$PY" "$HERE/g_t1_write.py" schedule --length "$LENGTH" --width "$WIDTH" --allow-write --win "$W" --run-dir "$RUN" "${XW[@]}" > "$RUN.log" 2>&1
src=$?
grep -a -E "^schedule|-> A|read back|stopped|written:|VERDICT" "$RUN.log" | cut -c1-170
[ "$src" = 3 ] && { verdict INCONCLUSIVE "the schedule refused (B precondition: the hub values are not in place after kick-off, or a guard) - see $RUN.log"; exit; }
[ "$src" = 0 ] || { verdict FAIL "the in-process schedule failed (exit $src, see $RUN.log)"; exit; }
kill "$WDOG" 2> /dev/null
gw check --expect-vanilla || say "WARNING: the final check did not read the vanilla values (they die with the kill below)"
kill_game; KEEP_GAME=1   # killed right after the last window; the analysis needs no game
analyse "$RUN/positions.csv" "$RUN"
arc=$?
say "$(grep -E '^OUTCOME' "$RUN.gt1_stats.txt")"
case "$arc" in 0) RC=0 ;; 1) RC=1 ;; *) RC=3 ;; esac
say "evidence: $RUN.gt1_stats.txt, $RUN.gt1_stats.json, $LOG, g_t1_write.log"
echo "VERDICT $(grep -E '^VERDICT' "$RUN.gt1_stats.txt" | awk '{print $2}') (g_t1_run match)"
exit
