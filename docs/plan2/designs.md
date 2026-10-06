# Turbo 2.0 design: safest-first (reliability angle)

Status: I did not re-read the repo and nothing was executed. I rely on the reports, with their VERIFIED / ASSESSED / UNCERTAIN labels carried through. Design rule: nothing ships unless it can be tested offline in the existing harnesses (`turbo/tests/run_tests.sh`, `turbogui/tests/native/run_native.sh`). Every game-memory write has its own kill-switch file. Slider catalogue entries that map to nothing proven are marked `needs RE` and are inert, not guesses.

## 1. Phasing

**Phase 0: discovery probes (read-only, ship first, no behaviour change)**
- `features/probe_tactics.lua` dumps to `turbo_output/`:
  - names and fields of every table matching `*tactic*`, `*mentalit*`, `*instruction*`, `*formation*`, `*teamsheet*`;
  - `bodytypes_fc27.json` (per distinct `bodytypecode`: count, height/weight min/max, `headclasscode`, 5 example names);
  - a `cm_teamsheets` row plus the PSM vector with player ages (settles role-bug cause A vs B).
- Extend `scripts/check_fc27_schema.py` with a coverage mode listing schema fields absent from the UI.
- RE tasks, offline analysis only: re-extract the 991 game-variable names and grep them for PASS, SHOT, FOUL, REF, SPEED, CARD, ERROR, TRAP, CONTROL. Probe `GameplayAttribulatorSetVar` and `GetVarType`.
- Output of phase 0 decides which `needs RE` rows get promoted. Until then the UI shows them greyed with "unavailable: not verified for this build".

**Phase L1: DB and Turbo-local only (no game memory beyond existing paths)**
1. Role bug fix (section 2).
2. Description cleanup (section 5).
3. Body-type catalogue (section 5).
4. Tactics editor shell: mini pitch, slider model, profile store (section 3). Computed preview is local.
5. Writes only to DB fields that the schema dump proves exist, via `App::edit`/`Database::set`.

**Phase L2: reapply and proven game variables**
- Match tuning from `gv::known_vars()` (injury frequency/severity per side, weather, time of day, difficulty, CPU subs).
- Reapply of squad roles after `SEASON_RESET` and event 0x17, plus a `tactics_reapply` store, only if the differential test (write, save, reload, read) shows tactic tables are reloaded.
- Opposition engine runs at fixture detection. It can only output values from L1 DB fields or L2 game variables.

**Phase L3: runtime attribulator / TeamSheet calls (experimental, off by default)**
- Speed, error rates, referee, per-team AI parameters. Each slider needs its own kill switch, a range clamp, and an in-game A/B pass before it leaves "experimental".
- Also gated by the anti-cheat question (mods-research: Javelin in FC 27 offline is unverified). Ship nothing here until phase 0 plus an in-game test of one harmless slider.

## 2. Role bug fix (L1 and L2)

Cause is unconfirmed. The fix addresses A (membership) and B (game rewrites roles) together, plus the report's smaller suspects.
- `squad_role.lua`: add `psm_members(layout)` returning all vector entries with pid > 0. Memory-pass target is `psm_members ∩ teamplayerlinks(user team)`. `cm_teamsheets` stays only as the `locate` anchor.
- `game.lua user_squad`: replace `break` on `-1` with skip, scan all `playerid0..51`, and fall back to `teamplayerlinks` when the sheet yields fewer. Log the difference.
- `locate()`: valid role bytes are 0..5 or 0xFF. Accept the vector at 90% valid or better, and log the offending pids.
- Summary adds counts: no vector entry, nil birthdate, loaned-in skipped, vector pids outside the squad.
- Role rule editor (`team_mass.lua`): ordered rules by age band, OVR rank, position group, plus per-player pins. Defaults reproduce today's rule (19+ Rotation, younger Prospect). A preview lists player, age, OVR, old role, new role, skip reason. This is the "over-30" control the user actually needs.
- Reapply: hook `SEASON_RESET` and event 0x17 and re-run the saved rule. Kill switch `turbo_output/role_reapply_off.txt`. The existing memory write is unchanged, so no new RE.
- Tests (extend `world.lua` with `sheet_gap`, a 52-entry vector with empties and mixed bytes):
  - sheet gap at slot 8, with every `teamplayerlinks` player getting a role;
  - ages 17–38, a 34-year-old on the last slot, a missing birthdate counted in the summary;
  - stale `playerloans` row counted as skipped;
  - vector cleared and refilled by a simulated 0x17, then rule re-applied;
  - role byte 9 still locates the vector, with a warning.

## 3. Tactics editor architecture

Teams sub-tab "Tactics" (inside `##ttabs`) for per-team and per-position work. A "Game settings" section for both-team values lives in the same panel when no team is selected, avoiding a change to the 7-tab numbering. Heavy logic stays out of ImGui so tests can reach it.

New files, each added to both source lists (`run_native.sh`, `build_win.sh`):
- `core/tactics.{h,cpp}`: pure model. Slider registry, formation geometry, role presets, preview computation (lines, width band, run arrows, press density grid). No ImGui, no game memory except via `Database`.
- `core/tactic_profiles.{h,cpp}`: profile store, copying `reapply.*` (atomic `.tmp` rename, malformed entries dropped and counted, unreadable file set aside).
- `core/opposition.{h,cpp}`: opposition engine (section 4).
- `ui/ui_tactics.{h,cpp}`: `draw_tactics(App&)`, `draw_pitch(...)`. Pitch uses an `InvisibleButton` (so the input shield and `WantCaptureMouse` work), then `AddRectFilled/AddLine/AddCircle/AddPolyline` and a 24x16 heat grid.
- `features/tactics.lua` (kind `action`) for batched or all-team DB edits, with `lua/scripts/turbo_tactics.lua`, a `turbo.lua` registration, `tNN_tactics.lua`.
- `test_tactics.h` included by `test_main.cpp`.

Frame-loop rules (V risk: an exception disables the overlay for the session):
- Preview is recomputed only when a slider value or `app.gen` changes, and cached.
- No file I/O or JSON in the frame path.
- A bounded primitive count.
- All code is wrapped so a failure shows an inline error, not a throw.

## 4. Data model

Profile store `turbo_output/tactic_profiles.json`:
```
{"turbo_tactic_profiles":1,"build":"fc27","profiles":[
 {"category":"match|team|position|opposition","name":"...","scope_key":"role:CDM-Holding"?,
  "values":{"line_height":55,...},"modified":"..."}],
 "active":{"match":"Authentic+","team":null,...}}
```
- Values are keyed by slider id, so unknown ids are kept and ignored (forward compatible). Sliders use 0–100 internally and are quantised on write.
- `build` tag allows later migration. Authentic baseline defaults are community-sourced and labelled so.
- Active-profile selection goes in `gui_settings.json`. Profile data is user data and goes in its own file.
- Load and save only at construction and on user action.

