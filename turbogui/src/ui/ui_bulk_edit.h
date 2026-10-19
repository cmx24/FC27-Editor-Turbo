// FC 27 LE Turbo GUI - Players > Bulk edit (brief 2.20-IA-pages, track "bulk"; Cassio 2026-10-19 10:56: "bulk edit menu should be within
// Players and should allow me to click and choose ANY existing field in player edits from a list").
//
// Who: players picked with filters (search, clubs, my club, free agents, position, gender, nationality, age / overall / potential) and
// ticked off one by one. What: "Add field" opens a searchable list of every field of the players table; each chosen field gets the
// picker that fits it (a name list, a nation list, a position list, a yes / no switch, a date, a number with a value or a +/- change,
// PlayStyle give / take away). Apply: every player goes through App::edit (the validated path of the Players editor: range check,
// overall follows the attributes, kept edits of the career), a few players per frame. The page keeps its own undo of the last run
// (the field values it replaced, per player); App::push_bulk_undo is not used (it holds 30 players).
//
// NEW file, self-contained: draw_bulk_edit(App&) is not registered with the shell (the wiring owner does that). Needs one weak
// reference, `bool team_preset_apply(App&, int64_t)` (core/team_preset.h of 2.20/team-defaults): the "My team preset" button is
// greyed out while the build does not have it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace turbo {

class App;

// Players > Bulk edit. Draws a hint instead while the game is not connected.
void draw_bulk_edit(App& app);

namespace bulkedit {

// How a chosen field changes a player's value
enum class Mode { Set, Add, Give, Take };

// One chosen field and its value as the page holds it
struct Row {
    std::string field;       // the players table field name
    Mode mode = Mode::Set;   // Add: numbers only; Give / Take: the PlayStyle fields
    int64_t value = 0;       // Set: the value to write (a nation, position, code, switch, number)
    int64_t delta = 0;       // Add: the change
    int bit = 0;             // Give / Take: the PlayStyle bit
    int ymd[3] = {2000, 1, 1};  // date fields
    bool value_chosen = true;   // false: a picker still waits for a pick (nation, club)
};

// What the last frame / run showed (plain text is not an ImGui item, so the tests read it here)
struct State {
    size_t matched = 0;          // players that pass the filters
    size_t selected = 0;         // of them, not ticked off
    size_t rows = 0;             // chosen fields
    std::string why_not;         // why Apply is greyed out ("" = it can run)
    std::vector<std::string> preview;  // up to three lines "Name: Jersey fit Normal -> Tight"
    std::string result;          // the line after a run: "Changed 2 players (2 values)."
    bool running = false;        // a run or an undo is in progress
    size_t done = 0, total = 0;  // players handled / to handle in the run
    size_t undo_players = 0;     // players the last run can put back
    std::string undo_what;       // "Jersey fit" (the fields of the last run)
    std::string preset_line;     // the My team preset block: what the button does, or why it is greyed out
    bool preset_enabled = false;
    std::string actions_why;     // the career actions block: why Send is greyed out ("" = it can run)
};
const State& state();

// ---- the page without the window (tests drive it the way the clicks do)
void reset();                                          // filters, chosen fields, run record: the page as new
bool add_field(App& app, const std::string& field);    // "Add field" pick; false when the field is not on the list
void remove_field(const std::string& field);
Row* row_of(const std::string& field);                 // the chosen row, nullptr = not chosen
std::vector<std::string> field_list(App& app);          // the field names the "Add field" list offers
std::string field_group(const std::string& field);      // "Attributes", "Looks", ...
std::string field_title_of(const std::string& field);   // the plain title ("Jersey fit")
bool field_offered(const std::string& field);           // in the list (false: playerid and the name ids)
void select_all_matching(App& app);                     // untick nobody
void pick_only(App& app, const std::vector<int64_t>& playerids);  // tick exactly these (of the players that match)
std::vector<int64_t> picked_ids();                      // the players an Apply would change
void set_club(App& app, int64_t teamid);                // the filter's clubs = this one club (0 = none)
void set_search(const std::string& text);
bool start_apply(App& app);                             // the Apply click (false: greyed out)
bool start_undo(App& app);                              // the Undo click
void run_to_end(App& app, int max_frames = 100000);     // what the frames do, without frames (tests)
// The value a row writes for a player who holds `current` (clamped to the field's range)
int64_t new_value(const Row& r, int64_t current, int64_t min, int64_t max);

}  // namespace bulkedit
}  // namespace turbo
