# Turbo X 1.0 — pre-release notes (2026-10-31)

**Status: compiled, booted and partly proven in game (2026-10-31 16:12–16:30 EDT, LEICESTER, driver run, Undo afterwards).** Built by the project's own `build_win.sh` (exit 0, 0 warnings, MinGW-w64 GCC 13.2). In the game: 129/129 hooks, footer "Turbo X 1.0", every area opens, the one picker opens for hair and beard with the same layout, a hair apply and its way back worked, the callname Preview path fires. Not yet seen: accessory/tattoo pickers, a slider move, saved sizes, audio actually heard. Details and shots: `BUILD_LOG.md`, `evidence\\2026-10-31\\turbox-rc2\\run3`. Install it only on the throwaway LEICESTER career, after `Protect-TurboFiles.ps1`, and follow `docs/turbo-x/TEST_CARD.md`.

Identity: `Turbo.dll` md5 `8b23d9c157f7c33475ed8f9ad5fa0bc1`, package `TurboX_<commit>_8b23d9c1.zip` (the commit is in the zip name and in its BUILD.txt; the docs commit after the build changes the id, the source does not: src is at ff1c547 + tests at 41cf307). The owner's PC builds the same script under MSYS2 UCRT64 and will produce a different md5 for the same commit; `BUILD.txt` in the zip carries this one.

## What changed against 5.0c/integration @ 00b28622 (the reviewed head)

Everything below follows `docs/review-2026-10-31/CONSENSUS_ACTION_PLAN.md` v2, step by step. Steps 3, 4 and 5 are test-only or need the game and have no code here.

- **Step 1–2, protect files and build identity** (`tools/turbo-x/`): `Protect-TurboFiles.ps1` copies the EA settings folder, Turbo presets and the live `Turbo.dll` to `turbo_backups\protect_<timestamp>\` with an MD5 manifest; `Restore-TurboFiles.ps1` puts them back; `Record-BuildIdentity.ps1` prints MATCH/MISMATCH between the installed DLL and the packaged one and appends to `docs/review-2026-10-31/BUILD_IDENTITY.md`. Statically checked (`static_check.py` PASS ×3). **Never executed**: no Windows here.
- **Step 6, menus cut by deletion**: the "Everything (classic)" tab, the duplicate Players > Callnames page, the whole Advanced ("Lab") area and its pages, the "Use the old pages" and "Preview the 4.0 menus" switches, and the 4.0 tree. 49 files, −3,562 lines, six source files deleted. One tab row on the player screen; Callname appears once. What lost its only door is listed in `BUILD_LOG.md`.
- **Step 7, one picker route**: hair, beard, accessories, tattoos, body type and kit all open `ui_visual_picker.cpp`. The old `item_galleries`, `tattoo_editor` and `gallery_picker` tables are unreachable (marked, not yet deleted: deletion waits for in-game proof, per the plan and Copilot's dissent). Each picker shows "N without picture — show".
- **Step 8–9, sliders and copy**: Game Settings uses real `SliderInt` on verified rows, written through the existing live block write; "read-only" / "not mapped" on the rest; no staging paragraph; cells ≤ 3 words.
- **Independent review** (after the merges): one functional regression from the menus cut fixed (CM Tracker → Create player), a false Undo claim removed, Refresh clears stale status, slider rescans throttled, 3D band chooser shows model-only tiles. Details in `BUILD_LOG.md`.
- **Version**: footer reads `Turbo X 1.0`.

## What is NOT in this build

- Step 5 (tactics study): the tactics pages still describe the `turbo_settings_apply.exe` flow. Untouched on purpose.
- Step 7 second half: the old picker code is still compiled in; `PICKER_DELETION_LIST.md` has the line ranges for after the proof.
- Native tests: brought in line (the test TU compiles, lint passes) but **not run** here; the legacy UI block of test_main.cpp still drives the deleted classic editor and will fail at runtime until re-routed.

## Undo

`tools\turbo-x\Restore-TurboFiles.ps1 -BackupDir "<the folder Protect printed>"`. It restores the previous `Turbo.dll` too.
