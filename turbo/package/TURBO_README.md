# FC 27 LE Turbo 0.3.0

Turbo adds FC 26 Live Editor features to **FC 27 Live Editor** (public build v27.1.0 or newer). Version 0.3.0 has an in-game window, the **Turbo GUI**, with player, team, manager and database editors and buttons for every Turbo tool. Turbo runs next to Live Editor, inside the same game session. Live Editor's own files are not modified.

Offline Career Mode / Kick-Off only. Never use Live Editor or Turbo in online modes.

## Install

1. Install the official FC 27 Live Editor as usual.
2. Copy everything in this package into the Live Editor folder (the folder with `FCLiveEditor.DLL`). Nothing of Live Editor is overwritten. You add:
   - `turbo\Turbo.dll`, `turbo\TurboProbe.exe`, `turbo\TurboInjector.exe` (the Turbo GUI)
   - `turbo_config.json`, `TURBO_README.md`, `turbo_output\`
   - `lua\autorun\turbo_boot.lua`
   - `lua\scripts\turbo_*.lua` (29 scripts, including `turbo_selftest.lua` and `turbo_images.lua`)
   - `lua\libs\v2\imports\turbo\` (the Turbo library and the GUI bridge)

## Open the Turbo GUI

Nothing to run:

1. Start the game through Live Editor as usual.
2. When the main menu appears, wait about 20 seconds, then press **F8** to show or hide the Turbo window. The key can be
   changed on the Status tab.
3. Load or start a career: the window's top line changes to **Connected** with the player and team counts.

At the main menu the window opens but says "Not connected" (Live Editor gives Turbo's Lua side no events outside a career). To
connect there, open Live Editor's **Lua Engine** and run `lua\scripts\turbo_gui_load.lua`: the message box says
"Game database shared with the Turbo GUI", or exactly why not. If the database cannot be read, the reason is also shown in
the Turbo window's top line.

What happens at launch: `lua\autorun\turbo_boot.lua` reads Turbo's settings, registers Live Editor's documented career event and
loads `turbo\Turbo.dll`. It calls no game function and reads no game memory. `Turbo.dll` then does nothing at all (no window
search, no Direct3D) until Live Editor writes "Initial setup done" in its own log for this game session
(`Logs\live_editor_<date>.log`, about 15 seconds after the main menu appears, once Live Editor's own window is ready), or until
Turbo's Lua side runs in game. Only then does it wait for the game window, let it settle five seconds and hook Direct3D.

To load nothing at launch, set `"gui": {"autoload": false}` in `turbo_config.json`; then run `turbo_gui_load.lua` in game to
load the GUI.

If F8 does nothing:
- `turbo_output\turbo_gui.log` says what the GUI did (waiting for Live Editor, the game window, hooks, overlay, database).
  No such file: `Turbo.dll` was not loaded; `turbo_output\turbo_boot.log` and Live Editor's log (lines with `[Turbo]`) say why.
- If Live Editor's log level is set above INFO it does not write "Initial setup done"; the GUI then starts when you enter a
  career or run `turbo_gui_load.lua`.
- If `turbo_gui_load.lua` says `package.loadlib` is missing, run `turbo\TurboInjector.exe` (as administrator) while the game is
  running through Live Editor. It loads Turbo only into an FC27.exe that already has Live Editor in it.

## If something goes wrong (game will not launch, crashes, hangs)

Nothing here modifies Live Editor or the game, so everything can be switched off by deleting files:

| Problem | What to do |
| --- | --- |
| Game will not launch or hangs at start | Delete `lua\autorun\turbo_boot.lua` (or set `"gui": {"autoload": false}`). That is the only thing Turbo runs at launch. Then send `turbo_output\turbo_boot.log` and `turbo_output\turbo_gui.log`: their last lines say what Turbo was doing. |
| Game crashes after loading the GUI | Create an empty file `turbo_output\turbo_gui_disable.txt` (kill switch: `Turbo.dll` never starts), or delete the `turbo` folder. Send `turbo_output\turbo_gui.log`. |
| "previous Turbo GUI start did not finish" in `turbo_gui.log` | The game died while the GUI was starting, so Turbo keeps the overlay off. Delete `turbo_output\turbo_gui_start.flag` to try again. |
| Remove Turbo completely | Delete `turbo\`, `turbo_config.json`, `TURBO_README.md`, `turbo_output\`, `lua\autorun\turbo_boot.lua`, `lua\scripts\turbo_*.lua` and `lua\libs\v2\imports\turbo\`. |

## The Turbo GUI

| Tab | What you can do |
| --- | --- |
| Players | Search by name, ID or club, "My club", filters: position (any of the 7 preferred positions), PlayStyle / PlayStyle+, retiring, minimum OVR / POT, maximum age; sort by any column. Edit: Profile (overall, potential, foot, skill moves, height, weight, ... as combos with readable labels, roles and body, **name ids and the spoken commentary name**), positions 1-7, birth date and join date as real dates, Attributes (grouped like the game, **sliders** with group averages), **Undo** of the last 20 edits per player, a field search box, PlayStyles / PlayStyles+ / Traits / Traits+ as tick boxes with All/None, **Appearance** (real-face picker, tattoo picker, every appearance field), **Miniface**, kit number and line-up slot per team, All fields. Career buttons: Transfer, Loan, Release, Terminate loan, Delete player (with confirmation), done by Turbo in the career database because FC 27 LE v27.1.2 has no native for them: **never for your own club** (moves into or out of it, and deleting your players, are refused: such database-only moves crashed a test career in FC 27; use the game's transfer screens for your club). Between other clubs they are not verified in game yet: back up your save first; squad screens show a move after saving and loading the career. Transfer list / Loan list / Remove from lists need Live Editor natives v27.1.2 does not have. |
| Teams | Team list, overview fields (ratings, prestige, budget, ...), squad with editable kit numbers and slots (click a player to open him), All fields. |
| Managers | Manager list, every field of the `manager` table, and the manager's miniface. |
| Competitions | League tables (`leagueteamlinks`): won, drawn, lost, goals for / against, points, table position per club, "Recalculate points" (3 per win, 1 per draw), "Write table positions". See the note below: FC 27's own Standings screen does not read these numbers yet. |
| Database | Any table of the live database: filter by field = value, double-click a cell to edit it. |
| Turbo Tools | Bulk edit players (your squad, team IDs, exactly the players the Players list shows, or everyone): any players-table field, fitness / form / morale, development (XP multiplier, bonus XP, no decline). Your club's transfer budget (read, set, add). Every Turbo 0.1 feature as a button: form/morale/fitness (now or every day), squad role, contract extensions, Player Career PlayStyles (now or automatic), season stats / fixtures / transfer history / any table to CSV, transfer bans, delete generated players, real-face head models, probe report, dry run. |
| Status | Game images (cache, waiting images, empty the cache), connection details, show/hide key, log. |

### Minifaces, real faces and tattoos (new in 0.3.0)

- **Players > Miniface** shows the player's miniface: the game's own, or your custom file in Live Editor's `mods\legacy` folder. Make a new one **from an image file** (PNG, JPG, BMP, TGA, DDS; put pictures in the `turbo_minifaces` folder or browse to them), **from another player's miniface**, **from his head model's miniface** or **from his youth face**. Frame it with Size / Left-right / Up-down (or drag the picture, mouse wheel to zoom), optionally "Remove plain background", then **Save as miniface**. Turbo writes a 256x256 DXT5 DDS to `mods\legacy\data\ui\imgAssets\heads\p<playerid>.dds`. The game shows it the next time the screen is drawn (seen in FC 27: no restart needed).
- **Managers > Miniface**: the same tools for `mods\legacy\data\ui\imgAssets\heads_staff\heads_staff_<headassetid>.dds` (512x512). Every manager using that head asset shows it.
- **Remove custom miniface** brings the game's own back. Any file Turbo replaces or removes is copied to `turbo_output\miniface_backups` first, including minifaces you made with other tools.
- **Players > Appearance > Choose a real face...**: a grid of every real-face head model (players with head class 0) with their minifaces and a search box. Click one to give the player that head (head asset ID, head class, real-face flag, head type, variation). Options: also hair, beard, eyes and skin; also his miniface.
- **Players > Appearance > Tattoos**: per body area, the current tattoo with its preview and **Choose...**: a grid of the tattoos made for that area (FC 27's `tattoo` table) with the game's own previews.

### Spoken names (commentary callnames)

The commentators call a player by the callname bound to his **common name id when it is set, else his last name id** (`playernames.commentaryid`, overridden per player by `playernamemap`); some real players also have their own name recorded under their player id ("real bank"). Which ids actually have audio depends on the **commentary language the game has loaded**: every language pack was recorded separately.

- **Players > Profile > Name and commentary** shows the four name ids with their text, the commentary language packs Turbo found in the game folder (`commentaryfull_<lang>.toc`: pick the one the game plays, Settings > Audio), and whether the player is spoken: "Spoken: ..." (green), "Not spoken in <lang>" or "Known to the database" (orange = no list for that language, so not verified).
- **Choose a spoken name...** lists every name id and every real-bank player that triggers a callname in the active language (type at least 2 letters). Clicking a name writes it to the binding field (common name when set, else last name; or force one); clicking a player copies his name ids. Undo puts them back.
- Lists of spoken ids per language live in `turbo_output\callnames\<lang>*.csv`, one or more files per language code (`ita_it`, `eng_us`, `por_br`, ...). They are header-driven: a `commentaryid` column lists generic-bank ids with audio, a `playerid` / `donor_playerid` column lists real-bank players, `kind,id` works too; comma, semicolon or tab separated, `#` comments, UTF-8 BOM accepted. Bank exports in the PT-BR callname project's format (`donors_generic.csv`, `donors_real.csv`) can be copied there unchanged (for example as `por_br.generic.csv` and `por_br.real.csv`). Without a list for the loaded language, Turbo shows what the database's `commentarynames` table knows and says it is unverified.
- `gui_settings.json`: `commentary.language` (the pack to use), `commentary.game_dir` (the game folder, normally found from the running FC27.exe). The Status tab lists the packs and lists found.
- The pictures come from the game's files through Live Editor's `LegacyFileExport`. Live Editor runs Turbo's Lua side only on career-mode events, so images arrive a few at a time while you play (advancing the calendar, opening screens). To load the waiting ones at once: hide Turbo (F8), open Live Editor's Lua Engine, run `lua\scripts\turbo_images.lua` (up to 20 seconds per run). They are kept in `turbo_output\cache\legacy`.

