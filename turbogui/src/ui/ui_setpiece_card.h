// FC 27 LE Turbo GUI - the Set-pieces card of Edit a player > Positions, attributes & PlayStyles (2.22 G-pickers owns the real card).
//
// CONTRACT between G-ia (the call line) and G-pickers (the card):
//     namespace turbo::setpiece_card { void draw(App& app, int64_t player_id); }
// G-ia calls it once per frame on the Positions tab, after "Playing style and roles", for the picked player (player_id > 0).
// G-ia shipped ui_setpiece_card_stub.cpp (a WEAK definition that drew nothing) until G-pickers' ui_setpiece_card.cpp (the real card) was merged;
// the 2.22 integration removed the stub and its entries in turbogui/scripts/build_win.sh and turbogui/tests/native/run_native.sh.
#pragma once
#include <cstdint>

namespace turbo {
class App;
namespace setpiece_card {
void draw(App& app, int64_t player_id);
}  // namespace setpiece_card
}  // namespace turbo
