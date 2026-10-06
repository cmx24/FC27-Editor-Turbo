# Root cause: squad roles not applied, players over 30 skipped

I found no age-based skip in the code, so I can't confirm the cause from the repo alone. The Lua offline tests could not run here (`lua` is not installed and `turbo/le27/libs` is absent), so nothing below was executed.

## 1. What the code does (VERIFIED)

- **Age band:** `team_mass.lua:47-48` sets ROLE_ROTATION=3, ROLE_PROSPECT=5 and ADULT_AGE=19. `role_of` (`team_mass.lua:232-237`) returns 3 for every age ≥19, so a 30+ player is never treated differently.
- **Age calculation:** `age_of` (`team_mass.lua:111-119`) turns `players.birthdate` into days and then a date. It returns nil only when the birthdate is ≤0 or missing, and then `apply` leaves that player alone. The day-number conversion (`util.lua:212-228`) round-trips correctly (I hand-checked day 1 = 1582-10-15).
- **Contract table pass:** `career_playercontract` does not exist in FC 27 (`docs/re/player_status_roles.md` §3.4), so this pass is a no-op.
- **Memory pass:** it writes one role byte per vector entry, for every entry whose id is in `squad` and not in `skip` (`squad_role.lua:144-151`).

## 2. Candidate causes, ranked

**A. Squad membership is read from `cm_teamsheets` (`game.lua:78-98`), not from the club's real player list. (ASSESSED, most likely code-side cause.)**
- `user_squad()` walks `playerid0..52` and does `break` on the first nil or `-1` (`game.lua:88`). The `team_mass` action lists players differently, from `teamplayerlinks` (`team_mass.lua:60-74`).
- `role.apply` ignores that list and uses the teamsheet set instead (`squad_role.lua:105`).
- If the sheet has an empty slot before the reserves, or the sheet is shorter than the club's player list, everyone after the gap is silently excluded. That includes the 30+ veterans who sit in the later reserve slots.
- The summary shows only "N players", so the shortfall is invisible.
- The simulator (`world.lua:204-214`) writes a gap-free sheet, so tests can never catch this.
- **UNCERTAIN:** I did not see a real `cm_teamsheets` row, and I don't know the sheet's slot order in FC 27. This needs a live check (see §5).

**B. Roles are recomputed by the game. (VERIFIED from the RE doc, but not age-specific.)**
- Event 0x17 (rebuild), season reset and event 0x5F (signing) call `Clear`/`AddPlayer` and overwrite the whole table with `ComputeSquadRole` (`player_status_roles.md` §3.1).
- `ComputeSquadRole` returns a rank bucket 1..5, and a Future role on a player over 23 becomes Sporadic (`player_status_roles.md` §3.2).
- So after any such event, Turbo's roles revert to rank-based values. High-ranked veterans come back as Crucial/Important.
- This may be what "players over 30 not updating" looks like in game: the veterans are the ones the game rates Crucial/Important.
- It happens to everyone, but it is only visible on players whose computed role differs from Rotation. Turbo does not re-apply after these events.

**C. Smaller or less likely causes**
- **`loaned_in` filter (`squad_role.lua:84-94`):** it skips any squad player with a `playerloans` row whose `teamidloanedfrom` is not the user team. Stale rows could skip veterans. This is the default, since `include_loaned_in` is false in `team_mass.lua:238`. (ASSESSED, low.)
- **Missing birthdate:** `role_of` returns nil and the player is silently skipped, with no count in the summary. (ASSESSED, low.)
- **`locate()` rejecting the vector:** `valid == matched` (`squad_role.lua:71`) would reject the whole vector for a stray role byte. That would stop all players, not just older ones, and only a role byte of 6..0xFE (not 0xFF) triggers it. (ASSESSED, unlikely.)
- **52-entry cap:** the vector holds at most 52 entries and `AddEntry` refuses more. A squad over 52 would lose the rest regardless of age. (VERIFIED, not age-specific.)
- **Contract status:** `contract_status` (LOANED_IN) and retiring players do not matter, because the contract pass is dead and the memory pass ignores both.

## 3. Proposed fix (exact changes)

**1. Take the squad from the PSM vector itself.**
- Add `squad_role.lua` function `psm_members(layout)`. It returns every entry whose pid is a positive number (not `-1`).
- Use it as the memory-pass target, intersected with `teamplayerlinks` for the user team. `cm_teamsheets` stays only as the anchor that `locate` scores against.
- Drop the `break` on `-1` in `user_squad`: use `continue`/skip for gaps and loop over all fields.
- Fall back to `teamplayerlinks` when the sheet yields fewer players than the links list. Log the difference.

**2. Report what was skipped.**
- In the summary, add counts: players in `teamplayerlinks` with no vector entry, `role_of` nil (no birthdate), loaned-in skipped, and vector entries with a pid outside the squad.

**3. Re-apply after the game rewrites roles.**
- Hook `SEASON_RESET` and event 0x17 and re-run the saved role rule. The docs already call this necessary.

**4. Make `locate()` tolerant.**
- Count only role bytes 0..5 or 0xFF as valid, and accept a vector at ≥90% valid instead of requiring all of them.
- Log which pid had a bad byte.

## 4. Regression tests (extend `turbo/tests/world.lua`, `t04`, `t18`)

- **Sheet with a gap:** `W.build` option `sheet_gap = 8`, so `playerid8 = -1` and later reserves follow. Assert every `teamplayerlinks` player gets a role.
- **Vector of 52 fixed entries:** add `{-1, 0xFF, 0, 0}` empties, and mix in roles 1, 2, 4, 5 and 0xFF. Assert all squad entries become 3 (or 5 for the young) and the empties are untouched.
- **Age spread:** squad ages 17 to 38, with one 34-year-old on the last sheet slot and one 31-year-old with a missing birthdate. Assert the 34-year-old gets 3, and that the missing-birthdate player is counted in the summary.
- **Stale `playerloans` row:** a veteran with a row whose `teamidloanedfrom` is another club. Assert the summary reports one skipped.
- **Re-apply after event 0x17:** simulate the vector being cleared and refilled with computed roles, then assert Turbo's rule is re-applied.
- **Layout robustness:** one role byte of 9 should still allow the vector to be found, with a warning logged.

## 5. What the GUI exposes (VERIFIED)

- **`ui_tools.cpp:106`:** "Set role for whole squad", which applies one role to everyone and has no age or position choice.
- **`ui_teams.cpp:311`:** Teams > Mass actions has a fixed "Squad roles" button: age 19+ gets Rotation, younger gets Prospect. There is no per-player, per-age-band or per-OVR control.

Add a role rule editor to the same module (`squad_role.lua` and `team_mass.lua`):
- **By age band:** a list of {min age, max age, role} rows (for example 17-21 Prospect, 22-29 by rank, 30+ Sporadic or Rotation), with defaults matching today's behaviour.
- **By OVR rank:** rank 1-N gets Crucial, the next group Important, and so on. Rank comes from `players.overallrating` over the sheet.
- **By position:** a role per position group (GK/DEF/MID/ATT), using `preferredposition1`.
- **Per-player override:** pin one player to a role. This beats the rules, and is stored in the saved preset and re-applied by item 3 above.
- **Preview:** show the resulting list (player, age, OVR, old role, new role) before writing, and show skipped players with the reason.

Next step: before coding, dump a real `cm_teamsheets` row and the PSM vector from a career where this happens, with ages. That separates cause A (membership) from B (the game rewriting roles).

Files:
- `/home/user/FC27-Editor-Turbo/turbo/package/lua/libs/v2/imports/turbo/features/squad_role.lua`
- `/home/user/FC27-Editor-Turbo/turbo/package/lua/libs/v2/imports/turbo/features/team_mass.lua`
- `/home/user/FC27-Editor-Turbo/turbo/package/lua/libs/v2/imports/turbo/core/game.lua`
- `/home/user/FC27-Editor-Turbo/turbo/tests/world.lua`
- `/home/user/FC27-Editor-Turbo/turbo/tests/t04_squad_role.lua`
- `/home/user/FC27-Editor-Turbo/turbo/tests/t18_team_mass.lua`
- `/home/user/FC27-Editor-Turbo/turbogui/src/ui/ui_teams.cpp`
- `/home/user/FC27-Editor-Turbo/turbogui/src/ui/ui_tools.cpp`
- `/home/user/FC27-Editor-Turbo/docs/re/player_status_roles.md`

---

# Turbo GUI audit: raw codes shown instead of descriptions, and missing player data

Scope was `turbogui/src/ui/*.cpp`, `core/field_labels.h`, `ui/widgets.cpp`, `core/face_filter.cpp` and the Lua bridge. I could not check anything against the real FC27 database. `turbo/le27/` holds only a README, so there is no `fc27_db_schema.json`, and Live Editor's `loc/eng_us/localize.json` is not in the repo. Claims about values outside the code are therefore ASSESSED or UNCERTAIN.

**Reading of "pending edit codes".** I found no GUI string literally called "pending edit code". The nearest things are the pending command or call label (`App::pending_label`, Lua `S.call_pending`), the "queued: …" match-switch status, the "3D pending" overlay, and raw field/ID/code echoes in tooltips and toasts (VERIFIED, table below). Treat the table as the scope of "codes to descriptions".

## 1. Raw code, ID or internal-name leaks

Paths are relative to `turbogui/src/` unless they start with `turbo/`.

