#!/usr/bin/env bash
# battery_run.sh - the one-launch check battery for the Turbo 2.5 gameplay-table knobs (AGENT_RULES_25 ship rule: a knob gets a slider only
# after (1) a deterministic engine-read check passes in game AND (2) a visible effect at an extreme value is seen in game).
# COORDINATOR ONLY (brief Part C: LTEST, never saved, NO to the team-sheet prompt, never Escape). Agents run only --mock.
#
#   bash battery_run.sh plan [battery_write.py options]          offline: the window schedule and the knob / observable table (any GAME_STATE)
#   bash battery_run.sh hub  [--knobs FILE] [--owned k=v] [--prewrite k=v --allow-write]
#        In the LTEST hub (GAME_STATE HUB or MATCH-AB). Read-only preflight: build, getter, attribute chain, every listed knob located and
#        identity-checked, current values against the owner rule (declare Turbo's own values with --owned knob=value, e.g. the profile's
#        shape.width_in=0.6, or reset them). --prewrite knob=value (+ --allow-write, MATCH-AB) writes a band knob before kick-off for the
#        set-up-latch test; then pass --prewritten knob=value to `match`. The game stays up. Any failure -> the game stays up as well
#        (nothing was written); this phase never kills.
#   bash battery_run.sh match [--knobs FILE] [--win 35] [--budget 420] [--wait-start 150] [--owned k=v] [--prewritten k=v]
#        Right after the Tactical View kick-off (GAME_STATE MATCH-AB). battery_write.py run waits for the clock (22 players moving), locates
#        and identity-checks the knobs, runs baseline A | extreme E1.. | A ... windows (<= 40 s each), writes between two samples, restores every
#        knob, prints the per-knob table. The game is KILLED when the run ends, on EVERY exit path (trap), before any further dev call.
#   bash battery_run.sh analyse EVIDENCE_DIR                     offline re-judgement of a saved run
#   --mock (before the phase): the whole flow offline over the synthetic world of battery_mock.py (BATTERY_MOCK_WORLD spec, BATTERY_TIME_SCALE);
#        the kill is NOT sent in mock mode (it is recorded in $BATTERY_DIR/mock/killed.txt).
# Refuses (exit 3, nothing sent, nothing killed) unless TOOLS\GAME_STATE.txt names a coordinator state: while it says CLOSED / USER the game on
# this PC may be Cassio's own, so a refusal never kills.
# Evidence: turbo_dev\evidence\<date>\battery_<HHMMSS>\ (plan.json, positions.csv, engine.csv, writes.json, report.txt, result.json, log.txt,
# battery_run.log). Exit: 0 PASS (run complete), 1 FAIL (write / restore / identity failure), 3 INCONCLUSIVE / refused.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-python}"
MOCK=0
[ "${1:-}" = "--mock" ] && { MOCK=1; shift; }
PHASE="${1:-}"; shift || true
TO="${TURBO_OUTPUT_DIR:-C:/FC 27 Live Editor/turbo_output}"
BATTERY_DIR="${BATTERY_DIR:-$TO/battery}"
EVROOT="${TURBO_EVIDENCE_DIR:-C:/FC 27 Live Editor/turbo_dev/evidence}"
STAMP="$(date +%H%M%S)"
case "$PHASE" in
  plan) exec "$PY" -I "$HERE/battery_write.py" plan "$@" ;;
  analyse) exec "$PY" -I "$HERE/battery_write.py" analyse "$@" ;;
  hub|match) ;;
  *) sed -n '2,30p' "$0"; exit 2 ;;
esac
if [ "$MOCK" = 1 ]; then
  EVD="$BATTERY_DIR/evidence/battery_$STAMP"
  MD="$BATTERY_DIR/mock"; mkdir -p "$MD"
  TS="${BATTERY_TIME_SCALE:-0.02}"
  MARGS=(--mock --mock-world "${BATTERY_MOCK_WORLD:-scale=$TS;delay=2}")
else
  EVD="$EVROOT/$(date +%Y-%m-%d)/battery_$STAMP"
  TS=1
  MARGS=()
