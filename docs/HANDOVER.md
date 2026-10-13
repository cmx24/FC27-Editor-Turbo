# Turbo handover (read this first)

Last updated: 2026-10-13 18:40 ET (Turbo 2.3.0 released). Older sections below keep their own timestamps. Update this file after EVERY release and every session that changes code, saves or game state. Every update MUST end with a fresh section 11 (next-LLM prompt) that matches the current state.

## 00. Update 2026-10-13 18:40 - Turbo 2.3.0 RELEASED (read first)
- **Released:** tag v2.3.0 = turbo-2.0 e952411 (pushed), https://github.com/cmx24/FC27-Editor-Turbo/releases/tag/v2.3.0 with
  FC27_LE_Turbo_2.3.0.zip (5104653 bytes, sha256 294ab9186ec42963d987d7493ac53ac098cfd744f05ba630237d6e9c4a1e5dbc, digest verified via the
  API; 112 files). Installed on Cassio's PC from wt-23-release e952411 (Turbo.dll cb079e84...). Game closed. Temp branches deleted.
- **Saves:** TORINO 2ded2b76, LB c45b2b12, LEICESTER 1e78df22, LTEST 3dced956 - unchanged all day (LTEST used for tests, never saved).
  Backup turbo_dev\backups\20261013-1729-pre-resume-23.
- **check_all (e7f20cf / e952411):** BUILD 0 warnings, LUA 405/0, NATIVE 38462/0, PY and CATALOG green.
- **How 2.3 was built:** branches 2.3/player-id 1abccb0, 2.3/archetype-target e96426d, 2.3/face-looks 8c11efd, 2.3/kits 0fd7625,
  2.3/channel-e2 014f81a (6 review fixes), 2.3/gameplay-tuning 8b486ad; merged on 2.3/release; review workflow (3 lenses + skeptics) found 3
  real issues: two fixed (e7f20cf "below" rule in the write job + row Reset of tied knobs; 18547f8 honest wording), the third (prematch GUID
  unread) was proven correct in game. Evidence: turbo_dev\evidence\2026-10-13\TEST_23.md.
- **In-game status (LTEST):** player id PASS; archetype target overall PASS; kit import phase 1 PASS (settings, colours, minikit file, 24
  textures staged; minikit on a game screen not checked; 3D kit on the pitch = phase 2); live standings PASS (regression); channel E2: the
  write lands and reads back, but Edit Tactic still shows BALANCED / DEEP (FAIL of the screen check; experimental, opt-in); gameplay tuning:
  both tables located in the running game, tab Apply 1.00 -> 1.40 and Undo read back ok; a live write during a match read back fine and the
  match ran on. **Not proven: that the match uses the tuning value (no on-pitch measurement yet).**
- **Engine tuning facts:** attribulator manager slot 0x14C26B128 (static); 15 AttribDBRuntime copies; one shared instance of the in-possession
  width table for all 15; heap addresses change at every relaunch (Turbo re-locates in-process in about 20 s at the first tab open).
  turbo_output\ATTRIBULATOR_LIVE.json/.txt, TUNING_FRAMEWORK.txt, TACTICS_MOD_CATALOGUE.txt.