Opposition engine: inputs are `strength_ratio` (opponent overall vs user), squad traits (pace, passing, height from `players` aggregates), reputation/form, opponent's own formation or style, and a per-match seed. Output is an archetype (Low block counter, Possession, Gegenpress, Wing play, Direct) plus jittered slider offsets. It is deterministic given a seed, so unit-testable. The seed rolls per fixture to maximise variety. Output is applied only through paths proven to work for the opposing side. Where the game provides `_USER` and `_CPUAI` names (injuries) that is per-side. Everything else is DB-only or `needs RE`. Dynamic Opposition and Authentic Gameplay stay selected in game, and Turbo writes nothing to the EA slider menu.

## 5. Cleanup and body types (L1)

- Add `describe(table, field, value)` in `core/field_labels.h`. Route `App::edit` logging, undo toasts, undo and mass-edit summaries and tooltips (`widgets.cpp:76,361,173,290`) through it. Raw field name and code go to a dim second line.
- Fix the audit table items 1–34: nationality, `rivalteam`, manager `teamid`/`nationality` become name pickers; team ID and club suffixes move to tooltips; head asset text becomes "Real face / Generic head"; match-setup rows get combos for weather, time of day, difficulty; bridge messages become plain wording ("update Turbo"). Keep IDs in destructive confirms (item 11) and bug-report codes (item 29). Collapse Tools/Status technical info under "Technical details" with a one-line health summary. Extend `label_map()` for the roughly 40 unmapped appearance fields.
- Body types: `bodytype_catalog.{h,cpp}` plus a generated `bodytype_data.h` from the probe JSON, via a script modelled on `scripts/gen_hair_styles.py`. UI shows "Generic" and "Player-specific" groups, "used by N players", and "Copy body type from player…". Specific models are labelled with example names from the probe, never invented. The first release exposes browse and copy, with a note that mixing a specific body onto a generic head is untested in game (UNCERTAIN), so it is not default-on.
- Tests: native tests for `describe` fallbacks, catalogue lookups, and that no tooltip in a UI walk contains `code N` outside the allow-list.

## 6. Slider catalogue

Legend: write path DB = schema-proven field once phase 0 confirms; GV = existing game variable (proven code, in-game check pending for some); RE = needs RE, inert until promoted; PRV = preview-only in Turbo (affects the pitch and exported profile, not the game). Ranges 0–100 unless noted; defaults are Authentic-seeded where sourced, else 50.

**General (both teams)**
| Slider | Range / default | Maps to |
|---|---|---|
| Injury frequency | 0–100, game default | GV `INJURY_FREQUENCY_USER` and `_CPUAI` set equal |
| Injury severity | 0–100 | GV `INJURY_SEVERITY_*` |
| Injuries off | toggle | GV `NEVER_INJURE` |
| Weather, time of day, difficulty, CPU subs off | enums | GV (existing) |
| Player speed (sprint, acceleration) | 0–100, 36/48 | RE (attribulator candidate) |
| Pass error, pass speed | 0–100, 54 | RE |
| Shot error, shot speed, shot frequency | 0–100, 55 | RE |
| First touch / trapping error | 0–100 | RE |
| Foul frequency | 0–100 | RE (no variable found) |
| Card strictness | 0–100 | RE (`gp_rules_refereestrictness`) |

**Team (own side, and applied per opponent by the engine)**
| Slider | Maps to |
|---|---|
| Mentality (7 steps) | DB `cm_mentalities` field (unknown) else RE |
| Defensive line height / line depth | DB if schema has it, else RE; PRV always |
| Defensive width, attack width | same |
| Line length / compactness | same |
| Press intensity, press trigger, engagement height | same |
| Tempo / build-up speed | same |
| Pass directness | same |
| Forward-run frequency, players in box | same |
| Counter-press, counter-attack | same |
| Marking tightness, offside trap | same |
| Cross frequency, shot patience | same |
| Formation (positions) | DB `formations` (name and field list from dump) |

