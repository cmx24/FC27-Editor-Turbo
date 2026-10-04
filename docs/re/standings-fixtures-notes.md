# FC 27 FCE standings and fixtures: static RE notes (preserved copy)

> **Provenance.** Written by the Turbo build session `cse_018KNHBkXKaKC6RHBU17Afhu` at 2026-10-03 16:55:30 UTC to
> `/home/claude/re/notes_standings.md` in that session's cloud container. That container is the only place the original
> existed, so this is a verbatim copy recovered from the session transcript. Nothing below was run by the person who
> copied it; the confidence tags `[H]` / `[M]` / `[L]` are the original author's.
>
> **Scope and limits.**
> - Addresses are for the one game image that was dumped on 2026-10-03 (game build as installed that day; see
>   `docs/HANDOVER-0.3.0.md`). Re-derive them with the signatures in section 5 before trusting them on another build.
> - The analysis was static only: it was not run in the game, and persistence of edits across a save was not checked.
> - Offline Career Mode only. See the ground-rules section of `docs/HANDOVER-0.3.0.md` (there is an open question
>   about whether disassembling the game is permitted under the project's original rules).

---

# FC 27 – FCE standings & fixtures (static RE notes)

Source: /home/claude/re/fc27.bin (base 0x140000000). Everything below comes from static disassembly only.
Nothing here was run in the game. Confidence tags: [H] = read directly from code, [M] = strong inference, [L] = guess.

## 0. Summary

* The live table rows and fixtures are in the FCE `DataManager` lists, and **FC 27 keeps the FC 26 offsets**:
  `DataManager+0x60 = FixtureDataList` and `DataManager+0x88 = StandingsDataList` [H].
  The containers changed shape, though. Standings is now an eastl::vector (begin/end/cap) plus a
  hash index (compObjId -> standing ids). Fixtures is `{int32 count; T* data}`. Code that assumes the old
  container layout reads garbage, which may be why "+0x60/+0x88 hold other things". Another possible cause
  is starting from the hub's `+0x18`, which holds the **DataConnector**, not the DataManager.
* The Standings screen (FCEI::RequestGetStandings, type 0x30) reads the standings rows
  **live from `DataManager+0x88` on every request**. It also sorts them on every request (DataSorter), so
  table position is not stored [H].
* Standings are **incremental**. A match result (RequestUpdateMatchResult, type 0x2a) adds W/D/L, goals and
  points to the two rows. Nothing recomputes the table from the fixtures [H].
  If you edit a fixture score, the table does not move. You must patch both the fixture and the two rows.

## 1. Object graph / pointer chain

```
[base+0xC2A8590]  ServiceRegistry*            (global; rip-ref in 0x148a0412d / 0x148935308)
   registry->vfunc(+0x30)(id=0x0A613B9A, obj)  = Register   (0x148a0413c)            [H]
   registry->vfunc(+0x38)(id=0x0A613B9A)       = Unregister (0x148935317, in dtor)   [M]
   registry->vfunc(+0x40)(id=0x0A613B9A)       = Get       (0x147c15bb3)            [M]
     -> FCE::FCEInterfaceImpl (0x20 bytes, ctor 0x148a16b20)
          +0x00 vtable  base+0xB180DF0   (slot3 0x148a198a8 = QueryInterface: 0xA613B9B/0xEE3F516E -> this)
          +0x08 vtable2 base+0xB180BE8
          +0x18 ManagerHub*                                          [H] (0x148a16c46)
ManagerHub (0x90 bytes, vtable base+0xB180C28, Init = 0x148a2ee24)
          +0x08 FCEI::MailBox*                                       [H]
          +0x10 mManagerList obj ; vector<Manager*> at +0x08(begin)/+0x10(end)   [H]
                 [0]=DataManager [1]=Advancement [2]=CompObj [3]=Debug [4]=Scheduling
                 [5]=Sim [6]=Standings [7]=Statistics             [H] (order of push in Init)
          +0x18 DataConnector*   (= DataManager+0x28)                [H]
DataConnector (0x108 bytes, ctor 0x148a164dc)
          +0x80 DataManager*                                         [H]
          +0x88 DataObjectCompStructure* (competition tree)          [H]
DataManager (0xC0 bytes, vtable base+0xB180CA8, ctor 0x148a1661c)    [H]
          +0x28 DataConnector*
          +0x30 VersionDataList     +0x38 ActiveTeamDataList  +0x40 AdvancementDataList
          +0x48 CompAverageDataList +0x50 CompObjectDataList  +0x58 CompTeamDataList
          +0x60 FixtureDataList     +0x68 SchedulingDataList  +0x70 ScriptFuncDataList
          +0x78 SettingsDataList    +0x80 StageAdvDataList    +0x88 StandingsDataList
          +0x90 StatisticsDataList  +0x98 WeatherDataList     +0xA0 ObjectiveDataList
          +0xA8 StadiumDataList     +0xB0 DeepSimCompDataList
```
Every FCE manager has `+0x18 = ManagerHub*` and `+0x20 = type` (base ctor 0x148a16cac) [H].
The managers use `mgr->hub(+0x18)->DataConnector(+0x18)->DataManager(+0x80)`. Example: 0x148a5bc3e..0x148a5bc83.

Chain: `DM = *(*(*(FCEInterfaceImpl+0x18)+0x18)+0x80)`; `rows = *(DM+0x88)`; `fixtures = *(DM+0x60)`.

## 2. Standings rows

`StandingsDataList` (0x58 bytes, allocated at 0x148a168d4):
```
+0x00 StandingData* begin      [H] (0x148a2d1de)
+0x08 StandingData* end        [H]
+0x10 cap  (+0x18/+0x20 allocator etc.)
+0x28 hash buckets (node**)    [H] (0x148a26dcd, 0x148a2ec6a)
+0x30 bucket count             [H]   (init: buckets=&gEmptyBucketArray 0x14bcad848, count=1, load 1.0f/2.0f)
   node: +0x00 int key = compObjId (group/stage node id) ; +0x08/+0x10 vector<uint16> standing ids ; +0x28 next
```
`GetItem(id)` = 0x148a2d1d4: `begin + id*0x18` with a bounds check on (end-begin)/0x18. So **standing id == vector index** [H].

StandingData, 0x18 bytes (reflection visitor 0x148a0d230 gives the names; the reads/writes in the code match them) [H]:
| off | type | field |
|---|---|---|
| 0x00 | u16 | mId (standing id) |
| 0x02 | u16 | mCompObjId (the leaf group/stage comp object, not necessarily the league id) [M] |
| 0x04 | u32 | mTeamId |
| 0x08 | u8  | mTeamIndex |
| 0x09 | u8  | mHomeWins |
| 0x0A | u8  | mHomeDraws |
| 0x0B | u8  | mHomeLosses |
| 0x0C | u8  | mHomeGoalsFor |
| 0x0D | u8  | mHomeGoalsAgainst |
| 0x0E | u8  | mAwayWins |
| 0x0F | u8  | mAwayDraws |
| 0x10 | u8  | mAwayLosses |
| 0x11 | u8  | mAwayGoalsFor |
| 0x12 | u8  | mAwayGoalsAgainst |
| 0x14 | s16 | mPoints |
| 0x16 | u8  | mUsed (1 = valid) |

The row has no "played", GD or position field. Played and goal difference are derived from the counters. Position comes from sorting.
Points are stored on their own, so changing W/D/L does not change points.
Find a team's row: 0x148a258c0 does a linear scan `mUsed==1 && mTeamId==team` [H]. A team can have several rows, one per competition/group. Filter on +0x02.

FCEI::StandingObject (the response row that the UI gets, about 0xA0 bytes). Filled by 0x148a1b100 [H]:
+0 id, +4 teamId, +0x68 points, +0x78..+0x9C = the 10 counters as int32 (HW,HD,HL,HGF,HGA,AW,AD,AL,AGF,AGA).
This fits the "FE view entries of 0xA0 bytes" that were seen in memory. They are rebuilt on every request, so editing them is pointless.

## 3. Fixtures / results

`FixtureDataList` (0x18 bytes): `+0x00 int32 count; +0x08 FixtureData* data; +0x10 u8 flag` [H] (0x148a2219a..0x148a221b0, 0x148a224e5).
Fixture id == array index (0x148a22168 `data + id*0x18`) [H].

FixtureData, 0x18 bytes (reflection 0x148a0cb20; read back field by field in 0x148a221c1..0x148a22208) [H]:
| off | type | field |
|---|---|---|
| 0x00 | u32 | mDate (packed yyyymmdd-style int, same as `actionDate`) [M on format] |
| 0x04 | u16 | mTime |
| 0x06 | u16 | mId |
| 0x08 | u16 | mCompObjId |
| 0x0A | s16 | mHomeStandingId (index into StandingsDataList) |
| 0x0C | s16 | mAwayStandingId |
| 0x0E | u8  | mMatchGroupId |
| 0x0F | s8  | mHomeScore (-1 = not played) |
| 0x10 | s8  | mHomePenalties (-1) |
| 0x11 | s8  | mAwayScore (-1) |
| 0x12 | s8  | mAwayPenalties (-1) |
| 0x13 | u8  | mGameCompletion: 0 = not played; set to 1 / 2 / 3 when a result is applied (3 if the pens test passes, 2 if the msg+0x30 test passes) [H for the values, M for the meaning: 1 = FT, 2 = AET, 3 = pens] |
| 0x14 | u8  | mUsed |

Team ids come from the standings rows: `rows[fx.HomeStandingId].mTeamId`. 0x148a22262..0x148a22292 does exactly this [H].
Init values: scores/pens = 0xFF, completion = 0 (0x148a18c0b..0x148a18c1b) [H].
Debug dump format (0x148a20cd4): `ID$COMP$COMPID$STAGE$GROUPID$DATE$TIME$COUNT$HOME$AWAY$HOMEID$AWAYID`.

## 4. Request flow and handlers

* UI side: 0x147da53a8 builds `FCEI::RequestGetStandings` (ctor 0x144036e6c, 0x70 bytes):
  `+0x10 type = 0x30`, `+0x18 user tag` ('mmbj' 0x6a626d6d), `+0x20 compObjId`, `+0x24 date (8 bytes)`, `+0x2C int`, `+0x6C u8 immediate`, `+0x6D u8` [H].
* FCEStandingsManager vtable = base+0xB1852E8. Slot 5 (0x14b185310) = HandleMessage 0x148a4e8fc. Slot 4 (0x14b185308) = queued-request pump 0x148a53238 [H].
  * Type 0x30 with +0x6C!=0 -> 0x148a54b00. Otherwise it is queued (0x148a197c4), and the pump later calls the same 0x148a54b00 (call at 0x148a53267) [H].
* **0x148a54b00 = RequestGetStandings handler** [H]:
  1. `DC = mgr->hub->DC`. Get the comp node with `CompStructure(DC+0x88).Find(compObjId)` (0x148a2c7a8).
  2. Get the CompetitionInfo of that comp (0x148a25758; struct: +0 actionDate, +6 baseYear, +8 u16 playedFixtures, +0xA active, +0xB s8 stageIndex). Take the current stage node (0x148a2c638).
  3. Collect the standing ids under that stage: 0x148a25960 -> 0x148a258a0 -> 0x148a26d48. It walks the tree and looks each node's compObjId up in the StandingsDataList hash.
  4. Each id goes through `GetItem` + 0x148a1b100 into an FCEI::StandingObject. 0x148a4a518 builds the `FCEI::ResponseStandingsList`, creates `FCE::DataSorter` (0x148a2f220) and sorts (0x148a36878) by comp settings.
  So the screen always shows the current `DataManager+0x88` rows, re-sorted on every request.
* **Match result = `FCEI::RequestUpdateMatchResult`**: ctor 0x1440376b8, 0x3C0 bytes, `type 0x2a`.
  Defaults: `+0x3B8=1, +0x3B9=1, +0x3BA=1`. Payload copied to +0x20 by 0x14403b7b8. Fields: +0x20 home goals, +0x24 away goals, +0x38/+0x3C pens, +0x70 fixture id.
  Built by career code at 0x147b490e0 and 0x147da3fba [H].
  * FCESchedulingManager (vtable base+0xB1854A8) HandleMessage 0x148a4e80c -> **0x148a5bb40** (if +0x3BA): writes fixture +0x0F/+0x11/+0x10/+0x12 and sets +0x13 = 1/2/3 [H].
  * FCEStandingsManager type 0x2a -> **0x148a5bc04** (if +0x3B8): **incremental** update [H]:
    * outcome = 0x144045d94(result):
      * 0 -> home.HW++ (+9), away.AL++ (+0x10), points += win/loss
      * 1 -> home.HL++ (+0xB), away.AW++ (+0xE)
      * 2 -> home.HD++ (+0xA), away.AD++ (+0xF), both += draw
    * Goals: home.HGF(+0xC) += hg, home.HGA(+0xD) += ag, away.AGF(+0x11) += ag, away.AGA(+0x12) += hg.
    * Points come from comp settings via 0x148a4e5bc(compObjId, id): **0x1F = win, 0x20 = draw, 0x21 = loss** [H that they are used this way].
      Points are only added when a local flag is 1. A special path for setting 0x11==2 (shootout-style comps) can clear it.
  * Statistics manager type 0x2a -> 0x148a5c000 (player stats).
* AdvancementLogicAggregateStandings (0x148a339f8) adds the rows of a previous stage into a new stage's rows [H].
  So edits made before a stage advance carry over where this logic is used.

**Does the table follow a result edit?** No [H]. Nothing derives the rows from the fixtures. Changing `FixtureData.+0x0F/+0x11` only changes what the fixtures/results views show.
Not verified: whether some UI column (e.g. form/last 5) is computed from the fixtures.

## 5. Recommended DLL approach

### Locating DataManager (pick one)
1. **Heap vtable scan (no game calls, most robust for this build)** [M]
   Walk committed MEM_PRIVATE RW regions and look for the qword `base + 0xB180CA8` (DataManager vtable).
   Validate each candidate DM:
   `DC=*(DM+0x28)`, check `*(DC+0x80)==DM`,
   `S=*(DM+0x88)`, check `(S.end-S.begin)%0x18==0`,
   `F=*(DM+0x60)`, check `0<=F.count<20000`.
   Cache it, and re-validate before each use. A new career load makes a new FCE instance.
2. **Service registry (one virtual call)** [M]
   `reg = *(void**)(base+0xC2A8590)`; `impl = reg->vtbl[0x40/8](reg, 0x0A613B9A)`.
   Check `*(uintptr_t*)impl == base+0xB180DF0`. Then `hub=*(impl+0x18)`, `DM=*(*(hub+0x18)+0x80)`.
   Signature for the global (2 hits): `48 8B 0D ?? ?? ?? ?? BA 9A 3B 61 0A 48 8B 01 FF 50 (30|38)`.
   The +0x40 = Get slot is inferred from 0x147c15b9c and not proven.
3. **Capture via hook** [H for the pointer math]
   Swap vtable slot `base+0xB185310` (StandingsManager::HandleMessage 0x148a4e8fc). On the first call, `this->+0x18` = hub.
   Needs VirtualProtect on the vtable page.

Signatures for other builds:
* RequestGetStandings handler start: `48 8B 41 18 4C 8B EA 8B 5A 20 4C 8B F9 8B D3 89 5C 24 30 4C 8B 70 18 49 8B 8E 88 00 00 00 E8` (at 0x148a54b31). Use it to re-derive the +0x18/+0x18/+0x88 offsets.
* GetItem: `45 33 C0 44 8B CA 85 D2 78 32 4C 8B 11 48 B8 AB AA AA AA AA AA AA 2A 48 8B 49 08` (0x148a2d1d4).

### Editing standings (shows up on the Standings screen next time it requests data)
* For each row in `[begin,end)` with `mUsed==1`: match on `mTeamId` (+4) and `mCompObjId` (+2) of the league group.
  To get the group id, take any league fixture's HomeStandingId row +2. Or take the row(s) of a team known to be in that league.
* Write the u8 counters at +0x09..+0x12 and the s16 points at +0x14. Keep them consistent: played = HW+HD+HL+AW+AD+AL. The UI derives played and GD; it is not stored.
* No position write is needed. The order is re-sorted on every request.
* u8 limits: max 255 per counter and per goal tally.
* Re-open the Standings screen (it re-requests). Do not write while a sim is running (Sim To Date / match processing).

### Editing a played result so table and fixtures agree
```
fx = F.data + id*0x18 ; H = S.begin + fx.HomeStandingId*0x18 ; A = ... AwayStandingId
old (h0,a0) = fx[0x0F], fx[0x11] ; new (h1,a1)
remove old: outcome(h0,a0) counters--, H.HGF-=h0, H.HGA-=a0, A.AGF-=a0, A.AGA-=h0, points -= (win/draw/loss pts)
add new   : same with +
fx[0x0F]=h1 ; fx[0x11]=a1   (leave fx[0x13] as is; pens fields only for cup ties)
```
Points per outcome: read comp settings 0x1F/0x20/0x21 from SettingsDataList (DM+0x78, 8-byte entries: +0 s32 value, +6 u8 settingId, +7 used; +4 is probably the u16 compObjId [M]). Or assume 3/1/0 for leagues.

### Caveats
* Marking an **unplayed** fixture as played (scores + completion=1) only updates FCE. Career managers (MainHubManager remaining-fixtures list, SimResultsManager, news, InterestingResultManager, player stats) are not updated. The scheduler may still sim or play it [M].
* The `leagueteamlinks` DB table is a separate copy. Script functions UpdateLeagueStats / UpdateTable / ClearLeagueStats (factory at 0x148a2145e..0x148a214c0) probably sync FCE->DB at certain points [L].
* Persistence: the FCE lists are probably serialized into the career save (`fce_standings` / `fce_fixtures` names exist; 0x148a3e6dc is the debug table dumper). **Not verified.** Save, reload and compare in game.
* Unknown: the `{teamid,0,0,0}` 16-byte array with header 337 seen in the live game. It is not one of the FCE DataManager lists. 337 may be the league compObjId passed in RequestGetStandings.

## 6. Key addresses
| addr | what |
|---|---|
| 0x148a1661c | DataManager ctor (list offsets) |
| 0x148a2ee24 | ManagerHub::Init |
| 0x148a16b20 | FCEInterfaceImpl ctor (+0x18 hub) |
| 0x148a0d230 | StandingData reflection (field names/offsets) |
| 0x148a0cb20 | FixtureData reflection |
| 0x148a2d1d4 | Standings GetItem(id) |
| 0x148a22168 | Fixture by id -> FCEIFixtureData |
| 0x148a2235c | GetFixtures filter (iterates DM+0x60) |
| 0x148a54b00 | RequestGetStandings handler |
| 0x148a4a518 | builds standings response + DataSorter |
| 0x148a5bc04 | standings incremental update on match result (type 0x2a) |
| 0x148a5bb40 | fixture score write on match result (type 0x2a) |
| 0x1440376b8 | RequestUpdateMatchResult ctor (type 0x2a, 0x3C0) |
| 0x144036e6c | RequestGetStandings ctor (type 0x30, 0x70) |
| 0x14c2a8590 | service registry global |
