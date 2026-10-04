# FC 26 Live Editor → Turbo for FC 27: feature parity (Turbo 1.0.2)

The 41 FC 26 Live Editor feature groups and 16 FC 26 Lua scripts below come from the assessment of your
`C:\FC 26 Live Editor` and `C:\FC 27 Live Editor` installs (changelogs, Lua libraries, UI strings, `DOC.MD`).
"FC 27 LE" is what FC 27 Live Editor itself already offers (its own window, F-key menus), which Turbo runs next to.

**What changed since 0.2.5.** FC 27 Live Editor v27.1.2 has no natives for transfer lists, the transfer budget, player
development and several other FC 26 features. Turbo 1.0 no longer waits for them: Turbo.dll calls the game's own code for
these features (the Transfer Hub, the job market, the scouting and development managers, the standings view, the match
settings), on the game thread, through 37+ game signatures for FC 27 build 1.0.140.64835. Every game call and hook has a
kill-switch file in `turbo_output` (`call_*_off.txt`, `hook_*_off.txt`).

FC 27 Live Editor's own "Miscellaneous Features" (`le_misc_features.json`) still covers CPU vs CPU, unlimited subs, never
tired, disabling the manager market, negative status checks, transfer / loan approval, stadium / kick-off time / crowd
overrides and remove suspension.

**Status key**

| Status | Meaning |
| --- | --- |
| **Verified in game** | Seen working in a test FC 27 career |
| **Untested in a match** | Built and tested offline; the effect only shows during or after a played match, which has not been checked yet |
| **In Turbo (from 0.x)** | Carried over from an earlier Turbo version; not on the 1.0 in-game check list |
| **Database tab** | No dedicated editor; the table can be edited field by field in Turbo's Database tab |
| **FC 27 LE** | Already in FC 27 Live Editor's own window; Turbo does not duplicate it |
| **Not in Turbo 1.0** | FC 27 Live Editor covers it, or it may come in a later Turbo version |
| **Not possible in FC 27** | The game has no such thing to edit or switch |

## Player editor

| # | FC 26 feature | FC 26 since | FC 27 LE | Turbo 1.0.2 |
| --- | --- | --- | --- | --- |
| 1 | Player editor: attributes, positions, playstyles | v26.1.0 | yes | **Verified in game**: sliders, combos, check boxes, PlayStyles / PlayStyles+ / Traits / Traits+, star-head (real face), tattoo, hair and item galleries, undo |
| 2 | Edit player or VPRO in Player Career | v26.1.1 | yes (v27.1.1) | **Verified in game**: the player editor works on your own player like any other. No dedicated VPRO page: use FC 27 LE's |
| 3 | Release clause | v26.1.2 | not announced | **Verified in game**: release clause and wage are fields of the player editor (Contract & Clubs) |
| 4 | Transfer, loan, terminate loan, release from team | v26.1.4 | not announced | **Not in Turbo 1.0** for your own club: moves through the game's engine come in a later version. Turbo refuses database-only moves into or out of your club for safety. Between two other clubs Turbo writes the move into the career database (back up your save) |
| 5 | Delete player, generate miniface, change name | v26.1.5 | not announced | Miniface from the game's 3D model: **Verified in game**. Miniface from an image file: **Verified in game**. Names: player editor, **Verified in game**. Delete player: refused for your own club |
| 6 | Season statistics | v26.1.7 | not announced | Live season statistics: **Not in Turbo 1.0**. **In Turbo (from 0.x)**: CSV export of the database's league numbers (labelled as not live) |
| 7 | Team selection bias, development XP boost | v26.2.1 | not announced | Develop to potential: **Verified in game**. Weekly forced growth: **Untested in a match**. FC 26's XP multiplier: **Not possible in FC 27** (the natives are gone; weekly forced growth replaces it). Team selection bias: **Not in Turbo 1.0** |
| 8 | Transfer- and loan-listed flags | v26.2.2 | not announced | **Verified in game** (transfer list, remove; loan list built, not yet seen): Transfer list / Loan list / Remove from lists for your own players, through the game's Transfer Hub. Asking price / loan terms: **Not possible in FC 27** (the game's listing takes only the player) |
| 9 | Create player, clone player, FUT card presets | v26.2.4 | partly | Export / import (Live Editor preset CSV and Turbo JSON, names included) and clone: **Verified in game**. Create player from a template: **Verified in game** |
| 10 | Sock style, Super Sub trait, tattoo picker | v26.3.1, v26.3.5 | not announced | **Verified in game**: traits as tick boxes, tattoo gallery, appearance fields (socks as a value) |
| 11 | Bulk edit players (release clause, fitness, heal, injury, never tired, dev XP, no decline) | v26.3.2, v26.3.5 | not announced | **In Turbo (from 0.x)**: Bulk edit for your squad / team IDs / the listed players / everyone. Never tired: FC 27 LE |
| 12 | Filters: playstyles, Is Retiring, preferred positions 5–7 | v26.3.2 | UI only | **In Turbo (from 0.x)**: position (any of the 7), PlayStyle / PlayStyle+, retiring, min OVR / POT, max age |
| 13 | Remove player suspension | v26.3.6 | yes (v27.1.2) | **Not in Turbo 1.0** (FC 27 LE has it) |
| 14 | Match sharpness | Lua API | gone | **Not possible in FC 27** (removed from FC 27) |

