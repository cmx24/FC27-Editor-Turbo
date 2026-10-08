# Turbo 2.0.0 in-game results, 2026-10-07 (turbo04, SSC Napoli, restored from backup 2026-10-04 08:39 because the slot was missing; real careers untouched)
Game build 6AC07E31-2145C000 is NOT in Turbo's signature table: all game hooks/calls are off (Status: "0 hooks active"). Lua-side features work.

VERIFIED in game
- Launch via LaunchFC27.lnk, input reaches the game (no UIPI block), start-up screens, Manager Career > Offline > Original > Load Career > turbo04.
- Turbo 2.0.0 connects (21,340 players / 841 teams), overlay toggles with Num+.
- probe_tactics + bodytypes probes (run from Live Editor's Lua Engine): 14 tactic-like tables, cm_teamsheets 34 filled slots / 0 gaps, role list 34 players / 0 without entry; 131 body type codes (122 outside the generic set). Files: probe_tactics.json/.txt, bodytypes_fc27.json.
- Squad role apply (Turbo Tools "Set role for whole squad", role 3): ran on the next career event, log "squad_role done: role 3 ... PlayerStatusManager +0x18: 34 players". Roles were NOT read back in the game's own screens.
- Tactics tab: renders the saved formation (4-4-1-1, saved formation 18); the game's own Edit Tactic screen shows "4-4-1-1 MIDFIELD" with the same positions. Presets list, injury settings with [Live] tags, "What the game will receive" panel.
- tactics_off.txt kill switch: banner "Tactics are off ...", effect layers grey, Apply disabled; removing the file restores normal.
- Role rule editor (Teams > Napoli > Mass actions > Squad roles): rules, pins, Preview/Apply/Clear, re-apply option and saved rule (role_rule.json written).
- Body type gallery (Players > Profile > Body types...): opens, shows the 11 generic types and the "probe not run" warning (probe was run later; gallery not reopened after).

NOT VERIFIED / FAILED
- Injury / weather / time of day / difficulty / CPU subs effects in a played match: Apply was REFUSED by Turbo: "Tactics: 0 write(s) sent to the game, 1 refused (match.injury_frequency_user: game hooks are off for this game build)". No match was played.
- Role re-apply after season reset, role_reapply_off.txt, formation write/save/reload, input shield with a match, Attribulator A/B, overrides cleared on exit: not tested.
- Role rule Preview was queued but no career event followed.

## UPDATE 20:30: Turbo 2.0.1 (signature table for game build 6AC07E31-2145C000)
- The old patterns still match exactly once in the new build (code shifted a few KB). 122 of 122 found live; after install: "game hooks: 122 found, 0 missing", 12 hooks active (game_tick, career events, job/manager, edit_unlock, callname voice/kick-off, team names, player capture).
- VERIFIED: with 2.0.1 deployed, Tactics > Injury frequency (your team) = 100 > Apply: "game call gamevar set GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER = 100: ok" (was refused on 2.0.0). Game stable through load, contract, overlay (2 restarts).
- STILL NOT VERIFIED: any effect in a played match (injuries, weather, difficulty, CPU subs), role re-apply after reset, role_reapply_off.txt, formation write/reload.

## UPDATE 20:50: played match on 2.0.1 (Napoli v Nott'm Forest, European Shield, Aug 1)
- VERIFIED IN A PLAYED MATCH: Weather override = 3 (applied through Tactics > Conditions, log "match setup: OVERRIDE/WEATHER = 3") -> the match shows "Snow" on the pre-match screen and snow on the pitch; the match loads and plays 4+ minutes with the hooks active, no crash. Weather id 3 = Snow. Time of day override 4 accepted by the game (match shows 5:00 PM, daylight).
- Not tested in a match: injury frequency effect (write accepted earlier), difficulty, CPU subs, role re-apply, formation write/reload.

## UPDATE 21:00: second played match on 2.0.1 (Napoli v Nott'm Forest, Aug 1, turbo04 loaded from its 20:31 autosave)
Applied in Tactics (all 8 "game call gamevar set ...: ok" in turbo_gui.log 20:50:37): injury frequency + severity 100 for both teams, weather 1, time of day 0, difficulty Legendary (5), CPU makes no substitutions.
- VERIFIED weather: pre-match "Partly Clear" (id 1); earlier match "Snow" (id 3).
- VERIFIED injuries (frequency/severity 100): Match Facts > Events: injuries 1' Gibbs-White (NFO), 3' Kalimuendo (NFO), 5' Neres (NAP, "Torn Calf Muscle"), later Di Lorenzo (NAP); the game forced substitutions ("You must substitute the injured player before resuming the match").
- VERIFIED CPU no substitutions (as far as observed, to 9:36 of a 12-minute match): Forest made NO substitution for its two injured players; no Forest substitution event.
- NOT VERIFIED: difficulty Legendary (pre-match line still says "Amateur"; no in-match place shows the level); time of day (0 and 4 both showed 5:00 PM daylight).
- Squad roles VERIFIED with live readback: Preview showed the game's roles (Crucial/Important...); Apply set Rotation for all 34; Preview again: old role == new role (Rotation).
- Role re-apply after season reset VERIFIED at handler level (Lua Engine, event 23 sent to squad_role.on_event; no season rollover played): roles damaged to 1 -> event 23 -> back to 3 for 34/34 (log "squad_role: re-applied the saved rule after event 23"); with turbo_output\role_reapply_off.txt: stayed 1 for 34/34, no re-apply line; file removed -> event 23 -> 3 again. Files role_preview_*.json.
- Process watch: the first match's game process ended between 20:33 and 20:44 with no crash dump, no Application Error and no WER report (cause unknown, possibly closed outside the session). The second session ran 20:45-21:00 without an exit (fc27_process_watch.txt); closed by me (taskkill, not saved).
- Formation write: not a 2.0 feature (Tactics formation is preview-only by design). Attribulator sliders: [RE], not applied to the game (N/A).

## UPDATE 21:12: Turbo 2.0.2 (bcfef5a) deployed and checked in game
- turbo\signatures_6AC07E31-2145C000.json removed from the install: "game hooks: built-in signature table for build 6AC07E31-2145C000 (122 signatures)", "122 found, 0 missing, 0 ambiguous", game_tick + 11 hooks installed, prompt Lua commands armed; overlay shows v2.0.2.
- Overrides do not persist across a game restart: the new session had no "Game overrides are active" banner and the Aug 1 pre-match showed the game's own weather "Rain" (previous session forced "Partly Clear").
- Weather 3 applied on 2.0.2 AFTER the pre-match screen ("game call gamevar set OVERRIDE/WEATHER = 3: ok"): pre-match label still said Rain, the played match had snow on the pitch (the override is read at kick-off).
- Auto-adapt (for future title updates) is covered by native tests only: it cannot be exercised in game until EA ships another build.

## UPDATE 21:45: Turbo 2.0.2 final (eaa315d, review fixes) checked in game and RELEASED
- Upgrade case: the 2.0.1 turbo\signatures_6AC07E31-2145C000.json was put back in the install: "ignored: identical to the Turbo 2.0.1 package file, using the built-in table"; "built-in signature table for build 6AC07E31-2145C000 (122 signatures)"; "122 found, 0 missing, 0 ambiguous" incl. the new edit_cfg_category_items pattern (match 0x1470D3FD6) and the 16 new instruction checks (e.g. player_inserted_event_vtable found); 12 hooks installed; game call "OVERRIDE/WEATHER = 3: ok". Game stable.
- Released: https://github.com/cmx24/FC27-Editor-Turbo/releases/tag/v2.0.2 (Latest), FC27_LE_Turbo_2.0.2.zip sha256 73f5e43b..., tag v2.0.2 on eaa315d.
