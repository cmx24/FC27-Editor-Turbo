# Handover: FC 27 LE Turbo, continue locally (Claude Cowork)

Written at Turbo **0.2.5** on branch `claude/trusting-cannon-rkxwrw` of `cmx24/FC27-Editor-Turbo` (see `git log` for the
commit). The work runs in a cloud workspace linked to the user's PC (Claude desktop app): code is built and tested in the
cloud, deployed into `C:\FC 27 Live Editor` and tested in the real game by driving it on the user's screen.

## 1. What this project is

**FC 27 LE Turbo** brings FC 26 Live Editor features to **FC 27 Live Editor** (xAranaktu, **v27.1.2** at
`C:\FC 27 Live Editor`, game at `C:\Program Files\EA Games\EA SPORTS FC 27`). Two parts:

| Part | Source | Installed as |
| --- | --- | --- |
| Lua feature pack + GUI bridge | `turbo/package/lua/...` | `lua\autorun\turbo_boot.lua`, `lua\scripts\turbo_*.lua` (28), `lua\libs\v2\imports\turbo\` |
| Turbo GUI (C++): Dear ImGui overlay drawn in the game via Direct3D 12 hooks | `turbogui/src` | `turbo\Turbo.dll`, `turbo\TurboProbe.exe`, `turbo\TurboInjector.exe` |

Install = unzip `dist/FC27_LE_Turbo_0.2.5.zip` into `C:\FC 27 Live Editor` (never overwrites Live Editor files).
Reference docs: `docs/turbo-reference.md` (architecture, bridge contract, launch safety, memory safety, FC 27 layouts,
tests), `docs/fc26-parity.md` (FC 26 feature groups and where they are), `turbo/package/TURBO_README.md` (user guide,
recovery table, in-game checklist).

### Ground rules (keep them)

- Offline Career Mode / Kick-Off only. Never touch online modes. At "You are offline" choose **Play Offline**.
- Never modify, decompile, disassemble or patch `FCLiveEditor.DLL` or the game. No bypassing Live Editor's Patreon
  authentication, no anti-cheat work. (Reading/writing game data through Live Editor's Lua API and Turbo's guarded
  memory reads is the product; finding data layouts by reading values, never by reading code.)
- Never commit Live Editor's files (its `lua\libs` is a test fixture in `turbo/le27/libs`, gitignored).
- Never commit personal data: Live Editor's logs contain the user's Patreon ID and name. Quote only the lines you need.
- Every database write is range-checked; destructive actions need confirmation.
- **Never save the user's career.** Revert any test edit before leaving the career (the career save is backed up).
- Commit on `claude/trusting-cannon-rkxwrw`; no pull request unless the user asks. Commit trailer:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` plus the `Claude-Session:` line.

## 2. State at this handover (verified in game, 02-10-2026, career "Torino FC", in-game date 2026-08-30)

| Area | Result |
| --- | --- |
| Launch | Game launches; Turbo.dll waits for Live Editor's "Initial setup done", skips TurboProbe (game ships its own D3D12Core.dll), probes in game, hooks, F8 shows the window. |
| Readable-memory map | `readable-memory map for Turbo's Lua side: 14462 regions`. Since it, the in-career self-test never crashed the game (it did twice before). |
| Database | Connects on the first career event; 21,610 players, 841 teams; names decoded (43,065 from `GetDBTableRows("playernames")`). |
| Player edit | Jonathan Silva overallrating 70 → 71 in the Turbo window showed "Baseline OVR 71" in the game's Team Management; set back to 70 and confirmed in the database. |
| In-game date | `2026-08-30 (calendar+0x34)` matches the career hub. |
| Self-test (`turbo_selftest.lua`) | **13 passed, 0 failed, 6 skipped**. Skipped = this Live Editor build has no native for it (below). |
| Exports | Transfer history 1,653 moves (FC 27 TransferManager lists); fixtures: 36 remaining Torino fixtures (MainHubManager); database league numbers 3,847 lines; jersey numbers; tables; probe report. |
| Input | Keyboard typed into a Turbo text field did **not** reach the game (tested with a game hotkey and Enter). A mouse click on the Turbo window still moved the game's menu focus to the card under the cursor (0.2.5 build before the last one). The last build also hooks GetAsyncKeyState / GetKeyState / GetKeyboardState / GetCursorPos; it is **installed but not yet tried in game**. |

### FC 27 Live Editor v27.1.2 does not have these natives

