# TURBO 2.0 PLAN

Labels used throughout:
- **VERIFIED**: read in code or docs by a discovery agent.
- **ASSESSED**: reasoned from evidence, not observed.
- **UNCERTAIN / NEEDS-RE**: not known.

Nothing below was executed. The Lua tests could not run in the cloud sandbox, because `lua` and `turbo/le27/libs` were missing. The real FC 27 schema dump (`fc27_db_schema.json`) was never available. The EA and FM research was search snippets only, because the fetches were blocked. FC 27 specifics from the web are second-hand.

## 1. Executive summary

- **Squad-role bug.** No age-based skip exists in the code (VERIFIED). Two leading causes remain, and neither is confirmed.
  - **A:** the squad is read from `cm_teamsheets` and stops at the first empty slot.
  - **B:** the game recomputes roles on season reset and on events 0x17 and 0x5F, and Turbo does not re-apply them.
  - The plan ships one fix covering both, plus counters and a diagnostic so the real cause shows up on your machine.
- **Tactics editor.** The full UI is buildable now, but most sliders have no proven write path.
  - **REAL:** injury frequency and severity per side, injuries off, weather, time of day, difficulty, CPU substitutions off, `players.role1..9`, preferred positions, traits and body type.
  - **Everything else** (speed, error rates, foul and card settings, line depth, width, pressing, forward runs, per-position instructions) has no known write site. These sliders ship as Preview or NEEDS-RE and are shown honestly as such.
  - Phase 0 probes on your machine decide which of them get promoted.
- **Opposition variety.** A deterministic, seeded, unit-tested solver is buildable. Its game-side output is limited today to injury and difficulty offsets, which is weak variety. Richer variety depends on DB tactic tables or the Attribulator, both unproven.
- **Presets.** Pure Turbo work with no unknowns. It ships in full.
- **Cleanup.** The audit found 36 spots where raw codes or IDs leak into the GUI. One `describe()` function fixes most of them.
- **Body types.** The code names only 10 body types (1–9 and 11). Whether real players use other codes is unknown. A probe will tell, and the catalogue is generated from its output.
- **Net outcome if Phase 0 finds no editable tactic table:** 2.0 is a preview-and-profile tool plus the role fix, cleanup, body types and live match-environment sliders. The UI will say so rather than ship sliders that do nothing.

## 2. Squad-role bug

### Root cause (confidence labelled)

| Cause | Evidence | Confidence |
|---|---|---|
| No age-based skip in `role_of` | `team_mass.lua:232-237` returns 3 for every age 19 or over | VERIFIED |
| **A. Membership from the teamsheet.** `user_squad()` walks `playerid0..52` and `break`s at the first nil or `-1` (`game.lua:88`). `role.apply` uses that set (`squad_role.lua:105`), not `teamplayerlinks`. A gap before the reserves drops everyone after it, including veterans in late reserve slots. | Code read. No real `cm_teamsheets` row was seen, and the FC 27 slot order is unknown. | ASSESSED, most likely code-side cause. The simulator writes gap-free sheets, so tests cannot catch it. |
| **B. The game rewrites roles.** Event 0x17, season reset and signing (0x5F) rebuild the table with `ComputeSquadRole`. A Future role on a player over 23 becomes Sporadic. | `docs/re/player_status_roles.md` §3 | VERIFIED, but not age-specific. It looks like "over 30" because rank-computed Crucial and Important players tend to be veterans. |
| C1. A stale `playerloans` row triggers the loaned-in filter (`squad_role.lua:84-94`). | Code read | ASSESSED, low |
| C2. A missing birthdate silently skips the player. | Code read | ASSESSED, low |
| C3. `locate()` requires `valid == matched`, so one stray role byte rejects the whole vector. | Code read | Unlikely. It would break all ages. |
| C4. The vector holds at most 52 entries. | Code read | VERIFIED, not age-specific |

The contract-table pass is dead, because `career_playercontract` does not exist in FC 27 (VERIFIED).

### Fix

1. **Take the squad from the vector.** Add `psm_members(layout)` in `squad_role.lua`, returning every entry with a pid above 0. The write target is `psm_members` intersected with the user team's `teamplayerlinks`. `cm_teamsheets` remains only the anchor that `locate` scores against.
2. **Stop breaking on gaps.** In `game.lua user_squad`, skip `-1` and nil slots, scan all slots, and fall back to `teamplayerlinks` when the sheet yields fewer players. Log the difference.
3. **Make `locate()` tolerant.** Accept role bytes 0..5 or 0xFF, accept the vector at 90% valid or better, and log the pid with the bad byte.
4. **Report skips.** The summary counts players with no vector entry, players with no birthdate, loaned-in skips, and vector pids outside the squad.
5. **Re-apply after the game rewrites roles.** Hook `SEASON_RESET` and event 0x17 and re-run the saved rule. Kill switch: `turbo_output/role_reapply_off.txt`. UNCERTAIN: whether the bridge actually sees these events, since it polls on career-mode events. This must be checked before relying on it.
6. **Role rule editor** (in `team_mass.lua` and Teams > Mass actions, replacing the fixed "Squad roles" button at `ui_teams.cpp:311`). It has ordered rules by age band, OVR rank and position group, plus per-player pins. Defaults reproduce today's behaviour (19+ Rotation, younger Prospect). A preview table shows player, age, OVR, old role, new role and skip reason before anything is written.
7. **Diagnostic first.** A read-only module dumps one `cm_teamsheets` row and the vector with ages. Run it on a career where the bug occurs. This separates A from B.