## 00c. Update 2026-10-12 09:10 - Turbo 2.2.0 RELEASED (read first)
- **Released:** tag v2.2.0 = turbo-2.0 ae3c23a (pushed), GitHub release https://github.com/cmx24/FC27-Editor-Turbo/releases/tag/v2.2.0 with FC27_LE_Turbo_2.2.0.zip (sha256 bfbf05a23cc37d6e5d481d86bbd1c3c27dd316fa3ab42c87bbb2de67be7a135c, digest verified via the API). Installed on Cassio's PC (deploy from wt-22-release). Game closed.
- **Saves unchanged:** TORINO 2ded2b76, LB c45b2b12, LEICESTER 1e78df22, LTEST 3dced956 (LTEST loaded for the test, never saved). Backup: turbo_dev\backups\20261012-0713-pre22test.
- **How 2.2 was built (07:15-08:15):** 2.2/channel-e 2da8c71 + e532ecc (adversarial review: 1 blocker - role/focus values the game never uses - and 7 majors, all fixed: role allow-list from the other clubs' cached sheets, job re-checks club / load / service, scan fails closed + 5 s re-check, undo validated and bound to the career load, DB mirror opt-in `channel_e_db_mirror_on.txt`, hub phase only); 2.2/standings da1fdbe + d59c391 (review 7 findings closed; second review 4 minors fixed: rollback checks the block first, gate via the comm service). Merged on 2.2/release (recipe turbo_output\MERGE_22_RECIPE.txt, resolver merge_22_resolve.py; the only code conflict = the 8th/9th positional Signature fields exact_build / adapt_optional). check_all green: Lua 394/0, native 37028/0, build 0 warnings.
- **In-game test 08:14-08:56 (LTEST; turbo_dev\evidence\2026-10-12\TEST_22.md + screenshots):**
  - Standings PASS: 140 signatures found, 0 missing; FCE objects identified by signature; Live standings shows EFL League One (6000 rows, 6300 fixtures), not read-only; Match setup fixtures = the game's hub. Not checked: a table edit + refresh + Undo, a result fix in a played match.
  - Channel E: read-out "433" 25 / 0; Apply build-up 0->2, depth 25->75, ST role 38213->38210 "read back ok"; Undo and re-apply ok; **the match uses the written role** (after kick-off both match-setup copies hold 38210 for the ST). **FAIL: the game's Edit Tactic screen shows Build-Up BALANCED / Defensive Approach DEEP while the live record holds 2 / 75** - it reads another copy for build-up / depth (the DB row? the mirror was off). Whether the match uses the written build-up / depth is unknown. Released as opt-in "experimental" with this limit in the notes.
- **Game driving changed:** computer-use access to FC27.exe was denied (instantly, twice); Cassio approved access in chat, so the game was driven with turbo_dev\tools\gk.ps1 (PowerShell SendInput: `shot <jpg>`, `key <name> [ms] [count] [mode]`, `click "x,y"` in 1400-wide screenshot coordinates, `clickhere`, `move`, `type`, `selall`; Escape is refused) + turbo_dev\tools\dev2.py (dev service with repost-safe writes). Facts: arrows only work as KEYEVENTF_SCANCODE + extended (mode 2), Enter as VK + scan (mode 0); the menus drop to "Press Any Button" after a few seconds and the first key only wakes them - send Shift, wait 1 s, then the real key; screen capture needs SetProcessDPIAware (2560x1600 at 150 %); Team Management exit = park the pointer on "Esc Back", Down, then `clickhere`. Main menu: Down to MANAGER CAREER, Enter, Reconnect / Play Offline prompt (Down, Enter), Left to the list, Down to ORIGINAL, Enter, Enter on Play Now; the Original screen offers Continue Career = the newest save (LTEST at the moment) - check the card name before Enter.

## 00b. Update 2026-10-11 23:35 - session end, Cassio: "stop for tonight"
- Installed on Cassio's PC: Turbo 2.1.0 (released 22:08). Game closed. Saves unchanged at session end (md5 TORINO 2ded2b76, LB c45b2b12, LEICESTER 1e78df22; LTEST 3dced956 = throwaway copy of LEICESTER for tests, never saved).
- Tactics verdicts:
  - Channel A (per-side GAMEPLAY_CUSTOMIZATION game variables) DOES NOT WORK: stored and read back, ignored by the match (turbo_output\TACTICS_CHANNEL_A_REPORT.txt VERDICT; turbo_dev\evidence\2026-10-11\2238_channelA_armB_HT_matchfacts.jpg). It stays default-off.
  - Channel E (your club's live team-sheet record) is LOCATED read-only (turbo_output\TACTICS_CHANNEL_E_REPORT.txt): CachedTeamSheetService (vtables 0x14AD7F6C8 / 0x14AD7E8B8, 841 teams cached), Leicester's active tactic record ("433", depth 25, build-up 0), role word = group*4096 + role*64 + focus, SetSheet 0x145E43A60 / UpdateSheet 0x145E43BA4. The match copies formation and roles from it at setup. Heap addresses in the report are from one game run: the code re-chases them by vtable at every start.
- 2.2 (LOCAL branches only: not pushed, not released, not run in game):
  - **2.2/standings** fixes live standings and fixtures on 6AC07E31 ("FCE interface object has another vtable"): 976104a identifies the FCE objects by the build's signatures; suites green (build 0 warnings, Lua 393/0, native 36388/0, Python and catalog green). Adversarial review: turbo_output\STANDINGS_REVIEW_20261011.txt.
    - 2 major findings:
      - A DataManager found by its back-pointer only still allows writes.
      - The FCE row, result and fixture writes become reachable on 6AC07E31 for the first time. They have no sim_busy gate and no kill switch, and result and fixture edits have no undo.
    - 5 minor findings.
    - 04d6572 = WIP of the fix pass, stopped part-way. It holds edits to fce_standings.cpp/.h and app.h; unbuilt. Check them first.
    - Next:
      1. Close the two majors: refuse writes when the DataManager is back-pointer only; add a sim_busy gate, a kill switch (live_standings_write_off.txt) and snapshot+undo for edit_result / swap / set_fixture_teams. Alternatively, ship the views read-only.
      2. Run check_all.
      3. Run the 1-minute in-game test (turbo_output\STANDINGS_FCE_FIX.txt section 5).
  - **2.2/channel-e** writes your tactic into the live record: build-up, defensive depth, role/focus. It has gates, read-back, undo, a DB mirror and a kill switch; formation changes are refused.
    - 2aeeaca = WIP of the implementation, stopped part-way at 23:23. Unbuilt, unreviewed, no IMPL report.
    - New files: core/team_sheet.{h,cpp}, win/team_sheet_win.{h,cpp}, ui/ui_live_tactic.{h,cpp}, tests/native/test_team_sheet.h.
    - Edits: bridge.lua/.cpp/.h, sigscan, tactics_status.h, app, ui_tactics, overlay_dx12, build_win.sh, run_native.sh, test_main.cpp, test_tactics_phase0.h, t07_bridge.lua.
    - Next:
      1. Finish it per TACTICS_CHANNEL_E_REPORT section 4.
      2. Run check_all.
      3. Adversarial review plus fix.
      4. Run the 2-minute acceptance test (section 5) on LTEST.
  - **Cassio APPROVED the 2-minute acceptance test at 23:17** ("go ahead with the 2-minute acceptance test"). It did not run only because the channel E code was unfinished.
    - Run both tests in ONE game start:
      1. Merge both branches into a throwaway test branch and build it.
      2. Deploy with scripts/deploy.sh, game closed.
      3. Desktop LaunchFC27.lnk, then load LTEST and run both tests.
      4. Screenshot into turbo_dev\evidence\<date>\.
      5. taskkill the game; never save.
      6. Redeploy 2.1.0, or the 2.2 release if both pass.
    - Keep tests short (memory feedback-short-tests).
  - The workflow that built them: turbo_output\WF_turbo_2_2_channel_e.js (Build / Review / Fix per track; reuse its prompts). Agent transcripts: C:\Users\cmode\.claude\projects\C--FC-27-Live-Editor\57b7ad75-ada0-4ecd-bc1d-2cfc3c2d3271\subagents\workflows\wf_0c9f7008-a5e\.
- This update also repairs two paths that an older edit had turned into control characters (turbo_output\turbo_gui.log, turbo_dev\evidence\2026-10-11).

## 00a. Update 2026-10-11 22:10 - Turbo 2.1.0 RELEASED (read first)
- turbo-2.0 = 557a2ae, tag v2.1.0, GitHub release with FC27_LE_Turbo_2.1.0.zip (sha256 f059032c...), installed on Cassio's PC. Contents and verified/untested list: CHANGELOG 2.1.0 and the release notes; evidence turbo_dev\evidence\2026-10-11\ (CHECK_21.md, 21xx/22xx screenshots).
- Merged branches (history-clean): 2.0/rc (rewritten), 2.1/ux, 2.1/overall, 2.1/parity, 2.1/testinfra, 2.0/asset-catalog (+ wp branches), 2.1/lang-detect, 2.1/integration, 2.1/assemble. Every OTHER 2.0/* branch is on old shas: rewrite before any merge (section 0).
- Builds: turbogui/scripts/parallel_compile.sh (build ~30 s, native ~2 min, check_all ~3 min incl. the 5 asset catalog runners).
- Open: tactics A/B verdict for channel A and channels B/C/E (TACTICS_REPORT D); hidden head render after apply (not checked in game); gear writes / overall follow / parity / tactics badges not checked in game; All callnames list squeezed (UI tests need the small height); stale worktrees cleanup; languages Q2-Q6 (LANGUAGE_STRUCTURE_AUDIT 9; Q1 answered by detection: eng_us).

- 22:40 tactics phase: channel A (per-side GAMEPLAY_CUSTOMIZATION game variables) DOES NOT WORK in a played match (stored + read back ok, ignored by the match; turbo_output\TACTICS_CHANNEL_A_REPORT.txt VERDICT). Channel E (the live team-sheet record of your club) is LOCATED read-only on 6AC07E31 (turbo_output\TACTICS_CHANNEL_E_REPORT.txt: service, sheet, active tactic record, role/focus encoding, SetSheet; the match copies it at setup). 2.2 implementation runs on branches 2.2/channel-e and 2.2/standings (workflow turbo-2-2-channel-e): code + offline tests, then the 2-minute acceptance test in game. Live standings/fixtures are broken on 6AC07E31 (FCE vtable check) - 2.2/standings.

## 0. Update 2026-10-11 19:45 (Handover-resume session) - read first
- **The crash Cassio reported is fixed and RELEASED as 2.0.4** (tag v2.0.4, GitHub release with FC27_LE_Turbo_2.0.4.zip, sha256 76e9d986...; installed in C:\FC 27 Live Editor). Cause: match load reads a missing PlayerStatusManager entry (`FC27.exe+0x7FCE520`, same as 10-05 `+0x7FCCDF0`) for three Turbo-created / DB-linked Leicester players 78106, 279128, 279987. 2.0.3's guard ran before Turbo.dll's signature scan finished and gave up; 2.0.4 adds a memory fallback, a retry on every event while players are missing, and runs on the first events. Verified: LEICESTER loaded (not saved) -> "added for 3 of 3" -> match loaded. Report: turbo_output\CRASH_20261011_REPORT.txt; evidence turbo_dev\evidence\2026-10-11\RUNBOOK.md.
- **History rewritten before the push:** turbo-2.0 = 92a226a on origin (masters removed from 0931203..; backup ref backup/turbo-2.0-pre-rewrite = old 266ce68). EVERY other 2.0/* branch still has the OLD shas: before merging one into turbo-2.0, run the same filter on it (`FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch -f --index-filter 'git rm -r --cached --ignore-unmatch -q turbo/package/turbo/callnames/masters' -- 0931203..<branch>`; deterministic, shared commits map to the same new shas), delete refs/original, check `git log <branch> -- turbo/package/turbo/callnames/masters` is empty.
- Saves: TORINO / LB / LEICESTER untouched (md5 2ded2b76 / c45b2b12 / 1e78df22); LTEST (19:14) is a throwaway copy of LEICESTER with the 3 entries. Backups turbo_dev\backups\20261011-1851-precrashfix.
- In flight at 19:45: lean tracks (wt-tactics0, wt-rc, wt-arrivals, wt-lang-it; reports TACTICS_PHASE0_REPORT / ARRIVALS_GUARD_REPORT / LANG_IT_REPORT in turbo_output), asset catalog research (turbo_output\ASSET_CATALOG_DESIGN.md: Cassio wants every game asset - callnames, 3D heads, hair, gear, minifaces - mapped from the game files incl. players no longer in the DB). wt-guard (2.0/guard-hardening) is superseded by 266ce68/92a226a: delete it.
- Cassio's rules added today: tests must be SHORT (prove the one pass/fail point, screenshot, kill the game); finish fast, agents in parallel.

## 1. Project in one paragraph
Turbo is a Lua + native overlay (Turbo.dll) that supplies FC 26 Live Editor features for EA SPORTS FC 27 Manager Career. Live Editor (LE) is the user's paid editor and must not be modified. Turbo branch: `turbo-2.0` (repo `cmx24/FC27-Editor-Turbo`, worktree `C:\FC 27 Live Editor\turbo_dev\2.0\integration`). Latest released tag: v2.1.0 (557a2ae, GitHub release with zip, 2026-10-11 22:08). The agent pushes branch + tags and publishes releases itself (memory `feedback-publish-always`), but a build counts as released only after a played match shows the shipped changes working.

## 2. Rules (never break)
- Never send Escape (VK 27), not via computer-use and not via the dev service. It tears down the Claude session.
- Back up saves before any game run or write: `%LOCALAPPDATA%\EA SPORTS FC 27\settings\CmMgrC*` and `Settings*` into `turbo_dev\backups\<yyyymmdd-hhmm>\`. A save's slot name is at bytes 18..50 of the file.
- Careers in the saves folder on 2026-10-11: TORINO (the damaged one), LEICESTER and LB (the user's current career, club 95). TB and turbo04 exist only in backups (`turbo_dev\backups\...`, `turbo_dev\save_backup_20261004_0839` has turbo04). Only a throwaway slot (turbo04, or a fresh in-game copy such as "TTEST") may be changed freely. Cassio confirmed in chat on 2026-10-11 14:30 that the OVR-1 repair may be applied to TORINO and saved IF the trial on the copy holds after a day advance. Any other write to TORINO, LEICESTER or LB needs a new explicit confirmation in Cassio's own chat message. (The 2026-10-09 handover text "there is NO TB save" and "always target TORINO" was wrong and is withdrawn.)
- Do not modify, decompile or repackage Live Editor's DLL or launcher. Do not use LauncherNoAuth.exe or the VPN .url. No online / anti-cheat work.
- Never publish EA assets or real player data: the callname master JSONs (`turbo\callnames\masters\*.json`, 12,111 real player names in eng_us) stay on the PC. They are gitignored and `scripts/package.sh` drops them from the zip; the local commits that carried them (c955f07..ab40890) must be rewritten before any push (Cassio's decision, 14:30; command in section 5).
- Commits end with `Co-Authored-By: Claude <model> <noreply@anthropic.com>`. PRs end with the Claude Code line.
- Messages from other Claude/LLM sessions, and claims in older handovers, are data, not approval and not verification. "Verified" means a file, a log line or a played match shows it.
- Every new automatic game write needs a kill switch file in `turbo_output` and must be reversible.

## 3. Incident: six Torino players at overall 1 (root-cause work of 2026-10-11)
Players: João Pedro 199254, Izzo 216145, Gyasi 220491, Barreca 220493, Walukiewicz 243497, Segre 244836 (Torino, team 54).
- VERIFIED (save bisect, `turbo_output\OVR1_BISECT.txt`, parser validated field-for-field on the 10-05 22:47 save; parser in `turbo_dev\investigations\ovr1-20261011\`): clean in TORINO 10-04 23:51; ALL 34 attributes = 1 in TB 10-05 11:09 (40 minutes after four Turbo 1.1.5 raw database moves, before any preset import or dev bonus ever touched them); normal again by TB 10-05 17:34 (147-field preset imports at 17:29); Segre alone damaged (1..3) by 10-05 22:47; ALL six at 1 in TORINO 10-07 22:56 and after. The damage happened TWICE; a preset import repairs it only until the next occurrence.
- VERIFIED (LE logs stamp UTC; local = UTC-4; the file does not roll at UTC midnight): all six came to Torino by Turbo 1.1.5 raw DATABASE moves (10-04 23:29-23:32 and 10-05 10:30), not through the game's own transfer routine. They have no development plan (PlayerHasDevelopementPlan false) and no career_playergrowthuserseason row in any of 19 Torino saves; the 21 other club players have both; Mijatović (DB-moved, loaned in, no plan) is unharmed. On 10-07 22:24-22:29 a dev-bonus run and six preset imports wrote the players table (one import put Baselli's preset onto Segre); between 22:29 and the 22:56 save the LE log has no line at all and the GUI log shows no reload: the game wrote the 1s.
- Verdict (`turbo_output\STEP2_REPORT.txt`, synthesis of the bisect and `STEP2_DRAFT_B.txt`; adversarial review was still being applied when the session ended): H1 = the game's bookkeeping for the user club's players recomputes attributes at some progression event from a per-player growth record and produces the floor 1 for a player without a record; confidence 75-80%; trigger event UNKNOWN; the 2026-10-09 explanation ("Turbo 1.2.0 preset imports caused it") is refuted. Turbo's share: the raw database moves (2.0.x uses op 11 but still falls back to the database path when the GUI natives are absent or for a loaned player: close that, see section 10).
- The 2026-10-09 "definitive fix" (features/repair_ovr1.lua) never ran a repair (no `[OVR1-REPAIR]` line in any log), swallowed every error and wrote to whichever career was loaded. Since 2026-10-11 (merged into turbo-2.0): opt-in file `turbo_output\repair_ovr1_on.txt`, kill switch `repair_ovr1_off.txt` always wins (present now, keep it until the trial), career guard (targets are touched only while they play for the user's club of the loaded career), the script `lua\scripts\turbo_repair_ovr1.lua` is a DRY RUN by default (`TURBO_REPAIR_CONFIRM = true` to write, `TURBO_REPAIR_PLAYER = <pid>` for one, `TURBO_REPAIR_UNDO = true` to restore from `turbo_output\repair_ovr1_undo_<pid>.json`), every failure is logged, and the plan sync reports "plan entry still missing" instead of "plan" when the game has no entry. The script only fixes the players table; the game must create the plan record.
- Repair plan (NOT yet run): `turbo_dev\evidence\2026-10-11\RUNBOOK.md`, steps 4-12: read-only `turbo_output\diag_plan.lua` on TORINO (plan flag, growth row, loan row per club player, in-game date); in-game copy "TTEST"; advance day by day with the diag to catch the growth event; repair ONE player first (the game's own Development Plan screen in the Squad Hub, else a game-routine transfer round trip through op 11: Players > Transfer to an AI club, then back), then import his preset, advance past the next event; TORINO only if the copy holds. Segre's career values are lost: use his rewritten preset (base values, OVR 73).
- Step 3(b) (plain launch without Live Editor) is dropped (Cassio agreed, 14:30).

## 4. Turbo features and status
- Verified in played matches (turbo04, 2026-10-07): weather override, injury frequency/severity, CPU no-subs, squad roles with live read-back, re-apply on event 23. Not verified: difficulty, time of day. Evidence `turbo_dev\evidence\2026-10-07\RESULTS.md`.
- Built-in signature table for game build 6AC07E31; auto-adapt (kill switch `turbo_output\signature_adapt_off.txt`).
- 2.0.3 on turbo-2.0 (NOT installed, NOT released; suites green: build 0 warnings, Lua 357/357, native 32,630/0): the 1.2.5 line merged (match crash guard with `squad_status_repair_off.txt`, squad roles through the game op 14, appearance codes as text, kept player edits + plan sync, minifaces fixes); OVR-1 repair safety + career guard; channel A: 68 per-side GAMEPLAY_CUSTOMIZATION slider variables (sprint speed .. CPU behaviour, USER and CPU) as untested, default-off controls in Competitions > Match setup with two A/B presets ("baseline" / "CPU handicap extremes"), read-back, kill switches `tactics_off.txt` and `call_gamevar_off.txt`. Whether channel A reaches the pitch is UNKNOWN until the A/B matches (`TACTICS_CHANNEL_A_REPORT.txt` section 4 has the exact clicks and the verdict rule).
- Installed in `C:\FC 27 Live Editor` right now: the 2026-10-09 build (2.0.2 + its changes, OLD repair module). The kill switch `turbo_output\repair_ovr1_off.txt` is in place, so it writes nothing. Install 2.0.3 with `bash scripts/deploy.sh` from the integration worktree (game closed).
- In progress on agent branches (see section 5): descriptors everywhere instead of codes (committed, two fixes in progress); filters in blocks, label-left/control-right, two-sided ranges, FC 27-visible positions (committed incl. the group-match fix); every table/pane resizable + image preload at start-up (committed; two major fixes in progress: a failing native case and a preload-queue stall); tactics Phase 0 honesty (uncommitted, 8 files).
- Tactics inventory (priority 2a): `turbo_output\TACTICS_REPORT.txt` (84 parameters) + `TACTICS_PARAMS.json`; section D lists what cannot be verified yet and why; section E the Phase-0 work list.
- Commentary languages (priority 3): `turbo_output\LANGUAGE_STRUCTURE_AUDIT.txt`. The 2026-10-09 claims were wrong for 6 of 7 languages; the Italian master was regressed (4,046 -> 4,039 players; the good file is `turbo\callnames\masters\ita_it.json.bak_20261004_1800`); Dutch has no audio directory linked; Turbo loads only the configured language and never reads the game's loaded one; "category" is not represented; the "replicate" script only re-runs the importer. The audit holds the ordered work list for 3a-3e (Italian first). Not started in code.

## 5. Branches, worktrees, unpushed work (2026-10-11 16:10, session end)
Nothing is pushed: origin/turbo-2.0 = 0931203. Do not push until the masters rewrite below is done.
- `turbo-2.0` (integration): eb17aeb = ab40890 + d5df98f (1.2.5 merge) + 87ae475 (ovr1-safety) + 5a541b7 (channel A) + d0896de/f463abb (release prep, README) + eb17aeb (career guard). Clean. Binaries built at 5a541b7 are in `turbogui/build/win` (C++ unchanged since).
- `2.0/rc` (worktree `turbo_dev\2.0\wt-rc`): 821f3c1 = 5a541b7 + merge of 2.0/ux-descriptors + a test fix; the merge of 2.0/ux-filters was IN PROGRESS (16 dirty files, 0 conflict markers) when the session ended. Finish it: `git status`, resolve/verify, build + suites, commit; then merge 2.0/ux-resize-preload and 2.0/tactics-phase0 the same way; then merge 2.0/rc into turbo-2.0 (also merge the fix commits of 2.0/ux-descriptors and 2.0/ux-filters that landed after 3d376a5). Report: `turbo_output\RC_INTEGRATION_REPORT.txt` (not yet written).
- `2.0/ux-descriptors` (wt-ux-codes): 7d24206 committed (reviewer: accept with fixes; blocker = 5 files converted CRLF->LF, so merges show whole-file diffs; majors = Database tab still shows position codes, Match setup weather value shows a code). A fixer had 9 dirty files uncommitted at session end (`turbo_output\UX_DESCRIPTORS_FIX.txt` written 16:03): inspect, finish, commit.
- `2.0/ux-filters` (wt-ux-filters): 1dd95d4 + 45e628c (position filter matches the visible group). Clean. Reviewer: accept.
- `2.0/ux-resize-preload` (wt-ux-resize): a229979 committed (reviewer: accept with fixes; majors = native case "UI: database reload (another save) invalidates rows safely" fails in the reviewer's runs; preload queue ignores want_background's refusal and can stall). A fixer had 7 dirty files uncommitted at session end: inspect, finish, commit. Do not merge into rc before both are fixed.
- `2.0/tactics-phase0` (wt-tactics0): NOTHING committed; 8 dirty files from the Phase-0 implementer (badges split, dead saved-tactic writes removed, legacy sliders hidden, role editor renamed, read-back + reset-to-default). Inspect with `git diff`, build, test, commit, or redo from `TACTICS_REPORT.txt` section E.
- `2.0/tactics-channel-a` (wt-chanA) c2752a6, `2.0/ovr1-safety` (wt-ovr1safe) bc95a23, `2.0/merge-turbo-0.4` (wt-merge04) d5df98f: all merged into turbo-2.0.
- Masters rewrite before the push (rehearsed on a temp branch, works): from the integration worktree with a clean tree, `FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch -f --index-filter 'git rm -r --cached --ignore-unmatch -q turbo/package/turbo/callnames/masters' -- 0931203..turbo-2.0`; then `git for-each-ref refs/original` and delete those refs; verify `git log --oneline turbo-2.0 -- turbo/package/turbo/callnames/masters` prints nothing; the files stay on disk (gitignored). Do it AFTER all branches are merged (the rewrite changes every sha above 0931203; agent branches then need `git rebase --onto`).
- Snapshot of the peer session's half-finished 1.2.5 merge: `turbo_dev\backups\merge-wip-20261011-1351\`. The old `turbo-0.4` main checkout is at 19cc7cd and fully merged.

## 6. Where things are
- Repo: `C:\FC 27 Live Editor\turbo_dev\2.0\integration` (branch turbo-2.0). Main checkout `turbo_dev\FC27-Editor-Turbo` (turbo-0.4).
- Live Editor: `C:\FC 27 Live Editor` (Logs\live_editor_<dd-mm-yyyy>.log stamps UTC and keeps writing past midnight; le_config.json; lua\scripts).
- Turbo output: `C:\FC 27 Live Editor\turbo_output` (turbo_gui.log stamps local time; gui_settings.json; reports of section 8).
- Backups: `C:\FC 27 Live Editor\turbo_dev\backups\`. Evidence: `turbo_dev\evidence\<date>\` (2026-10-11 holds the RUNBOOK).
- Saves: `%LOCALAPPDATA%\EA SPORTS FC 27\settings\`.
- Memory notes: `C:\Users\cmode\.claude\projects\C--FC-27-Live-Editor\memory\` (MEMORY.md index; `turbo-2026-10-11-orchestration.md`).

## 7. How to drive the game (summary; full detail in memory `fc27-game-driving`)
- Launch only via the desktop shortcut `C:\Users\cmode\OneDrive\Desktop\LaunchFC27.lnk` (or `schtasks //run //tn LaunchFC27`). Close a leftover elevated launcher window first (the user must do it). Never start FC27.exe directly.
- Key input: the dev service writes `turbo_output\turbo_dev_request.json` (atomic: write tmp, then mv; result in turbo_dev_result.json; ops ping, key {vk, ms}, read, find, scan, multi, seq). Overlay toggle = VK 164; numpad + (107) is the Turbo GUI key.
- Diagnostics run from Live Editor's Lua engine: `dofile("C:/FC 27 Live Editor/lua/scripts/turbo_diag_ovr1.lua")` and `dofile("C:/FC 27 Live Editor/turbo_output/diag_plan.lua")` (both read-only). Read-only checks come first.
- Deploy only with the game closed (`bash scripts/deploy.sh` in the worktree; it refuses while FC27.exe runs). Build/test: `bash scripts/msys.sh "bash scripts/check_all.sh"` (~10 min).

## 8. Reports in turbo_output (2026-10-11)
ORCHESTRATOR_BRIEF_20261011.md · OVR1_BISECT.txt/.json · STEP2_DRAFT_B.txt · STEP2_REPORT.txt (+ STEP2_HANDOVER_SNIPPET.md) · OVR1_SAFETY_REPORT.txt · MERGE_REPORT.txt · TACTICS_REPORT.txt + TACTICS_PARAMS.json · TACTICS_CHANNEL_A_REPORT.txt · UX_DESCRIPTORS_REPORT.txt + UX_DESCRIPTORS_FIX.txt · UX_FILTERS_REPORT.txt · UX_RESIZE_PRELOAD_REPORT.txt · LANGUAGE_STRUCTURE_AUDIT.txt · diag_plan.lua. Not yet written: TACTICS_PHASE0_REPORT.txt, RC_INTEGRATION_REPORT.txt, UX_RESIZE_PRELOAD_FIX.txt, UX_FILTERS_FIX.txt.

## 9. Game window (planned 16:30-18:15 ET 2026-10-11; NOT run by the orchestrator session)
Plan: `turbo_dev\evidence\2026-10-11\RUNBOOK.md` (backups, install 2.0.3, TORINO read-only diag, TTEST copy, day-advance probe, repair trial on one player, A/B arms for channel A, TORINO repair under the consent of 14:30). Results, screenshots and the timestamped log go to that folder. Cassio's confirmation of 14:30 covers exactly: the repair on TORINO after a clean TTEST trial that holds past a day advance.

## 10. Open items, in order (2026-10-13 18:40)
0. **On-pitch effect of gameplay tuning (top):** turbo_dev\tools\match_measure.py has a new route (HUD FifaPlayerData objects found from a
   starter's player id; `python match_measure.py --check -v`), only tested offline (the Lua-binding root S0 is empty in real matches).
   One short LTEST session: kick off a Tactical View match, run --check (FAIL -> kill at once), then 30 s baseline, write width 1.4 from the
   tab (needs the hub) or ab_width.py with the instance from the tab's log line, 30 s again, --compare, kill. Prove the 11 other knobs'
   tables live (attrib_locate.py groups 3-12) before marking them verified.
1. **Channel E2:** Edit Tactic fills its working copies from another source when it opens (not the live record). Find that source (dump the
   service while Edit Tactic is open after a Turbo write) or accept the record-only write; check whether the match uses +0xA4/+0xB0/+0xB4.
2. **Kit import phase 2:** the 3D kit on the pitch from the staged textures (KITS_IMPORT_RESEARCH.txt section 5); minikit on a game screen.
3. **Carry-overs:** standings writes not checked in game; 2.1 features not checked in game; OVR-1 repair on TORINO (conditional yes, trial
   on a copy first); languages; repo hygiene (stale worktrees wt-23-*, wt-23-rehearsal; 2.3/* branches are merged).
4. **Voice generation (3.0 PT-BR):** parked by Cassio.

## 11. Prompt for the next LLM (copy this as the first message)
```
You are continuing the FC 27 Turbo project for the user Cassio. Read docs/HANDOVER.md first (branch turbo-2.0), section 00 then section 10,
then the memory notes in C:\Users\cmode\.claude\projects\C--FC-27-Live-Editor\memory\ (MEMORY.md index).
State: Turbo 2.3.0 is released (tag v2.3.0, GitHub release with the zip) and installed; the game is closed; TORINO / LB / LEICESTER
untouched (LTEST is a throwaway copy of LEICESTER for tests).
Rules: never send Escape to the game; back up every save before any game run; write to none of TORINO / LB / LEICESTER without Cassio's
explicit confirmation in chat; test on LTEST only and never save it; keep in-game tests short (prove one point, screenshot, kill the game;
never keep a match running for probing; agents prepare offline and hand over a scripted check of 2 minutes at most); every game write needs a
kill switch, a read-back and an undo; publish no gameplay mod values; Live Editor's own DLL and launcher are off limits; masters and the asset
catalog never leave the PC; at every new-version request show an agent deployment plan first.
FIRST TASK: section 10 item 0 (on-pitch measurement of the tuning width knob), then item 1 (channel E2's Edit Tactic source). Report in
turbo_output and turbo_dev\evidence\<date>, update this handover (section 00, a fresh section 11), commit with the Co-Authored-By trailer.
```
