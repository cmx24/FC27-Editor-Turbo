# Cross-review and consensus protocol

Goal: several independent models review the same pack, then review each other, then one action plan is produced that
every reviewer either endorses or dissents from in writing. The owner decides; the protocol only makes the
disagreements visible.

## Roles
- **Owner (Cassio):** picks the reviewers, pastes prompts, collects answers, decides at the end.
- **Reviewers:** three to five models from different vendors, plus optionally one human. Each gets the same prompt.
  Suggested set: one OpenAI model, one Google model, one xAI or Mistral or DeepSeek model, one Claude instance that
  has NOT seen the chat (fresh session), one more of the owner's choice.
- **Collator:** the assistant that wrote the pack (Claude, this session or a fresh one), or any model the owner
  prefers. The collator never edits a reviewer's text; it only tabulates. Conflict of interest is declared: the
  collator is the party under review, so the owner should spot-check the collation against the raw responses in
  `responses/`.

## Round 1: independent assessments (parallel, same day)
1. For each reviewer: new chat, paste `PROMPT_FOR_EXTERNAL_REVIEWERS.md` (the text between the `=====` lines).
   If the model cannot browse, paste `REVIEW_PACK.md` after the prompt.
2. Save each answer unchanged as `responses/<vendor>-<model>_round1.md` (example: `openai-gpt5_round1.md`).
3. Do not show any reviewer another reviewer's answer in this round.
4. The collator produces `responses/ROUND1_TABLE.md`: one table, rows = the eleven asks plus the five verdict lines,
   columns = reviewers, cells = the reviewer's judgement and score. A second table lists every action step proposed
   by any reviewer, deduplicated, with which reviewers proposed it.

## Round 2: cross-critique (parallel)
1. Each reviewer, in its own existing chat, gets this message (replace the bracketed part):

   > Below are the round-1 reviews written by the other reviewers [paste all other responses, each under a heading
   > with the model name]. Critique them: where are they wrong, where did they miss something you found, where did
   > they find something you missed and now accept? Then revise your own section 9 (action plan) in the light of
   > theirs. Output: (a) a table with one row per other reviewer: strongest point, weakest point, one factual error if
   > any with a citation; (b) your revised action plan, at most ten steps, each marked KEPT, CHANGED or NEW; (c) the
   > three points on which you disagree with the majority and why.

2. Save as `responses/<vendor>-<model>_round2.md`.
3. The collator produces `responses/ROUND2_TABLE.md`: every action step across all revised plans, with a vote count
   (how many reviewers include it), and the list of explicit disagreements.

## Round 3: consensus plan
1. The collator drafts `CONSENSUS_ACTION_PLAN.md` from the round-2 table using these rules, written down so the owner
   can check them:
   - A step enters the plan if a majority of reviewers include it.
   - A step a minority insists on is kept in a "Dissent" section with the names and the reason.
   - Each step keeps the strictest proof any reviewer attached to it.
   - Order: the owner's original order of asks wins over any reviewer's reordering unless a majority reordered it
     for a stated dependency reason.
   - No step is added by the collator. No step's wording is softened by the collator.
2. Each reviewer gets the draft with: "Endorse, or state your dissent in at most five lines. No other changes."
   Save as `responses/<vendor>-<model>_round3.md`.
3. The final file has three parts: the plan; an endorsement table (reviewer, endorse / dissent, the five lines); the
   owner's decision line, which he writes himself.

## Template: CONSENSUS_ACTION_PLAN.md
```
# Consensus action plan (date)
Reviewers: <list>. Collator: <name>. Rounds completed: 1 / 2 / 3.

## Verdict scores (round 1, unchanged)
| Line | R1 | R2 | R3 | R4 | R5 | Median |
| GUI as shown | | | | | | |
| Code quality | | | | | | |
| Reporting honesty | | | | | | |
| Pit Wall value | | | | | | |
| Failsafe plan credibility | | | | | | |

## Agreed steps (majority)
| # | Step | Who | Proof of done | Stop condition | Votes |

## Dissent (minority insists)
| Step | Who insists | Reason |

## Deleted from the assistant's plan by majority
| Item | Why |

## Endorsements (round 3)
| Reviewer | Endorse / Dissent | Five lines |

## Owner's decision
<written by the owner>
```

## Mechanics
- Everything lives under `docs/review-2026-10-31/responses/` on branch `review/2026-10-31` of the public repo, so every
  reviewer can read every other reviewer's text by raw link in round 2 instead of pasting.
- The owner can run rounds 1 and 2 himself with copy and paste and hand the collation to any model. The collator's
  input is only the files in `responses/`; it must not use the chat history.
- Time: round 1 can be done in an hour; round 2 the same; round 3 within a day. Nothing in the project should start
  before the owner has read the round-1 table, because the first agreed step may differ from the assistant's.
- If a reviewer cannot read the code links, its code claims are marked "not verified" in the tables and weighted
  accordingly; its judgement on honesty, process and plan still counts fully.
