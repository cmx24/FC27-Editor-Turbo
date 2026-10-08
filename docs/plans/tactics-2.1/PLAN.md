# Turbo 2.1 tactics plan

Written 2026-10-08 in the cloud from the inputs in this folder (research.json, fc27_tactic_tables_schema.json, ingame-results-2026-10-07.md, README.md) and docs/TURBO_2_0_PLAN.md, then reviewed twice (evidence and feasibility; your intent and plain language) and corrected. Nothing in this plan has been implemented. Nothing starts until you approve it.

Labels used for every claim:
- **SEEN IN A PLAYED MATCH**: seen on the pitch or in Match Facts on turbo04 (2026-10-07). This is the only label that counts as "working".
- **VERIFIED**: read in the game's code, the game's database, or Turbo's code. Facts about the game's code come from the image of the previous game build unless stated; the running build is the October 2026 title update (6AC07E31).
- **ASSESSED**: reasoned from evidence, not observed.
- **UNKNOWN**: nobody knows yet; the plan contains a test for it.
- **[inferred]**: something I believe you want but you did not say in those words. Correct me.

Rules this plan follows: everything is coded, built and tested in the cloud; the PC is used only to install and to play the test matches. Nothing is called working or releasable until it is seen in a played match. Turbo never touches EA's slider menu or the Authentic / AI Behaviour settings. Live Editor's own files, EA assets and real player data are never committed.

**One shipping rule for everything new** (used in sections 6, 7 and the release): a control whose write channel passed its deciding match but whose own A/B test has not passed is shown switched off, labelled "untested", and listed under "not verified" in the release notes. Nothing untested is on by default. A control with no proven channel is shown as "preview" (visible, writes nothing) or not at all.

---

## 1. What you asked for

In your words: "a plan to implement what I want the way I want it", for tactical tweaks and settings for the team and for each position, plus game settings.

What I take that to mean, from the 2.0 plan, the README in this folder and the in-game session:

1. **Your club, Football-Manager style.** Set the team's shape and instructions and give every position its own instructions, using FM vocabulary (mentality, line, pressing, roles and duties, runs, width, tempo and so on). [inferred from the 2.0 plan sections 3 and 8, where FM-style sliders were the design]
2. **Per-position control**, not just per team. [stated]
3. **Game settings**: the gameplay sliders (speed, errors, goalkeepers, positioning), injuries, weather, difficulty, substitutions. [stated; the gameplay sliders are the ones 2.0 could not write]
4. **Opposition variety**: CPU teams whose style matches their own tactics and quality, different from match to match. [from the 2.0 plan section 4 and this folder's README]
5. **Authentic Gameplay and "Dynamic Opposition" stay selected**, and Turbo never edits EA's slider menu. [stated in the 2.0 plan; "Dynamic Opposition" is most likely the game's "AI Behaviour: Dynamic", see section 3.6]
6. **Honesty**: a control that does nothing in a played match is not shown as if it did. [stated repeatedly]
7. **Work split**: cloud for code, builds and tests; the PC only to install and to play test matches; you publish nothing yourself. [stated]

### 1.1 In short: what you will and will not get

| Wish | Sure (seen in a played match) | Depends on one test (which) | Not possible in FC 27 |
|---|---|---|---|
| Game settings | injuries (frequency, severity, off), weather, CPU makes no substitutions | the gameplay sliders per side (one deciding match, section 5 A); difficulty and time of day (accepted by the game, no effect seen yet) | nothing; the 25 sliders EA added in FC 27 need the harder channel B |
| Your club | nothing is written today; Turbo shows your saved tactic | formation, Build-Up Style, Defensive Approach / line height and the role + focus of each slot, through a new opt-in game call into the game's live tactic (section 5 E); until that passes, Turbo shows your tactic and you change it in the game's Edit Tactic screen | FM instructions beyond those four things (width, tempo, pressing triggers, per-player instructions, opposition instructions and so on; section 4.3) |
| Each position | nothing today | role + focus per slot (yours through E, the CPU's through D1), once the number that stores "role + focus" is decoded (section 7, Phase 0) | per-position sliders for runs, width, roaming, closing down, shooting, crossing: FC 27 has no such thing |
| Opposition variety | nothing today (only injury and difficulty offsets reach the game) | per fixture: the opponent's formation, build-up, line height and roles (one deciding match, section 5 D1); EA's own CPU style switches per club (section 5 D2); CPU-side gameplay sliders (section 5 A); CPU attack/defence strength (section 5 C) | mid-match adaptation by Turbo; anything the game's "Dynamic" behaviour overrides during the match |

What we do not know yet, and which test answers it, is listed in Appendix B.

Two things I am NOT assuming: that you want Turbo to replace the game's own Edit Tactic screen for your club (for your own club the game's screen already offers everything FC 27 has; Turbo adds FM vocabulary, presets, profiles and automation, see section 6), and that "the way I want it" means the exact 2.0 slider list. Section 4 shows which FM instructions FC 27 can honour; the ones it cannot are listed as such, not approximated silently.

---

## 2. Where we are today (honest status)

### 2.1 SEEN IN A PLAYED MATCH (Turbo 2.0.1 / 2.0.2, turbo04, Napoli v Nott'm Forest, 2026-10-07, 12-minute matches of two 6-minute halves)

| Control | What was seen |
|---|---|
| Weather override | id 3 gave snow on the pitch; id 1 gave "Partly Clear". Read at kick-off: set after the pre-match screen it still applied. |
| Injury frequency and severity 100 (both sides) | Injuries at 1', 3', 5' and later; the game forced substitutions. |
| CPU makes no substitutions | Forest made no substitution for two injured players (observed to 9:36 of the match). |
| Stability | 12 hooks active, several career loads, 3 played matches. No crash dump; the game process of the first session ended between two matches for an unknown reason (no Windows error, possibly closed outside the session); sessions 2 and 3 ran to the end. Overrides do not survive a game restart (by design). |

### 2.2 Verified by Turbo's own read-back, not in the game's screens

| Control | What was seen |
|---|---|
| Squad status roles (Crucial .. Prospect) | Preview reads the game's values; Apply set all 34 and Preview read them back. The game's own screens were not checked. |
| Role re-apply on season reset | Works at handler level only: the reset event was sent by hand from the Lua Engine (roles 1 → 3 for 34 of 34; stays 1 with `role_reapply_off.txt`). No season rollover was played. |

### 2.3 Accepted by the game, no effect seen

