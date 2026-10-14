# Tactics overhaul: the decision (what Opus builds, how it is proven, how it ships)

Written 2026-10-13 20:50 ET by the assessor (Fable). Cassio, 20:35: "i need you to build a surefire plan and thats it. opus will
implement." This document is that plan. It is written from the evidence in `docs/TACTICS_OVERHAUL_BRIEF.md` Part A and
`turbo_output\handover_24\01..04` (P1-P10 proven, R1-R6 refuted, Q1-Q11 open). It does NOT wait for the open questions: every open
question becomes a scripted gate inside the implementation with a pre-decided branch per outcome, so the implementer never has to stop
and ask. The design is chosen to be correct under every outcome of Q1-Q11 except one (R-G1-1 below), and that one is detected by the first
gate and escalated honestly.

Path shorthands as in the brief: `LE\` = `C:\FC 27 Live Editor\`, `TO\` = `LE\turbo_output\`, `TOOLS\` = `LE\turbo_dev\tools\` (not in git),
`EV\` = `LE\turbo_dev\evidence\`, `REPO\` = `LE\turbo_dev\2.0\integration\` (turbo-2.0), `SRC\` = `REPO\turbogui\src\`.
Tags: [V] verified, [A] assessed, [U] unknown. Build 6AC07E31-2145C000 unless marked old build.

Status of the supporting material: an offline evidence sweep (6 tracks T1-T6 with auditors) and an experiment pack (the X1-X3 session
plans and scripts, proven offline) are being produced in the background into `TO\assessment_20261013\` and `TOOLS\SESSION_25/26/27_PLAN.md`.
They give the implementer ready-made scripts and static traces. **Nothing in this plan depends on them**; where they refine a number
(window lengths, offsets), the implementer takes the refined number. Section 9 lists what to pick up from them.

---------------------------------------------------------------------------------------------------------------------------------------

## 0. The plan in ten lines

1. **G1, the engine tuning layer, is the product.** Turbo applies a *tuning profile* (its own values, FC 27 defaults as the baseline) to the
   attribulator tables at three moments: at boot (as soon as the tables exist, which is the moment a mod's values are already in place), at
   career load, and in the hub before every match. Located deterministically from the static manager slot, never by a 24 s heap search.
   Written to every DB copy. Opt-in, kill switch, read-back, undo, as today.
2. Whether the engine reads a table per tick, at match setup or at career load (Q1) does not change the design: all three moments are
   covered. The one failure mode left (values derived at asset instantiation, before any DLL can act) is detected by gate X1 and escalated.
3. **Only knobs with a measured on-pitch effect get a slider.** Located-but-unproven knobs are shown read-only; unlocated ones are hidden.
   Proof = the A/B protocol in section 5 (extreme value written BEFORE kick-off, both teams, stratified, pre-registered criterion), two
   knobs per launch.
4. **G2, the club's tactic, is kept to what the match provably follows.** The live map-record write (channel E, proven for roles) plus the
   DB mirror for persistence become one write. The match's own copy is read back at kick-off and logged: that log line is the proof.
5. Edit Tactic's display is fixed by gate X3: if the screen reads the inline sheet, the existing 2.4/channel-e3 code ships; if it reads
   another service copy, the same mechanism writes that copy; if it reads a UI-side cache, the display follows after the next career
   load and Turbo says so. A one-click re-apply covers the "Yes on leaving Team Management" overwrite.
6. Channel A, the E2 working-copy writes and the legacy fields are removed. The "accepted, untested" teams.* writes go unless X2 shows the
   match reads teams.*.
7. **Three decision gates first (X1, X2, X3, one short LTEST session each, about 30 min of game time in total), run by the coordinator while
   the code tracks already run.** Each gate has its scripted check and its branch table (section 4). No gate blocks a code track from
   starting.
8. Four code tracks in parallel worktrees (tuning core, club tactic, proof harness, cleanup), adversarial review per track, merge
   rehearsal early, one release branch.
9. **Release gate:** every item in 2.4.0 has its in-game pass point proven with a screenshot or log line in `EV\<date>`; anything unproven is
   taken out, not marked experimental. Then tag, push, GitHub release, handover.
10. **ETA** (section 7): decision gates tonight, code complete by 06:00 10-14, proofs 09:00-12:00, release 2.4.0 on 10-14 by ~15:00 ET. The
    only path that slips a day is X1 outcome 3 (no effect on any knob even when applied at boot), and it is called out.

---------------------------------------------------------------------------------------------------------------------------------------

## 1. Verdicts on Q1-Q11 and what the design does about each

| # | Question | Verdict today | How the plan handles it |
|---|---|---|---|
| Q1 | Does the engine use the tuning tables after load, and when? | [U]. Writes land and read back in hub and mid-match (P2); match runs on; first A/B inconclusive (R6). The engine's own accessor (`get_current_gameplay_attribdb` 0x141077070 -> G 0x14C26B128) returns the DB whose single shared `linelengthwidth` instance Turbo writes (P1), so the instance Turbo writes IS the one the engine resolves [V chain]. When it reads it is open. | Apply at boot + career load + hub (section 3.1). Gate X1 measures B1 (pre-kick-off) vs B2 (mid-match). Outcome table 4.1. |
| Q2 | Which copy feeds the match's LH / BU / DA? | [A] the map record: the old-build match-setup copy takes formation and roles from "this record" and the same site converts buildupplay / defensivedepth (TACTICS_REPORT l.1120); roles from the map record are proven to reach the match (P4). teams.* is read live at match build too (old build [V]); which wins is [U]. | Gate X2 (sentinels + kick-off readout). Branch table 4.2. Design default = map record. |
| Q3 | Where does Edit Tactic load from? | [U]. Not the map record (R3), not the DB row (R2), not a hub working copy (R4). Filled at career load [A]. Ranked: svc+0xA60 inline sheet, another code-keyed copy, a UI-side cache. | Gate X3 (one distinct Line Height per candidate copy, then the screen names the source). Branch table 4.3. |
| Q4 | Is Defensive Approach stored (+0xB4) or derived from Line Height through the bands? | [A] stored in the record for the live screen (+0xB4 changed 0->1 on the game's own save, P6), derived at load (cm_mentalities has no approach column [V]; CPU records are band-consistent [V]). | Turbo writes +0xB4 AND keeps it band-consistent with +0xA4 (G6 already). X2 writes the inconsistent pair LH 95 + DA 0 once to learn which the match uses; X3 the same for the screen. |
| Q5 | Short Passing = 2 or 3? Aggressive = 3? | [U]. CPU data: BU 2 used 279 times, 3 never; DA 3 never. | Turbo keeps refusing 2/3 until X3's last step reads svc+0xF80 +0xB0 after a Short Passing save. The decision is data, not a design input. |
| Q6 | Base or simulation DB in a career match? | [U]. Hub = index 0 base [V]. | Write every DB copy of every group (section 3.1); X1's hub phase reads the current index once. Not a design input. |
| Q7 | Can the 11 unlocated knobs be located live? | [A] yes: the type data gives every slot offset (catalogue s.2/s.4/s.7), and the chain from G is deterministic. | WP-A1 chain locator + `chain_check.py` (< 60 s, read-only) in X1's hub phase. A knob that fails identity is hidden. |
| Q8 | Persistence across save / reload? | [U] for G2 values; tuning values are memory-only [V code]. | G1: the profile re-applies at boot / career load (that is the persistence). G2: the DB mirror is in the write; the game's own save writes the row from the map record anyway (old build [V]). X4 only with Cassio's yes; otherwise the release notes say "persists through the game's own save" without claiming a reload proof. |
| Q9 | Is Tactical View representative? | [A] yes for engine-wide knobs: both teams are AI-controlled and get the same tables. | Every pass criterion requires BOTH teams to move. Sim speed measured once (clock screenshots) and used for the window plan. |
| Q10 | Side effects (record without a new code; mode > -1; TM events)? | [U]; gates G1-G6 already refuse mode > -1 and keep the code untouched. | Unchanged gates. The kick-off readout is the regression check. |
| Q11 | Are the depth bands read by the match engine or only by the UI? | [U]; "pm_" = prematch; the converter 0x141B314B4 may hardcode 30/60/90 [A risk]. | The 6 lineband knobs are NOT in the proof queue for 2.4.0. They ship only if a P-session proves a back-line change; otherwise they are shown read-only. The plan does not depend on them. |

---------------------------------------------------------------------------------------------------------------------------------------

## 2. The chosen architecture, the rejected ones, the residual risks

### 2.1 G1 = Arch A "mod-like tuning profile" (chosen)
- **What:** Turbo writes engine tuning fields in memory from a profile, at boot, at career load and in the hub (section 3.1).
- **Why it is surefire:** a mod's values are in the table before anything reads it. Turbo's boot apply happens at the first game tick after
  the tables exist (main menu, before any career or match). Any later read, per tick / at setup / at load, sees Turbo's value. Career load
  and hub re-apply cover a user who changes the profile mid-session and any table that is rebuilt at those moments (the shared instance did
  not move hub -> match [V]).
- **Rejected alternatives:** Arch "EBX-time" (change the asset before instantiation): out of scope (file repackaging is forbidden; Turbo
  writes live memory only). A mod manager route: forbidden (D2).
- **Residual risks:**
  - **R-G1-1 (the one real risk):** the engine derives per-match or per-player values from a table at asset instantiation, before Turbo's
    DLL runs, and never re-reads the instance. Detection: X1 outcome 3 for width AND for the fallback knob, repeated once with the boot
    apply in place (section 4.1). Escalation: the static trace of the consumers (T1 of the sweep) names the reader; the fix is a hook at
    that reader (Arch D-style, HOOK_BODY, kill switch) in a 2.4.1. Not promised in 2.4.0.
  - R-G1-2: effects small next to game-state noise. Mitigation: extremes (0.6 as well as 1.7), both teams, stratified, two alternations,
    pre-registered criterion; a knob that stays inconclusive after two launches is shipped read-only, not as a slider.
  - R-G1-3: one build. The knobs file is per build; the chain locator refuses on any identity mismatch; `exact_build` where needed.
  - R-G1-4: depth bands (Q11) may be UI-only. They are not promised.

### 2.2 G2 = Arch B (live map record) + the persistence half of Arch C, with X2/X3 gates (chosen)
- **What:** one guarded write of the club's active record (+0xA4 LH, +0xB0 BU, +0xB4 DA, 11 role words) AND the cm_mentalities row
  (buildupplay, defensivedepth), read back, undo; a kick-off read-back of the match's own copy that logs the proof; the display fix per X3;
  a one-click re-apply when the game overwrote Turbo's values.
- **Why:** the map record is the only copy proven to reach the match (roles, P4), its layout is proven (P6), the write is proven (P3), the
  code exists with gates and tests. The DB row is what the game's own save/load uses (old build [V]), so persistence rides on the game's
  mechanism. The display is the only part that needs a gate.
- **Rejected:**
  - Arch C(a), a pre-hook on LoadTeamSheets: a hook on the career load path for a display issue is the wrong risk/benefit; the DB row is
    already written before any load, which is all the hook would achieve.
  - Arch D, a match-setup hook, as the main channel: the output offsets are undecoded, two copies exist, a hook at match setup is a crash
    risk, and it does not fix the display. Kept ONLY as the X2 "neither" contingency (WP-B3).
  - Chasing further copies (E2 working copies: refuted R4; E3 inline sheets: kept only if X3 names them).
  - Channel A: refuted (R1), removed.
- **Residual risks:** R-G2-1 the screen source is a UI-side cache (X3 outcome c): then the display follows a career reload only; honest
  badge; not a blocker for the match proof. R-G2-2 the user's "Yes" on leaving Team Management overwrites and autosaves: detected by the
  owner rule, re-apply offered, never automatic. R-G2-3 `exact_build` signatures (11) break on the next game patch: refuse-closed, as today.

### 2.3 Arch F (profiles and presets): the UX layer on top, in 2.4.0 only as Turbo's own presets
A profile = engine knob set (Turbo's own relative changes) + the club tactic + a scope. 2.4.0 ships: the engine profile (on/off, values,
presets "FC 27 default", "Turbo compact", "Turbo expansive", all Turbo's own numbers within generic ranges) and the club tactic card. Scope
"all clubs" for engine knobs is inherent (engine-wide tables). Per-club CPU tactics and the .fifamod import are 2.5 items, pending Cassio's
answers (section 8).

---------------------------------------------------------------------------------------------------------------------------------------

## 3. Design details the implementer needs

### 3.1 Tuning core (G1)
- **Locate (deterministic):**
  1. Signature `attrib_db_getter` for `get_current_gameplay_attribdb` (0x141077070 on 6AC07E31; it reads G = 0x14C26B128 RIP-relative).
     Bytes: read once, read-only, in X1's hub phase (`bytes` op at 0x141077070, 96 bytes) and from the old image's equivalent (sweep T1/T5);
     wildcard the displacement; `resolve: rip` to G. `adapt_optional: true` (amended by the coordinator after review M4, 2026-10-13: a miss
     on a title update must never switch the other hooks off; it only disables the chain, the name search stays as the fallback),
     `exact_build: false`.
  2. mgr = [G]; the manager's DB table and current index as walked by `TOOLS\attrib_locate.py` / `TO\ATTRIBULATOR_LIVE.json` (mgr
     0x3C6807F0 -> current DB in the 17:41 run). The implementer copies that walk; if the sweep's T5 gives the reflected offsets, use them.
  3. DB -> family slot (gp_positioningformation +0x128, gp_positioning +0x38, gp_customization +0x50, gp_cpuai +0x68, gp_team +0xA8,
     gp_defense +0x138; prematch DB +0x40 pm_teamtacticssettings, +0x48 pm_tacticalrole) -> group slot (catalogue s.7 L3) -> instance.
  4. Identity per instance, inside the same job: +0 vtable (image), +8 TypeInfo == the TI found by type-name string in the image, +0x18 Name
     == the expected asset path, and the first scalar block equals FC 27 vanilla or Turbo's last write (owner rule). Any mismatch: that
     group is refused for the session and logged.
  5. Keep the existing name-based heap locator as a cross-check on the first run per build (both must agree) and as the fallback if the
     signature misses; log which path located.
- **All copies:** enumerate the 15 gameplay DBs + 1 prematch DB (TypeInfo at +8, Name at +0x18 [V]); for each group collect the distinct
  instance pointers across DBs; write all of them (today: base + simulation only). Most groups share one instance [V]; zonal_defense and
  defense_general have a simulation copy [A].
- **Apply moments (`when`):** `boot` (first game tick after signatures + locate succeed and no career is loaded: the main menu), `career_load`
  (the bridge's career-load event; the existing `tuning_keep.json` path already runs there: reuse it), `hub` (on entering the hub, and on
  every Apply). No writes in `prematch`/`match` unless the knob carries `live: true` set by X1 (B2 effect). The Lua phase goes
  hub -> prematch -> hub (0.5 s) -> match at kick-off [V]; the hub re-apply must not fire in that 0.5 s window: gate on "no prematch in
  the last 5 s".
- **Profile file:** `TO\tuning_profile.json` = `{"on": true|false, "preset": "<name>|custom", "values": {"<knob id>": <value>}}` replaces
  `tuning_keep.json`. Written by the UI only. Undo: `tuning_undo.json` bound to the game session (exists). "Restore FC 27 defaults" writes
  the vanilla values from the knobs file and switches the profile off.
- **Gates and switches:** G1 opt-in = profile `on` or the tab's session tick; kill `tuning_off.txt` (all), `tuning_profile_off.txt` (the
  automatic moments only), `call_attrib_off.txt`, `tactics_off.txt`. Kill wins, re-read in every job. Read-back of every field; rollback
  of the whole job on any mismatch; mismatch switches tuning off for the session (exists).
- **Honest UI (three states per knob):** PROVEN (slider enabled; the knobs file carries `proof: {"session": "P2", "evidence":
  "EV/2026-10-14/TEST_25.md#width_in"}`), LOCATED (read-only row "located, effect not proven"), UNLOCATED (hidden; listed under "not yet
  working" in the About text). The package guard refuses a knobs file with a slider-enabled knob that has no `proof`.
- **Knobs file v4:** all tactics groups of the catalogue's top-20 (s.6) as `chain` locators; FC 27 vanilla values only (catalogue s.4 =
  FC 27's own EBX); ranges generic (width 0.5..1.5 engine range per gp_customization ModTeamMin/Max [V], extended to 0.6..1.7 only where
  already proven to read back); `verified` set by the chain check; `proof` set by the P-sessions.

### 3.2 Club tactic (G2)
- **One job:** map record (+0xA4, +0xB0, +0xB4, roles) + DB row (buildupplay, defensivedepth) in the same game-thread job sequence with one
  undo file (`channel_e_undo.json` exists; extend with the DB values). Gates G1-G6 unchanged. The DB mirror opt-in file goes away: the mirror
  is part of the write (kill switch `channel_e_db_off.txt` keeps it off if ever needed).
- **Kick-off self-proof:** the existing game_tick kick-off listener finds the match-setup copies (role words at +0x118 [V]); extend it to
  read LH / BU / DA at the offsets X2 decodes and log one line: `channel E kick-off: match copy team 95 LH 95 BU 1 DA 2 roles 11/11 =
  Turbo (ok)`. That line is the S6 evidence. If X2's readout finds TeamState (DefendingTeamStyle +0x230, Mentality +0x280 [old-build type
  data]), log it too.
- **Display per X3 (WP-B2):** (a) inline sheet svc+0xA60 -> merge 2.4/channel-e3 468fe54 as is (its probe is `TOOLS\channel_e3_probe.py`);
  (b) another service copy -> the same guarded write (code AND XI match) for that offset; (c) UI-side -> no live write; the card says "Edit
  Tactic shows these values after the next career load".
- **Overwrite detection:** in the hub, after Team Management was open (bridge `team_mgmt` flag or the prompt), re-read the record; if it no
  longer holds Turbo's values, show "the game replaced your tactic (LH 50 / Balanced / Balanced). Re-apply?" with one click. Never automatic.
- **Removed:** channel A (`gameplay_sliders.*`, `match_setup.*` gv service, `sliders.cpp` slider block, `ui_match.cpp` tab) with its tests;
  E2 working-copy writes (svc+0xF80/+0x1078/+0x1170/+0x1268); legacy `mentalities` fields; teams.* writes unless X2 = teams.*.
- **Enums:** BU 2/3 and DA 3 stay refused until X3's Q5 read; then the labels are added as data.

### 3.3 Proof harness (in git, `REPO\tools\`, no game data, no mod values)
`dev2.py` (nested write gate fixed), `gk.ps1`, `launch_fc27.ps1`, `match_measure.py`, `mm_stats.py`, `chain_check.py`, `x1_run.sh`,
`x1_write.py`, `x2_readout.py`, `x2_sentinels.py`, `x3_sentinels.py`, `x3_readback.py`, `session_template.md`, `GAME_STATE` protocol. Each
script: time-boxed, prints PASS / FAIL / INCONCLUSIVE, exits 0 / 1 / 3, refuses when GAME_STATE is CLOSED unless `--mock`, read-only
unless its name says write, and the wrappers kill the game on every exit path. Python self-tests with mocks run in check_all's PY step.

---------------------------------------------------------------------------------------------------------------------------------------

## 4. The decision gates (WP0): three short LTEST sessions, coordinator only, pre-decided branches

Rules: Part C of the brief in full (never Escape; LTEST only, never saved, NO to the team-sheet prompt; backup + md5 before, kill + md5
after; one pass/fail point per check, screenshot, kill last; Edit Tactic / Team Management LAST; Cassio's "go" at session start). Plans
and scripts: `TOOLS\SESSION_25_PLAN.md` (X1), `SESSION_26_PLAN.md` (X2), `SESSION_27_PLAN.md` (X3) from the pack; if the pack is not there
yet, X1 can run with the existing `TOOLS\ab_run2.sh` + `ab_width.py` (proven live on 10-13) with the value changed to 0.6 and the
criterion below written into `EV\<date>\TEST_24.md` before kick-off. Evidence: `EV\<date>\TEST_24.md`.

### 4.1 X1: does the engine read the tuning tables, and when? (Q1, Q6, Q7)
Hub: open Gameplay tuning (locate), run `chain_check.py` (read-only, < 60 s: every group located and identity-checked; writes the located
list JSON), read the current DB index, read 96 bytes at 0x141077070 (signature material). Write **width_in = 0.6** in the hub (narrowing
cannot saturate at the touchlines; with-ball widths were 38-51 m). Kick off in Tactical View. Windows B1 45 s (0.6 since before kick-off),
A1 45 s (1.0), B2 45 s (0.6 written mid-match), A2 45 s (1.0); clock screenshots at the boundaries; revert; kill; `mm_stats.py`.
**Pre-registered criterion:** with-ball width of BOTH teams in B1 and in B2 is lower than in A1 and A2 by >= 3 m and >= 2 x |A1 - A2|, with
without-ball width moving less than half of that; p < 0.05 on the block permutation test. "B1 only" = the same for B1 alone.

| Outcome | Meaning | Branch |
|---|---|---|
| 1. B1 and B2 pass | read per tick / live | `live: true` for this group; sliders usable in match; the three apply moments stay. |
| 2. B1 passes, B2 fails | read at match setup or earlier in the session | automatic moments only (boot / career_load / hub); mid-match UI greyed. |
| 3. neither | cached before the hub write, or the metric is wrong | second launch: `runs.frequency` (if located) at the top of its range written in the hub, metric = players ahead of the ball / attacking centroid; if it passes, width is the wrong metric or saturates: re-test width with a longer window after WP-A1 lands; if it also fails, run X1 again once WP-A1's boot apply is installed (value applied at the main menu). If still nothing: R-G1-1, escalate (section 2.1); G1 sliders are not shipped; the release holds G2 + the locate/profile mechanism shown read-only. |

### 4.2 X2: which source feeds the match's LH / BU / DA? (Q2, Q4)
Hub with `call_team_sheet_on.txt` (the kick-off dump `TO\channel_e_kickoff_dump.bin` is written only then). Sentinels (coordinator,
dev `write`, identity re-check, read-back): map record LH 95 + DA 0 + BU 1 (the inconsistent pair); DB teams.defensivedepth 77,
teams.buildupplay 0 (Turbo's Tactics tab); no other copy touched. Kick off; `x2_readout.py` (<= 90 s) decodes the two match-setup copies
and TeamState if found; 45 s back-line measurement; kill.

| Outcome | Branch |
|---|---|
| match copy shows 95 (and DA = Deep -> stored; DA = Aggressive -> derived) | map record is the channel (design default): WP-B1 as written; teams.* writes removed; DA policy per the DA result. |
| match copy shows 77 | teams.* is the channel: the Tactics tab's teams.* write becomes part of WP-B1's job (same gates); map record stays for roles. |
| neither / undecodable | WP-B3 contingency: MCOPY post-hook that sets LH / BU / DA in the output (HOOK_BODY, `hook_mcopy_off.txt`), proven by the same readout. |

### 4.3 X3: where does Edit Tactic load from? (Q3, Q4 screen, Q5)
Hub: locate svc (registry Find(0x113EA820) or VT pair + header checks); write one DISTINCT valid Line Height per candidate copy that exists
in the hub (map record 95; svc+0xA60 active record 85; any other copy the sweep's T3 names: 75, 65), each with identity re-check and
read-back (`x3_sentinels.py`); overlay off; Edit Tactic (C.5); screenshot. Then Q5: select Short Passing, Save Changes, dev read of
svc+0xF80 +0xB0; screenshot; **kill** (Team Management trap).

| Screen shows | Branch |
|---|---|
| 85 | WP-B2 (a): merge 2.4/channel-e3. |
| 75 / 65 | WP-B2 (b): extend the guarded write to that copy. |
| 25 (career-load value) | WP-B2 (c): reload-based display; honest badge; X4 only with Cassio's yes. |
| 95 | the map record IS the source after all and earlier sessions had another cause (e.g. the read happened before Turbo's write): WP-B1 alone fixes the display; verify once more in the final smoke. |

### 4.4 X4 (only with Cassio's explicit yes): one save of a throwaway career copy, reload, Edit Tactic. Decides the wording of the
persistence claim. Without it, 2.4.0 claims "written to the save row" and not "proven after reload".

---------------------------------------------------------------------------------------------------------------------------------------

## 5. Proof protocol for a tuning knob (S1, S2) and the P-sessions

- **Protocol per knob:** the value is written in the hub BEFORE kick-off (through the profile), window B1 45 s, A1 45 s (revert), B2 45 s
  (write), A2 45 s (revert), both teams, samples stratified by possession (own / opponent / dead) and ball third, stoppages dropped,
  block permutation test, criterion pre-registered in the session plan: effect in the predicted direction in BOTH teams, >= the knob's
  minimum effect (3 m for width / length, 2 m for back line, 1.5 m for pressing distance) and >= 2 x |A1 - A2|, p < 0.05.
- **Two knobs per launch:** B1/A1 for knob 1 in the first half, B2/A2 for knob 2 after the revert (the match must not reach full time;
  the sweep's T4 gives the real seconds per half from the sim speed; if a half is too short, one knob per launch).
- **Queue (priority by expected visibility and route-C measurability):** P1 width_in (0.6) + width_out (0.6); P2 length_in (0.5) +
  length_out; P3 runs.frequency (top of range) + pressing.trigger_depth; P4 marking.tightness + lineband (Aggressive band min 91 -> 40 with
  LH 95, back line without ball; only if located); P5 reserves for inconclusive repeats. Any knob inconclusive after two launches ships
  read-only.
- **Metric per knob:** width_* = width with / without ball; length_* = length with / without ball; runs.frequency = players ahead of the
  ball and attacking centroid depth (with ball); pressing.trigger_depth / marking.tightness = pressing distance (without ball) and
  compactness; lineband = back line (without ball).
- **Session cost [A]:** launch -> kick-off 4 min, 4 windows 3 min, kill and post-flight 1 min, pre-flight 2 min = ~10 min per launch.
  P1-P4 = ~40 min of game time.

---------------------------------------------------------------------------------------------------------------------------------------

## 6. Work packages, in order, with files, gates, tests and in-game pass points

| WP | Name | Files (SRC\ unless noted) | Interface / data | Gates and kill switches | Native / Lua tests | In-game pass point (scripted check <= 2 min; evidence) |
|---|---|---|---|---|---|---|
| WP0 | Decision gates X1-X3 | `TOOLS\SESSION_25/26/27_PLAN.md`, pack scripts | branch tables 4.1-4.3 | Part C | offline mocks (pack) | `EV\<date>\TEST_24.md`: X1 verdict + mm_stats output; X2 readout line; X3 screenshot + Q5 value |
| WP-A1 | Deterministic locate + apply moments + profile | `core/tuning.{h,cpp}`, `win/tuning_win.cpp` (`native_locators()` returns the G walk), `core/sigscan.cpp` (+`attrib_db_getter`), `turbo/package/turbo/tuning/knobs_6AC07E31.json` v4 (`chain` locators, `when` boot/career_load/hub, `live`, `proof`), `bridge.lua` (career-load event -> re-apply; reuse the keep path), `tactics_status.h` (switch table) | `TO\tuning_profile.json`; log lines `tuning: chain locate ok <n> groups <m> knobs <ms> ms`, `tuning: profile applied (boot|career_load|hub) <k> knobs (read back ok)` | opt-in = profile on / session tick; `tuning_off.txt`, `tuning_profile_off.txt`, `call_attrib_off.txt`, `tactics_off.txt`; identity per instance; owner rule; rollback; no write in prematch/match unless `live` | simulated image: chain resolution, wrong TypeInfo -> group refused, 16 DB copies with shared instances written once each, apply at each moment, prematch guard window, owner rule across moments, undo/restore, profile file round trip | S-A1: launch, main menu: log `profile applied (boot)`; hub: `chain_check.py` PASS, `profile applied (career_load)` and `(hub)` lines; `EV\<date>\<HHMM>_tuning_boot.jpg` + grep |
| WP-A2 | Knob coverage + honest UI + presets | `ui/ui_tuning.cpp`, `core/tuning.cpp` (states), knobs v4, `tuning_presets\` (Turbo's own), `scripts/package.sh --guard` (refuse slider without proof) | knob states PROVEN / LOCATED / UNLOCATED; `proof` field | as WP-A1 | UI tests for the three states, guard test | P1-P4 sessions (section 5): one `TEST_25.md` entry per knob with the mm_stats verdict; the knobs file's `proof` entries point at them |
| WP-A3 | Roles tuning (stretch; ships only if proven by the release cut) | knobs v4 pm_tacticalrole chain (90 instances), `ui_tuning.cpp` role rows for the user's XI roles (role word -> instance name) | per-role lineInPossession / widthLevel | as WP-A1 | mapping tests | P-session: the player in that role moves as predicted (route C by field position); else read-only |
| WP-B1 | Club tactic: one write + kick-off self-proof + overwrite detection | `core/team_sheet.{h,cpp}`, `win/team_sheet_win.cpp` (kick-off listener decode), `ui/ui_live_tactic.cpp` (card, re-apply prompt), `core/tactics_db.*` (row write in the job), `tactics_status.h` | log `channel E kick-off: match copy team 95 LH .. BU .. DA .. roles 11/11 = Turbo (ok)`; `channel_e_undo.json` + DB values | `call_team_sheet_on.txt` opt-in; `call_team_sheet_off.txt`, `channel_e_db_off.txt`, `channel_e_kickoff_off.txt`, `tactics_off.txt`; G1-G6; code untouched; mode > -1 refused | simulated sheet: write + mirror + undo, kick-off decode on a synthetic copy, overwrite detection, re-apply once | S-B1: hub Apply (read back line), kick off, the kick-off line names Turbo's values; `EV\<date>\<HHMM>_kickoff_line.jpg` + grep |
| WP-B2 | Edit Tactic display per X3 | (a) merge 2.4/channel-e3; (b) `team_sheet.cpp` one more guarded copy; (c) `ui_live_tactic.cpp` badge text | per 4.3 | same as WP-B1 | (a)/(b): copy write + undo tests | S-B2: Apply, then Edit Tactic LAST: screenshot shows Turbo's LH / BU / DA ((c): after X4 or badge check); kill |
| WP-B3 | Contingency only if X2 = neither: MCOPY post-hook | `win/game_hooks.cpp` (`install_game_hook("mcopy", ...)`, HOOK_BODY), `team_sheet_win.cpp` | sets LH/BU/DA in the output after the original | `hook_mcopy_off.txt`, `call_team_sheet_off.txt` | hook body unit test with a fake output buffer | S-B3: the kick-off readout shows Turbo's values |
| WP-C | Proof harness into git | `REPO\tools\*` (section 3.3), `scripts/check_all.sh` PY step | `--mock` self-tests | n/a (read-only tools; write tools refuse without allow_write) | PY self-tests | none (it is the harness); guard: no game data, no mod values, no addresses beyond the documented build constants |
| WP-D | Cleanup, docs, release prep | remove channel A + tests (`build_win.sh`, `run_native.sh`, `test_main.cpp` registrations), legacy fields, `turbo-reference.md`, HANDOVER 1/4/5/8, `scripts/msys.sh` pointer; CHANGELOG `## 2.4.0`; version bump (3 places); `TO\RELEASE_24_CHECKLIST.txt` from the 2.3 template | expected counts updated | n/a | check_all green; native total = sum rule | none |
| WP-E | Release | `2.4/release`: merge --no-ff, eol check, check_all, package --guard, package, deploy (game closed), final smoke, ff-only merge, tag v2.4.0, push, GitHub release via Chrome, HANDOVER 00 + fresh 11, memory note | zip + sha256 | ship-only-when-working | all | S-E final smoke on LTEST: boot line, hub lines, one knob A/B spot check, kick-off line, Edit Tactic last, kill, md5 equal |

