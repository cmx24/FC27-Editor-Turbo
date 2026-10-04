# Player callnames for the loaded commentary language (Turbo tracks D2 and E4)

A *callname* is the name the commentary speaks for a player. This note records how FC 27 binds it, which commentary
language this PC's game has loaded, how the game itself decides that a name has audio, how Turbo asks the game's own
audio service which callnames are *spoken* (the way the Create Player screen filters its name list), and what the
Players > Callname tab writes. Everything below was checked on 2026-10-03/04 on this PC (FC27.exe build 1.0.140.64835,
build key `6AB9813C-211EF000`, Live Editor v27.1.2, Manager Career, Italian commentary pack `ita_it`) unless marked
otherwise.

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
   the commentary id). It is an **override**: when present its ids are the spoken surnames, whatever the game says.
   `turbo/package/turbo/callnames/spoken_por_br.txt` ships as an example (FC 26 PT-BR generic bank, 1,703 ids).
2. **The set the game itself answers** (`core/commentary_audio.{h,cpp}`, `win/commentary_audio_win.cpp`; the RE in
   §5). Turbo hands every commentary id the database knows (`commentarynames.commentaryid`, plus the ids
   `playernames` and `playernamemap` use) to the game's own audio-service filter, the function the Create Player
   screen runs on its name list to drop the names without a recording, in batches on the game thread (one batch per
   frame through the game-thread dispatcher, the batch size adapted to a 4 ms budget, 25..400 ids), and asks the
   in-match events `PLAYER_LOW_SIMPLE` / `PLAYER_LOW_LINK` for every player id with `SpeechQuery`s built with the
   game's own helpers. The result is cached in `<Live Editor>\turbo_output\callnames\spoken_<lang>.json` with
   `"source": "game audio service"` (§6.4). The build starts by itself the first time the Callname tab is opened
   without a list or a cache; *Rebuild from the game's audio service* repeats it. The Status tab shows the call
   (`commentary_has_audio: ready | names: FilterNames 0x1439074C8 … | players: ready … | runs N | steps M`, then
   `last: ok: 1,8xx of 4,9xx commentary ids have audio, …`). Kill switch: `turbo_output\call_commentary_audio_off.txt`
   (plus every game-hook switch: a build that is not in the signature table turns the call off).
3. **Turbo's memory scan of the loaded bank** (`core/commentary_bank.cpp`, §6) stays as a diagnostic behind the button
   *Capture from the loaded bank*: it writes the same cache file with `"source": "live bank capture"` when it finds
   selection tables, which it does not on this build (what it matches is the bank's sample index, §6.1), so it reports
   "no selection table found" and leaves the cache alone.
4. **Fallback**: every commentary id that `playernames` uses (other than 900000) counts as spoken; the tab says
   "Unverified: …" in orange and names the files it looked for.

Precedence: the hand-made list > the cache (written by 2, the default and the automatic path, or by 3) > the fallback.
A list only overrides the surnames: the players with their own recordings still come from the cache.

What was tried and dropped:

* **Parsing the bank on disk.** The cas files of `commentarylaunch_ita_it` / `commentaryfull_ita_it` start with the
  FC 24+ cas header (`00 00 00 F2 D6 8E 79 9D …`) and the manifest in the toc is hashed (bundle names and chunk ids);
  the EBX with the selection tables is inside compressed chunks (cas_02.cas's 256 KB blocks of type 0x0070..0x0074 are
  the uncompressed audio streams, cas_01 holds the compressed metadata). A Frostbite superbundle + EBX reader with
  decompression would be needed: out of scope and fragile across title updates.
* **Reading the selection tables from memory** (§6): the only record shape found is the bank's sample index; the
  per-variation selector values are stored in a form that was not located. Asking the game (§5) needs no table.
* **A guarded hook on the game's "has audio" lookup**: not needed either; the lookup is *called*, with the game's own
  query objects, from the game thread (§5.4).

## 4. What the Players > Callname tab does

* Shows the language (combo of installed packs, auto-detect, Refresh), the spoken set and where it comes from
  ("Spoken set from the game's audio service (built 2026-10-04 00:40): N names, M player callnames", or the list file,
  or the orange "Unverified" line), the *Rebuild from the game's audio service* button with its progress ("building:
  names 1200 / 4849 (310 with audio), batch 400") or last result, the diagnostic *Capture from the loaded bank* button
  with its status line, and the player's current callname: commentary id, source (player-specific / common name / last
  name, with the name and name id), whether it is spoken in the loaded language, and - when the game has the player's
  own recordings - "Recorded by name in ita_it: … (PLAYER_LOW_SIMPLE + PLAYER_LOW_LINK)".
