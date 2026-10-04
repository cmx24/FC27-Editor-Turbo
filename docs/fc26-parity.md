# FC 26 Live Editor → Turbo for FC 27: feature parity (Turbo 0.2.5)

The 41 FC 26 Live Editor feature groups and 16 FC 26 Lua scripts below come from the assessment of your
`C:\FC 26 Live Editor` and `C:\FC 27 Live Editor` installs (changelogs, Lua libraries, UI strings, `DOC.MD`).
"FC 27 LE" is what FC 27 Live Editor itself already offers (its own window, F-key menus), which Turbo runs next to.

**FC 27 Live Editor v27.1.2 limits (found in game, 02-10-2026).** The natives behind transfers, loans, release, terminate loan,
transfer / loan lists, transfer bans, the transfer budget, player deletion, player development and season stats are not in
this Live Editor build (their FC 26 Lua wrappers are). Those rows are marked *needs LE native*: the Turbo buttons exist and
are greyed out with the reason until Live Editor ships the native. FC 27 Live Editor's own "Miscellaneous Features"
(`le_misc_features.json`) covers CPU vs CPU, unlimited subs, never tired, disabling the manager market, negative status
checks, transfer / loan approval, reveal player data, stadium / weather / kick-off time / crowd overrides and unsackable.

**Status key**

| Status | Meaning |
| --- | --- |
| **Turbo GUI** | In the Turbo window, driven end-to-end in tests (see Verification) |
| **Turbo GUI (partial)** | In the Turbo window, but part of the FC 26 feature is missing (said in the row) |
| **Database tab** | No dedicated editor; the table can be edited field by field in Turbo's Database tab |
| **FC 27 LE** | Already in FC 27 Live Editor's own window; Turbo does not duplicate it |
| **Not available** | Needs code hooks inside FC27.exe found by in-game analysis; nothing in Turbo yet |
| **Gone** | Removed from FC 27 by EA |

## Player editor