| Control | What happened |
|---|---|
| Difficulty (the game's match-difficulty override set to 5) | Write accepted ("ok"); the pre-match line still said "Amateur"; no in-match place shows the level. UNKNOWN whether it does anything. |
| Time of day (0 and 4) | Both showed 5:00 PM daylight. UNKNOWN. |

Both carry the same green "Live" badge as the injury sliders today. That is misleading and is fixed in Phase 0.

### 2.4 Shown in the Tactics tab today but cannot work, or is mislabelled (VERIFIED in Turbo's code)

| Tactics tab item today | Problem |
|---|---|
| "Defensive line depth" and "Build-up play" (My team) write your club's saved tactic row | The game reads that row once when the career loads and overwrites it from its live copy at every save (section 3.2). A Turbo edit is never seen in a match and is gone at the next save. They also write the `teams` style fields, whose match effect is UNKNOWN (section 5, D2). |
| "Defensive width", "Attacking width", "Players in the box" (3), "Chance creation", "Attacking style", "Defensive style" write the old tactic table | EA removed these controls in FC 25; the career's tactic tables have no such fields. ASSESSED: no effect in a career match (section 3.4 says why this is not fully settled). |
| "Mentality" (7 steps, Very defensive .. Very attacking) | FC 27 has no such control. Preview-only today; it should not exist as a team slider. |
| "Role 1..9" under Position/Role | These write the player's own role familiarity (Role+ / Role++), not the role of a slot in the tactic. Real but mislabelled. |
| 11 gameplay sliders (sprint speed, acceleration, pass error, pass speed, shot error, shot speed, shot frequency, first touch error, trapping error, ball control error, goalkeeper ability) | Preview only, no write path (2.0 called them NEEDS-RE). The research found the write-path candidates (section 5, A and B). |
| "Foul strictness" / "Card strictness" | Write the referee table on every referee row (range 0..2, which end is strict is unknown). Never tested in a match. |
| Formation (My team) | Preview only by design in 2.0. The CPU-club pitch can show the wrong formation: the reader never looks at the CPU clubs' tactic table (section 3.3). |
| Opposition card | The solver's output is mostly preview; only injury and difficulty offsets reach the game. The "opponent" is the club selected in the list, not your next fixture. The per-fixture seed uses a Lua load counter and today's date, so the same fixture gets a new profile after a reload or a day advance. |
| Switching a slider off does not restore | Team, tactic-table and referee writes have no undo and no snapshot; only player-field writes have the per-player undo. |

The cloud build check (what runs in the cloud, what needs your PC) is in section 10.

---

## 3. How FC 27 really does tactics

Six words used below:
- **your saved tactic**: the row of your club in the career database (`cm_mentalities`).
- **the game's live tactic**: the copy of your tactic the game keeps in memory while the career runs; it is what matches use.
- **the CPU clubs' tactic table**: one row per CPU club in the career database (`default_mentalities`).
- **the old tactic table**: the table FC 25 stopped using for its removed controls (`mentalities`).
- **the slider block**: the game's own copy of every gameplay slider, one number 0-100 per slider and side.
- **game variable**: a named switch Turbo sets in the game's memory, read by the game at kick-off, gone when the game closes. The injury and weather overrides are game variables.

Evidence: EA's FC 25 "FC IQ" pitch notes, the FC 26 and FC 27 pitch notes, the Edit Tactic screen seen on 2026-10-07 ("4-4-1-1 MIDFIELD", Build-Up Style BALANCED, Defensive Approach DEEP, preset "Standard", a Role / Focus per player), the career tactic tables read from the running game (probe_tactics, this folder's schema file) and the game's code. Code addresses are in Appendix A, not here.

### 3.1 What a team tactic contains (VERIFIED: EA notes + the tables + the Edit Tactic screen)

| Control | Values | Where the career stores it |
|---|---|---|
| Tactical preset ("Tactical Vision" in career: Standard, Possession, Wing Play, Counter Attack, Park the Bus, Kick and Rush, High Pressing; FC 26 also listed Balanced) | one of the list | a preset number in your saved tactic (ASSESSED; which number is which Vision is read in Phase 0). The CPU clubs' tactic table has no preset field. |
| Formation | one of the game's 899 formation rows, with 11 slot positions and x/y offsets | three formation ids plus 11 positions and offsets |
| Build-Up Style | Short Passing / Balanced / Counter | one number 0..3. The game maps the four stored values onto three engine values (two of them collapse into one), so one stored value is a leftover. Which number is which label is read in Phase 0; the reports disagree on the engine order. |
| Defensive Approach | Deep / Balanced / High / Aggressive, each with a Line Height 1-100 inside its band (1-30, 31-60, 61-90, 91-100; EA defaults 25, 50, 70, 95) | only the line height 1..100 is stored. The code maps it to a band of 0..3 (VERIFIED); that this band is the Edit Tactic's Defensive Approach, in that label order, is ASSESSED until Phase 0. |
| Role + Focus for each of the 11 slots | e.g. Falseback / Balanced, Holding / Defend, Wide Midfielder / Support, Advanced Forward / Attack. Focus vocabulary: Defend, Balanced, Support, Attack, Build-Up, Roaming, Aggressive, Ball-Winning, Versatile, Wide | one number per slot holds both role and focus; which number means which pair is UNKNOWN (decoded in Phase 0) |
| Up to 5 tactics per team sheet, one active | | an index and an "active" flag |
| Set-piece takers and corner assignments | captain, penalties, free kicks L/R, corners L/R, long kick, throw-ins, corner support players | the team-sheet tables (yours: part of the live tactic data the game saves; CPU: `default_teamsheets` / `teamsheets`) |

What FC 27 does NOT have for a team any more (EA removed them in FC 25): defensive and attacking width, chance creation, pressure, players in the box, players at corners and free kicks, work rates, per-player instructions, separate in-possession and out-of-possession shapes. The "Without Ball" view on the Edit Tactic screen is derived from the roles; it is not a second formation.

During a match the manager also has Tactical Focus (Default / Attacking / Defending, which moves build-up and approach one step) and Quick Tactics (Offside Trap, Team Press, Overload Set Pieces, Get In Box). These are in-match, user-only buttons; Turbo has no lever for them.

### 3.2 Where your club's tactic lives (VERIFIED in the previous build's code; must be re-checked on the running build)

- The master copy is the game's live tactic (its cached team-sheet service in memory). It is filled from your saved tactic and team sheet once, when the career finishes loading, and written back over those tables at every save.
- The live record holds, per tactic: the active flag, the preset, the line height, the build-up style, the formation ids, and a list of 11 slots each with player, position, x/y offset and the slot's role + focus number. Match setup copies the formation and each slot's role from this record into the match, so slot roles and formation are real match inputs.
- Consequence: the only ways to change your club's tactic are the game's own Edit Tactic screen, or asking the game to change its live copy through the game's own "set team sheet" function (section 5, channel E). Writing your saved tactic row cannot work.

### 3.3 Where a CPU club's tactic lives

- CPU clubs have no saved-tactic rows of the kind your club has (both of those tables hold one row: Napoli). VERIFIED.
- The CPU clubs' tactic table (870 rows; one per club assumed, 841 clubs are connected; Phase 0 counts rows per club) is kept across save and load: at load the game rebuilds it from a backup table, at save it copies it into that backup. A write to it therefore survives save and reload; a write to the backup table is wiped at the next save. VERIFIED.
- Match-side code reads this table by club id, including the 11 slot roles, the build-up style, the line height and the formation. VERIFIED that the query exists; that it is what a career match uses for the opponent is ASSESSED.
- **The three research reports do not agree here.** One found the direct match-side query above. The other two point to the game's older database-backed sheet service, which reads the old tactic table (its row flagged active) and the team-sheet tables, as the probable source of CPU formations. Channel D1 therefore tries the CPU clubs' tactic table first (direct query), the old tactic table second. The old width / box / style fields are "dead" only if the first wins; if the second wins they get the 2-match check in section 5.
- Separately, when a match is built the game reads live from the `teams` table: build-up, line height, two opponent-strength thresholds and three "trait" masks (one each for weak, equal and strong opponents; 23 switches: Tiki Taka, Possession, Counter Attack, Direct, Crossing, Dribbling, Harass, Double Contain, Italian Defense, No Pressure, Defend The Lead, Keep Up Pressure, More Attacking At Home, physical defending styles and others). The traits are EA's own CPU style system: tuning names show they shift the CPU's behaviour sliders. Whether they still act under Authentic + Dynamic is UNKNOWN. Which value wins when `teams` and the club's tactic disagree on build-up or line height is UNKNOWN. Which switch is which style is inferred from Live Editor's labels and the game's own list; Phase 0 checks it against clubs whose style is known.
- Live Editor's own wiki says the CPU never changes a club's formation on its own, which fits a formation coming from a stored row.