### Tests

Extend `turbo/tests/world.lua` with a `sheet_gap` option and a 52-entry vector with empties and mixed role bytes. Add cases to `t04` and `t18`:
- Sheet gap at slot 8: every `teamplayerlinks` player gets a role.
- Ages 17 to 38, with a 34-year-old on the last slot and a missing-birthdate player counted in the summary.
- Stale `playerloans` row counted as skipped.
- Vector cleared and refilled as event 0x17 would, then the rule re-applied.
- A role byte of 9 still locates the vector, with a warning.

## 3. Tactics editor

### Scope and placement

- **Per-team and per-position** work goes in a new "Tactics" sub-tab inside `##ttabs` in Teams (`ui_teams.cpp`), because "active tactic" is per team (`app.sel_team`).
- **Both-team game settings** go in a "Game settings" section of the same panel, shown when no team is selected. This avoids changing the 7-tab numbering (`app.cpp:921-941`, `test_main.cpp:3341`). A top-level "Tuning" tab is the fallback if the sub-tab gets crowded.

### UX

Three columns:
- **Left, about 22%:** scope pills (`Match (both teams)`, `My team`, `Position/Role`, `Opposition`) and the preset browser.
- **Centre:** the mini pitch and its layer toggles.
- **Right, about 30%:** collapsible slider cards for the current scope.

Each slider row has a label and a plain-language hover description, with the technical name on a dim second line. It also has a typed value, a reset dot, a status badge (Live, DB, Preview, RE) and a diff against the base value. The footer shows "N changed", Apply, Revert and Save as preset. Match sliders default to "game decides" (unset), so Authentic stays untouched until you opt in. A dry-run toggle shows what would be written.

**Mini pitch.** The widget is an `InvisibleButton` region with a fixed 105:68 aspect (so ImGui owns input capture and the input shield works). Drawing uses `ImDrawList` only.
- Formation dots come from the team's `formations` row, or from a built-in fallback table if the schema probe fails.
- A dropdown previews other formations without writing.
- Phase toggle: With ball, Without ball, Overall.
- Compare mode shows the loaded profile against the current values as value deltas ("62 to 70"), not distances.
- An opposition ghost pitch shows the solver's chosen style and lines.

**Visual honesty rules** (adopted from the honesty review). Every visual carries a provenance tier and an application status.

| Tier | Meaning | Drawing |
|---|---|---|
| Exact (E) | Straight from stored data: formation dots, position and role labels, squad-status badges, injury numbers | Solid fills and lines |
| Derived (D) | Monotonic geometry from a slider: defensive line, press line, width band, run arrows | Dashed or thin, labelled "62/100 (schematic)", never in metres |
| Modelled (M) | Heat grid, roam rings, risk hint, opposition card | Stippled or hatched, monochrome ramp labelled "relative intensity (arbitrary)", no numbers. Off by default. |

- A persistent non-dismissible chip reads "Model view: Turbo's estimate, not game output".
- Preview and RE sliders render at reduced opacity with a "not applied to game" tag. A strip says "N of M visible settings are preview-only".
- A "What the game will receive" panel lists only Live and DB writes.
- When a kill switch is on, effect layers turn grey.
- No metre units, no outcome language, no animation. The earlier design's "about 19 to 48 m" mapping and animated roam are dropped. Those formulas were invented.
- Formation dots show saved data. Until the reload differential test passes, they carry the label "saved data, in-game effect unverified".
- Dragging a dot writes to `formations`, so it needs a confirmation and a warning that the game may overwrite it at save.

### Slider catalogue

Defaults are "game decides" unless stated. Authentic numbers below are community-sourced hints only. Lists disagree, and the internal scale is unknown.

**Status key:**
- REAL: proven write path.
- PLAUSIBLE: needs one verification step.
- NEEDS-RE: no known write site. It shows as Preview only until promoted.

**Match settings (both teams)**

| Setting | Scope | Range | Storage / write path | Status |
|---|---|---|---|---|
| Injury frequency | match, per side | 0–100 | `gv` `GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER` and `_CPUAI`, set equal for "both" | REAL (code verified; in-game effect check pending) |
| Injury severity | match, per side | 0–100 | `gv` `INJURY_SEVERITY_USER` and `_CPUAI` | REAL (same caveat) |
| Injuries off | match | on/off | `gv` `NEVER_INJURE` | REAL (accepted by the game; effect in a match untested) |
| Weather | match | enum | `gv` `OVERRIDE/WEATHER` | REAL (id names not in repo) |
| Time of day | match | enum | `gv` `OVERRIDE/TOD` | REAL (id names not in repo) |
| Difficulty | match | enum 0–5 | `gv` `OVERRIDE_MATCH_DIFFICULTY` | REAL |
| CPU substitutions off | match | on/off | `gv` `DISABLE_CPU_SUBSTITUTION` | REAL |
| Player speed (sprint, acceleration) | match | 0–100 | Possibly Attribulator | NEEDS-RE |
| Pass error, pass speed | match | 0–100 | Unknown | NEEDS-RE |
| Shot error, shot speed, shot frequency | match | 0–100 | Unknown | NEEDS-RE |
| First touch / trapping / ball control error | match | 0–100 | Unknown | NEEDS-RE |
| Goalkeeper ability | match | 0–100 | Unknown | NEEDS-RE |
| Foul frequency | match | 0–100 | No variable found | NEEDS-RE |
| Card strictness | match | 0–100 | Attribulator schema `gp_rules_refereestrictness`. Write method and read timing unknown. | NEEDS-RE |

