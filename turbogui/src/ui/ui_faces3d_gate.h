// FC 27 LE Turbo GUI - Players / Managers > Appearance > "Hair & beard (3D)" for real faces (head class 0).
// 2.16 live looks (core/faces3d.h, template v4): "Apply hair & beard" writes his hairtypecode / facialhairtypecode as Turbo band
// codes (raw: no undo step, no kept edit; this career's database, saved with the career; a player's own codes go to
// turbo_output\faces3d\preview_backup.json first). The game draws the new look at its next model build, with no restart, and for a
// player Turbo asks for the game's 3D picture of him (app.capture). "His own hair & beard" writes his own codes back. Needs Turbo's
// player_kit.lua v4 at game start: one restart the first time (tick_faces3d_preview rewrites an older file once per Turbo start
// when the feature is on). The saved list of older versions (players.json) is still read at game start.
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
// Every frame (App::draw): collect the picture, give up after a timeout; once per Turbo start, bring Turbo's player_kit.lua up to
// the live version when the feature is on. Never waits.
void tick_faces3d_preview(App& app);
// The pickers' choice for the person shown (generic ids, 0 = his own); for tests and scripted checks
void faces3d_set_choice(bool manager, int64_t id, int64_t hair, int64_t beard);
// A picture request is in flight (the chooser's 3D heads send nothing meanwhile)
bool faces3d_preview_pending();

}  // namespace turbo
