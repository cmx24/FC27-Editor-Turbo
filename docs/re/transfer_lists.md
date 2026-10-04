# Transfer list, loan list and transfer bans (FC 27): RE notes and Turbo design

Source: `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (FC27.exe 1.0.140.64835, base `0x140000000`), analysed
2026-10-04 with `scripts/re/` (`rx_jobs.py`, `disfn.py`, `refs.py`, `strings_grep.py`, `strings_range.py`). Confidence:
`[H]` read in the code, `[M]` strong inference, `[L]` guess. `scripts/re/transfer_list_signatures.json` holds the
signatures (`scripts/re/sig_transfer_lists.py` re-makes and checks them: all eight unique). Live (dev service, read only,
2026-10-04 08:0x, game at the main menu): the running FC27.exe has the same bytes at the helper functions and the
UserActionsHandlingHelperImpl vtable slots 31 / 32 / 33 hold `0x147F8E300` / `0x147F68368` / `0x147F68214` `[H]`.
**First in-game try (2026-10-04 08:45, build c86ed3f, career turbo04, Napoli)**: refused before calling the game because
the UserManager's users were read with a wrong layout (section 1b, fixed); the whole call path was then re-checked live
in that career (section 1b, read only). Section 8 is the in-game plan.

Replaces Live Editor's natives `cAddPlayerToTransferList`, `cAddPlayerToLoanList`, `cRemovePlayerFromLists`,
`cRemovePlayerFromTransferList`, `cRemovePlayerFromLoanList`, `cIsPlayerTransferListed`, `cIsPlayerLoanListed`, which FC 27
Live Editor v27.1.2 does not ship. Transfer bans (`cGetTransferBans` & co.): section 6, not delivered.

## 0. Summary

* The Transfer Hub's actions go through the career's **UserActionsHandlingHelperImpl** (a sub-object at `+0x478` of the
  CareerDaoFactoryImpl `[[comm+0x20]+0x30]`): `AddToTransferList(helper, playerId)`, `AddToLoanList(helper, playerId)`,
  `TryToRemoveFromList(helper, playerId, bool loanList)` `[H]`. Each calls the **TransferManager** (manager type 127) and
  posts the career event the rest of the game reacts to (UserTransferlisted `0x79`, UserLoanlisted `0x7A`) `[H]`.
* The listed state is the player's **contract status** in the **PlayerContractManager** (type 77): 7 transfer listed,
  8 loan listed, 9 both, 0 none `[H]`. Turbo reads it before and after each call.
* The helper lists the player **on the user's club** (team id from the **UserManager**, type 129) whoever he plays for
  `[H]`: Turbo accepts only the user's own players (Lua: teamplayerlinks club == user team, no playerloans row; DLL:
  the club Lua sends == the UserManager's team).
* **No asking price, no loan terms**: the helpers take only the player id and TransferManager's add functions only a
  list kind and a flag `[H]`. FC 27's own screens set no price when listing (AI clubs make offers; you negotiate).
* **The game's remove always clears both lists** (mask 1 applied to both list tables) `[H]`.
* **Transfer bans**: FC 27 has no transfer-ban list Turbo could call (section 6).

## 1. Finding the objects (proved: allocation names, sizes, constructors, slots)

Section 0 of `standings-ui-path.md` is the rule: a vtable is only trusted once the class is proved. The career hub
builder (`0x147F17xxx..0x147F19xxx`) allocates every manager as `alloc(size, name, flags)` (`mov edx, size` after
`lea r8, "Name"`, `call [allocator+0x10]`), runs the constructor on the result and stores it in its slot `[H]`:

| object | allocation name / size | constructor (vtable lea) | stored at | Turbo check |
|---|---|---|---|---|
| TransferManager | `"TransferManager"` 0x2FA0 (`0x147F18B14`/`0x147F18B29`) | `0x147C26450` (lea at +0x31 -> `0x14B0055A8`) | hub `+0xFF8` = slot 127 | `tm_vtable` |
| PlayerContractManager | `"PlayerContractManager"` 0x458 (`0x147F17A85`) | `0x147E54FB0` (+0x6 -> `0x14B01E240`) | hub `+0x9B8` = slot 77 | `pcm_vtable` |
| UserManager | `"UserManager"` 0xB20 (`0x147F1774D`) | `0x147AB2EB8` (+0x25 -> `0x14AFDF150`) | hub `+0x1038` = slot 129 | `um_vtable` |
| CareerDaoFactoryImpl | `"CareerDaoFactoryImpl"` 0x13E8 (`0x147F19B9F`) | `0x147F0EB10` (+0x22 -> `0x14B025C48`) | owner `+0x30` | `dao_vtable` |
| UserActionsHandlingHelperImpl | sub-object of the dao | lea at `0x147F0F267` in the dao ctor -> `0x14B029440`, stored at dao `+0x478`, hub at `+0x480` | dao `+0x478` | `uah_vtable` + slots 31..33 |

`owner = [comm+0x20]` (FeFceGMCommService plugin), `hub = [owner+0x10]` (the manager table Turbo already walks:
0x20-byte slots, `+0x08` type descriptor, `+0x10` count, `+0x18` holder -> object). The helper's `+0x8` is the same hub
(the helper reads `[[helper+8]+0xFF8]` for the TransferManager, `+0x4F8` for the event dispatcher, `+0x1038` for the
UserManager) `[H]`.

## 1b. The UserManager's users and the user's team (proven in the code and live)

**What was wrong.** Turbo read `[um+0x18]` / `[um+0x20]` as begin / end of a vector of 0x348-byte users. In the live
career they were `0x131EC2A50` / `0x3A4762470`, not a whole number of users, so every list action was refused with
"the UserManager's users ... are not a list of 0x348-byte users" before the game was called (the Lua side, which reads
`[um+0x18]+0x1F4`, was right). `+0x20` is not the end of anything: the users are a counted `new[]` array.

**The layout, from the code `[H]`:**

| UserManager | meaning | proof |
|---|---|---|
| `+0x10` int32 | user count | ctor `0x147AB2EB8` sets -1; `SetUserCount 0x147AC3530` writes it, frees the old array and allocates `count * 0x348 + 0x10` bytes named `"UserManager::mUser"` (the only reference to that name), writes `count` (u64) at the start, constructs each user (`0x147AAF490`) and stores `alloc + 0x10` at `+0x18`; called from `UserManager::HandleEvent 0x147ABDD40` |
| `+0x14` int32 | active user index, -1 = none | ctor -1; `GetActiveUser 0x14154ADBC` (= `0x147ABD178`): `index == -1 ? null : [um+0x18] + index * 0x348`; the setters `0x147AB6628` / `0x147AC34B4` accept only `0 <= index < [um+0x10]` |
| `+0x18` ptr | the users (`count` x 0x348 bytes; the `new[]` header at `-0x10` holds the count) | `GetUser(i) 0x147ABD994`: `0 <= i < [um+0x10] ? [um+0x18] + i * 0x348 : null`; `FindUserByTeam 0x147ABD9B4` loops `i < [um+0x10]` |
| `+0x20` ptr | something else (ctor 0), **not** the users' end | |
| `+0x2F` u8 / `+0x34` int32 | Live Editor's `mUserType` (0 manager career) / `mPlayerId` (-1 here) | `FCECareerModeUserManager.lua` |

A user's clubs are two 0x74-byte records at `+0x1F0`: `GetUserClub 0x142B1DDEC(user, slot)` returns `user + 0x1F0 +
slot * 0x74` for slot 0 / 1; the record has the team id at `+4`, the league id at `+8`, the team name at `+0xC`. Slot 0 is
the club (team at user `+0x1F4`), slot 1 the national team (`+0x268`, -1 = none); `FindUserByTeam` tests both. The list
code (`TransferManager::AddToTransferList` at `0x147C3FBC5`, `AddToLoanList`, the helper `TryToRemoveFromList` at
`0x147F8E321`) always reads `GetActiveUser` then `GetUserClub(user, 0)->+4`: the user's **club** team id.

**Live, read only (dev service, 2026-10-04 08:4x-08:5x, FC27.exe 1.0.140.64835, career turbo04 in the hub, user club
SSC Napoli 48; `bridge_state.json`: comm `0xF6425FE0`, managers `0x6C39E010`, user_team 48, date 1 July 2026):**

* manager table slot 129: count 1, type flag 1, holder `0x66EF3B50` -> UserManager `0x3B38EADE0`, vtable `0x14AFDF150`
  (= `um_vtable`), `+0x08` = the manager table;
* `+0x10` = 1, `+0x14` = 0, `+0x18` = `0x131EC2A50`, `+0x20` = `0x3A4762470`, `+0x2F` = 0, `+0x34` = -1;
* `[0x131EC2A50 - 0x10]` = 1 (the `new[]` header = the count);
* user 0: `+0x0` = 0 (its index), manager names at `+0x8` / `+0x68` / `+0xC8`, `+0x1E8` = 27 (nationality), club slot 0
  `+0x1F4` = **48**, `+0x1F8` = 31 (Serie A), `+0x1FC` = "SSC Napoli"; slot 1 `+0x268` = -1. The bytes after the single
  user are other heap data (not a user).

**Turbo's reading now** (`tl::user_team`): vtable + size, `0 < count <= 64`, `0 <= index < count`, users a pointer whose
`new[]` header equals the count, the whole active user readable, team = user `+0x1F4` > 0; plus the UserManager's `+0x08`
is the career's manager table. On the live career this gives 48.

**The rest of the call path, re-checked live in the same career** (a Python mirror of the new `tl::locate` over the dev
service; every check passed):

* owner `[comm+0x20]` = `0x3E68C0960`, manager table `[owner+0x10]` = `0x6C39E010` (= Lua's), dao `[owner+0x30]` =
  `0x128DF5410` vtable `0x14B025C48` (= `dao_vtable`), its 0x13E8 bytes readable;
* helper `dao+0x478` = `0x128DF5888`, vtable `0x14B029440` (= `uah_vtable`), slots 31 / 32 / 33 = `0x147F8E300` /
  `0x147F68368` / `0x147F68214` (= the three functions by signature), helper `+0x08` = the manager table;
* TransferManager (slot 127) `0x3B3956EB0`, vtable `0x14B0055A8`, `+0x08` = the manager table; `+0x2B80` lists store
  `0x67219E30` (vtable `0x14B006060`), `+0x2BE0` `0x6702EEC0` (`0x14B0057C8`), `+0x2D38` `0x67122030` (`0x14B005400`);
* PlayerContractManager (slot 77) `0x3B47CF9C0`, vtable `0x14B01E240`, `+0x08` = the manager table; buckets
  `0x2E3B9AD90`, 47 buckets; Cheddira's record (268511, bucket 268511 % 47) `0x67166840`: key 268511, `+0x08` = 48 (his
  club), status `+0x34` = 0 (not listed: eligible);
* event dispatcher (slot 39) `0x672166A0`: its first word is its event sink `0x6DE05E20` (vtable `0x14972FB80`);
  `PostEvent 0x14060124C` calls `[[dispatcher]]`'s vfunc `+0x30`, so Turbo now checks the sink's vtable (not just a
  non-zero first word);
* CalendarManager (slot 24, `[hub+0x318]`, which `AddTo*List` reads through `[tm+8]` for the listing date) `0x6BD6E260`,
  vtable `0x14AFDC498`, `+0x34` = {1, 7, 2026}: now checked too.

## 2. The helper functions `[H]`

```
UserActionsHandlingHelperImpl::AddToTransferList(this, int playerId)   0x147F68368  vtable slot 32
    TransferManager::AddToTransferList([[this+8]+0xFF8]->obj, playerId, kind 0, flag 0)   0x147C3FB60
    ev = alloc(0x20, "UserActionsHandlingHelperImpl::AddToTransferList"); ev = {vtable 0x14AFF6418, type 0x79,
         +0x18 playerId, +0x1C listed = 1}
    PostEvent([[this+8]+0x4F8]->obj, ev)                                0x14060124C (tail jump)
