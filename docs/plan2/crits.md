# Feasibility review: Turbo 2.0 designs (adversarial)

I did not re-read the repo. Labels (VERIFIED, ASSESSED, UNCERTAIN) are carried from the discovery reports.

## Verdict
All three designs promise a full slider catalogue, but only about 10 of roughly 80 sliders have a proven write path. The designs hedge with "Preview" and "RE" badges, which is honest. Even so, the opposition-variety goal, the main purpose of the request, rests almost entirely on unproven paths.

## REAL (proven write path in repo)

| Item | Evidence | Remaining caveat |
|---|---|---|
| Injury frequency and severity, per side | VERIFIED: `gv::known_vars()` has `GAMEPLAY_CUSTOMIZATION/INJURY_{FREQUENCY,SEVERITY}_{USER,CPUAI}`; read sites in `docs/re/match_setup.md` | In-game effect check still pending (`docs/fc26-parity.md:77` says "untested in a match"). The `_USER`/`_CPUAI` split means "user side vs CPU side", not "both teams" in a CPU-vs-CPU sim. |
| `NEVER_INJURE`, weather, time of day, difficulty, CPU substitutions off | VERIFIED code; injuries-off accepted by the game | Process-global. They apply until the game closes and are not saved with the career. |
| `players.role1..9`, `preferredposition*`, traits, `bodytypecode` | VERIFIED existing editor (`ui_players.cpp:1015`) | Ranges are UNCERTAIN until a schema dump exists. |
| Profile store (JSON, atomic save, set-aside) | VERIFIED pattern in `core/reapply.*` | None. This is pure Turbo code. |
| Squad role memory write | VERIFIED (`squad_role.lua:144-151`, `player_status_roles.md`) | The over-30 bug cause is unconfirmed (see below). |
| `describe()` cleanup, the audit table items | VERIFIED locations in the UI audit | UI work only. Nationality, `rivalteam` and similar pickers need only existing lookups. |
| Mini-pitch drawing | ASSESSED: ImDrawList primitives, no texture needed | Frame-loop exceptions disable the overlay for the session (VERIFIED `overlay_dx12.cpp:612-646`), so the draw path must be exception-proof. |

## PLAUSIBLE (needs one verification step before shipping)

1. **Role-bug fix (membership from the PSM vector).**
   - Cause A (the `break` on a gap in `user_squad`, `game.lua:88`) is ASSESSED only. Nobody saw a real `cm_teamsheets` row.
   - Cause B (the game recomputes roles on event 0x17, season reset and signing) is VERIFIED from the RE doc, but it affects all ages equally. It matches "over 30" only by inference that veterans are the rank-computed Crucial/Important players.
   - Neither fixes a bug whose cause is unconfirmed. The diagnostic dump must run first, and the designs correctly list it as P0.
   - Re-applying after event 0x17 depends on Turbo observing that event. Whether the bridge sees 0x17 and `SEASON_RESET` is UNCERTAIN, because the bridge polls on career-mode events.
2. **Body-type catalogue.** The probe is trivial, but the "player-specific body types exist above code 11" premise is UNCERTAIN. Nothing in the repo shows any real player uses a code outside 1–9 and 11. Labelling "Haaland body" from example names is only valid if the probe shows a code used by few players. Writing a specific body onto a generic head is UNCERTAIN for skeleton and kit mismatch.
3. **Formation as DB write.** The `formations` table is hand-written in the test fixtures (`world.lua:250`, `gui_world.lua:339`). The real field list is UNCERTAIN, and a real formation needs 11 positions, so `offset2x..offset11y` is a guess.
   - `TeamSheet` is a runtime master copy, loaded once at `LoadGameComplete` and written back at save (VERIFIED from disassembly notes, `[H code, not run]`).
   - A DB edit of `cm_teamsheets` is therefore "invisible at runtime and overwritten at next save". The same risk applies to any DB-based tactic edit.
   - The editor may silently do nothing in game unless it uses `SetSheet` (UNCERTAIN, never called) or forces a reload (UNCERTAIN).
