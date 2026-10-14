#!/usr/bin/env bash
# x2_run.sh - gate X2 (decision 4.2): which source feeds the match's LH / BU / DA? Coordinator only (Part C; never Escape; LTEST,
# never saved; NO to the team-sheet prompt). Agents run only --mock.
#   bash x2_run.sh hub     LTEST hub, TO\call_team_sheet_on.txt present, Tactics card opened once (svc + club logged):
#                          x2_sentinels --dry-run, then --allow-write (map record LH 95 / BU 1 / DA 0, read back).
#                          PASS -> the game stays up (next: Turbo Tactics tab teams.defensivedepth 77 / teams.buildupplay 0, overlay
#                          off, Tactical View); any other exit -> the game is killed.
#   bash x2_run.sh match   right after the Tactical View kick-off: x2_readout (<= 90 s) -> "X2 BRANCH: ...", then 45 s back line
#                          (match_measure, optional: X2_BACKLINE=0 skips), then KILL - on every exit path.
#   --mock (before the phase): offline; X2_MOCK_SHEETS = sheet world (sheet_copies.mock_world), X2_MOCK_READOUT = readout world
#                          (x2_readout.mock_world), X2_LOG = its log; the kill is only recorded ($X2_DIR/killed.txt).
# Refuses (exit 3, nothing sent, nothing killed) unless GAME_STATE names a live state. Exit 0 PASS / 1 FAIL / 3 INCONCLUSIVE.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-python}"
MOCK=0
[ "${1:-}" = "--mock" ] && { MOCK=1; shift; }
PHASE="${1:-}"
TO="${TURBO_OUTPUT_DIR:-C:/FC 27 Live Editor/turbo_output}"
X2_DIR="${X2_DIR:-$TO/x2}"; mkdir -p "$X2_DIR"
SM=(); RM=(); LG=()
if [ "$MOCK" = 1 ]; then
  SM=(--mock "${X2_MOCK_SHEETS:?}" --state "$X2_DIR/x2_state.json" --wlog "$X2_DIR/sentinels.log" --off "$X2_DIR/sentinels_off.txt")
  RM=(--mock "${X2_MOCK_READOUT:?}" --cache "$X2_DIR/cache.json" --mm-cache "$X2_DIR/none.json" --no-cache)
  LG=(--log "${X2_LOG:?}")
fi
say() { echo "[x2 $(date +%H:%M:%S)] $*"; }
kill_game() {
  if [ "$MOCK" = 1 ]; then echo "killed $(date +%H:%M:%S) phase $PHASE" >> "$X2_DIR/killed.txt"; say "mock: kill recorded (none sent)"; return 0; fi
  "$PY" "$HERE/harness.py" state --write > /dev/null || { say "GAME_STATE is no longer a coordinator session (re-read before the kill): the game is NOT killed"; return 0; }
  MSYS_NO_PATHCONV=1 taskkill /IM FC27.exe /F > /dev/null 2>&1 || powershell -NoProfile -Command "Stop-Process -Name FC27 -Force" > /dev/null 2>&1
  rmdir "$TO/turbo_dev_client.lock" 2> /dev/null
  say "game killed (post-flight C.8: GAME_STATE CLOSED, md5 of CmMgrC*, remove call_team_sheet_on.txt)"
}
[ "$PHASE" = hub ] || [ "$PHASE" = match ] || { sed -n '2,15p' "$0"; exit 2; }
if [ "$MOCK" != 1 ]; then
  "$PY" "$HERE/harness.py" state --write || { echo "VERDICT INCONCLUSIVE (x2_run $PHASE: refused, nothing sent, nothing killed)"; exit 3; }
fi
RC=3; KEEP=0
cleanup() { [ "$KEEP" = 1 ] || kill_game; exit "$RC"; }
trap cleanup EXIT
trap 'RC=3; exit' INT TERM
code() { case "$1" in 0) RC=0 ;; 1) RC=1 ;; *) RC=3 ;; esac; }
if [ "$PHASE" = hub ]; then
  "$PY" "$HERE/x2_sentinels.py" --dry-run "${SM[@]}" "${LG[@]}" || { code $?; say "dry run did not pass: no write"; echo "VERDICT INCONCLUSIVE (x2_run hub)"; exit; }
  "$PY" "$HERE/x2_sentinels.py" --allow-write "${SM[@]}" "${LG[@]}"; code $?
  [ "$RC" = 0 ] && KEEP=1
  echo "VERDICT $( [ "$RC" = 0 ] && echo PASS || ([ "$RC" = 1 ] && echo FAIL || echo INCONCLUSIVE)) (x2_run hub)"
  exit
fi
"$PY" "$HERE/x2_readout.py" "${RM[@]}" "${LG[@]}" | tee "$X2_DIR/$(date +%Y%m%d-%H%M%S)_readout.txt"; code "${PIPESTATUS[0]}"
if [ "${X2_BACKLINE:-1}" = 1 ] && [ "$MOCK" != 1 ]; then
  timeout 120 "$PY" "$HERE/match_measure.py" --seconds 45 --label x2_backline > "$X2_DIR/$(date +%Y%m%d-%H%M%S)_backline.log" 2>&1
  say "back line (45 s): $(grep -a -E 'written:|back line' "$X2_DIR"/*_backline.log | tail -2 | tr '\n' ' ')"
fi
echo "VERDICT $( [ "$RC" = 0 ] && echo PASS || ([ "$RC" = 1 ] && echo FAIL || echo INCONCLUSIVE)) (x2_run match)"
exit
