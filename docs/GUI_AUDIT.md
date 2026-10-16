# GUI_AUDIT - Turbo GUI tabs and panels (2.10/rc, 2026-10-16, track A6)

Sources: turbogui/src/ui/app.cpp (top-level tabs), ui_players/teams/competitions/tools/club_tools/game_settings/tactics.cpp (sub-tabs, sections), memory notes turbo-2-6/2-8/2-10 state. "Proven" = shown working in the game (LEICESTER tests) per those notes; "DB" = writes the career database through the Lua/Database layer, covered by native tests and used in earlier releases without in-game contradiction (not re-proven on 2.10). Hide = Advanced view only (A6 toggle); Merge/Keep = suggestions for the real 2.12 simplification.

## Top-level tabs (order and index fixed: App::request_tab)
| Tab | Purpose | Proven in game? | Simple view | Suggestion |
|---|---|---|---|---|
| Players | search/edit players, faces, callnames | DB edits yes; 3D hair/beard faces yes (2.8, few ids); callname yes (10-04 tests) | shown | Keep; merge sub-tabs |
| Teams | club overview, squad, transfers, kits, crest, tactics | kits3d Leicester home yes (2.6); crest/name DB; Tactics NO | shown (Tactics sub-tab hidden) | Keep; one "Club look" tab for Kits + Crest + Colours |
| Managers | manager edit, scouts | DB only | shown | Keep |
| Competitions | live standings, match setup, gameplay tuning, DB copy | standings yes (2.2); match setup / tuning: no effect proven | shown (standings + DB copy only) | Keep as "Standings" |
| Database | raw table browser/editor | DB only, expert tool | HIDDEN | Hide (Advanced) |
| Turbo Tools | squad, scouting, budget, exports, bulk edit, maintenance, safety | DB / Lua features since 0.1 | shown | Keep; collapse rarely used sections |
| Status | health, hooks, settings (zoom, density), log | yes (diagnostics) | shown | Keep; Technical details under Advanced |
| Game settings | EA Custom slider block + presets | write + EA screen read-back proven (A210); effect on play NOT proven (G-S3 FAIL) | HIDDEN + banner in Advanced | Hide until a slider earns an effect |

## Sub-tabs / panels
| Where | Panel | Purpose | Proven? | Simple | Suggestion |
|---|---|---|---|---|---|
| Players | Profile, Names, Attributes, PlayStyles, Appearance, Miniface, Contract & Clubs, Callname, Growth | edit one player | DB yes; Callname yes; 3D faces partly | shown | Merge Names+Callname, Attributes+PlayStyles+Growth |
| Players | All fields | raw player row | DB | shown | Hide (Advanced) next |
| Teams | Overview, Squad, Transfers, Name, Colours, Kits, Crest, Job offer, Mass actions | club editing | Kits home/GK proven; others DB | shown | Merge Name+Colours+Kits+Crest |
| Teams | All fields | raw team row | DB | shown | Hide (Advanced) next |
| Teams | Tactics | slider profiles (formation, depth, width, press; DB rows + game variables) | NO in-game effect (L5 exit at menu, G-T1 inconclusive, no slider earned) | HIDDEN + banner | Hide; revisit only after Branch A passes |
| Competitions | Live standings (game) | read/edit the engine's table | yes (2.2) | shown | Keep |
| Competitions | Match setup | next fixtures, fixed results, gameplay switches | not proven | HIDDEN + banner | Hide |
| Competitions | Gameplay tuning | engine tuning tables, presets | no slider earned (2.6) | HIDDEN + banner | Hide |
| Competitions | Career database copy | copy leagueteamlinks | DB | shown | Move to Turbo Tools |
| Turbo Tools | Your club: customisation hub (kits, crest, stadium) | club tools | kits yes | shown | Merge into Teams > Kits |
| Turbo Tools | Bulk edit, Exports, Transfer bans, Database maintenance, Safety | utilities | DB | shown | Bulk edit + maintenance to Advanced |
| Status | Technical details, Game hooks, Log | diagnostics | yes | shown | Advanced |

## What A6 changes (code)
- gui.advanced_mode (default false = Simple) in gui_settings.json via GuiPrefs; "Advanced view" checkbox on the status line.
- Simple hides the Database and Game settings tabs plus Teams > Tactics, Competitions > Match setup and > Gameplay tuning; a grey line under the status bar says they are under Advanced.
- Advanced shows everything and puts a yellow "no proven in-game effect" line on Game settings, Tactics, Match setup and Gameplay tuning.
- Plan deviation: LEAP_PLAN A6 named six tabs (Players, Teams, Kits, Faces, Gameplay, Tools); the GUI has no top-level Kits/Faces/Gameplay tabs (they are sub-tabs), so the toggle works on the real tab structure. The merges above, hiding the raw "All fields" tabs and a first-run choice are 2.12 topics.
- Not verified: screenshots of both views in the hub (no game was run for this track); native tests + build only.
