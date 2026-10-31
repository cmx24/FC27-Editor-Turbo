// FC 27 LE Turbo GUI - Turbo X: the window's side of core/gear_thumbs.h (the game draws the look items that have no picture).
// Started by <turbo_output>\pictures\gear_thumbs\request.json (polled at most every 1 s, only with a career loaded and no other
// capture going) or by the "Make missing pictures" button of the Body type card (from <turbo_output>\pictures\want.json, after a
// confirmation naming the career and the player). Every field a run touches is written back to the player's own value at the end.
#pragma once
#include <cstdint>

namespace turbo {

class App;

// Every frame (App::draw, before the window, like tick_bodytype_thumbs): the request file and the run in progress
void tick_gear_thumbs(App& app);
// The Body type card's button (ui_looks_studio.cpp), for `player` (the person the card shows): Make missing pictures / Cancel / status
void gear_thumbs_button(App& app, int32_t player);
// "" when no run is going; else the text other picture requests show instead of starting (the run owns the capture service)
const char* gear_thumbs_busy_text();

}  // namespace turbo
