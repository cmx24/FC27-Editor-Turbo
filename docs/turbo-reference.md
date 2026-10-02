# FC 27 LE Turbo — technical reference (0.2.2)

Turbo adds FC 26 Live Editor features to **FC 27 Live Editor** (public build v27.1.0 or newer). It is a user-owned add-on
that runs next to an official, unmodified Live Editor. Offline Career Mode / Kick-Off only.

## Ground rules

- Live Editor's own files (`FCLiveEditor.DLL`, launchers, Lua libs) are never modified, copied or redistributed.
  The test fixtures that need Live Editor's Lua libs are supplied by the user (`turbo/le27/README.txt`).
- Nothing in Turbo decompiles, disassembles or patches Live Editor or the game, bypasses authentication or touches the
  anti-cheat. `Turbo.dll` refuses to start unless Live Editor is running in the same process (or Turbo's Lua side,
  started by Live Editor, has just run).
- Every database write is range-checked against the field's own metadata; destructive actions need a confirmation.

## Launch safety (0.2.1)

Turbo 0.2.0 broke game launch: `lua\autorun\turbo_boot.lua` ran while Live Editor was still initialising the game
(Live Editor injects ~100 ms after `FC27.exe` starts), and it immediately called `GetPlugin`/`GetDBMeta`, registered an
undocumented `post__LEInitDoneEvent` handler, and loaded `Turbo.dll`, which probed Direct3D 12 and hooked DXGI/D3D12 before the
game had a window. The simulator's "game" is always ready, so the tests could not see it. The rules since 0.2.1:

1. **Launch is pure Lua.** `TURBO.boot({at_launch = true})` reads the config, remembers settings and, only if an automatic feature
   is enabled, registers the documented `post__CareerModeEvent` handler. No game native (not even `IsInCM`), no memory access,
   no `package.loadlib`, no other event name. `t08_launch_safety.lua` instruments every game native and `package.loadlib` and
   fails on any call during launch (mutation-checked: re-adding each unsafe behaviour makes it fail).
2. **The GUI starts on demand**: `turbo_gui_load.lua` (run in the Lua Engine in game) → `bridge.start()`. With `gui.autoload = true`
   (default false) it starts on the first career-mode event instead. Never at launch.
3. **`Turbo.dll` waits for the game** before touching Direct3D: a visible game window, `d3d12.dll` + `dxgi.dll` loaded, then a settle
   time (defaults: up to 180 s, settle 5 s; env `TURBO_GUI_WAIT_MS`, `TURBO_GUI_SETTLE_MS`). Until then nothing is probed or hooked.
4. **Kill switch**: `turbo_output\turbo_gui_disable.txt` (or env `TURBO_GUI_DISABLE=1`) keeps `Turbo.dll` from starting.
5. **Crash guard**: `turbo_output\turbo_gui_start.flag` exists while hooks are being installed / proven (300 frames) and while the
   first frames are drawn on screen (120 frames). A clean refusal or a clean process exit removes it; if the game dies in those
   phases it stays and the next start refuses to hook (message in `turbo_gui.log`; delete the file to try again).
6. **Stale files are never trusted**: `bridge_dll.json` carries `updated` (unix seconds), refreshed every ~2 s by the DLL. Lua
   reads the mailbox address only from a file younger than 15 s; an address left by an earlier game session is never dereferenced.
7. **Breadcrumbs**: every Lua start-up step is appended to `turbo_output\turbo_boot.log` *before* it runs (bounded to 64 KB), and the
   DLL logs each step (waiting, probing, MinHook, hooks, first frame) to `turbo_gui.log`, so a failure shows where it stopped.

## Two parts

| Part | Where | What |
| --- | --- | --- |
| Turbo (Lua) | `turbo/package` | Feature pack that runs inside Live Editor's Lua engine: 19 feature modules, 25 `turbo_*.lua` runner scripts, auto features, config, and the GUI bridge. |
| Turbo GUI (C++) | `turbogui/` | `Turbo.dll`: a Dear ImGui overlay (DirectX 12 `Present` hook via MinHook) with Players / Teams / Managers / Database editors and a button for every Turbo tool. `TurboProbe.exe` finds the hook targets in a separate process. `TurboInjector.exe` is an optional loader. |

Turbo.dll reads and writes the live database itself (`ReadProcessMemory`/`WriteProcessMemory` on the game process, using the
T3DB layout below). Anything that needs game functions (transfers, bans, form, ...) is sent to the Lua side through a mailbox
and run by Live Editor's Lua engine on the next career-mode event (or when the user runs `turbo_exec.lua`).

## Lua side (`turbo/package/lua`)

- `autorun/turbo_boot.lua` → `TURBO.boot({at_launch = true})`: pure Lua (see Launch safety); registers automatic features only.
- `scripts/turbo_gui_load.lua` → `bridge.start()`: writes the bridge files, registers the career-event tap and loads `turbo\Turbo.dll`
  with `package.loadlib`.
