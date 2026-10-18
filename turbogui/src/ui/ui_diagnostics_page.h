// FC 27 LE Turbo GUI - Advanced pages "Diagnostics & logs" and "Data export" (2.16 GUI revamp, plan TURBO_GUI_REVAMP_PLAN.md sections 3.2,
// 4.3 and 5.3, brief 2.16-gui-pages, track "advanced"). Two self-contained page functions in ui_diagnostics_page.cpp; they call
// existing code only (App::hook_report, the asset catalog, App::log_lines, the Exports page of ui_tools.cpp).
//
// Nothing is registered with the shell here: the page ids AdvDiagnostics / AdvExport do not exist until the navigation re-cut
// (ui_shell.h, gui/revamp-2 step 4). The wiring builder adds, next to the other registrations:
//     TURBO_REGISTER_PAGE(shell::Page::AdvDiagnostics, draw_diagnostics_page);
//     TURBO_REGISTER_PAGE(shell::Page::AdvExport, draw_data_export_page);
// and adds this file to the Advanced allow-list of the word gate (tests/native/gui_words_baseline.txt "exempt"): it names hooks, Lua,
// file names and paths on purpose, like ui_database.cpp or ui_tuning.cpp.
//
// Performance (gui/perf, the cursor lag): the page reads NO file and lists NO folder per frame. Everything it shows (the game-hook
// report, the folder listing of the switch files, the asset catalog status, the connection facts) is gathered at most once a second
// into one snapshot and drawn from that; the log is clipped to the lines in view.
#pragma once
#include <string>
#include <vector>

namespace turbo {

class App;

// Advanced > Diagnostics & logs: game link health, hooks, switch files, log
void draw_diagnostics_page(App& app);
// Advanced > Data export: the CSV exports (the Exports page of ui_tools.cpp, unchanged), under a plain title and a folder button
void draw_data_export_page(App& app);

// ---- what the last frames drew / gathered (plain text is not an ImGui item, so the tests read it here)
struct DiagSwitch {
    std::string file;    // "kits3d_off.txt"
    std::string what;    // plain words for what the file does
    bool opt_in = false; // true: a "_on" file (the feature is switched on by it); false: an "_off" kill switch (the feature is off)
};
struct DiagnosticsState {
    long long refreshes = 0;       // snapshots gathered since start-up (at most one per second of App::now while the page is shown)
    double refreshed_at = -1.0;    // App::now of the last one (< 0 = none yet)
    bool health_good = false;      // "Everything Turbo needs from the game is working" (all game code spots found, no hook off, Lua answering)
    std::string health_line;       // that sentence, or "Needs attention: ..."
    std::string database_line;     // "Connected to the game's database (N tables)" / "Waiting for your career: ..."
    std::string catalog_line;      // the game asset catalog's status line
    size_t hook_rows = 0;          // game-hook lines in the snapshot (signatures + hooks)
    size_t log_lines = 0;          // lines of App::log_lines the log box has (what the clipper walks)
    std::vector<DiagSwitch> switches;  // the *_off.txt / *_on.txt files in turbo_output right now
    bool export_page_drawn = false;    // draw_data_export_page ran in the last frame
};
const DiagnosticsState& diagnostics_state();
// Plain description of one switch file ("" when Turbo does not know the name)
std::string diagnostics_switch_what(const std::string& file);
// Forget the snapshot (the next frame gathers a new one at once). Tests; the page never needs it.
void diagnostics_reset_for_tests();

}  // namespace turbo
