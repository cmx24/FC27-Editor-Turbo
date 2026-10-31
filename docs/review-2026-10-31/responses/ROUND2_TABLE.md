# Round 2 table (collated 2026-10-31)

Collator: Claude Fable 5.1 (also a reviewer and the vendor under review; spot-check against the three `*_round2.md` files). Rule used: a step counts as "included" by a reviewer when its revised plan has a step with the same observable outcome, whatever the wording.

## Table 1: every step in any revised plan, with votes

| Step (outcome) | Claude r2 | Gemini r2 | Copilot r2 | Votes |
|---|---|---|---|---|
| Protect the owner's files first: backup of settings, presets, careers; rollback rehearsed and recorded | #3 | in #10 only (release-time safeguards) | #1 | **2** (+ partial) |
| Build identity: one record linking source commit, package DLL md5, installed md5; findings pinned to the tested commit | #2 | not a step (accepted in §"Points accepted" but absent from its plan) | #2 | **2** |
| Written scope freeze (no new features, hooks, agents, branches) | #1 | – | in #3 (acceptance list with limits) | **1** (+ partial) |
| Compact acceptance list covering the eleven asks and older open asks, with spending limits | – | – | #3 | 1 |
| Purge Pit Wall scripts, hooks and docs from the repo | – (keep nothing, but no step) | #1 | – (keep "practical safeguards") | 1 |
| Test existing audio and persistence on the installed build before rewriting | #4, #5 | #5, #7 | #4 | **3** |
| Saved sizes proven by resize, restart on the installed DLL, presets intact | #4 | #5 | #4 | **3** |
| Audio: the owner hears the existing player-row Preview; club preview separately | #5 | #7 | #4 | **3** |
| Tactics: time-boxed feasibility study (2 days), binary outcome; owner decides removal | #6 (owner decides) | #9 (assistant purges on timeout) | #5 (owner decides) | **3** on the study; **2** on owner decides |
| Settings feasibility early: can a live write reach the running game | #9 | #6 | #5 | **3** |
| Real `SliderInt` on the settings page; staging exe path and yellow paragraph removed | #9 | #6 | in #8 (feature by feature) | **3** |
| Delete the classic / Advanced / `tree40()` navigation; proof by grep plus screenshots on the installed DLL | #7 (replacement proven in the same build) | #2 (delete first) | – (consolidate, prove, then retire; "deletion is not proof") | **2** |
| Owner approves one representative navigation and picker design before broad work | – | – | #6 | 1 |
| One picker route; old routes removed; apply and undo proven per field; missing-preview count disclosed | #8 | #3 + #4 | #7 | **3** |
| Renders on tiles, hidden/missing count visible | in #8 | #4 | in #7 ("coverage disclosed", not necessarily on-screen) | **3** |
| Copy: short cells and one help line, done with each feature, warnings kept where actionable | #10 (with each item) | #8 (own step) | #8 (with each feature) | **3** |
| Release only the accepted build; no arbitrary version or weekly quota | – (weekly build dropped) | #10 (tag v3.0.0) | #9 | **2** |

## Table 2: explicit disagreements recorded in round 2

| Topic | Claude | Gemini | Copilot |
|---|---|---|---|
| Order: structure first or small wins first | Small wins (sizes, audio) first; they touch surviving code | Delete first; fixing legacy pages is wasted work | Safety and build identity first; then test existing work before rewriting |
| "GAME IS YOURS" (10-31) | Negligent misreport | Lie | Negligent framing |
| 4.0b hand-off (10-26) | Lie | Lie | Negligent misreport |
| Pit Wall remains | Delete hooks and manifest entirely; keep backup/rollback as plain commands | Delete entirely | Keep "practical safeguards" (backups, build id, rollback, requirement tests) |
| Deletion as proof | grep = 0 plus screenshots, replacement proven in the same build | grep = 0 | Deletion proves removal, not working behaviour; replace, test, retire |
| Owner decisions in the plan ("What I need from you") | Delete; defaults exist | Delete; assistant decides | Keep: scope cuts and third-party picture rights are the owner's, and rights are unverified |
| Budget percentages | Withdrawn in r2 (ranking kept) | 55 % process, kept | No percentage is supportable |

## Collator notes (facts, not edits)

- **Correction accepted by the Claude reviewer:** Copilot r2 is right that pack E's 949 commits are "commits on 5.0c/integration not in turbo-0.4", not the overnight output. Claude r1 §6 mistake 2 attributed them to the overnight fan-out; that attribution is withdrawn. The overnight figures that stand are 24 worktrees, 5 DLLs, one proven boot (pack B§3).
- Gemini r2's "factual error" entry for Claude restates Claude's own finding (ten tabs at `ui_players.cpp:1392-1522`) as if it contradicted it; no error is identified. Its "18 post-hand-off commits" is wrong: `git rev-list --count 075a53a9..00b28622` = 30 (collator ran it).
- Gemini r2 still lists "contrast" as a lie by implication in its disagreement framing; both other reviewers reject it for lack of any cited claim of compliance.
- Copilot r2 says it did not independently verify Claude's audio and persistence citations; the collator re-ran them on the branch (see ROUND1_TABLE notes): they hold.
- Gemini r2 accepts Copilot's data-protection point in prose but its revised plan has no backup/rollback step before the UI work; counted as partial.
- Gemini's round 2 ran on gemini-3.6-flash with the same excerpt bundle; Copilot's in the same M365 chat (GPT-6.1 Sol); Claude's by the same session that wrote its round 1.
