# Tactics overhaul brief: assessment (Fable), implementation (Opus), session protocol

Written 2026-10-13 19:50 ET (local = UTC-4) at the end of the 2.3.0 day, for the next phase that Cassio decided at 19:44:
**Fable (claude-fable-5-1) in ultracode mode assesses how to implement the tactics overhaul for good; then Opus 5.5 at extra effort
writes the code.** Do not re-open that choice.

- **Part A** (Fable): the assessment brief. It covers the goal, what is proven, refuted and unknown, the open questions, the candidate
  architectures, measurement, the deliverable and a workflow shape.
- **Part B** (Opus): the implementation brief. It covers the codebase, conventions, write safety, build and deploy, in-game proof and the
  release rule.
- **Part C** (anyone who drives the game): the session protocol.

The full fact base is in `C:\FC 27 Live Editor\turbo_output\handover_24\`. Every claim in those notes carries a tag and an evidence path:
- `01_tactics_knowledge.md`: channels, anchors, layouts, the tuning layer, measurement and the 10 open questions.
- `02_infra_rules.md`: rules, launch, menu path, gk.ps1, dev service, the session template and the switch files.
- `03_code_release.md`: branches, the code map, the knobs file, signatures, the dispatcher, build, release and stale docs.
- `04_goals_constraints.md`: Cassio's quotes, decisions D1-D14, forbidden actions and the success criteria S1-S8.

This brief condenses those notes. Where it differs from a note or from the evidence that note cites, **the evidence wins**.

Tags: **[V]** = verified (a file, log line, dump, screenshot or played match shows it; source named or in handover_24), **[A]** = assessed
or inferred, **[U]** = unknown. Claims from other sessions, including this one, are data, not proof.

Path shorthands:
- `LE\` = `C:\FC 27 Live Editor\`
- `TO\` = `LE\turbo_output\`
- `TOOLS\` = `LE\turbo_dev\tools\`. This folder is **not in git**.
- `EV\` = `LE\turbo_dev\evidence\`
- `REPO\` = `LE\turbo_dev\2.0\integration\` (branch turbo-2.0)
- `SRC\` = `REPO\turbogui\src\`

Addresses are for game build **6AC07E31-2145C000** (FC27.exe base 0x140000000) unless marked "old build" (6AB9813C, the only exe image on
disk: `TO\fc27_image.bin`). Heap addresses change at every game launch.

## Glossary
| Term | Meaning |
|---|---|
| LTEST | A throwaway copy of the LEICESTER career (club 95) used for every test. **Never saved.** |
| Hub | The career hub screen, where Turbo's write gates allow writes. |
| Tactical View | The match mode used for every test: both teams are AI-controlled and the sim runs. |
| Team Management / Edit Tactic | The game's own tactic screens ("Set up your Tactics" from the hub). |
| Channel E / map record | `CachedTeamSheetService` ("svc"): a hash map from team to sheet, each sheet with 5 tactic records, one of them active. The active record holds Line Height (+0xA4), Build-Up (+0xB0), Defensive Approach (+0xB4) and 11 slot role words. |
| E2 / E3 | E2 writes the Edit Tactic working copies (svc+0xF80 / +0x1170). E3 writes the inline sheet svc+0xA60's records. |
| MCOPY | 0x1480394BC, the match-setup copy of the team tactic made at kick-off. |
| Tuning / attribulator | The engine's gameplay tables (`AttribSchema_*` instances) that the Anth James / FIFER gameplay mods change. 15 `AttribDBRuntime_gameplayattribdb` variants plus a prematch DB. Static manager slot G = 0x14C26B128 (read by `get_current_gameplay_attribdb` 0x141077070). |
| Route C | The measurement route of `TOOLS\match_measure.py`: the match object global G1 = 0x14C27F138 -> M -> 22 players + ball, live. |
| Coordinator | The top-level session. **Only it drives the game and writes game memory.** Agents work offline. |

## State at handover (2026-10-13 19:50)
| Item | State |
|---|---|
| Release | Turbo **2.3.0**: tag v2.3.0 = e952411 (annotated 3cf5bf4), released 18:36, https://github.com/cmx24/FC27-Editor-Turbo/releases/tag/v2.3.0, zip sha256 294ab9186ec42963d987d7493ac53ac098cfd744f05ba630237d6e9c4a1e5dbc [V] |
| Branch | turbo-2.0 = 89e0c24 on origin (the 18:40 handover), plus the commit that holds this brief [V] |
| Installed | `LE\turbo\Turbo.dll` sha256 e7cf8d41..., 2.3.0 rebuilt 19:45 from wt-23-release e952411. Same source as the release, not byte-identical to the zip's cb079e84 [V] |
| Game | Closed. `TOOLS\GAME_STATE.txt` = `CLOSED (19:41)` [V] |
| Switch files | Only `TO\repair_ovr1_off.txt` (keep it). No `*_on.txt` file [V]. `TO\tuning_keep.json` (stale width 1.4, `"on": false`) was moved to turbo_dev/backups/tuning_keep_20261013-2008_stale.json at 20:08 [V] |
| Saves (md5, first 8 chars) | TORINO 2ded2b76, LB c45b2b12, LEICESTER 1e78df22, LTEST 3dced956. Unchanged all day; equal to `turbo_dev\backups\20261013-1729-pre-resume-23` and `...-1934-pre-width-ab2` [V] |
| Unpushed work | `2.4/channel-e3` = 468fe54 in worktree `turbo_dev\2.0\wt-24-channel-e3` (local only). It also writes the svc+0xA60 inline sheet's record copy. check_all green (LUA 405/0, NATIVE 38488/0). **Never run in game.** Report `TO\CHANNEL_E3.txt`, probe `TOOLS\channel_e3_probe.py` [V] |
| Release rule | **Ship only when working** (Cassio 18:39): no tag or release until every fix and addition is proven in game. 2.3.0 shipped two unproven items before this rule existed: the E2 Edit Tactic display and the tuning on-pitch effect [V] |

---------------------------------------------------------------------------------------------------------------------------------------

# Part A - Assessment brief for Fable (ultracode)

## A.1 The goal, in Cassio's words
His messages, from the transcripts quoted in handover_24/04:
- 10-11 13:53: "rewrite to add the tactical overhaul with actual working sliders and settings after the research that was conducted with
  different mods and the games memory and database"
- 10-11 14:31: "for the tactics, i dont want 10hours of testing. i want A/B testing of working and not working - the existing mods like Anth
  James already show it is possible"
- 10-12 09:26: "Anth James and FIFER mods are proven to affect match settings and team/player tactics regardless of settings in the game
  main options."
- 10-12 09:53: "there's no fc27 mod manager yet and I want everything in Turbo. I want Turbo to be all in one, one stop shop for all
  modding/editing purposes"
- 10-12 10:22: "im unclear on how you're implementing the tactics overhaul - seems like you're just adding the same options available in
  Tean Management into Turbo"
- 10-13 19:44: "this has been taking too long - focus the next step on properly identifying how to finally implement the tactics overhaul
  for good"

What this means:
- **G1 (primary): the engine tuning layer inside Turbo.** These are the attribulator tables that the Anth James / FIFER gameplay mods
  change: team shape, pressing and marking, runs, and per-role behaviour. They must work "regardless of settings" and need no mod manager.
- **G2 (secondary): the club's live tactic is honoured.** What Turbo writes (Line Height, Defensive Approach, Build-Up, roles) is what Edit
  Tactic shows and what the match plays.
- **What he does not want:**
  - another mirror of Team Management's own options (his 10-12 10:22 complaint);
  - hours of testing;
  - releases whose tactics features were never proven on the pitch. 2.1, 2.2 and 2.3 all did that.

**Success criteria S1-S8** (from 04 s.5; the assessment must make each one testable):
- S1: each offered knob has a measured on-pitch effect for both teams.
- S2: the test design separates the knob from the game state.
- S3: only proven knobs are offered as working.
- S4: coverage of what the mods do: shape, pressing/marking, runs, roles.
- S5: works like a mod, from Turbo alone.
- S6: Edit Tactic and the match follow Turbo's tactic writes.
- S7: kill switch, read-back, undo; real careers untouched.
- S8: one release with everything proven.

## A.2 What is proven, refuted and unknown

**Proven [V]**
| # | Fact | Evidence |
|---|---|---|
| P1 | Two tuning tables are located live with an identity check (TypeInfo at +8, Name at +0x18): the attacking `linelengthwidth` table and `pm_teamtacticssettings` (the depth bands). There is ONE attacking `linelengthwidth` instance, shared by all 15 gameplay DBs. The static manager slot G = 0x14C26B128 is stable across sessions of this build. | `TO\ATTRIBULATOR_LIVE.txt/.json`, `TO\TUNING_FRAMEWORK.txt` |
| P2 | Tuning writes land and read back in the hub: tab Apply 1.00 -> 1.40 in 3406 us, then Undo. They also land mid-match through `ab_width.py` (18:00 and 19:40). The match ran on with no crash. | `TO\turbo_gui.log` l.40497/40501, `EV\2026-10-13\TEST_23.md` |
| P3 | Channel E map-record writes land and read back: LH, BU, DA and role words, in jobs under 1 ms. | `EV\2026-10-12\TEST_22.md`, TEST_23 |
| P4 | **The match takes the role word from the map record.** After kick-off, both match-setup copies held Turbo's ST word 38210. | `EV\2026-10-12\0856_match_setup_copies.txt`, `0856_kickoff_rolewords_find.json` |
| P5 | Edit Tactic "Save Changes" writes only the working copy svc+0xF80. **Leaving Team Management and answering "Yes" copies that working copy into the map record AND autosaves the career.** | `TO\TACTICS_LAYER1_FINDINGS_20261012.txt` s.2 |
| P6 | The meanings of +0xA4 / +0xB0 / +0xB4 were decoded from the game's own Edit Tactic save (25->50, 0->1, 0->1). | LAYER1 |
| P7 | Opening Edit Tactic without saving does not overwrite the map record. | log l.38965/38970/40490 |
| P8 | Measurement route C reads all 22 players live in Tactical View. Engine units are feet (pitch 344.48 x 223.09 = 105 x 68 m) and are converted to metres. | TEST_23 24-B/C, `TO\match_measure_layout.json` |
| P9 | Some game variables do reach played matches (weather, injuries, CPU subs), because they are read at another site. | `EV\2026-10-07\RESULTS.md` |
| P10 | The mods change attribulator EBX: 828 mods were decoded. 100 of 124 tactics groups have the same layout in FC 26 and FC 27; FC 26 and FC 27 vanilla values are 94.5 % equal. | `TO\TACTICS_MOD_CATALOGUE.txt` s.0 |

**Refuted [V]**
| # | Refuted | Evidence |
|---|---|---|
| R1 | Channel A, the per-side `GAMEPLAY_CUSTOMIZATION/*_USER/_CPU` game variables (the main-option sliders). They are stored and read back, but the match ignores them. | `TO\TACTICS_CHANNEL_A_REPORT.txt` VERDICT; `EV\2026-10-11\2238_channelA_armB_HT_matchfacts.jpg` |
| R2 | The `cm_mentalities` row as Edit Tactic's source. Mirror test 19:22: the DB read back 25->95 / 0->1, and the screen still showed BALANCED / DEEP. | `EV\2026-10-13\1936_edittactic_mirror.jpg`, log l.41017 |
| R3 | The map record as Edit Tactic's source. Edit Tactic showed the career-load values (25 / Balanced / Deep) in 3 sessions. | TEST_22, TEST_23 (`1834_edittactic.jpg`) |
| R4 | E2 as built: in the hub, before Team Management opens, no working copy holds the active tactic code, so E2 writes nothing. | log l.40484, l.41016; `TO\CHANNEL_E2_REVIEW.txt` |
| R5 | Measurement routes A (Lua root S0, empty in career matches) and B (HUD FifaPlayerData, frozen in Tactical View). | TEST_23, `TO\session24\20261013-193013\` |
| R6 | A single 30 s A/B window pair as a test design. 24-C: team 0's width with the ball rose 4.4 m while team 1's fell 2.8 m, and the back lines moved 15-17 m because territory flipped. | TEST_23 24-C, `TO\match_measure\20261013-19*` |

**Unknown [U]** (the open questions in A.3, plus the following)
- teams.buildupplay / defensivedepth. The old build reads them LIVE at match build. Turbo writes them, labelled "accepted, untested". They were
  never tested in a match.
- default_mentalities, the CPU clubs' tactics: never tested.
- Whether a match keeps simulating while FC 27 is not the foreground window.
- How much game time passes per real second in Tactical View. It looks faster than real time [A].
- Which side index (team 0/1 in route C) is Leicester.

## A.3 The decisive open questions (ranked by how much of the design hangs on each)
| # | Question | What it decides | Cheapest way to answer [A] |
|---|---|---|---|
| Q1 | **Does the match engine use the tuning tables after load, and when does it read them?** The options are per decision (live), at kick-off or match setup, or at career load / boot. | Whether G1 works as live writes, and at what moment Turbo must write (hub before kick-off, career load, or game start). | Offline: trace the read sites of `linelengthwidth` +0x28 in the old image. Do the readers run per tick (AI positioning code, which also reads G1) or once? Then in game: X1 (A.6). |
| Q2 | **Which copy feeds the match's Line Height, Build-Up and Approach?** Candidates: the map record (proven for roles), teams.* (read at match build in the old build), cm_mentalities (read at load), an unknown copy. | Which channel G2 must write so the match plays it. | Sentinel values (a different Line Height in each candidate source), then read the match's own state at kick-off: the MCOPY output (a kick-off dump) or the in-match `TeamState` (DefendingTeamStyle +0x230, Mentality +0x280, OffsideLineX +0x17C, old-build type data). This is X2. |
| Q3 | **Where does Edit Tactic load from?** | Which copy G2 must write for the display, and whether the user's own Edit Tactic "Yes" puts the old values back over Turbo's. | The 2.4/channel-e3 build plus `channel_e3_probe.py`, or sentinels written by the dev service into each candidate copy. Edit Tactic then shows which copy it read. This is X3. Edit Tactic must be the LAST step. |
| Q4 | **Is Defensive Approach stored on its own (+0xB4) or derived from Line Height through the depth bands** (at match setup or after a reload)? cm_mentalities has no approach column. | Whether the approach needs its own write; how persistence works. | Write a deliberately inconsistent pair, e.g. LH 95 (Aggressive band) with DA 0 (Deep). Whatever the match / screen shows answers it. Fold into X2 and X3. |
| Q5 | Is Short Passing stored as 2 or 3? Is Aggressive 3? (CPU records: build-up 0 x388, 1 x173, 2 x279, 3 x0; no record uses approach 3.) | The enum labels; today Turbo refuses writing 2 or 3. | At the end of X3, inside Edit Tactic: select Short Passing, Save Changes (writes svc+0xF80 only, no save), dev read of svc+0xF80 +0xB0, then kill. |
| Q6 | Which AttribDBRuntime is current in a career (Authentic) match: base or simulation? | Matters only for groups with their own simulation copy (zonal_defense, defense_general). Turbo already writes base and simulation together. | One read of the manager's current DB during X1. |
| Q7 | Can the 11 unlocated knobs (5 groups) be located and identity-checked live? | Coverage: pressing, marking, runs, gaps, the defensive line. | Offline: a chain locator from G through the DB runtime's family and group slots (offsets from type data). Live: a read-only check under 60 s in X1's hub phase. `attrib_locate.py` took 815 s, which is not usable live. |
| Q8 | Persistence: does a Turbo-written tactic survive an in-game save and reload (approach included)? Must tuning be re-applied at every launch or career load? | The persistence design (tuning is memory-only [A]). | Testing a save/reload needs a career save. LTEST is never saved, so this needs Cassio's decision (A.10). |
| Q9 | Is Tactical View representative of a played match? | Whether it is a valid test bed. | Accept it [A]: both teams are AI-controlled, so an engine-wide knob affects both. Measure the clock rate once (screenshots of the match clock at two times). |
| Q10 | Side effects: a record changed without a new tactic code; sheets with mode > -1 (vf 0xF8 propagation); Lua Team Management events firing in FC 27. | Safety gates. | Code review plus static reads. |
| Q11 | **Do the match engine's systems read the depth bands (pm_teamtacticssettings) at all, or only the Edit Tactic UI** (to clamp the Line Height slider per approach)? [A risk: "pm_" = prematch] | Whether the 6 "lineband" knobs, which are already offered, can ever have an on-pitch effect. | Offline: find the readers of the pm_teamtacticssettings fields in the old image. |

## A.4 Candidate architectures to evaluate
For each candidate: what it is, what it delivers, the evidence, the risks, and the cheapest offline check and in-game experiment. The
assessment picks one architecture for G1 and one for G2 (or a combination) and records why the others were rejected.

### Arch A - Engine tuning layer: live attribulator writes (G1)
- **What.** Turbo writes engine tuning fields in memory, like the 2.3 tab:
  - when: in the hub before kick-off, and again at every launch / career load (so it works "like a mod");
  - all tables reached through a deterministic chain from G;
  - only knobs with a proven on-pitch effect are offered.
- **Evidence for:**
  - P1, P2, P10.
  - The mods change exactly these tables.
  - The data-driven framework exists: `SRC\core\tuning.*`, locator kinds attrib / chain / native, gates, snapshot, read-back, rollback,
    undo, keep and presets.
- **Risks:**
  1. **Cached values.** Mods change the EBX *before* the runtime instance is built; Turbo changes the instance *after*. The two are
     equivalent only if the engine reads the instance at use time, or at match setup after Turbo's write. If it caches derived values at
     career load or boot, a hub write does nothing [A].
  2. **UI-only knobs.** Some knobs may be UI-only (Q11).
  3. **Small effects.** Effects may be small next to game-state noise, and high values may clamp at the touchlines [A].
  4. **Unlocated knobs.** 11 knobs are unlocated, and the heap search costs 24 s per launch.
  5. **Engine-wide only.** The tables are engine-wide, so per-club behaviour can only come from per-club tactic inputs (D14).
  6. **One build.** The knobs file covers this one build.
- **Cheapest offline check:**
  - Trace the static read sites in `TO\fc27_image.bin` (old build; capstone in `turbo_dev\re_venv`) for:
    - `get_current_gameplay_attribdb` -> DB +0x128 -> group +0x28 -> field +0x28;
    - the pm_teamtacticssettings fields.
  - Classify each caller: per-tick AI positioning, match setup, or load.
  - This predicts the outcome of X1's two write timings (B1 vs B2, see A.6).
- **Cheapest in-game experiment:** X1 (A.6).
- **Fallback if X1 shows nothing:** check that the metric is the right one by taking a knob with an unmistakable, state-independent effect
  (run frequency 0.5 -> 25, marking tightness 0.5 -> 2.0), located by the chain. The other option is to write earlier: at the main menu,
  before the career loads. Today's gates allow the hub only.

### Arch B - Live team tactic through the team-sheet service copies (G2, live)
- **What.** Channel E, E2 and E3: write the map record and every other copy the game keeps (working copies, inline sheets), in the hub,
  live.
- **Evidence for:** P3, P4 (roles reach the match), P6, P7. The code exists (`SRC\core\team_sheet.*`, 2507 lines) with gates G1-G6, a role
  allow-list, undo and a kick-off dump.
- **Risks:**
  - Chasing copies one by one: E, then E2, then E3 have each missed Edit Tactic's source (R3, R4).
  - The user's own Edit Tactic "Yes" copies the screen values back and autosaves (P5).
  - 11 `exact_build` signatures.
  - Whether the match uses LH, BU and DA is unknown (Q2).
  - Sheets with mode > -1 are refused.
- **Cheapest experiments:**
  - Q2: X2 (kick-off readout with sentinels).
  - Q3: X3 (E3 build or dev sentinels, then Edit Tactic, last step).
  - Offline: `EV\2026-10-12\1012_svc_B00_1400.bin` (the service dumped while in Team Management) and the CHANNEL_E3 ranking of the
    candidate copies.

### Arch C - DB rows at career load (G2 at load; CPU clubs; persistence)
- **What.** Make the game build every copy from Turbo's values by changing the DB rows it reads at load. The old build reads every
  `cm_mentalities` / `cm_teamsheets` row ONCE in LoadTeamSheets (0x147B5DC4C, found by signature on 6AC07E31). Two ways to apply it:
  - (a) a guarded pre-hook on LoadTeamSheets, or the earliest career-load event, that writes the rows just before the game reads them;
  - (b) the save path: write the map record, the game's own save writes the row (WriteTeamSheet), and the next load builds all copies.
  - CPU clubs: `default_mentalities` (the working copy, rebuilt at load).
  - `teams.buildupplay` / `defensivedepth` are read live at match build in the old build.
- **Evidence for:**
  - Edit Tactic always shows the *career-load* values, which fits a cache filled at load [A].
  - The load-once lifecycle [V old build].
  - The LOAD, WRITE and TSAVE functions are found by signature on 6AC07E31 [V]. Their behaviour was not re-traced [U].
- **Risks:**
  - It takes effect only at the next career load unless combined with Arch B.
  - The approach has no column (Q4).
  - Hooking the load path risks the career load itself.
  - Testing (b) needs a career save (A.10).
  - A DB edit made after load is invisible and is overwritten at the next save [V old build].
- **Cheapest offline check:** a static trace of LoadTeamSheets in the old image:
  - which objects it fills (svc map records? svc+0xA60? the Team Management cache?);
  - where the approach comes from;
  - the Lua/LE career events around it.
- **Cheapest in-game experiment:**
  - Put a teams.* sentinel into X2.
  - (a) needs code first (a small pre-hook). Prove it with X3's screen check after a launch.

### Arch D - Match-setup / kick-off copies (G2 match truth, any club)
- **What.** A guarded MinHook post-hook on MCOPY 0x1480394BC (or the match build / converter) that sets the club's LH, BU, DA and roles in
  the match-setup output. The other option is a write to the in-match `TeamState` at kick-off.
- **Evidence for:**
  - MCOPY is found by signature [V].
  - Its output carries the role words at +0x118, the team at +0x28 and the positions at +0x30 [V 10-11].
  - It runs once per match setup [A], so the timing is deterministic.
  - It works for any club, which suits CPU profiles.
- **Risks:**
  - The LH / BU / DA offsets in the output are undecoded: 0x1A0 bytes were read, not decoded.
  - There are two setup copies (plus old and new role words 0x150 apart), and only one may be the one that is read.
  - A hook at match setup is a crash risk. The rules require HOOK_BODY and always calling the original.
  - `exact_build`.
  - It does not fix Edit Tactic's display.
- **Cheapest experiments:**
  - X2's kick-off dump, decoded with sentinels. The opponent's known values give a second, independent decoding key.
  - Offline: decode MCOPY and the old converter 0x148037D10 / 0x141B314B4 statically.
  - Note: the 2.3 kick-off dump `TO\channel_e_kickoff_dump.bin` is written only when `call_team_sheet_on.txt` is present during the match.
    It was absent at 19:29 and 19:37, so no dump exists yet [V].

### Arch E - Combinations (evaluate explicitly)
- **E1 = A (G1) + C (display and persistence) + B (live changes inside a session).**
- **E2 = A (G1) + D (match truth, any club) + C or B only for the display.**
- **E3 = A only**, with G2 limited to what is proven (roles), and Team Management left to the game.
- For each combination, judge it on:
  - which success criteria it meets;
  - how many in-game proofs it needs;
  - how fragile it is per build;
  - the implementation cost.

### Arch F - Turbo-owned presets and tactic profiles on top (UX layer; needs A plus one proven G2 channel)
- A **profile** has three parts:
  - an engine knob set, expressed as Turbo's own changes relative to FC 27's values (no mod values);
  - a per-club tactic (LH / BU / DA / roles);
  - a scope (my club / all CPU clubs / named clubs).
- When it applies: at career load and before every match setup, with an honest UI that offers only proven knobs.
- What exists today:
  - `SRC\core\tactic_profiles.*` (615 lines);
  - `tuning_presets\`;
  - `tuning_keep.json`.
- Optional, and only if Cassio says yes (D12): read a `.fifamod` he owns, locally, and map its fields onto knobs by name. Nothing is
  redistributed. The reader is `TOOLS\fifamod_catalog.py`.

**Decision principles [A]:**
- Answer Q1 and Q11 before anything else.
- For G2, prefer a channel where the game itself builds every copy (load time, or match setup) over chasing copies.
- For G2, prefer reading the match's own state (the MCOPY output or TeamState) over position statistics. Keep position A/B for the engine
  knobs (G1).
- Each experiment needs a pass criterion written down before it runs.

## A.5 Measurement method and pitfalls
- **Tool.** `TOOLS\match_measure.py` route C:
  - Commands: `--check -v --label X` (PASS/FAIL, under 2 min), `--seconds N --label X`, `--compare DIR_A DIR_B`.
  - Output goes to `TO\match_measure\<stamp>_<label>\` (positions.csv, summary.json, report.txt).
  - Metrics per team per 1 Hz sample: back line (the 4 deepest outfielders), length, width (max - min lateral of the 10 outfielders),
    compactness, centroid, pressing distance, ball depth and possession.
  - Every metric is split into with and without the ball.
  - `--compare` flags an effect at >= 0.8 sd. This is a per-sample sd, not a significance test, and the samples are autocorrelated.
  - Route C carries no DB player id. Team 0/1 is the side index.
- **Data to reuse offline.** `TO\match_measure\20261013-193914_check` (20 samples), `-193955_A_width100` (30) and `-194050_B_width140` (30).
  Use them to estimate the noise floor and design the windows before any new match.
- **Pitfalls [V]:**
  - Game state dominates short windows (24-C).
  - Tactical View runs faster than real time: up to 18 m moved between two 1 s reads.
  - There is one match per launch [A]: leaving a match needs the pause menu, which needs Escape, which is forbidden.
  - A session costs about 7 min: launch to kick-off is about 4 min.
- **Pitfalls [A]:**
  - High width values may saturate at the touchlines (68 m pitch, width with the ball already 38-51 m).
  - Do not let a match reach full time: the career's post-match flow may autosave [U]. Plan every window inside one half.
- **Design rules [A] (T4 finalises them):**
  1. **The write timing is itself the experiment.** Write the value in the hub BEFORE kick-off (window B1), then later mid-match (window B2).
     B1 vs B2 separates "read at kick-off" from "read live".
  2. **ABAB with A1 vs A2 as the noise floor.** At least two alternations. Windows of 45 s or more, or windows defined by the match clock if
     it can be read (MatchState Seconds +0xD0 in the old-build type data; reach it from M?).
  3. **Use extremes.** Test 0.6 as well as 1.7: narrowing avoids touchline saturation. B vs C at the two ends doubles the contrast.
  4. **Engine-wide knobs must move BOTH teams.** Pool the two teams by phase (with or without the ball).
  5. **Stratify by ball third and possession.** Compare within strata. Drop stoppage samples (dead ball, frozen players).
  6. **Test on blocks.** Use a block bootstrap or permutation test over windows, not the per-sample sd.
  7. **Write the pass criterion before the run.** Example [A]: both teams' with-ball width moves in the predicted direction in B1 and
     B2 vs A1 and A2, by at least 3 m and at least 2 x |A1 - A2|, while without-ball width moves less than half of that.
  8. **For G2, read the match's own state** (MCOPY output / TeamState) at kick-off. That reading needs no statistics.

## A.6 Proposed experiment pack (each one short scripted session; coordinator only; Cassio's "go")
These are proposals [A]. The assessment finalises them, proves every script offline (mocks or dumps, time-boxed, PASS/FAIL, kill on every
exit path) and writes `TOOLS\SESSION_<n>_PLAN.md` for each.

| Session | Answers | Outline | Pass point |
|---|---|---|---|
| **X1 - engine reads tuning?** | Q1, Q6, Q7 (read-only), Q9 clock | 1. Hub: open Gameplay tuning (locate, about 25 s). 2. Read-only chain check of the 11 knobs (under 60 s). 3. Optional, time-boxed: dump the 6AC07E31 image (dev `dump` op, len <= 1 GiB; image 0x2145C000 bytes) so static work can run on the current build. 4. Write width 0.6 (or 1.7) in the hub. 5. Kick off in Tactical View; `--check`. 6. Windows B1 45 s / A1 45 s (revert to 1.0) / B2 45 s (write) / A2 45 s. 7. Revert, kill, compare. 8. Clock screenshots at window boundaries; one read of the current DB index. | Pre-registered (A.5 rule 7) |
| **X2 - which source does the match use?** | Q2, Q4 | 1. Hub, with `call_team_sheet_on.txt` present so the kick-off dump is written. 2. Write different sentinels per source: map record LH 95 + DA 0 (the inconsistent pair for Q4) + BU 1; teams.defensivedepth 77; a cm_mentalities sentinel if a separate write path exists. 3. Kick off. 4. Read the kick-off dump and TeamState (if T2 finds the path); 45 s back-line measurement. 5. Kill. | Exactly one sentinel appears in the match copy, which identifies the source. Approach = Deep or Aggressive identifies stored vs derived. |
| **X3 - where does Edit Tactic load from?** | Q3, Q4, Q5 | 1. Deploy the 2.4/channel-e3 build, or write dev sentinels per candidate copy (map 95, svc+0xA60 record 85, others from `channel_e3_probe.py`). 2. Open Edit Tactic: screenshot. 3. Select Short Passing, Save Changes, dev read svc+0xF80 +0xB0. 4. Kill (Edit Tactic leaves the game in the Team Management trap). | The value on the screen names the source. |
| X4 (only with Cassio's yes) | Q8 | A save/reload round trip on a throwaway career that is restored from backup afterwards. | Values after reload. |

Values must be valid: Line Height 1..100. Never use an enum value whose meaning is unknown (BU 2/3, DA 3 as a sentinel). Sentinel writes
go through the dev `write` op (coordinator, `allow_write=True`, identity re-check, read-back) or through Turbo's own UI. Agents never
write.

## A.7 Test rules and tools available (details in Part C)
**Rules:**
- Never send Escape.
- LTEST only, never saved. Answer NO to "save your changes to your team sheet?".
- Back up and md5 the saves before every launch; compare after every kill.
- Short scripted sessions: one pass/fail point per check, a screenshot, and a planned kill as the last step.
- A live game is never a development sandbox.
- Edit Tactic / Team Management checks come LAST.
- Only the coordinator writes memory and drives the game. Driving needs Cassio's chat "go" at session start (his 10-12 standing "ok"
  covers short LTEST tactics tests).
- Every write needs an opt-in, a kill switch, a read-back and an undo.
- No mod values in git or the zip. Live Editor's DLL and launcher are off limits. No online or anti-cheat work.

**Tools (all in `TOOLS\`, not in git):**
- `launch_fc27.ps1`, `gk.ps1` (Escape refused; foreground guard per step), `dev2.py` (0.7 s re-post).
- `match_measure.py`, `ab_width.py`, `ab_run.sh`, `ab_run2.sh` (prepared, not run).
- `channel_e3_probe.py`, `sheet_snap.py`, `attrib_locate.py` (815 s: offline or hub only), `fifamod_catalog.py`, `session24_match.sh`.
- Static RE: the old image `TO\fc27_image.bin` with capstone in `turbo_dev\re_venv`.
- In Turbo: the dev service (ops ping, read/bytes, ptrs, find, scan, dump, write, key, multi, seq).

## A.8 Expected deliverable of the assessment
1. **`REPO\docs\TACTICS_OVERHAUL_DECISION.md`** (committed on turbo-2.0 or a `2.4/tactics-assessment` branch) with:
   - a. One verdict per question Q1-Q11, tagged, with evidence (offline traces, X1-X3 results).
   - b. **The chosen architecture** for G1 and G2 and the rejected ones, with reasons and the residual risks.
   - c. **An ordered implementation plan.** Work packages with files, interfaces, data (knobs file entries), gates and kill switches.
     It also says what to remove or hide: channel A, the E2/E3 paths that do not help, and the unproven knobs.
   - d. **A test plan.** For every work package: the native/Lua tests, plus the in-game pass point with its scripted check (<= 2 min,
     prints PASS/FAIL), the session it runs in, and the evidence file name. It ends with the release gate: every item proven, one release.
   - e. **An agent deployment plan for Opus** (Cassio's rule D8). It lists the tracks and worktrees, what runs in parallel, the critical
     path, the shared serial resources (one game, one dev-service lane, one deploy target), when the review and the merge/release
     rehearsal start, and an ETA.
   - f. The questions for Cassio (A.10), with the answers if he has given them.
2. The raw outputs of each track in `TO\assessment_20261014\` (or the date of the run).
3. `EV\<date>\TEST_24.md` (or the next number) for X1-X3, with screenshots.
4. An update to `docs/HANDOVER.md` (sections 00, 10 and 11) and a memory note.

**Do not** write product code before the decision, except tools for the experiments. **Do not** release.

## A.9 Suggested ultracode workflow shape (sized to the problem)
Per memory `feedback-ultracode-when`, say up front where fan-out helps. It helps in the offline evidence sweep and the review panel, which
are broad and independent. It does not help the live sessions: there is one game and one dev lane, so they are serial and
coordinator-only. Size estimate [A]: about 12-14 agent runs and about 40 min of game time including launches.

| Phase | Who | Content |
|---|---|---|
| 0 Ingest | Orchestrator | Read this brief, handover_24/01-04 and HANDOVER 00/10/11. Check git and the game state. Send Cassio the A.10 questions in ONE message (do not block on the answers). |
| 1 Evidence sweep | **6 parallel offline agents**, read-only. No game, no dev service, no deploy. Each writes `T<n>.md` with [V]/[A]/[U] and file:line or address evidence. | **T1 attribulator consumers** (static, old image): Q1, Q11, Q6; read-site classes, plus the predicted outcome of B1 and B2. **T2 match tactic path** (static): decode MCOPY, the converter, the teams.* read at match build, TeamState and the match clock reachable from M (route C); deliver a read-only kick-off readout tool spec or script for X2: Q2, Q4. **T3 Edit Tactic source** (static, plus `1012_svc_B00_1400.bin`, CHANNEL_E3, LAYER1): TM_CacheTeamSheets / applyCurrentTeamSheet, LoadTeamSheets targets, when svc+0xA60 is filled; the sentinel plan for X3: Q3, Q4, Q5. **T4 measurement** (24-C data): noise floor, stratification, window length, pass criteria, `match_measure.py` improvements tested offline; design X1. **T5 tuning coverage**: a chain locator from G for all 21 knobs and the 90 pm_tacticalrole entries; the X1 read-only check (under 60 s); rank the knobs by expected visible, state-independent effect: Q7. **T6 code and process audit**: what each architecture can reuse in `SRC\`, what to delete or hide, per-build fragility (`exact_build`), the gaps in B.9, tools into git. |
| 2 Experiment pack | 1 agent + orchestrator; 1 skeptic | Turn T1-T5 into the X1-X3 scripts and plans: offline-proven, time-boxed, kill on every path, pass criteria fixed in advance. The skeptic checks them against Part C (Escape, saves, Team Management last, foreground, coordinator-only writes, the dev2 nested-write gap). |
| 3 Live | **Orchestrator only, serial**; Cassio's "go" | Run X1, X2 and X3 (X4 only with his yes). Backup and md5 before, kill and md5 after. Write the evidence to TEST_24.md. Kill at once on FAIL; fix offline. |
| 4 Synthesis | 1 agent, high effort | Write the A.8 decision document draft. |
| 5 Adversarial verification | **3 parallel skeptics** | (a) Evidence auditor: checks every [V] claim against its file. (b) Red team / pre-mortem: "the chosen architecture fails in game because ...", and checks whether the experiments really decided the questions. (c) Feasibility and rules: can Opus implement it within the rules, the budgets and Cassio's constraints? Then one fix pass; the orchestrator signs off. |

Re-use the shape of the earlier workflow `TO\WF_turbo_2_2_channel_e.js` (shared RULES string, `pipeline` / `agent` with a findings
schema). Every agent prompt must carry the hard rules in A.7.

## A.10 Questions for Cassio (one short message at Phase 0; do not decide these silently)
1. Scope for "for good": team shape, pressing and marking, runs, and roles at minimum. Do CPU AI behaviour, passing/shooting realism and
   career boosts (`pm_attributeboosts`, momentum) also belong?
2. Engine-wide or per club: the tables are engine-wide. Does he want his club to behave differently from the CPU clubs (per-club
   profiles), or the same engine behaviour for everybody?
3. Persistence: should tuning re-apply itself at every game start / career load, like a mod? (`tuning_keep.json` exists, off by default.)
4. D12: may Turbo read a `.fifamod` he owns, locally, and apply its values live? Nothing would be redistributed.
5. Testing persistence (Q8) needs one save of a throwaway career that is restored from backup afterwards. Is that allowed, and on which
   copy?

---------------------------------------------------------------------------------------------------------------------------------------

# Part B - Implementation brief for Opus (extra effort)

Implement exactly what `docs/TACTICS_OVERHAUL_DECISION.md` decides. Where it is silent, this part gives the defaults. When the work starts
(rule D8), first show Cassio the agent deployment plan from the decision document: tracks, parallelism, critical path, shared resources and
ETA.

## B.1 Start point
- **Base:** `turbo-2.0` at the commit that holds the decision document (89e0c24 or later). Do not start from a 2.3 work branch.
- **New worktrees.** Number the version 2.4.0, since the 2.4/channel-e3 branch already exists:
  ```
  cd "C:/FC 27 Live Editor/turbo_dev/2.0/integration"
  git worktree add "../wt-24-<track>" -b 2.4/<track> turbo-2.0
  ```
  Then copy the gitignored `turbo/le27/libs` and `turbo/le27/fc27_db_schema.json` from the integration worktree.
- **`2.4/channel-e3` (468fe54):** local only, one commit on 89e0c24. Merge it only if the decision keeps E3. It was never run in game.
- **Branches from before the 2026-10-11 masters rewrite** carry old SHAs. Run the filter in HANDOVER section 0 before merging any of them.
- **Tools.** `TOOLS\` is not in git. If the decision says to version the tools, copy them into the repo (for example `tools/`) with no
  game data and no mod values.

## B.2 Codebase map (turbo-2.0 @ 89e0c24; line counts [V])
| Layer | Files | Notes |
|---|---|---|
| Engine tuning | `SRC\core\tuning.h` (527) / `.cpp` (2873), `SRC\win\tuning_win.{h,cpp}`, `SRC\ui\ui_tuning.{h,cpp}` (1166), tests `turbogui\tests\native\test_tuning.h` (1978), data `turbo\package\turbo\tuning\knobs_6AC07E31.json` (schema `turbo-tuning-knobs/1`, v3, 7 locators, 21 knobs, 10 verified) | Data-driven registry; locator kinds `attrib` (by type and field names), `chain` (signature anchor -> deref chain -> header checks), `native` (`tuning_win.cpp native_locators()` returns `{}` today). Every knob has `"when": ["hub"]`. A new `attrib` knob needs no code change: it needs a live identity proof plus `verified: true`. The static anchor G 0x14C26B128 / `get_current_gameplay_attribdb` 0x141077070 is **not used yet**, so each launch still pays a 24 s heap search. UI: Competitions > Gameplay tuning. |
| Channel E / E2 / E3 | `SRC\core\team_sheet.h` (661) / `.cpp` (2507), `SRC\win\team_sheet_win.{h,cpp}`, `SRC\ui\ui_live_tactic.{h,cpp}` (1125), tests `test_team_sheet.h` (2227) | Gates G1-G6, a role allow-list from the other clubs' sheets, a kick-off check through a game_tick listener, a DB mirror opt-in. UI: Teams > (club) > Tactics > My team card. |
| Channel A (refuted) | `SRC\core\gameplay_sliders.*`, `match_setup.*`, `sliders.cpp`, `ui\ui_match.cpp` | Off by default. Remove or hide it per the decision. |
| DB rows / badges | `SRC\core\tactics_db.*`, `tactics_status.h` (511; badges + kill-switch table l.255-259), `tactic_profiles.*` (615), `opposition.*`, `opp_rules.*` | `dead_target_reason()` refuses `cm_mentalities.*`. |
| Preview | `SRC\core\tactics.*`, `ui\pitch_view.cpp`, `ui\ui_tactics.*` (1877) | Writes nothing. |
| Signatures | `SRC\core\sigscan.h` (176) / `.cpp` (1130) | `Signature{name, pattern, resolve, offset, note, expect, hook_probe, exact_build, adapt_optional}`, positional. The table for 6AC07E31 = `with(fc27_signatures(), team_sheet_signatures())`; the 11 channel E entries are `exact_build`. 140 signatures in 70 s, serial. Auto-adapt is all-or-nothing (`signature_adapt_off.txt`). |
| Game thread and hooks | `SRC\core\gamethread.*`, `SRC\win\game_hooks.*` | `host::run_on_game_thread(fn)` -> "hook" / "lua" / "". Queue of 256, at most 32 jobs per frame. `add_game_tick_listener` (at most 8, never removed). `install_game_hook(name, sig, detour, &orig)` with `HOOK_BODY` that always calls the original; `hook_<name>_off.txt`. MinHook only, never patch game code. Tuning and channel E refuse unless the game_tick hook is live. Job deadline 2 s; lost after 10 s. |
| Lua bridge | `turbo\package\lua\libs\v2\imports\turbo\bridge.lua` (`PHASE_EVENTS` l.572, `SAVE_EVENTS`, `TEAM_MGMT_EVENTS`, `track_phase` l.597; `bridge_state.json` match_phase / phase_t / save_t / team_mgmt) | Feeds gate G2. The phase reads hub -> prematch -> hub (about 0.5 s) -> match at kick-off [V log], so a hub-only gate opens briefly before kick-off. |
| App wiring | `SRC\ui\app.h` l.177-180 (`team_sheet`, `tuning` shared_ptr), `SRC\win\overlay_dx12.cpp` install order: game_hooks (1071) -> match_setup (1091) -> team_sheet (1095) -> tuning (1099) | Tests inject simulated services. |
| Re docs | `docs/re/game_thread.md`, `match_setup.md`, `realtime_transfers.md`, `docs/plans/tactics-2.1/` | Background. |

Facts behind the code: `TO\TACTICS_CHANNEL_E_REPORT.txt`, `TACTICS_LAYER1_FINDINGS_20261012.txt`, `CHANNEL_E2_IMPL.txt`,
`CHANNEL_E2_REVIEW.txt`, `CHANNEL_E3.txt`, `TACTICS_MOD_CATALOGUE.txt` (s.4 field catalogue, s.7 locate plan, s.8 write safety, s.9
measurement), `ATTRIBULATOR_LIVE.json/.txt`, `TUNING_FRAMEWORK.txt`, `MATCH_POSITIONS_STATIC.txt`.

## B.3 Conventions
- **Line endings.** No `.gitattributes`, `core.autocrlf=false`. These files are **CRLF**: `ui/app.h`, `ui/ui_tactics.cpp`,
  `tests/native/test_main.cpp`. These are **LF**: team_sheet.*, tuning.*, ui_live_tactic.cpp, ui_tuning.cpp, sigscan.cpp, CHANGELOG.md,
  HANDOVER.md. After a merge, `git ls-files --eol <changed> | grep mixed` must print nothing.
- **A new .cpp needs 3 registrations:**
  - `turbogui/scripts/build_win.sh` (source list l.43-56);
  - `turbogui/tests/native/run_native.sh` (l.100-113; no `src/win/*`);
  - `tests/native/test_main.cpp` (`#include`, `xxx_test::ui_cases(app, ui, le)`, `xxx_test::run()`).

  If the native total after a merge is not the sum of both branches' counts, a registration was lost.
- **Expected counts.** 2.3.0: LUA 405/0, NATIVE **38462**/0. 2.4/channel-e3: 38488/0. Build: 0 warnings.
- **Git.** Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never use bare `git stash`: the stack is shared
  across worktrees. Never force-push.