| # | Location | Shown now | Proposed description |
|---|---|---|---|
| 1 | `ui/widgets.cpp:76` (`range_tooltip`) | `overallrating  [0 .. 99]` (raw DB field name and range) | "Overall (0–99)". Show the label from `field_label()` first and move the raw name to a "technical" second line, or hide it behind a debug toggle. |
| 2 | `ui/widgets.cpp:361` (code-combo tooltip) | `bodytypecode = code 5  [1 .. 11]` | "Body type: Tall and Normal". The raw code and range go on a dim second line. |
| 3 | `ui/widgets.cpp:356` (each combo row hover) | `code 3` | The row already shows the name. Drop the tooltip, or show the meaning where there is one (for example a PlayStyle). |
| 4 | `ui/widgets.cpp:173` (date tooltip) | `stored as birthdate = 150123` | "Stored by the game as a day count." |
| 5 | `ui/widgets.cpp:290` and `core/field_labels.h:125` | `Unknown (code 12)` | "Unnamed body type 12 (no name in the game's text)". For `bodytypecode`, say "Player-specific body model 12" (see section 2). |
| 6 | `ui/widgets.cpp:31` | label "Nationality ID", raw integer editor. `players.nationality` has no name lookup in the editor. `Geo::nation_name()` exists (`ui/geo.cpp:11`) and is used only for list filters (`ui_players.cpp:154,252`). | A searchable nation combo ("Brazil") built from `nations.nationname`, as the filter already does. |
| 7 | `ui/ui_teams.cpp:28`, `field_grid` | `rivalteam` and `cityid` are plain integer fields. `trait1`/`trait2` are bare ints. | `rivalteam` becomes a team-name picker (`model.team_name`). `cityid` becomes a city name, or the field is hidden if the table has none (UNCERTAIN whether a lookup table exists). Team `trait1`/`trait2` become named bit flags. Their meaning is UNCERTAIN, so it needs a probe. |
| 8 | `ui/ui_teams.cpp:708` (manager `field_grid`) | `teamid`, `nationality`, `personalityid`, `outfitid` as raw ints | Team name, nation name and a named personality. `outfitid` gets a descriptor or a picker. |
| 9 | `ui/ui_teams.cpp:413` | `ID 241 | OVR 78` | "Juventus, overall 78". Put the ID in a tooltip. |
| 10 | `ui/ui_teams.cpp:256` and `ui/ui_players.cpp:1103` | `Club: Roma (52)`, `Roma (ID 52)` | Name only, with the ID in a tooltip. |
| 11 | `ui/ui_players.cpp:952` | `Delete X (ID 1234)?` | Keep the ID here: it is a destructive confirm and disambiguates namesakes. |
| 12 | `ui/ui_players.cpp:1062` and `ui/ui_faces.cpp:888` | `head asset 4123, head class 0` | "Real face (own head model)" or "Generic head". `headclasscode` has names in `field_labels.h:40`, but this line prints the number. |
| 13 | `ui/ui_faces.cpp:835` | `player 20801, head 20801 | …` | "Name (player) – real face". Keep the numeric ID as a trailing small hint only for search. |
| 14 | `core/face_filter.cpp:185` and `:192` | `Dark Brown (code 3)`, `(style 250: …)` | Drop `(code N)` and `(style N)` from the visible tooltip. The label and `hair::describe` are enough. |
| 15 | `core/field_labels.h:78` and `:86` (`hair_text`, `facial_hair_text`) | `Short hair #123`, `Short beard #250` | "Short hair, style 123" is fine as a combo item. In summaries use `hair::describe` ("Long, curly, headband"), as `widgets.cpp:293` already does for `hairtypecode`. |
| 16 | `core/field_labels.h:98` and `:132` (`code_text` fallback) | `Boots #1432`, `Head #20801`, `Penalty stance #3` | Meaningful enough as a stopgap. Better: named catalogues, or "Boots style 1432 (no name)". `animpenalties*` and `animfreekick*` could carry human names (for example "Run-up: short"), but that data is not in the repo (UNCERTAIN). |
| 17 | `ui/widgets.cpp:408-421` (`all_fields`) | Raw field name, `0..255`, and an editor for every field | This is the intentional raw view, so keep it. Add a "Show friendly names" toggle that puts `field_label()` in the first column. |
| 18 | `ui/widgets.cpp:46-50` (`field_label` fallback) | Un-mapped fields print as "Headtypecode", "Skinsurfacepack", "Animpenaltiesmotionstylecode", … | Extend `label_map()` for every field in the Appearance grid. There are about 40 un-mapped names (`widgets.cpp:44-49` maps only some). |
| 19 | `ui/app.cpp:690` (`App::edit` log) | `players.overallrating = 87` in the log and Status > Log | "Overall changed 84 → 87 (Player X)". Use `field_label()` plus `enum_label()`. |
| 20 | `ui/app.cpp:727` (undo toast) | `undone: Role1 back to 14` | Use `enum_label()` or `code_text()`: "undone: Role 1 back to CDM Holding+". Use `v.to_string()` only for plain numbers. |
| 21 | `ui/app.cpp:~633` (`notify(pending_label + ": " + result)`) and `app.cpp:757` | Toast prefix is a free-form `pending_label`. The Lua results come from bridge messages that include "code %d". | Add a user-facing verb catalogue ("Moving player…", "Creating player…"). Keep the label as-is for logs. |
| 22 | `ui/ui_match.cpp:255` and `:261` | `queued: GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER = 30` | "Injury frequency (your team) set to 30; applies at kick-off". |
| 23 | `ui/ui_match.cpp:244` | Tooltip prints the raw game variable name, for example `OVERRIDE/WEATHER`. | Keep the technical name as a muted second line. Put the human description first (`v.what` is already close). |
| 24 | `core/match_setup.cpp:322-332` (`known_vars`) | "Weather: the game's weather id … (-1)"; "Time of day: 0, 1, 3 or 4"; "Difficulty: 0 Beginner .. 5 Legendary" typed as an int. The editor is `InputInt` (`ui_match.cpp:243`). | Combos with names for weather, time of day and difficulty. The weather and time-of-day id names are not in the repo (UNCERTAIN). Difficulty names are in the string. |
| 25 | `ui/ui_match.cpp:236` | Column "Now" shows a bare `%d` or "game decides". | Map to the same combo names as row 24. |
| 26 | `turbo/package/lua/libs/v2/imports/turbo/bridge.lua:702` | Default label `"op " .. op` (internal op number) | Callers should always pass a label. Add a lookup, for example "player move". |
| 27 | `bridge.lua:730`, `:762`, `:878` | `"unknown player_morale code 7 (1 very happy, 2 value, 3 count stale, 9 check)"`, and the same for `player_create` and `player_move`. The messages cite internal "code N" values. | They are developer errors and appear only if GUI and Lua are out of sync. Reword as "Turbo.dll and the Lua scripts are out of date, update Turbo". |
| 28 | `bridge.lua:~712` (failed result) | `"game call status " .. status` (raw status int) | "The game call failed without a message". |
| 29 | `core/game_calls.h:121`, `core/player_move.cpp:530` | `ReleasePlayer returned 5 (not a code Turbo knows)` | "The game refused the release with an unrecognised reason (code 5)". Keep the number here, since it helps bug reports. |
| 30 | `ui/ui_tools.cpp:448-480`, `draw_hook_status` | `found at 0x147B7225C`, `ticks`, `Lua pumps`, `jobs run`, `dropped`, threads | This tab is already a diagnostic view. Group it under a collapsible "Technical details" header. Put a one-line health summary on top: "All features available" or "3 features unavailable". |
| 31 | `ui/ui_tools.cpp:~500-515`, Status Connection | `DB service 0x…`, `Command channel: 0x…`, `update #123` | Same: a collapsed "Technical" section. |
| 32 | `ui/ui_tools.cpp:490-512` and `ui/ui_callnames.cpp:~1214` | `write callname 2300871 (Kane's) to his playernamemap row` and `playernamemap row (…) queued for Turbo's Lua side` | Say "Set Kane's spoken name to …" with no table names or IDs. |
| 33 | `ui/ui_callnames.cpp:143` and `:291-296` | "queued for Turbo's Lua side (kept-name row first, then the name id)" | "Waiting for the game to be ready; it will apply automatically." |
| 34 | `ui/ui_faces.cpp:827` | "3D pending" / "3D failed" (draw-list overlay) | Fine as words. Add a hover explanation ("Waiting for the 3D render to be captured"). |
| 35 | `ui/ui_teams.cpp:529` | "A sack is pending: the game sacks you on the next day…" | Already a description. No change. |
| 36 | `ui/ui_teams.cpp:504` | `addon %d | the game's bands: insecure from %d, okay from %d, safe from %d` | Keep the bands. Rename "addon" to "bonus from results". |

Strings that already read as descriptions, and need no work: `ui_reapply.cpp:159` and `:284`; the `ui_standings.cpp` statuses; `ui_players.cpp:751` and `:775`.

**Suggested approach.** Add one `describe(table, field, value)` function in `core/field_labels.h`. It would return a name for every enumerable field and fall back to "Name N" only when the code has no name. Then route `App::edit` logging, the undo toast, the undo and mass-edit summaries, and both tooltips (`widgets.cpp:76,361`) through it. The pattern is already in place: `widgets.cpp:286-298` and `field_labels.h:121-134`.

## 2. Player-specific body types

**What the code has (VERIFIED).**
- `core/field_labels.h:24` names only ten body types: codes 1–9 are the generic height/build grid (Average, Tall, Short × Lean, Normal, Stocky), and 11 is "Very Tall and Lean". Code 10 has no entry. Any other code prints "Unknown (code N)" (`field_labels.h:125`).
- The Profile tab exposes `bodytypecode` in the "Roles and body" grid (`ui/ui_players.cpp:1015`). The Appearance tab's "Every appearance field" grid, filtered by `is_appearance_field` (prefix "body", `ui_players.cpp:47`), lists it again.
- `height` and `weight` are in `profile_fields()` (`ui_players.cpp:41-42`). `muscularitycode` has an enum in `widgets.cpp:258`.
- `is_code_field` (`field_labels.h:112`) shows any field ending in "code" as a combo. For `bodytypecode` it offers only the named codes plus a typed one (`widgets.cpp:327-345`), so a real-player body model can be shown but not browsed.
- Manager `bodytypecode`, `height` and `weight` have the same issue (`ui_teams.cpp:708`).
- Real faces are identified by `headclasscode` 0 and `hashighqualityhead` 1. `headassetid` equals the CMTracker or player id (`ui_faces.cpp:45-62`, `custom_headassets.lua:10`).
- `turbo/package/lua/libs/v2/imports/turbo/core/preset.lua:47` carries `bodytypecode` in a preset list. `create_player.lua:225-231` does not set a body type.
- The repo has no list of `bodytypecode` values actually used, and no localize file.

**ASSESSED:**
- The 1–9, 11 table comes from Live Editor's `bodytype_N` strings (header comment, `field_labels.h:3`). Those strings name the generic types only, so I expect real players to carry higher or unnamed codes.
- The clean explanation is that real stars use dedicated models (`bodytypecode` above the generic range) and are excluded from the localization.
- I cannot confirm from this repo that any real player uses a code outside 1–9 or 11. That is **UNCERTAIN** until you inspect the live DB.

**Other missing fields (ASSESSED, because the schema file is absent).**
- The GUI never reads the schema's `fc27_db_schema.json` tables for the Teams editor beyond the 22 fields in `team_overview_fields()` (`ui_teams.cpp:24-30`).
- Team side: there is no tactics editor, so formation, `defense*` setup tables, `teamsheets`, `default_teamsheets` and `team_mentality` are not editable here. Those names are UNCERTAIN until checked against the schema.
- Teams, kits and stadium links have no Turbo editors outside the dedicated UI.
- Player side: contract and loan fields are only shown in a loans section (`ui_players.cpp:379`).
- Tattoos are exposed (`tattoo_editor`) and hair, boots and gloves galleries exist (`item_galleries`). There are no editors for `playerformdiff`, injuries or `playerstats`-style tables.
- Managers: only the "Details" grid and "All fields", `manager_appearance` and the miniface exist.

Run `python3 scripts/check_fc27_schema.py` with a freshly exported `turbo_output\fc27_db_schema.json`. It validates names Turbo already uses. For coverage, write a new script that lists the schema fields of `players`, `teams` and `manager` that appear nowhere in `ui/*.cpp` or `field_labels.h`.

## 3. How to enumerate player-specific body types