* **By name**: type-ahead over `playernames` names whose commentary id is spoken (name, name id, commentary id, how
  many players use the name as common or last name). *Assign as last name* / *Assign as common name* write
  `players.lastnameid` / `commonnameid` directly (`Database::set`, range-checked). With *Keep the shown name* (default)
  the name parts shown before the change go to `editedplayernames`: edited in place when the player has a row, else
  added by Turbo's Lua side (`InsertDBTableRow`) through the mailbox command `callnames` / `set_display_name`.
* **By player**: type-ahead over players whose `playernamemap` callname is spoken (player, club, commentary id).
  *Use this player's callname* writes this player's `playernamemap.commentaryid` in place when the row exists, else
  queues `set_playernamemap` (row inserted by Lua). *Remove player-specific callname…* asks for confirmation and queues
  `remove_playernamemap` (`DeleteDBTableRowByAddr`). A player's *own* recordings cannot be given to another player from
  the database: they are bound to the player id inside the bank (`player_db_pID`, D-016), which Turbo only asks about.
* Lua module `features/callnames.lua` (also `lua\scripts\turbo_callnames.lua` with `modules.callnames.actions` in
  `turbo_config.json`): `set_playernamemap`, `remove_playernamemap`, `set_display_name`, `set_name_ids`; every action is
  validated before the first one runs; dry run supported. The audio-service build needs no Lua: the GUI asks the host
  (`App::commentary_audio`, a `caudio::Service`), the host queues the steps on the game thread.

Tests: `turbo/tests/t12_callnames.lua` (9 cases) and in `turbogui/tests/native/test_main.cpp` the cases "callnames:
language packs, spoken list, resolution rule, index", "commentary bank: row signature, tables, capture over regions,
cache json", "callnames: bank capture cache, hand-made list override, Real recordings", "commentary audio: name batch
and canary, pointer chain checks, the stepped build with a fake caller, cache record and precedence", "signatures: the
built-in commentary-audio entries resolve on the game's bytes (registry + getter from one call site, strings by
offset)", "UI: Players > Callname: spoken set from the game's audio service (fake service): automatic build, status
line, Rebuild, pickers", "UI: Players > Callname: capture of the loaded bank on a background thread, Real recordings"
and "UI: Players > Callname: language, current callname, pickers, name and player assignment".

## 5. How the game decides that a name has audio (reverse engineering, FC27.exe build 6AB9813C-211EF000)

Material: the image dump `turbo_output\fc27_image.bin` (image base 0x140000000) with `scripts/re/rx.py`, and the live
process through the dev service (`scripts/re/callnames_bank.py` repeats the live steps). All addresses are for this
build; the signatures Turbo resolves are in `docs/re/E4-callnames-signatures.json` (checked against the image with
`bash scripts/re/py.sh scripts/re/verify_callnames.py`) and in the built-in table of `core/sigscan.cpp`.

### 5.1 The commentary event pipeline

The frontend asks the audio system by *event name* and *parameters*, never by family. Strings in the image:
`CommentaryBridge`, `CommentaryDbEvents`, the events `PLAYER_NAME_FE`, `PLAYER_LOW_SIMPLE`, `PLAYER_LOW_LINK`,
`PLAYER_NAME`, `PLAYER_NAME_MID`, `PLAYER_NAME_HIGH`, `PLAYER_NAME_START`, the parameters `surname_ID`
(0x1496A7408), `player_db_pID` (0x149622398), `player_intensity` (0x1497A5698), `cm_sim` (0x149675C88).

