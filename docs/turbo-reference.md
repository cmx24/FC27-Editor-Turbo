# FC 27 LE Turbo — technical reference (0.2.5)

Turbo adds FC 26 Live Editor features to **FC 27 Live Editor** (public build v27.1.0 or newer). It is a user-owned add-on
that runs next to an official, unmodified Live Editor. Offline Career Mode / Kick-Off only.

## Ground rules

- Live Editor's own files (`FCLiveEditor.DLL`, launchers, Lua libs) are never modified, copied or redistributed.
  The test fixtures that need Live Editor's Lua libs are supplied by the user (`turbo/le27/README.txt`).
- Nothing in Turbo decompiles, disassembles or patches Live Editor or the game, bypasses authentication or touches the
  anti-cheat. `Turbo.dll` refuses to start unless Live Editor is running in the same process (or Turbo's Lua side,
  started by Live Editor, has just run).
- Every database write is range-checked against the field's own metadata; destructive actions need a confirmation.

## Launch safety (0.2.1, revised in 0.2.3)

Turbo 0.2.0 broke game launch: `lua\autorun\turbo_boot.lua` ran while Live Editor was still initialising the game
(Live Editor injects ~100 ms after `FC27.exe` starts), and it immediately called `GetPlugin`/`GetDBMeta`, registered an
undocumented `post__LEInitDoneEvent` handler, and loaded `Turbo.dll`, which probed Direct3D 12 and hooked DXGI/D3D12 before the
game had a window. The user's Live Editor log shows the game dying inside that autorun call. Which of those steps did it was
never isolated, so every one of them is removed or neutralised.

0.2.1 and 0.2.2 loaded nothing at launch: the GUI started only when the user ran `turbo_gui_load.lua`. In the first in-game test of
0.2.2 the game launched fine, but F8 did nothing: `turbo_boot.log` shows that `turbo_gui_load.lua` never ran, so `Turbo.dll` was
never loaded. 0.2.3 therefore loads `Turbo.dll` at launch again, but inert. The rules since 0.2.3:

1. **No game native at launch.** `TURBO.boot({at_launch = true})` reads the config, registers the documented `post__CareerModeEvent`
   handler (the bridge connects on the first career event) and calls `bridge.load_gui("launch")`. No game native (not even
   `IsInCM`), no memory access, no other event name. `t08_launch_safety.lua` instruments every game native and `package.loadlib`:
   at launch it allows exactly one `package.loadlib`, of `turbo\Turbo.dll`, in launch mode, and nothing else
   (mutation-checked: starting the bridge at launch, or loading in "now" mode, makes it fail). `gui.autoload = false` restores
   the 0.2.2 behaviour (nothing loaded at launch).
2. **`Turbo.dll` imports no graphics DLL.** Its import table is `KERNEL32`, `USER32`, `GDI32`, `msvcrt` only; Direct3D 12, DXGI, the
   shader compiler and DWM are resolved on first use (`src/win/lazy_imports.cpp`), and ImGui's shell functions are compiled out.
   `build_win.sh` fails if any of them reappears in the import table, and the smoke test checks that loading `Turbo.dll` brings none
   of them into the process (mutation-checked with the 0.2.2 DLL, which fails it). Its `DllMain` only pins the module, finds the
   Live Editor folder and starts a thread.
3. **Launch mode: wait for Live Editor.** The Lua side writes `turbo_output\turbo_gui_load.json` (`{"mode":"launch"}` from autorun,
   `{"mode":"now"}` from `turbo_gui_load.lua` / a career event) right before it loads the DLL. In launch mode the DLL touches nothing
   (no window search, no Direct3D) until Live Editor writes `Initial setup done` for this process in `Logs\live_editor_<date>.log`
   (the session is found by the `Module <FCLiveEditor.DLL> 0x<base>-` header with this process's module base; the last such
   header counts; launcher logs are ignored), or until Turbo's Lua side writes `bridge_state.json` after the DLL was loaded (career
   event or `turbo_gui_load.lua`). In the user's logs `Initial setup done` comes ~15 s after `Main Menu reached`, after Live Editor
   has installed its own Direct3D 12 hooks, so Turbo always hooks after Live Editor. A mode file older than 2 minutes counts as
   launch mode; `TurboInjector.exe` loads in launch mode too (the line is already in the log then).
