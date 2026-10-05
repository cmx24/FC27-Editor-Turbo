# Block Offers, squad roles, morale and "likes the club" (FC 27): RE notes and a recipe for Turbo

Source: `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (FC27.exe 1.0.140.64835, build key `6AB9813C-211EF000`, base
`0x140000000`), static analysis only (2026-10-05; the game was not touched, no dev service, no Live Editor binary read).
Confidence: `[H]` read in the code, `[M]` strong inference, `[L]` guess. Signatures: `scripts/re/player_status_signatures.json`
(`bash scripts/re/py.sh scripts/re/sig_player_status.py --check` proves all 32 patterns unique and the rip targets). Helper scripts
added for this track (all read-only on the image): `xrefs.py` (callers + vtable slots, no ripscan.exe needed), `dfn.py` (disassemble
the whole function around a VA), `dispscan_ref.py` (every `[reg+disp]` user), `ser_fields.py` (save-serializer member names and
offsets), `reflect_fields.py` (UI struct member offsets), `imm_scan.py`, `hub_managers.py` (manager table from the hub builder).
Reads with `docs/re/transfer_lists.md` (sections 0 to 5: how the hub, dao and helper are reached, calling conventions,
signature scheme) and `docs/re/game_thread.md` (game thread, `PostEvent`).

## 0. The four answers

1. **Block Offers** is a **sibling of the list helpers**: `UserActionsHandlingHelperImpl` vtable **slot 30**
   `0x147F688B8` `ToggleTransferBlock(this, int playerId)` (slots 31 / 32 / 33 are TryToRemoveFromList / AddToTransferList /
   AddToLoanList) `[H]`. It does not write the PlayerContractManager node. The state is a **flag-0 entry `{playerId, 0}` in an
   eastl vector inside the TransferManager at `TM+0x2F50`**, mirrored by a cache object at `TM+0x2D38`; the helper updates both,
   removes the player from the transfer / loan lists and posts `TransferBlockedByUserEvent` (id `0xBE`) `[H]`. It is a **toggle**:
   call it only when the player is not blocked yet (the cache tells).
2. **Squad role** is stored by the **PlayerStatusManager (type 87)**, user club only: a fixed vector of **52 entries of 8 bytes** at
   `PSM+0x18` (count at `+0x14`, user team id at `+0x10`); entry = `{int32 playerId, int8 role, u8 wasPromised, u8 renewalDismissed, pad}`;
   roles **1 Crucial, 2 Important, 3 Rotation, 4 Sporadic, 5 Future (Prospect)**, `0xFF` = none `[H]`. The FC 26 layout of
   `features/squad_role.lua` is right (offset `0x18`, 8-byte entries, role at `+4`) but the role is a **byte**, not an int `[H]`
   (section 3.4). There is no `career_playercontract` table in FC 27 `[H]` (the DB has no such table), so the Lua DB pass is a no-op.
3. **"Likes the club"**: the morale record has **no such factor** (four components only, section 4). The game has two real
   club-attachment mechanisms, both plain database fields (no game call needed): the **love / grudge table `player_grudgelove`**
   `{playerid, emotional_teamid, level_of_emotion 0..7}` `[H]` (levels 1..5 are grudges `[M]`, 6 and 7 love `[M]`) and the **career
   trait `CAREER_ONE_CLUB_PLAYER` = `players.trait2` bit `0x400`** `[H for the bit, M for the effect]`. Which one the user means must be
   confirmed (section 5, open question 1).
4. **Morale "Unknown -2 OVR"**: a normal signing makes the game post **event `0x5F` (PlayerJoinedTeam)**; the PlayerMoraleManager
   reacts by creating a **0x60-byte morale record** for the player (`MoraleStore::Create` + `InitMorale`), the PlayerStatusManager
   adds the **role entry**, the TransferManager, FitnessManager, PlayerDataRevealManager and others update their own lists.
   Turbo's move writes only the database (`teamplayerlinks`), so **none of them is told** `[H]`: the player has no morale record,
   no role entry and "Unknown" morale. The game's own function for the database write + both events is
   `DataController::WriteTeamPlayersLinks` `0x147BA08CC` (section 4.4).

## 1. Reaching the objects (all `[H]`)

Hub = `[owner+0x10]` as in `transfer_lists.md` section 1. A manager of type `T` is `**(void***)(hub + T*0x20 + 0x18)` (the slot holds
a pointer to a holder whose first qword is the object; the code does `mov rax,[hub+OFF]; mov rax,[rax]`).

| manager | type | hub offset | size | ctor (vtable lea) | vtable | HandleEvent (slot 1) | seen in code |
|---|---|---|---|---|---|---|---|
| PlayerStatusManager | 87 | `+0xAF8` | 0x38 | `0x147BE47B8` (+6) | `0x14AFFB2A0` | `0x147BF30C0` | `mov rax,[rcx+0xaf8]` in `0x147F9FAA0`, `0x147F4C4A0` |
| PlayerMoraleManager | 83 | `+0xA78` | 0x5B8 | `0x147D7DE08` (+0xF) | `0x14B0156A8` | `0x147D8D354` | `mov rax,[rcx+0xa78]` in `0x147E37ED4`, `0x147B5EFF4` |
| TransferManager | 127 | `+0xFF8` | 0x2FA0 | `0x147C26450` | `0x14B0055A8` | `0x147C42494` | transfer_lists.md |
| PlayerContractManager | 77 | `+0x9B8` | 0x458 | `0x147E54FB0` | `0x14B01E240` | `0x147E6B3A4` | transfer_lists.md |
| DataController (players / teams DB access) | 32 | `+0x418` | 0x5E8 | `0x147B5500C` | (not a manager vtable) | | `mov rax,[rcx+0x418]` everywhere |
| event dispatcher | 39 | `+0x4F8` | | | | | `PostEvent(*[hub+0x4F8], id, ev)` |
| UserManager | 129 | `+0x1038` | | | | | transfer_lists.md |
| DynamicOverallManager | 148 | `+0x1298` | | | | | told by `SetTotalMorale` |
| FitnessManager / CalendarManager | 45 / 24 | `+0x5B8` / `+0x318` | | | | | |

`scripts/re/hub_managers.py` prints the whole table recovered from the hub builder (63 managers). The PSM and PMM allocations:
`"PlayerStatusManager"` size 0x38 at `0x147F18E92..0x147F18EC3` (ctor `0x147BE47B8`), `"PlayerMoraleManager"` size 0x5B8 at
`0x147F18BC8..0x147F18C06` (ctor `0x147D7DE08`).

## 2. Block Offers

### 2.1 The UI action and its helper `[H]`

The Squad hub's player actions are, in this order (the lead's live look): Negotiate Contract, Delegate Contract Negotiation, Add to
Transfer List, Add to Loan List, **Block Offers**, Release Player, ... The list actions are UAH slots 32 / 33; Block Offers is the slot
right before them:

```
UAH vtable 0x14B029440:  slot 27 0x147F68878 (a MissionManager call)   slot 29 0x147F6949C (renewal-dismissed query)
 slot 30 +0xF0 0x147F688B8  ToggleTransferBlock(this, int pid)           <- Block Offers
 slot 31 +0xF8 0x147F8E300  TryToRemoveFromList(this, pid, bool loan)    slot 32 0x147F68368 AddToTransferList   slot 33 0x147F68214 AddToLoanList
