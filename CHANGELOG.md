# Changelog

All notable changes to FC 27 LE Turbo. "Verified in game" means seen working in a test FC 27 Manager Career.
Feature-by-feature status: [`docs/fc26-parity.md`](docs/fc26-parity.md).

## Unreleased

Nothing here is shipped or claimed as a feature. Gates are in `turbo_dev\evidence6-10-15\TEST_31.md`.

- **UNPROVEN:** tactics sliders for team length and width (`shape.length_in`, `shape.width_in`). Gate G-T1 was INCONCLUSIVE in game (counted as FAIL for the live write route).
- **UNPROVEN:** any effect of the game settings sliders on match play. Gate G-S3 (ratio >= 1.4).
- **UNPROVEN (2.21 compdata guard, offline-green only):** when a career loads, Turbo checks the installed league data against the career. The launcher's country pick decides the data: Netherlands gives the Eerste Divisie data, any other country the standard data (`turbo_output\compdata_profile.json`, plus a stat-only look at the 11 Eerste files in `mods\legacy`). The career's need comes from `turbo_config.json` `compdata_profile.careers`; SPARTA `d91d1b4e90a27eda` = eerste is built in. Turbo logs one line, `compdata profile: <installed> installed, career needs <need>, OK|MISMATCH|UNKNOWN`. On a mismatch a red "DO NOT SAVE" line leads the overlay's message area, and it shows while Turbo is hidden too. A missing record, or a record the disk contradicts, gives UNKNOWN: logged only, no banner. The guard never writes a file.
- **UNPROVEN (2.21 launcher hint):** the commentary language detection ranks the launcher's pick (`gui_settings.json` `callnames.launcher_language` / `launcher_stamp`) after the manual override and this session's runtime answer. It also ranks the pick before an earlier session's saved answer, unless that answer was heard after the pick. The pick is used only when its pack is live in the game's `Data\Win32` (full toc + a non-empty cas). A hint answer is never "proven", so automatic callname writes still wait for the running game's answer.
- **UNPROVEN (2.21 CHANGE 8, registry Locale):** the old guess from the packs is gone. It read `<game>\commentary` (Cassio's backup store) and picked alphabetically. In its place, Turbo reads the registry Locale (`HKLM\SOFTWARE\EA Sports\EA SPORTS FC 27` `Locale`, 64-bit view then 32-bit, read-only) and maps it with the exe's table: nl_NL dut, it_IT ita, fr_FR fre, de_DE ger, pt_BR por, es_ES spa, en_* eng_us. That language is used only when its full pack is live in `Data\Win32`; otherwise the answer is eng_us. The game offers English plus the Locale's language, so this answer is never "proven". An earlier saved answer outside those two languages is not used. The language list shows only the live packs in `Data\Win32`. The log line names the decision source (`registry Locale fr_FR, pack present`) and, at the end, the data source (`[pack data: backup store <game>\commentary\commentaryfull_<lang>]`, else `Data\Win32`). The backup store never decides the language. The saved answer's pack fingerprint changes format, so one saved answer from before is dropped once.

## 2.21.0 (built 2026-10-19)

Release candidate; in-game status is filled in at G4 (`turbo_dev\PITWALL\briefs\2.21-G4.md`). Includes everything in 2.18.0, the 2.19/2.20 work merged up to c770dfb (full player editor wiring, create-player prefill, team pages and presets, faces safety, kit plan, crest Lua), the 2.20 B5 work (be3d8a5, below) and the 2.21 items listed under Unreleased above (country launcher with compdata pick, compdata guard, commentary language from the registry Locale).

- Pictures in the pickers: hair, facial hair and other looks are chosen from pictures, read from your RDBM26 folder at run time (set the folder in Settings).
- Club, crowd, stadium and kit fields are plain-English pickers: you choose from names and see what the choice does, instead of typing codes.
- Player types: a new Populate panel builds players from a player type (archetype), a wanted overall and a tier. The type list is `turbo\data\archetypes.json`.
- Edit a player: the Players > Positions, attributes & PlayStyles page opens with the player-type and "Wanted overall" boxes, and the growth setting has a shorter label, "weekly growth".
- The country launcher no longer touches the registry Locale by default. The EA app passes its own language for the game at launch, so that route does not work. The step is still in the script as `-WithLocale`, opt-in, and is not part of this release's proof.

## 2.18.0 (built 2026-10-18)

Package FC27_LE_Turbo_2.18.0.zip (md5 609b77396c131c92918731d50ff0c03e, sha256 cffff28dc23dad6bddcf512288c111a18e7ddea13062a2ad16bb08c990c7b11c). Turbo.dll md5 9db316f476bcd7f747f6d64519f1a0f8. Built for FC 27 build 6AC07E31-2145C000 and Live Editor 27.1.5. Includes everything in 2.17.2.

- 2.18.0: large modded crests (crest/dark|light|custom l<id>.dds from mods\legacy) are served by Turbo, fixing the new-career hang at 37% (InitPlayerCaptures) and match-load hangs caused by Live Editor's async descriptor for modded files; kill switch turbo_output\crest_serve_off.txt

In-game check: 2026-10-18, throwaway careers and Kick Off; no manual save (each new career wrote only its creation autosave); the tester's own careers untouched (evidence `turbo_dev\evidence\2026-10-18\crest-serve\g3\RESULTS.md`). Boots A, B and C ran on the test build of the same code (Turbo.dll 7b96c046); boots CS218V and CS218W ran on this release Turbo.dll (9db316f4) itself.

Proven in game:
- A (test build): Ajax new career, control case: the native Ajax crest is not served (read from the game's own file), 0 failures, hub reached.
- B (test build): new FC Den Bosch career (Eerste Divisie): the large crest is served from mods\legacy, no 37% hang (Live Editor captures to PrepareSeason 7.0 s); the contract screen and the hub show the large crest.
- C (test build): Den Bosch v Roda JC, Matchday 1, Play Match: kick-off reached, no crash; large crests on the scoreboard and the player cards.
- CS218V (release DLL): Kick Off, FC Eindhoven v FC Emmen: no crash; large crests on the Match Preview, the scoreboard and the player cards; the custom kits on the Select Kits screen and on the pitch; the Keuken Kampioen Divisie league logo.
- CS218W (release DLL): new FC Eindhoven career: no hang, hub reached; then Matchday 1 kick-off (Eindhoven v TOP Oss), crest served, no crash.

Not proven:
- Audio: no commentary was heard (the commentator card at kick-off was seen only).
- Modded crests on any path other than data/ui/imgAssets/crest/ (the only path Turbo serves in this release).

## 2.17.2 (built 2026-10-18)

Hotfix on top of 2.17.1. Package FC27_LE_Turbo_2.17.2.zip (sha256 12bc6d3a706221ca72676196d37f9056611b6ef795bc231051958441cee7b849). Turbo.dll md5 93ee363f917feab0bfaf2ccae207c0d3. Built for FC 27 build 6AC07E31-2145C000 and Live Editor 27.1.5. 2.17.1 was a test build and was never published; this release carries everything in the 2.17.1, 2.17.0, 2.16.0 and 2.15.0 sections below, so a player coming from 2.14.0 gets all of it at once.

In-game check of this package: 2026-10-18, one boot on the throwaway LEICESTER career (never saved; every save restored afterwards, 20 of 20 files md5-identical): Turbo 2.17.2 connected (141 hooks found, 0 missing, 0 refusals); live standings edit and its Undo, the player edit and its Undo with the file check below, three callnames assigned and removed, the faces picker, the manager hair and beard galleries and the folder picker all passed. Not run on this package: a real-time transfer (no Undo exists), a match load, an Advance, kits, settings and tactic presets, a career save and reload. Callname commentary on the pitch is not proven.

- **Fix: a kept player edit and its Undo no longer leave an empty careers block in `turbo_output\reapply_edits.json`.** Proven in game: Jamie Vardy's Penalties 83 -> 84 written and read back, then Undo -> 83, and turbo_output\reapply_edits.json was md5-identical (bb3b5361) before the edit and after the Undo, with no empty careers entry left. Cause: the file's careers list (the careers that kept edits are bound to) was only ever added to. When the last edit of a career went (Undo, Forget, or moved to Global), the career's entry stayed, so the file changed although no edit was left. Turbo now drops, at every save, each careers entry that no kit, callname or player edit is bound to. The block was only a display name for a career; the kept edits are applied from their own scope, so no game data and no player edit was ever affected. A file written by 2.17.0 or 2.17.1 that holds such an empty entry is cleaned at the first save by 2.17.2 (one change of the file's bytes, no edit touched). Offline: new native tests for the store and for the UI path, including edit + Undo with and without an existing careers block.

Known limits (2.17.2):
- A manager's full-body picture shows his face on a player in a kit, with a ball, not his manager outfit (David Moyes). Parked, not in this release.
- Everything listed as not proven in the 2.17.0 and 2.16.0 sections below is still not proven.

## 2.17.1 (built 2026-10-17, test build, never published; included in 2.17.2)

Fix build on top of 2.17.0. Package FC27_LE_Turbo_2.17.1.zip (sha256 5ccd94c174f1c6c68f8741ee3c99bf5ba7e73a911c991a100d0c3917fbaac60b). Turbo.dll md5 c32dc7384dad49c9e3a2d68aa91cdff4. Built for FC 27 build 6AC07E31-2145C000 and Live Editor 27.1.5. It was never published: its regression run (below) found the defect that 2.17.2 fixes.

In-game check of this package: 2026-10-17, one boot on the throwaway LEICESTER career (never saved; every save restored afterwards): the body-type and settings items below passed. The first regression run on it (R1, 2026-10-17 23:41) passed the standings edit, the connection to Live Editor and the season pack, and failed the edit-path file check: a player edit and its Undo left an empty careers block in turbo_output\reapply_edits.json. That is fixed in 2.17.2.

- **Body-type pictures in the Looks studio.** Proven in game. The body-type grid now shows a picture for each of the 10 generic body types. The pictures are made by Advanced > Diagnostics & logs > Body-type pictures, which you start yourself with a button; until it has run, and for the player-specific "Like ..." bodies, the tiles show names. Not tried on a clean install: the test machine already held the pictures from an earlier run.
- **Body-type changes and their Undo are written to Turbo's log file** as well as the in-game Log. Proven in game: Luke Thomas to "Tall, stocky" and back; the full-body picture got wider and returned.
- **Settings page.** Proven in game: 1.06 ms per frame on this package (target 3.0 ms).

Known limits (2.17.1):
- A manager's full-body picture shows his face on a player in a kit, with a ball, not his manager outfit. Tried again on this package with the game's outfit flag: still a kit. Not fixed.
- Everything listed as not proven in the 2.17.0 and 2.16.0 sections below is still not proven.

## 2.17.0 (built 2026-10-17, test build, never published; included in 2.17.2)

Package FC27_LE_Turbo_2.17.0.zip (sha256 8fe9fada61c651be42a830bf6f427ce8982a382aaea3e008e709e3863e064746). Includes everything in 2.16.0, and 2.16.1, which was never released. In-game check of this package: 2026-10-17, one boot on LEICESTER (never saved): pass, no regression. Each "proven" below was seen in that run unless it says otherwise.

- **Looks studio (Faces & looks > Looks studio), a new page for faces and looks.**
  - Full-body picture of a player, drawn by the game: proven in game (Luke Thomas, head to boots in the Leicester kit). A free agent (Alex Sandro) also draws, in a plain generic kit; no crash.
  - Full-body picture of a manager: it draws, but as the manager's face on a player in a kit, not his manager outfit (David Moyes). A known limit, see 2.17.2.
  - Hair and beard: proven in game (hair 155 and beard 46 on Luke Thomas; the new full-body picture shows both). "His own" brings back his own look. Hair and beard changes have no Undo step; "His own" is the way back.
  - Body type: proven in game ("Tall, stocky" made the full-body picture wider; Undo put it back). In this version the grid showed names only; the pictures came in 2.17.1.
  - Body-type picture generator (Advanced > Diagnostics & logs > Body-type pictures): proven in game. It asks first, naming the career and the player it uses as the reference, and puts his own body type back at the end. A limited run drew 10 pictures, 0 failed.
  - Face picker, accessories and kit fit pickers in the studio: not yet proven in game.
- **Hair and beard changes also go to Turbo's log file.** Proven in game.
- **Player page, Changes & Undo page and Kits & club pages in the menu.** Not yet proven in game (checked offline only). The top bar has an Undo button (seen with "Nothing to undo" while there is nothing). The older screens stay under Settings > Show advanced tools > "Use the old pages".
- **Faster pages.**
  - Settings and the other pages measured: Settings about 1.0 ms per frame, Looks studio about 1.1 ms, Diagnostics & logs about 1.1 ms. Proven in game.
  - Picture files checked on disk far less often; the face chooser no longer freezes the game when it opens (its list fills in with a progress line); the older Managers & staff screen no longer checks files on every frame. Not yet measured in game.
- **Automatic "his own look" restore (off by default).** When a career loads, Turbo can note which players it would give their own look back. For now it only writes a line to the log, and there is no menu switch. Not yet proven in game.

Known limits (2.17.0):
- Mouse pointer: while Turbo's panel is open, the pointer is drawn by Turbo and follows the game's frame rate. A follow-up.
- The face chooser's list fills in over a few frames after it opens; not timed in game.

## 2.16.0 (built 2026-10-17, test build, never published; included in 2.17.2)

Package FC27_LE_Turbo_2.16.0.zip (sha256 a6510aab25ec2e8c1230d6921cbc3b1b0b4b0570aaac427ec03c852402229bd3). Includes 2.15.1, which was never shipped. In-game check of this package: 2026-10-17, about 5 minutes on LEICESTER (never saved): no crash, the overlay opened and every area that was checked opened. Where a line says "earlier build", the proof comes from an in-game run of an earlier test build of the same feature.

- **Bench order kept after Team Management (off by default).** Proven in game on earlier builds: the first Team Management visit after a career loads shows your saved bench order, and the starting XI is untouched. Not run on this package. Not yet proven: the same after you save and reload; the menu switch itself (the tests turned it on with a file). The switch "Keep my bench order after Team Management" is on Tactics > Team sheet & bench.
- **Settings page no longer freezes (it was the Status page).** Proven in game on this package: about 1.6 to 2.0 ms per frame with builds running on the same PC (the 2.15.1 test build measured 5.5 ms; the target is 3.0 ms). Its counts now update in the background, not on every frame.
- **Mouse wheel.** Proven in game on an earlier build: one wheel notch scrolls one step, and the face and manager hair galleries scroll smoothly. Proven on this package: in one session the wheel handling did not need to be set up again (the earlier build needed it 4 to 11 times).
- **Plainer wording.** Proven in game on an earlier build, seen on screen: the player header no longer shows the internal code; the face chooser text; the tactic presets option "Let Turbo change my tactic"; the header "Changes Turbo puts back when you load your career". Not yet proven: "Waiting for your career" (it shows only while Turbo is not connected), and the wording on the new pages.
- **New top bar and a seven-area menu.** Proven in game on this package: the overlay opens with the areas Players, Faces & looks, Kits & club, Tactics, Career, Settings and Advanced, and each area that was checked opens. Not yet proven: the "Use the old pages" switch.
- **New Tactics pages.**
  - Presets: opens and shows your live tactic (proven in game). Save, Load and Undo not yet run.
  - Team sheet & bench: opens (proven in game). Holds the bench order switch above.
  - Game sliders: Prepare and Discard buttons. Not yet proven in game. What a prepared preset does in the game is not yet proven.
- **New Career pages.**
  - Standings: opens and shows the league table (read only, proven in game).
  - Managers & staff (under Players): opens, about 1 ms per frame with a manager selected (proven in game).
  - Transfers & money, Manager jobs, My squad: not yet proven in game. Their actions ask for confirmation first; that confirmation has not been checked in game.
- **New Advanced pages.** Diagnostics & logs: opens, with the game link, switches, game hooks and the log (proven in game). Data export: not yet proven in game.
- **3D hair and beard on real faces, live, without a restart.** Proven in game on players, on the Squad Hub: changing a real-face player's hair and beard works at once (the new look shows the next time his head is drawn: leave him and come back); "His own" brings back his own look; an untouched player stays as he was; picking another real face works and one Undo puts back the previous head and hair; with the switch-off file present, a player shows his own face with no Turbo beard. Now on by default for everyone (it was opt-in). One exception: if the game was started before Turbo's face support loaded, Turbo shows an amber line and the game needs one restart (not seen in the tests). Not yet proven: 3D hair and beard on managers.

Known limits (2.16.0):
- Tactic presets and Game sliders: their effect on matches is not proven.
- Bench order kept: off by default; see above.
- Mouse pointer: while Turbo's panel is open, the pointer is drawn by Turbo and follows the game's frame rate. A follow-up.

## 2.15.0 (built 2026-10-17, test build, never published; included in 2.17.2)

Built for FC 27 build 6AC07E31-2145C000 and Live Editor 27.1.5. Everything from 2.14.0 is unchanged.

- **Keep my bench order (opt-in, off by default). Did not work in this build.** With "Keep my bench order after Team Management" ticked, Turbo puts your saved bench and reserves order back when Team Management is built. The starting XI is never touched. In the LEICESTER test the game re-sorted the bench as the screen opened, so the game's order showed. Fixed in 2.16.0 (see above).
- **Kept edits by career (kit colours, callnames, player edits, club names).** Each kept edit now has a scope:
  - edits kept by older Turbo versions still apply in every career, as before;
  - "global" edits apply in every career;
  - "this career" edits apply only in their own career.
  Move an edit between scopes with Career tools > Changes & undo > "Where kept edits apply (careers)".
  - Proven in game on LEICESTER: Turbo recognised the career from its manager and start date, and the kept edits were re-applied at load (seen again on the later builds).
  - Not proven yet: the scope check itself (a global edit in another career, a career edit left alone there) and moving an edit with the new menu.
  - Undo of a kit import puts the kept fields back in the scope the import wrote. Not yet tested in game.
  - Two careers that are copies of each other, with the same manager and start date, count as one career. This is intended.
- **Crash fix for Turbo's hooks.** In some memory layouts, Turbo's hook library copied a jump from Live Editor's own DirectX hook the wrong way, and the game crashed when the overlay started (with the tactic presets' attribute patch on). Turbo now checks the jump distance and uses a safe form when it must.
  - Proven in game on a 2.10-based test build: the fix engaged and the game stayed alive.
  - Not re-run on 2.15.0 or later: the crash setup itself. On these builds the overlay hooks install normally.

