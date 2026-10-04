# Changelog

All notable changes to FC 27 LE Turbo. "Verified in game" means seen working in a test FC 27 Manager Career.
Feature-by-feature status: [`docs/fc26-parity.md`](docs/fc26-parity.md).

## 1.0.2 (callnames: players with their own recording; kit colours and player callnames kept across career loads)

Install as 1.0.1: unzip `FC27_LE_Turbo_1.0.2.zip` into the FC 27 Live Editor folder with the game closed. Back up your
saves. Then, once: `python turbo\tools\import_callname_masters.py` (from the repository; needs openpyxl) to turn your
master workbooks into `turbo\callnames\masters\<language>.json`, and press Refresh in Players > Callname. The tool uses
an FC 27 master (`<name>_master_fc27.xlsm`, the same format, built from the game) when there is one, else your FC 26
`<name>_master.xlsm`.

### Fixed

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