**Position / role** (seeded from role and duty presets, FC role+focus names where the dump exposes them, otherwise Turbo's own role list labelled "Turbo preset")
| Slider | Maps to |
|---|---|
| Forward runs, depth bias, roam vs hold, width bias | DB role fields if present, else PRV and RE |
| Close down, tackle aggression, mark tight | RE |
| Cross depth, shoot freq, dribble freq, risk passing | RE |
| Role and focus | `players.role1..9`, existing editor (L1, writable now) |

**Opposition (engine outputs, scope CPU)**
| Slider | Meaning |
|---|---|
| Style variety | 0–100, how far archetype jitter may stray |
| Quality-response | how strongly strength ratio shifts line height and pressing |
| Archetype bias | weights per style family |
| Adaptation | game-state modifier when trailing or leading (needs mid-match hook, RE) |
| Aggression, build-up speed, shot-type frequency, crossing, dribble and skill-move frequency | RE (FC 27 CPU slider names are single-source) |

Honest summary: at L1 only the injury and match-environment rows, formation, roles and body types are real writes. The tactic sliders are real writes only if the schema dump shows matching fields. Everything else is preview-plus-profile until RE lands.

## 7. Kill switches and safety

- New: `turbo_output/tactics_off.txt` (all tactics writes), `role_reapply_off.txt`, and one `call_<name>_off.txt` or `hook_<name>_off.txt` per L3 call, all listed on the Status tab and in the README troubleshooting table.
- Existing `reapply_off.txt`, `turbo_gui_disable.txt` unchanged.
- Nothing at launch: `t08_launch_safety` must still pass, and no boot-time DB reads. Tactics reapply runs only after `refresh()`.
- `table_alive` before every record write, ranges from `GetDBMeta` not hand-typed, `has_room` before inserts.
- Offline guard: all L2 and L3 writes blocked when an online mode is detected (detection method UNCERTAIN, needs RE). Auto-clear overrides on exit, and a visible "overrides active" indicator.
- Batch opposition-wide Lua writes in chunks, since a long run freezes the game thread.

## 8. Tests

- Lua: extend `world.lua` (formation table, `sheet_gap`, 52-entry vector); `t04`, `t18` regressions above; `tNN_tactics.lua` for validation, range clamps, batching, absent-field drop; `t08` unchanged.
- Native: formation geometry; preview numbers (line positions, width band, heat grid totals); slider registry (ranges, defaults, quantise); profile store round-trip, malformed entries, set-aside file, atomic save; opposition engine determinism and variety (different seeds yield different archetypes, strong vs weak opponents shift in the right direction); a UI case (select team, open Tactics, drag slider, assert DB value and toast, assert no write when `tactics_off.txt` exists); description-cleanup walk.
- Tooling: `luacheck`, extended `check_fc27_schema.py`, ASan/UBSan run, Windows smoke/overlay tests only if a native call or hook is added (L3).

## 9. Risks

1. Schema may have no editable tactic table. Then the editor is preview-plus-profile plus roles and formation only, until RE promotes rows. This is the largest risk to the user's expectations, so state it in the UI.
2. `TeamSheet` runtime service overwrites `cm_teamsheets` DB edits at save (ASSESSED). Formation writes may need `SetSheet` or a reload, so formation write is a phase 0 differential test, not an assumption.
3. Game variables are process-global and persist until the game closes. Opposition-specific values must come from per-side variables or DB fields.
4. Attribulator calls are unproven (read timing, per-side scope, valid ranges, anti-cheat tolerance). L3 is experimental only.
5. Preview is Turbo's model, not engine truth. Label it so, to avoid users trusting heatmaps.
6. FC 27 facts (25 new sliders, CPU Opponent/Teammate split) are second-hand summaries of Pitch Notes. Verify before building to them.
7. The role bug cause is unconfirmed until the phase 0 dump. The fix covers both leading hypotheses and adds counters, so the next occurrence is diagnosable.

## 10. Order of delivery

Phase 0 probes, then role fix with tests, then description cleanup and body-type catalogue, then profile store and slider registry, then pitch and preview, then L2 match tuning and opposition engine over proven variables, then reapply after the differential test, then L3 experiments one slider at a time.

Key files: `turbo/package/lua/libs/v2/imports/turbo/features/squad_role.lua`, `.../features/team_mass.lua`, `.../core/game.lua`, `turbo/tests/world.lua`, `turbogui/src/ui/ui_teams.cpp`, `turbogui/src/ui/app.cpp`, `turbogui/src/core/field_labels.h`, `turbogui/src/core/match_setup.cpp`, `turbogui/src/core/reapply.*`, `docs/re/match_setup.md`, `docs/re/player_status_roles.md`.

---

# Turbo 2.0 design (UX-first)

Labels: V = verified in repo or reports, A = assessed, RE = needs reverse engineering. I read only the discovery reports and did not re-read the repo. Nothing here is validated in game.

## 0. Constraints that shape the design

- The only proven gameplay write path is the game-variable store (`gv::known_vars`). It covers injury frequency and severity per side (USER and CPUAI), weather, time of day, difficulty, CPU subs and NEVER_INJURE.
- Speed, pass/shot/trap error, foul frequency, card strictness and the AI tactic sliders have no known write site. The leads are the Gameplay Attribulator (`GameplayAttribulatorSetVar`, `gp_rules_refereestrictness`) and the 1568-site game-variable name list, which was never saved.
- Tactic and formation DB tables are unknown, because `fc27_db_schema.json` is absent. Team sheets are a runtime master copy, loaded from `cm_teamsheets` and `cm_mentalities` at load and overwritten at save.
- So the UI ships in full from day 1. Every slider carries a write-state badge:
  - **Live** (proven write).
  - **DB** (table write, game read-back unproven).
  - **Preview** (saved in the profile and drawn on the pitch only).
  - **RE** (blocked on research).
- Preview sliders store values and render. They apply automatically once an adapter is proven. The badge and tooltip say so honestly.

## 1. Architecture

**New core files** (pure logic, no ImGui):
- `core/tactics.{h,cpp}`: formation geometry, slider registry, visual model, opposition solver.
- `core/tactic_profiles.{h,cpp}`: category store, copying the `reapply` pattern (atomic `.tmp` rename, `*.unreadable.json` set-aside, malformed-entry counting).
- `core/slider_apply.{h,cpp}`: the adapter layer. Each slider maps to one `Sink`: `GameVar`, `DbField`, `Attribulator` or `None`.

**New UI files:**
- `ui/ui_tactics.{h,cpp}`: `draw_tactics(App&)`.
- `ui/pitch_view.{h,cpp}`: reusable mini-pitch widget.

**Lua:**
- `features/tactics.lua`, registered in `M.MODULES` as `tactics` (kind `action`, `needs_cm=false`), plus `scripts/turbo_tactics.lua`.
- It does bulk DB writes in chunks, the schema probe and the opposition batch.
- Single-row slider drags stay native through `App::edit`.

**Wiring:**
- `app.h`: declare `draw_tactics` and add an `App::tactic_profiles` member, loaded in the constructor.
- Add the new files to the two source lists, `run_native.sh` and `build_win.sh`.
- Update `docs/turbo-reference.md`, `CHANGELOG.md` and `TURBO_README.md`.

**Placement:**
- Add a "Tactics" sub-tab in `##ttabs` (V), since "active tactic" is per team.
- Add a top-level "Tuning" tab for general and opposition settings. These are global and need no selected team.
- The Tactics sub-tab hosts the Team and Position scopes. Its header has a link into Tuning.
- This changes `names[]`, `request_tab` and `test_main.cpp:3341` (`tab < 7` becomes 8).

**Safety:**
- Nothing runs at launch, and `maybe_reapply` stays post-connect.
- Kill switches: `tactics_off.txt`, plus a per-sink file such as `call_attribulator_off.txt`.
- An offline guard blocks all runtime writes when an online session is detected. This addresses the Javelin/EAAC concern, which is unverified.
- Overrides auto-clear on exit. A persistent "overrides active" banner shows on Status and in the tab header.

## 2. Screen layout (Tactics sub-tab)

**Three columns:**
1. **Left (about 22%):** Scope selector, preset browser.
2. **Center:** mini pitch and the layer toggles.
3. **Right (about 30%):** slider panel for the current selection, with a diff footer.

**Scope selector.** Four pill tabs: `Match (both teams)`, `My team`, `Position/Role`, `Opposition`. This mirrors FC 27's own Opponent/Teammate split. The scope changes the right panel and which overlays are drawn.

**Preset browser (left):**
- Category chips: `Match`, `Team`, `Position`, `Opposition`, `Full bundle`.
- Searchable list with tags (for example "Authentic+", "Counter") and a per-category "active" star.
- Actions: Save as (name, category, tags, note), Load, Overwrite, Duplicate, Delete (confirm), Export and Import `.json`, Compare.
- Active profile name per category is kept in `gui_settings.json`.
- Every preset carries a `schema_version` and `game_build` tag so later patches can migrate or warn.

**Right panel behaviour:**
- Sliders are grouped in collapsible cards: In possession, Transition, Out of possession, and for Match, Physics, Errors, Frequencies, Referee, Injuries.
- Each slider row has the label, a slider with a typed-value box, a reset-to-default dot, a badge (Live, DB, Preview or RE), a pin (copy value to the other scope) and a lock.
- Hover shows a one-line plain description first and technical names on a dim second line. This follows the codes-to-descriptions rule.
- Moved values show an orange diff vs the base, and the footer shows "N changed, Apply, Revert, Save as preset".
- Slider drags redraw the pitch immediately, but the heatmap is recomputed on value change only.
- Apply is explicit for Live sinks. It writes through `gv::apply` and shows a "will apply at kick-off" toast.
- A "Dry run" toggle shows what would be written.

## 3. Mini pitch

**Widget:**
- A reserved `InvisibleButton` region with a fixed 105:68 aspect, vertical or horizontal (toggle).
- Drawn with ImDrawList only: pitch markings, 11 numbered dots coloured by position group, an optional opponent ghost set.
- The formation comes from the active tactic (the DB `formations` row, or a built-in fallback table if the schema probe fails). A dropdown switches the preview formation without writing.
- Dragging a dot edits its base offset. This writes to the profile and, for DB-backed sinks, to the `formations` row.

**Layers** (checkbox row, each with a legend chip):
- Defensive line.
- Press line (line of engagement).
- Width envelope.
- Pressure heatmap.
- Run arrows.
- Compactness, as a convex hull.
- Roaming radius.
- Ball-side shift.

**Phase toggle** (like FM): With ball / Without ball / Overall. Dots and overlays are recomputed per phase.

**Compare mode:** a split or ghost overlay of "before" (loaded profile or base) vs "now". Moved dots show a thin displacement line, and a numeric strip shows deltas such as "line +6 m".

**Risk gauge:** a small bar showing "coherence" on three axes (Risk, Stamina cost, Defensive exposure). Incoherent combinations give a warning chip, for example "High line + low pressure: exposed to balls in behind". The message follows FM's "coherent decisions" idea.

**Opposition view:** an extra mirrored mini pitch. It shows the opponent's formation and the lines the solver chose, and the My-team lines can be overlaid to show where the two sides' lines meet.

## 4. Visual model (slider to drawing)

All coordinates are normalised with x = 0 at own goal, x = 1 at the opponent's goal, y = 0..1 across the pitch. Pure functions in `core/tactics.cpp` are covered by native tests.

- **line_depth (0..100):** the defensive line x = 0.18 + 0.30·d (about 19 to 48 m). Drawn as a dashed horizontal line with the metre value. Defenders' base x is blended 70% toward it, midfield 40%, attackers 15%.
- **line_length / compactness (0..100):** the gap between the defensive line and the forward line, 0.55 at 0 down to 0.22 at 100. The midfield line sits between them, and the hull shades red when the gap exceeds 0.45.
- **line_width (0..100):** the half-width of the band is 0.28 + 0.22·w. Drawn as two vertical edges or a translucent band. Wide players' y is scaled toward the touchline.
- **press_intensity (0..100):** the heat radius per player is 0.04 + 0.10·p, with alpha scaled by p.
- **press_trigger (0..100):** the press line x = defensive line + (0.20 + 0.35·t). Heat is concentrated between the press line and the ball zone. The zone is drawn as a gradient band and a tick marker.
- **forward_runs / run_frequency (0..100):** arrow count and length per attacking player. Length is 0.04 + 0.16·r and arrows are drawn from the base dot. Opacity follows frequency.
- **cross_depth:** on wide players, an arrow that curves inward from the byline (high value) or from deeper (low value).
- **roam:** a dashed circle around the dot, radius 0.02 + 0.10·roam. The dot jitters inside it in an optional looping animation, where speed scales with tempo.
- **tempo:** animation speed of the idle dot movement.
- **ball_side_shift:** when the ball marker is dragged, the whole block shifts. The shift is 0.25·compactness·(ball.y − 0.5), and the heatmap follows the ball.
- **Heatmap:** a 24×16 grid, density = sum over players of a Gaussian (σ = press radius), weighted by phase (press heat Without ball, pass lanes With ball). Colour ramp is sequential and colour-blind safe, built following the `dataviz` skill's palette guidance. It is recomputed only on value change.

The preview is labelled "Turbo estimate, not engine output". It is meant to be intuitive and monotonic, not a simulation.

## 5. Slider catalogue

All sliders are integers 0..100 unless noted. "Def." is the default, seeded from the FC 26 community "Authentic" values where those exist (community-sourced, so a seed and not authoritative). Sink codes: GV = game variable, DB = DB field, ATT = Attribulator, PV = preview only until a sink is proven.

### A. Match (general, both teams)

| Slider | Range | Def. | Sink / status |
|---|---|---|---|
| Player sprint speed | 0-100 | 36 (user/cpu) | ATT, RE |
| Acceleration | 0-100 | 48 | ATT, RE |
| Pass error | 0-100 | 54 | ATT, RE |
| Pass speed | 0-100 | 50 | ATT, RE |
| Header pass error | 0-100 | 50 | ATT, RE |
| Shot error | 0-100 | 55 | ATT, RE |
| Shot speed | 0-100 | 50 | ATT, RE |
| Header shot error | 0-100 | 50 | ATT, RE |
| Shooting frequency | 0-100 | 50 | ATT/GV, RE |
| First touch / trapping error | 0-100 | 50 | ATT, RE |
| Ball control error | 0-100 | 50 | ATT, RE |
| Goalkeeper ability | 0-100 | 55 | ATT, RE |
| Referee foul frequency | 0-100 | 50 | RE (no variable found) |
| Referee card strictness | 0-100 | 50 | ATT `gp_rules_refereestrictness`, RE |
| Injury frequency | 0-100 | 90 (Authentic reference) | GV `INJURY_FREQUENCY_USER/CPUAI`, Live |
| Injury severity | 0-100 | 35 | GV, Live |
| Injuries off | on/off | off | GV `NEVER_INJURE`, Live |
| Weather / time of day / difficulty | enum | game decides | GV `OVERRIDE/*`, Live, with named combos |
| CPU substitutions off | on/off | off | GV, Live |

Match sliders default to "Game decides" (unset) so that Authentic stays untouched until the user opts in. Injury sliders write USER and CPUAI together and have an optional "split sides" toggle.

### B. Team (My team, and the Opposition baseline)

Defaults here are neutral 50 values, seeded from the active team's preset.

| Group | Slider | Def. | Sink |
|---|---|---|---|
| In possession | Build-up speed / tempo | 50 | PV, then DB/ATT |
| | Passing directness | 50 | PV |
| | Attacking width | 50 | DB (team width) if probe finds it, else PV |
| | Forward run frequency | 50 | PV |
| | Cross frequency | 50 | PV |
| | Shot patience | 50 | PV |
| | Dribble / skill frequency | 50 | PV |
| | Players in box (1-10) | 5 | DB if found |
| | Fluidity | 50 | PV |
| Transition | Counter-press | 50 | PV |
| | Counter attack | 50 | PV |
| | Regroup depth | 50 | PV |
| Out of possession | Line depth | 50 | DB (team depth) if found |
| | Line width (defensive) | 50 | DB if found |
| | Line length / compactness | 50 | PV |
| | Defence positioning (marking tightness) | 50 | PV |
| | Pressure intensity | 50 | PV |
| | Press trigger | 50 | PV |
| | Tackle aggression | 50 | PV |
| | Offside trap | off | PV |
| Set pieces | Corners and free kicks committed forward (1-10) | 3 | DB if found |
| Discrete | Build-up play (Balanced / Fast / Slow / Long Ball); Chance creation; Defensive style (Balanced / Drop Back / Press After Loss / Constant Pressure) | per team | DB/runtime sheet, RE |

### C. Position / role

Applies to the selected pitch dot. A role dropdown seeds the values from duty (Defend / Support / Attack), as in FM. The role list uses FC 25/26 roles with their focus options (the full per-position table is still to be fetched; role names are provisional).

| Slider | Def. | Sink |
|---|---|---|
| Forward runs (stay back to get forward) | duty-seeded | PV; role/focus fields DB (existing `role1..9`) |
| Hold position / Roam | 50 | PV |
| Width bias | 50 | PV |
| Depth preference | 50 | PV |
| Close-down (press this player) | 50 | PV |
| Tackle aggression | 50 | PV |
| Shooting frequency | 50 | PV |
| Dribble frequency | 50 | PV |
| Risk passing | 50 | PV |
| Cross depth (wide players) | 50 | PV |
| Marking: zonal/man (CBs, DMs) | zonal | PV |
| Base offset x, y (drag) | from formation | DB `formations` offsets if present |

Role and focus selection writes to `players.role1..9` (V, an existing path). Everything else is Preview until the sheet or Attribulator sink is proven.

### D. Opposition (variety engine)

Inputs per opponent: formation, role mix, team overall vs mine (strength ratio), a pace/pass/aerial profile from squad averages, and form.

Sliders:

| Slider | Def. |
|---|---|
| Variety strength (0 = off, 100 = maximum diversity) | 40 |
| Quality scaling (how strongly opponent overall changes aggression, error and speed) | 50 |
| Tactical mirroring (how closely settings follow the opponent's own tactic) | 70 |
| Underdog behaviour (low block / counter bias when weaker) | 50 |
| Favourite behaviour (high line / press bias when stronger) | 50 |
| Per-match random jitter | 15 |
| Style family weights (Possession / Counter / Wing play / Direct / Gegenpress / Low block) | equal |
| Game-state reaction (line/tempo change when leading or trailing) | 30 |
| CPU error offset (pass/shot/trap error by tier: Elite, Strong, Mid, Weak) | 0 per tier |
| CPU aggression offset (tackle/foul bias by tier) | 0 per tier |
| Anti-exploit (narrow when the user overloads the wings) | off, PV/RE |
| Seed: lock or re-roll | re-roll |

**Solver, a pure function with unit tests:**
1. Compute the strength ratio and the style family from the opponent's formation, roles and squad profile.
2. Pick the family by weighted score, with the seeded jitter.
3. Output Team and Match offsets for the CPU side only. Output is stored as a preview ("Opposition card": style, key sliders and the mirrored mini pitch).
4. Per-side application works only where the game provides USER/CPUAI variables (injuries today). Everything else is Preview or ATT-per-side, and RE determines whether per-team values exist at all.

**Fallback:** if per-side application is impossible, the solver still drives the DB tactic fields of the opponent team (formation and roles in `formations`/`players`) where proven. Otherwise it stays a preview, flagged clearly.

## 6. Data model (`tactic_profiles.json`)

```
{ "turbo_tactic_profiles": 1, "game_build": "...",
  "profiles": [ { "id", "category": "match|team|position|opposition|bundle",
                  "name", "tags": [], "note", "created", "schema_version",
                  "values": { "<slider_key>": number|string },
                  "scope_ref": {"teamid"?, "slot"?, "role"?} } ],
  "active": { "match": "id", ... } }
```

- Slider keys are stable strings from the registry. Unknown keys on load are kept and ignored.
- A registry entry holds key, label, description, scope, group, min, max, def, sink and status.
- The profile store is separate from the `reapply` store, and `reapply.*` is left untouched.
- If the differential test (write, save, reload, read) shows tactic tables are reset on load, add a sibling `tactic_reapply` store keyed by `reapply_key()`.

## 7. Bundled fixes and cleanup (2.0 scope)

**Squad roles (over-30 bug).** Root cause is unconfirmed, with two leading hypotheses (A and B in the role-bug report). Fix order:
1. Take membership from the PSM vector intersected with `teamplayerlinks`, not from the `cm_teamsheets` `break`-on-gap walk.
2. Report skipped counts in the summary: no vector entry, no birthdate, loaned-in filter, outside squad.
3. Re-apply on `SEASON_RESET` and event 0x17.
4. Make `locate()` tolerant (valid role bytes 0..5 or 0xFF, at least 90%).
5. Add the role rule editor (age band, OVR rank, position, per-player pin, preview table) in Teams > Mass actions.
6. Ship a read-only diagnostic first: dump a real `cm_teamsheets` row and the PSM vector, with ages.

**Codes to descriptions.** Implement one `describe(table, field, value)` in `field_labels.h` and route `App::edit` logging, the undo toast, tooltips (`widgets.cpp:76,361`), match-setup statuses (`ui_match.cpp:255,261`) and the pending-label toasts through it.
- Team, nation and city pickers replace raw ids (rivalteam, nationality, manager fields).
- Add combos for weather, time of day and difficulty.
- Move technical details such as hook addresses and counters to collapsed "Technical" sections.
- Keep the destructive-confirm IDs and the release-code number for bug reports.

**Player-specific body types:**
1. Add a Lua probe `bodytypes.lua` that writes `turbo_output/bodytypes_fc27.json` (per code: count, height and weight range, headclass, example names).
2. Load `bodytype_N` names from the Live Editor localize file at runtime.
3. Generate a `bodytype_catalog` like `hair_catalog`.
4. The UI becomes a gallery with "Generic" and "Player-specific" groups, "used by N players" and a "Copy body from player" button.
5. Field labels for the roughly 40 unmapped appearance fields.
6. Whether a specific body model on a generic head is safe is unverified, so the UI warns until tested.

## 8. Phasing

- **P0 (research and diagnostics, no behaviour change):**
  - Schema dump and a `*tactic*`/`*formation*`/`*mentalit*` table scan.
  - Over-30 diagnostic.
  - Bodytype probe.
  - Re-extract the game-variable name list and grep it for PASS, SHOT, FOUL, REF, SPEED, CARD, ERROR, TRAP, CONTROL.
  - Attribulator probe (one harmless slider: global shot error).
  - Differential reload test for tactic tables.
- **P1 (ship first):**
  - Role fix and rule editor.
  - `describe()` cleanup.
  - Body type gallery.
  - Tactics editor with the mini pitch, the full registry and the profile browser.
  - Live sinks only for injuries, weather, difficulty and subs.
  - Everything else is badged Preview.
- **P2:** DB sinks for formation, roles and any proven team fields. Opposition solver with a preview card. Per-fixture application using game variables.
- **P3:** Attribulator and team-sheet sinks, enabled one slider at a time after in-game A/B. Per-slider kill switch and clamps, offline guard, experimental badge.

## 9. Tests

**Native (`test_tactics.h`):**
- Formation geometry and every slider-to-visual function, including monotonicity and clamping.
- Heatmap grid sums.
- Profile store: round-trip, malformed entries, set-aside, atomic save.
- Opposition solver: determinism by seed, tier monotonicity, mirroring.
- UI case: select a team, open Tactics, drag a slider and check the preview, the diff footer and the `gv::apply` call.
- Kill-switch and offline-guard behaviour.

**Lua:**
- `tNN_tactics.lua` with a tactics table added to `world.lua`.
- Role regressions: sheet gap, 52-entry vector, age spread 17 to 38, missing birthdate, stale loan row, re-apply after event 0x17, tolerant `locate`.
- `t08_launch_safety` must still pass.

**Also:** `check_fc27_schema.py` extended for new fields, luacheck, ASan/UBSan, and the Windows smoke and overlay tests.

## 10. Risks

- **Unwritable core sliders.** Most requested sliders have no proven sink. Mitigation: honest badges, the P0 research and an experimental gate. The risk is that some stay Preview permanently.
- **Frame cost.** Cache by `app.gen`, recompute the heatmap only on change and cap primitives.
- **Revert on load or save.** Team sheets are overwritten at save. Mitigation: the differential test and a reapply store if needed.
- **Anti-cheat and online contamination.** Unverified for FC 27. Use the offline guard, auto-clear and a visible banner.
- **Gameplay breakage.** Clamp to tested ranges and ship bundled presets only after A/B.
- **Overlay input.** Use `InvisibleButton` for ImGui capture. No custom Win32 input.
- **Source confidence.** FM and EA research was snippet-only, and FC 27 Pitch Notes content is second-hand. Verify the role list, slider names and Authentic values before finalising defaults.
- **Version drift.** Keep the signature workflow and `game_build` tags on presets.

## 11. Key files

- `/home/user/FC27-Editor-Turbo/turbogui/src/ui/ui_teams.cpp`
- `/home/user/FC27-Editor-Turbo/turbogui/src/ui/app.cpp`
- `/home/user/FC27-Editor-Turbo/turbogui/src/core/match_setup.cpp`
- `/home/user/FC27-Editor-Turbo/turbogui/src/core/field_labels.h`
- `/home/user/FC27-Editor-Turbo/turbo/package/lua/libs/v2/imports/turbo/features/squad_role.lua`
- `/home/user/FC27-Editor-Turbo/turbo/package/lua/libs/v2/imports/turbo/features/team_mass.lua`
- `/home/user/FC27-Editor-Turbo/docs/re/match_setup.md`
- `/home/user/FC27-Editor-Turbo/docs/re/player_status_roles.md`

---

# Turbo 2.0 Opposition Variety and Preset Storage Design

I did not re-read the repo; this relies on the discovery reports. Labels: V = verified, A = assessed, U = unknown / needs RE.

## 0. Core constraint

The only proven runtime write path for gameplay is `gv::apply` on the GameVars store (`match_setup.cpp`). It covers injury frequency and severity, with separate USER and CPUAI names. Everything else is U:
- Attribulator pace and error values
- Referee foul rate and strictness
- Line, width and press values
- The tactic tables and fields

Opposition variety is therefore built as a pure-Turbo engine that outputs an abstract `SliderSet`. Each slider carries a binding: `gv` (writable now), `db` (writable once the schema dump confirms the field), `attrib` (needs RE), or `preview` (editor visuals only). Nothing breaks if a binding is dead; the UI shows an "inactive: needs RE" badge. Authentic Gameplay and Dynamic Opposition are never touched. Turbo writes no EA slider-menu state, and the opposition engine is only an overlay computed from the existing AI team's data.

## 1. Modules and files

Core is pure C++ with no ImGui and no game memory, so it is testable natively:

| File | Role |
|---|---|
| `core/sliders.{h,cpp}` | Slider catalogue, an array of `SliderDef{key,scope,label,desc,min,max,def,step,binding,status}`, plus `SliderSet` (map key to float) and clamp and validate |
| `core/opp_profile.{h,cpp}` | `TeamFacts` extraction, archetype scoring, blend, jitter, and the rules engine |
| `core/opp_rules.{h,cpp}` | User-editable rule list: JSON in, JSON out, evaluate |
| `core/profiles.{h,cpp}` | Category store with versioning, migration, import/export, atomic save and set-aside, copied from the `reapply` pattern |
| `core/pitch_model.{h,cpp}` | Formation to 11 points, then lines, width envelope, run arrows and the heat grid (24x16) from a `SliderSet` |
| `core/match_apply.{h,cpp}` | Per-match hook: resolve fixture, opponent, profile, `gv` writes, then clear |
| `ui/ui_tactics.{h,cpp}` | Tactics sub-tab under Team. Panels: Pitch, General, Team, Position, Opposition, Presets. |
| `features/tactics.lua` (module `tactics`) | Bulk DB writes and snapshots. Single-slider drags stay native through `App::edit`. |

Wiring:
- Declare `draw_tactics` in `app.h`.
- Add the two source lists (`run_native.sh`, `build_win.sh`).
- Register `turbo.lua M.MODULES`.
- Add the `turbo_tactics.lua` stub.
- Add a kill switch `tactics_off.txt`, shown on the Status tab.

Data lives in `turbo_output/tactic_profiles.json` (user data) and `turbo_output/opp_rules.json`. Light UI state (active preset name per category) goes in `gui_settings.json`.

## 2. Slider data model

```
SliderDef { key, scope: general|team|position|opposition,
            min,max,def (def = Authentic baseline or "game decides" = -1),
            unit, binding{kind, target, scale_fn}, status: live|probe|re|preview,
            group, desc, ref (FM/FC analogue) }
SliderSet  { version, values{key->float}, enabled{key->bool} }   // disabled = leave game alone
Layers     effective = clamp( general ⊕ team ⊕ position[slot] ⊕ opposition_offsets ⊕ user_override )
```

Every slider has an `enabled` flag. A slider that is off is never written, so "untouched Authentic" is the default state of the whole system.

## 3. Slider catalogue

Ranges are 0..100 unless noted. Defaults marked A are community "Authentic" values and are version-dependent, so they ship as hints only. Status key: LIVE = `gv`, DB? = needs the schema dump, RE = needs reverse engineering or Attribulator, PRV = preview only (drives the pitch but has no game effect until bound).

**General (both teams; each row writes both sides where split):**

| Key | Range / default | Binding |
|---|---|---|
| `gen.injury_freq` | 0..100, game decides | LIVE `GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER` and `_CPUAI` (V; in-game check pending) |
| `gen.injury_sev` | 0..100, game decides | LIVE `INJURY_SEVERITY_*` |
| `gen.injuries_off` | bool | LIVE `NEVER_INJURE` |
| `gen.player_speed` (sprint and accel) | 0..100, 36 A | RE (Attribulator `GameplayAttribulatorSetVar`) |
| `gen.pass_error`, `gen.pass_speed` | 0..100, 54 A | RE |
| `gen.shot_freq`, `gen.shot_error` | 0..100, 55 A | RE |
| `gen.control_error` (first touch and trapping) | 0..100 | RE |
| `gen.foul_freq` | 0..100 | RE (no variable found; `strings_grep` for FOUL/REF) |
| `gen.card_strictness` | 0..100 | RE (`gp_rules_refereestrictness`) |
| `gen.gk_ability` | 0..100, 55 A | RE |
| `gen.weather`, `gen.tod`, `gen.difficulty` | enums | LIVE (`OVERRIDE/WEATHER`, `OVERRIDE/TOD`, `OVERRIDE_MATCH_DIFFICULTY`) |

**Team (per side; "my team" is the user's; the opposition scope reuses the same keys as offsets):**

| Key | Notes | Binding |
|---|---|---|
| `team.formation` | enum from `formations` | DB?, runtime `SetSheet` if the DB is overwritten at save (U) |
| `team.line_depth` | defensive line height | PRV, then DB? or RE |
| `team.line_length` | compactness, the FC "Line Length" slider | PRV, RE |
| `team.width_def`, `team.width_att` | def and att width | DB? (FC Custom Tactics width) |
| `team.press_intensity`, `team.press_trigger` | FM trigger press | PRV, RE |
| `team.engagement_height` | low, mid or high block | PRV |
| `team.def_positioning` | marking tightness and zonal vs man | PRV, RE |
| `team.forward_runs` | run frequency | PRV, RE |
| `team.tempo`, `team.directness`, `team.buildup_short` | FM tempo and passing directness; FC build-up enum | DB? |
| `team.cross_freq`, `team.dribble_freq`, `team.shot_patience` | | PRV |
| `team.counter_press`, `team.counter_attack` | transition | PRV |
| `team.offside_trap`, `team.tackle_aggr` | | PRV, RE |
| `team.fluidity` | roaming overall | PRV |

**Position (slot-indexed, seeded from FC role and focus, FM duty):**
`pos.forward_runs`, `pos.roam`, `pos.hold_pos`, `pos.width_bias`, `pos.depth_bias`, `pos.close_down`, `pos.tackle_aggr`, `pos.shot_freq`, `pos.dribble_freq`, `pos.risk_passing`, `pos.cross_depth`. Role and focus selection writes the existing `players.role1..9` (LIVE, V). A duty or focus seeds `attack_bias` 0..1 into those sliders. Everything else is PRV, and DB? or RE.

**Opposition (CPU Opponent only; meaning "offset" is added to the team and general sliders):**
`opp.aggression`, `opp.tackle_slide_freq`, `opp.pro_foul_freq`, `opp.buildup_speed`, `opp.cross_freq`, `opp.shot_type_mix`, `opp.dribble_skill_freq`, `opp.line_depth`, `opp.width`, `opp.press`, `opp.tempo`, `opp.risk`. Only the injury offsets are LIVE. Opposition tactics are DB fields (formation or tactic rows) read at match setup (U). These are the main RE risk.

The honest outcome of Phase 1 is that the Tactics editor, presets and opposition engine all work end to end with LIVE and PRV sliders. RE sliders light up one at a time as bindings are proven, each with its own kill switch.

## 4. Opposition-variety engine

**Inputs (`TeamFacts`, read once per match from the DB):**
- OVR band (squad top-14 mean)
- League tier
- Reputation (`teams` overall and prestige fields)
- Squad mean age
- Formation (shape counts: back line size, number of strikers, wide midfielders)
- Mentality (`cm_mentalities`)
- Pace mean and passing mean
- Aerial profile (a tall-striker flag)
- Relative strength: `ratio = opp_ovr / user_ovr`
- Home or away

**Archetypes (scored 0..1, blended; not hard-assigned):**

| Archetype | Triggers | Offsets |
|---|---|---|
| High press (Gegenpress) | high pace, mean age under 26, aggressive mentality, ratio near or above 1 | press +, line depth +, tempo +, forward runs + |
| Low block counter | ratio below 0.9, mentality defensive, high pace | line depth -, press -, counter_attack +, width_def - |
| Possession | high passing, strong OVR, 4-3-3 or 4-2-3-1 shape | tempo -, buildup_short +, directness -, fluidity + |
| Wing play | 2 wide forwards or wingbacks and a tall striker | width_att +, cross_freq +, cross_depth - |
| Direct / route one | low passing mean, tall strikers, low OVR | directness +, buildup_short -, cross_freq + |
| Balanced | fallback | none |

**Quality scaling.** Quality sets the amplitude, not the style. A top band reduces pass and control error offsets, a bottom band increases them, and tier controls how large the style offsets are. Offsets are bounded by +/-`opp.max_delta` (default 15 on a 0..100 slider) so a Dynamic-Opposition team stays recognisable.

**Determinism.** `seed = hash(career_id, season, matchday, opp_teamid)`. An xorshift PRNG produces per-slider jitter (+/-`opp.jitter`, default 4) and a small archetype-weight perturbation, so the same fixture always gets the same profile in a given career. A "re-roll" button bumps `salt`. There is no `os.time` or `rand()` in the engine.

**Rules engine.** `opp_rules.json` is an ordered list of `{when: {field, op, value}[], then: {archetype_bias | slider_offset | force_preset}, priority}`. Evaluation: base archetypes, then rules by priority, then jitter, then clamp. The built-in rules are ordinary data in the same format, shipped as `builtin_rules`. User rules can disable or override them by id. "Explain" mode lists which rules fired, which makes the system debuggable.

**Per-match hook (`match_apply`).** On fixture and match-setup detection:
1. Resolve the opponent `teamid`.
2. Run `compute_profile`, giving a `SliderSet`.
3. Apply only enabled `gv`-bound keys via `gv::apply`.
4. Remember what was set, and on match end, game exit or the kill switch, clear it.
Nothing runs at launch (launch-safety rule, V) and nothing runs before the career connects. An offline guard blocks all writes if an online session is detected. A visible "overrides active" line on Status lists each variable.

**Overrides (strongest last):** global opposition scale (0 = off, 100 = full), per-archetype strength, per-team pinned preset (team id to preset), per-fixture one-off, and per-slider lock. A "none" mode leaves the opposition alone.

## 5. Presets and storage

**Categories:** `general`, `team`, `position`, `opposition`, `bundle` (a full snapshot of all four plus rules and bindings).

**File format (`tactic_profiles.json`, also the export unit):**
```json
{"turbo_tactic_profiles": 2, "game": "fc27", "build": "<turbo ver>",
 "profiles": [{"id":"uuid","category":"team","name":"Gegenpress 4-3-3",
   "created":"2026-10-06","note":"","tags":[],
   "formation":"4-3-3",                    
   "values":{"team.press_intensity":80},   
   "enabled":["team.press_intensity"],     
   "meta":{"bindings_seen":{"team.press_intensity":"preview"}}}],
 "active": {"general":"id","team":"id"}}
```
- A bundle embeds sub-profiles under `parts` keyed by category, and the opposition rules under `rules`.
- Versioning: top-level integer `turbo_tactic_profiles`. A migrations table `v1->v2->...` runs on load, and unknown keys are preserved in `extra` on round-trip. A file with a newer version is read-only and offered for import after a warning.
- Unknown slider keys are dropped and counted, as `parse_reapply_json` does.
- Atomic save (`.tmp` then rename), a corrupt file is set aside as `*.unreadable.json`, and a kill switch disables loading.
- Import and export: one profile, one category, or a bundle, via the existing `file_picker`. Import previews diffs and name clashes (rename, replace, skip).
- Built-in presets are shipped as code and immutable: "Authentic baseline", "Realistic fouls", "Low-scoring", plus archetype presets, along with FM-style role-and-duty seeds. Users clone them.

**General-setting presets:** ship about 8 starters, such as Authentic (all sliders off), Tough referee, Physical, Fewer injuries, and High scoring. Only the LIVE slider values apply until RE binds the rest.

## 6. Phasing

| Phase | Scope | Gate |
|---|---|---|
| P0 | Fixes first: role bug and descriptions, `describe()` and a body-type probe. Schema dump, `strings_grep` name list, Attribulator probe, DB reload differential test | Recorded evidence |
| P1 | `sliders`, `profiles`, `pitch_model`, Tactics UI with the preview, LIVE general sliders, the preset store | Native tests green |
| P2 | Opposition engine, rules, `match_apply` with LIVE bindings only (injury and difficulty offsets) | In-game verification of `gv` variables |
| P3 | DB-bound tactic fields once the schema is known, then Attribulator sliders one by one (each with a kill switch, "experimental" until an in-game A/B test passes) | RE per slider |
| P4 | Polish: heatmap modes (with ball, without ball, overall), import/export UX, docs | |

## 7. Tests

- **Core (native):**
  - Catalogue validation: unique keys, defaults within range.
  - Profile store: round-trip, malformed entries, migration v1 to v2, newer-version read-only, atomic save and set-aside.
  - Archetype scoring on fixture teams: a 90-OVR pacey 4-3-3 gives high-press weight, a weak side gives low-block.
  - Determinism: the same seed gives the same output, a different salt gives different output.
  - Clamping at `max_delta`.
  - Rules: priority, disabling a built-in, "explain" output.
  - `pitch_model`: formation to 11 points, line positions monotonic in `line_depth`, the heat grid sums to a constant.
- **UI cases** (`test_tactics.h`): select a team, open Tactics, drag a slider, check the toast and the stored profile, then save, load, import and export.
- **Lua** (`tNN_tactics.lua`): module registered, batch DB edits validated against the simulator extended with a tactics table, and `t08_launch_safety` unchanged.
- **Tooling:** extend `check_fc27_schema.py` with the new table and field names. Run `luacheck`.
- **In-game checklist** (manual): each LIVE variable A/B tested, with the match log recording what was applied.

## 8. Risks

1. **Most sliders are unwritable today.** The Attribulator, line and press values, foul and referee values, and the tactic tables are all U. The mitigation is the binding and status model, so the UI is honest and nothing is faked. Promised features must be reported against it.
2. **Opposition tactics may not be DB-readable by the AI at match setup.** The result may be that the engine only affects LIVE sliders (injury offsets, difficulty), which is a weak variety lever. A fallback is to vary the user-visible match difficulty override.
3. **Authentic or Dynamic Opposition may overwrite values.** Writes through GameVars are process-wide and apply to both sides. Opposition-only effects are limited to the `_USER`/`_CPUAI` split. Dedicated per-side checks are needed.
4. **Reapply semantics:** DB or team-sheet edits may be reverted at load or overwritten at save (the cached `TeamSheet` service). A differential test decides whether a reapply store is needed.
5. **Online or anti-cheat exposure.** Keep the offline guard, auto-clear on exit, and a visible overrides-active indicator.
6. **Frame cost:** compute the heat grid on change only, never read tables per frame, and keep it to a bounded number of draw primitives.
7. **Preview vs reality:** the pitch visuals are computed approximations. Label them "preview" so they are not mistaken for engine truth.
8. **Preset drift:** FC patches may change defaults. The version tag and `bindings_seen` metadata allow warnings on load.
9. **Community defaults conflict:** Authentic values vary between sources, so they are hints, not constants.

Key paths:
- `/home/user/FC27-Editor-Turbo/turbogui/src/core/match_setup.cpp`
- `/home/user/FC27-Editor-Turbo/turbogui/src/core/reapply.*`
- `/home/user/FC27-Editor-Turbo/turbogui/src/ui/ui_teams.cpp`
- `/home/user/FC27-Editor-Turbo/docs/re/match_setup.md`
- `/home/user/FC27-Editor-Turbo/scripts/check_fc27_schema.py`