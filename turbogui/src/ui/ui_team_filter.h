// FC 27 LE Turbo GUI - club filter for lists: a combo whose popup starts with a search box over the teams list.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace turbo {

class App;
struct TeamRow;

// Does the club match the typed text (lower case): part of its name, or the start of its ID. Empty text matches all.
bool team_matches(const TeamRow& t, const std::string& lower_query);

// Combo "Any club" / one club (teamid 0 = any). Typing in the popup narrows the list; Enter picks the first match.
// Returns true when the pick changed.
bool team_filter_combo(App& app, const char* id, int64_t& teamid, char* search, size_t search_size, float width);

}  // namespace turbo