Known limits (2.15.0):
- Tactics: no on-pitch effect is claimed.

## 2.14.0 (2026-10-17)

Tested in game on 2026-10-17 on FC 27 build 6AC07E31-2145C000 and Live Editor 27.1.5, on the throwaway LEICESTER career (never saved), with every save restored afterwards. Published 16:01.

- **Goalkeeper kits are never mixed.** Proven in game. In 2.13, a goalkeeper shirt shared with other clubs could come with your own shorts and socks. Now a kit that cannot be swapped completely stays fully EA's, and the status line says why. Tested: Man City's goalkeeper kit with the option off stayed fully EA's.
- **"Also change other clubs' kits" (per kit).** Proven in game (Sevilla). It swaps a shared shirt anyway, and the other clubs that use the same EA template change too. Tested: Sevilla's goalkeeper kit with the option on showed the full zip kit. Not proven: changing only your own club's kit (the per-club route) is still research.
- **A face comes with its miniface.** Proven in game. Giving a player a real face also gives him that face's small picture, and one Undo puts back both.
- **Warning when a head and its picture do not match.** Proven in game. If you type a head in by hand, Turbo warns that the small picture still shows the old face, and offers "Use the matching miniface" or "Keep his old picture". A generic head shows no warning.
- **Real faces that were hidden now show.** Proven in game (checked on Mbappé and Pedri). The high-quality filter still hides them when it is ticked.
- **Manager heads and pictures.** Proven in game. Managers show their game picture (checked on Arteta), and manager heads are matched by name (checked on Kompany).
- **Callnames from the game's own commentary files.** Proven in game for Spanish: 2,841 generic and 4,903 own names. Other languages need their commentary packs installed in the game; not checked in game yet.
- **Bench probe (investigation only).** Proven in game. Turbo logs your club's bench order at each career load. It changes nothing; it is there to study the reordering problem below.

Checked again on 2.14.0, passed: live standings edit and Undo, manager hair and facial-hair galleries, the Chilwell hair grid in the faces picker, a career match past kick-off, folder picker double-click, season pack apply and undo, live tactic Apply (Edit Tactic shows the new values), Turbo connecting on Live Editor 27.1.5.

Not proven / known limits (2.14.0):
- Not re-run on 2.14: the EA Custom slider screen and the tactic presets save and load. Their code is unchanged since 2.13.0.
- Team Management re-sorts your bench every time the screen opens. 2.14 does not fix it (the bench probe only observes it). See 2.15.0.
- Turbo does not yet notice when Team Management opens on Live Editor 27.1.5, so the probe's open and close lines do not appear.
- Tactics: no on-pitch effect is claimed.
- Cosmetic: in the face chooser the "Hidden" and "name match" badges overlap.

## 2.13.0

**GUI revamp, first cut: five areas instead of eight tabs, proven in game** (2026-10-17, LEICESTER, never saved, Live Editor 27.1.5). Package FC27_LE_Turbo_2.13.0.zip (md5 890f1ae22d14522ed6ee63edd496b7ed). Turbo.dll md5 c9351a8a88a68a7bc1991e32af1ed0b2. Includes every 2.12.2 fix.

- Eight top-level tabs become five areas with a page strip: **Squad & Players**, **Club & Kits** (Kits, Identity, Stadium & extras), **Tactics & Gameplay** (Tactic presets, Gameplay presets, Standings), **Career tools** (Transfers, Job offer, Scouting, Budget, Exports, Bans, CMTracker, Changes & undo, Maintenance), **Settings**; **Advanced** (Database, Raw rows, Bulk edit, Maintenance, Tactics, Match setup, Gameplay tuning) only with the Advanced view toggle. Old `request_tab` cross-links land on the new pages (`shell::area_for`).
- Every proven panel keeps its code path (kits, callnames, faces picker, club tools, tactic presets, standings, game settings, reapply log); only its place and chrome changed. The club-tools kit shortcut is now a link to Club & Kits > Kits.
- Fixed after the 10-16 integration build: live standings now sit at Tactics & Gameplay > Standings (the 10-16 build had no way to reach them); Career tools > Job offer and Transfers lead to the working panels; Maintenance links to the live table; long page strips wrap.
- Proven in game (P213fix): live standings located (6000 rows, 6300 fixtures); Pts +1 written and re-read by the game's standings view, Reload keeps it, Undo restores it; Career tools > Transfers, Job offer and Maintenance show the right panels; manager Appearance keeps the 2.12.2 hair and facial-hair galleries.
- Not proven in game: the Squad & Players pages are the 2.12 panels drawn unchanged (fallback); the in-match effect of slider presets and Tactics sliders (unchanged since 2.10).

## 2.12.2

Fix release on top of 2.12.1. Package FC27_LE_Turbo_2.12.2.zip (md5 0729184a41857317f216c60477377ba9). Turbo.dll md5 5657d8e51edd77fc338c2027b1f94345. Supported game build: 6AC07E31 only. Tested with Live Editor 27.1.5 on LEICESTER only.

- **Folder picker:** a double-click opens a folder; two clicks within 0.8 s count as one double-click.
- **Managers:** hair and facial hair galleries, as players have.
- **Kits:** one-click 3D kit opt-in.

Proven in game (test career LEICESTER, never saved, 2026-10-17, Live Editor 27.1.5): Turbo connects (141 game hooks); live standings edit (Pts +1) is written, the game's standings view re-reads it, and Undo puts it back; the manager hair and facial-hair galleries open with pictures; a double-click opens a folder in the picker.
Not proven in game: the "two clicks within 0.8 s" fallback of the picker (synthetic clicks only highlighted the folder); the one-click 3D kit button (shown only while 3D kits are off). Not re-run, code unchanged: player editor, tactic presets, kits apply, faces picker, season pack, match.

## 2.12.1

**More sliders and set pieces in tactic presets - offline-tested only, NOT seen in game** (one-launch checklist: `turbo_dev\research_20261016\P2121_PROOF.md`; the full table: `turbo_dev\research_20261016\SLIDER_MAP_FULL.md`; check_all green: build 0 warnings, Lua 418/0, native 33,591/0, catalog all 0 failed).

- Fix: the Load modal printed `turbo\turbo_settings_apply.exe` with the `\t` shown as a tab; the path is now spelled with a double backslash (and a test checks the modal has no tab). The role / focus allow-list is unchanged and stays strict (in game the pair 22 / 6 was refused, 38210 worked).

- **All 125 bytes of the EA Custom block are mapped** (`slider_map_6AC07E31.json`, regenerated by `tools/gen_slider_map.py`): User and CPU sprint speed, acceleration, shot / pass / header error and accuracy, shot speed, pass speed, finesse / chip / low driven / power shot error and speed, through / lob / cross / lobbed-through pass error and speed, first touch control, intercept control, tackle accuracy, injury frequency and severity, goalkeeper ability and ball deflection control, outfield ball deflection control, dribble touch intervals, jockey speeds, physicality impact, the 7 team positioning sliders (marking, line length, line width, line height, run frequency, fullback positioning, GK ability), the 32 CPU Sliders (defending aggression, tackle frequencies, shot-type frequencies ...), the 4 group sliders and the power bar. The order comes from the game's own slider table in FC27.exe (build 6AB9813C) and fills the block exactly. **Only 3 of them are seen on the EA Custom screen** (user sprint, user acceleration, CPU sprint); the other 122 are marked `evidence: table` (mapped, not yet seen). Dribble error and pass angle error have no slot in the block and cannot be set.
- **Tactic presets stage them**: a preset saves every mapped value and carries `include_unverified` (on by default in this test build; untick "unverified sliders" to stage only the 3 proven ones). `settings_pending.json` lists the table-mapped ids under `unverified`; `turbo_settings_apply.exe` writes a table-mapped byte only when it is listed. Unchanged: the effect of the sliders on play is NOT proven (G-S3 failed in 2.10).
- **Set pieces in tactic presets**: captain, penalty taker, free-kick takers (left / right / long), corner takers, throw-in takers, the 9 corner supports and the 3 pre-set substitutions of your club's team sheet (24 `cm_teamsheets` columns). New Lua feature `tactic_setpieces` (get / set / undo): refuses unknown columns, players that are not on your sheet, another club's preset; writes an undo journal first, reads every field back, puts everything back on any mismatch. The window sends it on Load and Undo and reads `tactic_setpieces_current.json` for Save current ("Read set pieces"). **Not proven:** that the running game shows the new takers without a career save and load, and that a match uses them.
- Not possible, and why: width / chance creation / attacking and defensive style (columns exist only in the generic `mentalities` table, not in the career team sheet `cm_mentalities`); per-slot instructions beyond role + focus (no such field: the unread slot bytes are undecoded); the formation, slot positions and offsets, the tactic name / code / preset id (no proven writer); the starting XI and bench order (not guarded against an invalid sheet); Build-Up Style 2 / 3 and Defensive Approach 3 (labels unknown).

## 2.12.0

**Tactic presets - offline-tested only, NOT yet seen in a played match** (checklist: `turbo_dev
esearch_20261016\TACTIC_PRESETS_PROOF.md`; native suite 33,097/0, Lua 408/0).