fi
mkdir -p "$EVD"
LOG="$EVD/battery_run.log"
say() { echo "[battery $(date +%H:%M:%S)] $*"; }
game_running() {
  [ "$MOCK" = 1 ] && return 0
  MSYS_NO_PATHCONV=1 tasklist /FI "IMAGENAME eq FC27.exe" /NH 2>/dev/null | grep -qi "FC27.exe"
}
kill_game() {
  if [ "$MOCK" = 1 ]; then
    echo "killed $(date +%H:%M:%S) phase $PHASE" >> "$MD/killed.txt"; say "mock: game kill recorded (no kill sent)"; return 0
  fi
  "$PY" -I "$HERE/harness.py" state --write > /dev/null || { say "GAME_STATE is no longer a coordinator session (re-read before the kill): the game is NOT killed"; return 0; }
  MSYS_NO_PATHCONV=1 taskkill /IM FC27.exe /F > /dev/null 2>&1 \
    || powershell -NoProfile -Command "Stop-Process -Name FC27 -Force" > /dev/null 2>&1
  rmdir "$TO/turbo_dev_client.lock" 2> /dev/null
  say "game killed (post-flight C.8: GAME_STATE CLOSED, md5 of CmMgrC*, remove this session's opt-ins)"
}

# ------------------------------------------------------------------------------------------------------------------ refusals (no kill)
if [ "$MOCK" != 1 ]; then
  WANT="--write"; [ "$PHASE" = hub ] && WANT=""
  "$PY" -I "$HERE/harness.py" state $WANT || { echo "VERDICT INCONCLUSIVE (battery_run $PHASE: refused, nothing sent, nothing killed)"; exit 3; }
  game_running || { echo "FC27.exe is not running: nothing to do"; echo "VERDICT INCONCLUSIVE (battery_run $PHASE)"; exit 3; }
fi

# ------------------------------------------------------------------------------------------------------------------ hub (never kills)
if [ "$PHASE" = hub ]; then
  "$PY" -I "$HERE/battery_write.py" hub --evidence "$EVD" "${MARGS[@]}" "$@" 2>&1 | tee "$LOG"
  RC=${PIPESTATUS[0]}
  say "hub preflight exit $RC (the game stays up); log $LOG"
  exit "$RC"
fi

# ------------------------------------------------------------------------------------------------------------------ match (kills on every exit)
RC=3; CHILD=""; WDOG=""
cleanup() {
  [ -n "$WDOG" ] && kill "$WDOG" 2> /dev/null
  [ -n "$CHILD" ] && kill "$CHILD" 2> /dev/null
  # kill FIRST, no dev call before it: a written value dies with the process; battery_write.py already restored what it could
  kill_game
  say "log: $LOG  evidence: $EVD"
  exit "$RC"
}
trap cleanup EXIT
trap 'say "interrupted"; RC=3; exit' INT TERM
say "match phase, mock $MOCK, evidence $EVD"
# the watchdog must not hold this script's output pipe
( exec > /dev/null 2>&1; sleep "${BATTERY_WATCHDOG_S:-$(awk -v k="$TS" -v m="$MOCK" 'BEGIN{printf "%.2f", (m ? 1500*k+60 : 900)}')}"
  echo "[battery $(date +%H:%M:%S)] watchdog: the deadline passed, TERM sent" >> "$LOG"; kill -TERM $$ ) &
WDOG=$!
"$PY" -I "$HERE/battery_write.py" run --allow-write --evidence "$EVD" "${MARGS[@]}" "$@" > "$LOG.run" 2>&1 &
CHILD=$!
wait "$CHILD"; RC=$?; CHILD=""
cat "$LOG.run" >> "$LOG"
grep -a -E "^  - |PROBLEM|waiting for the match|^battery evidence|the clock runs|engine chain resolved|REFUSED|located|-> (A|E|P)[0-9]+ at|battery_write:|no frozen|run ended|A/A control|^  E[0-9]|^KNOB|^shape\.|^marking\.|^runs\.|^pressing\.|^lineband\.|BATTERY KNOB|BATTERY SKIPPED|VERDICT|RESTORE FAILED|FAILED" "$LOG.run" | cut -c1-230
kill "$WDOG" 2> /dev/null; WDOG=""
case "$RC" in 0) RC=0 ;; 1) RC=1 ;; *) RC=3 ;; esac
echo "VERDICT $([ $RC = 0 ] && echo PASS || ([ $RC = 1 ] && echo FAIL || echo INCONCLUSIVE)) (battery_run match)"
exit "$RC"
