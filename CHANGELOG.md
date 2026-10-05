# Changelog

All notable changes to FC 27 LE Turbo. "Verified in game" means seen working in a test FC 27 Manager Career.
Feature-by-feature status: [`docs/fc26-parity.md`](docs/fc26-parity.md).

## 1.1.5 (face chooser by look, game editors fixes)

Released 2026-10-04. Checked in game (test career): Hair / Facial hair groups by look, career Edit Player Gear
without doubled pickers, editor files written before the game reads them.

### Changed

- Real-face chooser: the **Hair** and **Facial hair** filters group styles by look, from the game's own preview pictures of every style (1463 hair styles, 89 facial hair styles), 5 groups each. Hair: Bald & buzz cut, Short, Medium, Long, Tied, braids & dreads (instead of style code ranges). Facial hair: Clean-shaven, Stubble, Moustache & goatee, Short beard, Full beard (every style is now sorted; the "Any facial hair" bucket is gone). A code the game has no preview for takes the look of the nearest listed style. Each group keeps its count and a picture of one of its styles, and a head's tooltip still shows its exact style ("Short (style 2)").

### Fixed

- Game editors, "Unlock everything (experimental)": career Edit Player > Gear no longer shows doubled pickers. Main menu Create Player's KIT_SLEEVES / WAIST_FIT / GK_PANTS groups (their items are already in the screen's own KIT_FIT) and the Clubs editor's WRIST item (the screen has GLOVES_AND_WRIST's wrist pickers) are not copied any more: a gear group whose items the screen already has is never added, and no item name is ever added twice.
- Game editors, "Unlock everything (experimental)": **no more tattoo and arm sleeve pickers in career Edit Player**. Seen in game: put there (even shaped as groups like Create Player's), the Arm Sleeves tile closed the game. The Clubs editor is not read any more, and TATTOO / ARM_SLEEVES are on the deny-list, so no source can add them. Edit tattoos and arm sleeves in Turbo (Players > Appearance > Tattoos).
- Game editors: the unlocked files are written as soon as Turbo.dll loads, before Live Editor reads mods\legacy while the game starts (log: "Game editors: early pass: N written ..."); before, they were written after Live Editor's "Initial setup done" and only counted after a second game restart. File work only (no game call, no waiting), with every rule of the GUI's pass (manifest, only the 8 targets), and another tool's custom file is left alone. The GUI's pass still runs; when it has to write a file (a switch changed, a new original exported), the Game editors section and a toast say: "The game read its editor files before Turbo updated them: restart the game once to load the new ones."

## 1.1.4 (playtest round 2)

Released 2026-10-04. Checked in game: show/hide key control, Ctrl + wheel zoom, Gender filter and 5-group menus,
gear pictures, 4-field team names, Unlock everything (Edit Manager / Edit Player open without a crash or freeze),
Live standings second-click edit (the game's Standings screen shows the change).

### Changed

- Real-face chooser: every filter menu has at most 5 choices besides Any, each with its count and a picture: Ethnicity (European, Latin, African, Asian, Mixed & other), Skin tone (Very light .. Very dark), Hair colour and Facial hair colour (Black, Brown, Blonde, Red & ginger, Grey, white & other), Eyes (Blue, Green, Brown, Light brown & hazel, Other), Facial hair (Any facial hair, Clean-shaven, Stubble, Moustache & goatee, Beard) and Hair (style code families 0-999 .. 4000+, as no names are known for the styles). A head's tooltip still shows its exact value.
- Teams > Name has four boxes: **Display name** (what the game shows, "AC Milan"), **Long name** ("Associazione Calcio Milan"), **Short name** ("Milan", also cut for the 10-letter form) and **Abbreviation** ("ACM"), one Save. FC 27 has no long-name string (only the name and its 15 / 10 / 3-letter forms), so the long name is kept in Turbo's `team_names.json` and the form says the game does not show it; older files load as before.

### Added

- Game editors, "Unlock everything (experimental)": career Edit Player gets the game's own tattoo, arm sleeve, sock style, boot, glove and wristband pickers, copied with their ids from the game's own editor files: tattoos and arm sleeves from the Clubs editor (the only file that defines them; read, never written), the rest from main menu Create Player offline or Player Career's pro editor (Turbo asks the game for them). A gear item without sub-items (the Clubs editor's TATTOO and ARM_SLEEVES) is copied too. Store outfits, the head editor and the hidden attributes never come along.
- **Show/hide key in plain sight**: "Show/hide key: [F8] [Change...]" now sits in the Turbo window's top bar on every tab (and first thing in Turbo Tools, and in Status > Settings). Pick a key from the list (F1-F12, Insert, Home, End, Page Up/Down, Pause, Scroll Lock, numpad keys, the ` ~ key, mouse side buttons) or press Change... and press the key (Ctrl / Alt / Shift allowed; Esc cancels; Reset to F8). Mouse 4 / Mouse 5 (side buttons) can now be the show/hide key.
- Real-face chooser (players and managers): a **Gender** filter (Any / Male / Female, the `gender` field), shown first.
- Managers > Appearance: an **Outfit** gallery (outfitid) with the game's outfit pictures (outfit/item_<id>, genericManagerOutfits/gmo_<id>).

### Fixed

- **Live standings: the box a double-click opens takes your typing** (seen in game: digits and Enter went to the game,
  so nothing changed and Enter could press the game's selected button). Turbo only passed keys to itself once a text
  box was already active; while the cell box is open its keys now go to Turbo, never to the game.
- Game editors, "Unlock everything (experimental)": Edit Manager no longer closes the game and Edit Player no longer freezes. The experiments that could do it are gone: the head editor on real players and managers, the manager outfit picker, manager gender, every goal celebration, Composure / Defensive awareness. A shared deny-list stops the recipe and its validation from adding them, and the in-memory fallback now keeps exactly the same fields as the file override (manager gender included).
- A newly picked show/hide key that was still held down no longer hid Turbo right after it was chosen.
- Competitions > Live standings: the box a double-click opens on a number takes the keyboard. It asks for the keyboard on every frame until it has it (it asked once, while the double-click still held the cell), keys typed before that still count (digits, Backspace, Enter applies, Esc cancels), and only a click outside the box closes it. Live standings also stop calling a group "not shown by the game" (and warning about it) unless its clubs all sit in a group the game's view does show, such as a cup's setup pool. The view holds only the competitions the game has asked for so far, so the preseason Champions Trophy (group 1929) was flagged although the Standings screen showed it. Turbo still has no name for that group: it sits outside the competition tree and has no competition id.
- **Gear pictures show again**: boots come from FC 27's shoe/shoe_<id> pictures (every boot in use now has one, 47 of 183 before), hair and facial hair from craniumhair / craniumfacialhair (any letter case; 543 of 543 hair styles in use have a picture, 2 before), accessories without a _0 picture use the one the game lists, and boots with long variant names (item_1_0_0_0) are found. An item the game has no picture for shows "no picture in the game" at once instead of waiting forever, and the background loading asks only for pictures the game lists.

## 1.1.3 (hotfix)

Released 2026-10-04. Checked in game: Create job offer (one run, inbox email, Manager Market offer, contract
signed: "The job is yours"); the mouse wheel scrolls Turbo's lists.

### Fixed

- **Ctrl + mouse wheel zoom** also when Ctrl is let go right after the wheel (seen in game: a quick Ctrl + wheel
  scrolled the list): Ctrl is recorded with each wheel notch, not only when Turbo reads the notch a frame later.
- **Create job offer no longer crashes the game**: the game posts career-mode events while it makes the offer, and Turbo picked the same, still unanswered command up again from each of them (1.0.2-1.1.2), nesting until FC 27 died. Turbo's event handler, the Turbo window's commands and actions now never nest: the command runs once and the offer arrives as in 0.4.0.
- The mouse wheel scrolls Turbo's lists again and Ctrl + wheel zooms again: the wheel hook was skipped for the whole session after a single legacy mouse message, and a hook Windows removed was never put back; the wheel now comes from one live source (hook, else raw input, else window messages) and the hook is reinstalled when it goes quiet.

## 1.1.2 (live team names)

Released 2026-10-04. Checked in game: a saved name shows at once on the hub, the Standings screen and in-match
screens (the Office standings tile keeps its old text until it is rebuilt).

### Added

- **Live team names**: Teams > a club > Name, one form and one Save; the game shows the new name at once.

### Fixed

- Renaming a club a second time no longer keeps the old short name and 3-letter code: a short form or code made
  from the previous name now follows the new name.

## 1.1.1 (playtest fixes)

Released 2026-10-04. Checked in game: background pictures, club filter, moves for every club, real-face chooser,
competition picker, AC Milan crest without turbo_images.lua.

### Added

- **Pictures load by themselves in the background.** From the first F8 (or when a career connects) Turbo lists every
  picture its screens show (real-face minifaces, tattoo previews, hair / facial hair / boots / gloves / accessory
  previews, club crests, managers' real-face heads) and asks Turbo's Lua side for the missing ones after what is on
  screen. While Turbo is shown it sends its synthetic career event every 250 ms, and Lua exports for 0.08 s each
  time, so they arrive without `lua\scripts\turbo_images.lua` and without stalling the window. Files at hand are
  decoded on a worker thread (at most 128 MB in memory); the grids then only upload them (16 per frame). The top bar
  shows "Loading pictures: N of M". Exported files stay in `turbo_output\cache\legacy`, so the next session only
  decodes them. The callname language and spoken set are read at the same time. The picture hints (real faces,
  tattoos, items, crest editor) no longer send you to `turbo_images.lua`. (ui/preload.h, LegacyImages background
  list, TextureCache::preload)
- **Show/hide key: any key, with Ctrl / Alt / Shift.** Status > Settings > Change, then press the key (Esc cancels);
  "Reset to F8". Saved in `gui_settings.json` (`gui.toggle_key`, `gui.toggle_mods`) and used at once. While Turbo is
  shown, and for half a second after it hides, the game never sees that key (window messages, DirectInput state and
  buffered data, raw input, GetAsyncKeyState / GetKeyState / GetKeyboardState). Its key-ups still reach DirectInput
  buffered data and raw input, so the press that shows Turbo (which the game may already have seen) is never left
  held down in the game. A key that types text goes to a Turbo text box that has the keyboard. (core/hotkey.h,
  ui/hotkey_setting.h, win/overlay_dx12.cpp, win/input_shield.cpp)
- **Ctrl + mouse wheel zooms the whole Turbo window** (text, spacing and pictures together, 0.60x to 2.50x). Ctrl + 0
  goes back to 1.00x. A toast shows the size; it is kept in `gui_settings.json` like the Turbo Tools "UI size" slider.
- **Players list: club filter.** A combo next to "My club" with a search box (club name or ID, Enter picks the first
  match). It lists players of that club or national team and works with the other filters. Picking a club unticks
  "My club"; Clear resets it.
- **Real-face chooser filters.** Players > Appearance > Choose a real face: filters for **Ethnicity**, **Skin tone**,
  **Hair colour**, **Hair** (with the game's hairstyle pictures), **Facial hair** (clean-shaven, any, or a style),
  **Facial hair colour** and **Eyes**. Each filter lists how many heads have each value, with a picture, and they
  combine. **Sort** by name, overall, skin tone, hair colour or newest head. FC 27 has no ethnicity field: Turbo groups
  the game's head types (headtypecode).
- **Real faces for managers.** Managers > a manager > **Appearance** > Choose a real face: the same chooser, with
  **Player heads** or **Manager heads**. A player's head also gives the manager that player's miniface (saved as the
  head's 512 x 512 heads_staff picture). The tab also shows the manager's appearance fields.

### Changed

- **Team names: edit, Save, done.** Teams > Name is now one form (Name, Short name, 3-letter code) with one **Save**
  button. Save shows the new name in the game at once: no Live Editor restart, no Reload, no screen to visit first.
  Screens opened after Save should show it (career hub, fixtures, tables, news, the next match's scoreboard: to be
  confirmed in game); a screen that is already open behind Turbo shows it once you leave it and come back, and the
  line under Save says so. An
  empty short name or code is made from the name (the box shows what will be used). The line above the form says
  whether live names are on; when they are off (unknown game version, a kill switch) it says why, and Save still
  writes Live Editor's file, so the name shows after Live Editor's next start as before.
- How: Turbo answers the game's own text lookup for the clubs you renamed (`LocImpl::Lookup`, one level above Live
  Editor's own team-names hook, which keeps working for every other text). Renamed clubs are kept in
  `turbo_output\team_names.json` for every career and given to the game again at every start. Save also writes
  `teams.teamname` and Live Editor's `custom_team_names.csv` as before. Kill switch:
  `turbo_output\team_names_hook_off.txt`. Status tab: "Live team names: on | 2 renamed clubs | names given to the
  game 57".
- A 3-letter code or short name with accented letters is now cut by letters, not bytes ("ÖST" stays whole).
- **Live league table: double-click a number to change it.** Competitions > Live standings: double-click W, D, L,
  GF, GA or Pts of a club, type the value, Enter (or a click elsewhere) writes it to the game at once and the
  Standings screen and Office tile re-read it; Esc cancels. P and GD follow by themselves, Pts moves with W / D / L
  (3 / 1 / 0) unless you typed Pts. Refused with a short reason: negatives, more games than the club's fixtures.
  **Undo** puts the last edited line back. One status line ("Torino FC: W 2 -> 3, Pts 0 -> 3 (applied)"); the
  technical lines moved to the (?) tooltip. Click a column header to sort; "Home / away columns" shows (and edits)
  the home and away counters. The old +/- counters are under **Advanced**, closed by default.
- **Competitions tab: a searchable competition picker instead of the long combo** (Live standings, Match setup, Career
  database copy). Type to filter by name, country, kind or id (accents ignored; Up / Down, Enter, Esc; Right / Left show or
  hide a competition's stages). Your club's competitions on top, then leagues grouped by country, cups, continental and the
  rest; sort by country, name, clubs or id; "Leagues only" on by default (a search still looks at every kind). A
  competition's internal stages (knockout playoff pots, round of 16 pots, setup pools ...) are collapsed under it instead of
  being separate entries. The last choice, the toggle and the sort are remembered per picker (`gui_settings.json`
  `competitions.live / match / database`). Match setup gets a competition filter over your next fixtures.
- **Readable competition names**: "Competition 223" is now "UEFA Champions League" (Europe (UEFA), continental). FC 27's
  database names only leagues and Live Editor 27.1.2 has no `GetGameLocString` / `GetCompetitionNameByObjID`, so cups and
  continental competitions are named from Turbo's own list of FC's competition ids (Coppa Italia, FA Cup, Copa del Rey,
  UEFA Europa League, CONMEBOL Libertadores ...); the rest get a label from the competition tree ("Italy cup 5000").
  Match setup's fixture lines name the competition too (they said "competition 1120", the group node, before).
- **Every Browse... opens inside the overlay** (Export, Import, crest and miniface pictures): folders, files, Up,
  drive letters, shortcuts and **New folder**. No Windows dialog opens, so the game stays in full screen.
  **Open in Explorer** is a separate button, marked "(leaves full screen)".
- Export: pick the JSON folder, the CSV folder, and the file name (**Browse...** next to File name). Defaults:
  `turbo_output\players` (JSON and miniface) and `extensions\player_presets` (CSV). If a file already exists, Export
  lists it and asks first (**Replace and export** / **Back**). File formats are unchanged.
- Each picker remembers its last folder (`turbo_output\gui_folders.json`).
- **Transfer, loan, release, terminate loan and delete work for every club, yours included.** Turbo checks first that
  the career stays consistent: the new club has room (52 players at most) and a free shirt number, a club with a
  match squad keeps at least 18 players, a club's only goalkeeper stays, a loaned player's loan ends before a transfer
  or release, and a player on your transfer or loan list comes off it through the game's own remove before he leaves.
  Your team sheet follows: a starter who leaves is replaced by the first substitute. Back up your save; the squad
  screens show a move after saving and loading the career.
- **Clone, create and import as new player work for your club too.** He joins as a reserve and goes on your team sheet.
- **No more greyed buttons without a reason.** A move that cannot run for the selected player stays clickable, says
  why on hover ("Not possible for him: ...") and on click, and sends nothing. Example: Transfer list on another
  club's player (the game's lists are your club's only).
- **List status works for any player.** A player the career keeps no contract record for is "not listed" instead of
  an error.
- Rules and reasons: `docs/turbo-reference.md`, "Player moves for every club".

### Fixed

- The in-overlay file picker no longer stops on a folder holding a file whose extension the Windows code page cannot
  show (it read the extension as ANSI text).
- Players club filter: Enter with nothing typed keeps the current choice instead of picking the first club.

- **Moving one of your players never leaves him on your transfer / loan list.** A transfer, loan, release or delete
  out of your club is refused (nothing written, with the reason) when his list status cannot be read (Turbo GUI not
  running, another game call still pending) or the game's remove is only queued; a loan whose club move fails takes
  its new playerloans row back out, and a move that ends a loan needs Live Editor's row delete before it writes.
- Scrollbars no longer turn into big grey ovals after resizing the game window or changing the UI size: every size
  change rebuilt the style on top of the already scaled sizes. Scrollbars and grabs are also thinner.
- Clubs stored as an unresolved name key (`*TeamName_Abbr15_112264`) show a readable name in the Club column, the Teams
  list and every club picker: Live Editor's custom team name when there is one, else "Team <id>".
- **Export no longer drops the game to the desktop.** The export started a Windows `mkdir` command for its folders,
  and its console window took the game out of full screen (the screen blinked). Turbo now makes the folders itself,
  and the Lua side only runs `mkdir` for a folder that really is missing.

- **Game editors: FC 27's own edit screens unlocked** (Turbo Tools > Game editors, on by default). Career > Squad >
  Edit Player no longer greys out first / last / known-as name, commentary name, kit name and number, nationality,
  birth date, height, weight, position, role and preferred foot, and gets the **Attributes** (33 bars) and **Brand
  animations** sections the game defines for Create-a-Club players. Edit Manager (created or real manager): names,
  nationality and birth date editable, height and weight shown. Create-a-Club players get the commentary name; the main
  menu's Edit Players unlocks names, nationality, birth date, height and weight. Team stays locked (Turbo's moves do
  transfers) and a player's gender stays hidden.
- How: the game reads one small config per editor screen (`data/avatar/avatarcustomizationcfg_<screen>.json`) each
  time the screen opens. Turbo asks the game for its own files, keeps them with their SHA-256 in
  `turbo_output\edit_unlock\originals\`, applies a name-based recipe and writes the result to Live Editor's
  `mods\legacy\data\avatar\` (`manifest.json` lists what Turbo wrote). **Restore the game's originals** removes exactly
  those files (a file changed since by another tool is left alone). After a game update, **Re-read the game's files**
  removes them, exports the game's own again and rebuilds them. Online, Manager Live, Player Career, Clubs and
  tournament files are never written. Nothing EA made ships with Turbo: the files are built on your PC.
- **Unlock everything (experimental)**, off by default, with a warning: head editor for real players and real managers,
  Composure and Defensive awareness bars, the manager outfit picker, every goal celebration, the manager's gender.
- **Career settings too (advanced)**, off by default, in the same section: 27 of the 32 settings the hub's Settings
  screen locks in a running career (match setup, training and development rates, transfers, negotiation and scouting,
  board expectations, job offers, manager market, unexpected events, pitch wear, points deduction). Competition,
  currency, deeper simulation, financial takeover and youth academy stay locked. With "Unlock everything" the hub also
  gets the Squad settings (edit injuries, edit suspensions, release players). These change the simulation mid-career.
- **In-memory fallback** (same section, on by default): when Live Editor does not apply the files, a guarded hook on the
  game's editor config loader turns on the greyed fields the game loaded, with the same keep-list (TEAM, player
  GENDER, PREFERRED_POSITION and BODY_TYPE stay as shipped; Edit Manager's gender only with "Unlock everything"). It
  cannot add a section the game's file lacks (Attributes, Brand animations, head editor, outfit picker). Installed only
  when the loader, eight layout guards and the four keep-list ids resolve on the game build; kill switch
  `turbo_output\edit_unlock_hook_off.txt`; its status lines show in the section and on the Status tab.
- Details: a switch per screen and per file, and a status line per file (waiting for the game's export / original
  exported / unlocked / restored / failed: why). All switches are one `gui_settings.json` entry (`edit_unlock`).

- **Reopen club customisation** (Turbo Tools > *Your club: customisation hub*, Manager Career). One button brings the
  hub's *Customise club* tile back. Your created club (Create a Club) gets the kits, crest and stadium designer at any
  time, not once per season. Other clubs get the stadium hub. Leave the hub and come back after pressing it; a new season
  locks the tile again, so press it again then. Turbo checks the career's MainHubManager first (signature `mhm_vtable`,
  manager slot 58, link back to the manager table, flag values) and writes nothing when a check fails. *Licensed stadium
  too...* is opt-in because it can replace a club's real stadium in the save. It first offers to copy your Manager Career
  saves (`CmMgrC*`) to `turbo_output\save_backups`. The Create a Club setup steps (name, rival, squad, budget) cannot be
  reopened. The career settings unlock is in Turbo Tools > Game editors (*Career settings too*).

## 1.1.0 (voice swaps: any player gets any real or generic callname in matches)

Released 2026-10-04. Checked in game: the observe log shows the swapped player id on team-sheet, line-up and in-play
commentary requests.

### Added

- **Voice swaps: call a player with another player's own recording, in matches only.** Players > Callname > All
  callnames: an own-recording row has **Use his voice**. After a confirmation ("Marianucci will be called Lobotka in
  matches. His name on screen stays Marianucci.") the commentary uses Lobotka's recordings for Marianucci. Nothing is
  written to the database or the save, so his name on screen, his shirt and the career stay as they are. His surname
  lines are silent by default.
- **Turn a player's own recording off.** A player with his own recording has a **Use his own recording** checkbox.
  Untick it, then pick a generic callname with **Use in matches**: he is called by that callname in matches.
- **Voice swaps tab** (Players > Callname): every swap, what he is called, his other lines, *Name lines only*,
  Remove and *Forget all*. Swaps are kept in `turbo_output\callnames\voice_swaps.json` for every career; a player
  not in the loaded career is marked so.
- The "Current callname" line shows the swap ("In matches: Lobotka's own recording (voice swap)"), with the game's
  own rule greyed below it, and its play button plays what he is called. When the source has no recording in the
  loaded language the line says so ("... has no recording in ita_it: silent").
- Status tab: "Voice swaps: on | 3 swaps | lines changed 57 | kick-off set 2".
- When the game build is not the known one (or a kill switch is on) the tab says "Voice swaps are off: <why>" and
  shows only the database buttons. Kill switch: `turbo_output\callname_voice_off.txt`.

### Changed

- All callnames: the database buttons now sit under "Change the name in the database", below the voice-swap button.

## 1.0.3 (callnames: assigning a callname keeps the player's shown and printed names)

Install as 1.0.2. Not yet checked in game.

### Changed

- **A generic callname now goes to the player's own callname row first.** By name and All callnames write the pick as
  his player-specific callname (`playernamemap`: his row, a new row when the table has room, else a spare row that no
  player hears) whenever that is possible. No name changes, and Turbo writes it again at every career load (1.0.2).
  This is used only when the callname has a known recording (the spoken set or the master's generic ids), because the
  game says a player-specific callname only then. Otherwise the name id is written as before, and the toast says that
  the game shows the new name until the career is reloaded. A line above the button says which way will be used.
  The line next to "Assign callname" says "this career session only" for a player with his own recording: Turbo
  does not keep his callname for the next career loads.
- **All callnames uses the common name of a player who has one.** When no callname row can be used, All callnames
  now offers "Assign as common name" for a player with a common name, because the game then says his common name
  and never his last name. Players without a common name still get "Assign as last name".

- **The FC 27 master carries every name family.** `fc27_commentary` also extracts `pPLAYER_NAMES_HIGH` (split by
  selector; Italian: all 3,306 unique rows by player id, none by surname) and writes LINK / HIGH wavs (`real_link\`,
  `real_high\`), a per-family summary and a count of the other 120 `p*` families; the existing outputs are unchanged.
  The master adds `real link` / `real high` / `generic high` rows (Play cell = a link to the wav) to the unchanged
  `real` / `generic` rows: Italian 15,339 rows, 0 duplicates; JSON `real_high_players`, `generic_high_ids` and their
  segments. Turbo plays LINK wavs from `real_link\`. See `docs/callnames.md` §10.

### Fixed

- **Turbo's kept-name rows had no shirt name.** `editedplayernames` rows that Turbo added or edited left
  `playerjerseyname` empty (the game's own rows fill it). They now carry the player's current shirt name: his row's
  own, else the text of his `playerjerseynameid`, else his shown surname. Lua fills it too when a command gives none.
- **The name id was written before the kept name.** Turbo now writes the `editedplayernames` row first. When the row
  exists it is edited in place, then the name id is written; a failed row write stops the name id. When the row must
  be added, Turbo's Lua side gets one command (add the row, then write the name id), so a refused row also stops the
  name id. A full `editedplayernames` table now writes nothing (before, the name id was written and the shown name lost).
- **The confirmation for a player with his own recording misnamed a generic callname.** It said "callname 900017
  (generic 'Kane''s)". It now says "write the generic callname 'Kane' (900017) to his playernamemap row", and the
  kept callname is noted as coming from "the generic callname 'Kane'".

### Hear a callname in Turbo

- Players > Callname has a small play button on the "Current callname" line and on every row of By name, By player
  and All callnames. It plays the recording from your FC 27 audio folder (`C:\FC_Tools\My Mods\i27` for Italian),
  like the Play cells of your master workbook. Click again to hear the next variation; click while it plays to stop.
  When it cannot play, the button is greyed and its tooltip says why (no audio folder in the master, no recording of
  that id, or the wav is missing). Windows plays the sound; the game is not involved.
- Rows that share a callname (FC 27 gives 922045 to 11 players) each have their own button. Before this fix, the game
  showed a red "conflicting ID" message.
- Refresh checks the wav files again. A wav copied into the audio folder after it was reported missing can now be played.
- Needs the new master JSON: `turbo/tools/build_callname_master.py` now writes `wav_dir` and `segments`. Copy the new
  `ita_it.json` to `turbo\callnames\masters\` and press Refresh.

### FC 27 callname master

- `turbo/tools/build_callname_master.py` now gives exactly what `build_master_v0.py` gives: column H "nameid", the Play
  macro pointed at `i27` (`--vba-from` / `--vba-to`, with `turbo/tools/vba_tool.py`), `--copy-to`, and
  `<name>_new.xlsm` when the workbook is open in Excel. New options `--wav-dir` (checks every row's wav).
- Names for 'real' rows whose player is not in the FC 27 database or the template now come from
  `--extra-names` (default `turbo_dev\masters\raw\extra_player_names.json` when it exists), then from
  `--names-from <other FC 26 master>` (repeatable; its callnames, 'names' and 'fc26 heads' sheets). For Italian, 3
  rows are still without a name (ids 80815, 272258, 272260). The callnames sheet is the same as
  `C:\FC_Tools\My Mods\i27\italy_master_fc27.xlsm`, cell for cell.
- Safer writes. The tool refuses to run when any file it would write is the template. It compares paths the way
  Windows does (case-insensitive, links resolved). This covers the workbook, its `_new` name, the `--copy-to` copies
  and the JSON, and `--copy-to` may not be the template's folder. A `--vba-from` text that is not found, or a patch
  that leaves the old text in a module, now fails and leaves no file. `--vba-from` and `--vba-to` must have the same
  length. The JSON is written before the `--copy-to` copy. The copy keeps the requested file name and becomes
  `<name>_new.xlsm` when the old copy is open in Excel. A `--copy-to` that is not a folder is skipped with a note.

## 1.0.2 (callnames: players with their own recording; kit colours and player callnames kept across career loads)

Install as 1.0.1: unzip `FC27_LE_Turbo_1.0.2.zip` into the FC 27 Live Editor folder with the game closed. Back up your
saves. Then, once: `python turbo\tools\import_callname_masters.py` (from the repository; needs openpyxl) to turn your
master workbooks into `turbo\callnames\masters\<language>.json`, and press Refresh in Players > Callname. The tool uses
an FC 27 master (`<name>_master_fc27.xlsm`, the same format, built from the game) when there is one, else your FC 26
`<name>_master.xlsm`.

### FC 27 callname master (built from the game's own commentary files)

- `turbo/tools/fc27_commentary` reads the commentary pack on disk (toc / cas, Oodle through the game's own
  `oo2core_9_win64.dll`, the speech bank's data sets, EA Opus audio through ffmpeg) and gives what FIFA Editor Tool's
  "Export Data Set" gave for FC 26: every `pSIMPLE_SURNAME` (generic) and `pPLAYER_NAMES_SIMPLE` / `_LINK` (real)
  row with SegmentID, VariationId and id, plus each segment as a wav. Italian: 3,443 generic rows (2,533 commentary
  ids), 6,878 real rows (4,039 players; 4,046 with the LINK lines), 9,263 wavs. FIFA Editor Tool has no FC 27 support.
- `turbo/tools/build_callname_master.py` writes the master in your FC 26 workbook format (same sheets, styles and Play
  macro) plus Turbo's `masters\<language>.json`. Delivered: `C:\FC_Tools\My Mods\i27\italy_master_fc27.xlsm` with its
  own `real\` and `generic\` audio; its Play macro points at `i27` (your FC 26 workbook and audio are untouched).
  Column H "nameid" gives, for generic rows, the name id to set as a player's last / common name.
- Checked: Bianchi 900762 keeps FC 26's segments 931 / 1419 / 1420; 2,113 of 2,114 FC 26 generic rows keep their
  VariationId; Gutierrez 261865, Lobotka 216435 and Rrahmani 244263 are real recordings.

### Fixed

- **A common name without a callname was skipped.** FC 27's match code uses a player's common name whenever he has
  one, even a name without a callname, and never falls back to his last name (`docs/re/inmatch-callnames.md`). Turbo
  now resolves the same way and tells you to use "Assign as common name" for such players (Gutierrez is one).
- **Callname tab said "none" for players who have their own recording.** The game says a player's own recording
  (bound to his player id) before any callname. Turbo only knew these players from the game's audio service, which
  found 751 in Italian; your FC 26 list has 4,127 (Lobotka and Rrahmani among the missing ones). Turbo now also reads
  the master list. Such a player now shows "Current callname: his own recording in ita_it (your FC 26 list)", or
  "(the FC 27 master)". When only the FC 26 list says so, the line adds that the game's audio service did not confirm
  it (its list is known to miss players).
- **A callname could be written for such a player without notice, and never heard.** Both By name and By player now
  warn above the buttons and ask "Assign anyway?" before writing anything for him.
- **A full playernamemap could give away a callname that is spoken** (1.0.1 note corrected). 1.0.1 took over rows whose
  commentary id is above 965000, believing no commentary uses them. Your lists show generic recordings there in some
  languages (English 980001..980034, Italian 999931..999952, Spanish and Dutch 9999xx). Turbo now takes only a row
  whose player is not in the database, whose callname is none, or whose callname has no recording in the loaded
  language (the game's audio service for 900001..965000, the master list above that), and names that player before
  writing. A row Turbo cannot check is left alone.
- **A row could still be added to a full table.** The Turbo window checks the room when you click, but Turbo's Lua side
  adds the row at the next career event, maybe after a career load (FC 27 reloads `playernamemap` full). Lua now counts
  the rows again right before it adds one and refuses when the table is full by then or a career was loaded in between.

- **"Keep shown name: a command is still running" when assigning several players in a row.** Turbo's Lua mailbox
  holds one command at a time, so the second player's shown name was refused. The Turbo window now queues these
  actions and sends them, batched into one command, as soon as the mailbox is free; the top bar says how many wait.
- **The FC 27 master decides who has his own recording.** When the language's master is an FC 27 master (built from
  the game's commentary files: `real_players` = PLAYER_NAMES_SIMPLE + PLAYER_NAMES_LINK), it alone says whether a
  player has his own recording; the game's audio-service set (751 players in Italian, LINK only) is no longer added on
  top. Order: FC 27 master, else the audio-service set and your FC 26 list (either is enough). Its generic ids count as
  spoken surnames together with the spoken set, and the tab's source line says so. The master's `generic_names`,
  `real_simple_players` and `real_link_players` are read.

### Added

- **Kit colours and player-specific callnames are written again at every career load.** FC 27 reloads `teamkits` and
  `playernamemap` from its base data whenever a career loads, so these edits used to last one session (1.0.1 Known
  limits). Turbo now keeps them in `turbo_output\reapply_edits.json` (kit colours from Teams > Colours, per kit;
  player-specific callnames from Players > Callname, per player) and writes them again when it connects to a newly
  loaded career, also with the Turbo window hidden. Once per career load: not when the manager changes club, not on
  Refresh. A callname goes to the player's own row, else to a row nobody hears a callname from; this never adds a row
  (re-assign it in the Callname tab if he has none). Never written: a callname for a player with his own recording
  (not even kept), and anything for a club or player not in the loaded career. Only the colour channels are written,
  each kit separately (two kits of one type keep their own colours). One line in `turbo_gui.log` and in both tabs says
  what was written, what could not be and what was left alone; a toast only when something was written or failed. A
  **Forget** button (per kit, per player) drops a kept edit; *Remove player-specific callname* drops it too.
  `turbo_output\reapply_off.txt` turns the re-apply off.
- Players > Callname > **All callnames**: one type-ahead list of every callname the master lists for the loaded
  language: each generic surname (commentary id, text, how many name rows and players use it) and each player's own
  recording. A generic callname goes to the edited player through a name row that has it (his last name, the shown
  name kept, as By name), else as his player-specific callname (as By player: his row edited, a row added when there
  is room, else a spare row taken over; kept for career loads). Own recordings are listed but cannot be given to
  another player yet. The own-recording confirmation still comes before any write.
- Players > Callname > **Players without own recording**: the players of the same club whose callname the game does
  use, to pick a player a callname test can be heard on.
- `turbo/tools/import_callname_masters.py`: reads the master workbooks of the seven languages (por_br, eng_us, fre_fr,
  ger_de, ita_it, dut_nl, spa_es) and writes one list per language, with `"game": "fc27"` or `"fc26"`. An FC 27 master
  (`<name>_master_fc27.xlsm` / `.xlsx`, under `C:\FC_Tools\My Mods` or `<Live Editor>\turbo_dev\masters`) wins over the
  FC 26 list of its language; among FC 26 lists the `.xlsm` wins (the older Dutch `.xlsx` is skipped).

### Not yet seen in game

- Built and tested on the test world only (native and Lua suites). The FC 26 lists are FC 26 data: FC 27 mostly
  reuses those recordings, but a recording FC 27 dropped is still listed until an FC 27 master replaces the list.

## 1.0.1 (fixes from the in-game check of team name, team colours and callnames)

Install as 1.0.0: unzip `FC27_LE_Turbo_1.0.1.zip` into the FC 27 Live Editor folder with the game closed. Back up your saves.

### Fixed

- **Crash when loading a career after changing a crest.** FC 27's 256 x 256 crest files hold 4 mip levels. Turbo 1.0.0
  wrote one level but kept the original's count of 4, so the game read past the file and crashed while loading the
  career. Turbo now writes every level the game expects. At start, Turbo also repairs crest and miniface files that
  1.0.0 wrote (the original goes to `turbo_output\crest_backups` or `miniface_backups`; `turbo_gui.log` lists each one).
- **Crash on Players > Callname > By player > Use this player's callname.** FC 27's `playernamemap` table is full
  (106 of 106 rows), and Live Editor crashes the game when asked to add a row to a full table. Turbo now reads the
  table's capacity first. When the table is full, it takes over one of the 38 rows whose callname no commentary uses,
  so no other player loses a spoken callname (corrected in 1.0.2: some languages do speak ids above 965000). If no such row is left, Turbo says so and suggests By name instead.
  The same check guards the row that keeps a player's shown name.

### Added

- Commentary speech log (diagnostic, opt-in). With an empty `turbo_output\commentary_speech_log_on.txt` at game
  start, Turbo counts every callname id and player recording the game's commentary asks about, in
  `turbo_output\commentary_speech_log.txt`. It shows that the commentary used the callname you gave a player.

### Verified in game (1.0.1)

- **Team name**: the new name shows on the career card, the contract offer ("TURBO NAPOLI FC"), the hub header (10-letter
  name), the fixture panel ("TURBO NAPOLI"), news items and the tournament invitation. It is kept across save and reload.
- **Team colours 1 to 3** (club colours): written, and kept in the career save across save and reload. Their use on match
  screens was not seen, because no match loads on the test PC (see Known issue).
- **Generic callname** (Players > Callname > By name): Totti assigned to Lobotka as last name, "spoken in ita_it",
  shown name kept. Kept across save and reload. Not heard yet, because no match loads (see Known issue).
- **Player-specific callname** (By player): works on FC 27's full table (row takeover), no crash.
- **Crest repair**: a career with a 1.0.0 crest loads again (startup repair logged in turbo_gui.log).

### Known limits (found in this check)

- **Kit colours and player-specific callnames last for the session only.** FC 27 reloads `teamkits` and `playernamemap`
  from its base data at every career load, so these edits are gone after the career is loaded again. Club colours,
  names and generic callnames are saved with the career. Re-applying them on load: 1.0.2.
- **Keep the shown name** takes effect on all screens after the career is loaded again. Until then, the team sheet may
  show the new surname.
- Callname audio is per language. With the game's "Localized Commentary" on, a match abroad (for example a pre-season
  match in England) uses that country's commentary, not the language Turbo's spoken set was built for.

### Known issue: no match loads on the test PC (not Turbo)

Every match load (Kick-Off and career, Play Match and Tactical View) crashes on the test PC with FC 27 1.0.140.64835
and Live Editor 27.1.2. It crashes the same way with Turbo.dll not in the game (Turbo's start script removed), with
Turbo's game hooks off, with Turbo's image files moved out, in Kick-Off, and with a 1280 x 720 window.

Cause, measured in the running game: Live Editor's offsets file for this build (entries for 0x14216E818 and 0x14F414398)
points the game's front-end SetMousePosition (UI input manager, called with -1, -1 on the front-end thread during match
load) straight into the game's protected code at 0x14F414398. During match load the protection rewrites the jump at
0x14F4143B2 into a call (E9 -> E8, observed at 11:39:06 and 11:44:31; it reads E9 at the main menu). The stack ends up
8 bytes off and the game faults at 0x14216E881 two seconds later. Report it to Live Editor's author, or check for a Live
Editor update for this game build.

## 1.0.0 (since 0.3.0)

Install: unzip `FC27_LE_Turbo_1.0.0.zip` into the FC 27 Live Editor folder (same layout as 0.3.0 and 0.4.0), start the
game through Live Editor, press F8 to show Turbo. Offline career only. Back up your saves.

### Foundation

- Game-hook foundation: Turbo.dll calls the game's own code, so the game makes the change and its own screens show it.
- Game-thread tick: game calls run on the game's own thread, at a safe moment.
- Live Editor career-event integration: Turbo's commands run through the career event Live Editor really hooks.
- 37+ game signatures for FC 27 build 1.0.140.64835. A feature whose code is not found stays off, and the Status tab says why.
- Kill switches: an empty `call_<name>_off.txt` or `hook_<name>_off.txt` in `turbo_output` turns one game call or hook off.

### Verified in game

- Player editor: sliders, combos, check boxes, PlayStyles / traits, star-head, tattoo, hair and item galleries, undo.
- Miniface from the game's 3D model, for players and managers.
- Callnames spoken in the loaded commentary language. Turbo builds the spoken set from the game's own audio check when
  the game binds the bank (Create Player screen or a match). Italian: 2,462 surnames, 751 player recordings. Assign by
  name or by player.
- Create job offer from a chosen club: a real contract offer in the game.
- League table edits shown on the game's own Standings screen, without advancing the calendar.
- Export / import (Live Editor preset CSV and Turbo JSON, names included) and clone.
- Instant overlay commands: no day advance needed.
- Develop to potential.
- Reveal player data, through the game's own scouting.
- Injuries-off switch, accepted by the game.
- Teams, managers and database editors.
- Status tab.
- Transfer list and remove from lists, for your own players, through the game's Transfer Hub.
- Job security levels and unsackable.
- Transfer budget.
- Team crest (the game shows the new crest).
- Create player from a template.
- Miniface from an image file.
- Move a manager / make a manager available.
- Match setup switches (the game accepts weather and difficulty) and the home / away swap of a fixture.
Built and tested offline, not yet seen on a game screen in 1.0: loan list; team name (Live Editor shows a new name after its next start) and team colours; youth academy tools; time of day and CPU-substitutions switches; changing a fixture's opponent.


### Untested in a match

Built and tested offline. Their effect only shows during or after a played match, which has not been checked yet.

- Forced result for a chosen fixture (opt-in).
- Injuries off during a match.
- Editing a played result.
- Weekly forced growth.
- Callname heard in a match.

### Not in Turbo 1.0

FC 27 Live Editor's own features cover these, or later Turbo versions will.

- Player moves of your own club through the game's engine. Turbo refuses database-only moves for your club, for safety.
- Live season statistics.
- Negotiation and approach bypasses.
- Unsupported leagues.
- Remove suspension.
- Player Career funds, wage and attribute points.
- CPU vs CPU, unlimited subs, match length.
- Offsides and referee as switches.
- Formation editor UI (Database tab only).
- Speedhack, hotkeys, legacy file browser, Gameplay Attribulator.
- Stadium override: left out on purpose. A stadium id the game cannot load stalls the match load.

### Not possible in FC 27

- Transfer bans: the game has no ban list.
- Asking price or loan terms when listing: the game's listing takes only the player.
- Endless-career switch: not needed, FC 27 has no season limit or forced end.
- VAR off: the game has no such setting.
- Fatigue off as a match switch: FC 27 Live Editor's "never tired" covers it.
- FC 26's development XP multiplier: the natives are gone. Weekly forced growth replaces it.
- Match sharpness: removed from FC 27.

## 0.3.0 and earlier

- 0.3.0: minifaces, real-face picker and tattoo picker in the Turbo window (see `turbo/package/TURBO_README.md`).
- 0.2.x: see the "New in 0.2.x" notes in [`README.md`](README.md).
