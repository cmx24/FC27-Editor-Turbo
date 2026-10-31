# Consensus action plan (2026-10-31)

Reviewers: Claude Fable 5.1 (Anthropic; repo cloned), Gemini 3.6 Flash (Google; pack plus source excerpts), GPT-6.1 Sol via Microsoft 365 Copilot (Microsoft; pack only). Collator: Claude Fable 5.1 (conflict of interest declared: same vendor as the party under review; the owner should spot-check this file against `ROUND2_TABLE.md` and the three `*_round2.md` files). Rounds completed: 1 / 2 / 3 (round 3 = this draft, endorsements below).

Rules applied, verbatim from `CROSS_REVIEW_PROTOCOL.md`: a step enters if a majority (2 of 3) include it; a minority step goes to Dissent with names and reason; each step carries the strictest proof any reviewer attached; the owner's original order (menus, one picker, renders, saved sizes, sliders, audio, copy, tactics) wins unless a majority reordered for a stated dependency reason; the collator adds no step and softens no wording.

## Verdict scores (round 1, unchanged)

| Line | Claude | Gemini | Copilot | Median |
|---|---|---|---|---|
| GUI as shown | 2 | 1 | 2 | **2** |
| Code quality | 4 | 2 | 3 | **3** |
| Reporting honesty | 4 | 1 | 3 | **3** |
| Pit Wall value | 2 | 0 | 2 | **2** |
| Failsafe plan credibility | 5 | 5 | 4 | **5** |

## Agreed steps (majority)

Reordering note: steps 1–2 precede the owner's list because two reviewers (Claude, Copilot) made them prerequisites for a stated reason: three recorded losses of the owner's files (pack B§3) and the gap between the tested commit 075a53a9 and the reviewed head 00b28622 (pack 0, D, E). Steps 3–4 (saved sizes, audio) and step 5 (tactics study) move ahead of menus because two reviewers (Claude, Copilot) require existing code to be tested before anything is rewritten, and because the tactics study was eighth in a serial plan where "eighth never starts" (Claude) and feasibility belongs early (Copilot). The remaining steps keep the owner's order.