1. **Probe the live DB (authoritative).** Add a Lua feature next to `headmodels.lua`. It scans `players` and writes `turbo_output\bodytypes_fc27.json`. For each distinct `bodytypecode` it records the player count, min/max height and weight, `headclasscode`, `gender`, and up to 5 example names. Then compare against `field_labels.h:24`. The GetDBMeta range in `fc27_db_schema.json` gives the legal range.
2. **Names from Live Editor.** Load `loc/eng_us/localize.json` at runtime and read `bodytype_N`. This is the same source as the header comment of `field_labels.h`, and a string such as `bodytype_10` would show whether it exists. Use it for codes with names. Do not hard-code more than that.
3. **Generate a catalogue the way `hair_catalog` is built** (`core/hair_catalog.h`, `core/hair_styles_data.h`, `scripts/gen_hair_styles.py`). Add `bodytype_catalog.{h,cpp}` and `bodytype_data.h`, with `Entry{code, generic, height_class, build, label}`. Generate it from the probe JSON.
4. **Names for player-specific models.** Prefer the data over guessing: label a model "Specific body #N (Haaland, Mbappé… )" from the probe's example names, and keep the generic grid names for 1–9.
5. **UI.** Replace the plain combo with a gallery or filter: "Generic types" and "Player-specific models". Filter by height and weight class, show "used by N players", and add a "Copy body type from player…" button.
6. **Safety.** Writing a specific body model onto a generic head may mismatch skeleton and kit, so confirm behaviour in-game before offering it by default (UNCERTAIN).

---

# Architecture map: Tactics editor in Teams (Turbo 1.2.4, HEAD 778dbf2)

Labels: V = VERIFIED (read in code), A = ASSESSED, U = UNCERTAIN. I did not edit, commit or push anything.

## 1. Tabs and panels

- V: The tab bar is hard-coded in `turbogui/src/ui/app.cpp:921-941`. It has `names[]` with 7 entries (Players, Teams, Managers, Competitions, Database, Turbo Tools, Status) and a `switch(i)` calling `draw_*(App&)`. `App::request_tab` (`app.h`) selects a tab by index for the next frame.
- V: `draw_teams` (`ui_teams.cpp:463`) is a left child `##tlist` (team list, search and country/league filters) plus a right child `##tedit` calling `team_editor` (`:397`). `team_editor` has a sub-tab bar `##ttabs` with Overview, Squad, Name, Colours, Crest, Job offer, Mass actions and All fields.
- A: The cleanest hook is a new `BeginTabItem("Tactics")` inside `##ttabs`. The selected team is `app.sel_team`, so "positions from the active tactic" is per team. Global game settings (both sides) are not per team, so put them in a separate section. Two options:
  - a sub-tab shown with no team selected, or
  - a top-level tab (a change to `names[]`, `for (i<7)` and `request_tab` numbering; the comment in `app.h` lists the indices, and `test_main.cpp:3341` iterates `tab < 7`).
- V: Panels are free functions declared at the bottom of `app.h` (`draw_players(App&)` and so on). A new file must be added by hand to two source lists, `turbogui/tests/native/run_native.sh:~29-40` and `turbogui/scripts/build_win.sh:40-50`. Neither uses a glob.
- V: Sizes go through `S(px)` (UI scale). Shared widgets in `ui/widgets.cpp` and `app.h`: `field_editor`, `slider_editor(_ex)`, `enum_editor`, `field_grid`, `all_fields`. They write through `App::edit`, which calls `Database::set` (range-checked) and then bumps `app.gen` and pushes undo steps for the players tables.

## 2. How edits reach the game

There are three paths, all V.

1. **Native DB write.** `App::edit` calls `Database::set` (`core/t3db.h`), a bit-packed write into the live T3DB table memory.
   - It goes through `Memory& mem` and checks the field's depth and minimum from the `GetDBMeta` meta.
   - `table_alive` guards against a reloaded database.
   - Rows added after connect need `has_room` first, because Live Editor's insert crashes on a full table.
   - This is the right path for any tactic or formation table field (`ui_teams.cpp` already does this for teams and `teamkits`).
2. **Lua bridge (mailbox).** `App::send({{"op","run"},{"module","X"},{"overrides",{...}}}, label)` goes to `Mailbox::submit`.
   - The mailbox is a 4096-byte JSON command slot at `+0x20` (`bridge.h`), polled by `bridge.lua` on each career-mode event and run via `TURBO.run`.
   - Only one command is in flight at a time (`App::busy()`).
   - Results come back as a toast.
   - Modules are registered in `turbo.lua` `M.MODULES` (name, path, kind `action` or `both`, `needs_cm`, desc).
   - Each module is a `features/<x>.lua` exporting `M.run(ctx)` that returns `(ok, summary)`. Its settings come from `ctx.cfg`, merged from `turbo_config.json` `modules.<name>` plus the overrides.
   - Each module has a script stub `lua/scripts/turbo_<x>.lua` (`TURBO.run("<x>")`).
   - `features/db_edit.lua` is a generic "table + where + set" editor with validation. The Lua DB helpers are `core/db.lua`.
3. **Game call or hook (native, game thread).** This is how in-match behaviour is changed.
   - The one live mechanism for gameplay values is the game-variable store in `core/match_setup.h` (`gv::apply` with `Caller::set_int`, anchors resolved by signature).
   - It is exposed to the UI as `app.match_setup->set_var/clear_var` (`ui_match.cpp:219-272`).
   - Variables are listed in `gv::known_vars()` (`match_setup.cpp:~320-332`, struct `KnownVar{name,label,what,when,min,max,off}`).
   - Today it covers NEVER_INJURE, `GAMEPLAY_CUSTOMIZATION/INJURY_{FREQUENCY,SEVERITY}_{USER,CPUAI}`, OVERRIDE/WEATHER, OVERRIDE/TOD, OVERRIDE_MATCH_DIFFICULTY and DISABLE_CPU_SUBSTITUTION. Each has a read site proven in `docs/re/match_setup.md`.
   - Overrides live until the game closes. A career load does not reset them (md section 1), so there is no reapply need for them within one session.
   - Constraints: the arena and free-list guards, and names restricted to `A-Z 0-9 _ /`.

Findings that bound the feature scope:
- U: There is no verified game variable or memory site for player speed, passing, shooting or trapping error, referee strictness or foul frequency, line depth, width or pressure. The repo has no tactic code at all. `grep` for "tactic" only hits `db_edit.lua` (a comment mentioning the `formations` table), `docs/fc26-parity.md:57` ("Formation editor... Database tab only") and a handover line about being stuck on the Edit Tactic screen. The `formations` table in `turbo/tests/world.lua:251` is a simulator fixture, not the real FC 27 schema.
- U: The real FC 27 tactic or formation table names and fields are unknown from the repo. `scripts/check_fc27_schema.py` and `turbo_output/fc27_schema.json` are the sources. The schema JSON is not in the repo (the `turbo/le27` fixture is gitignored). The first task is a read-only schema dump.
- V: `docs/re/match_setup.md` section 2 records that the text simulator's `SIM_SETTINGS/*`, `INJURY/*`, `CARD/*` and `FATIGUE/*` live in the same store, but the ini load overwrites them before the read. Those variables cannot change simulated matches.
- A: Gameplay sliders (speed, error rates, referee) need reverse-engineering in the style of `match_setup.md`, using `scripts/re/*` and signatures. They are research items, not wiring items. Split the module so that "tactics data" (DB fields, buildable now) and "match tuning" (game variables, gated by proof) are separate.

## 3. Presets and reapply

- V: `ui_presets.*` is not reusable. It handles player Export / Import / Clone / Create dialogs, Live Editor CSV and Turbo player JSON (`preview_preset_file`, `player_preset_buttons`, `export_clashes`). Its only generic parts are `club_picker` and `club_notes`. The file picker is in `ui/file_picker.*`.
- V: `core/reapply.*` plus `ui/ui_reapply.cpp` is the right pattern to copy. It follows these steps:
  - `ReapplyStore` is plain data with a JSON round-trip.
  - `reapply_json`, `parse_reapply_json` (drops malformed entries and counts them) and `save_reapply_store` write `<file>.tmp` then rename.
  - An unreadable file is set aside as `*.unreadable.json`.
  - Reapply is keyed by `reapply_key()` (`<Lua session>#<load_gen>`), so it runs once per newly loaded career. `App::maybe_reapply()` is called from `refresh()` (`app.cpp:523`), and `reapply_due()` forces a connect even while the window is hidden.
  - It writes through `Database::set`, with no undo step.
  - Kill switch: `turbo_output/reapply_off.txt`.
- V: The store is hard-wired to two kinds (kits, callnames), with `size()` and `empty()` summing those two maps. Extending it is possible but couples the two. A: Better to add a sibling.
- V: FC 27 reloads only some tables at career load. Teamkits and playernamemap were measured as reloaded. Whether tactic or formation tables are reloaded is U. It needs a differential test: write, save, reload, read. That decides whether a tactics reapply store is needed at all.
- A: A "slider profiles / presets by category" store should be a new small module. Suggested name `core/tactics_profiles.{h,cpp}`, with `turbo_output/tactic_profiles.json`: `{"turbo_tactic_profiles":1, "profiles":[{"category":"match|team|position|opposition","name":..,"values":{key:number}}]}`, loaded in the `App` constructor like `load_reapply`. Copy the atomic save and set-aside logic.
- V: Light settings already persist in `turbo_output/gui_settings.json` (`App::load_gui_settings` and `save_gui_settings`, merged by Lua over `turbo_config.json`). That suits UI choices (active profile name per category) but not user data.

## 4. Drawing primitives

- V: There is no pitch drawing or heatmap anywhere. Existing custom drawing is limited to `ImDrawList` calls via `GetWindowDrawList()` for picture and grid cells: `AddRectFilled`, `AddText`, `AddImage`, `AddTriangleFilled` in `ui_identity.cpp:32,678,738`, `ui_images.cpp:40,446,616`, `ui_faces.cpp:635` and `ui_callname_play.cpp:30`. `geo.cpp` is a geography snapshot (nations, leagues), not drawing.
- A: For a mini pitch, use an `ImGui::InvisibleButton` or `Dummy` of fixed aspect to reserve the region. Then draw with `AddRectFilled`, `AddLine`, `AddCircle`, `AddPolyline` and `AddConvexPolyFilled` into the window draw list, using coordinates mapped from the formation's normalised offsets. For heatmaps, use a coarse grid (about 24x16) of `AddRectFilled` cells. All of this is immediate-mode and needs no texture. The ImGui version is v1.92.9 (vendored).
- A: Keep the geometry pure in `core/tactics.*` (formation to positions, line depth/width/press to polylines and a density grid) and keep the ImGui draw calls only in `ui/ui_tactics.cpp`. That way the native tests can check the numbers.

## 5. Tests

- V: Native harness: `turbogui/tests/native/test_main.cpp` (12k lines) plus `test_*.h` includes. `gui_world.lua` builds `world.img`, a synthetic game memory from the Turbo Lua simulator (`turbo/tests/mock/sim.lua`, `turbo/tests/world.lua`) with real T3DB layout. Run with `bash turbogui/tests/native/run_native.sh` (ASan and UBSan).
- V: Core tests are plain `run_case("name", [&]{ CHECK(cond, msg); })` (examples: `test_reapply_store`, `test_wheel.h`). UI cases drive `Ui ui(app)` with `ui.click("Players","##plist")`, `ui.find(...)`, `ui.type_into(...)`, `ui.frames(n)`, `ui.toast_contains(...)` inside `test_ui()` (`:3305`). A header-style case set is called near `:7380`, for example `test_edit_unlock_ui(app, ui, le)`.
- A: Add a `test_tactics.h` included by `test_main.cpp`. Its test functions should cover:
  - formation geometry,
  - the profile store round-trip, malformed input and set-aside,
  - a UI case that selects a team, clicks the Tactics tab and moves a slider, and checks the DB value and a toast.
