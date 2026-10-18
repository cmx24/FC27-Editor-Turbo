// FC 27 LE Turbo GUI - Tactics section pages (GUI revamp plan step 8, brief 2.16-gui-pages, track "tactics").
//
// Three pages for the new Tactics section (plan 4.2 / 5.3). Each is a plain `void draw_..._page(App&)` (the signature of PageFn in
// ui_shell.h; the brief's "(App&, Ui&)" names a type this code base does not have). They are NOT registered here: the shell tables are
// gui/revamp-2's (step 4, navigation re-cut). The wiring builder does this:
//   Page::TacticPresets    -> draw_tactics_presets_page   (was draw_tactic_presets, ui_tactic_presets.cpp)
//   Page::GameplayPresets  -> draw_game_sliders_page      (was draw_gameplay_presets; label "Game sliders")
//   new Page::TeamSheet    -> draw_team_sheet_page        (label "Team sheet & bench")
// Behaviour is not rewritten: the presets page frames the existing panel (all its actions stay in ui_tactic_presets.cpp), the sliders
// page stages and discards through game_settings_stage / game_settings_unstage, the team sheet page reads through live_tactic_read.
// The one thing written outside those APIs is the bench-keep switch file, written exactly like the live tactic card does.
//
// State structs: plain text is not an ImGui item, so the tests read what each page drew here.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace turbo {

class App;

// ---- Tactics > Tactic presets (the frame around the existing panel)
struct TacticsPresetsPageState {
    bool drawn = false;
    std::string tag;          // "Not tested yet"
    std::string proof_line;   // what is proven: written and read back; the effect in matches is not tested
    std::string store_line;   // one list of presets, shared with Game sliders
    std::string tactic_now;   // "Balanced, line height 50 - shape 4-3-3" / why it cannot be read
    bool tactic_read = false;
    bool opt_in = false;      // "Let Turbo change my tactic" is on
};
const TacticsPresetsPageState& tactics_presets_page_state();
void draw_tactics_presets_page(App& app);

// ---- Tactics > Team sheet & bench
struct TeamSheetPageState {
    bool drawn = false;
    std::string tag;                       // "New, not tested yet"
    bool bench_on = false;                 // the toggle (the switch file is there)
    std::string bench_status;              // one plain line under the toggle
    std::string bench_hint;                // "also turn on ..." when the tactic opt-in is off ("" = none)
    bool sheet_read = false;               // the team sheet was read
    std::string sheet_why;                 // why not
    std::vector<std::string> starting;     // the 11 names of the starting XI, in the sheet's order
    std::vector<std::string> bench;        // bench and reserves, in the sheet's order
    bool setpieces_read = false;
    std::string setpieces_line;            // what the read-out holds / why not
    std::vector<std::pair<std::string, std::string>> takers;  // role label -> player name
};
const TeamSheetPageState& team_sheet_page_state();
void draw_team_sheet_page(App& app);
// Switches bench keep on or off (creates / removes its switch file) - the page's toggle; false when the file could not be changed
bool team_sheet_set_bench_keep(App& app, bool on);

// ---- Tactics > Game sliders
struct GameSlidersPageState {
    bool drawn = false;
    std::vector<std::string> names;        // the presets that carry sliders
    int selected = -1;                     // index into names
    std::string tag;                       // "Effect in matches: not tested yet"
    std::string counts_line;               // "3 sliders confirmed on the Custom screen, 120 experimental, 2 left out"
    std::string status;                    // "Not prepared" / "Prepared: close the game and install"
    std::string steps;                     // the three steps in one string, one per line
    bool can_prepare = false, can_discard = false, prepared = false;
    std::string message;                   // the last Prepare / Discard outcome in plain words
};
const GameSlidersPageState& game_sliders_page_state();
void draw_game_sliders_page(App& app);
// Tests: forget the pages' state (a new simulated game)
void tactics_pages_reset();

}  // namespace turbo
