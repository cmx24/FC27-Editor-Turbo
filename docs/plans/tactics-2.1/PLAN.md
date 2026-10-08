# Turbo 2.1 tactics plan

Written 2026-10-08 in the cloud from the inputs in this folder (research.json, fc27_tactic_tables_schema.json, ingame-results-2026-10-07.md, README.md) and docs/TURBO_2_0_PLAN.md. Nothing in this plan has been implemented. Nothing starts until you approve it.

Labels used for every claim:
- **VERIFIED in a played match**: seen on the pitch or in Match Facts on turbo04 (2026-10-07). This is the only label that counts as "working".
- **VERIFIED**: read in the game's code, the game's database, or Turbo's code. Static facts about the game come from the image of the previous build (6AB9813C / 1.0.140.64835) unless stated; the running build is 6AC07E31-2145C000.
- **ASSESSED**: reasoned from evidence, not observed.
- **UNKNOWN**: nobody knows yet; the plan contains a test for it.
- **[inferred]**: something I believe you want but you did not say in those words. Correct me.

Rules this plan follows: everything is coded, built and tested in the cloud; the PC is used only to install and to play the test matches. Nothing is called working or releasable until it is tested in a played match. Turbo never touches EA's slider menu or the Authentic / Dynamic settings. Live Editor's Lua libs, EA assets and real player data are never committed.

---

## 1. What you asked for

In your words: "a plan to implement what I want the way I want it", for tactical tweaks and settings for the team and for each position, plus game settings.

What I take that to mean, from the 2.0 plan, the README in this folder and the in-game session:

1. **Your club, Football-Manager style.** Set the team's shape and instructions and give every position its own instructions, using FM vocabulary (mentality, line, pressing, roles and duties, runs, width, tempo and so on). [inferred from the 2.0 plan section 3 and 8: you asked for FM-style sliders there]
2. **Per-position control**, not just per team. [stated]
3. **Game settings**: the gameplay sliders (speed, errors, GK, positioning), injuries, weather, difficulty, substitutions. [stated; the gameplay sliders are the ones marked NEEDS-RE in 2.0]
4. **Opposition variety**: CPU teams whose style matches their own tactics and quality, different from match to match. [from the 2.0 plan section 4 and this folder's README]
5. **Authentic Gameplay and "Dynamic Opposition" stay selected**, and Turbo never edits EA's slider menu. [stated in the 2.0 plan; the in-game name of "Dynamic Opposition" is most likely "AI Behaviour: Dynamic" on the CPU Sliders tab, see section 3.5]
6. **Honesty**: a control that does nothing in a played match is not shown as if it did. [stated repeatedly]
7. **Work split**: cloud for code, builds and tests; the PC only to install and to play test matches; you publish nothing yourself. [stated]

Two things I am NOT assuming: that you want Turbo to replace the game's own Edit Tactic screen for your club (for your own club the game's screen already offers everything FC 27 has; Turbo adds FM vocabulary, presets, profiles and automation, see section 6), and that "the way I want it" means the exact 2.0 slider list. Section 4 shows which FM instructions FC 27 can actually honour; the ones it cannot are listed as such, not approximated silently.

---

## 2. Where we are today (honest status)

### 2.1 VERIFIED in a played match (Turbo 2.0.1 / 2.0.2, turbo04, Napoli v Nott'm Forest, 2026-10-07)

| Control | What was seen |
|---|---|
| Weather override | id 3 gave snow on the pitch; id 1 gave "Partly Clear". Read at kick-off: set after the pre-match screen it still applied. |
| Injury frequency and severity 100 (both sides) | Injuries at 1', 3', 5' and later; the game forced substitutions. |
| CPU makes no substitutions | Forest made no substitution for two injured players (observed to 9:36 of a 12-minute match). |
| Squad status roles (Crucial .. Prospect) | Preview reads the game's values; Apply sets all 34; re-apply on season reset works at handler level (event 23 sent by hand, kill switch `role_reapply_off.txt` honoured). A real season rollover was not played. |
| Stability | 12 hooks active, several loads, 3 played matches, no crash. Overrides do not survive a game restart (by design). |

### 2.2 Accepted by the game, no effect seen

| Control | What happened |
|---|---|
| Difficulty (`OVERRIDE_MATCH_DIFFICULTY` = 5) | Write accepted ("ok"); the pre-match line still said "Amateur"; no in-match place shows the level. UNKNOWN whether it does anything. |
| Time of day (`OVERRIDE/TOD` 0 and 4) | Both showed 5:00 PM daylight. UNKNOWN. |

Both carry the same green "Live" badge as the injury sliders today. That is misleading and is fixed in Phase 0.

### 2.3 Shown in the Tactics tab today but cannot work, or is mislabelled (VERIFIED in code)

| Tactics tab item today | Problem |
|---|---|
| "Defensive line depth" and "Build-up play" (My team) write `cm_mentalities` | The game reads `cm_mentalities` once at career load into its runtime team sheet and writes the runtime copy back over the table at every save. A Turbo edit is never seen in a match and is gone at the next save. (Static code: load path 0x147B5DB4C, save path 0x147B96DD8.) They also write `teams.defensivedepth` / `teams.buildupplay`, whose match effect is UNKNOWN (section 5, channel D2). |
| "Defensive width", "Attacking width", "Players in the box" (3), "Chance creation", "Attacking style", "Defensive style" write the legacy `mentalities` table | EA removed these controls in FC 25. The career tactic tables (`cm_mentalities`, `default_mentalities`) have no such fields; the legacy table is read only by the old database-backed sheet service, not by the career load/save paths. ASSESSED: no effect in a career match. |
| "Mentality" (7 steps, Very defensive .. Very attacking) | FC 27 has no such control. Preview only today; it should not exist as a team slider. |
| "Role 1..9" under Position/Role | These write `players.role1..9`, which is the player's own role familiarity (Role+ / Role++), not the role of a slot in the tactic. Real but mislabelled. |
| 11 gameplay sliders (sprint, acceleration, pass/shot error and speed, first touch, trapping, ball control, GK ability) | Preview only, no write path. 2.0 called them NEEDS-RE. The research found the write path candidates (section 5, channels A and B). |
| "Foul strictness" / "Card strictness" | Write `referee.foulstrictness` / `cardstrictness` on every referee row (range 0..2, which end is strict is unknown). Never tested in a match. |
| Formation (My team) | Preview only by design in 2.0. The CPU-club pitch can show the wrong formation: the reader never looks at `default_mentalities`, the table the career uses for CPU clubs. |
| Opposition card | The solver's output is mostly preview; the only values that reach the game are injury and difficulty offsets. The "opponent" is the club selected in the list, not your next fixture. The per-fixture seed uses a Lua load counter and today's date, so the same fixture gets a new profile after a reload or a day advance. |
| DB writes have no undo | Switching a DB slider off never restores the old value; no snapshot is kept. |