- **What never goes into git or the zip:** callname masters, the asset catalog, gate files, banks, mod values, real player data. Check with
  `scripts/package.sh --guard`.
- **Honest UI.** Offer a control as working only after its in-game proof. Unproven controls are shown read-only, or left out and listed as
  "not yet working".
- **Unknowns.** Anything the decision marks unknown is refused in code, not guessed.

## B.4 Safety rules for every game write (checklist; the existing tuning and channel E code already follow it)
1. An opt-in file in `TO\` (G1) plus kill switches. The kill always wins, and it is re-read in every job. Use the existing names where they
   fit: `tuning_on.txt` / `tuning_off.txt` / `call_attrib_off.txt` / `tactics_off.txt` / `call_team_sheet_{on,off}.txt`. Any new write
   path gets its own `*_off.txt`.
2. Phase gate: the knob's `when` phase, no save in the last 30 s, not in Team Management (unless the decision proves otherwise). Re-check
   it **inside** the job.
3. Build gate: the knobs file / signature table of the running build; `exact_build` where layouts are proven for one build only.
4. **Identity re-check in the same job:** TypeInfo at +8, Name at +0x18, vtable at +0, header checks. Also check the club, the career
   load and the service. A vtable-pair hit alone is not enough: it can be a freed copy.
5. **Owner rule:** write a copy only while it holds FC 27's value or Turbo's last write. Otherwise refuse: someone else owns it.
6. One game-thread job: re-locate, **snapshot**, write, **read back every field**, and roll back everything on any mismatch. A mismatch
   switches the feature off for the session.
7. **Undo** that survives the UI: `*_undo.json` bound to the career load. Undo, Reset and Restore are never blocked.
8. Clamp to generic ranges (no mod values). Keep band invariants (bands apart). Never write pointers, strings or array shapes.
9. Hooks: MinHook with `HOOK_BODY` that always calls the original, a per-hook off switch, and no patching of game code.
10. Log one grep-able line per write with old -> new and "(read back ok)". That line is the evidence.
11. The values die with the process. Re-apply them only through an opt-in mechanism (keep / profiles) at the proven moment.

## B.5 Build, test, package, deploy
```
cd "<worktree>" && bash scripts/msys.sh "bash scripts/check_all.sh"      # ~3 min; BUILD/LUA/NATIVE/PY/CATALOG; logs turbogui/build/check_logs
bash turbogui/scripts/parallel_compile.sh                                  # fast full build (~30 s)
bash scripts/package.sh --guard                                            # name guard (Git Bash, ~1 s)
bash scripts/msys.sh "bash scripts/package.sh"                             # dist/FC27_LE_Turbo_<ver>.zip + .sha256 (version.lua == kGuiVersion)
bash scripts/deploy.sh                                                     # GAME CLOSED; from the worktree whose build you want; backs up replaced files
```
- MSYS2 UCRT64 is at `C:\msys64`. Git Bash has no `zip`.
- The integration worktree's `turbogui\build\win` dates from 2026-10-11 and is stale. Deploy from the worktree you built.
- Version bump, 3 places:
  - `turbo/package/lua/libs/v2/imports/turbo/core/version.lua` (LF);
  - `turbogui/src/ui/app.h` `kGuiVersion` (CRLF; rebuild after the bump);
  - CHANGELOG `## Unreleased` -> `## 2.4.0 (...)`.

