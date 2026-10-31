# Turbo X 1.0 — release notes (2026-10-31)

**Package**: `dist/FC27_LE_Turbo_X_1.0.zip` (126 files, sha256 `befd4d19…8700`), `Turbo.dll` md5 `f2635cb9408a06fdce0db3d7ad9c37d5`, built by `turbogui/scripts/build_win.sh` (exit 0, 0 warnings, MinGW-w64 GCC 13).

**What is proven in game and what is not** (LEICESTER throwaway career, driver runs 3 and 4, Undo afterwards, saves byte-identical):
- SEEN in game (rc2/rc3): 129/129 hooks, footer "Turbo X 1.0", new theme, rail and page strip, every area opens, the one picker for hair and beard, a hair apply and its way back, the callname Preview path fires.
- BUILT, not yet seen in game (rc5 → 1.0): the redesigned Players page (people rows, chips, hero header with OVR/POT/Calc tiles, segmented Profile/Attributes/Looks/Callname), Attributes as cards, Appearance Editor portrait card and gear-first order, FC Editor body pictures, sliders-first Game settings, the English callname coverage fix, the club Preview count line. The final run on LEICESTER before the install checks these.

**Callnames (English)**: Turbo X lists every name FC 27's English commentary has a recording for: 7,453 players with their own recording (both commentary teams, dr and gm, full/demo/licensed sets) and 4,861 generic surnames, 12,314 in all. The owner's 16,694 (`uk_masterxxx.xlsm`) is the FC 26 bank: 5,306 of its players have no recording in FC 27. Details in `BUILD_LOG.md`.

**Clubs**: 791 clubs have a recording in the English commentary. The Preview needs each club's clip decoded once into `turbo_output\cache\teamaudio\eng_us`; the page now says how many are ready instead of a misleading "No FC 27 audio".

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