4. **Per-fixture opposition application using game variables.** The write mechanism is proven. Two weaker points:
   - Detecting "this fixture's opponent" at match setup is not described anywhere in the discovery reports. The mods-research claim "Turbo already hooks fixtures and match setup" is not supported by the data or arch reports (UNCERTAIN).
   - The only per-side proven variables are injuries. Varying only injury offsets is no meaningful playing-style variety.

## UNPROVEN (no write site known, silently inert if built)

Every item below is **RE or probe required**. If a slider is shipped without a binding, it only changes the Turbo preview and the profile file.

| Slider group | Why unproven |
|---|---|
| Player speed (sprint, acceleration) | No variable, field or call located (data report, section 3). Attribulator is a guess. |
| Pass error and speed, shot error, speed and frequency, header errors | Same. The FC 26 community slider names do not map to any Turbo-visible symbol. |
| First touch, trapping, ball control error | Same. |
| Foul frequency | No variable found (`match_setup.md` section 5). |
| Card strictness | `gp_rules_refereestrictness` is an Attribulator schema name. How to write it, and whether the match reads it at load or per frame, is UNCERTAIN. |
| Goalkeeper ability | Same Attribulator speculation. |
| Team line depth, width, line length, press intensity and trigger, forward runs, tempo, directness, offside trap, tackle aggression | No `teamtactics`-like table anywhere in the repo or the dumped schema (not in the repo). FC 25-era table names are memory-based guesses (UNCERTAIN). FC 27 custom tactics may be runtime `TeamSheet` data. |
| Per-position instructions | Same. FC 25 and FC 26 moved to role plus focus, so FM-style per-player sliders may have no EA equivalent. |
| Opposition team tactics via DB | The AI may recompute tactics at match setup or when the manager changes. Unknown whether it reads the DB at all. |
| Adaptation (game-state reactions mid-match) | Would need an in-match hook. Nothing exists. |
| Sim-match effects | VERIFIED: `SIM_SETTINGS/*`, `INJURY/*`, `CARD/*` and `FATIGUE/*` are overwritten by the ini load before the read. No override can change simulated matches. All sliders therefore apply only to played matches. |

## Specific hand-waving to reject

1. **"Attribulator calls exist in Live Editor's Lua env."**
   - VERIFIED only that the names `GameplayAttribulatorSetVar` and `GetVarType` are listed (`env.lua:136`).
   - Whether Turbo can call them is not shown. Turbo is a Lua pack plus an overlay DLL and calls game natives through its own bridge, with a strict launch-safety rule (`t08_launch_safety` allows exactly one `package.loadlib`).
   - Their semantics, per-side scope, value ranges and FC 27 coverage are UNCERTAIN. Treat the whole L3 tier as a research project, not a slider list.
2. **"Authentic Gameplay stays selected and Turbo overrides on top."**
   - Whether values written to GameVars or the Attribulator survive Authentic/Dynamic Opposition mode is unverified. The fc-research report records conflicting evidence on whether Authentic is locked.
   - Silent no-op or silent overwrite is the likely failure mode, with no visible error.
3. **FC 27 CPU slider names** ("25 new sliders and 10 CPU sliders") are second-hand snippets of Pitch Notes that nobody read (ea.com egress-blocked). Do not use them as a catalogue.
4. **Authentic default values** (sprint 36 and others) are community figures that differ between lists. Defaults of "36" and "48" are meaningless without knowing the scale and the internal unit the game uses.
5. **Preview math is invented.** Formulas such as `0.18 + 0.30·d` metres are design choices, not engine behaviour. This is fine only if labelled, but a heatmap will appear to show "effects" that the game never applies.
6. **Overrides auto-clear and offline guard.**
   - Online-mode detection is UNCERTAIN and has no known method. The "offline guard" is not buildable until it is researched.
   - Anti-cheat status (Javelin in FC 27 offline) is unverified (snippet only).
7. **Persistence.** Whether FC 27 resets tactic tables at career load is UNCERTAIN. A differential test is needed before deciding on a reapply store.

## Ordered verification list (cheapest first)