**Team settings** (my team, reused as offsets for opposition)

| Setting | Scope | Range | Storage / write path | Status |
|---|---|---|---|---|
| Formation (positions) | team | list | `formations` table (real field list from schema dump). `TeamSheet` runtime copy may overwrite DB edits at save. | PLAUSIBLE |
| Mentality (7 steps) | team | enum | `cm_mentalities` (fields unknown) | NEEDS-RE |
| Defensive line depth | team | 0–100 | `teamtactics`-style table is a guess only | NEEDS-RE |
| Line length / compactness | team | 0–100 | Unknown | NEEDS-RE |
| Defensive width, attacking width | team | 0–100 | Unknown | NEEDS-RE |
| Defence positioning / marking tightness | team | 0–100 | Unknown | NEEDS-RE |
| Pressure intensity, press trigger, engagement height | team | 0–100 | Unknown | NEEDS-RE |
| Forward-run frequency, players in box (1–10) | team | 0–100 | Unknown | NEEDS-RE |
| Tempo, pass directness, build-up short | team | 0–100 | Unknown | NEEDS-RE |
| Cross frequency, shot patience, dribble frequency | team | 0–100 | Unknown | NEEDS-RE |
| Counter-press, counter-attack, regroup depth | team | 0–100 | Unknown | NEEDS-RE |
| Offside trap, tackle aggression | team | 0–100 | Unknown | NEEDS-RE |
| Build-up play, chance creation, defensive style (FC enums) | team | enum | Runtime `TeamSheet` or DB | NEEDS-RE |

**Position and role settings** (selected pitch dot; role/duty seeds the values, FM-style)

| Setting | Scope | Range | Storage / write path | Status |
|---|---|---|---|---|
| Role 1..9 | position | enum | `players.role1..9`, existing editor | REAL (ranges from dump) |
| Preferred positions, traits | position | enum | `players` fields, existing editor | REAL |
| Formation slot offset (drag) | position | 0–1 | `formations` offsets (real field names unknown) | PLAUSIBLE |
| Forward runs, depth bias | position | 0–100 | Unknown | NEEDS-RE |
| Roam vs hold, width bias | position | 0–100 | Unknown | NEEDS-RE |
| Close-down, tackle aggression, mark tight | position | 0–100 | Unknown | NEEDS-RE |
| Shoot, dribble, risk passing, cross depth | position | 0–100 | Unknown | NEEDS-RE |

Role names in the UI are provisional ("Turbo preset") until the dump or Live Editor localization confirms the FC 27 list.

**Opposition settings** (solver outputs and controls)

| Setting | Scope | Range | Storage / write path | Status |
|---|---|---|---|---|
| Opposition injury offsets | opposition | 0–100 | `gv` `_CPUAI` names | REAL |
| Difficulty offset | opposition | enum | `gv` difficulty | REAL, but it applies process-wide |
| Variety strength, quality scaling, mirroring, underdog and favourite bias, jitter | opposition | 0–100 | Turbo solver parameters | REAL (pure Turbo) |
| Style family weights | opposition | per family | Turbo solver parameters | REAL (pure Turbo) |
| Opponent formation and roles | opposition | list | DB, if the AI reads it at match setup | NEEDS-RE |
| Per-tier CPU error and aggression offsets | opposition | 0–100 | Unknown | NEEDS-RE |
| Mid-match adaptation | opposition | 0–100 | Needs an in-match hook | NEEDS-RE |

### Honest limits

- Overrides last until the game closes, apply process-wide, and are not saved with the career (VERIFIED).
- Simulated matches ignore all overrides, because `simsettings.ini` is loaded over them (VERIFIED). Everything here affects played matches only.
- Whether game variables or the Attribulator survive Authentic Gameplay and Dynamic Opposition is unverified. The failure mode is a silent no-op, so the first in-game test covers it.

## 4. Opposition variety system

The goal is to match opposing playing styles to their tactics and player quality.

**Engine** (`core/opposition.{h,cpp}`, pure C++, no ImGui or game memory).

Inputs per fixture (`TeamFacts`):
- Squad OVR band (top-14 mean) and `strength_ratio` = opponent / user.
- Reputation and form, mean age, mean pace and passing, and an aerial flag.
- Formation shape counts, mentality, and home or away.

Style families are scored 0..1 and blended, not hard-assigned:

| Family | Triggers (design proposals, not sourced values) |
|---|---|
| Gegenpress | High pace, young squad, aggressive mentality, ratio near or above 1 |
| Low block counter | Ratio below 0.9, defensive mentality, pace |
| Possession | High passing, strong OVR |
| Wing play | Wide forwards or wingbacks plus a tall striker |
| Direct | Low passing, tall strikers, low OVR |
| Balanced | Fallback |

