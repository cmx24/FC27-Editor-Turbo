// FC 27 LE Turbo GUI - the searchable competition picker of the Competitions tab (Live standings, Match setup, Career
// database copy). Defined in ui_competitions.cpp; the list logic is core/comp_list.h.
#pragma once
#include <set>
#include <string>
#include <vector>

#include "core/comp_list.h"

namespace turbo {

class App;

struct CompPicker {
    const char* settings_key;  // gui_settings.json competitions.<key>: the last choice, "leagues only", the sort
    char search[96] = "";
    comps::View view;
    bool loaded = false;   // view options read from gui_settings.json
    std::set<int> open;    // competitions (parent_key) drawn open
    int cursor = -1;       // keyboard row of the open list
    bool refocus = false;  // Esc cleared the search: the box takes the keyboard again
    int64_t f_nation = -1; // country filter (nations.nationid), -1 = any country; remembered in gui_settings.json
    int64_t f_conf = -1;   // continent filter (nations.confederation), -1 = any continent; remembered too
    explicit CompPicker(const char* key) : settings_key(key) {}
};

// The picker: a combo whose list has a search box (focused when it opens; Up / Down / Enter / Escape, Right / Left open
// and close a competition's stages), "Leagues only", the sort and the competitions grouped by section. `selected` is an
// entry index (-1 = none, or the `all_label` row when given). true when the user picked another entry; the choice is
// remembered in gui_settings.json.
bool comp_picker(App& app, CompPicker& p, const char* label, const std::vector<comps::Entry>& entries, int& selected, float width,
                 const char* all_label = nullptr);
// The remembered entry of this picker in `entries`, or -1
int comp_picker_restore(App& app, const CompPicker& p, const std::vector<comps::Entry>& entries);
// Leagues (name, level, country) and nations of the database, rebuilt when app.gen changes
const comps::NameSources& comp_name_sources(App& app);

}  // namespace turbo