### 2.4 Cloud build status (checked 2026-10-08 in this session)

- `turbogui/scripts/build_win.sh` (MinGW cross-build of Turbo.dll and TurboInjector.exe) and `turbogui/tests/native/run_native.sh` (g++ with ASan/UBSan, ImGui test engine): both files have CRLF line endings. On Linux bash the first line `set -euo pipefail` fails ("pipefail\r: invalid option name"); on MSYS (your PC) bash ignores the CR. In the cloud they run from an LF copy made next to the original (`tr -d '\r' < build_win.sh > .build_win_lf.sh`, so that `$(dirname "$0")` still resolves). Results of the run started in this session are in section 10.
- The Lua suite needs `turbo/le27/libs` (Live Editor's own files, gitignored), so it runs only on your PC. `run_native.sh` also refuses to start without them (it checks for `le27/libs/v1/live_editor.lua`). Section 10 says what that means for cloud testing.

---

## 3. How FC 27 really does tactics

This section is the ground truth the rest of the plan is built on. Evidence: EA's FC 25 "FC IQ" pitch notes, the FC 26 and FC 27 pitch notes, the Edit Tactic screen seen on 2026-10-07 ("4-4-1-1 MIDFIELD", Build-Up Style BALANCED, Defensive Approach DEEP, preset "Standard", a Role / Focus per player), the career tactic tables read from the running game (probe_tactics, this folder's schema file) and the game's code.

### 3.1 What a team tactic contains (VERIFIED: EA notes + the tables + the Edit Tactic screen)

| Control | Values | Where the career stores it |
|---|---|---|
| Tactical preset ("Tactical Vision" in career: Standard, Possession, Wing Play, Counter Attack, Park the Bus, Kick and Rush, High Pressing; FC 26 also listed Balanced) | one of the list | `presetid` (ASSESSED; the label to value mapping is read in Phase 0) |
| Formation | one of the `formations` rows (899), with 11 slot positions and x/y offsets | `sourceformationid`, `formationfullnameid`, `formationaudioid`, `position0..10`, `offset0x..offset10y` |
| Build-Up Style | Short Passing / Balanced / Counter | `buildupplay` 0..3 (the game converts DB 0,1,2,3 to engine values 0,2,0,1; which number is which label is read in Phase 0) |
| Defensive Approach | Deep / Balanced / High / Aggressive, each with a Line Height 1-100 inside its band (1-30, 31-60, 61-90, 91-100; EA defaults 25, 50, 70, 95) | `defensivedepth` 1..100 only; the approach is derived from the band (VERIFIED in code: ≤30, 31-60, 61-90, >90) |
| Role + Focus for each of the 11 slots | e.g. Falseback / Balanced, Holding / Defend, Wide Midfielder / Support, Advanced Forward / Attack. Focus vocabulary: Defend, Balanced, Support, Attack, Build-Up, Roaming, Aggressive, Ball-Winning, Versatile, Wide | `pos0role..pos10role` (16-bit; how role and focus are packed is UNKNOWN, decoded in Phase 0) |
| Up to 5 tactics per team sheet, one active | | `mentalityid` (index = id % 5), `activetactic` |
| Set-piece takers and corner assignments | captain, penalties, free kicks L/R, corners L/R, long kick, throw-ins, `cksupport1..9` | `cm_teamsheets` (your club), `teamsheets` / `default_teamsheets` (CPU) |

What FC 27 does NOT have for a team any more (EA removed them in FC 25): defensive and attacking width, chance creation, pressure, players in the box, players at corners and free kicks, work rates, per-player instructions, separate in-possession and out-of-possession shapes. The "Without Ball" view on the Edit Tactic screen is derived from the roles; it is not a second formation.

During a match the manager also has Tactical Focus (Default / Attacking / Defending, which moves build-up and approach one step) and Quick Tactics (Offside Trap, Team Press, Overload Set Pieces, Get In Box). These are in-match, user-only buttons; Turbo has no lever for them.

### 3.2 Where your club's tactic lives (VERIFIED in code, old build; must be re-checked on 6AC07E31)

- The master copy is a runtime object, the game's cached team-sheet service. It is filled from `cm_teamsheets` + `cm_mentalities` once, when the career finishes loading, and written back over those tables at every save.
- Layout of the runtime tactic record (decoded from the save writer): `activetactic`, `presetid`, `defensivedepth`, `buildupplay`, the formation ids, and a list of 11 slots each with player id, position, x/y offset and the slot's role+focus value. Match setup copies the formation and each slot's role from this object into the match (so slot roles and formation are real match inputs).
- Consequence: the only ways to change your club's tactic are the game's own Edit Tactic screen, or a game call that edits the runtime sheet through the game's own `SetSheet` (section 5, channel E). Database writes to `cm_mentalities` cannot work.

### 3.3 Where a CPU club's tactic lives (VERIFIED that the tables and queries exist; ASSESSED that this is the match's source)

- CPU clubs have no `cm_mentalities` / `cm_teamsheets` rows (both tables hold one row: Napoli).
- `default_mentalities` (870 rows, one per club) is the working copy during a career: at load the game rebuilds it from `cm_default_mentalities`; at save it copies it into `cm_default_mentalities`. A write to `default_mentalities` therefore survives save and reload; a write to `cm_default_mentalities` is wiped at the next save.
- Match-side code reads `SELECT ... FROM default_mentalities WHERE teamid = N`, including `pos0role..pos10role`, `buildupplay`, `defensivedepth` and the formation. That this is the opponent's kick-off tactic in a career match is reasoned, not traced end to end: Phase 2's first test settles it.
- Separately, when a match is built the game reads live from `teams`: `buildupplay`, `defensivedepth`, `opponentweakthreshold`, `opponentstrongthreshold` and the three trait masks `trait1vweak / trait1vequal / trait1vstrong` (23 bits: Tiki Taka, Possession, Counter Attack, Direct, Crossing, Dribbling, Harass, Double Contain, Italian Defense, No Pressure, Defend The Lead, Keep Up Pressure, More Attacking At Home, physical defending styles and others). The traits are EA's own CPU style system: tuning names show they shift the CPU's behaviour sliders. Whether they still act under Authentic + Dynamic is UNKNOWN. Which value wins when `teams` and the sheet disagree on build-up/depth is UNKNOWN.
- Live Editor's own wiki says the CPU never changes a club's formation on its own, which is consistent with the formation coming from a stored row.
- The legacy `mentalities` table (4001 rows, about 5 per club, with the old width / box / style fields) is read by the old database-backed sheet service, not by the career paths. ASSESSED: not what a career match uses. Phase 2 tests it as a fallback only if `default_mentalities` shows no effect.

