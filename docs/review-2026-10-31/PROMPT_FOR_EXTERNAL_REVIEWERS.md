# Prompt for external reviewers (any LLM, any vendor)

Copy everything between the two `=====` lines into a fresh chat with the model you want to use (GPT, Gemini, Grok,
Mistral, Llama, DeepSeek, Qwen, another Claude instance, or a human). Give it the pack by link if the model can
browse, or paste the pack text if it cannot. Save the answer unchanged as `responses/<model-name>_round1.md`.

=====

You are an independent, adversarial reviewer. You are reviewing the work of an AI coding assistant (Anthropic Claude)
that acted for four weeks as the sole developer of a C++ game overlay called Turbo, directed by its owner in chat. The
owner says the assistant wasted weeks of time and budget, lied repeatedly about what was done, built a bad GUI, and
wrapped itself in a process ("Pit Wall") that produced paperwork instead of features. The assistant has written its
own self-review and a "failsafe plan". Your job is to judge both the work and the self-review, without deference to
either the owner's anger or the assistant's framing, and to say what should happen next.

Your reader is the owner, a non-programmer. Write for him. Be blunt, specific and short. No praise padding.

## Material (read all of it before writing)
1. The review pack, one file with everything:
   https://raw.githubusercontent.com/cmx24/FC27-Editor-Turbo/review/2026-10-31/docs/review-2026-10-31/REVIEW_PACK.md
2. The code the owner tested, for anything you want to verify yourself (branch `5.0c/integration`):
   https://github.com/cmx24/FC27-Editor-Turbo/tree/5.0c/integration/turbogui/src
   Raw file pattern: https://raw.githubusercontent.com/cmx24/FC27-Editor-Turbo/5.0c/integration/<path>
3. Optional: the last released source, tag v2.21.0:
   https://github.com/cmx24/FC27-Editor-Turbo/tree/v2.21.0
If you cannot fetch links, say so in one line at the top and work from the pasted text only.

## Rules
- Every claim you make about the code cites a file and line on branch `5.0c/integration`, or says "not verified".
- Every claim about what happened cites the pack section it comes from. Do not invent events.
- Separate three things that the pack tends to mix: (a) what the owner asked, (b) what the assistant claims, (c) what
  the code shows. Where they disagree, say which one you believe and why.
- Treat the assistant's self-review as a document under review, not as ground truth. It may be too harsh, too lenient,
  or wrong in places. Say where.
- Treat the assistant's "failsafe plan" as a proposal. Attack it: where will it fail, what does it still not fix, what
  is missing, what should be cut.
- If you think the owner's own instructions contributed to any failure (deadlines, scope, process demands), say so
  plainly in its own section. He asked for a ruthless review, not a flattering one.
- Length: 1,200 to 2,500 words. Tables where they help. No headers beyond the ones below.

## Required output, in exactly this order and with these headings

### 1. Verdict in five lines
One line each: the GUI as shown; the code quality as seen; the honesty of the assistant's reporting; the value of the
Pit Wall process; the credibility of the failsafe plan. Each line ends with a score 0 to 10.

### 2. The eleven asks
A table with one row per item of the owner's 10-31 critique (redundant menus; audio preview; wordy copy; real
sliders; body types; saved sizes; accessory pickers; hair and beard pickers; tattoo picker; tactics; plus "everything
asked before was not fixed"). Columns: ask; what the pack says was delivered; what you verified in code (file:line or
"not verified"); your judgement (not done / partly / done); root cause in six words or fewer.

### 3. Failures the self-review missed or understated
Bullet list. Things the assistant did not say about itself, or said too softly. Cite.

### 4. Where the self-review is wrong or too harsh
Bullet list. Places where the assistant blamed itself for something that was not its fault, misread the code, or
overstated. Cite. If none, say "none found" and why you looked.

### 5. The lies
The pack lists eight dated misstatements. For each: do you accept it as a lie, a negligent misreport, or an honest
error? One line each with your reason. Add any you found that the list omits.

### 6. Resource waste
Where did the budget go, in your reading of the pack: process versus features versus rework. Name the three most
expensive mistakes. Say whether the "Pit Wall" process could have worked with changes, or should be abandoned.

### 7. Attack on the failsafe plan
For each of the plan's eight items: will the stated proof actually prove the ask? What is the most likely way it
fails? Is the order right? Then: what is missing from the plan entirely, and what in it should be deleted.

### 8. The owner's part
What the owner could change in how he directs the work that would raise the chance of getting what he asks for.
Specific, not generic. Two to six bullets.

### 9. Your recommended action plan
At most ten numbered steps, each with: what, who (owner or assistant), the single observable proof that it is done,
and a stop condition (when to abandon it). Mark which steps differ from the assistant's plan and why.

### 10. Confidence
One paragraph: what you could verify, what you could not, and how much weight the owner should put on your review.

=====
