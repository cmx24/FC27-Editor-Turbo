# Created players in real time (FC 27): why raw rows are invisible, and how the game creates players

Source: `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (FC27.exe 1.0.140.64835, base `0x140000000`, build key `6AB9813C-211EF000`),
analysed 2026-10-05, **static only** (no game process, no dev service, no Live Editor binary). Tooling: the `scripts/re` helpers
(`rx_jobs`, `sig_jobs`, `ripscan.c`) plus `.pdata` function bounds. Confidence: `[H]` read in the code in this session, `[M]` strong
inference, `[L]` guess. Signatures: `scripts/re/created_players_signatures.json` (19 entries, each proven unique in the image).
Companion: `realtime_transfers.md` (events, `PlayerMoved`, the `player_move` call, section 2.5 / plan E).

## 0. Summary

* **Why a row added with Live Editor's `InsertDBTableRow` is invisible to the game.** The career database is EA's T3db with an SQL
  layer (CDBGSQL). Every T3db table keeps a linked list of **indexes** (`Table+0x18`); the persistent ones are built **from every record
  when the table is loaded** (the table's key definitions, `0x1406A655C` -> `CreateIndex 0x1406ABAD0`), and the engine keeps them in step
  only through its own write paths: `AddRecord 0x1406A7990` inserts the new record into every index of the table (`IndexInsert 0x1406AAF60`),
  the DELETE path removes it from every index, the UPDATE paths remove + re-insert it `[H]`. A `DataController` query (`IsPlayerInTeam`:
  `SELECT playerid FROM teamplayerlinks WHERE teamid = X`) is answered **through an index** whenever one matches the WHERE column: the
  result opener `0x1406A9B80` -> `0x1406AA8E0` takes the best existing index, or builds a temporary one **by iterating that index** (not
  the table) `[H]`. A record written into the table's memory without `AddRecord` is in **no index**, so every index-served query misses it
  until the career is saved and loaded (the load rebuilds every index from all records). The observed `IsPlayerInTeam(pid, 111592) == false`
  right after the raw insert means `teamplayerlinks` has a persistent key whose first column is `teamid` `[M]` (the key list lives in
  the database file, not in the executable; with no such index the query would have scanned the table and found the row).
  Editing a non-key field of an existing row shows at once because indexes store record numbers and the value is read from the record
  `[H]`; editing a **key** field raw (e.g. `teamplayerlinks.teamid`) leaves the record filed under its old key: it is then found by
  neither team's query until a reload `[H mechanism]` (consistent with the "lost player" bug fixed by the native `player_move`).
* Raw rows also miss the **insert observers**: `AddRecord` calls the table's callback slots `Table+0x58..+0x70` (installed by
  `0x145DF6A78`; `PlayerSearchManager` registers `Player_Data_Cache_Insert_Player` on `players` through `0x145DF47EC`) and refreshes open
  materialized results (global list `0x14C33FD60`) `[H]`. Live Editor's insert also skips the free list / capacity check of `AddRecord`
  (error `0x13` "table full" instead of the crash Turbo guards against) `[H for AddRecord; L for what Live Editor does]`.
* **There is no index-rebuild call for one table or one row that is safe to use from outside**: the only per-row entry is
  `IndexInsert(index, record)` (what `AddRecord` itself does after writing the record), which would have to be called for every index of the
  table under the engine's global lock (`0x14D24A060`) and would still skip the observers. `CreateIndex` rebuilds one index but fails when the
  id exists (`0xC`) and dropping persistent indexes is not something the game ever does at run time `[H]`. So "B: raw rows + rebuild" is
  possible in principle but not safe.
* **Safest design (C, recommended): insert the rows through the game's own SQL INSERT**, exactly the way the `DataController` does for
  `players`, `teamplayerlinks` and `editedplayernames`: the game's query builder (`Query::Init / SetInt / SetString / ~Query`) and the
  `DataController`'s db provider (`provider->vf[1]` = `Execute`). This runs `AddRecord` (indexes, observers, callbacks, free list, capacity),
  needs **no game struct layout** (column names are the database's), accepts every column of Turbo's preset row and is what the game
  itself does in `CreatePlayer`, `InsertTeamPlayer` and `SetEditedPlayerName`. Then replicate the game's own "create a player into a club"
  routine `0x147E0BC28`: post event `0x3A` exactly as `CreatePlayer` builds it, `InsertTeamPlayer(pid, FreeAgents, 99, 29, 0)` (event `0x5F`),
  `PlayerMoved(pid, FreeAgents, club)` (Turbo's existing op 11 path, with its contract record for the user's club).
* **Status (2026-10-05, Unreleased): implemented as the native op 12 `player_create`, OFF by default, not yet run in game.** Section 6
  is the plan, section 6.1 what was built (and where it differs from the plan), section 7 what must be measured live before the
  opt-in is shipped on.

## 1. How `DataController` queries are executed `[H]`

```
DataController dc = **(hub+0x418)      dc+0 = db provider (set by 0x147B97F48: commonData->vf[5]()), dc+8 = {commonData}, dc+0x10 = hub
Query q (0x60 bytes, on the caller's stack)
  Query::Init(&q, type, "table")             0x14197D5F4   type 1 SELECT, 2 DELETE, 3 UPDATE, 4 INSERT; +0 type, +8 eastl string table,
                                                           +0x28 select list, +0x30 where list, +0x38 set list, +0x40 order list,
                                                           +0x50 limit (-1), +0x58 node chunks (10 nodes of 0x140 bytes per 0xC90 block)
  Query::Select(&q, "col")                   0x140601F14
  Query::Where(&q, "col", op 0 (=), int)     0x140602D28   conditions of the where list are AND-joined (token 0 between them)
  Query::SetInt(&q, "col", int)              0x140601EA8   node: +8 column (<= 47 chars), +0x38 op 6, +0x39 kind 0, +0x3A value text "%d"
  Query::SetString(&q, "col", const char*)   0x14403F2E8   kind 2, value text = the last 256 chars (0x1440486C4)
  Query::SetFloat(&q, "col", float xmm2)     0x14403F3A8   kind 1 ("%f");  SetDate(&q, "col", Date*) 0x14403F348: kind 3 (yyyymmdd)
  provider->vf[1](provider, &q, &holder)     0x142297D8C   (vtable 0x14B075540; that this is the DataController's provider class is [M]:
                                                           it is obtained at run time) dispatch on q.type under the provider's RW lock (+0x10):
        1 -> 0x14229827C "FCEI_Select"   2 -> 0x142297EB8 "FCEI_Delete"   3 -> 0x142298054 "FCEI_Update"   4 -> 0x1482942E8 "FCEI_Insert"
  count = holder->vf[1]()                    (thunk 0x14154C5B8)  rows returned (SELECT) / rows affected (INSERT: the game checks == 1)
  value = holder->vf[2](row, "col")          (as IsPlayerInTeam reads "playerid")
  ResultHolder::Release(&holder)             0x1422985A4
  Query::~Query(&q)                          0x14154EB38
```

Each executor gets the DB service from the service registry (`0x14BE28208`, id `0xAE932D0`, `DBService::Database`, 0x98 bytes, ctor
`0x1473FBCB4`), creates a statement (service `vf[5]`, a pooled `CDBGSQL::QueryPimpl` of 0x7800 bytes) and a token builder (`vf[7]`,
`SQLTokenBuilder`), emits tokens (INSERT: `INSERT INTO table ( cols ) VALUES ( values )`, each value by its node kind: int ->
`strtol`, float, string, date) and runs it (`statement vf[1]` `0x1473FC8D0` -> `0x140B1E9B0` -> `0x1406AEFA0`: dispatch on the first token;
SELECT `0x1406AD3DC`, INSERT `0x1406A6650` (VALUES and INSERT ... SELECT), three more statement kinds not traced). The values are
tokens, not SQL text: no quoting problem for names `[M]`.
The INSERT executor writes `value - column minimum` into the bit-packed record, truncates strings to the column size, and fills an
auto key column (`teamplayerlinks.artificialkey`) when the statement does not name it (`0x1406A6BB8` / `0x140B1D6B8`) `[M]`; int tokens are
stored as given for every int-coded column, dates included (`0x1406AED2C`: token kinds 5 and 7 carry an int), so Lua's raw
`birthdate` / `playerjointeamdate` values go through `SetInt` unchanged `[H]`.

## 2. T3db tables, indexes and the paths that keep them right `[H]`

Table header (Turbo's `t3db.h` names in brackets): `+0x08` next table, `+0x18` **index list**, `+0x30` records, `+0x40` shortname,
`+0x44` record size, `+0x48` bit offset of the deleted flag, `+0x4C` flags (1 busy, 4 modified, 8 lazy allocation), `+0x54` callback count,
`+0x58..+0x70` four insert/delete callbacks, `+0x78`/`+0x7A` capacity, `+0x7C` written records, `+0x7E` free-list length, `+0x80` free-list
head (a deleted record's first u16 links to the next), `+0x82` column count, `+0x84` columns (0x10 each).

Index object (`0x1406AA8E0` / `0x1406ABE88`): `+0` table, `+8` next index, `+0x10` filter expression (temporary indexes: the WHERE),
`+0x1C` id, `+0x20` entry count, `+0x24` dirty, `+0x25` open-result references, `+0x26` flags, `+0x27` drop when unreferenced (1 for
query-made temporary indexes, 0 for the persistent key indexes), `+0x28` container (vtable `0x1496173D0`: hash/sort container of
`{u32 record, key...}` nodes, `vf[16]` new node, `vf[4]` fill key, `vf[5]` insert, `vf[17]` free node, `vf[12]` find, `vf[10]` next).

| path | function | what it does to the indexes |
|---|---|---|
| table load | `0x1406A63E0` -> `0x1406A655C` -> `CreateIndex 0x1406ABAD0` | one persistent index per key definition of the table (`+0x27 = 0`), filled by scanning records `0..+0x7C` that are not deleted |
| INSERT | `AddRecord 0x1406A7990` (from `0x1406A6650`) | free list or `+0x7C++` (error `0x13` when full), zero + write the fields, `IndexInsert` into **every** index (roll back on failure), then the type-3 open results (`0x145DF6DE4` / `0x145DFA6C4`), `+0x4C |= 4`, the `+0x58` callbacks |
| DELETE | `0x1406A982C` | for each matching record: removed from every index, observers, deleted bit + free list |
| UPDATE `[M]` (the three functions that call both index helpers) | `0x145DF71B4`, `0x145DF955C`, `0x145DFA944` | remove from the index (`0x1415E2F60`), write, re-insert (`0x1406AAF60`) |
| SELECT | `0x1406AD3DC` -> `0x1406AD658` -> `0x1406ADBE0` -> `0x1406A9B80` -> `0x1406AA8E0` (create = 1) | best existing index for the WHERE (`0x1406A8E34` scores: 0 = exact, the entry count when the WHERE constrains the key's first column, unusable otherwise); a temporary index (filter = the WHERE) built from the best partial index, else from a table scan; dropped when the result closes (`0x140B1EC94`) |

## 3. Answers to the questions

1. **Indexes / caches a raw append bypasses**: the table's index list (persistent key indexes made at load and any open temporary
   index), the open materialized results, the table callbacks (PlayerSearchManager's players cache) `[H]`. No per-table result cache
   exists besides these `[M]` (the career's manager caches of `realtime_transfers.md` 1.4 are kept by events, not by the database).
2. **Built at load** (`0x1406A63E0`, per table, from the key definitions in the database file) `[H]`; rebuilt only by the next load.
3. **Rebuild / invalidate one table or one row**: no game function does it for an existing record; `AddRecord`'s own loop
   (`IndexInsert` per index) is the per-row operation and is not exposed except through the INSERT path `[H]`.
4. **Safe way to make an appended raw row visible**: none that I would ship. The safe way is to not append raw rows: insert them through
   the game's INSERT (section 6). Rows Turbo already added raw become visible after a save + load (as today).

## 4. The game's creation path `[H]`

* `DataController::CreatePlayer(dc, PlayerRecord*, AppearanceRecord*, GkStyleRecord*)` `0x147B61750` -> pid or -1: `INSERT players` with
  ~60 columns, `headclasscode = 1` (a generic head, always), up to 8 `ucc` rows (`ucctype`, `animationid`, `probability`), and only if the
  INSERT affected 1 row: `UpdatePlayerAttributes 0x147B9F654` (`UPDATE players SET` the 34 attributes, `preferredposition1..7`, `role1..5`,
  `overallrating`, `potential`, `modifier`) and event **`0x3A`** (0x20 bytes from the allocator `*(void**)0x14C269EA8` `vf[2](size,
  "DataController::CreatePlayer", 0)`; `+0` base vtable `0x149803AE8` then `0x14AFF67C0`, `+8` refcount 0, `+0x10 = 0x3A`, `+0x18 = pid`;
  `PostEvent 0x14060124C(**(hub+0x4F8), 0x3A, ev)`).
* `DataController::InsertTeamPlayer(dc, pid, team, jersey, position [stack], suppress [stack])` `0x147B90074`: `INSERT teamplayerlinks
  (playerid, teamid, jerseynumber, position)`; if 1 row and `suppress == 0`: event **`0x5F`** `{pid, team, -1}` (vtable `0x14AFF6C00`).
* The game's own **create into a club**, `0x147E0BC28` (callers `0x147D9138C`, `0x147D94658`): builds the generation data, `0x147E0B51C`
  (-> `CreatePlayer`), `InsertTeamPlayer(pid, FA, 99, 29, 0)` with `FA = 111592` (or `0x20128` when the club is in the sorted vector at
  `dc+0x108`), then `TeamUtil::PlayerMoved(pid, FA, club)` when the club is not that pool, then the wage (`0x147BF5814` on the manager at
  `hub+0xB58`, with the calendar date). The youth generator `0x147E0F500` uses the scratch team `0x1B29D` instead of Free Agents and
  `PlayerMoved(0x1B29D -> 0x1B688)`; sign / promote (`0x147E20D3C` / `0x147E1A4D8`) are `realtime_transfers.md` 2.5.
* **Names**: generated players carry name ids (`firstnameid`, `lastnameid` (also written to `playerjerseynameid`), `commonnameid`)
  resolved through `playernames` / `dcplayernames` (`0x147B72BB0` reads both and `editedplayernames`; its exact precedence is `[M]`); typed names live in `editedplayernames`, which the game writes with
  `DataController::SetEditedPlayerName(dc, EditedName*)` `0x147BA142C`: `DELETE ... WHERE playerid` (`0x147B64854`) then `INSERT
  editedplayernames (playerid, firstname, surname, commonname, playerjerseyname)` from `{+0 pid, +8 / +0x35 / +0x62 / +0x8F char[45]}`
  (45 bytes = the columns' 360 bits, 44 characters + NUL). The same INSERT exists in `0x147B9593C`.

### 4.1 Record layouts (from `CreatePlayer` and `UpdatePlayerAttributes`) `[H]`

`PlayerRecord` (>= 0x1CC bytes): `+0x00` playerid, `+0x04` firstnameid, `+0x08` lastnameid and playerjerseynameid, `+0x0C` commonnameid,
`+0x10..+0x94` 34 ints in this order (name table `0x14BE9B8B0`): acceleration, sprintspeed, agility, balance, jumping, stamina, strength,
reactions, aggression, composure, interceptions, positioning, vision, ballcontrol, crossing, dribbling, finishing, freekickaccuracy,
headingaccuracy, longpassing, shortpassing, defensiveawareness, shotpower, longshots, standingtackle, slidingtackle, volleys, curve,
penalties, gkdiving, gkhandling, gkkicking, gkreflexes, gkpositioning; `+0x98` nationality, `+0x9C` potential, `+0xA0` overallrating,
`+0xA4` height, `+0xA8` weight, `+0xAC` skillmoves, `+0xB0` birthdate (a calendar Date, `SetDate`), `+0xCC..+0xE4` preferredposition1..7,
`+0xE8` growthprofile **- 1**, `+0xEC..+0xFC` role1..5, `+0x178` preferredfoot, `+0x17C` weakfootabilitytypecode, `+0x180` u8 isretiring,
`+0x188` modifier, `+0x190` trait bit set (split by `0x1424C3CE0` into trait1 / trait2 / icontrait1 / icontrait2), `+0x1C0` emotion **- 1**,
`+0x1C4` personality **- 1**, `+0x1C8` gender.

`AppearanceRecord` (>= 0x84): `+0x04` shortstyle, `+0x08` bodytypecode, `+0x0C` hairtypecode, `+0x10` headtypecode, `+0x14` haircolorcode,
`+0x18` facialhairtypecode, `+0x1C` facialhaircolorcode, `+0x20` sideburnscode, `+0x24` skintypecode, `+0x28` skintonecode, `+0x2C`
skincomplexion, `+0x30` skinmakeup, `+0x34` skinsurfacepack, `+0x38` lipcolor, `+0x3C` eyebrowcode, `+0x40` eyecolorcode, `+0x44` eyedetail,
`+0x48` gkglovetypecode, `+0x4C` shoetypecode, `+0x50` / `+0x54` shoecolorcode1 / 2, `+0x58` socklengthcode, `+0x5C` sockstylecode, `+0x60`
jerseyfit, `+0x64` jerseysleevelengthcode (also written to hasseasonaljersey), `+0x68` jerseystylecode, `+0x6C` undershortstyle, `+0x70..+0x7C`
accessorycode1, accessorycolourcode1, accessorycode2, accessorycolourcode2, `+0x80` headassetid.

`GkStyleRecord` (>= 0xA0): `+0x34` gkkickstyle, `+0x38` gksavetype, `+0x3C` runstylecode (-1 = not written), `+0x40` up to 8 ucc entries of
0x0C bytes `{ucctype (-1 ends the list), animationid, float probability}`.

`CreatePlayer` writes about 95 of the 152 `players` columns (the rest take their minimum) and forces a generic head: filling these structs
from a Turbo preset would lose columns (contract, value fields, tattoos, ...) and real faces, so Turbo should not use it (section 5).

## 5. Designs compared

| design | what | verdict |
|---|---|---|
| A: `CreatePlayer` + `InsertTeamPlayer` | fill the three records from the preset, call the game | event `0x3A` is posted before Turbo could fix the ~57 columns it does not write; `headclasscode` forced to 1 (no real faces); needs the full struct layouts right. **No.** |
| B: raw rows + index refresh | Live Editor inserts, then `IndexInsert` per index of the table under the lock `0x14D24A060`, plus the callbacks | re-implements half of `AddRecord` from outside (record number, every index, rollback, observers); a duplicate in a unique index would corrupt it. **No.** |
| **C: the game's INSERT** | the `DataController`'s own query builder + provider for `players` and `editedplayernames`; event `0x3A` as `CreatePlayer` builds it; `InsertTeamPlayer(pid, FA, 99, 29, 0)`; `PlayerMoved(FA -> club)` (op 11) | the game's own write path (indexes, observers, capacity, free list, auto key), every column from Turbo's preset, one synthesized event whose construction is read in `CreatePlayer`. **Yes** (`[H]` mechanics, `[M]` until run live). |

Risks of C: the `0x3A` listeners run before the club link exists (exactly as in the game, which posts it from `CreatePlayer` before
`InsertTeamPlayer`); `PlayerGrowthManager` needs its flag `+0x5F0` (`realtime_transfers.md` 3.3 E); a wrong column name makes the SQL
engine fail the statement (count 0: refuse and report, nothing written); the provider must be the `DataController`'s (validate `dc+0`'s
vtable slot 1 == `db_provider_execute`); game thread only (the call runs where the game's own `DataController` calls run).

## 6. Implementation plan (native op 12 `player_create`, in the style of `player_move`)

1. **Signatures** (built-in table in `core/sigscan.cpp`, from `created_players_signatures.json`): `db_query_init`, `db_query_set_int`,
   `db_query_set_string` (resolved through its call in `SetEditedPlayerName`: byte-identical to the date setter otherwise),
   `db_query_select_field`, `db_query_where_int`, `db_query_destroy`, `db_result_free`, `db_provider_execute`, `event_allocator_global`,
   `event_base_vtable`; reuse `dc_insert_team_player`, `dc_is_player_in_team`, `post_event`, `player_inserted_event_vtable` and op 11's set.
2. **Core** `core/player_create.{h,cpp}`: `Caller` with `insert_row(dc, table, cols)` (Init / SetInt / SetString / Execute / count / Release /
   ~Query, `q` = 0x60 bytes aligned on the stack, never copied), `count_rows(dc, table, col, value)` (SELECT + Where), `post_inserted(pid)`,
   `insert_team_player(dc, pid, team, jersey, position, 0)`. `locate` = `pm::locate` + the provider vtable check + allocator / vtables in the
   image. Sequence: validate (pid > 0 and < 460000, `count_rows(players, playerid) == 0`, no `teamplayerlinks` / `editedplayernames` row,
   target team as op 11's `check_team`, squad room `SquadCounts` + 1 <= MAX); INSERT `players` (count must be 1, read back 1 row);
   INSERT `editedplayernames` if names; post `0x3A`; `InsertTeamPlayer(pid, 111592, 99, 29, 0)`; read back `IsPlayerInTeam(pid, 111592)`
   (the check that failed live); then, for a club, `pm::run(MOVE, pid, 111592, club, months, wage)` (contract record for the user's club).
   A failure after the players INSERT reports what exists (no raw cleanup; a DELETE through the same provider is the rollback if wanted).
3. **Payload**: the players row has 152 columns: Lua writes `turbo_output\turbo_player_create.json` (`{seq, playerid, team, months, wage,
   players: {col: int}, names: {firstname, ...}}`, ints and strings only) and calls op 12 with `args = {comm, code (1 create / 9 check),
   seq, playerid}`; the DLL reads it with nlohmann, checks `seq` / `playerid` and refuses floats, unknown tables, strings over 44 chars.
4. **Host** `win/player_create_win.*`: copy of `player_move_win` (gate, queue, Status line, log), kill switch
   `turbo_output\call_player_create_off.txt`, **off by default until the first live test passes** (the switch file shipped present, or an
   opt-in setting).
5. **Lua**: `bridge.lua` `TurboPlayerCreate(code, payload)`; `create_player.lua` uses it when defined (no `InsertDBTableRow`, no
   `moves.add_to_sheet` for the native path), the database path unchanged otherwise.
6. **Tests**: native (fake provider + fake engine: count, refusals, order players -> names -> 0x3A -> link -> move, read-backs, kill switch,
   signatures on the game's bytes), Lua (payload, fallback when the global is missing).

### 6.1 What was built (2026-10-05)

* **Signatures** (`core/sigscan.cpp` built-in table, each re-proven unique in `fc27_image.bin` with the "rip" offsets at the instruction
  start, which is how Turbo's `resolve_signature` decodes them; `created_players_signatures.json` was aligned): `db_query_init`,
  `db_query_set_int`, `db_query_set_string` (via its call in SetEditedPlayerName, offset 15), `db_query_select_field`, `db_query_where_int`,
  `db_query_destroy`, `db_result_free`, `db_provider_execute`, `event_allocator_global` (offset 0), `event_base_vtable` (offset 0),
  `player_inserted_event_vtable` and `dc_insert_team_player` (as in `realtime_signatures.json`); PostEvent is the existing
  `post_career_event`, op 11's set is reused. Native test: every entry resolves on the image's bytes and equals the JSON.
* **Core** `turbogui/src/core/player_create.{h,cpp}` (namespace `turbo::pc`), **host** `turbogui/src/win/player_create_win.{h,cpp}`, op 12 in
  `core/game_calls.h` / `win/game_calls_win.cpp`. `pc::Caller` extends op 11's `pm::Caller` with `insert_row`, `update_row`, `select_ints`,
  `alloc_event`, `post_event`, `insert_team_player`; the Windows caller runs `Query::Init / SetInt / SetString / Select / Where`,
  `Execute` (the resolved function, validated equal to the provider's vtable slot 1), `holder->vf[1]()` (count) / `vf[2](0, col)` (value),
  `ResultHolder::Release`, `~Query` on a 0x100-byte aligned stack buffer (the game's Query is 0x60).
* **Differences from the plan:** (1) the 152 players columns are not sent in one statement: INSERT with playerid + 47 columns, then
  `UPDATE players ... WHERE playerid` in statements of at most 48 columns (the game's own INSERT has 59 setters and CreatePlayer itself
  follows its INSERT with UpdatePlayerAttributes), then a SELECT read-back of every int column in statements of 48 (exactly 1 row;
  differing values are reported as a WARNING, not a failure). (2) The link's `form = 3` (what the database path writes) is set after
  the move with `UPDATE teamplayerlinks SET form WHERE playerid AND teamid` and read back. (3) The shirt number is the game's (99 in
  Free Agents, then PlayerMoved's choice), not Lua's. (4) Columns are checked against `bridge_meta.json` (long names) before anything
  is written; floats, booleans, unknown keys / tables / columns, names over 44 bytes and a seq / playerid that differs from the call's
  words are refused.
* **Payload / contract:** `turbo_output\turbo_player_create.json` = `{seq, playerid, team, months, wage, players: {col: int},
  names: {firstname, surname, commonname, playerjerseyname}, link: {form}}`; args = comm, code (1 create, 9 check), seq, playerid;
  out[0] = written mask (1 players, 2 names, 4 event, 8 Free Agents link, 16 moved, 32 contract record, 64 link values; -1 = the
  call is off and nothing ran), out[1] = IsPlayerInTeam(pid, final team). Lua: `bridge.lua` `M.player_create` = `TurboPlayerCreate(code,
  payload)` -> ok, text, status ("ok" / "queued" / "failed" / "off"), written, in_team; `features/create_player.lua` builds the payload
  from the same row as the database path (CMTracker head choice, contract values, names) and uses it when the global exists; "off"
  -> the database path with a note; a refusal -> nothing written.
* **Safety gate:** off unless `turbo_output\call_player_create_on.txt` exists (Status line `player_create: off (opt-in: create
  turbo_output\call_player_create_on.txt)`), kill switch `call_player_create_off.txt`, game-hook switches, one call at a time, game
  thread only (queued otherwise), no match day running.
* **Tests:** `turbogui/tests/native/test_player_create.h` (synthetic game: Free Agents / AI club / your club, order, read-backs, every
  refusal, bad payloads, missing signatures, mismatched vtables, failures after the INSERT, switches, op 12 through the call block,
  signatures on the game's bytes); `turbo/tests/t21_native_create.lua` (fake TurboPlayerCreate: payload contents, no InsertDBTableRow /
  team-sheet write, fallback when missing / off, the bridge function's file and answers).

## 7. What to verify live (throwaway career, save backed up)

0. Without the opt-in file: Status tab line `player_create: off (opt-in: create turbo_output\call_player_create_on.txt)`; Create
   player still works through the database (its result ends with the "player_create: off" note). The log shows `game calls:
   player_create resolved (...)` with the addresses below.
1. Read-only first: `dc+0` vtable `== 0x14B075540`, slot 1 `== 0x142297D8C`; `*(0x14C269EA8)` readable; a code-9 check of a new id
   (with the opt-in file: `TurboPlayerCreate(9, payload)` answers "can be created ... (checked only, nothing was written)", out[0] 0).
2. Optional evidence for section 0: walk `teamplayerlinks`' `+0x18` index list (count, `+0x1C` ids, `+0x20` entries vs `+0x7C - +0x7E`) before
   and after a Live Editor insert: the entries do not grow.
3. Create into Free Agents: `IsPlayerInTeam(pid, 111592)` true at once, the players row read back, GTN / player search finds him.
4. Create into your club: Squad Hub / Team Management at once, morale shown, contract record; save + reload: no duplicate rows.
5. Create into an AI club, a full club (refused), a national team (refused), a name with accents (editedplayernames).

## 8. Evidence index

| what | address |
|---|---|
| query init / set int / set string / set float / set date / select / where / destroy | `0x14197D5F4` / `0x140601EA8` / `0x14403F2E8` / `0x14403F3A8` / `0x14403F348` / `0x140601F14` / `0x140602D28` / `0x14154EB38` |
| provider Execute / vtable / select / delete / update / insert executors | `0x142297D8C` / `0x14B075540` / `0x14229827C` / `0x142297EB8` / `0x142298054` / `0x1482942E8` |
| result count thunk / release / DC provider setter | `0x14154C5B8` / `0x1422985A4` / `0x147B97F48` |
| DB service (registry id `0xAE932D0`) / ctor / statement execute / SQL dispatch | `0x1473FC1E0` / `0x1473FBCB4` / `0x1473FC8D0` / `0x1406AEFA0` |
| T3db AddRecord / IndexInsert / index remove / DELETE / CreateIndex / table keys at load / table loaded | `0x1406A7990` / `0x1406AAF60` / `0x1415E2F60` / `0x1406A982C` / `0x1406ABAD0` / `0x1406A655C` / `0x1406A63E0` |
| SELECT executor / result opener / index chooser / index scoring / result close / index drop | `0x1406AD3DC` / `0x1406A9B80` / `0x1406AA8E0` / `0x1406A8E34` / `0x140B1EC94` / `0x1406A7F7C` |
| table callbacks installer / players cache registrar / global lock | `0x145DF6A78` / `0x145DF47EC` / `0x14D24A060` |
| CreatePlayer / UpdatePlayerAttributes / InsertTeamPlayer / SetEditedPlayerName / create into club | `0x147B61750` / `0x147B9F654` / `0x147B90074` / `0x147BA142C` / `0x147E0BC28` |
