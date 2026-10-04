# Changelog

All notable changes to FC 27 LE Turbo. "Verified in game" means seen working in a test FC 27 Manager Career.
Feature-by-feature status: [`docs/fc26-parity.md`](docs/fc26-parity.md).

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
  so no other player loses a spoken callname. If no such row is left, Turbo says so and suggests By name instead.
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
  names and generic callnames are saved with the career. Re-applying them on load is planned.
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