- **Quality sets amplitude, not style.** Offsets are bounded by `max_delta` (default 15 on a 0–100 scale) so each team stays recognisable.
- **Seed:** `hash(career, season, matchday, opp_teamid, salt)` into xorshift. The same fixture gets the same profile in a given career. A "re-roll" bumps the salt. There is no `os.time` or `rand()` in the engine.
- **Rules:** an ordered list in `turbo_output/opp_rules.json` (`when`, `then` and `priority`). Built-in rules ship as data in the same format and users can override them by id. "Explain" mode lists which rules fired.
- **Overrides:** global scale (0 = off), per-archetype strength, a team pinned to a preset, a per-fixture one-off, and a "none" mode.

**Application.** On fixture detection, resolve the opponent, compute the profile, then write only the Live-bound keys (injury, difficulty) through `gv::apply`. Clear on match end, on exit, or when the kill switch fires.
- UNCERTAIN: how Turbo detects "this match's opponent". Nothing in the reports shows it. This is a Phase 0 research item.
- DB-based tactic variation for the opponent (formation, roles) is possible only if the AI reads those fields at match setup. That is unknown.
- Dynamic Opposition and Authentic stay selected in game. Turbo never touches the EA slider menu.

**Honest statement.** Until DB tactic tables or the Attribulator are proven, the game-side effect is injury and difficulty offsets, which is not meaningful style variety. The engine, its preview card and its tests ship anyway, because they are proven code waiting for a channel. Do not describe it as delivering style variety in 2.0.0.

## 5. Presets and profiles by category

- **Categories:** `match`, `team`, `position`, `opposition`, `bundle` (a snapshot of all four plus rules).
- **File:** `turbo_output/tactic_profiles.json`. This is user data and is separate from `reapply` and from `gui_settings.json`. Only the active profile name per category goes in `gui_settings.json`.

```json
{"turbo_tactic_profiles":1,"game":"fc27","build":"<turbo ver>",
 "profiles":[{"id":"uuid","category":"team","name":"Gegenpress 4-3-3",
   "tags":[],"note":"","created":"...","schema_version":1,"game_build":"...",
   "formation":"4-3-3","values":{"team.press_intensity":80},
   "enabled":["team.press_intensity"]}],
 "active":{"match":"id","team":"id"}}
```

- **Keys** are stable slider ids from the registry. Unknown keys are kept and ignored on round-trip, and counted on load as `parse_reapply_json` does.
- **Safety:** atomic save (`.tmp` then rename), a corrupt file set aside as `*.unreadable.json`, and a version migration table. A file with a newer version loads read-only. Save and load happen only at construction and on user action, never in the frame path.
- **Every slider has an `enabled` flag.** A disabled slider is never written, so untouched Authentic is the default state.
- **Actions:** save as, load, overwrite, duplicate, delete (confirm), rename, compare, and export/import (single profile, category or bundle) through `ui/file_picker`. Import previews diffs and name clashes (rename, replace, skip).
- **Built-ins** are immutable code: Authentic baseline (all off), Tough referee, Physical, Fewer injuries, High scoring, and archetype presets seeded from FM role and duty. Only Live values apply until RE promotes more. Users clone them.

## 6. Cleanup

### Codes to descriptions

Approach: one `describe(table, field, value)` in `core/field_labels.h`. It returns a name for every enumerable field and falls back to "Name N" only when there is no name. Route `App::edit` logging, the undo toast, undo and mass-edit summaries, and the tooltips through it. Raw names and codes move to a dim second line.

The audit found no GUI string literally called a "pending edit code". It found the pending-command label, the queued status and raw field and ID echoes. The table covers those.

