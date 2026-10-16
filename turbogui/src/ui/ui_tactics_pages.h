// FC 27 LE Turbo GUI - the pages of the Tactics & Gameplay area (revamp track T4, GUI_REVAMP_SPEC.md 1.1, 2.3, 3.1) and the one amber line of
// the not-proven pages. Each page is registered from its own .cpp with TURBO_REGISTER_PAGE (ui_shell.h); the functions are declared here so the
// native tests can draw them without the registry. Chrome only: the panels' writers and logic are unchanged.
//   Tactic presets   ui_tactic_presets.cpp  draw_tactic_presets        (default page of the area, [select | editor] + one primary action)
//   Gameplay presets ui_tactic_presets.cpp  draw_gameplay_presets      (community slider presets; the old Game settings view inside, Advanced only)
//   Standings        ui_standings.cpp       draw_standings_page        (draw_live_standings, unchanged)
//   Advanced pages   ui_tactics.cpp / ui_match.cpp / ui_tuning.cpp: draw_tactics_page, draw_match_setup_page, draw_tuning_page (amber line on top)
#pragma once
#include "ui_theme.h"

namespace turbo {

class App;

void draw_gameplay_presets(App& app);
void draw_standings_page(App& app);
void draw_tactics_page(App& app);
void draw_match_setup_page(App& app);
void draw_tuning_page(App& app);

// The one amber line a not-proven page shows at the top in the Advanced view (spec 2.3). `doc` names the file with the details.
// Counts the draws so a test can see it was shown (and that Simple, which never draws these pages, did not).
inline int& unproven_line_draws() {
    static int n = 0;
    return n;
}
inline void draw_unproven_line(const char* doc) {
    ++unproven_line_draws();
    theme::warn_text("Not proven in game yet: writes the data, effect unmeasured. Details: %s", doc);
}

}  // namespace turbo
