# Real-time transfers, signings, created players and edits (FC 27): static RE notes and Turbo plan

Source: `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (FC27.exe 1.0.140.64835, base `0x140000000`, build key
`6AB9813C-211EF000`), analysed 2026-10-05, **static only** (no dev service, no game process, no Live Editor binary).
Tooling: capstone over the image plus the exception directory (`.pdata`, 653,114 unwind entries = exact function bounds)
and rip/call cross-reference indexes built for this track; the helpers in `scripts/re/` for the signatures.
Confidence: `[H]` read in the code (disassembly in this session), `[M]` strong inference, `[L]` guess / not traced.
`scripts/re/realtime_signatures.json` holds 76 unique signatures (functions + vtables, every one proven unique in the whole
image; the JSON also carries the hub slot offsets, the event table, special ids and object layouts used below).
Companion notes: `transfer_lists.md`, `game_thread.md`, `standings-ui-path.md`, `job_offer.md`, `development.md`.

Addresses are for this build only. "hub" = the career manager table `*(*(comm+0x20)+0x10)` (slot `i` at `hub+0x20*i`, holder at
`+0x18`, object `**(hub+0x20*i+0x18)`; slot ids = Live Editor's enum). A holder offset is `0x20*slot+0x18` (DataController slot 32
-> `+0x418`, TeamUtil 120 -> `+0xF18`, PlayerContractManager 77 -> `+0x9B8`, EventsMailBox 39 -> `+0x4F8`, ...: full table in the JSON, `hub_slots`).

## 0. Summary

**Why Turbo's moves are invisible.** The career database is only the *persistent* copy. The screens read **runtime objects** that the
game keeps in sync **through career events** (a typed event object posted with `PostEvent` and fanned out synchronously to ~90
manager listeners). A club change in the game is one function, `TeamUtil::PlayerMoved(pid, from, to)` `[H]`, that updates the DB
**and posts event `0x60` (PlayerRemovedFromTeam) then `0x5F` (PlayerAddedToTeam)**; the listeners of those two events create / drop the
per-player runtime state: morale, form, status, value, squad ranking, fitness set, development plan, FCE statistics team, contract
records, inbox purge, and the **runtime team sheet** (a separate service). Turbo writes the DB rows directly, so none of that fires:
that is the "Unknown morale / form 5 / counts wrong until save+reload" symptom `[H]` (while the career runs a morale entry is created by event `0x5F`
(and for the whole squad at season reset); a reload only restores the saved entries, so it cannot create the missing one, section 1.4). Worse, the runtime **team sheets are the master copy and are written back over `cm_teamsheets` at every
save** `[H code, not run]`: Turbo's `cm_teamsheets` edits are never seen by Team Management / Team Sheets and are overwritten (a player
Turbo moved out of your club stays on the runtime sheet and is written back into the DB at the next save) (section 1.5).

**Recommended call path (trusted, small, scalar arguments, game thread only):**

| need | call | where it lives |
|---|---|---|
| any club -> any club (transfer, free agent signing, release of any player, user -> AI) | `TeamUtil::PlayerMoved(teamUtil, pid, fromTeam, toTeam)` `0x147DEE368` | `teamUtil = **(hub+0xF18)` |
| user's own release with the game's compensation / checks | `ContractTerminationManager::ReleasePlayer(ctm, pid)` `0x147B94630` | `ctm = **(hub+0x3D8)` |
| user-club contract record after a signing | `PlayerContractManager::AddContractRecord(pcm, pid, team, months, wage, 100, &date, status)` `0x147E5BF9C` (or `CreateContract` `0x147E5B3A4`) | `pcm = **(hub+0x9B8)` |
| new player | Lua inserts the `players` row; native: `DataController::InsertTeamPlayer(dc, pid, FreeAgents, 99, 29, 0)` `0x147B90074`, post event `0x3A`, then `PlayerMoved(pid, FreeAgents, club)` | `dc = **(hub+0x418)` |
| delete a player | `PlayerUtil::DeletePlayer(playerUtil, pid)` `0x147E9DCE0` (replaces Lua row deletes) | `**(hub+0xB18)` |
| "just post the events" (row already written) | synthesize events `0x60` / `0x5F` / `0x3A` and call `PostEvent` `0x14060124C`; layouts in section 1.1 | `mailbox = **(hub+0x4F8)` |

Everything above is a **synchronous** call that finishes the whole job (DB write + events + runtime state) before it returns: no
save/load, no day advance. What I could **not** reduce to a scalar-argument call is the game's *deal executor*
(`TransferManager::ExecuteDeal` `0x147C531D0`: needs a 0xF0-byte deal with three eastl lists built by its negotiation engine); Turbo
composes the same result from the primitives above (section 2.1). What I could not settle statically: which UI refresh the *already
open* Squad Hub performs (section 2.2) and whether Live Editor's `InsertDBTableRow` fires the `players`-table observers (section 5).

## 1. How the game keeps squads consistent

### 1.1 The event bus `[H]`

* `PostEvent(void* mailbox, int32 id, Event* ev)` `0x14060124C` (233 call/jmp sites): `ev->vf[1]()` (AddRef), `ev->vf[4]()`,
  `(*mailbox)->vf[6](id, ev, 0)` (= `0x14230F760` -> `0x141BBC7C0`: **synchronous** dispatch to the handler lists of key `-1` and
  of `id`, `handler->vf[1](id, ev)`), then `ev->vf[2]()` (Release, frees through the global allocator). `mailbox = **(hub+0x4F8)`.
  Nothing is queued or deferred (the bytes at server `+0x170/+0x171/+0x174` read as lock / copy-list / removal flags, not a queue `[M]`).
  The game itself posts events from inside listeners (job offers, standings, loans), so nesting is normal.
* The **EventsManager** (slot 40, ctor `0x147B56080`, vtable `0x14AFF69B8`) registers itself with the mailbox for **every id 1..0xBE**
  (loop at `0x147B5614C`); its slot 1 is `Dispatch(this, id, ev)` `0x147B8C0D8` (Live Editor's hook site, `game_thread.md` section 4),
  which calls **all ~90 listeners** `listener->vf[1](id, ev)` (every manager's `HandleEvent`, vtable slot 1). So a posted event
  also reaches Live Editor's `post__CareerModeEvent` Lua handlers (Turbo's `bridge.on_career_event`: re-entrancy is already guarded
  by `S.cmd_running` in `poll_mailbox`).
* **Event object layout** `[H]`: `{+0 vtable, +8 s32 refcount (0), +0x10 s32 id, payload from +0x18}`; allocate with
  `A = *(void**)0x14C269EA8; A->vf[2](A, size, "Class::Method", 0)` (`call [rax+0x10]`, rcx = A, edx = size, r8 = tag, r9d = flags);
  construction writes the base vtable `0x149803AE8`, zeroes `+8`, then the class vtable, `+0x10 = id`, the payload. Slot 0 of the class
  vtable is the deleting destructor (frees `size` bytes), so the object must come from that allocator.
* **Event ids are the game's, not Live Editor's.** `enums.lua` agrees with the code up to id `0x3F` (`0x3A` 58 inserted, `0x3B` 59 deleted,
  `0x3E` 62 contract termination, `0x3F` 63 contract accepted; `consts.lua` `CONST_CM_EVENTS_NAMES` is one lower from `0x3A` on) and both
  diverge after it (FC 27 renumbered): the code's `0x4A` loan terminated is `enums` 74 `CPU_TRANSFER_INFO` (consts 74
  `NUM_DAYS_BEFORE_USER_GAME`); `0x4D/0x4E` TransferMove (about to complete / complete) are `enums` 85/86 (consts 84/85); `0x5F/0x60`
  PlayerAdded/Removed are `enums` 110/111 (consts 109/110); `0x79/0x7A` UserTransferlisted / Loanlisted are `enums` 137/138. A Turbo / Live Editor
  handler keyed on those names for ids >= `0x4A` never matches the game's event `[H]` (the ids Turbo uses today, DAY_PASSED 15, WEEK_PASSED 16,
  POST_LOAD_PREPARE 29, SEASON_RESET 23, are unaffected).

Events relevant here (full table in the JSON, `events`):

| id | name (alloc tag) | size | payload | posted by |
|---|---|---|---|---|
| `0x3A` | PlayerInserted (`DataController::CreatePlayer`) | 0x20 | `+0x18` pid | `0x147B61750` |
| `0x3B` | PlayerDeleted (`DataController::DeletePlayer`) | 0x20 | `+0x18` pid | `0x147B63668` |
| `0x3E` | contract termination (release) | 0x28 | pid, team, compensation | `0x147C59934` |
| `0x4D` / `0x4E` | TransferMoveAboutToComplete / Complete | 0x68 | `+0x18` buyer team (M), `+0x30` pid, `+0x34` seller team (M) | `0x147C5BFDC`, Legacy `0x147FA5388/54E8/56D0` |
| `0x5E` / `0x61` | youth signed / promoted | 0x20 | pid, flag / pid, team | `0x147E20D3C` / `0x147E1A4D8` |
| **`0x5F`** | **PlayerAddedToTeam** (`DataController::WriteTeamPlayersLinks` / `InsertTeamPlayer`) | 0x28 | `+0x18` pid, `+0x1C` **toTeam**, `+0x20` fromTeam (-1 from InsertTeamPlayer); vtable `0x14AFF6C00` | `0x147BA08CC`, `0x147B90074` |
| **`0x60`** | **PlayerRemovedFromTeam** | 0x28 | `+0x18` pid, `+0x1C` **fromTeam**, `+0x20` toTeam (-1 from DeletePlayer); vtable `0x14AFF6468` | `0x147BA08CC`, `0x147E9DCE0` |
| `0x73` | AboutToSwitchJob | 0x30 | | `0x147DE01DC` |
| `0x79` / `0x7A` | UserTransferlisted / UserLoanlisted | | | `0x147F68368` / `0x147F68214` |
| `0x9D` | transfers news message | 0x70 | | Legacy `0x147FA4xxx` |

### 1.2 DataController = the DB gateway that posts the events `[H]`

`dc = **(hub+0x418)` (slot 32; ctor `0x147B5500C`, no vtable of its own): `{+0 db provider (query executor: vf[1](query, result)), +0x10 hub}`.
Its ~500 methods build a query object (`0x14197D5F4(&q, type, "table")`, 1 select, 2 delete, 3 update, 4 insert; `0x140601EA8` set
field, `0x140602D28` where-clause) and run it; the career code never writes tables any other way. The ones that matter (all
`__fastcall`, rcx = dc, game thread):

| function | address | arguments | does |
|---|---|---|---|
| `WriteTeamPlayersLinks` | `0x147BA08CC` | `(dc, pid, fromTeam, toTeam, jersey [stack 5])` | `UPDATE teamplayerlinks SET teamid=to, jerseynumber=jersey, position=29 WHERE teamid=from AND playerid=pid`; posts **`0x60`** `{pid, from, to}`, then **`0x5F`** `{pid, to, from}`; then `EnforceSquadLimits`. The UPDATE result is not checked: the events fire even if no row matched |
| `InsertTeamPlayer` | `0x147B90074` | `(dc, pid, team, jersey, position [stack 5], suppressEvent [stack 6])` | `INSERT teamplayerlinks`; if it succeeded and `suppressEvent==0` posts **`0x5F`** `{pid, team, -1}` |
| `CreatePlayer` | `0x147B61750` | `(dc, PlayerRecord*, AppearanceRecord*, GkStyleRecord*)` | `INSERT players` (+`ucc` rows), `0x147B9F654`, posts **`0x3A`** `{pid}` |
| `DeletePlayer` | `0x147B63668` | `(dc, pid)` | deletes the players row, posts `0x3B`; the full delete is `PlayerUtil::DeletePlayer` (below) |
| `IsPlayerInTeam` | `0x147B90A8C` | `(dc, pid, team) -> bool` | SELECT on teamplayerlinks |
| `SetPlayerJoinDate` / `SetPreviousTeam` / `SetContractValidUntil` / `SetReleaseClause` / `SetWage` | `0x147B9E7B8` / `0x147B9CAB8` / `0x147B9E138` / `0x147B9739C` / `0x147B972F0` | `(dc, pid, value)` | one `UPDATE players SET ...` each (join date, `previousteam` row, `contractvaliduntil`, `releaseclause`, `wage`) |
| `EnforceSquadLimits` | `0x147B99BFC` | `(dc, pid, from, to)` | see section 1.3 |
| `WriteTeamSheet` / `LoadTeamSheets` | `0x147B9616C` / `0x147B5DB4C` | `(dc, TeamSheet*)` / `(dc)` | `cm_teamsheets` + `cm_mentalities` <-> the runtime team-sheet service (section 1.5) |

`PlayerUtil::DeletePlayer` `0x147E9DCE0` (`this = **(hub+0xB18)`, `(this, pid)`): `DataController::DeletePlayer` (event `0x3B`),
TransferManager+0x2D38 listener `vf[0x50](pid)`, `FCEI::RequestClearStatisticsForPlayerID {pid}`, deletes his teamplayerlinks rows
and posts `0x60 {pid, team, -1}` for each club he was in `[H]`. This is what Turbo's Lua delete (row deletes) misses.

### 1.3 `TeamUtil::PlayerMoved`: the game's own "player changes club" `[H]`

`TeamUtil` = hub slot 120, a 0x10-byte object `{+0 hub, +8 0}` (hub builder `0x147F18141`), `teamUtil = **(hub+0xF18)`, `__fastcall`.

```
void TeamUtil::PlayerMoved(TeamUtil* this, int pid, int from, int to)                 0x147DEE368   (20 call sites in 18 functions)
    DataController::SetPlayerJoinDate(dc, pid, &calendar.today)                        0x147B9E7B8
    DataController::SetPreviousTeam(dc, pid, from)                                     0x147B9CAB8
    windowOpen = calendar.IsDateInWindow(currentWindow(), today)                       0x147AA1EF4 / 0x141549DC4
    TeamUtil::MovePlayerLink(this, pid, from, to, windowOpen)                          0x147DEE4BC
