# Handover: FC 27 LE Turbo 0.3.0 work in progress

Prepared 2026-10-03 (about 17:10 UTC) from the transcript of Cowork session **`cse_018KNHBkXKaKC6RHBU17Afhu`**
("FC 27 LE Turbo build completion"). It continues `docs/HANDOVER.md` on branch `claude/trusting-cannon-rkxwrw`
(written at Turbo 0.2.4); read that file first for the architecture, launch flow, build commands and log locations.
This file only covers what happened after it: 0.2.5, the Turbo-native features and the work toward 0.3.0.

## 0. Read this first

1. **The 0.3.0 source code is not on GitHub.** The session committed in a cloud container (`/home/claude/FC27-Editor-Turbo`)
   and never pushed. GitHub's `claude/trusting-cannon-rkxwrw` still ends at `195cdb5` (the 0.2.4 handover).
   Local-only commits named in the transcript: `89fc73e`, `23d65be`, `6c44d80` (0.2.5), `e4c58b7`, **`663d67f`** and
   **`41d1ed5`** (0.3.0 work). The 0.2.5 series was exported as `turbo_0.2.5_patches.tar` (also copied to the PC), but I
   found **no export of `663d67f` or `41d1ed5`**. If that container is reclaimed, the C++ and Lua sources for minifaces,
   pickers, Competitions tab, `core/moves.lua`, `core/budget.lua` and the 0.3.0 docs are lost. The PC holds only the
   *deployed* result of them. See section 3 for how to recover them.
2. **0.3.0 contains a known crash path.** Turbo's own Transfer / Loan / Release / Delete edit only the career database.
   In the test career `turbolab` the game crashed while simulating forward after several such edits (crash dump
   `CrashDump_2026.10.03_12.34.23.152.mdmp`, 16:34 UTC). The session's reading of the dump is that the game builds the
   squad's player list from a team-sheet slot table and hit a missing player. That is its own assessment ("likely
   cause"). **The root cause was not confirmed, and no fix was written.** The planned mitigation (block moves into or out of the user's own club, label the rest as risky) was
   never implemented. Do not hand 0.3.0 to the user as safe until this is dealt with.
3. **League-table editing does not work in the game yet.** The new Competitions tab writes `leagueteamlinks`; the game's
   Standings screen ignores it, even after save and reload. The live table is in the football-engine ("FCE") memory.
4. **Three of the user's four stated priorities are not done**: edit match results, create a job offer, and make a
   miniface from a snapshot of the in-game 3D player model. Their RE work was cut off by the rate limit (section 6).
5. **One rule question needs the user's answer before more RE.** The original handover says "Never modify, decompile,
   disassemble or patch `FCLiveEditor.DLL` or the game". The session disassembled a memory image of the game after
   deciding that the user's "ALL features" message lifted that rule for code-level features. I found no explicit
   confirmation from the user. Ask once and record the answer here. See section 1.

## 1. Ground rules (carry forward, with one open item)

From `docs/HANDOVER.md`, still in force:

- Offline Career Mode / Kick-Off only. Never touch online modes. No anti-cheat work, no bypassing Live Editor's Patreon
  authentication.