How it works:
- When the GUI loads it runs `turbo\TurboProbe.exe` (a separate, windowless program) to find the Direct3D functions it draws with, so it creates nothing inside the game. It uses those addresses only after checking the game has loaded the very same Windows files. If that check fails it falls back to a short probe inside the game; `turbo_gui.log` says which.
- F8 and the mouse work whether the game delivers normal window messages or only raw input. While Turbo is hidden it leaves the game's mouse cursor alone.
- The editors read and write the game's database directly. Every value is checked against the field's own range before it is written; text that is too long is refused. Edits are live immediately, like in Live Editor.
- The Turbo Tools buttons and the player career buttons run through Live Editor's Lua engine, on the next career-mode event (advancing the calendar, many screen changes). In menus where nothing happens they wait: run `turbo_exec.lua` in the Lua Engine to run the queued command at once. Outside a career this is always needed. A queued command can be cancelled.
- While the Turbo window is open it takes the keyboard and mouse, so Live Editor's own window (Left Alt) does not react. Press F8 to hide Turbo first.
- When you load another save the GUI re-reads the database on its own. While the window is hidden it waits until you open it again.
- GUI settings (auto features, dry run, show/hide key) are saved in `turbo_output\gui_settings.json` and take priority over `turbo_config.json`.
- Birth dates are shown as the real calendar date. (Live Editor's own Lua date function shows the next day for many dates; Turbo does not use it.)

## Scripts (still available)

Every feature is also a script in `lua\scripts` named `turbo_<feature>.lua`, run from the Lua Engine, with its settings in `turbo_config.json`. Results show in a message box and in `Logs\live_editor_<date>.log` (lines start with `[Turbo]`). Files go to `turbo_output\`. `"dry_run": true` makes every tool report what it would change and write nothing.

| Script | What it does | Settings (`turbo_config.json`) |
| --- | --- | --- |
| `turbo_gui_load` | Connects the Turbo GUI to the database now (needed only at the main menu), loads it if it is not loaded yet, and says why if it cannot | `gui.autoload` (true = loaded at launch, connects on the first career event) |
| `turbo_exec` | Runs the command the GUI queued (needed outside a career) and loads 5 seconds of waiting game images | none |
| `turbo_images` | Loads the game images the Turbo window is waiting for (minifaces, tattoo previews), up to 20 seconds per run | none |
| `turbo_probe` | Read-only report: LE version, available functions, DB tables and field ranges, memory checks | none |
| `turbo_enable_auto` | Switches automatic features on/off from the config | `auto.*.enabled` |
| `turbo_form_morale_apply` | Sets form / morale / fitness of your squad now. Automatic: every in-game day, before matches, after loading | `auto.form_morale` (form 0-100, morale 0-100, fitness 5-95, 0 = leave alone) |
| `turbo_pap_playstyles_apply` | Player Career: every playstyle for your player. Automatic: re-applied after the game resets them | `auto.pap_playstyles` (`"max"` or bitmask) |
| `turbo_custom_headassets` | Head model per player | `modules.custom_headassets.map` `{"playerid": headassetid}` |
| `turbo_custom_tattoos` | Tattoo per player | `modules.custom_tattoos.map`, `field` |
| `turbo_delete_generated_players` | Lists generated players; deletes them with `"confirm": true` | `min_playerid` (460000), `confirm` |
| `turbo_export_season_stats` | Season stats CSV | `only_user_team` |
| `turbo_export_fixtures` | Fixtures and results CSV | none |
| `turbo_export_transfer_history` | Season transfers and loans CSV with fees | none |
| `turbo_extend_cpu_contracts` | Adds N years to every contract outside your club | `years` (5) |
| `turbo_extend_user_contracts` | Your squad contracted until today + N years | `years` (4) |
| `turbo_headmodels_capture` / `_apply` | Builds the FC 27 real-face list from an unmodified database, then applies it | `list_file`, `generic_for_others`, `min_list_size` |
| `turbo_transfer_bans_list` / `_ban_all` / `_unban_all` | Transfer bans | `ban_until` (YYYYMMDD), `exclude_user_team` |
| `turbo_squad_role` | Squad role for your players (1 Crucial ... 5 Prospect) | `role`, `include_loaned_in`, `use_memory` |
| `turbo_team_jersey_numbers` | Kit numbers of a team, duplicates flagged | `teamid` (0 = your club) |
| `turbo_bulk_edit` | Select players (your squad / teams / IDs / all + filters) and set fields, fitness, form, morale, development | `modules.bulk_edit` |
| `turbo_player_moves` | Transfer, loan, release, terminate loan (done by Turbo in the database when Live Editor lacks the natives), transfer-list, loan-list, unlist | `modules.player_moves.actions` |
| `turbo_db_edit` | Edit rows of any database table matching conditions | `modules.db_edit.edits` |
| `turbo_export_table` | Dumps tables to CSV with every field's allowed range | `tables`, `max_rows` |
| `turbo_export_player` | Exports players to Live Editor preset CSV (`extensions\player_presets`, readable by Live Editor's own Import from preset) and Turbo player JSON + miniface (`turbo_output\players`) | `modules.player_presets` (`playerids`, `csv`, `json`, `miniface`, `preset_dir`) |
| `turbo_import_player` | Imports a preset file (Live Editor CSV, FC 26 files too, or Turbo JSON) onto an existing player; pick the groups | `modules.player_presets` (`file`, `playerid`, `groups`, `row`) |
| `turbo_create_player` | Creates a new player: a copy of a player, from a preset file, or blank, in a club (Free Agents by default); refuses your own club unless `allow_user_club` | `modules.create_player` (`source`, `teamid`, `jersey`, `names`, `set`, `max_playerid`) |

### Safety rules built in

- Every value is checked against the database field's own range before anything is written. One bad value stops a script run with nothing changed.
- The GUI checks that a table is still where it was before every write; after a save is loaded it re-reads the database instead of writing to old memory.
- Destructive actions need an explicit flag or a confirmation (`confirm`, `confirm_all`, `allow_all_rows`, the GUI's Delete dialog).
- Game memory is read only where Turbo.dll's readable-memory map says it is readable (Live Editor's own memory reads crash the game on a bad address). Without the Turbo GUI running, the memory-based tools stop with a message instead of reading.
- Tools that need a Live Editor native this Live Editor build does not have are greyed out in the Turbo window, with the reason when you hover them (FC 27 LE v27.1.2: transfer / loan lists, transfer bans, player development, live season stats). They work again once Live Editor adds the natives.
- Without those natives Turbo does these itself: the **transfer budget** is read and written in the career's memory (your club's finance entry; both copies the game keeps); **transfers, loans, release, terminate loan and player deletion** are written into the career's tables (`teamplayerlinks`, `players`, `playerloans` through Live Editor's `InsertDBTableRow` / `DeleteDBTableRowByAddr`, your team sheet, set-piece takers). Seen in FC 27: a transferred player and a changed budget are still there after saving and reloading the career. A move shows on the game's squad screens after the career is saved and loaded again; the budget shows at once.
- Fixtures, transfer history and the squad-role list are read from game memory. FC 27 may have moved them since FC 26, so Turbo searches for them and only uses memory whose contents look right. If nothing matches, the feature stops and says so.

## In-game test checklist

Back up your career save first.

1. Start through Live Editor. At the main menu wait about 20 seconds and press F8: the Turbo window opens (Not connected). Load a career: the top line says Connected with player and team counts. If not, see the Status tab and `turbo_output\turbo_gui.log`.
2. Players: search your best player, change Acceleration, close and reopen the game's player screen: the value changed. Try 200: it is refused with a message.
3. Players > Profile: the birth date and age match the game's player profile.
4. Teams > your club > Squad: change a kit number; the game's squad screen shows it.
5. Turbo Tools > Your club: transfer budget: Add 10,000,000. Office > Finances shows the new budget.
5a. Players > a player of your club > Miniface > From an image file: pick a picture, Save as miniface. The game's Team Management shows the new face. Remove custom miniface brings the old one back.
5b. Players > Appearance > Choose a real face: pick a face (load images with `turbo_images.lua` if the grid shows "..."). The player's miniface changes in the Squad Hub.
5c. Players > Appearance > Tattoos > Choose...: pick one; the field shows the new ID and its preview.
6. Turbo Tools > Your squad > Apply now (form 100): a green message appears within a second; squad form shows Excellent.
7. Tick "Keep every day (auto)", sim a day: form stays at the set value.
8. Exports: Transfer history and Fixtures & results: CSVs appear in `turbo_output` (FC 27: completed transfers and loans of the season; your club's remaining fixtures). Season stats without Live Editor's `GetPlayersStats` exports the database's league numbers, labelled as not live.
8a. Run `lua\scripts\turbo_selftest.lua` in Live Editor's Lua Engine with a career loaded: it runs every tool once (dry runs, plus a reversible check where possible) and writes `turbo_output\turbo_selftest.log`. Expected with FC 27 LE v27.1.2: 0 failed; SKIP lines name the missing natives.
9. Optional, only on a test save: save, reload the save: the changes are still there; the GUI reconnects on its own.

Send `turbo_output\turbo_boot.log`, `turbo_output\turbo_gui.log`, `turbo_output\turbo_probe_*.txt` and the day's `Logs\live_editor_*.log` with any report.

## Not included yet

- **League tables in the game's Standings screen.** The Competitions tab edits the career's `leagueteamlinks` table and the values survive saving, but FC 27's Standings screen reads the live table from the competition engine's memory, which Turbo has not located yet. **Match results** editing, **job offers** and **minifaces generated from the 3D head** (FC 27 has its own portrait capture for youth players) are in progress.
- Match setup overrides, gameplay toggles (CPU vs CPU, unlimited subs, never tired, match time/score), manager market/job security/fire, endless career, reveal player data, negotiation bypasses and match-fixing need code hooks inside FC27.exe and in-game analysis first.

## Notes

- FC 27 Live Editor's bundled `auto_max_user_team_*.lua` and `auto_max_vpro_fitness.lua` compare events against the old `ENUM_CM_EVENT_MSG_*` names, which FC 27 LE's Lua files no longer define. Turbo's automatic features resolve both names.
- Match sharpness no longer exists in FC 27, so the FC 26 sharpness scripts are folded into `form_morale` without it.
- The Turbo GUI is a separate overlay drawn on top of the game, next to Live Editor's own window. It does not change Live Editor.
