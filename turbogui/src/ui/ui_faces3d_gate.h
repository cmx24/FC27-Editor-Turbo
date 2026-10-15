// FC 27 LE Turbo GUI - Players > Appearance > "3D hair and beard": enrol scanned players (HeadClass 0) with a verified generic
// hair / beard model, Apply = turbo_output\faces3d\players.json + lua_override\player_kit.lua + faces3d_on.txt (core/faces3d.h),
// Remove = override + opt-in deleted. Takes effect at the next game start.
// 2.9 live preview: a player enrolled with "Follow his database codes" (gate v3) at this game start is drawn from his DB hair /
// beard codes, so Preview writes the chosen codes (raw: no undo step, no kept edit; originals in memory and in
// turbo_output\faces3d\preview_backup.json first), asks the game for a 3D capture of him (app.capture, no faces3d cache) and shows
// it; Revert writes the originals back; Keep bakes the ids as a fixed entry (other careers unaffected) or keeps him following.
#pragma once
#include <cstdint>

#include "core/faces3d.h"

namespace turbo {

struct App;
class Table;
struct PlayerRow;

void faces3d_gate_section(App& app, const Table& t, const PlayerRow& p);
// The selected player's not-yet-applied choice (nullptr when he is not enrolled)
const faces3d::Entry* faces3d_draft_entry(int64_t playerid);
// Every frame (App::draw): collect the preview's picture, give up after a timeout. Never waits.
void tick_faces3d_preview(App& app);
// A preview capture is in flight (the chooser's 3D heads send nothing meanwhile)
bool faces3d_preview_pending();

}  // namespace turbo
