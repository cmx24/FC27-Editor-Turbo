// FC 27 LE Turbo GUI - Competitions > Gameplay tuning: the engine's gameplay tuning tables (core/tuning.h), live in game memory.
//
// Why Competitions: the tables are engine-wide. They change how every club plays every match (both teams, every competition), not the
// tactic of the club selected in Teams; Competitions already holds the other match-wide controls, and the view needs no club picked first.
//
// The view (2.4, docs/TACTICS_OVERHAUL_DECISION.md 3.1 / WP-A2): what the tables are and that values live in memory only (the profile
// writes them again); the knobs file's line and how the tables were located (through the attribute manager, or the name search); the
// tuning profile (turbo_output\tuning_profile.json: on / off, its preset and values, what the last automatic moment did); the tick that
// allows writes from the view in this game session; the status line with the first gate that stops a write; Apply (asks first), Undo last
// Apply, Restore FC 27 defaults (FC 27's own values back, the profile off), Discard edits; Turbo's own presets and the user's (use one for
// the profile, save, import, export); one collapsible group per knob group: a PROVEN knob (a "proof" in the knobs file: its effect was
// measured in a played match) has a slider and a number box, a LOCATED one a read-only row "located, effect not proven" with the value
// now, FC 27's own value and the profile's value; an UNLOCATED one is hidden and listed under "Not yet working on this build".
//
// The tick (tuning_tick, every frame): job results, the profile file (every 2 s), the locate (every second), and the profile's automatic
// moments (core/tuning.h MomentTracker): boot (main menu), career_load, hub; each one job, logged "tuning: profile applied (...)".
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace turbo {

class App;

// What the view drew in its last frame (tests read it here)
struct TuningViewState {
    bool drawn = false;
    std::string registry_line;  // "turbo\tuning\knobs_6AC07E31.json: 23 knobs (13 not confirmed) for game build ..." or why there is none
    std::string locate_line;    // "Located through the attribute manager: 2 groups, 10 knobs" / "Locating ..." / the fallback
    bool session_note = false;  // the "values live in game memory only" line was drawn
    std::string anchors_line;   // the locators' status
    std::string status;         // "ready" or the first failing gate
    std::vector<std::string> gates;
    bool opt_in = false;        // a write from the view is opted in (the session tick, the profile, tuning_on.txt)
    bool session_tick = false;  // the tab's tick
    bool profile_on = false;
    std::string profile_line;   // "Profile: Turbo compact, 3 value(s) ..." (and its file error)
    std::string moment_line;    // what the last automatic moment did
    bool test_mode = false;     // tuning_test_unproven_on.txt is present (the red line was drawn)
    struct Row {
        std::string id, label, group;
        std::string state;      // "proven" / "located" (unlocated knobs have no row: not_working)
        std::string located;    // "located" / "not located"
        std::string applies;    // "proven in <session>" / "located, effect not proven"
        std::string why;        // why not located ("" when located)
        std::string game, turbo, def, profile;  // the value in memory now, the slider's value, FC 27's own value, the profile's (formatted; "" = none)
        std::string copies;     // "base 8, simulation 8.5" when the knob has several database copies ("" = one)
        bool editable = false;  // the slider is enabled
        bool changed = false;   // the slider differs from the game's value
        bool can_reset = false; // Reset is enabled (a copy differs from FC 27's own value, or the slider was moved)
    };
    std::vector<Row> rows;
    std::vector<std::string> not_working;  // the labels of the knobs not located on this build (hidden)
    std::vector<std::string> groups;
    int proven = 0, located = 0, unlocated = 0;
    std::vector<std::string> plan;  // what Apply will write
    std::string plan_error;
    bool can_apply = false, can_undo = false, can_restore = false, busy = false;
    std::string undo_why, restore_why;  // why Undo / Restore FC 27 defaults are greyed ("" = they are not)
    bool confirm_open = false;
    std::vector<std::string> confirm_lines;
    std::string last, undo_state;
    std::vector<std::string> presets;  // the presets listed ("Turbo compact (Turbo)")
    std::string preset_line;           // the last preset use / save / import / export outcome
    std::string error;
};
const TuningViewState& tuning_view_state();

// The view (Competitions > Gameplay tuning). Never throws: a failure shows an inline error.
void draw_tuning(App& app);
// Every frame (App::tick): job results (the undo file, the profile), lost jobs, the profile's automatic moments. Never throws.
void tuning_tick(App& app);
// Tests: forget the view's state (a new simulated table)
void tuning_reset();
// The wall clock (seconds) the profile's moments measure the prematch window with (default std::chrono::system_clock; never App::now,
// which runs faster than wall time in game). Tests drive it by hand; nullptr = the default
void tuning_set_wall_clock(std::function<double()> clock);

}  // namespace turbo