- V: Lua side: `turbo/tests/run_tests.sh` runs `t*.lua` tests. A new `tNN_tactics.lua` should cover the module (the simulator can be extended with a tactics table in `world.lua`).
- V: `H.setup({le_27_1_2=true})` removes natives the real build lacks. Also run `luacheck --config turbo/tests/.luacheckrc turbo/package/lua` and `python3 scripts/check_fc27_schema.py` (extend its name list with the new tables and fields).
- V: The Windows smoke test (`tests/win/run_smoke.sh`) and overlay test (`run_overlay_wine.sh`) cover host safety. The host does not need changes unless a game call or hook is added.

## 6. Recommended module layout

Core and UI (all A):
- `core/tactics.{h,cpp}`: pure data and logic, with no ImGui and no game memory except through `Database`.
  - Structs for formation slots, the team settings sets (defence line, forward runs, line depth/width, pressing), position or role settings, and the opposition profile (a mapping from opponent tactics and quality to slider values).
  - Range tables with min, max, default and "game decides" values, in the style of `gv::KnownVar`.
  - Reads and writes via `Database::get/set`. The schema table and field names come only from the schema dump.
- `core/tactic_profiles.{h,cpp}`: the category store above (JSON plus atomic save).
- `ui/ui_tactics.{h,cpp}`: `draw_tactics(App&)` plus the mini pitch drawing helper. Add the pitch as its own function so it can be reused in the Competitions, Match setup view.
- `features/tactics.lua` (`MODULES.tactics`, kind `action`, `needs_cm=false`): batched DB edits with validation, and snapshot export for profiles. Use `core/db.lua`, like `db_edit.lua`, for bulk or all-teams operations (the opposition profile applied across many clubs). Single-row slider drags should stay native through `App::edit`.
- Match tuning:
  - Extend `gv::known_vars()` only after each read site is proven (a new RE note beside `docs/re/match_setup.md` with signatures in `docs/re/*-signatures.json`).
  - A UI group "Match tuning" should render from `known_vars()`, so every slider is data driven. It needs no new native module.
- Wiring: `app.h` (declare `draw_tactics`; an `App::tactic_profiles` member and load in the constructor), `turbo.lua` (register module), `lua/scripts/turbo_tactics.lua`, both source lists, `docs/turbo-reference.md`, `CHANGELOG.md`, README (the user guide is `turbo/package/TURBO_README.md`).

## 7. Risks

- Frame loop (V): `App::draw` runs inside the render hook each frame, inside a try/catch that switches the whole overlay off for the session (`overlay_dx12.cpp:612-646,699-710`). The panel is drawn only when its tab is open, but when open it runs every frame.
  - Do not read tables per frame. Cache per `app.gen`, and use `Snapshot` for bulk reads like `squad_table`.
  - Compute the heatmap only when the inputs change (slider released or value changed), and keep the draw loop to a bounded number of primitives.
  - No file I/O or JSON parse in the frame path (the store load and save happen on edit and at construction).
- Launch safety (V, `turbo-reference.md` rules 1-8):
  - Nothing new may run at launch. `t08_launch_safety.lua` allows exactly one `package.loadlib`, and the test fails if any other game native runs at launch.
  - No game native in autorun or boot, and no import of a graphics DLL into `Turbo.dll`; `build_win.sh` fails if one appears.
  - Any new tactics code must be inert until a career connects. Do not add a boot-time reapply that reads DB memory before the connect (`maybe_reapply` runs only after `refresh()`).
- Memory and DB safety (V):
  - Guard every record write with `table_alive`, because a reloaded database frees the old memory.
  - `has_room` is mandatory before any insert (Live Editor's insert crashes the game on a full table).
  - Use ranges from meta, not hand-typed ranges.
  - Never save the user's career, and keep destructive actions confirmed.
- Kill switches (V): the pattern is `turbo_output/call_<name>_off.txt` for a game call and `hook_<name>_off.txt` for a hook, and `reapply_off.txt`. These are listed on the Status tab (`draw_hook_status`) and in the README troubleshooting table. A new game variable or hook must have its own switch. The global switches are `turbo_gui_disable.txt` and the crash flag `turbo_gui_start.flag`. Any new tactics apply path needs a switch like `tactics_off.txt` (A).
- Input shield (V): `win/input_shield.cpp` hooks DirectInput, raw input, `GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState` and `GetCursorPos`.
  - While Turbo wants the mouse (`WantCaptureMouse`), the game sees no input.
  - Keyboard capture needs `WantCaptureKeyboard`. A custom pitch widget with drag handling must use `InvisibleButton` so that ImGui owns the hover and capture (a bare `IsMouseHovered` check would not set it; A).
  - The 0.2.5 hooks are still "installed but not yet tried in game" per `HANDOVER.md`; `turbo-reference.md:232` lists the same.
  - Never build custom Win32 input handling.
- Concurrency (V): one command in flight on the mailbox (`busy()`); a Lua module that edits many teams runs on the game thread inside a career-mode event (a long run freezes the game; the handover says to keep probes short). Batch opposition-wide writes in chunks (A).
- Reapply semantics (U): it is unknown whether FC 27 resets tactic or formation tables at load, and whether the AI recomputes tactics when a manager is changed or a match starts. Without a differential test, edits could silently revert. Decide persistence after that test.
- Game-variable lifetime (V): overrides last until the game closes, and they apply to both sides. Per-side overrides exist only where the game provides `_USER` and `_CPUAI` names, as for injuries. "Settings applicable to both teams" matches that. Opposition-specific values must therefore come from DB fields, not game variables (A).
- Docs and tooling (V): the bugs "squad roles not applied for over-30s" and "pending edit codes to descriptions" are outside this map. The squad-role code is `features/squad_role.lua` (layout `offset 0x18, size 8, role at +4` per `docs/re/player_status_roles.md`, which says the role is a byte and the vector holds 52 entries), and `team_mass.lua` has a role-by-age rule (`:103`). Those are the first places to look for the age-related bug.

---

# Tactics and gameplay data reachable from Turbo (read-only audit)

**Headline.** The repo holds no FC 27 tactics-table evidence. It has no `teamtactics` table, no `defensivedepth`, `buildupplay` or `defenderline`, no slider-style table, and no dumped schema. A grep for those names finds nothing in code or docs. The in-game schema dump `turbo/le27/fc27_db_schema.json` is gitignored and absent, so no table or field range below is confirmed from the real FC 27 schema. Every table-and-field claim here comes from code, fixtures and docs only.

## 1. Schema evidence in the repo

**Where it comes from.**
- `scripts/check_fc27_schema.py` checks Turbo's field names against the user's dumped `fc27_db_schema.json`. That file comes from `turbo_probe.lua` via `GetDBMeta` and carries type, range and bit depth for every field.
- The last recorded result was "116 names checked, 106 present, 10 optional absent, 0 missing" (`docs/HANDOVER.md:125`).
- `scripts/check_field_names.py` checks names against the FIFA 21 `db_meta.xml` and the FC 24/25/26 Live Editor Lua repos, fetched from GitHub. That is a FIFA 21–FC 26 name check, not FC 27 evidence.
- `turbo/package/lua/libs/v2/imports/turbo/features/probe.lua:15-16` dumps `formations` among its tables, but no output is checked in.

**Tables and fields Turbo already touches** (Turbo drops absent fields silently):

| Table | Fields | Type and range | Status |
|---|---|---|---|
| `formations` | `teamid` (depth 18), `formationid` (depth 9), `formationname` (string), `offset1x`, `offset1y` (float) | depths and floats are from the fixtures in `turbo/tests/world.lua:250` and `turbogui/tests/native/gui_world.lua:339`; the comment there says "floats live in formations in the real database" | ASSESSED. The fixtures are hand-written, so the real field list and ranges are UNCERTAIN. |
| `formations` (more) | the fixture has a single position offset; a real formation needs 11 positions, so I expect `offset2x..offset11y` | nothing in the repo shows them | UNCERTAIN. |
| `teamplayerlinks` | `position`, `jerseynumber` | `position=29` is a reserve (`docs/re/realtime_transfers.md:~104`, `WriteTeamPlayersLinks`) | VERIFIED. |
| `cm_teamsheets` | `teamid`, `playerid0..playerid51`, takers, `cksupport1..9`, `throwerleft`, `throwerright` | slot -1 means empty; `check_field_names.py:24-34` says these are in FC 27's own dump | VERIFIED names, ranges unknown. The player-id slots are slot-indexed. I could not tell from the repo whether formation or position lives there; `TeamSheet` (0x288 bytes) is said to hold "takers/formation after" the player ids (`scripts/re/realtime_signatures.json:897`). |
| `cm_mentalities` | row per team | `LoadTeamSheets` and `WriteTeamSheet` read and write it (`realtime_transfers.md:177,179`) | VERIFIED existence. Fields are not listed anywhere in the repo, and I could not tell what "mentality" covers. It is the closest thing to a per-team tactic row. |
| `teams` | set-piece takers, `cksupport*`, `throwerleft/right`, `trait1`, `trait2` | `ui_teams.cpp:28` | VERIFIED names. |
| `players` | `role1..role9`, `preferredposition1..7` (5-7 are shown only if present), `trait1/2`, `bodytypecode`, `runstylecode`, `skillmoveslikelihood`, `gkkickstyle` | `ui_players.cpp:1015`. FC 27 replaced work rates with roles (`check_fc27_schema.py` OPTIONAL list). | VERIFIED names. Ranges come from the dump, so UNCERTAIN here. Role and body-type names come from Live Editor's `loc/eng_us/localize.json` (`CHANGELOG.md:22`). |
| `career_playercontract` | none | the table does not exist in FC 27 | VERIFIED. |

**Not found anywhere.**
- `teamtactics`, `defaulteamsheets` and `teamsheets` do not appear in the repo. Only `cm_teamsheets` does.
- Per-position instruction tables, `formationpositions`, rival-preset tactic tables and playmaker or creative-freedom fields are all absent.
- Whether FC 27 has an editable "team instructions" table at all is UNCERTAIN. The `teamtactics` columns I remember from FC 25 are only a hypothesis (UNCERTAIN, not verified against FC 27).

**Where roles actually live at runtime.**
- Squad status roles (Crucial to Future) are in memory, not the database.
- They sit in the PlayerStatusManager at +0x18, in 8-byte entries with one role byte (`docs/re/player_status_roles.md:131-160`).
- The game recomputes them at every `SEASON_RESET`, event 0x17.
- This area is where the "players over 30 not updating" bug will sit. It is not the player-role fields `role1..9`.

## 2. Team sheets are the runtime master copy

- `GameServices::CachedTeamSheetService` (service id `0x113EA820`) holds one `TeamSheet` per team (`docs/re/realtime_transfers.md:172-187`).
- It loads from `cm_teamsheets` and `cm_mentalities` once, at `LoadGameComplete`, via `0x147B5DB4C`.
- At save, `TriggerSave` writes every sheet back with `WriteTeamSheet` `0x147B9616C`.
- So a DB edit of `cm_teamsheets` is "invisible at runtime and overwritten at the next save". VERIFIED from the disassembly notes, though the claim is `[H code, not run]`.
- Any tactics editor therefore cannot rely on Lua DB writes. It needs the runtime service, either by calling `SetSheet` (vtable slot 24, `0x145E3ED90`-style) or by editing the DB and then forcing a reload. Both are RE work and neither is tested.
- AI team formations are read from tables when a match is set up, and the matches are loaded with `formations`-style rows. How the game consumes them is UNCERTAIN.

## 3. Where gameplay slider values live in FC 27

**VERIFIED, existing precedent.** The game-variable store (`docs/re/match_setup.md` section 1; `turbogui/src/core/match_setup.cpp:317`) is a hash table `GameVars` at `0x14D26E510`. Turbo writes it through the game's own `SetInt` at `0x14154F384`, with a lock protocol, five agreeing signatures and a kill switch. Features implemented there:

- `NEVER_INJURE`
- `GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER`, `INJURY_FREQUENCY_CPUAI`
- `GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_USER`, `INJURY_SEVERITY_CPUAI`
- `OVERRIDE/WEATHER`, `OVERRIDE/TOD`
- `OVERRIDE_MATCH_DIFFICULTY`
- `DISABLE_CPU_SUBSTITUTION`

**Limits of that store.**
- Only variables the match code reads through `GetInt` or `Exists` with a literal name work.
- The enumeration found 1568 call sites over 991 names. That name list is not saved in the repo, so it needs re-extraction. `scripts/re/strings_grep.py` and `scripts/re/xrefs.py` exist for this.
- The injury and difficulty variables are "implemented", but the in-game check was still pending, so they may not match what is stated. The injuries-off switch is "verified in game", while injuries off during a match is "untested in a match" (`docs/fc26-parity.md:77`).
- Values are overridden, not stored. They persist until the game closes and are not saved with the career.

**Ruled out or not found** (`match_setup.md` section 5):
- Referee strictness is an Attribulator schema (`gp_rules_refereestrictness`, `referee_strictness_*`), not a game variable.
- Referee frequency of fouls: no variable found.
- Fatigue off as a match switch: none.
- VAR: no setting found.
- The simulator's `INJURY/*`, `CARD/*`, `FATIGUE/*` and `SIM_SETTINGS/*` variables load from `simsettings.ini`, and the load overwrites any override before the read, so they cannot change simulated matches.

**Not covered anywhere in the repo.**
- Player speed, passing error, shooting frequency and error, and ball control and trapping error. I found nothing on where these live.
- A likely home is the Gameplay Attribulator, but that is UNCERTAIN.
- Live Editor's Attribulator library (`GameplayAttribulatorSetVar`, `GameplayAttribulatorGetVarType`) is listed as a Live Editor API function at `turbo/package/lua/libs/v2/imports/turbo/core/env.lua:136`. VERIFIED that the names are listed. Its semantics and FC 27 coverage are UNCERTAIN. The repo marks it "Not in Turbo 1.0 (FC 27 LE's Lua library)" (`docs/fc26-parity.md:92`) and `AardvarkGetInt`/`AardvarkSetInt` sit beside it in the same list. This is the most promising lead for the global gameplay sliders.

