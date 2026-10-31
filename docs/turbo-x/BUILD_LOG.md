# Turbo X build log (2026-10-31, afternoon session)

Written by the integrator (Claude Fable 5.1, cloud session) for the owner's record. Every line says what was verified and how. Nothing in this log was run in the game.

## Ground truth

- Branch `turbo-x` cut from `5.0c/integration` @ 00b28622 (the reviewed head; the owner's tested DLL A was 075a53a9, 30 commits earlier).
- Toolchain here: `x86_64-w64-mingw32-g++-posix` (GCC 13.2, Ubuntu package) on Linux, driving the project's own `turbogui/scripts/build_win.sh`. The owner's PC builds the same script under MSYS2 UCRT64; md5s will differ between the two toolchains for the same commit, which is why every package carries its own md5 and `BUILD.txt`.
- Baseline: `5.0c/integration` @ 00b28622 compiled here first (183 files, exit 0, 8m32s on 2 cores, `Turbo.dll` md5 9d6e6b07). That is the control: the branch compiled before any change.
- Native tests (`turbogui/tests/native/run_native.sh`) cannot run here: they need Live Editor's Lua libs and lua5.4, neither present. Test files touched below are "updated, not run".

## Merges into turbo-x, in order

| # | Branch | Step | Commit | Compile after merge | Acceptance greps |
|---|---|---|---|---|---|
| 1 | — | version string `X 1.0`, test card | 3b723c9 | (in #2) | — |
| 2 | tx-settings | 8–9 sliders, live write only, copy | 506a188 → 87a3e36 | exit 0, 0 warnings | `SliderInt` in ui_game_settings.cpp = 1; `turbo_settings_apply` in ui_game_settings.* = 0 (7 hits remain in the two tactics pages, step 5 territory); banned state sentences: 0 in settings files (2 unrelated "read-only here" in core profile/rules loaders, 1 in ui_tactic_presets.cpp) |
| 3 | tx-scripts | 1–2 Protect/Restore/Record-BuildIdentity | 6987c37 → d746bb5 | n/a (PowerShell; static check PASS ×3, not executed) | — |
| 4 | tx-picker | 7 one picker route; old routes unreachable, not yet deleted | 6458db3 → 2c8910b | see below | jargon strings in ui_visual_picker.cpp = 0; `TURBO-X: unreachable` markers = 5; no caller of item_galleries/tattoo_editor/gallery_picker/##f3pick/##gallerypick/##tattoopick outside the two old files |
| 5 | tx-menus | 6 cut menus by deletion | pending | pending | pending |

## Known follow-ups (not done, stated plainly)

- Native tests asserting old settings strings: `tests/native/test_game_settings.h:613, :629, :1030`, `tests/native/test_sliders_rw.h:306-312` — to be updated to the new strings; cannot be run here.
- The tactics pages (`ui_tactic_presets.cpp`, `ui_tactics_page.cpp`) still describe the `turbo_settings_apply.exe` flow; untouched on purpose (step 5 says study tactics before changing them).
- Step 7 second half: delete `ui_images.cpp` galleries and the `ui_faces3d_gate.cpp` modal after the owner's in-game apply/undo proof; the exact line ranges are in the picker agent's report (kept in this folder as PICKER_DELETION_LIST.md).
- Steps 3 (saved sizes) and 4 (audio) are test-only: nothing to build; the test card says what to check.
- Step 5 (tactics study) needs the game; not started.
