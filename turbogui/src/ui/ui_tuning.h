// FC 27 LE Turbo GUI - Competitions > Gameplay tuning: the engine's gameplay tuning tables (core/tuning.h), live in game memory.
//
// Why Competitions: the tables are engine-wide. They change how every club plays every match (both teams, every competition), not the
// tactic of the club selected in Teams; Competitions already holds the other match-wide controls, and the view needs no club picked first.
//
// The page (2.5, one page, driven by the knob catalogue knobs_<build>.json, so it scales to many knobs): what the tables are and that values
// live in memory only; the knobs file's line and how the tables were located; the ACTIVE PRESET line (the profile's preset, "values changed
// since", its proof status) with the switch "apply at every career load and in the hub" and the session tick; the presets (use, duplicate,
// rename, delete, export, import, save the profile as one; Turbo's own starters are read-only); the CHANGES block (the first failing gate in
// plain words, what Apply will write, the knobs that differ from FC 27) with Apply (asks first), Undo last Apply, Restore FC 27 defaults and
// Discard edits; the KNOBS: a search box and "only changed", one collapsible category per catalogue group (a long catalogue opens the
// categories that hold a slider or a change; the search opens what it finds). Each row: a slider and a typed box only for a knob with a
// MEASURED effect (a "proof" in the knobs file), Reset, FC 27's own value, and a badge: "effect measured" (slider), "engine-confirmed" (the
// engine-read check passed in game, optional "engine_check" in the knobs file; read-only), "located, not proven" (read-only). A knob that is
// not located is hidden and listed under "Not yet working on this build".
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
        std::string badge;      // "effect measured" / "engine-confirmed" / "located, not proven" (2.5)
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
    int candidates = 0;  // 2.5: inventory candidates of the knobs file (unverified, never written); not counted in `unlocated`, not listed in not_working
    std::vector<std::string> plan;  // what Apply will write
    std::string plan_error;
    bool can_apply = false, can_undo = false, can_restore = false, busy = false;
    std::string undo_why, restore_why;  // why Undo / Restore FC 27 defaults are greyed ("" = they are not)
    bool confirm_open = false;
    std::vector<std::string> confirm_lines;
    std::string last, undo_state;
    std::vector<std::string> presets;  // the presets listed ("Turbo compact (Turbo)")
    std::string preset_line;           // the last preset use / save / import / export outcome
    std::string preset_status;         // 2.5: the picked preset's proof status ("4 knobs: 0 effect measured, 0 engine-confirmed, 4 located only, 0 not in this build")
    std::string active_preset;         // 2.5: the profile's preset ("" = none: custom values)
    std::string active_line;           // "Active preset: Turbo compact | not applied automatically (profile off)"
    bool active_modified = false;      // the values now differ from what the active preset gives
    std::vector<std::string> differs;  // 2.5: the knobs that hold another value than FC 27 (now or in the profile)
    std::string search;                // 2.5: the search box
    bool only_changed = false;
    int shown = 0;                     // knob rows the search and filter leave
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