1. Dump the schema (`fc27_db_schema.json`), then grep tables for `*tactic*`, `*mentalit*`, `*instruction*`, `*formation*`, `*teamsheet*`. This decides the whole team and position tier (about 40 sliders).
2. Dump a real `cm_teamsheets` row and the PSM vector with ages to settle role cause A vs B.
3. Re-extract the 991 game-variable names, then grep for PASS, SHOT, FOUL, REF, SPEED, CARD, ERROR, TRAP, CONTROL. This is cheap offline work using `scripts/re/strings_grep.py` and `xrefs.py`.
4. Probe Attribulator: write one harmless value, read the match back, and test Authentic compatibility.
5. In-game A/B for the injury variables that are still "pending".
6. Differential reload test: write, save, reload, read for the formation and sheet tables.
7. Body-type probe (`bodytypes_fc27.json`).

## Net feasibility call
- **Ship now:** role fix with diagnostics, description cleanup, profile store, mini-pitch preview, injury and match-environment sliders, role/formation editing if the schema confirms it.
- **Ship as preview only until proven:** everything else. The UI must say so per slider. Do not promise "player speed", "foul frequency" or "opposition style variety" as working game effects in 2.0.0.
- **The opposition-variety goal** is realistically limited to (a) DB formation and role variation if the AI reads them, and (b) injury and difficulty offsets. Anything richer depends on the unproven Attribulator or `TeamSheet` work.

---

# Scope-and-risk review: Turbo 2.0

I did no new repo reads. Everything below is ASSESSED from the discovery reports and designs, and the reports' own VERIFIED labels are carried through. Nothing was run, and no source was edited.

## 1. Where the designs are weakest

1. **The tactics editor is mostly unwritable today.**
   - The only proven gameplay write path is the game-variable store (`gv::known_vars`, `match_setup.cpp:~320-332`). It covers injuries per side, weather, time of day, difficulty, CPU subs and NEVER_INJURE. (VERIFIED)
   - Speed, pass/shot/trap error, foul frequency, card strictness and every team or position tactic slider have no known write site. (VERIFIED absence in the repo)
   - The real tactic tables are unknown, because `fc27_db_schema.json` is gitignored and absent. The `formations` fixtures in `world.lua:250` and `gui_world.lua:339` are hand-written. (VERIFIED)
   - Team sheets are cached at `LoadGameComplete` and rewritten at save, so a DB edit of `cm_teamsheets` may be overwritten. (VERIFIED from disassembly notes, tagged "H code, not run")
   - If 2.0 ships dozens of preview-only sliders, the user may take drawn heatmaps as engine behaviour. Every design flags this risk, but none caps the count.

2. **The opposition-variety engine has no proven output channel.**
   - The only per-side path is `_USER` / `_CPUAI` for injuries. Everything else applies to both sides. (VERIFIED)
   - A weak engine would vary injury odds and difficulty and nothing else. That barely delivers the user's "variety of playing styles".
   - The engine can be built and unit-tested, but it should not be sold as a 2.0 feature until a proven channel exists.

3. **Three of the four designs plan a new top-level "Tuning" tab or a "Tactics" sub-tab plus other edits.**
   - A top-level tab changes `names[]`, `request_tab` and `test_main.cpp:3341` (`tab < 7`). That is cheap but touches shared test code. (VERIFIED)
   - Keep it to one sub-tab in `##ttabs`, with a "Game settings" section shown when no team is selected.

4. **The role bug has no confirmed cause.**
   - No age-based skip exists in the code. (VERIFIED)
   - Candidate A is `user_squad()` stopping at the first nil or `-1` slot (`game.lua:88`). Candidate B is the game rewriting roles on `SEASON_RESET`, event 0x17 and event 0x5F. (A is ASSESSED, B is VERIFIED from the RE doc.)
   - Fixing A and B blind risks "fixed" with no change. A diagnostic dump must come first.

5. **Scope creep in the cleanup item.**
   - The UI audit lists 36 items. Items 11, 29, 30, 31 and 35 should stay as they are or become a collapsed "Technical" section.
   - Writing `describe()` as one function and routing the log, toasts and tooltips through it covers most of the value.

## 2. Risk register

