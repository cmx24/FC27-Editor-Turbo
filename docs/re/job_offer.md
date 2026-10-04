# FC 27 job offers (JobMarketManager): static RE notes and Turbo design

Source: `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (FC27.exe 1.0.140.64835, base `0x140000000`), analysed
2026-10-03 with the helpers in `scripts/re/` (`rx_jobs.py`, `refs.py`, `disfn.py`, `dispscan.py`, `sig_jobs.py`). Static only:
nothing below was run in the game yet. Confidence tags: `[H]` read directly from the code, `[M]` strong inference,
`[L]` guess. Every address is for this build; `scripts/re/job_offer_signatures.json` carries byte signatures and
`scripts/re/job_offer_verify.py` re-resolves them (all 15 are unique in this image).

Scope: the user's own offline Manager Career. Only career-mode game logic was looked at.

## 0. Summary

* Club job offers live in the career manager **JobMarketManager** (Live Editor manager type id **53**, object size
  `0xC90`, vtable `base+0xB016428`, ctor `0x147db6088(this, hub)`) `[H]`.
* The user's **applications and the offers that come out of them are one hash table keyed by team id at
  `JMM+0x8e8`** (`+0x8f0` buckets, `+0x8f8` bucket count, `+0x8fc` element count). Each node holds
  `{teamId, JobOffer{mTeamId, mLeagueId, mJobOfferSentDay, mWage, mLeaguePos, W/D/L, mIsAccepted, mWasTeamWatched...},
  wage, countdown}` `[H]`. An entry whose `mJobOfferSentDay != -1` is a received offer; `-1` is a pending application.
* The game's own flow `[H]`: user applies (`ApplyForJob 0x147dbbf64`) -> node inserted with
  `countdown = rand(CLUB_OFFER_RESPONSE_DAYS_MIN..MAX)` -> every `DAY_PASSED` (event 15) `HandleEvent 0x147dd232c`
  decrements it -> at 0 `OnResponseDue 0x147dd53ec` fills `offer.mTeamId/mWage` and, if the club says yes
  (compatibility score vs. a random roll), calls **`MakeOffer 0x147dd5510(this, &offer)`**, which stamps
  `mJobOfferSentDay = today` and posts the career event **JobOfferAction (type 0x72, action 0, log name
  `PamOfferReceived`)**. That event is what the rest of the game (email, hub notification, Job Offers screen) reacts to.
  Offers expire after `JOBOFFER/JOB_OFFER_EXPIRE` days (`JMM+0x10`) with action 3 (`JobOfferExpired`).
* **Minimal safe sequence for Turbo (club job, team T)**: on the game thread, `ApplyForJob(JMM, T)` if no application
  exists, find the node for T, copy `node.wage` into `offer.mWage`, set `offer.mTeamId = T`, call `MakeOffer(JMM, &offer)`.
  No struct is allocated by Turbo, no memory layout is invented: the game's own code creates the node and posts the event.
* National-team offers are a different system (InternationalsManager, see section 6); the serialised field
  `mForcedTeamsForOffers` exists there and is the natural hook, but its consumer was not traced in the time budget.

## 1. Finding the manager

Live Editor's enums (`lua/libs/v2/imports/career_mode/enums.lua`): `ENUM_FCEGameModesFCECareerModeJobMarketManager = 53`,
`JobSwitchManager = 54`, `InternationalsManager = 52`, `EventsManager = 40`, `CalendarManager = 24`, `UserManager = 129`.
Turbo's `core/mem.lua` `mem.manager(type_id)` already walks the FCE GM comm service table, so `mem.manager(53)` should
return the JobMarketManager `[M]`; validate with `*(uint64*)jmm == base + 0xB016428` before use `[H for the vtable]`.

The manager is created by the hub builder at `0x147f182c2..` (`mov edx, 0xC90`; name string `"JobMarketManager"` at
`0x14b0201f0`; ctor call `0x147db6088` at `0x147f18345`) `[H]`. Independent capture for the DLL: a MinHook detour on
`JMM_HandleEvent 0x147dd232c` sees `this` on every `DAY_PASSED` `[H]`.

```
JobMarketManager (0xC90)                                       [H unless noted]
+0x000 vtable base+0xB016428
+0x008 CareerHub* hub        slots (each is Manager**): +0x198 ?, +0x318 Calendar, +0x418 Teams,
                             +0x4f8 EventDispatcher, +0x6d8 ?, +0xf38 ?, +0x1038 User
