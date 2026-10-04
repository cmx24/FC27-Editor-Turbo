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
  Every Browse... of Turbo (export folders and file name, import file, crest and miniface pictures) is the in-overlay
  picker `turbogui/src/ui/file_picker.cpp`: no Windows file dialog, Explorer or console window opens by itself, because
  any other window takes the game out of full screen. The GUI creates the folders it sends to Lua (`ensure_folder`);
  `player_presets.lua` runs cmd.exe `mkdir` only for a folder that really is missing (`dir_exists` needs no process).
  Last folder per picker: `turbo_output\gui_folders.json`. `player_presets.json_dir` = the JSON / miniface folder.
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
| `bridge_state.json` | Lua | `session, seq, db_gen, load_gen, le_version, db_service (hex), comm_service, ifce, in_cm, user_team, date{year,month,day}` |
| `bridge_dll.json` | DLL | `{mailbox (hex address), session, gui_version, updated (unix seconds, refreshed every ~2 s)}` |
| `gui_settings.json` | DLL | `gui{toggle_key}`, `auto{form_morale{enabled,form,morale,fitness}, pap_playstyles{enabled}}`, `turbo{dry_run}` |

The DLL rebuilds its model only when `db_gen` or `db_service` changes (not on every in-game day). `db_gen` also changes
when the manager changes club or on a `refresh` command; `load_gen` (1.0.2) only when a career is loaded, entered or left
(a reload event, `in_cm` flipping, another database service): the kept edits are written again once per `load_gen`.

Mailbox (`VirtualAlloc`ed by the DLL, 0x2020 bytes): `+0x00` magic `0x4F425254` ("TRBO"), `+0x04` version 1, `+0x08` command seq,
`+0x0C` ack seq, `+0x10` status (1 = ok), `+0x14` Lua heartbeat, `+0x20` command JSON (4096 bytes), `+0x1020` result (4096 bytes).
Commands: `{"op":"run","module":..,"overrides":{..}}`, `{"op":"boot"}`, `{"op":"ping"}`, `{"op":"refresh"}`.
The Lua side polls the mailbox on every career event (`bridge.on_career_event`) and from `turbo_exec.lua`.

## Native code (`turbogui/src`)

