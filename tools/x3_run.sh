#!/usr/bin/env bash
# x3_run.sh - gate X3 (decision 4.3): where does Edit Tactic load from? Coordinator only (Part C; never Escape; LTEST, never saved;
# NO to the team-sheet prompt; Edit Tactic / Team Management is the LAST step, then kill). Agents run only --mock.
#   bash x3_run.sh hub     LTEST hub, Tactics card opened once (svc + club logged): x3_readback --phase hub (lists every copy; the
#                          MISMATCH lines before the writes are expected), x3_sentinels --dry-run, --allow-write (tools/x3_plan.json:
#                          map 95, svc+0xA60 85, A0 75, C0 65, X0 55 / Y0 45 when they hold the tactic), x3_readback --phase hub
#                          must PASS. PASS -> the game stays up (overlay off, Edit Tactic); any other exit -> killed.
#   bash x3_run.sh open    Edit Tactic open: x3_readback --phase open -> "SCREEN SOURCE = <copy>" + "X3 BRANCH: ..."; screenshot.
#                          The game stays up for the Q5 step (a "none of the copies" FAIL is a valid outcome, table 4.3 row 25);
#                          killed only when the readout could not run (INCONCLUSIVE).
#   bash x3_run.sh after   after Short Passing + Save Changes: x3_readback --phase after (Q5 value), then KILL - on every exit path.
#   --mock (before the phase): offline; X3_MOCK_SHEETS = sheet world (sheet_copies.mock_world), X3_LOG = its log; the kill is only
#                          recorded ($X3_DIR/killed.txt).
# Refuses (exit 3, nothing sent, nothing killed) unless GAME_STATE names a live state. Exit 0 PASS / 1 FAIL / 3 INCONCLUSIVE.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-python}"
MOCK=0
[ "${1:-}" = "--mock" ] && { MOCK=1; shift; }
PHASE="${1:-}"
TO="${TURBO_OUTPUT_DIR:-C:/FC 27 Live Editor/turbo_output}"
X3_DIR="${X3_DIR:-$TO/x3}"; mkdir -p "$X3_DIR"
RB=(); SN=()
if [ "$MOCK" = 1 ]; then
  RB=(--mock "${X3_MOCK_SHEETS:?}" --log "${X3_LOG:?}")
  SN=(--mock "$X3_MOCK_SHEETS" --log "$X3_LOG" --state "$X3_DIR/x3_state.json" --wlog "$X3_DIR/sentinels.log" --off "$X3_DIR/sentinels_off.txt")
fi
say() { echo "[x3 $(date +%H:%M:%S)] $*"; }
kill_game() {
  if [ "$MOCK" = 1 ]; then echo "killed $(date +%H:%M:%S) phase $PHASE" >> "$X3_DIR/killed.txt"; say "mock: kill recorded (none sent)"; return 0; fi
  "$PY" "$HERE/harness.py" state --write > /dev/null || { say "GAME_STATE is no longer a coordinator session (re-read before the kill): the game is NOT killed"; return 0; }
  MSYS_NO_PATHCONV=1 taskkill /IM FC27.exe /F > /dev/null 2>&1 || powershell -NoProfile -Command "Stop-Process -Name FC27 -Force" > /dev/null 2>&1
  rmdir "$TO/turbo_dev_client.lock" 2> /dev/null
  say "game killed (post-flight C.8: GAME_STATE CLOSED, md5 of CmMgrC*; the sentinels die with the process)"
}
[ "$PHASE" = hub ] || [ "$PHASE" = open ] || [ "$PHASE" = after ] || { sed -n '2,17p' "$0"; exit 2; }
if [ "$MOCK" != 1 ]; then
  "$PY" "$HERE/harness.py" state --write || { echo "VERDICT INCONCLUSIVE (x3_run $PHASE: refused, nothing sent, nothing killed)"; exit 3; }
fi
RC=3; KEEP=0
cleanup() { [ "$KEEP" = 1 ] || kill_game; exit "$RC"; }
trap cleanup EXIT
trap 'RC=3; exit' INT TERM
code() { case "$1" in 0) RC=0 ;; 1) RC=1 ;; *) RC=3 ;; esac; }
word() { case "$RC" in 0) echo PASS ;; 1) echo FAIL ;; *) echo INCONCLUSIVE ;; esac; }
STAMP="$(date +%Y%m%d-%H%M%S)"
case "$PHASE" in
  hub)
    "$PY" "$HERE/x3_readback.py" --phase hub "${RB[@]}" > "$X3_DIR/${STAMP}_hub_before.txt" 2>&1
    grep -E "^  - |VERDICT" "$X3_DIR/${STAMP}_hub_before.txt"
    "$PY" "$HERE/x3_sentinels.py" --dry-run "${SN[@]}" || { code $?; say "dry run did not pass: no write"; echo "VERDICT $(word) (x3_run hub)"; exit; }
    "$PY" "$HERE/x3_sentinels.py" --allow-write "${SN[@]}" || { code $?; echo "VERDICT $(word) (x3_run hub)"; exit; }
    "$PY" "$HERE/x3_readback.py" --phase hub "${RB[@]}" | tee "$X3_DIR/${STAMP}_hub_after.txt" | grep -E "sentinel|VERDICT"
    code "${PIPESTATUS[0]}"
    [ "$RC" = 0 ] && KEEP=1 ;;
  open)
    "$PY" "$HERE/x3_readback.py" --phase open "${RB[@]}" | tee "$X3_DIR/${STAMP}_open.txt" | grep -E "SCREEN SOURCE|X3 BRANCH|VERDICT|  - "
    code "${PIPESTATUS[0]}"
    [ "$RC" != 3 ] && KEEP=1 ;;
  after)
    "$PY" "$HERE/x3_readback.py" --phase after "${RB[@]}" | tee "$X3_DIR/${STAMP}_after.txt" | grep -E "Q5|after Save|VERDICT"
    code "${PIPESTATUS[0]}" ;;
esac
echo "VERDICT $(word) (x3_run $PHASE)"
exit