4. **Then `Turbo.dll` waits for the game** before touching Direct3D: a visible game window, `d3d12.dll` + `dxgi.dll` loaded, then a settle
   time (defaults: up to 180 s, settle 5 s; env `TURBO_GUI_WAIT_MS`, `TURBO_GUI_SETTLE_MS`). Until then nothing is probed or hooked.
5. **Kill switch**: `turbo_output\turbo_gui_disable.txt` (or env `TURBO_GUI_DISABLE=1`) keeps `Turbo.dll` from starting; it is checked
   at load and again after the wait for Live Editor.
6. **Crash guard**: `turbo_output\turbo_gui_start.flag` exists while hooks are being installed / proven (300 frames) and while the
   first frames are drawn on screen (120 frames). A clean refusal or a clean process exit removes it; if the game dies in those
   phases it stays and the next start refuses to hook (message in `turbo_gui.log`; delete the file to try again).
7. **Stale files are never trusted**: `bridge_dll.json` carries `updated` (unix seconds), refreshed every ~2 s by the DLL. Lua
   reads the mailbox address only from a file younger than 15 s; an address left by an earlier game session is never dereferenced.
8. **Breadcrumbs**: every Lua start-up step is appended to `turbo_output\turbo_boot.log` *before* it runs (bounded to 64 KB), the
   GUI load result goes to Live Editor's log (`[Turbo]` lines), and the DLL logs each step (waiting for Live Editor, the game
   window, probing, MinHook, hooks, first frame) to `turbo_gui.log`, so a failure shows where it stopped.

## Two parts

| Part | Where | What |
| --- | --- | --- |
| Turbo (Lua) | `turbo/package` | Feature pack that runs inside Live Editor's Lua engine: 19 feature modules, 25 `turbo_*.lua` runner scripts, auto features, config, and the GUI bridge. |
| Turbo GUI (C++) | `turbogui/` | `Turbo.dll`: a Dear ImGui overlay (DirectX 12 `Present` hook via MinHook) with Players / Teams / Managers / Database editors and a button for every Turbo tool. `TurboProbe.exe` finds the hook targets in a separate process. `TurboInjector.exe` is an optional loader. |

Turbo.dll reads and writes the live database itself (`ReadProcessMemory`/`WriteProcessMemory` on the game process, using the
T3DB layout below). Anything that needs game functions (transfers, bans, form, ...) is sent to the Lua side through a mailbox
and run by Live Editor's Lua engine on the next career-mode event (or when the user runs `turbo_exec.lua`).

## Lua side (`turbo/package/lua`)

- `autorun/turbo_boot.lua` → `TURBO.boot({at_launch = true})`: no game native (see Launch safety); registers the career event and
  loads `turbo\Turbo.dll` in launch mode (`gui.autoload`, default true).
- `scripts/turbo_gui_load.lua` → `bridge.start()`: writes the bridge files, registers the career-event tap and loads `turbo\Turbo.dll`
  with `package.loadlib` if it is not loaded yet. Every outcome, including a missing Turbo library, ends in a message box and a
  `[Turbo]` line in Live Editor's log. The first career event does the same automatically.
- `libs/v2/imports/turbo/turbo.lua`: module registry and `M.run(name, overrides, opts)` (`opts.silent` suppresses the message box).
- `core/`: version, log, trace (start-up breadcrumbs), util, env, config, db (validated writes), game, mem, events, csv, calib, select.
- `features/`: probe, form_morale, pap_playstyles, custom_headassets, custom_tattoos, delete_generated_players,
  export_season_stats, export_fixtures, export_transfer_history, extend_cpu_contracts, extend_user_contracts, headmodels,
  transfer_bans, squad_role, team_jersey_numbers, bulk_edit, player_moves, db_edit, export_table, player_presets
  (export to Live Editor preset CSV `extensions\player_presets` + Turbo player JSON with the miniface in
  `turbo_output\players`; import a preset onto a player by groups), create_player (new player rows via InsertDBTableRow:
  copy of a player, from a preset file, or blank; `core/preset.lua` parses LE CSV / FC 26 cards CSV / Turbo JSON).
  GUI: Players tab buttons Export... / Import... / Clone... / Create player... (`turbogui/src/ui/ui_presets.cpp`).
