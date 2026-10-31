// FC 27 LE Turbo GUI - the body-type picture maker (2.17 looks, track T): the window's side of core/bodytype_thumbs.h.
// A one-time run in a Tester window (LEICESTER, never saved): a real-face reference player is given every body type in turn and
// the game draws him once per body type; the pictures go to the body-type grid. Started ONLY from the "Body-type pictures" section
// (bodytype_thumbs_section), after a confirmation that names the career and the player (2.17.0: no request file starts it).
// The body type is written RAW (range-checked, read back): no undo step, no kept edit; his own body type is put back at the end.
#pragma once

namespace turbo {

class App;

// Every frame (App::draw, before the window is drawn, so a run goes on while the window is hidden): the run in progress
void tick_bodytype_thumbs(App& app);
// The "Body-type pictures" section (collapsed by default); no page draws it since the Diagnostics & logs page went
void bodytype_thumbs_section(App& app);
// The text the faces card and the Looks studio show instead of their own picture request while a run is going ("" = no run)
const char* bodytype_thumbs_busy_text();

}  // namespace turbo