| step | address | what it does |
|---|---|---|
| query helpers | 0x1407b0ec0 (construct: `SpeechQuery* ctor(q, ScratchScope*, EventCtx*)`), 0x1407b0f3c (set the event context), 0x1407b03e4 (`SetInt(q, name, value)`: find the parameter by name hash, overwrite, else add), 0x1407b0b6c (destroy) | parameter names are hashed with djb2-xor (0x1407aef2c / 0x141b74580: h = 5381; h = h*33 ^ c). The query is a 0x84-byte stack object (vtable 0x149621F80; +0x10/+0x38/+0x68 the scratch scope, +0x18 parameter count, +0x20 parameter array, +0x48 0x14BCAD848, +0x50 1, +0x58 1.0f, +0x5C 2.0f). The scope (0x140670bf4 / 0x14053a030, 0x40 bytes) is a mark on the thread's scratch allocator the parameters are carved from; it is destroyed last, in LIFO order |
| `CommentaryBridge::HasAudio` (vtable 0x14A8DC8C0 slot 23 = 0xB8) | 0x14294fca0 -> 0x1444f340c | looks the event up in the **registry** (`SpeechSystem+0x50`, hash map at +0x28: buckets `+0x30`, count `+0x38`; node: `+0` event id, `+8` ctx, `+0x10` "pre" handlers, `+0x30` handlers, `+0x50` next; ctx `+0x38` = name, `+0x44` = id) and asks every handler `vcall(0x10)(query)` |
| the one handler (object 0x7C378D40, vtable 0x14AD2F530) | 0x1414a9d14 | collects (param hash, value) pairs from the query and calls 0x1414aaef4 with the event *name* |
| event lookup | 0x1414aaef4 | finds the event by name in the base `CommentaryDb` (EBX partition 0x309250000.., 10,201 events: `+0x18` name, `+0x20` candidates, `+0x30` id) **with a linear `strcmp` over the array** (one HasAudio costs ~10k string compares, which is why Turbo budgets its batches per frame) then 0x1414aafcc: when `db+0x74` is set the candidates come from the **language db** (`owner+0x48 -> [y]`, hash map at `+0xd8/+0xe0` keyed by event id -> node `+0x10` -> candidate array); each candidate's parts (`+0x18` array) must pass 0x145ace484 -> 0x145acd02c |
| part check | 0x145acd02c | for every selector parameter of the part (`+0x38` array, name at `+0x18`) takes the query's value, then asks the global sound system (`[0x14C255BB8]`, vtable 0x1496FB5A8) `vcall(8)(asset = part+0x28, values)` - the Frostbite variation selection on the family's selection table |

A read of the language db's event map in the career hub on 2026-10-04 found it empty (count 1, the sentinel bucket)
and the base db's candidate arrays empty: if that holds, HasAudio answers "no" for everything outside the screens
that bind the bank, and Turbo's build reports it instead of caching an empty set (§5.4). Whether the hub binds
`PLAYER_NAME_FE` is the first thing the in-game test plan (§7) settles.

### 5.2 What feeds Create Player's commentary-name list (fully traced)

The UI bindings `GetCommentaryNameIds` (0x1470e1760) and `GetCommentaryNameIdsSorted` (0x1470e17d0, with the
`commentarystartingletter` argument) call the table reader 0x1480b2128:

1. it opens `commentarynames` (table hash 0xAE932D0 through the DB service) and selects `commentarypreview`,
   `commentarystring`, `commentaryid` **where `commentarystartingletter` = the letter asked for** (a 26-entry table at
   0x14A12FD80 gives the range for the letter index), caching the row indexes in `FETemp::CommentaryIndexList(Temp)`;