### 3.4 Gameplay sliders and how Authentic works (VERIFIED in code unless marked)

- EA's slider menu has, per side (User / CPU): sprint speed, acceleration, shot and pass error/speed families, header accuracy, trapping, interception and deflection errors, dribble error, tackle accuracy, injuries, GK ability, and the Team Positioning group (marking, run frequency, line height, line length, line width, fullback positioning). FC 27 added 25 more (chip/finesse/power/low-driven shot error and speed, lob/cross/through pass error and speed, jockey speeds, physicality impact, more deflection controls). The CPU Sliders tab has 16 per CPU Opponent / CPU Teammate (defending aggression, stand/slide tackle and professional foul frequency, build-up speed, shot frequencies, first-touch pass, regular and early crossing, dribble and skill-move frequency).
- Inside the game the values are bytes 0..100, 50 = neutral, in a 0x1268-byte "slider block" (50 sets x 39 sliders x {user, cpu}, plus 2 x 7 positioning and the CPU-AI arrays). In the match, byte x 0.01 is used.
- **Authentic**: when the block is copied into the match, 8 sliders (sprint, acceleration, trap error, pass speed, one more, line length, line width, run frequency) are replaced by the Authentic preset values **only if the byte is exactly 50**. Any other value is kept. So Authentic stays selected and untouched, and a value other than 50 still reaches the match. A value of 50 means "the game decides".
- 65 game variables `GAMEPLAY_CUSTOMIZATION/<NAME>_USER` / `_CPU` exist for the 16 basic sliders, the 7 positioning sliders and the 11 CPU-AI sliders (not for the 25 new FC 27 sliders). They are read in one function with `GetInt(name, current byte)`, the same reader that Turbo's injury overrides already go through. **Two research reports disagree on whether that function's result survives to kick-off**: one calls it the realistic channel for all 11 sliders; the other traced that it is the settings "reset" that runs first in match setup, after which the block is rebuilt from the profile's slider settings, so the overrides would be lost. The injury override that is VERIFIED in a match uses a different, separate read (`_CPUAI` names). One cheap played-match test decides it (section 5, channel A).
- `OVERRIDE_{HOME,AWAY}_{OFFENSE,DEFENSE}_DIFFICULTY` are per-team AI difficulty floats (0 = Beginner .. 1 = Legendary), read after the session's difficulty with the current value as default, so they probably survive (ASSESSED). They are keyed by home/away, not user/CPU.
- `GAME_SPEED` is the gameplay-type mode field, not a speed: Turbo must never set it. `CPUAI_PLAYER_BASED_DIFFICULTY_*` and `ADAPTIVE_DIFFICULTY/*` may be what "Dynamic" is made of: Turbo must not touch them.
- The Attribulator (Live Editor's gameplay-constant API) has no native functions behind it in Live Editor 27.1.2 (VERIFIED in a real-game probe). It is out until a Live Editor update restores them; Phase 0 re-checks on your current version.

### 3.5 "Dynamic Opposition"

No source uses that label. The CPU Sliders tab offers AI Behaviour: Custom / Tactical / Dynamic. Third-party guides describe Dynamic as "use the club's preferred style as a base and adapt during the match". I take your "Dynamic Opposition" to be AI Behaviour: Dynamic [inferred; please confirm with a screenshot of that tab, and whether Authentic greys it out]. It matters because Dynamic may change a CPU tactic Turbo wrote, during the match. The Phase 2 tests record whether the written shape holds to half-time.

---

## 4. FM-style instructions and what FC 27 can honour

This is the translation table the Tactics tab will show. "Side-wide" means the setting applies to the whole User side or the whole CPU side of every match (EA's slider sense), not to one club.

### 4.1 One to one

| FM instruction | FC 27 control | Your club | CPU club |
|---|---|---|---|
| Formation | Formation (one shape) | runtime sheet (channel E) or the game's Edit Tactic | `default_mentalities` row (channel D1) |
| Role + Duty (Defend / Support / Attack) | Role + Focus per slot (Defend, Support, Attack, Balanced, Build-Up, Roaming, Aggressive, Ball-Winning, Versatile, Wide) | channel E | channel D1 |
| Defensive line | Line Height 1-100 (`defensivedepth`) | channel E | channel D1 |
| Attacking transition (Counter / Standard / Patient build-up) | Build-Up Style (Counter / Balanced / Short Passing) | channel E | channel D1 |
| Set-piece takers | taker fields on the team sheet | `cm_teamsheets` (DB, untested) | `teamsheets` / `default_teamsheets` (DB, untested) |
| Manager style | Tactical Vision (`presetid`) | channel E | channel D1 (ASSESSED) |

### 4.2 Approximated only

| FM instruction | Nearest FC 27 lever | Honest note |
|---|---|---|
| Line of engagement, trigger press, counter-press | Defensive Approach band (Deep / Balanced / High / Aggressive) | one 4-step control bundles line, pressing and run tracking |
| Offside trap | Aggressive approach; in-match Quick Tactic (user only) | |
| Team mentality (5 steps) | Tactical Focus in match (user only) or Tactical Vision | no stored equivalent |
| Attacking / defensive width | role choice (Winger vs Inside Forward, Wide focus, Wide Back) + Line Width slider | slider is side-wide |
| Compactness | Line Length slider | side-wide |
| Marking | Marking slider | side-wide |
| Forward runs, overlaps | Run Frequency, Fullback Positioning sliders (plus the roles) | side-wide |
| Tackling (ease off / aggressive) | Defending Aggression, Stand / Slide Tackle, Professional Foul frequency | CPU side only |
| Tempo, passing directness | Build-Up Speed, First-Touch Pass frequency | CPU side only |
| Shots from distance, crossing, dribbling | shot / crossing / dribble / skill-move frequencies | CPU side only |
| Players in the box | Get In Box / Overload Set Pieces Quick Tactics | in match, user only |
| CPU "style families" (Tiki Taka, Possession, Counter Attack, Direct, Crossing, Dribbling, Harass, Italian Defense, No Pressure, ...) | team trait masks in `teams` / `manager` | per club, CPU only; effect under Authentic UNKNOWN (channel D2) |

### 4.3 No equivalent in FC 27 (shown as "not possible", never applied)

Separate in-possession and out-of-possession formations; opposition instructions; per-player instructions beyond role + focus; time wasting; play for set pieces; creative freedom; goal kicks and GK distribution; progress through a flank; supporting runs on one flank; pass reception; patience (work ball into box); pressing trap; prevent short GK distribution; cross engagement; a 5-step mentality ladder.

**Per position, concretely:** in FC 27 a position has a Role and a Focus, and the player in it has his own familiarity (Role+ / Role++). That is all. Per-position sliders for runs, width, roaming, closing down, shooting, crossing (the 2.0 "pos.*" preview sliders) have no control behind them and are removed from the tab in Phase 0, except as FM-side notes in the translation card.

---

## 5. Write channels, evidence and the in-game tests that decide them

Each channel has: what it covers, how it works, evidence, risk, the played-match test, and the decision rule. "Same autosave" means turbo04's autosave before the Napoli v Nott'm Forest fixture; every A/B arm is played from it. Matches are 12-minute halves as today unless the test says otherwise. Authentic and AI Behaviour: Dynamic stay selected throughout.

### Channel A: per-side gameplay sliders through game variables (existing Turbo path)

- **Covers**: the 11 sliders now marked Preview (sprint, acceleration, pass/shot error and speed, trapping, interception, ball control/dribble error, GK ability), the Team Positioning group (marking, line length, line width, line height, run frequency, fullback positioning) and the 11 CPU-AI sliders, each separately for USER and CPU. Not the 25 new FC 27 sliders.
- **How**: add the 65 names to Turbo's known game variables; the existing SetInt-on-the-game-thread call with read-back, `call_gamevar_off.txt`, clear on exit. "Off" clears the variable (never sends 50 as off).
- **Evidence**: names and the reader are VERIFIED in code. Whether the values survive to kick-off is contested (section 3.4): ASSESSED "probably yes" by one report, ASSESSED "probably lost" by another. The proven injury override uses a different read.
- **Your rule**: these overrides replace EA's slider values in memory; the menu stays on Authentic and untouched, exactly as the injury override you already accepted does. [inferred that this counts as "not touching EA's slider menu"; section 9 asks you to confirm]
- **Risk**: process-wide for every played match while set (also an online match if you played one); not saved with the career; values above 100 are not range-checked by the game (Turbo clamps). If the slider loop runs only at boot or mode entry, values set in the hub may only apply after the next match setup.
- **Deciding test (1 match)**: clear all overrides. Set only `GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPU = 100` and `INJURY_SEVERITY_CPU = 100` (the `_CPU` names, not the proven `_CPUAI` ones, not `_USER`). Play the fixture. **Pass**: 2 or more CPU injuries in the first 6 minutes (as the `_CPUAI` test gave on 2026-10-07). **Fail**: none. Read-only cross-check while paused: set `SPRINT_SPEED_USER = 23` and `SPRINT_SPEED_CPU = 77` and search memory for the byte pair `17 4D` repeating at stride 78: more than 50 hits means the block holds the values.
- **Per-slider tests after a pass** (each from the same autosave, CPU side only, extreme value, then the control with the variable cleared; two repeats): sprint + acceleration 0 (CPU players clearly slower in foot races in the first 5 minutes); GK ability 0 (your long shots go in far more than in the control); pass error 100 (CPU pass accuracy in Match Facts at least 15 points lower); line height 100 vs 0 (CPU back line at halfway vs edge of the box during your build-up, radar screenshots at the same minute). One slider counts as **Verified in a played match** only when both repeats show the stated difference.
- **Decision**: pass → Phase 1 ships the sliders as Live with "untested" badges until each one's A/B passes. Fail → channel A is dead for every slider; go to channel B.

### Channel B: write the slider block before the match copy (new game call)

- **Covers**: every slider in the block, including the 25 new FC 27 ones, per side; works with Authentic for any value other than 50 (VERIFIED in code, section 3.4).
- **How**: either patch the enabled bytes in the game-settings block (all 50 sets, user byte / CPU byte) after the profile builder runs and before match init, or, cleaner, call the game's own copy-and-recompute pair in the match on the game thread. Needs new signatures for the running build and locators for three objects. A read-only step first: in a paused match, find both copies of the block, diff them; the differing offsets must be exactly the 8 Authentic-substituted sliders, which also reveals the Authentic preset values.
- **Evidence**: block layout, copy, substitution and recompute are VERIFIED in the old build's code. Nothing is signatured for 6AC07E31 yet.
- **Risk**: a new game call into match code with wrong pointers crashes the game; the pause-menu slider path resends the profile block and would undo the change (reapply after any settings exit). Size L.
- **Test**: the read-only diff first; then CPU sprint = 0 through the call, 3 timed breakaways vs a control; CPU shot error 100 (shots on target ratio vs control); CPU pass error 100 (accuracy drop of 15 points or more).
- **Decision**: only if channel A fails. If the RE needs more than two local sessions, the gameplay sliders stay Preview in 2.1 and channel B moves to 2.2.

### Channel C: per-team AI difficulty (game variables, float)

- **Covers**: CPU attacking and defending strength separately, by home/away: `OVERRIDE_{HOME,AWAY}_{OFFENSE,DEFENSE}_DIFFICULTY`. Also usable as an opposition-variety lever (a weaker club defends at "World Class" and attacks at "Semi-Pro").
- **How**: the existing SetInt gives 0 (Beginner) or 1 (Legendary) only; in-between levels (0.1 / 0.25 / 0.5 / 0.75) need the game's SetFloat (a small new game call; the function is known in the old build). The CPU side is mapped from the next fixture's home/away.
- **Evidence**: ASSESSED (read after the session levels, current value as default).
- **Risk**: overlaps with the in-game difficulty setting; process-wide. `OVERRIDE_MATCH_DIFFICULTY` showed no visible effect on 2026-10-07, so this needs a measured test.
- **Test (2 matches)**: same fixture; match 1 CPU offense = 1 and defense = 1; match 2 both = 0. Compare Match Facts: CPU shots, possession, pass accuracy, your shots on target. Pass: a large gap in the same direction for all four.

### Channel D1: the CPU club's tactic row (`default_mentalities`, DB)

- **Covers**: opponent formation, Build-Up Style, Defensive Approach / line height, Role + Focus per slot. The per-club style lever FC 27 actually has.
- **How**: for the next opponent, write its `default_mentalities` row: `buildupplay`, `defensivedepth` (1..100, clamped to the schema range, not the 7-bit range the probe shows), a formation copied whole from one `formations` row (ids, 11 positions, offsets from `formationoffsets` if the row's are zero, default roles), `pos{n}role` from the decode table. Never `cm_default_mentalities`. Keep the original row in a restore ledger (`turbo_output/opp_db_restore.json`, atomic write, set aside if unreadable) and restore it after the match (date past the fixture), at the next connect if still pending, and when `tactics_off.txt` or `opp_auto_off.txt` appears. If the CPU sheet turns out to be cached for the session, the write only applies after an in-game save + reload; the test below finds out.
- **Evidence**: the table's load/save lifecycle and the match-side queries are VERIFIED in code; that the row is what kick-off uses is ASSESSED.
- **Risk**: save drift if a restore is missed (medium; this is why the ledger is mandatory and the feature stays off until it exists); Dynamic may regenerate or adapt the tactic; a wrong `pos{n}role` value could crash at match load (decode before writing roles; back up the save); player ids in the row may not match a new formation (first pass leaves `playerid0..10` alone and records what happens).
- **Tests**:
  - **D1-formation (decides the channel)**: write Forest's row to a clearly different formation (e.g. 3-5-2 copied from a `formations` row). Go to the pre-match line-up / kick-off team-sheet overlay. Pass: Forest lines up in the new shape AND the shape in play matches (three centre-backs in possession). If not: save in game, reload, check again (the "cached per session" case). If still not: try the legacy `mentalities` row with `activetactic = 1` once. If nothing shows: the CPU formation channel is dead; stop and record.
  - **D1-style (AABB, 4 matches)**: A = `defensivedepth` 10 + the "Counter" build-up value; B = 95 + "Short Passing". Record Forest possession and passes, offsides you commit, and the radar position of Forest's last defender at each of your goal kicks in the first half. Pass: the median defender line moves by at least 15% of pitch length between A and B, and the stats move the same way in both repeats.
  - **D1-roles (2+2 matches)**: Forest's full-backs as Fullback / Defend vs Attacking Wingback / Attack. Pass: their post-match heat maps shift clearly up the pitch in both repeats.
  - **Restore**: after the match, the probe shows the row identical to before (field by field).

### Channel D2: CPU style traits and thresholds (`teams`, `manager`, DB)

- **Covers**: EA's own CPU style families per club, per opponent strength (weak / equal / strong), through `teams.trait1vweak / vequal / vstrong` and the two thresholds; mirrored into `manager.trait1v*` so a manager-driven refresh does not undo them (ASSESSED). Also `teams.buildupplay` / `defensivedepth` (effect UNKNOWN).
- **How**: ordinary DB writes (no hook, no game call), saved with the career unless restored; same restore ledger as D1. Bit order "bit = enum - 1" is inferred from Live Editor's labels and the game's enum table; Phase 0 checks it against well-known clubs.
- **Evidence**: fields and the live read at match build are VERIFIED in code; the effect under Authentic + Dynamic is UNKNOWN.
- **Test (AABB, 4 matches)**: A = all three masks Possession + Tiki Taka; B = Counter Attack + Direct + No Pressure. Pass: Forest possession % and pass count higher in A than B in both repeats, and visibly less pressing in B (fewer tackles/interceptions in your half). Probe before and after confirms the masks were not rewritten by the game.

### Channel E: your club's runtime team sheet (new game call, opt-in)

- **Covers**: your formation, Build-Up Style, Defensive Approach / line height, Role + Focus per slot, preset, active tactic; persisted by the game's own next save.
- **How**: on the game thread, in the career hub only (Team Management closed, no match, no save in progress): resolve the team-sheet service through the service registry, fetch your sheet with the service's own getter, validate everything (team id, 1..5 tactics, 11 slots, every value inside the database ranges), copy, change only the requested fields, call the game's `SetSheet`, read back. Turbo never saves. Kill switches: `tactics_off.txt` plus `call_team_sheet_off.txt`; and it is **opt-in** (`call_team_sheet_on.txt`), like player creation.
- **Evidence**: the service, `SetSheet`, the getters and the record layout are VERIFIED in the old build's code; nothing is signatured for the running build; the side effects of `SetSheet` are not traced.
- **Risk**: highest in this plan. A wrong layout corrupts the runtime sheet, which the game then saves into the career; the Team Management screen keeps its own cached copy and may overwrite the edit. Mitigation: a read-only phase first (Phase 0 locates and reads the record and proves the layout on 6AC07E31 by diffing against the game's own Edit Tactic changes and against `cm_mentalities` after a save); throwaway career only; backups.
- **Tests**: (U1) change Build-Up Style and Defensive Approach through Turbo, open the game's Edit Tactic: it must show the new values. (U2) Play: pause > Team Management shows them; a higher line is visible on the radar at the opponent's goal kicks vs a run from the same save at the other extreme. (U3) Save in game, run the probe: `cm_mentalities` equals the runtime values. (U4) Change one slot's role and focus: Edit Tactic shows it on that player.
- **Fallback if E is not proven in time**: for your own club the Tactics tab is read-only (it shows your tactic as the game has it, from the runtime record, with FM names) and says "change it in the game's Edit Tactic". That is honest and still useful; the game's screen already offers everything FC 27 has for your club.

### Channels not pursued

| Channel | Why not |
|---|---|
| DB writes to `cm_mentalities` / `cm_teamsheets` for your club | overwritten by the runtime sheet at save; never read mid-career (VERIFIED). Removed in Phase 0. |
| Legacy `mentalities` width / box / style fields | the career tactic has no such fields; ASSESSED dead. Demoted in Phase 0; one 2-match check only if you want it (count Forest players in your box at corners with `playersinboxcorner` 0 vs 4; expected: no difference). |
| Attribulator (Live Editor Lua) | natives missing in LE 27.1.2 (VERIFIED). Phase 0 re-checks on your installed version; if present, the ball-radius sanity test (0.365 → 1.0 on the `simulation` database) comes first. |
| EA's profile slider settings (the menu values) | this is editing EA's slider menu, which you ruled out. |
| `GAME_SPEED`, `CPUAI_PLAYER_BASED_DIFFICULTY_*`, `ADAPTIVE_DIFFICULTY/*` | the first switches the gameplay type; the others may be Dynamic itself. Never touched. |
| Managerial line-up AI weights (`MAI_*`) | read from a private store; game variables cannot reach them (VERIFIED). |

### Summary of what each wish needs

| Wish | Channel | State today |
|---|---|---|
| Game settings: injuries, weather, CPU subs | existing game variables | VERIFIED in a played match |
| Game settings: difficulty, time of day | existing game variables | accepted, no effect seen; difficulty may move to channel C |
| Game settings: the 11 gameplay sliders, team positioning, CPU behaviour sliders (per side) | A, else B | 1 deciding match |
| Your club: formation, build-up, approach, role + focus per slot | E (opt-in game call), else read-only + the game's screen | RE + read-only phase first |
| Each position: role + focus | E (yours), D1 (CPU) | needs the `pos{n}role` decode |
| Opposition variety per fixture | D1 + D2 (+ A CPU-side, + C) with a restore ledger | tests decide |
| FM instructions with no FC 27 control | none | shown as "not possible" |

---

## 6. The Tactics tab in 2.1

Same place (Teams > Tactics sub-tab) and the same three columns (scope pills and presets left, pitch centre, cards right). The visual honesty rules of the 2.0 plan stay (Exact / Derived / Modelled tiers, no metres, no outcome language, "What the game will receive" lists only real writes, grey layers when a kill switch is on). What changes:

**Scopes become what FC 27 has.**
- **Match (both sides)**: Conditions (weather, time of day, injuries, CPU subs, difficulty) and the gameplay sliders in the game's own groups (Player speed, Shooting, Passing, Ball control, Goalkeeping, Team positioning, CPU behaviour), each with a User value and a CPU value, default "game decides". Hidden until channel A or B is proven, then shown with the badge the test earned.
- **My team**: Tactical Vision, Formation, Build-Up Style (3), Defensive Approach (4) with Line Height 1-100, and the 11 slots as dots. Read from the runtime record (read-only locator from Phase 0) so it always shows what the game has, no save needed. Apply goes through channel E once proven; before that the Apply button is replaced by "change it in the game's Edit Tactic", and Turbo's job is to show, save as a profile and compare.
- **Position / Role**: click a dot: Role and Focus for that slot (the FC 27 list, grouped by position), plus "the player's familiarity" (Role+ / Role++, the `players.role1..9` editor, clearly named as the player's property). Nothing else, because nothing else exists per position.
- **Opposition**: the per-fixture engine (section 7, Phase 2): next fixture detected by Turbo (not the club selected in the list), the chosen style family and why ("Explain"), what will be written (D1 row, D2 traits, CPU-side A values, C difficulty) and the restore state; "apply automatically per fixture" checkbox; re-roll; pin a club to a preset; "none" mode.

**FM translation card** (new, on My team and Opposition): the FM instruction list from section 4 as the entry point. Picking "Defensive line: higher" moves Line Height; "Counter-press" shows "approximated by Defensive Approach: Aggressive"; "Opposition instructions" shows "not possible in FC 27". Nothing on this card writes anything by itself; it only drives the FC controls and labels them honestly.

**Badges** carry two things, separately: how it is written (Live game variable, DB row, Runtime sheet, Preview) and what was seen (Verified in a played match, with the date and the note; Accepted, untested; No effect seen; Legacy, likely ignored). Apply asks for confirmation when untested or legacy items are enabled. Today's "DB = saved data, in-game effect unverified" wording goes away.

**Removed**: the 7-step Mentality, the legacy width / box / chance / style sliders (hidden behind "show legacy fields", labelled), the `pos.*` movement and attacking sliders, the `cm_mentalities` write targets.

**Presets and profiles** keep the 2.0 store and file. Built-ins are rewritten so each one only sets controls that exist: Authentic baseline (all off), High line / Low block (Line Height), Counter / Short Passing (Build-Up), role sets per formation (from the decode table), and opposition style families. An "apply automatically" option re-sends the active Match profile once per game session (overrides die with the process).

---

## 7. Phases, acceptance tests in played matches, stop rules

Sizes: S under a day, M 1-3 days, L 3-7 days of cloud work. "Local session" = you at the PC: install the build, run what the phase asks, send `turbo_output` (logs, probe files, screenshots). Every phase ends with a cloud build that passes the native suite (and the Lua suite on your PC), is installed by you, and is checked in game before the next phase starts. Each phase can ship as its own 2.1.x release if you want to keep the game current.

### Phase 0: stop misleading, learn the encodings (cloud M, local 1 session, no match needed)

Cloud:
1. Tactics tab honesty fixes (section 6 "Badges" and "Removed"): effect flag with notes per slider; difficulty and time of day marked "accepted, no effect seen"; `cm_mentalities` targets removed; legacy fields hidden and labelled; Mentality and `pos.*` preview sliders removed; `players.role1..9` renamed "role familiarity"; a native test that every Live binding is a known game variable with a compatible range.
2. CPU formation reader fixed to read `default_mentalities` first for CPU clubs.
3. Opposition seed fixed: career id from the game's save UID (published by the Lua bridge), matchday from the fixture, home/away from the fixture rows; "opponent" = next fixture from Turbo's existing fixture reader.
4. Probe extension (Lua, read-only): all `cm_mentalities` fields of your row and your `cm_teamsheets` row; `default_mentalities` and `cm_default_mentalities` rows for your club and the next opponent; rows per team id in both and in `mentalities`; the `formations` row behind `sourceformationid`; `teams` style, trait and threshold fields and `manager.trait1v*` for both clubs; trait masks of 8 well-known clubs (bit-order check); whether `GameplayAttribulatorSetVar` exists in your Live Editor.
5. Runtime record locator (read-only, dev service): find your sheet by the first three line-up ids, read the tactic record and the 11 slots; checked against the probe.
6. Decode procedure for you to run (read-only, about 30 minutes): in Edit Tactic change only one slot's focus, save, probe; only its role, save, probe; cycle Build-Up Style through its 3 options and Defensive Approach through its 4 (save + probe each); change the formation once; after each step Turbo re-reads the runtime record. Output: the value tables for `pos{n}role`, `buildupplay` labels, `defensivedepth` per approach, `presetid` labels, and proof that the runtime layout holds on 6AC07E31.

Acceptance: the tab shows no "Live" badge on anything not verified in a match; probe files present; decode tables complete for at least 8 role/focus pairs; runtime record equals `cm_mentalities` after an in-game save, field for field. Stop rules: if the runtime layout does not match on the running build, channel E (Phase 3) is blocked until it is re-derived; if `pos{n}role` cannot be decoded, roles are left out of D1 and E (formation, build-up and approach still proceed).

### Phase 1: gameplay sliders per side (cloud S for A / L for B; local 1 session for the deciding test, then 1-2 sessions of A/B)

1. Local: the channel A deciding test (one match).
2. Pass: cloud adds the 65 names, converts the 11 Preview sliders and adds the positioning and CPU-behaviour groups, per side, "untested" badges, confirm-on-apply, clear on exit, banner; tests. Local: the four per-slider A/B tests (section 5 A), two repeats each; each slider's badge is set from the result.
3. Fail: cloud starts channel B RE (signatures for 6AC07E31, read-only block diff in a paused match first). Decision after two local sessions: ship B in 2.1 or move it to 2.2 and leave the sliders as Preview.

Acceptance: each shipped slider is Verified in a played match or carries "untested" and is off by default. Stop rule: no slider is shown as working on the strength of a "write accepted" log line.

### Phase 2: opposition per fixture (cloud L, local 2-3 sessions, about 8-12 short matches)

1. Local first (cheap, decides the channel): D1-formation test as written in section 5, with Turbo's current DB write path plus a hand-written row (no new feature code needed for the test itself beyond the probe and the formation copy helper).
2. If the formation shows: cloud builds the per-fixture engine: a pure core module that turns (next fixture, both clubs' facts, active match profile, opposition parameters and rules) into a plan of writes and clears; the restore ledger; style families projected onto `buildupplay`, `defensivedepth`, a formation, slot roles (if decoded), the trait masks, and CPU-side channel A values if Phase 1 passed, plus channel C difficulty if its test passed; driven from the app when the in-game date or next fixture changes (no per-frame memory reads); kill switch `opp_auto_off.txt`; the Opposition scope UI; tests (determinism per fixture, clamps, only the opponent's row touched, never a `_USER` name, restore round trip, crash recovery of a pending restore, nothing written when a kill switch exists).
3. Local: D1-style, D1-roles, D2-traits (section 5), and the restore check after each match. If the game required save + reload to pick up the row, the engine writes at fixture detection and the UI tells you to save and reload before the match; that is recorded as a limitation, not hidden.
4. Channel C test (2 matches) if time allows; otherwise C stays out of 2.1.

Acceptance: a played match against a written formation shows it on the line-up overlay and in play; the style and trait A/B tests pass as defined; the DB is identical before and after a fixture (probe diff); Dynamic's behaviour during the match is recorded (shape held or changed by half-time). Stop rules: D1-formation fails on all three tables → the CPU tactic channel is dead; the Opposition scope keeps only what works (traits if D2 passed, CPU-side sliders if A passed, difficulty if C passed) and says so. No restore ledger → no automatic writes, ever.

### Phase 3: your club through the runtime sheet (cloud L, local 1-2 sessions)

1. Cloud: signatures for the team-sheet service, `SetSheet` and the getters on 6AC07E31 (auto-adapt covered as for the other patterns); a pure core module for the record layout, validation, patch, snapshot and restore over synthetic memory; the host game call with its kill switches and the opt-in file; the My team scope Apply path; UI tests with a fake service (Apply patches only your sheet; a failed validation writes nothing).
2. Local: U1-U4 (section 5 E) on turbo04 with a backup of the save first.

Acceptance: U1 (Edit Tactic shows the change), U2 (a played match shows the line difference), U3 (save → `cm_mentalities` equals the runtime values), U4 (slot role shows on the player). Stop rules: any validation mismatch, any `SetSheet` side effect not understood, or any corruption of the sheet → the call stays opt-in-off and My team ships read-only with the fallback wording. The runtime record read (Phase 0) stays in either case.

### Phase 4: release 2.1.0 (cloud S-M, local 1 session)

Docs (`docs/turbo-reference.md`, `CHANGELOG.md`, `turbo/package/TURBO_README.md` kill-switch table with `opp_auto_off.txt`, `call_team_sheet_off.txt` / `_on.txt`), version bump, GitHub release with the zip, release notes that list verified vs not verified exactly as the evidence file does; final check on your PC: upgrade over 2.0.2, load turbo04, one played match with the shipped defaults (nothing enabled = the game unchanged), kill switches, overrides cleared on exit.

### Order and what can move

Phase 0 first, always. Phases 1 and 2 are independent of each other and of Phase 3; I would run Phase 1's deciding test and Phase 2's formation test in the same local session (both are one match each) and let the results order the rest. If you care most about your own club, Phase 3 moves ahead of Phase 2. Anything not proven by the time you want 2.1 out ships as Preview with its badge, exactly like 2.0 did, and is listed in the release notes under "not verified".

Your time, honestly: Phase 0 about 1-1.5 hours at the PC; Phase 1 one match plus about 8 short matches if A passes; Phase 2 about 8-12 short matches; Phase 3 about 1 hour plus 2 matches. Match time dominates. Tell me if that is too much and I will cut the A/B lists to one slider per group.

---

## 8. Risks

| Risk | Rating | What the plan does about it |
|---|---|---|
| Channel A is dead (the overrides are overwritten before kick-off) | Medium-high (two reports disagree) | One cheap deciding match before any slider code; channel B as the fallback with its own gate |
| CPU tactic is not read from `default_mentalities` or Dynamic rewrites it | Medium | Formation test first (one match), three tables tried, Dynamic's effect recorded; the feature keeps only what passes |
| Save drift from CPU DB writes | Medium | Restore ledger is a precondition for any automatic write; restore on match end, next connect and kill switch; probe diff in every test; throwaway career |
| Runtime sheet corruption (channel E) | High if done carelessly | Read-only phase, layout proven on the running build, validation of every field, opt-in file, kill switch, backup before U1, hub-only, Team Management closed |
| Wrong `pos{n}role` value crashes match load | Medium | Decode first; write only values seen in the game's own rows; roles are optional in D1 and E |
| Process-wide overrides reach an online match | Low (you play offline) | Banner, clear on exit, nothing persisted |
| Title update changes names, offsets or layout | Medium | Every new address is a signature with auto-adapt and instruction checks, like 2.0.2; layout checks fail closed |
| Misreading your "never touch EA's slider menu" | Decision | Section 9 asks; the plan works either way (B and D/E do not use EA's slider values at all) |
| Research addresses are from the old build | Certain | Nothing is called verified until it is re-found on 6AC07E31 and seen in a match |
| Time at the PC | Certain | Phases are independent; each ships on its own; A/B lists can be cut |
| Two research reports contradict each other (channel A; CPU source table) | Known | Both readings are stated; tests, not opinions, decide |

---

## 9. What I need from you

1. **Approval of this plan**, or the changes you want, before any code is written.
2. **Your reading of "never touch EA's slider menu"**: are per-side slider values replaced in memory (menu stays Authentic, untouched, like the injury override) allowed? Yes → channel A/B are in. No → the gameplay sliders stay out and only D, E and C proceed.
3. **Confirmation that "Dynamic Opposition" means AI Behaviour: Dynamic** on the CPU Sliders tab (a screenshot of that tab with Authentic selected answers it and shows whether it is greyed out).
4. **Local sessions** when each phase asks (section 7), on turbo04 with a backup; send `turbo_output` and the screenshots. Nothing else is needed from you for the cloud side.
5. **Priority**: your own club (Phase 3) or opposition variety (Phase 2) first, if you have a preference; otherwise the order in section 7.
6. **Your Live Editor version** (27.1.2 or 27.1.3): decides whether the Attribulator check in Phase 0 is worth a line.
7. **The risk you accept for channel E**: a new, opt-in game call into career code on the throwaway career only. If you would rather not, say so and My team ships read-only.

No credentials, keys or purchases are needed.

---

## 10. Cloud build check (this session)

Filled in from the run started on 2026-10-08; see section 2.4 for the CRLF note.

- `scripts/build_win.sh` (MinGW cross-build): **runs in the cloud** after `apt-get install g++-mingw-w64-x86-64-posix` (Ubuntu's `x86_64-w64-mingw32-g++-posix`, GCC 13.2). Verified 2026-10-08: exit 0, produced `build/win/Turbo.dll` (7.3 MB), `TurboInjector.exe`, `TurboProbe.exe` from HEAD d058e98. Cloud-built binaries are what you install; the only local step is copying the zip.
- `tests/native/run_native.sh`: **refuses to start in the cloud** (exit 2, "Live Editor's Lua libs are missing") because it builds its fixtures with `tests/native/gui_world.lua` on top of the Lua test world, which loads Live Editor's libs. The C++ part compiles (g++ 13, lua5.4 installed). A dry run with that check bypassed was started in this session; its result is recorded in the review-fix commit that follows this one. Until that is settled, the native suite counts as local-only, like the Lua suite.
- Lua suite: not runnable in the cloud (needs `turbo/le27/libs`, local only). Phase 0's Lua probe tests therefore run on your PC with `turbo/tests/run_tests.sh` after install (the same harness as today); in the cloud they are only parsed (`luac -p`), not run. Every phase's local session starts with that suite, and its TOTAL line goes into the evidence folder.

---

## Appendix A: evidence index

| Topic | Where |
|---|---|
| Played-match results | `ingame-results-2026-10-07.md`; `C:\FC 27 Live Editor\turbo_dev\evidence\2026-10-07\RESULTS.md` |
| Career tactic tables and fields | `fc27_tactic_tables_schema.json` (14 tables, row counts); `probe_tactics.txt` on the PC |
| Your club's load/save lifecycle, runtime record layout | research.json `db-tactics`, `runtime-path` (static, build 6AB9813C); `docs/re/realtime_transfers.md`; `scripts/re/realtime_signatures.json` |
| CPU `default_mentalities` lifecycle and match-side queries | research.json `db-tactics` |
| Team traits enum and live read | research.json `db-tactics`, `runtime-path` |
| Gameplay slider game variables, block layout, Authentic substitution, profile builder | research.json `gameplay-sliders`, `turbo-code-map` |
| FC 27 tactic controls, removed controls, roles/focus list, slider menus, AI Behaviour | research.json `fc27-surface` (EA pitch notes FC 25/26/27, Live Editor wiki, guides) |
| Turbo code map (registry, apply path, kill switches, solver, seed) | research.json `turbo-code-map`; `turbogui/src/core/sliders.cpp`, `ui/ui_tactics.cpp`, `core/opposition.cpp`, `core/match_setup.cpp` |
| 2.0 design rules (visual honesty, profiles, architecture) | `docs/TURBO_2_0_PLAN.md` sections 3, 4, 5, 9 |

## Appendix B: open questions the tests answer

1. Does the `GAMEPLAY_CUSTOMIZATION/*_CPU` override reach kick-off? (Phase 1 deciding test)
2. Which table does a career match read for the CPU tactic, and does Dynamic change it mid-match? (Phase 2 formation test)
3. How are role and focus packed in `pos{n}role`; which `buildupplay` value is which label; which `defensivedepth` does each approach write; what `presetid` is each Vision? (Phase 0 decode)
4. Does the runtime record layout hold on build 6AC07E31? (Phase 0 locator)
5. Is the trait bit order "enum - 1", and do traits act under Authentic + Dynamic? (Phase 0 check, Phase 2 D2 test)
6. Do `teams.buildupplay` / `defensivedepth` matter for the CPU side when its sheet also carries them? (Phase 2 D1-style with and without the `teams` values)
7. Do `OVERRIDE_{HOME,AWAY}_{OFFENSE,DEFENSE}_DIFFICULTY` work, and is `OVERRIDE_MATCH_DIFFICULTY` read at all? (Phase 2 step 4)
8. Does your Live Editor expose the Attribulator natives? (Phase 0 probe)