- `core/`: `mem.h` (Memory interface), `t3db.*` (database walk, bit-exact read/write, validation, stale-table guard), `model.*`
  (names, clubs, ages, teams, managers, dates), `bridge.*` (bridge files, mailbox), `le_log.*` (reads Live Editor's log for
  this session's `Initial setup done`).
- `ui/`: `app.*` (tick/draw, settings), `widgets.cpp` (validated field editors), `ui_players/teams/database/tools.cpp`, `playstyles.h`.
- Real-face chooser (Players > Appearance and Managers > Appearance, `ui/ui_faces.*`, filters in `core/face_filter.*`): heads
  from `players` (headclasscode 0 + hashighqualityhead 1) or `manager` (headclasscode 0); filters on headtypecode (ethnicity:
  ranges of 500 grouped as European / Mediterranean / Latin / African / Asian / Mixed, named from the skin tones and nations
  of the FC 27 players in each range - FC 27 players and managers have no ethnicity field), skintonecode (10..100, Live
  Editor's labels Caucasian 1..African 3), haircolorcode / facialhaircolorcode (Live Editor's haircolor_0..14), hairtypecode
  and facialhairtypecode (the game's `imgAssets/hairstyle` / `facialhairstyle` previews), eyecolorcode. Giving a head copies
  headassetid, headclasscode, hashighqualityhead, headtypecode, headvariation (+ hair / beard / eyes / skin fields on request)
  into the target's own table; for a manager given a player's head, the player's miniface is written (512 x 512 DXT5) to
  `mods\legacy\data\ui\imgAssets\heads_staff\heads_staff_<headassetid>.dds`.
- Callnames (Players > Callname tab, `core/callnames.*`, `ui/ui_callnames.cpp`, Lua `features/callnames.lua`): the name the
  commentary speaks for the loaded commentary language (packs found in the game folder; spoken ids asked from the game's
  own audio service on the game thread - `core/commentary_audio.*`, `win/commentary_audio_win.cpp`, the Create Player
  list's filter plus the in-match player check, cached in `turbo_output\callnames\spoken_<lang>.json` - overridden by
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
- **Zoom (1.1.1, `ui/ui_zoom.*`)**: Ctrl + mouse wheel over a Turbo window changes the user's UI size factor
  (`App::ui_scale_user`, 0.1 per notch, 0.6 .. 2.5, the Turbo Tools "UI size" slider); Ctrl + 0 goes back to 1.00x. The
  render thread polls Ctrl (`GetAsyncKeyState`, game in the foreground) and hands such a wheel to `App::zoom` instead of
  Dear ImGui, so it never scrolls; Ctrl + 0 is polled the same way. The size is written to `gui_settings.json`
  (`gui.ui_scale`) once the wheel rests for 1 s. `App::update_style` rebuilds the style from Dear ImGui's unscaled sizes
  (`ImGuiStyle()` + theme, then `ScaleAllSizes`) and sets `style.FontScaleDpi`; Dear ImGui 1.92's dynamic fonts bake the
  glyphs for the new size on demand (the DX12 backend has `RendererHasTextures`), so the atlas is never rebuilt as a
  whole. Before 1.1.1 every scale change multiplied the already scaled sizes again (scrollbars and grabs grew into big
  grey ovals after a few window resizes).
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
| `python scripts/callname_voice_log.py --self-test` / `python scripts/playtest_monitor.py --self-test` | the voice-swap log parser and summary; the play-session watcher (log classifiers, following, rotation, dumps, FC27.exe start / exit, stop rules) on temporary folders | Python 3 |

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

## Competition pickers and names (Competitions tab, 1.1.1)

Every competition list of the Competitions tab (Live standings, Match setup filter, Career database copy) is one picker:
`ui/comp_picker.h` (the ImGui widget, defined in `ui_competitions.cpp`) over `core/comp_list.h` (pure, native-tested).

- **One row per competition.** A standings group sits in FCE's competition tree as group -> stage -> competition `C<id>` ->
  nation (`NationName_<nationid>`) -> confederation (type 1: `UEFA`, `CNBL`, `CCAF`, `AFC`, `CAF`, `OFC`) -> root. The groups of
  one `C<id>` are collapsed under it; the row picks the group the game's Standings view shows, else the league stage, else the
  biggest non-setup table. Stages (`FCE_Knockout_Playoff_Pots`, `FCE_Round_of_16_Pots`, `FCE_Setup_Stage` ...) open with
  `+` / Right arrow; two groups of one stage are told apart by their short name (`G1`, `G2`).
- **Names**: `leagues.leaguename` for leagues (with `leagues.level` and `countryid`); FC 27's database has no name for cups or
  continental competitions (the `competition` table has none), and Live Editor 27.1.2 lacks `GetGameLocString` and
  `GetCompetitionNameByObjID` (globals dump), so the tree's `TrophyName_Abbr15_<id>` cannot be looked up. Turbo's built-in list
  (`comps::known_competitions()`, ids as in FC's `compobj.txt`: 223 Champions League, 224 Europa League, 226 Conference League,
  232 Super Cup, 210 Coppa Italia, 201 FA Cup, 208 Copa del Rey, 1003 Libertadores ...) names them; anything else gets
  "<country> cup <id>" / "<confederation> competition <id>". Kinds: the list, else a league of the `leagues` table, else the
  tree's shape (under a confederation = continental, under a nation = cup, under the root = international).
- **Order**: your club's competitions (league first), then leagues grouped by country (tier order), cups (cups, super cups,
  playoffs), continental, other. Sort: country (default), name, clubs, id. "Leagues only" (default on) hides the non-league
  sections while the search box is empty; a search (every word in name, country, kind, id or stage; case and Latin-1 accents
  ignored) looks at every kind and opens the competitions whose stage matched.
- **Keyboard**: the search box takes the keyboard when the picker opens (it keeps Up / Down through a history callback, so
  ImGui's keyboard navigation does not move the focus away); Up / Down move, Enter picks, Esc clears the search, then closes.
- **Remembered** in `gui_settings.json` `competitions.<live|match|database>`: `{key, comp, stage, leagues_only, sort}`. The
  choice comes back by the same key (group node), else the same competition's same stage, else the competition's row.

## Team name, colours and crest (Teams tab, 0.4)

- **Name** (`ui/ui_identity.cpp`, `core/teamnames.*`): writes `teams.teamname` (validated through `Database::set`) and the
  four rows Live Editor itself reads from `<LE>\extensions\global\custom_team_names.csv` (`TeamName_<id>`,
  `TeamName_Abbr3_<id>`, `TeamName_Abbr10_<id>`, `TeamName_Abbr15_<id>`; `key;value`, other rows kept, written as
  `.tmp` + rename, previous file copied to `turbo_output\team_name_backups`). Live Editor reads that file when it starts,
  so the game shows the new name after Live Editor's next start; the UI says so.
- **Readable club names (1.1.1)**: some careers store a club's `teams.teamname` as a localization key the game could not
  resolve (`*TeamName_Abbr15_112264`). `Model` shows such a club (players' Club column, Teams list, every picker) by
  Live Editor's custom name for it (`custom_team_names.csv`: `TeamName_<id>`, else Abbr15 / Abbr10 / Abbr3; read at every
  `App::refresh`), else as `Team <id>` (`core/teamnames.*` `is_unresolved_team_name`, `Model::readable_team_name`).
  The Name tab still shows and edits the stored value.

- **Name, live (1.1.1)** (`ui/ui_identity.cpp`, `core/teamname_override.*`, `win/teamname_override_win.cpp`,
  `core/teamnames.*`; RE: `docs/re/team_names.md`). One form: **Name**, **Short name** (lists, fixtures; at most 15
  letters), **3-letter code** (scoreboard), and one **Save**. An empty short name or code is made from the name
  (shown as the box's hint: the whole name when it fits, else cut at a word end; the first three letters, upper case);
  the 10-letter form the game also asks for is made from the short name. Save, in this order:
  1. publishes the club's four strings to the `team_names` hook and keeps them in `turbo_output\team_names.json`
     (`{"turbo_team_names": 1, "teams": [{"teamid", "name", "abbr15", "abbr10", "abbr3", "when"}]}`, every career, written
     `.tmp` + rename; a file that cannot be parsed is set aside as `team_names.json.bad-<stamp>`, one that cannot be
     opened is never overwritten). The game's next lookup of that club's name shows it;
  2. writes `teams.teamname` (validated through `Database::set`; what Turbo's lists read);
  3. writes the four rows of Live Editor's `<LE>\extensions\global\custom_team_names.csv` (`TeamName_<id>`,
     `TeamName_Abbr3_<id>`, `TeamName_Abbr10_<id>`, `TeamName_Abbr15_<id>`; other rows kept, `.tmp` + rename, previous
     file copied to `turbo_output\team_name_backups`): what shows after Live Editor's next start if the hook is off.

  One line next to Save says the outcome: "Saved: shown in the game now." (plus "A game screen that is already open
  shows it once you leave that screen and come back.": view models build their text once, `docs/re/team_names.md`), or
  "Saved. The game shows it after Live Editor's next start (live names are off: <why>).", or "Not saved: <why>." with
  nothing written. The line above the form says whether live names are on, and why not. Status tab: "Live team names:
  on | 2 renamed clubs | names given to the game 57" and the hook line `team_names: on | ... | lookups | team-name keys |
  names given`, plus whether Live Editor hooks `loc_strtab_get`.

  **The hook** `team_names` on `loc_lookup` (`LocImpl::Lookup` `0x1421E2120`, `int (this, eastl::string* out, const char*
  key, int mode)`): every localized string, one level ABOVE Live Editor's hook on StrTab::GetString `0x140B1C034`. The
  original runs first; then a `[IWL_]TeamName[_Abbr15|_Abbr10|_Abbr3]_<id>` key (any case, parsed before the call) of a
  club in the published table gets Turbo's text (upper case for mode 0, the game's `"_upper"` request; Windows'
  `LCMapStringEx` invariant upper case, computed when the table is built) through the game's own
  `eastl::string::assign(const char*)` `0x1406C04D4`, and the return value 1 (found). The detour reads atomics only (an
  immutable table behind an atomic pointer; every published table is kept for the session), allocates nothing and takes
  no lock. Installed only when `loc_lookup`, `eastl_string_assign_cstr` and the guard `loc_lookup_out_assign` (Lookup's
  own `out = "*"` call at `+0x110`, which must resolve to that assign) all resolve and Lookup's entry carries no other
  module's hook. Off switches (cached, re-read every 2 s): `turbo_output\team_names_hook_off.txt`,
  `hook_team_names_off.txt`, `game_hooks_off.txt`, `TURBO_GUI_NO_GAME_HOOKS=1`; off = Save falls back to Live Editor's
  file and says so. Clubs renamed only in Live Editor's CSV keep their CSV names (Live Editor answers them below the
  hook). Not covered: clubs in GetTeamName's `TEAM_IDS` ini redirect (localized under another key).
- **Colours**: `ImGui::ColorEdit3` pickers for `teams.teamcolor1..3`, `goalnetstanchioncolor1..2` and, per kit row of
  `teamkits` (`teamtechid == teamid`, grouped by `teamkittypetechid`), `teamcolorprim/sec/tert`, `jerseynamecolor`,
  `jerseynameoutlinecolor`, `jerseynumbercolorprim/sec/ter`, `shortsnumbercolorprim/sec/ter` plus the percent / font / template
  ints. A picker's value is written once the mouse is up (or after 0.8 s), three validated `Database::set` calls (r, g, b).
- **Kit colours across career loads (1.0.2)**: FC 27 reloads `teamkits` (and `playernamemap`) from its base data every
  time a career loads; `teams.teamcolor1..3` are kept by the save. So every `teamkits` colour a picker writes
  (`write_colour` -> `App::remember_kit_colour`) is also kept in `turbo_output\reapply_edits.json` (`core/reapply.*`):
  `"kits": [{"teamtechid", "teamkittypetechid", "teamkitid", "team", "when", "fields": {"teamcolorprimr": 12, …}}]`, one
  entry per kit row (teamtechid, teamkittypetechid, teamkitid: two kits of one type keep their own colours), the latest
  value per field; only the colour channels of the Colours tab's pickers (`kit_colour_field`: `teamcolorprim/sec/tert`,
  `jerseynamecolor`, `jerseynameoutlinecolor`, `jerseynumbercolorprim/sec/ter`, `shortsnumbercolorprim/sec/ter`, each
  `r` / `g` / `b`) are kept, read back from the file and written. The first time Turbo connects to a newly loaded career
  (`App::refresh` -> `reapply_stored_edits`, once per Lua session + `load_gen`, even with the window hidden; not on a
  Refresh, not when the manager changes club) each entry is written to its row (the row with its `teamkitid`; the single
  row of that type only when no other kit of that type is kept; else left alone rather than guessed) through
  `Database::set` (range-checked, stale-table guard, no undo step), fields already in place are left alone. A colour set back to the game's own value keeps its entry (Turbo does not know the
  base value; writing it again changes nothing); **Forget** under the kit's header drops it (the colours shown stay until
  the career is loaded again). Only the picker colours are kept, not the percent / font / template ints. One summary
  line (`re-apply at career load: re-applied N kit colours, M player callnames (K already in place); J not written: …;
  I left alone: …`) goes to the GUI log, `turbo_gui.log` and the Colours / Callname tabs; a toast only when something
  was written (an error toast when a write failed). Kill switch: `turbo_output\reapply_off.txt`. An unreadable store is
  set aside as `reapply_edits.unreadable.json` at the next edit. Player callnames: `docs/callnames.md` §4.
- **Crest**: a club's crest is a set of legacy files `data/ui/imgAssets/crest{,16x16,32x32,50x50,512x512,1024x1024}/{light,dark,custom}/l<teamid>.dds`
  (`crest_variants()`; which ones exist differs per club, see `legacy_filename_hash_list.csv`). The editor asks the Lua
  exporter for every variant, reads each exported file's DDS header (`parse_dds_format`) and writes the new picture with
  `encode_dds` in the SAME size, pixel format (B8G8R8A8 / R8G8B8A8 / X8 / 24-bit, DXT1 / DXT3 / DXT5, legacy or DX10
  header; the original header bytes are reused) and mip count. Save is disabled until every variant is known (exported or
  in `missing.txt`). Files go to `<LE>\mods\legacy\...`, backups to `turbo_output\crest_backups`; "Remove custom crest"
  deletes them all (confirmation). "Another club's crest" copies the other club's files byte for byte per variant, or
  uses its big crest as a picture to reframe. Minifaces keep their own `encode_dds_dxt5` path.
- **Miniface from the 3D model** (Miniface editor tab "The 3D model", players and managers): Turbo asks the game's own
  `PlayerCaptureController` to render the player (or the manager's head) and uses the picture as the New source, then
  the usual "Save as miniface" writes the DDS. Needs the game hooks (known build) and a menu screen (career hub, squad
  screens); the first request may need the game to have captured a portrait itself (squad hub / player bio), which
  Turbo learns from and logs to `turbo_output\player_capture.log`. Camera presets are the game's (mode, extra) pairs,
  the exact framings are to be confirmed in game; "Advanced" exposes the two numbers. Off switches:
  `turbo_output\player_capture_off.txt`, `hook_pc_start_off.txt`, `hook_pc_slot_off.txt`. Code: `core/player_capture.*`
  (descriptor, callback shape, picture decode; tested), `win/player_capture_win.cpp` (hooks, request, callbacks),
  RE notes `docs/re/player_capture.md`.

## Player moves for every club (1.1.1)

Players tab: Transfer / Loan..., Release, Terminate loan, Delete player, Transfer list, Loan list, Remove from lists, List
status; Clone / Create / Import as new player. Up to 1.1.0 every move into or out of the user's club was refused (a test
career crashed while simulating after several database-only moves, 03-10-2026, root cause not found; that career had
none of the checks below) and the list buttons were greyed for other clubs' players.

**Who does what.** FC 27 Live Editor v27.1.2 has no native for transfer, loan, release, terminate loan or delete
(`core/caps.lua` M.TURBO_MADE), and Turbo has no RE of the game's transfer engine (no "complete transfer" call), so these
are written by Turbo into the career database (`core/moves.lua`): `teamplayerlinks` (club, shirt number, reserve slot),
`players` (join date, contract end, wage, release clause), `playerloans`, `cm_teamsheets` (the user's team sheet and
set-piece takers), `teams` set-piece takers. The only game routines Turbo has for these screens are the Transfer Hub
list actions (`docs/re/transfer_lists.md`: add to transfer / loan list, remove, contract status), which Turbo uses where
they apply. Every rule below is checked before the first write; a refused move writes nothing.

| rule | why (career consistency) | where |
|---|---|---|
| a loaded career (user club known) | the user's team sheet and lists need it | `moves.guard` |
| the club he joins has fewer than 52 players (Free Agents: no limit) | the team sheet has 52 slots (`cm_teamsheets` playerid0..51) | `moves.check_squads`, `move_rules::why_not_to` |
| a club with 18 or more players never drops below 18 (a club already below 18 is not limited) | a match squad is 11 starters + 7 substitutes | same |
| a club's only goalkeeper (`preferredposition1` 0) does not leave it | a club with no goalkeeper cannot field a team | same |
| a free shirt number at the new club, else refused | two players with one number at a club | `moves.free_jersey` (nil when none) |
| transfer / release of a loaned player ends the loan first (his `playerloans` row is deleted; the parent club is the seller) | a club link and a loan row must agree | `moves.transfer`, `moves.release` |
| a player on loan cannot be loaned again; a free agent cannot be loaned; Free Agents is not a loan club | `playerloans.teamidloanedfrom` must be his real club | `moves.loan` |
| terminate loan: the parent club must have room; the loan club's minimum is not checked | the game ends loans whatever the squad | `moves.terminate_loan` |
| leaving the user's club (transfer, loan, release, delete): if the game has him on the user's transfer / loan list (contract status 7 / 8 / 9) he comes off through the game's own remove first; if that remove fails the move is refused | the Transfer Hub lists and AI offers must not point at a player the club no longer has | `moves.leave_lists` |
| moves into the user's club add him to the end of the team sheet (reserve); moves out remove him: a starter's slot (playerid0..10) goes to the first substitute (the goalkeeper's slot to the first goalkeeper on the bench or in reserve), so the other starters keep their positions; the bench and reserves move up; set-piece takers that were him become the sheet's first player | the team sheet lists the squad and the line-up | `plan_sheet_add` / `plan_sheet_remove` |
| delete = release (all the rules above), then his `players`, `teamplayerlinks`, `editedplayernames`, `playerloans` rows | | `moves.delete` |
| create / clone / import as new: any club, the user's included (the old `allow_user_club` key is no longer read); the club must have room and a free number; in the user's club he goes on the team sheet as a reserve | | `features/create_player.lua` |

**What truly cannot be done.** The game's transfer / loan list helper lists the player on the **user's** club whoever he
plays for (`docs/re/transfer_lists.md` section 0), and AI clubs' lists have no call Turbo knows: Transfer list / Loan
list / Remove from lists for another club's player (or a player loaned to the user's club) cannot run. List status runs
for anyone; a player the PlayerContractManager has no record of (another club's player, or one Turbo moved since the
career was loaded) is answered "not listed" (Turbo.dll `tl::run`, query action).

**In the window** (`turbogui/src/ui/move_rules.h`, used by `ui_players.cpp` and `ui_presets.cpp`): a button is greyed
only when the build cannot run the tool at all (tooltip: the reason from `core/caps.lua`). When a move cannot run for
the selected player the button stays enabled, its label is dimmed, hovering says "Not possible for him: <reason>" and a
click shows the reason as a toast and sends nothing. The window knows the club link, the loan row, the squad sizes and
the goalkeepers; Lua checks everything again (whole database) and refuses with the same reasons. Moves that touch the
user's club show the note "Your club: Turbo writes the move into the career database ...".

**Still to verify in game** (throwaway career only, save backed up): a transfer out of and into the user's club, then
save, reload and simulate several weeks; a listed player transferred out (he leaves the Transfer Hub list); delete of
one of the user's players; clone into the user's club.

## Live league table (Competitions > Live standings, 1.1.1)

- The table shows the rows of the game's competition engine (`core/fce_standings.*`, FCE StandingsDataList); edits go
  straight into them. **Double-click** a W / D / L / GF / GA / Pts cell (or, with "Home / away columns", a home or away
  counter) for an input box in place; Enter or a click elsewhere commits, Esc cancels. The commit re-reads the row
  (identity checked), `fce::set_cell` changes it, `fce::write_row` writes it, then the rows are re-read and the
  standings view refresh is queued (`core/standings_refresh.h`), the same path as Advanced > Apply to the game.
- `fce::set_cell`: a total is split home / away the way the game stores it (a game added goes to the side with fewer
  games played that still has a fixture free, one removed comes off the side with more; goals go to a side with games,
  fewer first). P (W + D + L) and GD (GF - GA) are derived and not editable. Pts moves by the points per Win / Draw /
  Loss (3 / 1 / 0, Advanced) for each W / D / L added or removed, never below 0, so a deduction stays; a Pts the user
  typed is kept as typed (also when W / D / L change later in the session). Refused with a reason, row unchanged:
  negatives, a counter past 255, P past the club's fixtures (`fce::fixture_cap`: the used fixtures naming the row on
  each side, at least a double round robin of its group), goals while no game is played.
- **Undo** (one step) writes the row back only while it still holds what Turbo wrote (a match day in between is not
  overwritten). The status is one line (`Torino FC: W 2 -> 3, Pts 0 -> 3 (applied)`); what the game's standings view
  reads and the last refresh outcome are in the (?) tooltip and the GUI log. Column headers sort (position by
  default). The per-side +/- counters, the points per result and the played results (Change result) are under
  **Advanced**, closed by default.

## Not implemented yet

Match setup overrides, gameplay toggles (CPU vs CPU, unlimited subs, never tired, match time/score), manager market / job security /
fire, endless career, reveal player data, negotiation bypasses, match-fixing and job offers. These need code hooks
inside FC27.exe and in-game analysis first (minifaces from the 3D model are in, pending the first in-game run). The full FC 26 → Turbo map is `docs/fc26-parity.md`.