2. with the mode argument 2 it builds an `eastl::vector` of 8-byte elements `{int32 row, int32 commentaryid}` (one per
   row; after the call the loop reads the ID column from `+4` and the row from `+0`) and hands it to the audio service:
   `registry = [0x14C2A8590]`; `GetCommentaryService(&svc, registry)` = 0x142a52420 (`registry->vcall(0x40)(0xA621C80)`
   finds the service entry, `entry->vcall(0x18)(0xA621C86)` is its QueryInterface, 0x142899df4, which returns the entry
   itself for the ids 0xEE3F516E / 0xA621C86 - and the lookup takes a reference that the caller drops afterwards through
   `svc->vcall(8)` = 0x141d4281c, a `lock xadd -1` on `svc+0x10`); `names = svc->vcall(0x60)` = 0x1438a59a0 = `[svc+0x40]`;
   `names->vcall(0xC0)(&vector)` = 0x1438aee38 = `if ([names+8]) FilterNames([names+8], &vector)`;
3. **FilterNames** 0x1439074c8 (`inner`, `vector*`): `bridge = [inner+0x10]->vcall(0xE0)("CommentaryBridge")` (returns
   without touching the vector when there is none); `ctx = audio->vcall(0x48)("CommentaryDbEvents", "PLAYER_NAME_FE")`;
   one scratch scope + one `SpeechQuery(scope, ctx)` with `player_intensity = 2`; then for every element
   `SetInt(q, "surname_ID", element.id)`, `bridge->vcall(0xB8)(q)`, and when it answers false the element is erased in
   place (`memmove` of the tail, `end -= 8`; the order is kept, the vector never grows); query and scope destroyed;
4. it returns rows of `ID` + `NAME` (`commentarystring`) to the screen.

The classes: the service (vtable 0x14A8C6D70, constructor 0x1438a145c, 1,117 code references to the registry global)
allocates the names object (vtable 0x14A8C5F48) at `svc+0x40` with `+8 = null`; `+8` is bound later, when the audio is
up, and `[inner+0x10]` is the audio system. The sibling 0x1480aef9c (`GetCommentaryName(id)`) returns `NAME` and
`PREVIEW_AVAILABLE = (commentarypreview == 1)`: `commentarypreview` is the per-row flag that a preview clip exists in
the *launch* bank, `commentarystartingletter` is the letter index the list is paged by.

### 5.3 The player-specific path (the in-match / frontend per-player check)

0x14294a0f4 is `int GetCallname(this, int playerid, int mode)` (`this+8` = the audio system; its only caller is
0x1438eba79 in 0x1438eb8b0, which walks a 0xB10-byte player array and passes `playerid` from `+8` and `mode` from `+4`):

0. `bridge = audio->vcall(0xE0)("CommentaryBridge")`; two queries on one scratch scope, `{player_db_pID = playerid,
   player_intensity = 2}` with the contexts of `PLAYER_LOW_SIMPLE` and `PLAYER_LOW_LINK` (a null context is tolerated:
   the query is built without it); if either `HasAudio` is true the function returns **-1**: the player is spoken by
   his own recordings;
1. else `playernamemap.commentaryid where playerid` (0x1473f8b6c); when > 0 a third query `{surname_ID = that id,
   player_intensity = 2}` with `PLAYER_NAME_FE`; if `HasAudio` the id is returned;
2. else the player record is loaded (0x140b1f9b4 / 0x140b23d18, 0xCD8 bytes) and the commentary id of the common name
   (`+0xB08`, when `+0x4C4` is set) or of the last name (`+0xB04`) is returned, **without** an audio check.

`PLAYER_LOW_SIMPLE` / `PLAYER_LOW_LINK` are referenced nowhere else in the image, so no vector-style helper exists for
the player path: Turbo builds the same two queries itself (§5.4) rather than calling this function (whose -1 also
means "no bridge", and whose step 2 loads a player record per call).

### 5.4 What Turbo calls (`win/commentary_audio_win.cpp`, on the game thread, one batch per frame)

