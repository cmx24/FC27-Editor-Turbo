# Turbo X 1.0 — pre-release notes (2026-10-31)

**Status: compiled, not proven.** This build came out of the project's own `build_win.sh` with exit 0 and 0 warnings, cross-compiled on Linux with MinGW-w64 GCC 13.2. It has **not run in EA FC 27**. Nobody has seen the overlay draw. Install it only on the throwaway LEICESTER career, after `Protect-TurboFiles.ps1`, and follow `docs/turbo-x/TEST_CARD.md`.

Identity: commit `21c851a (source identical to 230d49a; 21c851a adds only docs)`, `Turbo.dll` md5 `e679e40aea0da72d20f2c6fe9ea8c106`, package `TurboX_21c851a_e679e40a.zip`. The owner's PC builds the same script under MSYS2 UCRT64 and will produce a different md5 for the same commit; `BUILD.txt` in the zip carries this one.

## What changed against 5.0c/integration @ 00b28622 (the reviewed head)

Everything below follows `docs/review-2026-10-31/CONSENSUS_ACTION_PLAN.md` v2, step by step. Steps 3, 4 and 5 are test-only or need the game and have no code here.

- **Step 1–2, protect files and build identity** (`tools/turbo-x/`): `Protect-TurboFiles.ps1` copies the EA settings folder, Turbo presets and the live `Turbo.dll` to `turbo_backups\protect_<timestamp>\` with an MD5 manifest; `Restore-TurboFiles.ps1` puts them back; `Record-BuildIdentity.ps1` prints MATCH/MISMATCH between the installed DLL and the packaged one and appends to `docs/review-2026-10-31/BUILD_IDENTITY.md`. Statically checked (`static_check.py` PASS ×3). **Never executed**: no Windows here.
- **Step 6, menus cut by deletion**: the "Everything (classic)" tab, the duplicate Players > Callnames page, the whole Advanced ("Lab") area and its pages, the "Use the old pages" and "Preview the 4.0 menus" switches, and the 4.0 tree. 49 files, −3,562 lines, six source files deleted. One tab row on the player screen; Callname appears once. What lost its only door is listed in `BUILD_LOG.md`.
- **Step 7, one picker route**: hair, beard, accessories, tattoos, body type and kit all open `ui_visual_picker.cpp`. The old `item_galleries`, `tattoo_editor` and `gallery_picker` tables are unreachable (marked, not yet deleted: deletion waits for in-game proof, per the plan and Copilot's dissent). Each picker shows "N without picture — show".
- **Step 8–9, sliders and copy**: Game Settings uses real `SliderInt` on verified rows, written through the existing live block write; "read-only" / "not mapped" on the rest; no staging paragraph; cells ≤ 3 words.
- **Version**: footer reads `Turbo X 1.0`.

## What is NOT in this build

- Step 5 (tactics study): the tactics pages still describe the `turbo_settings_apply.exe` flow. Untouched on purpose.
- Step 7 second half: the old picker code is still compiled in; `PICKER_DELETION_LIST.md` has the line ranges for after the proof.
- Native tests: being brought in line on `tx-tests`; they cannot run here either way.

## Undo

`tools\turbo-x\Restore-TurboFiles.ps1 -BackupDir "<the folder Protect printed>"`. It restores the previous `Turbo.dll` too.