- **Tactic presets** (Teams > Tactic presets, also in the Simple view). Saves EA's OWN tactic of your club (Defensive Approach, Line Height = depth, Build-Up Style, role id and focus of the 11 slots; the formation is recorded) and the Custom sliders block of the Settings file as `turbo_output\tactic_presets\<name>.json` (schema 1, build 6AC07E31-2145C000 only, out-of-range values refused). Load asks first, writes the tactic through the Tactics card's guarded channel E job (live record + its cm_mentalities row, `channel_e_undo.json`) and stages only the sliders verified in game (`settings_pending.json`, applied with FC 27 closed by `turbo_settings_apply.exe`, needs Gameplay Type CUSTOM). Another formation is refused (no formation writer is proven). Width is not part of a preset (the game's team sheet carries none). Undo last load, and a one-click Re-apply prompt after a career load (never automatic; own file `tactic_preset_last_load.json`, `reapply_edits.json` is never touched). Proof checklist: `turbo_dev\research_20261016\TACTIC_PRESETS_PROOF.md`.

Limits, plainly:
- The tactic half loads live in the career hub with Team Management closed (the Tactics card's guarded channel E job).
- The slider half is only staged; it is applied with the game closed (`turbo_settings_apply.exe --apply`) and needs Gameplay Type CUSTOM in the EA menu.
- EA's Edit Tactic screen may still show the old wording although the live record holds the preset. Never answer Yes on it (it autosaves).
- After each career load, load the tactic again (the panel offers a one-click Re-apply, never automatic).
- Build-Up Style 2 / 3 and Defensive Approach 3 (Aggressive) stay refused; another formation is refused.

## 2.11.0

Integration build of the 2.10 side branches. Everything below is **offline-tested only** unless it says "Seen in game". Offline suites: build 0 warnings, Lua 405/0, native 32,920/0, Python 40+22+568 ok, catalog 840/0. Evidence for the blocked tactics route and the L6 delivery demo: `turbo_dev\evidence6-10-16\` (`BRANCH_A_RESULT.md`, `L6_RESULT.md`).

### NOT in this release
- **Tactics presets are NOT in this release.** There is no tactics preset UI and none is offered. The load-time tactics route is blocked: the game exits at the menu (about 5 s after Play Offline, no crash dump) when the defensive-line or pressing attribulator groups are patched (line length, line width, partial-team press; attacking line width exited the same way in L5). Hook delivery was fine each time, so the cause is the game, not delivery. No tactics knob reached a match, so nothing is proven to move the pitch and nothing is proven dead. See `BRANCH_A_RESULT.md`. The existing Tactics tab and live-tactic (channel E) controls are unchanged from 2.10.0: no on-pitch effect proven, do not rely on them.
- **L6 (soccer ball radius) is a delivery demo only, not a user feature.** On 2026-10-16 (LEICESTER test career, never saved) the opt-in attrib preset made the ball about 3x its normal size on the pitch, so the load-time EBX patch route reaches a career match for the physics group. Turbo ships no ball-size preset and no UI for it.
- Game settings sliders: effect on match play still NOT PROVEN (gate G-S3 failed in 2.10.0).

### Fixed (offline-tested only, NOT yet seen in game)
- **Season pack Undo did not move players back (seen in game 2026-10-16).** Live Editor 27.1.4's TransferPlayer logs "Can't transfer player from 1 to 1" and returns normally when it looks the club up itself for a player moved earlier the same session; player_moves counted that call as done. Now the player's club from the database is passed as from_teamid, the club link is read back after every transfer or loan, Turbo's own move is tried when Live Editor left him where he was, and a move nobody did is reported as a failed action (per-row reason in the summary), so the Undo record is kept and the window says what failed. Real-handler test: `turbo/tests/t28_pack_undo.lua`.

### Added (offline-tested, not proven in game as a combination)
- **Shared Oodle decompress hook.** The 3D kit swap and the attribulator EBX patch share one detour, so both can be on together. `kits3d_on.txt` no longer switches the attrib patch off. The combined kits3d + attrib path has NOT been run in game.
- **Second copy ("also") per attrib preset entry** (for assets that also exist under databases/simulation) with a per-location write log.
- **One-click installer with rollback** (`Install-Turbo.cmd/.ps1`, `Undo-Turbo.cmd/.ps1`); round-trip tested on a fake folder only, never on a real install.
- **Simple / Advanced view** toggle (`gui.advanced_mode`); panels not proven in game sit under Advanced with a banner (`docs/GUI_AUDIT.md`).
- **3D hair and beard picker** shows the game's own preview pictures.
- **Season update pack importer** (CM Tracker CSV library only): preview with rejected rows, send in 50-row pages with confirmation, undo; players on loan are refused.
- **UNPROVEN (K1, offline tests only):** Teams > Colours kit font pickers (name font, number font, shorts number font, name layout, outline width). Labels are the game's own folder names from its bundle manifests (88 name fonts, 137 number fonts; the folder number being the database id is an inference) plus "worn by" clubs from the live teamkits. Kept for every career load like the colours. Not shown: any font sample. Not proven in game: that the names match the ids, and that a font change shows in a match.
- Tools: `tools/gate_runner.py` (one-command gameplay-preset gate; dry-run tested, live path unproven), `tools/g3_deploy.ps1` (no longer refuses when `kits3d_on.txt` exists), `tools/kit_fonts_manifest.py`.

### Version
- Turbo and the GUI report 2.11.0.

## 2.10.0 (2026-10-15)

Seen in game 2026-10-15 on build 6AC07E31 (test career LEICESTER, never saved). Evidence: `turbo_dev\evidence6-10-15\TEST_31.md`, `launchA210\` and `launchB210\`. Only the three items under "Seen in game" are claimed; everything else is offline-tested only.

### Seen in game
- **Load-time gameplay-data patch delivery (opt-in).** Turbo's Oodle hook patches the attribulator EBX (the game's gameplay tuning data) while the game loads it, and the live attribulator database read back the patched values in two launches. The patch is opt-in through `turbo_output\attrib_preset.json` (without that file nothing is patched) and is skipped when `turbo_output\kits3d_on.txt` exists (the 3D kit swap owns the Oodle hook). This proves delivery of the values only, not any effect on the pitch.
- **Game settings.** Turbo writes Sprint Speed User, Acceleration User and Sprint Speed CPU into the Custom block of the game's Settings file, and EA's Custom screen shows them. To see them the user must pick Gameplay Type CUSTOM in the EA menu. The other sliders are read-only.
- **Safe undo.** The settings undo refuses until a game-written Settings file has been copied to evidence, so a settings undo can no longer overwrite what the game wrote.

### NOT PROVEN
- Any slider's effect on match play (sprint speed, acceleration, CPU sprint speed). Gate G-S3 FAILED: with sprint set to 0/0 the measured p95 sprint speed was 0.97 / 0.985 of vanilla, so the values show on the EA Custom screen but no change on the pitch was measured.
- **Tactics do not work in this release.** The Tactics tab and its live-tactic (channel E) controls exist, but no on-pitch effect was proven and nothing is claimed. Do not rely on them.
- Running Turbo alongside the FIFA Editor Tool or community gameplay mods: untested. They patch the same attribulator (attribdb) partitions that Turbo's opt-in preset patches, so combining them may conflict.
- The attrib preset is an inert opt-in, off by default (no file, no patch). Delivery was proven, effect was not.
- Tactics effect: NOT PROVEN. L5 (preset length 0.5 / width 0.6) exited at the main menu with no crash dump; cause unknown; re-test next session with half-step values. Do not use these values. The attrib preset stays OFF unless turbo_output\attrib_preset.json is added.
- Settings-live (changing the settings while the game runs) is not included.
- Only 6AC07E31 is supported; other game builds, other careers and long sessions were not tested.

### Version
- Turbo and the GUI report 2.10.0.

## 2.8.0 (2026-10-15)

Seen in game 2026-10-15 on build 6AC07E31 (test career LEICESTER, never saved). Everything not listed as seen is offline-tested only. Offline suites: build 0 warnings, Lua 405/0, native 32,057/0, Python 40+22+488 ok, catalog all pass (kitswap 193, lualoader 168, faces3d 146).

**Installed-build verification (2026-10-15 19:1x-19:50, LEICESTER test career, never saved):** picker Apply writes the override, the game serves it on the next start (header ok, 18 substitutions, Chilwell shows generic hair 155 + long beard 46); Follow entry + Preview -> Revert -> Preview -> Keep all behaved correctly on the fixed build 877b33a2 (originals 696/259/0 restored every time, backup file correct). Still not seen: the fixed look after a restart following Keep, match close-ups, other ids/heads/builds.

### In short
- **NEW: 3D hair and beard on real scanned faces (opt-in).** Players > Appearance > "3D hair and beard": enrol a scanned player, pick a generic hair and/or beard, with a live preview. The first time a player is enrolled the game needs one restart; after that, changes made in the picker show in the capture preview without a restart.
  - **Seen in game (build 6AC07E31, LEICESTER):** Chilwell, Evans and Coulibaly in Squad Hub and Edit Players; an 8-minute match stayed stable; a deliberately broken (syntax-error) override fell back to the game's own file; the capture preview updated without a restart (hair 155 / 696 and beard 46 were seen).
  - **Fixed after the first installed-build test:** after Revert the picker showed a bogus "A preview of (id) was not reverted: original nothing" banner with no name, and a second Preview no longer recorded his original codes in `turbo_output\faces3d\preview_backup.json`, so Revert / Keep could not put them back. Revert now removes his record completely (the file goes when it is empty), a Preview always records his originals, and the banner appears only for a real leftover (originals recorded and his codes still the previewed ones). The earlier sighting above covers the capture refresh only.
  - Verified in game on the installed build: picker Preview -> Revert -> Preview -> Keep (originals restored, backup file correct at each step).
  - **Not proven:** any other player id, in-match close-ups, any other game build (the feature switches itself off on every build except 6AC07E31), any other career, long sessions.
- **How it works:** the game compiles `player_kit.lua` with Lua's `lua_load`; Turbo installs one hook on that call and serves a substituted `player_kit.lua` for that file only. The stock file is captured from your own game (`turbo_output\faces3d\stock_player_kit.cache`) and is never bundled with Turbo. On any mismatch (stock md5, anchor lines, build key) or compile error the game's own file is served.
- **Kits (carried from 2.7.x, `799919e..2.7/kits-next`):** the status text now says plainly one badge (zip baked in) versus two (decals off) and the two-badge case is logged; the hotspot patch (name / number placement) is applied only for "Show" slots, never for "Watch only"; the crest option label says what it does. Offline-tested only; the kit results from 2.7.0 stand as written there.
- **Not in this release:** game-settings presets (parked), tactics (no tuning slider; the tactics overhaul is not done).

### Switches
- Opt-in: `turbo_output\faces3d_on.txt` (written by the picker when you Apply). Without it the hook is not installed and nothing is scanned or logged.
- Kill switches (any one present = every call passes through untouched, re-read every 250 ms): `turbo_output\faces3d_off.txt`, `turbo_output\hook_lua_load_off.txt`, `turbo_output\game_hooks_off.txt`.
- Developer opt-ins (not needed for the picker): `lua_loader_on.txt` (log only), `lua_loader_substitute.txt` (with the first).
- A `player_kit.lua` under `mods\legacy\data\fifarna\lua\assets\` is a conflict: Turbo warns and refuses to substitute.

### Uninstall
Press Remove in the picker, or delete `turbo_output\faces3d_on.txt` (and `turbo_output\lua_override\player_kit.lua`) and restart the game. Your choices stay in `turbo_output\faces3d\players.json`.

## 2.7.0 (2026-10-14)

Seen in game 2026-10-14 on build 6AC07E31 (test career LEICESTER, never saved, save files byte-identical afterwards; `turbo_dev\evidence6-10-14\TEST_29.md`). Offline suites: build 0 warnings, Lua 405/0, native 31,992/0, Python and catalog all pass.

### In short
- **3D kits, seen in game for one club (Leicester City), Home and Goalkeeper slots.** The zip's kit shows on the players in a played match, and **the zip's shirt names and numbers show on the back** (names and numbers were read on the release candidate; moving the number rectangle in the zip's file visibly shrank the number of the same player, so the zip's placement is what the game uses). On the final 2.7.0 binary the Leicester players' zip kit on the pitch and the served hotspot assets were seen (log). The goalkeeper's pink zip kit was seen, **not compared against the game's own goalkeeper kit**.
- **Not verified:** the away kit worn in a match (the game loaded and replaced its textures, but Leicester played in its home kit), the kit select screen's 3D preview, any other club, the new "keep the game's club badge" default (what the chest badge looks like was not checked), sharing within a club, the third and fourth slots (the game has no 3D third kit for Leicester). These are offline-tested only.
- **Still no tuning slider and the tactics overhaul is not done.** The slider pipeline (proof promotion, live apply, style presets) is in but no knob has both proofs, so the page is as in 2.6.0 (the style presets are greyed and empty). Press-cycle check this release: the chain was verified but the press never triggered in 45 s (inconclusive).

### Details (everything below is offline-tested only unless the lines above say it was seen in game)

### 3D kits: every slot, any club (the goalkeeper slot is verified in game for Leicester; away and other clubs are offline-tested only)
- **Away, goalkeeper, third and fourth kits** use the same path as the home kit, for any club: the slot comes from the game's kit folder names (`home_0_0`, `away_1_0`, `goalie_home_2_0`, `third_3_0`, `fourth_4_0` and the `loc2_*_4x_0` variants of the same slot). Each imported kit has its own controls and its own saved choices (`swap_state.json` version 2; a version 1 file still loads).
- **Keep the game's club badge** (default on, per kit): the zip's `crest_*` / `crest_shorts_*` pictures are not served, so a blank crest decal in the zip does not remove the game's dynamic badge. Untick it to serve the zip's crest pictures as in 2.6.0. The sponsor (`brand_*`) pictures follow "Also use the zip's badge and brand pictures".
- **Also change this club's other kits that share a texture** (default off, per kit): big clubs let several kits use one texture chunk; off, such a texture stays the game's, on it is served and the list says which other kits show it too. A texture another club uses is never served.
- **Which kit folder the match loads:** the club's other kit folders are watched (never replaced); `turbo_output\kit_swap_log.txt` has a "KIT FOLDERS THE GAME LOADED" block (per folder: textures and blocks decompressed).
- **The reading of the game's kit data is cheaper and obeys the switches:** the index is saved in `turbo_output\kits\kit_index.cache` (valid until the game's tocs change), it runs below normal priority, it never runs without `kits3d_on.txt`, and `kits3d_off.txt` stops it (also while it runs).

### 3D kit hotspots (track kits-numbers)
- **3D kit: the zip's name / number / sleeve-badge placement (hotspots), verified in game for Leicester Home and Goalkeeper.** Each kit has one small hotspot asset in the game (`.../hotspots_<team>_<type>_<var>`, 25 rectangles in jersey UV space). With the 3D kit switched on, Turbo now also overwrites the rectangles the zip names (back number, name, name arc, shorts number, sleeve badges, centre badge) in that asset when the game decompresses it, through the same Oodle hook and with the same kill switches. A zip point where the game has a rectangle (the zip's front number) never replaces the game's rectangle; the zip's "not used" team / brand badge follows the "use the zip's badge and brand pictures" option. New option "Also use the zip's name and number placement (hotspots)" (default on). The log lists every patched rectangle (old and new values) and, per item, the clock time of the first and last block the game loaded (shows which screen loaded what, for the kit select question). Default OFF behaviour is unchanged: no opt-in file, no kit on = no hook, no write.

### Tactics and gameplay tuning (track tactics-effect)
- **Proof promotion:** `tools/promote_proof.py` turns an evidence folder's verdicts (battery `result.json` or a `verdicts.json`) into the knobs file's proof block, only for a knob with engine-read PASS and visible-effect PASS; dry run with a diff by default, `--apply` to write. `scripts/knobs_guard.py` also refuses `live` without `applies: live`, `applies: live` without a proof and a candidate with a proof, slider, live or verified.
- **Gameplay tuning page (only when a knob is proven):** each slider row says when it applies (now (live) / next match (latched at kick-off) / not measured), always reads back the engine's value, and a knob proven live gets "Apply now" (row) and "Apply now (live)" (Changes block) that write during a match with the owner rule, read-back and rollback. `turbo_output	uning_live_off.txt` stops every write in a match. With no proven knob the page is as in 2.6.0.
- **Turbo mixed styles:** High press, Low block, Wide, Narrow, Counter, Possession; each holds only the knobs proven in the knobs file (empty and greyed until a knob is promoted). An absolute preset value outside a knob's range is brought into it and the page says so.

## 2.6.0 (2026-10-14)

Verified in game 2026-10-14 (test career LEICESTER, never saved, save files byte-identical afterwards; `turbo_dev\evidence\2026-10-14\TEST_28.md`), build 6AC07E31. Everything not listed as verified passes the offline suites only (build 0 warnings, Lua 405/0, native 31,856/0, Python 463, catalog 0 failed).

### In short
- **3D kit on the pitch (opt-in, EXPERIMENTAL): verified in game for one club and one slot.** Leicester City's Home kit from a FIFA Kit Creator zip showed on the players in a played match (lighter blue with the zip's white sponsor); with the kill switch the game's own kit showed, same fixture, same camera. **Not checked:** the kit select screen's 3D preview, shirt numbers and name placement (the zip's hotspots are not applied), away / third / goalkeeper kits, other clubs. 28 of the club's 60 textures stay the game's (shared with other clubs, byte-identical to another kit's block, too small to recognise safely, or not in the zip); the other 32 are served. Off by default: it needs the empty file `turbo_output\kits3d_on.txt` AND a kit switched on in Teams > Kits; only then is one hook installed on the game's Oodle decompressor (`OodleLZ_Decompress`). Switches: `kits3d_on.txt` (opt-in), `kits3d_off.txt` / `hook_kit_oodle_off.txt` / `game_hooks_off.txt` (no hook). Note: `kits3d_off.txt` keeps the hook out but the one-off background read of the game's kit data (about 30 s) still runs while a kit is switched on; set the kit to Off in the panel to stop that too. 2.4.0 installed no new hook; 2.6.0 adds this one only after the opt-in.
- **Gameplay tuning page fixed and checked on the real overlay:** the search box narrows the list (label, id, group, category, field); "Different from FC 27" counts the game and your slider edits only, and the profile's differences are their own line; presets save / duplicate / delete verified; a collapsed, read-only "Experimental candidates" list (101 unverified knobs, no sliders). A preset named like one of Turbo's own can no longer replace Turbo's file (checked: the file stayed unchanged; the refusal message was not seen). Not checked on the overlay: preset rename and import.
- **Still no tuning slider.** A slider needs an engine-read PASS and a visible effect on a played match. This release found one more engine-read PASS (the defensive line range `shape.def_line_range_min/max`, written in the hub, is the value the match constructor copies) but no visible-effect measurement, so every knob stays read-only. The club tactic (channel E) from 2.4.0 was checked again on this build: Apply High / Line Height 73 / Counter read back, still in place 5 s later, and the match's own copy at kick-off held `LH 73 BU 1 DA 2 roles 11/11 = Turbo`.
- Tools only (not in the install zip): a sharper knob check battery that writes every database copy, and per-knob engine-read checks.

### Also in 2.6.0: the 2.5 work (never released on its own; seen on the real overlay in TEST_27 / TEST_28)
- **Competitions > Gameplay tuning is one page:** search, categories, badges (verified / candidate), "Active preset and profile", "Presets", "Changes", "Knobs"; presets can be duplicated, renamed and deleted (Turbo's own presets are read-only); starter presets Turbo compact / expansive / narrow-long / wide-short (they name FC 27's values and relative changes only).
- **Knobs catalogue v5: 124 knobs, 36 locators** (2.4.0 had 23 knobs, 8 locators). Ten are verified in the running game (found, identity-checked, read back); 114 are candidates (listed read-only, never written). At a career load Turbo now tries 36 locators instead of 8 (read-only): the log line `tuning: chain locate ok` shows the time (about 6 s on the first use).
- **Teams > Tactics (My team):** the channel E card is on top ("Edit the live tactic"); the other rows are under "Experimental (preview-only and untested settings)".

### Known issue (not new in 2.6.0)
- Turbo's crash guard (`turbo_output	urbo_gui_start.flag`) stays set from the first drawn frame until 120 overlay frames have been drawn. If you play two sessions without ever opening Turbo's window, the guard reads it as two crashes and switches Turbo off at the next start (log: "Turbo GUI stays off"); delete that file to bring Turbo back. The code is unchanged since 2.3.

### Kits: the staged FIFA Kit Creator textures are served to the game (phase 2 of the 2.3 kit import; EXPERIMENTAL, off by default: needs the empty file turbo_output\kits3d_on.txt, without it no hook is installed and the controls are hidden; verified in game for Leicester Home, see above)
- **Teams > Kits, under "Kits imported for this club": "3D kit on the pitch: Off / Watch only / Show it on the pitch".** Turbo hooks the game's Oodle decompressor (`OodleLZ_Decompress` in `oo2core_9_win64.dll`, the DLL FC27.exe imports) the first time a kit is switched on (never for a player who does not use it). FC 27 stores every kit texture as a chunk of Oodle blocks; Turbo recognises the blocks of the club's kit textures by compressed size, raw size, first bytes and a 64-bit hash, and in "Show it" mode overwrites the decompressed output with the staged textures' bytes (same size, format and mip chain as the game's own: checked against the game's texture headers before anything is used). "Watch only" changes nothing and only counts which kit blocks the game loads (`turbo_output\kit_swap_log.txt`).
- **Safety rules:** a texture is swapped only when every use of its data in the game is this club's same kit slot (the home kit and its `loc2_home_40_0` variant are both served); data shared with other clubs (for example the 119-club brand normal map), blocks under 1 KB (uniform fill), textures with a byte-identical block elsewhere, textures the zip does not carry, and weave arrays stay the game's own. A texture is all-or-nothing. Kill switches: `turbo_output\kits3d_off.txt` (feature, within 2 s), `turbo_output\hook_kit_oodle_off.txt`, `game_hooks_off.txt`. Undo of the kit import switches the swap off. The choice is kept across game starts (`turbo_output\kits\swap_state.json`).
- Takes effect when the game next loads the kit (the next match load; after a game restart when the kit was already loaded in this session). Nothing is written to the game's files or to the save.
- New offline tool `turbo	ools\kits3d\kit_chunks.py` (read-only): resolves a club's kit textures to their chunks, headers and blocks in every toc and lists what is shared.

## 2.4.0 (2026-10-14)

Verified in game 2026-10-14 (test career LEICESTER, never saved; `turbo_dev\evidence\2026-10-13\TEST_26.md`, screenshots in `evidence\2026-10-14`), build 6AC07E31:

### Club tactic (channel E): one write, and the game's own screens show it
- **Verified in game:** Apply in Teams > Tactics > My team (High, Line Height 70, Counter) wrote the live record and the `cm_mentalities` row in one job and read both back; at kick-off the match's own copy of your club's tactic held exactly those values (`LH 70 BU 1 DA 2 roles 11/11 = Turbo`). With no Apply, the match held the game's own values (`LH 25 BU 0 DA 0 = game`). Edit Tactic (Squad > Team Management > Edit Active Tactic) loads from that same record (sentinel test: it showed the sentinel written into the map record), so what Turbo wrote is what Edit Tactic shows.
- **Behaviour change:** Turbo no longer writes Line Height above 90 or Aggressive (Build-Up Style 2 / 3 and Defensive Approach 3 stay refused); the game's own 95 / Aggressive is kept.
- Still not available: the live tactic is not part of an in-game save made after the write (Turbo says so in the panel after a career reload).

### Gameplay tuning: found through the game's attribute manager; your tuning profile
- The tables are found through the game's attribute manager (the name search stays as the first-run cross-check and the fallback); every database copy of a group is written together and once. Nothing is searched unless you use the tab or your profile.
- Your tuning profile (`turbo_output	uning_profile.json`) replaces "write again at every career load": while on it is applied at every career load and each time you enter the hub (never in the 5 s before kick-off, never in Team Management). **Verified in game** on three launches: `profile applied (career_load)` and `(hub)`, each read back. A third moment at the main menu exists in the code but needs Turbo's Lua side to report the main menu, which did not happen in this build: it is **not** claimed. Kill switch for the automatic writes: `turbo_output	uning_profile_off.txt`.
- **Verified in game (engine side):** with the profile's team width 0.60 written at the hub, the match's formation AI read 0.60 (all 12 with-ball samples fell in the predicted band, control OK; the engine's instance is the one Turbo writes). Whether a value changed during a match takes effect is not known, and the size of the effect on the pitch has not been measured: **every knob is shown read-only** ("located, effect not proven"); a slider appears only once an effect is measured in a played match.
- Restore FC 27 defaults puts FC 27's own values back and switches the profile off. Turbo's own presets: FC 27 default, Turbo compact, Turbo expansive. `tuning_keep.json` is no longer read.

### Removed: tactics channel A and the legacy tactic fields (2.4 cleanup; offline suites only, nothing here needs a game check)
- **Per-side gameplay sliders (tactics channel A) are gone.** The 68 game variables `GAMEPLAY_CUSTOMIZATION/<slider>_USER` / `_CPU` (sprint speed, pass error, line height, CPU behaviour and the rest) were tested in a played match on 2026-10-11: the game stores the value and reads it back, but the match ignores it (`turbo_output\TACTICS_CHANNEL_A_REPORT.txt`). Removed: their 66 rows in Teams > Tactics (Match scope, the "Per side" groups), the per-side table and the two A/B presets ("baseline", "CPU handicap extremes") in Competitions > Match setup, and the names from the list of game variables Turbo knows. Turbo now refuses to set any game variable that is not on that list. A preset saved by 2.1 - 2.3 that still holds one of these settings loads without it (nothing is ever sent for it).
- **Kept as they were:** Match setup's fixtures (swap home and away, another opponent), fixed results and its nine game-variable switches (injury frequency and severity, weather and CPU makes no substitutions were seen working in played matches; injuries off, time of day and difficulty as before, not seen to work), and the same variables' rows in the Tactics tab. `call_gamevar_off.txt` stops every game variable (Match setup and the Tactics tab), `tactics_gamevar_off.txt` only the Tactics tab's game-variable rows, `call_match_fix_off.txt` the fixed results.
- **The legacy tactic fields are no longer written.** The rows that wrote the old `mentalities` table (players in the box for crosses, corners and free kicks, chance creation, attacking style, defensive style: controls EA removed in FC 25) are gone. Defensive and attacking width stay as Turbo's preview only (the pitch's width band and the opposition card use them; nothing is written), still behind "Show legacy fields". `turbo_output\tactics_legacy_off.txt` is no longer read. A value a 2.1 - 2.3 Turbo wrote into that table is still put back when `tactics_off.txt` appears.
- **Not changed by this cleanup:** My team's `teams.defensivedepth` / `teams.buildupplay` rows. Your live tactic's `cm_mentalities` copy (channel E) belongs to the 2.4 club-tactic work: there the copy is no longer a separate opt-in (`channel_e_db_mirror_on.txt` is gone), the row is written in the same job as the live record, and `turbo_output\channel_e_db_off.txt` leaves it out (2.3's `channel_e_db_mirror_off.txt` still works). That part is pending its in-game proof and gets its own entry with the club-tactic changes.
- **Tests:** suites green on this branch alone (build 0 warnings, Lua 405/0, native 28,849/0). The native count drops by 9,613 from 2.3.0's 38,462: 11 cases removed (2,045 checks), 5 added (282: channel A stays gone, the nine kept game variables pinned, the kept game-variable rows still blocked by their kill switches, 2.1-2.3 mentalities rows still put back by `tactics_off.txt`, the teams row case), and 16 cases that loop over every slider or every known variable lost the checks of the removed rows (7,850; 6,015 of them in "sliders: clamp_slider", which checks every value of every slider). The case-by-case list is in `turbo_output\assessment_20261013\IMPL_cleanup.md`.

### Docs
- `docs/turbo-reference.md` brought up to 2.4 (its title still said 0.2.5): a module map, build and test with today's commands and counts, the tactics section without channel A, every kill switch as Turbo reads it today (`call_team_sheet_on/off.txt` are channel E's switches since 2.2, not "reserved"), and the planned 2.4 tactics design (marked as planned, pending its in-game proof). New `docs/BUILD_WINDOWS.md` (how to build and test on the PC; `scripts/msys.sh` pointed at it and it did not exist). HANDOVER sections 1, 4, 5 and 8 brought up to date.

## 2.3.0 (create player with a chosen id, archetype with a target overall, 3D face looks, kit zip import phase 1, channel E live tactic fields, engine gameplay tuning)
- **Players > Create player: optional Player ID** (empty = automatic, as before). A typed id is checked while it is typed, again at the click and again by Lua right before the first write: a whole number from 1 to 459,999 (460,000 and up is the game's own range), not the player-career id 31399, no row in players, editedplayernames, teamplayerlinks, playerloans, transfers or the career tables (youth players, growth, awards, match ratings, team of the week, contracts), and (Lua) no entry in your club's squad status table (PlayerStatusManager); a refusal says which table holds the id. With a current asset catalog the box says what the game files ship for the id ("This id has a 3D head and a callname in the game files: the new player will use them"): the new player gets that 3D head (headassetid = his id, class 0, the first shipped variation; only with a current asset gate, else nothing is written; a hidden head is logged in `asset_writes.log`) and the commentary finds the callname recording by his id. **Use a hidden player's id...** lists the ids with a hidden 3D head and / or an own callname that no player of the save has, searchable by the catalog's names (local, never in the repository) or id. Every existing guard of the create stays (opt-in / kill switches of `player_create`, read-back, the squad status entry of the match crash guard); the chosen id is logged (`create_player: player id N (typed / automatic ...)`). Kill switch: `turbo_output\create_player_typed_id_off.txt` (typed ids refused, automatic ids as before). A typed text, fraction or negative id no longer falls back to an automatic id. Verified in game 2026-10-12: id 300123 created by the game at the user's club and shown in its squad, an id in use refused with the table.
- **Archetype with a target overall** (Players > Attributes; verified in game 2026-10-12: Poacher, target 80 -> the game shows Baseline OVR 80): pick a role, the position (his own by default) and a **Target overall 1..99** (default: his overall; a broken OVR-1 overall falls back to the formula's value). The role's profile is scaled until the fitted formula for that position gives exactly the target (the profile's shape kept: a higher profile value never ends below a lower one; the unrounded value nearest the target is chosen); attributes outside the role stay, unless the role alone cannot reach the target because it is clamped at 1 / 99, then they move too and the preview says which (marked *). A target the position cannot have at all (e.g. 1 or 2 for a full-back, 1 for a keeper) is refused with the closest reachable overall and a "Use N" button. **Preview** first (every attribute old -> new, stored overall and formula both shown, the resulting overall), then **Apply** writes the attributes and the stored overall (= the target, so the game shows it at once, as a typed overall does) through the usual validated path (undo steps, kept edits, development plan of your players), reads them back, and **Undo archetype** / Undo bulk puts everything back; a write refused part-way puts back what was written. Another position than his: "Make <pos> his main position" (on by default) writes preferredposition1 too; off, his stored overall follows what his own position's formula moved (the 2.1 follow rule). **Create player** gets the same choice: an archetype makes the new player's 34 attributes the template's reshaped to the Overall box for the chosen position (refused targets disable Create). The keep-the-overall archetype apply now uses the same walk (exact in every synthetic case).
- **Real-face chooser: the 3D looks were classified again, measured, and now say how sure they are.** A hand check of 144 heads (each render looked at one by one, lower face zoomed; ids in `turbogui/data/face3d_looks_check.csv`) found the 1.2.4 table right, in the filter groups the menus offer, for 73% of skin tones, 89% of hair colours, 87% of hair lengths, 79% of hair types, 83% of facial hair, 80% of beard colours and 95% of headwear (mostly fair skin read as olive, combed short hair as slicked, stubble vs clean-shaven). Every cached render (3011 heads: the 2779 of 1.2.4 plus 232 never classified) was classified again by eye from contact sheets against a written rubric and a sheet of labelled example heads, a second time with the lower face zoomed wherever the first pass and the 1.2.4 table disagreed, and the two passes and the old table were combined by vote, with two checks: an exposure-corrected cheek brightness from the render against the skin tone, and an image-embedding probe (a CLIP model already on this PC, nothing downloaded) whose confident contradictions (412 values) went to a third look. Against the same hand check the table is now right for 92% of skin tones, 94% of hair colours, 91% of hair lengths, 97% of hair types, 87% of facial hair, 88% of beard colours and 98% of headwear. Each value carries a confidence (`conf` column: sure / probable / unsure); **an unsure value matches no filter value** (935 of 21077 values, at most 9% of a trait), is marked "?" in the head's tooltip ("3D look: olive? skin, ...") and is counted in the filter menu ("12 heads unsure"), which also shows the trait's hand check ("Hand check of 144 heads: 92% in the right group, 3% unsure"). **Hidden game heads** whose player was rendered while he was still in a save (the head id of a real face is its playerid) now show that render and filter on its 3D look. `scripts/gen_face3d_looks.py` reads the conf column and the hand check, and `--check` tells whether the generated header is up to date; `scripts/face3d_sheets.py` makes the contact sheets (whole heads or the lower face) from `turbo_output\cache\faces3d`. Not yet checked in game.

### Kits: import a FIFA Kit Creator kit zip into a club (phase 1; verified in game 2026-10-13: kit settings, colours, minikit file and staged textures; the 3D kit on the pitch is phase 2)
- **Teams > Kits: "Import kit zip..." and "Import team folder..."** for the zips of fifakitcreator.com (the picker starts in `C:\FC_Tools\My Mods`; a team folder such as `eng\leicester` imports its `h / a / t / g` zips at once, also `kit.zip` files one folder down). The slot (Home, Away, Goalkeeper, Third, Fourth) comes from the file name (`h`, `away`, `ado_den_haag_third`, a `gk` folder) and can be changed. The panel shows the minikit, a jersey thumbnail and the zip's `settings.json` next to the club's current values.
- **Every zip is checked before anything is written**, with the reason when it is refused: the 12 main textures, `minikit.png` and `settings.json` present; every PNG readable; every texture FC 27's size (or 2, 4, 8 times it: scaled down; most zips have 512 decals for the game's 256); the 12 brand / crest decals all there or all absent (absent: the game's own decals stay); `settings.json` values valid; CRC of every file.
- **Apply** (each part with **Undo**, also after a restart: `turbo_output\kits\<club id>\<slot>\import.json`):
  - the `settings.json` fields into the club's teamkits row of that slot: the 11 kit colours through Teams > Colours' writer, plus `teamcolor*percent`, `jerseycollargeometrytype`, `jerseynamelayouttype`, `shortstyle`; fields FC 27's teamkits lacks are listed, not written. They are **kept for every career load** like the kit colours (kill switch `turbo_output\reapply_off.txt`); the kept-edits store now also keeps these six setting fields (never a key column).
  - `minikit.png` as a custom legacy file (`mods\legacy\data\ui\imgAssets\kits\j<type>_<club id>_0.dds`) in the game file's own size, pixel format and mips, the crest writer's way; the earlier file is copied to `turbo_output\kit_backups`. Apply waits until the game's minikit has been read (so its format is known); a slot the game has no minikit for is skipped with the reason.
  - the 24 textures converted on a worker thread to FC 27's own sizes, formats and full mip chains, read from the game's texture headers (jersey colour 2048 BC1 sRGB, coeff BC1, normal BC5, taa BC7, decals BC3 / BC5) and staged as DX10 DDS with `manifest.json` (game asset path, format, size, checksum) and the zip's `hotspots.hotspot` in `turbo_output\kits\<club id>\<slot>\`. All or nothing; a new import of the slot replaces them. **The game does not read them yet**: the 3D kit on the pitch needs Turbo's in-match texture swap (phase 2, see `turbo_output\KITS_IMPORT_RESEARCH.txt`).
  - A second import of the same slot keeps the first one's "before" values, so Undo always goes back to the game's kit. Undo puts back only fields that still hold the imported value.
- **Fix: kit type names in Teams > Colours and the kept-edits lines** follow the game's own numbers: 2 = Goalkeeper, 3 = Third, 4 = Fourth (2.2 and earlier said 2 = Third, 3 = Goalkeeper home). Evidence: FC 27's kit folders `goalie_home_2_0` / `third_3_0` / `fourth_4_0` and the minikits `j2_*` / `j3_*`.

### Tactics: your live tactic writes the real fields, and Edit Tactic's own copy (channel E, opt-in, experimental; in game 2026-10-13: the three fields are written and read back, but the game's Edit Tactic screen still shows its own copy (BALANCED / DEEP), and whether the match uses them is not proven)
- **The right fields.** The 2026-10-12 decode of the game's own Edit Tactic save showed that the live record keeps **Line Height** at +0xA4 (1..100), **Build-Up Style** at +0xB0 and **Defensive Approach** at +0xB4; 2.2 never wrote the approach (Edit Tactic kept showing DEEP). The three are now written together in one job, read back and undone together.
- **The game's words.** Teams > Tactics > My team: Defensive Approach (Deep, Balanced, High, Aggressive), a Line Height slider 1..100 with a box for the exact number, Build-Up Style (Balanced, Counter) instead of raw buildupplay / defensivedepth numbers. Picking an approach puts in its default Line Height (Deep 25 and Balanced 50 as Edit Tactic showed them, High 70 and Aggressive 95 from EA's notes); you can change it, and a line height outside the approach's band is written as set, with a note. **Short Passing is not offered**: its stored value (2 or 3) is not decoded yet, so Build-Up Style 2 and 3 are refused, never guessed (a 2 or 3 the game put into the live record is kept there and never copied into an Edit Tactic copy that holds another value). Deep / Balanced and Balanced / Counter were seen in Edit Tactic; High and Aggressive follow the game's band order (not yet seen).
- **Edit Tactic's working copy.** Edit Tactic reads and writes its own copies of the tactic inside the team sheet service, not the record the match copies. In the same game-thread job Turbo now writes the same fields into every working copy whose tactic code is the live record's (checked: code, active flag, formation ids, 11 sane slots holding the live record's 11 players slot by slot; a copy with other players is left out and named in the log), so Edit Tactic should show Turbo's values. When none holds the tactic, only the live record is written and the card and the log say "Edit Tactic shows its own copy until the next career load". The copies are in the snapshot, the read-back, the undo file and Undo.
- **Safety:** the card says: "If you open Edit Tactic and answer Yes to 'save your changes to your team sheet', the game commits its screen values and AUTOSAVES your career." Turbo never calls a game function for this and never triggers that prompt; writes (and Undo) are refused while Turbo's Lua side reports Team Management open (new: ENTERING / LEAVING_TEAM_MANAGEMENT tracked in `bridge_state.json` "team_mgmt"). A plan whose tactic code changed since Apply (Edit Tactic's Yes) writes nothing.
- **Database copy (opt-in):** Build-Up Style goes to cm_mentalities.buildupplay and Line Height to defensivedepth (the career schema's 1..100); cm_mentalities has no column for the Defensive Approach (said in the log). The row must also carry the live record's tactic code when the save has it, and the log names the row (teamid, mentalityid, sourceformationid, teamsheetid, code).
- **Kick-off check rebuilt:** it runs on Turbo's game_tick listener (game thread, whether or not the Turbo window or the card is drawn; the Turbo window's tick stands in when the hook is not running) 20 s after Lua's phase becomes "match", looks for the match-setup copy of the tactic last read in the hub of this career load (or written by Turbo; the log calls the values Turbo's only while they are exactly what its write left in this load), and dumps each copy (out+0x00..+0x1A0 and the 0x200 bytes after it) to `turbo_output\channel_e_kickoff_dump.bin` with an index `channel_e_kickoff_dump.txt` (where the expected Line Height sits). It always logs its outcome (also "no kick-off check: ..."), looks once more 30 s later when no copy was found, needs the channel E opt-in, stops with `call_team_sheet_off.txt` / `tactics_off.txt` like the writes, and has its own off switch `turbo_output\channel_e_kickoff_off.txt`. Read-only. Every change of Lua's match phase is logged. (2.2's check never logged: it depended on a fresh read of the live record during the match.)
- **Evidence in the log:** the role allow-list line also counts the other clubs' Defensive Approach / Build-Up Style values and their line heights (to decode Short Passing next).
- Undo files of 2.2 still parse (defensivedepth / buildupplay names).

### Gameplay tuning: the engine's own tactics tables, live (opt-in, experimental)
- **Competitions > Gameplay tuning** changes the gameplay engine's tuning tables (the "attribulator" groups gameplay mods edit) in game memory, from inside Turbo; no mod manager. Engine-wide: every club, every match, this game session only (the tables are not in the career save).
- **What can be written (verified on game build 6AC07E31, 2026-10-13):** team width with the ball, centre-back split with the ball, team length with the ball (balanced / possession build-up) — one table the game shares between all 15 of its gameplay databases (Authentic included) — and the four defensive line depth bands (Deep / Balanced / High / Aggressive) of the prematch tactics table. 11 more knobs (zonal defence lines, pressing, marking, run frequency, slider ranges) are shown with FC 27's value but **never written**: "Not located on this build" until a live run proves their tables.
- **How a table is found:** by the game's own type and field names and its database chain (database -> group -> table), the table's name and asset id checked, re-checked inside every write job; FC 27's own values come from the game's data (no gameplay mod's values are shipped).
- **Safety:** opt-in `turbo_output\tuning_on.txt`; kill switches `tuning_off.txt`, `call_attrib_off.txt`, `tactics_off.txt`; career hub only; a value is written only while the table holds FC 27's value or Turbo's last write (another tool's value is left alone); every copy written together or not at all; snapshot, read-back, every knob put back on a mismatch; Undo and Reset (FC 27's value) work with the kill switches present.
- **Proven in game (2026-10-13, LTEST, never saved):** both tables located in the running game (type, name, asset id, database chain); the tab's Apply (team width with the ball 1.00 x -> 1.40 x) and Undo (back to 1.00 x) both read back ok; an earlier live write of the same value during a match read back fine and the match ran on normally. **Not proven yet:** that the match uses the new value (no on-pitch measurement yet).

## 2.2.0 (live standings and match setup on game build 6AC07E31, live tactic write)
Status words below: "verified in game" = seen in a loaded career or a started match on game build 6AC07E31 (throwaway copy of a career, 2026-10-12, never saved); everything else passes the offline suites only (build 0 warnings, Lua 394/0, native 37028/0, Python and catalog suites green).

### Live standings and Match setup work again on game build 6AC07E31 (verified in game)
- **Competitions > Live standings and Match setup no longer stop at** "The game's standings are not reachable: the FCE interface object has another vtable (game update?)" / "The fixtures are not reachable: ...". Turbo compared the engine objects with vtable addresses of the old build (6AB9813C) on every build; it now identifies the FCE interface by its Post function (signature `fce_iface_post`) and the DataManager by the new signature `fce_datamanager_vtable`, and keeps the old addresses for the old build only. A wrong or stale object is still refused. `turbo_gui.log` says how the objects were identified.
- The same old-build addresses also stopped the standings refresh after a table edit (new signature `svm_live_vtable`) and every fixed result in Match setup (the manager is now the object whose vtable holds the hooked HandleMessage).
- **What this newly enables on 6AC07E31**, each with its kill switch and undo:
  - Manual writes into the game's competition engine: Live standings cells and the Advanced counters (one table row), Change result (the fixture's score and both rows), Match setup Swap home and away and Play this opponent. Kill switch `turbo_output\live_standings_write_off.txt`, looked at right before every write and every Undo (the views turn read-only while it exists). Refused while the game is processing a match day (or when that cannot be read). Each write is all or nothing and read back; a failure part-way puts back what was already written, and a block the game changed in the meantime is left alone ("ROLLBACK SKIPPED"). Undo for every one of them (result edits, venue swaps and new opponents have one now too), only while the game still holds what Turbo wrote.
  - The standings refresh after an edit (kill switch `call_standings_refresh_off.txt`; refused while a match day is processed).
  - The automatic result rewrite of a fixed result (kill switch `call_match_fix_off.txt`; both goals read back; the log says when one result hook rewrote and the other left the result as played, or when the other hook is switched off).
- **Read-only fallback:** if `fce_datamanager_vtable` is not found (the DataManager identified by its back-pointer only), both views are read-only with the reason.
- `svm_live_vtable` and `fce_datamanager_vtable` are optional for auto-adapt: a miss on a future game update no longer makes auto-adapt refuse the other hooks.
- **Verified in game (LTEST, Leicester, EFL League One, matchday 1):** all 140 signatures of the 6AC07E31 table found (0 missing), the FCE interface identified "by Post in its vtable slot 4" and the DataManager "vtable 0x14B183AE8 (signature)"; Live standings shows the league (6000 rows, 6300 fixtures), not read-only; Match setup lists the next fixtures, the first one (08.08.2026 17:00 Huddersfield v Leicester City, Carabao Cup) as the game's hub shows it. Not checked in game: a table edit with refresh and Undo, a result fix in a played match.

### Tactics: your club's live tactic (channel E, opt-in, experimental)
- **Teams > Tactics > My team: "Edit the live tactic (channel E)"** (game build 6AC07E31 only) writes **build-up play**, **defensive depth** and, per slot, the **role and its focus** (within the slot's position group) into your club's live tactic record in the game's team sheet service, which the next match setup copies. Values are raw numbers (labels not decoded). The preset and the formation are never written in 2.2.
- **Role and focus:** only (group, role, focus) combinations that other clubs' tactics in the same career use are offered and accepted ("role allow-list: N triples from M teams" in the log); without that list only build-up and depth are editable.
- **How it writes:** in place, in one job on the game thread, with no game function called and nothing allocated; the job finds the record again, checks your club, the career load and the old values, takes a snapshot, writes, reads every field back and puts everything back on a mismatch. Career hub only (never while a match is set up or played, never during a save), only for the career load it was located in. A service found by the memory scan is used only if it passes its checks again 5 s later.
- **Database copy:** off unless `turbo_output\channel_e_db_mirror_on.txt` exists (your in-game save writes the live record into the career database anyway).
- **Switches:** opt-in `call_team_sheet_on.txt` (the card's "Channel E on" checkbox); kill switches `call_team_sheet_off.txt`, `tactics_off.txt`, `call_team_sheet_find_off.txt` (no registry lookup), `channel_e_db_mirror_off.txt`, `game_hooks_off.txt`. **Undo** puts back the values Turbo replaced (`turbo_output\channel_e_undo.json`), only in the same career load and only for fields that still hold Turbo's value; it works while the kill switches exist.
- **Verified in game (LTEST, Leicester):** read-out "433", depth 25, build-up 0; Apply build-up 0->2, depth 25->75, ST (slot 9) role word 38213->38210 (focus 5->2) "read back ok" [job 822 us]; still in place 5 s later and after opening and leaving Edit Tactic without changes; Undo "read back ok" back to 25 / 0 / 38213; re-apply; **the match uses the written role**: after kick-off both match-setup copies of the match (team 95, formation 27, the same 11 players) hold role word 38210 for the ST.
- **Not verified / known limit:** the game's **Edit Tactic screen does not show** the written build-up and depth (it showed Build-Up BALANCED, Defensive Approach DEEP while the live record held 2 / 75), so it reads another copy; whether the match uses the written build-up and depth is not known yet. Role and focus labels are not decoded. Badges stay "untested".

### Safety
- No new hook. Every new write is opt-in or gated, logged in `turbo_gui.log`, read back, and has an Undo and a kill switch. Turbo never saves the career.
- A future title update: channel E's 11 signatures are exact-build (never adapted: channel E stays off) and the two standings signatures are optional for auto-adapt, so neither can make Turbo refuse the other game hooks.

## 2.1.0 (game asset catalog, commentary language detection, UX, tactics honesty, FC 26 parity, crash guard hardening)
Status words below: "verified in game" = seen working in a played or loaded career on game build 6AC07E31; everything else passes the offline suites only.

### Overall follows attribute edits
- An attribute or position edit (sliders, number boxes, All fields, field search, Database tab, position picker) now also writes the overall for his position with Turbo's fitted formula (same path as a typed overall; kept edits and development plan included). One Undo puts both back. Off switch: `turbo_output\overall_follow_off.txt`. Tactics writes and their put-backs never move the overall.

### FC 26 parity
- **Teams list: Transfers columns** (in / out / loans / spent / received / net per club, from the database transfers table and Turbo's transfer history export) and a club **Transfers** tab. Read only.
- **Managers > Your scouts:** read your career scouts and make them the best (experience and knowledge, clamped to the game's 0..4), with Restore originals (backup file). Writes only on a click; off switch `turbo_output\scouts_off.txt`.
- **Bulk edit:** a Pick list for the field names.

### UX
- Label left / control right on the Tools tab, Status > Settings, game editors, squad role rules, live standings, body types, competitions and club tools. A **Loans** filter in the Players list (On loan / Loaned in / Loaned out; "(loan)" after the club).

### Crash guard (after 2.0.4)
- The guard honours `call_squad_role_create_off.txt` in its memory fallback too, removes nothing in memory once the game's own Remove has decided, backs off after three identical failures, and collects a finished game call before it runs. A player created by Turbo at your club gets his squad status entry at once.

### Tactics: honest badges
- Every Tactics row shows two badges: how its value is written (game variable / database row / preview / Turbo only) and what was seen of it in a played match. Rows with no proven effect (incl. the 66 per-side gameplay sliders of channel A) are off by default and Apply asks before writing them; each row shows the value the game holds now and has Reset to default (from a snapshot of the original). Dead writes to the saved-tactic row are gone; legacy fields are hidden behind "Show legacy fields". Values are bound to the club or player they were set for. Off switches: `tactics_off.txt` (puts the database rows back), `tactics_gamevar_off.txt`, `call_gamevar_off.txt`.

### Readable values and filters
- Positions, roles, match-setup values and other codes read as words everywhere (Database tab and Match setup included). The Players filters sit in blocks with label left / control right, two-sided ranges (age, overall, potential) and the positions FC 27 shows.

### Callname tools
- The masters importer understands FC 27's "real link" / "real high" types; the Dutch audio folder is found; the language pipeline (`replicate_language_structure.py`) runs dry by default and never copies player-name files into the package.

### Commentary language: always detected
- Turbo detects the commentary language the game LOADED: the game's own audio service says which commentary ids it speaks this session and the language that matches clearly wins (else "unknown"; the last answer is kept in `turbo_output\callnames\loaded_language.json` while the game build and the installed packs stay the same), then the installed packs, then English. The old forced `callnames.language` setting is ignored; a manual choice is `callnames.override_language`. The pick and its reason are logged ("callnames: language <lang> (<why>)") and shown in the Status tab. While the language is not proven, Turbo writes no player callnames automatically. Off switch for the in-game check: `turbo_output\callnames_lang_detect_off.txt`.
- Callnames for that language come from every source: the game's commentary bank (game files, incl. players no longer in the database), the database and the masters, with source and Hidden badges; Status shows the counts per source.

### Game asset catalog
- **Every asset the game ships, not only the ones a database row points to (not yet checked in game).** A new offline tool, `turbo/tools/asset_catalog/build_asset_catalog.py`, reads the game files (read-only, about 10 s the first time, under 2 s when nothing changed), builds the commentary bank of each installed language when it is missing or outdated, and writes `turbo_output\asset_catalog\catalog_<game build>.json`: 6,141 3D heads, 20,879 minifaces, 11,460 youth faces, 550 staff faces, 238 legend pictures, 1,436 hair / 133 facial hair / 862 boot (+120 only under content_ts) / 157 GK glove / 371 tattoo models, 1,730 club crests (the 3,204 create-a-club crest templates are left out), 1,534 kits, 299 balls, and the callname recordings of every installed commentary pack (eng_us Derek Rae and Guy Mowbray, ita_it), each id with its proof and a readable name. It runs in a Turbo source checkout (`py -3 -I turbo\tools\asset_catalog\build_asset_catalog.py`): **release builds cannot build a catalog yet** and then work exactly as 2.0. The GUI loads it in the background, checks it matches the running game build, and reloads it when it changes (a rebuild for the same game build reaches every picker too). Details: `docs/asset-catalog.md`.
- **"Hidden" assets in the pickers:** an asset the game ships and no player (team) of the loaded save uses gets a **Hidden** badge, and each picker has an **All / In this save / Hidden** filter (remembered per picker).
  - **Choose a real face:** + **game heads**, the 3D heads of players who left the database (about 2,070 on build 6AC07E31), named and badged; a click asks first and writes the head through a gate (current catalog, a real 3D head). Every filter menu of the chooser now offers at most 5 choices (colours and looks grouped; the tooltip keeps the exact value).
  - **Gear galleries (hair, facial hair, boots, small-sided boots, GK gloves) and tattoos:** the ids the game really ships: 3D-only ids get a "3D only" tile, preview-only ids are greyed and refused, boots whose 3D model is only under content_ts are listed "Not checked" and refused, tattoos outside the save's tattoo table are listed and refused (no tattoo was ever checked in a match), and the ids of Live Editor's name list the game does not ship are gone. New rows: small-sided boots, socks and sleeves.
  - **Accessories 1-4:** only the codes players use (wrist tapes, sweatband, knee tape and brace, ankle tapes, glove, finger tape, bracelet), named from the game folders, with the Hidden badge and filter; referee, NPC and outfit items are no longer offered to players. No write gate (as before). Managers' outfits keep Live Editor's list.
  - **Minifaces:** new sources **Game minifaces**, **Youth faces**, **Legend pictures** and (managers) **Staff faces**, in a paged browser with search. **Crests:** **Crests in the game files**, other clubs included, named from the game's team names (when a language-strings dump is at hand) or kit folders (1,194 of 1,730 on this PC; the rest show "Team #id"); ids that are not clubs say so.
  - **All callnames:** every recording of the commentary bank of your language and team (players no longer in the database badged Hidden; master-list ids with no FC 27 recording badged "No recording"); a voice source needs a simple or link recording (a high-intensity-only one is listed but refused: the game's kick-off name check never asks it). A current catalog's bank is the spoken set, with the same simple-or-link rule as 2.0.
  - **Commentary team (eng_us):** a line in the callnames header: Automatic (the team the live capture of your game's bank matches, else Derek Rae), Every team, dr or gm; remembered. It decides the spoken set and the voice-source gate in every tab (before: every team by default, not remembered).
- **Safety:** an id is offered for writing only when a catalog built for the running game proves the game ships it (a 3D model for heads and gear; a preview alone is not enough). A catalog of another build keeps the lists but refuses hidden ids and shows the rebuild command. Every write of a hidden id is logged in `turbo_output\asset_writes.log` (old and new value). Kill switches: `turbo_output\asset_catalog_off.txt` (everything as in 2.0) and `asset_hidden_off.txt` (no hidden ids). Tools > Status shows the catalog's state with Reload, Open folder and Copy rebuild command.
- **Lua gates:** `custom_headassets` refuses a head the game does not ship, and the CMTracker import gives a real face only when the game has that head, both through `turbo_output\asset_catalog\lua_gate.txt`, which the GUI writes for its own session only (a gate left from another session or game build is ignored, and the features fall back to today's rules). **Fixed:** without a gate, the CMTracker import gave `headclasscode` 0 to players whose database row had a generic head (no 3D head to show); now only when that row has its own real head.
- **Commentary banks for every pack** (`turbo/tools/fc27_commentary`): the base-game packs, every `<lang>_<team>_<full|demo|licensed>` set, per-set results, and errors that no longer stop the run. **FC 27 callname masters** (`turbo/tools/build_callname_master.py`): team-aware, names for every player with his own recording.
- **Never shipped:** the catalog, its banks and name sources hold real names; they stay in `turbo_output`, are in `.gitignore`, and `scripts/package.sh` refuses to package them (`bash scripts/package.sh --guard` checks git and the package).
- **Choose a real face:** the game-head confirmation (Apply, Cancel and its warnings) now stays in view inside the chooser, and its long lines wrap.
- In-game checks still to do (throwaway slot): a hidden head and a model-only boot / hidden hair in a match, a new voice swap with a hidden source, the Lua gate with and without the GUI (`docs/asset-catalog.md` section 14). Game heads of unknown gender are only warned about, not refused.

### Resizable everything, pictures preloaded from the career start
- **Every table has resizable, reorderable and hideable columns** (drag a header border, drag a header, right-click a header). The five that did not (the Attributes slider grid and bit fields, field search, the field editor grid) now do; `turbogui/src/ui/table_registry.h` lists all 32 with their flags and a native test walks the sources (the Players filter grid, the Match tab's per-side sliders and Turbo Tools > Kept player edits got the same column flags when this work met the 1.2.5 line and the filter grid). The exceptions are the tables inside an auto-sizing popup (the competition picker's combo, the club search dropdown, the kept-edits tooltip): saved column settings there broke the popup's second opening, so they keep fixed columns. The Database tab remembers column widths per database table (its ID is pushed per table; before, one `##dbt` for every table lost the widths at each switch). Widths, order and hidden columns are kept in `turbo_output\turbo_gui_layout_v2.ini`.
- **Resizable panes**: the real-face grid and the Tactics tab's left and centre columns got a resize border (the other split panes had one since 2.0.2). Left alone on purpose: the Callname lists (their height is computed, 80 .. 200 px to the buttons below; a remembered height pushed the buttons out of view) and the lists inside popups (the competition picker, the face filter values: a resizable child inside an auto-sizing popup made the second opening fail).
- **Row density** setting (Status > Settings, `gui_settings.json` `gui.row_density` = `compact` | `normal` | `comfortable`): the height of table and list rows everywhere; the font size stays the UI size slider.
- **Pictures load in the background from the moment the career connects, in the order you open them**: your club's faces, the listed players' faces, the real-face chooser (its minifaces and the 3D head renders already on disk), tattoos, the hair / body galleries, crests, manager heads; the first 400 of every kind before the rest of any. The Status tab shows the progress per kind; `gui.preload_images` (default true) switches it off. Loading stops when the game leaves the career and starts again with the next one. Lua's export slice per career event is unchanged (0.25 s). New 3D head renders are still made on request only (Render all filtered heads).
- **Background loading never stalls on a full list**: Lua's background list takes 20 000 missing pictures at a time (`LegacyImages::kMaxBackground`). A picture it refuses now stays queued and unasked, is asked again a few times per frame as the list drains, and the progress line says `waiting: N refused` meanwhile (the Status tab too). Before, the loader marked the refused picture as asked, so with more than 20 000 pictures missing the queue could never finish. Native cases: `preload queue: a picture refused ...`, `legacy files: ask_background ...`, `UI: Lua's background list is full ...`.
- **Fixed: a `bridge_state.json` rewritten within the same second as the previous one was missed** (the GUI kept the old `db_gen`, so the lists were not rebuilt for the newly loaded save until the next change). `std::filesystem::last_write_time` has whole-second resolution with this toolchain, and the bridge compared stamps only; it now reads the small state file every poll and compares the text as well. This was also the cause of the native case `UI: database reload (another save) invalidates rows safely` failing depending on the suite's timing (two cases stamped the file into the same second).

### Arrivals guard: no raw database moves into your club, plan warnings, entries at once for created players
- **No more raw database moves into your club** (offline tests only, not yet checked in game). A transfer into your club without Turbo.dll's game call (the GUI's natives not loaded), the transfer of a loaned player into your club, a loan to your club and the early return of your loaned-out player are database moves: the player gets no development plan, growth record, morale record or squad status entry (the OVR-1 incident and the 2026-10-11 match-load crashes). They are now refused with the reason ("the game's own transfer routine (Turbo.dll op 11) is needed for arrivals at your club"), nothing is written, and the refusal is logged. Opt-in: `turbo_output\db_move_user_club_on.txt` lets them run, with a WARNING in the result and the log. Moves between AI clubs, moves out of your club and moves done by the game's own routine are unchanged.
- **Preset import onto one of your players without a development plan** now says "no development plan: the game may undo this at its next growth step" (the dry run too) instead of leaving out "development plan updated".
- **Mass actions > dev bonus / match XP (without Live Editor's development manager)** counts the plans really written ("development plan updated for 26 of 27") and names your players without one, instead of the constant "plans updated".
- **A player created at your club gets his squad status entry at once**: the match crash guard's repair runs right after the create (both ways) instead of at the next career event; the result and Live Editor's log say whether he has one ("squad status entry added at once", "present", or "not added now" with the reason). Kill switch: `turbo_output\create_status_repair_off.txt` (`squad_status_repair_off.txt` stops it too).

## 2.0.4 (match crash guard hardened; first release of the 2.0.3 changes)
- **Fixed: the match crash guard could add no entries when a career was loaded right after the game started** (seen in game 2026-10-11 on the Leicester career: "added for 0 of 3 ... Turbo.dll did not answer the game call"; the game had crashed twice that day at match load, `FC27.exe+0x7FCE520`, three players created by Turbo had no squad status entry). Turbo.dll finishes its scan of the game about a minute after a load, and the guard ran before that and gave up until the next day. Now: players the game's AddPlayer does not take (not answering yet, queued, refused) get their entry from the memory path in the same run; while any player is still missing, the guard runs again on every career-mode event (at most once a second) instead of waiting for the next day; and it runs on the first events after the game starts too (the load event used to be skipped then). A guard result that leaves players without an entry is a warning in Live Editor's log. Kill switches unchanged (`squad_status_repair_off.txt`, `call_squad_role_off.txt`, `call_squad_role_create_off.txt`).
- **Verified in game (2026-10-11, game build 6AC07E31, copy of the Leicester career):** Teams > Mass actions > Squad roles created the three missing entries through the game's own AddPlayer ("31 of 31 players have the role now through the game (Turbo.dll squad_role) (1 updated, 3 entries created, 27 unchanged)"); after a save and a reload the guard reported "all 31 players of your club have a squad status entry" and the match that crashed before (Huddersfield v Leicester, a Turbo-created player in the XI) loaded and ran.

## 2.0.3 (merged 1.2.5 fixes: match-load crash guard, squad roles, appearance codes, kept player edits, minifaces)
- **The 1.2.5 line merged into 2.0** (offline suites pass; not yet checked in game). Squad roles are one implementation: the game call (op 14), the entries created for players without one, loaned-in players included by default and the per-player result lines come from 1.2.5; the rule editor, Preview, pins, the saved rule applied again after the season reset, the tolerant role-list search and the counts of club players with no role-list entry / role-list players outside your club come from 2.0 and are part of the result text (a club player with no entry in a role list Turbo cannot add to is listed as skipped, "no entry in the role list"; `include_loaned_in: false` on the mass action leaves loaned-in players alone). A missing birth date reads "no birth date in the database" everywhere (rule engine, Preview rows, result lines). A development-plan write the game refuses is reported again ("development plan NOT updated for ...": bulk edit, `db_edit`, `plan_sync`, the preset import); the lenient mode of the OVR-1 repair module (write without asking for a plan, count the fields the game took) stays for that module only.
- **Fixed: the game crashed when a match loaded (seen 2026-10-05 22:50, crash dump read).** FC 27 keeps a squad status entry (squad role, promised, contract renewal flags) for every player of your club and the match load reads it without checking that it exists (`FC27.exe+0x7FCCDF0`, a null entry). Players Turbo had linked to your club through the database (players created by Turbo, moves made before 1.2.0) never got one, because the game only adds it when a player joins through its own transfer. Turbo now adds the missing entries itself, the way the game's own AddPlayer does, at every career load and on each day / match build-up after it (logged in Live Editor's log as "match crash guard: ..."); when the game's table (52 slots) is full, entries of players who are no longer at your club (and not out on loan from it) are removed first, the way the game's own Remove does. Off switch: `turbo_output\squad_status_repair_off.txt`.
- **Fixed: Mass actions > squad roles skipped players** (21 of 28 in the report): the same players had no squad status entry to write. The action now creates their entries first, with the role asked for, so every player of your club gets it.
- **Player appearance: no more codes or missing values (not yet checked in game).** Every code field of the player editor (Profile > Roles and body, Appearance, the gallery rows) reads as text, its combo lists every value FC 27's database uses, and the player's current value is always in the list. Checked on every value of the real players table (Turbo's export, 21340 players): 2805 values in 73 code fields: 615 named, 2186 described, none empty or a bare number; 4 still read "Code N", all outside Appearance (Profile: growth profile 5 and 6, personality 5, skill moves likelihood 4, which no source names).
  - **Body types:** Live Editor's 10 generic ones ("Tall and Lean") and every player-specific one by the players who use it, best-rated first: "Unique: Cristiano Ronaldo", "Unique: Lionel Messi", "Unique: Nélson Semedo, Kyle Walker-Peters" (122 in the database; the combo is searchable: type "Messi" or "unique"). A body type Turbo's list does not know reads "Unique: <the player of your database who has it>"; one nobody has reads "Code N".
  - **Skin tone:** FC 27 stores 10, 20 .. 100, so every player read "Unknown (code 20)"; now Live Editor's names ("Caucasian 2") and the ten values in the combo.
  - **Hair colours 15 .. 27** ("Pale Blonde", "Golden Blonde", "Jet Black", "Copper"...: Live Editor names 0 .. 14 only; named after the colour of each code's preview and checked against the 3D looks of the real faces). **Facial hair colours** use the same 28 colours (the game's previews show the hair palette for beards, and a beard has the hair's code more often than any other), so beards read "Light Blonde", "Jet Black"... (Live Editor's 5 facial hair names no longer match codes 2 and 4). The real-face chooser's colour filters use the same names ("Jet Black" instead of "Colour 21").
  - **Boot colours** (Live Editor's 32 names) and **Seasonal jersey** (Live Editor's names) instead of numbers.
  - **Boots and GK gloves by brand:** "Adidas boots #16", "Reusch GK gloves #37" (brand of each FC 27 id from FC Editor's FC 27 maps); ids without a brand "Boots #460"; gloves 0 "Default". The only boot and glove model names on disk are FC 26's, and FC 27 moved many ids to other brands, so no model names yet.
  - **Free kick / penalty stances and running animations:** 0 "Standard", the others by their players ("Free kick stance 4: Lionel Messi", "Penalty stance 1: ... +127 more").
  - **Head types** with their group ("Head type 1003 (African)"), **eyebrows** by their parts ("Eyebrows 3, variant 1-1"), tattoos "None" / "Tattoo #12", accessories Live Editor does not name "Accessory #52".
  - **Nothing missing from the lists:** big code fields (head types, eyebrows, boots, skin surface...) list the values the loaded database uses and the game's pictures instead of the first 4096 numbers (head types above 4095 were not offered); a typed number still reaches any value; small unnamed fields list their whole range.
  - The gallery rows (hair, facial hair, boots, gloves, accessories) show a combo with the name instead of the bare id, and each gallery picture's tooltip names it. Every appearance field has a readable label ("Free kick stance", "Tattoo: left arm", "Skin surface" instead of "Animfreekickstartposcode"); shorts, undershorts and muscularity are in the Appearance tab too; the Head line reads "head class Specific".
  - A value nothing names reads "Code N" (was "Unknown (code N)"). Labels: `src/core/field_labels.h`; data generated by `scripts/gen_appearance_labels.py` (`src/core/appearance_labels_data.h`; the tests' `tests/native/fixture_player_codes.h` holds the codes of each field only).
- **Players list: a Gender filter** (Any gender / Men / Women, the `gender` field), next to the nationality and continent filters. It combines with the other filters and **Clear** resets it; like them it is not remembered between sessions. (The real-face chooser already had its own Gender filter; the other player lists have only a search box.)
- **Clubs by name or ID everywhere** (not yet checked in game). Transfer / Loan... ("To club"), the Import, Clone, Create player and From CMTracker dialogs ("Club"), and Turbo Tools (Reveal club, Jersey numbers, Bulk edit's team list) now take a club in one box: type its ID or part of its name and a dropdown under the box lists the matching clubs with ID and league, best first (the club with exactly that ID, then names starting with the text). Click a row, or use Up / Down and Enter; leaving the box after typing takes the highlighted club and Esc keeps the one chosen before. Next to the box: the chosen club's ID and league, "national team" for a national team (a move there is still refused with the reason). An ID no club has stays, so the move says "no team has this ID". Bulk edit adds the picked club's ID to its team list (once). The job offer and manager market club lists were already searchable by name or ID and stay as they are.
- **Fixed: your players' edited attributes and overall came back with the old values after a save and a reload (user report; not yet checked in game).** In Manager Career every player of your club has a development plan whose attributes, work rates, skill moves and weak foot outrank the `players` table (Live Editor DOC.MD), and the Players editor wrote only the table: sliders, the number boxes, Overall +1 / -1, archetypes, Undo / Undo bulk, All fields and the Database tab. The save kept the plan's old values, so the edit was gone in every later save (whatever the CmMgrC file is called). Now every such field the window writes for a player of your club (or one it lent out) is sent to Turbo's Lua side, which writes the table's value into his plan (`development` module, new mode `plan_sync`; one command per run of edits, after a quiet second, several players at a time; logged, a toast only when it fails; Players > Attributes shows the last answer). Kill switch: `turbo_output\plan_sync_off.txt`. Bulk edit (Set fields), `db_edit` and the preset import (now work rates, skill moves and weak foot too) update the plan the same way.
- **An overall typed into the Overall box now lasts.** FC 27 works the overall out from the attributes (the player card, its own growth), so a bare `overallrating` write did not last. A typed overall (Profile, All fields, the field search, the Database tab) now moves the attributes that weigh most for his position until Turbo's fitted formula gives it (as Overall +1 / -1, `core/overall.h` `reach`), then writes the overall; one Undo step and one Undo bulk entry put everything back; a value the attributes cannot give (outside 1..99, attributes at their limits) is refused with a toast. The box's tooltip says so.
- **Player edits are kept for every career load (Turbo Tools > Kept player edits).** Attribute, overall, potential, work rate, skill move and weak foot edits made in a career are kept in `turbo_output\reapply_edits.json` (player id + field, the value written and the value it replaced) and written again whenever a career loads, whatever the save file is called, but only while the field holds the value the edit replaced (the game put the old value back); a value the game moved since (its own growth or decline, another career's player) is left alone. Written-again fields of your players go into their development plans too. On / off switch and the full list (kept, was, now, Forget per field, Forget all) in Turbo Tools; Players > Attributes shows the player's kept fields with Forget. The "re-apply at career load" line in `turbo_gui.log` adds "N player fields" when player edits are kept.
- **Fixed: minifaces stayed blank ("..." / "Loading from the game...") when a player was opened for editing** (reported in the Torino career; not yet checked in game). Turbo's Lua side exports the game's pictures only when it runs, and since 1.1.1 Turbo prompted it (the synthetic career event) only while the background loading ran. With a warm picture cache that is over a second after the start (every session of `turbo_gui.log` shows "background loading done" about 1 s after "loading 6778 pictures"), so from then on a miniface the cache did not hold yet (players without a real face, a created player after his custom miniface was removed, a game picture chosen as the New source) waited for the next real career-mode event: in the 2026-10-05 21:31 session Turbo's first prompt came 25 minutes in. Now every picture asked on screen prompts Lua while Turbo is shown (4 times a second, a short slice each) until it arrives or the game says it has none; when Lua gives no answer for 20 s (no career loaded, Live Editor busy) the prompting stops, the log says so once and the box says so.
- **A miniface box always says why it shows no picture.** A spinner and the seconds while it loads from the game; "no answer" with the reason and Lua's status when Turbo's Lua side does not answer; "The game has no miniface"; "Unreadable ...: reason" naming the DDS format Turbo cannot read ("DDS DX10 format BC7 (DXGI 98) is not supported", "DDS compression ATI2 ...") or the bytes a cut-off file lacks; "Decoding..." for the moment it decodes. Every unreadable file is logged once to `turbo_gui.log`. Players / Managers > Miniface has a **Reload** button under the current miniface: it reads a custom file again, or drops the cached copy and asks the game for its picture again (also after a "not in the game" answer). A game picture chosen as the New source shows how long it has waited, with Reload and Cancel.
- **Fixed: an unreadable picture stayed unreadable.** A file that could not be read once (a copy Live Editor's export was still writing, a file another program held open) was remembered as unreadable until its time stamp changed; it is read again 1 s later, then after 2, 4 ... up to 30 s while it is shown.
- **Fixed: the New miniface preview could show the previous player's picture.** Each player's editor counts its versions from 1, so the first picture chosen for the next player (a 3D render, a file) got the same number as the last one chosen for the previous player and his texture was drawn again; switching players now drops the old preview (the crest editor had the same flaw, fixed the same way).
- **Fixed: Miniface > The 3D model could stay "rendering..." for good after switching players.** A render asked for one player and not collected before another player was opened kept the capture service busy (Generate greyed out for the new player, the face chooser's 3D heads stopped) until Cancel was pressed; opening another player now drops it.
- **Removing a custom miniface shows the game's own again.** Live Editor exports the file the game loads, so the cached "game's own" copy can be the custom file itself (seen in the cache: p88162 is the custom file byte for byte, p72030 an older custom one); Remove now drops that copy and asks the game for its picture again.
- **Real-face chooser, "His miniface too":** the minifaces of heads shown as 3D renders are asked from the game too, so a click finds them at hand; a head whose miniface is still loading now says "choose the face again in a moment", one the game has none for says so (both said "not loaded yet").
- Picture textures stay under 448 alive (the overlay has 512 texture descriptors): a burst of new pictures waits a frame instead of running out.
- **Squad roles through the game itself** (Turbo.dll `squad_role` game call, op 14; not yet checked in game). Roles are now written by the game's own PlayerStatusManager functions: an existing entry gets its role, a player without one gets an entry from the game's AddPlayer, and a full table makes room through the game's Remove (only for players who are no longer at your club and not out on loan from it). Every step is checked first (the manager, the 52-slot table, the player in your club by the game's own check) and read back. The match crash guard uses the same call: after the load event the game picks the role for a player who had no entry (the role the Squad screen already showed for him). Without the call (older Turbo.dll, `turbo_output\call_squad_role_off.txt`) Turbo writes the same memory itself as before; `turbo_output\call_squad_role_create_off.txt` stops entries from being created or removed.
- **Squad roles: loaned-in players are included** (the game keeps a role for them like for anyone: they get an entry when they join, and the Squad screen and the match load read it). Tools > Your squad still has "Include loaned-in" (now on by default).
- **Squad roles: the result lists every player** (Teams > Mass actions shows it under the buttons; the toast shows the summary line): updated, entry created, unchanged, or skipped with the reason (for example "no birth date in the database"). The Squad screen shows the new roles when you open it again (the game reads them when the screen is built; no way was found to redraw an open screen).
- **Players editor > Contract & Clubs: Squad role** for a player of your club (Set / Read), through the same game call.
- **Squad roles again after each season reset** (opt-in): FC 27 recomputes every role at the start of a season. Teams > Mass actions ("Squad roles again after each season reset", the by-age rule) and Tools > Your squad (the chosen role) switch on `auto.squad_role`, which applies the rule again right after the SEASON_RESET event.
- **OVR-1 repair made opt-in, dry-run first, reversible, no silent failures** (`features/repair_ovr1.lua`, `lua\scripts\turbo_repair_ovr1.lua`). Before: the module ran at bridge start and on every career event and wrote to whichever career was loaded as soon as one of the six target players had overall <= 5; every error inside it was swallowed by `pcall` and nothing was logged; a forced plan sync was reported as "plan" even for a player without a plan entry. Now: the automatic path runs only while `turbo_output\repair_ovr1_on.txt` exists and never with the kill switch `turbo_output\repair_ovr1_off.txt` (it wins over the opt-in and over the script's confirm flag); the run at bridge start is gone (career events only). The runner script is a **dry run by default** and lists every field it would write per player (`turbo_output\REPAIR_REPORT.txt`, old -> new); it writes only with `TURBO_REPAIR_CONFIRM = true` set before the `dofile`, and the flag counts for one run. Before any write the current values are saved to `turbo_output\repair_ovr1_undo_<pid>.json` (one entry per run, never overwritten); `TURBO_REPAIR_UNDO = true` + `TURBO_REPAIR_CONFIRM = true` (or `repair_ovr1_undo(pid, true)`) puts them back. Every failure is logged with `log.warn` (+ `LOGGER:LogWarning` when present) and listed under FAILURES in the report; one line is logged even when nothing needs repair. `moves.sync_plan(pid, values, true)` now reads `PlayerHasDevelopementPlan` back and returns `no_entry` when the game still says there is no plan; the repair reports "plan entry still missing (the game must create it)" instead of "plan". When every plan write raises, `sync_plan` returns `failed: ...` instead of `none`. Tests: `turbo/tests/t24_repair_ovr1.lua` (10 cases).
- **Callname masters: the FC 27 Italian master is imported whole again** (`turbo/tools/import_callname_masters.py`; offline tool, not yet checked in game). The 2026-10-09 rebuild dropped the workbook's "real link" / "real high" rows, so 7 players whose only own recording is a LINK one lost it, with `game_build` and the LINK / HIGH lists. The importer now reads the FC 27 types: `real_players` = 'real' | 'real link' (4,046 for ita_it), `real_simple_players`, `real_link_players`, `real_high_players`, `generic_high_ids`, `segments.real_link` / `real_high` / `generic_high`, and `game_build` (from `--game-build ita_it=...`, else the JSON build_callname_master.py wrote for the same workbook, else the bank). FC 26 lists keep their shape. A SegmentID of 0 is kept (it was dropped: one generic and one real id per language lost a recording although `pSIMPLE_SURNAME_0_0.wav` exists). Audio folder: `--wav-dir <lang>=DIR` (`<lang>=` = none); else the FC 27 Italian audio `i27`, the workbook's folder, then `<workbook folder>\audio` (the Dutch wavs in `ned\audio`, so dut_nl gets play buttons), and for FC 26 lists only the language folder. `--self-test` covers all of it on a small synthetic `.xlsx`.
- **`turbo/tools/replicate_language_structure.py` is the one-run pipeline its description promised** (dry run by default, `--write` to write): per installed commentary pack (found like the GUI does) plus `--lang`, it optionally extracts the FC 27 bank and builds the FC 27 master (`--extract`, downloaded packs only), imports the master, writes `spoken_<lang>.txt` from the FC 27 bank (`--spoken-intensity any|1|2`, default any) or else from the master's generic ids (a comment line says which), counts the ids with / without a wav per family (`audio_check` in the JSON) and prints a table per language. It no longer copies the JSONs and spoken lists into the git worktree's package, and refuses an `--out` inside a git worktree. `--self-test` on synthetic data.
- **The Callname tab's language pick is logged**: every refresh writes "callnames: language <lang> (<why>)" to Turbo's log and `turbo_gui.log`.
- `scripts/check_all.sh` also runs both tools' self-tests (a line "PY ..."; skipped when no Python with openpyxl is found).

## 2.0.2 (built-in table for game build 6AC07E31-2145C000, signature auto-adapt for title updates)
- **Game build 6AC07E31-2145C000 is built in.** Turbo.dll now carries the table for the October 2026 title update itself (the 122 code spots that 2.0.1 shipped as `turbo\signatures_6AC07E31-2145C000.json`, every one verified live: found exactly once, 12 hooks active, game-variable calls accepted, a played match ran fine). Both known builds share one signature list. The file is no longer in the package. A copy left by 2.0.1 (unzipping 2.0.2 over 2.0.1 does not delete it) is recognised by its signatures and ignored, so it cannot hide later fixes to the built-in table: `turbo_gui.log` says `game hooks: ...\turbo\signatures_6AC07E31-2145C000.json ignored: identical to the Turbo 2.0.1 package file, using the built-in table (the file can be deleted)` and the Status tab shows the "built-in" table. Any other `turbo\signatures_6AC07E31-2145C000.json` (an edited one) is still used first. A placeholder-only `turbo\signatures_<build>.json` (the template Turbo leaves for an unknown build, e.g. from 2.0.0 on this build) is now ignored instead of hiding the built-in table.
- **Auto-adapt after an EA title update.** When the game's build is in no table (no `turbo\signatures_<build>.json`, no built-in table), Turbo scans the updated game with the table of the newest built-in build and uses it only if EVERY signature with a pattern is found exactly once and every address it yields rests on checked bytes (all or nothing). A signature whose address is read outside its pattern (16 of them, e.g. the lea 1683 bytes into DataController::CreatePlayer that loads the 0x3A event's vtable) names the instruction it expects there (`expect`, e.g. `48 8D 0D` for lea rcx,[rip+x]; the two hook probes career_event_dispatch and loc_strtab_get are used only where Live Editor's hook is seen), so a title update that keeps a function's first bytes but changes its body makes that signature `bad pattern` (`match+1683 holds E8 ..., not the expected 48 8D 0D`) and refuses the adapt instead of handing out a wrong address; a signature with an offset outside its pattern and no expect is never adopted (`unverified offset`). The expect check applies to the built-in and file tables too. Then hooks and game calls work as before and `turbo_gui.log` says `game hooks: build <new> is not in any table; adapted the table of build 6AC07E31-2145C000: N of N signatures found exactly once`; the Status tab shows the table as "adapted from 6AC07E31-2145C000", and the adapted table is written to `turbo_output\signatures_adapted_<build>.json` (diagnostics only, each note prefixed "adapted from ...:"; an adapted table never goes into `turbo\`). If any signature is missing or ambiguous nothing is adapted: every hook stays off and the template is written as before, with `game hooks: build <new> is not in any table; adapt refused (table of build ...): K missing, M ambiguous of N (first: name (missing), ...)`.
- **Kill switch** `turbo_output\signature_adapt_off.txt`: never adapt (an unknown build keeps every game hook off, the pre-2.0.2 behaviour); the log says `adapt off`.
- The edit_cfg_category_items pattern is partly masked: the tail jmp's displacement is masked except its top byte (`... 5B E9 ?? ?? ?? F9`, was `E9 ?? ?? 99 F9`). The distance to the item-vector dtor may now move by 6 to 10 MB (it moved 8 KB with the October update) instead of crossing a 64 KB boundary, which would have turned the signature missing and refused every future adapt. Fully masked it would match four identical destructors; their prologue and padding are identical too, so only a neighbouring function (link order) or this byte tell them apart. Matches build 6AB9813C-211EF000 exactly once (checked against its image) and the code of 6AC07E31-2145C000 (jmp displacement 0xF9997BF7 there).

## 2.0.1 (game build 6AC07E31-2145C000)
- Signature table for the current game build (turbo\signatures_6AC07E31-2145C000.json, 122 code spots, every pattern found exactly once in the running game). With it the game hooks and game calls work again on this build: 12 hooks active, game-variable writes (injuries, weather, difficulty, CPU substitutions, Match setup) are accepted by the game. Without it every game call was refused ("game hooks are off for this game build").
- The edit_cfg_category_items pattern is partly masked (the low two bytes of the tail jmp's displacement; 2.0.2 masks three).

## 2.0.0 (tactics editor, squad-role rules, body type gallery, opposition variety)

- **What each slider really does.** Every tactics/match slider carries a badge. **Live**: a game variable set for the running game (injury frequency and severity, injuries off, weather, time of day, difficulty, CPU substitutions off); accepted by the game, effect in a played match still being checked one by one. **DB**: a career database field (roles, preferred positions, traits, formation tables), saved with the career, but only effective if the game reads it at match setup. **Preview**: Turbo's own model (pitch preview, opposition solver); nothing is sent to the game. **RE**: no known write site yet (pass/shot error, foul frequency, card strictness, most team tactic sliders); shown as Preview only and not promised to do anything.
- **Tactics tab: the pitch, the formation, the opposition card and the presets are now wired to the real model.**
  - **Pitch.** It draws the core preview model: lines, width band, run arrows, roaming rings, the hatched intensity grid and the exposure hint are the model's, not a separate mapping. A dashed layer is only drawn for a slider you switched on, the hatched layers are off by default, and the primitive count stays under 400 (the grid gets coarser when every layer is on). Phase buttons (with the ball, without, overall) and a click on a dot (which selects the slot the position sliders apply to) are in.
  - **Formation.** The selected club's saved formation is read from the save (the club's style link, else its active tactic, then the `formations` row, with `formationoffsets` when the row has no offsets) and cached by the database generation. When the save has none, or the positions cannot be read, Turbo's built-in shape is drawn and the pitch says why. The pitch always says "saved data, in-game effect unverified" or "built-in shape, not read from your save".
  - **Mentality tables.** The team sliders that live in `mentalities` / `cm_mentalities` (defensive and attacking width, players in the box, the four style steps, and line depth and build-up in `cm_mentalities`) are now written to the club's own row, found by team id (the active tactic first), instead of being reported "not written". A club with no row there is reported by name. Formation slot offsets are still not written by this build and are listed as such.
  - **Opposition card** (Opposition scope). It reads the best-14 squad rating, the average age and the formation shape of both clubs from the save, solves a seeded profile from the fixture, shows the style blend, the changes (labelled preview only) and the explain text, and has a Re-roll button. Only an injury or difficulty offset from a rule can reach the game, only through "Apply live offsets to the game", added to the Match setting (or the middle value when none is set; Turbo cannot read the game's own number). Your own rules in `turbo_output\opp_rules.json` are read at start-up.
  - **Presets.** Compare mode shows the loaded preset as "was X" next to each slider that differs; presets can be renamed, duplicated and deleted (with a confirmation; built-ins can only be duplicated), and exported and imported through the in-overlay file picker with a plan that offers keep both, replace or skip for every clash. An opposition offset slider is now sent as base plus offset, not as the raw offset.
- **Squad roles: players missing from the sheet are no longer skipped, and you write the rules.** The role button now writes every player in the game's own role list that is on your club (before, it worked from the match-day sheet, so players missing from the sheet's slots were skipped), finds the list even when a few entries look odd, says how many players it did not write and why, and applies the saved rule again after the game's season reset (career event 23; the old behaviour, roles rebuilt by the game, is what made roles look wrong after a new season). Not yet checked in game. **Teams > team > Mass actions > Squad roles** also has a rule editor: up to 32 rules, each a squad role (Crucial .. Prospect) for an age band, an OVR rank band inside the squad and a position group (GK / DEF / MID / ATT), plus per-player pins. **Preview** lists what each player would get and why (`turbo_output\role_preview.json`, nothing written); **Apply** writes the roles and remembers the rule (`turbo_output\role_rule.json`), which is applied again after the game's season reset (turn off with `role_reapply_off.txt`); **Clear rule** forgets it. The default rules are the 1.2.4 behaviour (19 and older Rotation, younger Prospect). The role is one byte of the 8-byte PlayerStatusManager entry; loaned players and players with no entry are listed, not touched.
- **Body type gallery.** **Body types...** next to the body type of a player or manager opens a gallery built from the bodies the game really uses (Generic and Player-specific groups, height / weight filters, "used by N players", **Copy body type from player...**). It is generated at run time from the read-only `bodytypes` probe (`turbo_output\bodytypes_fc27.json`), not from guesses. A player-specific body is not written onto a generic (or unknown) head unless you tick the box that says it is untested. Unknown codes read "Specific body #N".
- **Phase 0 probes (read-only).** Turbo Tools > `probe_tactics` writes `turbo_output\probe_tactics.json` / `.txt`: every tactic / mentality / instruction / formation / teamsheet table with its fields, one `cm_teamsheets` row of your club, and the squad-role diagnostic (sheet slots, club links, role list). `bodytypes` writes `bodytypes_fc27.json`. Neither writes to the database or the game's memory.
- **Codes read as descriptions** in the describe / routing / picker texts and the Technical details of the editors that still showed numbers.
- **Dropped from 2.0 on purpose** (not proven, so not promised): style variety for CPU clubs (the Opposition card shows its changes as a preview; only injury and difficulty offsets reach the game), pass / shot error, foul frequency and card strictness sliders, writing formation slot offsets, dragging a dot to move a player on the pitch.
- **Simulated matches ignore all overrides** (the game loads `simsettings.ini` over them), so Live sliders affect matches you play only. Overrides are process-wide and are not saved with the career; they are cleared when the game closes. Whether Authentic Gameplay or Dynamic Opposition bypasses them is not verified.
- **New kill switches** (empty files in `turbo_output`): `tactics_off.txt` (no tactics writes) and `role_reapply_off.txt` (no squad-role re-apply on season reset). Existing ones are unchanged and now all listed in the README troubleshooting table.
- `scripts/check_fc27_schema.py --coverage` lists players/teams/manager schema fields no GUI source mentions. Developer tooling only.

## 1.2.4 (real-face chooser: filters on the 3D heads, renders 6 at a time)

- **Real-face chooser: the filters now describe the 3D head you see.** For real faces the game takes hair, beard, skin and eyes from the face scan, so the database codes the filters used (skin tone, hair colour, hair style, facial hair, facial hair colour) often did not match the 3D render. All 2779 real-face player heads are now classified from the game's own 3D renders (verified: every head of the list rendered and classified; skin tone, colours and beard length are judged by eye, so borderline cases such as stubble vs clean-shaven can differ) (`turbogui/data/face3d_looks.csv`, compiled into Turbo by `scripts/gen_face3d_looks.py`) and the chooser filters on that look: **Skin tone** (very light, light, medium, olive, brown, dark), **Hair colour**, **Hair length** (bald, buzz cut, short, medium, long), **Hair type** (straight, wavy, curly, afro, braids, dreads, twists, tied, mohawk, slicked, spiky), **Facial hair** (clean-shaven, stubble, moustache, goatee, short / full / long beard), **Beard colour** and **Headwear** (headband, hairband, hat / cap). Each filter lists every value with its count under the other filters. Gender stays from the database; **Eyes (database)** and **Ethnicity (database)** come last, since the render cannot judge them. Heads not classified yet are "not classified yet" in each 3D filter (never matched by a look value), and **Only heads with a 3D look** hides them (on by default from 200 classified heads). The head's tooltip reads its look ("3D look: light skin, dark brown short wavy hair, short beard (dark brown), no headwear"). Manager heads keep the database filters.
- **The real-face chooser's 3D heads render several at a time** (not yet checked in game). Turbo used to ask the game for one head per request (about 0.55 s each, so 2779 heads took about 26 minutes); it now sends up to 6 heads per request, the size of the game's own capture batch (PlayerCaptureController renders up to 6 players per batch), each with the player / staff descriptor that rendered the real head in 1.2.3. Every picture is saved to the cache as it arrives; a head the game gives no picture for is retried once, then skipped for the session ("3D failed" on its cell; "Render all filtered heads" gives it its tries back).
- **"Render all filtered heads" keeps going with the chooser closed** (and with Turbo hidden): the renders run from Turbo's frame loop, never wait for the game, and send only while the game says it can capture. Turbo's status line shows "3D heads: done / total" and a Stop button while it runs; the Status tab has a line with heads per request, renders, failures and what is waiting. Heads on screen in the chooser still go first.
- **Fallback switch:** create `turbo_output\player_capture_batch1.txt` to go back to one head per request (looked at every 2 s, no restart). `turbo_output\player_capture.log` logs each batch ("request batch of 6 ... ids ...", one "picture for id" line per head) and "capture done for ...: 6 of 6 picture(s) in X s since the request ran (Y s per head)".
- The Miniface editor's "The 3D model" tab no longer picks up the chooser's renders: it only listens while it waits for its own picture.

## 1.2.3 (fix: 3D real faces showed one default head)

- **Fixed: the real-face chooser's 3D heads were all the same bald default head (seen in game).** The capture descriptor Turbo learns from the game's own captures came from the hub's portrait of your manager avatar, flagged "user's created avatar", so the game drew that avatar's head for every player. Player requests now always carry the player flags (Miniface > 3D model too), and the chooser uses the player descriptor that rendered the real 3D head live (Achraf Hakimi). Renders cached by 1.2.2 are removed.
- **Fixed: the hair gallery's "None" picture appeared twice with one ImGui id** (a red ImGui warning on hover). Hair pictures now carry their length under the id ("233 Medium").
- **Roles and body grid in two columns** so names such as "ST Advanced Forward+" and "Short and Lean" are not cut.

## 1.2.2 (player editor: text instead of codes, hair filters, 3D real faces, more mass actions)

- **Code fields now read as text.** Roles (role1..role9: "CB Ball-Playing Defender++"), body type ("Tall and Lean"), emotion ("Ice Cold" .. "Volcano"), personality, growth profile, run style, skin tone, hair / facial hair / eye colours, accessories and their colours, jersey, socks and shorts styles are combos with Live Editor's own names (loc/eng_us/localize.json); long lists have a search box; the raw code is only in the tooltip. Appearance items without names read as a descriptor plus the id ("Long, curly, headband #233", "Boots #120").
- **OVR modifier next to Overall and Potential** (Profile grid and the Attributes header).
- **Mass actions: Form, Match XP, Dev bonus** (Teams > team > Mass actions, also in "All actions"; not yet checked in game). Form: your club through the game's SetPlayerForm (100) plus `teamplayerlinks.form` 5, other clubs `teamplayerlinks.form` 5. Dev bonus: +N (1..10, default 2) on every attribute of each player's group through Turbo's development (plans updated too), never above his potential; Match XP: a one-off +1 growth the same way (FC 27 keeps no match XP and this Live Editor build has no development manager; with a Live Editor that has PlayerDevelopmentManagerAddPlayer they use its XP multiplier x2 / bonus XP instead).
- **Hair gallery filters: Length, Type, Accessories.** Every one of the 1463 FC 27 hair styles was classified from the game's own preview pictures (bald / buzz / short / medium / long / unknown preview; straight, wavy, curly, afro, braids, dreads, twists, tied, mohawk, slicked, spiky; headband, hairband, hair tie, hat / cap / durag), with counts per choice, a search box and a description on every picture. The real-face chooser's Hair filter uses the same catalogue.
- **Real-face chooser shows the 3D in-game heads.** The heads on screen are rendered by the game from their 3D models (the same capture as Miniface > 3D model) and cached in `turbo_output\cache\faces3d`, so later visits are instant; "Render all filtered heads" renders the whole filtered list with progress and Stop. Without Turbo's game hooks it falls back to the minifaces.
- **Exact face filters.** Skin tone, hair colour, facial hair colour and eye colour now filter by the game's exact value with Live Editor's names instead of five coarse groups.

## 1.2.1 (morale "very happy", name fixes, Names tab)

- **Mass action "Morale: very happy" (was "Morale and happiness to 100"; verified in game).** 100 is the game's top morale level "Complacent" (+0 OVR), which is why the old action looked broken. For your club, each player now gets the highest morale the game's own level function still calls "very happy" for his emotion type, written with the game's SetTotalMorale so the overall's morale modifier refreshes (Turbo.dll game call `player_morale`, op 13; on by default, `turbo_output\call_player_morale_off.txt` turns it off). The summary says how many were set and the morale read back. Verified in game (Torino career): 18 players very happy (morale 94..104), the squad screen shows Very Happy with +2..+7 OVR. Players with no morale record (moved in by Turbo's old database moves: "Unknown", -2 OVR) now get one first, created the way the game's own signing handler does (GetPlayerEmotionType, MoraleStore::Create, InitMorale; only after every store check passes and the game's Find says there is none), then very happy; the summary says "including N whose missing morale record was created". Creation verified in game too (same career): 6 records created (40 -> 46), all 24 players very happy, a second run created none, and Barreca / Walukiewicz showed Very Happy +4 / +3 OVR instead of "Unknown" -2; `turbo_output\call_player_morale_create_off.txt` turns it off (those players are then counted only, as before). Morale records of players who left are counted, not removed. The Mass actions descriptions now wrap inside the window (they ran off the right edge). Without the game call, or for another club: Live Editor's SetPlayerMorale with 85 (nothing read back).
- **Created players in real time (off by default, not yet checked in game).** Create player / Clone / From CMTracker / Import as
  new can add the player through the game's own database INSERT (Turbo.dll game call `player_create`, op 12): the `players` row
  (every column, read back), the typed names, the game's "player inserted" event 0x3A, `InsertTeamPlayer` into Free Agents and
  the game's own move into the club (with the contract record at your club), so Squad Hub, Team Management, the player search,
  morale and the contract should see him at once instead of after a save and a load. It runs only while
  `turbo_output\call_player_create_on.txt` exists (`call_player_create_off.txt` turns it off again); otherwise, or when the call
  answers "off", the database path of 1.2.0 runs unchanged and the result says so. Research and live checks:
  [`docs/re/created_players.md`](docs/re/created_players.md).
- **Fixed: Import turned player names into common names (seen in a live career; Repair names checked in game).** Export
  wrote a player without an `editedplayernames` row as his shown name in `commonname` with the first name, surname and shirt
  name empty, and Import (Names group on by default) wrote exactly that back: "Jacopo Segre" became one common name and his
  shirt lost its name. Now Export writes his real names (the texts of his name ids, `core/names.lua`), Import writes names only
  when they differ from the ones he has (a 1.2.0 file gets its real names back from its own name ids), the shirt name is never
  left empty, the Names group is off by default in the Import dialog, and a clone given only a new surname keeps the original's
  first name. **Players > Repair names...** (`player_presets` mode `repair_names`, Check first) gives the players 1.2.0 changed
  their own names back: every `editedplayernames` row whose first name, surname and shirt name are empty and whose common name
  is the name his own ids show is rewritten in place (no row added or deleted); any other name is left alone and listed. Save
  the career afterwards to keep it. **Checked in game (2026-10-05, the user's career):** Check listed 6 players, Repair names
  gave them back (Team Management showed BARRECA, SEGRE, GYASI, WALUKIEWICZ at once, without a reload).
- **Players > Names tab (not yet checked in game).** Every name the game shows for a player in one place: first, last,
  common and shirt name (his edited names, prefilled with what the game shows) and the four name ids, typed or found by
  text. Save names writes only what changed: in place when he has an `editedplayernames` row (with Undo), else through
  Turbo's Lua side (`set_display_name`, never into a full table). An empty common name stays empty, an empty shirt name
  takes the last name, too-long names are refused before any write. "Restore database names" sets his row back to the
  name ids' texts (no delete) and a hint names rows damaged by the 1.2.0 preset import. The Players list follows at once.

## 1.2.0 (real-time transfers, Mass actions, From CMTracker, filters)

Released 2026-10-05. Checked in game (test career turbo04, SSC Napoli): transfers and releases made by the game itself (the
player is in Team Management at once with his morale), Mass actions incl. Block offers, From CMTracker with the miniface, the
Players tab filters / sorting / Overall +1 / Undo bulk / archetypes, team and competition filters, everything still in place after
a save and a reload. Still shown only after a save and a load: loans, ending a loan, created / cloned / imported players (the
game's own queries do not see rows added from outside until then; docs/re/created_players.md has the plan for real-time ones).
Anything marked "not yet checked in game" below was built and passes the offline tests only.

### Added

- **Real-time transfers, releases and created players (the game does the move).** Turbo.dll's new `player_move` game call
  (mailbox op 11, `core/player_move.*`, `win/player_move_win.cpp`, docs/re/realtime_transfers.md) runs the game's own
  `TeamUtil::PlayerMoved` on the game thread, plus `PlayerContractManager::AddContractRecord` for an arrival at your club and
  `ContractTerminationManager::ReleasePlayer` for a release of your own player (the game pays the compensation from your budget,
  as its own Release does). The game's join / leave handlers run inside the call, so squad screens, morale, form, status,
  roles and team sheets should have the player at once, without a save and a load. Before anything is written the game's checks
  run (code 9: the player is where Turbo thinks, neither club is a national team, he is not on loan, the squad limits from the
  game's ini would not make the game release or sign anyone); a refusal changes nothing. Lua: `TurboPlayerMove(code, pid, from,
  to, months, wage)` (bridge.lua), used by `moves.transfer` / `moves.release`. Loans, loaned players and created players stay
  database moves (a row added with Live Editor's InsertDBTableRow is not seen by the game's own queries until a save and a load:
  the game answered "not in team 111592" for a player created at Free Agents; real-time created players need the game's
  CreatePlayer). Kill switch: `turbo_output\call_player_move_off.txt`. The Transfer box and the Release tooltip say which moves
  the game makes (`game_moves` in bridge_state.json). **Checked in game (turbo04, Napoli, 2026-10-05)**: all 10 functions resolved
  at the research addresses; AI to AI (Kostons, PEC Zwolle to Ajax); AI to Napoli (Dadie): he was in Team Management's reserves
  and the Squad tab counts at once, with **morale Happy (+1)** instead of "Unknown", the game's contract record (60 months) and
  his development plan; his release through the game (ReleasePlayer returned 0, compensation paid, Defence count back at once).
  After a save and a reload everything held (Kostons at Ajax, Dadie a free agent, blocked players still blocked).
- **Teams > team edit > Mass actions**: one button per action for every player of the selected team, each asks first:
  *Block incoming offers* (your own club), *Squad roles* (age 19 and over Rotation, younger Prospect; your own club only),
  *Morale and happiness to 100*, *Long contract (60 months)* and *All actions*. Lua module `features/team_mass.lua`
  (`tests/t18_team_mass.lua`). Long contract, morale and squad roles work with what Turbo already had; **Block offers calls the
  game's own "Block Offers" toggle through Turbo.dll** (new actions 7 / 8 / 9 of the transfer_list game call, docs/re/player_status_roles.md).
  **Checked in game**: long contract (Squad Hub shows 4y 10m for everyone), squad roles (Rotation at once), morale to 100 ran, Block offers (32 players, the game's own menu then offers "Allow Offers"). ("Likes the club" was dropped on request.)
- **Squad roles** now write the FC 27 layout correctly: the role is ONE byte of the 8-byte entry (`{pid, role, wasPromised,
  renewalDismissed}`); before, Turbo read / wrote it as a 4-byte int, which failed for promised players and would have cleared their flags.
- **Players > From CMTracker...**: pick a player from the CMTracker library (search by name, club, nation or id), pick a club and shirt number, and Turbo creates the fully populated player. The library is every players CSV in `<Live Editor>\turbo_cmtracker` (the CSV button on cmtracker.net/players, 50 rows per page; any number of files, merged by player id; "Add CSV files..." copies them in). Mapped: all 34 attributes and the six card values (goalkeepers' too), overall / potential, positions, PlayStyles, skill moves, weak foot, foot, height / weight, birthdate, contract, nationality, hair / eye / skin / head / body codes, names (common name only when it differs from first + last), release clause, the real face when CMTracker has one and the game knows that head (else a generic head of the new id), and the miniface (one picture request to CMTracker's picture host for that player, cached in `turbo_output\cmtracker`, converted to 256x256 DXT5). Hair style, facial hair, boots and tattoos are not in the CSV (FC Editor's new-player defaults are used; change them in Players > Appearance). No bulk download, no key, no automation of the site (its bot check and its 50-row cap are respected). New files: `core/cmtracker.*`, `core/http.h`, `win/http_win.cpp` (winhttp.dll loaded on first use), `ui/ui_cmtracker.cpp`. **Checked in game**: Pedri from the sample CSV was created in your club with his attributes, three positions, weak foot / skill moves, joined today (his save + reload is still to be checked). The miniface download wrote no picture on that try (the result is now logged: `CMTracker: <name> -> team ...; <note>`).
- **Import from FC Editor** (`turbo/tools/fce_import/fce_to_preset.py`, drag-and-drop `Import_from_FC_Editor.bat`): turns players FC Editor (decoruiz) has fetched from CMTracker or created, exported as `.player` files or read from its workspace by player id, into Turbo player presets (`turbo_output\players\<name>_<id>.json` + the miniface `.dds` from FC Editor's `legacy\imgAssets\heads`). Players > Import and "Create player..." take them as they are. Offline (no network, no keys, nothing is called in FC Editor). FC 26 files are converted to FC 27 values (skin tone 1..10 to 10..100, contract year) and every value is clamped to the game's own ranges. Checked: 20 real FC Editor players, all inside the game's schema with no unknown field; `tests/t16_fce_import.lua` loads one and creates a player from it (offline).
- **Players tab**: Nationality and Continent filters (and a Nationality column), sortable columns (click a header), roomier attribute sliders (two columns when wide), **28 archetypes** (Poacher, Ball Playing Defender, Playmaker ... with a preview of every change; the overall stays the same), **Overall -1 / +1** (formula fitted on 21,887 real players, 91.4% exact, 99.87% within 1) and a separate **Undo bulk** button (the normal Undo keeps 20 single-field steps). **Checked in game**: filters and sorting, Overall +1 with its preview and Undo bulk, a Poacher archetype applied with the overall kept (65 to 65), and the archetype's attributes still there after a save and reload.
- **Teams / competitions**: team country and league filters, competition country and continent filters (continental competitions match the continent by the first word of the region name: a heuristic), sortable squad / teams / managers / league / club lists. The Job offer moved from Managers to a **Job offer** tab in Team edit (prefilled with the selected team). **Checked in game**: Italy + Serie A gives 20 clubs, the competition picker filters, and a job offer from Ajax created from the new tab ("Ajax wants you as manager", weekly wage 32,000).

### Changed

- The default contract of a moved, created or imported player is **60 months** (decision 2026-10-05): the Transfer box, the `player_moves` module and `moves.transfer` all said 36 before (the Lua unit test caught it; the loan default stays 12).
- **Transfer keeps the player's wage** unless a wage is given: the box's Wage field starts at 0 = "keep" (it used to default to 10000 and overwrote real wages: Yamal 400,577 became 10,000 in the test). `wage = 0` in `player_moves` means keep as well.

### Fixed

- Review fixes before release (Lua): a transfer or release the game refuses puts back the contract fields Turbo wrote for it (before,
  they stayed on a player who never moved); a failed write in the middle of a move puts back the earlier ones; a release the game
  only queued no longer resets the wage first (the game works out the payment from it), and Delete waits for it; a failed loan-row
  delete puts the move back; a damaged squad-role calibration can no longer write outside the role entry; Mass actions refuse Free
  Agents. Lua tests 277 passed.
- Review fixes before release (window): **Undo bulk after a Refresh wrote through freed memory** (could crash; checked in game: Overall
  +1, Refresh, Undo bulk restores every attribute); Overall +1 could write 100; a CMTracker CSV saved as ANSI (e.g. "Müller") or with
  "nan" values could crash the game from the download worker; a bad miniface answer was cached for good; the league filter listed
  international leagues that never match; "Requested" was shown when the request was not sent; the Job offer / manager confirmations
  wrapped in a narrow column.
- A created / cloned / imported player's club link starts at form 3 like the game's own (was 0, poor form).

- **Players that came to a club were "lost"** (in Turbo but not in the squad hub / team management). Cause found in the code: the game's club screens skip a player whose `playerjointeamdate` is 0 / after today or whose `contractvaliduntil` is 0 / already over, and Turbo left those as they were (Create player took the source's values, Import onto a player wrote another game's dates, loans never touched them). One shared rule now (`moves.contract_values`): he joins today (the game's date), a contract that still runs is kept, a missing / expired one is replaced by a 60-month contract, a transfer with a length sets that length. Used by transfers, loans, Create player / import as new and Import with the Contract group. `tests/t17_join_contract.lua` (10 cases). **Checked in game**: Chiesa, Yamal and Haaland (moved into Napoli) appear in Team Management and the Squad Hub counts after a save + reload (Attack 9 to 11 players, Defence 14 to 13 after Beukema left), joined 2026-08-05, contract 2031.
- **Players changing after a save + reload**: could not be reproduced. Three players (Yamal, Haaland, Beukema) were exported before the move, after the move + save + reload and after 3 simulated days: every `players` attribute is identical (only wage / contract / join date / release clause differ, as written). The development-plan sync (`moves.sync_plan` / `refresh_plan`) stays as a safeguard. Known side effect, not Turbo-specific data: a player moved into your club has **no morale record** ("Unknown", -2 OVR in matches) until the game creates one; `SetPlayerMorale` does not create it (see `docs/re/realtime_transfers.md` for the real-time work).
- **Players of 85 clubs could not be moved** ("player ... has no club link in teamplayerlinks"; seen in game with Arbër Hoxha of
  Dinamo Zagreb). Turbo took every team in `teamnationlinks` for a national team, but FC 27's table also ties 85 clubs to a nation
  (Rest of World league 76 such as Dinamo Zagreb, leagues 1003 / 1014 / 2226). National teams are now the game's own rule
  (IsInternationalLeague: league 78, 2136 or 3004) in Lua (`moves.national_teams`: transfers, loans, releases, create player, job
  offers, manager market, Mass actions) and in the window (which also counted only league 78, so women's national teams were
  clubs there). `tests/t20_national_teams.lua`. **Checked in game**: Hoxha (Dinamo Zagreb 211) transferred to Napoli by the game.
- **Mass actions > Block incoming offers** called the whole action "not done" when players on loan were in the squad (checked in
  game: 32 of 37 Napoli players blocked, the game's own Squad Hub then offers "Allow Offers"; the 5 others had playerloans rows).
  Players on loan are now skipped and reported as such (the game blocks only players under contract at your club).
- **From CMTracker: the miniface was never downloaded.** Cause: an offline setup's Windows Firewall rule that blocks FC27.exe's
  outbound traffic (here "FC 27 Block Out") also blocks Turbo.dll, which runs inside the game, so WinHTTP never connected (the
  picture host itself answers: 28 KB PNG for Pedri). When WinHTTP gets no connection, the same single GET now runs through Windows'
  own `System32\curl.exe` (a separate program the rule does not cover): only the one picture asked for, https only, plain address
  characters only, 15 s and 4 MB limits, no window. Your firewall rules are not changed. **Checked in game**: Jude Bellingham from
  the sample CSV got his miniface (p252371_27.png, 29 KB) with the block rule in place. Outside the game: a missing picture gives
  403 ("CMTracker has no miniface picture"), a too-large answer and a malformed address are refused.
- The From CMTracker dialog no longer grows taller than the screen (its Create player / Close buttons were off-screen at 1080p): a fixed-size window with a scrolling body and a footer.
- The Players tab keeps all nine editor tabs visible at the default window width (the list takes at most 45% of the window; the tab bar shrinks tabs further before it scrolls).
- A player created from a preset, FC Editor or CMTracker shows his name at once (a full refresh follows the create / clone / import result); before, the list showed "#<id>" until Refresh was pressed.

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