```
`xrefs.py 0x147F688B8` finds exactly one reference, the qword at `0x14B029530` = `0x14B029440 + 0xF0`. Body (annotated, `0x147F688B8`):

```
this = rcx (r14), pid = edx (spilled at [rsp+0x10] = [rbp+0x48])
tm   = [[[this+8]+0xFF8]]            (r13)                 um = [[[this+8]+0x1038]]
dao  = [tm+0x2D38]                   (rbx)  CachedTransferblockDaoImpl, vtable 0x14B005400
user team id = GetUserClub(GetActiveUser(um),0)+4   ->  [rbp+0x58]
blocked = dao->vf[1](pid)                                   ; 147F68913  call [rax+8]
if blocked:  dao->vf[7](pid)                                ; 147F68929  call [rax+0x38]   unblock
else:        dao->vf[4](pid)                                ; 147F68931  call [rax+0x20]   block
             TransferManager::RemoveFromLists(tm, pid, &removedT, &removedL, mask 1)   ; 147F6894A call 0x147C3F588
             erase the player from the sets at tm+0x2B38 (two calls 0x147ABCBC4 / 0x147ABCAA4)     ; offers made / received
             svc = [0x14DAF08A8]; svc->vf[0](&h); h->vf[4](pid, tm+0x2710) ; 147F689BC..689DD  (service object, see 2.4)