- Settings: `turbo_config.json`; settings changed in the GUI are saved in `turbo_output\gui_settings.json` and win over the file.
- Memory-based features (fixtures, transfer history, squad roles) try the FC 26 layout, then search, and only accept memory whose
  contents look right (real teams/players/dates); results are cached per build key (`LE_VERSION|LE_GAME_MODULE_SIZE`).

## T3DB in-memory layout (as used by Live Editor's own Lua t3db library)

- Service: `GetPlugin(ENUM_djb2Database_CLSS = 0x0ae932d0) - 8`, then the pointer chain `{0x20, 0x08, 0x10}` (each step dereferenced) to the first DB node.
- DB node: `+0x10` first table, `+0x18` next DB node.
- Table header: `+0x08` next table, `+0x30` first record, `+0x40` shortname[4], `+0x44` record size, `+0x7C` written records (u16),
  `+0x82` column count (u8), `+0x84` columns (0x10 each: type, bit offset, shortname at `+8`).
- Records: the last byte's bit `0x80` marks a deleted record. Field types: 3 = int, 4 = float, 0 = string.
  Int value = stored bits + `min`; depth and `min` come from `GetDBMeta()`.
- `GetManagerObjByTypeId` walks the comm plugin (`0x1297f047`) through `{0x20, 0x10}`, then `mode_managers + 0x20 × type`.
  FC 27 manager type IDs: UserManager 129, PlayerStatusManager 87, TransferManager 127, CalendarManager 24.
- Player `birthdate` / `playerjointeamdate` are Lilian day numbers (day 1 = 1582-10-15). Live Editor's `DATE:FromGregorianDays`
  is one day off for many dates; Turbo converts with its own exact inverse (`core/util.lua`, `model.cpp`).

## Bridge contract (Lua ↔ Turbo.dll)

Files in `<LE>\turbo_output` (falling back to `<LE>`):

| File | Written by | Content |
| --- | --- | --- |
| `bridge_meta.json` | Lua | `{session, shortname_name_tables_map{short: name}, field_desc_map{tshort: {fshort: {name, depth, min}}}}` |
| `bridge_state.json` | Lua | `session, seq, db_gen, le_version, db_service (hex), comm_service, ifce, in_cm, user_team, date{year,month,day}` |
| `bridge_dll.json` | DLL | `{mailbox (hex address), session, gui_version, updated (unix seconds, refreshed every ~2 s)}` |
| `gui_settings.json` | DLL | `gui{toggle_key}`, `auto{form_morale{enabled,form,morale,fitness}, pap_playstyles{enabled}}`, `turbo{dry_run}` |

The DLL rebuilds its model only when `db_gen` or `db_service` changes (not on every in-game day).

Mailbox (`VirtualAlloc`ed by the DLL, 0x2020 bytes): `+0x00` magic `0x4F425254` ("TRBO"), `+0x04` version 1, `+0x08` command seq,
`+0x0C` ack seq, `+0x10` status (1 = ok), `+0x14` Lua heartbeat, `+0x20` command JSON (4096 bytes), `+0x1020` result (4096 bytes).
Commands: `{"op":"run","module":..,"overrides":{..}}`, `{"op":"boot"}`, `{"op":"ping"}`, `{"op":"refresh"}`.
The Lua side polls the mailbox on every career event (`bridge.on_career_event`) and from `turbo_exec.lua`.

## Native code (`turbogui/src`)