## 4. Feasibility matrix

| Setting | Storage | How Turbo can write it | Confidence | Needs |
|---|---|---|---|---|
| Injury frequency and severity (per side) | `GAMEPLAY_CUSTOMIZATION/INJURY_*` game vars | existing `match_setup` / `gv::apply` | High (VERIFIED code, in-game check still pending) | in-game verification |
| Injuries off, difficulty, weather, time of day, CPU subs | game vars | same | High (injuries off verified accepted by the game) | in-game match check |
| Referee strictness | Attribulator `gp_rules_refereestrictness` | Attribulator call (needs checking), or the game's own call | Low | RE: how `GameplayAttribulatorSetVar` stores and is read; a name list; in-game test |
| Foul frequency | unknown | unknown | Very low | RE name search, e.g. `strings_grep` for FOUL, REFEREE |
| Player speed, passing and shot error, control and trap error | unknown, possibly the Attribulator | unknown | Very low | RE; the 1568-site name list; in-game test |
| Shot, forward-run and pass frequency | unknown | unknown | Very low | same |
| Formation (positions, offsets) | `formations` table, probably per team | Lua `db_edit` or the GUI Database tab | Medium to write, Low that the game reads it live | schema dump for the real field list; when AI teams load their formation |
| User or CPU team formation choice | `cm_teamsheets` or the runtime `TeamSheet` | runtime `SetSheet`, because the DB is overwritten at save | Low | RE of the 0x288-byte layout (formation, takers) and a `SetSheet` call |
| Team mentality or style | `cm_mentalities` (fields unknown) | runtime sheet or DB | Very low | schema dump; RE |
| Team instruction sliders (depth, width, pressure, runs) | not found: no table in the repo | n/a | Very low. A `teamtactics`-like table is a guess. | schema dump first; RE if it is runtime-only |
| Per-position instructions | not found | n/a | Very low | same |
| Player roles `role1..9`, preferred positions, traits, body type | `players` | existing Lua and GUI editor (done) | High | none |
| Squad roles (Crucial..Future) | PlayerStatusManager memory | existing `squad_role` memory write | High, with the over-30 bug to fix | re-apply after `SEASON_RESET` |
| Opposition tactics ("dynamic opposition") | unknown. Likely the CPU team's team sheet or formation row, read at match setup. | unknown | Very low | RE; and the game must read from the DB |
| Save and load custom preset profiles | Turbo's own files (existing `gui_settings.json` pattern) | pure Turbo | High | none |

## 5. Next steps

1. Get the real schema dump (`turbo_output\fc27_db_schema.json`). It is the only source for the real tactics and formation fields, types and ranges (`GetDBMeta`).
2. Check the dump for any table named like `*tactic*`, `*mentalit*`, `*instruction*` or `*formation*`. This is the missing step and decides most of the matrix.
3. Re-run the game-variable name enumeration and save the 991 names. Grep them for `PASS`, `SHOT`, `FOUL`, `REF`, `SPEED`, `CARD`, `ERROR`, `TRAP`, `CONTROL`.
4. Probe `GameplayAttribulatorSetVar` and `GameplayAttribulatorGetVarType` in game, and list what the Attribulator schema exposes.
5. RE the `TeamSheet` formation and takers layout and the `SetSheet` call to write tactics at runtime.

**Key files.** `docs/re/match_setup.md`, `docs/re/realtime_transfers.md` (section 1.5), `docs/re/player_status_roles.md` (section 3), `turbogui/src/core/match_setup.cpp:317-335`, `scripts/check_fc27_schema.py`, `scripts/check_field_names.py`, `turbo/package/lua/libs/v2/imports/turbo/core/env.lua:136`.

---

# Research: Football Manager tactics as inspiration for a Turbo tactics editor

## Source status

- **Reached (search snippets only):** FM24 SI manual, FM Scout, Passion4FM, guidetofm, fm-arena, FM Stag, and Neal Guides on FC25 tactics. I got them through WebSearch result summaries.
- **Not reached:** WebFetch was `EGRESS_BLOCKED` for fmscout.com, passion4fm.com, ea.com and nealguides.substack.com. I could not read any full page. The EA FC 26 Pitch Notes, the FC 25 tactics deep dive, the Aranaktu docs and the SI manual are unread beyond search snippets.
- **Written from memory (marked [M] below):** FM option lists beyond the snippets, per-role detail, and all numeric slider mappings. The slider mappings are my design proposals, not sourced values.
- **EA FC 27:** nothing is published that I could find. Everything about FC is extrapolated from FC 25/26. Treat the FC 27 data layout as unverified until it is checked in the Live Editor or the DB.
- **Live Editor:** search confirms it is a C++ DLL with a Formation Editor and a "Gameplay Attribulator for the LUA API". The release notes I could see did not document tactics tables or CPU-AI sliders. Whether FC 27 exposes per-team tactic fields, as FC 25/26 did, needs checking in the Live Editor and DB.

## 1. FM team instructions (FM24/FM26 naming)

**Mentality [M]:** Very Defensive, Defensive, Cautious, Balanced, Positive, Attacking, Very Attacking. Mentality is the master risk/positioning setting. It shifts the whole team's risk-taking, line height and forward bias. Unlike the others, it is not a dial on a single behaviour.

**Team Shape and Fluidity [M]:** Fluidity runs from Very Rigid to Very Fluid. Shape has presets. FM24 ties Creative Freedom to Fluidity, so "Be More Expressive" raises it and "Be More Disciplined" lowers it.

### In possession

The first six are snippet-confirmed. The rest are from memory.

| Instruction | Options | Footballing effect | Candidate slider |
|---|---|---|---|
| Attacking Width | Much Narrower to Much Wider (5 steps) | Spreads or funnels play across the pitch | `attack_width` 0–100 |
| Approach Play | Pass Into Space, Work Ball Into Box, Overlap Left/Right, Underlap, Play Through the Middle | Where and how the ball is advanced | enum or tri-slider |
| Passing Directness | Much Shorter to Much More Direct | Pass length, forward-pass share, risk | `pass_directness` |
| Tempo | Much Lower to Much Higher | Speed of circulation and decisions | `tempo` |
| Final Third: Dribble Less / Run at Defence | Dribble Less, Run At Defence | Share of passing versus dribbling | `dribble_freq` |
| Creative Freedom | More Disciplined to More Expressive | Fluidity and individual freedom | `fluidity` |
| Final Third: Work Ball Into Box | Toggle | Patience before shooting | `shot_patience` |
| Final Third: Shoot on Sight / Low / High Crosses | Toggles and options | Shot and cross frequency and type | `shot_freq`, `cross_freq` |
| Cross Engagement | Cross Rarely to Cross Often, Whipped / Floated | Crossing | `cross_freq` |
| Play Out Of Defence | Toggle with a Play Out vs Clearance option | Build-up from the back versus clearing | `buildup_short` |
| Goalkeeper distribution | Distribute to CBs / Fullbacks / Playmaker / Target Man, Slow/Fast Pace Up, Roll Out, Distribute Quickly, Take Short Kicks | GK first pass | enum |
| Pass Into Space, Overlap and Underlap | See Approach Play | Wide-runner use | n/a |

### In transition

The four options below are confirmed by the snippet.

- **After losing the ball:** Counter-Press, or Regroup. Counter-Press means immediate pressure to win it back, which gives a high turnover and a high-risk tradeoff. Regroup means falling back into shape, giving fewer gaps and less pressure.
- **After winning the ball:** Counter, or Hold Shape. Counter means an immediate forward attack with fast breaks and a more stretched shape. Hold Shape means keeping the formation and building patiently.
- **Also [M]:** "Counter Press" and "Counter" are exclusive with their pair. FM26 also has Distribute Quickly and Take Short Kicks, plus a "Counter / Hold Shape" tick on the GK.
- **Slider mapping:** `counter_press` 0–100, `counter_attack` 0–100, `regroup_depth`.

### Out of possession

