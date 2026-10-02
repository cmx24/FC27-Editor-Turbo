# FC27-Editor-Turbo

**FC 27 LE Turbo 0.2.2**: FC 26 Live Editor features for **FC 27 Live Editor** (public build v27.1.0 or newer), plus an
in-game window (the **Turbo GUI**) with Players / Teams / Managers / Database editors and a button for every Turbo tool.

Which FC 26 Live Editor feature is where (Turbo window, Database tab, FC 27 Live Editor itself, or not available yet):
[`docs/fc26-parity.md`](docs/fc26-parity.md).

Offline Career Mode / Kick-Off only. Never use Live Editor or Turbo in online modes. Turbo runs next to an official,
unmodified Live Editor and never modifies, copies or redistributes its files.

## Install (the build to test)

1. Install the official FC 27 Live Editor as usual.
2. Unzip [`dist/FC27_LE_Turbo_0.2.2.zip`](dist/FC27_LE_Turbo_0.2.2.zip) into the Live Editor folder (the folder with `FCLiveEditor.DLL`). Nothing of Live Editor is overwritten.
3. Start the game through Live Editor as usual. Turbo does **nothing native while the game launches**.
4. Once the game is running, open Live Editor's Lua Engine, run `turbo_gui_load.lua`, wait ten seconds, press **F8**.

[`turbo/package/TURBO_README.md`](turbo/package/TURBO_README.md) has the in-game test checklist and the recovery steps
(game will not launch: delete `lua\autorun\turbo_boot.lua`; kill switch: `turbo_output\turbo_gui_disable.txt`).

Back up your career save before testing.

**0.2.0 broke game launch** (it loaded `Turbo.dll`, probed Direct3D 12 and called game natives while Live Editor was still
initialising the game). Since 0.2.1 none of that is on the launch path; see `docs/turbo-reference.md` (Launch safety).

**New in 0.2.2**: Players list filters (position 1–7, PlayStyle / PlayStyle+, retiring, min OVR / POT, max age), Bulk edit
players in Turbo Tools, Delete player (with confirmation), `TurboProbe.exe` (finds the Direct3D functions in a separate
process, so nothing is created inside the game), fixes found by the new tests (Squad role button sent the wrong role, two
wrong manager field names, the window and some buttons off-screen on small displays), and the show/hide key and mouse are now
also polled, so they work when the game reads only raw input.

## Status

| Check | Result |
| --- | --- |
| Lua feature pack + GUI bridge + launch safety, 100 tests over a simulated game memory (`turbo/tests/run_tests.sh`) | 100 passed, 0 failed |
| luacheck on `turbo/package/lua` | 0 warnings, 0 errors |
| Native engine + every UI panel, 3,268 checks with AddressSanitizer + UBSan (`turbogui/tests/native/run_native.sh`), including every GUI button that sends a command (28) run through Turbo's real Lua side | 3,268 passed, 0 failed |
| Every table / field name Turbo uses against EA's database schema and xAranaktu's FC 24–26 scripts (`scripts/check_field_names.py`) | 110 names: 108 known, 2 deliberately allowed, 0 unknown |
| Windows `Turbo.dll` / `TurboProbe.exe` / `TurboInjector.exe` cross-build (mingw-w64) | builds with 0 warnings |
| Real `Turbo.dll` loaded under Wine (`turbogui/tests/win/run_smoke.sh`): imports, refusal without Live Editor, no hooking without a game window, kill switch, crash guard (clean exit and kill), mailbox, Lua-side start | all 8 modes pass |
| Real `Turbo.dll` inside a running Direct3D 12 program (a stand-in, not FC 27) under Wine + vkd3d + software Vulkan (`turbogui/tests/win/run_overlay_wine.sh`): hooks while it renders, F8 shows the window (screenshot), survives a swap-chain resize | passes with `TurboProbe.exe` and with the in-game fallback |
| Inside FC 27 with Live Editor: hooks and drawing, `package.loadlib`, the bridge against the real game | **not run** (needs Windows + the game); use the checklist in `TURBO_README.md` |

## Layout

| Path | What |
| --- | --- |
| `turbo/package/` | The Lua feature pack, exactly as installed (`lua/`, `turbo_config.json`, `TURBO_README.md`) |
| `turbo/tests/` | Lua test suite and game-memory simulator |
| `turbogui/` | C++ source of `Turbo.dll` / `TurboProbe.exe` / `TurboInjector.exe` (Dear ImGui + MinHook + nlohmann/json vendored in `third_party/`), native tests, Wine smoke and overlay tests |
| `scripts/package.sh` | Builds `dist/FC27_LE_Turbo_<version>.zip` |
| `scripts/check_field_names.py` | Checks every database name Turbo uses against independent schema sources |
| `docs/fc26-parity.md` | Every FC 26 Live Editor feature group and script, and where it is in Turbo 0.2.2 |
| `docs/turbo-reference.md` | Architecture, bridge contract, build/test commands, what is and is not verified |

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
