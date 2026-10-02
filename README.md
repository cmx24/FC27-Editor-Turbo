# FC27-Editor-Turbo

**FC 27 LE Turbo 0.2.4**: FC 26 Live Editor features for **FC 27 Live Editor** (public build v27.1.0 or newer), plus an
in-game window (the **Turbo GUI**) with Players / Teams / Managers / Database editors and a button for every Turbo tool.

Which FC 26 Live Editor feature is where (Turbo window, Database tab, FC 27 Live Editor itself, or not available yet):
[`docs/fc26-parity.md`](docs/fc26-parity.md).

Offline Career Mode / Kick-Off only. Never use Live Editor or Turbo in online modes. Turbo runs next to an official,
unmodified Live Editor and never modifies, copies or redistributes its files.

## Install (the build to test)

1. Install the official FC 27 Live Editor as usual.
2. Unzip [`dist/FC27_LE_Turbo_0.2.4.zip`](dist/FC27_LE_Turbo_0.2.4.zip) into the Live Editor folder (the folder with `FCLiveEditor.DLL`). Nothing of Live Editor is overwritten.
3. Start the game through Live Editor as usual. Nothing to run: about 20 seconds after the main menu appears, press **F8**.
4. Load a career: the Turbo window's top line says **Connected**. (At the main menu, run `turbo_gui_load.lua` in Live Editor's
   Lua Engine to connect there.)

[`turbo/package/TURBO_README.md`](turbo/package/TURBO_README.md) has the in-game test checklist and the recovery steps
(game will not launch: delete `lua\autorun\turbo_boot.lua`; kill switch: `turbo_output\turbo_gui_disable.txt`).

Back up your career save before testing.

**0.2.0 broke game launch** (it loaded `Turbo.dll`, probed Direct3D 12 and called game natives while Live Editor was still
initialising the game). Since 0.2.1 none of that is on the launch path; see `docs/turbo-reference.md` (Launch safety).

**New in 0.2.3**: in the first in-game test of 0.2.2 the game launched, but F8 did nothing because the GUI was only loaded by
`turbo_gui_load.lua`, which did not run. 0.2.3 loads `Turbo.dll` at launch, inert: it imports no Direct3D / DXGI DLL and does
nothing until Live Editor writes "Initial setup done" for this game session in its own log (about 15 s after the main menu).
It connects to the database on the first career event. `gui.autoload = false` in `turbo_config.json` restores the 0.2.2 behaviour.

