# Turbo X — owner's test card (20 minutes on LEICESTER)

Rule for this whole card: a step is **done** only when you saw it on your screen. "Compiles" means nothing. Everything in the Turbo X package was compiled with the project's own build script but **has not run in the game** — you are the first run.

## 0. Before you install (5 minutes, game closed)

1. Close FC 27 and Live Editor.
2. Run `tools\turbo-x\Protect-TurboFiles.ps1` (right-click → Run with PowerShell). It copies your EA settings folder, your Turbo presets and the current `Turbo.dll` into `C:\FC 27 Live Editor\turbo_backups\protect_<timestamp>\` and writes a MANIFEST with MD5s. **If it refuses or errors, stop here and send me the window text.**
3. Note the backup folder path. Undo for everything below is `tools\turbo-x\Restore-TurboFiles.ps1 -BackupDir "<that path>"`.
4. Copy `Turbo.dll` from the Turbo X zip over `C:\FC 27 Live Editor\turbo\Turbo.dll`. Run `tools\turbo-x\Record-BuildIdentity.ps1 -Commit <hash from the zip's BUILD.txt> -Source "turbo-x zip" -PackagedDll "<zip folder>\Turbo.dll" -Note "owner test"`; it must print **MATCH**. If it prints MISMATCH, stop and send me the line.

## 1. Boot (2 minutes)

Start the game the usual way (Desktop LaunchFC27, Live Editor running). Load **LEICESTER only**. Open Turbo (Numpad +). Footer must read `Turbo X`. If the overlay does not appear, or the game exits at the main menu: close the game, run Restore, send me `turbo_output\turbo_gui.log`.

## 2. Saved sizes (step 3) — 3 minutes

- Drag the main window to a clearly different size and position. Drag one list divider (the player list) somewhere obvious. Widen one table column.
- Close the overlay (hotkey), reopen it: all three must be where you left them.
- Quit the game completely, start it again, load LEICESTER, open Turbo: all three must still be where you left them.
- Also check your tactic presets list still shows your presets.
- Result to send: **"sizes: window Y/N, divider Y/N, column Y/N, presets Y/N"**.

## 3. Audio preview (step 4) — 2 minutes

- Players → any player → Callname tab. Press the **Preview** (▶) button on a row.
- Result: **"audio: heard it"** or **"audio: silent"** (and whether the button changed to Stop). Do the same once on the Club commentary card: **"club audio: heard / silent / no button"**.

## 4. Menus (step 6) — 3 minutes

- Count the tab rows on the player screen. There must be **one**. There must be no "Everything (classic)" tab, no "Advanced" tile or switch anywhere, and "Callname" must appear once.
- Open every top-level area once (Home, Squad, Looks, Club, Tactics, Game Settings, Career, Settings). Note any page that used to exist and that you actually used which is now missing.
- Result: **"menus: rows=1 Y/N, classic gone Y/N, advanced gone Y/N, missing pages: <list or none>"** plus one screenshot per area if you have time.

## 5. One picker (step 7) — 5 minutes

- In the **Looks** area, for one player: open the hair picker and the beard picker (Hair and beard card, Choose...), the Accessory 1 picker (Accessories card) and one tattoo picker (Tattoos card). All four must look the same (same tile layout, same header). The old classic "Everything" tab that also had these is gone (step 6), so Looks is the route.
- Each must show a line like **"N without picture hidden — show"**. No tile may say "3D only", "Preview only", "Hidden", "no table row", "drawn".
- In each: pick a tile → the player changes → press Undo → the player is back. Do this once per picker.
- Result: **"picker: same layout 4/4, hidden count shown 4/4, apply+undo hair Y/N beard Y/N accessory Y/N tattoo Y/N"** and one screenshot of any picker.

## 6. Sliders (step 8) — 3 minutes (needs the game at the career hub, not in a match)

- Game Settings page. There must be **sliders** on the verified rows, numbers with "read-only" on the rest, no yellow paragraph.
- Move one verified slider (e.g. a CPU slider) and release it. Any message under the table must be one line.
- In the game: Customise → Game Settings → Game Type Settings. The slider you moved must show the new value.
- Result: **"sliders: present Y/N, EA screen shows value Y/N, message: <text>"**.

## 7. Copy (step 9) — while doing the above

Any table cell with more than three words, any help paragraph longer than one line, any sentence like "not seen on the EA screen": screenshot it. Result: **"copy: clean / <n> screenshots"**.

## 8. Finish

Send the seven result lines in one message. Then decide: keep Turbo X installed, or run Restore to go back to your previous DLL. Either way, nothing of yours has been modified except `Turbo.dll`, and the backup from step 0 restores even that.
