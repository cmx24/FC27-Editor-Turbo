# Club edits: global or one career (Turbo X 2.0.2, 2026-11-01)

Owner's rule (2026-10-18, restated 2026-11-01): an edit made with no career loaded belongs to the squads file and is **global** (every
save, Kick Off, the main menu). An edit made in a career lives in **that career only**. Example given: "the Italian team names, crests
and kits are global, every other combo (name + crest + kit) is save-specific."

## Where each part lives
| Part | Global | One career |
|---|---|---|
| Name | `turbo_output\team_names.json` entry `scope: "global"` + Live Editor's `extensions\global\custom_team_names.csv` | entry `scope: "career:<id>"`; taken OUT of custom_team_names.csv (Live Editor reads it in every save) |
| Crest | the files in `mods\legacy\data\ui\imgAssets\crest*\...` (recorded in `careers\global_own.json` when Turbo wrote them) | `turbo_output\careers\<id>\crest*\...`, copied into `mods\legacy` only while that career is identified (core/career_faces.h sync, one safety record `careers\active.json`) |
| Minikit | `mods\legacy\data\ui\imgAssets\kits\j<type>_<team>_0.dds` | `careers\<id>\kits\...` (same route) |
| Kit colours / settings | `reapply_edits.json` kit rows `scope: "global"` | rows `scope: "career:<id>"` |
| 3D kit | `kits\swap_state.json` entry `scope: "global"` (an entry without one: global) | `scope: "career:<id>"`: in the kit table only while that career is identified (kitswap::Runtime::set_career) |

The scope of an edit made now: `App::identity_scope()` = `global` outside a career, `career:<id>` in an identified one, refused in a
career Turbo has not identified yet (it could not be kept with it).

## Moving edits
Turbo settings > Changes > **Where club edits live**: every club with a name / crest / kit edit, where each part lives, and per club
**Global** / **This career**. Global takes every scope of the club; a career takes only the shared parts (global / not assigned): another
career's own name, crest or kit stays that career's. Files taken out of `mods\legacy` are kept in `turbo_output\careers\_moved\<stamp>\`.

**Sort club edits** (one button, a preview list first): Serie A (31) / Serie B (32) clubs -> Global; every other club -> the career it
belongs to (the one career holding any of its edits, else the career whose own club it is, from the career label "<manager>, <club>");
a club with no such career is listed and left as it is.

## Not covered / known limits
- One staged 3D kit per club and slot (one scope for it), not one per career.
- Main-menu editing needs Turbo connected there (Live Editor's Lua: `turbo_gui_load.lua`); the bridge connects by itself only on the
  first career-mode event.
- Kit colours / settings kept as global are written at each career load, not at the main menu (there the edit goes straight into the
  loaded squads database).
- Not yet seen in game.
