// FC 27 LE Turbo GUI - archetype with a target overall (2.3): the Players editor's Attributes tab (ui_players.cpp
// archetype_bar: role, position, Target overall, preview, Apply, Undo) and Create player (ui_presets.cpp create_dialog).
// core/archetypes.h apply_target() does the arithmetic; the widgets below are shared by both places (ui_players.cpp).
#pragma once
#include <cstdint>
#include <string>

#include "core/archetypes.h"

namespace turbo {

// The Attributes tab's archetype row (the tests read it back)
struct ArchetypeUiState {
    int64_t playerid = 0;           // the player the row was set up for (another player resets position and target)
    int arch = -1;                  // index into archetypes::all(), -1 = none chosen (kept from one player to the next)
    int position = -1;              // the position whose formula the target is for: his own unless picked
    bool position_picked = false;
    int target = 0;                 // Target overall 1..99: his overall unless typed
    bool target_typed = false;
    bool adopt_position = true;     // a position other than his: Apply makes it his main position too
    // the preview opened by "Preview / apply..."
    int64_t pend_playerid = 0;
    int pend_arch = -1;
    archetypes::TargetResult pend;
    overall::Attrs pend_before{};   // his attributes when previewed (Apply refuses when they changed since)
    int pend_position = -1;         // the position the target is for
    int pend_his_position = -1;     // his preferredposition1 when previewed
    int64_t pend_stored = 0;        // his stored overall when previewed
    // the last Apply: its bulk undo label (the Undo button undoes it while it is his newest bulk change)
    int64_t applied_playerid = 0;
    std::string applied_label;
    std::string applied_note;       // the line shown after Apply ("... read back: ...")
};
const ArchetypeUiState& archetype_ui_state();

// Combo over archetypes::all(): the group of `position` first ("(this position)"), the others below; `none_label`
// (optional) adds an entry for -1. Sets `sel` and returns true on a pick.
bool archetype_combo(const char* id, int& sel, int position, float width, const char* none_label = nullptr);
// The attributes that differ between two sets (Attribute, Now, New); with `arch`, those outside its profile are marked
// with a * (moved only because the profile alone could not reach the target)
void attribute_changes_table(const overall::Attrs& before, const overall::Attrs& after, const archetypes::Archetype* arch = nullptr);
// The lines under a target result: the refusal with the closest overall, or the resulting overall, the attributes
// outside the profile that moved and the clamped ones. Returns the closest overall when refused (a "Use N" button
// is the caller's), else 0.
int archetype_result_lines(const archetypes::TargetResult& r, int position);

}  // namespace turbo
