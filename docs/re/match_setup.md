# Match setup and gameplay switches (FC 27 1.0.140.64835)

Static analysis of `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (base 0x140000000) with `scripts/re/*.py`, plus
read-only dev-service reads of the running game (04-10-2026, main menu and the throwaway career turbo04, Napoli, 1 July
2026). Confidence: [H] read in code or in the live game, [M] strong inference, [L] guess. Code: `turbogui/src/core/match_setup.*`,
`turbogui/src/win/match_setup_win.*`, `turbogui/src/ui/ui_match.cpp`, fixture edits in `core/fce_standings.*`.
Signatures: `docs/re/match_setup-signatures.json` (check: `bash scripts/re/py.sh scripts/re/check_signatures.py docs/re/match_setup-signatures.json`).

## 0. What Turbo offers (Competitions > Match setup)

| Feature | Mechanism | State |
|---|---|---|
| Injuries off for the played match | game variable `NEVER_INJURE` = 1 (SetInt game call) | implemented; in-game check pending |
| Injury frequency / severity beyond the slider, per side | `GAMEPLAY_CUSTOMIZATION/INJURY_{FREQUENCY,SEVERITY}_{USER,CPUAI}` 0..100 | implemented |
| Weather / time of day for the next matches | `OVERRIDE/WEATHER`, `OVERRIDE/TOD` (-1 = game decides) | implemented; value ids [L] |
| Difficulty for the next matches | `OVERRIDE_MATCH_DIFFICULTY` 0..5 | implemented |
| CPU makes no substitutions | `DISABLE_CPU_SUBSTITUTION` = 1 | implemented |
| Venue of an unplayed fixture | swap FixtureData home / away rows | implemented |
| Opponent of an unplayed fixture (same group) | write FixtureData rows, clash check | implemented, experimental |
| Forced result of a fixture ("match fixing") | hooks on the two FCE result handlers, opt-in | implemented, opt-in |
| Edited played result feeds the table | existing Live standings `edit_result` + the standings refresh (standings-ui-path.md) | already in place |
| Fatigue off, offsides off, VAR off, referee strictness | no game variable read by the match; see section 5 | not offered |

## 1. The game-variable store [H]

`GameVars` object at **0x14D26E510** (live: +0x00 = 1 enabled, +0x18 table 0xB8DF01F0, +0x20 / +0x28 / +0x30 arena
begin 0xB8E40218 / end 0xB8E6D1F0 / cursor 0xB8E55EF0, i.e. 95 KB free).

* **GetInt(name, default) 0x140856DB4**: reader lock on **0x14C193230** (0x14085873C: `lock xadd -1`), hash, walk the
  bucket, `node.var ? [var+8] : default`, `lock add +1`. The table is read through the global **0x14D26E528** (= object
  + 0x18; `mov rbp,[rip+..]` at +0x2E). Exists 0x140FEC134 (same walk, `var != 0`), GetBool 0x140856CE4.
* **Hash**: djb2 seeded with the table's +4 (5381) **over the name and its terminating NUL** (the loop adds the byte
  before it tests it). Verified on 12 live nodes (`TEST_AI_60HZ` = 0x46102015, ...). The WIP checkpoint hashed without
  the NUL: every lookup would have missed and every insert duplicated a name.
* **Table**: +0 u32 mask (live 0x1FFF), +4 seed 0x1505, +0x10 node pool (8192 nodes of 0x20), +0x18 buckets, +0x20 free
  list head (next at node +0x10). Node `{u32 hash; var*; next*; name*}`. Live walk: 1408 nodes, no node in a foreign
  bucket, longest chain 3, no node without a value.
* **SetInt(store, name, value) 0x14154F384**: writer lock (0x140D9BCA8: `lock add 0xFF000000`, succeeds when the word
  becomes 0, i.e. it was 0x01000000 = free), builds `{type 2, value}`, calls the insert 0x14154F3E0, releases with
  `lock add 0x01000000`. SetFloat 0x14154F324 writes type 3. Insert: an existing node with a value is updated in place
  (16 bytes, the bound-variable list at +0x10 kept, then 0x142780DAC copies the value to bound C++ variables);
  otherwise a value (0x18 bytes) and the upper-cased name are bump-allocated from the store's arena (0x14154F5C8) and
  0x14154F514 links the value to an existing node of that hash or takes a node from the free list to the bucket head.
  **Two null writes**: an exhausted arena returns 0 and the code writes the value / name through it; an empty free list
  writes through `0 + 0x10`. Turbo checks both before calling.
* Names are upper-case in the table (the insert upper-cases its copy, but the lookup hashes the caller's string):
  Turbo only accepts `A-Z 0-9 _ /`.

**What Turbo does** (`gv::apply` / `gv::clear`, game thread, kill switch `turbo_output\call_gamevar_off.txt`):
anchors from five signatures that must agree (table global == store + 0x18, SetInt's lock == GetInt's lock); store
enabled; table shape walked; for a new name: arena room for 0x18 + round4(len + 1) and a readable free-list head;
for an existing node: same name string (hash collisions refused), type 2. Then the game's SetInt, then a read-back.
Clearing a variable Turbo created stores 0 in node +0x08 under the writer lock, taken with the game's own protocol
(compare-exchange 0x01000000 -> 0, bounded spin, released with +0x01000000): GetInt / Exists see "absent" again; the
value object stays in the arena and is linked back on the next set (set / clear cycles allocate nothing). A variable
the game declared gets its previous value back through SetInt. Overrides live until the game closes (the store is
global; a career load does not reset these names).

## 2. Read sites of the offered variables [H unless marked]

Found by enumerating every `call GetInt` with a `lea rcx,"<name>"` before it (1568 call sites, 991 names).

| Variable | Read in | Semantics |
|---|---|---|
| `NEVER_INJURE` | 0x140FEBDB4 (match injury setup; callers 0x140FEB620 <- 0x140FEC1AC / 0x146A9A0F8) | `== 1` -> injury config byte 0 (never injure). Effect at kick-off [M] |
| `GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER` / `_CPUAI`, `INJURY_SEVERITY_USER` / `_CPUAI` | same function | if Exists: `GetInt(name, 50)` replaces the slider byte (`[settings + 0x28C + side * 0x4E ..]`) |
| `OVERRIDE/WEATHER` | 0x1407A24BC (fifagamesettings "Weather"), 0x148011994 (match settings build, +0x1F64), getters 0x1410C2AA8 / 0x147F50708 | `!= -1` replaces the weather; 0x1447A1598 maps ids 0..8 to five classes. Labels of the ids [L] |
| `OVERRIDE/TOD` | 0x148011994 (+0x1F68), career 0x1462254D8 / 0x146225660 ("CareerMode LWSQL": an override skips the career's own choice [M]), 0x14807ED38 | the match settings accept 0, 1, 3, 4 only (other values fall back) [M]: `TimeOfDay_0/1/3/4` strings |
| `OVERRIDE_MATCH_DIFFICULTY` | 0x1407A24BC ("DifficultyLevel") | `!= -1` replaces the level; clamped to 5 unless a mode flag allows more |
| `DISABLE_CPU_SUBSTITUTION` | 0x1416F48A0, 0x1416F4DE0, 0x145E0516C | nonzero skips the CPU substitution step |

Not offered:
* `OVERRIDE/STADIUM` (0x1410C2AD4, 0x14804BF60): an id the game cannot stream would stall the match load; FC 27 Live
  Editor's own `override_stadium` covers it.
* `OVERRIDE/HOMETEAM` / `AWAYTEAM`: swaps the teams of the match object, not the career fixture: the result would be
  booked against the wrong clubs.
* `WEATHER_TYPE`, `STADIUM_TOD`, `STADIUM_ID` in 0x1447AA610 (the WIP checkpoint's "career match setup"): their values
  only reach globals 0x14C11F120..128 that no code reads (write-only telemetry). [H]
* The text simulator's `INJURY/*`, `CARD/*`, `FATIGUE/*`, `SIM_SETTINGS/*`: 0x1410611EC loads
  `dlc/.../FootballCompEng/data/simsettings.ini` **into the same store** and 0x141061E30 reads them at once into
  `FCEI::RequestSimEngineInitialize` (career create / load, 0x147B00D40, 0x147F17404). The ini load overwrites any
  override before the read, so a variable cannot change simulated matches. [H]

## 3. Result fixing [H]

`FCEI::RequestUpdateMatchResult` (ctor 0x1440376B8, 0x3C0 bytes, type 0x2A at +0x10): +0x20 home goals, +0x24 away
goals, +0x30 extra-time pair, +0x38 penalty pair (0x144046D18: a pair is "set" when neither half is -1), +0x70 fixture id,
flags +0x3B8 (standings) / +0x3BA (scheduling) default 1. Built by the career for played and simulated matches
(0x147B490E0, 0x147DA3E34, 0x147EBAE30, 0x147EBCAE0) and inside FCE (0x148A08FE2).

* FCESchedulingManager vtable **0x14B1854A8**, slot 5 = HandleMessage **0x148A4E80C** -> 0x148A5BB40 (type 0x2A):
  fixture +0x0F / +0x11 goals, +0x10 / +0x12 penalties, +0x13 completion 1 / 2 / 3.
* FCEStandingsManager vtable **0x14B1852E8**, slot 5 = HandleMessage **0x148A4E8FC** -> 0x148A5BC04: the incremental
  table update (standings-fixtures-notes.md section 4).
* Live (turbo04): the FCE hub's manager list `[ifce+0x18] -> +0x10 -> {+8 begin, +0x10 end}` holds 8 managers;
  [4] carries vtable 0x14B1854A8 and [6] 0x14B1852E8.

Turbo hooks both HandleMessage functions **only after the first fix** (`fce_result_sched`, `fce_result_standings`; both
or none). The detour reads the message type first; for type 0x2A of a fixed fixture it checks that `this` carries the
manager's vtable (image base + RVA) and that neither extra time nor penalties are set, then writes the two goal ints
and calls the original. Both managers see the rewritten message, so fixture and table agree; the second hook finds the
score already set. A draw is refused by the UI unless the user's row sits in a league stage (a cup tie needs a winner).
Kill switches: `turbo_output\call_match_fix_off.txt` (no install, no rewrite), `hook_fce_result_sched_off.txt`,
`hook_fce_result_standings_off.txt`. Limits: player statistics (the statistics manager's 0x148A5C000 copy), the match
report, news and morale keep the real match; a match that went to extra time / penalties stays as played.

## 4. Fixture edits [H layout, M effect]

FixtureData (0x18 bytes, id == index): +0 u32 date YYYYMMDD, +4 u16 time HHMM (live values 1130..2000), +6 id,
+8 compobj (live: the **competition** node, 1118 for Serie A; the rows carry the group 1120), +0x0A / +0x0C home / away
standing ids, +0x0F.. scores (-1 unplayed), +0x13 completion, +0x14 used. Live: Napoli (team 48, row 3506) plays fixture
731 on 22.08.2026 19:45 away at row 3487.

* `swap_fixture_sides`: unplayed fixtures only (completion 0, both scores -1); both rows must be used rows.
* `set_fixture_teams`: two different used rows of the group the fixture's rows belong to; the UI keeps the user's side,
  offers the other clubs of the opponent's group and refuses a club that already plays on that date
  (`pairing_conflict`). The season's pairings are no longer balanced: experimental.
* Not verified: whether FCE keeps a per-team fixture index (none was seen; the scheduler reads the list) and when the
  career hub's fixture list (MainHubManager / FixtureManager caches) picks the edit up: the UI says "after the next day
  advance". The date is not edited: the scheduler's day processing was not traced.

## 5. Asked for, not offered

* **Fatigue off** in the played match: no match-time variable (the `FATIGUE/*` names are the simulator's, section 2);
  FC 27 Live Editor's Miscellaneous Features ("never tired players") covers it.
* **Offsides / bookings / injuries as competition rules**: `rule_offsides`, `rule_bookings`, `rule_injuries`, ... are
  competition settings (name -> id table 0x141F4000C) kept in FCE's SettingsDataList (DataManager +0x78, entry layout
  unverified, C3-live-standings.md). Not traced to the match: no switch.
* **Referee strictness**: an Attribulator schema (`gp_rules_refereestrictness`, `referee_strictness_*` fields): FC 27
  Live Editor's Gameplay Attribulator library edits it. No game variable.
* **VAR**: no variable or setting found by name.
* Stadium, kick-off time, crowd, CPU vs CPU, unlimited subs: FC 27 Live Editor's `le_misc_features.json` already has
  them; Turbo does not duplicate them.

## 6. In-game test plan (throwaway career only)

1. Status tab: `gamevar: ready | store 0x14D26E510, table 0x14D26E528, ...` and `match_fix: ready (hooks are installed
   on the first fix)`; turbo_gui.log "gamevar resolved ... with 1408 entries".
2. Competitions > Match setup: the next fixture is listed with date, time and competition. Swap home and away; advance
   a day; the hub shows the new venue. Pick another opponent; advance; the hub shows it.
3. Set "Injuries off" (NEVER_INJURE = 1) and "Weather"; dev-service read: the table holds the names (hash + NUL).
   Play the match (injury frequency at 100 for both sides as a control in a second match): nobody gets injured.
4. Fix the next CPU-vs-CPU fixture of the league at 5-0, advance past its date: the fixture shows 5-0, both table rows
   moved accordingly (Live standings view and the Standings screen). Remove the fix.
5. Live standings: edit a played result; the standings refresh line says the rows are shown; the Standings screen follows.
6. Clear every switch; the Status line shows 0 active overrides; kill switch files turn each part off.