### 3.4 Gameplay sliders and how Authentic works

- EA's slider menu has, per side (User / CPU): sprint speed, acceleration, shot and pass error/speed families, header accuracy, trapping, interception and deflection errors, dribble error, tackle accuracy, injuries, goalkeeper ability, and the Team Positioning group (marking, run frequency, line height, line length, line width, fullback positioning). FC 27 added 25 more. The CPU Sliders tab has 16 per CPU Opponent / CPU Teammate (defending aggression, stand/slide tackle and professional foul frequency, build-up speed, shot frequencies, first-touch pass, regular and early crossing, dribble and skill-move frequency). ASSESSED: the names come from guides and today's screenshots; EA confirms only the counts.
- Inside the game every slider is a number 0-100 (50 = neutral) in the slider block, copied into the match at kick-off. VERIFIED.
- **Authentic**: when the block is copied into the match, 8 sliders (sprint, acceleration, trap error, pass speed, one more, line length, line width, run frequency) are replaced by Authentic's own values **only if the number is exactly 50**; any other number is kept. VERIFIED in code. That Authentic is one of the two modes that substitute, and that a kept number changes play, is ASSESSED until tested. So, if a Turbo value reaches the block, Authentic stays selected and untouched and the value still counts; 50 means "the game decides".
- About 65-70 game variables named after the sliders exist (`GAMEPLAY_CUSTOMIZATION/<slider>_USER` and `_CPU`) for the 16 basic sliders, the 7 positioning sliders and the 11 CPU-behaviour sliders; the reports count them differently; the exact list comes from the game's name table. 23 of the 39 block sliders per side (the FC 27 additions) have no variable. They are read with the same reader Turbo's injury override already uses. **Two research reports disagree on whether that read survives to kick-off**: one calls it the realistic channel for all sliders; the other traced that it is the settings reset that runs first in match setup, after which the block is rebuilt from the profile's slider settings, so the values would be lost. The injury override that was seen in a match uses a different, separate read (the `_CPUAI` names). One cheap played-match test decides it (section 5, channel A). For the CPU-behaviour group, `_USER` most likely means "CPU teammates" and `_CPU` "CPU opponent" (ASSESSED), and no in-match reader of that group's values was found in the code at all.
- Four per-team AI difficulty variables exist (home/away x attack/defence, values 0 = Beginner .. 1 = Legendary), read after the session's difficulty with the current value as default, so they probably survive (ASSESSED). They are keyed by home/away, not user/CPU.
- `GAME_SPEED` is the gameplay-type mode field, not a speed (ASSESSED): Turbo must never set it. The "player based difficulty" and "adaptive difficulty" variables may be what "Dynamic" is made of: Turbo must not touch them.
- The Attribulator (Live Editor's gameplay-constant API) had no native functions behind it in Live Editor 27.1.2 (VERIFIED in a real-game probe on 2026-10-02). Phase 0's probe reports your installed version and whether they exist now.

### 3.5 What "write accepted" means

Every game variable Turbo sets is read back and logged as "ok". That proves the switch is stored, nothing more: difficulty and time of day were "ok" and did nothing visible. Only a played match proves an effect. This is why every channel below ends in a match test.

### 3.6 "Dynamic Opposition"

No source uses that label. The CPU Sliders tab offers AI Behaviour: Custom / Tactical / Dynamic. Third-party guides describe Dynamic as "use the club's preferred style as a base and adapt during the match". I take your "Dynamic Opposition" to be AI Behaviour: Dynamic [inferred; say so only if you mean something else]. It matters because Dynamic may change a CPU tactic Turbo wrote, during the match. The Phase 2 tests record whether the written shape holds to half-time. Whether Authentic greys that tab out is checked in Phase 0 from the game's settings data if Turbo can read it; if not, I will ask for one screenshot then.

---

## 4. FM-style instructions and what FC 27 can honour

This is the translation the Tactics tab will show. "Side-wide" means the setting applies to the whole User side or the whole CPU side of every match (EA's slider sense), not to one club.

### 4.1 One to one

| FM instruction | FC 27 control | Your club | CPU club |
|---|---|---|---|
| Formation | Formation (one shape) | the game's live tactic (channel E) or the game's Edit Tactic | the CPU clubs' tactic table (channel D1) |
| Role + Duty (Defend / Support / Attack) | Role + Focus per slot (table 4.4) | channel E | channel D1 |
| Defensive line | Line Height 1-100 | channel E | channel D1 |
| Attacking transition (Counter / Standard / Patient build-up) | Build-Up Style (Counter / Balanced / Short Passing) | channel E | channel D1 |
| Set-piece takers | taker fields on the team sheet | only through channel E if the takers are part of the live record Turbo can reach (the mapped part covers players and tactics, not takers: UNKNOWN); otherwise the game's screen | `default_teamsheets` / `teamsheets` (DB, untested; which one a match reads is UNKNOWN) |
| Manager style | Tactical Vision | channel E (preset number, after Phase 0 reads the labels) | not possible: the CPU clubs' tactic table has no preset field |

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
| Tackling (ease off / aggressive) | Defending Aggression, Stand / Slide Tackle, Professional Foul frequency | CPU side only; no in-match reader found yet (section 3.4) |
| Tempo, passing directness | Build-Up Speed, First-Touch Pass frequency | CPU side only; same caveat |
| Shots from distance, crossing, dribbling | shot / crossing / dribble / skill-move frequencies | CPU side only; same caveat |
| Players in the box | Get In Box / Overload Set Pieces Quick Tactics | in match, user only |
| CPU "style families" (Tiki Taka, Possession, Counter Attack, Direct, Crossing, Dribbling, Harass, Italian Defense, No Pressure, ...) | the trait switches in `teams` / `manager` | per club, CPU only; effect under Authentic UNKNOWN (channel D2) |

### 4.3 No equivalent in FC 27 (shown as "not possible", never applied)

Separate in-possession and out-of-possession formations; opposition instructions; per-player instructions beyond role + focus; time wasting; play for set pieces; creative freedom; goal kicks and GK distribution; progress through a flank; supporting runs on one flank; pass reception; patience (work ball into box); pressing trap; prevent short GK distribution; cross engagement; a 5-step mentality ladder.

**Per position, concretely:** in FC 27 a position has a Role and a Focus, and the player in it has his own familiarity (Role+ / Role++). That is all. Per-position sliders for runs, width, roaming, closing down, shooting, crossing (the 2.0 "pos.*" preview sliders) have no control behind them and are removed from the tab in Phase 0, except as notes in the translation card.

### 4.4 FM roles to FC 27 roles (draft; Phase 0 finalises it against the game's own list)

The FC 27 role list below is ASSESSED from FC 26 sources and the four roles seen on screen; Phase 0 reads the real list from the game. "≈" means the nearest FC 27 role, not the same thing. The position card accepts the FM name and shows the FC 27 pair it becomes.

| FM role | FC 27 role / focus |
|---|---|
| Goalkeeper | Goalkeeper / Defend or Balanced |
| Sweeper Keeper | Sweeper Keeper / Balanced or Build-Up |
| Full-Back (Defend / Support) | Fullback / Defend or Balanced |
| Wing-Back; Complete Wing-Back (Attack) | Wingback; Attacking Wingback / Support or Attack |
| Inverted Wing-Back | Inverted Wingback / Build-Up or Attack |
| Inverted Full-Back | ≈ Falseback / Defend or Balanced |
| Central Defender (Defend); No-Nonsense CB | Defender / Defend |
| Ball-Playing Defender | Ball-Playing Defender / Defend or Build-Up |
| Stopper (Cover) | Stopper / Balanced or Aggressive |
| Wide Centre-Back | Wide Back / Defend, Support or Aggressive |
| Libero | not possible |
| Anchor; Half-Back | Holding (CDM) / Defend |
| Defensive Midfielder (Defend) | Holding / Defend; Centre-Half / Defend |
| Ball-Winning Midfielder | Holding / Ball-Winning or Box-to-Box / Ball-Winning |
| Deep-Lying Playmaker | Deep-Lying Playmaker / Defend, Roaming or Build-Up |
| Regista | ≈ Deep-Lying Playmaker / Roaming |
| Box-to-Box Midfielder | Box-to-Box / Balanced |
| Mezzala; Carrilero | Half-Winger / Attack; ≈ Half-Winger / Balanced |
| Segundo Volante | ≈ Box Crasher / Balanced |
| Central Midfielder (Attack); Advanced Playmaker | Playmaker / Attack |
| Attacking Midfielder; Enganche | Classic 10 / Versatile or Attack; Playmaker (CAM) |
| Trequartista | ≈ Playmaker / Roaming or Classic 10 / Attack |
| Shadow Striker | Shadow Striker / Attack |
| Winger | Winger / Balanced or Attack |
| Inverted Winger; Inside Forward; Raumdeuter | Inside Forward / Balanced, Attack or Roaming |
| Wide Playmaker | Wide Playmaker |
| Defensive Winger; Wide Midfielder | Wide Midfielder / Defend, Support or Build-Up |
| Wide Target Forward | not possible |
| Advanced Forward; Pressing Forward | Advanced Forward / Attack or Versatile |
| Poacher | Poacher |
| False Nine; Deep-Lying Forward | False 9 / Build-Up or Attack |
| Target Forward; Complete Forward | Target Forward / Balanced, Attack or Wide; ≈ Advanced Forward / Versatile |

---

## 5. Write channels, evidence and the in-game tests that decide them

Each channel has: what it covers, how it works, evidence, risk, the played-match test, and the decision rule. "Same autosave" means turbo04's autosave before the Napoli v Nott'm Forest fixture; every A/B arm is played from it. Matches are 12-minute matches (two 6-minute halves) as on 2026-10-07, about 25 minutes each with loading and Match Facts. Authentic and AI Behaviour: Dynamic stay selected throughout. Everything a test needs (a button, a name, a row writer) is built in the cloud and installed before the local session; you only press buttons and play.

### Channel A: per-side gameplay sliders through game variables (Turbo's existing path)

- **Covers**: the 11 sliders now preview-only, the Team Positioning group (marking, line length, line width, line height, run frequency, fullback positioning) and the 11 CPU-behaviour sliders, each for the User side and the CPU side (for the CPU-behaviour group: CPU teammates and CPU opponent). Not the 25 FC 27 additions.
- **How**: add the names to the list of game variables Turbo knows; sent the same way as today's injury override, same off-switch file (`call_gamevar_off.txt`), cleared when the game closes. "Off" clears the variable (never sends 50 as off).
- **Evidence**: the names and the reader are VERIFIED in code. Whether the values survive to kick-off is contested (section 3.4). The injury override that was seen in a match uses a different read.
- **Your rule**: these overrides replace EA's slider values in memory; the menu stays on Authentic and untouched, exactly as the injury override you already accepted does. One research report nevertheless reads any in-memory replacement of slider values as "driving EA's slider menu". Section 9 asks you; the default is "allowed" [inferred from your acceptance of the injury override and of the 2.0 design].
- **Risk**: process-wide for every played match while set (an online match too, if you played one); not saved with the career; Turbo clamps to 0..100 because the game does not. If the read runs only at boot or mode entry, values set in the hub may apply only after the next match setup.
- **Before the test (cloud, small)**: Turbo 2.0.2 refuses any variable it does not know, and it knows only the 9 current ones. Phase 1 step 0 adds the test names (CPU injury `_CPU` names and sprint speed both sides) to the Match setup panel and a read-only "Check slider block" button on the Status tab, then you install that build.
- **Deciding test (1-2 matches)**: play one match with no overrides as the baseline. Then clear all overrides and set only `INJURY_FREQUENCY_CPU = 100` and `INJURY_SEVERITY_CPU = 100` (the `_CPU` names, not the proven `_CPUAI` ones, not `_USER`). **Pass**: 2 or more CPU injuries in the first 6 minutes (as the proven names gave). **1 injury**: repeat once from the same autosave; pass if either run gives 2 or more. **Fail**: none in both. Read-only cross-check while paused: with sprint speed set to 23 (user) and 77 (CPU), press "Check slider block": Turbo searches memory for that pair repeating every 78 bytes and logs the count; about 50 hits in one region (one copy of the block) or about 100 over both copies means the block holds the values; a handful means it does not.
- **Per-slider tests after a pass** (each from the same autosave, CPU side only, extreme value, two repeats; one shared no-override control pair serves all sliders): sprint + acceleration 0: time the same CPU forward's run along the touchline from halfway to the edge of the box in a replay, 3 runs per match, pass = more than 15% slower than the control in both repeats; goalkeeper ability 0: goals per shot on target over at least 8 of your shots, pass = clearly higher than the control in both repeats; pass error 100: CPU pass accuracy in Match Facts at least 15 points lower than the control in both repeats; line height 100 vs 0: the CPU back line at halfway vs at the edge of the box during your build-up, radar screenshots at the same minute, pass = the difference is obvious in both repeats. A slider counts as **seen in a played match** only when both repeats show the stated difference.
- **Decision**: pass → Phase 1 ships the basic and positioning sliders under the shipping rule. Fail → dead for the 16 basic sliders; the positioning group gets its own one-match check (line height 100 vs 0) because its part of the block may be rebuilt differently; the CPU-behaviour group stays preview until a reader of its values is found in the code. Then channel B.

### Channel B: write the slider block itself (new game call)

- **Covers**: every slider in the block, including the 25 FC 27 additions, per side; keeps Authentic selected (section 3.4).
- **How**: either write the enabled numbers into the game's settings copy of the block after the game has built it and before match start, or, cleaner, ask the game in the match to run its own "copy the block and recompute" step with Turbo's numbers patched in. Needs three new signatures on the running build and locators for the objects. One research report treats the first option as editing EA's slider menu (it rewrites the same numbers the menu holds); your answer to section 9 question 2 covers B as well; the second option is further from the menu.
- **Read-only step first**: in a paused match the "Check slider block" button finds both copies of the block and diffs them; the differing entries must be exactly the 8 Authentic-substituted sliders, which also reveals Authentic's own values.
- **Evidence**: block layout, copy, substitution and recompute are VERIFIED in the previous build's code. Nothing is signatured for the running build yet; that needs a memory image of the running game (section 9, item 4).
- **Risk**: a new game call into match code with wrong pointers crashes the game; the pause-menu slider path resends the profile block and would undo the change (reapply after any settings exit). Cloud size L.
- **Test**: the read-only diff first; then CPU sprint = 0 through the call, 3 timed runs vs the control; CPU shot error 100 (shots on target ratio vs control); CPU pass error 100 (accuracy drop of 15 points or more).
- **Decision**: only if channel A fails. If the research on the memory image does not reach a safe call within the Phase 1 budget, the gameplay sliders stay preview in 2.1 and channel B moves to 2.2.

### Channel C: per-team AI difficulty (game variables, decimal values)

- **Covers**: CPU attacking and defending strength separately, by home/away. Also an opposition-variety lever (a weaker club defends at "World Class" and attacks at "Semi-Pro").
- **How**: Turbo's current call sets whole numbers only, so 0 (Beginner) or 1 (Legendary); in-between levels need the game's decimal setter (a small new game call, known in the previous build). The CPU side is mapped from the next fixture's home/away.
- **Evidence**: ASSESSED (read after the session levels, current value as default).
- **Risk**: overlaps with the in-game difficulty setting; process-wide. The match-difficulty override showed no visible effect on 2026-10-07, so this needs a measured test.
- **Test (4 matches: setting A twice, setting B twice)**: A = CPU attack 1 and defence 1; B = both 0. Pass: in both repeats, CPU shots differ by 4 or more, possession by 8 points or more, CPU pass accuracy by 8 points or more, all in the same direction.
- **Decision**: in 2.1 only if Phase 2 has time left; otherwise 2.2.

### Channel D1: the CPU club's tactic row (database)

- **Covers**: opponent formation, Build-Up Style, Defensive Approach / line height, Role + Focus per slot. The per-club style lever FC 27 actually has.
- **How**: for the next opponent, write its row in the CPU clubs' tactic table: build-up, line height (kept within the game's own 1-100 range), a formation copied whole from one of the game's formation rows (ids, 11 positions, offsets, default roles), slot roles from the decode table. Never the backup table. Before any write the original row goes into a restore file (`turbo_output/opp_db_restore.json`, written so that a crash cannot leave it half done); the row is restored on the first career event after the match (fallback: the next day change), at the next connect if still pending, and when `tactics_off.txt` or `opp_auto_off.txt` appears. The post-match autosave carries the edited row until the save after the restore; that is recorded, not hidden. If the game turns out to cache the CPU sheet for the session, the write applies only after an in-game save + reload; the test finds out.
- **Evidence**: the table's save/load lifecycle and the match-side query are VERIFIED in code; that the row is what kick-off uses is ASSESSED (section 3.3).
- **Risk**: save drift if a restore is missed (medium; the restore file is a precondition for any automatic write); Dynamic may regenerate or adapt the tactic; a wrong role number could crash at match load (decode first; write only numbers seen in the game's own rows; back up the save); the row's player ids may not match a new formation (first pass leaves them alone and records what happens).
- **Before the test (cloud, medium)**: Turbo 2.0.2 cannot write this table at all (its writer resolves only five tables). Phase 2 step 0 adds the row writer with the restore file, the whole-formation copy, and a one-click test button "Give the next opponent formation X" that backs the row up first; you install that build.
- **Tests**:
  - **D1-formation (decides the channel; 1-3 matches)**: press the button with a clearly different formation (e.g. 3-5-2). Go to the pre-match line-up / kick-off team-sheet overlay. Pass: Forest lines up in the new shape AND the shape in play matches (three centre-backs in possession). If not: save in game, reload, check again (the "cached per session" case). If still not: the button writes the old tactic table's active row instead, once. If nothing shows: the CPU formation channel is dead; stop and record.
  - **D1-style (4 matches: A twice, B twice)**: A = line height 10 + the "Counter" build-up value; B = 95 + "Short Passing". Record Forest possession and passes, offsides you commit, and the radar position of Forest's last defender at each of your goal kicks in the first half. Pass: the typical position of that defender moves by at least 15% of pitch length between A and B, and the stats move the same way in both repeats. A second pair with the `teams` build-up and line height left alone vs set the same way tells which value wins (Appendix B, 6) if time allows.
  - **D1-roles (4 matches)**: Forest's full-backs as Fullback / Defend vs Attacking Wingback / Attack. Pass: their post-match heat maps shift clearly up the pitch in both repeats.
  - **Restore**: after each match, the probe shows the row identical to before, field by field.

### Channel D2: CPU style traits and thresholds (database)

- **Covers**: EA's own CPU style families per club, per opponent strength (weak / equal / strong), through the three trait masks and the two thresholds in `teams`; mirrored into `manager` so a manager-driven refresh does not undo them (ASSESSED). Also the `teams` build-up and line height (effect UNKNOWN).
- **How**: ordinary database writes (no hook, no game call), saved with the career unless restored; same restore file as D1. Which switch means which style is checked in Phase 0 against clubs whose style is known.
- **Evidence**: the fields and the live read at match build are VERIFIED in code; the effect under Authentic + Dynamic is UNKNOWN.
- **Test (4 matches: A twice, B twice)**: A = all three masks Possession + Tiki Taka; B = Counter Attack + Direct + No Pressure. Pass: Forest possession % and pass count higher in A than B in both repeats, and visibly less pressing in B (fewer tackles/interceptions in your half). The probe before and after confirms the game did not rewrite the masks.

### Channel E: your club's live tactic (new game call, opt-in)

- **Covers**: your formation, Build-Up Style, Defensive Approach / line height, Role + Focus per slot, preset, active tactic; persisted by the game's own next save.
- **How**: on the game thread, in the career hub only (Team Management closed, no match, no save in progress): ask the game for its live copy of your tactic the way its own screens do, check everything (your club, 1..5 tactics, 11 slots, every value inside the database ranges), change only the requested fields, hand it back through the game's own set-team-sheet function, read it back. Turbo never saves. Kill switches: `tactics_off.txt` plus `call_team_sheet_off.txt`; and it is **opt-in** (`call_team_sheet_on.txt`), like player creation.
- **Evidence**: the service, the set function and the record layout are VERIFIED in the previous build's code; three candidate "get a club's sheet" functions are known and which one is right is UNKNOWN; nothing is signatured for the running build; the side effects of the set function are not traced. All of this needs the memory image (section 9, item 4).
- **Risk**: highest in this plan. A wrong layout corrupts the live tactic, which the game then saves into the career; the Team Management screen keeps its own cached copy and may overwrite the edit. Mitigation: a read-only phase first (Phase 0 locates and reads the record and proves the layout on the running build by diffing against the game's own Edit Tactic changes and against your saved tactic after a save); throwaway career only; backups; if the record does not look exactly as expected, Turbo writes nothing.
- **Tests**: (U1) change Build-Up Style and Defensive Approach through Turbo, open the game's Edit Tactic: it must show the new values. (U2) Play: pause > Team Management shows them; a higher line is visible on the radar at the opponent's goal kicks vs a run from the same save at the other extreme. (U3) Save in game, run the probe: your saved tactic equals the live values. (U4) Change one slot's role and focus: Edit Tactic shows it on that player.
- **Fallback if E is not proven in time**: for your own club the Tactics tab is read-only (it shows your tactic with FM names, from your saved tactic "as of the last save", or live once Phase 3's read works) and says "change it in the game's Edit Tactic". That is honest and still useful; the game's screen already offers everything FC 27 has for your club.

### Channels not pursued

| Channel | Why not |
|---|---|
| Writing your saved tactic / team sheet rows | overwritten by the live tactic at save; never read mid-career (VERIFIED). Removed in Phase 0. |
| The old tactic table's width / box / style fields | the career tactic has no such fields; ASSESSED dead if D1's first table wins. One 2-match check (count Forest players in your box at corners with the "players in box at corners" value 0 vs 4; expected: no difference) only if D1 shows the old table is the one read. |
| Attribulator (Live Editor Lua) | natives missing in LE 27.1.2 (VERIFIED). Phase 0's probe re-checks on your installed version; if present, the ball-radius sanity test (0.365 → 1.0 on the `simulation` database) comes first. |
| EA's profile slider settings (the menu values) | this is editing EA's slider menu, which you ruled out. |
| `GAME_SPEED`, "player based difficulty", "adaptive difficulty" | the first switches the gameplay type; the others may be Dynamic itself. Never touched. |
| The CPU line-up AI's weights | read from a private store; game variables cannot reach them (VERIFIED). |

### Summary of what each wish needs

| Wish | Channel | State today |
|---|---|---|
| Game settings: injuries, weather, CPU subs | existing game variables | SEEN IN A PLAYED MATCH |
| Game settings: difficulty, time of day | existing game variables | accepted, no effect seen; difficulty may move to channel C |
| Game settings: the 11 gameplay sliders, team positioning, CPU behaviour (per side) | A, else B | 1-2 deciding matches |
| Your club: formation, build-up, approach, role + focus per slot | E (opt-in game call), else read-only + the game's screen | memory image + read-only phase first |
| Each position: role + focus | E (yours), D1 (CPU) | needs the role + focus decode |
| Opposition variety per fixture | D1 + D2 (+ A CPU side, + C) with the restore file | 1-3 deciding matches |
| FM instructions with no FC 27 control | none | shown as "not possible" |

---

## 6. The Tactics tab in 2.1

Same place (Teams > Tactics sub-tab) and the same three columns (scope pills and presets left, pitch centre, cards right). The visual honesty rules of the 2.0 plan stay (Exact / Derived / Modelled tiers, no metres, no outcome language, "What the game will receive" lists only real writes, grey layers when a kill switch is on). What changes:

**Scopes become what FC 27 has.**
- **Match (User side and CPU side)**: Conditions (weather, time of day, injuries, CPU subs, difficulty) and the gameplay sliders in the game's own groups (Player speed, Shooting, Passing, Ball control, Goalkeeping, Team positioning, CPU behaviour), each with a User value and a CPU value (CPU behaviour: CPU teammate and CPU opponent), default "game decides". Shown only after channel A or B passed its deciding match, under the shipping rule.
- **My team**: Tactical Vision, Formation, Build-Up Style (3), Defensive Approach (4) with Line Height 1-100, and the 11 slots as dots. Until Phase 3's live read works, the values come from your saved tactic and are labelled "as of your last save"; after it, they are live. Apply goes through channel E once proven; before that the Apply button is replaced by "change it in the game's Edit Tactic", and Turbo's job is to show, save as a profile and compare.
- **Position / Role**: click a dot: Role and Focus for that slot (the FC 27 list, grouped by position; FM names accepted through table 4.4), plus "the player's familiarity" (Role+ / Role++, the existing role editor, clearly named as the player's property). Nothing else, because nothing else exists per position. Your own slots are shown, not edited, until Phase 3 passes.
- **Opposition**: the per-fixture engine (section 7, Phase 2): next fixture detected by Turbo (not the club selected in the list), the chosen style family and why ("Explain"), what will be written (D1 row, D2 traits, CPU-side A values, C difficulty) and the restore state; "apply automatically per fixture" checkbox; re-roll; pin a club to a preset; "none" mode.

**FM translation card** (new, on My team and Opposition): the FM instruction list from section 4 as the entry point. Picking "Defensive line: higher" moves Line Height; "Counter-press" shows "approximated by Defensive Approach: Aggressive"; "Opposition instructions" shows "not possible in FC 27". Nothing on this card writes anything by itself; it only drives the FC controls and labels them honestly.

**Badges** carry two things, separately: how it is written (game variable, database row, live tactic, preview) and what was seen (seen in a played match, with the date and the note; accepted, untested; no effect seen; legacy, likely ignored). Apply asks for confirmation when untested or legacy items are enabled. Today's "DB = saved data, in-game effect unverified" wording goes away.

**Removed**: the 7-step Mentality, the old width / box / chance / style sliders (hidden behind "show legacy fields", labelled), the "pos.*" movement and attacking sliders, the writes to your saved tactic row.

**Presets and profiles** keep the 2.0 store and file (`turbo_output\tactic_profiles.json`). Your saved profiles still load; controls that no longer exist are ignored and listed once as "no longer exists in FC 27"; nothing is deleted. Built-ins are rewritten so each one only sets controls that exist: Authentic baseline (all off), High line / Low block (Line Height), Counter / Short Passing (Build-Up), role sets per formation (from the decode table), and opposition style families. An "apply automatically" option re-sends the active Match profile once per game session (overrides die with the process).

---

## 7. Phases, acceptance tests in played matches, stop rules

Sizes: S under a day, M 1-3 days, L 3-7 days of cloud work. Every phase ends with a cloud build that passes the test suites (section 10 says which run where), published by me as a pre-release zip on GitHub. **Your part of each phase**: unzip it over the Live Editor folder (TURBO_README, "Install", step 2), start the game through Live Editor, load turbo04 (backup first), press the buttons and play the matches the phase lists, then attach to the chat: `turbo_output\turbo_gui.log`, `turbo_output\turbo_boot.log`, the `probe_*` files and your screenshots. Nothing else. Each phase can ship as its own 2.1.x release if you want to keep the game current.

### Phase 0: stop misleading, learn the encodings (cloud M, local about 1.5 hours, no match needed)

Cloud:
1. Build hygiene: the one-line Linux compile fix in the native test suite (section 10), so the suite runs in the cloud once Live Editor's libs are uploaded.
2. Tactics tab honesty fixes (section 6 "Badges" and "Removed"): effect flag with notes per slider; difficulty and time of day marked "accepted, no effect seen"; the saved-tactic write targets removed; legacy fields hidden and labelled; Mentality and "pos.*" preview sliders removed; the role editor renamed "role familiarity"; a native test that every live binding is a known game variable with a compatible range.
3. CPU formation reader fixed to read the CPU clubs' tactic table first for CPU clubs.
4. Opposition seed fixed: career id from the game's save id (the bridge publishes it; today it publishes only a load counter), matchday from the fixture, home/away from the fixture rows; "opponent" = next fixture from Turbo's existing fixture reader.
5. Probe extension (Lua, read-only): all fields of your saved tactic and team sheet; the CPU clubs' tactic table and its backup for your club and the next opponent; rows per club in both and in the old tactic table; the formation row behind your tactic; the `teams` style, trait and threshold fields and `manager` traits for both clubs; the trait masks of 8 well-known clubs (which switch is which style); your Live Editor version and whether the Attribulator functions exist; whether the AI Behaviour setting is readable.
6. Decode in the cloud first: the game's role + focus vocabulary (its own strings), the code that reads slot roles, and the 870 CPU rows (for example, every goalkeeper slot should share one role family) give a first table for the role + focus number, the build-up values and the Vision numbers. Only the gaps go to you.
7. Live-record locator (read-only, for the decode session only): Turbo finds your live tactic by your first three line-up players and reads the record and the 11 slots; it fails closed when those players move. The always-current My team view needs Phase 3's proper read.
8. Your confirmation (read-only, about 1 hour): in Edit Tactic change role, then focus, across 6-8 combinations on 2-3 slots; cycle Build-Up Style through its 3 options, Defensive Approach through its 4 and Tactical Vision through its options; after each change Turbo re-reads the live record (no save needed); save once at the end so the probe can prove the live layout against your saved tactic.

Acceptance: the tab shows no "seen in a match" badge on anything that was not; probe files present; decode tables complete for at least 8 role/focus pairs, all build-up and approach values and the Vision numbers; the live record equals your saved tactic after the final save, field for field. Stop rules: if the live layout does not match on the running build, channel E (Phase 3) is blocked until it is re-derived from the memory image; if the role + focus number cannot be decoded, roles are left out of D1 and E (formation, build-up and approach still proceed).

### Phase 1: gameplay sliders per side (cloud M for A, L for B; local 2-3 hours)

0. Cloud (S): the test names in the Match setup panel and the "Check slider block" button; pre-release; you install.
1. Local: the baseline match, then the channel A deciding test (section 5 A): 2-3 matches, about 1 hour.
2. Pass: cloud adds the full name list, converts the 11 preview sliders and adds the positioning and CPU-behaviour groups per side, badges, confirm-on-apply, clear on exit, banner; tests. Local: the per-slider tests. **Default (cut) list**: sprint + acceleration and line height only, two repeats each plus the shared control pair = 6 matches, about 2.5 hours. **Full list** (goalkeeper ability and pass error too) = 10 matches, about 4 hours, only if you ask for it. Each slider's badge is set from its result.
3. Fail: cloud starts channel B on the memory image (signatures on the running build, the read-only block diff first). Decision at the end of the Phase 1 budget: ship B in 2.1 or move it to 2.2 and leave the sliders as preview.

Acceptance: each shipped slider was seen in a played match or carries "untested" and is off by default. Stop rule: no slider is shown as working on the strength of a "write accepted" log line.

### Phase 2: opposition per fixture (cloud L, local 3-7 hours)

0. Cloud (M): the CPU tactic row writer with the restore file, the whole-formation copy, the "Give the next opponent formation X" button, the probe diff; pre-release; you install.
1. Local (decides the channel): D1-formation, 1-3 matches, about 1 hour.
2. If the formation shows: cloud builds the per-fixture engine: the part of Turbo that decides what to write and undo for the next fixture, tested in the cloud without the game; style families projected onto build-up, line height, a formation, slot roles (if decoded), the trait masks, and CPU-side channel A values if Phase 1 passed, plus channel C difficulty if its test passed; driven from Turbo when the in-game date or next fixture changes; kill switch `opp_auto_off.txt`; the Opposition scope UI; tests (same result for the same fixture every time, clamps, only the opponent's row touched, never a User-side name, restore round trip, a pending restore survives a crash, nothing written when a kill switch exists).
3. Local: **Default (cut) list**: D1-style once (A, B = 2 matches) and D2-traits once (2 matches), plus the restore check after each = 4 matches, about 2 hours. **Full list**: D1-style twice, D1-roles, D2 twice, channel C = 14 matches, about 6 hours, only if you ask for it. If the game required save + reload to pick up the row, the engine writes at fixture detection and the UI tells you to save and reload before the match; that is recorded as a limitation, not hidden.

Acceptance: a played match against a written formation shows it on the line-up overlay and in play; the style and trait tests pass as defined; the row is identical before and after a fixture (probe diff); Dynamic's behaviour during the match is recorded (shape held or changed by half-time). Stop rules: D1-formation fails on the CPU clubs' tactic table (direct and after reload) and on the old tactic table → the CPU tactic channel is dead; the Opposition scope keeps only what works (traits if D2 passed, CPU-side sliders if A passed, difficulty if C passed) and says so. No restore file → no automatic writes, ever.

### Phase 3: your club through the live tactic (cloud L, local about 1.5 hours)

1. Cloud: find the three game functions (the sheet service, its getter, the set function) on the running build from the memory image, with the same auto-adapt and instruction checks as the other patterns; test the record handling on a copy of the record in the cloud; the host game call with its kill switches and the opt-in file; the My team Apply path; UI tests against a stand-in for the game (Apply changes only your sheet; a failed check writes nothing).
2. Local: U1-U4 (section 5 E) on turbo04 with a backup of the save first; 2 matches.

Acceptance: U1 (Edit Tactic shows the change), U2 (a played match shows the line difference), U3 (save → your saved tactic equals the live values), U4 (slot role shows on the player). Stop rules: any check mismatch, any side effect of the set function not understood, or any corruption of the sheet → the call stays opt-in-off and My team ships read-only with the fallback wording. The live read stays in either case.

### Phase 4: release 2.1.0 (cloud S-M, local about 45 minutes)

Docs (`docs/turbo-reference.md`, `CHANGELOG.md`, `turbo/package/TURBO_README.md` kill-switch table with `opp_auto_off.txt`, `call_team_sheet_off.txt` / `_on.txt`), version bump, GitHub release with the zip, release notes that list seen-in-a-match vs not verified exactly as the evidence file does; final check on your PC: upgrade over 2.0.2, load turbo04, one played match with the shipped defaults (nothing enabled = the game unchanged), kill switches, overrides cleared on exit.

### Order and what can move

Phase 0 first, always. Phases 1 and 2 are independent of each other and of Phase 3; I would run Phase 1's deciding test and Phase 2's formation test in the same local session (both are one to three matches) and let the results order the rest. If you care most about your own club, Phase 3 moves ahead of Phase 2. Anything not proven by the time you want 2.1 out ships under the shipping rule and is listed in the release notes under "not verified".

Your time at the PC, honestly, with the cut lists: Phase 0 about 1.5 hours; Phase 1 about 3.5 hours (1 hour deciding, 2.5 hours sliders); Phase 2 about 3 hours; Phase 3 about 1.5 hours; Phase 4 about 45 minutes. About 10 hours in total over several sessions; about 18 hours with the full lists. Match time dominates. Say so if that is too much and I will cut further.

---

## 8. Risks

| Risk | Rating | What the plan does about it |
|---|---|---|
| Channel A is dead (the overrides are overwritten before kick-off) | Medium-high (two reports disagree) | One cheap deciding match before any slider code; channel B as the fallback with its own gate |
| The CPU tactic is not read from the table D1 writes, or Dynamic rewrites it | Medium (reports disagree on the table) | Formation test first (1-3 matches), both candidate tables tried, Dynamic's effect recorded; the feature keeps only what passes |
| Save drift from CPU database writes | Medium | Restore file is a precondition for any automatic write; restore on the first career event after the match, next connect and kill switch; probe diff in every test; throwaway career |
| Live tactic corruption (channel E) | High if done carelessly | Read-only phase, layout proven on the running build, every field checked, opt-in file, kill switch, backup before U1, hub-only, Team Management closed |
| Wrong role + focus number crashes match load | Medium | Decode first; write only numbers seen in the game's own rows; roles are optional in D1 and E |
| Process-wide overrides reach an online match | Low (you play offline) | Banner, clear on exit, nothing persisted |
| Title update changes names, offsets or layout | Medium | Every new address is a signature with auto-adapt and instruction checks, like 2.0.2; if a record does not look exactly as expected, Turbo writes nothing |
| Misreading your "never touch EA's slider menu" | Decision | Section 9 asks; the plan works either way (D and E do not use EA's slider values at all) |
| Research addresses are from the previous build | Certain | Nothing is called verified until it is re-found on the running build (memory image) and seen in a match |
| Time at the PC | Certain | Cut lists by default; phases are independent; each ships on its own |
| Research reports contradict each other (channel A; the CPU table) | Known | Both readings are stated in sections 3.3 and 3.4; tests, not opinions, decide |

---

## 9. What I need from you

1. **Approval of this plan**, or the changes you want, before any code is written.
2. **"Never touch EA's slider menu"**: per-side slider values replaced in the game's memory, with the menu staying on Authentic and untouched (the way the injury override already works), are allowed. That is my default reading [inferred from your acceptance of the injury override and of the 2.0 design]. Say no, and channels A and B are dropped; D, E and C proceed.
3. **"Dynamic Opposition" = AI Behaviour: Dynamic** [inferred]. Say so only if you mean something else.
4. **One-time, before Phases 1 B, 3 and anything else that needs new game addresses: a memory image of the running game** (`turbo_output\fc27_image.bin`, written by Turbo's existing dev-service dump; I will give you the one-line command in Phase 0's instructions), uploaded to the cloud session, never committed. Without it, channels B and E cannot be researched from the cloud and stay out of 2.1. This replaces any local reverse-engineering session.
5. **One-time, so both test suites run in the cloud**: upload Live Editor's `lua\libs` folder to the cloud session (private, never committed; `turbo/le27/README.txt` names the folder). Until then the native and Lua suites run only on your PC (section 10).
6. **Local sessions** when each phase asks (section 7), on turbo04 with a backup; attach the files listed there. Nothing else is needed from you for the cloud side.
7. **Priority**: your own club (Phase 3) or opposition variety (Phase 2) first, if you have a preference; otherwise the order in section 7.
8. **The risk you accept for channel E**: a new, opt-in game call into career code on the throwaway career only. If you would rather not, say so and My team ships read-only.

No credentials, keys or purchases are needed.

---

## 10. Cloud build check (this session, 2026-10-08, HEAD d058e98)

- **Windows build** (`turbogui/scripts/build_win.sh`, MinGW cross-compile of Turbo.dll, TurboInjector.exe and TurboProbe.exe): **runs in the cloud** after installing Ubuntu's `g++-mingw-w64-x86-64-posix` (GCC 13.2). Exit 0; `build/win/Turbo.dll` 7.3 MB, `TurboInjector.exe`, `TurboProbe.exe`. Cloud-built binaries are what you install.
- **Native C++ test suite** (`turbogui/tests/native/run_native.sh`, g++ with sanitizers and the ImGui test engine): it refuses to start without Live Editor's Lua libs (`turbo/le27/libs`, gitignored, local only), because its fixtures are built by a Lua script on top of the Lua test world, which loads those libs. A dry run in this session with that check bypassed compiled 84 of the 85 translation units; the last one, `tests/native/test_main.cpp`, fails on Linux g++ 13 with one type error (line 2765: a braced list mixes `uint64_t`, which is `unsigned long` on Linux, with `~0xFFFULL`, an `unsigned long long`; on MinGW both are the same type, so it compiles on your PC). So today the native suite does not compile on Linux at all, libs or no libs. Phase 0 includes the one-line fix (cast the four masked constants to `uint64_t`); after that, and after section 9 item 5, the suite runs in the cloud. Until both are done it is local-only, like the Lua suite, and every phase's local session starts by running both suites on your PC (`turbogui\tests\native\run_native.sh` and `turbo\tests\run_tests.sh`), with their TOTAL lines attached.
- **Lua suite** (`turbo/tests/run_tests.sh`): needs the same libs; local only until item 5 of section 9 is done. In the cloud the Lua files are only parsed (`luac -p`), not run.
- **Line endings**: both shell scripts are CRLF files. On your PC (MSYS bash) that is fine; on Linux bash the first line fails ("pipefail\r: invalid option name"). In the cloud they are run from an LF copy made next to the original so that the script still finds the project root. No change to the scripts is proposed (CRLF stays where it is used); the helper copies are deleted after each run and never committed.

---

## Appendix A: evidence index (addresses and file references)

All game-code addresses are from the image of game build 1.0.140.64835 (6AB9813C) dumped on 2026-10-03; the running build is 6AC07E31-2145C000 (1.0.141.12554).

| Topic | Where |
|---|---|
| Played-match results | `ingame-results-2026-10-07.md`; `C:\FC 27 Live Editor\turbo_dev\evidence\2026-10-07\RESULTS.md` |
| Career tactic tables and fields | `fc27_tactic_tables_schema.json` (14 tables, row counts); `probe_tactics.txt` on the PC |
| Your club: load once (LoadTeamSheets 0x147B5DB4C, reader 0x147B9B9A4), write back at save (WriteTeamSheet, writer 0x147B96DD8); live record layout (TeamSheet 0x288 bytes, +0x1B0 tactic records of 0xF8, slots of 0x48 with role at +0x30); match copy of formation and slot roles (0x148037DC8); converters (depth bands 0x141B314B4, build-up 0x143D2AC9C) | research.json `db-tactics`, `runtime-path`; `docs/re/realtime_transfers.md`; `scripts/re/realtime_signatures.json` |
| CPU clubs: `default_mentalities` lifecycle (restore at load 0x147B5FF78, backup at save 0x147B60308), match-side queries (0x146FCADF0, 0x145E2724C, 0x1440CFDD0); old service reads (0x145E28140, 0x145E2F2A0, 0x145E34344) | research.json `db-tactics`, `runtime-path` |
| Team traits enum (23 entries) and live read at match build (0x14803C768, 0x1480463C8, 0x1480461EC) | research.json `db-tactics`, `runtime-path` |
| Gameplay slider game variables (reader 0x1442DC794, GetInt 0x140856DB4), slider block (0x1268 bytes), match copy and Authentic substitution (0x141EBCEA4 / 0x141EBD184), recompute (0x141EBD1C4), profile builder (0x14807E7E4), per-team difficulty (0x14802AA7F..), SetFloat 0x14154F324 | research.json `gameplay-sliders`, `turbo-code-map` |
| Team-sheet service (id 0x113EA820), SetSheet (slot 24, 0x145E45C40), getter candidates (0x588 / 0x590 / 0x598) | research.json `runtime-path` |
| FC 27 tactic controls, removed controls, roles/focus list, slider menus, AI Behaviour, FM instruction set | research.json `fc27-surface` (EA pitch notes FC 25/26/27, Live Editor wiki, guides) |
| Turbo code map (registry `core/sliders.cpp`, apply path and kill switches `ui/ui_tactics.cpp`, known variables `core/match_setup.cpp`, solver and seed `core/opposition.cpp`, formation reader `core/tactics_db.cpp`, profiles `core/tactic_profiles.cpp`) | research.json `turbo-code-map` |
| Memory image route for reverse engineering (dev-service `dump` op) | `docs/re/pending-re-briefs.md` |
| 2.0 design rules (visual honesty, profiles, architecture) | `docs/TURBO_2_0_PLAN.md` sections 3, 4, 5, 9 |

## Appendix B: open questions the tests answer

1. Does the slider game variable for the CPU side reach kick-off? (Phase 1 deciding test)
2. Which table does a career match read for the CPU tactic, and does Dynamic change it mid-match? (Phase 2 formation test)
3. How are role and focus packed in the slot number; which build-up value is which label; which line height does each approach write; which preset number is each Vision? (Phase 0 decode)
4. Does the live record layout hold on the running build? (Phase 0 locator)
5. Is the trait switch order as inferred, and do traits act under Authentic + Dynamic? (Phase 0 check, Phase 2 D2 test)
6. Do the `teams` build-up and line height matter for the CPU side when its tactic row also carries them? (Phase 2 D1-style, second pair)
7. Do the per-team difficulty variables work, and is the match-difficulty override read at all? (Channel C test)
8. Does your Live Editor expose the Attribulator functions? (Phase 0 probe)
9. Is the CPU-behaviour slider group read anywhere in a match? (code search on the memory image; otherwise it stays preview)
