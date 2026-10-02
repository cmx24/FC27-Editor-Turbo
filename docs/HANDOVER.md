# Handover: FC 27 LE Turbo, continue locally (Claude Cowork)

Written at commit `b32d74b` (Turbo **0.2.4**) on branch `claude/trusting-cannon-rkxwrw` of `cmx24/FC27-Editor-Turbo`.
The previous work ran in a cloud container with no access to the game; this file is everything a local session needs
to carry on with the game and Live Editor on the same PC.

## 1. What this project is

**FC 27 LE Turbo** brings FC 26 Live Editor features to **FC 27 Live Editor** (xAranaktu, v27.1.2 installed at
`C:\FC 27 Live Editor`, game at `C:\Program Files\EA Games\EA SPORTS FC 27`, game version 1.0.140.64835). Two parts:

| Part | Source | Installed as |
| --- | --- | --- |
| Lua feature pack + GUI bridge | `turbo/package/lua/...` | `lua\autorun\turbo_boot.lua`, `lua\scripts\turbo_*.lua` (25), `lua\libs\v2\imports\turbo\` |
| Turbo GUI (C++): Dear ImGui overlay drawn in the game via Direct3D 12 hooks | `turbogui/src` | `turbo\Turbo.dll`, `turbo\TurboProbe.exe`, `turbo\TurboInjector.exe` |

Install = unzip `dist/FC27_LE_Turbo_0.2.4.zip` into `C:\FC 27 Live Editor` (it never overwrites Live Editor files).
Reference docs: `docs/turbo-reference.md` (architecture, bridge contract, launch safety, tests),
`docs/fc26-parity.md` (all 41 FC 26 feature groups and where they are), `turbo/package/TURBO_README.md` (user guide,
recovery table, in-game checklist).

### Ground rules (keep them)

- Offline Career Mode / Kick-Off only. Never touch online modes.
- Never modify, decompile, disassemble or patch `FCLiveEditor.DLL` or the game. No bypassing Live Editor's Patreon
  authentication, no anti-cheat work.
- Never commit Live Editor's files (its `lua\libs` is used as a test fixture in `turbo/le27/libs`, which is gitignored).
- Never commit personal data: Live Editor's logs contain the user's Patreon ID and name. Quote only the lines you need.
- Every database write is range-checked; destructive actions need confirmation.
- Commit on `claude/trusting-cannon-rkxwrw`; no pull request unless the user asks. Commit trailer used so far:
  `Co-Authored-By: Claude <noreply@anthropic.com>`.

## 2. How it starts (0.2.3+)

1. At game launch Live Editor runs `lua\autorun\turbo_boot.lua` → `TURBO.boot({at_launch=true})`: **no game native** is called.
   It registers the documented `post__CareerModeEvent` handler and loads `turbo\Turbo.dll` with `package.loadlib` in
   "launch" mode (`turbo_output\turbo_gui_load.json` = `{"mode":"launch"}`). Disable with `"gui": {"autoload": false}`.
2. `Turbo.dll` imports only KERNEL32/USER32/GDI32/msvcrt (D3D12, DXGI, D3DCompile, DWM resolved lazily in
   `src/win/lazy_imports.cpp`). In launch mode it does nothing until Live Editor writes `Initial setup done` for this
   process in `Logs\live_editor_<dd-mm-yyyy>.log` (session matched by `Module <FCLiveEditor.DLL> 0x<base>-`), or until
   Turbo's Lua side writes `bridge_state.json` (`src/win/dllmain.cpp`, `src/core/le_log.*`).
3. Then it waits for the game window + 5 s, finds hook targets (TurboProbe.exe, else an in-game probe), hooks
   Present/Present1/ResizeBuffers/ResizeBuffers1/ExecuteCommandLists with MinHook and draws on F8.
4. **Database connection** needs the Lua side to write `turbo_output\bridge_meta.json` (from `GetDBMeta()`) and
   `bridge_state.json` (DB service address from `GetPlugin`). That happens in `bridge.start()`
   (`lua\libs\v2\imports\turbo\bridge.lua`), called by `lua\scripts\turbo_gui_load.lua` (manual) or automatically on the
   **first career-mode event**. Outside a career Live Editor gives Lua no events, so at the main menu only the script can do it.

Logs to read: `turbo_output\turbo_boot.log` (every Lua start-up step, written before it runs),
`turbo_output\turbo_gui.log` (every DLL step), `Logs\live_editor_<date>.log` (Live Editor; Turbo lines start with `[Turbo]`;
also visible in Live Editor's in-game Logger window).

## 3. What is verified in the real game (user tests, 02-10-2026)

| Build | Result |
| --- | --- |
| 0.2.0 | Broke game launch (game died inside the autorun call; it called GetPlugin/GetDBMeta, loaded the DLL and probed D3D12 at launch). |
| 0.2.2 | Game launches. F8 did nothing: `turbo_gui_load.lua` never ran (no trace line), so the DLL was never loaded. |
| 0.2.3 | Game launches with the launch-time DLL load. `turbo_gui.log`: waited for `Initial setup done` (58 s after load), TurboProbe could not match `execute` (FC 27 ships its own `x64\D3D12Core.dll`, Agility SDK) → in-game probe OK → hooks installed → queue = "last direct queue that submitted work" (so `IDXGISwapChain::GetDevice(ID3D12CommandQueue)` does **not** return the queue on Windows) → 3 back buffers, format 24 → overlay proven → F8 drew the window in game (screenshot, next to Live Editor's window). **Not connected**: "waiting for Turbo's Lua side (bridge_meta.json)". |
| 0.2.4 | Session 18:10: DLL waited for Live Editor, started TurboProbe.exe at 18:10:45, then the log stops; the game process ended within ~20 s without a clean exit. The crash guard (`turbo_output\turbo_gui_start.flag`) was held, so the next launches (18:11:05, 18:11:09) refused to start Turbo ("previous Turbo GUI start did not finish"). The 0.2.4 database fix never ran. |

Across all sessions `turbo_boot.log` has **no** `turbo_gui_load.lua started` line, although the user said they ran the
script. Either it was never executed through Live Editor's Lua Engine, or Live Editor runs it in a way that fails before
its first line. This is the first thing to find out (section 5).

What 0.2.4 changed (untested in game): `bridge.write_meta` no longer requires `GetDBMeta()` to be plain Lua tables
(Live Editor's own `t3db` code only indexes it, so it may be C++ userdata); it iterates only inside `pcall`, falls back to
Live Editor's own loader (`LE.db:Reset()` → `LE.db.tables`), and every failure is reported (message box, `turbo_boot.log`,
`[Turbo]` warning in Live Editor's log, `meta_error` in `bridge_state.json`, shown in the Turbo window's top line).

## 4. First steps on the user's PC

1. Delete `C:\FC 27 Live Editor\turbo_output\turbo_gui_start.flag` (it blocks Turbo after the 18:10 session).
2. Confirm 0.2.4 is installed: `lua\libs\v2\imports\turbo\core\version.lua` says `0.2.4`.
3. Start the game through Live Editor. At the main menu wait ~20 s, press F8: the Turbo window should appear
   ("Not connected"). Read `turbo_gui.log`.
4. Run `lua\scripts\turbo_gui_load.lua` **through Live Editor's Lua Engine** (find out exactly how FC 27 LE runs a script:
   its window, load/execute buttons). Expected: `turbo_boot.log` gets `turbo_gui_load.lua started` and
   `bridge.start: bridge_meta.json written from GetDBMeta|LE.db` (or `NOT written: <reason>`), a message box
   ("Game database shared with the Turbo GUI" or "Turbo GUI problem: ..."), and the Turbo window turns **Connected**.
5. If nothing is traced, test the Lua Engine itself with a one-liner script, e.g.
   `Log("[Turbo test] " .. type(GetDBMeta) .. " " .. type(require) .. " " .. type(io) .. " " .. tostring(package and package.loadlib))`
   and `local m = GetDBMeta(); Log(type(m) .. " " .. type(m.field_desc_map) .. " " .. tostring(pcall(pairs, m.field_desc_map)))`.
   These answer: is `require`/`io` available to manual scripts, and what does `GetDBMeta` really return.
6. Load a career: the first career event should connect the GUI automatically (no script).
7. Then run the in-game checklist in `TURBO_README.md` (edit an attribute, kit number, budget, Turbo Tools buttons).

## 5. Planned 0.2.5 (designed, not implemented)

| Change | Where | Why |
| --- | --- | --- |
| Skip TurboProbe.exe when the game's loaded `D3D12Core.dll` is not the one in System32 (go straight to the in-game probe) | `turbogui/src/win/overlay_dx12.cpp` `start_overlay`, before `find_targets_external` | In FC 27 TurboProbe can never match `ExecuteCommandLists`; it only adds an external process and seconds (the 18:10 session ended during it). |
| Hold the crash guard only around in-game work (in-game probe, MinHook enable, first 300 frames), not while waiting or while TurboProbe runs | move `guard_hold` from `init_thread` (`dllmain.cpp`) into `start_overlay`; `guard_release` on failure stays in `init_thread` | A game closed or crashing for any reason during the long wait/probe window switched Turbo off for good. |
| Retry once: a plain flag → log and try again (write the flag as `RETRY: ...`); a `RETRY:` flag → stay off | `dllmain.cpp` `init_thread` flag check and `guard_hold` | One unlucky exit should not need manual file deletion; two in a row still protects the game. Update smoke modes `guard` (RETRY flag → refuse) and add `guardretry`. |
| At launch, warn in Live Editor's log when the flag exists (pure Lua `io`) | `turbo.lua` `M.boot` (at_launch branch) | The user sees Live Editor's Logger window; today the reason is only in `turbo_gui.log`. |
| Ignore a `bridge_state.json` older than the DLL load − 2 min | `turbogui/src/core/bridge.*` (`poll_files`, new `set_min_state_time`), set in `start_overlay` | With the launch-time load the DLL could read a previous game session's DB address. ReadProcessMemory makes it safe, but it must not be used. |
| TurboProbe: inherit only the stdout pipe (`STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`) | `find_targets_external` | Today `bInheritHandles=TRUE` hands every inheritable game handle to the child. |
| If `GetDBMeta` still fails in game: build the meta from the documented `GetDBTablesNames()` / `GetDBTableFields(name)` (DOC.MD: array of `DBFieldDescription`, `fields[i]["name"]`) | `bridge.lua` `write_meta` | Needs a check of what `DBFieldDescription` contains (short name? depth? min?) in `C:\FC 27 Live Editor\lua\DOC.MD`. |

## 6. Build and test

The scripts are bash and were run on Ubuntu (use WSL Ubuntu locally). Packages: `mingw-w64` (posix threads,
`x86_64-w64-mingw32-g++-posix`), `wine64`, `lua5.4`, `lua-check` (luacheck), `zip`, and for the overlay test `xvfb`,
`mesa-vulkan-drivers`, `imagemagick`. Copy `C:\FC 27 Live Editor\lua\libs` to `turbo/le27/libs` first (test fixture, gitignored).

| Command | Expected at 0.2.4 |
| --- | --- |
| `bash turbo/tests/run_tests.sh` | TOTAL: 105 passed, 0 failed |
| `luacheck --config turbo/tests/.luacheckrc turbo/package/lua` | 0 warnings / 0 errors |
| `bash turbogui/tests/native/run_native.sh` | RESULT 3283 passed, 0 failed (ASan + UBSan) |
| `bash turbogui/scripts/build_win.sh` | 0 warnings; fails if Turbo.dll imports a graphics DLL |
| `bash turbogui/tests/win/run_smoke.sh` | ALL SMOKE MODES PASSED (10 modes) |
| `MODE=probe bash turbogui/tests/win/run_overlay_wine.sh` and `MODE=fallback ...` | OVERLAY TEST PASSED (18 and 17 checks) |
| `python3 scripts/check_field_names.py` | 110 names: 108 known, 2 allowed, 0 unknown (needs GitHub access) |
| `bash scripts/package.sh` | `dist/FC27_LE_Turbo_<version>.zip`; version in `core/version.lua` must equal `kGuiVersion` in `turbogui/src/ui/app.h` |

`smoke_loader.exe` (built by `run_smoke.sh` into `turbogui/build/smoke/bin`) also runs on Windows itself without Wine:
`smoke_loader.exe <folder> <mode>` (see `run_smoke.sh` for the folder layout). Release routine used so far: bump `core/version.lua`, `kGuiVersion`, the smoke loader's `gui_version` check;
run everything above; package; `git rm` the previous zip; verify the zip's DLL equals the tested build; commit; push.

The simulator (`turbo/tests/mock/sim.lua`) models game memory and Live Editor natives; `sim.meta_mode` = `"table"`,
`"userdata"` or `"userdata_noiter"` controls what `GetDBMeta` returns. Anything learned in game about natives' real return
types should be added there.

## 7. Facts from the user's logs worth keeping

- Live Editor log lines: `Module <FCLiveEditor.DLL> 0x<base>-0x<end>` at injection; `[LUA] Execute: ...\turbo_boot.lua`;
  `Main Menu reached`; `Initial setup done` ~15 s later (after Live Editor's own DX12 hooks and its Patreon check).
- Live Editor hooks the same five functions (Present at `dxgi.dll+0x19530`, ExecuteCommandLists in the game's
  `x64\D3D12Core.dll`); MinHook chains onto them and both overlays draw.
- Live Editor's launcher sometimes sees two FC27.exe processes; in one session Turbo loaded into two processes 4 s apart.
- GPU: NVIDIA RTX 5090 Laptop (hybrid with Intel iGPU), Windows build 26300; game window 2582x1550, swap chain format 24
  (R10G10B10A2), 3 back buffers.
