# Game asset catalog (Turbo 2.1, schema v1)

The game asset catalog lists every asset the game ships, not only the ones a current database row points to: 3D heads
and gear of players who left the database, minifaces, youth and staff faces, crests, kits, balls and callname
recordings. Turbo's pickers use it to offer those ids, badge them "Hidden" and refuse ids the game does not ship.

This file is the contract between the offline builder (`turbo/tools/asset_catalog/`), the GUI core
(`turbogui/src/core/asset_catalog.h`), the pickers (`turbogui/src/ui/ui_asset_browser.h` and the pickers that use it)
and the Lua side (`lua_gate.txt`). The design and its evidence are in the local design note
`turbo_output\ASSET_CATALOG_DESIGN.md` (not in the repository).

## 1. Files

| File (under `<Live Editor>\turbo_output\`) | Written by | Content |
|---|---|---|
| `asset_catalog\catalog_<buildkey>.json` | builder | the catalog, schema v1 (section 2) |
| `asset_catalog\catalog_<buildkey>.summary.txt` | builder | counts per family, unnamed ids, timings |
| `asset_catalog\cache\<toc>.json` | builder | per-toc scan cache, keyed by size + mtime |
| `asset_catalog\banks\<lang>_bank.json` | `fc27_commentary.py --out` | commentary banks the builder reads (section 10) |
| `asset_catalog\lua_gate.txt` | GUI | head3d ids of the running build (section 5); exists only while the catalog is current |
| `asset_writes.log` | GUI | one line per write of a hidden id (section 6) |
| `asset_catalog_off.txt`, `asset_hidden_off.txt` | Cassio | kill switches (section 7) |
| `asset_gate_allow.txt` | Cassio (optional) | head ids vouched for (mods), honoured by the Lua gate (section 5) |

The catalog holds real names (EBX slugs, master names). It is **never committed and never shipped**: `turbo_output` is
not packaged, and repository tests use the synthetic fixture `turbogui/tests/fixtures/asset_catalog_v1_small.json`
("Test Head 90001", `test_club_one`).

**Build key**: FC27.exe PE `TimeDateStamp-SizeOfImage` in upper-case hex, e.g. `6AC07E31-2145C000` (the key Turbo's
signature scan uses). At start-up the GUI loads `catalog_<running key>.json`; if there is none it loads the newest
`catalog_*.json` as **stale** (section 4).

Rebuild, in a Turbo source checkout with Python 3: `py -3 -I turbo\tools\asset_catalog\build_asset_catalog.py`
(read-only on the game, Live Editor and saves; writes only under `turbo_output\asset_catalog\`; it also builds missing
or outdated commentary banks). **Release builds ship no builder**: release users cannot build a catalog until the
builder inside Turbo (WP-14) exists, and without one every picker works exactly as in 2.0. The GUI's status line and
Copy rebuild command say so.

## 2. Schema v1

```json
{
  "schema": "turbo-asset-catalog/1",
  "build_key": "6AC07E31-2145C000",
  "game_version": "1.0.141.12554",
  "built_at": "2026-10-12T10:00:00-04:00",
  "builder": "build_asset_catalog.py 1",
  "sources": { "Data/Win32/contentsb.toc": {"size": 23025479, "mtime": 0}, "commentary/eng_us": {"size": 0, "mtime": 0} },
  "families": {
    "head3d":     { "ids": [41, 250], "vars": {"250": [0, 1]}, "toc": {"41": "L", "250": "LS"},
                    "slug": {"41": "test_slug_name", "250": "test_player"}, "generic": [26642] },
    "miniface":   { "ids": [250, 488] },
    "youthface":  { "ids": [] }, "staffface": { "ids": [] }, "legendface": { "ids": [] },
    "hair":       { "model": [1, 2], "preview": { "1": "data/ui/imgAssets/craniumhair/hair_1_0.dds" } },
    "facialhair": { "model": [], "preview": {} }, "boots": { "model": [], "preview": {} },
    "gkglove":    { "model": [], "preview": {} }, "tattoo": { "model": [], "preview": {} },
    "accessory":  { "model": [], "preview": {} }, "eyebrow": { "ids": [] }, "skinsurface": { "ids": [] },
    "crest":      { "ids": [], "paths": {} }, "kit": { "teams": {"2": {"slug": "test_club", "kits": ["home_40_0"]}} },
    "ball":       { "model": [], "preview": {} },
    "callnames":  { "eng_us": { "teams": { "dr": { "generic": [], "simple": [], "link": [], "high": [] }, "gm": {} } },
                    "ita_it": { "teams": { "": { "generic": [], "simple": [], "link": [], "high": [] } } } }
  },
  "names": {
    "player":     { "250": ["Test Player", "master:eng_us"], "41": ["Test Slug Name", "slug"] },
    "team":       { "2": ["Test Club", "kit_slug"] },
    "commentary": { "999931": ["Testname", "master:ita_it"] }
  }
}
```

Rules (checked by `turbogui/tests/catalog/test_asset_catalog.cpp`, `validate_v1`):

- `schema` is exactly `turbo-asset-catalog/1`. Any other value is refused (the GUI shows "error"). A new incompatible
  layout gets a new number; adding optional keys does not.
- `build_key`, `game_version`, `built_at` (ISO 8601 with offset), `builder` are strings. `sources` maps each scanned
  input (tocs, commentary packs, Live Editor csv files) to `{size, mtime}` integers.
- All 17 family keys are present (empty lists when the builder found nothing). Unknown family keys are refused.
- Id lists are JSON integers, **sorted ascending, no duplicates**. Object keys that are ids are decimal strings.
- Preview and crest paths are game paths exactly as listed in the manifests (letter case kept): `data/...`,
  `[A-Za-z0-9_/.-]` only, no `..`, at most 200 bytes (the rule of `legacy_path::valid`).
- `head3d.vars` / `head3d.toc` / `head3d.slug` keys and `head3d.generic` ids are head ids. `toc` letters: `L` = launch
  bundle, `S` = contentsb, combined when the head is in both (e.g. `LS`).
- `head3d.slug` (optional, builder 2, 2026-10-17): the head's EBX folder slug (`doe_john` of
  `content/character/player/player_#/doe_john_<id>/`), one per head. The managers' chooser matches manager names against
  it ("name match": a manager's own head shipped under a player or legend id). A builder-1 catalog has none; nothing
  else changes, so the schema stays `turbo-asset-catalog/1`.
- `callnames.<lang>.teams.<team>`: `<team>` is the commentary team (`dr`, `gm`, ...) or `""` for single-team packs; its
  keys are among `generic`, `simple`, `link`, `high` (missing key = empty).
- `names.<table>.<id>` = `[name, source]`, name not empty, source one of `master:<lang>`, `workbook`, `extra_names`,
  `slug`, `kit_slug`, `managers`, `kind`, `loc_strings`, `le_team_names`, `turbo_team_names` (section 3).

### Families

| Key | Ids | Proof | DB field(s) it feeds |
|---|---|---|---|
| `head3d` | headassetid | 3D model (face texture + morph preset + starhead bundle); optional `unproven`: heads missing a part (not writable) | `headassetid` with `headclasscode` 0 |
| `miniface` | playerid / headassetid | legacy file `data/ui/imgAssets/heads/p<id>.dds` | picture copies |
| `youthface` | headassetid | `data/ui/imgAssets/youthheads/p<id>.dds` | picture copies |
| `staffface` | managerid (the game's key; also Turbo's older copies under the head id) | `data/ui/imgAssets/heads_staff/heads_staff_<id>.dds` | picture copies |
| `legendface` | id | legend pictures | picture copies |
| `hair` | hairtypecode | `model` (EBX) and/or `preview` | `hairtypecode` |
| `facialhair` | facialhairtypecode | model / preview | `facialhairtypecode` |
| `boots` | shoetypecode | `model` = the shoe ids under `content/` (the gate); optional `model_ts_only`: ids only under `content_ts/` (proof bit `kProofModelTs`: listed with a "Not checked" badge, refused with their own reason until one is checked in game) | `shoetypecode`, `smallsidedshoetypecode` |
| `gkglove` | gkglovetypecode | model / preview | `gkglovetypecode` |
| `tattoo` | tattoo id | model / preview | `tattoo*` (7 fields) |
| `accessory` | accessory code | model / preview; optional `desc`: folder description per id (the GUI's names). `model` includes NPC, referee and outfit items, so it is **not** a write gate: the galleries offer players only the player set (6-17, 22-27) | `accessorycode1..4` (no gate) |
| `eyebrow` | eyebrowcode | cranium preset | `eyebrowcode` |
| `skinsurface` | skinsurfacepack | cranium preset | `skinsurfacepack` |
| `crest` | teamid | `paths`: crest file per team. Create-a-club templates (only `crest/custom/l<id>.dds`, 99010101..99171212) are left out by the builder and dropped by the loader of an older catalog | picture copies |
| `kit` | teamid | `teams.<id>`: kit folder slug + kit folders | none yet (crest browser names) |
| `ball` | ball id | model / preview | none yet |
| `callnames` | playerid (own) / commentaryid (generic) | bank recording per language and team | voice sources, kick-off generic ids |

## 3. Names

The builder keeps the **first** source that names an id; the GUI puts the live DB name in front of all of them.

| Priority | Source tag | Source |
|---|---|---|
| 0 | (none: runtime) | loaded DB: player names, teams, commentarynames |
| 1 | `master:<lang>` | callname masters `LE\turbo\callnames\masters\<lang>.json` |
| 2 | `workbook`, `extra_names` | FC 26 workbooks' `names` sheet; `turbo_dev\masters\raw\extra_player_names.json` |
| 3 | `slug` | EBX head folder slug (`doe_john` -> "Doe John") |
| 4 | `managers` | `turbo_dev\managers.csv` |
| 5 | (unnamed) | fallback "Head #id", "Youth face #id", "Callname #id" (`assets::fallback_name`) |

Teams (crests and kits), in order: `kind` (an id that is not a club, from its kit slug: default, generic, goalkeeper,
referee, walk-out: "Referee kit (referee_..., not a club)"); `loc_strings` (the game's `TeamName_<id>` strings from a
language-strings dump: `--team-names-loc`, default `<out>\names\LanguageStrings*.txt` and
`C:\FC_Tools\My Mods\loc\LanguageStrings*.txt`); `kit_slug`; then the user's renames `le_team_names` (Live Editor's
`custom_team_names.csv`) and `turbo_team_names` (`turbo_output\team_names.json`). On this PC (10-11) 1,194 of the 1,730
club crests have a name (853 game strings, 338 kit slugs, 3 kinds); the other 536 show "Team #id" and are found by id.
The dump on this PC is FC 26's: clubs new in FC 27 come from the kit slugs only.

Tooltips say where a name came from (`assets::name_source_label`: "name from: ita_it master", "name from: game file
name", ...).

## 4. Hidden, computed live

The catalog never stores "hidden": it depends on the loaded save. The GUI rebuilds `assets::DbUse` when
`model.version()` changes. An id is **Hidden** when the catalog ships it and no current row has it in:

| Family | Fields |
|---|---|
| head3d | `players.headassetid` (any class) and `manager.headassetid` |
| miniface | `players.playerid` and `players.headassetid` |
| staffface | `manager.managerid` and `manager.headassetid` |
| youthface | `players.headassetid` |
| hair / facialhair / boots / gkglove / eyebrow / skinsurface | the matching `players.*code` field |
| tattoo | all 7 `players.tattoo*` fields; the badge adds "not in the tattoo table" when the DB `tattoo` table lacks the id |
| crest / kit | `teams.teamid` / `teamkits.teamtechid` |
| callnames own / generic | `players.playerid` / `commentarynames.commentaryid` (+ `playernamemap`) |

UI: badge **"Hidden"** (tooltip "In the game files; no player of this save uses it"; "team" for crests and kits), and the
filter combo **All / In this save / Hidden** (`asset_scope_combo`, `assets::Scope`), remembered per picker in
gui_settings by the GUI. Other badges: "3D only" (model, no preview: placeholder tile), "Preview only" (greyed, not
writable), "Not in game", "No recording", "Not in tattoo table".

**Stale catalog** (build key differs from the running game): lists stay, with a "built for <key>" banner and the
rebuild command; Apply on hidden ids is disabled with the reason; DB-linked ids keep working (the DB proves them);
`lua_gate.txt` is deleted.

## 5. Write gates

An id is offered for writing only when a **current** catalog proves the game ships it (`Catalog::writable`).

| Field | Proof required | When the catalog is off or stale |
|---|---|---|
| `headassetid` with `headclasscode` 0 | head3d id (face texture + morph preset + starhead bundle) | DB-owned heads only |
| `hairtypecode`, `facialhairtypecode`, `shoetypecode`, `gkglovetypecode`, `tattoo*` | 3D model id (`model`); 0 = none is always allowed | today's gallery |
| `eyebrowcode`, `skinsurfacepack` | listed cranium preset | values in use |
| `accessorycode1..4` | none yet (mapping unknown) | today's gallery |
| minifaces, crests (picture copies) | source path in the manifests, or already exported to the cache | `LegacyFileExist` through the pump |
| voice swap source / kick-off generic id | recording in the bank of the active language (and team) | master list |

A preview picture alone never proves a 3D-backed field. The generic field grid (raw `headassetid` etc.) stays an
expert tool: it shows a red "not in the game files (catalog <key>)" note but does not block.

Hidden head write: `headassetid=id, headclasscode=0, hashighqualityhead=1, headvariation=<first shipped var>`;
`headtypecode` is kept.

**Lua gate** (`asset_catalog\lua_gate.txt`), written by the GUI only while the catalog is current and not switched off,
deleted otherwise:

```
#turbo-asset-gate 1
#build 6AC07E31-2145C000
#session <the "session" the GUI writes into bridge_dll.json>
head3d 41
head3d 250
```

Optional lines: `#count <n>` (number of head3d lines; a mismatch = file cut short), other `<family> <id>` lines
(`miniface`, `callname*`: ids a new player must not take). Any malformed line, another header or no head3d line makes
the gate "bad".

Lua (`core/asset_gate.lua`) reads it on each gated feature run (a few ms). It trusts the gate ("current") only when
`asset_catalog_off.txt` is absent and `#session` equals the session of a **fresh** `bridge_dll.json` (at most 15 s old;
the GUI writes its own session into the gate, `Catalog::sync_lua_gate(out, err, session)`):
a gate left by an earlier game session, or written before a title update while the GUI was not running, is never
trusted. Otherwise (none / bad / stale / off) the features log "unchecked" and apply today's rules:

- `custom_headassets`: with a current gate, an id not in the gate (nor in `asset_gate_allow.txt`) is refused before
  anything is written; with `asset_hidden_off.txt`, so is an id no row of the save uses. Unchecked: range check only.
- `create_player` (CMTracker real face): with a current gate, a real face iff the gate (or the allow file) has the id.
  Unchecked: a real face only when the DB row of that player has `headclasscode` 0 and `headassetid` == his playerid
  (before 2.1 any DB player counted, which gave class 0 to generic heads). With a current gate the free id skips ids
  listed in the gate, and a typed id listed there is refused unless it is the player's own real face id or the GUI's
  Player ID box chose the id for that head (`id_assets.head3d`, below).
- `create_player` with a typed id (2.3, Players > Create player > **Player ID**, `core/player_id.h`): the GUI sends
  `"playerid"` and, when its current catalog knows a 3D head or an own callname for the id, `"id_assets": {head3d,
  headvariation, callname, miniface}`. With `head3d` Lua gives the head only when the gate is current and lists the id
  (`check_head`, strict; `asset_hidden_off.txt` refuses a hidden one): `headassetid` = the id, `headclasscode` 0,
  `hashighqualityhead` 1, `headvariation`; a hidden head is logged (old value `(new player)`). Without a trusted gate
  the create is refused before anything is written. The callname needs no write: the commentary finds an own recording
  by player id.
- `turbo_output\asset_gate_allow.txt` (Cassio's, optional): ids vouched for (heads added by mods), one `<id>` or
  `head3d <id>` per line, `#` comments. Ignored when the catalog is switched off.
- `asset_writes.log` (section 6), key = the gate's `#build` or `unchecked`: `custom_headassets` logs every field it
  changes; `create_player` logs a real face no row of the save used (old value `(new player)`, logged before the insert).

## 6. Write log

Every write of a hidden id appends one line to `turbo_output\asset_writes.log` (`assets::append_write_log`), tab
separated, so any change can be reverted by hand:

```
<local time ISO 8601>	<target>	<field>	<old>	<new>	<catalog build key>
2026-10-12T10:03:11-04:00	player 12345	headassetid	12345	41	6AC07E31-2145C000
```

## 7. Kill switches

Files in `turbo_output\` (same convention as `reapply_off.txt`); the file existing switches the feature off:

| File | Effect |
|---|---|
| `asset_catalog_off.txt` | catalog not loaded; every picker behaves exactly as in 2.0.x; `lua_gate.txt` removed |
| `asset_hidden_off.txt` | catalog loaded (galleries cleaned, gates on) but hidden ids are neither listed nor offered |

Both are listed in Tools > Status with their state.

## 8. Tests

- `bash turbogui/tests/catalog/run.sh` (g++ only, a few seconds): the fixture validates against schema v1, the
  validator refuses broken files, and `core/asset_catalog`'s names, paths, switches and scope filter. Extra arguments are
  catalog files to validate too (a real catalog can be checked locally; it is never committed).
- The same kind of runner per picker, each a few seconds to 15 s: `run_game_heads.sh` (core/game_heads.h + the 5-choice
  face filter), `run_gear.sh` (core/gear_ids.h), `run_pages.sh` (core/asset_pages.h), `run_callnames.sh`
  (core/callname_bank.h, core/callnames_catalog.h).
- Python (synthetic bytes only, under 2 s): `py -3 -I turbo\tools\asset_catalog\test_asset_catalog_builder.py`,
  `-I turbo\tools\fc27_commentary\test_fc27_commentary.py`, and (without `-I`: openpyxl is in the user site-packages)
  `turbo\tools\test_callname_master_teams.py`.
- Native: `TURBO_TEST_ONLY="asset catalog wiring"` (tests/native/test_asset_app.h): the app hands a loaded catalog to the
  real-face chooser, the gear galleries, callnames and the browsers, writes `lua_gate.txt` with its session, hands a
  catalog rebuilt under the same build key to every picker again, and takes it all back when the catalog goes away. `TURBO_TEST_ONLY="gear pictures"`: a catalog that is not loaded keeps the 2.0
  galleries.
- Lua: `lua5.4 turbo/tests/t26_asset_gate.lua` (under 1 s); the typed id of `create_player` (range, per-player tables,
  squad status table, kill switch, the head of the id): `lua5.4 turbo/tests/t27_player_id.lua`.
- Native: `TURBO_TEST_ONLY="player id"` (tests/native/test_player_id.h): the Player ID verdicts, what the fixture ships
  for an id (current / stale / hidden off), the hidden player list, the scan of the GUI world, and the dialog (typing,
  the picker, the command, Turbo's Lua side running it).
- Native GUI cases: `TURBO_TEST_ONLY=<text> bash turbogui/tests/native/run_native.sh` runs only the cases whose name
  contains the text (any letter case), e.g. `TURBO_TEST_ONLY="gear pictures"`. The runner rebuilds only the objects
  whose source or included headers changed (g++ `-MMD` dependency files). Measured on this PC (MSYS2 UCRT64): cold build
  about 5.5 min; nothing changed about 10 s; a changed `src` header about 12 s plus its includers; a changed test header
  about 70 s (`tests/native/test_main.cpp` is recompiled, at `-O0`).
- A new worktree can skip the cold build: right after `git worktree add` (same sources as the cache), copy a warm
  cache with `cp -r <warm worktree>/turbogui/build/native_bin <new worktree>/turbogui/build/` (plain `cp -r`, so the
  copies are newer than the checkout). Copy `turbo/le27/libs` the same way (Live Editor's own Lua libs, not in git).
- No test reads the game folder. A full catalog build and in-game checks are acceptance steps, timed and reported.

## 9. GUI side (core and app wiring)

- **Loading** (`App`, ui/app.cpp): the first tick starts `Catalog::load_dir(turbo_output, running key)` on a worker
  thread; the result is swapped in on the render thread (`App::asset_catalog()`, never null) and the pickers rebuild
  (`gen`). The running key is the host's (`hook_report().build`), else the PE header of `<game>\FC27.exe`
  (`assets::exe_build_key`); unknown = stale. Every 5 s the GUI compares `assets::dir_signature` (catalog file names,
  sizes, times and both switch files) and reloads on a change, so a rebuilt catalog or a switch file takes effect
  without a restart. `lua_gate.txt` is synced by the same worker after each load.
- **Load rules**: the running build's file is loaded as current; a broken one is an **error** (no fallback to an older
  file). Without it the newest `catalog_*.json` is loaded as stale. Unknown family keys are ignored (noted in the
  status), unsorted id lists are sorted. Measured: a 68,700-id + 10,000-name file loads in about 140 ms at `-O0`.
- **What the save uses**: `App::asset_use()` builds `assets::DbUse` lazily, once per `model.version()`: players
  (`playerid`, `headassetid`, hair, facial hair, `shoetypecode` + `smallsidedshoetypecode`, GK gloves, eyebrows, skin
  surface, the 7 tattoo fields, `accessorycode1..4`), `manager.headassetid`, `teams.teamid`, `teamkits.teamtechid`,
  `commentarynames` + `playernamemap` commentary ids, and the `tattoo` table.
- **Pickers**: `hidden()` for the badge; `offered()` to decide whether a row is listed at all (DB ids always; catalog
  ids unless `asset_hidden_off.txt`); `writable(f, id, &why)` before Apply (show `why` when refused);
  `App::log_asset_write(target, field, old, new)` after a write of a hidden id. The filter is remembered with
  `App::asset_scope(picker)` / `App::set_asset_scope(picker, scope)` in gui_settings.json (`asset_scope.<picker>` =
  `all` / `in_save` / `hidden`).
- **Tools > Status**: one row under Health (current with key, date and counts / stale with the rebuild command / off /
  not loaded), the switch and gate files, and Reload, Open folder and Copy rebuild command buttons.

## 10. Building the catalog (offline, read-only on the game)

```
py -3 -I turbo\tools\asset_catalog\build_asset_catalog.py
```

Run it in a Turbo source checkout (any Python 3; numpy, as in `re_venv`, makes the first legacy census fast). Release
builds have no builder yet (WP-14).

Defaults: game `C:\Program Files\EA Games\EA SPORTS FC 27`, Live Editor `C:\FC 27 Live Editor`, output
`<LE>\turbo_output\asset_catalog`. Options: `--game`, `--le`, `--out`, `--bank <file>` (repeatable; replaces the
discovered banks and skips the bank build), `--no-bank-build`, `--masters`, `--workbook-names`, `--extra-names`,
`--managers`, `--team-names-loc <file>` (repeatable). It reads the game, Live Editor and saves and writes only under the
output folder.

- **Steps**: build key from the FC27.exe PE header; legacy census (CFC manifests, FNV-1 of the candidate names from both
  Live Editor csv files plus generated patterns); EBX census (bundle manifests of every Data and Patch toc); commentary
  banks; names (section 3); `catalog_<key>.json` + `catalog_<key>.summary.txt`.
- **Time on this PC (build 6AC07E31-2145C000, game 1.0.141.12554)**: first run 9 to 14 s; unchanged rerun under 2 s
  (the per-toc cache). After a title update only the changed tocs are scanned again.
- **Banks**: since the review fixes the builder **builds them itself**: for every installed language (a
  `commentaryfull_` / `commentarylaunch_<lang>.toc` in Data, Patch or `<game>\commentary`) it runs `fc27_commentary`
  into `banks\<lang>_bank.json` when the file is missing, has another format or game build, or one of the game files it
  was read from changed size or time; the summary has one "Bank <lang>:" line (up to date / built / NOT built). Then it
  reads `banks\*.json`, and `turbo_output\commentary_banks\*.json` and the two research files only for languages the
  built banks lack. By hand: `py -3 -I turbo\tools\fc27_commentary\fc27_commentary.py --lang eng_us --out <LE>\turbo_output\asset_catalog\banks\eng_us_bank.json`.
  Since 2.1 it finds the base-game packs (Patch, then Data, then the downloaded `<game>\commentary` copy), reads every
  `<lang>[_<team>]_<full|demo|licensed>` set and records per set; a family it cannot read is an error of that set, not of
  the run. The builder merges a team's sets (eng_us: Derek Rae `dr` and Guy Mowbray `gm`; ita_it: one team, `""`).
- **Boots**: `model` holds the 862 shoe ids under `content/`. The 120 ids that exist only under `content_ts/` (2200 and
  up, used by no DB row; 82 with a preview, 38 without) are `model_ts_only`: listed with a "Not checked" badge (a "3D,
  content_ts" tile when there is no preview), refused with "its 3D model is only under content_ts".
- **Crests**: 1,730 club crests. The 3,204 create-a-club templates (`crest/custom/l99010101..l99171212.dds`, no kit, no
  name) are left out; the summary counts them.
- **Not in schema v1** (counted in the summary only): the 216 outfield glove models (probably variants of accessory 16,
  unverified).
- **Callname masters** (FC 27, team-aware): `turbo\tools\build_callname_master.py` with a new-format bank lists the union
  of one team's sets (`--kinds full,demo,licensed` by default; `--team` other than the primary set's is refused) and
  names every player with his own recording. The eng_us FC 27 master (7,264 dr / 7,453 both teams, 3 unnamed) is built
  locally; deploying it to `<LE>\turbo\callnames\masters` is Cassio's decision.

## 11. What each picker offers

Without a loaded catalog (none for this build and no older one, `asset_catalog_off.txt`, a refused file) every picker is
exactly 2.0's. The All / In this save / Hidden filter is remembered per picker in gui_settings.json.

| Picker | With a catalog | Writes |
|---|---|---|
| Players / Managers > Appearance > Choose a real face | The save's heads, then **game heads**: every shipped 3D head no player or manager row uses (generic heads left out), named DB > master > workbook > game file name (a file name always shows the id), badged Hidden. Tiles show the 3D render cached while the head's player was still in a save (2.3: that render's 3D look then filters it too), else the game miniface when the catalog lists one, else the game's legend picture, else a 3D render the game makes for it on demand (5.0c E7: asked through the manager route, head id with manager = true, cached as `m_<headid>.dds`; it shows a spinner meanwhile; whether the game renders a head id no row uses is **not yet proven in game**, see the Tester probe P-E7; after 12 failed renders with no success Turbo stops asking for the session and the tile says so). A head with no picture and no render shows its initials and the reason, never the words "no picture". Badges (Hidden, name match, 3D pending, HQ) sit on backed chips that never overlap. Every menu offers at most 5 choices (colours, eyes and the 3D looks are grouped; the tooltip keeps the exact value) | A click asks first (what is written, gender unknown, not checked in a match). Gate: current catalog, a 3D head, not generic; stale = refused with the rebuild command. Writes `headassetid`, `headclasscode` 0, `hashighqualityhead` 1, the first shipped `headvariation`; `headtypecode` kept; optional copy of the game miniface `p<head>` to the player's. Every field logged |
| Gear galleries: hair, facial hair, boots, small-sided boots (new row), GK gloves | 3D models + previews confirmed in the manifests, with their exact paths. Model-only ids: "3D only" placeholder tile. Preview-only ids: greyed, "Preview only", refused. content_ts-only boots: greyed, "Not checked", refused. Hash-list ids the game does not ship are gone | Gate: 0 and ids the save uses always; others need a current catalog and a `content/` 3D model. Hidden writes logged |
| Gear code combos (widgets) | `[Hidden]`, `[preview only]`, `[content_ts model, not checked]`, `[not in the game files]` marks; refused ids are not offered | same gate |
| Tattoos (grid and combo) | The save's tattoo table plus every tattoo model the game ships; ids outside the table are listed greyed ("Hidden, no table row") | Gate: the save's tattoo table (or a tattoo the save uses). **Tattoos outside the table are refused** until one is checked in a match (section 14 item 6) |
| Accessories 1-4 | The players' codes only (6/7 wrist athletic tape ... 26/27 wrist rubber bracelet; the odd code of a pair is named after the even one, "unverified"), named from the game folders (tooltip "In the game files: ..."), Hidden badge, All / In this save / Hidden filter; referee (2, 4, 20, 32, 34), card (28, 30), NPC and outfit items (36 and up, 1001+) are not offered | none new (no write gate, as in 2.0); a code the save already uses stays listed |
| Managers' outfits | Unchanged (hash list) | none new |
| Socks and sleeves (new rows) | `sockstylecode`, `socklengthcode`, `jerseysleevelengthcode` as named lists | normal field writes |
| Miniface editor (players / managers) | Tabs **Game minifaces**, **Youth faces** (only the picture is copied: no 3D youth heads exist), **Legend pictures**, and **Staff faces** for managers: a paged browser (200 tiles a page, only tiles on screen ask for pictures), search by name or id, Hidden badge | The picture becomes the new miniface's source (existing writer, backups kept) |
| Crest editor | Tab **Crests in the game files** (default filter Hidden): the 1,730 club crests, named DB > the game's team strings > kit folder slug (with the id) > "Team #id"; ids that are not clubs say so | Copy the files to this club or use as a picture (existing writer) |
| Callnames > All callnames | Every recording of the bank for the language and team (players who left the database: Hidden), plus master-list ids the bank lacks ("No recording"); players with only a high-intensity recording are listed as "high-intensity recording only"; a count line bank / master list / save; names DB > catalog > master > "Callname #id" | A voice source needs a SIMPLE or LINK recording (HIGH-only refused: the kick-off name check never asks the HIGH set); a kick-off generic id a generic recording; in the bank of the language and team; a stale catalog refuses hidden sources |
| Callnames: commentary team (eng_us) | **Commentary team** line in the callnames header (and the combo in All callnames): Automatic = the team whose SIMPLE / LINK recordings the live capture (bank capture / audio service cache) matches, else dr (Derek Rae, checked in game); Every team; dr; gm. Remembered in gui_settings.json `callnames.bank_team` | It decides the spoken set and the voice-source gate of every tab |
| Callnames: spoken set | A **current** catalog's bank (of that team) is the spoken set (it beats `spoken_<lang>.txt` and the captures); only SIMPLE / LINK recordings make a player "spoken by his own recording" (the 2.0 rule); a stale one only adds rows to All callnames | n/a |
| Lua: `custom_headassets`, `create_player` | `lua_gate.txt` (section 5) | refused / real face as in section 5 |
| Players > Create player > Player ID (2.3) | Typed id: what the game files hold for it ("This id has a 3D head and a callname in the game files: the new player will use them", miniface, gender unknown, callname in another language, a head another row wears); generic heads and, with `asset_hidden_off.txt`, hidden heads refused; stale catalog: "not checked". **Use a hidden player's id**: ids with a hidden 3D head or an own recording in the loaded language that no per-player table of the save has, search by name (catalog names, local only) or id, filter 3D head / callname / both | The id (and the head of the id, section 5) through Lua's `create_player`; kill switch `create_player_typed_id_off.txt` |

## 12. Not covered (known, out of scope for 2.1)

- **Generated heads** (`headclasscode` 1, `headtypecode`): no proof source in EBX names; `headtypecode` stays ungated.
- **Colour and preset fields** (eye, hair, facial hair colours, skin tone, lip colour, eye detail, complexion, make-up, skin
  type): small closed lists with named presets, no id proof. Unchanged.
- **Eyebrows and skin surface**: in the catalog (`eyebrow`, `skinsurface`) and in DbUse, but their code fields keep the
  values-in-use list (no picker change, no gate in the UI yet).
- **Accessories**: the DB uses codes 0, 6-9, 16, 22-27; the even codes have folders (6 wrist athletic tape, 8 wrist
  sweatband, 10 knee athletic tape, 12 knee brace, 14 ankle tapes, 16 player glove, 22 finger tape, 24 wrist long tape,
  26 wrist rubber bracelet), the odd ones are probably the other side (unverified). The galleries offer players only
  these codes; there is no write gate (as in 2.0) until one in-game check of an odd code.
- **Kits and balls**: in the catalog, no picker (kits need `teamkits` rows; ball tables are not exported). Kit slugs name
  crests only; the 102 referee, goalkeeper and generic kit ids get no team name.
- **Stadiums, sleeve badges, jersey fonts, kit numbers, armbands, flags, league logos, trophies**: not mapped.
- **Referees**: only referee kits exist, no referee heads.
- **Women's heads**: EBX head names carry no gender. The chooser warns ("gender unknown") and asks before writing; the
  catalog stores no per-head gender yet, so **a cross-gender head is not refused**: only the warning stands in front of
  it (critic S1's refusal is not done).
- **Callname selectors** other than the name families (for example `pgoal_scored_player`) are not catalogued; the voice
  gate checks the name families only.

## 13. Never in git, never in the zip

The catalog, its summary, the cache, the banks, `lua_gate.txt`, `asset_writes.log`, `asset_gate_allow.txt`, the callname
masters and the name sources (`names_sheets.json`, `extra_player_names.json`) hold real names or are per-PC state. They
live in `turbo_output` (never packaged) and are listed in `.gitignore`. `scripts/package.sh` refuses to build the zip
when git tracks such a file or the staged package holds one (by name, or any file containing `turbo-asset-catalog/`).
`bash scripts/package.sh --guard` runs only that check (under 1 s). Repository tests use the synthetic fixture.

## 14. Checks still to do in game (throwaway save slot, one player, revert)

1. A hidden 3D head on a player, in a match: one launch-only head and one contentsb-only head; the 3D capture after Apply
   and one kick-off.
2. A model-only boot and a hidden hair on one player, visible in a match.
3. A new voice swap made through All callnames with a hidden source: the voice log line and the kick-off line.
4. The Lua gate: with the GUI running, `custom_headassets` on an id not in the gate is refused and the log says "checked";
   with the GUI closed it says "unchecked".
5. Optional: a game-heads tile for a manager (does the manager capture render a hidden head without a write?).
6. Later, each before its gate opens: a `content_ts`-only boot (refused today), a tattoo outside the save's tattoo table
   in a match (refused today), an odd accessory code (offered today without a gate, as in 2.0).
7. A women's head on a men's player: **no gate closes this today** (only the "gender unknown" warning); check it before
   relying on game heads for men's players, and until then treat every game head as possibly the wrong gender.
