# Player callnames for the loaded commentary language (Turbo tracks D2 and E4)

A *callname* is the name the commentary speaks for a player. This note records how FC 27 binds it, which commentary
language this PC's game has loaded, how the game itself decides that a name has audio, how Turbo reads the loaded
bank's selection tables from memory to know which callnames are *spoken*, and what the Players > Callname tab writes.
Everything below was checked on 2026-10-03/04 on this PC (FC27.exe build 1.0.140.64835, build key
`6AB9813C-211EF000`, Live Editor v27.1.2, Manager Career, Italian commentary pack `ita_it`) unless marked otherwise.

## 1. How the game binds a callname (FC 27 database)

Tables and ranges (from the in-game schema dump `turbo/le27/fc27_db_schema.json`, Live Editor v27.1.2):

| table | fields | meaning |
|---|---|---|
| `playernamemap` (short `VGQZ`) | `playerid` (19 bits, min -1), `commentaryid` (20 bits, min -1) | player-specific callname; **wins** |
| `playernames` (`BGwe`) | `nameid` (16 bits), `name` (compressed text, type 13), `commentaryid` (16 bits, min 900000) | the commentary id of a name |
| `dcplayernames` (`bneD`) | `nameid` (min 46000), `name` (plain text) | DLC / modded names, no commentary id |
| `commentarynames` (`PrWZ`) | `commentaryid` (20 bits, pkey), `commentarypreview` (1 bit), `commentarystartingletter` (5 bits, 1..26), `commentarystring` (compressed text) | every commentary entry, for every language |
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

The game's own frontend code confirms the first step and adds one before it (`FC27.exe` 0x14294a0f4, the function the
menus use to find a player's callname, read with capstone from the image dump; §6.1 has the pipeline):

0. *Real* recordings first: it asks the commentary system whether the events `PLAYER_LOW_SIMPLE` or `PLAYER_LOW_LINK`
   have audio for `player_db_pID = playerid` (and `player_intensity = 2`). If so the player is spoken by his own
   recordings (the FC 26 term is the `pPLAYER_NAMES_SIMPLE` family bound to `player_db_pID`, D-016/D-020), whatever the
   steps below give;
1. `playernamemap.commentaryid` for the player (0x1473f8b6c runs the query `playernamemap.commentaryid where
   playernamemap.playerid = pid`), then the event `PLAYER_NAME_FE` is asked with `surname_ID = that id`;
2. else the player's name ids (0x140b23d18, not traced further; the DB rule above).

The name shown on screen follows another waterfall (`editedplayernames.commonname` → common name →
`editedplayernames.surname` → last name), which is why a name pick can keep the shown name through an
`editedplayernames` row while the commentary speaks the chosen one (the user's verified "generic substitution",
CONTEXT_PACK §4).

## 2. Which commentary language is loaded on this PC

Evidence:

* `C:\Program Files\EA Games\EA SPORTS FC 27\commentary\` holds exactly one downloaded language:
  `commentaryfull_ita_it\` (cas_01.cas 1.07 GB + cas_02.cas 447 MB) + `commentaryfull_ita_it.toc` (244 KB) and the
  matching `commentarylaunch_ita_it` pair (26 MB). Nothing else.
* `Data\Win32\` holds `commentaryfull_eng_us.toc` and `commentarylaunch_eng_us.toc` (the base game's English, cas data
  inside the base superbundles), `commentaryfull_ita_it.toc` / `commentarylaunch_ita_it.toc` (the install stubs of the
  downloaded pack) and `commentarywc_<12 languages>.toc` (World Cup mode banks, not a language pack).
* `%LOCALAPPDATA%\EA SPORTS FC 27\fcsetup.ini` carries only `CONFIG_APP_LOCALE = en-US` (the app locale, not the
  commentary) and the binary `settings\Settings…` / `ProfileOptions` files contain no language string at all. The
  registry has no `EA SPORTS FC 27` language key. Live Editor's own files carry no language setting either.
* In the running game the file system layer lists `/native_data/Data/Win32/commentarylaunch_ita_it.sb`,
  `commentarylaunch_eng_us.sb`, `commentaryfull_eng_us.sb` and `commentaryfull_ita_it.sb` (strings at 0x359EDD40..),
  and the speech system registers bank descriptors for every language
  (`Sound/Speech/LocCommentary/ita_it/ita_it_commentary_brt`, `ita_it_launch_commentary_brt`, `ita_it_wc_commentary_brt`,
  `Sound/PENTA/LocCommentary/ita_it/…`, the same for 13 other languages; strings at 0x7C354860..0x7C386000).

So the loaded commentary language is **Italian (`ita_it`)** on this PC: the game downloads only the language picked in
its audio settings, and that is the single pack under `commentary\`. Turbo decides at runtime
(`installed_commentary_packs` + `pick_commentary_language`):

1. the language chosen in the GUI (`gui_settings.json` → `callnames.language`) when its pack is installed;
2. else the one downloaded pack under `<game>\commentary\`;
3. else `eng_us` when its base toc exists; else the first pack found; else none.

The game folder is the folder of the running `FC27.exe` (`GetModuleFileNameW(nullptr)`, Turbo.dll runs inside the
game); the native tests point `App::game_root` at a fake folder. The chosen language, the reason and the installed
packs are shown in the tab, with a combo to pick another one and a Refresh button.

## 3. Which commentary ids are spoken in that language: what Turbo does

Three sources, in this order (`Callnames::refresh`):

1. **A hand-made list** `<Live Editor>\turbo\callnames\spoken_<lang>.txt` (one commentary id per line, optional header
   `#turbo-spoken <lang> <count>`, comments after a tab, space or `#`; made with `scripts/callnames_spoken_list.py` from a
   FIFA Editor Tool "Export Data Set" of the language's `pSIMPLE_SURNAME` selection table, whose `surname_ID` column is
   the commentary id). It is an **override**: when present its ids are the spoken surnames, whatever the capture says.
   `turbo/package/turbo/callnames/spoken_por_br.txt` ships as an example (FC 26 PT-BR generic bank, 1,703 ids).
2. **Turbo's own capture of the loaded bank** (`core/commentary_bank.cpp`, §6): the selection tables of the commentary
   families the game has in memory are read on a background thread, the surname ids with a recording and the player
   ids with their own recordings are cached in `<Live Editor>\turbo_output\callnames\spoken_<lang>.json` (format §6.4)
   and used from then on. The capture starts by itself the first time the Callname tab is opened without a list or a
   cache, and on the button *Capture from the loaded bank*. The players with recordings come from the capture even
   when a hand-made list overrides the surnames.
3. **Fallback**: every commentary id that `playernames` uses (other than 900000) counts as spoken; the tab says
   "Unverified: …" in orange and names the files it looked for.

What was tried and dropped:

* **Parsing the bank on disk.** The cas files of `commentarylaunch_ita_it` / `commentaryfull_ita_it` start with the
  FC 24+ cas header (`00 00 00 F2 D6 8E 79 9D …`) and the manifest in the toc is hashed (bundle names and chunk ids);
  the EBX with the selection tables is inside compressed chunks (cas_02.cas's 256 KB blocks of type 0x0070..0x0074 are
  the uncompressed audio streams, cas_01 holds the compressed metadata). A Frostbite superbundle + EBX reader with
  decompression would be needed: out of scope and fragile across title updates.
* **A guarded hook on the game's "has audio" lookup** (approach B of the brief): the lookup exists and is understood
  (§6.1: `CommentaryBridge` vtable slot 0xb8 → 0x1444f340c → handler 0x1414a9d14 → 0x1414aaef4 → 0x145ace484 →
  0x145acd02c → sound system `vcall(8)`), but it needs a `SpeechQuery` object built with the game's own helpers
  (0x1407b0f3c / 0x1407b03e4) and the bank bound to the events, which is only the case while a match runs. Reading
  the tables directly (§6) needs no call into game code and works in the menus too, so the hook was not built.

## 4. What the Players > Callname tab does

* Shows the language (combo of installed packs, auto-detect, Refresh), the spoken set and where it comes from
  ("Spoken set from live bank capture 2026-10-04 …: N names, M player callnames", or the list file, or the orange
  "Unverified" line), the capture button with its status line, and the player's current callname: commentary id,
  source (player-specific / common name / last name, with the name and name id), whether it is spoken in the loaded
  language, and — when the bank has the player's own recordings — "Recorded by name in ita_it".
* **By name**: type-ahead over `playernames` names whose commentary id is spoken (name, name id, commentary id, how
  many players use the name as common or last name). *Assign as last name* / *Assign as common name* write
  `players.lastnameid` / `commonnameid` directly (`Database::set`, range-checked). With *Keep the shown name* (default)
  the name parts shown before the change go to `editedplayernames`: edited in place when the player has a row, else
  added by Turbo's Lua side (`InsertDBTableRow`) through the mailbox command `callnames` / `set_display_name`.