- `libs/v2/imports/turbo/turbo.lua`: module registry and `M.run(name, overrides, opts)` (`opts.silent` suppresses the message box).
- `core/`: version, log, trace (start-up breadcrumbs), util, env, config, db (validated writes), game, mem, events, csv, calib, select.
- `features/`: probe, form_morale, pap_playstyles, custom_headassets, custom_tattoos, delete_generated_players,
  export_season_stats, export_fixtures, export_transfer_history, extend_cpu_contracts, extend_user_contracts, headmodels,
  transfer_bans, squad_role, team_jersey_numbers, bulk_edit, player_moves, db_edit, export_table.
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
  (names, clubs, ages, teams, managers, dates), `bridge.*` (bridge files, mailbox).
- `ui/`: `app.*` (tick/draw, settings), `widgets.cpp` (validated field editors), `ui_players/teams/database/tools.cpp`, `playstyles.h`.
- `win/`: `dllmain.cpp` (start-up checks, `luaopen_turbo_gui`), `overlay_dx12.cpp` (hooks, ImGui DX12 backend, WndProc), `host.h`.
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
| `bash turbo/tests/run_tests.sh` | 100 Lua tests over a simulated game memory (incl. launch safety) | lua5.4, `turbo/le27/libs` |
| `luacheck --config turbo/tests/.luacheckrc turbo/package/lua` | lint | luacheck |
| `bash turbogui/tests/native/run_native.sh` | 3,268 native checks (engine vs Live Editor's Lua T3DB library, headless ImGui UI driving, ASan + UBSan). Includes clicking every GUI button that sends a command (28) and running each command through Turbo's real Lua bridge in a simulated career, and the Players list filters against a brute-force oracle | g++, lua5.4, `turbo/le27/libs` |
| `python3 scripts/check_field_names.py` | every table / field name in the GUI and Lua sources against EA's `db_meta.xml` (FIFA 21 Live Editor) and the names xAranaktu's FC 24–26 Live Editor scripts use | git, GitHub access |
| `bash turbogui/scripts/build_win.sh` | cross-compiles `Turbo.dll` + `TurboProbe.exe` + `TurboInjector.exe` | mingw-w64 (posix) |
| `bash turbogui/tests/win/run_smoke.sh` | loads the real `Turbo.dll` under Wine in 8 modes: imports, refusal without Live Editor, no hooking without a game window, kill switch, crash guard (clean exit / kill), mailbox, Lua-side start | Wine, mingw-w64 |
| `bash turbogui/tests/win/run_overlay_wine.sh` (`MODE=probe` default, `MODE=fallback`) | runs `tests/win/game_stub.cpp`, a Direct3D 12 stand-in game (not FC 27), under Wine + vkd3d + Mesa lavapipe on Xvfb; it loads `Turbo.dll` while presenting, presses F8 from another thread and resizes its swap chain. Checks the log (waited, hooked, queue, ImGui ready, start and drawing proven, no errors, crash flag cleared) and screenshots (nothing drawn while hidden, the window after F8 and after the resize) | Wine, Xvfb, mesa-vulkan-drivers, ImageMagick |
| `bash scripts/package.sh` | builds `dist/FC27_LE_Turbo_<version>.zip` (the files to copy into the Live Editor folder) | zip, built binaries |

## What is and is not verified

Verified by execution (see the commands above): the Lua feature pack and bridge against a simulated game memory; the native
database engine, model and every UI panel against a memory image produced by the same simulator; every GUI command through
the real Lua side; every database name against independent schema sources; the Windows `Turbo.dll` loads under Wine,
exports `luaopen_turbo_gui`, refuses to start without Live Editor, starts from the Lua side's signal and publishes a valid
mailbox; inside a running Direct3D 12 stand-in game (Wine + vkd3d) it hooks, draws its window on F8 and survives a resize,
both with `TurboProbe.exe` and with the in-game fallback.

**Not verified (cannot be executed outside Windows + the game):** the DirectX 12 hooks and drawing inside FC 27 on a real
Windows driver, the real `package.loadlib` call inside Live Editor, and the Lua-side bridge against the real FC 27 process. The simulator's memory
layout is derived from Live Editor's open Lua libraries; field names beyond those used by Live Editor's own scripts are
confirmed only by `turbo_probe` on a real install. Use the in-game checklist in `TURBO_README.md`.

## FC 26 → FC 27 differences that matter

- `career_mode/FCECareerModeUserManager.lua`: `mUserType` `0x37→0x2F`, `mPlayerId` `0x3C→0x34`, VPRO playerid `30999→31399`.
- `career_mode/helpers.lua`: `SetSquadRole` is a stub in FC 27 LE; user club team id = `ReadInt(ReadPointer(UserManager+0x18)+0x1F4)`.
- Career event constants were renamed `ENUM_CM_EVENT_MSG_*` → `ENUM_FCEGameModesCM_EVENT_MSG_*` (same IDs). Turbo resolves both names.
- Match sharpness no longer exists in FC 27 (`SetPlayerSharpness` removed).

## Not implemented yet

Match setup overrides, gameplay toggles (CPU vs CPU, unlimited subs, never tired, match time/score), manager market / job security /
fire, endless career, reveal player data, negotiation bypasses, match-fixing, job offers and minifaces. These need code hooks
inside FC27.exe and in-game analysis first. The full FC 26 → Turbo map is `docs/fc26-parity.md`.