| Location | Shown now | Becomes |
|---|---|---|
| `widgets.cpp:76` range tooltip | `overallrating [0 .. 99]` | "Overall (0–99)", with the raw name as a second line |
| `widgets.cpp:361` code-combo tooltip | `bodytypecode = code 5 [1 .. 11]` | "Body type: Tall and Normal" |
| `widgets.cpp:356` combo row hover | `code 3` | Dropped, or the meaning where one exists |
| `widgets.cpp:173` date tooltip | `stored as birthdate = 150123` | "Stored by the game as a day count" |
| `widgets.cpp:290`, `field_labels.h:125` | `Unknown (code 12)` | "Player-specific body model 12" for `bodytypecode`, else "Unnamed N" |
| `widgets.cpp:31` | "Nationality ID" integer | Searchable nation combo from `nations.nationname` |
| `ui_teams.cpp:28` | `rivalteam`, `cityid` integers | Team-name picker. City name, or hide it (lookup table UNCERTAIN). |
| `ui_teams.cpp:708` | Manager `teamid`, `nationality`, `personalityid`, `outfitid` as integers | Team, nation and personality names |
| `ui_teams.cpp:413` | `ID 241 \| OVR 78` | "Juventus, overall 78", with the ID in a tooltip |
| `ui_teams.cpp:256`, `ui_players.cpp:1103` | `Roma (52)` | Name only, ID in a tooltip |
| `ui_players.cpp:1062`, `ui_faces.cpp:888` | `head asset 4123, head class 0` | "Real face (own head model)" or "Generic head" |
| `ui_faces.cpp:835` | `player 20801, head 20801` | "Name, real face", with the numeric ID as a small hint |
| `face_filter.cpp:185,192` | `Dark Brown (code 3)` | Drop `(code N)` and `(style N)` |
| `field_labels.h:78,86` | `Short hair #123` in summaries | `hair::describe` text |
| `field_labels.h:98,132` | `Boots #1432`, `Head #20801` | "Boots style 1432 (no name)". Named catalogues if data appears. |
| `widgets.cpp:46-50` | About 40 appearance fields print as "Headtypecode" etc. | Extend `label_map()` |
| `widgets.cpp:408-421` all-fields view | Raw field names | Keep, add a "Show friendly names" toggle |
| `app.cpp:690` edit log | `players.overallrating = 87` | "Overall changed 84 → 87 (Player X)" |
| `app.cpp:727` undo toast | `undone: Role1 back to 14` | "undone: Role 1 back to CDM Holding+" |
| `app.cpp:~633,757` | Free-form pending label in toasts | User-facing verb catalogue ("Moving player…"). Keep the label for logs. |
| `ui_match.cpp:255,261` | `queued: GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER = 30` | "Injury frequency (your team) set to 30; applies at kick-off" |
| `ui_match.cpp:244` | Raw game variable in tooltip | Human description first, technical name second |
| `match_setup.cpp:322-332`, `ui_match.cpp:236,243` | Weather, time of day, difficulty as integers | Named combos. Weather and time-of-day names are not in the repo (UNCERTAIN). |
| `bridge.lua:702,730,762,878` | `"op 7"`, `"unknown player_morale code 7 ..."` | Plain wording ("update Turbo") |
| `bridge.lua:~712` | `game call status 5` | "The game call failed without a message" |
| `ui_tools.cpp:448-515` | Hook addresses, ticks, DB service pointers | Collapsed "Technical details" with a one-line health summary on top |
| `ui_tools.cpp:490-512`, `ui_callnames.cpp:143,291-296,~1214` | "write callname ... to his playernamemap row", "queued for Turbo's Lua side" | "Set Kane's spoken name to …" and "Waiting for the game to be ready; it will apply automatically" |
| `ui_teams.cpp:504` | `addon %d` | "bonus from results" |
| `ui_faces.cpp:827` | "3D pending" | Add a hover explanation |

**Deliberately kept:**
- The ID in destructive confirms (`ui_players.cpp:952`).
- The release code number in bug-report text (`game_calls.h:121`, `player_move.cpp:530`).
- `ui_teams.cpp:529` ("A sack is pending…"), which is already a description.

**Test:** a native UI walk that fails if any tooltip contains `code N` outside an allow-list.

### Player-specific body types

**What exists (VERIFIED).**
- `field_labels.h:24` names 1–9 (the generic height by build grid) and 11 ("Very Tall and Lean"). Code 10 has no entry.
- Anything else prints "Unknown (code N)".
- The editor is a combo of the named codes plus a typed value.
- Live Editor's `bodytype_N` strings name only the generic types.

**What is unknown.** Whether real players carry codes outside this set. The clean explanation is dedicated models for stars, but nothing in the repo confirms it.

**Plan.**
1. A probe, `bodytypes.lua`, writes `turbo_output/bodytypes_fc27.json`. For each distinct `bodytypecode` it records the player count, height and weight range, `headclasscode`, `gender`, and up to 5 example names.
2. Compare the result with `field_labels.h:24` and the legal range from `GetDBMeta`.
3. Load `bodytype_N` names from Live Editor's `loc/eng_us/localize.json` at runtime. Hard-code nothing beyond that.
4. Generate `bodytype_catalog.{h,cpp}` and `bodytype_data.h` from the probe JSON, on the `hair_catalog` and `gen_hair_styles.py` pattern.
5. Label specific models from probe data ("Specific body #N (examples: …)"), never from guesses.
6. UI: a gallery with "Generic" and "Player-specific" groups, filters by height and weight, "used by N players", and "Copy body type from player…". Apply it to the manager editor too.
7. Writing a specific body onto a generic head may mismatch skeleton and kit (UNCERTAIN). It is not offered by default until tested in game.

**Also missing, per the audit (ASSESSED, since the schema was absent).** There are no editors for `playerformdiff`, injuries or `playerstats`-style tables, and manager coverage is thin. A new script lists schema fields of `players`, `teams` and `manager` that appear nowhere in `ui/*.cpp` or `field_labels.h`.

## 7. General improvements

- Extend `check_fc27_schema.py` with a coverage mode, and add the new table and field names.
- Add a Status health summary, "All features available" or "N features unavailable", above the collapsed technical section.
- Show an "overrides active" banner on Status and in the Tactics tab header, listing each active game variable. Auto-clear overrides on exit.
- Keep all new code inert until a career connects. `maybe_reapply` stays post-`refresh()`.
- Make every slider data-driven from a registry (`SliderDef`: key, scope, label, description, min, max, default, step, binding, status). This bounds maintenance, because each hand-written file also needs adding to `run_native.sh` and `build_win.sh` by hand.
- Register everything new in `turbo.lua` `M.MODULES`, with stubs in `lua/scripts/`.
- Update `docs/turbo-reference.md`, `CHANGELOG.md` and `turbo/package/TURBO_README.md` (kill-switch table).
- Deferred beyond 2.0:
  - A global opposition rule editor UI.
  - Mid-match adaptation.
  - Any new native hook.
  - Any `SetSheet` game call.