## Team editor

| # | FC 26 feature | FC 26 since | FC 27 LE | Turbo 1.0.2 |
| --- | --- | --- | --- | --- |
| 15 | Team editor: core | v26.1.0 | yes | **Verified in game**: Teams tab |
| 16 | Team name, transfer budget, transfer bans | v26.1.2 | partly | Team crest: **Verified in game**. Team name: **Verified in game** (career card, contract offer, hub, fixtures, news; Live Editor shows it after its next start). Club colours: kept in the save, not yet seen in a match. Kit colours: the game reloads kits at every career load; Turbo 1.0.2 keeps them and writes them again then (built and tested offline, not yet seen in game). Transfer budget: **Verified in game**. Transfer bans: **Not possible in FC 27** (no ban list in the game) |
| 17 | Coaches, scouts, perfect staff | v26.1.4 | not announced | **Database tab** |
| 18 | Standings, fixtures, match-fixing | v26.1.7 | not announced | League table edits shown on the game's own Standings screen without advancing: **Verified in game**. Editing a played result: **Untested in a match**. Forced result for a chosen fixture (opt-in): **Untested in a match**. Fixtures CSV: **In Turbo (from 0.x)** |
| 19 | Create job offer | v26.2.1 | not announced | **Verified in game**: Managers > Job offers; pick a club and the game makes a real contract offer |
| 20 | Formation editor, team traits | v26.2.2, v26.3.1 | not announced | Team traits: Teams editor, **Verified in game**. Formation editor UI: **Not in Turbo 1.0** (**Database tab** only) |
| 21 | Teams list transfer columns | v26.3.5 | not announced | **Not in Turbo 1.0** (transfer history is exported to CSV) |

## Manager editor

| # | FC 26 feature | FC 26 since | FC 27 LE | Turbo 1.0.2 |
| --- | --- | --- | --- | --- |
| 22 | Manager editor: core | v26.2.7 | basic | **Verified in game**: Managers tab |
| 23 | Manager and team ID, name, miniface import / generate | v26.2.8 | not announced | IDs and names: **Verified in game**. Miniface from the game's 3D model: **Verified in game**. Miniface from an image file: **Verified in game** |
| 24 | Transfer or fire manager, job security, unsackable | v26.2.9 | unsackable (Misc Features) | Job security levels and unsackable: **Verified in game**. Move a manager / make a manager available: **Verified in game** |
| 25 | Manager traits | v26.3.1 | not announced | **Database tab** (`manager` table) |

## Career