UserActionsHandlingHelperImpl::AddToLoanList(this, int playerId)       0x147F68214  vtable slot 33
    TransferManager::AddToLoanList(tm, playerId, 0, 0)                  0x147C3F95C, then event 0x7A
UserActionsHandlingHelperImpl::TryToRemoveFromList(this, int playerId, bool loanList) -> bool   0x147F8E300  slot 31
    team lists = store->vfunc+8(userTeam), global lists = store->vfunc+0x18()   (store = TM+0x2B80)
    pre-check: the player is in the user team's list set and in the global hash (loanList ? +0x30 : +0) with flag bit 1;
               otherwise returns false and does nothing
    TransferManager::RemoveFromLists(tm, playerId, &removedT, &removedL, mask 1)   0x147C3F588
    removedT -> event 0x79 listed = 0; else removedL -> event 0x7A listed = 0; returns true
```
The two add helpers differ only in the callee and the event id (their signatures differ only in the `BA 79` / `BA 7A`
byte).

## 3. TransferManager internals `[H]`

* `AddToTransferList(tm, pid, kind, flag)` `0x147C3FB60` / `AddToLoanList` `0x147C3F95C`: read the player's contract
  info (`GetPlayerInfo 0x147E61F20` on the PlayerContractManager); **status must be 0, 7 or 8** (else nothing happens);
  user team = UserManager (`0x14154ADBC`: user = `[um+0x18] + [um+0x14] * 0x348`; `0x142B1DDEC(user, 0)` = user+0x1F0,
  team at +4 = user `+0x1F4`); list bits = 1 for kind 0 (2 or 4 for kind 1, not used by the helpers); the player goes
  into the team's list copy and the global hash with the bits; the listener `TM+0x2D38` (vfunc +0x38) is told with the
  calendar date (`[hub+0x318]` CalendarManager `+0x34`); the status is written through `0x147E75D88(pcm, pid, status,
  &date, -1)`: transfer `7 + 2 * (loan listed)`, loan `8 + (transfer listed)`; the lists are written back
  (store vfunc +0x10 team, +0x20 global).
* `RemoveFromLists(tm, pid, bool* removedT, bool* removedL, uint8 mask)` `0x147C3F588`: for each list (global transfer
  entry, team loan entry): if `mask & bits'` (bits with 2/4 widened to 6) the entry's bits become `bits & ~mask`; an
  entry with no bit left is erased, the out flag is set and the listener `TM+0x2BE0` (vfunc +0x90) is told; the new status
  is `transfer kept ? (loan kept ? 9 : 7) : (loan kept ? 8 : 0)`; status 7..9 is required on entry. The helper passes
  mask 1, so **both lists lose bit 1**: one removal unlists the player from both lists.