## 8. Research findings

### Football Manager → mapped sliders

Snippet-confirmed unless marked [M] (from memory). All slider mappings are my design proposals, not sourced values.

| FM concept | Source status | Turbo slider |
|---|---|---|
| Mentality (Very Defensive to Very Attacking) [M] | Partly snippet | `team.mentality` enum |
| Tempo, Passing Directness, Attacking Width | Snippet | `team.tempo`, `team.directness`, `team.width_att` |
| Creative Freedom / Fluidity | Snippet | `team.fluidity` |
| Counter-Press vs Regroup; Counter vs Hold Shape | Snippet | `team.counter_press`, `team.counter_attack`, `team.regroup_depth` |
| Defensive Line, Line of Engagement | Snippet/[M] | `team.line_depth`, `team.engagement_height` |
| Trigger Press, Tackling, Offside Trap | Snippet/[M] | `team.press_trigger`, `team.tackle_aggr`, `team.offside_trap` |
| Roam From Position / Hold Position | Snippet | `pos.roam`, `pos.hold_pos` |
| Get Further Forward / Stay Back | [M] | `pos.forward_runs`, `pos.depth_bias` |
| Close Down More/Less | Snippet | `pos.close_down` |
| Dribble More/Less, Shoot More/Less | Snippet | `pos.dribble_freq`, `pos.shot_freq` |
| Take More Risks, Cross From Deep / Byline | Snippet | `pos.risk_passing`, `pos.cross_depth` |
| Roles and duties (Defend / Support / Attack) [M] | Memory | Duty seeds an `attack_bias` that fills the position sliders |
| Opposition Instructions | Snippet | The per-position opposition sliders |

**FM visuals** that inform the preview:
- Formation diagram with role/duty labels and run arrows.
- Analysis tab with average positions (With ball, Without ball, Overall) and heat maps.
- FM's message that the engine rewards coherent decisions. Turbo uses this only as a labelled heuristic hint.

**FM AI** (partly snippet, partly [M]): the AI sets its approach by reputation and favourite status rather than reading your tactic. Over-performing teams push more AI sides cautious. Managers have preferred styles. This supports quality-driven and style-driven opposition offsets with a per-match seed.

### EA FC 27 possibilities and honest limits

**Baseline (documented across sources for FC 25 and FC 26).**
- Roles with focuses (31 roles and 52 combinations at FC 25 launch, expanded in FC 26).
- Custom tactics: build-up play, chance creation and defensive style as enums, with width and depth as 1–100 sliders. Players in box is 1–10. Corners and free kicks are 1–10.
- FC 26 community sliders: sprint and acceleration, shot and pass error and speed, header errors, injury frequency and severity, GK ability, marking, run frequency, line height, line length, line width.

**FC 27 (second-hand summaries of Pitch Notes that nobody read; verify before building to them).**
- "Authentic Gameplay 2.0", with reportedly 25 new gameplay sliders and 10 new CPU sliders.
- CPU sliders split into "CPU Opponent" and "CPU Teammate".
- Reduced auto-tackles.
- No change found to the Custom Tactics menu or the role list.

**Limits.**
- The real EA tactic space is coarse (enums plus width and depth). Finer Turbo sliders can only affect the game if written to DB fields or runtime structures that exist. That is unverified.
- Authentic Gameplay might be locked or a copyable preset. Evidence conflicts.
- Table names like `teamtactics` and `playerassignments` are guesses from older FIFA schemas. Only a dump from your machine settles them.
- The Attribulator route is the main lead for global gameplay sliders. VERIFIED only that `GameplayAttribulatorSetVar` and `GameplayAttribulatorGetVarType` are listed in `core/env.lua:136`. Semantics and FC 27 coverage are UNCERTAIN.
- Anti-cheat: Live Editor claims offline-only use. A snippet claims FC 27 requires Javelin even offline, and that is unverified. Add no new hooks in 2.0.

### Prior art (mods research)

| Approach | Fit |
|---|---|
| Archive/EBX overrides (FMT, attribdb) | Static, restart-time and global. Useful only as a map of what is tunable. |
| Career DB tables | Persistent and safe, but limited to formation, roles and team sheets |
| Runtime memory (Live Editor style) | Turbo's lane. Attribulator calls exist in Live Editor's Lua env. |
| Game-variable store | Already working in Turbo for injuries, weather, time of day, difficulty and CPU subs |

Tiering from this: L1 DB and Turbo-local, L2 proven game variables with reapply, L3 Attribulator and `TeamSheet` runtime (experimental).

## 9. Architecture and file layout

**Core** (pure C++, no ImGui, natively testable):

