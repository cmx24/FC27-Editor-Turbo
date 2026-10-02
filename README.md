# FC27-Editor-Turbo

**FC 27 LE Turbo 0.2.0**: FC 26 Live Editor features for **FC 27 Live Editor** (public build v27.1.0 or newer), plus an
in-game window (the **Turbo GUI**) with Players / Teams / Managers / Database editors and a button for every Turbo tool.

Offline Career Mode / Kick-Off only. Never use Live Editor or Turbo in online modes. Turbo runs next to an official,
unmodified Live Editor and never modifies, copies or redistributes its files.

## Install (the build to test)

1. Install the official FC 27 Live Editor as usual.
2. Unzip [`dist/FC27_LE_Turbo_0.2.0.zip`](dist/FC27_LE_Turbo_0.2.0.zip) into the Live Editor folder (the folder with `FCLiveEditor.DLL`). Nothing of Live Editor is overwritten.
3. Start the game through Live Editor, load a career, press **F8**. If nothing happens, see
   [`turbo/package/TURBO_README.md`](turbo/package/TURBO_README.md) (it also holds the in-game test checklist).

Back up your career save before testing.

## Status

| Check | Result |
| --- | --- |
| Lua feature pack + GUI bridge, 93 tests over a simulated game memory (`turbo/tests/run_tests.sh`) | 93 passed, 0 failed |
| luacheck on `turbo/package/lua` | 0 warnings, 0 errors |
| Native engine + every UI panel, 2,840 checks with AddressSanitizer + UBSan (`turbogui/tests/native/run_native.sh`) | 2,840 passed, 0 failed |
| Windows `Turbo.dll` cross-build (mingw-w64) | builds with 0 warnings |
| Real `Turbo.dll` loaded under Wine (`turbogui/tests/win/run_smoke.sh`): imports, refusal without Live Editor, mailbox, Lua-side start | all 3 modes pass |
| DirectX 12 hooks and drawing inside FC 27; `package.loadlib` and the bridge inside the real Live Editor process | **not run** (needs Windows + the game); use the checklist in `TURBO_README.md` |

## Layout

| Path | What |
| --- | --- |
| `turbo/package/` | The Lua feature pack, exactly as installed (`lua/`, `turbo_config.json`, `TURBO_README.md`) |
| `turbo/tests/` | Lua test suite and game-memory simulator |
| `turbogui/` | C++ source of `Turbo.dll` / `TurboInjector.exe` (Dear ImGui + MinHook + nlohmann/json vendored in `third_party/`), native tests, Wine smoke test |
| `scripts/package.sh` | Builds `dist/FC27_LE_Turbo_<version>.zip` |
| `docs/turbo-reference.md` | Architecture, bridge contract, build/test commands, what is and is not verified |

## Build and test

The tests run against Live Editor's own Lua libraries, which are not redistributed here: copy `<Live Editor>\lua\libs` to
`turbo/le27/libs` (see `turbo/le27/README.txt`).

```bash
bash turbo/tests/run_tests.sh                 # needs lua5.4
bash turbogui/tests/native/run_native.sh      # needs g++, lua5.4
bash turbogui/scripts/build_win.sh            # needs mingw-w64 (posix threads)
bash turbogui/tests/win/run_smoke.sh          # needs wine64
bash scripts/package.sh                       # needs zip
```

Not included yet: match setup overrides, gameplay toggles, manager market / job security, endless career, reveal player data,
negotiation bypasses, match-fixing and job offers (they need code hooks inside FC27.exe and in-game analysis first).
