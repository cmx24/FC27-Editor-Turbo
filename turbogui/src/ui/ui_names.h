// FC 27 LE Turbo GUI - the names the game shows for a player: shared by Players > Names (ui_names.cpp) and
// Players > Callname (ui_callnames.cpp)
#pragma once
#include <cstdint>
#include <string>

#include "app.h"

namespace turbo {

// The name parts the game shows for the player: editedplayernames when it has a row (a non-empty text wins), else the
// name ids' texts (app.model.names_by_id())
struct ShownName {
    std::string first, last, common;
    // the shirt name (1.0.3): his row's playerjerseyname, else his playerjerseynameid's text, else the shown surname.
    // A kept-name row always carries it: the game prints the row's playerjerseyname, and an empty one printed nothing.
    std::string jersey;
    uint64_t edited_rec = 0;  // his editedplayernames row (0 = none)
};
ShownName shown_name(App& app, const Table& t, const PlayerRow& p);

// What a Lua insert carries so that Turbo's Lua side counts the rows again right before it adds one
// (features/callnames.lua room_now): the table's capacity as read now, and the career load it was read in
void add_room_check(App& app, nlohmann::json& a, uint32_t capacity);

// Players > Names tab: the shown names (editedplayernames) and the name ids (players)
void names_editor(App& app, const Table& t, const PlayerRow& p);

// What the Names tab drew in its last frame (plain text is not an ImGui item, so the tests read it here)
struct NamesTabState {
    int64_t playerid = 0;
    bool has_row = false;          // he has an editedplayernames row
    std::string first, last, common, jersey;  // the four boxes
    std::string error;             // the last refusal, shown under the boxes ("" = none)
    std::string import_hint;       // "These names look changed by a preset import: ..." ("" = not shown)
    std::string common_note;       // the common name box is empty but his common name id still gives one
};
const NamesTabState& names_tab_state();

}  // namespace turbo