| # | FC 26 feature | FC 26 since | FC 27 LE | Turbo 0.2.5 |
| --- | --- | --- | --- | --- |
| 1 | Player editor: attributes, positions, playstyles | v26.1.0 | yes | **Turbo GUI**: Profile, Attributes (grouped like the game), positions 1–7, PlayStyles / PlayStyles+ / Traits / Traits+, Appearance, dates as real calendar dates, all fields |
| 2 | Edit player or VPRO in Player Career | v26.1.1 | yes (v27.1.1) | **Turbo GUI (partial)**: the editor works on your own player like any other; "Give my player every PlayStyle" (now or automatic). No dedicated VPRO page: use FC 27 LE's |
| 3 | Release clause | v26.1.2 | not announced | **Turbo GUI**: `players.releaseclause` (and `wage`) editable in Players > Contract & Clubs (fields confirmed in the FC 27 schema dump) |
| 4 | Transfer, loan, terminate loan, release from team | v26.1.4 | not announced | **Turbo GUI**, *needs LE native* (`cTransferPlayer`, `cLoanPlayer`, `cReleasePlayer`, `TerminateLoan` missing in v27.1.2) |
| 5 | Delete player, generate miniface, change name | v26.1.5 | not announced | **Turbo GUI (partial)**: Delete player (with confirmation), *needs LE native* (`DeletePlayer` missing in v27.1.2). Names: edit the `editedplayernames` table in the Database tab. Minifaces: **Not available** (image pipeline) |
| 6 | Season statistics | v26.1.7 | not announced | **Turbo GUI (partial)**: export to CSV, *needs LE native* `GetPlayersStats` for live stats; with v27.1.2 it exports the database's league goals / cards (labelled as not live) |
| 7 | Team selection bias, development XP boost | v26.2.1 | not announced | **Turbo (development)**: Players > Growth tab and Tools > Scouting, development and youth academy: develop to potential, +N per attribute, set potential / growth profile, weekly forced growth with no decline (auto), written to the players table and the game's development plan (docs/re/development.md). FC 26's XP multiplier natives are not in FC 27 LE (Bulk edit's Development stays greyed). Team selection bias: **Not available** |
| 8 | Transfer- and loan-listed flags | v26.2.2 | not announced | **Turbo GUI**: Players editor and Contract & Clubs: Transfer list / Loan list / Remove from lists / List status through Turbo.dll's game call (the game's own Transfer Hub actions, your own players; `docs/re/transfer_lists.md`); Live Editor's missing `cAddPlayerToTransferList` & co. are defined on top of it. FC 27 sets no asking price when listing. Needs the in-game test |
| 9 | Create player, clone player, FUT card presets | v26.2.4 | partly | **Not available** in Turbo (FC 27 LE's preset manager loads) |
| 10 | Sock style, Super Sub trait, tattoo picker | v26.3.1, v26.3.5 | not announced | **Turbo GUI (partial)**: sock and tattoo fields as values in Appearance; traits as tick boxes; no visual picker. Tattoo / head maps by ID: `turbo_custom_tattoos`, `turbo_custom_headassets` scripts |
| 11 | Bulk edit players (release clause, fitness, heal, injury, never tired, dev XP, no decline) | v26.3.2, v26.3.5 | not announced | **Turbo GUI (partial)**: Bulk edit (new in 0.2.2) for your squad / team IDs / the players the list shows / everyone: any players-table field, fitness, form, morale, development XP and no decline. Heal, injury, never tired, release clause: **Not available** |
| 12 | Filters: playstyles, Is Retiring, preferred positions 5–7 | v26.3.2 | UI only | **Turbo GUI** (new in 0.2.2): position (any of the 7), PlayStyle / PlayStyle+, retiring, min OVR / POT, max age |
| 13 | Remove player suspension | v26.3.6 | yes (v27.1.2) | **FC 27 LE** |
| 14 | Match sharpness | Lua API | gone | **Gone** (no sharpness in FC 27; folded out of form/morale) |

## Team editor

| # | FC 26 feature | FC 26 since | FC 27 LE | Turbo 0.2.5 |
| --- | --- | --- | --- | --- |
| 15 | Team editor: core | v26.1.0 | yes | **Turbo GUI**: Teams tab: overview fields, squad with kit numbers and line-up slots, all fields |
| 16 | Team name, transfer budget, transfer bans | v26.1.2 | partly | **Turbo GUI (partial)**: `teamname` editable; FC 27 has no `teams.transferbudget` (Turbo Tools budget section, *needs LE native* `SetUserTransferBudget`); transfer bans: list, ban every team, remove all, one club (Teams > Overview), one player (Contract & Clubs), *needs LE native* (`cGetTransferBans` missing in v27.1.2) and FC 27 has no ban list Turbo could call instead (`docs/re/transfer_lists.md` section 6). The game may show translated club names instead of `teamname` (FC 27 LE has a custom-names file for that) |
| 17 | Coaches, scouts, perfect staff | v26.1.4 | not announced | **Database tab** |
| 18 | Standings, fixtures, match-fixing | v26.1.7 | not announced | **Turbo GUI (partial)**: fixtures and results to CSV. Standings view: no. Match-fixing: **Not available** |
| 19 | Create job offer | v26.2.1 | not announced | **Turbo GUI**: Managers > Job offers: pick a club, Create job offer; the game's own JobMarketManager applies and answers at once through Turbo.dll's game call (docs/re/job_offer.md). Club jobs only; the Lua runner `turbo_job_offer.lua` does the same from `turbo_config.json`. Needs the in-game test (0.4.1) |
| 20 | Formation editor, team traits | v26.2.2, v26.3.1 | not announced | **Turbo GUI (partial)**: team traits (`trait1` / `trait2` in Teams overview). Formations: **Database tab** only |
| 21 | Teams list transfer columns | v26.3.5 | not announced | **Not available** (transfer history is exported to CSV) |

## Manager editor

| # | FC 26 feature | FC 26 since | FC 27 LE | Turbo 0.2.5 |
| --- | --- | --- | --- | --- |
| 22 | Manager editor: core | v26.2.7 | basic | **Turbo GUI**: Managers tab (names, team, nationality, personality, looks, all fields) |
| 23 | Manager and team ID, name, miniface import / generate | v26.2.8 | not announced | **Turbo GUI (partial)**: IDs and names editable; minifaces **Not available** |
| 24 | Transfer or fire manager, job security, unsackable | v26.2.9 | unsackable (Misc Features) | **Turbo GUI**: Managers > Manager rules (job security safe / okay / insecure / very insecure / a score / the game's own, through the game's own `UpdateJobSecurityScore`; unsackable: an opt-in hook refuses `JobSwitchManager::SackManager`) and Manager market (move a manager, swapping with the club's manager; make one a free agent: career database `manager.teamid`, what the game's AI hiring reads). `docs/re/manager_rules.md`; needs the in-game test |
| 25 | Manager traits | v26.3.1 | not announced | **Database tab** (`manager` table) |

## Career

| # | FC 26 feature | FC 26 since | FC 27 LE | Turbo 0.2.5 |
| --- | --- | --- | --- | --- |
| 26 | Youth academy tools | v26.1.3 | partly | **FC 27 LE** (youth scout reports) + **Turbo (youth)**: list the academy, set a youth player's potential, position, tier and potential range width |
| 27 | Reveal player data | v26.1.4 | not announced | **Turbo (reveal, game call)**: one player, a club or a league marked fully scouted through the game's own PlayerDataRevealManager (Turbo.dll, kill switch call_reveal_off.txt; docs/re/development.md) |
| 28 | Match setup: stadium, weather, kick-off time, crowd | v26.1.5 | yes (misc features) | **Turbo GUI (partial)**: Competitions > Match setup: weather, time of day, difficulty for the next matches, venue swap / another opponent for an unplayed fixture; stadium, kick-off time, crowd: FC 27 LE's misc features (docs/re/match_setup.md) |
| 29 | Transfer history | v26.1.8 | not announced | **Turbo GUI (partial)**: export to CSV; in FC 27 read from the TransferManager's lists (completed transfers and loans of the season) |
| 30 | Gameplay: CPU vs CPU, never tired, unlimited subs, match time and score | v26.2.0 | partly (misc features) | **Turbo GUI (partial)**: injuries off, injury sliders beyond the menu, CPU subs off, a fixed result for a chosen fixture (opt-in hooks); CPU vs CPU, never tired, unlimited subs: FC 27 LE's misc features |
| 31 | Player Career: funds, wage, attribute points, personality, playstyle slots | v26.2.1 | yes (v27.1.1) | **FC 27 LE** |
| 32 | Contract objectives bypass, negotiation check, always allow approach | v26.2.1, v26.2.5 | negotiation status check, transfer / loan approval (Misc Features) | **FC 27 LE** for now; Turbo's own not built (leads: `docs/re/manager_rules.md` section 8) |
| 33 | Unsupported CM teams and leagues | v26.2.6 | not announced | **Not available** (a pre-career `modeavailability` gate; leads: `docs/re/manager_rules.md` section 9) |
| 34 | Disable manager market | v26.2.9 | yes (Misc Features) | **FC 27 LE**; Turbo moves managers and makes them free agents (row 24) |
| 35 | Endless manager career | v26.3.4 | not announced | **Not needed**: FC 27's career code has no season limit or forced end for your manager (`docs/re/manager_rules.md` section 7) |

## Tool and scripting

| # | FC 26 feature | FC 26 since | FC 27 LE | Turbo 0.2.5 |
| --- | --- | --- | --- | --- |
| 36 | Speedhack (menu, gameplay), hotkeys | v26.1.0–v26.3.6 | yes | **FC 27 LE** |
| 37 | Lua script on hotkey, CJK font, font size | v26.3.6 | not announced | **FC 27 LE** for scripts; Turbo has its own show/hide key only |
| 38 | Legacy file browser | v26.1.0 | yes | **FC 27 LE** |
| 39 | Lua: WriteJMP, AllocateMemory, DeallocateMemory, LegacyFileExist, LegacyFileExport | v26.3.6 | partly | **FC 27 LE** (these are its Lua API); `turbo_probe` / Probe report lists which of them this install has |
| 40 | Lua: Gameplay Attribulator | v26.2.2 | partly | **FC 27 LE** (its Lua library) |
| 41 | Database editor | FC 27 only | yes (v27.1.2) | **Turbo GUI**: Database tab (any table, filter, double-click to edit, range-checked) |

## The 16 FC 26 scripts FC 27 dropped

All 16 are ported (Turbo 0.1) and, except the two ID-map scripts, are buttons in the Turbo window.

| FC 26 script | Turbo |
| --- | --- |
| auto_max_user_team_sharpness, auto_max_user_team_form_morale_sharpness | Turbo Tools → Your squad: form / morale / fitness now or every day (no sharpness in FC 27) |
| custom_headassetid_to_playerid, custom_tattoos_to_playerid | `turbo_custom_headassets`, `turbo_custom_tattoos` scripts (ID maps in `turbo_config.json`) |
| delete_generated_players | Turbo Tools → Database maintenance (count, then delete with confirmation) |
| export_fixtures, export_season_stats, export_transfer_history | Turbo Tools → Exports |
| extend_cpu_players_contracts, extend_user_team_players_contracts | Turbo Tools → Your squad |
| fix_players_headmodels | Turbo Tools → Database maintenance (capture the FC 27 real-face list, then apply) |
| list_transfer_bans, transfer_ban_all_teams | Turbo Tools → Transfer bans |
| mass_edit_squadrole | Turbo Tools → Your squad → Squad role |
| pap_all_playstyles | Turbo Tools → Player Career |
| print_team_jersey_numbers | Turbo Tools → Exports → Jersey numbers |

## Summary

Of the 41 groups: **7** are in the Turbo window in full (1, 4, 8, 12, 15, 22, 41), **12** partly (2, 3, 5, 6, 7, 10, 11, 16,
18, 20, 23, 29; each row says what is missing), **2** only through the Database tab (17, 25), **8** are left to FC 27 Live
Editor itself (13, 26, 31, 36, 37, 38, 39, 40), **1** is gone from the game (14), and **11** are **Not available** (9, 19, 21,
24, 27, 28, 30, 32, 33, 34, 35).

The Not available groups, plus the missing parts of partial rows (minifaces, match-fixing, team selection bias, heal / injury /
never tired in bulk edit), are mostly game switches: match setup, CPU vs CPU / never tired / subs / match time, contract and
negotiation bypasses, manager market, firing managers, endless career, reveal player data, job offers, unsupported CM teams.
They need code hooks inside FC27.exe that can only be found by analysing the running game, which cannot be done from here.
Creating or cloning players needs new database records, which Live Editor's Lua API does not offer. The transfer columns
in the teams list need the in-game transfer history inside the Turbo window; today it is exported to CSV. None of these are
claimed by this build.

## Verification (what was executed, what was not)

| Check | Result |
| --- | --- |
| Lua: 105 tests over a simulated game memory, run against xAranaktu's published Live Editor Lua code (GPL-3.0, identical to FC 27's where compared) plus the FC 27 files recovered from your install | all pass |
| Every GUI button that sends a command (28) clicked in the real GUI, its command run through Turbo's real Lua bridge in a simulated career | all succeed, except Turbo's own deliberate refusal to apply a real-face list of fewer than 100 players |
| Players list filters, compared with a brute-force calculation over the same players | match |
| Every table / field name Turbo uses (110) against EA's database schema and xAranaktu's FC 24–26 scripts (`scripts/check_field_names.py`) | 108 confirmed, 2 deliberately allowed, 0 unknown (2 wrong manager field names were found and fixed) |
| Native engine and every UI panel, with AddressSanitizer + UBSan | 3,283 checks pass |
| The real `Turbo.dll` inside a running Direct3D 12 program (a stand-in, not FC 27) under Wine + vkd3d + software Vulkan: loaded in launch mode while the program renders, waits for Live Editor's `Initial setup done`, hooks, initialises on its queue, F8 shows the window (screenshot), survives a swap-chain resize, no errors (`turbogui/tests/win/run_overlay_wine.sh`) | passes, both with TurboProbe.exe and with the in-game fallback |
| Inside FC 27 itself, with Live Editor | 0.2.3 (your screenshot of 02-10-2026): the game launches, the Turbo window draws in game on F8, but it did not connect to the database. 0.2.4 (database connection fix) in game: **not run yet**, use the checklist in `TURBO_README.md` |
