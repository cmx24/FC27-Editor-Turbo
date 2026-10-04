# FC 27 manager rules: job security, unsackable, manager market, career limits (RE notes and Turbo design)

Source: `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (FC27.exe 1.0.140.64835, base `0x140000000`), analysed
2026-10-04 with `scripts/re/` (`rx_jobs.py`, `disfn.py`, `bytescan.py`, `strings_grep.py`, `dump_bytes.py`). Confidence
tags: `[H]` read directly from the code, `[M]` strong inference, `[L]` guess. Every address is for this build.
`scripts/re/manager_rules_signatures.json` carries the byte signatures and the field map;
**`scripts/re/manager_rules_verify.py` re-checks every signature and every proof below** (all pass on this image).
Live check (dev service, 2026-10-04, main menu): the running game's vtables `0x14B019370` (slots `0x147DF7F8C,
0x147DFFFA0, 0x147E02E74`) and `0x14B016598` (slots `0x147DBAEC0, 0x147DD2A30, 0x147DBAF60`) match the image; no career
was loaded, so no live object was read (the in-game plan below covers it).

Scope: the user's own offline Manager Career. Only career-mode game logic was looked at.

## 0. Summary

| FC 26 LE feature | FC 27 Turbo | How |
|---|---|---|
| Job security control | **built** (Managers > Manager rules) | addon field + the game's own `UpdateJobSecurityScore` (game call) |
| Unsackable | **built** (opt-in switch, off at every start) | refusing hook on `JobSwitchManager::SackManager` (kill switch) |
| Fire / move manager, make any manager available | **built** (Managers > Manager market) | career database `manager.teamid` (what the game's AI hiring reads) |
| Endless career | **nothing to switch off found**: no season limit or forced end in FC 27's career code | section 7 |
| Negotiation / approach bypasses | **not built**: FC 27 Live Editor's own Misc Features cover it (`disable_neg_status_check`, `allow_transfer_loan_app`); leads in section 8 | |
| Unsupported leagues | **not built**: a pre-career database gate (`modeavailability`); leads in section 9 | |
| Managers tab 3D-model miniface | **descriptor fixed** to the game's own manager-head builder (section 6); in-game test decides | |

Note on the previous checkpoint (track 3 WIP): it read the job security thresholds one setting off (the settings loader
stores each value after the NEXT name's `lea`, section 2.2), wrote the score fields directly instead of letting the game
recompute, and wrote a "retirement age" byte at JobMarketManager+0x150 that is in fact read as a signed score modifier
(`0x147DBC9E4`, section 7) and a `career_calendar.enddate` that no career code reads (the only `careerenddate` /
`enddate` users are the legends bio and `playersuspensions`). All three were dropped.

## 1. Proving the two managers (standings-ui-path.md section 0: never trust a constructor's vtable unproven)

The career hub builder allocates each manager with its name string, then calls its constructor `[H]`:

```
0x147F18F30  lea r9,"JobSwitchManager"      ; mov ecx,0x1E8 (size) ; call alloc ; ... call 0x147DB6984 (ctor)
0x147F19471  lea r9,"ClubObjectivesManager" ; ...                                ; call 0x147DF42D0 (ctor)
```

* `ClubObjectivesManager` ctor `0x147DF42D0(this, hub)`: `lea rax,[0x14B019370]; [rcx+8] = hub; [rcx] = rax;
  [this+0x108] = this; [this+0x110] = 1; [this+0x114] = -1; [this+0x118] = 0; [+0x11C..+0x124] = 80` `[H]`.
  Live Editor type id 133 (`ENUM_FCEGameModesFCECareerModeClubObjectivesManager`), found by Lua through `mem.manager(133)`.
* `JobSwitchManager` ctor `0x147DB6984(this, hub)`: `[rcx+8] = hub; lea rax,[0x14B016598]; [rcx] = rax` `[H]`; type id 54.
  Vtable slot 1 = `HandleEvent 0x147DD2A30` `[H]`.
* Hub slots are `type * 0x20 + 0x18` (`+0x318` calendar = type 24, `+0x6D8` = type 54 JobSwitchManager, `+0x1038`
  user = 129) `[H]`; the JobMarketManager reads `[[hub+0x6D8]]+0x1E1` (mWasSacked) in `ApplyForJob`, consistent.

Turbo resolves both vtables from the constructors' `lea` (`com_vtable` rip at +0x1C, `jsm_vtable` rip at +0x13) and checks
every object against them before reading or writing; the capture hooks only record a `this` whose vtable matches.

## 2. ClubObjectivesManager: job security

### 2.1 Fields

```
ClubObjectivesManager                                                     [H]
+0x008 CareerHub*
+0x010 OBJECTIVES/* settings block (ctor passes this+0x10 to 0x147DF5D14; the loader 0x147E00E7C fills it)
       +0x38 JOB_SECURITY_VERY_INSECURE  +0x3C JOB_SECURITY_INSECURE  +0x40 JOB_SECURITY_OKAY
       +0x44 JOB_SECURITY_SAFE           +0x4C JOB_SECURITY_FIRED_POINTS
+0x108 serialised block (serialiser 0x147EDB3A8 gets this+0x108): +0 = the manager itself
       +0x110 u8  mIsManagerMode    +0x114 i32 mUserTeamId    +0x118 i32 mJobSecurityScoreAddon
       +0x11C mJobSecurityScore { +0 mPreviousSeasonFinalScore, +4 mPreviousScore (+0x120), +8 mScore (+0x124) }
+0x218 score logic: eastl::vector<Objective*> (+0x218/+0x220/+0x228), +0x248 calendar, +0x250 JobSwitchManager,
       +0x258 hub+0x6F8 manager, +0x268 level object (vtable 0x14B019638), +0x278..+0x288 level thresholds
       (copies of +0x38, +0x3C, +0x40, +0x44, +0x4C made by 0x147E06AAC)
```

### 2.2 Threshold loader: the store follows the NEXT name

`0x147E00E7C` reads every OBJECTIVES setting with `0x140B24848(settings, name)`; the compiler schedules
`lea rdx,<next name>` before `mov [rsi+X],eax`, so each store holds the value of the name loaded one step earlier
(`[rsi+0]` = CATEGORY_IMPORTANCE..., the first pair proves it). VERY_INSECURE -> block +0x28 (manager +0x38), INSECURE
+0x2C (+0x3C), OKAY +0x30 (+0x40), SAFE +0x34 (+0x44), FIRED_POINTS +0x3C (+0x4C) `[H]`. The level copies (+0x278 ..
+0x288) come in that ascending order, which confirms it.

### 2.3 The score and the level

* `UpdateJobSecurityScore(this)` `0x147E07E2C` (its own name string) `[H]`:
  `[+0x120] = [+0x124]; [+0x124] = 0x147E5F664(this+0x218, this+0x11C, [this+0x118]);` then compares
  `level(new)` with `level(previous)` through the level object (`[this+0x268]` vtable slot 6) and on a change posts career
  event `0xA7` (`PostEvent(*(hub+0x4F8), 0xA7, ev)`).
* `0x147E5F664`: averages the objectives' scores, blends from `mPreviousSeasonFinalScore` over the influence days, then
  `result = clamp(blend + addon, 0, 100)` (`add eax, ebx; js -> 0; cmp eax,0x64; cmovg`) `[H]`.
* Level function `0x147DF9A8C` (this = manager+0x268): `score >= [+0x1C] (safe) -> 3`, else the first `i` in 1..3 with
  `score < [+0x10 + 4i]` gives `i - 1`: score < insecure -> 0 very insecure, < okay -> 1 insecure, < safe -> 2 okay `[H]`.
* Callers of `UpdateJobSecurityScore`: `0x147E01980`, `0x147E01A30`, `0x147F9D640` and a tail jump from `0x147DBB490`;
  the function only touches its own manager, the calendar through the score logic and the event dispatcher `[H]`.
* The addon is written by the game only in the job-switch handler `0x147E0314C` (registered by the ctor): on a new job
  it sets `addon = 0`, the three scores to 80 and `mUserTeamId` to the new club `[H]`. Nothing else resets it, and it is
  serialised, so an addon Turbo writes stays with the career until the user changes clubs.

### 2.4 Turbo's job security call (core/manager_rules.h)

`job_security_call(mem, game, fns, req)` on the game thread:
1. `validate_com`: vtable `0x14B019370`, 0x2A0 bytes readable, `[+0x108] == this`, `mIsManagerMode == 1`, `mUserTeamId > 0`
   and equal to the user's club Lua passes, score 0..100, readable hub.
2. The addon: Safe -> `+100` (locked: any objectives part clamps to 100), Very insecure -> `-100` (locked at 0), Okay /
   Insecure -> the middle of the band (`(okay+safe)/2`, `(insecure+okay)/2`), a score -> `target - (score - addon)` (the
   objectives part of the last update), "game's own" -> 0. Clamped to +-100.
3. Write `+0x118`, read it back, call `UpdateJobSecurityScore(this)` (the game computes the score and posts the level
   event), read the result back. If the objectives part moved since the last update the result says so ("82 instead of
   77"). A failed call puts the old addon back.

Nothing else is written. The level shown in the board screen is the game's own result.

## 3. JobSwitchManager: the sack

```
JobSwitchManager (0x1E8)                                                   [H]
+0x008 CareerHub*
+0x1B8 serialised block (serialiser 0x147EDF91C): +0 mLastJobSwitchDate, +4 mPreviousTeamId, +8 ..., +0x28 mPendingSack
       (+0x1E0), +0x29 mWasSacked (+0x1E1)
```

* `HandleEvent(this, id, ev)` `0x147DD2A30` (vtable slot 1): `0x17` (job switch): date / previous team, `word [+0x1E0] = 0`;
  `0x0F` DAY_PASSED: `if byte [+0x1E0] != 0: jmp SackManager`; `0x3B`: an entry into the +0x1C0 list `[H]`.
* `SackManager(this)` `0x147DDF900` (its own name string): `byte [+0x1E1] = 1`, allocates a 0x18 event and
  `PostEvent(*(hub+0x4F8), 0xAD, ev)` - the game's "you have been sacked" flow `[H]`.
* Callers: the DAY_PASSED tail jump above and `0x147F57D14`, the `CM_Manager_Contract_Ended` handler (adds that message,
  then `SackManager`) `[H]`. Who sets `mPendingSack` was not found as a direct byte store in the career code (the block is
  copied whole by `0x147DB9900` / `0x147DB9C04`); it does not matter for the hook, which sits on the one function both
  paths reach.
* Readers of `mWasSacked`: `ApplyForJob 0x147DBBF64`, `OnResponseDue 0x147DD53EC`, `0x147DCB274`, `0x147DCEA40`,
  `0x147F52A64` (the post-sack flow, `SackAdvance`, `SackIntoAcceptedTeam`) `[H]`.

### 3.1 Unsackable (manager_rules_win.cpp)

The detour on `SackManager` is the only refusing hook in Turbo. With the switch off, or with
`turbo_output\call_manager_rules_off.txt`, `hook_jsm_sack_manager_off.txt` or `game_hooks_off.txt` present, it calls the
original. With the switch on it counts the attempt, clears `mPendingSack` on a validated JobSwitchManager (so DAY_PASSED
does not ask again; `mWasSacked` is never touched) and returns without calling the original. Skipping the call is safe for
both callers: `SackManager` is `void`, the DAY_PASSED path tail-jumps into it and the contract-ended handler only frees its
string afterwards. The switch is off at every game start; "keep" (`turbo_output\manager_rules_keep.json`) makes the Lua
bridge switch it on again once per session as soon as the native is installed.

Live Editor's own `le_misc_features.json` has an `unsackable` switch too. The addresses in its `le_offsets.json` do not
include `SackManager` or `HandleEvent`, so the two should coexist (either one keeps the user in the job); if Live Editor
ever rewrites the first bytes of `SackManager`, Turbo's signature no longer matches and its hook is simply not installed
(the Status line says why).

## 4. Turbo implementation (as built)

| Layer | File | What |
|---|---|---|
| core | `turbogui/src/core/manager_rules.h/.cpp` | layouts above, `validate_com` / `validate_jsm`, `read_job_security`, `level_plan`, `job_security_call`, `unsackable_call`, `sack_should_refuse` (tested on synthetic memory with a fake `UpdateJobSecurityScore`) |
| signatures | `turbogui/src/core/sigscan.cpp` | `com_vtable`, `com_update_job_security`, `jsm_vtable`, `jsm_handle_event`, `jsm_sack_manager` (same bytes as `scripts/re/manager_rules_signatures.json`; the native test resolves them on the image's bytes) |
| Windows host | `turbogui/src/win/manager_rules_win.cpp` | resolves the entries; pass-through capture hooks `com_update_job_security` / `jsm_handle_event` (vtable-checked `this`; an independent check of Lua's `mem.manager` pointers); the guarded `jsm_sack_manager` hook; the call (game call op 3) runs at once on the game thread or is queued; Status tab line "manager_rules: ready / off (why)" with managers seen, refused / passed sacks, last outcome |
| mailbox | game call op 3 | args `sub-op, address, value, user team`; sub-ops 1 get, 2 set level, 3 set score, 4 restore, 5 unsackable, 6 flags |
| Lua bridge | `imports/turbo/bridge.lua` | `TurboManagerRules(sub, addr, value, team) -> ok, text, status, out0, out1`; `bridge_state.json` `manager_rules` {score, addon, level, bands, locked, sack_pending, sacked, keep_unsackable} read with checked reads; re-applies a kept unsackable |
| Lua features | `features/manager_rules.lua` (`turbo_manager_rules.lua`), `features/manager_move.lua` (`turbo_manager_move.lua`) | feature flags `modules.manager_rules` / `modules.manager_move`, validation, dry run, confirm |
| GUI | `ui_teams.cpp` Managers tab | "Manager rules: job security, unsackable (Manager Career)" and "Manager market: move a manager, make one available" |
| tests | `turbo/tests/t14_manager_rules.lua`, `test_main.cpp` `test_manager_rules` + UI case | |

Kill switches: `turbo_output\call_manager_rules_off.txt` (the call and the refusing hook), `hook_com_update_job_security_off.txt`,
`hook_jsm_handle_event_off.txt`, `hook_jsm_sack_manager_off.txt`, `game_hooks_off.txt`.

## 5. Manager market: the database is what the game reads

`0x147B6E1C8` (called by the JobMarketManager's AI hiring `0x147DDC104`) builds
`SELECT managerid FROM manager WHERE teamid = 0 AND islicensed = 0 ... ORDER BY managerjointeamdate` (first up to 10),
then the same with `islicensed = 1` (`0x140602D28(query, field, op, value)`: op 0 at `+0x38`, the value printed with
`%d`) `[H]`. **A free-agent manager is a `manager` row with `teamid = 0`**, and the AI hires from those rows; the club
pages read `manager.teamid`. The JobMarketManager also keeps `mFreeAgentsManagerIdsWithLastTeamMale/Female` (serialised,
`0x147ED93D8`): a memory of the last club, not the pool.

`features/manager_move.lua` therefore edits `manager.teamid` only, keeping every club at one manager:
* move M to club T: M -> T, T's manager -> M's old club (or the free-agent pool when M was free);
* make M available: M -> 0, a free agent (the chosen one or the first) -> M's old club; refused when there is none;
* never the user's club (either side), never a national team, both teams must exist; everything validated against the
  field before the first write; a failed write puts the earlier ones back.

## 6. Managers tab: the 3D-model miniface

The game's only manager-head builder `0x147D94218` (`cmAvatarPlayerHeadsVdd`, `heads_staff`) is called from
`0x147BDD350`, `0x147C98AE4`, `0x147DEDE70` and `0x147F46604`; `0x147BDD350` and `0x147F46604` set `[obj+0x48] = 9999`
(the created avatar) or `31399` (the pro) and `[obj+0x4C] = [userEntity+4]`, the user's club id (the same `0x14154ADBC` / `0x142B1DDEC` pair
`0x147E0314C` uses to read `mUserTeamId`) `[H]`. The descriptor gets `+0x00 = id`, `+0x08 = club id`, `+0x64 = 0`,
`+0x68 = (id == 9999)`. Turbo's manager request now matches it: second id = the manager's club (`manager.teamid`,
-1 when none), `+0x64 = 0`, `+0x68` only for head id 9999, also when the descriptor was learned from a player request.
The game itself never renders an AI manager's head through this path (AI managers use their `heads_staff` pictures), so
whether `headassetid` renders as `+0x00` is for the in-game test; the "Game id" box lets the tester try the managerid.

## 7. Endless career / retirement

* No season limit: there is no `MAX_SEASONS` / career-length setting string, `CareerComplete` (a flow transition name at
  `0x14AFDDD28`) has no code reference, and the only `careerenddate` reads are the legends bio `0x1472F056C`. FC 27's
  manager career has no forced end to switch off `[M]`.
* `MANAGER_RETIREMENT/MANAGER_RETIREMENT_MAX_AGE` (`0x147B3D10C`, a byte in the JobMarketManager settings copy) governs AI
  managers in the manager market, not the user `[M]`. The previous checkpoint's "JobMarketManager+0x150" is not that
  setting in any proven way: `0x147DBC9E4` reads `+0x150/+0x151/+0x152` as signed score modifiers set together by
  `0x147DE1164` `[H]`, so nothing is written there.

## 8. Negotiation / approach bypasses (not built; leads)

* FC 27 Live Editor's own Miscellaneous Features (`le_misc_features.json`: `disable_neg_status_check`,
  `allow_transfer_loan_app`) cover "negotiation status check" and "transfer / loan approval"; Turbo runs next to it.
* The approach refusal reason: the transfer hub UI `0x148315552` asks `[ui+0x18]->vfunc(0x128)(playerId, teamId)` and maps
  4 Transferblock, 7 Retiring, 9 Contract, 10 Grudge, 11 RivalTeam, 12 NoRelocate, 13 IncompleteArea, 15
  LeagueOverallLow, 16 OneClubPlayer to `CM_Transfer_CannotApproach_*` `[H]`. The game-side enum (BuyReason, to-string at
  `0x147F8B53C`): 1 ALREADY_LOANED, 2 EXCHANGE_DEAL_SIGNED, 3 HAS_PRESIGN_CONTRACT, 4 TRANSFERBLOCKED, 5
  LOAN_LISTED_WITH_OFFER, 6 MAX_OFFERS, 7 RETIRING, 8 SAME_TEAM, 9 CONTRACT_RESTRICTION, 10 GRUDGE, 12
  NOT_WILLING_TO_RELOCATE, 13 PLAYER_TOO_IMPORTANT, 14 CANNOT_AFFORD, 15 LEAGUE_TOO_LOW, 16 ONE_CLUB_PLAYER, 17
  SETTING_RESTRICTION, 19 LOW_LOB `[H]`.
* A Turbo version needs the function behind that interface slot proven (which class, live), then an opt-in hook that
  returns 0 for the "soft" reasons only (grudge, relocate, league too low, one-club, too important), never for loaned /
  pre-signed / same team, which would corrupt the transfer state. Not done in this pass.

## 9. Unsupported leagues (not built; leads)

The career setup's league / team lists come from `0x147B70154` (leagues + nations, teams by `leagueteamlinks`,
`cmplayers`, and the `modeavailability` table at `0x147B70644`) `[H]`; Live Editor hooks inside it (`0x147B70518` in
`le_offsets.json`), most likely its own "unsupported leagues" switch. The gate is a database table read when a new career
is created, i.e. before any career-only Turbo tool runs; a Turbo version would edit `modeavailability` from the main menu.
Not done in this pass.

## 10. In-game test plan (integrator; the throwaway career, save first)

1. Status tab > Game hooks: `com_vtable`, `com_update_job_security`, `jsm_vtable`, `jsm_handle_event`, `jsm_sack_manager`
   found; hooks `com_update_job_security`, `jsm_handle_event`, `jsm_sack_manager` active; "manager_rules: ready". Advance a
   day: "ClubObjectivesManager seen at 0x..", "JobSwitchManager seen at 0x.." (the capture hooks).
2. Managers tab > Manager rules: the score and level match the board / job security screen. "Very insecure (locked)":
   toast "job security N -> 0 (locked very insecure)"; open the board screen: very insecure. "Safe (locked)": 100, safe.
   "Okay": the middle of the band. "Game's own": the game's score. Advance a week with each lock: it stays.
3. Unsackable ON (this session), then "Very insecure (locked)" and sim a few weeks: the log shows "the game called
   JobSwitchManager::SackManager ... refused" and you keep the job (Status line: refused N). OFF + very insecure: the
   game sacks you (the normal flow; reload the save).
4. Manager market: pick an AI manager, move him to another club: both club pages show the swap after reopening them;
   save, reload: it stays. Make one available: he shows without a club, a free agent takes the club; advance some weeks:
   the AI may hire him elsewhere.
5. Miniface: Managers > a manager > Miniface > The 3D model: Generate (head id); if nothing renders, try the managerid in
   "Game id", and the user's own manager with 9999.
6. Kill switches: `call_manager_rules_off.txt` -> "manager_rules: off (kill switch ...)" and the SackManager hook passes
   through; `hook_jsm_sack_manager_off.txt` -> the hook passes through.
