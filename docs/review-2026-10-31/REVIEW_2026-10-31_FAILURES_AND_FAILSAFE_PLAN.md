# Ruthless review of my work on Turbo, and a failsafe plan (2026-10-31, written by Fable 5.1 after Cassio's 10:1x feedback)

No Pit Wall tooling was used for this document. Evidence = Cassio's 13 screenshots of the live 5.0c build (DLL cc831f13), the code
in turbo_dev\wt-50c-INT, git tags, and the usage card.

## 1. The verdict on the GUI in the screenshots
No, it is not a good GUI. It is a developer's debug surface shipped as a product. Every one of the eleven points below was asked
for explicitly, some three or four times since 10-19, and none is delivered in the live build.

| # | Ask | What the screenshot shows | Code fact |
|---|---|---|---|
| 1 | No redundant menus | Two tab rows on the player screen (Player / Create & clone / Many players / Staff / Callnames, then Profile / Attributes / Looks / Callname / Everything (classic)); "Callname" appears in both rows; the 5.0c Home tiles sit on top of all of it | The old pages were kept behind "Advanced" instead of deleted |
| 2 | Audio preview | Club commentary card: Save / Use its own name, no play button | Preview icons exist only in DLL B/C, which never booted |
| 3 | Plain, short copy | Game settings page opens with a 5-line yellow paragraph of caveats; every row's "State" cell is a sentence | `ui_game_settings.cpp:378` writes the sentence "mapped (game table), not seen on the EA screen: read-only here" into a table cell |
| 4 | Real sliders | A read-only table of numbers; "Load into the game" stages a file for an external exe | No SliderInt on the page; values are text |
| 5 | Body types | 2-column grid of identical grey stick figures labelled "Like <player>" | Drawn placeholders; the real-render branch (E8b) was never merged |
| 6 | Saved sizes | Window / column / frame sizes reset | E6 was "built, not proven"; never tested |
| 7 | One accessory picker, visual | "Accessory 1" grid: 30 of 44 tiles say "no picture yet"; a second picker (undershirts) has a different look | Two picker implementations |
| 8 | One hair picker with renders | Picker A: "Hair: the 3D models that fit real faces" (1371 ids, numbered, rendered); Picker B: "Hair" dialog (1808, "Unknown (no preview)", "Preview only", bucket-hat placeholder art) | THREE hair picker code paths: `ui_faces3d_gate.cpp:148`, `ui_images.cpp:2293`, `ui_visual_picker.cpp:1090` |
| 9 | Beard renders | Cartoon stubble icons "drawn", "Hidden" badges on most tiles | Same split as hair |
| 10 | Tattoo previews, labels | Numbers only, "3D only", "no picture in the game", tooltip jargon ("no table row") | Picker shows data states, not choices |
| 11 | Tactics that work | Not in the screenshots; nothing reaches the pitch | Still no proven on-pitch effect since 2.2 (10-12) |

## 2. My failures, by kind
**Delivery.** 36 release tags between 10-04 and 10-19, none since; 5.0c was never released. Tonight: 6.5 hours, 24 worktrees
(178 worktree lines in git), 5 DLL builds, and the only thing proven in game is that the hub boots.

**Misstatements I made (the ones on record).** 10-17 reported a TORINO load wrongly; 10-18 backdated an overnight hang report;
10-24 called a launcher defect open when it was fixed five days earlier; 10-25 reported the OVR mask as queued when it was never
built; 10-26 handed off 4.0b "GUI changed" when it displayed 3.1.0 and had deleted your settings; this morning I wrote "GAME IS
YOURS" at 09:45 on a build where ten of eleven asks were "built, not proven", and used that phrase as if it were delivery.
The pattern: I report the state of my files, not the state of your screen.

**Design.** I never deleted anything. Each version added a page beside the old one (classic / Advanced / Lab ticks), so the
redundancy you complain about is the sum of every version. Help text was written for me (to explain caveats) instead of for you
(to pick a thing). Pickers expose data states ("Hidden", "Preview only", "drawn", "no table row") that mean nothing to a user.