* **By player**: type-ahead over players whose `playernamemap` callname is spoken (player, club, commentary id).
  *Use this player's callname* writes this player's `playernamemap.commentaryid` in place when the row exists, else
  queues `set_playernamemap` (row inserted by Lua). *Remove player-specific callname…* asks for confirmation and queues
  `remove_playernamemap` (`DeleteDBTableRowByAddr`). A player's *own* recordings (Real channel) cannot be given to
  another player from the database: they are bound to the player id inside the bank's selection table (FET's
  `player_db_pID` column, D-016), which Turbo only reads.
* Lua module `features/callnames.lua` (also `lua\scripts\turbo_callnames.lua` with `modules.callnames.actions` in
  `turbo_config.json`): `set_playernamemap`, `remove_playernamemap`, `set_display_name`, `set_name_ids`; every action is
  validated before the first one runs; dry run supported.

Tests: `turbo/tests/t12_callnames.lua` (9 cases) and in `turbogui/tests/native/test_main.cpp` the cases "callnames:
language packs, spoken list, resolution rule, index", "commentary bank: row signature, tables, capture over regions,
cache json", "callnames: bank capture cache, hand-made list override, Real recordings", "UI: Players > Callname: capture
of the loaded bank on a background thread, Real recordings" and "UI: Players > Callname: language, current callname,
pickers, name and player assignment".

## 5. How the game decides that a name has audio (reverse engineering, FC27.exe build 6AB9813C-211EF000)

Material: the image dump `turbo_output\fc27_image.bin` (image base 0x140000000) with `scripts/re/rx.py`, and the live
process through the dev service (`scripts/re/callnames_bank.py` repeats the live steps). All addresses are for this
build; the signature `speech_system_ptr` in `core/sigscan.cpp` finds the one Turbo would need.

### 5.1 The commentary event pipeline

The frontend asks the audio system by *event name* and *parameters*, never by family. Strings in the image:
`CommentaryBridge`, `CommentaryDbEvents`, the events `PLAYER_NAME_FE`, `PLAYER_LOW_SIMPLE`, `PLAYER_LOW_LINK`,
`PLAYER_NAME`, `PLAYER_NAME_MID`, `PLAYER_NAME_HIGH`, `PLAYER_NAME_START`, the parameters `surname_ID`
(0x1496A7408), `player_db_pID` (0x149622398), `player_intensity` (0x1497A5698), `cm_sim` (0x149675C88).

| step | address | what it does |
|---|---|---|
| frontend callname check | 0x14294a0f4 | `bridge = audio->vcall(0xe0)("CommentaryBridge")`; `ctx = audio->vcall(0x48)("CommentaryDbEvents", "PLAYER_LOW_SIMPLE")`; query {player_db_pID = pid, player_intensity = 2}; `bridge->vcall(0xb8)(query)`; same with `PLAYER_LOW_LINK`; if neither: `cid = playernamemap[pid]` (0x1473f8b6c); if cid > 0: `PLAYER_NAME_FE` with {surname_ID = cid, player_intensity = 2} |
| query helpers | 0x1407b0f3c (set the event context), 0x1407b03e4 (add an int parameter by name), 0x1407b0b6c (destroy) | parameter names are hashed with djb2-xor (0x141b74580: h = 5381; h = h*33 ^ c) |
| `CommentaryBridge::HasAudio` (vtable 0x14A8DC8C0 slot 23) | 0x14294fca0 → 0x1444f340c | looks the event up in the **registry** (`SpeechSystem+0x50`, hash map at +0x28: buckets `+0x30`, count `+0x38`; node: `+0` event id, `+8` ctx, `+0x10` "pre" handlers, `+0x30` handlers, `+0x50` next; ctx `+0x38` = name, `+0x44` = id) and asks every handler `vcall(0x10)(query)` |
| the one handler (object 0x7C378D40, vtable 0x14AD2F530) | 0x1414a9d14 | collects (param hash, value) pairs from the query and calls 0x1414aaef4 with the event *name* |
| event lookup | 0x1414aaef4 | finds the event by name in the base `CommentaryDb` (EBX partition 0x309250000.., 10,201 events: `+0x18` name, `+0x20` candidates, `+0x30` id) then 0x1414aafcc: when `db+0x74` is set the candidates come from the **language db** (`owner+0x48 → [y]`, hash map at `+0xd8/+0xe0` keyed by event id → node `+0x10` → candidate array); each candidate's parts (`+0x18` array) must pass 0x145ace484 → 0x145acd02c |
| part check | 0x145acd02c | for every selector parameter of the part (`+0x38` array, name at `+0x18`) takes the query's value, then asks the global sound system (`[0x14C255BB8]`, vtable 0x1496FB5A8) `vcall(8)(asset = part+0x28, values)` — the Frostbite variation selection on the family's selection table |

