// FC 27 LE Turbo GUI - Players / Managers > Appearance > "Hair & beard (3D)" for real faces (head class 0).
// 2.16 live looks (core/faces3d.h, template v4): "Apply hair & beard" writes his hairtypecode / facialhairtypecode as Turbo band
// codes (raw: no undo step, no kept edit; this career's database, saved with the career; a player's own codes go to
// turbo_output\faces3d\preview_backup.json first). The game draws the new look at its next model build, with no restart, and for a
// player Turbo asks for the game's 3D picture of him (app.capture). "His own hair & beard" writes his own codes back (a player on the
// saved list of older versions gets the "his own, live" codes 9999 / 511 and leaves the list). No restart: the hook builds
// player_kit.lua v4 in memory at every game start (win/lua_loader_win.cpp, core/faces3d.h build_live_override).
#pragma once
#include <cstdint>

#include "core/faces3d.h"

namespace turbo {

struct App;
class Table;
struct PlayerRow;
struct ManagerRow;

void faces3d_gate_section(App& app, const Table& t, const PlayerRow& p);
// Managers > Appearance: the same band on the manager row (no picture: the game's 3D capture is for players)
void faces3d_manager_section(App& app, const Table& t, const ManagerRow& m);
// The selected player's entry on the saved list (nullptr when he is not on it)
const faces3d::Entry* faces3d_draft_entry(int64_t playerid);
// Every frame (App::draw): collect the picture, give up after a timeout. Never waits.
void tick_faces3d_preview(App& app);
// The pickers' choice for the person shown (generic ids, faces3d::kLookNone = bald / no beard, 0 = his own); for tests and scripted checks
void faces3d_set_choice(bool manager, int64_t id, int64_t hair, int64_t beard);
// 2.17, at every career connect (App::refresh): a player who got the "his own, live" marker (9999 / 511) while he was on the saved
// list and is off it since this game start gets his recorded own codes back, when that is safe (no baked look of this game start
// can return: the hook is off, serves stock, or built a saved list that does not hold him; his codes still the marker this record
// wrote). 2.17.0: LOG-ONLY by default ("would put his own look back for N players: <ids>", nothing written) unless
// turbo_output\faces3d_auto_restore_on.txt exists. Returns how many players were restored (written).
int faces3d_restore_markers(App& app);
// Re-reads players.json and preview_backup.json (tests; the card reads them once per Turbo start)
void faces3d_reload(App& app);
// A picture request is in flight (the chooser's 3D heads send nothing meanwhile)
bool faces3d_preview_pending();

}  // namespace turbo