- Never commit Live Editor's files (`turbo/le27/libs` is a gitignored test fixture) and never commit personal data
  (Live Editor logs hold the user's Patreon ID and name; Windows paths hold the user name, so use `C:\Users\<user>\...`).
- Every database write is range-checked; destructive actions need confirmation.
- Never save or modify the user's real career, **`turbotest` (Torino)**. Use the throw-away career **`turbolab`
  (Juventus)**. Be aware `turbolab` now holds the DB-only edits that probably caused the crash, so make a fresh test
  career before judging any fix.
- No pull request unless the user asks.

**Open item.** The session told the user it was assuming "ALL features" lifts the "never patch the game's code" rule
for code-only features (match-fixing, negotiation bypass, endless career, job offers, ...), keeping offline-only and no
anti-cheat. I found no reply from the user that confirms or rejects that. Static disassembly of the game's main-module
image (`fc27.bin`, dumped from the running game by Turbo's dev service) was done on that assumption. Until the user
confirms, do not patch game code and do not extend the disassembly; memory reads and writes of data structures, which
the project already did before this session, are a separate matter.

## 2. Session facts

| | |
| --- | --- |
| Session | `cse_018KNHBkXKaKC6RHBU17Afhu` (API id `session_018KNHBkXKaKC6RHBU17Afhu`), Cowork remote, started from the desktop app |
| Model / effort | `claude-opus-5-5`, high |
| Span | created 2026-10-02 22:32 UTC; last event 2026-10-03 16:55:32 UTC |
| End state | idle, **stopped by the 5-hour rate limit** ("resets 3pm America/New_York" = **19:00 UTC**) |
| Context | 778,566 of 1,000,000 tokens used; one compaction at 13:11 UTC (784k tokens down to 11.5k) |
| Cost | about 121 USD per the session metadata |
| Machine | the user's Windows PC through the Claude Desktop device bridge (computer use plus `device_bash`). Connected folders: `C:\FC 27 Live Editor`, `Downloads`, `AppData\Local\EA SPORTS FC 27`, `AppData\Local\CrashDumps` |
| Times | the session's own messages use US Eastern (UTC-4). This file uses UTC unless it says otherwise |

**How I built this file.** For 13:11-16:55 UTC I read the transcript page by page: the assistant's own text, every status
message sent to the user, every user message, file writes and git commands, and one-line descriptions of the other tool
calls. I did not read the detailed tool output (test logs, disassembly), I did not see the game, and I ran no Turbo code.
For 22:32 UTC on 2 October to 13:11 UTC I had only the session's own compaction summary. Everything marked "reported"
below is the session's claim, not something I re-checked.

## 3. Repository state and how to recover it

GitHub (`cmx24/FC27-Editor-Turbo`), checked 2026-10-03:

| Ref | Head | Note |
| --- | --- | --- |
| `main` | `5ca1c73` | "Initial commit" (README only) |
| `claude/trusting-cannon-rkxwrw` | `195cdb5` | Turbo 0.2.4 plus `docs/HANDOVER.md`; open pull request `#1` |
| `claude/cool-brahmagupta-e599j5` | this branch | handover docs only |

Container-only commits. The first four lines are from the session's own `git log` output (14:47 and 16:28 UTC); the last
three come from the 13:11 compaction summary, which also says the 0.2.5 patch series is "7 patches from `379554c`", so
there are commits in that series I cannot name:

```
41d1ed5  0.3.0: user guide for minifaces, pickers, competitions and Turbo-native moves/budget;
         dev key press only when the game is in front; multi/seq/ptrs/bytes/region/write dev ops
663d67f  Turbo-native moves, budget, minifaces, real-face and tattoo pickers, league tables
e4c58b7  Handover: how to publish the 0.2.5 patch series
6c44d80  Turbo 0.2.5 release: package, docs (handover, reference, parity, README, user guide) ...
  ...    23d65be (input shield, 8 hooks), 89fc73e (env.api, caps, transfer history / fixtures, db league stats, tests)
379554c  base of the 0.2.5 patch series
```

**History does not match GitHub.** The patch base `379554c` is not among GitHub's commits, so the container's history
was probably re-created rather than cloned. A plain `git push` may be rejected as non-fast-forward. Assessment, not tested.

Recovery options, best first:

1. **Resume the old session after 19:00 UTC** and ask it to run
   `cd /home/claude/FC27-Editor-Turbo && git format-patch 379554c..HEAD -o /mnt/user-data/outputs/turbo_0.3.0_patches && git bundle create /mnt/user-data/outputs/turbo_0.3.0.bundle HEAD`
   and, in addition, to try `git push origin HEAD:claude/turbo-0.3.0-wip` (a new branch name, so a history mismatch cannot
   block it). Then apply onto `195cdb5` with `git am --3way`. I have not done this; messaging a rate-limited session is an
   action for the user to approve.
2. If the container is already gone, the PC still has the last *deployed* Lua pack and binaries
   (`C:\FC 27 Live Editor\lua\...\turbo\` and `turbo\Turbo.dll`) and the staged tars in `C:\FC 27 Live Editor\turbo_dev\`.
   Those are enough to keep testing, but the C++ source and the git history are not recoverable from them.

Other container-only items at risk: `/home/claude/re/` (`fc27.bin` 555 MB, `rx.py`, `rx2.py`, `fields.py`),
`/home/claude/bin/mkdeploy.sh` (the deploy-tar builder), `/mnt/user-data/outputs/`, and the session's probe scratch files.
The standings notes are now preserved in `docs/re/standings-fixtures-notes.md`.

## 4. What was built, and how well each part is proven

Evidence tags: **G** = observed running in FC 27 by the session (reported); **DB** = changed in the career database only,
effect in the game not shown; **T** = covered by offline tests; **B** = built, never tried in the game; **-** = not started.

Last full offline test run (reported at 14:47 and again at 16:28): **137 Lua tests and 3,889 GUI checks passed**. The
16:28 report says the key-press change came *after* that run, so it is not a test of the final tree. I saw no sign of a
Windows build, smoke test or Wine overlay run after `41d1ed5`.

### Working

| Feature | Evidence | Notes |
| --- | --- | --- |
| Transfer budget get / set / add (`core/budget.lua`) | G, T | Path: `UserManager(129)+0x18` user info, `+0x2F8` finance list, entry `+0x08`; team at entry `+0x28`; budget at entry `-0x10` and `+0x08` (both written). Test: 81.5M to 91.5M, shown on Budget Overview, **still there after save and reload**. Needs the memory map, not a Live Editor native. |
| Player miniface from a PC picture, another player's miniface, the head model's miniface, or the youth face | G, T | Tab "Miniface"; framing sliders; DXT5 DDS written under Live Editor's `mods\legacy` with backups of existing files; new face showed **immediately** in Team Management and Squad Hub (De Gea test). "Remove custom miniface" restores the original. Images are exported with `LegacyFileExport` on career events or by running `lua\scripts\turbo_images.lua`; hide Turbo (F8) first because Turbo blocks Live Editor's keys while open. |
| Real-face picker (4,024 heads, thumbnails, search) | G for the miniface, T | Giving De Gea Bremer's head changed his miniface to Bremer's. The 3D head in a match was **not** looked at. |
| Tattoo picker per body area, game preview pictures | G, T | |
| Turbo dev service in `Turbo.dll` | G | `scan`, `refine`, `changed`, `read`, `dump`, `find`, `key` plus `multi`, `seq`, `ptrs`, `bytes`, `region`, `write` (the last six were added in `41d1ed5`; I did not check whether any was exercised). Request `turbo_output\turbo_dev_request.json`, result `turbo_dev_result.json`. Disable with `TURBO_GUI_NO_DEVTOOLS=1`. |
| In-game key injection (`key` op) | G | Needed because Escape and Backspace from the automation never reach the game. Fixed at 16:15 so it presses only when the game window is in front (earlier it sent Escape to whichever window had focus, including the Claude app, which cancelled the session's own steps). |
| Transfer history and fixtures CSV from FC 27 memory, GUI greying, `env.api` capability checks, input shield (8 hooks) | reported in the 0.2.5 work | Before 13:11 UTC; known only from the compaction summary. |

### Built but unsafe or unproven

| Feature | Evidence | Notes |
| --- | --- | --- |
| **Transfer / Loan / End loan / Release / Delete** (`core/moves.lua`) | G for one transfer; DB for the rest; **crash risk** | A De Gea move Fiorentina to Juventus survived save and reload and showed in Team Management (Reserves). Loans, release and delete were only checked in the database. A move shows on squad screens only after save and reload (Customise, then Save). Later the test career crashed (section 0, item 2). |
| Manager minifaces (512x512) | B | Same tools as players; never tried in the game. GUI tests exist for minifaces; I did not check that managers are covered. |
| Competitions tab: edit `leagueteamlinks` (W/D/L, goals, points, position) | DB, T | Stored and persisted, but the Standings screen ignores it. |
| Delete generated players (uses `moves.delete`) | DB | Same crash risk as above. |

### Not started

Edit match results; create a job offer; miniface generated from the in-game 3D model (the game's `PlayerCapture` code
was located by string only); transfer-list and loan-list flags; transfer bans; live season stats; heal / injury /
never-tired; player-development boost; match setup (stadium, weather, kick-off, crowd); match-fixing; manager transfer
and firing, job security; unsupported leagues and teams; CPU-vs-CPU, unlimited subs, reveal player data, manager market off,
endless career, negotiation and objective bypasses; create / clone / preset players; formation editor; transfer columns
in the team list.

### Feature roadmap at 13:36 UTC (all 41 FC 26 Live Editor groups)

The session's own count then: 15 groups fully done, 9 partly done, 2 through the Database tab, 8 left to FC 27 Live Editor,
1 removed by EA, 6 with nothing in Turbo yet (19 job offers, 24 manager transfer/fire, 27 reveal player data, 28 match
setup, 30 match switches, 33 unsupported leagues). Since then minifaces, both pickers and the Competitions tab were added.
`docs/fc26-parity.md` on the other branch is the long-form version and is now out of date for these.

## 5. Technical facts learned in the game (reported in the 13:11 compaction summary unless noted)

FC 27 Live Editor v27.1.2 natives:

- Present: `GetDBMeta`, `GetDBTableRows`, `GetDBTableFields`, `EditDBTableField`, `InsertDBTableRow`, `DeleteDBTableRowByAddr`,
  `ExecuteSQL`, `GetPlugin`, `IsInCM`, `SetPlayerForm/Morale/Fitness`, `MEMORY`, `LE`.
- Missing: every `c*` native (`cTransferPlayer` ...), `Get/SetUserTransferBudget`, `GetPlayersStats`, `DeletePlayer`,
  `TerminateLoan`, `PlayerExists`, `GetTeamIdFromPlayerId`, `GetCompetitionNameByObjID`, a `GetCurrentDate` native,
  `PlayerDevelopmentManager*`. `GetTransferBudget/SetTransferBudget` exist only as deprecated no-ops.
- `GetDBTableRows` rows carry no `row.addr`. Each field entry is `{name, value (string), addr (decimal string of the record
  address), table_name}`. `DeleteDBTableRowByAddr(table, "<decimal addr>")`.

Game structures (all offsets for the build in use that day):

- `CalendarManager` (24): date at `+0x34/+0x38/+0x3C`.
- `TransferManager` (127): eastl lists at `+0x2998` completed transfers, `+0x29D8` loans, `+0x2978` AI offers,
  `+0x29F8` / `+0x2A18` offers for the user's players. Node: `+0x10` player id, `+0x14` to club, `+0x18` from club,
  `+0x24` date, `+0x30` fee.
- `MainHubManager` (58): `+0x60` count, `+0x68` user fixtures, 0x130-byte entries (`+0x2C` comp, `+0x38` date, `+0x3C` time,
  `+0x128` home, `+0x12C` away).
- `BudgetManager` (23): AI clubs' hash only (not the user's budget).
- Database: `teamnationlinks` lists national teams; Free Agents is team 111592; `teamplayerlinks` position 29 = reserve;
  `cm_teamsheets` has `playerid0..51`; `players.wage` depth 27, `releaseclause` depth 30, `contractvaliduntil` minimum 2000,
  `playerjointeamdate` in Lilian days.
- A DB-level transfer (`teamplayerlinks` team change plus `cm_teamsheets`) does **not** update the running career's squad
  screens until save and reload, and is the suspected source of the crash.
- FCE (football-engine) layout for standings, fixtures and results, with confidence tags: `docs/re/standings-fixtures-notes.md`.

Input and automation:

- FC 27 polls `GetAsyncKeyState` / `GetCursorPos`. Menu navigation works with held arrow and Return keys (0.12-0.2 s) and
  with mouse clicks (hover alone does not select). **Escape and Backspace from the automation never reach the game**; use
  the Turbo `key` op.
- Game flow used by the session: launch with the desktop shortcut to Live Editor (`LaunchFC27.lnk`); on the "Personal
  Settings" prompt click the window and press Enter; "EA Servers unavailable" Enter; Manager Career, Play Offline,
  ORIGINAL, then Load Career. **Never press Enter on Ultimate Team** and **never press Enter in Calendar** (it simulates
  to a date). Hub tabs: hold `x` / `c`; `w` opens the shortcut menu. Save through **Customise, then Save**. Close the game
  with the window X (no save).
- Live Editor overlay toggles with Left Alt. Features, Lua Engine, click the editor, `ctrl+a`, type
  `assert(loadfile('C:/FC 27 Live Editor/turbo_dev/<script>.lua'))()`, Execute. Clear `package.loaded["imports/turbo..."]`
  to reload a module.
- Computer-use access to the PC **expires after 30 minutes without activity** and needs the user to approve again; a batch
  of actions is cut off at 60 s of total wait; the Windows lock screen blocks everything (15:58-16:03 UTC the PC was locked); some windows (File Explorer, Text Input Host, EA app) are only "click" tier and cannot be typed into.
- Broad memory probes freeze the game (one took about 3 minutes). Keep scans bounded (a full scan takes about 57 s).

Deploy loop used: build in the container (`WITH_BIN=1 bash /home/claude/bin/mkdeploy.sh`), commit the tar to
`C:\FC 27 Live Editor\turbo_dev\turbo_deploy.tar`, extract it with `tar -xf ... --overwrite --no-same-owner
--no-same-permissions` from `device_bash`. **The game must be closed to replace `Turbo.dll`.** `mkdeploy.sh` is not in the
repo; it has to be recreated or recovered (section 3).

## 6. Timeline since the compaction (UTC)

| Time | Event |
| --- | --- |
| 13:11 | Compaction. Wired `core/budget.lua` as the fallback for `transfer_budget`; deployed. |
| 13:12-13:25 | Loaded `turbolab`, ran the persistence test, saved through Customise, Save (about 13:17), rebuilt the DLL with a `SendInput` key op, relaunched and reloaded; **the transfer and the budget had persisted**. Status message sent. |
| 13:36 | User asked for the roadmap; the session sent the 41-group list. |
| 13:41 | User set priorities: **minifaces (player and manager), edit results and league tables, create job offer, visual pickers for star heads / head assets and tattoos**; "full release asap". |
| 13:57-14:47 | Built image pipeline (stb, DDS/PNG/JPG, DXT5), Miniface tab, pickers, Competitions tab, `core/legacy.lua`, tests `t11`. Commit **`663d67f`** at 14:47. Wishlist status sent. |
| 14:54-15:05 | Started static analysis of the game image for the live standings table (`fc27.bin`). Searches in the running game found a `{teamid,0,0,0}` array with header 337 and nothing usable, because pre-season values are all zero. |
| 15:54-16:28 | Stuck on the Edit Tactic screen (needs Escape). The PC was on the lock screen for part of it. Fixed the key press to act only when the game is in front. Version bumped to 0.3.0, user guide updated, commit **`41d1ed5`** at 16:28. |
| 16:34-16:35 | **Crash** of `turbolab` while simming. Warning sent: do not use Transfer / Loan / Release / Delete on a career you care about. |
| 16:40-16:44 | User: "this is taking too long". Session plan: block own-club moves, package 0.3.0, hand over a test list, then standings, results, job offers. User added: miniface from an in-game snapshot like FC 26 Live Editor. |
| 16:44-16:55 | Three static-RE sub-agents started; all three died on the rate limit. The standings notes were saved at 16:55:30. Session ended. |

Interruptions: the user declined or interrupted tool calls at 13:27, 13:36, 13:48, 15:05, 16:06, 16:19 and 16:37. The
transcript does not say why. At 16:28 the session asked whether the last stop was deliberate; no answer is recorded.

## 7. What to do next (priority order)

**P0. Recover the unpushed work** (section 3). Nothing else is safe to build on until it is out of the container.

**P1. Make 0.3.0 safe to hand over.**
- Block Transfer / Loan / Release / Delete for the user's own club in Lua and in the GUI, label the rest "unverified, can
  crash the game", and keep `delete_generated_players` behind the same gate. This is the session's own plan at 16:40.
- Find the real cause. Start a *fresh* test career, do one DB-only move, simulate a few days, and compare which
  structure still lists the moved player (candidates: the user's lineup / team-sheet in game memory, `MainHubManager`
  squad lists, `TransferManager` lists). The session's idea was to reload `turbolab` and find "which list is out of
  step"; it never got to do it.
- Re-run every test in `docs/HANDOVER.md` section 6, rebuild `Turbo.dll`, run the Wine smoke and overlay tests, run
  `bash scripts/package.sh` to produce `dist/FC27_LE_Turbo_0.3.0.zip` (I found no sign it was built; `core/version.lua` and
  `kGuiVersion` must both read 0.3.0), `git rm` the 0.2.5 zip, verify the zip's DLL is the tested build.
- Hand the user a short test list (what to open, what working looks like).

**P2. The user's wishlist, in their order.**
1. **Miniface from a snapshot of the in-game 3D player** (what FC 26 Live Editor did through the game's `PlayerCapture`).
   Brief 1 in `docs/re/pending-re-briefs.md`. Also verify manager minifaces in the game.
2. **Edit league tables and match results.** Design from `docs/re/standings-fixtures-notes.md`: standings are stored
   incrementally per row (`StandingData`, 0x18 bytes: counters `u8` at `+0x09..+0x12`, points `s16` at `+0x14`), the screen
   re-reads and re-sorts them on every request, and a result edit does **not** move the table, so a result edit has to patch
   the fixture *and* the two rows. Locate `DataManager` by scanning the heap for the vtable at image base `+0xB180CA8`, then
   validate. None of this was run. Pre-season rows are zero, so test after a few league matches have been played.
3. **Create a job offer.** Brief 2 in `docs/re/pending-re-briefs.md`.
4. Visual pickers: done for heads and tattoos. Anything for "head assets" beyond the real-face picker was not separately
   confirmed with the user.

**P3. Remaining roadmap** (section 4, "Not started"). The session's own order was: list flags and transfer bans, live
season stats, game switches, match setup and match-fixing, create / clone players and standings view, then polish.

## 8. Decisions the session made on its own

- Treated "ALL features" as lifting the no-game-patching rule (section 1).
- Chose Turbo-native database and memory implementations over waiting for Live Editor natives (user: "i dont want to wait for
  fc live editor 27 to add the features - thats why turbo exists").
- Renamed the season-stats export to `turbo_database_league_stats_*.csv` with an honest note, because values from
  `teamplayerlinks` were not live.
- Added the `key` op to Turbo's dev service instead of asking the user to press keys.
- Manager minifaces are 512x512 (same tools as players).

## 9. Working with this user

Wants a status roughly every 30 minutes in plain language (the session used `SendUserMessage`), a working build as soon as
possible, and the work focused on their priorities. Their saved preferences ask for an honest report of what was run
versus not run: say "verified in game", "database only", or "built, not tried" and do not blur them.
Tone of recent messages: impatient with slow in-game automation ("this is taking too long").

## 10. Resuming the build

The user asked "can you resume the build?" at about 17:10 UTC, right after this handover was written.

- Only session `cse_018KNHBkXKaKC6RHBU17Afhu` can continue the work: it has the 0.3.0 source, the connection to the user's PC
  and the context. A follow-up session has none of those.
- Its limit resets at **19:00 UTC**. A message sent before then would only hit the limit again.
- A scheduled routine cannot start it: `create_trigger` was refused with "This session only accepts scheduled tasks bound
  to its own computer". So it has to be woken by a message.
- The wake-up message is in `docs/RESUME-PROMPT.md`. Its first step is to export and push the unpushed commits (section 3),
  its second is the P1 / P2 list in section 7, and it asks the user the rule question in section 1.
- A reminder for 19:02 UTC was set in the follow-up session that wrote this file, to send that prompt with `send_message`.
  If that reminder does not arrive, the user can paste the prompt into the session from the desktop app.

