# Standings screen data path (FC 27, why an edited FCE row does not show)

Static analysis of `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (FC27.exe 1.0.140.64835, image base
0x140000000), 2026-10-03, with `scripts/re/rx.py` / `rx_jobs.py` / `sig_jobs.py`. Nothing was run in the game.
Confidence: [H] read in code, [M] strong inference, [L] guess. Companion of `C3-live-standings.md` and
`standings-fixtures-notes.md` (FCE side); this note covers the career-mode side between FCE and the UI.

## 0. Answer

1. **The Standings screen and the Office tile do not ask FCE.** They read a cached copy kept by the career manager
   `FCECareerModeStandingsViewManager` (SVM, Live Editor type id 108): `StandingsViewManager::mLiveStandings`, an
   `eastl::map<int compObjId, LiveStandings*>` at `svm+0x250` (critical section at `svm+0x280`). The screen's data
   provider calls SVM vtable slot 10 (`0x147da3ae8`) which copies `mLiveStandings[selected comp]` to the UI
   (`0x14892ec20`). Nothing on the screen-open path sends `FCEI::RequestGetStandings`. [H]
2. **The cache is rebuilt only when a `ResponseStandingsList` with tag `'rmvs'` (type 0x12) or a type-0x13 response
   reaches the SVM** (`StandingsViewManager::HandleFCEMessage` 0x147da1240 -> cache writer 0x147da6f04). The game
   sends those on career events 22 MAIN_COMPETITION_SCHEDULED / 23 SEASON_RESET / 29 POST_LOAD_PREPARE /
   33 COMPETITION_COMPLETE (SVM listener 0x147da0e10) and from the SimDayManager's day-processing state machine
   (`RequestStandingsCheckEarlyResults`, type 0x2c -> type-0x13 response). Opening a screen or advancing a day
   without a match day does none of that, so Turbo's row edit (correct, in the right DataManager) stayed invisible. [H]
3. **There is one FCE instance.** `FCE::FCEInterfaceImpl` is created once (0x148a040c8) and published in the service
   registry (0x0a613b9a); the career's own handle (`ctx+0x38`, type id 1 `IFCEInterface`) is fetched from that
   registry (0x1489386f8 / 0x147c15b9c). One ManagerHub, one DataManager. The object Turbo wrote to is the one the
   handlers read. [H]
4. **What Turbo must do:** after writing rows, run on the game thread, for every key `k` of the SVM map,
   `0x147da5310(svm, k)` (synchronous `RequestGetStandings`, tag `'rmvs'`, immediate). FCE answers inside the call
   (mailbox dispatch is synchronous), the SVM replaces `mLiveStandings[k]` with a fresh clone of the live rows and
   posts the `LiveTableUpdate` event (0x75) that the UI listens to. Signatures in section 6. [H for the mechanism]

## 1. Career manager table ("ctx")

Every career manager keeps a pointer to the manager table at `+0x08`. The table is what Live Editor's
`GetManagerObjByTypeId` walks (`career_mode/helpers.lua`): `managers = *(*(comm + 0x20) + 0x10)` with
`comm = GetPlugin(ENUM_djb2FeFceGMCommServiceInterface_CLSS = 0x1297f047)` (Turbo publishes it as
`comm_service` in `bridge_state.json`; `mem.manager(type_id)` in `turbo/core/mem.lua` does the walk with checks).
Slot `i` = `managers + 0x20*i`: `+0x08` type descriptor (its `+0x10` == 1), `+0x10` int instance count (== 1),
`+0x18` holder pointer, `*holder` = the manager. [H: LE's walk; and every holder offset used by the career code is
`0x20*id + 0x18` for the id Live Editor lists in `career_mode/enums.lua`:]

| ctx offset | type id (`ENUM_FCEGameModes...`) | used by |
|---|---|---|
| 0x38 | 1 `IFCEInterface` | all request senders: `[[ctx+0x38]]->vt[4](request)` |
| 0x298 | 20 `FCECareerModeActiveCompetitionsManager` | comp lists, 'moca' requests |
| 0x318 | 24 `...CalendarManager` | date for `RequestGetStandings` (+0x34 double, +0x3c int) |
| 0x3b8 | 29 `...ConcurrentMatchManager` | `+0x9c8` = competition shown on the Standings screen |
| 0x4f8 | 39 `...EventsMailBox` | career events (`LiveTableUpdate`) posted through 0x14060124c |
| 0x538 | 41 `...FCECommsManager` | owns the response router 0x147b0cdc4 |
| 0x5d8 | 46 `...FixtureManager` | |
| 0x758 | 58 `...MainHubManager` | |
| 0xcf8 | 103 `...SimDayManager` | state machine that sends `RequestStandingsCheckEarlyResults` |
| **0xd98** | **108 `...StandingsViewManager`** | the standings cache |
| 0x1038 | 129 `...UserManager` | user's club / league ids |

## 2. StandingsViewManager (SVM)

Vtable `base+0xB0160D8` (80 slots) [H, re-verified 2026-10-04: the constructor **0x147d9a5c8** stores `ctx` at +0x08
and `lea rax,[rip+..]` -> 0x14B0160D8 at +0x33; the destructor 0x147d9af90 sets it on entry and ends with the base
vtable `base+0x975EA28`. The 0x147d9a570 named earlier is the constructor of another class (vtable 0xB016160), not the
SVM]. Career-event listener base vtable `base+0x975EA28` (slot 3 = 0x147da0e10) [H]. Every function Turbo calls takes
the slot-108 object as `this`: the listener's event-29 path writes `[this+0x490]`, calls 0x147da5310(this, comp) and
reads `[this+0x488]`; the cache writer uses `this+0x250` / `+0x280` / `+0x490`; the screen feed (slot 10) hands
`[[ctx+0xd98]]` to the same map reader [H].

| offset | content | evidence |
|---|---|---|
| +0x08 | ctx (manager table) | every function |
| +0x10 + idx | u8 "request pending" per known tag (27 tags, table in 0x147d9f4a4) | 0x147da53ce |
| +0x18 | u8 enabled (slots 9/10 return early when 0) | 0x147da3af7 |
| +0x30 + idx*8 | `mStandings[idx]`: cached response per known tag ("mStandings[ ]") | 0x147da17ed |
| +0x250 | `mLiveStandings`: eastl rbtree anchor (+0 rightmost, +8 leftmost, +0x10 root = svm+0x260); node: +0 right, +8 left, +0x10 parent, +0x20 int key compObjId, +0x28 LiveStandings*; **+0x270 u32 node count** (anchor+0x20, `inc dword [rsi+0x20]` in the insert helper 0x147d9d4c4) | 0x147d9f6f3..0x147d9f764, 0x147d9d548 |
| +0x280 | CRITICAL_SECTION for the map | 0x147da6f32 / 0x147d9f6e6 |
| +0x380 | second map, type-0x10 'rmvs' responses | 0x147da182b |
| +0x3b0 + idx*8 | "StageInfo" per tag (type 0x17 responses) | 0x147da16c5 |
| +0x488 | LiveStandings* copy of the user's competition (written by the cache writer when null and by the refresh; no reader found) | 0x147da7108, 0x147da1008 |
| +0x490 | u8 "LiveTableFirstUpdate posted" | 0x147da70a9 |

`LiveStandings` = 0x10 bytes: `+0` vtable `base+0xB016148`, `+8` deep copy of the response's standings data
(0x144040198 allocates, 0x14403fc30 copies; the rows are the sorted `FCEI::StandingObject`s of
`standings-fixtures-notes.md` §2). Allocated under the name "StandingsViewManager::mLiveStandings". [H]

Known request tags (index -> tag, 0x147d9f4a4): 0 hmcs, 1 pmcc, 2 port, 3 tlcl, 4 rmos, 5 amrt, 6 mmoc, 7 moca,
8 mmbj, 9 trpg, 10 eert, 11 moci, 12 qrmt, 13 rots, 14 tuet, 15 adst, 16 oadc, 17 clcu, 18 ccle, 19 swen, 20 mrlp,
21 mptt, 22 vhlg, 23 ctbl, 24 cdus, 25 clce, 26 2vlg. `'rmvs'` (0x73766d72) is deliberately **not** in the table.

### 2.1 Readers (the UI side)

* `GetLiveStandingsCopy` 0x147d9f6b8(svm, allocator, compObjId): EnterCriticalSection, find `map[compObjId]`, clone
  into a new `LiveStandings`, LeaveCriticalSection; returns 0 when the key is absent. **No request on a miss.** [H]
  Wrappers: 0x147d9caac(out, svm, comp), 0x147d9c6e4 (A), 0x147d9c7d0 (B: comp = user's league via 0x147abd100,
  or the team's comp via 0x147c9d264/0x147c9d20c on the ActiveCompetitionsManager), 0x147d9cadc, 0x147d9cb44.
* **Standings screen:** SVM slot 10 (`+0x50`) = 0x147da3ae8(svm, arg): if `svm+0x18`, `comp =
  [[ctx+0x3b8]]+0x9c8` (the competition selected on the screen), `0x147d9caac` -> if found and comp != -1 ->
  `0x14892ec20(copy)` (career UI layer) -> `0x147da3b70`. Its caller is the UI action object 0x144f1984c:
  `ctx = [[0x14c2a6c90]]->vt[0x180]()`, `svm = [[ctx+0xd98]]`, `svm->vt[0x50]()`. [H for the chain; M that this is
  the only path of the screen - it is the only UI-side caller of slot 10 and the only one that pushes rows to the UI]
* **Office hub tile / league cache:** `LeagueCache::CreateLiveStandingsByTeamId` 0x147aca434 ->
  0x147d9cb44 -> user-comp copy from the map. [M]
* Many managers read `mStandings[idx]` for their own tags (sync requests through 0x147da546c, 24 call sites); those
  copies are not what the screen shows.

### 2.2 The FCE message handler 0x147da1240 = `StandingsViewManager::HandleFCEMessage(svm, type, msg)` [H]

Reached from the FCECommsManager router 0x147b0cdc4 (vtable function at 0x14afe05f8; it fans a message out to every
manager's handler, the SVM call is at 0x147b0cecc) and from three other routers (0x147ebc5e7, 0x147ebcae0,
0x147ec0247).

| message | what the SVM does |
|---|---|
| 0x12 `FCEI::ResponseStandingsList` (reply to `RequestGetStandings` 0x30) | `tag = msg+0x14`. Known tag -> clone `msg+0x18` into `mStandings[idx]`, then 0x147d9be20(svm, tag). **Unknown tag and tag == 'rmvs' -> cache writer 0x147da6f04(svm, msg+0x18, msg+0x20 compObjId).** |
| 0x13 (second `ResponseStandingsList` flavour, built by 0x148a541d8 as the reply to `RequestStandingsCheckEarlyResults` 0x2c) | cache writer, **no tag check** |
| 0x10, tag 'rmvs' | map at +0x380 |
| 0x17 "StageInfo", 0x23 `ResponseCompetitionStageInfo`, 0x27 "FCEI::GroupDataList" | per-tag side data |

**Cache writer 0x147da6f04(svm, list, compObjId)** [H]: lock +0x280; if `map[comp]` exists free it; allocate a
`LiveStandings` and clone `list`; `map[comp] = it`; if `comp == user league (0x147abd100)` and `+0x490 == 0` post
career event 0x5d "StandingsViewManager::LiveTableFirstUpdate" and set `+0x490 = 1`; if `+0x488 == 0` fill it with a
copy; find the user's position (`entry->vt[1]() == 0`, position = `entry+0x20`); unlock; post career event 0x75
"StandingsViewManager::LiveTableUpdate" (0x20 bytes, `+0x18` = user position) through 0x14060124c to the
EventsMailBox `[[ctx+0x4f8]]`. The UI refreshes on those events; the screen re-reads the map on open.

### 2.3 Request senders in the SVM

| function | request | tag | immediate | notes |
|---|---|---|---|---|
| **0x147da5310(svm, compObjId)** | `RequestGetStandings` (0x70 bytes, ctor 0x144036e6c) `+0x20 = comp` | 'rmvs' | `+0x6c = 1` | returns at once when comp == -1; no date fields (ctor defaults) - the game uses it this way itself |
| 0x147d9b250(svm**, comp) | same (`rcx` = pointer to the svm pointer) | 'rmvs' | 1 | second loop of the full refresh |
| 0x147da53a8(svm, comp) | `+0x24` date / `+0x2c` from the calendar, then `RequestCompetitionStageInfo` (0x3c) | 'mmbj' | 0 (queued) | result lands in `mStandings[8]`, not in the map |
| 0x147da5260(svm, comp) | + stage info | 'moca' | 0 | ActiveCompetitionsManager slot 13 (0x147c9be28) / 0x147c959b8 |
| 0x147da546c(svm, comp, tag, flag) | stage info + standings | caller's | 1 | 24 callers (other managers) |
| 0x147da4fcc | `RequestGetCompetitionStandings` | | | |

All of them send through `[[ctx+0x38]]->vt[4]` = `FCE::FCEInterfaceImpl` slot 4 = 0x148a35d3c = `Post(request)`.

## 3. When the game refreshes the cache (and why Turbo's edit stayed hidden)

1. **Career events -> SVM listener slot 3 = 0x147da0e10(svm, eventId, event)** [H]:
   * 29 `POST_LOAD_PREPARE` (if user entry `+0x30c > 0`; the entry is `[um+0x18] + 0x348*[um+0x14]` of the
     UserManager, 0x14154adbc) and 23 `SEASON_RESET` (if `event+0x30 != 0`): **full refresh** = 0x147d9b6b0 builds
     the list of the user's competitions, `+0x490 = 0`, for each comp `0x147da5310` (sync 'rmvs') +
     `0x147da5104` (`RequestScheduleGetFixtures`), for the user's league also `+0x488 = copy` (0x147d9c7d0) and
     0x147d9c220(svm, 'pmcc'); then for every ActiveCompetitionsManager entry `0x147d9b250` (sync 'rmvs').
   * 22 `MAIN_COMPETITION_SCHEDULED` (if `+0x30c == 1`): 0x147da77e0(svm, 0x147abd528()).
   * 33 `COMPETITION_COMPLETE`: per comp 0x147da5310 + 0x147da5104.
   * 115: refresh of the comps in `[[ctx+0x418]]`.
   (Event ids from Live Editor's `CONST_CM_EVENTS_NAMES`.)
2. **Match days:** the SimDayManager (103) state machine 0x147da6e58 (`this+0x14` = state, driven by its FCE handler
   0x147da1164 and the per-frame update 0x147f1bdd8 / 0x147b03c60) reaches state 9 -> 0x147da67b0 ->
   `FCEI::RequestStandingsCheckEarlyResults` (type 0x2c, ctor inline at 0x147da6a35) -> FCE StandingsManager
   0x148a534d8 then 0x148a541d8 -> type-0x13 response -> cache writer. [M on the state meaning, H on the chain]
3. A router stub 0x147ec5b14 re-requests standings ('mmbj', queued) when a `RequestSwapCompetitionTeams` (type 0x1e)
   tagged 'mmbj' passes by; that only feeds `mStandings[8]`.

Turbo's test: career loaded (cache filled at POST_LOAD_PREPARE with all-zero rows), rows written on 1 July, screen
opened, day advanced to 2 July with no fixtures: none of the triggers above ran, the SVM map still held the clone
made at load, and both the tile and the screen rendered that clone. The FCE rows themselves were fine. [H]

## 4. One FCE instance; the request/response transport

* `FCE::FCEInterfaceImpl` (0x20 bytes) is created by 0x148a040c8: allocate, ctor 0x148a16b20 (the only caller of
  `ManagerHub::Init` 0x148a2ee24, which is the only creator of `FCE::DataManager` 0x148a1661c), then
  `registry->Register(0x0a613b9a, impl)`. 0x148a040c8 is called only from the service holder 0x1489386f8, which
  creates the interface **once, when its holder is still empty**, and otherwise resolves it through
  `registry->Get(0x0a613b9a)` + `QueryInterface(0x0a613b9b)` (0x147c15b9c). The destructor path 0x1489352d4
  unregisters. The career's `ctx+0x38` holder is filled by that same code, so Live Editor's
  `GetPlugin(ENUM_djb2IFCEInterface_CLSS)` and the career's requests use the same hub, DataConnector and DataManager
  (`DM = *(*(*(impl+0x18)+0x18)+0x80)`). No preview / per-career copy of the DataManager exists in code. [H]
* `FCEI::MailBox` (ctor 0x144036258) = `{+0 mRequestServer, +8 mResponseServer, +0x10 mEventServer}`, each a
  0x178-byte server (ctor 0x14244d564, vtable `base+0x972FB80`). `FCEInterfaceImpl` slot 4 (0x148a35d3c) =
  `hub->mailbox->mRequestServer->vt[6](type, req)` then `req->Release()`. Server `vt[6]` 0x14230f760 ->
  `vt[23]` 0x141bbc7c0: looks the handler lists up for key -1 and for the message type and **calls every handler's
  `vt[1](type, msg)` synchronously** (re-entrancy guard byte at server+0x170: a message posted while that server is
  already dispatching is queued instead). `FCEStandingsManager::HandleMessage` (0x148a4e8fc) handles type 0x30 at
  once when `req+0x6c != 0` (0x148a54b00), otherwise queues it for its pump 0x148a53238. The reply is posted by
  0x142a60d5c (copies `req+0x18` tag into `resp+0x14`, then `mResponseServer->vt[6]`), which dispatches synchronously
  to the FCECommsManager router 0x147b0cdc4 and so to 0x147da1240. **An immediate `RequestGetStandings` therefore
  returns with the SVM cache already rebuilt** (the game relies on it: 0x147da0e10 reads the map right after the
  request, 0x147da1008). [H]

## 5. What Turbo must do

### 5.1 Locate the SVM (no scan, no game call)

```
comm     = bridge_state.json comm_service (GetPlugin(0x1297f047))
managers = *(*(comm + 0x20) + 0x10)                      // career manager table = "ctx"
slot     = managers + 0x20 * 108                         // ENUM_FCEGameModesFCECareerModeStandingsViewManager
require  *(int*)(slot + 0x10) == 1
svm      = **(void***)(slot + 0x18)
validate *(uint64*)svm == game_base + 0xB0160D8  &&  *(uint64*)(svm + 8) == managers
```
The Lua side can publish it as `svm` (`mem.manager(108)`) next to `ifce`; re-validate before every use like
`fce::validate` (a new career = new managers). `ifce` must equal `**(void***)(managers + 0x38)` (type id 1) - a cheap
cross-check that the located DataManager is the career's.

### 5.2 Refresh after a row write (recommended)

On the game thread (`host::run_on_game_thread`; the "lua" path runs inside Live Editor's career-event hook, which
is exactly the context the game itself uses for these calls in 0x147da0e10):

```
keys = walk the rbtree at svm+0x250 (root = *(svm+0x260); node: +0 right, +8 left, +0x20 int key) - a handful
for k in keys:  svm_request_standings_sync_rmvs(svm, k)       // 0x147da5310, __fastcall(svm, int compObjId)
```
Effect per key: `RequestGetStandings{tag 'rmvs', comp k, immediate}` -> FCE re-reads and re-sorts the live
`DataManager+0x88` rows for that competition -> the SVM replaces `mLiveStandings[k]` and posts `LiveTableUpdate`
(0x75) (and `LiveTableFirstUpdate` 0x5d if it never did). The next Standings screen open, and listeners of the
events, show the edited values. The map keys are the ids the game itself requested with (the competition ids the
screen selects), which is why Turbo should refresh the existing keys rather than guess one: the row's `compObjId`
(+0x02, the stage/group node) is **not** necessarily the key. Refreshing every key costs one synchronous FCE request
each (typically 2-4 keys).

Alternatives:
* **Full refresh** `svm_on_career_event(svm, 29, nullptr)` (0x147da0e10): replays POST_LOAD_PREPARE - also
  re-requests fixtures (`RequestScheduleGetFixtures`), rebuilds `+0x488` and the 'pmcc' data. Requires a club
  career (`user entry +0x30c > 0`); the event pointer is not read on this path. Heavier, but it is the game's own
  load-time sequence. `(svm, 23, fake_event)` with `fake_event+0x30 != 0` (allocate >= 0x40 bytes) does the same
  plus the SEASON_RESET branch.
* Only the user's league: `k = svm_user_comp_id(svm)` (0x147abd100) then one `0x147da5310` call.
* Do **not** patch the cached `LiveStandings` clones: the writer replaces them wholesale and they are re-sorted
  by FCE anyway.

Constraints: run on the game thread only; never during Sim To Date / a match (same rule as the row writes); the
functions use the game allocator global (`base+0xC269EA8`) and the career managers, so they need a loaded career
(`svm+0x08 == managers`, `*(managers+0x20*108+0x10) == 1`). Each call returns after the synchronous dispatch;
nothing to wait for.

### 5.3 In-game verification

1. Edit a row in Turbo (points +10), Apply -> Turbo queues the refresh -> open Standings: position and points follow.
2. Office tile: after the refresh (LiveTableUpdate) or after re-entering the hub.
3. Play / sim a match day afterwards: the SimDayManager's 0x2c request rebuilds the cache on top of the edited row.

## 6. Signatures (unique in `.text1` unless noted)

| name | va | signature |
|---|---|---|
| `svm_request_standings_sync_rmvs` (call this) | 0x147da5310 | `83 FA FF 74 65 48 89 5C 24 08 57 48 83 EC 20 48 8B 41 08 4C 8D 05 ?? ?? ?? ??` |
| `svm_on_career_event` (full refresh) | 0x147da0e10 | `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 F8 49 8B D8` |
| `svm_user_comp_id` | 0x147abd100 | `48 83 EC 28 4C 8B C9 E8 ?? ?? ?? ?? 80 79 2C 00 44 8B 90 08 03 00 00 74 30` |
| `svm_user_comp_list` | 0x147d9b6b0 | `48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 40 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 30 48 8B 41 08` |
| `svm_request_standings_sync_rmvs_b` | 0x147d9b250 | `48 89 5C 24 08 57 48 83 EC 20 48 8B 01 4C 8D 05 ?? ?? ?? ?? 45 33 C9 8B FA` |
| `svm_request_standings_mmbj` | 0x147da53a8 | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 8B FA 48 8B D9 BA 6D 6D 62 6A` |
| `svm_cache_writer` (hook point to observe refreshes) | 0x147da6f04 | `48 89 5C 24 10 48 89 6C 24 20 44 89 44 24 18 56 57 41 54 41 56 41 57 48 83 EC 30 4C 8D A1 80 02 00 00` |
| `svm_get_live_standings_copy` | 0x147d9f6b8 | `48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 48 8D A9 80 02 00 00` |
| `svm_handle_fce_message` | 0x147da1240 | `4D 85 C0 0F 84 ?? ?? ?? ?? 48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9` |
| `svm_slot10_refresh_screen` | 0x147da3ae8 | `48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 80 79 18 00 48 8B EA 48 8B F9` |
| `svm_check_early_results` (SimDayManager) | 0x147da67b0 | `40 53 48 83 EC 30 48 8B 41 08 48 8B D9 48 8B 90 58 07 00 00 48 8B 02 48 8B 50 08` |
| `fcei_request_get_standings_ctor` | 0x144036e6c | `48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8D 05 ?? ?? ?? ?? 48 89 01 33 C0 89 41 08 87 41 08 48 8D 0D ?? ?? ?? ?? 48 C7 47 10 30 00 00 00` |
| `fce_iface_post` (FCEInterfaceImpl slot 4) | 0x148a35d3c | `40 53 48 83 EC 20 48 8B 01 48 8B DA FF 50 40 8B 53 10 4C 8B C3 48 8B C8` |
| `fce_response_type13_builder` | 0x148a541d8 | `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B 41 18` |