void TeamUtil::MovePlayerLink(this, pid, from, to, bool windowOpen)
    if pid/from/to == -1: return
    if !(IsPlayerInTeam(pid, from) && !IsPlayerInTeam(pid, to)): return                (join date / previousteam above were already written)
    jersey = 99 for special teams (0x1B29D, 0x1B688, free agents), else chosen by the jersey service
             (0x147DEADCC auto, 0x147DEB08C when the window flag is set); may renumber ANOTHER player of `to` (0x147B9F874)
    DataController::WriteTeamPlayersLinks(dc, pid, from, to, jersey)                   0x147BA08CC  -> events 0x60, 0x5F, EnforceSquadLimits
    if from is not a special team: TeamSheetService(0x113EA820)->vf[0x70](modeId, from, pid)    RemovePlayerFromTeamSheets
```

Callers prove the argument order `(pid, from, to)`: `YouthPlayerUtil::PromotePlayer` `0x147E1A4D8` passes `(pid, 0x1B688 youth pool, team)`,
`ContractTerminationManager::ReleasePlayer` `0x147B94630` passes `(pid, userTeam, 111592 Free Agents)`, the youth generator moves
`0x1B29D -> 0x1B688`. A **loan** variant exists, `TeamUtil` `0x147DEC5DC` (`MovePlayerLink` + `LoansManager::AddLoan` `0x147DE5EC0` + career story
note) `[M]`, not needed for the first delivery.

`EnforceSquadLimits` (`0x147B99BFC`, called inside `WriteTeamPlayersLinks`) is **a side effect Turbo must plan for** `[H]`. The game's
squad total of a club is `DC::SquadCounts(dc, team, &rows, &loanedOut, &pending)` `0x147B7225C` = `teamplayerlinks` rows of the team
(`0x147B86260`) + players loaned out whose returning club is the team (`LoansManager` `0x147DCFC20`) + pre-signed deals naming the team
as buyer (`TM+0x2F30` records, `0x147C40FB4`). After a move, if `to` (not Free Agents / pool / special) has a total above
`IniSettings.MAX_SQUAD_SIZE` (`[[hub+0x658]]+0x58`, loaded from `DEFAULTS/MAX_SQUAD_SIZE`) the game **releases the lowest-value reserve**
(`teamplayerlinks.position >= 28`, not loaned out) of `to` to Free Agents, one by one (`0x147B5E558`, any club, **the user's included**); if
`from` has fewer rows than `MIN_SQUAD_SIZE` (`+0x64`) it recalls CPU loans (`LoansManager::RecallCPULoanedPlayer`) or signs fillers
(`0x147DE8A6C`). Turbo's own limits (52 / 18, rows only) avoid both only if the ini values are the same and no loans / pre-signed deals
are pending: read the two ints and call `SquadCounts` at run time instead of hard-coding.

### 1.4 Who consumes `0x5F` / `0x60` (the runtime state a squad member needs) `[H]` unless marked

All are `HandleEvent` = vtable slot 1; "user club" = `GetUserClub(activeUser,0)->+4` (the UserManager's club team id).

| manager (slot, hub off) | handler | on `0x5F` (`ev.team = +0x1C`) | on `0x60` |
|---|---|---|---|
| **PlayerMoraleManager** (83, `+0xA78`, vtable `0x14B0156A8`) | `0x147D8D354` | only if flag `+0x554==0` (set by `0x73` AboutToSwitchJob, cleared by `0x17` SeasonReset), `ev.team == user club`, `pid>0` and `find(this+0x518, pid)==null`: `morale_create_entry(this+0x518, pid, overall-1)` `0x147D81108` + `ComputeEntry` `0x147D93B08`. Entries are 0x60-byte records in `[+0x528,+0x530)`, `+0` = pid | removes the entry (same gates) |
| **PlayerFormManager** (80, `+0xA18`, `0x14B01D6C0`) | `0x147E3DED4` | `team>0`: `AddPlayer(this, pid, team)` `0x147E2EAD4` (per-team tree at `+0x1C8`, key `+0x20`; only teams the manager tracks) | erases from the team's container |
| **PlayerStatusManager** (87, `+0xAF8`, `0x14AFFB2A0`) | `0x147BF30C0` | `ev.team == this+0x10` (user club): `ComputeStatus(this, pid, flag)` `0x147BE7BB4` (asks the TransferManager DAO at `+0x2C70`) | `0x147D90C80` erase |
| **SquadRankingManager** (106, `+0xD58`, `0x14B016160`) | `0x147DA0B1C` | flag `+0x70==0`, user club: upsert `{pid, 0, overall*10}` in the vector at `+0x50` (0xC-byte records) | `0x147D9CCA8` erase |
| **FitnessManager** (45, `+0x5B8`, `0x14B009808`) | `0x147CB4E64` | user career: insert pid into the hash set `+0x6C90` (club) / `+0x6CB0` (national team) | erase |
| **PlayerValueManager** (89, `+0xB38`, `0x14AFFB290`) | `0x141549F1C` | user club: `AddPlayer` `0x147BEA034` (market-value cache); `0x4E` refreshes, `0x3B` drops | `0x147BEA238` remove |
| **PlayerGrowthManager** (81, `+0xA38`, `0x14B01D810`) | `0x147E3E018` | user club, `from != youth pool`: `AddPlayer` `0x147E3EFFC` (growth record = **development plan**) if `DataController 0x147B6904C` says none, then plan set-up `0x147E383C8` `[M]`; `0x3A` also tracks a new players row (needs the flag `+0x5F0`) | drops |
| **StatisticViewManager** (109, `+0xDB8`, `0x14AFF81B0`) | `0x147BCFC58` | sends `FCEI::RequestUpdateStatisticsTeamIDForPlayerID {pid, team}` to the FCE: his season statistics follow him | |
| SuspensionManager (114) | `0x147BD01C4` | `0x147BDDC60(this, pid, from, to)` `[M]` | |
| CoachManager (27) / CareerStoryManager (111) | `0x147B89A74` / `0x147AA0AC4` | user-club squad bookkeeping `[M]` | |
| TransferManager (127) | `0x147C42494` | `0x147C4F9A4(tm, team, pid, 1)` and the DAO at `+0x2CF8` `[M]` | negotiations / lists of a player who left `[M]` |
| **PlayerContractManager** (77, `+0x9B8`, `0x14B01E240`) | `0x147E6B3A4` | **no `0x5F` handler**: contract nodes are created by the signing code, not by the event | user player: drops his records (`0x147E5C58C`, ...); `0x4E` records the move |
| EmailManager (36) | `0x147B8A7D8` | | purges inbox items about him (user club / national team) `[M]` |
| YouthPlayerManager (130) | `0x147DEBFD4` | only the generator id `0x704DF` | youth table cleanup |

Reading: morale, status, ranking, value, development plan, fitness set are **user-club only**; form and the FCE statistics team are
**per club**. Live Editor's `SetPlayerMorale` "has no effect" on a Turbo-moved player because there is no entry to set.
The **player-card builder confirms it** `[H]`: `0x147F4C624` shows `CM_Morale_Unknown` when `LoansManager::IsPlayerLoaned(pid)`
(`0x147DC7508`), otherwise `PlayerMoraleManager::GetMoraleLevel(pid, overall)` `0x147D8B738` (0 = unknown when there is no entry or the
score is out of range).

### 1.5 Team sheets are a runtime service, written back at save `[H]`

* The service is **`GameServices::CachedTeamSheetService`** (service id `0x113EA820`, 0x1418 bytes, vtable `0x14AD7E188`, ctor
  `0x145E196DC`; installed by `0x145E1D1A0` (from `0x147F2FB90`) while career mode runs, the DB-backed `TeamSheetService` by `0x145E1D054`
  (from the hub teardown `0x147F19C60`) `[M]`). It
  holds one 0x288-byte `TeamSheet` per team (`+0 teamsheetid`, `+4 teamid`, player ids from `+0xC4`, takers, formation).
* **Load**: `ActionLoadGame::LoadGameComplete` `0x147CA102C` -> `LoadTeamSheets` `0x147B5DB4C` reads every `cm_teamsheets` /
  `cm_mentalities` row and pushes it into the service (`vf[0xC0]` SetSheet). Nothing else reads those tables back.
* **Save**: `FCECommsManager::TriggerSave` `0x147B99DE4` posts `0x1C`, calls service `vf[0x150]`, fetches **every sheet the service holds**
  (`vf[0x2A8]`, `0x147B82940`) and writes each with `WriteTeamSheet` `0x147B9616C` (UPSERT `cm_teamsheets` + `cm_mentalities`).
  So a DB edit of `cm_teamsheets` (Turbo's `plan_sheet_add/remove`, set-piece takers) is **invisible at runtime and overwritten at the
  next save**; for a player moved out of your club the runtime sheet still lists him and the save writes him back into the DB.
  This may be the root cause of the 2026-10-03 "crash while simulating after several moves" (a lineup slot naming a player who
  plays elsewhere) `[L]`: a hypothesis, not evidence.
* The game keeps the sheet right itself: `MovePlayerLink` calls `TeamSheetService::RemovePlayerFromTeamSheets(modeId, from, pid)`
  (vtable slot 14, `0x145E3ED90`: slot -> -1, takers cleared, normalize, SetSheet). **The arriving player is not put on any sheet** by
  the game (he is a reserve, `position 29`); the sheet is normalized against the squad when it is fetched `[M]`. So Turbo must stop writing
  `cm_teamsheets` for moves and let `PlayerMoved` do it.

### 1.6 PlayerContractManager `[H]` unless marked

`pcm = **(hub+0x9B8)`. Records: hash node `pcm+0x3D8` (buckets) / `+0x3E0` (count), key pid, chain `+0xB8`; node `+0x08` team, `+0x0C` wage,
`+0x14` s8 length, `+0x18` f64 date, `+0x34` status (0 none, 7 transfer listed, 8 loan listed, 9 both; loan variants 1..6). Reader
`GetPlayerInfo` `0x147E61F20`. Writers:

* `SetContract(pcm, ContractParams*)` `0x147E77AF4`: upsert by pid, copies team, **wage** (and writes `players.wage` through
  `DataController::SetWage`), dates, status, element lists. `ContractParams` is built by `0x147B55164` and freed by `0x147AC8B80`.
* `AddContractRecord(pcm, pid, team, months, wage, 100, Date* start, status)` `0x147E5BF9C`: fills a `ContractParams` and calls `SetContract`. Roles of
  the arguments as the game's own `CreateContract` passes them `[H]`: `months` = contract length in months (12 * k, stored as a signed byte at node
  `+0x14`), `wage` = `PlayerWageManager::GetWage` result, `100` = a byte stored at node `+0x15` (meaning not identified), `Date*` = `calendar+0x34`
  (`{day, month, year}` int32 x3), `status` = `GetContractType`.
* `CreateContract(pcm, pid, team, status)` `0x147E5B3A4`: default contract (months and wage computed from the calendar and
  `PlayerWageManager::GetWage` `0x147BF140C`), writes `contractvaliduntil` / join date in some branches. `GetContractType(pcm, pid, team)`
  `0x147E657DC` gives `status` (0 normal). Callers: `YouthPlayerUtil::SignPlayer`, and `0x147E6CAE0` (initial contracts for the user
  squad, loans and the youth pool at career start).
* The pre-signed / negotiated flows build whole contract sets (`0x147E5C0F8`: bonus and clause lists at `pcm+0x1B8..0x2E0`).

A Turbo-moved player has **no PCM node** until a game path creates one; the contract screens fall back to defaults (`GetPlayerInfo`
returns a blank record). Value of an explicit `AddContractRecord` for user arrivals: PCM wage / length equal what Turbo wrote to the DB.

### 1.7 The transfer engine, and why there is no `(pid, toTeam, fee, wage, end)` routine `[H/M]`

* `TransferManager` (slot 127, 0x2FA0 bytes, ctor `0x147C26450`, `HandleEvent` `0x147C42494`) owns a `LegacyController` and ~30 DAOs
  (`NegotiationsStorageDaoImpl`, `TransferActivityController`, ...). Negotiations end in an `ITransferResultHandler` (the object at
  `TM+0x2D78`, vtable `0x14B02C5A0`): slots 2..35 **complete a deal of a given kind** (user buys / sells / loans / exchange / free
  transfer, each with a user and an AI flavour), slots 37+ send the user notifications (`OnUserTransferOfferReceived`, ...).
  The completion bodies (`0x147FB907C`, `0x147FBB4CC`, `0x147FBA2C4`, `0x147FBC534`, `0x147FC006C`, `0x147FC0810`, `0x147FBCCB4`, ...)
  all follow the same recipe, e.g. `0x147FB907C`: record in the transfer-event list, `PlayerWageManager::GetWage`, post **`0x4D`**,
  **`TeamUtil::PlayerMoved`**, `DataController::SetContractValidUntil` / `SetReleaseClause`, club finance objects (`0x147FC18AC`,
  buyer and seller budgets), news **`0x9D`** (`0x147FA4xxx`), post **`0x4E`**.
* The **pre-signed** path: `TM::HandleEvent` (day pass) -> `ProcessPresignedContracts(tm, dateYmd)` `0x147C53B8C` -> for each deal
  `ExecuteDeal(tm, Deal*)` `0x147C531D0`. A `Deal` is 0xF0 bytes (`+0 pid`, `+4 / +8` the two clubs, `+0xC` date, `+0x14` fee,
  `+0x1C` kind (0..2 simple, >2 loan / pre-contract variants), three eastl lists at `+0x28/+0x58/+0x88`, flags `+0x20/+0x21/+0xE2`)
  converted from the 0x88-byte pre-contract records at `TM+0x2F30` by `0x147C3D4A4`. `ExecuteDeal` makes room (`0x147DE8A6C`), creates the
  PCM contract records (`0x147E5BF9C` / `0x147E5C0F8`), wages, finance, `PlayerMoved`, posts `0x4D`/`0x4E`, news.
* **Not callable from outside with scalars**: the deal and its lists are produced by the engine; the only scalar entry points are the
  primitives of sections 1.2-1.6. The `transfers` DB table is only *read* by the TM at load (`0x147B6DA28`: future rows = real-world
  transfer schedule); no string-named writer of history rows was found `[L]`.
* Release by the user: `ContractTerminationManager::ReleasePlayer(ctm, pid)` `0x147B94630` (`ctm = **(hub+0x3D8)`, 0x18-byte object, vtable
  `0x14AFF6D58`): `CanRelease` `0x147B5F380` (0 ok, 1 budget cannot pay the compensation, 2 squad at `MIN_SQUAD_SIZE`), compensation
  `0x147B5EBC0`, `PlayerMoved(pid, userTeam, FreeAgents)`, pays the user's finance, counter in slot 60; returns the `CanRelease` code.
  It only releases from **the user's club**; the UI posts `0x3E` first (`0x147C59934`).

## 2. Answers to the five questions

### 2.1 Completing a transfer / signing / AI deal in real time

* **There is no scalar completion routine.** The game's completions take an engine-built deal (section 1.7). The closest "one call"
  that does the whole *club change* with every consequence Turbo needs is **`TeamUtil::PlayerMoved(teamUtil, pid, from, to)`**
  `0x147DEE368`: `__fastcall`, rcx = `**(hub+0xF18)`, edx pid, r8d from, r9d to, returns nothing; **game thread**. It creates / does: DB
  `playerjointeamdate`, `previousteam`, the `teamplayerlinks` row (shirt number chosen by the game, `position 29`), event `0x60` then
  `0x5F` (-> morale, form, status, ranking, value, fitness set, development plan, FCE stats team, contract drops, inbox purge,
  TransferManager), squad-limit housekeeping, and the **runtime team sheet** update of the club he leaves. It does **not** create a PCM
  contract node, change wage / contract end / release clause, pay a fee, post `0x4D/0x4E`, write the `transfers` history or news.
* Turbo supplies the rest: contract fields (`wage`, `contractvaliduntil`, `releaseclause`) by DB writes **before** the call (the morale
  computation of `0x5F` reads them); the PCM node for **user-club** arrivals (`AddContractRecord`) after it; fees / budgets are not
  modelled (Turbo has none today).
* **Directly with `(playerId, toTeamId, fee, wage, contract end)`?** Only in pieces: `PlayerMoved` (ids), DB setters (wage, end, clause),
  `AddContractRecord(pcm, pid, team, months, wage, 100, &date, 0)` for the contract record. A fee has no scalar entry point.
* **Free agent**: `PlayerMoved(pid, 111592, club)` (`from` = Free Agents, which is not a special case of `EnforceSquadLimits`' `from` check).
* **AI <- AI, user -> AI**: the same call (`from`/`to` are any non-national clubs); for `from == user club` the user's sheet, morale, status,
  value, ranking, development plan and PCM records are dropped by the `0x60` listeners.
* **Release**: user's own: `ReleasePlayer` (with compensation and the squad-minimum check); any other: `PlayerMoved(pid, club, 111592)`.
* **Events posted by the game that Turbo's path does not** (consumers noted): `0x4E` (PlayerValueManager refresh, BudgetManager,
  TransferManager, PCM "recent move" record = status text `recentmove`), `0x9D` news. Posting `0x4D/0x4E` needs the 0x68-byte payload
  (`0x147C5BFDC` builds it from an event-data struct); not needed for the squad symptoms and not recommended in the first delivery.

### 2.2 How the squad screens learn that squads changed

* **There are no dirty flags / "squad changed" screen messages for Squad Hub, Team Management or player cards in the career module.**
  They are built when opened from (a) the DB through `DataController`, (b) the manager caches of section 1.4, (c) the team-sheet service
  (section 1.5), (d) front-end services (`TeamManagementServices::RosterResolverService`, service `0xE004102`, its constructor is in the
  protected code region `0x15CF1BE90`: not analysable here). The managers are kept right by events `0x5F/0x60` (and `0x3A/0x3B/0x4E`).
  Hence the right trigger is the **same event**, posted on the game thread through the game's own functions.
* UI-side facilities that exist (not triggered by the managers, found by cross-references): the UI model schema (`0x147D40088`) declares named
  events `UserSquadSizeChanged` (type descriptor `UserSquadSizeChangedEvent`, thunk `0x147D56878` -> `0x15F19F5D8`) and
  `UserTransferMoveCompleted`; the poster of `UserSquadSizeChanged` was **not found** (protected region) `[L]`. Screen controllers refresh themselves with
  the named screen events `RefreshCareerModeScreen` (`ScreenController::RefreshCareerModeScreen` `0x147ACE9C4`, one vtable slot per
  controller class), `RefreshFullScreen`, `RefreshPlayerList`, `RefreshRows` (`ScreenEventMessage` objects, `0x147FDC548`). They need the *active
  controller instance*, which Turbo cannot locate statically: **leaving and re-entering the screen is the supported refresh**, to be verified live.
* `FCEGameModes::External::ResetSquads` (message type 0xB to the game-mode layer, sent by `ITeamManagementDao` slot 37 at `dao+0x600` /
  `0x147F32218` and the NavHelper `0x147C996BC`) resets the match-side squads `[M]`; the game does not send it on transfers, so it is not part of the plan.
* **Safe thread and arguments** (lesson of `standings-ui-path.md` section 0: never call a listener with an object found by vtable
  search): call the DataController / TeamUtil functions, not listeners; derive `dc`, `teamUtil`, `pcm`, `mailbox` from the **hub**
  (re-walked from `comm` on every call) and validate each object (section 3.2). The calls run in the career-event thread (Lua pump /
  `turbo_game_call` synchronous) or in the `game_tick` hook; the listeners then run **nested inside that call**, which is the same
  situation as the game's own `MakeOffer` / `TryToRemoveFromList` event posts.

### 2.3 Where per-player runtime state is created, and can it be created from outside?

Creation points (section 1.4 table): **morale** `0x147D81108` + `0x147D93B08` (on `0x5F` for the user club, and for the whole squad by the `0x17` season-reset initialisation `0x147D90F18`); **form** `0x147E2EAD4`
(`0x5F`, tracked clubs); **status** `0x147BE7BB4`; **ranking** `0x147DA7A78`; **fitness set** (`+0x6C90`); **value** `0x147BEA034`;
**development plan** `0x147E3EFFC` / `0x147E383C8`; **contract record** `0x147E77AF4` via `AddContractRecord`/`CreateContract`; **team sheet**
(removal only, `0x145E3ED90`); **role**: a field of the contract record (`ContractParams`, element lists) `[M]`; **form events** and the
`career_*` tables are not touched by squad changes `[M]`.
**From outside, for an existing player id**: yes, two ways, in order of preference: (1) post `0x5F {pid, team, -1}` (idempotent for
ranking / value / status / development plan because each handler first looks the player up; morale returns when an entry exists; form
`AddPlayer` duplicate behaviour not checked `[L]`); (2) call the creators directly (`0x147D81108` + `0x147D93B08`, `0x147E2EAD4`, ...):
works but bypasses the gating flags (`+0x554`, `+0x70`) and is riskier. The gate `PlayerMoraleManager+0x554` is 0 on a fresh load and
after `0x17`; it is 1 after a job switch until the next season reset, in which case morale ignores `0x5F`.

### 2.4 Plain field edits

* **No notification exists.** The only DB observers in the career module are the two registered by `PlayerSearchManager` on the
  `players` table (`0x145DF47EC(table, "Player_Data_Cache_Insert_Player" / "..._Delete_Player", kind 5 / 6)`: insert / delete, **not update**; the
  only two call sites of the registrar are in `0x147D8DFB4`). Opening a player card reads the row through `DataController`, so attribute /
  appearance / contract edits show the next time it is built; overall is not cached by the card.
* Caches that **do** hold copies and are invalidated only by their own events: `SquadRankingManager` (overall*10 per user player: `0x5F` upsert),
  `PlayerValueManager` (value cache: `0x5F`, `0x4E`, day passes), `PlayerSearchManager` (GTN / search rows: refreshed on `0x17`, trimmed on `0x3B`),
  the development plan (its values take priority over the table: Live Editor's `PlayerSetValueInDevelopementPlan` stays the way), the
  scouting `PlayerDataRevealManager` records (what the user may *see* of other clubs' players), `CoachManager` (`0x36` PLAYER_GROWTH and `0x8A`
  position change are the game's own "attributes changed" events and only the CoachManager listens).
* For the user's players an idempotent refresh is posting `0x5F {pid, userTeam, -1}` again (ranking + value + status) `[L]`; do **not** replay
  `0x60`+`0x5F` for edits (it drops the morale entry and the PCM records). Needs a live test before shipping.

### 2.5 Created players

The game's youth intake (`YouthPlayerUtil::GeneratePlayer` `0x147E0F500`) does, after the DB rows: `CreatePlayer` (event `0x3A`),
`InsertTeamPlayer(pid, 0x1B29D scratch team, 99, 29)` (event `0x5F`), `PlayerMoved(0x1B29D -> 0x1B688 youth pool)`, wage
(`0x147BF5814`), stores the generated data; promotion / signing then `PromotePlayer` (`PlayerMoved` + youth table cleanup + event `0x61`) /
`SignPlayer` (`SetContractValidUntil`, `PCM::CreateContract(pid, 0x1B688, 0)`, event `0x5E`). Beyond the rows a created player needs:
event **`0x3A`** (PlayerGrowthManager starts tracking him), a **`0x5F`** into his club (morale / ranking / status / value / form /
development plan / FCE statistics team), and for the user's club a **PCM node**. Turbo creates the `players` / `teamplayerlinks` /
`editedplayernames` rows with `InsertDBTableRow`; the missing registrations are the three events and the node. Whether the
`players`-table observers (`PlayerSearchManager` cache) fire for Live Editor's insert is **unknown** (section 5). The game numbers
its own generated players from `460000` (the youth generator expects id `0x704DF` = 459999 as scratch id).
Answered in `created_players.md` (2026-10-05): rows added outside the engine's `AddRecord` are in none of the table's indexes (built at load)
and fire none of the table's insert callbacks (the `players` observers included), so the game's index-served queries miss them until a
load; the safe path is the game's own SQL INSERT through the `DataController`'s provider (plan in that file, section 6).

## 3. Implementation plan for Turbo (ordered)

### 3.1 Native side (`Turbo.dll`)

New mailbox op (next free after `kCallOpTransferList` 10), implemented like `transfer_list.*`: Lua passes `(action, pid, from, to, flags)`; the
DLL re-derives everything from the hub, validates, calls on the game thread, reads back, answers `ok / failed / queued` with a text and
two outputs. Kill switch `turbo_output\call_player_move_off.txt`; one call at a time (existing gate); queued path = existing
`run_on_game_thread`. Resolved by the signatures (`realtime_signatures.json`): `post_event`, `teamutil_player_moved`,
`dc_is_player_in_team`, `dc_insert_team_player`, `dc_squad_counts` / `dc_squad_rows` (the squad-total check of 3.2), `ctm_release_player`, `pcm_add_contract_record`, `pcm_get_player_info`,
`playerutil_delete_player`, the vtables for validation (`morale_vtable`, `ctm_vtable`, `player_added_event_vtable`, ...).

```c
using PlayerMoved_fn   = void (__fastcall*)(void* teamUtil, int32_t pid, int32_t from, int32_t to);          // 0x147DEE368
using IsPlayerInTeam_fn= bool (__fastcall*)(void* dc, int32_t pid, int32_t team);                           // 0x147B90A8C
using InsertLink_fn    = int  (__fastcall*)(void* dc, int32_t pid, int32_t team, int32_t jersey,
                                            int32_t position, uint8_t suppressEvent);                         // 0x147B90074