| File | Role |
|---|---|
| `core/sliders.{h,cpp}` | Registry of `SliderDef`, `SliderSet`, clamp and validate |
| `core/tactics.{h,cpp}` | Formation geometry, preview model (lines, width band, run arrows, heat grid), role presets |
| `core/pitch_model.{h,cpp}` | Optional split of the geometry from `tactics` |
| `core/tactic_profiles.{h,cpp}` | Profile store, copying `reapply.*` |
| `core/opposition.{h,cpp}` | Facts extraction, archetype scoring, seeded jitter, rules |
| `core/opp_rules.{h,cpp}` | Rule JSON in and out |
| `core/match_apply.{h,cpp}` | Fixture, opponent and profile resolution, then `gv` writes, then clear |
| `core/bodytype_catalog.{h,cpp}`, `bodytype_data.h` | Generated from the probe |

**UI:**
- `ui/ui_tactics.{h,cpp}`: `draw_tactics(App&)`.
- `ui/pitch_view.{h,cpp}`: reusable pitch widget.
- `ui_teams.cpp`: a new `BeginTabItem("Tactics")` in `##ttabs`.
- `app.h`: declare `draw_tactics`, and add an `App::tactic_profiles` member loaded in the constructor.

**Lua** (`turbo/package/lua/libs/v2/imports/turbo/`):
- `features/tactics.lua` (kind `action`, `needs_cm=false`): batched DB edits (chunked) and snapshots. Single-slider drags stay native through `App::edit`.
- `features/probe_tactics.lua`: Phase 0 schema, teamsheet and vector dumps.
- `features/bodytypes.lua`: the body-type probe.
- Edits to `features/squad_role.lua`, `features/team_mass.lua` and `core/game.lua` for the role fix.
- Script stubs in `lua/scripts/turbo_*.lua`, and registration in `turbo.lua` `M.MODULES`.

**Wiring:** add every new `.cpp` by hand to `turbogui/tests/native/run_native.sh` and `turbogui/scripts/build_win.sh` (neither uses a glob).

**Data files** (`turbo_output/`):
- `tactic_profiles.json`
- `opp_rules.json`
- `bodytypes_fc27.json` (probe output)

**Frame-loop rules.** An exception in `App::draw` disables the overlay for the session (VERIFIED, `overlay_dx12.cpp:612-646`).
- Wrap the tab body so a failure shows an inline error, never a throw.
- Cache the preview by `app.gen` and slider values.
- Recompute the heat grid only on change.
- Cap draw primitives (about 400).
- Do no file I/O or JSON parsing in the frame path.

**Write-path rules.**
- `Database::set` with `table_alive`.
- `has_room` before any insert.
- Ranges from `GetDBMeta`.
- Never save the career.

## 10. Phased roadmap

Size key: S under a day, M 1–3 days, L 3–7 days.

### Phase 0: read-only probes (ship first, no behaviour change)

| # | Task | Size |
|---|---|---|
| 0a | `probe_tactics.lua`: table and field scan for `*tactic*`, `*mentalit*`, `*instruction*`, `*formation*`, `*teamsheet*`. Dump one `cm_teamsheets` row and the PSM vector with ages. | S |
| 0b | `bodytypes.lua` probe | S |
| 0c | Re-extract the 991 game-variable names (`scripts/re/strings_grep.py`, `xrefs.py`) and grep for PASS, SHOT, FOUL, REF, SPEED, CARD, ERROR, TRAP, CONTROL | M |
| 0d | You run 0a–0c locally and return the dumps (external) | — |
| 0e | Bundled with 0d, in game: probe `GameplayAttribulatorSetVar` with one harmless value, run a write, save, reload, read differential test on formation and sheet tables, and check fixture and opponent detection | external |

The outcome of Phase 0 promotes or demotes rows in section 3.

### Phase 1: L1, Turbo-local and DB

| # | Task | Tests | Depends on |
|---|---|---|---|
| 1 | Role fix and diagnostics (section 2) | `world.lua` `sheet_gap` and 52-entry vector, `t04`, `t18` cases | 0d for the cause |
| 2 | Role re-apply on `SEASON_RESET` and 0x17, with its kill switch | Simulated 0x17 case | 1 |
| 3 | `describe()` plus routing through log, toasts and tooltips | Native "no `code N` leak" walk | none |
| 4 | Pickers (nation, rival team, manager fields), match-setup combos, collapsed Technical sections | UI cases | 3 |
| 5 | Body-type catalogue, generator and gallery UI | Catalogue lookup tests | 0d |
| 6 | Slider registry and profile store | Round-trip, malformed entries, set-aside, atomic save, unique keys, defaults in range | none |
| 7 | `core/tactics` geometry and preview model (fallback formation table if the schema lacks one) | Monotonicity, clamping, formation-isolation, deterministic heat grid | 6 |
| 8 | `ui_tactics` and the pitch widget (critical path) | UI case: select team, open Tactics, move slider, check DB value and toast. Tier and status registry check. No unit strings on D and M labels. | 6, 7 |
| 9 | Live match sliders over `gv::known_vars`, `tactics_off.txt`, overrides banner, auto-clear on exit | UI case: no write when `tactics_off.txt` exists | 8 |
| 10 | Opposition solver with preview card (no game application beyond Live keys) | Determinism by seed, tier monotonicity, `max_delta` clamp, rule priority, explain output | 6 |
| 11 | Docs, README, CHANGELOG, `check_fc27_schema.py` coverage mode, luacheck, ASan/UBSan | — | all |

