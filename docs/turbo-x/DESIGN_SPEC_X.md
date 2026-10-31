# Turbo X — page design spec (2026-10-31, from the design critique of the rc3 shots)

Critique of rc3 (shots `evidence\2026-10-31\turbox-rc2\run4\shots\X6_players.jpg`, `X6_home.jpg`, run3 `X7_picked.jpg`):

- **Three navigation layers stacked** (rail → page strip → editor tab bar) before any content; the eye lands on a 12-control filter form, not on a player.
- **The list is a spreadsheet**: 11 columns, four of them truncated ("Q", "Leice", "P..."), ID first. Nobody scans players by ID.
- **55 % of the Players screen is empty** ("Pick a player on the left") until a click.
- **Flat hierarchy**: one text size for filter labels, column heads, names and values; the selected row is a low-contrast wash.
- **Appearance Editor**: the middle column is a grey box with a paragraph about capture ("Draw him asks the game…"); the person's picture is not the hero.
- Works: the rail and mark, the chip, the cards with accent-barred titles, the picker modal (tiles, preview pane, counts).

## Principles (apply everywhere)
1. One subject per screen: the person. Their picture and name are the largest thing after the page title.
2. Lists are people, not rows: picture · name (semibold) · one muted line (club · position · age) · one number pill (OVR). ID on hover only.
3. Filters are a search box plus chips; the full form lives behind one "Filters" fold, collapsed by default, with a count badge when active.
4. Numbers are stat tiles (label small muted, value large semibold), never a sentence.
5. Empty states say what to do in one line and offer the two likeliest actions.
6. Help text is a tooltip, not a paragraph on the page. Three words per cell, one line per hint.
7. Scale: body 17 px, card title 1.12×, page title 1.35×, hero name 1.6×, stat value 1.8×; spacing 8/12/16/24; radius 7/10.
8. Colour: accent only for selection, primary action and the one key number; status chips green/amber/red; everything else slate.

## Players › Player
- Left pane 320 px: search ("name, ID or club"), chips row: **My club · Free agents · GK · DEF · MID · ATT**, "Filters (n)" fold holding the existing form unchanged. List rows 44 px: miniface 32 px (legacy_path::player_miniface), name semibold, second line "club · pos · age" muted, OVR pill at the right (accent when ≥ 85). Selected row: accent bar left + accent wash. Keyboard up/down moves the selection.
- Right pane: **hero header** — picture 96 px, name 1.6× semibold, line "club · position · age · nationality", then three stat tiles OVR / POT / Calc (Shown OVR field inside the OVR tile as a small "shown as" input). Undo button right-aligned.
- Below the hero: a **segmented control** Profile · Attributes · Looks · Callname (pill buttons, accent-tinted selected) instead of the window-wide tab bar; the bodies stay as they are except Attributes (below).
- Empty state (no player): centred "Pick a player" with the search box focused, two buttons: **My club** (filters the list) and **Create & clone** (goes to the page).

## Players › Player › Attributes & roles
- Six group cards in a two-column grid (Pace, Shooting, Passing, Dribbling, Defending, Physical; Goalkeeping replaces Shooting for keepers), each: card title with the group average as a pill, one slider per attribute (label 150 px, slider, value 40 px). PlayStyles: a chip grid (on = accent fill) under its own card. Player type & tier card keeps its content.

## Looks › Appearance Editor
- Left pane: same person rows as Players (reuse the row renderer).
- Middle column becomes the **portrait card**: the person's picture large (the game's render when it exists, else the miniface scaled, else the silhouette), name + club under it, and two small buttons "Draw him" / "Head and shoulders". The capture explanation is a tooltip on "Draw him"; failures show as one line under the buttons.
- Right column: the cards in the gear-first order (Face, Hair & beard, Boots gloves accessories, Tattoos, Kit fit, Body type, Miniface). Each card ≤ 3-word title; "Choose..." buttons are primary only on the row the person is editing.

## Home
- Keep. Fix the last tile's clipped second line (tile height = 3.4 lines).

## Out of scope tonight
Icons (no icon font in the build), animations, light theme.
