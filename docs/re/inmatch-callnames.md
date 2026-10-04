# In-match callnames: how FC 27 picks the name the commentary says for a player (static RE)

Build: FC27.exe 1.0.140.64835, build key `6AB9813C-211EF000`, Manager Career, Italian pack `ita_it`. Static work only
(no process access): the image `turbo_output\fc27_image.bin` (base 0x140000000) with capstone (`scripts/re/rx.py`,
`rx_capture.py`, `ripscan.c`), plus two raw reads of the running game made by track E4 on 2026-10-03 23:02-23:04 (same
build): the **base CommentaryDb EBX partition** `[0x309250000, +0x340000)` and the event contexts `[0x80800000, +1 MB)`.
The CommentaryDb dump is kept in `C:\FC 27 Live Editor\turbo_dev\masters\re_inmatch\` (not in git; the full 10,201-event
list with parameters is `commentarydb_base_events_all.json` there). Addresses, signatures and the event lists are in
`docs/re/inmatch-callnames.json` (22 signatures, each unique in the image's code; check with
`bash scripts/re/py.sh scripts/re/verify_inmatch_callnames.py`). The event table is re-extracted from a dump with
`bash scripts/re/py.sh scripts/re/commentarydb_params.py <dump>`.

Legend: **[V]** verified in the code or data; **[I]** inferred (consistent with the code, not proven); **[G]** what
the in-game test of 2026-10-04 showed.

## 0. Short answers

1. **Events.** 10,201 CommentaryDb events; every event declares its parameters, and a query can only carry declared
   ones [V]. `surname_ID` is taken by **5** events (`PLAYER_NAME`, `PLAYER_NAME_FE`, `PLAYER_LOW_SURNAME`,
   `PLAYER_NAME_HIGH`, `SHOT_TAKEN_VOLLEY`); `player_db_pID` by **1,665** (always with `player_db_gID`); 2,772 events
   take some `*_pID` (29 parameter names). The match code never speaks CommentaryDb events directly: it fires 181
   `trigger_*` events of `CommentaryContextSystemEvents`; 110 carry a player id, **7** carry `surname_ID` (§1.4).
2. **Precedence.** At match set-up the game computes one callname per match player (`GetCallname` 0x14294A0F4, once
   per player): own recordings (`PLAYER_LOW_SIMPLE` / `PLAYER_LOW_LINK` with `player_db_pID`, intensity 2) → -1;
   else `playernamemap` (only if `PLAYER_NAME_FE` has the id) → else the common name's commentary id when the player
   has a common name → else the last name's; no audio check on the last two, no further fallback (§2). That value is
   the `surname_ID` of the 7 triggers. Every player event also carries `player_db_pID`, which selects the player's own
   recordings independently. `player_db_gID` is **not** a name id: it is the gender group (1 men, 2 women), §1.3.
3. **Audit.** Ask `HasAudio` with Turbo-built queries for the surname events (`PLAYER_NAME`, `PLAYER_LOW_SURNAME`,
   `PLAYER_NAME_HIGH`) per commentary id, and for the 12 player-name events (`PLAYER_LOW_SIMPLE(_LOC)`, `_LINK`,
   `SPECULATIVE_*`, `LONG_RUN(_LOC)`, `BANTER(_LOC)`, `AND_CLUB`, `NAME_HIGH`) per player id, at the intensities the
   match uses (§3). The current build asks 2 of those 12 at one intensity, which is why it saw 751 `LINK` players and
   no `SIMPLE` one, and missed Gutierrez.
4. **Remap.** Do not rewrite in `SpeechQuery::SetInt`: it serves every trigger, crowd and SFX query and probably never
   sees the final CommentaryDb query. The right point is the speech registry's **pre-handler 0x1414A90C8**
   (`Preprocess`), which runs for every CommentaryDb event the game plays *and* asks about, after the trigger/context
   stage and before the bank selection. Rewriting `player_db_pID` there (B → A, or B → 0) gives (a) B speaks A's own
   recordings and (b) B, who has own recordings, falls back to his database callname (a generic surname set with
   Turbo's existing last-name / common-name / playernamemap writes), because the game's own set-up check
   (`GetCallname`) asks `HasAudio`, which runs the same pre-handler. One hook, one kill switch (§4).

## 1. The events that name a player

### 1.1 How parameters work [V]

* A query (`SpeechQuery`, 0x84 bytes, vtable 0x149621F80) gets its parameter list from the event context:
  `SpeechQuery::SetCtx` 0x1407B0F3C creates one `ParamValue` (0x50 bytes, from the scratch scope) per descriptor of
  `[ctx+0x28]` (u32 count at -4, entries `& ~4`), value = the descriptor's float default `desc+0x38`, and a map
  `desc+0x3C` (hash) → index at `q+0x48/+0x50`.
* `SpeechQuery::SetInt` 0x1407B03E4 → `FindParam` 0x1407AEF2C (djb2-xor of the name, `h = 5381; h = h*33 ^ c`, then a
  case-insensitive compare with `desc+0x20`) → `ParamValue::SetInt` 0x1407AFA58. **A name the event does not declare
  is silently ignored**, and a value outside a descriptor's allowed-value list (`desc+0x18`, flags `+0x44/+0x45`) is
  ignored too.
* Event ids (`ctx+0x44`), group ids (`ctx+0x40`) and parameter hashes are djb2-xor of the strings: group
  `CommentaryDbEvents` = 0x515CA0C5, `player_db_pID` 0x73D7AD2D, `player_db_gID` 0x73D7FE3A, `surname_ID` 0xDE127DE4,
  `player_intensity` 0xA896C286, `cm_sim` 0x5BE02103 (all re-checked by the verify script).

### 1.2 The CommentaryDb events (base EBX, 10,201 events) [V]

Layout read from the dump: db `+0x50` event array; event `+0x18` name, `+0x20` candidates (empty in the base db: the
language db fills them, `docs/callnames.md` 5.1), `+0x28` parameters, `+0x30` id; parameter `+0x18` name, `+0x20`
hash, `+0x24` position. Every id and hash equals djb2-xor of its name (0 mismatches over 10,201 events).

| | events |
|---|---|
| take `surname_ID` | 5 |
| take `player_db_pID` (each also `player_db_gID`) | 1,665 (270 of them `_LOC` variants) |
| take `surname_ID` or `player_db_pID` (listed in the JSON) | 1,668 |
| take any `*_pID` (keeper_pID, pass_from_pID, ptw_player_db_pID, sub_on_pID, ...) | 2,772 |
| use no player selector but `cm_sim` / gID / intensity only around a `player_db_pID` ("pure" player lines) | 387 |

The name events (all with `cm_sim` unless noted; order = declared position):

| event | id | parameters | names by |
|---|---|---|---|
| `PLAYER_NAME` | 0xA3037DAE | player_intensity, surname_ID, cm_sim | surname |
| `PLAYER_LOW_SURNAME` | 0x11DE0331 | cm_sim, surname_ID, player_intensity | surname |
| `PLAYER_NAME_FE` | 0xAD0CBBB2 | surname_ID, player_intensity, cm_sim | surname (frontend, launch bank) |
| `PLAYER_NAME_HIGH` | 0x232D915F | cm_sim, player_intensity, surname_ID, player_db_pID, player_db_gID | both |
| `SHOT_TAKEN_VOLLEY` | 0x25034BB5 | player_intensity, surname_ID, cm_sim, player_db_gID, player_db_pID | both |
| `PLAYER_LOW_SIMPLE` | 0x9D0D5E6C | player_intensity, cm_sim, player_db_pID, player_db_gID | own recording |
| `PLAYER_LOW_SIMPLE_LOC` | 0x0810BC33 | cm_sim, player_db_pID, player_intensity, player_db_gID | own recording |
| `PLAYER_LOW_LINK` | 0x0A3EC8A2 | player_intensity, cm_sim, player_db_pID, player_db_gID | own recording |
| `PLAYER_SPECULATIVE_SIMPLE` / `_LINK` / `_RETURN` | 0xCFF93E8B / 0x167E9305 / 0xC845D38F | cm_sim, player_db_pID, player_db_gID | own recording |
| `PLAYER_LONG_RUN` / `_LOC` | 0xD1CD7315 / 0x12E90DCA | cm_sim, player_db_gID, player_db_pID | own recording |
| `PLAYER_AND_CLUB` | 0xDA9E4225 | asset_team_ID, player_intensity, cm_sim, player_db_pID, player_db_gID | own recording |
| `PLAYER_BANTER` / `_LOC` | 0x7B2A7AE7 / 0xB012C838 | cm_sim, player_db_pID, player_db_gID (+ team_gID, opposite_team_gID, referee_gender, team_type for _LOC) | own recording |
| `PLAYER_NAME_START`, `PLAYER_NAME_MID` | 0x87091291, 0x4EA45731 | player_intensity | no name selector |
| `PLAYER_LOW` | 0x5A45F5DD | (none) | no name selector |

Beyond these, 1,650 situation events (`GOAL_SCORED_*` 253, `REPLAY_GOAL_*` 179, `REFLECT_UPON_*` 89, `COLOUR_REACT_*`
76, `SAVE_*`, `FOUL_*`, `MISS_*`, `SHOT_TAKEN_*`, ...) take `player_db_pID` + `player_db_gID`: their player-specific
variants are own recordings too. The JSON lists all 1,668 with their parameters (`events_with_surname_or_player_db_pid`).

### 1.3 `player_db_gID` is the gender group, filled by the game [V]

`Preprocess` 0x1414A90C8 (§2.3) gives every parameter whose name contains `_pID` and whose value is > 0 a twin
`_gID` (`player_db_pID` → `player_db_gID`, `keeper_pID` → `keeper_gID`, ...) = `map[value]` from the global
`shared_ptr` at 0x14C6451E0, or 1 when the player is not in the map (nothing at all while the map is null). The map is
built by `SpeechEventHandler` slot 161 (0x1438EBCE0): both teams' match players, `map[playerid] = 1 / 2 / 3` from
record `+0x4B4` (1 → 1, 2 → 2, other → 3) or, when that is 0, `1 + (record+0x4B0 != 0)` (0x1438DDB58), and the team
pair `team_gID |= player gID` over the starting XI (1 = men, 2 = women, 3 = both). It is handed to the audio system
(vtable 0x14A8DC638 slot 0x188 = 0x14392D67C → protected code 0x157C7D59F → 0x157BF7419), which swaps it into the
globals 0x14C6451E0 / 0x14C6450F0. So `gID` selects gendered grammar ("il"/"la", Italian is a `_LOC` language), never
a name: a name is either `player_db_pID` (own recording) or `surname_ID` (commentary id).

### 1.4 The trigger side: what the match code fires [V]

The match code (class `SpeechEventHandler`, vtable 0x14A8D3810, constructor 0x1438DAA80) fires `trigger_*` events of
the group `CommentaryContextSystemEvents` from `DefaultGameToAudio`, either through a `Trigger` builder
(`Trigger::SetInt` 0x141A5B550: a pointer-keyed map, first value wins; `Trigger::Post` 0x141A5B6BC converts every int
entry with `SpeechQuery::SetInt` and posts with audio vcall 0x70) or inline (ctx, query ctor, `SetInt`, vcall 0x70).
The context system (data) then decides which CommentaryDb lines to play. 181 triggers are fired from code; 110 carry a
`*_pID` (list in the JSON, `triggers_with_player_params`). **Only these carry `surname_ID`**, and all but two take it
from the per-match callname table `[SpeechEventHandler+0x40]+0x58+slot*4` (§2.1):

| trigger | builder | surname_ID source |
|---|---|---|
| `trigger_PASS` | 0x143906B1C (callers 0x1438EE72A, 0x1438F30AF) | table[receiver slot] (0x1438EE603 / 0x1438F29F6) |
| `trigger_ATTACK_PASS`, `trigger_THROUGH_BALL_RECEIVED_IN_BEHIND` | 0x1438F1E38 | table[slot] (0x1438F29F6), -1 when no slot |
| `trigger_THROUGH_BALL_IN_PROGRESS` | 0x1438C1428 / 0x1438C16D4 | table[slot] (0x1438C17F8) |
| `trigger_DRIBBLE_PROGRESS` | 0x140D0B1C8 | table[slot] (0x140D0B519), -1 when no slot |
| `trigger_SHOT_TAKEN` | 0x141A59772 (delayed dangerous-shot path only) | a stored value, source not traced |
| `trigger_PLAYER` | 0x1438EAE68 (SpeechEventHandler slot 195) | `player_db_pID` and `surname_ID` both = the message's id |

`trigger_PASS` also carries `player_intensity`: 2, or another value from the handler's local when the pass is rated
high (> 0.85, or > 0.64 plus a check); the CommentaryDb name events then select on it.

## 2. The precedence the match applies

### 2.1 At match set-up: one callname per match player [V]

`SpeechEventHandler` slot 85 (0x1438EB8B0, a match-state message handler) walks the match player array of the current
game-state snapshot (ring of 10 × 0x46A00 at `[0x14C27D180]`; array `[state+0x15E50 .. +0x15E58]`, 0xB10 bytes per
player: `+0` slot index, `+4` mode, `+8` playerid) and stores `table[slot] = GetCallname(handler, playerid, mode)` for
slot ≤ 0x31 (0x1438EBA79 / 0x1438EBA8D). `GetCallname` 0x14294A0F4 (its only caller):

0. `bridge = audio->vcall(0xE0)("CommentaryBridge")`; without a bridge it jumps straight to step 2 (the
   `playernamemap` id is then **skipped**, not used unchecked).
1. Two queries on one scratch scope: `PLAYER_LOW_SIMPLE` and `PLAYER_LOW_LINK`, each `{player_db_pID = playerid,
   player_intensity = 2}` (a null context is tolerated). If either `HasAudio` is true → **-1** ("own recordings").
2. `playernamemap.commentaryid where playernamemap.playerid = playerid` (0x1473F8B6C, a DB-service query; -1 when no
   row). When > 0: a third query `PLAYER_NAME_FE {surname_ID = id, player_intensity = 2}`; `HasAudio` true → that id.
3. Else the player record is loaded (0x140B1F9B4 / 0x140B23D18, which runs `SELECT commentaryid FROM playernames
   WHERE nameid = %d` for `lastnameid` (record +0x4A0 → +0xAE4) and, when `commonnameid` (+0x4A4) is not 0, for it
   (→ +0xAE8)). If the loaded playerid matches: **commonnameid ≠ 0 → the common name's commentary id, else the last
   name's**. No audio check, no fallback from a silent common name to the last name; a record that does not load
   leaves -1.

So when an id has no audio: an own-recording check that fails moves on; a `playernamemap` id without `PLAYER_NAME_FE`
audio moves on to the name ids; the name-id result is used as it is (900000 or an id without recording = silence in
the surname events). The table is computed when that message arrives (match start) and is not refreshed by database
edits during the match.

### 2.2 During play [V] + [I]

* Every player trigger carries `player_db_pID` = the real player id; the 7 triggers of §1.4 also carry `surname_ID`
  = the table value (-1 for players judged "own recordings").
* The context system turns triggers into CommentaryDb lines. Every CommentaryDb line goes through the speech
  registry (`[SpeechSystem+0x50]`): dispatch 0x1407AFD88 (play) or HasAudio 0x1444F340C (ask). Both take the
  registry lock `[registry+0xE0]`, find the node by event id + group, run the node's **pre-handler**
  (0x1424C2B18 → `Preprocess` 0x1414A90C8: `cm_sim`, `*_gID`, `team_gID`) and then the handlers (play: vcall 0x08;
  ask: vcall 0x10 = 0x1414A9D14 → event lookup 0x1414AAEF4 → bank selection on the declared parameters).
* A name is said through one of two independent selectors: `surname_ID` (5 events, the generic/real surname bank,
  value = table) or `player_db_pID` (1,665 events, the player's own recordings). Which line is chosen is data in the
  language bank and the context system [I].

### 2.3 What this explains [G] + [I]

* **Generic surnames by last-name id (Bianchi 900762, Pirlo 926385, Del Piero 922149) were spoken**: those players
  have no own recordings in the two checked events, so `GetCallname` returned the last name's commentary id (they had
  no common name and no `playernamemap` row), the table fed `surname_ID` of the pass/dribble/through-ball triggers.
* **Miguel Gutierrez 261865 was said by his own recording although his last name id was Totti 927940**: the game's own
  set-up check uses exactly the two events and parameters of Turbo's build, which found nothing for him. His table
  entry was **not** Totti: he has a common name (`commonnameid` 26180 "Miguel Gutiérrez" in the 13:16 `players` export),
  so step 3 takes the common name's commentary id, and 26180 has commentary id 900000 in FC 27's `playernames`
  (`C:\FC_Tools\FC Editor\default\27\playernames.txt`; his last name 14924 too; the generic "Gutierrez" is name 14919 /
  923354, not in the 2,462). So his surname lines were silent and Totti on the last name could never be used; the
  "Gutierrez" heard is his own recording, reachable through other `player_db_pID` events (most likely
  `PLAYER_LOW_SIMPLE_LOC` or a `PLAYER_SPECULATIVE_*` / `PLAYER_LONG_RUN` event, §3.3) that the context system plays
  for him whatever his table entry is [I]. To give him a generic surname, write his **common name** id (or a
  `playernamemap` row with a `PLAYER_NAME_FE` id), not the last name (§7.2).
* **Not read in a match**: `commentarynames`, `commentarypreview`, `editedplayernames` (display only; the record
  loader reads it for one special id 0x1C31E), the `playernames` text. `playernamemap` counts only through the
  frontend `PLAYER_NAME_FE` check.

## 3. Asking the game during a match: the true lists

### 3.1 Surname ids (a)

For each event E in `PLAYER_NAME`, `PLAYER_LOW_SURNAME`, `PLAYER_NAME_HIGH` (and `SHOT_TAKEN_VOLLEY` if wanted), with
Turbo's existing sequence (§5.4 of `docs/callnames.md`): `ctx = audio->vcall(0x48)("CommentaryDbEvents", E)`, scope
ctor, `SpeechQuery ctor(q, scope, ctx)`, `SetInt(player_intensity, i)`, for `PLAYER_NAME_HIGH` also
`SetInt(player_db_pID, 0)` (no player has id 0; `Preprocess` skips values ≤ 0), then per id `SetInt(surname_ID, id)` +
`bridge->vcall(0xB8)(q)`. FilterNames cannot be used: it hard-codes `PLAYER_NAME_FE`. Intensities: 2 (frontend and
default pass), plus the other values the match uses (log them first, §4.4; until then ask 1..4). Bits per id: one per
event (× intensity). `PLAYER_NAME_FE` stays the frontend list (launch bank).

### 3.2 Player ids (b)

Per player id P, the 12 own-recording events: `PLAYER_LOW_SIMPLE`, `PLAYER_LOW_SIMPLE_LOC`, `PLAYER_LOW_LINK`,
`PLAYER_SPECULATIVE_SIMPLE`, `PLAYER_SPECULATIVE_LINK`, `PLAYER_SPECULATIVE_RETURN`, `PLAYER_LONG_RUN`,
`PLAYER_LONG_RUN_LOC`, `PLAYER_AND_CLUB`, `PLAYER_BANTER`, `PLAYER_BANTER_LOC`, `PLAYER_NAME_HIGH`; query
`{player_db_pID = P}` plus `player_intensity = i` where declared; leave everything else unset (`cm_sim` and
`player_db_gID` are filled by `Preprocess`: gender from the current match's map, 1 = men for a player not in it, so a
women's player is only answered right in a match of her team). Skip an event whose context is null (not bound).

### 3.3 Why the Create Player build said `PLAYER_LOW_LINK` only, for 751 players

1. It asks 2 of the 12 own-recording events, at intensity 2 only (the same two `GetCallname` asks).
2. `PLAYER_LOW_SIMPLE` answered "no" for all 21,340 players in both builds (03:22 Create Player, 13:39 in a match),
   so the ita_it bank has no player-specific candidate there at intensity 2. The localized twin
   `PLAYER_LOW_SIMPLE_LOC` (same selectors, plus the gender `_LOC` events carry) is the likely carrier for Italian [I].
3. The bank was bound in both runs (2,462 names answered), so binding is not the limit; `player_db_gID` was 1 or unset
   (the map is null before the first match), which did not stop `LINK` either.
4. Consequence for the game itself: for those 751 players `GetCallname` returns -1 and the surname lines are silent
   whatever their name ids say; for players like Gutierrez it returns the database callname while own-recording lines
   still use their recording.

### 3.4 Cost

The 13:39 build did 47,529 `HasAudio` in 0.61 s of game time (≈ 13 µs each: the linear `strcmp` over 10,201 events
dominates). §3.1 at 4 intensities: ≈ 58k queries (≈ 0.8 s); §3.2: 21,340 × (7 + 5 × intensities) ≈ 470k at 3
intensities (≈ 6 s), i.e. ≈ 1,500 batches of 4 ms. Cheaper: run §3.2 for the career's league players first, or read
the runtime descriptor of `player_db_pID` in a bound context (`[ctx+0x28]` entry, `desc+0x44/+0x45/+0x18`): if it
carries an allowed-value list, that list may be exactly the recorded player ids (not checked: the E4 dump stops
before `0x809AACE8`).

## 4. Rewriting the name in a match

### 4.1 Why not `SpeechQuery::SetInt` (0x1407B03E4)

* Callers [V]: `Trigger::Post` (every int parameter of every trigger), ~90 inline trigger builders in the speech
  module (all 92 `player_db_pID` references), `Preprocess` (`cm_sim` and every `*_gID`), `FilterNames`,
  `GetCallname`, Turbo's own build, and every other `SpeechQuery` user (crowd, SFX, bridge events). Rewriting
  `player_db_pID` there changes which player the context system thinks acted (storylines, crowd reactions), not only
  the spoken name.
* The CommentaryDb query the context system finally plays is built in data-driven code; nothing shows it uses
  `SetInt` (it can copy values). The opt-in speech log (§7 4c of `docs/callnames.md`) therefore counts trigger
  requests, not the final lines; its 10:18 file shows `requests 0` (no match was played with it on).
* `SetInt` cannot add a parameter (§1.1); a `surname_ID` remap there would miss `PLAYER_NAME` lines anyway, which carry
  no player id to key on.

### 4.2 The hook point: `Preprocess` 0x1414A90C8 [V]

`void Preprocess(IQueryPreprocessor* self, SpeechQuery* q)`; slot 0 of the interface at object+0x40 (vtable
0x1497BE0A0); the registry's pre-runner calls `(p+0x40)->vtable[0](q)` for every node (all 12,395 registry nodes have
one pre-handler). It runs:

* for every CommentaryDb line the game **plays** (dispatch 0x1407AFD88) and every **HasAudio** (0x1444F340C) -
  including the game's own `GetCallname` checks at match set-up and Turbo's audit;
* after the context system, before the bank selection; with the registry lock held (so calls are serialized and the
  detour must never call anything that takes it: no `HasAudio`, no event lookup, no post);
* on whatever thread dispatches: the match side is driven by `SpeechEventHandler` message handlers that read the
  snapshot ring (a consumer of the simulation, not the simulation thread); frontend asks come from the UI flow. Treat
  it as any thread [I]; log `GetCurrentThreadId()` in the observe mode below.
* Frequency [I]: one call per CommentaryDb line asked or played (a few per second in open play) plus ≤ 2 × 50 at
  set-up and Turbo's audit queries.

Reading a query without game calls (layout in the JSON): `ctx = [q+0x78]`, group `[ctx+0x40]`, event id `[ctx+0x44]`;
`n = [q+0x18]`, `pv = [q+0x20][i]`, `desc = [pv+0x30]`, hash `[desc+0x3C]`, type `[desc+0x40]` (1 = int), value
`[pv+0x00]`, set flag `[pv+0x44]`.

### 4.3 Design (one hook, crash-safe, kill switch)

Rule table, keyed by player id, published by the GUI as an immutable snapshot (`std::atomic<const Table*>`, old
snapshots kept alive): `B → A` ("speak like A": A's own recordings) or `B → 0` ("own recordings off": the database
callname is used). Optional persistence: `turbo_output\callnames\inmatch_remap.json`.

```cpp
// signature "speech_query_preprocess" (docs/re/inmatch-callnames.json); hook name "callname_voice"
using PreprocessFn = void (*)(void* self, void* q);
PreprocessFn g_preprocess = nullptr;
std::atomic<const VoiceTable*> g_voice{nullptr};       // never freed while installed
constexpr uint32_t kGroupCommentaryDb = 0x515CA0C5, kHashPlayerDbPid = 0x73D7AD2D;