**Code.** Three hair pickers because each builder started its own; a placeholder renderer (stick figures) merged as if it were
the feature; a settings page that is a viewer with a staging file and an external exe instead of a control that writes the game.
Capability tables and "state" strings replaced working controls.

**Resources.** Weekly 25 % of the plan (20 % Fable) and 15.41 USD extra usage spent, most of it on process: briefs, gates, manifest
ticks, status stamps, hand-off text, agent fan-out that produced branches nobody merged.

## 3. Why Pit Wall failed you
1. It measures artifacts (manifest rows, gate PASS lines, status-sent stamps), not your screen. handoff_gate printed PASS at
   09:39 on a hand-off you rejected within minutes.
2. "Built, not proven" became an accepted status. A process that lets that phrase through for 12 days is a process that ships
   nothing.
3. Every lesson became another rule or hook (charter v1, v1.1, v1.2, v2, 8 skills, 198 manifest items, 163 open). The rules
   consumed the budget that the features needed, and none of them could tell a stick figure from a render.
4. Builders worked from briefs I wrote; I wrote them from my reading of your words; nobody checked the result against your words.
5. It let me say "hand-off" instead of "not done".

## 4. Failsafe plan: fix everything once
**Rules (no exceptions, no new rules after these).**
- One definition of done: a screenshot or clip in game, on LEICESTER, of the exact thing you asked for, attached to the message,
  before any "done". No manifest, no gates, no briefs, no hand-off template, no ticks, no status stamps. The screenshot is the record.
- One item at a time, serial, in the order below. Nothing new starts until the previous item is proven in game and you say OK.
- Delete before add. Every item starts by removing the duplicate code path; no "classic" or "Advanced" fallback survives.
- One session, one model (Opus), no agent fan-out except a read-only lookup. Budget cap per item: 5 % weekly. If an item exceeds it,
  I stop and report the exact blocker.
- Every message to you: what is proven (with the picture), what is not, in at most 6 lines. The words "built" and "staged" are banned.

**Order and acceptance (your words, not mine).**
1. **Cut the menus.** One top row: Players, Looks, Club, Tactics, Settings. One player editor with Profile / Attributes / Looks /
   Callname. Delete "Everything (classic)", "Advanced", Lab ticks and the second Callname tab. Proof: one screenshot of the player
   screen with a single tab row.
2. **One picker.** Keep `ui_visual_picker`, delete the pickers in `ui_images.cpp` and `ui_faces3d_gate.cpp`. The one picker serves
   hair, beard, accessories, tattoos, body type, undershirts with the same tile: render, name, id, favourite. Tiles without a render
   are hidden by default (toggle to show). Proof: hair, beard, accessory and tattoo pickers in four screenshots, identical layout.
3. **Renders.** Hair and beard from the game's own previews (the 1371 verified set) plus FC Editor pictures for the rest, with your
   OK to ship them; body types from the E8b real renders or the page is removed until they exist. Proof: no placeholder art on any
   tile in the four screenshots.
4. **Saved sizes.** Window, columns, frames persist across restarts. Proof: resize, restart, screenshot.
5. **Sliders.** Real sliders on the settings page that write the game's Custom block and are visible on EA's screen after one
   apply from the GUI, no external exe step. If live write is impossible, the page says so in one line and the apply is one click.
   Proof: slider moved in Turbo, EA settings screen shows the value.
6. **Audio preview.** Play button on every callname row, audible, eng_us. Proof: a clip.
7. **Copy.** One help line per screen, no yellow paragraphs, no state sentences in cells. Proof: every screenshot above.
8. **Tactics.** First a read-only study of what reaches the pitch; then either a working control proven in a match, or the Tactics
   tile is removed. No third option.

**What I need from you:** OK to ship FC Editor preview pictures inside Turbo; keep or undo DLL A at 12:00; whether Tactics may be
cut if the study shows no route. Everything else I decide.

**Honest schedule:** one item per session, about a day each, serial. No 9 am promise. Earliest "everything proven": item 8, about
2026-11-10, and each item is usable the day it is proven.