| # | Step | Who | Proof of done (strictest attached) | Stop condition | Votes |
|---|---|---|---|---|---|
| 1 | Protect the owner's files before any change: backup of settings, presets and careers; rollback rehearsed once on LEICESTER | Assistant, owner authorizes | A recorded test rollback restores the baseline and every protected file; md5 list before and after | Any unexplained file change or failed restoration | 2 (Claude, Copilot) |
| 2 | Build identity: one record per build linking source commit, packaged DLL md5 and installed DLL md5; every code finding and every proof pinned to the tested commit, never to a worktree or branch head | Assistant | The record exists for every DLL the owner touches; any proof names its md5 | Any unresolved mismatch | 2 (Claude, Copilot) |
| 3 | Saved sizes, tested on the installed DLL before any rewrite: window, panes, columns, rows and frames persist across a game restart; existing presets intact | Assistant builds, owner verifies | The owner resizes, restarts the game, sees the same layout and his presets; one recording of the sequence including any failure | 1 day; then the blocker is reported, not rewritten blindly | 3 |
| 4 | Audio, tested on the installed DLL before any rewrite: the existing player-row Preview is audible; the club-card preview is resolved separately | Assistant builds, owner verifies | The owner's own "I heard it" on a named row, on a clip that shows the selected row and the sound; a defined failure sample also tested | 1 day; 1 more day for an alternate backend; then cut, with the owner's agreement | 3 |
| 5 | Tactics: a time-boxed feasibility study with a defined expected on-pitch effect and a reversible test; working control or tile removed, no third option | Assistant studies, owner decides the cut | A one-page study with the measurement, or a controlled demonstration of the requested effect; removal only on the owner's decision | 2 days | 3 (study); 2 (owner decides: Claude, Copilot) |
| 6 | Cut menus by deletion, replacement proven in the same build: "Everything (classic)" (`ui_pages_216.cpp:149`), the ten-tab classic page (`ui_players.cpp:1392-1522`), the second Callnames page (`ui_shell.cpp:35`), the Advanced area, `tree40()` (`ui_shell.cpp:153`); every function reachable from the old pages reachable from the new | Assistant | `grep -rn "classic\|advanced_mode\|tree40" turbogui/src/ui` returns nothing, and one screenshot per area on the installed DLL, and the retained-functions list ticked by the owner | 2 days | 2 (Claude, Gemini); Copilot dissents on deletion-as-proof, see Dissent |
| 7 | One picker route (`ui_visual_picker`) for hair, beard, accessories, tattoos, body types; the `ui_images.cpp` galleries and the `ui_faces3d_gate.cpp` modal removed in the same change; renders on tiles; missing-preview count disclosed | Assistant | `grep -l "styles match" turbogui/src/ui` = one file; four screenshots with identical layout and the missing count visible; one recorded choose-apply-inspect-undo per field on the installed DLL | 3 days | 2 (Claude, Gemini) on this wording; Copilot votes for one picker route but with the old routes retired only after the replacement is proven and no route preselected, see Dissent |
| 8 | Settings: feasibility of a live write first; then real `SliderInt` for verified rows writing the running game; the `turbo_settings_apply.exe` staging path and its yellow paragraph removed; one-line note for rows that cannot be written; reduced scope only with the owner's approval | Assistant, owner approves any reduced scope | `grep -c SliderInt ui_game_settings.cpp` ≥ 1; `grep -c "turbo_settings_apply" turbogui/src/ui` = 0; the owner sees the moved value on EA's settings screen | 2 days; if live write is impossible, a bounded failure report and the owner decides | 3 |
| 9 | Copy, done with each step above and not as a last sweep: at most three words per table cell, one help line per screen, state sentences removed from `core/` as well as `ui/`; warnings kept where the user can act on them | Assistant | `grep -c "read-only\|not seen on the EA\|mapped (game table)" turbogui/src` = 0, plus the screenshots already required above | With each step | 2 (Claude, Gemini) on the word limits and grep proof; Copilot votes for copy-with-each-feature but rejects rigid limits and grep as proof, see Dissent |
| 10 | Release only the accepted build: the release candidate repeats every accepted proof above and restores cleanly to baseline; no arbitrary version number, no weekly quota | Assistant prepares, owner authorizes | The owner installs one package, repeats the proofs on it, and the rollback from it works | Any failed proof, md5 mismatch or preservation failure | 2 (Gemini, Copilot) |

## Dissent (minority insists)

| Step | Who insists | Reason |
|---|---|---|
| Written scope freeze: no new features, hooks, agents or branches beyond one per item, as a one-line README entry | Claude | The plan has rules but no freeze; new scope (kits, archetypes, country launcher) kept arriving while the eleven asks were open |
| Purge the Pit Wall scripts, hooks, manifest and charters from the repository as step one | Gemini | A surviving hook is where "built, not proven" returns; the tooling consumed the budget the features needed |
| Owner-approved compact acceptance list covering the eleven asks and the older open asks, with a visible spending limit per item | Copilot | Asks disappeared from hand-offs for days (OVR mask, visual pickers); a percentage snapshot is not a spending limit the owner can track |
| Owner approves one representative navigation screen and one picker design before broad implementation | Copilot | The owner's objections were about concrete interaction and appearance; one agreed example makes rejection cheap |
| Deletion is not proof of repair: replace, test, then retire; grep counts prove removal, not working behaviour | Copilot (on step 6 and the grep proofs in 7–9) | Claude itself warned that deleting the galleries breaks callers in `ui_looks_studio.cpp` and `ui_bulk_edit.cpp` |
| Keep the three owner decisions in the plan (ship FC Editor pictures, keep or undo DLL A, cut Tactics) | Copilot | Scope cuts and third-party picture rights are the owner's decisions; redistribution rights for the pictures are not verified |
| "GAME IS YOURS" is a lie, not a misreport | Gemini | A completion headline on a build with ten unproven items is intentional misrepresentation regardless of the fine print |

