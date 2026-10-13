// FC 27 LE Turbo GUI - Competitions > Gameplay tuning: the engine's gameplay tuning tables (core/tuning.h), live in game memory.
//
// Why Competitions: the tables are engine-wide. They change how every club plays every match (both teams, every competition), not the
// tactic of the club selected in Teams; Competitions already holds the other match-wide controls (Match setup's game variables), and the
// view needs no club picked first (Teams > Tactics lives inside a selected club's editor).
//
// The view: what the tables are and that values last for this game session only (the tables are not in the career save; written again
// at a career load only with the keep option on), the knobs file's line (which file, how many knobs, how many not confirmed), the
// opt-in, the status line with the first gate that stops a write, presets (Turbo's own and the user's, turbo_output\tuning_presets; load
// into the sliders, save, import, export), one collapsible group per knob group with a row per knob (plain label, unit, badges located /
// not located and applies live / next match / not known, the slider and a number box clamped to the knob's range, FC 27's own value and
// the value now, the database copies, Reset = FC 27's own value back now in every copy), what Apply will write, Apply (asks first), Undo
// last Apply, Restore game defaults, Discard edits, "write these values again at every career load" (turbo_output\tuning_keep.json), and
// the Measure area for track T2 (coming: its button is disabled). A knob not confirmed in the running game reads "Not located on this
// build: shown only, never written".
#pragma once
#include <string>
#include <vector>

namespace turbo {

class App;

// What the view drew in its last frame (tests read it here)
struct TuningViewState {
    bool drawn = false;
    std::string registry_line;  // "turbo\tuning\knobs_6AC07E31.json: 21 knobs (21 not confirmed) for game build ..." or why there is none
    bool session_note = false;  // the "values last for this game session only" line was drawn
    std::string anchors_line;   // the locators' status
    std::string status;         // "ready" or the first failing gate
    std::vector<std::string> gates;
    bool opt_in = false;
    struct Row {
        std::string id, label, group;
        std::string located;    // "located" / "not located"
        std::string applies;    // "meant live, not measured" / "meant for next match, not measured" / "applies: not known yet"
        std::string why;        // why not located ("" when located)
        std::string game, turbo, def;  // the value in memory now, the slider's value, FC 27's own value (formatted; "" = not known)
        std::string copies;     // "base 8, simulation 8.5" when the knob has several database copies ("" = one)
        bool editable = false;  // the slider is enabled
        bool changed = false;   // the slider differs from the game's value
        bool can_reset = false; // Reset is enabled (a copy differs from FC 27's own value, or the slider was moved)
    };
    std::vector<Row> rows;
    std::vector<std::string> groups;
    std::vector<std::string> plan;  // what Apply will write
    std::string plan_error;
    bool can_apply = false, can_undo = false, can_restore = false, busy = false;
    std::string undo_why, restore_why;  // why Undo / Restore game defaults are greyed ("" = they are not)
    bool confirm_open = false;
    std::vector<std::string> confirm_lines;
    std::string last, undo_state, keep_line;
    bool keep_on = false;
    std::vector<std::string> presets;  // the presets listed ("High line + aggressive press (Turbo)")
    std::string preset_line;           // the last preset load / save / import / export outcome
    bool measure_enabled = true;       // the Measure button (false: coming)
    std::string measure_label;
    std::string error;
};
const TuningViewState& tuning_view_state();

// The view (Competitions > Gameplay tuning). Never throws: a failure shows an inline error.
void draw_tuning(App& app);
// Every frame (App::tick): job results (the undo file, the kept values), lost jobs, the re-apply of kept values at a career load. Never throws.
void tuning_tick(App& app);
// Tests: forget the view's state (a new simulated table)
void tuning_reset();

}  // namespace turbo