## B.6 In-game proof (how it must be produced)
- **Every work package names its in-game pass point and its scripted check before code is written** (the decision document's test plan).
  The check is offline-proven, at most 2 minutes, prints its own PASS/FAIL, and kills the game on every exit path. Patterns:
  `TOOLS\session24_match.sh`, `ab_run.sh`.
- **Agents never launch the game or use the dev service.** They hand the coordinator the check. The coordinator batches checks into as few
  sessions as possible, following Part C.
- A tuning knob is "working" only with a measured on-pitch effect that meets the A.5 design: pre-kick-off write, ABAB, both teams,
  stratified, pre-registered criterion. A G2 item is "working" when the match's own copy (and Edit Tactic, if claimed) shows the written
  value.
- Evidence goes to `EV\<date>\TEST_<nn>.md`: build sha256, launch time, each check PASS/FAIL with screenshot name and the exact log line,
  kill time, md5 result.

## B.7 Release rule and steps
- **Ship only when working.** No tag, release push or GitHub release until EVERY item in the version has its in-game pass point proven
  (evidence plus screenshot). A failing item is fixed and retested, or taken out of the version, and Cassio is told. "Experimental opt-in"
  no longer counts as shippable. Work-branch commits and pushes are fine. Once proven, the agent publishes itself (`feedback-publish-always`).
- Steps (template `TO\RELEASE_23_CHECKLIST.txt`):
  1. Clean worktree; masters check.
  2. Merge into `2.4/release` with `--no-ff`; eol check.
  3. check_all with the expected counts.
  4. CHANGELOG from the in-game results.
  5. Version bump.
  6. Build, guard, package; `unzip -l` grep.
  7. Back up the saves; deploy with the game closed.
  8. Final in-game smoke on LTEST (never saved, killed).
  9. `git merge --ff-only` into turbo-2.0; `git tag -a v2.4.0`.
  10. Pre-push scans; `git push origin turbo-2.0 v2.4.0`.
  11. GitHub release through Claude in Chrome. Chrome is signed in to GitHub; there is no `gh` CLI. Recipe: orphan `tmp-release-assets`
      branch, JS upload, digest check through the API, delete the temp branch.
  12. HANDOVER 00 plus a fresh 11; memory note; commit and push.

## B.8 Shared serial resources (plan the tracks around them)
- One game instance.
- One dev-service request file. The `turbo_dev_client.lock` directory serialises every client, and calls take 4-6 s each under contention.
- One deploy target (`LE\`), and only with the game closed.
- Start-up costs per launch: the signature scan takes 70 s (until then there is no game_tick dispatch and no channel E); the tuning locate
  takes about 25 s at the first tab open.
- Only offline work parallelises.

## B.9 Known gaps and traps in code and tools (fix or design around)
- **`TOOLS\dev2.py` write gate (l.31)** inspects only the top-level op and the STRING entries of `ops`/`steps`. A `multi`/`seq` with dict
  sub-ops `{"op":"write",...}` gets through. Fix it before any agent tool uses multi or seq.
- The tuning heap search takes 24 s at each launch. A `chain` locator from G (a signature for 0x141077070) would make it deterministic.
- Knobs are hub-only. The phase reads hub -> prematch -> hub -> match at kick-off.
- `TO\tuning_keep.json` (stale `shape.width_in` 1.4, `"on": false`) was moved aside at 20:08; there is no keep file now.
- The match object global G1 (route C) exists only in `TOOLS\match_measure.py`, not in Turbo.
- Stale docs:
  - HANDOVER sections 1, 4, 5 and 8;
  - `docs/turbo-reference.md` (titled 0.2.5; old test counts; it calls `call_team_sheet_on/off.txt` "reserved");
  - `scripts/msys.sh` points to a missing `docs/BUILD_WINDOWS.md`.
- 79 worktrees exist, and the old ones are delete candidates (HANDOVER 10). Never delete a worktree with uncommitted work.

---------------------------------------------------------------------------------------------------------------------------------------

# Part C - Session protocol (any LLM driving FC 27 + Turbo)

## C.1 Hard rules (never break)
1. **Never send Escape (VK 27)** by any route: computer-use, gk.ps1 (it refuses), or the dev `key` op. It tears the Claude session down.
2. **LTEST only, never saved.** No load-for-write and no save of TORINO, LB or LEICESTER without Cassio's explicit chat confirmation.
   **Answer NO** to "Would you like to save your changes to your team sheet?" (YES autosaves the career; it happened on 10-12 10:12 and was
   restored from backup).
3. Back up `CmMgrC*` before every launch and md5-compare after every kill.
4. **Short tests:** one pass/fail point per check, a screenshot or log line, and the kill as the planned last step. Never keep a match or
   the hub running for probing or development. On FAIL, kill at once and fix offline.
5. **Ship only when working** (B.7).
6. Launch only through `TOOLS\launch_fc27.ps1` (Cassio's scheduled tasks LE_NoUAC -> FC_NoUAC) or the desktop `LaunchFC27.lnk`. Never start
   FC27.exe, Launcher.exe or LauncherNoAuth.exe directly. Never use computer-use `open_application`. Never touch the anti-cheat file. No
   online work. Live Editor's DLL and launcher are off limits.
7. Every automatic game write needs an opt-in, a kill switch, a read-back and an undo. Memory writes are made by the coordinator only.
8. Deploy only with the game closed.
9. **Driving with gk.ps1 needs Cassio's chat "go" at session start.** Tell him when a session starts: `gk.ps1` and `front` pull the game
   to the front even while he works in another window.

## C.2 Pre-flight (game closed)
1. A plan file `TOOLS\SESSION_<n>_PLAN.md`: one launch, at most about 8 min, the pass/fail observation per check, Edit Tactic / Team
   Management last, the kill last.
2. Every live tool proven offline (mock or dump), time-boxed, printing PASS/FAIL. The wrapper kills the game on every exit path.
3. Back up the saves and record the md5:
   ```
   S="$LOCALAPPDATA/EA SPORTS FC 27/settings"; B="C:/FC 27 Live Editor/turbo_dev/backups/$(date +%Y%m%d-%H%M)-<label>"
   mkdir -p "$B" && cp -p "$S"/CmMgrC* "$B"/ && (cd "$B" && md5sum CmMgrC* | cut -c1-8 | tr '\n' ' ')
   ```
   Compare `CmMgrC*` only: the game rewrites `Settings<timestamp>` at every launch.
4. Deploy if needed (B.5) and check the sha256. Touch only the opt-ins this session needs, and check that no kill switch masks them:
   ```
   cd "C:/FC 27 Live Editor/turbo_output" && touch tuning_on.txt && ls call_team_sheet_off.txt tactics_off.txt tuning_off.txt 2>/dev/null
   ```
5. Write `GAME_STATE.txt` = LAUNCHING. Get Cassio's "go".

## C.3 Launch
```
cd "C:/FC 27 Live Editor/turbo_dev/tools" && printf 'state: LAUNCHING (%s) - <purpose>, coordinator only.\n' "$(date +%H:%M)" > GAME_STATE.txt \
 && powershell -NoProfile -ExecutionPolicy Bypass -File launch_fc27.ps1 -WaitCloseMin 1; sleep 22
```
- **Exit codes:** 0 game window | 2 an old launcher or game is still running | 3 LE could not install its swap | 4 LE launcher timeout |
  5 game window timeout.
- **Leftover launcher window.** A leftover ELEVATED LE launcher window cannot be closed from here: Cassio must close it.
- **No builds during a launch.** LE needs 16-90 s under CPU load, so do not run builds while the game launches.
- **Timing (10-13):** launch -> LTEST hub about 3 min; -> Tactical View kick-off about 4 min.
- **Relaunch after a kill** with `-WaitCloseMin 1` worked every time.

## C.4 Menu path: launch -> LTEST hub -> Tactical View (proven 10-13)
`P="powershell -NoProfile -ExecutionPolicy Bypass -File gk.ps1"` (run from `TOOLS\`). Take a screenshot after EVERY seq:
`$P shot "<EV>\<date>\<HHMM>_<label>.jpg"`.

| Step | Command | Lands on |
|---|---|---|
| L0 | `$P front; sleep 1; $P seq "key:shift,wait:1500,key:enter:0,wait:6000,key:enter:0,wait:5000,key:enter:0,wait:8000,key:shift,wait:1200,key:down,wait:700,key:down,wait:700,key:down,wait:1200"` | Settings-load dialog, intro, press-any-key, main menu Down x3 = MANAGER CAREER |
| L1 | `$P seq "key:shift,wait:1000,key:enter:0,wait:2800,key:shift,wait:1200,key:down,wait:900,key:enter:0,wait:3200,key:shift,wait:1000,key:down,wait:1500"` | LIVE / ORIGINAL; offline prompt Down+Enter = Play Offline; Down = ORIGINAL |
| L2 | `$P seq "key:shift,wait:1000,key:enter:0,wait:4500"` | ORIGINAL screen. **The card MUST read LTEST** (Leicester City, "Last Modified 11 October 2026 7:14 PM") before the next Enter. |
| L3 | `$P seq "key:shift,wait:1000,key:enter:0,wait:26000,key:shift,wait:1000,key:enter:0,wait:18000"` | Continue Career (about 26 s load) + Season Pass notice -> hub |
| Check | `grep -a "Turbo GUI 2.3.0 started" "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" \| tail -1; python dev2.py '{"op":"ping"}'` | Build and dev service are up |
| TV | `$P seq "key:shift,wait:1000,key:up,wait:900,key:enter:0,wait:5500,key:shift,wait:900,key:right,wait:900,key:enter:0,wait:32000"` | Matchday -> Tactical View -> live match at about 01:24-01:42 on the clock |

Match settings: World Class, 8-minute halves.

## C.5 In-hub targets (1400-wide screenshot coordinates)
- **Turbo overlay toggle:** `$P key add 80 1 0` (numpad +, VK 0x6B / 107). The alternative is the dev op `{"op":"key","vk":107}` with
  `allow_write=True`. Close the overlay before sending game keys. If a Turbo text box has focus, the toggle types "+": click a blank overlay
  area first.
- **Channel E card:** Teams 126,97 -> My club 286,134 -> Tactics 879,176 -> My team 447,270 -> channel E card 1196,342.
- **Gameplay tuning:** Competitions 268,97 -> Gameplay tuning 323,126. At the first open per launch, wait 25 s for the in-process locate
  (log: `locator attrib.attackinglinelengthwidth found: ... base 0x...`).
- **Edit Tactic:** overlay off, then `$P seq "key:shift,wait:1000,key:down,wait:600,key:down,wait:900"` (hub focus "Set up your Tactics"),
  then `$P seq "key:shift,wait:900,key:enter:0,wait:6000"`. **Make this the last step before the kill** (C.6).

## C.6 Traps
- **Team Management trap.** After Edit Tactic, the game sits in Team Management, and on 10-13 no route left it without Escape. Tried and
  failed: pointer on "Esc Back" + click, Down + click in place, the Home tab, q, Backspace. **Any Edit Tactic / Team Management check is
  the LAST step; then kill.**
- **Idle menus.** Menus drop to "Press Any Button", and the first key only wakes them. Every seq starts with `key:shift` and a wait of at
  least 1 s.
- **Keys.** Arrows register only as scancode + extended (mode 2, the default). Enter is VK + scan (`key:enter:0`).
- **Offline prompt.** It appears only at the FIRST Manager Career entry per launch; on a second entry L1 goes elsewhere. Screenshot before
  each next seq.
- **No q in menus.** The main menu shows "Press q to reconnect": q = Reconnect -> "EA Servers unavailable" Ok dialogs.
- **Continue Career loads the NEWEST save.** That is LTEST only while LTEST is the newest. Check the card.
- **Foreground guard.** `gk.ps1 seq` stops with `STOPPED: game not in front before '<step>'`. This was added after the 10-13 19:14
  incident, when keys reached Cassio's Chrome tab. `move` and `shot` do not check, and `shot` captures whatever window is in front. Keep
  each seq short (under 15 s) and never chain seqs blind.
- **Team-sheet prompt:** answer NO (C.1 rule 2).
- **Full time.** Do not let a match run to full time: the post-match career flow may autosave [U]. One match per launch [A].

## C.7 Dev service and GAME_STATE
- **Files and logging.** Requests go to `TO\turbo_dev_request.json` and results to `turbo_dev_result.json`. The service polls every
  250 ms, runs each new `id` once, and logs `dev service: request <id> (<op>) done|failed`.
- **Ops:**
  - ping
  - read / bytes (<= 64 KiB)
  - ptrs
  - write {addr, hex} (read back)
  - region(s), modules
  - find (45 s budget)
  - scan / refine / changed / list
  - dump (<= 1 GiB)
  - key
  - multi / seq

  Off switch: `TO\turbo_dev_disable.txt`.
- **Client.** `python dev2.py '{"op":"ping"}'`, or `dev2.call(req, timeout=60, repost=0.7, allow_write=False)`. It takes the lock
  directory `TO\turbo_dev_client.lock` (stale after 180 s) and re-posts every 0.7 s. `write` / `key` need `allow_write=True` (coordinator
  only); see B.9 for the nested-op gap.
- **After every kill:** `rmdir "C:/FC 27 Live Editor/turbo_output/turbo_dev_client.lock"`.
- **`TOOLS\GAME_STATE.txt`.** The first line is `state: <TOKEN> (HH:MM) - <note>`. Tokens:
  - CLOSED;
  - LAUNCHING;
  - MATCH-AB (coordinator-only session, no agent calls);
  - HUB / MATCH (read-only agent calls).

  Since 10-13 18:05 no HUB / MATCH windows are granted for agent development.

## C.8 Kill and post-flight (always, also after a FAIL)
```
powershell -NoProfile -Command "Stop-Process -Name FC27 -Force"
cd "C:/FC 27 Live Editor/turbo_output" && rm -f <the opt-ins of this session> && rmdir turbo_dev_client.lock 2>/dev/null
printf 'state: CLOSED (%s) - <result>\n' "$(date +%H:%M)" > "C:/FC 27 Live Editor/turbo_dev/tools/GAME_STATE.txt"
cd "$LOCALAPPDATA/EA SPORTS FC 27/settings" && md5sum CmMgrC* | cut -c1-8      # must equal the backup
```
- **Session log.** Append the session to `EV\<date>\TEST_<nn>.md`.
- **Restore** (game closed): copy the `CmMgrC*` file back from the backup. An accidental autosave writes a NEW `CmMgrC<timestamp>` file
  with the same slot name: move it out.

## C.9 Evidence conventions
- **Screenshot names.** `<HHMM>_<label>.jpg` names drift from real time. Use the file's mtime.
- **Log proof by exact grep:**
  - `channel E: team 95`
  - `gameplay tuning`
  - `locator attrib.attackinglinelengthwidth found`
  - `Turbo GUI 2.3.0 started`
- **Clocks.** turbo_gui.log uses local time. The LE logs and the launcher log stamp UTC; the launcher log file is named by the local date.
- **Status reports.** Cassio follows from his phone. Send short status + ETA messages every 30 min with screenshots (SendUserFile).