* Team list copy (`0x147C26278`): two red-black sets of player ids (`+0x00` loan, `+0x30` transfer); global copy
  (`0x147C27920`): two hash tables (`+0x00` transfer, `+0x30` loan) of `{pid, bits}`.

## 4. PlayerContractManager contract records `[H]`

`GetPlayerInfo 0x147E61F20`: buckets `node**` at `pcm+0x3D8`, bucket count u32 at `pcm+0x3E0`; bucket =
`(int64)pid % count`; chain through `node+0xB8`; `node+0` key = player id; `buckets[count]` is the end sentinel. Node
`+0x34` is the **contract status** (copied to the info at `+0x1C`, which the add / remove functions test). Turbo walks
the same table read-only (bounded chain, every read checked).

## 5. Turbo's game call (`turbogui/src/core/transfer_list.*`, `src/win/transfer_list_win.*`)

* Signatures (built-in table, `sigscan.cpp`; JSON next to the RE scripts; native test resolves each on the game's bytes):
  `uah_add_transfer_list`, `uah_add_loan_list`, `uah_try_remove_from_list`, `uah_vtable`, `dao_vtable`, `tm_vtable`,
  `pcm_vtable`, `um_vtable`.
* Validation before any call: every function / vtable resolved and inside FC27.exe; comm -> owner -> hub; dao vtable;
  helper vtable **and its slots 31 / 32 / 33 equal the three functions resolved by signature** (the vtable and the
  functions prove each other); the helper's hub is the career's; TransferManager vtable, its `+0x2B80` / `+0x2BE0` /
  `+0x2D38` objects (vtables in FC27.exe); PlayerContractManager vtable; TransferManager / PlayerContractManager /
  UserManager `+0x08` = the career's manager table (the game reaches the other managers through `[tm+8]`); event
  dispatcher with an event sink whose vtable is in FC27.exe; CalendarManager with a vtable and a readable date;
  UserManager vtable, user count 1..64, active index below it, the users' `new[]` header equal to the count, the active
  user readable, team id (user `+0x1F4`) > 0 (section 1b). Then the player: his club (from Lua) must be the user's team, he must have a
  contract record, the status must allow the action (the game's own rule 0 / 7 / 8 for adds; listed for removals; a
  single-list removal of a player on both lists is refused because the game would clear both).
* The call runs on the game thread only (synchronous when Lua calls from the career-event thread; otherwise queued for
  the dispatcher). After the call the status is read back and must be the expected one (7 / 8 / 9 / 0); "the game
  refused" when it did not change.
* Mailbox op **10** (`kCallOpTransferList`): args = action (1 transfer list, 2 loan list, 3 remove from lists, 4 / 5
  remove from the transfer / loan list, 6 status only), player id, comm service, the player's club; outputs = status
  before / after.
* Kill switch `turbo_output\call_transfer_list_off.txt` (plus every game-hook switch); Status tab line
  `transfer_list: ready | ... | runs n (ok n, queued n)` and the last outcome.
* Lua (`core/moves.lua` M.list, `bridge.lua` install_natives): `TurboTransferList(action, pid, club)`; Live Editor's
  missing natives (`cAddPlayerToTransferList`, `cAddPlayerToLoanList`, `cRemovePlayerFromLists`,
  `cRemovePlayerFromTransferList`, `cRemovePlayerFromLoanList`, `cIsPlayerTransferListed`, `cIsPlayerLoanListed`) are
  defined on top of it (never replacing a native Live Editor ships), so Live Editor's own wrappers
  (`AddPlayerToTransferList(pid)` ...) and FC 26 scripts work; `features/player_moves.lua` actions `transfer_list`,
  `loan_list`, `unlist`, `unlist_transfer`, `unlist_loan`, `list_status`; `core/caps.lua` lights `move_transfer_list`,
  `move_loan_list`, `move_unlist`, `move_list_status` up once `TurboTransferList` exists.
* GUI: Players editor header (Transfer list / Loan list / Remove from lists) and Contract & Clubs tab (the same plus
  List status); greyed out for players of other clubs.

## 6. Transfer bans: what FC 27 has (static search) and why Turbo does not deliver them

FC 26 Live Editor's API: `cGetTransferBans()` -> `{ id, member_type (0 team, 1 player), date_end }`,
`cAddTransferBan(id, date, type)`, `cRemoveTransferBan(id, type)`, `cSaveTransferBans()` (lua `core/managers/
transfer_ban_manager.lua`). Searched in FC27.exe:

* Strings with ban / embargo / restrict / block / banned / until / expiry in career code, the career-save member
  names (`m...` serializer names around `0x14B01F000..0x14B025000`), Live Editor's manager list (enum ids 19..154): there
  is **no transfer-ban manager, list or member** `[H for the strings, M for the absence]`.
* What exists: **match bans** (`CM_BannedUntil_Date`, `Match_Ban`: suspensions, `0x147E0EC2A` / `0x147F52002`),
  the career-setup **transfer embargo** of the user's club (`CareerSettingsData` `TransferEmbargoSeasonNumber`
  `0x147D3378F`, game setting `CAREER_TRANSFER_EMBARGO_DURATION`, texts `CM_SETTINGS_TRANSFER_EMBARGO_*`), and a
  per-player transfer-interest record `{mPlayerId, mReasonFlags, mExpiryDate, mTransferScore, mStatisticValue}`
  (serializers `0x147EE991C` / `0x147EDADC4`, next to the statistic reasons MostGoalsInLeague, HatTricks, POTM...),
  which is a rumour / interest boost, not a ban `[M]`.