| Instruction | Options | Effect | Slider |
|---|---|---|---|
| Defensive Line | Much Lower to Much Higher | Line depth; high compresses space but risks pace-based balls in behind | `line_height` |
| Line of Engagement | Low Block, Mid Block, High Press | Where the team starts pressing | `engagement_height` |
| Trigger Press | Much Less Often to Much More Often | Frequency of press triggers (confirmed by the snippet) | `press_trigger` |
| Defensive Width | Narrower to Wider | Compactness vs coverage of wide areas | `def_width` |
| Prevent Short GK Distribution | Toggle | Presses the GK's short kicks | `press_gk` |
| Tackling | Stay On Feet / Get Stuck In / Normal | Tackle aggression and foul and card risk | `tackle_aggr` |
| Use Offside Trap | Toggle | Line steps up in unison; risk if mistimed | `offside_trap` |
| Marking | Zonal / Man marking by role | Man-to-man vs zonal; man marking drags players out of shape | `marking_tightness` |
| Pressing Intensity (Press Intensity / Pressing Focus) | Stay Narrow / Press Wide, Funnel, Press the Ball Carrier, Trap Outside or Inside | Where the press steers play | `press_funnel` |
| Play Out Wide / Cut Out Crosses etc. | Options | Hunts or blocks wide play | enum |

**Opposition Instructions:** these are per-opponent-player orders, such as Show Onto Weaker Foot, Tight Marking, Tackle Harder, Mark Tighter or Don't Dribble. They are the mechanism FM uses to counter a specific opposition type. They map to Turbo's opposition per-position sliders (section 4).

## 2. Player instructions (PIs)

All options below are in FM24 and FM26. The first group is snippet-confirmed.

### Movement

- **Roam From Position:** looks for pockets of space outside his designated position (confirmed). Slider: `roam` 0–100.
- **Hold Position:** stays in his assigned position and rarely leaves it (confirmed). Slider: `hold_pos`, the inverse of roam.
- **Move Into Channels:** runs the half-space channels.
- **Sit Narrower / Stay Wider / Move Wider:** horizontal positioning. Slider: `width_bias`.
- **Get Further Forward / Hold Position / Stay Back at All Times:** depth preference. Slider: `forward_runs`.
- **Run Wide With Ball / Cut Inside With Ball / Take Fewer Risks** [M].

### Passing

- **Pass Shorter / Pass Longer:** (confirmed) Pass Shorter asks players to keep possession patiently.
- **Take More Risks:** (confirmed) more low-percentage through balls. Slider: `risk_passing`.
- **Take Fewer Risks / Play Safe.**
- **Cross From Deep / Cross From Byline:** (confirmed) crosses from deep versus after reaching the byline. Slider: `cross_depth` 0–100.
- **Cross Often / Rarely / Floated / Low / Whipped / Hit Early Crosses / Cross to Far Post** [M]. Also **Pass Into Space**, **Try Killer Balls More Often**.

### Dribbling and shooting

- **Dribble More / Dribble Less:** (confirmed). Slider: `dribble_freq`.
- **Run at Defence:** (confirmed). It is a Team Instruction, and PI-level in some roles.
- **Shoot More Often / Shoot Less Often:** (confirmed). Slider: `shot_freq`.
- **Work Ball Into Box** (player-level).

### Defending

- **Close Down More / Close Down Less:** (confirmed) pressing intensity of the individual player. Slider: `close_down`.
- **Tackle Harder / Ease Off Tackles:** (confirmed, HARD, NORMAL, EASY) tackle aggression. Slider: `tackle_aggr`.
- **Mark Tighter / Mark Looser** [M]. **Stay On Feet** [M].
- **Defensive PIs by position:** CB can have Cover, Stopper and Wide CB; FB can have Press or Hold.

### Other

- **Roles and Duties** [M]. Each position has roles, and each role has Defend, Support and Attack duties. Examples:
  - GK: Goalkeeper, Sweeper Keeper.
  - CB: Central Defender, Ball-Playing Defender, No-Nonsense CB, Wide CB, Libero, Advanced CB.
  - FB/WB: Full-Back, Wing-Back, Inverted Wing-Back, Complete WB, No-Nonsense FB.
  - DM: Defensive Midfielder, Anchor, Half Back, Deep-Lying Playmaker, Regista, Ball-Winning Midfielder.
  - CM: Box-to-Box, Central Midfielder, Mezzala, Carrilero, Advanced Playmaker.
  - AM/W: Attacking Midfielder, Shadow Striker, Winger, Inside Forward, Inverted Winger, Wide Playmaker, Raumdeuter, Trequartista, Enganche, Wide Forward.
  - ST: Advanced Forward, Complete Forward, Deep-Lying Forward, Poacher, Pressing Forward, Target Forward, False Nine, Treinador.
- **Duty effect on sliders:**
  - Defend: runs forward less, holds depth, stays back.
  - Support: balanced.
  - Attack: more forward runs, higher position, more shooting.
  - Map duty to a `attack_bias` preset from 0 to 1 that seeds `forward_runs`, `depth`, `shoot_freq` and `close_down`.

## 3. How FM visualizes tactics (UX inspiration)

- **Tactics pitch view:** a formation diagram with player discs. Arrows and symbols show a player's PI: forward-run arrow, wide arrow, and role/duty labels such as "(A)", "(S)", "(D)". Clicking a disc opens the role and PI panel. The view is draggable in Create-a-Tactic.
- **Analysis tab:** has Average Positions in three modes (With Ball, Without Ball, Overall), plus Heat Maps and key stats (confirmed). During a match there is a cog for "Show Opposition Analysis", which shows both teams' average positions and a combined heat map (confirmed, FM Stag tip).
- **Using it:** large gaps in average positions mean a tweak is needed, and in possession all players push up slightly (confirmed). This translates to a Turbo "preview" overlay.
- **Passing networks** and shot maps also exist [M].
- **Turbo UX takeaways:**
  1. A mini pitch with player dots that move when sliders change.
  2. Line overlays: defensive line height, midfield line, forward line.
  3. A width envelope (a translucent band).
  4. A pressing heat map from `press_trigger`, `engagement_height` and `close_down`.
  5. Arrows for forward runs, cross-from-deep and underlap.
  6. Toggle "With Ball / Without Ball / Overall" as in FM.
  7. A red/yellow/green "risk" gauge. FM's own coherence message is that "the match engine rewards a coherent set of decisions".

## 4. FM AI managers (inspiration for opposition variety)

Snippet-confirmed points first.

- **The AI responds to reputation and favourite status, not to a read of your tactic.** It sets up as a defensive block, an open attack, or balanced, depending on how it rates the matchup. If you are a top team you meet defensive teams weekly.
- **Over-performing** pushes more AI teams to cautious approaches (confirmed).
- **Mentality** describes risk-reward decisions, movement and positioning (confirmed).
- **Probably also true, from memory:**
  - Each manager has a preferred style and formation list, from attributes and tactical-style history.
  - In-match flexibility varies. Some change shape when trailing, and a few use shouts.
  - Quality of squad and reputation set the baseline mentality.
  - Hidden "tactical flexibility" is limited.
  - Style families exist: Gegenpress, Tiki-Taka, Wing Play, Direct, Counter, Catenaccio, Route One, Fluid Counter-Attack. Passion4FM's "tactical styles" and the FM preset tactics are the reference list.
- **Turbo opposition mapping:** each AI team gets an `archetype` plus per-slider jitter, driven by:
  1. `strength_ratio` (opponent vs user): lower ratio gives deeper line, low block and counter; higher ratio gives high line, more press, more width.
  2. `squad_traits`: pace, passing and aerial profile choose the style (fast wingers lead to Counter or Wing Play; high passing leads to possession; tall strikers lead to crossing).
  3. `reputation` and `form` shift mentality.
  4. `flexibility`: trailing or leading shifts line height and tempo mid-game, which can be modelled in a "game state" modifier.
  5. `variety seed`: a random per-match offset so the same team does not always play the same way.

## 5. EA FC 25/26 (extrapolated for FC 27)

All items are search-snippet-confirmed unless marked [M]. Nothing is verified for FC 27.

- **Roles and Focus:** FC 25 introduced 31 roles, each with 1–3 focuses, giving 52 combinations at launch. Roles control off-ball positioning and movement, determine team width, affect chance creation and inform pressing. FC 26 added Ball-Playing Keeper, Wide Back, Inverted Wingback and Box Crasher, plus 10 new focuses including a Versatile focus for Fullback, Classic 10, Winger and Poacher. Wide Mids now have Defend, Support and Build-Up focuses.
- **Team tactics:**
  - Build-Up Play: Short Passing, Balanced, Counter.
  - Defensive Approach: Deep, Balanced, High, Aggressive.
  - Chance Creation: Balanced, Forward Runs, Possession, More Crossing.
  - Width and Depth sliders.
- **Presets vs custom:** pre-made tactical presets exist alongside Custom Tactics.
- **Gameplay layer [M]:** the 7 or so gameplay sliders live in a separate tuning set, as FC 24 and earlier did. Your plan stores these in Turbo instead.
- **Implication for Turbo:** the real EA tactic space is coarse (preset enums plus width and depth), so Turbo's finer sliders have to be computed values that get quantised into the nearest EA role, focus and approach. The extra detail will only show in the editor's preview and in values that can be written to the game DB or Live Editor. Whether the DB tactics fields are writable in FC 27 is unverified.

## 6. Footballing effect to numeric slider mapping (design proposals, not sourced)

Scale 0–100 unless stated. Direction is higher means more.

- **Team, in possession:** `tempo`, `attack_width`, `pass_directness` (0 = short), `buildup_short`, `cross_freq`, `shot_freq`, `dribble_freq`, `fluidity`, `risk_passing`.
- **Team, transition:** `counter_press`, `counter_attack`, `regroup_depth`.
- **Team, out of possession:** `line_height`, `def_width`, `engagement_height`, `press_trigger`, `press_intensity`, `tackle_aggr`, `offside_trap`, `marking_tightness`, `press_funnel` (-100 wide to 100 central), `press_gk`.
- **Per position / role:** `forward_runs`, `roam`, `hold_pos`, `width_bias`, `close_down`, `cross_depth`, `tackle_aggr`, `shot_freq`, `dribble_freq`, `risk_passing`, `pass_range`.
- **Role presets:** each FM role/duty seeds those position sliders.
- **Visual effects the preview should render:**

| Slider group | Visual |
|---|---|
| line_height and engagement_height | Horizontal lines for defence and press |
| def_width, attack_width | Width band |
| forward_runs, cross_depth | Run arrows |
| press_trigger, close_down | Pressing heat map |
| roam | Dot jitter radius |
| tempo | Pulse or speed of animation |

## 7. Caveats

1. Because no full pages were read, option names and per-role detail are [M] and need a check against the SI manual and fmscout (blocked here).
2. FM26 specifics are barely covered. Only a FM Scout headline, "FM26 Team Instructions: Complete Breakdown", surfaced. FM26's tactic changes remain unverified.
3. FC 27 DB fields, Live Editor tactics access and CPU-AI tuning options are unverified. Check the Live Editor's Formation Editor and Gameplay Attribulator on a real FC 27 build.