| # | FC 26 feature | FC 26 since | FC 27 LE | Turbo 1.0.2 |
| --- | --- | --- | --- | --- |
| 26 | Youth academy tools | v26.1.3 | partly | **Built, not yet seen in game**: list the academy, set a youth player's potential, position, tier and potential range. FC 27 LE has youth scout reports |
| 27 | Reveal player data | v26.1.4 | not announced | **Verified in game**: one player, a club or a league marked fully scouted by the game's own scouting |
| 28 | Match setup: stadium, weather, kick-off time, crowd | v26.1.5 | yes (misc features) | Weather and difficulty (accepted by the game) and fixture home / away swap: **Verified in game**. Time of day, CPU substitutions off and opponent change: built, not yet seen in game. Stadium override: **Not in Turbo 1.0** (left out on purpose: an id the game cannot load stalls the match load). Kick-off time, crowd: FC 27 LE |
| 29 | Transfer history | v26.1.8 | not announced | **In Turbo (from 0.x)**: export to CSV |
| 30 | Gameplay: CPU vs CPU, never tired, unlimited subs, match time and score | v26.2.0 | partly (misc features) | Injuries-off switch: **Verified in game** (the game accepts it); injuries off during a match: **Untested in a match**. CPU substitutions off: **Verified in game**. Forced result: **Untested in a match**. CPU vs CPU, unlimited subs, match length, offsides / referee switches: **Not in Turbo 1.0**. VAR off: **Not possible in FC 27** (no setting). Fatigue off as a match switch: **Not possible in FC 27** (FC 27 LE's "never tired" covers it) |
| 31 | Player Career: funds, wage, attribute points, personality, playstyle slots | v26.2.1 | yes (v27.1.1) | **Not in Turbo 1.0** (FC 27 LE has it) |
| 32 | Contract objectives bypass, negotiation check, always allow approach | v26.2.1, v26.2.5 | negotiation status check, transfer / loan approval (Misc Features) | **Not in Turbo 1.0** (FC 27 LE's Misc Features) |
| 33 | Unsupported CM teams and leagues | v26.2.6 | not announced | **Not in Turbo 1.0** |
| 34 | Disable manager market | v26.2.9 | yes (Misc Features) | **FC 27 LE**; Turbo moves managers and makes them available (row 24) |
| 35 | Endless manager career | v26.3.4 | not announced | **Not possible in FC 27**, and not needed: FC 27 has no season limit or forced end for your manager |

## Tool and scripting

| # | FC 26 feature | FC 26 since | FC 27 LE | Turbo 1.0.2 |
| --- | --- | --- | --- | --- |
| 36 | Speedhack (menu, gameplay), hotkeys | v26.1.0–v26.3.6 | yes | **Not in Turbo 1.0** (FC 27 LE has it) |
| 37 | Lua script on hotkey, CJK font, font size | v26.3.6 | not announced | **FC 27 LE** for scripts; Turbo has its own show/hide key only |
| 38 | Legacy file browser | v26.1.0 | yes | **Not in Turbo 1.0** (FC 27 LE has it) |
| 39 | Lua: WriteJMP, AllocateMemory, DeallocateMemory, LegacyFileExist, LegacyFileExport | v26.3.6 | partly | **FC 27 LE** (these are its Lua API) |
| 40 | Lua: Gameplay Attribulator | v26.2.2 | partly | **Not in Turbo 1.0** (FC 27 LE's Lua library) |
| 41 | Database editor | FC 27 only | yes (v27.1.2) | **Verified in game**: Database tab |

## Turbo 1.0 features FC 26 Live Editor did not have

| Feature | Turbo 1.0.2 |
| --- | --- |
| Callnames: the commentators speak the name you pick, in the commentary language the game has loaded (assign by name or by player) | **Verified in game**. Turbo builds the spoken set from the game's own audio check when the game binds the bank (Create Player screen or a match). Italian: 2,462 surnames, 751 player recordings |
| Callname heard in a match | Generic callnames (By name): **Verified in game** on 2026-10-04 with 1.0.1 (Bianchi, Pirlo and Del Piero given as last names were spoken in a match). A player with his own recording is spoken by it whatever is assigned (Miguel Gutierrez): 1.0.2 knows these players from the master list too and asks before writing. Player-specific callnames: the game reloads them at every career load; Turbo 1.0.2 keeps them and writes them again then (not yet seen in game) |
| Instant overlay commands: Turbo's buttons work at once, no day advance needed | **Verified in game** |
| Status tab: connection, game calls, hooks, log | **Verified in game** |

## The 16 FC 26 scripts FC 27 dropped

All 16 were ported in Turbo 0.1 and, except the two ID-map scripts, are buttons in the Turbo window.

| FC 26 script | Turbo |
| --- | --- |
| auto_max_user_team_sharpness, auto_max_user_team_form_morale_sharpness | Turbo Tools → Your squad: form / morale / fitness now or every day (no sharpness in FC 27) |
| custom_headassetid_to_playerid, custom_tattoos_to_playerid | `turbo_custom_headassets`, `turbo_custom_tattoos` scripts (ID maps in `turbo_config.json`) |
| delete_generated_players | Turbo Tools → Database maintenance (count, then delete with confirmation) |
| export_fixtures, export_season_stats, export_transfer_history | Turbo Tools → Exports |
| extend_cpu_players_contracts, extend_user_team_players_contracts | Turbo Tools → Your squad |
| fix_players_headmodels | Turbo Tools → Database maintenance (capture the FC 27 real-face list, then apply) |
| list_transfer_bans, transfer_ban_all_teams | **Not possible in FC 27** (no ban list in the game); the buttons stay greyed out |
| mass_edit_squadrole | Turbo Tools → Your squad → Squad role |
| pap_all_playstyles | Turbo Tools → Player Career |
| print_team_jersey_numbers | Turbo Tools → Exports → Jersey numbers |

## Summary

Of the 41 groups, by status (many rows have parts with different statuses; each row says which):

- **Verified in game**, in full or for the main part: 1, 2, 3, 5, 8, 9, 10, 15, 16, 18, 19, 22, 23, 24, 26, 27, 28, 41. <!-- verify: 8, 16, 24, 26, 28 and parts of 5, 9, 23, 30 -->
- **Untested in a match**: parts of 7, 18 and 30 (forced result, injuries off during a match, editing a played result,
  weekly forced growth), and a callname heard in a match.
- **In Turbo (from 0.x)**: 11, 12, 29, and parts of 6 and 18.
- **Database tab**: 17, 25, and formations (row 20).
- **FC 27 LE**: 34, 37, 39.
- **Not in Turbo 1.0**: 4 (your own club), 13, 21, 31, 32, 33, 36, 38, 40, and parts of 6, 7, 20, 28 and 30.
- **Not possible in FC 27**: 14, 35, and parts of 7, 8, 16 and 30.

## Verification (what was executed, what was not)

| Check | Result |
| --- | --- |
| Inside FC 27 (build 1.0.140.64835) with FC 27 Live Editor, in a test Manager Career | Every feature marked **Verified in game** was seen working |
| Played matches | Not run yet for the **Untested in a match** features: forced result, injuries off during a match, editing a played result, weekly forced growth, callname heard in a match |
| Offline tests (Lua tests over a simulated game memory, native tests with AddressSanitizer + UBSan, Wine smoke and overlay tests, FC 27 schema check) | See `docs/turbo-reference.md` and the README's Status table |
