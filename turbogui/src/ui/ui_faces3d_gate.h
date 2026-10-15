// FC 27 LE Turbo GUI - Players > Appearance > "3D hair and beard" (2.8 picker v1, no live preview): enrol scanned players
// (HeadClass 0) with a verified generic hair / beard model, Apply = turbo_output\faces3d\players.json + lua_override\player_kit.lua
// + faces3d_on.txt (core/faces3d.h), Remove = override + opt-in deleted. Takes effect at the next game start.
// A later live-preview panel can sit next to the pickers: it needs only the selected player's faces3d::Entry (faces3d_draft_entry).
#pragma once
#include <cstdint>

#include "core/faces3d.h"

namespace turbo {

struct App;
class Table;
struct PlayerRow;

void faces3d_gate_section(App& app, const Table& t, const PlayerRow& p);
// The selected player's not-yet-applied choice (nullptr when he is not enrolled): for a later preview panel
const faces3d::Entry* faces3d_draft_entry(int64_t playerid);

}  // namespace turbo