Sources:
- [FM26 Team Instructions: Complete Breakdown](https://www.fmscout.com/a-fm26-team-instructions-guide.html)
- [Football Manager 2024 Tactics (SI manual)](https://community.sports-interactive.com/sigames-manual/football-manager-2024/tactics-r4960/)
- [Transition Play Tactics](https://www.guidetofm.com/tactics/transition-play/)
- [Football Manager Player Instructions, Passion4FM](https://www.passion4fm.com/football-manager-player-instructions/)
- [Tactical styles, Passion4FM](https://www.passion4fm.com/guide-to-football-manager-tactical-styles-preset-tactics/)
- [Can the Football Manager AI figure out your tactic?](https://www.fmscout.com/a-can-the-football-manager-ai-figure-out-your-tactic.html)
- [Understanding mentality in football](https://pontadelancafm.substack.com/p/understanding-mentality-in-football)
- [FM Stag, Analysis tab tip](https://x.com/FM_Stag/status/1717325845520388504)
- [FC 26 Gameplay Deep Dive (EA, not fetched)](https://www.ea.com/en/games/ea-sports-fc/fc-26/news/pitch-notes-fc26-gameplay-deep-dive)
- [FC 25 tactics breakdown (Neal Guides)](https://nealguides.substack.com/p/fc-25-deep-dive-on-tactics)
- [FUT Custom Tactics Guide](https://footballgpt.co/fut/fut-custom-tactics)
- [FC 26 Live Editor (xAranaktu)](https://github.com/xAranaktu/FC-26-Live-Editor)

---

# Research report: EA SPORTS FC tactics and gameplay system for Turbo

## Source status and honesty notes

- **Official EA pages could not be read.** `ea.com` (FC 26 and FC 27 Pitch Notes), `fc26liveeditor.com`, Wikipedia, context7, Red Bull, thespike, fifplay and simulationdaily were all blocked by the egress proxy (EGRESS_BLOCKED). Everything below comes from WebSearch result summaries, which are lossy and third-party. I quote no EA text verbatim.
- **FC 27 is not unreleased as of today (2026-10-06).** Search results show official FC 27 Pitch Notes (gameplay, Career, FUT), plus guides and slider write-ups. The FC 27 claims below are second-hand summaries of those Pitch Notes. Verify them against the Pitch Notes before building to them.
- **FC 25 and FC 26 are the better-documented baseline.** I label each claim as Documented (consistent across several sources), Single-source, or Speculation.
- **Database and Live Editor internals are mostly not publicly documented.** One search for the table names `teamtactics`, `formations` and `playerassignments` returned nothing technical. Treat table names as unverified until you inspect your own `fifa_ng_db`.

## 1. FC 27 (second-hand summaries of the Pitch Notes)

- **Authentic Gameplay 2.0** is the headline Career feature.
  - Sources say it adds "25 new Gameplay Sliders and 10 new CPU Sliders".
  - It also brings heavier, more physical movement, smarter and less predictable CPU, more fouls, and weather that affects footing. (Documented across several sources, including a Soccer Gaming post quoting EA.)
  - The 25 new slider names were not retrievable.
- **CPU sliders are split into "CPU Opponent" and "CPU Teammate".** This is mainly for Player Career, where you lock to one player. The split is a useful model for Turbo's separate "both teams / my team / opposition" scopes.
- **Reported CPU slider names** (single-source guide summaries, so treat as indicative):
  - Defending Aggression, stand/slide tackle frequency, professional foul frequency, build-up speed.
  - Shot frequencies by type (regular, chip, low driven, finesse, long, power).
  - Crossing frequencies (regular, early), dribble frequency, skill move frequency, first-touch pass frequency.
  - Toggles: AI Behaviour (Custom/Dynamic/Tactic), Competitor Mode, and Player Based Difficulty, each per opponent/teammate.
- **Dynamic Opposition behaviour.** One source says AI managers adapt mid-match, for example narrowing defensive width if you overload the flanks. That is single-source and marketing-flavoured.
- **Gameplay changes in FC 27:**
  - Auto-tackles and AI defender and teammate-contain influence are reduced, so defending is more manual.
  - Crosses go into space more often.
  - A new "Attacking Spatial Awareness" system adds triggered curved runs and pass-and-follow runs.
  - Player Roles still decide which runs are available, and Role Familiarity sets how urgently players make them.
  - Corners are redesigned.
- **Positions:** one primary position and up to six secondary positions. The base OVR is calculated dynamically for the position being played.
- **Not found:** any FC 27 change to the Custom Tactics menu or the role list. Assume it matches FC 26.

## 2. FC 26 and FC 25 baseline

### Custom Tactics (Documented)

- **Defensive:**
  - Defensive Style: Balanced, Drop Back, Press After Possession Loss, Constant Pressure.
  - Width and Depth are 1–100 sliders, with community rules of thumb.
  - Typical advice: width 40–45 for a compact back four; depth 40–45 to protect a lead, 55–65 when pressing.
- **Offensive:**
  - Build-Up Play: Balanced, Fast Build-Up, Slow Build-Up, Long Ball.
  - Chance Creation: Balanced, Direct Passing, Forward Runs, Possession.
  - Width is a 1–100 slider.
  - Players in Box is 1–10.
  - Corners and Free Kicks are 1–10 (players committed forward).
- **Team Tactics presets:** Balanced, Possession, Counter and similar exist. I did not retrieve the full preset list.
- **Tactical Vision (Career):** the FC 26 snippet says a Build-Up Style and a Defensive Approach are required, which allows more flexibility within each vision. This is single-source.
- **Dynamic Tactics / Dynamic Development Plans (FC 26 Career):** development plans follow the player's closest tactical role and adapt as tactics change. Single-source.

### Player Roles and Roles++

- **Roles tell a player what to do when the team has the ball, and when it is lost.** (Documented.)
- **Each role has a focus.** Examples: Ball-Playing Defender (Defend/Build-Up/Aggressive), Playmaker (Balanced/Roaming/Build-Up), Box-to-Box (Balanced).
- **Role++ and PlayStyle++ tie a role to players with the matching PlayStyles.**
- **Role Familiarity** determines how well a player executes a role.
- **FC 26 additions:** Ball-Playing Keeper, Wide Back (CB), Inverted Wingback (LB/RB), Box Crasher (CDM), and a reworked Wide Midfielder that behaves like a wingback.
  - Other roles seen in results: Sweeper Keeper, False 9, Poacher.
  - Roles are no longer requirements; the Key Roles list was expanded.
- **I did not retrieve a full per-position role table.** Futbin's `/26/roles` and fifaindex's `/roles` appear to hold one but were not fetched.
- **Per-player attacking and defensive instructions** (Get Forward, Stay Back and similar) are not fully verified.
  - FC 25 and FC 26 mostly moved to roles plus focus, though FC 24-era per-player instructions probably still exist for some positions.
  - Do not treat a "Press / Cover" instruction list as confirmed.

### Formation positions

- I found no authoritative source for the internal position IDs.
- Community knowledge points to the `formations` table with position slots plus `teamformationteamstyle`-type tables. This is Speculation. Check your own DB schema in Turbo.
- FC 26 has about 45 formations.

## 3. Sliders and Authentic Gameplay

### The FC 26 slider set (Documented, from OS Forums and Evoweb community lists)

- **Per-side values (User/CPU):** Sprint Speed, Acceleration, Shot Error, Shot Speed, Header Shot Error, Pass Error, Pass Speed, Header Pass Error, Injury Frequency, Injury Severity, Goalkeeper Ability.
- **Positioning:** Positioning Marking, Run Frequency, Line Height, Line Length, Line Width, Fullback Positioning.
- **Others reported:** First Touch Error, Fouls and Game Speed (community lists mention them, but the evidence here is weaker).
- **Authentic values:** the "Authentic" preset values quoted in community lists (post-patch 1.1) include Sprint 36/36, Acceleration 48/49, Shot Error 55/65, Pass Error 54/65, Injury Frequency 90, Injury Severity 35, GK 55, Marking 77, Run Frequency 40, Line Height 62, Line Length 30, Line Width 55.
  - Different community lists quote different values, so these are not authoritative.

### What Authentic Gameplay does to sliders

- **Conflicting evidence.**
  - One summary says EA lets you "create custom gameplay sliders using the Authentic slider values as your starting point". That implies Authentic is a preset you copy, not a hard lock.
  - Another summary suggests the preset itself is locked and you must create a Custom preset based on it.
  - I could not confirm either from EA. The likely behaviour is that Authentic is the fixed default for Career and offline modes, and tweaking sliders means switching to a Custom preset seeded from Authentic.
- **Implication for Turbo.** Turbo can treat Authentic as the base and layer overrides on top.
  - This fits the user's goal of "keep authentic playstyle mode with dynamic opposition mode".
  - Sliders written through Turbo bypass the in-game UI entirely, which is the user's intent.
  - Whether the game accepts edited values while Authentic is selected is unverified.

## 4. Modding and Live Editor (mostly unverified)

- **Live Editor is documented as a C++ DLL injected into the running game.** It edits memory in real time. A separate listing says it covers 52 capability groups.
  - It includes a Formation Editor that can change a formation, swap players, and freeze a lineup in Player or Manager Career.
  - Manager AI can still pick players, or you can take over every starting slot.
  - Player Career support is described as partial (version 26.2.1).
  - Its Lua and DB-table access is documented on Aranaktu's GitHub, which I could not fetch.
- **Whether gameplay tuning files are editable offline without breaking Authentic mode is not documented in anything I found.**
  - The Frostbite tuning-file route (`fifaconfig`, `attribdb`, tuning `.ini`) and FIFA Mod Manager are Speculation for FC 26 and FC 27. I have no source for any of them.
  - That approach is also risky: EA anti-cheat, mismatched ModData caches, and Career saves overriding the static DB.
- **Ban and anti-cheat risk:** no information found. Offline-only Career use is the usual community stance, but I have no source here.
- **Manager AI and tactics tables** (`teamtactics`, `formations`, `playerassignments`) were not confirmed to exist in FC 26 or 27 under those names. They are plausible guesses from older FIFA databases. Inspect the real schema through Turbo's own DB tooling first.

## 5. Recommendations for Turbo 2.0

1. **Do not promise slider writes until feasibility is tested.** Build the tactics editor as a data model with an export or apply layer. Gate "write to game" behind a probe: does the in-game slider value change when Turbo writes it, and does Authentic stay selected?
2. **Model three scopes**, mirroring FC 27's own split: global (both teams), my team, and opposition (CPU Opponent, with CPU Teammate as a possible fourth).
3. **Seed defaults from the Authentic values above**, marked as community-sourced and version-dependent.
4. **Derive position-specific and role settings from the roles plus focus model.** Fetch Futbin's `/26/roles` for a full role-by-position table before finalising.
5. **Opposition variety:** use quality tiers (from team overall) × style archetypes (from the opponent's own tactic) → slider offsets. The FC 27 CPU slider list gives concrete knobs: aggression, tackle frequency, build-up speed, shot-type frequency, crossing, dribble and skill-move frequency.
6. **Treat everything FC 27-specific as provisional.** Pin version tags on presets so later patches can migrate them.

## Sources (via search results, not fetched directly)

- https://www.ea.com/games/ea-sports-fc/fc-27/news/pitch-notes-fc27-career-mode-deep-dive
- https://www.ea.com/games/ea-sports-fc/fc-27/news/pitch-notes-fc27-gameplay-deep-dive
- https://www.ea.com/en/games/ea-sports-fc/fc-26/news/pitch-notes-fc26-gameplay-deep-dive
- https://www.ea.com/en/games/ea-sports-fc/fc-26/news/pitch-notes-fc26-career-mode-deep-dive
- https://x.com/SoccerGaming/status/2083233690658767183
- https://forums.operationsports.com/forums/forum/soccer/ea-sports-fc-and-fifa/ea-sports-fifa-sliders/26900275-fc26-sliders-authentic-realistic
- https://evoweb.uk/threads/ea-sports-fc-27-slider-discussion.106387/
- https://allthings.how/best-fc-27-career-mode-slider-settings-for-authentic-gameplay/
- https://www.dexerto.com/wikis/ea-fc-26-guides-walkthrough-tips/all-new-player-roles-explained/
- https://www.futbin.com/26/roles
- https://fifaindex.com/roles
- https://footballpark.com/football-blogs/mastering-ea-fc-26-the-custom-tactics-that-give-you-the-edge
- https://context7.com/xaranaktu/fc-26-live-editor
- https://fc26liveeditor.com/features/

---

# Research: prior art for gameplay and tactics modding (FC 26 and earlier), and what it means for Turbo

**Source status.** Reached through search snippets and fetches:
- the FC 26 Live Editor README on GitHub;
- search summaries of fifa-infinity, evoweb, OS forums and Passion4FM;
- FM Arena and the FM manual, as search snippets only.

Blocked by the egress proxy: soccergaming.com, fifauteam.com, forums.ea.com, fifa-infinity.com (direct fetch).

Unpublished or unverified:
- EA has no public FC 27 modding policy.
- FC 27 specifics are extrapolated from FC 25 and FC 26 community reports.
- The claim that Javelin is required in FC 27 offline mode comes from a search snippet of a third-party guide (fifauteam), which I could not open.

## 1. Prior art: how slider-like changes are achieved

| Approach | Mechanism | Examples | Fit for Turbo |
|---|---|---|---|
| **A. Archive/EBX overrides** | The Frostbite Modding Tool or FIFA Mod Manager patches gameplay attribute data (attribdb/attribulator: passing error, shot error, first touch, AI tactics, referee strictness). The files load at startup. | V/PaulV2k4 FC25, Anth James Realistic, KIARIKA, FIFER's Realism Mod. Older FIFA 16 and 17 attribdb editors on soccergaming. | Static and global. It is a restart-time mod with no per-match or per-opponent logic. Turbo's goal needs runtime control, so this is only a reference for what is tunable. |
| **B. Career DB tables** | The career database holds `teamtactics`/`defaultteamsheets`/`formations` style tables. These come from the FIFA-era database schema (`fifa_ng_db`); I did not verify them for FC 27. Turbo's `db_edit.lua` and the `fce_to_preset.py` tool already touch them. | Aranaktu's Formation Editor and Database Editor, and Turbo's Database tab (fc26-parity row 20: formations). | Safe and persistent. It is limited to what the tables hold: formation, roles, team sheets, team traits. It has no pace, error or referee sliders. |
| **C. Runtime memory edits** | A DLL is injected into the game process, with a Lua API that can call game functions. The Live Editor README says it runs "without launching the anticheat". | FC 26 Live Editor. Its Lua API includes Gameplay Attribulator calls (`GameplayAttribulatorSetVar/GetVarType`, listed in `turbo/core/env.lua`). | This is Turbo's lane. The Gameplay Attribulator calls exist in the FC 27 Live Editor's Lua library and are not yet in Turbo 1.0 (fc26-parity row 40). |
| **D. Game-variable overrides** | The game's variable store (`GameVars`), which Turbo already writes through the game's own SetInt. | Turbo's Match setup, per `match_setup.md`. | Already working: `GAMEPLAY_CUSTOMIZATION/INJURY_*`, `NEVER_INJURE`, `OVERRIDE/WEATHER`, `OVERRIDE/TOD`, `OVERRIDE_MATCH_DIFFICULTY`, `DISABLE_CPU_SUBSTITUTION`. |

The Live Editor's own feature list for gameplay is thin: Formation Editor, Match-Fix, Speedhack, No Crowd and Playstyles. Nothing in it exposes team-tactic sliders, so Turbo would be breaking new ground.

An FC 17-era source reports that modders have mostly guessed what each attribulator field does. Any attribulator slider therefore needs in-game A/B validation, and the UI should say so.

## 2. Evaluation against Turbo's four requirements

**(a) General settings affecting both teams.**
- Injury frequency and severity: already implemented through `GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER/_CPUAI` and `INJURY_SEVERITY_USER/_CPUAI`. Set both sides to the same value for a global setting.
- Referee strictness: not a game variable. It is the Attribulator schema `gp_rules_refereestrictness` (`referee_strictness_*` fields), so it needs the Attribulator route (C).
- Player speed, passing error, shooting error and frequency, first-touch and trapping error, foul frequency: these live in gameplay attribulator data. The set-variable calls exist in the game and in the Live Editor Lua env. Turbo does not call them yet, and each field's real effect is unverified.
- Unknown: whether attribulator values are read once at match load or every frame. This decides whether a "reapply on match load" step is enough.

**(b) Per-team tactical settings (defense positioning, forward runs, line depth, width, pressure).**
- The persistent route is the DB team-tactics and team-sheet tables (B). The mini pitch can render these directly.
- At runtime, the AI tactic parameters per team are likely attribulator-backed and overridable per side. I have not seen the schema, so this is extrapolation. It is the main reverse-engineering task.
- FM gives a good vocabulary for the editor. Team instructions cover mentality, tempo, width, directness, defensive line, line of engagement and pressing trigger. Roles and duties are the per-position layer.
- FM's insight that defensive line and line of engagement together define vertical compactness suits the mini-pitch lines and heatmap overlay.

**(c) Per-position settings.** These map to player-instruction and role fields. FC 26 community guides show the same concepts in the game: Stay Back While Attacking, Cover Center, Get In Behind, Box Crasher (new in FC 26), and a defensive-line preset such as 40 or 45. Whether FC 27 adds more is unpublished. Implement what the live attribute and DB schema exposes.

**(d) Per-opponent profiles applied before each match.** Turbo already hooks fixtures and match setup, and the game-variable writes are cleared when needed. The mechanism is: on fixture or match-load detection, resolve the opponent's team id, pick or compute a profile, then apply it with the same SetInt and attribulator calls. Scale it from opponent rating and tactics, since quality-based matching is the user's goal. The profile choice itself is pure Turbo logic with no engine risk.

## 3. Risks

- **Anti-cheat.** The Live Editor README says it bypasses EAAC/Javelin and is "for offline use only". Community reports say FC 27 requires Javelin even in single-player. I could not verify that, and it is the largest unknown for deploying locally. Users report bans and a restricted-access state from the anti-cheat.
- **Online contamination.** Overrides persist in the global store until the game closes (`match_setup.md` section 1). Attribulator edits are probably also process-wide. Mitigation: a hard offline guard that blocks all writes when an online session or mode is detected, an auto-clear on exit, and a visible "overrides active" status. Turbo already has kill-switch files and a Status line.
- **Save corruption.**
  - Level 1 (DB edits) writes persistent career data, so it needs backups and range validation.
  - Level 2 and 3 values are runtime-only, so they cannot corrupt a save unless a bad value crashes the game mid-save. Never write during a save.
  - `match_setup.md` already flags null-write hazards in SetInt, which Turbo guards.
  - The simulated-match path cannot be overridden: simsettings.ini is reloaded over the store (section 2), so sliders affect only played matches.
- **Gameplay breakage.** Extreme error or speed values may break animations or AI. Clamp to tested ranges and ship presets only after A/B tests.
- **Version drift.** Offsets and signatures change with each patch. Keep the signature-check workflow already in `docs/re/*-signatures.json`.

## 4. Recommended path, safest first

**L1: DB-only (ship first).**
- Persist per-team tactics: formation, role assignment, instructions per position, from the existing tables.
- Build the mini pitch, preset library and per-category presets on this.
- Pitch visuals are computed in Turbo from the settings (lines, width, heatmap approximations), clearly labelled as a preview and not engine truth.
- No injection risk beyond what Turbo already does.

**L2: DB plus reapply on match load.**
- Add the game-variable layer, which is already proven (injuries, weather, difficulty, CPU subs).
- Store the opponent profile per team id or fixture, and apply it on match-setup detection, clearing it afterwards.
- This also covers the per-opponent profile logic, using only known-safe variables.

**L3: runtime attribulator and game calls.**
- Add the Gameplay Attribulator set and get calls for pace, error and referee fields and the per-team AI tactic parameters.
- Mark each slider "experimental" until an in-game A/B check passes, add a per-slider kill switch and range clamps, and enforce the offline-only guard.
- Do this last, because it depends on reverse engineering the schema and on the anti-cheat question.

**Suggested next steps.**
1. Dump the attribulator schema list from the running game (names and types) to decide which sliders are real.
2. Test one harmless slider (for example, a global shot-error scale) at L3 to learn when values are read.
3. Confirm anti-cheat behaviour for FC 27 offline on the user's machine.

## Sources
- [FC-26-Live-Editor README (xAranaktu)](https://github.com/xAranaktu/FC-26-Live-Editor/blob/main/README.md)
- [FC 26 Live Editor changelog](https://github.com/xAranaktu/FC-26-Live-Editor/blob/main/changelog.txt)
- [Best Gameplay Mods for EA FC 25 (fifa-infinity, search snippet)](https://www.fifa-infinity.com/ea-sports-fc/best-gameplay-mods-for-ea-fc-25/)
- [V / Paulv2k4 FC25 Mods (evoweb)](https://evoweb.uk/threads/v-paulv2k4-fc25-mods-gameplay-career-tools.99126/)
- [FIFA 17 attribdb gameplay editor (soccergaming, search snippet only)](https://soccergaming.com/forums/threads/tool-fifa-17-attribdb-gameplay-editor-released-ebx-gameplay-modding-for-fifa-17.6475647/)
- [FC 27 EA AntiCheat Guide for PC (fifauteam, snippet only)](https://fifauteam.com/fc-27-ea-anticheat-pc/)
- [FC 26 Modding (EA forums, snippet only)](https://forums.ea.com/discussions/fc-26-general-discussion-en/fc-26-modding/12577305)
- [Are mods safe to use in FC26? (Steam discussion)](https://steamcommunity.com/app/3405690/discussions/0/693124570228605371/)
- [How to Set EA FC 26 Custom Tactics & Player Instructions (fdaytalk)](https://www.fdaytalk.com/ea-fc-26-custom-tactics-and-player-instructions/)
- [Best EA FC 26 formations & tactics (Dexerto)](https://www.dexerto.com/wikis/ea-fc-26-guides-walkthrough-tips/best-formations-and-custom-tactics/)
- [Football Manager 2024 Tactics manual (Sports Interactive)](https://community.sports-interactive.com/sigames-manual/football-manager-2024/tactics-r4960/)
- [FM Arena: team instructions](https://fm-arena.com/thread/7467-useful-information-regarding-team-instructions/)
- [Passion4FM: player instructions](https://www.passion4fm.com/football-manager-player-instructions/)
- Repo files read: `/home/user/FC27-Editor-Turbo/docs/re/match_setup.md` and `/home/user/FC27-Editor-Turbo/docs/fc26-parity.md`.