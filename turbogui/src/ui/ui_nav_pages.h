// FC 27 LE Turbo GUI - Players > Commentary names (2.22 G-ia): what the last frame of the page drew, for the native tests (plain text and a
// filtered list are not ImGui items). The page itself is registered in ui_nav_pages.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace turbo {

struct CommentaryNamesState {
    size_t listed = 0;              // players in the list after the search, the club and the "No callname yet" filter
    size_t all = 0;                 // players before the filters
    int64_t first_listed = 0;       // the player id on the first row of the list (after the sort)
    int64_t editor_playerid = 0;    // the player the callname editor was drawn for (0 = nobody picked)
    bool editor_shown = false;      // callname_editor ran in the last frame
    bool callnames_ready = false;   // the callname index was built (the column and the filter are meaningful)
    std::string now_of_first;       // the "Callname now" text of the first listed row
    std::string now_of_selected;    // the same for the picked player
};
const CommentaryNamesState& commentary_names_state();

}  // namespace turbo