| Risk | Rating | Mitigation |
|---|---|---|
| Launch safety | Low if rules hold | `t08_launch_safety` allows exactly one `package.loadlib`. Nothing new may run at boot, and no DB read before connect. (VERIFIED) |
| Save corruption | Medium for DB writes, low for game variables | Gate every record write on `table_alive`, check `has_room` before inserts, take ranges from `GetDBMeta`, and never auto-save. Game variables are runtime only. |
| Revert or overwrite by the game | Medium | A differential test (write, save, reload, read) decides whether a tactic reapply store is needed. |
| Online contamination | Medium | Game-variable overrides last until the game closes and are process-wide. (VERIFIED) The offline-detection method is UNCERTAIN, so do not promise an "offline guard" until one is found. Ship auto-clear on exit and an "overrides active" banner. |
| Anti-cheat | UNCERTAIN | The Javelin-in-offline claim for FC 27 came from an unopened snippet. Do not build to it. Add no new hooks in 2.0. |
| Overlay frame cost | Medium | An exception in `App::draw` disables the overlay for the session (`overlay_dx12.cpp:612-646`). (VERIFIED) Cache by `app.gen`, recompute the heat grid only on change, cap primitives at about 400, and do no file I/O or JSON in the frame. |
| Input capture | Low-medium | Use `InvisibleButton` so ImGui owns the capture. The input shield hooks (0.2.5) are still "installed but not tried in game". (VERIFIED) |
| Maintenance burden | High | Each new file must be added by hand to `run_native.sh` and `build_win.sh`. (VERIFIED) Registry-driven sliders (`SliderDef` data) keep that bounded. Hand-written draw code per slider does not. |
| Game thread freeze | Medium | Opposition-wide Lua writes must be chunked, with one command in flight on the mailbox. (VERIFIED) |
| Source quality | Medium | The FM and EA research is snippet-only. The FC 27 slider names and "25 new sliders" claim are second-hand. Authentic defaults conflict between sources, so ship them as hints. |

## 3. Minimum coherent 2.0

**In 2.0:**
1. Role-bug diagnostic, then the fix (membership from the PSM vector, skip counters, tolerant `locate`, reapply after `SEASON_RESET` and 0x17).
2. A `describe()` pass: `App::edit` log, undo toast, the two tooltips, nationality, `rivalteam`, `teamid`, match-setup combos, and `label_map` gaps.
3. Body-type probe, a catalogue, and a generic versus player-specific browser with "copy from player". Do not offer an untested specific body on a generic head by default. (UNCERTAIN)
4. Tactics sub-tab with:
   - the mini pitch (formation, defensive line, width band, a press heat grid);
   - a slider registry with per-slider status badges (Live, DB, Preview, RE);
   - the preset store with save, load, rename, delete and export, by category.
5. Live write sliders limited to the proven game variables: injury frequency and severity, injuries off, weather, time of day, difficulty, CPU subs off.
6. Opposition engine as a deterministic, unit-tested solver with a preview card ("opposition card"), applied only to proven channels.
7. Docs and changelog updates.
8. Phase-0 probes (read-only) as a prerequisite.

**Deferred:**
- **2.1:** DB-bound tactic fields once the schema dump proves them, formation writes after the differential test, role and focus editing, and a rule editor for opposition rules.
- **2.2:** Attribulator sliders (speed, error, referee), one at a time, each with its own kill switch, clamps, and an in-game A/B pass. Also per-fixture auto-apply and mid-match adaptation.
- **Never in 2.0:** new native hooks, a game call for `SetSheet`, or anything touching the EA slider menu.

## 4. Sequenced task list

Sizes: S is under a day, M is 1-3 days, L is 3-7 days.