using ReleasePlayer_fn = int  (__fastcall*)(void* ctm, int32_t pid);                                         // 0x147B94630
using AddContract_fn   = void (__fastcall*)(void* pcm, int32_t pid, int32_t team, int32_t months, int32_t wage,
                                            int32_t k100, const void* date /*{day,month,year}: calendar+0x34*/, int32_t status); // 0x147E5BF9C
using DeletePlayer_fn  = void (__fastcall*)(void* playerUtil, int32_t pid);                                  // 0x147E9DCE0
using PostEvent_fn     = void (__fastcall*)(void* mailbox, int32_t id, void* ev);                            // 0x14060124C
```

### 3.2 Validation chain (all read-only, stop at the first failure, nothing called)

1. career loaded; `hub` re-walked `*(*(comm+0x20)+0x10)`; the UserManager's club known (`transfer_lists.md` 1b); SimDayManager idle
   (`+0x14 == 0`, `standings-ui-path.md` 5.2); no match in progress.
2. every resolved function inside FC27.exe; **`dc = **(hub+0x418)`**: pointer-shaped, `*(dc+0x10) == hub`, `*dc` a readable heap pointer whose
   vtable is inside the image; **`teamUtil = **(hub+0xF18)`**: `*(void**)teamUtil == hub`; `pcm = **(hub+0x9B8)` vtable `0x14B01E240` (`pcm_vtable`),
   `+8 == hub`; `ctm = **(hub+0x3D8)` vtable `0x14AFF6D58` (`ctm_vtable`), `+8 == hub`; `mailbox = **(hub+0x4F8)` (its `[0]` readable, vtable in the image);
   for synthesized events `*(void**)0x14C269EA8` is an allocator whose vtable slot 2 is inside the image and the event vtables equal the signature targets.
3. the player: `dc_is_player_in_team(dc, pid, from) == true` and `(dc, pid, to) == false` (also read through Lua's `teamplayerlinks`);
   `from != to`; neither is a national team; not on loan (end the loan first, existing rule); Free Agents (111592) allowed on either side.
4. limits from the ini, not constants: `maxSquad = *(int*)([[hub+0x658]]+0x58)`, `minSquad = *(int*)(...+0x64)`; `dc_squad_counts(dc, to, &rows, &loaned, &pending)`
   (`0x147B7225C`, read-only); refuse when `rows+loaned+pending+1 > maxSquad` (the game would otherwise release the lowest reserve of `to`, the user's club
   included) or when `from` is a club and `rows(from)-1 < minSquad` (the game would sign fillers / recall loans); the existing GK rule stays.
5. user arrivals only: morale gate readable: `*(u8*)(morale+0x554) == 0` (else warn "morale will not be created until the next season reset").

### 3.3 Scenarios, ordered steps, before / after, risks

**A. User club <- AI club (transfer).** Lua (`core/moves.lua`): (1) validations as today; (2) `leave_lists` for outgoing user players only;
(3) **DB writes without** `teamplayerlinks`, `playerjointeamdate`, `cm_teamsheets`: `contractvaliduntil`, `wage`, `releaseclause` (+ Turbo's `teams` set-piece
cleanup of the seller). (4) native `PlayerMoved(pid, from, userTeam)`. (5) native `AddContractRecord(pcm, pid, userTeam, months, wage, 100, &today, status 0)`
(status from `GetContractType`). (6) read back. Expected after (static): `IsPlayerInTeam(pid,user)`; `teamplayerlinks` row `{user, jersey n, position 29}`;
`playerjointeamdate` = today; `previousteam = seller`; morale container +1 entry for pid (`(end-begin)/0x60` rises by one); SquadRanking vector +1
`{pid,0,ovr*10}`; status / value / fitness-set / development plan present; the seller's runtime sheet no longer lists him; PCM node
`wage/length = Turbo's`; Squad Hub shows a real morale level after the screen is rebuilt. Risks: squad overflow auto-release (3.2.4); `0x5F`
handlers read the DB, so write contract fields first; morale gate; nested events reach Lua handlers (guarded).
**B. AI <- AI.** Steps 3 (contract fields only), 4, 6 (no PCM, no lists). Expected: form record in the buyer's table, buyer / seller sheets right, FCE stats
team switched, TM negotiations of the player re-evaluated `[M]`. Risks: `EnforceSquadLimits` side effects on both AI clubs (fillers, recalls, release).
**C. User club -> AI club.** As B with `from = user`; keep `leave_lists`; the `0x60` listeners drop morale / status / ranking / value / development plan / PCM
records and purge his emails; **the user's runtime sheet is repaired by the game** (no Lua sheet code). Release to Free Agents: `ReleasePlayer(ctm, pid)` for the user's players
(returns 0 ok / 1 / 2; show the game's reason), `PlayerMoved(pid, club, 111592)` for others.
**D. Free agent signing.** `from = 111592`; same as A/B. A signed free agent has `playerjointeamdate` and `previousteam` written by the game.
**E. Created player.** Lua inserts `players` (+ names) as today but **no** `teamplayerlinks`; native: `InsertTeamPlayer(pid, 111592, 99, 29, 0)` (event `0x5F` for the Free Agents pool),
post **`0x3A`** (synthesize: size 0x20 via the global allocator, tag `"DataController::CreatePlayer"`, vtable `0x14AFF67C0`, `+0x10 = 0x3A`, `+0x18 = pid`; `PostEvent(mailbox, 0x3A, ev)`),
then `PlayerMoved(pid, 111592, club)` and, for the user's club, `AddContractRecord`. Same path as the game's own signing, only `0x3A` is synthesized. Risks: growth flag `+0x5F0` (set to 1 by the manager's data-load routine `0x147E3F2F8` at the end of its table scan, a one-time manager setup, not a per-player switch; when exactly it runs was not traced, so confirm the flag reads 1 in a loaded career `[L]`);
player id range (`>= 460000`); the observer question (section 5).
**F. Delete.** `PlayerUtil::DeletePlayer(pid)` instead of Lua row deletes (events `0x3B`, `0x60`, FCE stats cleared, TM cleanup); keep the Lua cleanup of Turbo-only tables.
**G. Plain edit.** No call; for a user player optionally post `0x5F` again after a live test (2.4); keep the development-plan sync.
**H. Loans (follow-up).** `0x147DEC5DC` + `LoansManager::AddLoan` `0x147DE5EC0`; CPU loan expiry is driven by the `playerloans` DB rows (`0x147DDB104`), the user's by the manager list `[M]`.

### 3.4 What changes in the Lua side

`plan_move` stops writing `teamplayerlinks` (`teamid`, `jerseynumber`, `position`) and `playerjointeamdate`; `plan_sheet_add/remove` are dropped for moves (the game's runtime
sheet is right; the DB sheet is rewritten from it); `free_jersey` / `RESERVE_POSITION` become unused for moves (the game picks the number, position 29). The squad-size checks use the
ini limits. Without the native op the Lua moves stay as they are (DB only) and must say so ("shown after save + reload"; **and** the user's `cm_teamsheets` edits are overwritten at save).

## 4. Crash risks and mitigations

* **Wrong object** (the 2026-10-04 standings crash): never call listeners; hub-derived, validated objects only (3.2.2). A refusal is always the right outcome.
* **Wrong time**: during a match day (SimDayManager state != 0), inside a match, while a save is running, with the career not loaded. Existing gates.
* **Preconditions of `PlayerMoved`**: it writes join date and `previousteam` *before* checking the club link; a call with a wrong `from` silently changes those two fields. Check first.
* **Side effects**: squad auto-release / fillers (3.2.4); jersey renumbering of another player of `to`; morale gate; `0x60` drops contract records of the user's player
  (intended); FCE statistics request sent on `0x5F`.
* **Events reach Lua**: Live Editor's hook runs `post__CareerModeEvent` handlers for the posted `0x5F/0x60/0x3A` while Turbo's Lua command is mid-flight. The `S.cmd_running`
  guard exists; the handlers of `bridge.on_career_event` (state files, `manager_rules.reapply`, image pump) ran under the transfer-list events as well; keep them free of DB writes during a move.
* **Synthesized events** (only `0x3A` in the plan): wrong allocator / vtable would corrupt the heap: validate as in 3.2.2 and refuse otherwise; refcount starts 0, `PostEvent` does AddRef / Release.
* **`DataController` queries run on the game's DB provider**; the same tables Live Editor edits; do not interleave Lua DB writes with the native call (write first, call, then read).

## 5. Open questions and what must be verified live (throwaway career, save backed up)

1. **First call, read-only checks** (dev service): hub chain; `dc+0x10 == hub`, `*teamUtil == hub`; `ctm` vtable; ini `+0x58 / +0x64` values; `*(u8*)(morale+0x554)`; baseline counts
   (morale `[+0x528,+0x530)/0x60`, ranking `[+0x50,+0x58)/0xC`, PCM bucket of the player).
2. **A**: move one AI bench player to your club with `PlayerMoved` only; expect the morale / ranking counts +1, `teamplayerlinks` row, Squad Hub (re-entered) with a real morale
   level, `PlayerHasDevelopementPlan(pid) == true`, Live Editor `SetPlayerMorale` now working; then `AddContractRecord` and compare `GetPlayerInfo` with the DB (`wage`, length).
3. Does the already-open Squad Hub / Team Management refresh without leaving it? (decides whether a screen-refresh call is worth finding.)
4. Save, reload, simulate a week: the runtime sheet vs `cm_teamsheets`; no duplicate morale entry; the moved-out player not on the user's sheet.
5. `EnforceSquadLimits`: fill a test club to `MAX_SQUAD_SIZE` and add one (expect the lowest reserve released); take one from a club at `MIN_SQUAD_SIZE`.
6. **E**: create a player: does he appear in GTN / player search at once (Live Editor's `InsertDBTableRow` vs the `players` observers)? does `0x3A` give him a plan on the user's club?
7. Idempotence of a second `0x5F` for a player already in the squad (form container duplicates?).
8. `ReleasePlayer` return codes, compensation paid, `0x3E` not needed (UI-only).
9. Not traced: the poster of `UserSquadSizeChanged`, the `RosterResolverService` refresh rules (protected code), history rows of the `transfers` table, competition squad
   submission (`mSquadSubmissionState`, tournament deadlines), budgets / fees (`0x147FC18AC`, `0x147C6B0A0`), the `0x4D/0x4E` payload for a hand-built completion, why `0x7AA7` is special.

## 6. Re-proving the signatures

`realtime_signatures.json` uses the format of `transfer_list_signatures.json` (`pattern` with `??` for rip / rel32 operands, `resolve` none / rip at `offset`, `va`, `target`).
To re-check one against the image: `sys.path.insert(0,'scripts/re'); import sig_jobs; sig_jobs.find("<pattern>")` must return exactly `[va]` (that is how all 76 were
proven; the four `DataController` single-column setters are listed under `unsigned_references` because they differ only in their string operand).

## 7. Evidence index (addresses)

| what | address |
|---|---|
| PostEvent / mailbox dispatch / sync dispatcher | `0x14060124C` / `0x14230F760` / `0x141BBC7C0` |
| EventsManager ctor (registers ids 1..0xBE) / Dispatch | `0x147B56080` (loop `0x147B5614C`) / `0x147B8C0D8` |
| WriteTeamPlayersLinks / InsertTeamPlayer / CreatePlayer / DeletePlayer | `0x147BA08CC` / `0x147B90074` / `0x147B61750` / `0x147B63668` |
| TeamUtil::PlayerMoved / MovePlayerLink / loan variant | `0x147DEE368` / `0x147DEE4BC` / `0x147DEC5DC` |
| EnforceSquadLimits / ReleaseLowestReserve / AddFillers | `0x147B99BFC` / `0x147B5E558` / `0x147DE8A6C` |
| CTM ReleasePlayer / CanRelease / compensation | `0x147B94630` / `0x147B5F380` / `0x147B5EBC0` |
| PCM CreateContract / AddContractRecord / SetContract / GetPlayerInfo / HandleEvent | `0x147E5B3A4` / `0x147E5BF9C` / `0x147E77AF4` / `0x147E61F20` / `0x147E6B3A4` |
| TM HandleEvent / ExecuteDeal / ProcessPresigned / GetDealsForDay | `0x147C42494` / `0x147C531D0` / `0x147C53B8C` / `0x147C3D4A4` |
| ITransferResultHandler vtable (TM+0x2D78) / user-buy completion | `0x14B02C5A0` / `0x147FB907C` |
| morale / form / status / ranking handlers | `0x147D8D354` / `0x147E3DED4` / `0x147BF30C0` / `0x147DA0B1C` |
| fitness / value / growth / stat-view handlers | `0x147CB4E64` / `0x141549F1C` / `0x147E3E018` / `0x147BCFC58` |
| team sheet: service installer / vtable / RemovePlayer / GetAll / SetSheet | `0x145E1D1A0` / `0x14AD7E188` / `0x145E3ED90` / `0x145E26F84` / `0x145E45C40` |
| team sheet: LoadTeamSheets / TriggerSave / WriteTeamSheet | `0x147B5DB4C` / `0x147B99DE4` / `0x147B9616C` |
| PlayerUtil::DeletePlayer / PlayerRetirementManager call | `0x147E9DCE0` / `0x147D918F3` |
| youth: generate / sign / promote | `0x147E0F500` / `0x147E20D3C` / `0x147E1A4D8` |
| player-card morale text | `0x147F4C624` (`CM_Morale_Unknown`), `0x147D8B738` GetMoraleLevel, `0x147DC7508` IsPlayerLoaned |
| UI model schema / screen refresh | `0x147D40088` / `0x147ACE9C4` |
| special teams | Free Agents 111592 `0x1B3E8` (+`0x20128`, `0x20260`), youth pool 112264 `0x1B688`, generator scratch 111261 `0x1B29D`, `0x1B72C` |

## 8. The `player_move` game call (implemented 2026-10-05; static only, not yet run in the game)

Code: `turbogui/src/core/player_move.{h,cpp}` (platform-independent: validation, sequence, read-backs, mailbox words), `src/win/player_move_win.{h,cpp}` (Windows host:
signature resolution, real calls, game-thread queue, one-at-a-time gate, kill switch, Status line, log), `core/game_calls.h` (`kCallOpPlayerMove = 11`),
`src/win/game_calls_win.cpp` (op dispatch, Status lines, installer), `core/sigscan.cpp` (10 built-in signatures), tests `tests/native/test_player_move.h`
(433 checks: fake game + synthetic career), signatures `scripts/re/realtime_signatures.json` (8 of the 10) + `scripts/re/player_move_signatures.json` (2 new).
Pattern copied from `transfer_list.*` (locate / validate / `Caller` / host / op 10 handling / kill switch / queueing).

### 8.1 What it does

`TurboPlayerMove(code, pid, from, to, months, wage)` -> `ok, text, status, from_ok, to_ok` (the Lua global is defined by the Lua side from this section).

| code | name | calls | notes |
|---|---|---|---|
| 1 | MOVE | `TeamUtil::PlayerMoved(teamUtil, pid, from, to)` `0x147DEE368`; when `to` is the user's club and `months > 0`: then `PlayerContractManager::AddContractRecord(pcm, pid, to, months, wage, 100, &calendar+0x34, 0)` `0x147E5BF9C` unless the PCM already holds a record for him | Free Agents 111592 on either side; `months` / `wage` ignored for any other `to` |
| 2 | RELEASE | the user's club: `ContractTerminationManager::ReleasePlayer(ctm, pid)` `0x147B94630` (return 0 ok / 1 budget / 2 squad minimum, text carries the game's reason); any other club: `PlayerMoved(pid, club, 111592)` | `to` ignored |
| 9 | CHECK ONLY | nothing that changes anything (the read-only `IsPlayerInTeam`, `SquadCounts`, `GetLeagueOfTeam`, `IsInternationalLeague` still run) | the whole validation chain of a MOVE with the same arguments, the text lists the counts it read |

Not in scope (follow-ups): delete (`PlayerUtil::DeletePlayer`), created players (events `0x3A` / `0x5F`), loans (`0x147DEC5DC` + `LoansManager::AddLoan`), women's careers
(8.6), fees / budgets, `0x4D` / `0x4E`.

### 8.2 Mailbox op 11 (call block of `core/game_calls.h`, `i64 args[4]` in, `i64 out[2]` + text out)

```
args[0] = comm service (FeFceGMCommService plugin, as op 10)
args[1] = code | (months << 8)     code 1 / 2 / 9; months 0..120 (0 = no contract record); nothing above bit 15 (else refused: "unknown bits")
args[2] = pid | (wage << 32)       pid > 0; wage 0..10,000,000 (weekly wage, int32)
args[3] = from | (to << 32)        team ids > 0 (to is ignored by code 2)
out[0]  = from_ok                  out[1] = to_ok     1 true / 0 false / -1 not read (refused before the read-back)
          code 1: from_ok = he is NO LONGER in `from`, to_ok = he IS in `to`   (the two IsPlayerInTeam read-backs)
          code 2: from_ok = he is no longer in his club, to_ok = he is in a free-agent pool (111592, else 0x20128 / 0x20260)
          code 9: from_ok = he IS in `from`, to_ok = he is NOT in `to`          (the two preconditions; nothing was called)
text    = the sentence (<= 511 chars: longer text is cut; a test asserts the longest ones fit)   status = kCallOk / kCallFailed / kCallQueued
```
All fields are non-negative, so Lua's `code | (months << 8)`, `pid | (wage << 32)`, `from | (to << 32)` are the same words (`pm::args_from_request` is that formula, tested).
Lua's third return is the usual `M.game_call` status ("ok" / "queued" / "failed"); `from_ok` / `to_ok` are `out0 == 1` / `out1 == 1` (keep -1 as "unknown" if the
GUI shows it). A failure after `PlayerMoved` ran (stage `check`, or `call` of the contract record) says so in the text and keeps the read-backs: the game may have moved the player.

### 8.3 Validation chain (everything before any change, stop at the first failure; `pm::locate` + `pm::run`)

1. arguments: code 1 / 2 / 9, `pid > 0` and not `0x7AA7` (the player-career player: `MovePlayerLink` special-cases him), teams > 0, `from != to`, months 0..120, wage 0..10,000,000.
2. every resolved function and vtable inside FC27.exe; the hub re-walked `[[comm+0x20]+0x10]` (equal to Lua's when it passes one).
3. objects, each read through `turbo::Memory`: `dc = **(hub+0x418)` (`+0x10 == hub`, `+0` a db provider whose vtable and slot 1 are inside the image), `teamUtil = **(hub+0xF18)`
   (`+0 == hub`), PCM (`pcm_vtable`, `+8 == hub`), TransferManager (`tm_vtable`, `+8`, its pre-signed list `+0x2F30/+0x2F38` of 0x88-byte records, <= 5000, bounded vector),
   UserManager (as `transfer_list`: `tl::user_team`), the dispatcher's event sink, the calendar (vtable in the image, `+0x34` a real date), IniSettings (`+0x58` MAX_SQUAD_SIZE,
   `+0x64` MIN_SQUAD_SIZE, `1 <= MIN <= MAX <= 1000`), the LoansManager (vtable, runtime loan list `+0x40/+0x48` of 0x20-byte `{pid, club}` records, <= 20000), the SimDayManager idle
   (`svm::sim_busy`), the PlayerMoraleManager (`morale_vtable`, slot 1 == `morale_handle_event`, gate byte `+0x554`: informational, never refuses). Release also: `ctm = **(hub+0x3D8)`
   (`ctm_vtable`, `+8 == hub`), the object in slot 60 (`+0xC5` readable: `ReleasePlayer` increments it), the user's finance object (`user+0x2F0`, vtable slots 0..2 inside the image).
   The type-descriptor flag (`slot+8 -> +0x10 == 1`) is checked for the slots `transfer_list` / the standings call proved live (24, 39, 77, 127, 129) and not for the new ones
   (30, 32, 50, 57, 60, 83, 120): each of those has its own back pointer / vtable / plausibility proof and the hub builder registers every manager the same way
   (`0x147F180DA..0x147F180FB`: `array[count] = obj; ++count`), so a descriptor that differs from the proven slots' must not refuse what the object checks accept.
4. the teams: not a pseudo team or another free-agent pool (`0x1B688`, `0x1B29D`, `0x1B72C`, `0x20128`, `0x20260`); a club only if `GetLeagueOfTeam(dc, team)` is known and
   `IsInternationalLeague(league)` is false (national = leagues 78 / 2136 / 3004 or the runtime id at `0x14BE9C9C0`, the game's own definition, section 8.5); Free Agents 111592 is accepted as is.
5. the player: `IsPlayerInTeam(pid, to)` false and `(pid, from)` true (the game's own SELECT; `PlayerMoved` writes join date and previous team BEFORE its own check, so a wrong `from` must never reach it),
   not in the LoansManager's list.
6. the squad limits, read at run time: `to` a club: `rows + loanedOut + pendingSigned + 1 <= MAX_SQUAD_SIZE` (else `EnforceSquadLimits` would release his lowest-value reserve, the user's club included);
   `from` a club: `rows - 1 >= MIN_SQUAD_SIZE` (else fillers / CPU-loan recalls). Both from `DataController::SquadCounts`. Free Agents has no limit on either side (the game skips it too).
7. user arrival: the morale gate byte is read and reported (closed gate: "no morale entry until the next season reset").

Then the call, then the read-backs: `IsPlayerInTeam` (to / from), the squad counts again (a changed squad beyond the move is reported as a WARNING, not an error), the contract record
(PCM hash walk of `tl::contract_status`: key, team `+8`, wage `+0xC`; a record that already exists is left alone and reported, one that names another team is flagged).
Release: `ReleasePlayer` return code 1 / 2 / unknown is refused with the game's reason; the read-back accepts any of the three free-agent pools and names a non-default one.

### 8.4 Host (`player_move_win.cpp`)

Kill switch `turbo_output\call_player_move_off.txt` (plus every game-hook switch; the core part `pm::killed(dir)` is tested). Queued path: from the game thread it runs at once, otherwise
`run_on_game_thread` (game_tick hook, else the next career-mode event) and the result is published into the call block; one call at a time (an atomic gate, a flag older than 60 s is taken over so a
dropped queue entry cannot block it for good; a second request gets stage `busy`). Status line `player_move: ready | player_moved ..., in_team ..., squad_counts ..., league ... / ..., add_contract ...,
release ... | vtables pcm, tm, um, ctm | runs n (ok n, queued n, busy n)` plus `  release: off (...)` / `  morale gate: not checked (...)` / `  last: ...`; log label
`game call player_move(<move|release|check>, player, from, to, months, wage, comm): ok|failed [stage] text (from_ok, to_ok, user team, dc, team util)`.
`install_player_move()` is called from `install_game_calls` (right after the hooks), so `overlay_dx12.cpp` is untouched. The calls are plain `__fastcall` function pointers (MS x64 ABI = MinGW's default):
`PlayerMoved(void*, int, int, int)`, `uint8_t IsPlayerInTeam(void*, int, int)` (`& 1`), `void SquadCounts(void*, int, int*, int*, int*)`, `int GetLeagueOfTeam(void*, int)`,
`uint8_t IsInternationalLeague(int)`, `void AddContractRecord(void*, int, int, int, int, int, const void*, int)`, `int ReleasePlayer(void*, int)`.

### 8.5 Signatures: how each was resolved

All patterns come from `realtime_signatures.json` (byte-for-byte, proven unique in the image with `sig_jobs.find` again for this work) and sit in the built-in table of `core/sigscan.cpp` under the same names;
`tests/native/test_player_move.h` resolves every one on the game's own bytes (read from `fc27_image.bin`), proves it unique next to its look-alikes (the 5-argument `PlayerMoved` overload `0x147DEE410` has
the same prologue up to `mov r10,[rcx]`) and compares it with the JSON.

| name | VA / target | kind |
|---|---|---|
| `teamutil_player_moved` | `0x147DEE368` | function |
| `dc_is_player_in_team` | `0x147B90A8C` | function |
| `dc_squad_counts` | `0x147B7225C` | function |
| `pcm_add_contract_record` | `0x147E5BF9C` | function |
| `ctm_release_player` | `0x147B94630` | function |
| `ctm_vtable` | lea at `0x147F180CB` -> `0x14AFF6D58` | vtable (built inline by the hub builder; rip operand at +8) |
| `morale_vtable` / `morale_handle_event` | ctor `0x147D7DE08` -> `0x14B0156A8` / `0x147D8D354` | vtable + its slot 1 (read in the image: `[0x14B0156A8+8] == 0x147D8D354`) |
| `pcm_vtable`, `tm_vtable`, `um_vtable` | existing `transfer_list` entries | vtables |
| **`dc_get_league_of_team`** (new) | `0x14154B92C` | function: `int (DC*, int team)`: the game's cache (global `0x14C36C220`) or `SELECT leagueid FROM leagueteamlinks WHERE teamid` (exactly one row, else -1) |
| **`is_international_league`** (new) | `0x14479F50C` | leaf: `bool (int league)`: `0x4E` / `0x858` / `0xBBC` or the runtime id at `0x14BE9C9C0` |

The seven `unsigned_references` need no signature for this call and none was added: they were checked by disassembly instead. `teamutil_is_player_in_team_thunk` `0x147DEC458` is
`mov rax,[rcx]; mov rcx,[rax+0x418]; mov rcx,[rcx]; jmp 0x147B90A8C` (so Turbo calls `IsPlayerInTeam` with the DataController directly, no thunk); `dc_set_join_date` `0x147B9E7B8`,
`dc_set_contract_valid_until` `0x147B9E138`, `dc_set_release_clause` `0x147B9739C`, `dc_set_wage` `0x147B972F0` each build `UPDATE players SET <playerjointeamdate | contractvaliduntil | releaseclause | wage>
WHERE playerid` (their string operands, read in the image; `PlayerMoved` calls the first one, `CreateContract` the second); the allocator global `0x14C269EA8` and the service registry `0x14BE28208`
are used exactly as described (`mov rcx,[rip+..]; mov rax,[rcx]; call [rax+0x10]` with `(size, tag, 0)` in `TriggerSave` `0x147B99E11`, `mov edx,0x113EA820; call [rax+0x38]` in `MovePlayerLink`). None of the seven is
called or read by `player_move` (it synthesizes no event).

### 8.6 Corrections to sections 1-5 and what could not be verified (static check of this implementation)

Found wrong (the code says otherwise; the older text above is left as written):

1. **2.1 "`from` = Free Agents ... is not a special case of `EnforceSquadLimits`' `from` check"** is wrong: `0x147B99CC3..0x147B99CE8` skips `from` when it is `0x1B29D`, `0x1B72C`, `IsFreeAgentTeam(from)` or `0x1B688`, exactly as for `to`. A free agent leaving the pool needs no squad-minimum check.
2. **1.7 / 3.3C "`ReleasePlayer` passes `(pid, userTeam, 111592)`"**: `from` is `DataController::GetPlayerTeam(dc, pid)` `0x14154C5C4` (his club link: the `teamplayerlinks` row whose team's league is not 78 / 2136, with fallbacks for the free-agent / youth leagues, the exact fallback order is `[M]`), not the user's team; `to` is `0x1B3E8`
   unless that team id is in the SORTED int vector at `DataController+0x108 / +0x110` (`std::binary_search`, `0x147A91630`), then `0x20128`. The compensation is added to `[activeUser+0x300]` and applied through the finance object at `user+0x2F0`; on success the byte `+0xC5` of the object in slot 60 is incremented.
3. **1.7 "`CanRelease` ... 2 squad at `MIN_SQUAD_SIZE`"**: code 2 is `0x147DEC2F8(teamUtil, team, MIN_SQUAD_SIZE) != 1`, i.e. the number of NON-LOANED players of the club (`IsPlayerLoaned` over the club's player list) is below the minimum BEFORE the release. With exactly the minimum the game releases him and `EnforceSquadLimits` then signs a filler.
   Turbo refuses that case itself (`rows - 1 < MIN`). Code 1 is `[finance object vf[2]()+8] < compensation`.
4. **3.2.3 "neither is a national team"** had no mechanism. The game's own predicate is `IsInternationalLeague(GetLeagueOfTeam(dc, team))` (`GetPlayerTeam` compares the league with `0x4E` / `0x858` itself, the nation lookup `0x147B85A10` calls both functions; `teamnationlinks.leagueid` is 78 / 2136 for national teams). Lua's `teamnationlinks` set and the DLL's check agree on national teams.
5. **hub slot layout wording** ("holder at `+0x18`, object `**holder`"): `slot+0x10` is the instance COUNT and `slot+0x18` points to an ARRAY of instance pointers (`array[count] = obj; ++count` in the hub builder); `**(slot+0x18)` is `array[0]` and every statement in the text still holds.
6. **1.3 / 3.2.4 `from` side of `EnforceSquadLimits`**: the trigger is `teamplayerlinks rows(from) < MIN_SQUAD_SIZE` only (loans / pre-signed deals do not enter it); they decide recall (`0x147DCCD2C`) versus fillers (`0x147DE8A6C`): `loaned + pending + MIN > MAX` recalls, else it signs `MIN - rows` fillers.
7. **`SquadCounts`** details the text lacked: it writes nothing when `team <= 0` (Turbo presets the three outputs to -1 and treats an unset one as a failure); `loanedOut` counts records whose `+4` equals the team (the pid key is at `+0`); `pendingSigned` counts the 0x88-byte records of `TM+0x2F30` whose `+4` equals the team ("buyer" is `[M]`).
8. `IsPlayerInTeam` runs a `SELECT playerid ... WHERE teamid` and scans the returned rows: for the Free Agents team (thousands of rows) every call costs a full scan; Turbo calls it twice before and twice after a move and, for a release, up to three more times.

Not verifiable without the game (all marked for the live test, section 8.7): the runtime effects of the `0x5F` / `0x60` listeners and whether the open Squad Hub refreshes; the gate byte value in a loaded career; whether the type descriptors of slots 30 / 32 / 50 / 57 / 60 / 83 / 120 carry flag 1; the content of the `dc+0x108` vector and of
`UserManager+0x2D` (the free-agent pool is `0x20128` when that byte is 1 at `0x147D92CD5`: probably a women's career `[L]`; Turbo supports 111592 only); the runtime league cache at `0x14C36C220` (a team with several `leagueteamlinks` rows gets -1 from the DB path, which Turbo refuses as "knows no league"); the real `IniSettings`
values; that Lua handlers reached by the posted events stay harmless during the call; the order of `AddContractRecord` against the `0x5F` listeners (the status listener asks the contract data while the event is dispatched); the side effects of `ReleasePlayer` on a player of another club (Turbo never does it);
`TeamUtil+8` (an int the builder zeroes; not checked); whether a save in progress is detectable (only the SimDayManager state is).

### 8.7 First live test (throwaway career, save backed up; dev service for the reads)

1. Status tab: `player_move: ready | ...`; `TurboPlayerMove(9, pid, from, to, 36, 25000)` on an AI bench player -> `ok`, counts in the text match the Squad Hub (`MAX_SQUAD_SIZE` / `MIN_SQUAD_SIZE` from the log of the first call).
2. read-only reads as in section 5.1 (`dc+0x10 == hub`, `*teamUtil == hub`, `ctm` vtable, the three type descriptors, `um+0x2D`, `dc+0x108` vector, `*(u8*)(morale+0x554)`).
3. code 1 with `months 0` (PlayerMoved only): morale count `[+0x528,+0x530)/0x60` +1, ranking +1, `teamplayerlinks` row; then code 1 with months / wage on another player: `GetPlayerInfo` wage / length equal the arguments.
4. code 2 on a user's player (compensation paid, record gone, he is in `111592`) and on an AI player; a code 9 on a full club (expect the refusal text), a national team, a loaned player.
5. kill switch file: Status says off; the queued path from a non-game thread (the result arrives through the call block).
