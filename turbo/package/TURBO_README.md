# FC 27 LE Turbo 0.2.3

Turbo adds FC 26 Live Editor features to **FC 27 Live Editor** (public build v27.1.0 or newer). Version 0.2.3 has an in-game window, the **Turbo GUI**, with player, team, manager and database editors and buttons for every Turbo tool. Turbo runs next to Live Editor, inside the same game session. Live Editor's own files are not modified.

Offline Career Mode / Kick-Off only. Never use Live Editor or Turbo in online modes.

## Install

1. Install the official FC 27 Live Editor as usual.
2. Copy everything in this package into the Live Editor folder (the folder with `FCLiveEditor.DLL`). Nothing of Live Editor is overwritten. You add:
   - `turbo\Turbo.dll`, `turbo\TurboProbe.exe`, `turbo\TurboInjector.exe` (the Turbo GUI)
   - `turbo_config.json`, `TURBO_README.md`, `turbo_output\`
   - `lua\autorun\turbo_boot.lua`
   - `lua\scripts\turbo_*.lua` (25 scripts)
   - `lua\libs\v2\imports\turbo\` (the Turbo library and the GUI bridge)

## Open the Turbo GUI

Nothing to run:

1. Start the game through Live Editor as usual.
2. When the main menu appears, wait about 20 seconds, then press **F8** to show or hide the Turbo window. The key can be
   changed on the Status tab.
3. Load or start a career: the window's top line changes to **Connected** with the player and team counts.

At the main menu the window opens but says "Not connected" (Live Editor gives Turbo's Lua side no events outside a career). To
connect there, open Live Editor's **Lua Engine** and run `lua\scripts\turbo_gui_load.lua`; a message box confirms it.

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
| Players | Search by name, ID or club, "My club", filters: position (any of the 7 preferred positions), PlayStyle / PlayStyle+, retiring, minimum OVR / POT, maximum age; sort by any column. Edit: Profile (overall, potential, foot, skill moves, height, weight, ...), positions 1-7, birth date and join date as real dates, Attributes (grouped like the game), PlayStyles / PlayStyles+ / Traits / Traits+ as tick boxes with All/None, Appearance, kit number and line-up slot per team, All fields. Career buttons: Transfer, Loan, Release, Terminate loan, Transfer list, Loan list, Remove from lists, Delete player (with confirmation). |
| Teams | Team list, overview fields (ratings, prestige, budget, ...), squad with editable kit numbers and slots (click a player to open him), All fields. |
| Managers | Manager list and every field of the `manager` table. |
| Database | Any table of the live database: filter by field = value, double-click a cell to edit it. |
| Turbo Tools | Bulk edit players (your squad, team IDs, exactly the players the Players list shows, or everyone): any players-table field, fitness / form / morale, development (XP multiplier, bonus XP, no decline). Every Turbo 0.1 feature as a button: form/morale/fitness (now or every day), squad role, contract extensions, Player Career PlayStyles (now or automatic), season stats / fixtures / transfer history / any table to CSV, transfer bans, delete generated players, real-face head models, probe report, dry run. |
| Status | Connection details, show/hide key, log. |

How it works:
- When the GUI loads it runs `turbo\TurboProbe.exe` (a separate, windowless program) to find the Direct3D functions it draws with, so it creates nothing inside the game. It uses those addresses only after checking the game has loaded the very same Windows files. If that check fails it falls back to a short probe inside the game; `turbo_gui.log` says which.
- F8 and the mouse work whether the game delivers normal window messages or only raw input. While Turbo is hidden it leaves the game's mouse cursor alone.
- The editors read and write the game's database directly. Every value is checked against the field's own range before it is written; text that is too long is refused. Edits are live immediately, like in Live Editor.
- The Turbo Tools buttons and the player career buttons run through Live Editor's Lua engine. In a career they usually run within a second. **Outside a career** Live Editor sends Lua no events, so after clicking a tool open the Lua Engine and run `turbo_exec.lua`. A queued command can be cancelled.
- When you load another save the GUI re-reads the database on its own. While the window is hidden it waits until you open it again.
- GUI settings (auto features, dry run, show/hide key) are saved in `turbo_output\gui_settings.json` and take priority over `turbo_config.json`.
- Birth dates are shown as the real calendar date. (Live Editor's own Lua date function shows the next day for many dates; Turbo does not use it.)

## Scripts (still available)

Every feature is also a script in `lua\scripts` named `turbo_<feature>.lua`, run from the Lua Engine, with its settings in `turbo_config.json`. Results show in a message box and in `Logs\live_editor_<date>.log` (lines start with `[Turbo]`). Files go to `turbo_output\`. `"dry_run": true` makes every tool report what it would change and write nothing.

| Script | What it does | Settings (`turbo_config.json`) |
| --- | --- | --- |
| `turbo_gui_load` | Connects the Turbo GUI to the database now (needed only at the main menu), loads it if it is not loaded yet, and says why if it cannot | `gui.autoload` (true = loaded at launch, connects on the first career event) |
| `turbo_exec` | Runs the command the GUI queued (needed outside a career) | none |
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
| `turbo_player_moves` | Transfer, loan, release, terminate loan, transfer-list, loan-list, unlist | `modules.player_moves.actions` |
| `turbo_db_edit` | Edit rows of any database table matching conditions | `modules.db_edit.edits` |
| `turbo_export_table` | Dumps tables to CSV with every field's allowed range | `tables`, `max_rows` |

### Safety rules built in

- Every value is checked against the database field's own range before anything is written. One bad value stops a script run with nothing changed.
- The GUI checks that a table is still where it was before every write; after a save is loaded it re-reads the database instead of writing to old memory.
- Destructive actions need an explicit flag or a confirmation (`confirm`, `confirm_all`, `allow_all_rows`, the GUI's Delete dialog).
- Fixtures, transfer history and the squad-role list are read from game memory. FC 27 may have moved them since FC 26, so Turbo searches for them and only uses memory whose contents look right. If nothing matches, the feature stops and says so.

## In-game test checklist

Back up your career save first.

1. Start through Live Editor. At the main menu wait about 20 seconds and press F8: the Turbo window opens (Not connected). Load a career: the top line says Connected with player and team counts. If not, see the Status tab and `turbo_output\turbo_gui.log`.
2. Players: search your best player, change Acceleration, close and reopen the game's player screen: the value changed. Try 200: it is refused with a message.
3. Players > Profile: the birth date and age match the game's player profile.
4. Teams > your club > Squad: change a kit number; the game's squad screen shows it.
5. Database: open `teams`, double-click your club's `transferbudget`, change it; the game shows the new budget.
6. Turbo Tools > Your squad > Apply now (form 100): a green message appears within a second; squad form shows Excellent.
7. Tick "Keep every day (auto)", sim a day: form stays at the set value.
8. Exports: Season stats: the CSV appears in `turbo_output`.
9. Save, reload the save: the changes are still there; the GUI reconnects on its own.

Send `turbo_output\turbo_boot.log`, `turbo_output\turbo_gui.log`, `turbo_output\turbo_probe_*.txt` and the day's `Logs\live_editor_*.log` with any report.

## Not included yet

Match setup overrides, gameplay toggles (CPU vs CPU, unlimited subs, never tired, match time/score), manager market/job security/fire, endless career, reveal player data, negotiation bypasses, match-fixing and job offers. These need code hooks inside FC27.exe and in-game analysis first.

## Notes

- FC 27 Live Editor's bundled `auto_max_user_team_*.lua` and `auto_max_vpro_fitness.lua` compare events against the old `ENUM_CM_EVENT_MSG_*` names, which FC 27 LE's Lua files no longer define. Turbo's automatic features resolve both names.
- Match sharpness no longer exists in FC 27, so the FC 26 sharpness scripts are folded into `form_morale` without it.
- The Turbo GUI is a separate overlay drawn on top of the game, next to Live Editor's own window. It does not change Live Editor.