| # | Task | Size | Depends on |
|---|---|---|---|
| 0a | `probe_tactics.lua`: table scan for `*tactic*`, `*mentalit*`, `*formation*`, `*teamsheet*`; `cm_teamsheets` row; PSM vector with ages | S | none |
| 0b | `bodytypes.lua` probe to `turbo_output/bodytypes_fc27.json` | S | none |
| 0c | Re-extract the 991 game-variable names with `strings_grep.py`; grep for PASS, SHOT, FOUL, REF, SPEED, CARD, ERROR, TRAP, CONTROL | M | none |
| 0d | User runs 0a-0c locally and returns the dumps | external | 0a-0c |
| 1 | Role fix and tests: `psm_members`, `user_squad` skip-not-break, tolerant `locate`, summary counters, `world.lua` `sheet_gap` and 52-entry vector, `t04` and `t18` cases | M | 0d (for the cause) |
| 2 | Role reapply on `SEASON_RESET` / 0x17 with a `role_reapply_off.txt` switch | S | 1 |
| 3 | `describe()` in `field_labels.h`, routed through the log, undo toast and tooltips; native test that tooltips do not leak "code N" | M | none |
| 4 | Pickers: nation, rival team, manager `teamid`; match-setup combos; Technical collapse in Tools and Status | M | 3 |
| 5 | Body-type catalogue, data header, UI browser, generator script | M | 0d |
| 6 | `core/sliders` registry (key, range, default, binding, status) and `core/tactic_profiles` store (atomic save, set-aside, malformed count) with native tests | M | none |
| 7 | `core/tactics` (formation geometry, preview model) with native tests, using a built-in fallback formation table if the schema lacks one | M | 6 |
| 8 | `ui/ui_tactics` plus pitch widget (`InvisibleButton`, cached preview, layer toggles), wiring in `app.h` and both source lists | L | 6, 7 |
| 9 | Live General sliders over `gv::known_vars`, `tactics_off.txt`, overrides-active banner, auto-clear on exit | M | 8 |
| 10 | Opposition solver (seeded, tiers and archetypes) with determinism and monotonicity tests; preview card only | M | 6 |
| 11 | Docs, README, `CHANGELOG.md`, `check_fc27_schema.py` coverage mode, luacheck, ASan/UBSan run | S | all |
| 12 | In-game verification checklist (user, manual) | external | 9 |

Items 3, 6 and 7 can run in parallel with 1. Item 8 is the critical path.

## 5. Test coverage gaps to close

- No test can catch the real-world role bug, because the simulator writes a gap-free sheet (`world.lua:204-214`). (VERIFIED, reported) The `sheet_gap` option is essential.
- The Lua simulator has no tactics table. Add one, but label it a fixture, not schema.
- UI tests must include a "no write when `tactics_off.txt` exists" case, and a case that drags a slider while the overlay is disabled.
- The in-game checks (injuries during a match, difficulty and weather overrides, input shield) are not automatable. They must appear in the release notes as "untested in game".
- The Windows smoke and overlay tests only matter if a native call or hook is added, so keep 2.0 free of both.

## 6. Biggest decision for you

If the phase-0 dump shows no editable tactic table, 2.0 is honestly a preview-and-profile tool plus the role fix, cleanup and body types. State that in the UI, rather than shipping sliders that do nothing.

---

# Honesty-of-visuals review of the mini-pitch effect preview

Claims are labelled VERIFIED (read in code or source), ASSESSED (reasoned from evidence) or UNCERTAIN. I did not re-read the repo for this review. VERIFIED here means "stated as verified in the discovery reports". I edited nothing.

## 1. Core finding

The designs treat "preview" as a small caveat ("Turbo estimate, not engine output"). That is not enough. The risk is a visual that implies a game effect that does not exist. There are three separate ways this happens:

1. **Unapplied sliders drawn as if they work.** The data reports (VERIFIED) show only injuries, weather, time of day, difficulty, CPU subs and `NEVER_INJURE` have a proven write path (`gv::known_vars`, `match_setup.cpp:~320-332`).
   - No table or variable for line depth, width, pressing, speed, error rates or referee settings is known. `fc27_db_schema.json` is absent.
   - If a "line depth" slider moves a crisp line on the pitch, the user will believe the game moved its line. In most cases it did not.
2. **Invented unit conversions.** The ux-first design maps line depth to metres: `x = 0.18 + 0.30·d`, "about 19 to 48 m". It also maps width, run length and roam radius with similar constants.
   - These constants are made up (ASSESSED).
   - A label such as "line +6 m" asserts a physical effect no source supports.
3. **Heatmaps and gauges that look like measurements.** The Gaussian press-heat grid, the "coherence" risk gauge and the roam jitter animation are computed from invented formulas. A "Compare" mode with displacement lines makes them look like measured before/after data.

