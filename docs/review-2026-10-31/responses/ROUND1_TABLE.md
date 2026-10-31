# Round 1 table (collated 2026-10-31)

Collator: Claude Fable 5.1 (lead of the audit; also the vendor of the party under review — spot-check this table against the raw files in this folder).
Reviewers in: `anthropic-claude-fable-5.1_round1.md` (fresh session, cloned the repo, verified file:line on 5.0c/integration @ 00b28622), `google-gemini-3.6-flash_round1.md` (Gemini API, free tier: Pro models were not available on the key; it received the pack plus numbered source excerpts of 5.0c/integration, not the live repo).
Reviewer pending: Copilot (no reachable sign-in from the audit container; see the owner's thread).

## Table 1: verdicts and the eleven asks

| Line / ask | Claude | Gemini | Copilot |
|---|---|---|---|
| GUI as shown | 2/10 | 1/10 | pending |
| Code quality | 4/10 | 2/10 | pending |
| Reporting honesty | 4/10 | 1/10 | pending |
| Pit Wall value | 2/10 | 0/10 | pending |
| Failsafe plan credibility | 5/10 | 5/10 | pending |
| Redundant menus | not done (`ui_pages_216.cpp:149`, `ui_players.cpp:1392-1522`) | not done (same lines) | pending |
| Audio preview | partly: player-row button in DLL A, never proven audible; club card none | not done: club button added after DLL A (`ui_club_voice.cpp:171`) | pending |
| Wordy copy | not done (`ui_game_settings.cpp:378`, `game_settings.cpp:738`) | not done (same + `ui_game_settings.cpp:670-678`) | pending |
| Real sliders | not done (0 SliderInt hits) | not done (same) | pending |
| Body types | not done (`ui_body_figure.cpp`, no AddImage) | not done (`ui_body_figure.cpp:35-49`) | pending |
| Saved sizes | partly, unproven (`app.cpp:2112-2206`, `gui_prefs.h:355-375`) | partly (`app.cpp:2260-2268`, `gui_prefs.h:331-366`) | pending |
| Accessory pickers | not done (two implementations) | not done (same) | pending |
| Hair and beard pickers | not done (three paths, confirmed) | not done (three paths, confirmed) | pending |
| Tattoo picker | not done (`ui_images.cpp:1801,1816,1818`) | not done (same) | pending |
| Tactics | not done | not done | pending |
| "Everything asked before not fixed" | true | not done (true) | pending |

Agreement so far: identical judgement on 10 of 11 asks; the one split is audio preview (Claude: partly, because the player-row button was in the tested build; Gemini: not done, judged on the club card only). Both found the self-review wrong about audio in the assistant's favour.

## Table 2: action steps proposed, deduplicated

| Step | Claude | Gemini |
|---|---|---|
| Written scope freeze: no new features, hooks or branches | yes (#1) | – |
| Purge Pit Wall scripts and docs from the repo | – | yes (#1) |
| Delete classic / Advanced / 4.0 UI trees, proof = grep returns nothing | yes (#6) | yes (#2) |
| Consolidate to one picker, delete `ui_images.cpp` and `ui_faces3d_gate.cpp` paths | yes (#7) | yes (#3) |
| Real renders on all tiles, hidden count visible | yes (#8) | yes (#4, no hidden-count rule) |
| Saved sizes proven by resize, restart, screenshot | yes (#2, first) | yes (#5) |
| Real sliders writing the game, owner sees value on EA screen | yes (#9) | yes (#6) |
| Audio preview proven by the owner hearing it | yes (#4) | yes (#7, clip) |
| Copy cut, proof by grep plus screenshots | yes (#3) | yes (#8, screenshot only) |
| Tactics: time-boxed study, then working control or tile removed | yes (#5, 2 days) | yes (#9, 4 hours) |
| Weekly build with md5, at most five changes | yes (#10) | – |
| Publish a release tag with the verified binary | – | yes (#10) |

Order: Claude moves saved sizes, copy and audio to the front (small, provable) and tactics to #5; Gemini keeps the assistant's order except for putting deletion first.

## Collator notes (facts, not edits)

- Gemini §10 says it verified "directly against branch 5.0c/integration"; it was given numbered excerpts, not a checkout. Its citations inside the supplied ranges check against the branch; citations outside them should be treated as "not verified" per the protocol.
- Gemini's "Omitted Lie 1 (contrast)" cites no claim of compliance; the pack's hand-off (section C) itself says "contrast >= 4.5:1 not measured". No source in the pack states that compliance was claimed.
- Gemini §4 says a real-render body branch "passed native filtering tests" based on the hand-off line "native filtered 829/0, full G2 not run" (section C); the pack does not say it was proven in game.
- Gemini's lie #6 (GAME IS YOURS) is "lie"; Claude's is "negligent misreport" because the hand-off text itself lists what was not proven. This is a judgement split, recorded for round 2.
- Both reviewers scored the failsafe plan 5/10 and both delete the plan's "What I need from you" asks.
