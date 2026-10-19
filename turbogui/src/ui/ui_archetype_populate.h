// FC 27 LE Turbo GUI - 2.20 R3: "Fill the whole player" - one player type, one wanted overall and one tier (superstar ... prospect) set
// his positions, attributes, roles, PlayStyles, traits, potential, contract and wage, and the squad role (core/archetype_gen.h).
// Two entry points, each ONE call line in the page that shows them (the page owners add it; this file draws everything else):
//   Edit player > Positions, attributes & PlayStyles, below the player type row:   archetype_populate_panel(app, table, player);
//   Create player, below the Player type row:  if (archetype_populate_create(app, template, form)) arch = -1;
// Everything is written through the existing paths: App::edit_field (validated, undo steps, kept edits, development plan) with one
// App::BulkUndo entry, the same "Undo last big change" the Overall +1/-1 buttons use; Create fills the form's boxes (the Create
// click sends what changed, core/create_form.h). Nothing here edits the squad file.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/archetype_gen.h"
#include "core/create_form.h"
#include "core/model.h"

namespace turbo {

class App;

// One players field a Populate writes: the value now and the value after
struct PopulateChange {
    std::string field;
    int64_t was = 0, to = 0;
};

// What Populate would write for this player: only the fields that differ (positions 1..7, the 34 attributes, role1..9, the four
// PlayStyle/trait fields, overall, potential and, in a career, contract and wage). Fields the players table lacks are skipped.
std::vector<PopulateChange> populate_changes(App& app, const Table& t, const PlayerRow& p, const archetype_gen::PlayerSpec& s, bool in_career);

// Write them (one bulk undo entry labelled `label`), read every field back. Returns the number of fields written, or -1 when a
// write was refused (what was written is put back). `readback_ok` is true when every field reads as written.
int populate_write(App& app, const Table& t, const PlayerRow& p, const std::vector<PopulateChange>& changes, const std::string& label, bool* readback_ok);

// Undo the newest bulk change of this player (the same as the "Undo last big change" button on the attributes page)
bool populate_undo(App& app, const Table& t, const PlayerRow& p);

// The panel's state as the last frame drew it (the tests read it back)
struct PopulateUiState {
    bool table_ok = false;
    std::string table_error;
    int arch = -1;                         // index into archetypes::all(), -1 = none chosen
    int target = 0;
    bool target_typed = false;
    int tier = 1;                          // archetype_gen::Tier as an int
    int side = 0;                          // archetype_gen::Side
    archetype_gen::PlayerSpec pend;        // the previewed spec
    std::vector<PopulateChange> pend_changes;
    bool pend_ok = false;
    int64_t applied_playerid = 0;
    std::string applied_label;
    std::string applied_note;
    bool create_mode = false;
};
const PopulateUiState& populate_ui_state();

// Edit player: the three controls (player type, wanted overall, tier), a review window listing what will change, Populate and Undo
void archetype_populate_panel(App& app, const Table& players, const PlayerRow& p);

// Create player: the same three controls over the form `cur`; Populate fills the form's boxes (the Create click sends what changed),
// Undo puts the form back as it was. Returns true on the frame Populate was clicked: the caller then clears its older player type
// choice (that one would send its own attributes over these).
bool archetype_populate_create(App& app, const PlayerRow& tmpl, create_form::Form& cur);

}  // namespace turbo
