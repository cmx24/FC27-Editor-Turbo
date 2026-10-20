// FC 27 LE Turbo GUI - the Set-pieces card of Edit a player > Positions, attributes & PlayStyles (2.22 G-pickers owns the real card).
//
// CONTRACT between G-ia (the call line) and G-pickers (the card):
//     namespace turbo::setpiece_card { void draw(App& app, int64_t player_id); }
// G-ia calls it once per frame on the Positions tab, after "Playing style and roles", for the picked player (player_id > 0).
// G-ia ships ui_setpiece_card_stub.cpp: a WEAK definition that draws nothing. G-pickers' ui_setpiece_card.cpp defines the same function
// (strong); the Integrator drops the stub from both source lists (turbogui/scripts/build_win.sh, turbogui/tests/native/run_native.sh) when both are merged.
#pragma once
#include <cstdint>

namespace turbo {
class App;
namespace setpiece_card {
void draw(App& app, int64_t player_id);
}  // namespace setpiece_card
}  // namespace turbo