**Verdict.** Treat any visual as dishonest unless it carries both of these:
- (a) a provenance tier, showing what kind of number it is, and
- (b) an application status, showing whether the game will receive the value.

## 2. Classification of visuals

### Tier E, Exact (drawn straight from stored values)

Allowed to look crisp and carry numbers.

| Visual | Source | Condition |
|---|---|---|
| Formation dots | `formations` offsets (positions) or the built-in table | Exact only if the schema probe proves the real field names. The fixture in `world.lua:250` has `offset1x/offset1y` only (ASSESSED, hand-written). Real 11-slot field names are UNCERTAIN. |
| Position labels and roles | `teamplayerlinks.position`, `players.role1..9`, `preferredposition1..7` | VERIFIED names. |
| Squad status badges | PSM vector | VERIFIED. |
| Injury and difficulty readouts as numbers, not drawings | `gv` values | VERIFIED write path, in-game check pending for some. |

Even Tier E is "exact" only about the stored value. It is not a claim about on-pitch behaviour. If the game recomputes or overwrites the formation, such as the `TeamSheet` service overwriting `cm_teamsheets` at save (VERIFIED from RE notes, `[H code, not run]`), the dot map shows the saved value, not what plays. That needs the "applied" status below.

### Tier D, Derived (monotonic geometry from a slider, no real units)

Allowed, but only as ordinal and schematic.
- Lines (defensive, press, midfield), the width band and run arrows are drawn from slider values.
- **Rules:**
  - Position is a normalised 0–100 scale along the pitch axis. It is never in metres.
  - Drawn dashed or thin, with the slider value as the label ("Line depth 62/100"), not "37 m".
  - The mapping is monotonic and its range is fixed and documented. Dragging a slider only moves the line. It does not claim to move players by a stated distance.
  - Dot blending (defenders 70% toward the line, and so on) must not move the dots on the Tier E formation. Draw a separate ghost layer "where the slider suggests", so the real formation stays visible.

### Tier M, Modelled (heatmaps, gauges, animation)

Allowed only under strong labelling (section 3). In the current design these are:
- the press-density heatmap,
- the compactness hull colouring,
- the roam jitter animation and tempo pulse,
- ball-side shift,
- the risk or coherence gauge,
- the opposition ghost pitch showing solver output.

Facts that make them modelled, not measured (ASSESSED):
- No engine data about heat or movement is available to Turbo.
- FM's heatmaps come from simulated matches. Turbo has no equivalent match output.
- The simulated-match path ignores overrides (`match_setup.md` §2, VERIFIED), so even true effects would only show in played matches.

## 3. Labelling rules for estimated visuals

1. **Persistent mode chip** on the pitch header: "Model view: Turbo's estimate, not game output". It is not dismissible and does not auto-hide.
2. **Visual grammar.**
   - Exact: solid fills and lines.
   - Derived: dashed lines, no numeric units.
   - Modelled: hatched or stippled fill in a distinct hue. Heatmaps use a monochrome ramp labelled "relative intensity (arbitrary)", with no legend values in %, metres or seconds.
3. **No numbers on modelled layers.** The legend says "more / less". Never show "+6 m" or "34% pressure".
4. **Remove unfounded claims.**
   - The "risk gauge" must not state outcomes like "exposed to balls in behind" as fact. Show it as a heuristic hint ("rule of thumb") or drop it.
   - Drop the animation. Moving dots implies simulated behaviour.
5. **Hover text states provenance.** Examples: "Drawn from your slider value; the game has not been shown to use this." or "Illustration only; derived by Turbo's formula."
6. **Layer toggles default.** Modelled layers (heatmap, hull) are off by default, with Tier E and D on. The user opts in.

## 4. Application-status overlay (the other honesty axis)

The write-state badges (Live, DB, Preview, RE) from ux-first are right but live on the sliders only. Carry them into the pitch:
- **Live** (proven write): normal rendering.
- **DB** (table write proven, game read-back unproven): normal, with a small "saved, game read-back unverified" marker.
- **Preview** or **RE** (no sink): the visual is drawn at reduced opacity with a "not applied to game" tag. A global strip lists "N of M visible settings are preview-only".
- A **"What the game will receive"** side panel lists only Live/DB writes. Anything else is shown as "saved in profile only".
- An applied-state change (a write confirmed, kill switch active, `tactics_off.txt` present) must update the pitch. When a kill switch is on, all effect visuals go grey.

