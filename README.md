# FC27-Editor-Turbo

**FC 27 LE Turbo 1.0.2**: FC 26 Live Editor features for **FC 27 Live Editor** (public build v27.1.0 or newer), plus an
in-game window (the **Turbo GUI**) with Players / Teams / Managers / Database editors and a button for every Turbo tool.

Which FC 26 Live Editor feature is where, and its status in 1.0.2 (verified in game, untested in a match, not in Turbo 1.0,
not possible in FC 27): [`docs/fc26-parity.md`](docs/fc26-parity.md). What changed: [`CHANGELOG.md`](CHANGELOG.md).

Offline Career Mode / Kick-Off only. Never use Live Editor or Turbo in online modes. Turbo runs next to an official,
unmodified Live Editor and never modifies, copies or redistributes its files.

## What Turbo 1.0 does

Turbo 1.0 is for Manager Career players. Many of its features call the game's own code, so the game makes the change
and its own screens show it. Seen working in a test career:

- **Players**: a full editor (sliders, combos, check boxes, PlayStyles and traits, star-head, tattoo, hair and item
  galleries, undo), minifaces from the game's 3D model, export / import / clone (Live Editor preset CSV and Turbo JSON),
  develop to potential, reveal player data through the game's own scouting.
- **Callnames**: the commentators speak the name you pick, in the commentary language the game has loaded
  (Italian: 2,462 surnames, 751 player recordings).
- **Career**: a real job offer from the club you pick; league table edits shown on the game's Standings screen at once;
  injuries-off switch.
- **Editors** for teams, managers and any database table, and a Status tab. Buttons work at once, no day advance needed.
- Also in 1.0, being checked in game now: transfer / loan lists for your own players, job security and unsackable,
  transfer budget, team name / colours / crest, create a player from a template, miniface from an image file, manager
  moves, youth academy tools, match setup switches and fixture swaps.

Built but not yet seen in a played match: forced results, injuries off during a match, editing a played result, weekly
forced growth. Not in 1.0: transfers of your own club through the game's engine, live season stats, stadium override and
the features FC 27 Live Editor already has. Not possible in FC 27: transfer bans, VAR off, match sharpness.

## Install

1. Install the official FC 27 Live Editor as usual.
2. Unzip `FC27_LE_Turbo_1.0.2.zip` into the Live Editor folder (the folder with `FCLiveEditor.DLL`). Same layout as 0.3.0
   and 0.4.0. Nothing of Live Editor is overwritten.
3. Start the game through Live Editor as usual. Nothing to run: about 20 seconds after the main menu appears, press **F8**.
4. Load a career: the Turbo window's top line says **Connected**. (At the main menu, run `turbo_gui_load.lua` in Live Editor's
   Lua Engine to connect there.)

[`turbo/package/TURBO_README.md`](turbo/package/TURBO_README.md) has the in-game test checklist and the recovery steps
(game will not launch: delete `lua\autorun\turbo_boot.lua`; kill switch: `turbo_output\turbo_gui_disable.txt`).

Back up your career save before testing. Offline career only. If one feature misbehaves, turn just that one off with an
empty file in `turbo_output`: `call_<name>_off.txt` for a game call, `hook_<name>_off.txt` for a game hook (the Status tab
lists the names).

**New in 0.2.5** (tested in a real FC 27 career, 02-10-2026: self-test 13 passed, 0 failed, 6 skipped):

- Fixed the two in-career crashes: Live Editor's message box formats text like printf (a `%` crashed the game), and Live
  Editor's memory reads crash on unreadable addresses. Turbo.dll now publishes a map of readable memory and Turbo's Lua
  side reads game memory only inside it.
- Player names in FC 27 are compressed in memory; they now come from Live Editor (`GetDBTableRows`).
- FC 27 Live Editor v27.1.2 does not ship the natives behind transfer budget, transfers / loans / transfer lists, transfer
  bans, player deletion, player development and season stats (the FC 26 Lua wrappers are there, the natives are not).
  Turbo detects that, greys those buttons out with the reason, and they work again once Live Editor adds the natives.
