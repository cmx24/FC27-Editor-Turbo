// FC 27 LE Turbo GUI - Tactics section pages (GUI revamp plan step 8, brief 2.16-gui-pages, track "tactics").
//
// Three pages for the new Tactics section (plan 4.2 / 5.3). Each is a plain `void draw_..._page(App&)` (the signature of PageFn in
// ui_shell.h; the brief's "(App&, Ui&)" names a type this code base does not have). They are NOT registered here: the shell tables are
// gui/revamp-2's (step 4, navigation re-cut). The wiring builder does this:
//   Page::TacticPresets    -> draw_tactics_presets_page   (was draw_tactic_presets, ui_tactic_presets.cpp)
//   Page::GameplayPresets  -> draw_game_sliders_page      (was draw_gameplay_presets; label "Game sliders"; 3.0.1: a pointer to the Lab)
//   Page::AdvTactics       -> draw_lab_tactics_page       (3.0.1 GUI30K, registered here: the Lab page + the offline slider file section)
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
    // 2.20 (brief 2.20-T-tactics): files of the store that could not be used (never silent), the Custom sliders the game saved last
    std::vector<std::string> skipped;      // "x.json: why"
    std::string sliders_now;               // "Custom sliders now: 125 values (...)" / "... not read (why)"
    int refreshes = 0;
    // 3.0.1 (GUI30K): the list, Prepare / Discard, the steps and the "Save one on Tactic presets" hint are drawn only in the Lab, on
    // Tactics (experimental) under the heading kOfflineSlidersHeading; the Tactics area shows no slider staging
    bool moved_note = false;               // draw_game_sliders_page drew where the section went (no list, no Prepare)
    std::string lab_heading;               // the heading the Lab section drew ("" = not drawn)
    std::string lab_label;                 // the label under it, verbatim
};
const GameSlidersPageState& game_sliders_page_state();
// Tactics > Game sliders: since 3.0.1 only a pointer to the Lab section (the page stays registered: Home and Changes & undo link to it)
void draw_game_sliders_page(App& app);
void game_settings_link_line(App& app, const char* id);  // 10-28: the one-line link to Game settings > Game settings & CPU sliders
// 3.0.1 (GUI30K): the offline slider file section - heading, label, the list, Prepare / Discard, the steps (Lab > Tactics (experimental))
extern const char* const kOfflineSlidersHeading;  // "Offline slider file (experimental)"
extern const char* const kOfflineSlidersLabel;    // "No effect seen in game on 2026-10-24: ... Kept for diagnosis."
void draw_offline_slider_section(App& app);
// Lab > Tactics (experimental) in the new registry: draw_tactics_page (ui_tactics.cpp) as it is, then the section above in the bottom part
void draw_lab_tactics_page(App& app);
// Tests: forget the pages' state (a new simulated game)
void tactics_pages_reset();

}  // namespace turbo