Globals dump from the game (`_G` in Live Editor's Lua Engine): the FC 26 names still exist as Lua wrappers in
`lua\libs\v1\live_editor.lua`, but the `c*` natives behind them are missing, and some v2 helpers print `TODO: FC27`.

| Missing | Turbo tools affected (greyed out with the reason; self-test SKIP) |
| --- | --- |
| `GetUserTransferBudget`, `SetUserTransferBudget` (and `GetTransferBudget` is a deprecation stub) | Your club: transfer budget |
| `cTransferPlayer`, `cLoanPlayer`, `cReleasePlayer`, `cAdd/RemovePlayerTo/FromTransfer/LoanList`, `cIsPlayer*Listed`, `TerminateLoan` | Players tab: Transfer / Loan / Release / Terminate loan / Transfer list / Loan list / Remove from lists |
| `cGetTransferBans`, `cAddTransferBan`, `cRemoveTransferBan` | Transfer bans |
| `DeletePlayer` | Delete player, Delete generated players (counting still works) |
| `PlayerDevelopmentManager*` | Bulk edit: development |
| `GetPlayersStats` | Season stats: exports the database's league numbers instead, labelled as not live |
| `SetSquadRole` (Lua placeholder) | none: Turbo writes squad roles through memory itself |

Present and used: `GetDBMeta`, `GetDBTableRows`, `GetDBTableFields`, `EditDBTableField`, `SetPlayerForm/Morale/Fitness`,
`GetPlugin`, `IsInCM`, `MEMORY`, `LE.db`, event handlers. Detection is generic (`env.api()` reads the wrapper's source
and checks every `c<Name>(` it calls), so a Live Editor update that adds the natives lights the tools up again.

## 3. How a test session works

1. Build in the cloud (section 6), `WITH_BIN=1 bash /home/claude/bin/mkdeploy.sh` (or `scripts/package.sh`), commit the tar
   to `C:\FC 27 Live Editor\turbo_dev\turbo_deploy.tar` and extract there with
   `tar -xf turbo_dev/turbo_deploy.tar --overwrite --no-same-owner --no-same-permissions` (device shell). Compare the
   Turbo.dll sha256. The game must be closed to replace Turbo.dll; Lua files can be replaced while it runs (clear
   `package.loaded["imports/turbo/..."]` before running a script, see `turbo_dev/reload_selftest.lua`).
2. Start the game with the desktop shortcut `LaunchFC27`. It first shows "Carica / Personal Settings 1": press Enter.
   Then "EA Servers are unavailable": Enter. Maximise the window (it starts small).
3. Loading the career: Manager Career > Play Offline > **Original** > Load. The "Original" entry does not react to
   synthetic clicks or arrow keys; the user loaded the career each time. Hovering the mouse over a card and pressing Enter
   works for most other menus. **Escape does not reach the game** from the automation; use `w` (shortcut menu) +
   Enter, or the `x` / `c` tab keys. Never press Enter in the Calendar (it is "Sim To Date").
4. Live Editor's UI toggles with **Left Alt**; Features > Lua Engine; paste
   `assert(loadfile('C:/FC 27 Live Editor/lua/scripts/turbo_selftest.lua'))()` and click Execute. Results:
   `turbo_output\turbo_selftest.log`. Probe scripts used to find FC 27 layouts are in `turbo_dev\probe_*.lua`.
5. Turbo window: F8. Close the game with the window's X (no save).

## 4. Next steps (in order)

1. **Input shield in game**: with the newest Turbo.dll, click a Turbo button above a game menu card; the game's focus
   must not move. After ~1 minute `turbo_gui.log` prints `input shield: game polled ... GetAsyncKeyState n, GetKeyState n,
   GetKeyboardState n, GetCursorPos n; m blocked` — that line says which APIs FC 27 really reads.
2. **Greyed-out tools in game**: Status tab lists "Not possible with this Live Editor build"; Players tab move buttons
   and the budget / bans buttons are disabled with tooltips.
3. **Transfer budget without Live Editor's native** (research, started): `BudgetManager` (career manager type 23) has an
   AI-club hash table at `+0x18` (823 buckets at `+0x20`, `teamid % 823`, node `{teamid, 2, value@+0x08, value@+0x10,
   ...}`); the user's club is **not** in it (bucket 54 empty). The UI showed Torino's budget as $40M (current) / $43M (total);
   the game stores values already in the display currency (BudgetManager item ranges are multiples of 114,000 = 100,000 ×
   1.14). Candidates seen: `TcmFinanceManager` (type 65). Only implement after a differential test (change the budget in
   game by a legal action, diff, revert) proves the field.
4. **Full fixtures / results and live season stats**: not found yet. FC 26's FCEDataManager lists (+0x60/+0x88) hold other
   data in FC 27. User fixtures come from MainHubManager `+0x60` count / `+0x68` entries (0x130 bytes). Probe output
   files in `turbo_output\probe_*.txt` on the PC show the searches done.
5. Release: `scripts/package.sh`, replace `dist/` zip, push the branch (the cloud workspace has no push credential; see 5).

## 5. Getting commits to GitHub

The cloud repo at `/home/claude/FC27-Editor-Turbo` has no remote credential. Earlier sessions downloaded the repo
through the browser pane (GitHub signed in). To publish: `git bundle create turbo.bundle <base>..HEAD`, commit it to
`C:\FC 27 Live Editor\turbo_dev\`, and on the PC run `git pull <path>\turbo.bundle claude/trusting-cannon-rkxwrw` in a
clone, then push; or ask the user to push. The bundle for this handover is `turbo_dev\turbo_0.2.5.bundle` (made from
`379554c`, the snapshot of `195cdb5`).

## 6. Build and test

Scripts are bash on Ubuntu (mingw-w64 posix, wine64, lua5.4, luacheck, zip, xvfb, mesa-vulkan-drivers, imagemagick).
Copy `C:\FC 27 Live Editor\lua\libs` to `turbo/le27/libs` first (gitignored fixture; also `turbo/le27/fc27_db_schema.json`
from `turbo_output\fc27_db_schema.json` for the schema check).

| Command | Result at this handover |
| --- | --- |
| `bash turbo/tests/run_tests.sh` | TOTAL: 129 passed, 0 failed (10 files; t10 = FC 27 LE v27.1.2 natives only) |
| `luacheck --config turbo/tests/.luacheckrc turbo/package/lua` | 0 warnings / 0 errors |
| `bash turbogui/tests/native/run_native.sh` | RESULT 3336 passed, 0 failed (ASan + UBSan) |
| `bash turbogui/scripts/build_win.sh` | 0 warnings |
| `bash turbogui/tests/win/run_smoke.sh` | ALL SMOKE MODES PASSED (11 modes) |
| `MODE=probe` / `MODE=fallback bash turbogui/tests/win/run_overlay_wine.sh` | OVERLAY TEST PASSED (8 input hooks, game's polled input still works) |
| `python3 scripts/check_fc27_schema.py` | 116 names checked, 106 present, 10 optional absent, 0 missing |
| `bash scripts/package.sh` | `dist/FC27_LE_Turbo_0.2.5.zip` |

The simulator (`turbo/tests/mock/sim.lua`) models game memory and Live Editor natives; `H.setup({le_27_1_2 = true})`
removes every native the real v27.1.2 lacks. `world.lua` options `fc27_transfer_lists` and `fc27_user_fixtures` build the
FC 27 layouts seen in game.

## 7. Facts worth keeping

- Live Editor log: `Initial setup done` ~30 s after start once the settings file is loaded; Lua Engine runs on the game
  thread (a long script freezes the game until it ends — keep probes short).
- Live Editor's MessageBox formats text like printf (`%` must be doubled). Its MEMORY natives crash on unreadable
  addresses: Turbo reads only through `core/mem.lua` (readable-memory map published by Turbo.dll).
- FC 27 DB: 268 tables; `playernames.name` is compressed (field type 13) and only `GetDBTableRows` decodes it;
  `cm_teamsheets` has `playerid0..playerid51` (FC 27 LE's helper skips playerid0, the goalkeeper);
  `transfers` and `fixtures` tables are empty in a career; `teamplayerlinks.leagueappearances` stays 0.
- Career managers (comm service plugin, `GetManagerObjByTypeId` walk): CalendarManager 24 (date at +0x34/+0x38/+0x3C),
  MainHubManager 58, BudgetManager 23, TransferManager 127 (eastl lists: +0x2998 completed transfers, +0x29D8 loans,
  +0x2978 open AI offers, +0x29F8/+0x2A18 offers for the user's players; node +0x10 player, +0x14 to club, +0x18 from club,
  +0x24 date, +0x30 fee), PlayerStatusManager 87 (squad roles at +0x18).
- GPU: NVIDIA RTX 5090 Laptop; game window 2582x1550 maximised, swap chain format 24, 3 back buffers.