- FC 27 replacements Turbo does itself: transfer history (completed transfers and loans) and your club's remaining fixtures
  read from the game, database league numbers instead of live season stats, generated-player count.
- Your squad now includes the goalkeeper (FC 27's team sheet slot 0, which Live Editor's own helper skips).
- UI size follows the screen and has a size slider; wage and release clause editors; input shield (the game does not see
  keys typed into Turbo; the newest build also hides mouse clicks over the Turbo window, not yet tried in game);
  crash guard retries once; TurboProbe is skipped for FC 27.

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
| Lua feature pack + GUI bridge + launch and memory safety, 129 tests over a simulated game memory, including a mode with exactly the natives of FC 27 LE v27.1.2 (`turbo/tests/run_tests.sh`) | 129 passed, 0 failed |
| luacheck on `turbo/package/lua` | 0 warnings, 0 errors |
| Native engine + every UI panel, 3,336 checks with AddressSanitizer + UBSan (`turbogui/tests/native/run_native.sh`), including every GUI button that sends a command (30) run through Turbo's real Lua side, greyed-out tools, readable-memory map | 3,336 passed, 0 failed |
| Every table / field name Turbo uses against the real FC 27 schema dumped from the game (`scripts/check_fc27_schema.py`) | 116 names: 106 present, 10 optional absent, 0 missing |
| Windows `Turbo.dll` / `TurboProbe.exe` / `TurboInjector.exe` cross-build (mingw-w64) | builds with 0 warnings; `Turbo.dll` imports only KERNEL32, USER32, GDI32, msvcrt |
| Real `Turbo.dll` loaded under Wine (`turbogui/tests/win/run_smoke.sh`): imports, refusal without Live Editor, no hooking without a game window, kill switch, crash guard (clean exit and kill), mailbox, Lua-side start, launch mode (inert until Live Editor's `Initial setup done` or the Lua side runs) | all 11 modes pass |
| Real `Turbo.dll` inside a running Direct3D 12 program (a stand-in, not FC 27) under Wine + vkd3d + software Vulkan (`turbogui/tests/win/run_overlay_wine.sh`): loaded in launch mode, waits for Live Editor's line, hooks while it renders, F8 shows the window (screenshot), survives a swap-chain resize, 8 input-shield hooks leave the program's own input working | passes with `TurboProbe.exe` and with the in-game fallback |
| Inside FC 27 with Live Editor (02-10-2026, career loaded) | 0.2.5: launches, F8 window, connected to the database (21,610 players, names), an Overall edit shows in the game's Team Management, in-game date correct, self-test 13 passed / 0 failed / 6 skipped (natives missing in LE v27.1.2), no crash. Not yet tried in game: the last input-shield hooks (mouse over the Turbo window) and the greyed-out buttons |

## Layout

| Path | What |
| --- | --- |
| `turbo/package/` | The Lua feature pack, exactly as installed (`lua/`, `turbo_config.json`, `TURBO_README.md`) |
| `turbo/tests/` | Lua test suite and game-memory simulator |
| `turbogui/` | C++ source of `Turbo.dll` / `TurboProbe.exe` / `TurboInjector.exe` (Dear ImGui + MinHook + nlohmann/json vendored in `third_party/`), native tests, Wine smoke and overlay tests |
| `scripts/package.sh` | Builds `dist/FC27_LE_Turbo_<version>.zip` |
| `scripts/check_field_names.py` / `scripts/check_fc27_schema.py` | Check every database name Turbo uses against independent schema sources / the FC 27 schema dumped in game |
| `scripts/callname_voice_log.py` / `scripts/playtest_monitor.py` | Summarize the voice-swap observe log / watch a long play session for errors and crash dumps (`docs/callnames.md` section 12) |
| `docs/fc26-parity.md` | Every FC 26 Live Editor feature group and script, and its status in Turbo 1.0.2 |
| `CHANGELOG.md` | What changed in each release |
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

Not included yet: see "What Turbo 1.0 does" above and `docs/fc26-parity.md` (rows marked **Not in Turbo 1.0** and
**Not possible in FC 27**).
