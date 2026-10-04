# Player development, reveal player data, youth academy (Phase 3 track 2)

Scope: offline Manager Career only. FC27.exe 1.0.140.64835 (build key `6AB9813C-211EF000`), image
`turbo_output\fc27_image.bin` (base 0x140000000). Live Editor's DLL is not read or decompiled: the only Live Editor
facts used are its documented Lua API (`lua\DOC.MD`) and its enum files. [H] = read in the image, [L] = seen live,
[D] = Live Editor documentation.

What FC 26 Live Editor had and what Turbo does now:

| FC 26 LE | FC 27 Turbo | How |
| --- | --- | --- |
| Reveal player data | `reveal` module, Players > Growth > Reveal data / Reveal his club, Tools > Scouting | game call into the career's PlayerDataRevealManager (Turbo.dll, section 1) |
| Development (XP multiplier, bonus XP, no decline) | `development` module (action + weekly auto), Players > Growth, Tools | players table + the game's development plan through LE's plan natives (section 2) |
| Youth academy tools | `youth` module, Tools > Scouting, development and youth academy | `career_youthplayers` + `players` rows (section 3) |

## 1. Reveal player data: PlayerDataRevealManager (PDRM)

### 1.1 The class [H]

* Live Editor manager type id **78** (`ENUM_FCEGameModesFCECareerModePlayerDataRevealManager`, enums.lua) [D].
* The career hub builder allocates **0x7D0** bytes tagged `"PlayerDataRevealManager"` (0x147F19108 `lea r9, "PlayerDataRevealManager"`,
  0x147F19119 `mov ecx, 0x7D0`) and calls the constructor **0x147E2C104** on it with `hub = [r14+0x10]` (0x147F2B3E8 is
  `mov rax,[rcx+0x10]`). The constructor stores the vtable **0x14B01D5B0** (RVA 0xB01D5B0, `lea rax,[rip+..]` at
  0x147E2C113) at +0 and the hub at +8. So the vtable is proven to be this class's by the allocation tag right before
  the constructor call (standings-ui-path.md section 0: no vtable taken from a constructor without proving its class).
* The builder then registers the object in the hub: 0x147EC0868 appends it to the hub's list at `hub+0x9D8`
  (count `hub+0x9D0`), so `[[hub+0x9D8]] == pdrm`. The screens fetch the manager exactly that way
  (`PlayerDataRevealManager::PlayerRevealData` handler 0x147F2DD70: `mov rcx,[rax+0x9D8]; mov rcx,[rcx]; call GetPlayerRevealData`).
* Record vector at **+0x790** (begin / end / capacity), records of **0x14** bytes, sorted by player id:

| off | type | field |
| --- | --- | --- |
| +0x00 | int | playerId |
| +0x04 | i16 | scout id (-1 = none) |
| +0x06 | u16 | flags |
| +0x08 | int | scouting points; **204 (0xCC) = fully revealed** (34 attributes x 6 levels; level 7 of 7 = exact) |
| +0x0C | int | day the record was made (yyyymmdd) |
| +0x10 | u8 | 0xFF |

* Insert **0x147E2ECD0** (vector*, Record*): lower_bound by player id; the same player is overwritten, else inserted.
  When `count + 1 >= 1500` (0x5DC) it first sorts the vector with the game's own comparator and keeps **1400** records
  (`lea rdx,[r9+0x6D60]`), then re-sorts by player id: revealing too many players makes the career forget older
  scouting reports. Turbo refuses a scope that would reach the limit unless `allow_evict` is set.
* `GetPlayerRevealData` **0x147E38D54** (pdrm, pid, out, ...) is what every screen asks (23 call sites: Player Bio, GTN,
  squad screens); it turns the record into the per-attribute exact / range / hidden state (34 attributes, level 7 = exact).

### 1.2 The functions Turbo calls [H]

