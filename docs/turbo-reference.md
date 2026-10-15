# FC 27 LE Turbo — technical reference (2.4)

Turbo adds FC 26 Live Editor features to **FC 27 Live Editor** (public build v27.1.0 or newer). It is a user-owned add-on
that runs next to an official, unmodified Live Editor. Offline Career Mode / Kick-Off only.

This file is the technical contract. It was first written for 0.2.5; sections carry the version that introduced them. Brought up to
date for 2.4 (2026-10-13): the module map, build and test, the tactics sections (channel A and the legacy fields removed, the 2.4
design) and the kill switches. Release state and open items: `docs/HANDOVER.md`; what each release changed: `CHANGELOG.md`; how to
build on the development PC: `docs/BUILD_WINDOWS.md`.

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
| Turbo (Lua) | `turbo/package` | Feature pack that runs inside Live Editor's Lua engine: 33 feature modules, 40 `turbo_*.lua` runner scripts (2.4), auto features, config, and the GUI bridge. |
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
- `core/`: version, log, trace (start-up breadcrumbs), util, env, config, db (validated writes), game, mem, events, csv, calib, select,
  names (a player's name texts: name ids via GetDBTableRows playernames, editedplayernames).
- `features/` (the 1.x list below; 2.4 has 33 modules, among them also bodytypes, callnames, development, job_offer, manager_move,
  manager_rules, probe_tactics, repair_ovr1, reveal, team_mass, transfer_budget, youth): probe, form_morale, pap_playstyles, custom_headassets, custom_tattoos, delete_generated_players,
  export_season_stats, export_fixtures, export_transfer_history, extend_cpu_contracts, extend_user_contracts, headmodels,
  transfer_bans, squad_role, team_jersey_numbers, bulk_edit, player_moves, db_edit, export_table, player_presets
  (export to Live Editor preset CSV `extensions\player_presets` + Turbo player JSON with the miniface in
  `turbo_output\players`; import a preset onto a player by groups, names only when they differ from his own; mode
  `repair_names` rewrites the 1.2.0 common-name rows in place), create_player (new player rows via InsertDBTableRow:
  copy of a player, from a preset file, or blank; `core/preset.lua` parses LE CSV / FC 26 cards CSV / Turbo JSON).
  GUI: Players tab buttons Export... / Import... / Clone... / Create player... / Repair names... (`turbogui/src/ui/ui_presets.cpp`).
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
The GUI polls the three files every 0.5 s (`Bridge::poll_files`). `bridge_meta.json` and `bridge_names.txt` are re-read
when their time stamp changes; `bridge_state.json` (small, rewritten in place several times a second during a career event)
is read every poll and taken as new when its stamp **or its text** changed, because `std::filesystem::last_write_time` has
whole-second resolution with this toolchain and a stamp alone missed a rewrite landing in the same second as the one before.

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

### Module map (2.4)
Every `core/` file is platform independent and runs in the native tests on simulated memory; its `win/<name>_win.cpp` host makes the
game calls, installs the hooks and reads the switch files inside FC27.exe; `ui/` draws it. Each header's first lines say what it is.

| Area | core | win host | ui |
| --- | --- | --- | --- |
| Database, model, bridge | `t3db`, `model`, `bridge`, `mem`, `memmap`, `le_log`, `field_labels`, `user_text` | `memmap_win`, `dllmain` | `app`, `widgets`, `ui_database` |
| Game hooks and calls | `sigscan` (built-in signature table per game build, auto-adapt), `gamethread` (game-thread queue), `game_calls` | `game_hooks` (MinHook, `HOOK_BODY`, `hook_<name>_off.txt`, game_tick listeners), `game_calls_win` | Status tab |
| Players | `overall`, `archetypes`, `player_create`, `player_id`, `player_move`, `player_morale`, `squad_role`, `reveal`, `reapply`, `player_capture`, `face_filter`, `face3d_looks`, `hair_catalog`, `bodytype_catalog`, `callnames`, `callname_voice`, `callname_audio`, `commentary_*` | the matching `*_win.cpp` | `ui_players`, `ui_presets`, `ui_faces`, `ui_bodytypes`, `ui_callnames`, `ui_names`, `ui_archetype` |
| Clubs | `teamnames`, `teamname_override`, `kit_import`, `hub_customise`, `transfer_list`, `transfer_stats`, `scouts`, `manager_rules` | the matching `*_win.cpp` | `ui_teams`, `ui_identity`, `ui_kits`, `ui_club_tools`, `ui_parity` |
| Competitions | `fce_standings` (live tables, fixtures), `standings_refresh`, `comp_list`, `match_setup` (game variables + result fixing) | `standings_refresh_win`, `match_setup_win` | `ui_competitions`, `ui_standings`, `ui_match` |
| Tactics | `sliders` (registry), `tactics_status` (badges, kill switches, snapshots), `tactics` (preview model), `tactics_db`, `tactic_profiles`, `opposition`, `opp_rules`, `team_sheet` (channel E), `tuning` (engine tuning tables) | `team_sheet_win`, `tuning_win` | `ui_tactics`, `pitch_view`, `ui_live_tactic`, `ui_tuning` |
| Assets | `asset_catalog`, `asset_pages`, `gear_ids`, `game_heads`, `callnames_catalog` | - | `ui_asset_browser` |
| Game editors | `edit_unlock`, `edit_unlock_rules`, `edit_unlock_hook` | `edit_unlock_hook_win` | `ui_edit_unlock` |

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
- **Row density (2026-10-11, `ui/gui_prefs.h`)**: Status > Settings "Row density" = compact | normal | comfortable
  (`gui_settings.json` `gui.row_density`). `App::update_style` multiplies `CellPadding.y`, `ItemSpacing.y` and
  `FramePadding.y` after `ScaleAllSizes`, so table rows (text + 2 x CellPadding.y), list rows (text + ItemSpacing.y) and
  widget rows (FrameHeight) follow one setting; the font size stays the UI size.
- **Tables and panes (`ui/table_registry.h`)**: every table has resizable, reorderable and hideable columns; the registry
  lists the tables (36 at 2.4) with their flags and `tests/native/test_ux_resize.h` scans `src/ui` so none loses them. The
  exceptions are the tables inside an auto-sizing popup (`Spec::popup`): `##comptable` in the competition picker's combo
  (with the column flags Dear ImGui saved its column settings and the popup's second opening failed, native "Live
  standings: groups" case), `##clubrows` in the club search dropdown and `##keptplayer` in the kept-edits tooltip; they
  keep fixed columns. Column
  widths / order / hidden columns live in `turbo_output\turbo_gui_layout_v2.ini` per table ID (the Database tab pushes the
  database table's name so each table keeps its own). Split panes and fixed-height lists carry `ImGuiChildFlags_ResizeX`
  / `ResizeY` (a resize border; the size is kept in the same file). Not the Callname lists (`list_height`, 80 .. 200 px to
  the buttons below): a remembered height there pushed the buttons out of view (native Callname cases failed). Not the
  children of auto-sizing popups either (`##complist` in the competition picker combo, `##fvals` in the face filter): a
  resizable child inside an auto-resizing parent feeds its saved size back into the parent, and the second opening of the
  picker failed (native "Live standings: groups" case).
- **Background loading (`ui/preload.h`, `ui/preload_queue.h`)**: from the first show or the career connection, pictures
  load in the order they are opened: the user's club's minifaces, the listed players' minifaces (topped up when the Players
  list changes), the real-face chooser (its minifaces and the 3D head renders already in `turbo_output\cache\faces3d`),
  tattoos, the hair / body galleries, crests, manager heads; the first 400 of every kind before the rest of any. Missing
  files are asked from Lua (`want.txt`, exported inside its 0.25 s slice per career event), files at hand are decoded on the
  texture worker. `gui.preload_images` (default true) switches it off; the Status tab shows the progress per kind; the queue
  is cancelled when the game leaves the career (`LegacyImages::clear_background`) and collected again for the next one.
  Lua's background list holds at most `LegacyImages::kMaxBackground` (20 000) paths: `ask_background` answers Accepted,
  Known (already asked, at hand, missing or invalid) or Full; a Full answer keeps the picture queued and unasked
  (`PreloadQueue::refuse`), the round robin asks again (at most `Preloader::kRefusedPerFrame` = 4 retries per frame) as
  arrived pictures leave the list, and the progress line shows `waiting: N refused` until then.
  New 3D head renders are never started by the preloader (they need the game's capture service: Render all filtered heads).
- **Failure handling**: every frame runs behind an exception barrier; any error switches the overlay off for the session and
  the game keeps running. The main window is clamped to the screen size.
- Third party (vendored in `turbogui/third_party`): Dear ImGui v1.92.9, MinHook v1.3.4, nlohmann/json 3.11.3.
- Toolchain: mingw-w64 13 (posix threads) with `-DWIDL_EXPLICIT_AGGREGATE_RETURNS` (needed for the D3D12 descriptor-handle ABI), static linking.

## Build and test

How to set the development PC up and the exact commands: `docs/BUILD_WINDOWS.md`. In short:

| Command | What it does | Needs |
| --- | --- | --- |
| `bash scripts/msys.sh "bash scripts/check_all.sh"` | the whole gate in about 3-4 minutes: BUILD (0 warnings expected), LUA, NATIVE, PY, CATALOG; one summary line each, logs in `turbogui/build/check_logs` | MSYS2 UCRT64, lua5.4, `turbo/le27/libs`, Python with openpyxl |
| `bash turbo/tests/run_tests.sh` | the Lua suite: 33 test files over a simulated game memory (launch safety, bridge, every feature module; `GetDBMeta` as Lua tables, as iterable C++ objects and as index-only C++ objects). 2.3.0: 405 checks | lua5.4, `turbo/le27/libs` |
| `luacheck --config turbo/tests/.luacheckrc turbo/package/lua` | lint | luacheck |
| `bash turbogui/tests/native/run_native.sh` | the native suite: the database engine against Live Editor's Lua T3DB library, every core module on simulated memory, headless ImGui UI driving of every tab (each button that sends a command runs through Turbo's real Lua bridge in a simulated career). 2.3.0: 38,462 checks; 2.4/cleanup alone: 28,849 (the removed channel A and legacy rows took about 9,900 checks with them, most of them per-slider loops; 282 were added) | g++, lua5.4, `turbo/le27/libs` |
| `bash turbogui/tests/catalog/run*.sh` | the asset catalog runners (g++ only, synthetic data, seconds) | g++ |
| `python3 scripts/check_field_names.py` | every table / field name in the GUI and Lua sources against EA's `db_meta.xml` (FIFA 21 Live Editor) and the names xAranaktu's FC 24–26 Live Editor scripts use | git, GitHub access |
| `bash turbogui/scripts/build_win.sh` | builds `Turbo.dll` + `TurboProbe.exe` + `TurboInjector.exe` (parallel and incremental through `turbogui/scripts/parallel_compile.sh`); fails if a graphics DLL appears in the import table | mingw-w64 (posix) or MSYS2 UCRT64 |
| `bash turbogui/tests/win/run_smoke.sh` | loads the real `Turbo.dll` under Wine in 10 modes: imports, refusal without Live Editor, no hooking without a game window, kill switch, crash guard (clean exit / kill), mailbox, Lua-side start, and launch mode | Wine, mingw-w64 |
| `bash turbogui/tests/win/run_overlay_wine.sh` (`MODE=probe` default, `MODE=fallback`) | runs `tests/win/game_stub.cpp`, a Direct3D 12 stand-in game (not FC 27), under Wine + vkd3d + Mesa lavapipe on Xvfb; checks the log and screenshots of the overlay | Wine, Xvfb, mesa-vulkan-drivers, ImageMagick |
| `bash scripts/package.sh [--guard]` | builds `dist/FC27_LE_Turbo_<version>.zip` (the files to copy into the Live Editor folder); `--guard` runs only the name guard (no masters, catalog, banks, gate files) | zip (MSYS2), built binaries |
| `bash scripts/deploy.sh` | installs this worktree's build into the Live Editor folder (game closed; backs up what it replaces) | Git Bash |
| `python scripts/callname_voice_log.py --self-test` / `python scripts/playtest_monitor.py --self-test` | the voice-swap log parser and summary; the play-session watcher on temporary folders | Python 3 |

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
  `core/teamnames.*`; RE: `docs/re/team_names.md`). One form (four boxes since 1.1.4): **Display name** (what every
  screen shows, `TeamName_<id>` + `teams.teamname`), **Long name** ("Associazione Calcio Milan": FC 27 has no string
  for it, so Turbo keeps it in its store only and the game never shows it), **Short name** (lists, fixtures; at most 15
  letters), **Abbreviation** (3 letters, scoreboard), and one **Save**. An empty short name or code is made from the name
  (shown as the box's hint: the whole name when it fits, else cut at a word end; the first three letters, upper case);
  the 10-letter form the game also asks for is made from the short name. Save, in this order:
  1. publishes the club's four strings to the `team_names` hook and keeps them in `turbo_output\team_names.json`
     (`{"turbo_team_names": 1, "teams": [{"teamid", "name", "long", "abbr15", "abbr10", "abbr3", "when"}]}`, `long` since 1.1.4, every career, written
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
  hook). Clubs in GetTeamName's `TEAM_IDS` redirect are looked up with the same key on the global service (0x142394E78 calls LocalizeString with a copy of the key), so the hook covers them too. LocImpl slots 0x258 / 0x260 / 0x2F8 / 0x300 (formatted strings) read StrTab::GetString directly and are not covered; the match scoreboard path is not verified yet.
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
| arrivals at the user's club (Unreleased, after 2.0.3): a move INTO the user's club that would be a database move (a transfer without `TurboPlayerMove`, op 11; the transfer of a loaned player; every loan to the user's club; the early end of a loan back to it) is refused before anything is written, with the reason and "the game's own transfer routine (Turbo.dll op 11) is needed", and logged (`arrival guard: ...`). Opt-in file `turbo_output\db_move_user_club_on.txt`: the move runs, the summary and the log carry a WARNING. Moves between AI clubs, out of the user's club and through op 11 are not affected; a failing op 11 call never falls back to the database | a database arrival gets none of the game's PlayerJoinedTeam work: no development plan / growth record (the OVR-1 players, `turbo_output\STEP2_REPORT.txt`), no morale record, no squad status entry (the match-load crash) | `moves.db_arrival_gate`, `t25_arrivals_guard.lua` |
| create / clone / import as new: any club, the user's included (the old `allow_user_club` key is no longer read); the club must have room and a free number; in the user's club he goes on the team sheet as a reserve, and right after the create (both ways) the match crash guard's repair runs at once (`create_player.lua` calls `squad_role.repair_entries` directly; the guard in `bridge.lua` is unchanged): the summary and the log say "squad status entry added at once / present / not added now (reason)"; kill switches `create_status_repair_off.txt` (this step) and `squad_status_repair_off.txt` (the whole guard) | a club player without a squad status entry crashes the match load (2026-10-11: two created players) | `features/create_player.lua` |

**Development plan words** (Unreleased, after 2.0.3): an attribute write onto a player of the user's club who has no plan
(`PlayerHasDevelopementPlan` false: a database arrival, a created player) says `moves.NO_PLAN_WARNING` = "no development
plan: the game may undo this at its next growth step": the preset import (also its dry run, from a read of the flag) and
the dev bonus / match XP mass actions without Live Editor's development manager, which count the plans really written
("development plan updated for N of M", from `development.run`'s third return value) instead of "plans updated".

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

## Tactics (2.0 - 2.4)

Design and rationale: `docs/TURBO_2_0_PLAN.md` (2.0), `docs/plans/tactics-2.1/` (2.1), `docs/TACTICS_OVERHAUL_DECISION.md` (2.4). This section is
the short technical contract; what each release shipped is in `CHANGELOG.md`.

### Slider tiers

Since 2.1 Phase 0 every row carries TWO badges (`core/tactics_status.h`): how it is written (game variable / database row / preview / Turbo only / live team sheet) and what was seen of it (seen in a played match + date / no effect seen + date / untested / nothing written); the registry tier below is the write path only and is no longer shown as a "Live" badge. A row that writes but was never seen working in a played match (untested, no effect seen) loads switched off from a preset, and Apply lists those rows and asks before writing them. Controls EA removed in FC 25 are hidden behind "Show legacy fields"; since 2.4 none of them writes: the two widths (`team.width_def` / `team.width_att`) and the 7-step mentality are Turbo's preview only (the pitch's width band and the opposition solver use the widths), and the rows that wrote the old `mentalities` table (players in the box, chance creation, attacking / defensive style) are gone. The saved-tactic targets `cm_mentalities.defensivedepth` / `cm_mentalities.buildupplay` are not in the registry (the game reads that row once at career load and overwrites it from its live copy at every save) and `dead_target_reason()` refuses any `cm_mentalities.*` target; that row is written only by channel E as the live tactic's database copy (`ui_live_tactic.cpp`: opt-in in 2.3; in 2.4, pending proof, part of the one channel E write). The role editor is "Role familiarity N (Role+ / Role++)": the player's own list, not the role of a tactic slot. **2.4 removed tactics channel A**: the 66 per-side `GAMEPLAY_CUSTOMIZATION/<slider>_USER` / `_CPU` rows (the game stores and reads them back, the match ignores them: `turbo_output\TACTICS_CHANNEL_A_REPORT.txt` VERDICT, 2026-10-11) are gone from the registry, from Competitions > Match setup (with the A/B presets) and from `gv::known_vars()`; the host refuses any variable name outside that list, and a 2.1-2.3 preset that still names one of their keys keeps it as an unknown key that is never written. Every row that writes shows a read-back line: a game variable as the host read it back after the write, a database field as it is now (with the value before Turbo's first write). Before Turbo's first write to a database row the row's value is kept in `turbo_output\tactics_snapshot.json` (per career club, target and row); the row's Reset button, switching the row off + Apply, and `tactics_off.txt` appearing put it back, only while the row still holds the value Turbo wrote (a `mentalities` row kept by 2.1-2.3 is still put back by `tactics_off.txt`). Since 2.1 a database value belongs to the club or player it was set for: the My team rows (`teams`) are the selected club's and the Position/Role rows (`players`) the selected player's, and selecting another club or player puts the values (switched on, last applied) aside until he is selected again. Apply, Reset and switching a row off + Apply therefore only reach the rows the values were set for (selecting the opponent for the Opposition scope and pressing Apply never writes your club's settings into his row), and "What the game will receive", the Apply confirmation, the Apply log and its toast name the club or player ("teams.defensivedepth = 62 for Arsenal"). Every write and every put-back of a `players` field other than `overallrating` goes through `App::edit_field`: a preferred position written or put back by the tab never moves the stored overall (the overall follow of the player editor is not run), so a Reset, off + Apply and `tactics_off.txt` leave the overall as it was. "Clear overrides" clears the game variables only: the database rows keep their applied state, so switching one off + Apply still puts it back.

Every tactics/match slider carries one registry tier, defined by where its value goes:

| Tier | Meaning | Write path | Effect in a played match |
| --- | --- | --- | --- |
| Live | A game variable of `gv::known_vars()` set through the `gv` bridge (the four injury sliders `GAMEPLAY_CUSTOMIZATION/INJURY_*_USER` / `_CPUAI`, `OVERRIDE/WEATHER`, `OVERRIDE/TOD`, `NEVER_INJURE`, `DISABLE_CPU_SUBSTITUTION`, `OVERRIDE_MATCH_DIFFICULTY`) | process-wide override, not saved with the career, cleared when the game closes | Accepted by the game; the in-game effect of each key is verified one by one (the "what was seen" badge says which: see the plan, section 3) |
| DB | A field of the career database (players role/position/trait fields, formation tables) | normal DB write, saved with the career | Only if the game reads that field at match setup; the `TeamSheet` runtime copy may overwrite DB edits |
| Preview | A Turbo-side model (pitch heat grid, opposition solver output) | none, nothing is sent to the game | None. It is a visual aid, not engine truth |
| RE | No known write site yet (needs reverse engineering) | none | None. Shown as Preview only until promoted |

Honest limits: simulated matches ignore every override (`simsettings.ini` is loaded over them), so Live sliders affect played matches only. Overrides are process-wide, so they also reach online modes; Turbo clears them on exit and shows an "overrides active" banner. Whether Authentic Gameplay or Dynamic Opposition bypasses them is unverified (the failure mode is a silent no-op).

### Tactics tab: where each thing comes from

| Thing on screen | Source | Honesty |
| --- | --- | --- |
| Pitch dots | The selected club's saved formation: `teamformationteamstylelinks` (team id to formation id), else the `sourceformationid` of the club's active tactic in `cm_mentalities` / `mentalities`, else a `formations` row carrying the club's id; positions from `formations.position0..10`, offsets from `formations.offset0x..offset10y`, or `formationoffsets` when those are all zero. Read through `core/tactics_db.cpp`, cached by `App::gen`. A goalkeeper near the far goal line turns the y axis round. | Exact (E), solid; your club: "your saved tactic as of your last save" (change it in the game's Edit Tactic), another club: "saved data (the club's saved tactic row)"; both say nothing on the pitch is written. When nothing usable is found Turbo's own shape from `fallback_formations()` is drawn and the reason is shown. |
| Lines, band, arrows | `build_preview` (core/tactics.h) through `PreviewCache`; drawn only for sliders that are switched on | Derived (D): dashed, "62/100 (schematic)", never a unit |
| Grid, rings, exposure | The same model, only when asked for | Modelled (M): hatched or stippled, "(arbitrary)", off by default |
| Team sliders written to the database | `teams.defensivedepth` / `teams.buildupplay` of the selected club (2.4: nothing else; the old `mentalities` table is no write target) | Database row, untested; your saved tactic row (`cm_mentalities`) is never written by this tab |
| Opposition card | `core/opposition.cpp` with the built-in rules plus `turbo_output/opp_rules.json`; seed from the career, the season, the day, the opponent and the re-roll count | Preview, except injury and difficulty offsets, which are game variables and are sent only by the card's Apply button as base plus offset |

### Kill switches (files in `turbo_output`)

`reapply_off.txt`, `turbo_gui_disable.txt`, `call_<name>_off.txt`, `hook_<name>_off.txt`, the crash flag `turbo_gui_start.flag`, and new in 2.0: `tactics_off.txt` (no tactics writes, effect layers greyed; since 2.1 Phase 0 its appearance first puts back the database rows the Tactics tab wrote in this career, see "Slider tiers") and `role_reapply_off.txt` (no squad-role re-apply on season reset). 2.1 Phase 0 adds one switch per tactics write channel (`core/tactics_status.h` `kill_switches()`): `tactics_gamevar_off.txt` (the game-variable rows: injuries, weather, time of day, difficulty, CPU substitutions) and `tactics_db_off.txt` (teams / referee / players rows), plus `call_team_sheet_off.txt` for your live tactic (channel E, since 2.2); a switched-off channel's rows are greyed with the file name, its writes and resets are skipped and counted in the Apply toast. 2.4 dropped `tactics_legacy_off.txt` (nothing writes the old `mentalities` table any more). Match setup (`core/match_setup.h`): `call_gamevar_off.txt` (the host refuses every game-variable write, from the Tactics tab and from Match setup alike), `call_match_fix_off.txt` (fixed results), `live_standings_write_off.txt` (every competition-engine write and its Undo) and `call_standings_refresh_off.txt`. Channel E (2.2 / 2.3, `core/team_sheet.h`; read by every build since 2.2, no longer "reserved"): opt-in `call_team_sheet_on.txt` (the card's "Channel E on" box), kill switches `call_team_sheet_off.txt`, `call_team_sheet_find_off.txt` (no registry lookup), `channel_e_kickoff_off.txt` (no kick-off check); in 2.3 the `cm_mentalities` database copy is opt-in (`channel_e_db_mirror_on.txt`, kill switch `channel_e_db_mirror_off.txt`); 2.4 (branch `2.4/tactic`, pending its in-game proof) removes that opt-in: the row is written in the same channel E job as the live record, and `channel_e_db_off.txt` (or `tactics_db_off.txt`; 2.3's `channel_e_db_mirror_off.txt` still honoured) leaves it out; undo file `channel_e_undo.json`. Gameplay tuning (2.3, `core/tuning.h`): opt-in `tuning_on.txt`, kill switches `tuning_off.txt` and `call_attrib_off.txt` (and `tactics_off.txt`); keep file `tuning_keep.json` (2.4, branch `2.4/tuning`, no longer reads it: the profile `tuning_profile.json` and its switch `tuning_profile_off.txt` replace it, see "Tactics 2.4"; pending in-game proof). Game hooks: `game_hooks_off.txt`, `signature_adapt_off.txt` (no auto-adapt of the signature table on an unknown build). Still reserved (not read by any build): `opp_auto_off.txt` (per-fixture opposition engine) and the restore file `opp_db_restore.json`. From the 1.2.5 line (2.0.3): `squad_status_repair_off.txt` (no match crash guard: `bridge.lua` `repair_squad_status` adds no squad status entries at career load / day / pre-match events; tested in `turbo/tests/t04_squad_role.lua`), `call_squad_role_create_off.txt` (the `squad_role` game call, op 14, creates and removes no entries; `turbogui/tests/native/test_squad_role.h`), `plan_sync_off.txt` (the Turbo window sends no development-plan sync for its player edits; `turbogui/tests/native/test_main.cpp`), and `call_player_morale_create_off.txt` (no morale records created by the morale mass action; `t18_team_mass.lua`). Since 2.0.4: `create_status_repair_off.txt` (no squad status repair right after `create_player` at the user's club; `squad_status_repair_off.txt` stops it too; `t25_arrivals_guard.lua`) and the opt-in `db_move_user_club_on.txt` (database moves into the user's club allowed again, with a warning; `t25_arrivals_guard.lua`). The user-facing table is in `turbo/package/TURBO_README.md`.

### Schema coverage

`python3 scripts/check_fc27_schema.py --coverage [schema.json]` lists the fields of `players`, `teams` and `manager` in the game's schema dump (`turbo/le27/fc27_db_schema.json`) that no `turbogui/src/ui/*.cpp` or `turbogui/src/core/field_labels.h` mentions as a whole word. It is informational (exit 0) and prints a hint instead of failing when the schema file is absent. Without `--coverage` the script keeps its old job: checking every name Turbo uses against the schema.

### Tactics 2.4: the plan (docs/TACTICS_OVERHAUL_DECISION.md), NOT the current code
**Status (2026-10-13): planned.** Only the removals of the cleanup branch (channel A, the legacy `mentalities` writes) are in code at the
time of writing; everything else below is the design of the decision document, built on the branches `2.4/tuning`, `2.4/tactic` and
`2.4/tools`, subject to the decision gates X1-X3 and to its in-game proof. It becomes true only with the 2.4.0 release merge, and only for
the items proven in game (the rest is taken out of 2.4.0). Check `CHANGELOG.md` 2.4.0 before relying on any of it.
- **G1, the engine tuning layer (planned).** Turbo is to apply a *tuning profile* (its own values; FC 27's defaults as the baseline; no
  mod values) to the attribulator tables at three moments: at boot (the first game tick at the main menu once the signatures and the locate
  succeeded), at career load, and in the hub before every match (never in the ~0.5 s hub blip between prematch and match). The tables are
  to be located deterministically from the static manager slot (`get_current_gameplay_attribdb` -> G -> manager -> database -> family slot
  -> group slot -> instance), identity-checked in the same job (vtable, TypeInfo at +8, Name at +0x18, owner rule), and written to every
  database copy. Planned files and switches: profile `turbo_output\tuning_profile.json` (`{"on", "preset", "values"}`), undo
  `tuning_undo.json`, kill switches `tuning_off.txt` (all), `tuning_profile_off.txt` (the automatic moments only), `call_attrib_off.txt`,
  `tactics_off.txt`; log lines `tuning: chain locate ok ...` and `tuning: profile applied (boot|career_load|hub) <k> knobs (read back ok)`.
  Gate X1 decides whether a knob may also be written during a match.
- **Honest knobs (planned).** A knob is to have one of three states: PROVEN (a slider; the knobs file carries a `proof` entry naming the
  session and the evidence), LOCATED (a read-only row: "located, effect not proven"), UNLOCATED (hidden); the package guard is to refuse a
  slider-enabled knob without `proof`. Proof = an A/B on the pitch: the value written in the hub before kick-off, windows B1 / A1 / B2 / A2
  of 45 s, both teams, stratified by possession and ball third, block permutation test, criterion written down before the run.
- **G2, the club's tactic (planned).** One guarded write of the active record of your club's sheet in `CachedTeamSheetService` (Line
  Height +0xA4, Build-Up +0xB0, Defensive Approach +0xB4, 11 role words) together with the `cm_mentalities` row (`buildupplay`,
  `defensivedepth`), one undo file. The kick-off listener is to read the match's own copy back and log `channel E kick-off: match copy team
  <id> LH .. BU .. DA .. roles 11/11 = Turbo (ok)`, the proof line. Gate X2 decides which source the match reads; gate X3 decides how Edit
  Tactic is made to show the values (the inline sheet, another service copy, or only after the next career load). When the game's own
  "Yes" on leaving Team Management replaced Turbo's values, Turbo is to offer a one-click re-apply, never an automatic one. Build-Up 2 / 3
  and Defensive Approach 3 stay refused until decoded.
- **To be removed in 2.4:** tactics channel A and the legacy `mentalities` writes (done on `2.4/cleanup`); the Edit Tactic working-copy
  writes (E2) and the `channel_e_db_mirror_on.txt` opt-in (done on `2.4/tactic`); `tuning_keep.json` (no longer read on `2.4/tuning`); the
  "experimental" wording; the `teams.*` writes unless gate X2 shows the match reads them. Every 2.4 item stays pending until its in-game
  proof and the release merge.
- **Decision gates** (coordinator, short LTEST sessions): X1 (does the engine read the tuning tables, and when), X2 (which source feeds the
  match's Line Height / Build-Up / Approach), X3 (where Edit Tactic loads from). Each has a pre-decided branch table in the decision document.

## Game asset catalog (2.1)

Full contract: `docs/asset-catalog.md`. Short version:

- **Builder** (offline, Python, read-only on the game, in a Turbo source checkout; release builds have none until WP-14):
  `py -3 -I turbo\tools\asset_catalog\build_asset_catalog.py`
  writes `turbo_output\asset_catalog\catalog_<FC27.exe TimeDateStamp-SizeOfImage>.json` (schema `turbo-asset-catalog/1`) and a
  summary. Proof per family: CFC legacy manifests (FNV-1 of the path) for pictures, EBX names in the toc bundle manifests for
  3D assets, `fc27_commentary` banks (`turbo_output\asset_catalog\banks\<lang>_bank.json`, built by the builder itself
  when missing or outdated) for callnames. Per-toc cache: an unchanged rerun takes under 2 s.
- **GUI** (`core/asset_catalog.*`, `App::asset_*`): loaded on a worker thread at the first tick, swapped in on the render
  thread, folder polled every 5 s. The running build's file = current; else the newest file = stale (lists only, hidden
  ids refused). `App::publish_asset_catalog` hands it to `set_faces_catalog`, `set_gear_catalog`, `Callnames::use_catalog`
  and `set_asset_view` (with `DbUse`, rebuilt once per `model.version()`) after **every** finished load, a rebuild under
  the same build key included; not loaded = nullptr = 2.0 behaviour.
- **Hidden** is computed live against the save (`assets::DbUse`), never stored. **Write gate** `Catalog::writable`: a
  current catalog and, for heads and gear, a 3D model (0 = none always allowed; content_ts-only boots refused). The
  galleries (`core/gear_ids.h`) also refuse tattoos outside the save's tattoo table and offer accessories only for the
  players' codes (no gate). Callnames: only SIMPLE / LINK recordings count as a player's own (`kBankSpoken`); the eng_us
  team is automatic (live capture, else dr) or chosen (`callnames.bank_team`). Writes of hidden ids:
  `turbo_output\asset_writes.log`.
- **Lua** (`core/asset_gate.lua`): `asset_catalog\lua_gate.txt` (`#turbo-asset-gate 1`, `#build`, `#session`, `head3d <id>`
  lines), written by the GUI only while the catalog is current and trusted only when `#session` equals a fresh
  `bridge_dll.json` session. Used by `custom_headassets` and `create_player`.
- **Kill switches**: `turbo_output\asset_catalog_off.txt`, `turbo_output\asset_hidden_off.txt`.
- **Never shipped**: catalog, banks, gate, write log and name sources stay in `turbo_output`; `scripts/package.sh` refuses
  them (`--guard` runs the check alone).
- **Tests**: `turbogui/tests/catalog/run*.sh` (g++ only, seconds), the Python tests next to each tool, `t26_asset_gate.lua`,
  native case "UI: asset catalog wiring". No test reads the game folder.

## Lua loader hook (2.9 branch, opt-in; install + boot compile + chunkname substitution verified in game on 6AC07E31, Launches L1/L2)

`core/lua_loader.{h,cpp}` (logic, tests `turbogui/tests/catalog/run_lualoader.sh`) and `win/lua_loader_win.cpp` (host). Research:
`turbo_dev/research/c2_lua_loader/C2_LUA_LOADER.md`. Started from `DllMain` on its own thread; without the opt-in file it returns at
once (nothing scanned, hooked or logged, the Status tab unchanged).

| file in `turbo_output` | effect |
|---|---|
| `lua_loader_on.txt` | opt-in, LOG-ONLY: scan for `lua_load` every 250 ms for up to 60 s (`TURBO_LUA_LOADER_WAIT_MS`), install when `lua_load`, its cross-check (luaL_loadstring's call) agree and the prologue is not a jump; one `lua_loader: #N ...` line per call in `turbo_gui.log` (time, thread, caller, reader, chunkname, size + md5 for the blob reader and getS, rc) |
| `lua_loader_substitute.txt` | with the opt-in: a source whose (size, md5) matches a row is replaced by its override (`lua_override\player_kit.lua` for player_kit 0x41140 / 9399e39a...); for a reader Turbo cannot measure ("other"), a row also matches by exact chunkname (`data/fifarna/lua/assets/player_kit.lua`, one leading `@` tolerated) but only on the row's build (`6AC07E31-2145C000`, the running exe's build key) and only when the lua_load stack layout was confirmed at install; text chunks only; the override must be compile-checked offline with Lua 5.4 first |
| `faces3d_on.txt` | product opt-in written by the GUI (`set_faces3d_opt_in`, tmp + rename): the same as `lua_loader_on.txt` + `lua_loader_substitute.txt` |
| `faces3d_off.txt` | kill switch like `hook_lua_load_off.txt` |
| `faces3d\stock_player_kit.cache` | written by the hook (watcher thread, tmp + rename): the first captured stock player_kit.lua; in LOG-ONLY (capture mode) whatever its md5, in SUBSTITUTE only when it equals the row's expected md5; never while `mods\legacy\...\player_kit.lua` exists |
| `hook_lua_load_off.txt` | kill switch: refuses the install; while installed, re-read every 250 ms, every call passes through untouched (`game_hooks_off.txt` too) |
| `lua_loader_start.flag` | left behind only if the game died while the hooks were being written: the next start refuses until it is deleted |

How the source is seen: getS's data `{const char*, uint32}` is read, never called; the blob reader is hooked itself and records what
the original returned when the parser asked for it (no early call, no state restore); other readers are never called (substitution by chunkname only, below). Signatures were read on
build 6AB9813C (`fc27_image.bin`); Launch L1 found all four live on 6AC07E31 (lua_load FC27.exe+0x81F470, getS +0x46A769C, blob reader
+0x89E334C) and installed ~3 s after Turbo loaded, ~4 s before the boot compile of player_kit.lua. MinHook may be initialised by the loader before the overlay; the overlay then accepts `MH_ERROR_ALREADY_INITIALIZED` and on a
failed start removes only its own hooks.

Stock capture (2.9 WP-B, NOT run in game yet): player_kit.lua comes through the game's one-shot reader (6AB9813C 0x1446B08C8 /
6AC07E31 FC27.exe+0x46AE738), a leaf: `if (d->done) {*size=0; return NULL;} d->done=1; *size=obj[+0x20]; return obj[+0x18];` (no call,
no Lua API, cannot raise). Turbo calls it itself only when the live 35 bytes equal `kOtherReaderExpect` exactly; it drains it (until NULL
or size 0), copies + md5s the bytes, logs `captured stock player_kit: N bytes md5 X`, and from then on hands lua_load its own replay
reader (the game's reader is consumed). The override is served only when its first line is `-- TURBO build <key> stock <md5> table
<hash>` naming the running build and the md5 just captured; otherwise (no header, other build, stale stock, mode, layout, latch) the
captured stock bytes are served and the reason is logged and kept as the Status `last`. A failed override compile replays the captured
stock. A `mods\legacy\dataifarna\luassets\player_kit.lua` refuses substitution (log-only, no cache). Status:
`host::faces3d_status()` (`Faces3dStatus`) and a second Status tab line.

Substitution by chunkname (Launch L1, 6AC07E31: player_kit is compiled at boot through reader `FC27.exe+0x46AE738`, 18 calls on 18
threads, chunkname without `@`): lua_load is called with the same L / chunkname / mode and Turbo's one-shot reader over the override
(loaded once at start, immutable, shared read-only; the reader state is a per-call struct on the detour's stack). The game's reader is drained once by the capture above. If that compile fails, the host reads the error message, pops it (L->top -= 16, only when
`L->top - L->ci->func` grew by exactly one slot; the layout is checked live in lua_load's bytes after luaD_protectedparser,
`44 8B D8 85 C0 75 ?? 48 8B 4B 10 4C 8B 51 F0` at +0x45, else no chunkname substitution at all) and calls lua_load again over the captured stock bytes; that rc is returned and a per-row latch sends every later call straight to the original. Log:
`SUBSTITUTED row=player_kit override bytes N md5 X (stock unknown: chunkname match)` or `fallback to original (override failed: rc=3
"msg")`; the Status line counts `fallbacks`. Not covered: an override that compiles but fails or faults when it runs.

## Not implemented yet

The FC 26 → Turbo map with the state of every feature is `docs/fc26-parity.md`; the open items in order are in `docs/HANDOVER.md`
section 10. Known gaps at 2.4: the 3D kit on the pitch (kit import phase 2: an in-match texture swap), per-club CPU tactics and a local
`.fifamod` reader (2.5, pending Cassio's answers), the engine tuning groups beyond shape / pressing / marking / runs (CPU AI, realism,
career boosts: 2.5), and anything that needs a game function FC 27 Live Editor v27.1.2 lacks (see the API section above).