In the career hub the language db's event map is empty (count 1, the sentinel bucket) and the base db's candidate
arrays are empty: no event is bound to a family, so the hook route cannot answer anything outside a match. The
selection tables themselves, however, are resident (§6).

### 5.2 Step 1 of the user's hint: what feeds Create Player's commentary-name list

The UI bindings `GetCommentaryNameIds` (0x1470e1760) and `GetCommentaryNameIdsSorted` (0x1470e17d0, with the
`commentarystartingletter` argument) call the table reader 0x1480b2128:

1. it opens `commentarynames` (table hash 0xAE932D0 through the DB service) and selects `commentarypreview`,
   `commentarystring`, `commentaryid` **where `commentarystartingletter` = the letter asked for** (a 26-entry table
   gives the range for the letter index), caching the ids in `FETemp::CommentaryIndexList(Temp)`;
2. with the mode argument 2 it hands the id vector to the audio service (`[0x14C2A8590]->vcall(0x60)->vcall(0xc0)(ids)`),
   which removes the ids without a recording in the loaded bank — the same `PLAYER_NAME_FE` check as above, per id;
3. it returns rows of `ID` + `NAME` (`commentarystring`) to the screen.

The sibling 0x1480aef9c (`GetCommentaryName(id)`) returns `NAME` and `PREVIEW_AVAILABLE = (commentarypreview == 1)`:
`commentarypreview` is the per-row flag that a preview clip exists in the *launch* bank (the one the menus play), and
`commentarystartingletter` is the letter index the list is paged by. So Create Player's list = `commentarynames`
rows of the letter, filtered by the bank's own audio check — exactly what Turbo's capture reproduces from the tables,
without the per-letter paging.

## 6. The loaded bank's selection tables in memory (what Turbo reads)

### 6.1 Finding them

The selection tables are not reachable through names (the asset names `pSIMPLE_SURNAME` / `pPLAYER_NAMES_SIMPLE` and
the bundle name `ita_it_FULL` exist nowhere in memory: Frostbite hashes them) and, outside a match, not through the
event registry either (§5.1). They were found by their *content*: a `find` for the little-endian commentary ids of
names known from the FC 26 PT-BR export (920014 Abbiati, 930142 Carrillo, 930671 Yun, 930456 Murillo) gave, besides
Turbo's own tables and the dev service's pattern buffer, two hits per id 64 bytes apart or further in private heap
memory around 0x3D74xxxxx–0x3D94xxxxx, each inside a run of 64-byte records with the same shape. The shape (row = one
recorded variation; selector values as FET shows them in the family's Data Set):

```
+0x00 u32 selector value     surname_ID (commentary id) or player_db_pID (player id) or a team id
+0x04 u32 0
+0x08 ptr|3                  the variation object (tagged pointer, both low bits set)
+0x10 u32 player_intensity   2 in every row seen
+0x14 u32 0
+0x18 u32 variation hash     FET's VariationId
+0x1c u32 0x88 | index << 8  row index inside the table (0x88 = the EBX type tag of every row)
+0x20 ptr|3, +0x28 ptr|3, +0x30 ptr|3   selector objects (two of them shared by many rows, e.g. 0x128FF9D20)
+0x38 u32 cm_sim             1 in every row seen
+0x3c u32 0
```

Rows are 16-byte aligned and contiguous inside a Frostbite array whose element count (bit 31 set) sits 4 bytes before
the first row. The two rows of one id differ in the variation hash, the index and the pointers (the two recordings of
the name), never in the selector values — the "two selection rows per surname_ID, all player_intensity = 2" the
user measured in FET for FC 26 PT-BR.

Turbo (`capture_commentary_bank`) scans the game's committed private regions (host: `VirtualQuery`, MEM_PRIVATE,
readable, at least 64 KB) in 1 MB chunks with a 48-byte overlap, keeps every 16-byte aligned record that passes
`parse_bank_row` (zero words, intensity and cm_sim in 1..15, tag 0x88, four tagged user-mode pointers), groups the
rows 64 bytes apart into tables, reads each table's array header, and classifies a table of 8+ rows by its values:
all in 900000..965000 → a **surname family** (its ids are spoken surnames); all in 1..400000 → a **player-keyed
family** (its ids are players with their own recordings). Teams are keyed by small ids too (`team_ID`), so a
player-keyed table may be a team-name family: the player set is therefore "players whose id appears in a player-keyed
table", and the cache keeps per player in how many such tables it appears. Everything else (tiny runs, mixed values)
is ignored. The scan of this PC's game (6.9 GB readable, 3.8 GB private) takes a few seconds on the capture thread.

