# C3: live league tables (FCE standings) - verified notes and Turbo design

Re-verification of `docs/re/standings-fixtures-notes.md` against `C:\FC 27 Live Editor\turbo_output\fc27_image.bin`
(FC27.exe 1.0.140.64835, image base 0x140000000), done 2026-10-03 with `scripts/re/rx.py` + `verify_standings*.py`.
Static analysis only; the in-game check is in section 6. Offline Manager Career only.

## 1. Result of the re-verification

The image is the same build the notes were written for: **every address, offset, signature and vtable in the notes
checked out**. Corrections / additions:

| Item | Notes said | Verified |
|---|---|---|
| Service registry id 0x0A613B9A | "registry Get, unproven" | It is **`ENUM_djb2IFCEInterface_CLSS`** (Live Editor `services/enums.lua`), so `GetPlugin(0x0a613b9a)` = `FCE::FCEInterfaceImpl`. Turbo's Lua side already publishes it in `bridge_state.json` as `ifce`. No registry call or heap scan is needed. |
| Hub chain | `impl+0x18 hub`, `hub+0x18 DC`, `DC+0x80 DM` | Confirmed: `0x148a54b31` (`mov rax,[mgr+0x18]` hub, `mov r14,[rax+0x18]` DataConnector, `[r14+0x88]` = its comp structure) and `0x148a5bc77` (`[DC+0x80]` = DataManager, `[DM+0x88]` = StandingsDataList). Turbo's Lua export_fixtures uses the equivalent `impl+0x18 -> hub+0x10 (manager list) -> +0x08 begin -> [0] = DataManager`. |
| StandingsDataList | eastl::vector begin/end at +0/+8 | Confirmed by GetItem `0x148a2d1d4` (`(end-begin)/0x18` bound check, `begin + id*0x18`). The FC 26 `{+0x1C count, +0x28 begin}` layout in Turbo's Lua `export_fixtures.lua` is stale for FC 27 (that is why the Lua exporter "found the lists gone"). |
| FixtureDataList | `{i32 count; +8 data}` | Confirmed by `0x148a5bb93 -> 0x148a22168` (fixture by id) and the counter write code. |
| Incremental update `0x148a5bc04` | +9/+0xA/+0xB home, +0xE/+0xF/+0x10 away, goals +0xC/+0xD/+0x11/+0x12, points +0x14 | Confirmed instruction by instruction (`0x148a5bf02..0x148a5bfb0`): outcome 0 `inc [home+9]`, `[away+0x10]++`, home += setting 0x1F, away += 0x21; outcome 1 `[home+0xB]++`, `[away+0xE]++`; outcome 2 `[home+0xA]++`, `[away+0xF]++`, both += 0x20; goals as noted. |
| Fixture score write `0x148a5bb40` | +0x0F/+0x11 scores, +0x10/+0x12 pens, +0x13 = 1/2/3 | Confirmed. |
| Points settings | 0x1F win, 0x20 draw, 0x21 loss | Confirmed as the setting ids passed to `0x148a4e5bc`. The SettingsDataList entry layout stays **unverified [L]**; Turbo uses 3/1/0 by default and lets the user set win/draw/loss. |
| `FCE::DataSorter` string | present | Not found as a plain string in this image (irrelevant; the sorting behaviour is in code). |

Vtables (image-relative): `FCEInterfaceImpl` 0xB180DF0, `DataManager` 0xB180CA8, `ManagerHub` 0xB180C28,
`StandingsManager` 0xB1852E8 (slot 4 = request pump 0x148a53238, slot 5 = HandleMessage 0x148a4e8fc).

## 2. Data layout used by Turbo (all [H])

```
FCEInterfaceImpl (+0x00 vtable base+0xB180DF0)  +0x18 ManagerHub*
ManagerHub  +0x18 DataConnector*       DataConnector +0x80 DataManager*      DataManager +0x28 DataConnector* (back-pointer)
DataManager +0x60 FixtureDataList*  -> { i32 count; +0x08 FixtureData* data }          FixtureData 0x18 bytes, id == index
DataManager +0x88 StandingsDataList* -> { StandingData* begin; StandingData* end; ... }   StandingData 0x18 bytes, id == index
StandingData: +0 u16 id, +2 u16 compObjId, +4 u32 teamId, +8 u8 teamIndex, +9..+0xD u8 HW HD HL HGF HGA,
              +0xE..+0x12 u8 AW AD AL AGF AGA, +0x14 s16 points, +0x16 u8 used
FixtureData:  +0 u32 date, +4 u16 time, +6 u16 id, +8 u16 compObjId, +0xA s16 homeStandingId, +0xC s16 awayStandingId,
              +0xE u8 group, +0xF s8 homeScore, +0x10 s8 homePens, +0x11 s8 awayScore, +0x12 s8 awayPens,
              +0x13 u8 completion (0 unplayed / 1 FT / 2 AET / 3 pens), +0x14 u8 used
```
Played games, goal difference and the position are not stored; the Standings screen derives them and re-sorts on every
request (`0x148a54b00` -> `0x148a4a518`).