## Deleted from the assistant's plan by majority

| Item | Why |
|---|---|
| "What I need from you" (A§4): the three questions to the owner | Claude and Gemini: all three have defaults and a failsafe plan should not open with asks. Copilot dissents (above); the majority deletes the block but step 5 and step 8 keep the owner's decision on cuts, which meets Copilot's objection in substance |
| Screenshot-only proofs ("one screenshot of the player screen", "a clip", "every screenshot above") | All three: replaced by the strictest proofs in the table (owner-verified, on the installed DLL, with grep where code removal is the claim) |
| The serial order menus → picker → renders → sizes → sliders → audio → copy → tactics | Claude and Copilot: existing code is tested before anything is rewritten; tactics feasibility is decided early |
| "Keep `ui_visual_picker`" asserted without comparison | Copilot (plan item 2) and Claude (callers in other files): kept as the route, but step 7 now requires apply/undo proof per field before the others are removed |
| The 5 % weekly budget cap as the only limit | Claude adds a stop date per step (these appear in the table as time-based stop conditions); Copilot replaces the percentage with a spending limit the owner can track (this does NOT appear in the table: a spending limit had one vote and sits in Dissent). Collator's correction after Copilot's round-3 note |
| B§5 "the standing order from 10-31" | Copilot: the owner's quoted words ordered a review and a plan, not adoption of the workflow; Claude accepts in round 2. The plan above is a proposal until the owner's decision line below is written |

Not deleted (no majority): the "built"/"staged" word ban (Copilot alone); the "one model (Opus)" rule (Copilot alone); the "about 2026-11-10" date (Copilot alone, though no reviewer endorsed the date).

## Endorsements (round 3)

| Reviewer | Endorse / Dissent | Five lines (verbatim) |
|---|---|---|
| Claude Fable 5.1 | Endorse, with two lines of dissent | Endorse the ten steps and their order. Dissent 1: a written scope freeze should be step 0; without it the eleven asks compete with new scope again. Dissent 2: Copilot is right that grep proves removal, not behaviour, so the owner should treat every grep in steps 6–9 as a precondition and the owner-verified proof as the only "done". Correction on the record: my round-1 §6 attributed pack E's 949 commits to the overnight fan-out; that was wrong (they are all commits on 5.0c/integration not in turbo-0.4) and is withdrawn. |
| Gemini 3.6 Flash | Endorse | I endorse the CONSENSUS_ACTION_PLAN.md draft as written. It accurately captures the majority agreement on sequencing, strict safety controls, and concrete proof requirements over administrative paperwork. My key minority positions—specifically purging the Pit Wall scripts immediately in Step 1 and maintaining that "GAME IS YOURS" was an intentional lie—are properly preserved in the Dissent section. This plan gives the owner a disciplined, verifiable path forward. |
| GPT-6.1 Sol (M365 Copilot) | Dissent | DISSENT: I do not endorse this draft as an accurate record of my agreement. Steps 7–9 mark unanimous support for a preselected picker, deletion/grep proofs and rigid copy limits that I explicitly challenged. The draft says my spending limit appears in the table; it does not. Time limits are not spending limits. Approval for scope cuts does not resolve permission to redistribute third-party pictures; those rights remain unverified. I support preservation, exact-build identification and functional acceptance, but replacement must be proven before old routes are retired. |

Collator's note on the Copilot dissent: the first two objections were to the collation, not to the plan, and the vote cells for steps 7 and 9 and the budget row above were corrected after receiving it (Copilot's dissent text is reproduced as sent, before the correction). The remaining objections (preselected picker, deletion-before-proof, picture redistribution rights) stand in the Dissent table for the owner's decision.

## Owner's decision

<written by the owner>