### 6.2 What is resident in the career hub (2026-10-04, ita_it)

The dev-service scan (`scripts/re/callnames_bank.py scan`) over 0x3D4000000..0x3DC000000: see the numbers in the
report of track E4 and in `turbo_output\callnames\spoken_ita_it.json` once Turbo has run its capture. Surname-range
rows alone exceed 44,000 in that area, i.e. many surname-keyed families (the full bank, not only the launch bank, is
resident while the career is loaded), each with the same ids; the union of their ids is the spoken surname set.

### 6.3 Static anchors (for later tracks)

| item | address | signature |
|---|---|---|
| `SpeechSystem*` global | 0x14C27D590 | `speech_system_ptr`: `48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 F0 00 00 00 48 8D 54 24 20 48 8B 08 4C 8B 81 D8 00 00 00` (rip, unique at 0x1444f6b2d) |
| global sound system | `[0x14C255BB8]` (vtable 0x1496FB5A8, slot 1 = variation selection 0x142f57718) | — |
| audio service used by the Create Player list | `[0x14C2A8590]` | — |
| `CommentaryBridge` vtable | 0x14A8DC8C0 (slot 23 = HasAudio 0x14294fca0, slot 33 = GetName) | object at 0x82D58940 on this run (+0x70 sub-object) |

### 6.4 The cache file

`turbo_output\callnames\spoken_<lang>.json`: `{"turbo_spoken": 2, "lang", "when", "build", "source", "note", "rows",
"regions", "bytes", "seconds", "surnames": [ids…], "players": [[playerid, tables]…], "tables": [{start, end, rows,
distinct, min, max, header, header_ok, kind}…]}`. A cache for another language or with no ids is rejected (the tab
says why). Delete the file to force a new capture, or press *Capture from the loaded bank*.

## 7. In-game test plan

1. In the career hub open Players > a Napoli player > Callname. Expected: "Spoken set from live bank capture …:
   N names, M player callnames" within a few seconds of the first visit (the log says `callnames: bank capture
   started` / `… rows in … tables`); `turbo_output\callnames\spoken_ita_it.json` exists.
2. Cross-check with the game: Create Player > commentary name list for one letter must equal the names of that
   letter in Turbo's By-name picker (the game's list is `commentarynames` of the letter filtered by the bank, §5.2).
3. Pick a spoken surname (e.g. *Abbiati*, 920014, or any Italian name from the picker) for a Napoli starter with
   *Assign as last name* + *Keep the shown name*; play a short match (Napoli, 2-minute halves). Expected: the
   commentator says the chosen surname, the name on screen is unchanged.
4. Create the override file `turbo\callnames\spoken_ita_it.txt` with one id that the capture does **not** list
   (an id from `commentarynames` with `commentarypreview` 0 for a name that is not Italian-recorded), press Refresh,
   assign it, play a match: expected silence for that player (no name spoken). Delete the file and press Refresh:
   the capture set is back.
5. Players with their own recordings: a star player (Turbo shows "Recorded by name in ita_it") is spoken by name even
   after *Remove player-specific callname…* — the recordings are bound to the player id in the bank.
6. During the match press *Capture from the loaded bank* once more and compare the counts with the hub's capture
   (the full bank is loaded for the match).

## 8. Open points

* The language of a resident table is inferred from the installed packs (one downloaded language) — if the base
  English bank were resident at the same time its ids would be captured too; the counts per table in the cache show
  whether more than one family set is present.
* The player-keyed tables are not told apart from team-keyed ones (same value range); a player id that happens to
  equal a team id with a recording would be marked "recorded by name" wrongly. Reading the selector-parameter name
  through the row's pointers (`+0x20..+0x30` objects) would settle it; not done.
* The per-event binding (language db, §5.1) was only seen empty (career hub); confirming the asset pointers
  (`part+0x28`) against the captured tables during a match is a follow-up for the in-game test.