Tasks 3, 6 and 7 can run in parallel with 1.

### Phase 2 (2.1)

- DB-bound tactic fields and formation writes, once the dump proves them and the differential test passes.
- Role and focus editing.
- Opposition rule editor.
- A tactics reapply store, only if the differential test shows the tables are reset on load.
- Per-fixture auto-apply, if opponent detection is proven.

### Phase 3 (2.2)

- Attribulator and team-sheet sinks, one slider at a time. Each gets its own kill switch (`call_<name>_off.txt`), range clamps and an experimental badge until an in-game A/B check passes.
- Mid-match adaptation.

### In-game verification checklist (your local deploy)

1. Launch with a career loaded. Confirm no crash and that Turbo does nothing before connect.
2. Run the Phase 0 probes and return the files in `turbo_output/`: the schema, the teamsheet and vector dump, `bodytypes_fc27.json`, and the game-variable name list.
3. Role fix: use a squad with players aged 30+ and one with a gap in the teamsheet. Apply roles. Check every squad member changed, and that the summary skip counts match what you see.
4. Advance to a season reset or sign a player. Confirm roles re-apply, or note that they revert.
5. Injury frequency and severity: set extreme values (frequency 100, then 0) for your side and the CPU side. Play or watch a match and confirm the effect. Also confirm injuries-off holds.
6. Weather, time of day, difficulty, CPU substitutions off: set each and confirm the match reflects it. Record the weather and time-of-day id-to-name mapping you observe.
7. Authentic Gameplay and Dynamic Opposition: confirm they stay selected and the overrides still take effect.
8. Formation write test (if the dump shows an editable table): edit, play a match, save, reload, read back. Note whether the game used or reverted the edit.
9. Input: with Turbo capturing the mouse, confirm the game receives no clicks, and the pitch drag works. The input-shield hooks are still "installed but not tried in game" per `HANDOVER.md`.
10. Kill switches: create `tactics_off.txt` and `role_reapply_off.txt` and confirm each stops its writes and greys the visuals. Delete the files and confirm normal behaviour returns.
11. Close the game and confirm overrides do not persist into the next session.
12. If you test any Attribulator slider, make one change at a time, A/B it, and report the result before the next.

## 11. Risks and kill switches

| Risk | Rating | Mitigation |
|---|---|---|
| Most sliders unwritable today | High | Status badges, Phase 0 promotion, and no promises in the UI or notes for NEEDS-RE rows |
| Users read the preview as engine truth | High | Tier and status labelling (section 3), Modelled layers off by default, no metres or outcome language |
| Role bug cause unconfirmed | Medium | The fix covers A and B, plus counters and the diagnostic |
| Game overwrites or reverts tactic and sheet edits | Medium | Differential test before any reapply store. Formation visuals labelled "in-game effect unverified". |
| Online contamination (overrides are process-wide) | Medium | Auto-clear on exit and a visible banner. The offline-detection method is UNCERTAIN, so no offline-guard promise. |
| Anti-cheat | UNCERTAIN | No new hooks in 2.0 |
| Save corruption | Medium for DB writes, low for game variables | `table_alive`, `has_room`, `GetDBMeta` ranges, no auto-save, confirmed destructive actions |
| Overlay frame exception disables the overlay | Medium | Exception-proof draw path, caching, bounded primitives |
| Launch safety | Low if rules hold | `t08_launch_safety` allows exactly one `package.loadlib`. Nothing new at boot and no DB read before connect. |
| Game-thread freeze on long Lua runs | Medium | Chunked batch writes, one command in flight |
| Maintenance (hand-edited source lists) | High | Registry-driven sliders instead of per-slider code |
| Source quality | Medium | FC 27 slider names and Authentic defaults are second-hand, so they ship as hints with a `game_build` tag |
| Body-type write mismatches skeleton or kit | UNCERTAIN | Not default-on until tested |

**Kill switches** (files in `turbo_output/`), all listed on the Status tab and in the README troubleshooting table:
- Existing: `reapply_off.txt`, `turbo_gui_disable.txt`, `call_<name>_off.txt`, `hook_<name>_off.txt`, and the crash flag `turbo_gui_start.flag`.
- New: `tactics_off.txt` (all tactics writes, and greys the effect layers), `role_reapply_off.txt`, and one per L3 call or hook.

## 12. Open items that truly need you

All of these are in-game verification or data only you can supply. There are no credentials or business decisions pending.

1. **Phase 0 dumps from your machine.** The schema (`turbo_output\fc27_db_schema.json`), the teamsheet and vector dump from a career where the bug occurs, and the body-type probe output. Without them, the tactic-table tier, the role-bug cause and the body-type catalogue stay open.
2. **In-game checks** (section 10): injury, weather, difficulty and CPU-substitution effects in a played match, Authentic and Dynamic Opposition compatibility, the formation write, save and reload differential, input shield behaviour, and the Attribulator A/B test.
3. **Anti-cheat behaviour for FC 27 offline** on your setup. Online use is out of scope. Do not enable any new hook until you have judged it.
4. **Weather and time-of-day id names**, as you observe them in game.