## 3. What Turbo now does (branch track/C3)

* `turbogui/src/core/fce_standings.h/.cpp` (pure, Memory-interface only):
  `locate(mem, ifce, image_base)` follows the chain with vtable checks (skipped when image_base is 0, i.e. in tests),
  requires `DM+0x28 == DC`, a 0x18-aligned standings vector (<= 65535 rows) and a fixture count 0..20000;
  `validate()` re-checks the chain before every use (new career = new FCE objects; the Lua side also re-publishes `ifce`
  on every db_gen bump). `read_rows/read_fixtures` bulk-copy the arrays. `write_row` re-reads the row, checks
  id/compObjId/teamId/used are unchanged and writes only bytes +0x09..+0x15 (counters + points). `apply_result`
  adds or removes one result from the two rows with 0..255 / s16 limits (nothing is changed on a limit hit).
  `edit_result` = remove old outcome, add new, write both rows, then the fixture's +0x0F/+0x11.
* `turbogui/src/ui/ui_standings.cpp`: the Competitions tab's first view "Live standings (game)": competition groups
  (compObjId) labelled with the league whose leagueteamlinks clubs make up >= 80 % of the group's rows
  (`leagues.leaguename`), the table sorted like the game (points, GD, GF), a per-club counter/points editor with
  "Points from W/D/L", and a result editor for the club's played fixtures. The old leagueteamlinks editor is the second
  view "Career database copy".
* `App::game_base` (set from `GetModuleHandleW(nullptr)` in `overlay_dx12.cpp`) feeds the vtable checks.
* Writes are plain `ProcessMemory` writes from the render thread. They are a few bytes inside objects the game only
  touches when a match result or the Standings request runs, so the UI tells the user not to edit during a match or
  Sim To Date. When the hook-foundation API (`game_hooks.h` dispatcher) is merged, `write_row`/`edit_result` can be
  queued onto the game thread with no change to the pure code (they take a `Memory&` and addresses).

## 4. Known limits

* Editing a result only updates FCE (fixture + rows): the career managers' copies (SimResults, news, player stats)
  are not touched. Unplayed fixtures are refused (completion 0).
* `leagueteamlinks` is a separate copy; the DB view edits it independently (sync by script functions is [L]).
* Persistence across save/load is **not verified statically** (`fce_standings` / `fce_fixtures` strings suggest the
  lists are serialised). See the in-game plan.
* Group naming needs the DB (leagueteamlinks + leagues); cups show "Competition group (n clubs, comp N)".

## 5. Signatures (`docs/re/C3-signatures.json`)

| name | signature (unique in the image) | use |
|---|---|---|
| fce_registry_get_site | `BA 9A 3B 61 0A 49 8B C8 FF 50 40` (0x147c15bab) | the registry's Get slot (+0x40) for an alternative locate path |
| fce_registry_global | `48 8B 0D ?? ?? ?? ?? BA 9A 3B 61 0A 48 8B 01 FF 50 (30\|38)` (2 hits, both -> base+0xC2A8590) | ServiceRegistry global |
| fce_standings_getitem | `45 33 C0 44 8B CA 85 D2 78 32 4C 8B 11 48 B8 AB AA AA AA AA AA AA 2A 48 8B 49 08` (0x148a2d1d4) | proves vector layout / 0x18 stride |
| fce_request_get_standings | `48 8B 41 18 4C 8B EA 8B 5A 20 4C 8B F9 8B D3 89 5C 24 30 4C 8B 70 18 49 8B 8E 88 00 00 00 E8` (0x148a54b31) | the +0x18/+0x18 hub chain |
| fce_datamanager_ctor_vtable | `48 8D 05 ?? ?? ?? ?? 48 89 07 4C 8D 05 ?? ?? ?? ?? 48 C7 47 28 00 00 00 00` (0x148a1664d) | rip target = DataManager vtable (re-derive 0xB180CA8 on another build) |
| fce_result_update_rows | `FE 43 09 FE C0 80 7C 24 20 01 88 47 10` (0x148a5bf2a) | the incremental update: counter offsets +9 / +0x10 |

## 6. In-game check (not done on this track; integration runs it)

1. Load a Manager Career with league matches played; open Turbo (F8) > Competitions > Live standings (game).
2. The user's league must be pre-selected and show the same W/D/L/GF/GA/points as the game's Standings screen.
3. Change the user's club points (+10), Apply; open the game's Standings screen: the points and position follow.
4. Change a played result (e.g. 2-1 -> 1-3); check Fixtures & Results and the table both show it.
5. Save, quit to the main menu, load: the edited values persist (or report that FCE rebuilds them from the save's
   leagueteamlinks, in which case the DB view must be edited too).
6. Advance a day / play a match afterwards: no crash, the next result adds on top of the edited row.
