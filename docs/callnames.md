# Player callnames for the loaded commentary language (Turbo track D2)

A *callname* is the name the commentary speaks for a player. This note records how FC 27 binds it, which commentary
language this PC's game has loaded, how Turbo decides which callnames are *spoken* in that language, and what the
Players > Callname tab writes. Everything below was checked on 2026-10-03 on this PC unless marked otherwise.

## 1. How the game binds a callname (FC 27 database)

Tables and ranges (from the in-game schema dump `turbo/le27/fc27_db_schema.json`, Live Editor v27.1.2):

| table | fields | meaning |
|---|---|---|
| `playernamemap` (short `VGQZ`) | `playerid` (19 bits, min -1), `commentaryid` (20 bits, min -1) | player-specific callname; **wins** |
| `playernames` (`BGwe`) | `nameid` (16 bits), `name` (compressed text, type 13), `commentaryid` (16 bits, min 900000) | the commentary id of a name |
| `dcplayernames` (`bneD`) | `nameid` (min 46000), `name` (plain text) | DLC / modded names, no commentary id |
| `commentarynames` (`PrWZ`) | `commentaryid`, `commentarystring` (compressed), `commentarypreview`, `commentarystartingletter` | every commentary entry, for every language |
| `editedplayernames` (`nQVU`) | `playerid`, `firstname`, `surname`, `commonname`, `playerjerseyname` (45 bytes each) | the name shown on screen |
| `audionation` (`Mmou`) | `defaultcommlang` (compressed text), region indexes | per-nation defaults, not the loaded language |
| `playercalls`, `SMPlayerCallLang`, `playervoicemap` | voice banks | player shouts, not callnames |

Rule implemented in `core/callnames.cpp` `resolve_callname()`:

1. `playernamemap[playerid]` with a commentary id other than 900000 wins (the user's research D-011: it is a per-player
   override of the generic surname bank; 106 rows in the FC 26 PT-BR database, 38 of them in a dangling `980xxx` range
   that no bank or `commentarynames` row knows, so Turbo refuses ids outside 900000..965000 when writing);
2. else the commentary id of `players.commonnameid` through `playernames.commentaryid`, when the common name is set
   and its id is not 900000 ("no callname"; D-019: writing only `lastnameid` is a silent no-op for players that carry
   a common name);
3. else the commentary id of `players.lastnameid`;
4. else none (900000).

The name shown on screen follows another waterfall (`editedplayernames.commonname` → common name →
`editedplayernames.surname` → last name), which is why a name pick can keep the shown name through an
`editedplayernames` row while the commentary speaks the chosen one (the user's verified "generic substitution",
CONTEXT_PACK §4).

Not modelled: the *Real* channel (`pPLAYER_NAMES_SIMPLE`), whose selection table binds multi-take recordings to a
`playerid` directly (D-016/D-020). A player with Real audio is spoken even when the rule above says "none"; a spoken-id
list cannot express that yet (see §5).

## 2. Which commentary language is loaded on this PC

Evidence:

