# Turbo X 1.0 — release notes (2026-11-01, build final12)

**Package**: `dist/FC27_LE_Turbo_X_1.0.zip` (126 files, sha256 `a3ca80debf9aad8d36a2bf6faad3284e0d4ef48705cf584c6068a2250ed87d9b`),
`Turbo.dll` md5 `3caad80fbe316c2074730c04a2be8cd6`, cross-built by `turbogui/scripts/build_win.sh` (MinGW-w64, exit 0).
Installed on the owner's PC 2026-11-01 00:32 local: INSTALL PASS, 120 files, 80,374 user files unchanged.
Undo: `turbo_backups\install_20261101_003213\Undo-Turbo.cmd`.

"SEEN" = seen working in game (throwaway LEICESTER career, driver runs, Undo afterwards). "BUILT" = compiled and installed,
not yet seen in game. The day-by-day record is `BUILD_LOG.md`.

## What Turbo X 1.0 is

A redesign of the Turbo overlay for FC 27 Live Editor, built from the multi-LLM audit of 2026-10-31
(`docs/review-2026-10-31/CONSENSUS_ACTION_PLAN.md` v2): fewer menus (cut by deletion), one visual picker for every look item,
picture sets for the picker, every callname of the game in every commentary language, and live real-face hair/beard edits.

## Interface

- SEEN: new theme, left rail and page strip, footer "Turbo X 1.0", every area opens; one tab row on the player screen;
  Players page (people rows, hero header with OVR/POT/Calc tiles, Profile / Attributes / Looks / Callname); Appearance Editor
  list with minifaces (the ImGui "SetCursorPos" error box is gone, run13); Game settings with real sliders on verified rows.
- Cut on purpose: the classic "Everything" tab, the duplicate Callnames page, the Advanced ("Lab") area, the old-pages and
  4.0-preview switches (49 files, −3,562 lines). Settings and tactics pages: frozen as they are (owner, 2026-11-01).

## Visual pickers and pictures

- One picker (`ui_visual_picker.cpp`) for hair, beard, accessories, tattoos, body type and kit. SEEN for hair and beard.
- Picture sets made by the game itself (in-game capture of every item without a picture, run11): **531 pictures** — hair 138,
  beard 15, boots 177, accessories 163, body types 38 — in `turbo_output\pictures\assets`, read first by the picker.
  Not done: 126 tattoos (generator option built, not run); the 9 goalkeeper glove ids are outside the column range.
- Hairstyle tiles overlapping (final9 regression): fixed in final11. BUILT.

## Real faces

- Changing hair or beard on a real (scanned) face no longer leaves the old hair underneath: Turbo's player_kit template v5
  cleans the scan's painted hairline and stubble. SEEN (run13: Mbappé, Haaland, Pedri).
- Hair 0 = **Bald** and beard 0 = **No beard** on real faces too (codes 9998 / 510), with a separate "His scanned hair /
  beard" tile. BUILT.

## Shown OVR

- The fake ("Shown") overall now shows in game: Turbo writes it into `players.overallrating` at career load and on Set,
  re-pins it every 5 s, and puts the game's value back on Clear. Off switch: `turbo_output\ovr_pin_off.txt`. BUILT.
  The game also uses that overall for value and squad picks.

## Callnames and languages

- English: every name FC 27's commentary has a recording for — 7,453 players with their own recording + 4,861 generic
  surnames (12,314). Dutch, French, German, Italian, Portuguese (BR), Spanish: FC 27 lists rebuilt from the game's banks,
  0 missing wavs over 81,187 segments; demo-set names added (0 players without a Preview in the four demo languages).
- Club Preview clips: every club in every language's list has a clip — eng_us 791, fre_fr 736, spa_es 687, dut_nl 648,
  ger_de 661, ita_it 735, por_br 628. The decoder now keeps the 10 English club calls whose second audio stream carries sound.
- In game: English Preview path SEEN firing; other languages' Previews BUILT, not yet heard in game.

## Transfers

- Team > Transfers > Loans no longer lists players whose loan the game has already ended (stale `playerloans` rows: a row
  naming the player's own club as lender, or no loan in the game's runtime LoansManager list); the card re-checks the game's
  list every 2 s. BUILT (final12).

## Also in

- `.player` import and accent-agnostic search (tx-playerimport), 122 file-mapped game-settings sliders (tx-sliders),
  real-clock timers for logs (tx-perf), gear generator `real_face` / `body_mode` / tattoo options.

## Known open items

- Not yet seen in game: Bald / No beard, Shown OVR, tile layout fix, non-English Previews, the loans fix.
- Tattoo pictures (126) and sharper boots/accessories pictures (body_mode 1 test staged, not run).
- Old picker code still compiled in (`PICKER_DELETION_LIST.md`), native test suite not run against this head.
