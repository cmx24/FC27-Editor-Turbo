# Standings screen data path (FC 27, why an edited FCE row does not show)

Static analysis of `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (FC27.exe 1.0.140.64835, image base
0x140000000), 2026-10-03, with `scripts/re/rx.py` / `rx_jobs.py` / `sig_jobs.py`; **corrected 2026-10-04 after the crash
analysis of section 0 and verified in the live game (dev-service reads, two sessions)**. Confidence: [H] read in code or
in the live game, [M] strong inference, [L] guess. Companion of `C3-live-standings.md` and `standings-fixtures-notes.md`
(FCE side); this note covers the career-mode side between FCE and the UI.

## 0. The crash of 04-10-2026 01:26 (build a464cab) and what it corrected

### 0.1 Minidump (`CrashDumps\minidump-2026_10_04_01_26_18.dmp`, parsed read-only with python `minidump`)

* **Exception:** `0xC0000008 STATUS_INVALID_HANDLE`, flags `0x81` (software-originated, non-continuable), raised by
  `ntdll!RtlRaiseStatus` (ntdll+0x12ce0; ExceptionAddress ntdll+0x12d2f is the return from its `ZwRaiseException`). Not an
  access violation: nothing in FC27.exe or Turbo.dll faulted on a bad pointer.
* **Thread:** 4464, a job-pool thread running the frame body (Turbo's `game_tick` hook, which drained the queued call).
  The dump was written by the game's own handler on that thread (dbgcore frames below the raise). Thread 25584 (the one
  that queued, the render thread with the overlay) and 14328 (the thread recorded as "game thread", an earlier frame-body
  thread) were idle in `ZwWaitFor*`.
* **Stack of the raise (callee first; return addresses from the dump, ntdll names from the local ntdll's exports):**

  | frame | address | what |
  |---|---|---|
  | ntdll | +0x12d2f `RtlRaiseStatus(0xC0000008)` | `EXCEPTION_RECORD` at rsp+0x20 (`0x10480e270`), `CONTEXT` at rsp+0xC0 |
  | ntdll | +0xfe21, +0x1166f (internal: the contended wait of a critical section) | the wait on the "semaphore" failed |
  | ntdll | +0x128e2 `RtlEnterCriticalSection+0xf2` | called through the import at `0x14f05b148` |
  | FC27 | `0x147d9f6ec` = return of the `call [rip] RtlEnterCriticalSection` at `0x147d9f6e6` in `GetLiveStandingsCopy` 0x147d9f6b8 | `rcx = this + 0x280` |
  | FC27 | `0x147d9c7ff` in the user-comp copy helper 0x147d9c7d0 (`call 0x147d9f7f4` at 0x147d9c7fa) | |
  | FC27 | `0x147da101a` in the SVM career-event listener 0x147da0e10: the event-29 loop (`call 0x147d9c7d0` at 0x147da1015, after `0x147da5310(this, comp)` and `0x147da5104`) | comp `0x45E` = 1118 (the user's league) on the stack |
  | Turbo.dll | +0x2146e1 (the fallback `career_event(svm, 29, nullptr)` of `standings_refresh_win.cpp`) | the SVM vtable value `0x14B0160D8` sits in Turbo's locals |

  `this` of the whole chain = `0x6C4C78F0` (saved rdi of the listener; critical section `0x6C4C7B70` = this+0x280 in the
  ntdll frames).
* **What the critical section was:** the object at `0x6C4C78F0` carried vtable `0x14B0160D8` (Turbo's validate had
  accepted it). Its +0x280 is no `CRITICAL_SECTION`: in the live game the same class reads `LockCount 0x38547280`,
  `OwningThread 0x2C30312C08F0D180`, `LockSemaphore 0x34B6AC20` (section 0.3). `RtlEnterCriticalSection` sees
  `LockCount != -1` (owned), takes the contended path and waits on the garbage "semaphore": `STATUS_INVALID_HANDLE`,
  raised as a non-continuable exception, which `HOOK_BODY` (C++ `catch` only) cannot catch; the game's handler wrote the
  dump and FC27.exe exited.

### 0.2 Cause: the "SVM vtable" was the StaffManager's

The ctor named in the 2026-10-04 "re-verification" of section 2, **0x147d9a5c8, is the StaffManager constructor**: it
writes vtable `0x14B0160D8`, stores `ctx` at +0x08, zeroes +0x18..+0x58 and allocates 0xE0 bytes named
`"StaffManager::mScoutManagerData"` (stored at +0x120). The **StandingsViewManager constructor is the next function,
0x147d9a700**: `[rcx+8] = ctx`, `lea rax,[rip+0x19c431e]` at +0x13 -> **vtable 0x14975EA38**, 27 pending-tag bytes at
+0x10, 27 `{i32 -1, u64 0}` entries at +0x108, the `mLiveStandings` rbtree anchor at +0x250 (`+0`/`+8` = the anchor
itself, root +0x10 = 0, color +0x18, count +0x20 = 0), **`InitializeCriticalSectionAndSpinCount(this+0x280, 0x100)`**
(import 0x14f05b140), the second anchor at +0x380, +0x488 = 0, +0x490 = 0, +0x498..+0x4B0 = 0. Its callers
(0x147b01b40, 0x147f188c4) allocate **0x4B8** bytes and store the object in **slot 108** of the manager table
(`holder = [table+0xD98]`, `holder[[table+0xD90]] = obj`), exactly Live Editor's type id. [H]

Turbo (commit cc50c23 / a464cab) therefore:
1. refused the real SVM that the Lua side published from slot 108 ("vtable 0x14975EA38, expected 0x14B0160D8", log
   00:55:02), and at 01:26 located "the SVM" by searching the table for vtable 0x14B0160D8: **slot 107, the
   StaffManager** (its +0x08 is `ctx` like every manager, and `0x14B0160D8+0x50` = 0x147da3ae8 matched the "slot 10"
   anchor, which is a StaffManager method, section 2.1);
2. walked `staff+0x250` as the map: not a map, so the walk failed and the **fallback** ran
   `SVM::OnCareerEvent(staff, 29, nullptr)`;
3. the listener's event-29 path sent the sync request for comp 1118 (harmless: 0x147da5310 only reads
   `[this+8]` = ctx), `RequestScheduleGetFixtures`, then the user-comp copy entered the "critical section" at
   staff+0x280.

Everything in the a464cab validation that "passed" was derived from the wrong vtable. The vtable scan over the table
(`locate` trying every slot) is what turned a wrong constant into a call on the wrong object: a refusal would have been
the right outcome.

### 0.3 Live verification (dev service, 04-10-2026, two career sessions)

| what | session 1 (table 0x6E41BE00) | session 2 (table 0x6B90F850) |
|---|---|---|
| slot 108 (type 0x6C) object | 0x3B6820070, vtable **0x14975EA38**, +8 == table | 0x68500F80, same |
| its map +0x250 | right = left = root = one node, count 1; node parent == anchor, key **1118**, value = LiveStandings (vtable 0x14B016148, +8 data) | count 1 |
| its +0x280 | DebugInfo -1, **LockCount -1, RecursionCount 0, OwningThread 0, LockSemaphore 0, SpinCount 0x100** | same |
| +0x488 / +0x490 | LiveStandings copy / 1 | / 1 |
| slot 107 (type 0x6B) object | 0x3E8C943D0, vtable **0x14B0160D8** (StaffManager); +0x280 bytes: LockCount 0x38547280, LockSemaphore 0x34B6AC20 | 0x3043DC180, vtable 0x14B0160D8 |
| slot 1 | IFCEInterface 0x66FFA840 == `bridge_state.json ifce`, vtable 0x14B180DF0, **slot 4 = 0x148A35D3C** (`fce_iface_post`) | |
| allocator global `[0x14C269EA8]` | 0x60C9AF0, vtable 0x14970F668, slot 2 = 0x142F95928 | |
| SimDayManager (slot 103, type 0x67) | | 0x3B553E9D0, **+0x14 = 0** in the hub (state machine 0x147da6e58: 0 = nothing to do, 1..9 = day processing) |
| `[[comm+0x20]+0x10]` | == the published table | |
| objects with the SVM vtable (writable scan) | one (a second hit was a transient pattern, not an object) | |

### 0.4 What changed in Turbo (track E7)

* `svm_vtable` = the SVM ctor 0x147d9a700 (rip +0x13 -> 0x14975EA38); built-in RVA 0x975EA38. `svm_slot10` removed.
* The SVM is **only** the object of slot 108; `locate` never searches other slots. A vtable mismatch is reported.
* The vtable's slot 1 must be `svm_listener` (0x147da0e10): the vtable and the function identify each other.
* Before any call: +0x280 must be an initialised, free critical section; the request path of 0x147da5310 is checked
  (`[[ctx+0x38]]` = the interface Turbo wrote to, its vtable slot 4 = `fce_iface_post`, the allocator global
  `svm_allocator` holds an object with an allocate function; function pointers inside the image); every map value must
  be a LiveStandings (vtable 0xB016148); the SimDayManager state must be 0; one call in flight (one-shot gate).
* The fallback through the listener is opt-in (`turbo_output\call_standings_refresh_full.txt`); an empty or
  inconsistent map is reported and nothing is called.

## 0b. Answer

1. **The Standings screen and the Office tile do not ask FCE.** They read a cached copy kept by the career manager
   `FCECareerModeStandingsViewManager` (SVM, Live Editor type id 108): `StandingsViewManager::mLiveStandings`, an
   `eastl::map<int compObjId, LiveStandings*>` at `svm+0x250` (critical section at `svm+0x280`). Readers copy
   `mLiveStandings[comp]` out through `GetLiveStandingsCopy` 0x147d9f6b8 (section 2.1). Nothing on the screen-open path
   sends `FCEI::RequestGetStandings`. [H]
2. **The cache is rebuilt only when a `ResponseStandingsList` with tag `'rmvs'` (type 0x12) or a type-0x13 response
   reaches the SVM** (`StandingsViewManager::HandleFCEMessage` 0x147da1240 -> cache writer 0x147da6f04). The game
   sends those on career events 22 MAIN_COMPETITION_SCHEDULED / 23 SEASON_RESET / 29 POST_LOAD_PREPARE /
   33 COMPETITION_COMPLETE (SVM listener 0x147da0e10) and from the SimDayManager's day-processing state machine
   (`RequestStandingsCheckEarlyResults`, type 0x2c -> type-0x13 response). Opening a screen or advancing a day
   without a match day does none of that, so Turbo's row edit (correct, in the right DataManager) stayed invisible. [H]
3. **There is one FCE instance.** `FCE::FCEInterfaceImpl` is created once (0x148a040c8) and published in the service
   registry (0x0a613b9a); the career's own handle (`ctx+0x38`, type id 1 `IFCEInterface`) is fetched from that
   registry (0x1489386f8 / 0x147c15b9c). One ManagerHub, one DataManager. The object Turbo wrote to is the one the
   handlers read (and the live game shows `[[ctx+0x38]]` == `bridge_state.json ifce`). [H]
4. **What Turbo does:** after writing rows, on the game thread, for every key `k` of the SVM map,
   `0x147da5310(svm, k)` (synchronous `RequestGetStandings`, tag `'rmvs'`, immediate), with the object and every word
   on the request path validated first (section 5). FCE answers inside the call (mailbox dispatch is synchronous), the
   SVM replaces `mLiveStandings[k]` with a fresh clone of the live rows and posts the `LiveTableUpdate` event (0x75)
   that the UI listens to. [H for the mechanism; the in-game run is still to do, section 10]

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

Vtable **`base+0x975EA38`** [H, live-verified: the SVM constructor **0x147d9a700** stores `ctx` at +0x08 and
`lea rax,[rip+..]` -> 0x14975EA38 at +0x13; its deleting destructor is slot 0 = 0x14260ffac (which writes the same vtable
on entry). Slot 1 = 0x147da0e10 = `OnCareerEvent`, slot 10 = 0x142b2b858 (`ret 0`). **Not** `base+0xB0160D8`: that is the
StaffManager (ctor 0x147d9a5c8, dtor 0x147d9af90, slot 107), see section 0.2]. Allocation size 0x4B8. Every function
Turbo calls takes the slot-108 object as `this`: the listener's event-29 path writes `[this+0x490]`, calls
0x147da5310(this, comp) and reads `[this+0x488]`; the cache writer uses `this+0x250` / `+0x280` / `+0x490` [H].

| offset | content | evidence |
|---|---|---|
| +0x00 | vtable 0x14975EA38 | ctor 0x147d9a713 |
| +0x08 | ctx (manager table) | ctor 0x147d9a70f, every function |
| +0x10 + idx | u8 "request pending" per known tag (27 tags, table in 0x147d9f4a4; **+0x18 is tag index 8 'mmbj', not an enable flag**) | 0x147da53ce; ctor loop 0x147d9a809 |
| +0x30 + idx*8 | `mStandings[idx]`: cached response per known tag (27 pointers, +0x30..+0x108; not written by the ctor, zero from the allocation) | 0x147da17ed |
| +0x108 + 12*i | 27 entries `{i32 -1, u64 0}` per tag (array constructed by 0x14071d420(this+0x108, 12, 27, ctor)) | ctor 0x147d9a737, loop 0x147d9a802..0x147d9a823 |
| +0x250 | `mLiveStandings`: eastl rbtree anchor (+0 rightmost, +8 leftmost: the anchor itself when empty; +0x10 root = svm+0x260; +0x18 color; **+0x270 u32 node count**); node: +0 right, +8 left, +0x10 parent, +0x18 color, +0x20 int key compObjId, +0x28 LiveStandings* | ctor 0x147d9a73e..0x147d9a778, 0x147d9f6f3..0x147d9f764, insert helper 0x147d9d4c4; live: one node, key 1118, parent == anchor |
| +0x280 | CRITICAL_SECTION for the map (`InitializeCriticalSectionAndSpinCount(cs, 0x100)`) | ctor 0x147d9a77b; 0x147da6f32 / 0x147d9f6e6; live: LockCount -1, no owner |
| +0x380 | second map, type-0x10 'rmvs' responses (same anchor layout) | ctor 0x147d9a787, 0x147da182b |
| +0x3b0 + idx*8 | "StageInfo" per tag (type 0x17 responses) | ctor 0x147d9a825, 0x147da16c5 |
| +0x488 | LiveStandings* copy of the user's competition (written by the cache writer when null and by the refresh) | ctor; 0x147da7108, 0x147da1008; live: set |
| +0x490 | u8 "LiveTableFirstUpdate posted" | 0x147da70a9; live: 1 |
| +0x498..+0x4B0 | zeroed by the ctor | 0x147d9a7ce |

`LiveStandings` = 0x10 bytes: `+0` vtable `base+0xB016148`, `+8` deep copy of the response's standings data
(0x144040198 allocates, 0x14403fc30 copies; the rows are the sorted `FCEI::StandingObject`s of
`standings-fixtures-notes.md` §2). Allocated under the name "StandingsViewManager::mLiveStandings". [H, live-verified]

Known request tags (index -> tag, 0x147d9f4a4): 0 hmcs, 1 pmcc, 2 port, 3 tlcl, 4 rmos, 5 amrt, 6 mmoc, 7 moca,
8 mmbj, 9 trpg, 10 eert, 11 moci, 12 qrmt, 13 rots, 14 tuet, 15 adst, 16 oadc, 17 clcu, 18 ccle, 19 swen, 20 mrlp,
21 mptt, 22 vhlg, 23 ctbl, 24 cdus, 25 clce, 26 2vlg. `'rmvs'` (0x73766d72) is deliberately **not** in the table.

### 2.1 Readers (the UI side)

* `GetLiveStandingsCopy` 0x147d9f6b8(svm, allocator, compObjId): EnterCriticalSection, find `map[compObjId]`, clone
  into a new `LiveStandings`, LeaveCriticalSection; returns 0 when the key is absent. **No request on a miss.** [H]
  Wrappers: 0x147d9caac(out, svm, comp), 0x147d9c6e4 (A), 0x147d9c7d0 (B: comp = user's league via 0x147abd100,
  or the team's comp via 0x147c9d264/0x147c9d20c on the ActiveCompetitionsManager), 0x147d9cadc, 0x147d9cb44.
* **Standings screen feed, corrected:** 0x147da3ae8(this, arg) is **slot 10 of the StaffManager vtable 0x14B0160D8**
  (`cmp byte [rcx+0x18], 0` is the StaffManager's flag). It fetches the SVM itself: `ctx = [this+8]`,
  `svm = [[ctx+0xd98]]` (slot 108), `comp = [[ctx+0x3b8]]+0x9c8` (the competition selected on the screen), then
  `0x147d9caac(&out, svm, comp)` -> `GetLiveStandingsCopy` -> if found and comp != -1 -> `0x14892ec20(copy)` (career UI
  layer) -> `0x147da3b70`. The UI action object 0x144f1984c does `ctx = [[0x14c2a6c90]]->vt[0x180]()`, then
  `[[ctx+0xd98]]->vt[0x50]()`: on the SVM's own vtable slot 10 is `ret 0` (0x142b2b858), so that `vt[0x180]()` table is
  not the career manager table, or the call is a no-op; the only confirmed screen feed is the StaffManager method above.
  [H for the code; M that it is the screen's only path]
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
managers = *(*(comm + 0x20) + 0x10)                      // career manager table = "ctx" (live: == the published table)
slot     = managers + 0x20 * 108                         // ENUM_FCEGameModesFCECareerModeStandingsViewManager
require  *(int*)(slot + 0x10) == 1
svm      = **(void***)(slot + 0x18)
require  *(uint64*)svm == game_base + 0x975EA38          // the SVM vtable (ctor 0x147d9a700); never search other slots
require  *(uint64*)(*svm + 8) == svm_listener            // vtable slot 1 = OnCareerEvent 0x147da0e10
require  *(uint64*)(svm + 8) == managers
require  readable to svm + 0x4B8
require  critical section at svm + 0x280: LockCount == -1, RecursionCount == 0, OwningThread == 0
```
The Lua side publishes slot 108 as `svm` (`mem.manager(108)`) next to `ifce` and `managers`; Turbo re-locates slot 108
itself and refuses a published `svm` that differs (stale `bridge_state.json`). `ifce` must equal
`**(void***)(managers + 0x38)` (type id 1), which is also the object 0x147da5310 posts through.

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
  re-requests fixtures (`RequestScheduleGetFixtures`), rebuilds `+0x488` and the 'pmcc' data, then (sil = 1 for event
  29 as for 23) re-requests every ActiveCompetitionsManager entry through 0x147d9b250. Requires a club career
  (`user entry +0x30c > 0`, read through `[[ctx+0x1038]]`); the event pointer is not read on this path. Heavier, but it
  is the game's own load-time sequence. **Opt-in only** (`turbo_output\call_standings_refresh_full.txt`) and only when
  the map is empty: this is the call that crashed on the StaffManager (section 0), so it runs solely on an object that
  passed every check of 5.1 and 5.2.