* **Names** (the Create Player check): `svc = GetCommentaryService(&out, [registry])`; the chain is read and checked
  through `ProcessMemory` before anything is called (`[svc]` = the service vtable, `[svc+0x40]` = names with its
  vtable, `[names+8]` = inner non-null, `[inner+0x10]` = audio readable; `core/commentary_audio.cpp resolve_chain`);
  a Turbo-owned vector `{begin, end, capacity, allocator}` over a batch of `{row, id}` elements plus a **canary**
  element (id 999999, never a name: `commentaryid` is a 20-bit field, names are 900000..965000) is handed to
  `FilterNames(inner, &vector)`; the survivors are checked against what was asked (rows increasing, ids unchanged;
  `unpack_name_batch`) and the canary must be gone - if it survived the filter did not run (no bridge in this screen)
  and the build fails instead of counting everything as spoken; the service reference is released through its vtable
  slot 1 (only when the vtable is the expected one). A build whose every id came back "no" fails too ("the loaded bank
  is not bound in this screen") and is never cached.
* **Players** (the in-match check): with the same chain, `bridge = audio->vcall(0xE0)("CommentaryBridge")`, and for
  each of `PLAYER_LOW_SIMPLE` / `PLAYER_LOW_LINK`: `ctx = audio->vcall(0x48)("CommentaryDbEvents", event)` (an unknown
  event is skipped; both unknown = "not bound in this screen", the names stand and the result notes it), scope ctor,
  query ctor(scope, ctx), `SetInt(player_intensity, 2)`, per player `SetInt(player_db_pID, id)` +
  `bridge->vcall(0xB8)(q)`, query dtor, scope dtor. The answer is a bit mask per player (1 = SIMPLE, 2 = LINK), stored as
  the cache's `players` value. Every string the game sees is the game's own constant (resolved from the instructions
  that load them: `commentary_str_*` signatures), the objects live on Turbo's stack with the game's sizes rounded up
  (query 0x100 bytes, scope 0x40).
* **Scheduling** (`core/commentary_audio.cpp Build`): names first, then players; the first batch is 100 ids, a step
  that took more than 4 ms halves the next batch (down to 25), one under 2 ms doubles it (up to 400); each step is one
  job of the game-thread dispatcher (`run_on_game_thread`, the game_tick hook drains jobs queued by jobs on the next
  frame), so ~4,900 ids + ~20,000 players take a few seconds without a stall. Every step runs inside try/catch; a throw
  ends the build as failed. Progress and counters are on the Status tab; the finished result is polled by the GUI,
  cached (§6.4) and the pickers rebuilt.

**Live check, 2026-10-04 00:53, career hub right after the restart (read-only dev-service reads, no call):** the
registry at `[0x14C2A8590]` = 0x35CD11B0 is an open-addressing table of 0x21D slots (keys at `+0x48`, values at
`+0x8C0`, the active half picked by the int at `+0x3308`; AddRef is the value's vtable slot 0, which settles the
AddRef / Release pairing); id 0xA621C80 sits in slot 525 -> service 0x683819A0 with vtable 0x14A8C6D70 and reference
count 1; `+0x40` -> names 0x683F0920 with vtable 0x14A8C5F48; `+8` -> inner 0x67388FC0 (bound); `+0x10` -> audio system
0x82D5A4E0 (vtable 0x14A8DC638, slot 0xE0 = 0x141DFD784: FNV-1a name hash into the map at `+0x90`/`+0x98`, strcmp on the
node's name, object at node `+8`); "CommentaryBridge" -> 0x82D5A630 with the bridge vtable 0x14A8DC8C0. So in the hub
every object the build touches exists and `FilterNames` runs; what `HasAudio` answers there is step 2 of §7.

### 5.5 Signatures (18, all unique in the image; `docs/re/E4-callnames-signatures.json`)

| name | resolves to | anchor |
|---|---|---|
| `commentary_service_registry` | 0x14C2A8590 (the registry pointer slot) | the call site 0x1480B255F in the list reader, rip |
| `commentary_service_get` | 0x142A52420 GetCommentaryService | same pattern, the `call` at +12 |
| `commentary_filter_names` | 0x1439074C8 FilterNames | its prologue |
| `commentary_str_bridge`, `_player_name_fe`, `_db_events`, `_player_intensity`, `_surname_id` | the string constants | the `lea`s inside FilterNames (+0x22, +0x42, +0x50, +0x7F, +0x99) |
| `commentary_str_player_low_simple`, `_player_db_pid`, `_player_low_link` | the string constants | the `lea`s inside 0x14294A0F4 (anchor 0x14294A15B; +4, +0xC5, +0xEF) |
| `speech_query_ctor` / `_set_int` / `_dtor` | 0x1407B0EC0 / 0x1407B03E4 / 0x1407B0B6C | prologues |
| `scratch_scope_ctor` / `_dtor` | 0x140670BF4 / 0x14053A030 | prologues |
| `commentary_service_vtable` / `commentary_names_vtable` | 0x14A8C6D70 / 0x14A8C5F48 | the `lea`s in the service constructor (0x1438A149A / 0x1438A1509), rip |

## 6. The loaded bank in memory: what was found, what Turbo reads

### 6.1 The 64-byte records (the bank's sample index, not a selection table)

The selection tables are not reachable through names (the asset names `pSIMPLE_SURNAME` / `pPLAYER_NAMES_SIMPLE` and
the bundle name `ita_it_FULL` exist nowhere in memory: Frostbite hashes them) and, outside a match, not through the
event registry either (§5.1). A `find` for the little-endian commentary ids of names known from the FC 26 PT-BR export
(920014 Abbiati, 930142 Carrillo, 930671 Yun, 930456 Murillo) gave two hits per id in private heap memory around
0x3D74xxxxx–0x3D97xxxxx, each inside long runs of 64-byte, 16-byte-aligned records of one shape:

```
+0x00 u32 key            +0x04 u32 0
+0x08 ptr|3              a record of the same shape (tagged pointer, low bits set)
+0x10 u32 2              +0x14 u32 0
+0x18 u32 hash           +0x1c u32 0x88 | index << 8   (the EBX instance tag every object of the partition carries)
+0x20 ptr|1 / ptr|3      +0x28 ptr|3      +0x30 ptr|1 / ptr|3
+0x38 u32 1              +0x3c u32 0
```

Reading every run back (`docs/re/E4-callnames-live/tables_live_2026-10-04.json`, 149 runs, 138,802 records with a
key in the 0x0D/0x0E byte range alone) showed what they are: **one key-sorted sequence**, each run's highest key being
the next run's lowest (851,962 → 947,313; the runs are only split by the 4 KB guard pages between the 68 KB heap
regions), two records per key with a few singles, 35,486 distinct keys in 900000..947313 alone — far more than the
4,849 ids of `commentarynames`. The keys are the bank's sample numbers, the records are the nodes of a map/tree over
them (`+0x20` and `+0x30` of leaf nodes point at two shared sentinel nodes, 0x128FF9D20 / 0x128FF9460; `+0x08` and
`+0x28` at other nodes), and the known commentary ids were found simply because every integer of the range is a key.
Selector values packed as int or float triples (cm_sim, surname_ID, player_intensity in any order, for 920014) exist
nowhere in memory: the selection data is stored in another form (per-variation parameter objects with hashed names,
§5.1's `part+0x38` list, most likely), which was not reached before the game restart of 2026-10-04 00:15.

So the row shape that `capture_commentary_bank` matches is this index. The capture keeps the mechanism (scan of the
private regions on a background thread, run grouping, cache, UI) but **rejects** what it finds unless it looks like a
selection table:

* a run whose distinct keys cover more than half of their own range is an *Index* (`classify_bank_table`,
  `kBankMaxDensity`); the sample index covers ~100 %;
* a surname table must consist, for at least 90 % of its values, of commentary ids the database knows
  (`commentarynames.commentaryid`, else the ids `playernames` uses; `App::commentary_ids`), otherwise *Rejected*;
* player-keyed tables (values 1..400000) are only accepted when sparse, like the surname ones.

On this PC's hub the result is therefore "no selection table found (N runs rejected: dense index or unknown ids)", the
cache is not written and the tab keeps the fallback. The native tests feed synthetic sparse tables and a dense run.

### 6.2 What is resident in the career hub (2026-10-04, ita_it)

* The sample index above: ~140k records / ~95k keys (851,962..947,313 and below), i.e. the whole full bank's samples
  are indexed while the career is loaded, not only the launch bank's.
* The base `CommentaryDb` (EBX partition 0x309250000.., 10,201 named events, candidate arrays empty) and the event
  registry (15,173 buckets, 8,297 used, 12,395 nodes); the language db's event map is empty (count 1, sentinel):
  nothing bound outside a match.
* The speech bank descriptors of all 14 languages (`…/ita_it/ita_it_commentary_brt`, `_launch_`, `_wc_`, PENTA) and
  the file-system list of the four installed commentary superbundles (§2).
* Live Editor's / the game's own name tables in EBX form (32-byte `playernames`-like rows with commentaryid 900000 at
  0x3FFD26xxx, 56-byte `commentarynames`-like rows with the name text inline at 0x40037Bxxx).

### 6.3 Static anchors (for later tracks)

| item | address | signature |
|---|---|---|
| `SpeechSystem*` global | 0x14C27D590 | `speech_system_ptr`: `48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 F0 00 00 00 48 8D 54 24 20 48 8B 08 4C 8B 81 D8 00 00 00` (rip, unique at 0x1444f6b2d) |
| `[SpeechSystem+0x50]` | the commentary event registry (§5.1) | — |
| `[SpeechSystem+0x58]` | the variation selector (vtable 0x14AB5CB20; slot 0xd8 = 0x141a8a2fc, a cache keyed by the part descriptor in front of a callback) | — |
| global sound system | `[0x14C255BB8]` (vtable 0x1496FB5A8, slot 1 = 0x142f57718, the Frostbite sound-entity start with parameters) | — |
| audio service used by the Create Player list | `[0x14C2A8590]` | — |
| `CommentaryBridge` vtable | 0x14A8DC8C0 (slot 23 = HasAudio 0x14294fca0, slot 33 = GetName) | object at 0x82D58940 on that run (+0x70 sub-object); 0x82D5A630 on the 00:47 run |
| audio service registry slot, getter, FilterNames, SpeechQuery / scope helpers, the two vtables, the 8 strings | §5.5 | `commentary_*`, `speech_query_*`, `scratch_scope_*` in `core/sigscan.cpp` (18 entries) |

### 6.4 The cache file

`turbo_output\callnames\spoken_<lang>.json`: `{"turbo_spoken": 2, "lang", "when", "build", "source", "note", "rows",
"checked_names", "checked_players", "steps", "rejected", "regions", "bytes", "seconds", "surnames": [ids…],
"players": [[playerid, value]…], "tables": […]}`. `source` is `"game audio service"` (the default path, §5.4: `surnames`
= the ids the game answered yes for, `players` values = 1 PLAYER_LOW_SIMPLE | 2 PLAYER_LOW_LINK, `checked_*` = what was
asked, `steps` = game-thread ticks used, `seconds` = wall time) or `"live bank capture"` (the memory scan, §6: `players`
values = tables the id was in). A cache for another language or with no ids is rejected (the tab says why). Delete
the file to force a new build, or press *Rebuild from the game's audio service*.

## 7. In-game test plan

1. **The call is live.** Career hub, Turbo window > Status tab > Game hooks: every `commentary_*`, `speech_query_*` and
   `scratch_scope_*` signature "found at 0x…" (18 of them; `commentary_filter_names` at 0x1439074C8,
   `commentary_service_registry` at 0x14C2A8590), and under Game calls the line
   `commentary_has_audio: ready | names: FilterNames 0x1439074C8, GetCommentaryService 0x142A52420, registry slot
   0x14C2A8590 | players: ready … | runs 0 | steps 0`. With `turbo_output\call_commentary_audio_off.txt` present the
   line says `off (kill switch …)` and the Rebuild button is disabled; delete the file again.
2. **The build in the hub.** Players > a Napoli player > Callname (with no `spoken_ita_it.txt` and no
   `spoken_ita_it.json`). Expected: the build starts by itself ("asking the game's audio service about 4,9xx
   commentary ids and 2x,xxx players (automatic)…"), the Status tab shows `build: names 1200 / 4919 (…), batch 400` for a
   few seconds and then `last: ok: N of 4,9xx commentary ids have audio, M of … players have their own recordings
   (… steps, … s in the game)`; the log has `game call commentary_has_audio: build started (… via hook)` and
   `build ok: …`; the tab shows "Spoken set from the game's audio service (built hh:mm): N names, M player callnames";
   `turbo_output\callnames\spoken_ita_it.json` has `"source": "game audio service"`. N for an Italian pack should be
   in the low thousands (FC 26 PT-BR: 1,703 generic surnames). The game must not stutter while the build runs (one
   batch per frame, 4 ms budget).
   *If instead* the build fails with "the game answered 'no audio' for every one of the … ids: the loaded bank is not
   bound in this screen" (§5.1's empty language db in the hub) or "the canary id survived: the game's filter did not
   run": press *Rebuild from the game's audio service* from a screen that shows commentary names - Customise > Create
   Player > Commentary name (the list there is this very filter) - or during a match (pause, open Turbo); note in §8
   which screens answer. The players part may only answer during a match (`PLAYER_LOW_*` are bound with the full
   bank): the result then says "players not checked: the events PLAYER_LOW_SIMPLE / PLAYER_LOW_LINK are not bound in
   this screen" and the names still count; a Rebuild during a match fills the players.
3. **Cross-check against the game.** Create Player > Commentary name > letter A: every name listed there appears in
   the Callname tab's *By name* picker (type "a"), and a name the picker does not list is not in the game's list
   either (the same check, batch-wise).
4. **A spoken surname in a match.** Pick a name the picker lists for a Napoli starter with *Assign as last name* +
   *Keep the shown name*; play a short match (2-minute halves). Expected: the commentator says the chosen surname, the
   name on screen is unchanged.
5. **An unlisted id stays silent.** Write `turbo\callnames\spoken_ita_it.txt` with one id the picker does **not** list
   (e.g. one that commentarynames has but the build dropped), press Refresh (the tab shows "hand-made list"; the
   players with recordings still come from the game-built cache), assign it, play a match: expected silence for that
   player. Delete the file, Refresh: the game-built set is back.
6. **A player with his own recordings.** Open a famous player whose cached `players` value is non-zero: the green
   "Recorded by name in ita_it: … (PLAYER_LOW_SIMPLE + PLAYER_LOW_LINK)" line; in a match his own name is said whatever
   his name ids say.
7. **The diagnostic scan.** *Capture from the loaded bank* still runs on a background thread and reports "no selection
   table found (… runs rejected …)" without touching the cache.

## 8. Open points

* **Which screens bind the bank.** The hub's language-db event map looked empty on 2026-10-04 (§5.1); the build detects
  an unbound bank (all "no") and an absent bridge (the canary) instead of caching, but it has not yet been seen
  answering in-game. If the hub does not answer, the automatic build should be re-tried from the Create Player screen
  or a match (step 2 of §7), and a note in the tab should say so; a rebuild during a match is also what fills the
  players (`PLAYER_LOW_*`).
* **Launch bank vs full bank.** `PLAYER_NAME_FE` is the frontend preview event (the launch bank, 26 MB); the match
  speaks `PLAYER_NAME` / `PLAYER_LOW_*` from the full bank. The Create Player list is the game's own promise that a
  name has recordings; step 4 of §7 checks that promise on one name.
* **Cost.** One HasAudio is a linear scan over 10,201 event names (§5.1); the batch budget keeps a frame under ~4 ms
  of extra work, so ~25,000 queries take a few seconds. If the game still hitches, lower `BuildRequest::batch_max`.
* **Title updates.** Every pattern is unique on build 6AB9813C-211EF000; `scripts/re/verify_callnames.py` re-checks
  them on a new image, and a build that is not in the signature table turns the call off (the tab falls back).
* The memory scan's row signature (§6.1) still matches the sample index only; it is kept as a diagnostic.