`svm_get_user_comp_copy_B` 0x147d9c7d0 has a twin (0x147d9c6e4) with identical code: not signable, not needed.
Vtables (RVA): SVM 0xB0160D8, SVM listener 0x975EA28, LiveStandings 0xB016148, mailbox server 0x972FB80; game
allocator global 0xC269EA8 (used inside the request builders). The SVM vtable is re-derived on another build from the
ctor site **0x147d9a5c8** (`lea rax,[rip+..]` at +0x33 of the prologue below, Turbo's `svm_vtable` signature, resolve
"rip" offset 0x33) and cross-checked by reading `*svm` once it is located through the manager table and comparing
slot 10 with the `svm_slot10_refresh_screen` function (Turbo's `svm_slot10`):

| name (Turbo.dll built-in table) | va | signature |
|---|---|---|
| `svm_vtable` (ctor, rip +0x33 -> 0x14B0160D8) | 0x147d9a5c8 | `4C 8B DC 49 89 5B 10 49 89 73 18 57 48 83 EC 70 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 60 C5 FE 6F 05 ?? ?? ?? ?? C5 FA 6F 0D ?? ?? ?? ?? 48 89 51 08 48 8D 05 ?? ?? ?? ?? 48 89 01` |
| `svm_refresh_comp` (= `svm_request_standings_sync_rmvs`) | 0x147da5310 | as above |
| `svm_listener` (= `svm_on_career_event`) | 0x147da0e10 | as above |
| `svm_slot10` (= `svm_slot10_refresh_screen`) | 0x147da3ae8 | as above |

## 7. Key addresses

| addr | what |
|---|---|
| 0x147da1240 | SVM::HandleFCEMessage (types 0x10/0x12/0x13/0x17/0x23/0x27) |
| 0x147da6f04 | cache writer: map insert + LiveTableFirstUpdate (0x5d) / LiveTableUpdate (0x75) |
| 0x147d9f6b8 | GetLiveStandingsCopy (map read + clone) |
| 0x147da3ae8 | SVM slot 10: Standings screen feed (map -> 0x14892ec20) |
| 0x144f1984c | UI action: `[[0x14c2a6c90]]->vt[0x180]()` = ctx, `[[ctx+0xd98]]->vt[0x50]()` |
| 0x147da0e10 | SVM career-event listener (22/23/29/33/115) |
| 0x147da5310 / 0x147d9b250 | sync 'rmvs' RequestGetStandings |
| 0x147da53a8 / 0x147da5260 / 0x147da546c | 'mmbj' / 'moca' / caller-tag requests |
| 0x147d9f4a4 | tag -> index (27 known tags) |
| 0x147abd100 | user's league comp id (UserManager entry +0x308) |
| 0x147b0cdc4 | FCECommsManager response router (vtable fn at 0x14afe05f8) |
| 0x147da6e58 / 0x147da1164 / 0x147da67b0 | SimDayManager state machine / FCE handler / RequestStandingsCheckEarlyResults |
| 0x147ec5b14 | router stub: RequestSwapCompetitionTeams (0x1e) 'mmbj' -> re-request |
| 0x1489386f8 / 0x148a040c8 / 0x147c15b9c / 0x1489352d4 | FCE interface holder / factory+Register / registry Get+QI / Unregister |
| 0x144036258 / 0x14244d564 / 0x14230f760 / 0x141bbc7c0 | MailBox ctor / server ctor / server Post / synchronous dispatch |
| 0x142a60d5c | FCE: post a response (tag copy) to mResponseServer |
| 0x148a541d8 / 0x148a54c33 | ResponseStandingsList builders: type 0x13 (for 0x2c) / type 0x12 (for 0x30) |

FCEI message types seen here: 0x12 ResponseStandingsList, 0x13 ResponseStandingsList (early-results flavour),
0x1e RequestSwapCompetitionTeams, 0x23 ResponseCompetitionStageInfo, 0x2a RequestUpdateMatchResult,
0x2c RequestStandingsCheckEarlyResults, 0x30 RequestGetStandings, 0x3c RequestCompetitionStageInfo.

## 8. Open points

* `svm+0x488` (user-comp copy) has writers but no reader was found; if some tile reads it, the full-refresh
  alternative (5.2) covers it. [L]
* The global 0x14c2a6c90 whose `vt[0x180]` returns the manager table is an unverified alternative anchor to the
  comm-service walk. [L]
* Which career event, if any, fires on a plain day advance without fixtures was not traced; the observed behaviour
  (no refresh on 1 -> 2 July) matches the trigger list above.
* Persistence of the FCE rows across save/load is still the open item of `C3-live-standings.md` §6.
* How the game reaches 0x147da0e10: it sits only in slot 3 of the shared base vtable 0x975EA28 (written at the end
  of ~100 destructors across the career code), while the SVM's own vtable has a `this+0x10` thunk in slot 3. The
  function's body uses the slot-108 object as `this` (0x490 / 0x488 / 0x250 offsets, see §2), so Turbo's fallback
  call `(svm, 29, nullptr)` matches what the event-29 path expects; the dispatch mechanism itself is not traced. [M]

## 9. Implementation in Turbo (track E5-standings-refresh, 2026-10-04)

**Lua** (`turbo/package/lua/libs/v2/imports/turbo/bridge.lua`, `core/mem.lua`): `bridge_state.json` carries `svm`
(`mem.manager(108)`) and `managers` (`mem.manager_table()` = `[[comm+0x20]+0x10]`) next to `ifce`, both `0x0`
outside a career or without the GUI's memory map; a change of either re-publishes the state. The native
`TurboStandingsRefresh()` (installed with `TurboJobOfferCreate`) sends mailbox call op 2 with
`{svm, managers, comm, ifce}`.

**Turbo.dll** (`turbogui/src/core/standings_refresh.{h,cpp}`, `src/win/standings_refresh_win.{h,cpp}`; signatures
`svm_refresh_comp`, `svm_listener`, `svm_vtable`, `svm_slot10` in `core/sigscan.cpp`):

1. Locate: the published `svm`, else `manager_at(managers, 108)` with `managers` published or derived from
   `comm_service`.
2. Validate (nothing is called before every check passes): pointer-shaped, readable to +0x490, `*svm` == the vtable
   from `svm_vtable` (fallback RVA 0xB0160D8 + image base), `*(*svm+0x50)` == `svm_slot10`, `svm+0x08` == the
   manager table, the table's slot 108 holds `svm`, and the table's slot 1 == the `ifce` Turbo wrote to.
3. Walk `mLiveStandings`: bounded in-order walk from the root at svm+0x260 (explicit stack, at most 64 nodes),
   every node's parent link checked against the node it was reached from (root: the anchor svm+0x250), keys must be
   strictly ascending, the walked count must equal the u32 at svm+0x270. Any inconsistency stops the walk.
4. Refresh: `svm_refresh_comp(svm, key)` for every key, in key order (§5.2). A failing call stops the sequence
   (message counts what ran).
5. Fallback when the map is empty or inconsistent: `svm_listener(svm, 29, nullptr)` (§5.2 full refresh), reported as
   such.
6. Threading: after `fce::write_row` / `fce::edit_result` succeed in `ui/ui_standings.cpp` the request goes through
   `App::standings_refresh` (the host's `RefreshService`) to `run_on_game_thread` (game_tick hook, else the Lua
   pump); a call from the game thread itself (Lua's `TurboStandingsRefresh()` inside a career-event handler) runs at
   once. Outcomes are polled by `App::tick` (toast "Standings refresh: ..." + log + the "Standings view:" line of
   the Live standings view) and listed in the Status tab under "Game calls". Kill switch
   `turbo_output\call_standings_refresh_off.txt`; every game-hook switch applies too.

Tests (`turbogui/tests/native/test_main.cpp`, `turbo/tests/t07_bridge.lua`, `t12_job_offer.lua`): synthetic manager
table + SVM with a real rbtree layout (3 keys, 1 key, 70 keys, empty), locate / validate / walk, every refusal
(wrong vtable, wrong slot 10, back-pointer, slot 108 mismatch, ifce mismatch, unmapped / cut-off object, missing
functions), every walk inconsistency (parent link, order, size counter, bad / unmapped node, cycle, bound) with the
fallback, a failing sync call, the four signatures against the image bytes (vtable via the ctor's lea), and a UI
driver case: a row write through the Live standings view queues the request with the published addresses, the
outcome arrives as a toast, a refused service and no service leave the row written.

## 10. In-game test plan (not run yet: nothing here was executed in the game)

Preconditions: Turbo 0.4.x with this track, Live Editor v27.1.2, a Manager Career loaded, Turbo GUI running (F8),
Status tab > Game hooks shows `standings_refresh: ready` with the four addresses (0x...DA5310, 0x...DA0E10,
0x...0160D8, 0x...DA3AE8 on 1.0.140.64835) and `bridge_state.json` carries non-zero `svm` / `managers`.

1. Office hub, calendar on a day without fixtures. Competitions > Live standings (game) > the user's league
   (e.g. Serie A, 20 clubs). Note Napoli's row.
2. Select Napoli, set Home wins 2, Points 6, "Points from W/D/L" or type 6, "Apply to the game". Expect: toast
   "Standings: Napoli updated in the game", then within a frame (game_tick hook) the toast "Standings refresh: Napoli
   row: the game's standings view re-read N competitions (comp ids ...)" with N = the map's keys (typically 2-4) and
   the Status tab line `last: ok: ...`. `turbo_gui.log` has `game call standings_refresh(...)`.
3. Open the Office tile / the Standings screen (Office > Standings) **without advancing the calendar**: Napoli shows
   6 points, 2 wins and the position the sorted table gives it (the sort is FCE's: points, then the comp's
   tie-breakers). Close and reopen the screen: unchanged.
4. Turbo's view: "Reload" shows the same rows (FCE rows are the source for both).
5. Change a played result of Napoli (Change result): the Standings screen follows (both rows) after the next refresh
   toast; the Schedule / results screens show the new score.
6. Advance one day (no fixtures): nothing crashes; the screen keeps the edited values.
7. Play or sim the next match day: no crash; the SimDayManager's own request (§3.2) rebuilds the cache on top of the
   edited rows: the table shows the edit plus the new results.
8. Kill switch: create `turbo_output\call_standings_refresh_off.txt`, apply an edit: error toast "Standings refresh:
   kill switch ... present (the rows are written; ...)", the Standings screen shows the old numbers until a match
   day; delete the file, apply again: refreshed.
9. Fallback: with the kill switch off, from Live Editor's Lua console run `TurboStandingsRefresh()` right after
   loading a career (the map should be filled, so the per-key path runs); the text names the competitions. A
   career where the map is empty (observed: none yet) would report "the game's full standings refresh
   (POST_LOAD_PREPARE) was run instead".
10. Save, reload the career, open the Standings screen: report whether the edit survived (open point of
    `C3-live-standings.md` §6).

Things to watch in `turbo_gui.log`: a `validate` failure names the check (vtable / slot 10 / back-pointer /
slot 108 / ifce) and means the layout differs from this build: do not retry until it is understood; a `walk`
fallback with a reason other than "map is empty" means the rbtree layout differs; `dispatcher_failed` growing means
the call threw inside the game (HOOK_BODY counted it).