* Only the user's league: `k = svm_user_comp_id(svm)` (0x147abd100) then one `0x147da5310` call.
* Do **not** patch the cached `LiveStandings` clones: the writer replaces them wholesale and they are re-sorted
  by FCE anyway.

What `0x147da5310(svm, comp)` dereferences (the complete list, section 0.2 disassembly): `comp == -1` -> return;
`rax = [svm+8]` (ctx); `rcx = [rax+0x38]` (holder of slot 1); `rdi = [rcx]` (IFCEInterface); `alloc = [0x14C269EA8]`,
`[[alloc]+0x10](alloc, 0x70, "FCEI::RequestGetStandings", 0)` (a null allocation would be written to: the game assumes
success); ctor 0x144036e6c; `req+0x6C = 1`, `req+0x20 = comp`, `req+0x18 = 'rmvs'`; `[[rdi]+0x20](rdi, req)` = Post.
`[svm]` is never read, the key is only stored. Turbo checks each of these words before the call (`validate_request_path`
in `core/standings_refresh.cpp`: holder, interface == the one edited, vtable slot 4 == `fce_iface_post` 0x148a35d3c,
allocator object with an allocate function, function pointers inside the image).

Constraints: run on the game thread only (the frame-body job: Turbo's `game_tick` hook, which migrates between job-pool
threads like the career events themselves); never during a match day: the SimDayManager (slot 103) state at +0x14
must be 0 (1..9 = its day-processing state machine 0x147da6e58, which sends its own standings requests); the functions
use the game allocator global (`base+0xC269EA8`) and the career managers, so they need a loaded career
(`svm+0x08 == managers`, `*(managers+0x20*108+0x10) == 1`). Each call returns after the synchronous dispatch;
nothing to wait for. A request posted while the FCE request server is already dispatching is queued instead of
answered at once (server+0x170 re-entrancy byte): the hook runs after the frame body, outside any dispatch.