**New in 0.2.4**: in FC 27, 0.2.3 launched, loaded and drew the Turbo window next to Live Editor's (user's screenshot), but it
stayed "waiting for Turbo's Lua side (bridge_meta.json)" even after `turbo_gui_load.lua`. Turbo read `GetDBMeta()` only if it
returned plain Lua tables; Live Editor's own t3db code only ever indexes it, which also works for C++ objects (userdata).
0.2.4 reads it the same way (and, if it cannot be iterated, through Live Editor's own table loader `LE.db`), and every failure
is reported: in `turbo_gui_load.lua`'s message box, in `turbo_boot.log`, in Live Editor's log and in the Turbo window.

**New in 0.2.2**: Players list filters (position 1–7, PlayStyle / PlayStyle+, retiring, min OVR / POT, max age), Bulk edit
players in Turbo Tools, Delete player (with confirmation), `TurboProbe.exe` (finds the Direct3D functions in a separate
process, so nothing is created inside the game), fixes found by the new tests (Squad role button sent the wrong role, two
wrong manager field names, the window and some buttons off-screen on small displays), and the show/hide key and mouse are now
also polled, so they work when the game reads only raw input.

## Status

| Check | Result |
| --- | --- |
| Lua feature pack + GUI bridge + launch safety, 105 tests over a simulated game memory, with `GetDBMeta` as Lua tables and as C++ objects (`turbo/tests/run_tests.sh`) | 105 passed, 0 failed |
| luacheck on `turbo/package/lua` | 0 warnings, 0 errors |
| Native engine + every UI panel, 3,283 checks with AddressSanitizer + UBSan (`turbogui/tests/native/run_native.sh`), including every GUI button that sends a command (28) run through Turbo's real Lua side and the Live Editor log reader | 3,283 passed, 0 failed |
| Every table / field name Turbo uses against EA's database schema and xAranaktu's FC 24–26 scripts (`scripts/check_field_names.py`) | 110 names: 108 known, 2 deliberately allowed, 0 unknown |
| Windows `Turbo.dll` / `TurboProbe.exe` / `TurboInjector.exe` cross-build (mingw-w64) | builds with 0 warnings; `Turbo.dll` imports only KERNEL32, USER32, GDI32, msvcrt |
| Real `Turbo.dll` loaded under Wine (`turbogui/tests/win/run_smoke.sh`): imports, refusal without Live Editor, no hooking without a game window, kill switch, crash guard (clean exit and kill), mailbox, Lua-side start, launch mode (inert until Live Editor's `Initial setup done` or the Lua side runs) | all 10 modes pass |
| Real `Turbo.dll` inside a running Direct3D 12 program (a stand-in, not FC 27) under Wine + vkd3d + software Vulkan (`turbogui/tests/win/run_overlay_wine.sh`): loaded in launch mode, waits for Live Editor's line, hooks while it renders, F8 shows the window (screenshot), survives a swap-chain resize | passes with `TurboProbe.exe` and with the in-game fallback |
| Inside FC 27 with Live Editor (user's tests, 02-10-2026) | 0.2.2: the game launches. 0.2.3: the game launches with the launch-time load, and F8 draws the Turbo window in game next to Live Editor's; the database was not connected (fixed in 0.2.4, **not yet run in game**) |

## Layout

| Path | What |
| --- | --- |
| `turbo/package/` | The Lua feature pack, exactly as installed (`lua/`, `turbo_config.json`, `TURBO_README.md`) |
| `turbo/tests/` | Lua test suite and game-memory simulator |
| `turbogui/` | C++ source of `Turbo.dll` / `TurboProbe.exe` / `TurboInjector.exe` (Dear ImGui + MinHook + nlohmann/json vendored in `third_party/`), native tests, Wine smoke and overlay tests |
| `scripts/package.sh` | Builds `dist/FC27_LE_Turbo_<version>.zip` |
| `scripts/check_field_names.py` | Checks every database name Turbo uses against independent schema sources |
| `docs/fc26-parity.md` | Every FC 26 Live Editor feature group and script, and where it is in Turbo 0.2.4 |
| `docs/turbo-reference.md` | Architecture, bridge contract, build/test commands, what is and is not verified |
| `docs/HANDOVER.md` | Where the work stands, what the user's in-game tests showed, next steps for a local session |

## Build and test

The tests run against Live Editor's own Lua libraries, which are not redistributed here: copy `<Live Editor>\lua\libs` to
`turbo/le27/libs` (see `turbo/le27/README.txt`).

```bash
bash turbo/tests/run_tests.sh                 # needs lua5.4
bash turbogui/tests/native/run_native.sh      # needs g++, lua5.4
bash turbogui/scripts/build_win.sh            # needs mingw-w64 (posix threads)
bash turbogui/tests/win/run_smoke.sh          # needs wine64
bash turbogui/tests/win/run_overlay_wine.sh   # needs wine64, Xvfb, mesa-vulkan-drivers, ImageMagick (MODE=fallback too)
python3 scripts/check_field_names.py          # needs git + GitHub access
bash scripts/package.sh                       # needs zip
```

Not included yet: match setup overrides, gameplay toggles, manager market / job security / firing, endless career, reveal
player data, negotiation bypasses, match-fixing, job offers and minifaces (they need code hooks inside FC27.exe found by
in-game analysis first). See `docs/fc26-parity.md`.
