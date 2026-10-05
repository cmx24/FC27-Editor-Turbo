// FC 27 LE Turbo GUI - club filter for lists: a combo whose popup starts with a search box over the teams list.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace turbo {

class App;
struct TeamRow;

// Does the club match the typed text (lower case): part of its name, or the start of its ID. Empty text matches all.
bool team_matches(const TeamRow& t, const std::string& lower_query);

// Combo "Any club" / one club (teamid 0 = any). Typing in the popup narrows the list; Enter picks the first match.
// Returns true when the pick changed. With `nation` / `league` (both optional, -1 = any) the popup also has "Any country" /
// "Any league" combos that narrow the club list (geo.h: a club's country and domestic league).
bool team_filter_combo(App& app, const char* id, int64_t& teamid, char* search, size_t search_size, float width,
                       int64_t* nation = nullptr, int64_t* league = nullptr);

// ---- geography filters (country / league / continent): searchable combos over geo.h. -1 = "Any ...".
struct GeoOption {
    int64_t id = -1;
    std::string name;
};
// A combo with a search box over the options `fill` provides (only called while the popup is open); Enter picks the first
// match. Returns true when the pick changed.
bool geo_pick_combo(const char* id, const char* any_label, const std::string& preview, int64_t& sel, float width,
                    const std::function<void(std::vector<GeoOption>&)>& fill);
// True when a geo combo's popup is open this frame (a picker with its own keyboard handling skips its keys then)
bool geo_popup_open();
// `only`: when given, just those nations are offered (the ones a list actually has)
bool country_filter_combo(App& app, const char* id, int64_t& nation, float width, const std::vector<int64_t>* only = nullptr);
bool league_filter_combo(App& app, const char* id, int64_t& league, int64_t nation, float width);  // nation >= 0: its leagues only
bool continent_filter_combo(App& app, const char* id, int64_t& conf, float width);
// The club passes the country / league filters (-1 = any)
bool team_in_geo(App& app, const TeamRow& t, int64_t nation, int64_t league);

}  // namespace turbo