* So FC 26 Live Editor's bans were most likely Live Editor's own list, enforced by its own hooks on the AI transfer
  logic `[L]`. Turbo has no such enforcement point yet: it would be a detour on the AI clubs' transfer decision
  (shortlist / negotiation creation), not found in this track, and an unproven detour on AI transfers could corrupt a
  career. Per the rules it is **not delivered**: `core/caps.lua` keeps `transfer_bans` unavailable with this reason,
  the Turbo Tools section, the Teams > Overview "Transfer bans" section and the player editor's Contract & Clubs section
  are greyed out with it, and `features/transfer_bans.lua` (list / ban every team / remove all / one club / one player)
  works unchanged with a Live Editor build that ships the natives.
* Next steps for a later track: (a) the user's club embargo: find the live `CareerSettingsData` and how the transfer
  screens read `TransferEmbargoSeasonNumber`; (b) AI-club bans: find the function that starts an AI negotiation
  (buyer, seller, player) and gate it with a Turbo ban list (opt-in hook with a kill switch).

## 7. Tests

* Native (`turbogui/tests/native/test_main.cpp`): a synthetic career (comm -> owner -> hub, dao + helper with the real
  vtable addresses and slots, TransferManager, PlayerContractManager hash table, dispatcher, UserManager with two users)
  and a fake game with the helpers' status transitions: end to end list / loan-list / unlist, single-list removals,
  eligibility, only the user's players (club checks, user index), every validation stopping before the call, bounded
  chain walks, the eight signatures on the game's bytes and equal to the JSON. The UserManager is laid out as the live
  game has it (count, index, `new[]` users with their header, an unrelated `+0x20`); a regression case replays the
  live career (one user, Napoli 48, `+0x20` far beyond the users) and lists / unlists Cheddira (268511); the user
  checks cover count -1 / 0 / 65, index = count, header != count, an unreadable active user, a bad / unmapped users
  pointer, team 0 and a UserManager of another manager table; the new path checks (managers' `+0x08`, dispatcher sink,
  CalendarManager, vtables outside FC27.exe) each stop before the call. UI driver: list buttons (disabled for
  another club's player, List status for anyone), Contract & Clubs tab, the ban sections (greyed out / sending the
  module with a ban-capable Live Editor).
