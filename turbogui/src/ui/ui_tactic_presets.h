// FC 27 LE Turbo GUI - Teams > Tactic presets (2.12): save and load the EA game's OWN tactic and gameplay settings as Turbo JSON
// (core/tactic_presets.h). It is a tab of the team editor, not under Tactics: the game applies the values
// natively; Turbo only writes them with the same guarded channel E job as the Tactics card (ui_live_tactic.h) and stages the settings sliders
// with the Game settings tab's staging (ui_game_settings.h). Nothing here is proven in a played match yet: the banner says so.
//   Save current  reads your club's live tactic (channel E) and the Custom sliders block of the settings file into
//                 turbo_output\tactic_presets\<name>.json
//   Load          asks first, then writes the tactic part into the live team sheet (+ its cm_mentalities row, undo file channel_e_undo.json)
//                 and stages the verified sliders (turbo_output\settings_pending.json, applied with FC 27 closed)
//   Undo last load puts the values from before the load back (same job), unstages sliders that were not applied yet
//   Delete        removes the preset's file
//   After a career load, if the live tactic no longer holds the last load, a prompt offers Re-apply (one click, never automatic)
// The preset files, turbo_output\tactic_preset_last_load.json and the staged settings are the only things written; reapply_edits.json is never touched.
#pragma once
#include <string>
#include <vector>

namespace turbo {

class App;

struct TacticPresetsState {
    bool drawn = false;
    std::string banner;                 // the honest banner
    std::string live_line;              // "Your club's live tactic (read now): ..." / "not read (why)"
    bool live_ok = false;
    bool opt_in = false;                // channel E on (needed to load)
    bool include_settings = true;       // Save current also stores the Custom sliders block
    bool include_unverified = true;     // 2.12.1: a saved preset also stages the table-mapped sliders the EA screen has not shown yet (test build default)
    bool include_setpieces = true;      // 2.12.1: Save current also stores the captain / takers / corner supports / pre-set subs (read through Lua)
    std::string setpieces_line;         // what the set-piece read-out holds now / why not
    std::vector<std::string> names;     // the presets, by name
    std::vector<std::string> summaries;
    int selected = -1;
    std::string status;                 // the last Save / Load / Undo / Delete outcome
    std::string gate;                   // why Load would be refused now ("" = ready; the selected preset)
    std::vector<std::string> notes;     // notes of the selected preset's load (formation differs ...)
    bool can_save = false, can_load = false, can_undo = false, busy = false;
    bool confirm_open = false;
    std::string confirm_title;
    std::vector<std::string> confirm_lines;
    std::string last_line;              // "Last load: <preset> (loaded) ..." ("" = none)
    std::string reapply;                // the Re-apply prompt ("" = none)
    std::vector<std::string> errors;    // preset files that were skipped (not valid) and why
    // 2.20 (brief 2.20-T-tactics): the plain reason of a failed live read (tpreset::live_why_plain), the Custom sliders block the game saved
    // last (read at the first look and at every Refresh), and how often Refresh ran
    std::string live_plain;
    bool sliders_ok = false;
    std::string sliders_line;           // "Custom sliders now: 125 values (sprint speed you 50 / CPU 50, acceleration you 50)" / "... not read: why"
    int refreshes = 0;
};
const TacticPresetsState& tactic_presets_state();

// 2.13 revamp: the Gameplay presets page (community slider presets: the presets that carry a Custom sliders block), same presets, same Load / Undo
struct GameplayPageState {
    bool drawn = false;
    std::string note;                   // the one muted sentence under the title
    std::vector<std::string> names;     // presets with a sliders block
    int selected = -1;                  // index into the shown list (-1 = none)
    bool can_apply = false, can_undo = false;
    bool read_back_ok = false;          // the last load of the selected preset finished ("loaded")
};
const GameplayPageState& gameplay_page_state();
// True when the two pages registered themselves with the shell at start-up (the registry can be cleared by tests later)
bool tactic_pages_registered_at_start();

// The Tactic presets page (2.13: the default page of Tactics & Gameplay; before, a tab of the team editor). Never throws.
void draw_tactic_presets(App& app);
// Every frame (App::tick): finishes a load / undo when its game-thread job came back (the last-load file, the staging of the sliders).
void tactic_presets_tick(App& app);
// Tests: forget the panel's state (a new simulated game)
void tactic_presets_reset();

}  // namespace turbo