ev = alloc(0x20, "TransferBlockedByUserEvent")  ; vtable 0x14AFF7570, +8 = 0, +0x10 = 0xBE, +0x18 = pid, +0x1C = (blocked was false)
PostEvent(*[hub+0x4F8], 0xBE, ev)                          ; 147F68A6C call 0x14060124C
```
So `ev+0x1C` is 1 when the call **blocked** and 0 when it **unblocked**. No return value, no argument check, no team check: the
state is global by player id, the helper is meant for the user's own players (so is the transfer-list helper).

### 2.2 The record `[H]`

* `TM+0x2D38` = **CachedTransferblockDaoImpl** (0x50 bytes; allocation name at `0x147C27050`, stored by the TM ctor at `0x147C270B0`);
  `+8` = the inner `TransferblockDaoImpl` (0x18 bytes, vtable `0x14B005630`, `+0x10` = hub, stored at `TM+0x2C30`);
  `+0x10 / +0x18` = **vector A** of int32 player ids (flag 0), `+0x30 / +0x38` = **vector B** of int32 ids (flag 1).
  Slots: `vf[1] 0x147D702CC IsInA(pid)`, `vf[2] 0x147D70304 IsInB(pid)`, `vf[3] 0x147D73E14` move to B (flag 1), `vf[4] 0x147D73DB8` move
  to A (flag 0), `vf[5]/vf[6]` fill a vector from the inner store, `vf[7] 0x147D73F2C` remove from A.
* The **authoritative store** (what the save holds) is the TransferManager's own vector: `begin = [tm+0x2F50]`, `end = [tm+0x2F58]`,
  `cap = [tm+0x2F60]`, **8-byte entries `{int32 playerId, u8 flag, pad[3]}`** `[H]` (`TransferManager::IsInBlockList(tm, pid, flag)`
  `0x147C471EC`: `mov r9,[rcx+0x2f58]; mov rax,[rcx+0x2f50]; cmp [rax],edx; cmp [rax+4],r8b`; the inner dao's `0x147D73D48(dao, pid,
  flag)` updates the entry or appends one). `TM::ClearDaoCaches` `0x147C59538` (called from `TM::HandleEvent`) clears vectors A / B and
  refills them from this store (flag 0 into A, flag 1 into B).
* **flag 0 = Block Offers** (user action `0x147F688B8`; the Transfer hub status index 9 `0x147F79354` and the squad row's
  `HasOffersBlocked` byte both test flag 0) `[H]`. **flag 1** is set by `ScreenControllerSellPlayers::HandleRelease` (`0x147C59934`,
  the string at `0x147C59981`; it calls `dao->vf[3]` after posting event `0x3E`): a released player gets the "transfer blocked" status
  badge `[M]` (the status-flags builder sets bit 8 = `cm_playerstatus_transferblocked` from `dao->vf[2]`, `0x147EA10E8`).
* The squad screen's player status struct (reflection `0x147D49BE4`, size 0x30) has **`HasOffersBlocked` at `+0x19`** (written by
  `0x147F99061`: `rcx = [tm+0x2D38]; call [rax+8]; mov [rsi+0x19], al`, only for a manager career); the other members are
  `TimeAtClub +0, EnergyLevel +4, Role +8, PositionInTeamSheet +0xC, PitchPositionInTeamSheet +0x10, IsRetiring +0x14, IsLoanedIn
  +0x15, HasPrecontract +0x16, HasPendingNegos +0x17, HasOngoingConversation +0x18, HasOffersBlocked +0x19, IsContractRenewalDismissed
  +0x1A, IsOnInternationalDuty +0x1B, IsSuspended +0x1C, IsInjured +0x1D, InternationalReputation +0x20, WasYouthPlayer +0x24,
  IsLocalPlayer +0x25, Stamina +0x28, InternationalTeamId +0x2C` (`reflect_fields.py 0x147D49BE4`).
* The PlayerContractManager node (`+0x34` status 0 / 7 / 8 / 9) is **not** where Block Offers lives. The toggle affects it only through
  `RemoveFromLists` (a listed player is unlisted, status back to 0).
* Error texts built from it (UAH `0x147F8B53C` / `0x147F8B694` / `0x147F8B854`): `CANNOT_BUY_TRANSFERBLOCKED`, `CANNOT_LOAN_TRANSFERBLOCKED`,
  `CANNOT_SIGN_TRANSFERBLOCKED`, `CM_Transfer_CannotApproach_Transferblock`: AI offers and the user's approaches to a blocked player
  are refused.

### 2.3 Threading and events

Runs on the game thread like the list helpers (it reads and writes the TM and posts an event). Posts `TransferBlockedByUserEvent`
(`0xBE`, object 0x20 bytes, `+0x18` pid, `+0x1C` blocked-now). Nothing else is needed for the Squad hub to show it: the status byte is
read from the cache when the screen builds its rows (reopen the screen; the event is what the game itself uses to refresh).

### 2.4 Unknown part `[L]`

`svc = [0x14DAF08A8]; svc->vf[0](&h); h->vf[4](pid, tm+0x2710)` (blocking branch only) is a service-object call whose purpose
(withdraw the pending AI offers for the player, most likely) was not traced. It is part of the game's own path, which is why the
recipe calls the helper and does not write the vector directly.

## 3. Squad roles

### 3.1 The record `[H]`

PlayerStatusManager `0x38` bytes: `+0 vtable 0x14AFFB2A0`, `+8 hub`, **`+0x10` int32 team id** (the user's club, -1 before init; the
events compare `[ev+0x1C]` with it), **`+0x14` int32 count of used entries (0..52)**, **`+0x18` pointer to the entries**,
`+0x20` end (`begin + 0x1A0`, fixed), `+0x28` capacity end (same), `+0x30` allocator. `PSM+0x10` is also passed as the "status data"
object `sub` to the helpers below (sub+0 team, sub+4 count, sub+8 begin).

Entry (8 bytes): `+0 int32 playerId` (-1 = empty), **`+4 int8 role`** (1..5; 0xFF = none), `+5 u8 wasPromised` (the save member
`mWasPromised`), `+6 u8 renewalDismissed` (`mRenewalDismissed`; also `PlayerStatus.IsContractRenewalDismissed`), `+7` pad. Used
entries come first, then empty entries `{-1, 0xFF, 0, 0}` up to 52. The save serializer (`0x147EDD960`): `mPlayerId +0`, role via
`0x147EF4CCC` at `+4`, `mWasPromised +5`, `mRenewalDismissed +6` (`ser_fields.py`).

Functions (signatures: `psm_*`):

| address | meaning | evidence |
|---|---|---|
| `0x147D8BADC` `Find(sub, pid)` | scans entries 0..51 of `[sub+8]`, stops at pid -1, returns entry or null | `cmp r9d,0x34; mov eax,[rdx]; cmp r11d,eax; cmp eax,-1` |
| `0x147D811D0` `AddEntry(sub, pid)` | `if count < 52: entry = begin + count*8; entry.pid = pid; count++`; **no duplicate check**, role bytes untouched | disassembly |
| `0x147BE7BB4` `AddPlayer(psm, pid, role)` | `AddEntry(psm+0x10, pid)`; role 0 or 0xFF = auto: `role = ComputeSquadRole(&hub, team, pid)` (`0x147E9DED8`), `wasPromised = 0`; role 1..5 = stored as given, `wasPromised = 1`; `entry+6 = 0`; returns 0 when the table is full | `lea eax,[rbx+r9]; cmp al,bl; jbe` |
| `0x147D90C80` `Remove(sub, pid)` | memmove down, count--, appends an empty entry | disassembly |
| `0x147D91228` `Clear(sub)` | `sub+0 = -1`, count 0, all 52 entries `{-1,0xFF,0,0}` | disassembly |
| `0x147BF5D40` | event `0x17` rebuild: `Clear`, `sub+0 = userTeam`, `0x147EA3354` recomputes every squad player's role with `0x147E9E0BC` and re-adds them (**`wasPromised` 0**) | `0x147EA3573..0x147EA3582` |

`HandleEvent` (`0x147BF30C0`): `0x5F` (PlayerJoinedTeam, `ev+0x18` pid, `ev+0x1C` new team, `ev+0x20` old team): if `ev+0x1C == psm+0x10`
query the TransferManager object at `TM+0x2C70` for the player's contract record `[M]` (a signing has one: its role, a Turbo move has none) and call `AddPlayer(psm, pid, role from the contract, else 0xFF)`;
`0x60` (PlayerLeftTeam, `+0x1C` old team): `Remove`; `0x17` (season reset / new career, only when `ev+0x30 != 0 || ev+0x18 != 0`):
rebuild; `5`: `0x147EA898C`. **So the roles are recomputed at every season reset**; Turbo must re-apply after `SEASON_RESET`
(event 23 / `0x17`) if it wants them to persist, and a changed user team (new job) rebuilds the table.

The squad screen row builder (`0x147F987B4`, role read at `0x147F99BD3`: `rax = Find(PSM+0x10, pid); [row+8] = (int8)[rax+4];
[row+0x1A] = [rax+6]`) and the other readers (`0x147ACD6F8`, `0x147D6F260`, `0x147F9FAA0`, ...) all **read** the entry; **nothing else
in the image writes `entry+4`** except `AddPlayer` and the rebuild loop (every call site of `Find` was checked). When a user player
has **no entry** the UI computes the role on the fly: `0x147F4C4A0` does `AddPlayer(psm, pid, 0xFF)`, reads the entry, then `Remove`
(a peek) `[H]`. So a Turbo-moved player shows a computed role until he gets a real entry.

### 3.2 Role numbers `[H]`

`0x14073DD80` builds the role list: `{1 CM_CW_Role_Crucial_upper, 2 ..._Important_upper, 3 ..._Rotation_upper, 4 ..._Sporadic_upper,
5 ..._Future_upper}` (`mov byte [rbp-0x30],1` before the Crucial string, then 2, 3, 4, 5). `ComputeSquadRole` returns the rank bucket
1..5 (`0x147E9E08C` / `0x147E9E0BC`), a Future (5) older than 23 becomes Sporadic (4) (`cmp [rsp+0x48],0x17 ... cmovg edx,4`), 0xFF when
unranked. The role also feeds morale: `0x147D8300B` indexes a per-role expectation table with it (0xFF read as 5) and the game's own
readers treat 4 and 5 as "not a regular" (`0x147C40315`) and 1 as key player (`0x147E33A95`).

### 3.3 Hub / UI side effects

No event is posted when a role is read or changed by the UI itself (the contract flow adds roles through event `0x5F`). Changing the
byte in memory is invisible to the engine until a screen rebuilds its rows; reopening the Squad hub is enough `[M]`. The role morale
component (`mSquadRoleTotalMorale`) is refreshed by the morale manager's own events.

### 3.4 What is wrong in `features/squad_role.lua` for FC 27 `[H]`

* the entry is `{int pid, int8 role, u8 promised, u8 renewalDismissed}`: `mem.int(e+4)` reads `role | promised<<8 | dismissed<<16`,
  which exceeds 5 for every promised player (so `locate` rejects the vector: `valid == matched` fails), and `MEMORY:WriteInt(e+4, role)`
  would clear `wasPromised` and `renewalDismissed`. Read and write **one byte**.
* the vector is `{begin +0x18, end +0x20, cap +0x28}` with a **fixed length of 52** (empties `pid -1` included); the used count is the
  int at `+0x14`. The search heuristic is unnecessary: validate the layout (section 7.2) and use it.
* `career_playercontract` does not exist in FC 27 (`turbo_probe_*.txt`: "table not found"), so the DB pass writes nothing.

## 4. Morale

### 4.1 The record `[H]`

PlayerMoraleManager (0x5B8 bytes, vtable `0x14B0156A8`): `+0x10` a 0x508-byte settings object (ctor `0x147D7ED54`), **`+0x518` the morale
store**, `+0x548` last update date (day, month), `+0x550` year, **`+0x554` byte "not initialised yet"** (set by event `0x73`, cleared by
the season init; while set, events `0x5F` / `0x60` are ignored), `+0x558..` a 53-slot ring of 8-byte `{pid, emotion-1}` pairs `[M]` (cache of the
no-record path `0x147D8B43C`), `+0x598..+0x5A0` a vector of 12-byte `{pid, float, float}` (previous values, trimmed on `0x60`).

Store at `PMM+0x518`: `+0` int32 **team id of the store** (the user's club; `GetMorale(team, pid)` only reads the store when the
team matches), `+0x10 begin`, `+0x18 end`, `+0x20 cap` of a vector of **0x60-byte records, at most 52** (`0x147D81108` refuses at
`cmp esi,0x34`).

Record (0x60 bytes; ctor `0x147D7DD14`, copy `0x147D791CC`, save serializer `0x147EDD28C` via `ser_fields.py 0x147EDD1D0`):

| off | member | notes |
|---|---|---|
| `+0x00` | `mPlayerId` int32 | key; default -1 |
| `+0x04` | int32 `players.emotion - 1` (0..7) | `0x147B866BC` = `SELECT emotion FROM players WHERE playerid` (field range 1..8); picks the level thresholds (`MORALE_LEVELS_%s`, `0x147D837D8(pmm, total, rec[+4])`) |
| `+0x08` | `mLeagueObjectiveMorale` | |
| `+0x0C` | `mDomesticCupObjectiveMorale` | |
| `+0x10` | `mContinentalCupObjectiveMorale` | |
| `+0x14` | `mLastMatchResultMorale` | |
| `+0x18` | `mTeamPerformanceTotalMorale` | UI `TeamPerformanceMorale` |
| `+0x1C` | `mPlayerPerformanceTotalMorale` | UI `PlayerPerformance` |
| `+0x20` | `mPlayerWageMorale` | |
| `+0x24` | `mContractLengthMorale` | |
| `+0x28` | `mContractTotalMorale` | UI `ContractMorale` |
| **`+0x2C`** | **`mTotalMorale`** | **the morale value the game shows and uses (0..120; `GetMorale` returns it)** |
| `+0x30` | `mPlayTimeMorale` float | |
| `+0x34` | `mSquadRoleTotalMorale` int, clamped to 120 | UI `SquadRoleMorale` |
| `+0x38` | `mStartingGameBenchExpectationMorale` float | |
| `+0x3C` | `mSuspendedForNextMatch` byte | |
| `+0x40..+0x58` | vector of 0x48-byte modifier entries (`float` at `+0`, type byte at `+8`) | `0x147D89D64(rec, type)` sums them |

The squad screen's `PlayerMorale` struct (`0x147D47B38`, 0x28 bytes): `OverallMoraleValue +0, ContractMoraleValue +4, SquadRoleMoraleValue +8,
PlayerPerformanceValue +0xC, TeamPerformanceMoraleValue +0x10, OverallMorale +0x14 (level), ContractMorale +0x18, SquadRoleMorale +0x1C,
PlayerPerformance +0x20, TeamPerformanceMorale +0x24`: **four factors (contract, squad role, player performance, team performance); there
is no affection / loyalty factor** `[H]`. LE's levels (very unhappy < 15 < unhappy < 40 < content < 65 < happy < 75 < very happy < 95 <=
complacent) refer to `+0x2C`.

`GetMorale(pmm, team, pid, ctx)` `0x147D8B528`: `team == store team ? (record ? [rec+0x2C] : 0) : (AI: computed 0x147D8A204)`.
Morale exists only for the user's club (LE: "only for player in user team").

### 4.2 What a normal signing creates `[H]`

`PlayerMoraleManager::HandleEvent` (`0x147D8D354`), event `0x5F`:

```
if (pmm[+0x554]) return;                        // season init not done yet
if (ev[+0x1C] != userTeam) return;              // new team must be the user's club
pid = ev[+0x18];  if (MoraleStore::Find(pmm+0x518, pid)) return;  if (pid <= 0) return;
v   = DataController::GetPlayerEmotionType(dc, pid)                 // 0x147B866BC, players.emotion
rec = MoraleStore::Create(pmm+0x518, pid, v - 1)                    // 0x147D81108, null when 52 records
if (rec) InitMorale(pmm, rec, /*prev*/ 0)                           // 0x147D93B08
```
and event `0x60` (new team `ev+0x1C` = the leaver's old club == user): erase the record (`0x60`-byte vector erase at `0x147D8D5C2`) and
the 12-byte entry. The season init `0x147D90F18` (event `0x17`) rebuilds the store for the whole squad the same way
(`Create` + `InitMorale(prev)` per player, carrying the old `+0x30` / `+0x38` values).

`InitMorale` computes the components (`0x147D8ACB8`, `0x147D8A984`, `0x147D8A398`, `0x147D8CA64`, `0x147D8B928`, `0x147D93A58`, role
morale `0x147D8AA64`) and ends with `SetTotalMorale(pmm, rec, total)` (tail jump `0x147D960A8`). **`SetTotalMorale`** `0x147D960A8`:
`rec+0x2C = total`; when the level bucket (`0x147D837D8`) changed it calls `0x147B9C218(hub+0x1298 DynamicOverallManager, pid)`, which
refreshes the player's dynamic overall (the "-2 OVR" the user sees next to "Unknown" is that morale modifier of the overall).

Without a record: `GetMorale` returns 0, the UI shows "Unknown" (`IsMoraleAvailable`: team == user team `0x140F86FC0`, but no record), and
LE's `SetPlayerMorale` has nothing to write (`[M]`: its native is not readable here; "only for player in user team" matches).

**What else a signing informs** (a scan for `cmp edx,0x5f` in the career code finds 15 handlers). Read in full `[H]`: PlayerStatusManager (role),
PlayerMoraleManager (morale), TransferManager (`0x147C42D5E`: `0x147C4F9A4(tm, newTeam, pid, true)`, plus `0x147D73BA4` when `tm+0x2718`), FitnessManager (`0x147CB4FAD`: appends
the pid to its user-club list at `+0x6C90` or its loan list `+0x6CB0`, manager career only). By address only `[M]`: PlayerDataRevealManager (`0x147E3DCA5` inside its
HandleEvent `0x147E3DAC8`: a reveal for the new player), CoachManager (`0x147B89CAC`), EmailManager / FCEComms (`0x147B8A41B`), StatisticViewManager (`0x147BCFD29`),
SuspensionManager (`0x147BD0323`), YouthPlayerManager (`0x147DEC11B`), StandingsView (`0x147DA0B65`). The PlayerContractManager reacts to `0x60` (and `0x17`, `5`, `0x33`, `0x53`, `0xF`) but **not** to
`0x5F`: its node comes from the contract / signing flow (transfer_lists.md section 4), which Turbo does not run either.

### 4.3 Event layouts `[H]`

```
alloc: A = *(void**)0x14C269EA8;  ev = ((void*(*)(void*,uint32,const char*,uint32))(*(void***)A)[2])(A, size, name, 0)   // 0x147BA09B3..0x147BA09BD
PlayerLeftTeam   id 0x60, size 0x28: +0 vtable 0x14AFF6468, +8 refcount 0, +0x10 0x60, +0x18 pid, +0x1C OLD team, +0x20 NEW team
PlayerJoinedTeam id 0x5F, size 0x28: +0 vtable 0x14AFF6C00, +8 refcount 0, +0x10 0x5F, +0x18 pid, +0x1C NEW team, +0x20 OLD team
TransferBlockedByUser id 0xBE, size 0x20: +0 vtable 0x14AFF7570, +8 0, +0x10 0xBE, +0x18 pid, +0x1C blocked-now
post: PostEvent(*(void**)*(void**)(hub+0x4F8), id, ev)       // 0x14060124C: ev->vf[1] (AddRef), ev->vf[4], dispatcher->vf[6](id, ev, 0), ev->vf[2] (Release)
```
(`xchg dword [r8+8], r..` sets the refcount to 0 before the post; the post's AddRef / Release frees the object, so it must come from
the game allocator, as in the UAH helpers.)

### 4.4 `DataController::WriteTeamPlayersLinks` `0x147BA08CC` `[H]`

`void (DataController* this = [[hub+0x418]], int pid, int oldTeam, int newTeam, int jersey /*stack*/)`: builds `UPDATE teamplayerlinks SET
teamid = newTeam, jerseynumber = jersey, position = 29 WHERE teamid = oldTeam AND playerid = pid` (through `[this]->vf[1]`), then posts
**PlayerLeftTeam (0x60)** and **PlayerJoinedTeam (0x5F)** (that order) and calls `0x147B99BFC(this, pid, oldTeam, newTeam)` (squad-size
enforcement; the special team ids `0x1B29D`, `0x1B72C`, `0x1B688` are skipped). Callers: `0x147B5EF60` (transfer completion code) and
`0x147DEE5BA`. This is the game's own "a player changes club" write.

## 5. "Likes the club"

Not in the morale record (section 4.1). What exists, all in the database (the game reads these through SQL each time):

1. **`player_grudgelove`** (DB table, 268-table list of the live career: `playerid [-1..524286]`, `level_of_emotion [0..7]`,
   `emotional_teamid [-1..262142]`). Game code: `GetEmotion(dc, team, pid)` `0x147B8868C` (0 when no row), `SetEmotionLevel1(dc, team, pid)`
   `0x147BA02D8` (insert or update the row with level **1**; max **7 rows per player**, the reader `0x147B5F52C` prunes), `0x147B639B0` delete,
   `0x147D70484` "players with level 1..5 for team X" = the **`PlayerTeamGrudgeDAOImpl`** at `TM+0x2C00` (vtable `0x14B015508`, a cache of the
   level 1..5 players of one team) `[H]`. Uses: `CANNOT_BUY_GRUDGE` / `CANNOT_LOAN_GRUDGE` (the player refuses a club he holds a grudge against),
   the AI player selection `0x147BD6AC1` (levels 1 and 3..5 skipped, 6 and 7 handled separately), the next-match report `PlayerGrudgeLove
   {PlayerID +0, PlayerTeamID +4, EmotionTeamID +8, LevelOfEmotion +0xC}` (`0x147F9FE34`, journal `PREVTEAM_GRUDGE` / `PREVTEAM_LOVE`) and
   the renewal flag `0x147E321D4` (`GetEmotion(userTeam, pid) - 1 <= 4`). So **1..5 = grudge, 6..7 = love** `[M]` (the grudge DAO covers exactly
   1..5; the "love" journal entry exists next to the grudge one). The game itself only ever **writes level 1** (grudge, player-career stories);
   love rows come from the base data. Adding `{playerid, emotional_teamid = club, level_of_emotion = 6 or 7}` is a plain `InsertDBTableRow` /
   `ExecuteSQL` insert; the difference between 6 and 7 is unknown (both take the same branches in the code seen).
2. **`CAREER_ONE_CLUB_PLAYER` trait** = **`players.trait2` bit 10 (`0x400`)** (`ENUM_PLAYSTYLE2_CAREER_ONE_CLUB_PLAYER = 1024` in Live Editor's
   `playstyles_enum.lua`; the game's trait list `0x147B2212C` has FINISHING ... GK, CPUAI, then `CAREER_SOLID_PLAYER` (256), `CAREER_TEAM_PLAYER` (512),
   `CAREER_ONE_CLUB_PLAYER` (1024), `CAREER_INJURY_PRONE`, `CAREER_LEADERSHIP`, `CAREER_SUPER_SUB`) `[H]`. The UI struct `PlayerTraits {HasLeadershipTrait
   +0, HasOneClubPlayerTrait +1}` and `CANNOT_BUY_ONE_CLUB_PLAYER` use it `[M]`. Game accessors: `GetPlayerTraits(dc, pid, uint8[])` `0x147B75024` and
   `SetPlayerTraits(dc, pid, uint8[])` `0x147B9FB40` (byte per trait bit: 0 none, 1 trait, 2 icon trait; trait2 bit 10 = index 40).
3. **`TimeAtClub`** (`PlayerStatus +0`) = `12 * years + months` since `players.playerjointeamdate` (`0x147F98F5B..0x147F98F74`), shown with the
   "legend / veteran / leader" status texts `[M]`.

None of the three needs a game call; none exists as a morale factor. Open question 1: which one the user means.

## 6. The game calls (catalogue)

Convention: Windows x64, `this` in rcx; all run on the game thread (the frame job; Turbo's `run_on_game_thread` / the career-event pump). None
takes a lock. Signatures in `player_status_signatures.json`.

| purpose | address | signature | args | posts / effects |
|---|---|---|---|---|
| Block / unblock (toggle) | `0x147F688B8` UAH slot 30 | `void(UAH* helper, int pid)` | helper = `dao+0x478` as in transfer_lists.md | cache + TM+0x2F50 flag 0, unlist, withdraw offers, event `0xBE` |
| Is blocked | `0x147D702CC` (cache vf[1]) or read vector A | `bool(CachedTransferblockDao*, int pid)` | `tm+0x2D38` | none |
| Role entry add | `0x147BE7BB4` | `bool(PSM*, int pid, uint8 role)` | role 1..5 explicit (promised), 0 / 0xFF auto | none (returns 0 when 52 entries) |
| Role entry find / remove | `0x147D8BADC` / `0x147D90C80` | `Entry*(sub = PSM+0x10, int pid)` / `bool(sub, int pid)` | | none |
| Morale find | `0x147D8B5E8` | `Rec*(store = PMM+0x518, int pid)` | | none |
| Morale create | `0x147D81108` | `Rec*(store, int pid, int emotionMinus1)` | `players.emotion - 1` | none; null at 52 |
| Morale init | `0x147D93B08` | `void(PMM*, Rec*, void* prev = 0)` | r8 unused, r9 = prev | ends in SetTotalMorale |
| Morale set | `0x147D960A8` | `void(PMM*, Rec*, int total)` | total 0..120 | refreshes dynamic overall when the level changes |
| Link write + join / leave events | `0x147BA08CC` | `void(DC*, int pid, int oldTeam, int newTeam, int jersey)` | DC = `[[hub+0x418]]` | DB UPDATE + events `0x60`, `0x5F` |
| Post an event | `0x14060124C` | `void(void* dispatcher, int id, Event*)` | `*(void**)*(void**)(hub+0x4F8)` | |
| Emotion get / set-grudge | `0x147B8868C` / `0x147BA02D8` | `int(DC*, int team, int pid)` / `bool(DC*, int team, int pid)` | | DB |
| Traits get / set | `0x147B75024` / `0x147B9FB40` | `void(DC*, int pid, uint8*)` | | DB |

## 7. Recipe for Turbo

General rules (same as transfer_lists.md section 5): every address / vtable resolved by signature and inside FC27.exe, every pointer read
checked, the career's manager table is `hub`, no write before all validation passed, dry-run mode, kill switch file
(`turbo_output\call_player_status_off.txt`), game thread only, read back after the call and report "the game refused" when the state did not
change, and the user's own club only (`teamplayerlinks.teamid == [um user club]`, no `playerloans` row).

### 7.1 Block Offers (`team_mass` action `block_offers`)

1. Locate as `tl::locate` does (comm -> owner -> hub -> dao `+0x478` helper) and check, in addition, **helper vtable slot 30 == `uah_toggle_transfer_block`**.
2. Validate: TM vtable `0x14B0055A8`; `cache = [tm+0x2D38]` readable, `[cache] == 0x14B005400` (`cachedblock_vtable`); vectors A (`cache+0x10/+0x18`)
   and the store (`tm+0x2F50/+0x2F58`) have `begin <= end`, whole-element sizes (4 and 8 bytes), at most 2000 elements, every byte readable; `[cache+8]`
   points to an object with vtable `0x14B005630`.
3. Per player (user club only): `blocked = (pid in vector A)`. If `blocked == wanted` skip. **Never call the helper when it would flip the wrong
   way**: blocking calls it only when the pid is absent from A; unblocking only when present.
4. Call `toggle(helper, pid)` on the game thread. Read back: for "block" the pid is now in A **and** the store holds `{pid, 0}`; for "unblock" neither does.
   Expected before / after for a Napoli player (e.g. 268511): before A has no 268511 and the store has no `{268511,0}`; after both do; the PCM status
   (`node+0x34`) is 0 (a listed player (7/8/9) is unlisted); one `0xBE` event is posted (`ev+0x1C = 1`).
5. Reverse for "unblock"; flag-1 entries (released players) are untouched by the toggle and not reported as "blocked by you".
6. Risks: it is a toggle (guarded in step 3); it posts one event and runs the unlist path per player (a squad of 30 is fine); the service call in 2.4 is
   untraced (`[L]`); do not write the vectors by hand (the cache would disagree and the event / offers would be missed). If a call path is ever
   unavailable, the only safe fallback is `cache->vf[4](pid)` (`0x147D73DB8`) which updates store + cache but does not unlist or post.

### 7.2 Squad roles (`squad_roles`, `squad_role` feature)

Memory only, plus one optional game call. Validation (all must hold or nothing is written): `psm = **(hub+0xAF8)`; `[psm] == 0x14AFFB2A0`;
`[psm+8] == hub`; `[psm+0x10] == user club team id` (> 0); `count = int32[psm+0x14]` in 0..52; `begin = ptr[psm+0x18]`, `ptr[psm+0x20] == begin + 0x1A0`,
`ptr[psm+0x28] == begin + 0x1A0`, all 0x1A0 bytes readable; entries `i < count`: pid > 0, role in {1..5, 0xFF}; entries `i >= count`: pid == -1; no duplicate pids.

For each user-club player (not loaned in unless `include_loaned_in`):
* found at index `i`: write **one byte** `begin + i*8 + 4 = role`; leave `+5` and `+6` alone (if the role should count as promised, also write `+5 = 1`). Read back.
* not found and `count < 52`: preferred = game call `AddPlayer(psm, pid, role)` (`0x147BE7BB4`; creates the entry with `wasPromised = 1`); memory-only equivalent: write
  `pid` at `begin + count*8`, `role` at `+4`, `0` at `+5` and `+6`, then `count++` (that is exactly `AddEntry` + the two stores). **Check for the pid first** (neither does).
* `count == 52`: report "the squad table is full" (the game cannot track more).
Role numbers: 1 Crucial, 2 Important, 3 Rotation, 4 Sporadic, 5 Future (Prospect). Expected after: `Find` returns the entry with the new byte; the Squad hub shows the role
after the screen is reopened `[M]`. **Re-apply after every `SEASON_RESET`** (the game rebuilds all roles then, section 3.1) and after a team change. Risk: `wasPromised`
(`+5`) is read by morale / negotiation code not traced here; leaving it 0 is the state the game's own auto roles have.

### 7.3 Morale for players Turbo moved into the club (`morale` action, `form_morale`)

Preferred, **one change that also fixes roles, the TransferManager, fitness, reveal data**: after Turbo's database move into the user's club, post the game's own events from
`Turbo.dll` on the game thread: first `PlayerLeftTeam (0x60)` for the old club, then `PlayerJoinedTeam (0x5F)` for the user's club (section 4.3; allocate with the game allocator
`*(void**)0x14C269EA8`, vtables `0x14AFF6468` / `0x14AFF6C00`, dispatcher `**(hub+0x4F8)`, `PostEvent 0x14060124C`). The handlers then create the PSM entry (auto role, `0xFF` -> computed) and the
morale record; then call Live Editor's `SetPlayerMorale(pid, 100)` as today. Alternative with the same effect: have the **game** do the link write
(`WriteTeamPlayersLinks 0x147BA08CC`, section 4.4) instead of Turbo's `teamplayerlinks` edit for the club change (untested: it also sets `position = 29` and the jersey, and runs squad-size
enforcement); calling it after Turbo's own write would update zero rows (`WHERE teamid = old`) but still post the events `[L]`.

Direct fallback without events (morale only), for a player of the user's club who has no record: validate `pmm = **(hub+0xA78)`, `[pmm] == 0x14B0156A8`, `[pmm+8] == hub`,
`int32[pmm+0x518] == user team`, `byte[pmm+0x554] == 0`, store vector `[+0x528, +0x530)` a whole number of 0x60-byte records, count < 52, no record with that pid (`0x147D8B5E8` returns null);
`emotion = players.emotion` (Lua, 1..8); `rec = Create(pmm+0x518, pid, emotion-1)`; `InitMorale(pmm, rec, 0)`; then `SetTotalMorale(pmm, rec, value)` or LE's native. Expected after:
`rec+0x2C == value`, `GetMorale` returns it, the squad screen shows the level and the overall's morale modifier. Risks: Create / Init allocate inside the record's modifier vector (game allocator, game
thread only); `InitMorale` reads calendar / fixtures / form managers, so it must run when a career is loaded and the calendar is valid; a second record for the same pid (no duplicate check in
`Create`) corrupts lookups, so always `Find` first.

### 7.4 "Likes the club" (once the user says which one)

* love rows: for each user player without a `player_grudgelove` row for the club: count that player's rows (max 7, else remove the oldest or refuse), then `InsertDBTableRow`
  `{playerid, emotional_teamid = club, level_of_emotion = 7}` (or update the existing row's level to 7 when it is a grudge 1..5: `[M]`, the cached `PlayerTeamGrudgeDAO` (`TM+0x2C00`) refills when the
  queried team changes). Verify with a read of the row; the game reads it per query.
* trait: `players.trait2 |= 0x400` through the DB edit (or `SetPlayerTraits`, byte index 40 = 1).
Neither needs Turbo.dll.

## 8. To verify in the live game (throwaway career turbo04 only)

1. Block Offers: pick a Napoli player, read `[tm+0x2D38]` (vtable `0x14B005400`), vector A and `[tm+0x2F50..2F58]` before; use the Squad hub "Block Offers"; read again: A gains the pid, the store gains `{pid, 0}`.
   Then do it with Turbo's helper call and compare; confirm the helper's slot 30 equals `0x147F688B8`.
2. Roles: read `psm+0x10..0x38` and the entries of the squad; change one player's role byte; reopen the Squad hub (shows the new role?); advance to a `SEASON_RESET` and see them rebuilt.
3. Morale: a Turbo-moved player: `Find` returns null; post `0x5F` (or use the direct fallback) and check the record, the level, the overall's morale modifier; confirm the PSM got an entry.
4. Likes the club: ask the user for the exact in-game text / screen; read the candidate player's `player_grudgelove` rows and `trait2` before / after.

## 9. Open questions

1. Which "likes the club" the user means: a love row, the One Club Player trait, or time at club (section 5). A screenshot of the text decides it.
2. Whether level 6 and 7 differ, and whether 6 / 7 are really "love" (only inferred from the grudge DAO range 1..5 and the AI branches).
3. Meaning of `wasPromised` (`+5`) outside the role flow, and flag 1 of the block vector (set on release only).
4. Whether posting `0x5F` after Turbo's own database move double-processes anything in the TransferManager (`0x147C4F9A4`) or Fitness / Youth managers; the game does exactly this after its own writes, so it should be consistent (live test).
5. The untraced service call in the block path (2.4) and whether the Squad hub needs a reopen to show a changed role.
6. How Live Editor's `SetPlayerMorale` writes (not readable here); it most likely finds the record and calls `0x147D960A8` or writes `rec+0x2C`.