Heuristic: a user must never be able to read the pitch and conclude the game will behave that way unless the slider is Live and verified. This is the main acceptance test.

## 5. Proposed visual model (concrete)

State per slider: `{value, tier(E|D|M), status(Live|DB|Preview|RE), verified_in_game(bool)}`.

| Layer | Tier | Drawing | Label |
|---|---|---|---|
| Formation dots | E | solid discs, role letters | "Formation (from game data)" |
| Defensive line | D | dashed line on a 0–100 scale | "Line depth 62/100 (schematic)" |
| Press line | D | dashed, secondary colour | "Press trigger 48/100 (schematic)" |
| Width band | D | two thin edges | "Width 55/100" |
| Run arrows | D | fixed-length arrow style, thickness by value | "Forward-run tendency (schematic)" |
| Press heat | M | stippled low-contrast ramp | "Illustrative pressure zone (Turbo model)" |
| Roam radius | M | static dashed ring, no animation | "Roam (illustrative)" |
| Risk hint | M | text chip only | "Heuristic hint" |
| Opposition card | M | style name and sliders, plus mirrored pitch | "Solver output; applies only where marked Live" |

Compare mode shows value deltas ("62 to 70"), not distances. Drag-to-edit a formation dot is the one visual that writes data (Tier E, DB). It must show a confirmation ("writes to `formations` row") and warn that the game may overwrite it at save.

## 6. Specific issues in the designs

- **ux-first (VERIFIED in text):**
  - Metre values ("about 19 to 48 m").
  - Gaussian heatmap with a "colour-blind safe palette" implying data rigour.
  - Animated roam and tempo.
  - A risk gauge with outcome language.
  - Opposition "mirrored pitch" with solver lines "where the two sides' lines meet", which implies real match meetings.
- **opposition-variety:** the heat grid sums to a constant (a sound normalisation but not a truth claim). The pitch model is described as "preview" in one risk bullet only. Needs the persistent chip above.
- **safest-first:** the closest to honest, since it labels sliders `needs RE` and inert. It still lists "heatmap approximations" without a tier rule.
- **Cross-cutting UNCERTAIN:**
  - Whether AI teams even read the `formations` offsets live.
  - Whether `cm_mentalities` or team sheet edits survive load or save.
  - These determine whether even Tier E dots reflect play. Until the differential test (write, save, reload, read) passes, mark formation visuals "saved data; in-game effect unverified".

## 7. Tests (native, in `test_tactics.h`)

- Every visual layer has a tier and status. A layer without them fails a registry check.
- No label string for tier D or M contains a unit ("m", "%", "sec", "yards"). Regex test over the UI strings.
- Preview-only sliders render with the reduced-opacity flag. UI test: set a Preview slider, assert the "not applied" tag and the strip count.
- With `tactics_off.txt` present, all effect layers render grey.
- The formation layer is unchanged by any slider move (Tier E isolation).
- Heat grid is deterministic and monotonic (relative only), and hidden by default.

## 8. Recommendation

- Ship Tier E and D first, with the status overlay. They give the user real feedback (formation, relative line and width) without false claims.
- Hold heatmaps and gauges until the Phase 0 probes show what the game reads. If shipped earlier, use the hatched model style and the persistent chip. Remove metres, outcome language and animation.
- An effect visual should be promoted from "illustrative" to "applied" only after an in-game A/B test passes. This mirrors the existing rule for L3 sliders.

Source files referenced (from the reports): `/home/user/FC27-Editor-Turbo/turbogui/src/core/match_setup.cpp`, `/home/user/FC27-Editor-Turbo/docs/re/match_setup.md`, `/home/user/FC27-Editor-Turbo/docs/re/realtime_transfers.md`, `/home/user/FC27-Editor-Turbo/turbo/tests/world.lua`, `/home/user/FC27-Editor-Turbo/turbogui/src/ui/ui_teams.cpp`.