# Session <nn> - <gate / check> (template; copy to TOOLS\SESSION_<nn>_PLAN.md, fill BEFORE the launch)

Rules: brief Part C in full - never Escape; LTEST only, never saved, answer NO to the team-sheet prompt; one launch, <= ~8-10 min;
one pass/fail point per check with a screenshot or log line; Edit Tactic / Team Management LAST, then kill; Cassio's chat "go" first.
`T=<repo>/tools`, `TO=C:/FC 27 Live Editor/turbo_output`, `EV=C:/FC 27 Live Editor/turbo_dev/evidence/<date>`.

## 0. Pre-registration (written before the launch; never edited after it)
- Question / gate: <X1 | X2 | X3 | P<n> knob | S-<wp> smoke>
- Build: <installed 2.x.y | deployed branch @ sha> (sha256 of the zip / DLL)
- Opt-ins present: <tuning_on.txt | call_team_sheet_on.txt | ...>; kill switches absent: <list> (`ls` output pasted)
- Pass criterion (exact command + threshold, copied from the decision): <...>
- Branch per outcome (table of the decision): <...>

## 1. Pre-flight (game closed)
- [ ] every script of this plan passed `--self-test` today (`python "$T/selftest_all.py" --quick`)
- [ ] saves backed up, md5 recorded: `S="$LOCALAPPDATA/EA SPORTS FC 27/settings"; B=".../turbo_dev/backups/$(date +%Y%m%d-%H%M)-<label>";`
      `mkdir -p "$B" && cp -p "$S"/CmMgrC* "$B"/ && (cd "$B" && md5sum CmMgrC* | cut -c1-8 | tr '\n' ' ')`  -> <md5s>
- [ ] GAME_STATE = LAUNCHING (see tools/GAME_STATE.md); no build running (GAME_LAUNCH_HOLD.txt for the agents)

## 2. Launch and path (C.3 / C.4)
- [ ] `powershell -NoProfile -ExecutionPolicy Bypass -File "$T/launch_fc27.ps1" -WaitCloseMin 1` (exit 0; 6 = GAME_STATE not LAUNCHING)
- [ ] L0..L3 with `gk.ps1 seq` (screenshot after EVERY seq; the card must read LTEST) -> hub; GAME_STATE = MATCH-AB
- [ ] Check: `grep -a "Turbo GUI .* started" "$TO/turbo_gui.log" | tail -1`; `python "$T/dev2.py" '{"op":"ping"}'`

## 3. Checks (in order; Edit Tactic / Team Management last)
| # | Step | Command | Pass point (grep / screenshot) | Time box |
|---|---|---|---|---|
| 1 | <hub step> | `<command>` | `<exact log line / VERDICT>` | <s> |
| 2 | <kick-off step> | `<command>` | `<...>` | <s> |
| n | KILL | the wrapper / `taskkill /IM FC27.exe /F` | game gone | - |

## 4. Post-flight (always, also after a FAIL; C.8)
- [ ] game killed at <HH:MM:SS>; `rm -f` this session's opt-ins; `rmdir "$TO/turbo_dev_client.lock"`
- [ ] md5 of CmMgrC* == backup (<md5s>); a new CmMgrC<timestamp> file = accidental autosave -> move it out, restore
- [ ] GAME_STATE = CLOSED (<result>)

## 5. Result (appended to EV\<date>\TEST_<nn>.md)
- Build sha256, launch time, each check PASS / FAIL / INCONCLUSIVE with its screenshot name (file mtime) and exact log line
- The outcome against the pre-registered criterion, the branch taken, kill time, md5 result