- `core/`: `mem.h` (Memory interface), `t3db.*` (database walk, bit-exact read/write, validation, stale-table guard), `model.*`
  (names, clubs, ages, teams, managers, dates), `bridge.*` (bridge files, mailbox), `le_log.*` (reads Live Editor's log for
  this session's `Initial setup done`).
- `ui/`: `app.*` (tick/draw, settings), `widgets.cpp` (validated field editors), `ui_players/teams/database/tools.cpp`, `playstyles.h`.
- Callnames (Players > Callname tab, `core/callnames.*`, `ui/ui_callnames.cpp`, Lua `features/callnames.lua`): the name the
  commentary speaks for the loaded commentary language (packs found in the game folder, spoken ids from
  `turbo\callnames\spoken_<lang>.txt`, fallback = every id playernames uses), pickers by name and by player, writes to
  `players.lastnameid/commonnameid`, `editedplayernames` and `playernamemap` (rows added/removed through Lua). See `docs/callnames.md`.
- `win/`: `dllmain.cpp` (start-up checks, launch-mode wait, `luaopen_turbo_gui`), `overlay_dx12.cpp` (hooks, ImGui DX12 backend,
  WndProc), `lazy_imports.cpp` (Direct3D 12 / DXGI / D3DCompile / DWM resolved on first use), `host.h`.
- `probe/main.cpp`: `TurboProbe.exe` (see Overlay below).
- `injector/main.cpp`: `TurboInjector.exe` (waits for FC27.exe with Live Editor, then `CreateRemoteThread` + `LoadLibraryW`).

## Overlay (`win/overlay_dx12.cpp`)

- **Hook targets**: `IDXGISwapChain::Present` / `Present1` / `ResizeBuffers`, `IDXGISwapChain3::ResizeBuffers1`,
  `ID3D12CommandQueue::ExecuteCommandLists`. Since 0.2.2 they are found by `turbo\TurboProbe.exe`, a separate windowless process
  that creates its own DXGI factory, D3D12 device and swap chain and prints, per function, the module path, image size, time
  stamp, offset and 32 code bytes from offset 16 (past any inline hook another overlay placed at the start). `Turbo.dll` uses an
  address only if the game has loaded that very module (same path, size and time stamp), the address is executable and the
  code bytes match. Otherwise (no `TurboProbe.exe`, a different `D3D12Core.dll` such as a game-shipped Agility SDK, a 20 s
  time-out) it falls back to the 0.2.1 probe inside the game. `turbo_gui.log` says which path was used. Reason: under Wine +
  vkd3d, creating a DXGI factory inside a program while it presents deadlocks (reproduced without any Turbo code); the
  separate process avoids that whole class of problem.
- **Queue**: the overlay draws on the queue the swap chain was created on (DXGI returns it), or, if that is not available,
  the last direct queue of the same device that submitted work (seen by the `ExecuteCommandLists` hook). ImGui uploads
  fonts through its own queue.
- **Input**: the window procedure only queues messages (it never waits for the render thread). The render thread polls the
  show/hide key (`GetAsyncKeyState`, game in the foreground) and also counts key messages, so one press is seen whether the
  game gets window messages or only raw input (presses within 250 ms count once). Mouse position and buttons are polled too.
  While the Turbo window is shown and wants the mouse or keyboard, those messages are kept from the game.
- **Failure handling**: every frame runs behind an exception barrier; any error switches the overlay off for the session and
  the game keeps running. The main window is clamped to the screen size.
- Third party (vendored in `turbogui/third_party`): Dear ImGui v1.92.9, MinHook v1.3.4, nlohmann/json 3.11.3.
- Toolchain: mingw-w64 13 (posix threads) with `-DWIDL_EXPLICIT_AGGREGATE_RETURNS` (needed for the D3D12 descriptor-handle ABI), static linking.

## Build and test

| Command | What it does | Needs |
| --- | --- | --- |
| `bash turbo/tests/run_tests.sh` | 105 Lua tests over a simulated game memory (incl. launch safety; `GetDBMeta` as Lua tables, as iterable C++ objects and as index-only C++ objects) | lua5.4, `turbo/le27/libs` |
| `luacheck --config turbo/tests/.luacheckrc turbo/package/lua` | lint | luacheck |
| `bash turbogui/tests/native/run_native.sh` | 3,283 native checks (engine vs Live Editor's Lua T3DB library, headless ImGui UI driving, ASan + UBSan). Includes clicking every GUI button that sends a command (28) and running each command through Turbo's real Lua bridge in a simulated career, the Players list filters against a brute-force oracle, and the Live Editor log reader against real FC 27 log lines | g++, lua5.4, `turbo/le27/libs` |
| `python3 scripts/check_field_names.py` | every table / field name in the GUI and Lua sources against EA's `db_meta.xml` (FIFA 21 Live Editor) and the names xAranaktu's FC 24–26 Live Editor scripts use | git, GitHub access |
| `bash turbogui/scripts/build_win.sh` | cross-compiles `Turbo.dll` + `TurboProbe.exe` + `TurboInjector.exe` | mingw-w64 (posix) |
| `bash turbogui/tests/win/run_smoke.sh` | loads the real `Turbo.dll` under Wine in 10 modes: imports, refusal without Live Editor, no hooking without a game window, kill switch, crash guard (clean exit / kill), mailbox, Lua-side start, and launch mode (touches nothing and loads no graphics DLL until Live Editor's `Initial setup done` for this process or a fresh `bridge_state.json`; earlier sessions, the launcher log and stale files do not count) | Wine, mingw-w64 |
| `bash turbogui/tests/win/run_overlay_wine.sh` (`MODE=probe` default, `MODE=fallback`) | runs `tests/win/game_stub.cpp`, a Direct3D 12 stand-in game (not FC 27), under Wine + vkd3d + Mesa lavapipe on Xvfb; it loads `Turbo.dll` in launch mode while presenting, writes Live Editor's `Initial setup done` half a second later, presses F8 from another thread and resizes its swap chain. Checks the log (waited for Live Editor before anything else, hooked, queue, ImGui ready, start and drawing proven, no errors, crash flag cleared) and screenshots (nothing drawn while hidden, the window after F8 and after the resize) | Wine, Xvfb, mesa-vulkan-drivers, ImageMagick |
| `bash scripts/package.sh` | builds `dist/FC27_LE_Turbo_<version>.zip` (the files to copy into the Live Editor folder) | zip, built binaries |

## What is and is not verified

Verified by execution (see the commands above): the Lua feature pack and bridge against a simulated game memory; the native
database engine, model and every UI panel against a memory image produced by the same simulator; every GUI command through
the real Lua side; every database name against independent schema sources; the Windows `Turbo.dll` loads under Wine,
exports `luaopen_turbo_gui`, refuses to start without Live Editor, starts from the Lua side's signal and publishes a valid
mailbox; loaded in launch mode it touches nothing and loads no graphics DLL until Live Editor's `Initial setup done` for its
session; inside a running Direct3D 12 stand-in game (Wine + vkd3d) it waits for that line, hooks, draws its window on F8 and
survives a resize, both with `TurboProbe.exe` and with the in-game fallback.

Verified in FC 27 by the user (02-10-2026):
- 0.2.2 (Live Editor log): `turbo_boot.lua` runs at launch and the game launches.
- 0.2.3 (screenshot, Live Editor v27.1.2, NVIDIA RTX 5090 laptop): the game launches with the launch-time `package.loadlib` of
  `Turbo.dll`; `Turbo.dll` waited for Live Editor's `Initial setup done`, hooked Direct3D 12 and drew its window in game on F8,
  next to Live Editor's own window. It did not connect to the database: the window stayed "waiting for Turbo's Lua side
  (bridge_meta.json)" after `turbo_gui_load.lua`.

That points at `bridge.write_meta`: it accepted `GetDBMeta()` only as plain Lua tables (the simulator's form), while Live
Editor's own t3db code only indexes the result, which also works for C++ objects (userdata). 0.2.4 reads the meta by indexing,
iterates it only inside `pcall`, and falls back to Live Editor's own table loader (`LE.db`); failures are reported in the
message box, `turbo_boot.log`, Live Editor's log and the GUI (`meta_error` in `bridge_state.json`). The exact reason in 0.2.3
was not logged (it was a debug-level line), so this is the most likely cause, not a confirmed one.

Verified in FC 27 with 0.2.5 (career loaded, Live Editor v27.1.2, driven on the user's PC, 02-10-2026): the database connects
on the first career event (21,610 players, 841 teams, names via `GetDBTableRows`); an `overallrating` edit in the Turbo window
shows in the game's Team Management and was reverted; the in-game date (CalendarManager +0x34) matches the hub; the readable-memory
map is published (14,462 regions) and the in-career self-test runs without crashing: **13 passed, 0 failed, 6 skipped**; FC 27
transfer history (1,653 moves) and the user's remaining fixtures (36) export; keys typed into a Turbo text field do not reach the
game.

**Not verified in game yet:** the last input-shield hooks (GetAsyncKeyState / GetKeyState / GetKeyboardState / GetCursorPos;
built and tested under Wine only), the greyed-out buttons (native UI tests only), mailbox commands sent from the Turbo Tools tab in
the real process (the self-test runs the same Lua code directly), and anything that needs the natives FC 27 LE v27.1.2 lacks.

## FC 27 Live Editor v27.1.2 API (globals dump from the game)

Present natives: `GetDBMeta`, `GetDBTablesNames`, `GetDBTableFields`, `GetDBTableRows`, `EditDBTableField`, `InsertDBTableRow`,
`DeleteDBTableRowByAddr`, `ExecuteSQL`, `GetPlugin`, `IsInCM`, `GetSaveUID`, `GetPlayerName`, `GetTeamName`,
`SetPlayerForm/Morale/Fitness`, `PlayerHasDevelopementPlan`, `PlayerSetValueInDevelopementPlan`, event handlers, `Read*/Write*`,
`AOBScan`, `AllocateMemory`, `MEMORY`, `LE` (`LE.db`, `LE.MEMORY`, ...), `LegacyFile*`, `SendHTTPRequest`, `SaveVPRO`.

Missing although `lua\libs\v1\live_editor.lua` still defines their wrappers: every `c*` native (`cTransferPlayer`,
`cLoanPlayer`, `cReleasePlayer`, `cIs/Add/RemovePlayer*List*`, `cGet/Add/RemoveTransferBan`, `cSaveTransferBans`), plus
`GetUserTransferBudget`, `SetUserTransferBudget`, `GetCPUTransferBudget`, `GetPlayersStats`, `DeletePlayer`, `TerminateLoan`,
`PlayerExists`, `GetTeamIdFromPlayerId`, `GetCompetitionNameByObjID`, `GetCurrentDate` (only the v2 Lua helper exists),
`PlayerDevelopmentManager*`. Placeholders: `GetTransferBudget` / `SetTransferBudget` (print "deprecated"), `SetSquadRole`
(prints "TODO: FC27").

`core/env.lua` `env.api(name)` returns the function only when it is usable: for a Lua function from Live Editor's `lua\libs`
it reads the function's source lines and refuses placeholders (TODO / NOT IMPLEMENTED / deprecated) and any `c<Name>(` call
whose native is missing. `core/caps.lua` maps Turbo tools to the functions they need; the bridge publishes
`"unavailable": {tool: reason}` in `bridge_state.json`, the Turbo window disables those buttons (tooltip = reason) and lists
them in the Status tab, and the self-test reports them as SKIP. Tested with `H.setup({le_27_1_2 = true})` (t10).

## FC 27 memory layouts found in game (values, not code)

| What | Where | Used by |
| --- | --- | --- |
| In-game date | CalendarManager (type 24) +0x34 day, +0x38 month, +0x3C year | `game.calendar_date` |
| Completed transfers / loans of the season | TransferManager (127): eastl lists, head `{first, last}` at +0x2998 (transfers) and +0x29D8 (loans); node +0x00 next, +0x08 prev, +0x10 playerid, +0x14 club he moves to, +0x18 club he leaves, +0x24 date YYYYMMDD, +0x30 fee (transfers) / small number (loans). Open AI offers +0x2978; offers for the user's players +0x29F8, +0x2A18. Completed lists are recognised by ≥ 50 % of players now at the +0x14 club | `export_transfer_history` (FC 26 storage first) |
| User club's remaining fixtures | MainHubManager (58): count at +0x60, pointer at +0x68 to 0x130-byte entries; +0x2C competition object id, +0x38 date, +0x3C time HHMM, +0x128 home club, +0x12C away club | `export_fixtures` (FC 26 lists first) |
| Squad roles | PlayerStatusManager (87) +0x18, 8-byte entries | `squad_role` (unchanged) |
| AI clubs' finances (not the user's) | BudgetManager (23) +0x18 bucket array, +0x20 bucket count 823, key `teamid % 823`, node `{teamid, 2, value +0x08, value +0x10, ...}` | not used yet (research for the transfer budget) |

Not found: FC 26's FCEDataManager fixture/standings lists (+0x60/+0x88 hold other objects in FC 27), live season stats, the user
club's transfer budget. The probe scripts that found the above are in `C:\FC 27 Live Editor\turbo_dev\probe_*.lua` on the PC.

## FC 26 → FC 27 differences that matter

- `career_mode/FCECareerModeUserManager.lua`: `mUserType` `0x37→0x2F`, `mPlayerId` `0x3C→0x34`, VPRO playerid `30999→31399`.
- `career_mode/helpers.lua`: `SetSquadRole` is a stub in FC 27 LE; user club team id = `ReadInt(ReadPointer(UserManager+0x18)+0x1F4)`.
- Career event constants were renamed `ENUM_CM_EVENT_MSG_*` → `ENUM_FCEGameModesCM_EVENT_MSG_*` (same IDs). Turbo resolves both names.
- Match sharpness no longer exists in FC 27 (`SetPlayerSharpness` removed).
- `cm_teamsheets` numbers its slots `playerid0..playerid51`; FC 27 LE's `GetUserSeniorTeamPlayerIDs` starts at 1 and misses
  slot 0 (the goalkeeper). Turbo's `game.user_squad` reads slot 0.
- `teams` has no `transferbudget`; `playernames.name` is a compressed field (type 13) decoded only by `GetDBTableRows`;
  the `transfers` and `fixtures` tables are empty in a career; `teamplayerlinks.leagueappearances` stays 0.
- Live Editor's `MessageBox` is printf-formatted (`%` must be `%%`); its `MEMORY` reads crash the game on unreadable addresses.

## Team name, colours and crest (Teams tab, 0.4)

- **Name** (`ui/ui_identity.cpp`, `core/teamnames.*`): writes `teams.teamname` (validated through `Database::set`) and the
  four rows Live Editor itself reads from `<LE>\extensions\global\custom_team_names.csv` (`TeamName_<id>`,
  `TeamName_Abbr3_<id>`, `TeamName_Abbr10_<id>`, `TeamName_Abbr15_<id>`; `key;value`, other rows kept, written as
  `.tmp` + rename, previous file copied to `turbo_output\team_name_backups`). Live Editor reads that file when it starts,
  so the game shows the new name after Live Editor's next start; the UI says so.
- **Colours**: `ImGui::ColorEdit3` pickers for `teams.teamcolor1..3`, `goalnetstanchioncolor1..2` and, per kit row of
  `teamkits` (`teamtechid == teamid`, grouped by `teamkittypetechid`), `teamcolorprim/sec/tert`, `jerseynamecolor`,
  `jerseynameoutlinecolor`, `jerseynumbercolorprim/sec/ter`, `shortsnumbercolorprim/sec/ter` plus the percent / font / template
  ints. A picker's value is written once the mouse is up (or after 0.8 s), three validated `Database::set` calls (r, g, b).
- **Crest**: a club's crest is a set of legacy files `data/ui/imgAssets/crest{,16x16,32x32,50x50,512x512,1024x1024}/{light,dark,custom}/l<teamid>.dds`
  (`crest_variants()`; which ones exist differs per club, see `legacy_filename_hash_list.csv`). The editor asks the Lua
  exporter for every variant, reads each exported file's DDS header (`parse_dds_format`) and writes the new picture with
  `encode_dds` in the SAME size, pixel format (B8G8R8A8 / R8G8B8A8 / X8 / 24-bit, DXT1 / DXT3 / DXT5, legacy or DX10
  header; the original header bytes are reused) and mip count. Save is disabled until every variant is known (exported or
  in `missing.txt`). Files go to `<LE>\mods\legacy\...`, backups to `turbo_output\crest_backups`; "Remove custom crest"
  deletes them all (confirmation). "Another club's crest" copies the other club's files byte for byte per variant, or
  uses its big crest as a picture to reframe. Minifaces keep their own `encode_dds_dxt5` path.

## Not implemented yet

Match setup overrides, gameplay toggles (CPU vs CPU, unlimited subs, never tired, match time/score), manager market / job security /
fire, endless career, reveal player data, negotiation bypasses, match-fixing, job offers and minifaces. These need code hooks
inside FC27.exe and in-game analysis first. The full FC 26 → Turbo map is `docs/fc26-parity.md`.
