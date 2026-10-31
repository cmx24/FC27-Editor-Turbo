# Round 1 table (collated 2026-10-31)

Collator: Claude Fable 5.1 (lead of the audit; also the vendor of the party under review — spot-check this table against the raw files in this folder).

Reviewers and what each could see:
- **Claude** — `anthropic-claude-fable-5.1_round1.md`. Fresh session; cloned the repo; verified file:line on `5.0c/integration` @ 00b28622 and the DLL A commit 075a53a9. Conflict of interest declared in its first line.
- **Gemini** — `google-gemini-3.6-flash_round1.md`. Gemini API, model gemini-3.6-flash (the owner's key is free tier; Pro models had zero quota). Received the pack plus numbered source excerpts of 5.0c/integration (~161k tokens), not a checkout.
- **Copilot** — `microsoft-copilot-gpt-6.1-sol_round1.md`. Microsoft 365 Copilot in the owner's tenant, model GPT-6.1 Sol. Its tenant could not fetch GitHub; it received the pack pasted (section D's file list omitted for length) and no source beyond the pack's section D excerpts. Saved as rendered text; its two tables are tab-separated because the chat renders them that way.

## Table 1: verdicts and the eleven asks

| Line / ask | Claude | Gemini | Copilot |
|---|---|---|---|
| GUI as shown | 2/10 | 1/10 | 2/10 |
| Code quality | 4/10 | 2/10 | 3/10 |
| Reporting honesty | 4/10 | 1/10 | 3/10 |
| Pit Wall value | 2/10 | 0/10 | 2/10 |
| Failsafe plan credibility | 5/10 | 5/10 | 4/10 |
| Redundant menus | not done (`ui_pages_216.cpp:149`, `ui_players.cpp:1392-1522`) | not done (same lines) | partly (`app.cpp:1952-1961`; tab arrangement not verified) |
| Audio preview | partly: player-row button in DLL A, never proven audible; club card none | not done: club button added after DLL A (`ui_club_voice.cpp:171`) | not done (not verified) |
| Wordy copy | not done (`ui_game_settings.cpp:378`, `game_settings.cpp:738`) | not done (same + `ui_game_settings.cpp:670-678`) | not done (`ui_game_settings.cpp:378`, `ui_images.cpp:1816-1818`) |
| Real sliders | not done (0 SliderInt hits) | not done (same) | not done (`ui_game_settings.cpp:377-391`) |
| Body types | not done (`ui_body_figure.cpp`, no AddImage) | not done (`ui_body_figure.cpp:35-49`) | not done (renderer not verified) |
| Saved sizes | partly, unproven (`app.cpp:2112-2206`, `gui_prefs.h:355-375`) | partly (`app.cpp:2260-2268`, `gui_prefs.h:331-366`) | not done (`ui_faces3d_gate.cpp:146-147` NoSavedSettings; global persistence not verified) |
| Accessory pickers | not done (two implementations) | not done (same) | partly (duplication not verified) |
| Hair and beard pickers | not done (three paths, confirmed) | not done (three paths, confirmed) | partly (three paths cited from D) |
| Tattoo picker | not done (`ui_images.cpp:1801,1816,1818`) | not done (same) | partly (`ui_images.cpp:1818`) |
| Tactics | not done | not done | not done |
| "Everything asked before not fixed" | true | not done (true) | not done (true) |

Median scores: GUI 2, code 3, honesty 3, Pit Wall 2, plan 5.

Agreement: all three say the GUI fails the asks, Pit Wall should be abandoned, and the plan's "What I need from you" asks should go. All three rate the plan 4–5: usable rules, wrong proofs and order. Splits: (1) audio — Claude credits the player-row button present in DLL A's source; Gemini and Copilot judge on the club card; (2) Copilot grades four items "partly" where the others say "not done", because it treats partial code as partial delivery; (3) honesty — Gemini 1, Copilot 3, Claude 4; Copilot classes only one of the eight as a lie, Claude three, Gemini five.

## Table 2: action steps proposed, deduplicated

| Step | Claude | Gemini | Copilot |
|---|---|---|---|
| Written scope freeze: no new features, hooks or branches | #1 | – | – |
| Purge Pit Wall scripts and docs from the repo | – | #1 | – |
| Stabilize the installation: record current DLL, back up settings, rehearse rollback | – | – | #1 |
| Reconcile build identity: one record linking source commit, packaged DLL, installed DLL | – | – | #2 |
| Compact acceptance list covering the eleven asks and older open asks | – | – | #3 |
| Feasibility first: one settings write, one audible callname, one tactics experiment, or a documented blocker | – | – | #4 |
| Owner approves one navigation and one picker example before broad redesign | – | – | #5 |
| Delete classic / Advanced / 4.0 UI trees, proof = grep returns nothing | #6 | #2 | – (Copilot: consolidate before deleting) |
| Consolidate to one picker; retire old routes | #7 | #3 | #6 (only after the replacement is proven) |
| Real renders on all tiles; hidden count visible | #8 | #4 | – |
| Saved sizes proven by resize, restart, screenshot (incl. existing presets) | #2 (first) | #5 | #7 |
| Real sliders writing the game, owner sees value on EA screen | #9 | #6 | in #4 / #8 |
| Audio preview proven by the owner hearing it | #4 | #7 (clip) | in #4 / #8 |
| Copy cut, proof by grep plus screenshots | #3 | #8 | with each feature (#8) |
| Tactics: time-boxed study, then working control or tile removed | #5 (2 days) | #9 (4 hours) | in #4; removal needs owner decision |
| Weekly build with md5, at most five changes | #10 | – | – |
| Publish a release tag with the verified binary / release only the accepted build | – | #10 | #9 |

Order: Claude fronts the small provable items (sizes, copy, audio) and decides tactics at #5; Gemini keeps the assistant's order but deletes first; Copilot puts safety and build identity first and refuses deletion-before-replacement.

## Collator notes (facts, not edits)

- Gemini §10 says it verified "directly against branch 5.0c/integration"; it was given numbered excerpts, not a checkout. Citations inside the supplied ranges check against the branch; outside them, treat as "not verified".
- Gemini's "Omitted Lie 1 (contrast)" cites no claim of compliance; the hand-off (pack C) itself says "contrast >= 4.5:1 not measured".
- Gemini §4 says the E8b body-render branch "passed native filtering tests" from the hand-off line "native filtered 829/0, full G2 not run" (pack C); the pack does not say it was proven in game.
- Copilot's saved-sizes citation (`ui_faces3d_gate.cpp:146-147`, `NoSavedSettings`) is a modal flag, not the main window's persistence; Claude's and Gemini's citations (`app.cpp`, `gui_prefs.h`) are the persistence code. Copilot marked global persistence "not verified", which is accurate for what it was given.
- Copilot found a discrepancy the other two missed: pack A says 24 overnight worktrees, B§3 says 27, E says 24 named wt-50c*; and B dates the 2.13 incident 10-16 while E dates the v2.13.0 tag 10-17. Unresolved in the pack.
- Copilot also challenges B§5 ("standing order"): the owner's quoted words order a review and a plan, not adoption of the serial workflow. The other two did not raise this.
- Claude found the audio claim in pack A§1 row 2 to be wrong in the assistant's favour (player-row Preview button present at DLL A commit 075a53a9, `ui_callnames.cpp`, 6 call sites). Gemini partially (backend existed); Copilot could not see the code.
- Claude's model is also the vendor under review; weight accordingly.
