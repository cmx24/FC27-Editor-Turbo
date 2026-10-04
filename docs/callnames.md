# Player callnames for the loaded commentary language (Turbo tracks D2, E4 and E8)

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
   override of the generic surname bank; 106 rows in the FC 26 PT-BR database, 38 of them in a `980xxx` range that no
   `commentarynames` row knows. Turbo writes only ids in 900000..965000. Correction (1.0.2): ids above 965000 are not
   "dangling" everywhere - the user's lists hold generic recordings there in some languages (eng_us 980001..980034,
   ita_it 999931..999952, spa_es and dut_nl 9999xx), so a row holding one may be spoken (§4, full table);
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
   steps below give. Which players have such recordings: the game's audio service in a match, and the user's FC 26
   lists (§9);
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
   **Where the game answers (track E8).** The bank is not bound in the career hub: there every id answers "no audio"
   (§5.6), and such a build fails instead of caching an empty set. So while the set is not verified Turbo's *watcher*
   (`caudio::SpokenWatch`, run from `App::tick` whether the window is shown or not) sends a quiet *probe* every 3 s - a
   sample of up to 64 ids (the `commentarypreview` ones first, then 48 spread over the list) through the same
   FilterNames call, one step - and starts the full build by itself as soon as a probe answers "bound" (the Create
   Player screen, main menu or career; a match). The Callname tab and the Status tab say meanwhile "spoken set not
   built yet: open the game's Create Player screen (Customise > Create Player > Commentary name) or start a match,
   Turbo builds it there (last check hh:mm:ss: the bank is not bound in this screen; checking again every 3 s)". The
   result persists in the cache for later hub sessions. The id list comes from the career database when it is
   connected and is then cached in `turbo_output\callnames\ids.json` (§6.4), so that a build from the main menu's
   Create Player screen (no database) asks the same ids; without that cache the ids come from Lua's
   `bridge_commentary.txt`. The host logs a probe only when its outcome changes (`probe (64 ids): not bound in this
   screen` / `bound (23 of 64 sample ids have audio)`), the Status tab counts them (`runs N | probes M | steps K`,
   `last probe: …`).
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
  *Done since (2026-10-04, outside Turbo.dll):* `turbo/tools/fc27_commentary/fc27_commentary.py` reads the whole chain
  (toc, bundle manifest, Oodle blocks, the families' `SBle` data sets, the EA Opus audio) and writes every
  `pSIMPLE_SURNAME` / `pPLAYER_NAMES_SIMPLE` / `pPLAYER_NAMES_LINK` row plus the wavs; the formats, counts and the
  cross-checks against the game's own answers are in `docs/re/fc27-commentary-bank.md`.
* **Reading the selection tables from memory** (§6): the only record shape found is the bank's sample index; the
  per-variation selector values are stored in a form that was not located. Asking the game (§5) needs no table.
* **A guarded hook on the game's "has audio" lookup**: not needed either; the lookup is *called*, with the game's own
  query objects, from the game thread (§5.4).

## 4. What the Players > Callname tab does

* Shows the language (combo of installed packs, auto-detect, Refresh), the spoken set and where it comes from
  ("Spoken set from the game's audio service (built 2026-10-04 00:40): N names, M player callnames", or the list file,
  or the orange "Unverified" line followed by the watcher's "spoken set not built yet: open the game's Create Player
  screen … or start a match, Turbo builds it there (…)" line, §3), the *Rebuild from the game's audio service* button with its progress ("building:
  names 1200 / 4849 (310 with audio), batch 400") or last result, the diagnostic *Capture from the loaded bank* button
  with its status line, the master list ("Your FC 26 list: 4127 players with their own recording, 1202 generic
  names", or "FC 27 master: …" for a list built from the game, §9), and the player's current callname: commentary id,
  source (player-specific / common name / last name, with the name and name id), whether it is spoken in the loaded
  language - or, when the player has his own recording (the game's audio service or the master list says so, §9),
  "Current callname: his own recording in ita_it (your FC 26 list)" (or "(the FC 27 master)") in green with the rule's
  result below it as "Not used while he has it: …". Never "none" for such a player. When only the FC 26 list says so
  and the game's audio service asked about players without listing him, the line adds "not confirmed by the game's
  audio service, whose list is incomplete" (`Callnames::own_unconfirmed`; the list's answer still counts).
* **Own recording gate** (§9): for a player with his own recording both pickers show an orange line above their
  buttons ("He has his own recording (…): a callname set here will not be heard. Writing asks to confirm.") and the
  buttons open a confirmation (*Assign anyway* / *Cancel*) instead of writing; nothing is written or queued before
  *Assign anyway*, and every write asks again.
* **Players without own recording** (third picker tab): the players of this player's club whose name the game takes
  from the callname rule (no own recording known), i.e. the ones a callname test can be heard on; a click opens the
  player. The club's players with their own recording are named below the list.
* **By name**: type-ahead over `playernames` names whose commentary id is spoken (name, name id, commentary id, how
  many players use the name as common or last name). The route is chosen first (1.0.3, §11) and named in one line
  above the button: the player-specific route (*Assign callname*) when it can be used, else *Assign as last name* /
  *Assign as common name*, which write `players.lastnameid` / `commonnameid` (`Database::set`, range-checked). With
  *Keep the shown name* (default) the name parts shown before the change and his shirt name go to `editedplayernames`
  **before** the name id: edited in place when the player has a row (a failed write stops the name id), else Turbo's
  Lua side adds the row and then writes the name id, in one mailbox command `callnames` /
  `[set_display_name, set_name_ids]`.
* **By player**: type-ahead over players whose `playernamemap` callname is spoken (player, club, commentary id).
  *Use this player's callname* writes this player's `playernamemap.commentaryid` in place when the row exists (the
  index's row address is used only while that row still holds him, else the row is looked up again), else queues
  `set_playernamemap` (row inserted by Lua) when the table has room. The command carries `"room": true`, the table's
  `"capacity"` and the bridge's `"load_gen"`; Lua counts the rows again right before `InsertDBTableRow` and refuses when
  they reach the capacity or a career was loaded since (the command runs at the next career event, and FC 27 reloads
  the table full). On a full table (FC 27: 106 of 106) a row is taken over only when no player hears a callname from it
  (`Callnames::spare_playernamemap_row`): first a row whose player is not in the database, then one whose callname is
  none, then one whose callname has no recording in the loaded language (`Callnames::spoken_answer`: the spoken set
  for 900001..965000, the master list's generic ids above); a row whose callname is spoken or cannot be checked is
  never taken. The line above the button and the confirmation popup name that row's player before anything is
  written; without such a row the button says why and suggests By name. *Remove player-specific callname…* asks for confirmation and queues
  `remove_playernamemap` (`DeleteDBTableRowByAddr`). A player's *own* recordings cannot be given to another player from
  the database: they are bound to the player id inside the bank (`player_db_pID`, D-016), which Turbo only asks about.
* **Kept across career loads (1.0.2).** FC 27 reloads `playernamemap` from its base data every time a career loads
  (measured 2026-10-04 on 1.0.140.64835: a row edit was gone after save + reload, while `players.lastnameid` and
  `editedplayernames` were kept), so a player-specific callname lasted one session. Every callname *Use this player's
  callname* writes or queues is also kept in `turbo_output\reapply_edits.json` (`"playernamemap": [{"playerid",
  "commentaryid", "player", "from", "when"}]`, one entry per player, the last assignment wins) and written again the
  first time Turbo connects to a newly loaded career (`App::reapply_stored_edits`, once per Lua session + `load_gen`,
  even with the window hidden). `load_gen` is published by Lua's `bridge.lua` next to `db_gen` and changes only when a
  career is loaded, entered or left (a reload event, `in_cm` flipping, another database service); `db_gen` also changes
  when the manager changes club or on a refresh command, which reload nothing, so a club change mid-career or a press
  of Refresh does not re-apply. The write is the tab's own (`write_player_callname` with `allow_insert` false): the
  player's row in place (an id already in place is not written again), else a row taken over as above; it never adds
  a row (its room check would be read while the game reloads the table: "he has no playernamemap row in this career;
  assign the callname again"). Never written, and not an error: a player who is not in the career's database, and a
  player with his own recording (`Callnames::own_recording`: the master list or the game's audio service; the game says
  that recording and never a player-specific callname). Such a player's callname is not even kept: *Use this
  player's callname* / *Assign anyway* writes it for the session and says "Not kept for the next career loads: he has
  his own recording …", dropping an older kept one. Without a known commentary language nothing is written (own
  recordings cannot be checked). The tab shows "Kept for every career load: player-specific callname N (from …)" with a
  **Forget** button (the callname stays until the career is loaded again), and *Remove player-specific callname…*
  forgets it too. One summary line (`re-apply at career load: re-applied N kit colours, M player callnames (K already in
  place); J not written: …; I left alone: …`) goes to the GUI log and `turbo_gui.log` and is shown in this tab and in
  Teams > Colours; a toast only when something was written (an error toast when a write failed), none for entries left
  alone. Kill switch: `turbo_output\reapply_off.txt`. Generic callnames (By name) need none of this: `lastnameid` /
  `commonnameid` and `editedplayernames` are saved with the career.
* Lua module `features/callnames.lua` (also `lua\scripts\turbo_callnames.lua` with `modules.callnames.actions` in
  `turbo_config.json`): `set_playernamemap`, `remove_playernamemap`, `set_display_name`, `set_name_ids`; every action is
  validated before the first one runs; dry run supported. The audio-service build needs no Lua: the GUI asks the host
  (`App::commentary_audio`, a `caudio::Service`), the host queues the steps on the game thread.

Tests: `turbo/tests/t12_callnames.lua` and in `turbogui/tests/native/test_main.cpp` the cases "callnames:
language packs, spoken list, resolution rule, index", "commentary bank: row signature, tables, capture over regions,
cache json", "callnames: bank capture cache, hand-made list override, Real recordings", "commentary audio: name batch
and canary, pointer chain checks, the stepped build with a fake caller, cache record and precedence", "commentary
audio: probe flags, probe sample, id cache, Lua list, the watcher state machine, an unbound result is never cached",
"UI: Players > Callname: the watcher: an unbound bank is probed, never cached, and built by itself once the game
answers; the id cache serves a build without the database", "signatures: the
built-in commentary-audio entries resolve on the game's bytes (registry + getter from one call site, strings by
offset)", "UI: Players > Callname: spoken set from the game's audio service (fake service): automatic build, status
line, Rebuild, pickers", "UI: Players > Callname: capture of the loaded bank on a background thread, Real recordings",
"UI: Players > Callname: language, current callname, pickers, name and player assignment" (also: a full table's row
choice and the takeover named before the write, the Lua add carrying the table's capacity, an assigned callname kept and
a removed one not), and for §9 "callnames: your FC 26 list (masters json): loader, own-recording sources and
precedence" (with the FC 27 master's name and the "not confirmed" note), "callnames: a full playernamemap: the row
taken over never holds a callname spoken in the loaded language" and "UI: Players > Callname: own recordings from your
FC 26 list: current callname line, warning and confirmation before any write, players without own recording" (also
the common-name button, a player with his own recording and a row, a question dropped when another player is opened,
the FC 27 master's name, an own-recording player's callname not kept). The kept edits (§4, 1.0.2): "kept edits store:
upsert, forget, save and load back, missing file" (two kits of one type, the colour-channel whitelist), "kept edits
store: a bad file is reported, never overwritten silently; malformed entries are dropped" and "UI: kept edits: kit
colours and player-specific callnames are written again when a newly loaded career connects" (the tables copied back
from the world image as the game's reload; row present, a silent 980001 row taken over with the master list loaded, a
full table with no free row, own recordings from the game and from the master list left alone, a stale index entry, a
removed callname, the hidden window, no added row on a table with room, a club change and leaving the career without a
re-apply, no toast when nothing changed, the kill switch, two kits of one type, a hand-edited key column). Lua:
`turbo/tests/t12_callnames.lua` "the room is checked again right before the insert …" and `t07_bridge.lua` "the
manager changing club bumps db_gen (re-read) but not load_gen (no career load)".

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

The language db's event map is empty in the career hub (count 1, the static empty bucket 0x14BCAD848; read on
2026-10-04 at 01:40 and again at 02:05 after a restart) while the base db's `+0x74` flag is set, so `HasAudio` takes
the language path and answers "no" for every event there: the build of 01:33 got "no audio" for all 4,849 ids and
failed as designed (§5.4, §5.6). The game binds the events on its Create Player screen and in a match; Turbo waits
for that (§3, the watcher).

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

### 5.6 What binds the bank, and why Turbo does not do it from the hub (track E8, 2026-10-04)

What was traced in the image (static) and read live (dev service, read-only), with the conclusion at the end.

*The language db object* (`owner+0x48 -> [y]`, 0x6EF57C30 then 0x6B07EDF0 after the restart; no vtable: +0 and the
allocator slots of its maps point into a private heap region at 0x24Cxxxx): a run-time wrapper around the base
`CommentaryDb` (`+0x388` = the base db EBX object, `+0x380` a sibling, `+0x368` = the holder `y`), with a lock at
`+0x3C8` and a row of EASTL hash maps (`{buckets, count, 1.0f, 2.0f, allocator}` at `+0xC8`, `+0xD8`, `+0x108`,
`+0x138`, …, every one empty in the hub). The map `HasAudio` reads is `+0xD8/+0xE0`, keyed by the event id
(`event+0x30`), node `{+0 id, +0x10 candidate array, +0x18 next}`, found by 0x145ACC224 under the lock. The functions
that fill these maps (the bank's event registration) were not found by scanning the speech module for the offsets:
the inserts are inlined in the loader of the language's speech bank data, which is EBX (no strings to anchor on).

*The frontend side.* The commentary service (vtable 0x14A8C6D70, 1,117 references, 60 callers of
`GetCommentaryService` listed with `scripts/re`): its names object (vtable 0x14A8C5F48) is the interface the screens
use - slot 24 FilterNames (§5.2), slot 11 reads the speech parameter `COMMENTATOR_TEAM` (hash 0x14C6447BC through
`[SpeechSystem+0x58]`, 0x1414AAA64), slot 12 reads the language-list holder's `+0x24`, slots 25 / 27 / 28 forward
4-character codes (`'trck'` 0x7472636B and others) to the `CommentaryBridge` (`vcall(0xE0)(code, x)`, `vcall(0xF0)()`,
`vcall(0xD8)(code)` / `vcall(0x80)()`; `SpeechEventHandler::RefreshAudioTrack` 0x14390197C uses the same path), slot 9
jumps into protected code. The bridge's own slots 27 (+0xD8), 9 (+0x48) and 16 (+0x80) lead into the exe's
**anti-cheat-protected region** (0x157C12F40…: trampolines that compute their targets with xor/add chains), i.e. the
bank / event management behind the bridge is not plain code Turbo can call or audit. The object at `service+0x30`,
which the Create Player screen controller 0x1470DC1D0 calls once (`svc->vcall(0x48)->vcall(0x18)()`) when its
option list is the commentary-name kind (type 0xD), is the FUT anthem / chant preview player (vtable 0x14A8C6458:
`Play_Goal_Anthem`, `Stop_Goal_Anthem`, `triggersfx_FUT_CHANT_PREVIEW_*`): a "stop the preview" call, not the
binding. 0x1480E00F0 (a Create Player data reader) only checks `names->vcall(0x58)() != 3`.

*The launch bank.* `superbundlelayout/commentarylaunch_%s` / `commentarylaunch_%s` are formatted by 0x143901AB0 (per
language entry of 0x70 bytes: install-package state, `superbundlelayout/installpackage_05`, `ita` / `eng,us`, flags
`+0x60..+0x65`, the entry's own vtable slots 5 (loaded?) and 1 (load)) called from 0x1438FCBDC (the audio bridge
set-up next to `MusicBridge`) and 0x143901E90, and by 0x145C7A324 (`is the launch package installed`, with
`STREAMINGINSTALL/LIMITED_MODE`) which 0x145C7A210 wraps for the frontend; the bank descriptor names
(`…/ita_it/ita_it_launch_commentary_brt`) exist only as EBX data (§2).

*Conclusion.* Binding `PLAYER_NAME_FE` means mounting the launch superbundle, streaming the bank and letting the
speech system register the language db's events, driven by the frontend flow through the bridge's protected code and
Frostbite's asynchronous resource loading; the unbind is the same path in reverse. That is neither a cheap nor a
reversible call, it touches anti-cheat-protected code and it allocates tens of MB inside the game: **Turbo does not
bind the bank itself.** It waits until the game has done it (the watcher of §3: a probe every 3 s, the full build when
a probe answers, the result cached) and makes sure an unbound bank is never mistaken for an empty bank
(`BuildResult::unbound`, `no_filter`; `Callnames::apply_capture` refuses a result that is not ok; the native tests
cover both, and the toast is suppressed for the watcher's own "not bound" outcome).

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

`turbo_output\callnames\ids.json` (track E8): `{"turbo_ids": 1, "when", "session", "source", "names": [ids…],
"preview": [ids with commentarypreview = 1…], "players": [player ids…]}` - the list a build asks about, written by
the GUI while the career database is connected (once per session and list size) and read when it is not (the main
menu's Create Player screen), before Lua's `bridge_commentary.txt` (commentary ids only, no players). A file that is
not Turbo's or holds no commentary id is ignored.

## 7. In-game test plan

1. **The call is live.** Career hub, Turbo window > Status tab > Game hooks: every `commentary_*`, `speech_query_*` and
   `scratch_scope_*` signature "found at 0x…" (18 of them; `commentary_filter_names` at 0x1439074C8,
   `commentary_service_registry` at 0x14C2A8590), and under Game calls the line
   `commentary_has_audio: ready | names: FilterNames 0x1439074C8, GetCommentaryService 0x142A52420, registry slot
   0x14C2A8590 | players: ready … | runs 0 | steps 0`. With `turbo_output\call_commentary_audio_off.txt` present the
   line says `off (kill switch …)` and the Rebuild button is disabled; delete the file again.
2. **The hub: the watcher waits (seen 2026-10-04 01:33 with the E4 build: every id "no").** Career hub, no
   `spoken_ita_it.txt`, no `spoken_ita_it.json`. Expected within a few seconds of the GUI start, window shown or not:
   the log has one line `game call commentary_has_audio: probe (64 ids): not bound in this screen` and then nothing
   more (a probe runs every 3 s but is logged only when its outcome changes); the Status tab shows `… runs 0 | probes
   N | steps N` with N growing by one every 3 s, `last probe: not bound in this screen`, and under it `spoken
   callnames (ita_it): spoken set not built yet: open the game's Create Player screen (Customise > Create Player >
   Commentary name) or start a match, Turbo builds it there (last check hh:mm:ss: the bank is not bound in this screen;
   checking again every 3 s) | probes N`; Players > a player > Callname shows the orange "Unverified" line and the
   same watcher line below it. `turbo_output\callnames\ids.json` exists (`"source": "the career database"`, 4,8xx
   names, ~20 preview ids, 2x,xxx players). No `spoken_ita_it.json` appears. *Rebuild from the game's audio service*
   pressed here fails as before ("the game answered 'no audio' for every one of the 4849 commentary ids …", no toast
   for the automatic path, a toast for the manual one) and the watcher keeps probing. The game must not stutter (a
   probe is one FilterNames call over 64 ids, well under a millisecond).
3. **Create Player from the career (or the main menu): the build runs by itself.** Leave the Turbo window hidden (F8),
   open Customise > Create Player and go to the Commentary name list. Expected: within 3 s the log has `probe (64
   ids): bound (… of 64 sample ids have audio)`, `callnames: the game answered the probe: the bank is bound in this
   screen, building the spoken set`, `build started (4849 commentary ids, 21340 players, via hook)` and a few seconds
   later `build ok: N of 4849 commentary ids have audio, 0 of 21340 players have their own recordings (… steps, … s in
   the game); players not checked: the events PLAYER_LOW_SIMPLE / PLAYER_LOW_LINK are not bound in this screen (a
   match binds them)`; `spoken_ita_it.json` is written with `"source": "game audio service"`; the Status tab's
   watcher line is gone, replaced by `spoken callnames (ita_it): the game's audio service (built …)`; back in the hub
   the Callname tab shows "Spoken set from the game's audio service (built hh:mm): N names, 0 player callnames" and
   no more probes run. N for an Italian pack should be in the low thousands (FC 26 PT-BR: 1,703 generic surnames).
   *From the main menu* (no career loaded, database not connected): the same, with the build's status line saying
   `(from the id cache ids.json (written …))` - delete `ids.json` first to see the fallback `(from Lua's
   bridge_commentary.txt (4849 commentary ids, no players))`.
   *If instead* the probe says `no commentary bridge in this screen` (the canary survived) or `failed: …`, note the
   screen in §8; the watcher backs off (6, 12, … 60 s) after errors and goes back to 3 s after a plain "not bound".
3b. **A match fills the players.** With the set built, press *Rebuild from the game's audio service* during a match
   (pause, open Turbo). Expected: `build ok: N of 4849 commentary ids have audio, M of 21340 players have their own
   recordings`, M in the hundreds; the cache's `players` is filled and step 6 works. (The watcher does not re-run by
   itself once the set is verified: the names are the same, only the players part needs a match.)
4b. **The next session.** Restart the game into the hub: the Callname tab shows the cached set at once, the Status tab
   shows no watcher line and `probes 0`.
3. **Cross-check against the game.** Create Player > Commentary name > letter A: every name listed there appears in
   the Callname tab's *By name* picker (type "a"), and a name the picker does not list is not in the game's list
   either (the same check, batch-wise).
4. **A spoken surname in a match.** Pick a name the picker lists for a Napoli starter with *Assign as last name* +
   *Keep the shown name*; play a short match (2-minute halves). Expected: the commentator says the chosen surname, the
   name on screen is unchanged.
4c. **Proof from the game's own requests (diagnostic, opt-in).** Create an empty
   `turbo_output\commentary_speech_log_on.txt` before starting the game. Turbo then hooks the game's
   `SpeechQuery::SetInt` (0x1407B03E4, the same helper §5.4 calls) and counts every `surname_ID` and `player_db_pID`
   the commentary asks about, Turbo's own spoken-set checks excluded. Every 5 s the counts go to
   `turbo_output\commentary_speech_log.txt`. After a match, the id assigned in the Callname tab appears there when the
   commentary looked it up for that player. Delete the file again afterwards (`hook_commentary_speech_log_off.txt`
   turns the hook off at run time). Record the game's output to hear it: Windows loopback capture of the default
   playback device.
4d. **A full playernamemap.** FC 27's `playernamemap` holds 106 of 106 rows (header +0x78 capacity, +0x7C rows in use).
   *Use this player's callname* on a player without a row names, before the click, the row it takes over: a row whose
   player is not in the database, whose callname is none, or whose callname has no recording in ita_it (with the
   Italian master list loaded, a 980xxx row; without a list a 980xxx row cannot be checked and is left alone). It never
   asks Live Editor to add a row to a full table (that crashes the game).
4e. **Kept across a career load (1.0.2).** After 4d, save, go back to the main menu and load the career again. Expected:
   `turbo_gui.log` has `re-apply at career load: re-applied 0 kit colours, 1 player callname` (plus any kit colours),
   the player's Callname tab shows the callname again with "Kept for every career load", and the speech log of 4c shows
   the id asked for him in the next match. *Forget*, save, reload: the line says nothing about him and the base
   callname is back. Change club through Job offers: no `re-apply at career load` line (not a career load).
5. **An unlisted id stays silent.** Write `turbo\callnames\spoken_ita_it.txt` with one id the picker does **not** list
   (e.g. one that commentarynames has but the build dropped), press Refresh (the tab shows "hand-made list"; the
   players with recordings still come from the game-built cache), assign it, play a match: expected silence for that
   player. Delete the file, Refresh: the game-built set is back.
6. **A player with his own recordings.** Open a famous player whose cached `players` value is non-zero: the green
   "Current callname: his own recording in ita_it (the game's audio service)" line (its tooltip names PLAYER_LOW_SIMPLE /
   PLAYER_LOW_LINK); in a match his own name is said whatever his name ids say. With the master list in place (§9):
   open Lobotka (216435) - "… (your FC 26 list; not confirmed by the game's audio service …)" or "(the FC 27 master)",
   the rule's result greyed below it, and *Assign as last name* asks first. Miguel Gutierrez (261865) is the check for
   an FC 27 master: the game said his own recording on 2026-10-04 although the audio-service set lacks him.
7. **The diagnostic scan.** *Capture from the loaded bank* still runs on a background thread and reports "no selection
   table found (… runs rejected …)" without touching the cache.

## 8. Open points

* **Which screens bind the bank.** The hub does not (§5.1, §5.6: read twice on 2026-10-04, and the 01:33 build got "no"
  for every id). The watcher (§3) now waits for a screen that does; which screens answer - Create Player from the
  career, from the main menu, a match, the pause menu - is what §7 steps 3 / 3b settle, and whether the launch bank's
  `PLAYER_NAME_FE` set equals the full bank's (step 4). The build has not yet been seen answering in-game with this
  code: the E8 DLL was not deployed during the track.
* **Binding from the hub.** Not done: the path runs through the bridge's anti-cheat-protected code and Frostbite's
  streaming (§5.6). If a later track finds a plain "preload FE commentary" entry point (a candidate: the names object's
  slot 9, +0x48, which jumps into the protected region), it should be a hooked observation first, never a call.
* **The game's own id list.** The build asks the ids the database knows (or the cache / Lua's list, §6.4). Calling the
  game's `GetCommentaryNameIds` reader (0x1480B2128, 26 letters) instead would also work from the main menu but goes
  through the DB service and `FETemp` tables: not needed while `ids.json` or `bridge_commentary.txt` exist.
* **Launch bank vs full bank.** `PLAYER_NAME_FE` is the frontend preview event (the launch bank, 26 MB); the match
  speaks `PLAYER_NAME` / `PLAYER_LOW_*` from the full bank. The Create Player list is the game's own promise that a
  name has recordings; step 4 of §7 checks that promise on one name.
* **Cost.** One HasAudio is a linear scan over 10,201 event names (§5.1); the batch budget keeps a frame under ~4 ms
  of extra work, so ~25,000 queries take a few seconds. If the game still hitches, lower `BuildRequest::batch_max`.
* **Title updates.** Every pattern is unique on build 6AB9813C-211EF000; `scripts/re/verify_callnames.py` re-checks
  them on a new image, and a build that is not in the signature table turns the call off (the tab falls back).
* The memory scan's row signature (§6.1) still matches the sample index only; it is kept as a diagnostic.

## 9. Players with their own recording: the master list per language (Turbo 1.0.2)

**Why.** Step 0 of the game's rule (§1, §5.3): a player with his own recording (bound to his player id in the bank,
`PLAYER_LOW_SIMPLE` / `PLAYER_LOW_LINK` with `player_db_pID`) is said from it, whatever `playernamemap` or his name ids
give. Turbo learned who has one only from the game's audio service, which answers the player events during a match
only (§3, §5.4). On 2026-10-04 (FC 27 1.0.140.64835, `ita_it`) that set held 751 players, while the user's own FC 26
research lists 4,127 players with a 'real' (player-bound) Italian recording, 2,820 of whom are in FC 27's database and
2,136 of those missing from Turbo's set - Stanislav Lobotka (216435) and Amir Rrahmani (244263) among them (both 'real'
rows of `italy_master.xlsm`, checked). So the tab said "Current callname: none" for Lobotka and let a generic surname
(Totti, 1.0.1's in-game check) be written that the game never says.

**The lists.** `C:\FC_Tools\My Mods\<folder>\<name>_master.xlsm`, sheet `callnames`, header row `SegmentID,
VariationId, playerid, name, type, Play Audio, …` (the later columns differ per file; Turbo's tool finds `playerid`,
`name` and `type` by header name). `type` is `real` (the `playerid` column is the player's id) or `generic` (the column
holds the commentary id 9xxxxx of a generic surname). They describe FC 26; FC 27 mostly reuses the recordings, so the
GUI shows them as "your FC 26 list".

**One list per language, FC 27 first.** The concept is generic: one master list per commentary language, in the same
workbook format. An FC 27 master built from the game's own data is named `<name>_master_fc27.xlsm` (or `.xlsx`; e.g.
`italy_master_fc27.xlsm`) and lives under `<Live Editor>\turbo_dev\masters` (or next to the FC 26 workbook). When a
language has both, the tool takes the FC 27 master and writes `"game": "fc27"` into `masters\<lang>.json`; the FC 26
list gets `"game": "fc26"` (a file without the field, as the first 1.0.2 tool wrote, counts as FC 26). The GUI names
the source accordingly: "FC 27 master" / "the FC 27 master" or "Your FC 26 list" / "your FC 26 list"
(`MasterList::label`). Only for an FC 26 list does the tab add that the game's audio service did not confirm a player
(`Callnames::own_unconfirmed`: own == list only, FC 26 list, the audio service asked about players).

| folder | workbook | FC 27 language | real players (rows) | generic ids (rows) | real ids over 999,999 / generic ids outside 900001..965000 |
|---|---|---|---|---|---|
| `br` | `br_master.xlsm` | `por_br` | 3,532 (6,760) | 1,703 (1,703) | 9 / 0 |
| `eng` | `uk_master.xlsm` | `eng_us` | 12,111 (21,020) | 4,866 (7,129) | 0 / 17 |
| `fra` | `france_master.xlsm` | `fre_fr` | 3,426 (5,780) | 2,157 (2,159) | 9 / 0 |
| `ger` | `ger_master.xlsm` | `ger_de` | 3,555 (5,793) | 3,439 (3,444) | 0 / 0 |
| `ita` | `italy_master.xlsm` | `ita_it` | 4,127 (6,976) | 1,202 (2,114) | 9 / 22 |
| `ned` | `dutch_master.xlsm` (the older `.xlsx` copy skipped) | `dut_nl` | 3,591 (6,051) | 1,509 (2,165) | 9 / 15 |
| `spa` | `spain_master.xlsm` | `spa_es` | 4,510 (6,684) | 1,534 (1,534) | 8 / 10 |

(Import run of 2026-10-04 into a temporary folder; no row had a non-numeric id. The codes are the game's pack names,
`commentaryfull_<code>` / `commentarywc_<code>.toc` in `Data\Win32`. Ids over 999,999 - e.g. 999999143 'Aade' - are
bank oddities that match no player; generic ids outside the commentary range are kept as listed.)

**The tool.** `python turbo/tools/import_callname_masters.py [--root DIR …] [--le "C:\FC 27 Live Editor"] [--out DIR]
[--lang ita_it …] [--dry-run] [--self-test]` (Python 3.11 + openpyxl; the workbooks are opened read-only). `--root` is
repeatable; by default it reads `C:\FC_Tools\My Mods` and `<le>\turbo_dev\masters` (each when present). It maps each
workbook to its language by folder (`br`, `eng`, `fra`, `ger`, `ita`, `ned`, `spa`), else by the workbook's name
without `_master` / `_master_fc27`, prefers an FC 27 master, then `.xlsm` over `.xlsx`, then the newest, reports every
workbook it skipped, skips blank rows, rows of
another type and rows whose id is not a positive whole number (counted), keeps the most frequent spelling as a
player's name, and writes `<le>\turbo\callnames\masters\<lang>.json` atomically:

```
{"turbo_masters": 1, "language": "ita_it", "game": "fc26", "source": "C:\\FC_Tools\\My Mods\\ita\\italy_master.xlsm",
 "built": "2026-10-04T12:07:12-04:00", "note": "...", "counts": {"rows": 9090, "real_players": 4127, ...},
 "real_players": [27, ...], "generic_ids": [900002, ...], "names": {"216435": "Stanislav Lobotka", ...}}
```

One unreadable workbook (no `callnames` sheet, missing columns) is reported and skipped; the others are written.
`--self-test` builds small workbooks in a temporary folder and checks the mapping, the duplicate, the skipped rows,
the JSON and an FC 27 master winning over the FC 26 list (in another folder and in the same folder).

**What Turbo does with it** (`core/callnames.{h,cpp}`): `Callnames::refresh` reads `masters\<lang>.json` for the
loaded language into `Callnames::masters` (`parse_master_list_json`: never throws, skips entries of the wrong type,
refuses a file that is not JSON, holds no id or names another language - the tab shows the reason in orange; a missing
file is not an error). `Callnames::own_recording(playerid)` is a bit mask: `kOwnFromGame` (the audio service's or the
bank capture's `players`) | `kOwnFromMasters` (the list's `real_players`); **either one counts**, and
`own_recording_source_name` names them ("the game's audio service", "your FC 26 list" / "the FC 27 master", "the
game's audio service and your FC 26 list"; the game's side is named by where the players came from,
`SpokenSet::players_from`, also under a hand-made spoken list). `resolve()` sets `CallnameInfo::own` and `real` (= own
!= 0); the callname rule is still computed and shown as not used. The list's generic ids decide whether a
`playernamemap` row above 965000 is spoken (`spoken_answer`, the full-table takeover of §4). The re-apply at career load
never writes, and the tab never keeps, a player-specific callname for a player with his own recording (§4). Turbo never
writes the list.

**FC 27 master (2026-10-04).** A master built from the game's commentary files (`turbo_dev\masters\build_master_v0.py`,
`"game": "fc27"`) also holds `real_simple_players` (PLAYER_NAMES_SIMPLE), `real_link_players` (PLAYER_NAMES_LINK),
`generic_ids` (commentary ids with a generic surname recording) and `generic_names` (commentary id -> its text);
`parse_master_list_json` reads them all (a file without `real_players` gets the union of the two banks). Precedence for
"has his own recording": **the FC 27 master decides alone** (`own_recording` returns only `kOwnFromMasters` or 0: the
audio service's set is LINK only and known to be wrong); without one, the audio service's set and the FC 26 list,
either one enough, as above. The master's generic ids are spoken (`spoken_answer`), in union with the spoken set of §3;
the tab's source line reads "FC 27 master: N players with their own recording (decides alone); spoken surnames = its M
generic ids + the game's audio service set". Verified in game on 2026-10-04: a generic surname set as a player's last
name (keeping the shown name through `editedplayernames`) is spoken (Bianchi 900762, Pirlo 926385, Del Piero 922149);
a player with his own recording (Gutierrez 261865) keeps it whatever his name ids say.

**All callnames (picker tab, `all_callnames`).** One type-ahead list (`##cnallsearch`, `##cnall`) of every callname the
master lists: each generic id (commentary id, its `generic_names` text, the `playernames` rows with that commentary id
and the players using them) sorted by text, then each player with his own recording (named by the database, else by
the master). 1.0.3 (§11): a generic id goes through the player-specific path when it can be used
(`request_generic_callname` -> `request_player_callname` -> `assign_player_callname`: edit in place, room check,
spare-row takeover, never an insert into a full table; kept for the re-apply at career load), else through the name row
most players use (`request_name`: last name, shown name kept, same code path as By name), else nothing is written and
the route line says why. An own recording of another player is shown, not assignable ("The game always uses a
player's own recording; giving it to another player is not possible yet."). The own-recording popup (`##cnown`) still
comes before any write for a player with his own recording.

**Keep shown name queue.** Turbo's Lua mailbox holds one command at a time; 1.0.2 as first built refused the second
"keep shown name" (`set_display_name`) of players assigned in a row ("a command is still running"). The GUI now queues
these actions (`LuaActionQueue`, `App::lua_queue`) and sends them, batched into one `callnames` command
(`LuaActionQueue::batch`, within the mailbox's text size), as soon as the mailbox is free (`App::flush_lua_queue`, every
tick and right after a result); the top bar shows "N 'keep shown name' waiting".

**In the tab** (§4): the language block's "Your FC 26 list: N players with their own recording, M generic names"
(or "FC 27 master: …") line (tooltip: file, workbook, date, FC 26 or FC 27 data), or "No master list for ita_it …"
with the path looked for, or "Master list not used: …" with the reason; the current callname
"his own recording in ita_it (your FC 26 list)" with the rule's result greyed below; the one-line warning above the
assignment buttons and the confirmation popup (`##cnown`) before any write for such a player; the third picker tab
*Players without own recording* for the player's club. Without a list, a player whose rule gives nothing reads
"Current callname: none from the callname rule (no master list for ita_it: an own recording of his would not be known
here)". The picker lists now take the height the tab has left (80..200 px), so the assignment buttons stay visible in
the default window with the extra lines; the reason for the detected language moved into a "(?)" tooltip unless it
needs attention.

**Limits.** An FC 26 list is FC 26 data: a recording FC 27 dropped is still flagged (the write stays possible after
the confirmation), and a player FC 27 recorded anew (Miguel Gutierrez 261865, heard in game on 2026-10-04) is only
known once an FC 27 master lists him. The audio-service build of 2026-10-04 13:39 during a match returned the same 751
players as the Create Player build, so its player query is incomplete, not only its timing. The generic ids decide only
about ids above 965000; the spoken set of §3 decides 900001..965000.

**Install.** The tool is not part of the package zip: the operator runs `python turbo\tools\import_callname_masters.py`
once (defaults: the workbooks under `C:\FC_Tools\My Mods` and `C:\FC 27 Live Editor\turbo_dev\masters`, the JSON
files into `C:\FC 27 Live Editor\turbo\callnames\masters`), and again after an FC 27 master is added; then *Refresh*
in the Callname tab loads the list for the loaded language.

## 10. FC 27 master

**How it is made.** Two steps, both offline (nothing touches the running game):

1. `turbo/tools/fc27_commentary` extracts the commentary bank of one language from the game's commentary files on disk
   into `<lang>_bank.json`: every recording bound to a player id (`real`: `PLAYER_NAMES_SIMPLE`; `real_link`:
   `PLAYER_NAMES_LINK`) and every generic surname recording (`generic`, by commentary id).
2. `python turbo/tools/build_callname_master.py --bank <lang>_bank.json --players turbo_table_players.csv
   --edited turbo_table_editedplayernames.csv --names bridge_names.txt --commentary bridge_commentary.txt
   --template <FC 26 *_master.xlsm> --out-xlsm <name>_master_fc27.xlsm --out-json <lang>.json [--lang <lang>]`
   (Python 3.11 + openpyxl; the CSVs are Turbo's *Export tables* of the career database, the two text files the
   name id / commentary id texts from the bridge). It copies the user's FC 26 workbook (never written), replaces the
   `callnames` rows (one per recording, sorted accent-insensitively by name like the user's sheet; FC 27 names first,
   the FC 26 sheet's name and category as fallback, the category of an unknown player guessed from his nationality)
   and the `names` sheet, keeps styles, widths and the VBA project byte for byte, and writes the Turbo masters JSON
   (`"game": "fc27"`, `real_players` = SIMPLE | LINK, plus `real_simple_players`, `real_link_players`, `generic_ids`,
   `names`, `generic_names`). `--self-test` builds a tiny template with a dummy `vbaProject.bin` and checks the rows,
   the sort order, the copied styles, the VBA bytes and the JSON keys.

   Since 1.0.3 the tool reproduces `turbo_dev\masters\build_master_v0.py` exactly (checked on `ita_it`: same
   `callnames` and `names` cells, same patched `vbaProject.bin`, same JSON apart from the new keys): column H `nameid`
   (generic rows: the `playernames` id(s) whose text is the commentary name, exact text first, else case-insensitive,
   several joined with `,`; Italian: 3,424 rows found, 19 without a name row), `--vba-from` / `--vba-to` (a
   same-length text patch of the VBA project with `turbo/tools/vba_tool.py`, a copy of the masters folder's tool:
   module source, p-code and `__SRP_` caches) and `--copy-to` (v0 delivers the workbook next to the FC 27 audio). New
   JSON keys for Turbo's play buttons: `wav_dir` (`--wav-dir`, default the bank's own `wav_dir`; every row's wav is
   checked there) and `segments` = `{"generic": {"<commentaryid>": [seg, ...]}, "real": {"<playerid>": [seg, ...]}}`
   (`PLAYER_NAMES_SIMPLE`), plus `"real_link"` only for `PLAYER_NAMES_LINK` segments whose wav exists (none yet: the
   extraction wrote SIMPLE and generic wavs only). Italian, as v0 built it:

   ```
   python turbo/tools/build_callname_master.py --bank raw\ita_it_bank.json --players raw\turbo_table_players.csv
     --edited raw\turbo_table_editedplayernames.csv --names raw\bridge_names.txt --commentary raw\bridge_commentary.txt
     --template "C:\FC_Tools\My Mods\ita\italy_master.xlsm" --out-xlsm ita\italy_master_fc27.xlsm --out-json ita_it.json
     --wav-dir "C:\FC_Tools\My Mods\i27" --vba-from "C:\FC_Tools\My Mods\ita\" --vba-to "C:\FC_Tools\My Mods\i27\"
     [--copy-to "C:\FC_Tools\My Mods\i27"]
   ```

The user's *Play* macro hard-codes `C:\FC_Tools\My Mods\<lang folder>\` as the audio base (the FC 26 recordings); the
FC 27 workbook is shipped with its own `real\` and `generic\` folders of FC 27 recordings next to it, and its macro is
patched to point there (`i27`, the same length as `ita`, so the compound file keeps its stream sizes).

**Hear a callname in Turbo (1.0.3).** A small play button (a triangle; a square while it plays) sits on the "Current
callname" line (his own recording, else the rule's callname) and on every row of By name (the name's callname), By
player (the callname that would be copied) and All callnames (generic rows and own recordings). It plays
`<wav_dir>\generic\pSIMPLE_SURNAME_<seg>_<seg>.wav` or `<wav_dir>\real\pPLAYER_NAMES_SIMPLE_<seg>_<seg>.wav` (then
`pPLAYER_NAMES_LINK_...` when the master lists LINK segments); every click plays the id's next segment (its
variations in turn), a click while it plays stops it. Disabled, with the reason in its tooltip, without a wav folder
in the master, without a segment for the id, or when no wav of the id is in the folder. Playback is Windows'
`PlaySoundW` (`SND_ASYNC | SND_FILENAME | SND_NODEFAULT`) from `winmm.dll`, loaded at run time and called on a worker
thread (`src/win/callname_audio_win.cpp`), behind `WavPlayer` (`core/callname_audio.h`; native tests fake it). File
checks are cached per wav folder; a wav's length is read from its header to know when it ends. Nothing touches the
game. Needs the master JSON with the new keys in `<Live Editor>\turbo\callnames\masters\<lang>.json`, then *Refresh*.

**`ita_it` (FC 27 1.0.140.64835, built 2026-10-04):** 10,321 rows, 6,878 real / 3,443 generic; 4,046 players with
their own recording (4,039 SIMPLE, 985 LINK; 3,038 of the SIMPLE ones in the career database), 2,533 generic ids.
Verified in game the same day: a generic surname set as a player's last name (shown name kept through
`editedplayernames`) is spoken (Bianchi 900762, Pirlo 926385, Del Piero 922149), and a player with his own recording
(Miguel Gutierrez 261865) keeps it whatever his name ids say. The audio service's player set (751 players, LINK only,
§9) is incomplete; the FC 27 master is the reference. `import_callname_masters.py` (§9) takes it over the FC 26 list.

## 11. Assigning a callname keeps the shown and printed names (Turbo 1.0.3)

**Why.** Seen in game on 2026-10-04 (1.0.2): when Turbo writes a last name id, the game *shows* the new surname
(squad screens, match HUD: Vergara appeared as "Del Piero") until the career is reloaded; only then does the
`editedplayernames` row (added by Turbo's Lua at the next career event) bring the original name back. Turbo's rows also
left `playerjerseyname` empty, while the game's own rows fill it.

**Route choice** (`ui_callnames.cpp` `plan_generic_route`, By name and All callnames). A generic callname is written as
the player's **player-specific callname** whenever that is possible, because it changes no name: his `playernamemap`
row in place, else a row added by Lua when the table has room (with the room check of §4), else a spare row
(`Callnames::spare_playernamemap_row`, the full-table takeover of §4); it is kept in `reapply_edits.json` and written
again at every career load (1.0.2). The game says a player-specific callname only when it has audio (else it falls back
to the name ids), so this route is taken only when the callname has a known recording: the verified spoken set (§3)
or the master's generic ids (§9, §10); the unverified fallback set does not count. Otherwise the **name route**
(`assign_name`, *Assign as last name* / *common name*) is used, and its toast says "The game shows the new name until
the career is reloaded." When neither is possible (All callnames: no free row and no name row with that id) nothing is
written. One line above the button names the route before the click ("Route: player-specific callname (a spare
playernamemap row). No name changes." / "Route: name id (no playernamemap row free). The game shows the new name until
the career is reloaded."; its tooltip names a taken-over row); the tests read it as `CallnameTabState::route_line`.
A player with his own recording still gets the confirmation popup first (§9); his player-specific callname is written
for the session and not kept, as in §4.

**Kept-name rows** (the name route with *Keep the shown name*):

* `playerjerseyname` is always filled: the player's current shirt name = his row's `playerjerseyname`, else the text
  of `players.playerjerseynameid`, else his shown surname (`ShownName::jersey`). Lua's `set_display_name` fills it too
  when the action gives none or gives "" (the row's own, else the surname kept, else the row's surname, else the common
  name; an existing shirt name is never cleared).
* **Order.** The row is written before the name id. A player with a row: `firstname`, `surname`, `commonname`,
  `playerjerseyname` are edited in place, then `lastnameid` / `commonnameid`; a failed row write stops the name id.
  A player without a row: one Lua command `[set_display_name (room, capacity, load_gen), set_name_ids]`, queued as one
  entry (`LuaActionQueue::push_group`, never split across two commands); Lua runs the actions in order and a refused
  row (table full by then, a career loaded since) stops the name id. Nothing is written natively in that case.
* A full `editedplayernames` table writes nothing (1.0.2 wrote the name id and lost the shown name); untick *Keep the
  shown name* to write the name id anyway.

Tests: `turbo/tests/t12_callnames.lua` "1.0.3: a kept-name row never leaves playerjerseyname empty ..." and
"1.0.3: [set_display_name, set_name_ids] in one command: the kept-name row first, a refused row stops the name id";
`turbogui/tests/native/test_main.cpp` the queue's group in "callnames: your FC 26 list (masters json) ...", and in "UI:
Players > Callname: language, current callname, pickers, name and player assignment" the player-specific route on a
table with room (no name id written), the name route on a full table (one command, row first, shirt name, the toast),
the in-place edit (shirt name filled from `playerjerseynameid`, row written before the name id), a full
`editedplayernames` table (nothing written), a spare row (player-specific again) and the unverified set (name route);
"UI: Players > Callname: own recordings ..." (the name route's single command after *Assign anyway*, the
player-specific route without a popup for a player without his own recording, All callnames: player-specific first,
the name row on a full table). Not yet checked in game.

## 12. Voice swaps (Turbo 1.1.0, draft)

**What.** In matches only, a player (B) is called with another player's own recording (A: "Use his voice"), or his own
recording is turned off and a generic callname is used instead ("Use in matches"). Nothing is written to the database
or the save: his name on screen, his face and his stats stay his, and none of the side effects of §11 (a new surname on
screen until a reload, the full `playernamemap`, the re-apply at career load) apply. Plan and reverse engineering:
`turbo_dev/research/real_callnames_plan.md` §4-7, [`docs/re/inmatch-callnames.md`](re/inmatch-callnames.md); the
contract between the parts is `turbogui/src/core/callname_voice.h`.

**How it works (short).** Two guarded game hooks read a table Turbo publishes: the commentary's pre-handler rewrites
B's id to A's (or to 0 = own recording off) in every player-id parameter of a line, and the kick-off callname of B is
replaced (-1 = his surname lines silent, the default for a swap, or the generic callname's id). The host part
(`win/callname_voice_win.cpp`, kill switch `turbo_output\callname_voice_off.txt`) is described with its build.

**Store.** `turbo_output\callnames\voice_swaps.json`, one entry per player for every career (player ids are the same
in all careers). Loaded when Turbo starts (no career needed) and published to the hooks at once and after every edit;
written atomically. A file that is not a voice-swap file is set aside as `voice_swaps.json.bad-<date>` and Turbo starts
empty; malformed entries are dropped. The note shows on the Status tab and in the Voice swaps tab. A file that cannot
be read is never overwritten (that session's swaps are not saved).

**Players > Callname** (`ui_callnames.cpp`):

* **Current callname** line, when the player has a swap: "In matches: Lobotka's own recording (voice swap)" or "In
  matches: Del Piero (generic), own recording off", with the game's own rule greyed below ("Not used while the swap is
  on: ..."). Its play button plays what he is called: A's own recording, else the generic callname.
* **Use his own recording** (a checkbox, only for a player with his own recording): untick it to turn the recording
  off in matches (`voice_of: 0`; his surname lines keep the callname rule); tick it to forget his entry.
* **All callnames**: an own-recording row gets **Use his voice**, a generic row **Use in matches**. Both ask first:
  "Marianucci will be called Lobotka in matches. His name on screen stays Marianucci." and write only the store. For
  a player with his own recording on, Use in matches is greyed with "Turn off his own recording first"; once it is off
  the entry becomes `voice_of: 0` + `kickoff: <id>`. The older database buttons follow under "Change the name in the
  database".
* **Voice swaps** (a fifth picker tab: the Callname tab has no height for a section below the pickers): Player |
  Called | Other lines | Remove. *Other lines* are his surname lines (silent, a generic callname, or the callname
  rule). *Name lines only* (per swap) keeps his other lines in his own voice. *Forget all* asks first. A player not in
  the loaded career is marked "not in this career"; his entry stays.
* **No recording**: when the FC 27 master (or the verified spoken set, for a generic id) says the source has no
  recording in the loaded language, the line says "Lobotka has no recording in ita_it: silent".
* **Off**: when the host's hooks are not installed (another game build, a kill switch, game hooks off) the tab says
  "Voice swaps are off: <why>" and shows only the database buttons; the entries are kept.

**Status tab.** "Voice swaps: on | 3 swaps | lines changed 57 | kick-off set 2", or "Voice swaps: off (<why>)".

Tests: `turbogui/tests/native/test_main.cpp` "UI: Players > Callname: voice swaps: ..." (a fake service: off without
one, published once it is there, Use his voice with its confirmation, the current line, Name lines only, the status
counters, a silent source, a player of another career, own recording off and Use in matches, the service off, Remove,
Forget all; never a database write), plus the core cases "voice swaps: ...". Not yet checked in game (plan §7).