* `C:\Program Files\EA Games\EA SPORTS FC 27\commentary\` holds exactly one downloaded language:
  `commentaryfull_ita_it\` (cas_01.cas, cas_02.cas, 1.5 GB) + `commentaryfull_ita_it.toc` (244 KB) and the matching
  `commentarylaunch_ita_it` pair (26 MB). Nothing else.
* `Data\Win32\` holds `commentaryfull_eng_us.toc` and `commentarylaunch_eng_us.toc` (the base game's English, cas data
  inside the base superbundles), `commentaryfull_ita_it.toc` / `commentarylaunch_ita_it.toc` (the install stubs of the
  downloaded pack) and `commentarywc_<12 languages>.toc` (World Cup mode banks, not a language pack).
* `%LOCALAPPDATA%\EA SPORTS FC 27\fcsetup.ini` carries only `CONFIG_APP_LOCALE = en-US` (the app locale, not the
  commentary) and the binary `settings\Settings…` / `ProfileOptions` files contain no language string at all (searched
  for `comm|lang|ita|eng_us|por_br|locale`). The registry has no `EA SPORTS FC 27` language key.
* Live Editor's own files carry no language setting either.

So the loaded commentary language is **Italian (`ita_it`)** on this PC: the game downloads only the language picked in
its audio settings, and that is the single pack under `commentary\`. Turbo therefore decides at runtime
(`installed_commentary_packs` + `pick_commentary_language`):

1. the language chosen in the GUI (`gui_settings.json` → `callnames.language`) when its pack is installed;
2. else the one downloaded pack under `<game>\commentary\`;
3. else `eng_us` when its base toc exists; else the first pack found; else none.

The game folder is the folder of the running `FC27.exe` (`GetModuleFileNameW(nullptr)`, Turbo.dll runs inside the
game); the native tests point `App::game_root` at a fake folder. The chosen language, the reason and the installed
packs are shown in the tab, with a combo to pick another one and a Refresh button.

## 3. Which commentary ids are spoken in that language: what was tried, what Turbo does

* **Parsing the bank on disk.** `commentaryfull_ita_it.toc` starts with magic `00 D1 CE 01` (signed, not
  obfuscated); its payload at 0x22C begins `3C 00 00 00 …` (the FC 24+ manifest layout with hashed bundle names and
  chunk ids: 292 "strings" in 200 KB are all noise). The names, selection tables and audio live as EBX inside the
  Oodle-compressed `cas_0x.cas` chunks (`oo2core_9_win64.dll` ships with the game). Reading them would mean a Frostbite
  superbundle + EBX reader plus Oodle decompression at game start: out of scope and fragile across title updates.
* **Reading the loaded bank from memory.** `turbo_output\fc27_image.bin` (555,675,648 bytes = `FC27.exe`'s module
  size 0x211EF000) is the executable image, not the heap, so the loaded bank is not in it; the runtime layout of the
  selection tables is unknown and a heap-wide scan from the render thread is not acceptable.
* **What works and is reliable:** the ids are known once per language from a FIFA Editor Tool export of the
  language's generic surname family `pSIMPLE_SURNAME` ("Export Data Set" of the selection table, whose `surname_ID`
  column is the commentary id — the user already made this export for FC 26 PT-BR: 1,703 segments ↔ 1,703 commentary
  ids, `C:\FC_Tools\callnames_ptbr\data\donors_generic.csv`). `scripts/callnames_spoken_list.py` turns such a CSV
  into `<Live Editor>\turbo\callnames\spoken_<lang>.txt`; Turbo loads the file for the detected language at startup
  (`Callnames::refresh`) and marks the pickers *verified*. `turbo/package/turbo/callnames/spoken_por_br.txt` ships as
  an example (FC 26 PT-BR generic bank; FC 27 keeps the ids of `commentarynames`, but it is FC 26 data).
* **Fallback:** without a list for the loaded language every commentary id that `playernames` uses (other than
  900000) counts as spoken; the tab says "Unverified: …" in orange, names the file it looked for and points here.

The list format: optional header `#turbo-spoken <lang> <count>`, one id per line, comments after a tab, space or `#`.
A header for another language, a count mismatch or an empty list is rejected with the reason shown in the tab.

## 4. What the Players > Callname tab does

* Shows the language (combo of installed packs, auto-detect, Refresh), the spoken-id source, and the player's current
  callname: commentary id, source (player-specific / common name / last name, with the name and name id), and whether
  it is spoken in the loaded language.
* **By name**: type-ahead over `playernames` names whose commentary id is spoken (name, name id, commentary id, how
  many players use the name as common or last name). *Assign as last name* / *Assign as common name* write
  `players.lastnameid` / `commonnameid` directly (`Database::set`, range-checked). With *Keep the shown name* (default)
  the name parts shown before the change go to `editedplayernames`: edited in place when the player has a row, else
  added by Turbo's Lua side (`InsertDBTableRow`) through the mailbox command `callnames` / `set_display_name`.
* **By player**: type-ahead over players whose `playernamemap` callname is spoken (player, club, commentary id).
  *Use this player's callname* writes this player's `playernamemap.commentaryid` in place when the row exists, else
  queues `set_playernamemap` (row inserted by Lua). *Remove player-specific callname…* asks for confirmation and queues
  `remove_playernamemap` (`DeleteDBTableRowByAddr`).
* Lua module `features/callnames.lua` (also `lua\scripts\turbo_callnames.lua` with `modules.callnames.actions` in
  `turbo_config.json`): `set_playernamemap`, `remove_playernamemap`, `set_display_name`, `set_name_ids`; every action is
  validated before the first one runs; dry run supported.

Tests: `turbo/tests/t12_callnames.lua` (9 cases) and in `turbogui/tests/native/test_main.cpp` the cases
"callnames: language packs, spoken list, resolution rule, index" and "UI: Players > Callname: …".

## 5. Open points

* Real-channel players (spoken by `playerid`, not by commentary id) are not in the spoken set; a second list
  (`spoken_<lang>_players.txt`) or a FET export of `pPLAYER_NAMES_SIMPLE` could add them.
* The Italian list for this PC still has to be exported with the FIFA Editor Tool (the tab shows the fallback until
  `turbo\callnames\spoken_ita_it.txt` exists).
* A direct in-game check of the rule order (player-specific over common over last name) was not run in this track.