* Lua (`turbo/tests/t14_transfer_lists.lua`): refused without Turbo.dll (reason, caps), install_natives defines
  TurboTransferList + the Live Editor natives (cache reset, caps light up), op 10 arguments, refusals before the call
  (other club, loaned-in, no career), dry run, the game's refusal / queued answers, squad selection, Live Editor's
  wrappers, the ban modes (refused in FC 27; FC 26 parity with the natives).

## 8. In-game test plan (throwaway career only)

First try (2026-10-04 08:45, build c86ed3f): step 3 on Walid Cheddira (268511) was refused at "validate" with the users
layout error (section 1b); fixed on `fix/transfer-list-user`, every piece of the path then re-checked live (read only).

1. Load the test career (Napoli). Status tab: `transfer_list: ready` with the eight addresses.
2. Players > a Napoli player > Contract & Clubs > List status: toast "player N is not listed".
3. Transfer list: toast "... done, he is now transfer listed"; open the game's Transfer Hub > Transfer list: he is there;
   the news / inbox shows the listing. Advance a day: AI clubs may make offers (the game's own flow).
4. Loan list on another Napoli player: he appears on the Transfer Hub loan list.
5. Remove from lists on both: they leave the Transfer Hub lists; List status says "not listed".
6. A player of another club: the buttons are greyed out ("your own players only").
7. Kill switch: create `turbo_output\call_transfer_list_off.txt`: the Status line says off and the buttons report it.
8. Transfer bans: the sections are greyed out with the reason (FC 27 has no ban list).