+0x010 int  JOBOFFER/JOB_OFFER_EXPIRE            +0x014 int CLUB_OFFER_RESPONSE_DAYS_MIN
+0x018 int  CLUB_OFFER_RESPONSE_DAYS_MAX         +0x01c int MAX_SIMULTANEOUS_APPLICATIONS
+0x020 int  REAPPLY_COOLDOWN_DAYS                +0x024 int CHANCE_OTHER_GENDER_TEAMS
+0x028 int  MANAGER_TRANSFER_MAX_ENTRIES         +0x02c int MANAGERMARKET/WATCHLIST_MAX_ENTRIES
+0x048..+0x06c int WAGES_TEN..WAGES_HUNDRED      +0x08c.. float WAGES_MULTIPLIER_* (settings loader 0x147b3c930,
                                                  local offsets map as JMM = local - 0x50) [M on the copy, H on use sites]
+0x3f8 eastl::map<int teamId, TeamJobInfo>       AI-side per-team manager info (node key +0x20, value +0x28)
+0x458 / +0x4b0 queues of AI-team messages (AIHireManagerReply etc.)
+0x8e8 hashtable: +0x8f0 node** buckets, +0x8f8 u32 bucketCount, +0x8fc u32 elementCount   <- user applications/offers
+0x918 mDeclinedJobOffers  +0x948 mRejectedByUserJobOffers (serialised)
+0x978 JobOfferSystem (embedded, vtable base+0xB0165D8; slot +0x28 = compatibility score(teamId))
+0xb58 mWatchedTeamIds (rbtree set; size at +0xb78)
```
Serialisation (`0x147ed93d8`) names the members; the application-table element serialiser is `0x147ed3dc0 ->
0x147eaf3f0 -> 0x147ed997c ("mTeamId") -> 0x147ee1690` (the JobOffer fields below) `[H]`.

## 2. Application / offer node

```
ApplicationNode (bucket chain of the hashtable at JMM+0x8e8)             [H]
+0x00 int  key = teamId
+0x04 int  value.teamId
+0x08 JobOffer offer:
       +0x00 int mTeamId      +0x04 int mLeagueId
       +0x08 int mJobOfferSentDay   (yyyymmdd as int; -1 = application pending, no offer yet)
       +0x0c int mWage        +0x10 u8 mLeaguePos  +0x11 u8 mWins  +0x12 u8 mDraws  +0x13 u8 mLosses
       +0x14 u8 mIsAccepted   +0x15 u8 mWasTeamWatchedBeforeAcceptingApplication
