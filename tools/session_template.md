# Launch-session checklist (one page; copy to a SESSION_<nn>.md, tick BEFORE the launch)

Binding rules (plan section 6): LEICESTER only, never saved. Never TORINO or EVERTON. Answer NO to the team-sheet prompt. Never Escape (on-screen Back or gk.ps1 click). Launch only with `tools\launch_fc27.ps1`. One short launch per gate (<= 12 min, one pass/fail point, screenshot). Kill FC27 at the end, always. Ship only what is proven in game. No launch without Cassio's chat "go".

Paths: `T` = `C:\FC 27 Live Editor\turbo_dev\tools`, `TO` = `C:\FC 27 Live Editor\turbo_output`, `EV` = `C:\FC 27 Live Editor\turbo_dev\evidence\<date>`.

## 0. Before the launch
- [ ] Gate and pass criterion written in `EV\<date>\TEST_<nn>.md` with the branch table (never edited after the launch).
- [ ] Any baseline numbers the criterion needs are measured and filled in (no "BASELINE NOT FOUND" left in a criterion).
- [ ] Build and sha256 recorded: ____
- [ ] Opt-ins for this session present: ____ ; kill switches absent (`ls` pasted): ____
- [ ] Offline self-tests green (`python -I selftest_all.py --quick`): ____
- [ ] `prelaunch_check.ps1` run: saves and settings backed up to `turbo_dev\backups\<stamp>-<label>`, md5s recorded: ____
- [ ] Push-notify Cassio; wait for his chat "go". No push, no launch.
- [ ] No other build or agent is holding the game (`GAME_LAUNCH_HOLD` set, `GAME_STATE` = LAUNCHING).

## 1. Launch and path
- [ ] Start with `tools\launch_fc27.ps1` only (same as the Desktop LaunchFC27.lnk). Exit code: ____
- [ ] Menus with `gk.ps1` or on-screen controls, screenshot after every step. The card must read LTEST.
- [ ] LEICESTER selected (never TORINO or EVERTON). Team-sheet prompt answered NO.
- [ ] Turbo started: `grep -a "Turbo GUI .* started" "$TO/turbo_gui.log" | tail -1` ____

## 2. Checks (one pass/fail point each; Edit Tactic / Team Management last)
| # | Step | Command | Pass point (log line or screenshot) | Time box |
|---|---|---|---|---|
| 1 | | | | |
| 2 | | | | |
| n | KILL | `taskkill /IM FC27.exe /F` or the wrapper | game gone | - |

Never wait out a match. Stop each window at its time box.

## 3. Teardown (always, also after a FAIL)
- [ ] Game killed at ____ (`postlaunch_teardown.ps1` run).
- [ ] Session opt-in files removed; `turbo_dev_client.lock` removed if present.
- [ ] md5 of saves and settings equals the pre-launch md5s. If not, the launch is reported as FAIL.
- [ ] Any new `CmMgrC*` or `Settings*` written by the game: copied to evidence first, then moved out. No `--undo --force` before that copy exists.
- [ ] `GAME_STATE` = CLOSED.

## 4. Report (append to `EV\<date>\TEST_<nn>.md`)
- Build sha256, launch time, kill time, md5 result.
- Each check: PASS / FAIL / INCONCLUSIVE, with screenshot name and exact log line.
- Outcome against the pre-registered criterion, branch taken.
- Still NOT proven after this launch (say it plainly).