### 5.3 In-game verification

1. Edit a row in Turbo (points +10), Apply -> Turbo queues the refresh -> open Standings: position and points follow.
2. Office tile: after the refresh (LiveTableUpdate) or after re-entering the hub.
3. Play / sim a match day afterwards: the SimDayManager's 0x2c request rebuilds the cache on top of the edited row.

## 6. Signatures (unique in `.text1` unless noted; `scripts/re/check_signatures.py` checks the `ui_path` block of `C3-signatures.json`)

| name | va | signature |
|---|---|---|
| `svm_request_standings_sync_rmvs` (call this) | 0x147da5310 | `83 FA FF 74 65 48 89 5C 24 08 57 48 83 EC 20 48 8B 41 08 4C 8D 05 ?? ?? ?? ??` |
| `svm_on_career_event` (full refresh, opt-in) | 0x147da0e10 | `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 F8 49 8B D8` |
| `svm_ctor` (vtable: rip at +0x13 -> 0x14975EA38) | 0x147d9a700 | `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 89 51 08 48 8D 05 ?? ?? ?? ?? BA 0C 00 00 00 48 89 01 48 8B D9 4C 8D 0D ?? ?? ?? ?? 48 81 C1 08 01 00 00` |
| `fce_iface_post` (FCEInterfaceImpl slot 4) | 0x148a35d3c | `40 53 48 83 EC 20 48 8B 01 48 8B DA FF 50 40 8B 53 10 4C 8B C3 48 8B C8` |
| `svm_user_comp_id` | 0x147abd100 | `48 83 EC 28 4C 8B C9 E8 ?? ?? ?? ?? 80 79 2C 00 44 8B 90 08 03 00 00 74 30` |
| `svm_user_comp_list` | 0x147d9b6b0 | `48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 40 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 30 48 8B 41 08` |
| `svm_request_standings_sync_rmvs_b` | 0x147d9b250 | `48 89 5C 24 08 57 48 83 EC 20 48 8B 01 4C 8D 05 ?? ?? ?? ?? 45 33 C9 8B FA` |
| `svm_request_standings_mmbj` | 0x147da53a8 | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 8B FA 48 8B D9 BA 6D 6D 62 6A` |
| `svm_cache_writer` (hook point to observe refreshes) | 0x147da6f04 | `48 89 5C 24 10 48 89 6C 24 20 44 89 44 24 18 56 57 41 54 41 56 41 57 48 83 EC 30 4C 8D A1 80 02 00 00` |
| `svm_get_live_standings_copy` | 0x147d9f6b8 | `48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 48 8D A9 80 02 00 00` |
| `svm_handle_fce_message` | 0x147da1240 | `4D 85 C0 0F 84 ?? ?? ?? ?? 48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9` |
| `staff_slot10_standings_feed` (StaffManager slot 10, was "svm_slot10") | 0x147da3ae8 | `48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 80 79 18 00 48 8B EA 48 8B F9` |
| `svm_check_early_results` (SimDayManager) | 0x147da67b0 | `40 53 48 83 EC 30 48 8B 41 08 48 8B D9 48 8B 90 58 07 00 00 48 8B 02 48 8B 50 08` |
| `fcei_request_get_standings_ctor` | 0x144036e6c | `48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8D 05 ?? ?? ?? ?? 48 89 01 33 C0 89 41 08 87 41 08 48 8D 0D ?? ?? ?? ?? 48 C7 47 10 30 00 00 00` |
| `fce_response_type13_builder` | 0x148a541d8 | `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B 41 18` |

`svm_get_user_comp_copy_B` 0x147d9c7d0 has a twin (0x147d9c6e4) with identical code: not signable, not needed.
Vtables (RVA): **SVM 0x975EA38**, StaffManager 0xB0160D8, LiveStandings 0xB016148, IFCEInterface 0xB180DF0, mailbox
server 0x972FB80; game allocator global 0xC269EA8 (read by 0x147da5310 at +0x2A: `mov rcx,[rip+0x44c4b67]`). The
0x975EA28 named earlier as a "listener base vtable" is the end of the previous vtable in .rdata (0x14975EA38 - 0x10); the
SVM's listener interface is its own vtable slot 1.

Turbo.dll's built-in table (`turbogui/src/core/sigscan.cpp`):

| name | va | resolve |
|---|---|---|
| `svm_refresh_comp` (= `svm_request_standings_sync_rmvs`) | 0x147da5310 | none |
| `svm_listener` (= `svm_on_career_event`) | 0x147da0e10 | none |
| `svm_vtable` (= `svm_ctor`, rip +0x13) | 0x147d9a700 -> 0x14975EA38 | rip |
| `svm_allocator` (the `svm_refresh_comp` pattern, rip +0x2A) | 0x147da5310 -> 0x14C269EA8 | rip |
| `fce_iface_post` | 0x148a35d3c | none |

## 7. Key addresses

| addr | what |
|---|---|
| 0x147da1240 | SVM::HandleFCEMessage (types 0x10/0x12/0x13/0x17/0x23/0x27) |
| 0x147da6f04 | cache writer: map insert + LiveTableFirstUpdate (0x5d) / LiveTableUpdate (0x75) |
| 0x147d9f6b8 | GetLiveStandingsCopy (map read + clone) |
| 0x147da3ae8 | StaffManager slot 10: Standings screen feed ([[ctx+0xd98]] map -> 0x14892ec20) |
| 0x144f1984c | UI action: `[[0x14c2a6c90]]->vt[0x180]()`, `[[+0xd98]]->vt[0x50]()` (on the SVM itself slot 10 is `ret 0`) |
| 0x147d9a700 / 0x14260ffac | SVM constructor / deleting destructor (vtable 0x14975EA38, 0x4B8 bytes, slot 108) |
| 0x147d9a5c8 / 0x147d9af90 | StaffManager constructor / destructor (vtable 0x14B0160D8, slot 107) |
| 0x147b01b40 / 0x147f188c4 | manager factories: allocate 0x4B8, call the SVM ctor, store in slot 108 |
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
* The global 0x14c2a6c90 whose `vt[0x180]` is called by the UI action 0x144f1984c: on the SVM itself slot 10 is `ret 0`,
  so either that table is not the career manager table or the action is a no-op on this build. Not needed by Turbo. [L]
* Which career event, if any, fires on a plain day advance without fixtures was not traced; the observed behaviour
  (no refresh on 1 -> 2 July) matches the trigger list above.
* Persistence of the FCE rows across save/load is still the open item of `C3-live-standings.md` §6.
* How the game reaches 0x147da0e10: it is slot 1 of the SVM vtable 0x14975EA38 (the earlier "base vtable 0x975EA28
  with a this+0x10 thunk" reading was an artefact of the wrong vtable). The dispatch mechanism itself is not traced;
  the event-29 path reads no event field, event 23 reads `event+0x30`, event 33 reads `event+0x18/+0x1c/+0x28/+0x30`,
  so only 29 may be called with a null event. [H for the reads]
* Turbo's `HOOK_BODY` catches C++ exceptions only: an SEH exception inside a game call (as in section 0) still ends the
  process. The MinGW build has no `__try`; the defence is the validation of every dereference before the call.
* The 0x147da5310 request path assumes the allocator returns memory (the game writes `[rdx+0x6c]` even when `rdx` is
  0); Turbo checks the allocator object exists but cannot pre-empt an out-of-memory allocation. [L]

## 9. Implementation in Turbo (track E5-standings-refresh 2026-10-04, corrected by track E7-standings-crash)

**Lua** (`turbo/package/lua/libs/v2/imports/turbo/bridge.lua`, `core/mem.lua`): `bridge_state.json` carries `svm`
(`mem.manager(108)`, which is the right object) and `managers` (`mem.manager_table()` = `[[comm+0x20]+0x10]`) next to
`ifce`, both `0x0` outside a career or without the GUI's memory map; a change of either re-publishes the state. The
native `TurboStandingsRefresh()` (installed with `TurboJobOfferCreate`) sends mailbox call op 2 with
`{svm, managers, comm, ifce}`. Unchanged by E7.

**Turbo.dll** (`turbogui/src/core/standings_refresh.{h,cpp}`, `src/win/standings_refresh_win.{h,cpp}`; signatures
`svm_refresh_comp`, `svm_listener`, `svm_vtable`, `svm_allocator`, `fce_iface_post` in `core/sigscan.cpp`):

1. Locate: the manager table (published, else `[[comm+0x20]+0x10]`), then **slot 108 only** (`locate`); the object must
   carry the SVM vtable (`svm_vtable`, fallback RVA 0x975EA38 + image base). A published `svm` that is not that object
   is refused as stale. No other slot is ever searched (section 0.2).
2. Validate (`validate`): pointer-shaped, readable to +0x4B8, vtable, **vtable slot 1 == `svm_listener`**, `svm+0x08` ==
   the table, slot 108 holds the object, the critical section at +0x280 is initialised and free (LockCount -1,
   RecursionCount 0, OwningThread 0).
3. Validate the request path (`validate_request_path`): `[[ctx+0x38]]` is the FCE interface Turbo wrote to (published
   `ifce`, else the slot-1 object), its vtable slot 4 is `fce_iface_post`, the allocator global (`svm_allocator`) holds
   an object whose vtable slot 2 is a function; function pointers must lie inside FC27.exe (`image_base` + `image_size`).
4. Busy check (`sim_busy`): the SimDayManager (slot 103) state at +0x14 must be 0; a missing manager counts as busy.
5. Walk `mLiveStandings`: bounded in-order walk from the root at svm+0x260 (explicit stack, at most 64 nodes), every
   node's parent link checked against the node it was reached from (root: the anchor svm+0x250), keys strictly
   ascending, every value a LiveStandings (vtable RVA 0xB016148), the walked count equal to the u32 at svm+0x270. Any
   inconsistency stops the call: stage `walk`, nothing is called.
6. Refresh: `svm_refresh_comp(svm, key)` for every key, in key order (§5.2). A failing call stops the sequence.
7. Empty map: reported (stage `walk`). With `turbo_output\call_standings_refresh_full.txt` present the game's own
   load-time refresh `svm_listener(svm, 29, nullptr)` runs instead (`Request::allow_fallback`), once.
8. One-shot gate (`OneShotGate`, host `g_gate`): a request (the UI after a successful `fce::write_row` /
   `fce::edit_result`, or Lua's explicit `TurboStandingsRefresh()`) arms and takes the gate; while that run is queued
   or running every further request is refused (stage `busy`, counted as `refused` in the Status tab).
9. Threading: `App::standings_refresh` (the host's `RefreshService`) -> `run_on_game_thread` (game_tick hook, else the
   Lua pump); a call from the game thread itself runs at once. Outcomes are polled by `App::tick` (toast "Standings
   refresh: ..." + log + the "Standings view:" line of the Live standings view) and listed in the Status tab under
   "Game calls". Kill switch `turbo_output\call_standings_refresh_off.txt`; every game-hook switch applies too.

Tests (`turbogui/tests/native/test_main.cpp`, `turbo/tests/t07_bridge.lua`, `t12_job_offer.lua`): synthetic manager
table with the SVM in slot 108 (real vtable, free critical section, LiveStandings values), the FCE interface in slot 1
(vtable slot 4 = Post), the allocator global, the SimDayManager in slot 103 and **the StaffManager in slot 107 with the
live garbage at +0x280**; the crash scenario (StaffManager published, in slot 108, without any vtable anchor) is refused
on every path and nothing is called; every refusal (vtable, listener slot, back-pointer, slot 108, stale published
object, owned / garbage critical section, interface mismatch, Post mismatch / outside the image, broken holder chain,
allocator missing / outside the image, sim state, missing SimDayManager, unmapped / cut-off objects, missing functions);
every walk inconsistency (parent link, order, size counter, bad / unmapped node, value vtable / unmapped value, cycle,
bound) reported with nothing called; the opt-in fallback once and never on an inconsistent map; a failing sync call;
the one-shot gate; the five signatures against the image bytes (vtable via the SVM ctor's lea, allocator via the rip
operand); and the UI driver case (a row write queues the request with the published addresses, outcome as a toast).

## 10. In-game test plan (E7: not run yet; the a464cab run crashed, section 0)

Preconditions: Turbo 0.4.x with track E7, Live Editor v27.1.2, a Manager Career loaded, Turbo GUI running (F8),
Status tab > Game calls shows `standings_refresh: ready` with the five addresses (0x...DA5310, 0x...DA0E10,
**0x...975EA38**, 0x...8A35D3C, 0x...C269EA8 on 1.0.140.64835) and `bridge_state.json` carries non-zero `svm` /
`managers`. Keep `turbo_output\call_standings_refresh_off.txt` until step 0 passes.

0. **Dry validation with the kill switch on**: apply an edit; expect the toast "Standings refresh: kill switch ...". Then
   remove the kill switch and, from Live Editor's Lua console, run `TurboStandingsRefresh()` once with the game in the
   hub: expect in `turbo_gui.log` the line `game call standings_refresh(svm 0x..., managers 0x..., ...)` with `ok [done]
   Lua: the game's standings view re-read 1 competition (comp ids 1118)` (or the comp ids of the career). A `failed
   [validate]` line names the check that did not pass: stop and read it; nothing was called in that case.
1. Office hub, calendar on a day without fixtures. Competitions > Live standings (game) > the user's league
   (e.g. Serie A, 20 clubs). Note Napoli's row.
2. Select Napoli, set Home wins 2, Points 6, "Points from W/D/L" or type 6, "Apply to the game". Expect: toast
   "Standings: Napoli updated in the game", then within a frame (game_tick hook) the toast "Standings refresh: Napoli
   row: the game's standings view re-read N competitions (comp ids ...)" with N = the map's keys (1 in the sessions
   probed: the user's league 1118) and the Status tab line `last: ok: ...`.
3. Open the Office tile / the Standings screen (Office > Standings) **without advancing the calendar**: Napoli shows
   6 points, 2 wins and the position the sorted table gives it. Close and reopen the screen: unchanged.
4. Turbo's view: "Reload" shows the same rows (FCE rows are the source for both).
5. Apply a second edit at once: the first refresh has finished (one frame), so the second runs too; the Status tab
   `refused` counter stays 0. Click Apply twice within the same frame (or run `TurboStandingsRefresh()` twice from Lua
   in one line): the second is refused with "a standings refresh is still running".
6. Change a played result of Napoli (Change result): the Standings screen follows (both rows) after the next refresh
   toast; the Schedule / results screens show the new score.
7. Advance one day (no fixtures): nothing crashes; the screen keeps the edited values.
8. Sim To Date over a match day: while the simulation runs (SimDayManager state != 0) an edit's refresh is refused with
   "the game is processing a match day"; afterwards the SimDayManager's own request (§3.2) has rebuilt the cache on top
   of the edited rows: the table shows the edit plus the new results. Apply again in the hub: refreshed.
9. Fallback: with `turbo_output\call_standings_refresh_full.txt` present and an empty map (observed: none yet) the text
   says "the game's full standings refresh (POST_LOAD_PREPARE) was run instead"; without the file an empty map reads
   "nothing to re-request ... opt-in". With the map filled the file changes nothing.
10. Save, reload the career, open the Standings screen: report whether the edit survived (open point of
    `C3-live-standings.md` §6).

Things to watch in `turbo_gui.log`: a `validate` failure names the check (vtable / slot 1 / back-pointer / slot 108 /
stale / critical section / interface / Post / allocator) and means the layout differs from this build: do not retry
until it is understood; a `walk` failure with a reason other than "map is empty" means the rbtree layout differs;
`busy` means the SimDayManager was processing or a refresh was in flight; `dispatcher_failed` growing means a C++
exception inside the call (an SEH exception would still end the process, section 8).