+0x20 int  value.wage        (computed at apply time by WageForTeam 0x147dbd138)
+0x24 int  value.countdown   (days until the club answers; set by AddApplication, decremented on DAY_PASSED)
+0x28 ApplicationNode* next
```
Lookup (`HasApplication 0x147dd4fc4`): `b = buckets[teamId % bucketCount]`, walk `next` until `node.key == teamId`;
`buckets[bucketCount]` is the end sentinel `[H]`.

Evidence: `HandleEvent` DAY_PASSED branch (`0x147dd23c0..0x147dd253f`) reads `node+0x24` as the countdown, `node+0x10`
as the offer date, `node+0x1c` as "accepted", and passes `node+4` to `OnResponseDue`; `OnResponseDue` writes
`[value+4] = teamId` (offer.mTeamId) and `[value+0x10] = [value+0x1c]` (offer.mWage = wage); `AddApplication`
writes `[value] = teamId, [value+0x1c] = wage, [value+0x20] = rand(JMM+0x14, JMM+0x18)` `[H]`.

## 3. The functions (MS x64 ABI; `this` in rcx)

| name | VA | proto | what it does |
|---|---|---|---|
| ApplyForJob | `0x147dbbf64` | `(JMM*, int teamId)` | `"JobOfferApplicationApply"`. Returns at once if `HasApplication`. `wage = WageForTeam(..., leagueId)`; posts JobOfferAction(action 4); `AddApplication(this, teamId, wage)`; may add the team to the watch list (`0x147de5cf4`) `[H]` |
| HasApplication | `0x147dd4fc4` | `(JMM*, int teamId) -> bool` | hash lookup `[H]` |
| AddApplication | `0x147de2c08` | `(JMM*, int teamId, int wage)` | inserts the node (no-op if present), countdown = random(MIN..MAX) `[H]` |
| HandleEvent | `0x147dd232c` | `(JMM*, int eventId, Event*)` | 15 DAY_PASSED, 16 WEEK_PASSED, 17 MONTH_PASSED, 0x72 JobOfferAction `[H]` |
| OnResponseDue | `0x147dd53ec` | `(JMM*, ApplicationValue*)` | sets offer.mTeamId/mWage; `ClubWantsManager` and `rand(0,100) < JobOfferSystem.score(teamId)` decide; yes -> `MakeOffer`, no -> `DeclineApplication(teamId, 0)`; then `(**(hub+0xf38)+0x98)->vfunc[2](obj, accepted)` (unidentified listener) `[H]` |
| MakeOffer | `0x147dd5510` | `(JMM*, JobOffer*)` | `offer.mJobOfferSentDay = TodayInt(calendar)`; `offer.mWasTeamWatched = IsTeamWatched(teamId)`; allocates a 0x28-byte JobOfferAction event (vtable `base+0xAFF5DC0`, `+0x10 = 0x72`, `+0x18 = teamId`, `+0x1e = 0`) and `PostEvent(**(hub+0x4f8), 0x72, ev)` `[H]` |
| DeclineApplication | `0x147de0e98` | `(JMM*, int teamId, bool)` | `"JobOfferApplicationDeclined"` `[H]` |
| WageForTeam | `0x147dbd138` | `(JMM*, -, -, int leagueId) -> int` | star-rating interpolation of WAGES_* times league multiplier `[H]` |
| PostEvent | `0x14060124c` | `(Dispatcher*, int type, Event*)` | generic career event post, 232 call sites `[H]` |
| TodayInt | `0x142aa5824` | `(CalendarDate*) -> int` | `*(hub+0x318)+0x34` -> yyyymmdd `[H]` |

The JobOfferAction event is also produced with action 3 by `HandleEvent` when an offer expires (`JOB_OFFER_EXPIRE`
days after `mJobOfferSentDay`, only when `mIsAccepted == 0`) and with action 4 by `ApplyForJob` `[H]`. Who turns
action 0 into the inbox email / `JOB_OFFER_RECEIVED` (Live Editor event 124) was not traced: the handlers found for
type 0x72 outside the JobMarketManager were unrelated code. It does not matter for Turbo because `MakeOffer` posts
exactly the event the game posts; the in-game test confirms the email/notification `[M]`.

Note on ids: the game compares the event type with `0x72` (114) while Live Editor's enum lists 114 as
`YOUTH_PLAYERS_RISK_FACTOR`; either the enum is stale for this build or the JobOfferAction uses a separate id space.
15/16/17 match Live Editor's DAY/WEEK/MONTH_PASSED `[H]`.

## 4. Minimal safe sequence (club job)

All of it on the game thread (the hook-foundation dispatcher), with the career loaded and no simulation running:

1. `jmm = mem.manager(53)` (Lua) or the pointer captured by a `HandleEvent` hook; check the vtable.
2. `if (!HasApplication(jmm, T)) ApplyForJob(jmm, T);` -- the game computes the wage, creates the node, posts the
   "applied" action (harmless UI/telemetry side effect) and calls the hub+0xF38 listener's `vfunc[1]`. Re-checked on
   2026-10-03 (track "job offer - native call"): **ApplyForJob has no eligibility check of its own** (the only early
   return is `HasApplication`), so eligibility (club in a league, not the user's club, not a national team) is
   Turbo's job before the call; `HasApplication` afterwards only confirms the node exists.
3. `node = lookup(jmm, T)`; `offer = node + 8`; `offer->mTeamId = T; offer->mWage = node->wage;` (exactly what
   `OnResponseDue` does before `MakeOffer`); optionally `node->countdown = 0` so DAY_PASSED does not call
   `OnResponseDue` again later (it only fires when the countdown goes from 1 to 0, so a second response is possible if
   left alone: set it to 0 `[M]`).
4. `MakeOffer(jmm, offer)`.
5. Success check: `offer->mJobOfferSentDay != -1` and equals today (`TodayInt`), and the Job Offers screen / inbox
   shows the club (in-game).

The game's gates that this bypasses: compatibility score, `ClubWantsManager`, `MAX_SIMULTANEOUS_APPLICATIONS`
(`ApplyForJob` does not check it; the UI does), `REAPPLY_COOLDOWN_DAYS` (JobOfferSystem `+0x100` map teamId -> day
`[M]`). Nothing else is written outside the manager's own container.

## 5. Turbo implementation (as built, 2026-10-03: track "job offer - native call")

Feature flag: `turbo_config.json` `modules.job_offer` (`enabled` false by default, `teamid`, `confirm`) for the Lua
runner `lua\scripts\turbo_job_offer.lua`; the Turbo window sends the same module with overrides. The `caps` key
`job_offer` needs the Lua function `TurboJobOfferCreate`, which the bridge defines only after it found Turbo.dll's
`turbo_game_call` export (so the button is greyed with a reason until the GUI runs in the game).

Pieces (all in the repo; the offsets / signatures above are the source of truth):

| Layer | File | What it does |
|---|---|---|
| core (tested on synthetic memory) | `turbogui/src/core/game_calls.h/.cpp` | `jmm::` layout constants, `JobMarketFns`, the `GameCaller` interface, `validate_jmm` (vtable, 0xC90 readable, hub slots `+0x198/+0x318/+0x418/+0x4F8/+0x6D8/+0xF38(+0x98)` readable, bucket count 1..2^20, buckets readable incl. the `~0` end sentinel), `find_application` (the `HasApplication` walk: bucket `team % count`, chain through `+0x28`, at most 1024 hops, every pointer checked), `job_offer_create` (the section-4 sequence; refuses an open or accepted offer; writes `offer.mTeamId`, `offer.mWage = node.wage`, `countdown = 0`; reads `mJobOfferSentDay` back and compares it with `TodayInt`) and the mailbox **call block** helpers |
| signatures | `turbogui/src/core/sigscan.cpp` (`kBuiltin`, build `6AB9813C-211EF000`) | `jmm_vtable` (ctor pattern, offset 0x29, `resolve: rip` -> `0x14B016428`), `jmm_handle_event`, `jmm_has_application`, `jmm_apply_for_job`, `jmm_make_offer`, `calendar_today_int`: the same bytes as `scripts/re/job_offer_signatures.json` |
| Windows host | `turbogui/src/win/game_calls_win.cpp/.h` | `install_game_calls(mailbox)` after `install_game_hooks()`: resolves the functions, installs the pass-through capture hook **`jmm_handle_event`** (records `this` after checking its vtable: `jmm_seen()`), `job_offer_request(jmm, team, seq)` runs the sequence **at once when the caller is the game thread** (the thread the dispatcher last ran on = the career-event thread Lua pumps from) and otherwise queues it through `run_on_game_thread`; the exported `turbo_game_call(lua_State*)` (ignores the Lua state) reads op / args from the call block and writes status / outputs / text back. Kill switches: `turbo_output\call_job_offer_off.txt`, plus the hook switches (`game_hooks_off.txt`, `hook_jmm_handle_event_off.txt`) |
| mailbox | `turbogui/src/core/bridge.h` (version 2, size 0x2300) | call block at `+0x2020`: `i32 op, i32 status (0 idle / 1 ok / -1 failed / 2 queued), i32 seq, i32 result_seq, i64 args[4], i64 out[2] (sent day, wage), char[0x200] text` |
| Lua bridge | `turbo/package/lua/libs/v2/imports/turbo/bridge.lua` | `game_call(op, args, label)` writes the block, calls the export, reads the answer; `"queued"` is remembered and `check_game_call` publishes the outcome on a later event as `bridge_state.json` `game_call {seq, ok, text}`; `install_natives` defines `TurboJobOfferCreate(jmm, teamid) -> ok, text, status`; the event handler pumps the dispatcher before it runs mailbox commands, so a GUI request runs synchronously |
| Lua feature | `imports/turbo/features/job_offer.lua`, runner `scripts/turbo_job_offer.lua` | validation (flag, career loaded, team exists, not national, in a league when `leagueteamlinks` is readable, not the user's club, `confirm`), dry run, `mem.manager(53)` -> native |
| GUI | `turbogui/src/ui/ui_teams.cpp` `job_offers_section` (Managers tab, right pane, collapsing header "Job offers (Manager Career)") | club picker with search (league clubs only; own club and national teams hidden), "Create job offer" (disabled with a tooltip when the cap is missing / no career / own club), confirmation modal, status line, the native status lines (`HookReport::calls`). The Status tab shows the same lines under "Game calls" and the `jmm_handle_event` hook counters. Results arrive as toasts (`App::tick`: the mailbox result, and `game_call` from the state file for queued calls) |
| tests | `turbo/tests/t12_job_offer.lua` (7 cases: validation, dry run, the call-block protocol with a fake Turbo.dll, queued / failed / stale-answer / old-DLL paths), `turbogui/tests/native/test_main.cpp` `test_game_calls` (synthetic JobMarketManager + fake game: end to end, bucket chains, every refusal path, call block round trip, the six built-in signatures on the real prologue bytes incl. the vtable through the ctor's lea) and the UI driver case "Managers > Job offers" | |

Thread rule: the game functions are only ever called on the game thread. From the GUI the request travels GUI ->
mailbox command -> Lua (career-event thread) -> `turbo_game_call` (synchronous on that thread). From Live Editor's
script runner (`turbo_job_offer.lua`) the call is queued and runs on the next career-mode event; the runner says so
and the outcome lands in the Turbo log / a toast.

What the game is asked to do is its own DAY_PASSED path minus the two gates (`ClubWantsManager`, compatibility roll):
`ApplyForJob` (if needed) and `MakeOffer` are the game's own functions on the game's own objects; Turbo writes three
ints into the node the game created and nothing else. Nothing is allocated by Turbo.

### 5.1 In-game test plan (integrator; test career only, save first)

1. Start the game with Turbo, load the test Manager Career, press F8. Status tab > Game hooks: the six `jmm_*` /
   `calendar_today_int` signatures `found`; hook `jmm_handle_event` active; "Game calls: job_offer: ready".
   Advance one day (or open a career screen): "JobMarketManager seen at 0x..." appears (the capture hook), and the
   Status tab's "Not possible with this Live Editor build" list no longer names `job_offer`.
2. Managers tab > open "Job offers (Manager Career)". Search "Juve", pick Juventus (45), "Create job offer", Create.
   Expected toast: "Job offer from Juventus: job offer created: Juventus (45) wants you as manager (job offer sent on
   <today> (weekly wage N))". `turbo_gui.log` has "game call job_offer(team 45, jmm 0x...): ok [done]".
   If the toast says "queued": the request came off the game thread; advance a day and watch for the second toast.
3. In the game: inbox email about the offer (PamOfferReceived) and the Job Offers screen listing Juventus with the
   wage; the hub notification if the game shows one. Note which of these appear (open question 8.1).
4. Accept the offer in the game's Job Offers screen; the manager moves to Juventus (the season-end / immediate
   switch the game normally does). Also test Decline on a second club (e.g. Milan, 47).
5. Save, quit to the main menu, reload: the accepted job persists (and a declined club is in
   `mRejectedByUserJobOffers`: re-creating an offer from it should still work since Turbo bypasses the cooldown).
6. Negative paths: the same club again while its offer is open -> "already made you an offer on <day>"; your own
   club and a national team are not in the picker; with the career not loaded the button is disabled; create
   `turbo_output\call_job_offer_off.txt` -> "off (kill switch ...)"; `hook_jmm_handle_event_off.txt` -> the capture
   hook passes through (the call still works from Lua's `mem.manager(53)` pointer).
7. Stability: let a week pass with Turbo loaded (the capture hook runs on every JMM event), play a match, use the
   game's own job market (apply for a job the normal way) to confirm nothing about the table is disturbed.

## 6. National-team (international) offers

Different system, not the JobMarketManager. `InternationalsManager` (type 52) owns a serialised block
(`0x147ed97bc`) `{+0x00 mRejectedJobs, +0x20 mEligibleJobs, +0x40 mCurrentJob, +0x7c mLastDay, +0x88 mStartDate,
+0x94 mLastRumourEmailDay, +0xa0 mLeftJobDay, +0xd0 mWonWorldCup, +0xd1 mWonRegionalCup,
+0xd8 mForcedTeamsForOffers (vector<int>)}` `[H]`; an international offer is
`{+0 mNationalTeamId, +4 mOfferDate, +0x10 mExpirationDate, +0x1c mRenewalDate, +0x28 mRejectionDate,
+0x36 mWorldCupOccured, +0x37 mRegionalCupOccured, +0x38 mHasBeenStalled, +0x39 mIsRenewal}` (`0x147ed9658`) `[H]`.
Emails come from `InternationalCommSystem::SendMadeJobOfferEmail` (`0x147b45030`). `mForcedTeamsForOffers` is a
debug/forcing list: pushing a national team id there should make the next evaluation offer that job `[L]`; the
consumer and the evaluation trigger (season end? `JOB_OFFER_SCHEDULE`) still need to be traced. Turbo v1 therefore
offers club jobs only and greys national teams in the picker.

## 7. Risks

* `MakeOffer` skips the game's eligibility checks, and (re-checked) so does `ApplyForJob`: an offer from a club that
  cannot hire the user (wrong gender league, club with a protected manager) may produce an offer the UI cannot
  complete. Turbo's own gates: the club must be in `leagueteamlinks`, not a national team, not the user's club; an
  open or accepted offer from the club is refused. Anything beyond that is for the in-game test (5.1).
* Writing while the game simulates (Sim To Date, match) or outside the hub can race the DAY_PASSED handler. The call
  runs on the career-event thread (the same thread that runs DAY_PASSED), so it never overlaps the handler; the
  integrator should still make the request from the hub, not during a simulation.
* The call is made from inside Live Editor's career-event hook, i.e. while the game is posting an event. `MakeOffer`
  posts another event from there; the game itself does the same (`HandleEvent(DAY_PASSED)` -> `MakeOffer` ->
  `PostEvent`), so nesting a post inside event handling is the game's normal pattern `[M]`; the in-game test confirms.
* The hash table layout was read from this build; a bucket walk with a wrong `next` offset reads garbage. All reads
  are checked and bounded; the node is validated (`node.key == node.value.teamId == T`).
* The accepted/declined listener `(**(hub+0xf38)+0x98)->vfunc[2]` is not called by Turbo's sequence; if it is what
  drives a hub notification, the offer still appears in the Job Offers screen through the JobOfferAction event `[M]`.
* The unknown `[JMM+0x8d8]` / `[user+0x1e1]` "forced" flag path in `OnResponseDue` sets the date without posting the
  event (probably the tutorial); not used.

## 8. Open questions

* Which handler consumes JobOfferAction(0) to send the inbox email and the `JOB_OFFER_RECEIVED` career event.
* Whether the Job Offers screen reads the hash table directly or a cached list refreshed by the event (if cached,
  opening the screen after the event is enough).
* `mForcedTeamsForOffers` consumer and trigger for national-team offers.
* Live Editor's event-id enum vs. the game's 0x72.
* Whether Live Editor's Lua handler really runs on the thread that posts career events (the dispatcher records the
  pump thread; `game_thread_id()` on the Status tab). If it does not, the call still only runs there, i.e. on the
  thread Live Editor calls Lua from, and the in-game test decides if that is acceptable.

## 9. Re-verification log (2026-10-03, track "job offer - native call")

* `scripts/re/job_offer_verify.py` against `fc27_image.bin`: all 14 signatures unique, no drift; JMM vtable
  `0x14b016428` (slot 0 `0x147dbae58`).
* Re-read `JMM_ctor` (`lea rax,[rip+0x3260370]` at ctor+0x29 -> the vtable; `[rcx+8] = hub`), `MakeOffer` (reads
  `[offer+0]`, writes `[offer+8]` = TodayInt(`*(*(hub+0x318))+0x34`), `[offer+0x15]` = IsTeamWatched, allocates
  0x28 bytes through the global allocator at `0x14c269ea8`'s vtable slot 2, posts type 0x72 action 0 via
  `PostEvent(*(*(hub+0x4f8)), 0x72, ev)`), `HasApplication` (`div r8` with the 32-bit bucket count: bucket =
  `(int64)team % count`; chain `+0x28`; sentinel `buckets[count]`), `ApplyForJob` (no eligibility check; league from
  `0x14154b92c(teams manager, team)`; `AddApplication(this, team, wage)`; listener `(*(*(hub+0xf38))+0x98)->vfunc[1]`;
  watch list unless `[this+0x8d8]` / `[(*(*(hub+0x6d8)))+0x1e1]`), `OnResponseDue` (`[v+4] = [v]`, `[v+0x10] =
  [v+0x1c]`, then `MakeOffer(this, v+4)`), `AddApplication` (`0x147db9ecc(&table, &key)` returns the value slot;
  `[v] = team, [v+0x1c] = wage, [v+0x20] = rand(this+0x14, this+0x18)`), `HandleEvent` prologue (`this, edx = event
  id, r8 = event`). Everything matches sections 2-3; the one correction is noted in section 4 step 2.
* The built-in signature table's six entries are the same byte patterns; the native test resolves them on the real
  prologue bytes, including the vtable through the constructor's lea.
