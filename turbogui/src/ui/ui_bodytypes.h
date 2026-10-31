// FC 27 LE Turbo GUI - body type entry point (Players > Profile > Roles and body, Managers > Details, Players list, Career, Images):
// the ONE "Body type..." button per page. It opens the visual picker window of ui_visual_picker.h for bodytypecode (figures on one
// height scale, plain names; the same picker as the Look card's grid), and keeps "Copy body type from player...". A player-specific
// body is not written onto a generic (or unknown) head unless the user ticks the box that says it is untested.
#pragma once
#include <cstdint>
#include <string>

#include "core/bodytype_catalog.h"
#include "core/t3db.h"

namespace turbo {

class App;

// The catalogue the editors share: loaded from the App's Live Editor folder (probe file, localize.json) on first use and again
// when the probe file changes
bodytype::Catalog& bodytype_catalog(App& app);
// Text of a bodytypecode value for the editors' combos: Live Editor's name, field_labels.h's, else "Specific body #N"
std::string bodytype_label(int64_t code);

// "Body type..." button (opens the visual picker window, drawn from here) for the record's bodytypecode; nothing when the table has none.
// manager: the record is a manager (head class unknown unless the table has headclasscode)
void bodytype_gallery_button(App& app, const Table& t, uint64_t rec, bool manager);

// Write `code` as the record's bodytypecode through the App's validated edit. A risky pairing (bodytype::risky_pairing) is
// refused with *msg set unless allow_risky. Returns true when the value is written or already there.
bool apply_bodytype(App& app, const Table& t, uint64_t rec, int64_t code, bool allow_risky, std::string* msg);

// The Body type window's "allow a player-specific body on this head" box: drawn by the picker, read by visual_apply
void bodytype_risky_box(App& app, const Table& t, uint64_t rec);
bool bodytype_allow_risky();

// Last action of the entry point, for the tests (plain text is not an ImGui item)
struct BodyTypeUiState {
    std::string last_result;  // "Body type set to Tall and Lean", or the refusal
};
const BodyTypeUiState& bodytype_ui_state();

}  // namespace turbo
