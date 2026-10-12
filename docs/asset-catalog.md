# Game asset catalog (contract, schema v1)

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
| `asset_catalog\lua_gate.txt` | GUI | head3d ids of the running build (section 5); exists only while the catalog is current |
| `asset_writes.log` | GUI | one line per write of a hidden id (section 6) |
| `asset_catalog_off.txt`, `asset_hidden_off.txt` | Cassio | kill switches (section 7) |

The catalog holds real names (EBX slugs, master names). It is **never committed and never shipped**: `turbo_output` is
not packaged, and repository tests use the synthetic fixture `turbogui/tests/fixtures/asset_catalog_v1_small.json`
("Test Head 90001", `test_club_one`).

**Build key**: FC27.exe PE `TimeDateStamp-SizeOfImage` in upper-case hex, e.g. `6AC07E31-2145C000` (the key Turbo's
signature scan uses). At start-up the GUI loads `catalog_<running key>.json`; if there is none it loads the newest
`catalog_*.json` as **stale** (section 4).

Rebuild: `re_venv\Scripts\python.exe -I turbo\tools\asset_catalog\build_asset_catalog.py` (read-only on the game, Live
Editor and saves; writes only under `turbo_output\asset_catalog\`).

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
    "head3d":     { "ids": [41, 250], "vars": {"250": [0, 1]}, "toc": {"41": "L", "250": "LS"}, "generic": [26642] },
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
- `head3d.vars` / `head3d.toc` keys and `head3d.generic` ids are head ids. `toc` letters: `L` = launch bundle,
  `S` = contentsb, combined when the head is in both (e.g. `LS`).
- `callnames.<lang>.teams.<team>`: `<team>` is the commentary team (`dr`, `gm`, ...) or `""` for single-team packs; its
  keys are among `generic`, `simple`, `link`, `high` (missing key = empty).
- `names.<table>.<id>` = `[name, source]`, name not empty, source one of `master:<lang>`, `workbook`, `extra_names`,
  `slug`, `kit_slug`, `managers` (section 3).

### Families

| Key | Ids | Proof | DB field(s) it feeds |
|---|---|---|---|
| `head3d` | headassetid | 3D model (face texture + morph preset + starhead bundle) | `headassetid` with `headclasscode` 0 |
| `miniface` | playerid / headassetid | legacy file `data/ui/imgAssets/heads/p<id>.dds` | picture copies |
| `youthface` | headassetid | `data/ui/imgAssets/youthheads/p<id>.dds` | picture copies |
| `staffface` | headassetid | `data/ui/imgAssets/heads_staff/heads_staff_<id>.dds` | picture copies |
| `legendface` | id | legend pictures | picture copies |
| `hair` | hairtypecode | `model` (EBX) and/or `preview` | `hairtypecode` |
| `facialhair` | facialhairtypecode | model / preview | `facialhairtypecode` |
| `boots` | shoetypecode | model / preview | `shoetypecode` |
| `gkglove` | gkglovetypecode | model / preview | `gkglovetypecode` |
| `tattoo` | tattoo id | model / preview | `tattoo*` (7 fields) |
| `accessory` | accessory code | model / preview (mapping unknown) | `accessorycode1..4` (no gate) |
| `eyebrow` | eyebrowcode | cranium preset | `eyebrowcode` |
| `skinsurface` | skinsurfacepack | cranium preset | `skinsurfacepack` |
| `crest` | teamid | `paths`: crest file per team | picture copies |
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
| 4 | `kit_slug` | kit folder slug (teams) |
| 5 | `managers` | `turbo_dev\managers.csv` |
| 6 | (unnamed) | fallback "Head #id", "Youth face #id", "Callname #id" (`assets::fallback_name`) |

Tooltips say where a name came from (`assets::name_source_label`: "name from: ita_it master", "name from: game file
name", ...).

## 4. Hidden, computed live

The catalog never stores "hidden": it depends on the loaded save. The GUI rebuilds `assets::DbUse` when
`model.version()` changes. An id is **Hidden** when the catalog ships it and no current row has it in:

| Family | Fields |
|---|---|
| head3d | `players.headassetid` (any class) and `manager.headassetid` |
| miniface | `players.playerid` and `players.headassetid` |
| staffface | `manager.headassetid` |
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
head3d 41
head3d 250
```

Lua (`core/asset_gate.lua`) reads it once per session into a set. No file = today's rules, logged "unchecked".
`custom_headassets` refuses ids not in a present gate; `create_player` gives a real face iff the gate has the id.

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
- Native GUI cases: `TURBO_TEST_ONLY=<text> bash turbogui/tests/native/run_native.sh` runs only the cases whose name
  contains the text (any letter case), e.g. `TURBO_TEST_ONLY="gear pictures"`. The runner rebuilds only the objects
  whose source or included headers changed (g++ `-MMD` dependency files). Measured on this PC (MSYS2 UCRT64): cold build
  about 5.5 min; nothing changed about 10 s; a changed `src` header about 12 s plus its includers; a changed test header
  about 70 s (`tests/native/test_main.cpp` is recompiled, at `-O0`).
- A new worktree can skip the cold build: right after `git worktree add` (same sources as the cache), copy a warm
  cache with `cp -r <warm worktree>/turbogui/build/native_bin <new worktree>/turbogui/build/` (plain `cp -r`, so the
  copies are newer than the checkout). Copy `turbo/le27/libs` the same way (Live Editor's own Lua libs, not in git).
- No test reads the game folder. A full catalog build and in-game checks are acceptance steps, timed and reported.