```
void RevealPlayerFully(PDRM* this, int playerId)   0x147E325F0   signature pdrm_reveal_player
    today = TodayInt([[hub+0x318]] + 0x34)                        (0x142AA5824, calendar_today_int)
    if PlayerExists([[hub+0x418]], playerId)                      (0x147B9110C: a "players" row query on playerid, count == 1)
        Insert(this+0x790, {playerId, -1, 0, 204, today, 0xFF})
void RevealTeamFully(PDRM* this, int teamId)       0x147E32678   signature pdrm_reveal_team
    for every player row of the team ([[hub+0x418]], 0x147B7281C, 0x130-byte rows): RevealPlayerFully(this, pid)
```

The game runs these itself from `HandleEvent` **0x147E3DAC8** (`pdrm_handle_event`, `(this, int eventId, Event*)`):
events 0x3E / 0x4A / 0x60 (a player joins the user's club, `ev+0x1C` == user team) -> `RevealPlayerFully(ev+0x18)`;
0x73 (the user takes a club) -> `RevealTeamFully(ev+0x20)`; 0x4E scout report; 0x0F / 0x10 day / week passed.
Turbo therefore makes no state the game cannot make on its own: it is what happens when a player joins your club.

### 1.3 Turbo.dll (turbogui/src/core/reveal.*, src/win/reveal_win.*)

* Signatures (sigscan.cpp built-in table, `scripts/re/development_signatures.json`, every pattern unique in the image;
  check with `bash scripts/re/py.sh scripts/re/check_signatures.py scripts/re/development_signatures.json`):
  `pdrm_vtable` (constructor, resolve rip at +0xF), `pdrm_handle_event`, `pdrm_reveal_player`, `pdrm_reveal_team`,
  plus the shared `calendar_today_int`.
* Capture hook `pdrm_handle_event` (pass-through; records `this` when its vtable is the PDRM's): an independent source
  for the manager pointer. Lua passes its own (manager table slot 78) plus the manager table.
* `pdrm::choose`: Lua's pointer, else the captured one; when both exist and differ, Lua's is used only with the manager
  table (slot 78 then decides in validate; the captured one may still be the previous career's).
* `pdrm::validate` before any call, nothing written: pointer readable, vtable == 0x14B01D5B0, the whole 0x7D0 bytes
  readable, hub at +8, `[[hub+0x9D8]] == pdrm` (the hub's own slot), the hub's calendar (+0x318) and teams (+0x418)
  managers readable (the game dereferences them in the call), manager table slot 78 holds it, the record vector sane
  (begin <= end <= cap, whole 0x14 records, <= 1500, readable, sorted by player id).
* The call runs on the game thread only (synchronously when Lua runs there, which is the case for Turbo window commands
  sent through the synthetic career event; else queued to the dispatcher). Read back afterwards: player mode needs the
  player's record with 204 points (dated today when the calendar is readable); team mode reports the record count.
* Op 3 of the mailbox call block (`kCallOpReveal`): args pdrm, mode (0 player / 1 team), id, manager table; outputs
  points after (player) or records after (team), records before. Lua native `TurboRevealPlayerData(pdrm, mode, id)`.
* Kill switches: `turbo_output\call_reveal_off.txt` (the call), `hook_pdrm_handle_event_off.txt` (the capture hook),
  every game-hook switch. Status tab: `reveal: ready | PlayerDataRevealManager ... | runs, queued` and the last outcome.

### 1.4 Live check

Not done live in this track: during the work the game was at the main menu (no career; `find` of the vtable pointer
gave one stale object whose record vector held 0x5000000000 garbage, i.e. freed memory, which validate refuses). The
static proof above plus the run-time validation (vtable, the hub's own slot pointing back, slot 78) make a wrong object
a refusal, never a call. In-game plan: section 4.

## 2. Development

FC 27 Live Editor v27.1.2 does **not** ship FC 26's `PlayerDevelopmentManagerAddPlayer / Save / Load / RemovePlayer`
(XP multiplier, bonus XP, no decline) [D: globals dump]. It ships two natives for the game's own development plans [D]:
"players in the team managed by the user got development plans; data stored in these plans got higher priority than
fields in players table" (all attributes, work rates, skill moves, weak foot):

```
bool PlayerHasDevelopementPlan(int playerid)
void PlayerSetValueInDevelopementPlan(int playerid, string field_name, int value)   -- e.g. ("composure", 99)
```

So an attribute written only into the players table is put back by the plan for the user's players. Turbo's
`development` module (features/development.lua) writes the players table and, for a player with a plan, the plan with
the same values:

* **to_potential**: every attribute of the player's group (goalkeeping + reactions for a goalkeeper, every other
  attribute for an outfielder) rises by `potential - overall` (capped at 99) and `overallrating` becomes the potential.
* **add**: +N (or -N) on the group or on listed attributes; the overall moves by N when at least 6 attributes did.
* **set**: given attributes to given values. **potential** (1..99) and **growthprofile** (players field) on any mode.
* **weekly forced growth (auto)** `auto.development {enabled, players[], user_team, weekly 0..5, no_decline, events}`:
  on WEEK_PASSED each listed player below his potential gains `weekly` per group attribute; with `no_decline` an
  attribute lower than last week's snapshot is put back (age decline). The snapshot lives in TURBO_STATE (this game
  session) and starts over on POST_LOAD_PREPARE and when the auto is armed (arming grows nobody).
* Every value is validated against the field range before anything is written (core/db.lua). Caps key
  `development` = the two plan natives (greyed when missing); `development_xp` = FC 26's XP natives (Bulk edit).
* The game recomputes the overall itself on its own growth events; Turbo's overall write keeps the screens consistent
  until then.

Not done: a native call into the game's development manager. The plan natives are Live Editor's own documented API and
cover the same need without a new game call; a native path stays a follow-up only if the plan natives turn out not to
stick (in-game plan below).

## 3. Youth academy

`career_youthplayers` (fields from the game's meta: `playerid`, `potentialvariance` 0..7, `playertier` 0..3,
`monthsinsquad`, `swinglowpotential`) lists the academy. `youth` module: `list` (CSV `turbo_output\youth_academy.csv` +
summary), `set` one academy player's potential / position (players table) and tier / potential range width
(`potentialvariance` 0 = the scout report gives the exact potential). Only academy players are changed.

## 4. In-game test plan (throwaway career only)

1. Status tab: `reveal: ready | PlayerDataRevealManager not seen yet ... reveal_player 0x147E325F0, reveal_team 0x147E32678,
   vtable 0x14B01D5B0`. Load the career, advance a day: the line shows the manager address and events seen.
2. GTN: pick a player your scouts have not reported on (potential shown as a range or "?"). Players tab > that player >
   Growth > Reveal data. Expect the toast "player N: data revealed fully (scouting points 204/204, dated <today>, ...)",
   and in his Player Bio the exact attributes and potential. Dev service read of `[pdrm+0x790]` shows his record.
3. Tools > Scouting > Reveal club ID (e.g. a Serie A rival): every player of that club exact. League: tick, Reveal
   league ID (Serie A); the toast lists the game calls and the record count; a scope near 1500 is refused.
4. Development: Napoli youngster (OVR below POT): Players > Growth > Develop to potential: OVR = POT in Turbo; open his
   game bio. Then "Grow him every week" (2 points, no decline), advance a week: his attributes +2, the overall +2,
   and they stay after the game's own growth event (the plan holds them). If the game puts the old values back, the
   plan natives do not stick for that field: note it here.
5. Youth: Tools > List my youth academy; set one youth player's potential 90 and range width 0; the academy screen shows
   the exact potential.
6. Kill switch: create `turbo_output\call_reveal_off.txt`: Status says off, the button reports it; delete it again.