**Order:** WP0 (coordinator) runs while WP-A1, WP-B1, WP-C, WP-D start in parallel. WP-A2 starts when WP-A1's locate is testable (same
worktree, same agent). WP-B2 starts when X3 is in. WP-A3 after WP-A2 if time allows. WP-E last.

**What is removed or hidden:** channel A (removed), E2 working-copy writes (removed), legacy mentalities fields (removed), teams.* writes
(removed unless X2 says teams.*), the 11 unverified knobs (hidden until located; read-only until proven), BU 2/3 and DA 3 (refused until
X3's Q5), `tuning_keep.json` (replaced by the profile file), the "experimental" wording everywhere.

---------------------------------------------------------------------------------------------------------------------------------------

## 7. Agent deployment plan for Opus (rule D8)

**Shared serial resources:** one game instance (coordinator only; Cassio's "go" per session), one dev-service lane (`turbo_dev_client.lock`),
one deploy target (`LE\`, game closed), no builds during a launch (LE needs the CPU). Per launch: 70 s signature scan before the game
thread dispatcher exists, 25 s tuning locate at first tab open (WP-A1 removes this).

| Track | Worktree / branch (from turbo-2.0 at the commit holding this document) | Content | Parallel with | Effort [A] |
|---|---|---|---|---|
| T-gates | coordinator session, no worktree | WP0: X1, X2, X3 (X4 only with yes); writes `EV\<date>\TEST_24.md`; posts the branch results into `TO\assessment_20261013\GATES.md` for the code tracks | everything | 1.5 h wall, 3 launches |
| T-tuning | `wt-24-tuning` / `2.4/tuning` | WP-A1, then WP-A2, then WP-A3 if time | all | 8-10 agent-h |
| T-tactic | `wt-24-tactic` / `2.4/tactic` | WP-B1, then WP-B2 per X3 (merge 468fe54 only in branch (a)), WP-B3 only if X2 = neither | all | 5-7 agent-h |
| T-tools | `wt-24-tools` / `2.4/tools` | WP-C | all | 3-4 agent-h |
| T-cleanup | `wt-24-cleanup` / `2.4/cleanup` | WP-D (channel A removal first, docs after) | all | 3 agent-h |
| T-review | fresh agents at each track's code-complete | adversarial review per track (crash and career safety first: gates, same-job identity re-check, rollback, kill switch re-read, no write outside the allowed phases), then a fix pass | per track | 1 h per track |
| T-release | `2.4/release`, rehearsal on `tmp/merge-rehearsal-24` at T+5 h | WP-E; the rehearsal merges all four branches, runs check_all, and reports the native total vs the sum rule; the real merge repeats it | after reviews | 2 h |
| T-proofs | coordinator session | P1-P4 (section 5), S-A1, S-B1, S-B2, S-E | after T-tuning / T-tactic deploy | 1.5-2 h wall, 6-7 launches |

**Critical path:** WP-A1 code (6 h) -> review + fix (1 h) -> deploy -> S-A1 + P1-P4 (1.5 h) -> merge/release (2 h) = ~10.5 h of work
after start. WP0 (1.5 h) runs inside WP-A1's window and only changes WP-B2's branch and WP-A1's `live` flags.

**ETA (start 10-13 22:00 ET):** WP0 done ~23:30; code complete and reviewed ~06:00 10-14 (overnight agents); deploy + proofs 09:00-12:00;
merge, package, final smoke, release ~15:00 10-14. If X1 lands in outcome 3 twice (R-G1-1), G1 sliders slip to 2.4.1 (+1 day of RE and a
hook); 2.4.0 then ships the mechanism read-only plus G2, and Cassio is told before the cut.

**Where ultracode helps:** the per-track adversarial reviews and the merge rehearsal (independent, parallel). It does not help the game
sessions or the merge -> build -> release chain (serial). Plain background agents for the code tracks.

---------------------------------------------------------------------------------------------------------------------------------------

## 8. Questions for Cassio (asked 20:25; the plan assumes these defaults until he answers)

| # | Question | Default assumed in this plan |
|---|---|---|
| 1 | Scope beyond shape, pressing/marking, runs, roles (CPU AI, passing/shooting realism, career boosts)? | 2.4.0 = shape, pressing/marking, runs (+ roles as a stretch). CPU AI (`gp_cpuai`), realism groups and `pm_attributeboosts` / `gp_team` = 2.5, same mechanism, same proof rule. |
| 2 | Engine-wide or per club? | Engine knobs are engine-wide (the tables are shared). Per-club behaviour = the club tactic card (G2). CPU club rows (default_mentalities) = 2.5 if wanted. |
| 3 | Persist like a mod? | Yes: the profile re-applies at boot and career load, default ON once a profile exists; one switch turns it off. |
| 4 | Read a .fifamod he owns, locally? | Not in 2.4.0. 2.5 opt-in: a local reader maps fields by name onto knobs; nothing redistributed. |
| 5 | One save of a throwaway career to test persistence? | Only with his yes (X4). Without it, no reload claim is made. |

---------------------------------------------------------------------------------------------------------------------------------------

## 9. What to pick up from the background sweep and pack (when they land; the plan does not wait for them)

`TO\assessment_20261013\`: `T1.md` (static trace of the tuning consumers: use it to pre-read X1's outcome and, in R-G1-1, to name the hook
site), `T2.md` + `T2\kickoff_readout.py` (the X2 readout and the match-setup copy offsets for WP-B1's self-proof), `T3.md` + `T3\`
(the X3 sentinel plan and read-back), `T4.md` + `T4\mm_stats.py` (window lengths, sim speed, the statistics tool), `T5.md` +
`T5\knobs_6AC07E31_v4_draft.json` + `T5\chain_check.py` (the chain locator spec and the hub check for WP-A1), `T6.md` (reuse / delete /
fragility tables and the exact dev2 gate fix for WP-C/WP-D), `T<n>_verify.md` (audits: where an audit downgrades a claim, the audit wins),
`PACK.md` + `TOOLS\SESSION_25/26/27_PLAN.md` + the `x1_* x2_* x3_*` scripts (WP0 ready to run). If a file is missing when WP0 starts, X1 runs
with `ab_run2.sh` as described in section 4.

---------------------------------------------------------------------------------------------------------------------------------------

## 10. Rules that bind every work package (from Part B/C; repeated because they are the release gate)
- Ship only when working: every item proven in game with a screenshot or log line in `EV\<date>`; failing items are fixed or removed.
- Every game write: opt-in, kill switch (re-read in the job), identity re-check in the same job, snapshot, read-back of every field,
  rollback on mismatch, undo that survives the UI. Hooks: MinHook + HOOK_BODY, always call the original, per-hook off switch.
- Never Escape. LTEST only, never saved, NO to the team-sheet prompt. Backup + md5 before every launch, compare after every kill.
- No mod values in git or the zip. Live Editor's DLL and launcher off limits. No online or anti-cheat work. No mod-manager route.
- Agents never drive the game, never use the dev service, never write memory; they hand the coordinator a scripted check <= 2 min.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; no bare `git stash`; never force-push.