void preprocess_detour(void* self, void* q) {
    g_preprocess(self, q);  // the game's cm_sim / *_gID first, from the real player id (keeps B's gender)
    HOOK_BODY("callname_voice", {
        const VoiceTable* t = g_voice.load(std::memory_order_acquire);
        if (!t || t->empty() || t_own_query > 0 || !voice_enabled()) break;   // break leaves the do-while
        auto* qb = static_cast<uint8_t*>(q);
        auto* ctx = *reinterpret_cast<uint8_t**>(qb + 0x78);
        if (!ctx || *reinterpret_cast<uint32_t*>(ctx + 0x40) != kGroupCommentaryDb) break;
        const uint32_t n = *reinterpret_cast<uint32_t*>(qb + 0x18);
        auto** pvs = *reinterpret_cast<uint8_t***>(qb + 0x20);
        for (uint32_t i = 0; pvs && i < n && i < 64; ++i) {
            uint8_t* pv = pvs[i];
            uint8_t* desc = pv ? *reinterpret_cast<uint8_t**>(pv + 0x30) : nullptr;
            if (!desc || *reinterpret_cast<uint32_t*>(desc + 0x3C) != kHashPlayerDbPid) continue;
            if (*reinterpret_cast<uint32_t*>(desc + 0x40) != 1 || desc[0x44] != 0) continue;  // int, no value list
            int* v = reinterpret_cast<int*>(pv);
            if (const int* to = t->find(*v)) { *v = *to; pv[0x44] = 1; ++g_voice_rewrites; }
        }
    });
}
```

* Install like the speech log (`install_game_hook("callname_voice", "speech_query_preprocess", ...)`), only when the
  signature resolves on the known build; `game_hook_enabled("callname_voice")` inside `voice_enabled()`.
* Kill switches: `turbo_output\callname_voice_off.txt` (feature, checked every 2 s like
  `call_commentary_audio_off.txt`), `turbo_output\hook_callname_voice_off.txt` (per hook, built in),
  `game_hooks_off.txt`, env `TURBO_GUI_NO_GAME_HOOKS=1`.
* No allocation, no lock, no game call inside; Turbo's own audit queries are never rewritten (`t_own_query`); a
  descriptor with a value list is left alone (counted, logged once).
* Effects:
  * (a) `B → A`: every CommentaryDb line with `player_db_pID = B` selects A's recordings, and at the next match
    set-up `GetCallname(B)` asks `HasAudio` with B → A rewritten, finds A's recordings and stores -1, so the surname
    lines do not say B's database name. Lines about A's real-life facts that are keyed on the player id come along
    (accepted risk). A must have recordings in the loaded language.
  * (b) `B → 0`: B's own-recording lines find no candidate; at the next set-up `GetCallname(B)` sees no recording and
    returns B's database callname: give B the wanted generic with Turbo's existing writes (last name / common name /
    `playernamemap`) and the surname lines say it. Lines that only existed as own recordings go silent for B [I].
  * Both take effect for the surname lines at the next match set-up (table built once); the `player_db_pID` lines
    change at once.
* `surname_ID` itself never needs rewriting: the database path feeds it.

### 4.4 Before turning rewrites on: observe mode

Same hook with an empty table and `turbo_output\callname_voice_log_on.txt`: log (thread id, event id → name through
the registry dump, `player_db_pID`, `player_db_gID`, `surname_ID`, `player_intensity`, `cm_sim`) for every
CommentaryDb line, aggregated every 5 s like the speech log. One match gives: which events name players, the real
intensities (for §3.1), the thread, and which event said "Gutierrez".

### 4.5 In-game checks (lead)

1. Observe mode, one half: the log names `PLAYER_*` events with `player_db_pID`; Gutierrez's line identified.
2. `B → A`: a Napoli starter without recordings, A = Gutierrez 261865: B is called "Gutierrez" (own-recording lines
   at once, surname lines from the next match).
3. `B → 0`: Gutierrez with Bianchi 900762 as his **common name** (*Assign as common name*: he has a common name, so
   a last-name write is ignored by `GetCallname`, §7.2): next match, "Bianchi".
4. Kill switch file present → no rewrite, log line once; delete → back on within 2 s.

## 5. Signatures (all unique in the image; `docs/re/inmatch-callnames.json`)

| name | resolves to | what |
|---|---|---|
| `speech_query_preprocess` | 0x1414A90C8 | the pre-handler (hook point) |
| `speech_str_pid_suffix`, `speech_gender_map_slot` | 0x14967581C, 0x14C6451E0 | `"_pID"`, the gender map shared_ptr (rip, one pattern) |
| `speech_team_gender_slot`, `speech_str_cm_sim` | 0x14C6450F0, 0x149675C88 | team pair shared_ptr, `"cm_sim"` |
| `commentary_get_callname` | 0x14294A0F4 | `GetCallname` |
| `speech_callname_table_store` | 0x14294A0F4 (call at 0x1438EBA79) | the table store `[handler+0x40]+0x58+slot*4` |
| `speech_callname_setup`, `speech_gender_map_build`, `speech_gender_insert` | 0x1438EB8B0, 0x1438EBCE0, 0x1438DDB58 | SpeechEventHandler slots 85 / 161, gender insert |
| `speech_event_handler_vtable` | 0x14A8D3810 | from the constructor 0x1438DAA80 |
| `trigger_set_int`, `trigger_post`, `trigger_post_set_int_call`, `trigger_pass` | 0x141A5B550, 0x141A5B6BC, 0x1407B03E4, 0x143906B1C | trigger side |
| `speech_registry_dispatch`, `speech_registry_has_audio`, `speech_run_preprocessors` | 0x1407AFD88, 0x1444F340C, 0x1424C2B18 | registry (the pre-runner has a byte-identical twin at 0x142501234: resolved from its call in HasAudio) |
| `speech_query_set_ctx`, `speech_query_find_param`, `speech_param_set_int`, `speech_param_get_int` | 0x1407B0F3C, 0x1407AEF2C, 0x1407AFA58, 0x1407B4344 | query internals |
| `speech_param_name_layout`, `speech_param_get_int_layout`, `speech_param_set_int_store` | 0x1414A95C3, 0x1414A9AE0, 0x1407AFAA7 | voice-swap layout guards (unique in the whole image): count +0x18, params +0x20, desc +0x30, name +0x20; single value desc +0x44, value +0x00, set flag +0x44 |

All `rip` entries use instruction forms Turbo's resolver supports (`E8 rel32`, `48 8D/8B [rip+disp32]`).

## 6. Open points

* Which CommentaryDb events the context system plays for a pass chain, and with which intensities: data, not code;
  §4.4 answers it in one match.
* `PLAYER_LOW_SIMPLE_LOC` as the Italian carrier of the own-recording "simple" line, and Gutierrez's family: §3.2
  answers it.
* The source of `surname_ID` in the delayed `trigger_SHOT_TAKEN` (0x141A59772) was not traced.
* Exact meaning of record `+0x4B4` / `+0x4B0` behind the gender id (players.gender is the obvious candidate).
* Whether the runtime `player_db_pID` descriptor carries a value list (§3.4).

## 7. Additions from a second static pass (same build, same image)

### 7.1 The bank side and FC 26's selection columns [V]

The ask path below the pre-handler, with signatures in the JSON (`commentary_bridge_has_audio`, `has_audio_impl_jmp`,
`speech_run_pre_handlers`, `speech_db_handler`, `speech_db_event_lookup`, `speech_db_candidates`,
`speech_candidate_check`, `speech_part_check`): `CommentaryBridge::HasAudio` 0x14294FCA0 (bridge vtable 0x14A8DC8C0
+ 0xB8) jumps to 0x1444F340C; the handler 0x1414A9D14 (vtable 0x14AD2F530 + 0x10) sends `(desc hash, value)` for every
**declared** parameter, set or not (an unset one goes with its default); 0x1414AAFCC takes the language db's
candidates; a candidate passes only when **every** part passes (0x145ACE484); the part check 0x145ACD02C keeps only
the selectors the query has (by hash) and asks the sound system's selection asset. So an event whose candidate has a
second part keyed by something the query leaves at its default can answer "no" for everyone.

`cm_sim` is the speech variable `IS_CAREER_MODE_SIM` == 1 (variable id at 0x14DB33E88, registered on first use by
`Preprocess`; store read 0x1414AAE74, default -1).

The user's FIFA Editor Tool exports of FC 26 PT-BR (`C:\FC_Tools\callnames_ptbr\dist\tables\*Selection.csv`,
`FCAudioMatrix\bck\pSIMPLE_SURNAME Selection.csv`) show the selector columns of the families behind these events (JSON
`fc26_selection_columns`; every row exists for `cm_sim` 0 and 1, so `cm_sim` never decides there):

| family | selectors | `player_intensity` values | PT-BR size |
|---|---|---|---|
| `pPLAYER_NAMES_SIMPLE` | cm_sim, player_db_pID, player_intensity | 2 (4 rows 1) | 3,509 players |
| `pPLAYER_NAMES_LINK` | cm_sim, player_db_pID, player_intensity | 2 | 900 players |
| `pPLAYER_NAMES_HIGH` | cm_sim, player_db_pID, player_intensity | 1 | 1,802 players |
| `pPLAYER_SPECULATIVE_SIMPLE` / `_LINK` / `_RETURN` | cm_sim, player_db_pID | - | 959 / 314 / 249 players |
| `pATTACK_SUPPORT_PLAYER`, `pDRIBBLE_PROGRESS_PLAYER_TOWARDS_GOAL` | cm_sim, player_db_pID, team_gID, opposite_team_gID (1..3) | - | 109 / 274 players |
| `pSIMPLE_SURNAME` | cm_sim, surname_ID, player_intensity | 2 | 3,406 rows |

That fits the trigger side: `trigger_PASS` sends `player_intensity` 2 (default, 0x1438EE629) or 1 when the pass is
rated high (0x1438EE6BC) - 2 = the "low" families (SIMPLE / LINK / SURNAME), 1 = HIGH. For the audit (§3) intensities 1
and 2 are the ones that matter. FC 26 had no `player_db_gID` selector in these tables; if FC 27 added one to
`pPLAYER_NAMES_SIMPLE` only, an ask without a match (gID unset, default) would miss SIMPLE while LINK still answers -
but the 13:39 in-match build missed it too, so the `_LOC` twin stays the first candidate [I].

### 7.2 Turbo's own callname rule differs from the game's [V]

`core/callnames.cpp resolve_callname()` falls back to the last name when the common name's commentary id is 900000 or
missing; `GetCallname` does not (`commonnameid != 0` → the common name's id, whatever it is; 0x14294A42E..0x14294A43B).
So for every player with a common name *Assign as last name* is a no-op in a match (Gutierrez: `commonnameid` 26180),
and the Callname tab can show a callname the commentary never uses. The tab should resolve exactly like the game and,
for a player with a `commonnameid`, write the chosen name as the common name (keeping the shown name through
`editedplayernames`) or tell the user. D-019 in `docs/callnames.md` said the same from the database side.

### 7.3 Read-only checks that settle §6 without a game call

All in a bound bank (a match), with the dev service's memory reads; ids are djb2-xor of the names (§1.1):

1. The registry node of `PLAYER_LOW_SIMPLE` (0x9D0D5E6C) and `PLAYER_LOW_SIMPLE_LOC` (0x0810BC33): `ctx+0x28`
   descriptors (default `+0x38`, value list `+0x18`, flags `+0x44` / `+0x45`) for `player_db_pID`, `player_intensity`,
   `player_db_gID`.
2. The language db's candidate map (`[owner+0x48] -> [y]`, map at `+0xD8` / `+0xE0`, keyed by the event id;
   `docs/callnames.md` 5.6): does each of the two ids have a node, and how many candidates / parts.
3. The same for `PLAYER_LOW_LINK` (0x0A3EC8A2) as the known-good reference.

### 7.4 About a `SetInt` hook instead of `Preprocess`

A pointer-keyed rewrite in `SpeechQuery::SetInt` (the 28 player-id parameter names are each one unique string in the
image, JSON `player_param_names`; the deferred `Trigger::Post` replays the same pointers) would also reach the triggers
and `GetCallname`'s step-1 queries, but it changes what the context system records about the player and does not reach
CommentaryDb lines whose values the context system copies without `SetInt`. `Preprocess` (§4.2) sees exactly the
CommentaryDb queries, after the decorator computed B's gender, so it is the better point; `SetInt` stays the
diagnostic log's hook.

### 7.5 What was added to the JSON

`strings` (18, checked by text), `vtables` (decorator interface, CommentaryDb handler, bridge HasAudio slot),
`callname_rule`, `layout_static`, `name_events_fc26_families`, `trigger_posts` (every `trigger_*` post with its
parameters and address, 119 with a player parameter), `setint_player_param_sites` / `setint_sites_total` (905 call
sites, 309 parameter names), `player_param_names` (28 names, string addresses), `fc26_selection_columns`, and 28 more
signatures (`getcallname_*`, `decorator_*`, the ask path of §7.1, `player_record_*`, `playernamemap_commentaryid`,
`trigger_pass_*`, `trigger_through_ball_in_progress` + the two group / channel strings, `trigger_player_fe`). The
verify script checks all 50 signatures, the hashes, the strings, the vtable slots and the uniqueness of the 28
parameter-name strings.
