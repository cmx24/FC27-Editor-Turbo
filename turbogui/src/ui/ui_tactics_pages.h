// FC 27 LE Turbo GUI - the pages of the Tactics & Gameplay area (revamp track T4, GUI_REVAMP_SPEC.md 1.1, 2.3, 3.1). Each page is registered
// from its own .cpp with TURBO_REGISTER_PAGE (ui_shell.h); the functions are declared here so the native tests can draw them without the
// registry. Chrome only: the panels' writers and logic are unchanged.
//   Tactic presets   ui_tactic_presets.cpp  draw_tactic_presets        (default page of the area, [select | editor] + one primary action)
//   Gameplay presets ui_tactic_presets.cpp  draw_gameplay_presets      (community slider presets; the old Game settings view inside)
//   Standings        ui_standings.cpp       draw_standings_page        (draw_live_standings, unchanged)
#pragma once

namespace turbo {

class App;

void draw_gameplay_presets(App& app);
void draw_standings_page(App& app);

}  // namespace turbo
