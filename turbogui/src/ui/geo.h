// FC 27 LE Turbo GUI - geography of the game database for the filters: nations (name, continent = the nations table's
// confederation), leagues (name, country) and each club's league and country. One snapshot per database generation.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace turbo {

class App;

struct GeoNation {
    int64_t id = 0;
    std::string name;
    int conf = -1;   // nations.confederation, -1 = unknown
};

struct GeoLeague {
    int64_t id = 0;
    std::string name;
    int64_t nation = -1;      // leagues.countryid (a nations.nationid)
    int level = 0;
    bool international = false;
};

struct Geo {
    std::map<int64_t, GeoNation> nations;
    std::map<int64_t, GeoLeague> leagues;
    std::map<int64_t, int64_t> team_league;   // teamid -> its domestic league (lowest level, not international)
    std::map<int64_t, int64_t> team_nation;   // teamid -> nationid (clubs: the league's country; national teams: teamnationlinks)
    std::map<int, std::string> continents;    // confederation code -> label ("Europe", ...), named from well-known member nations

    std::string nation_name(int64_t nationid) const;                 // "" when unknown
    int continent_of_nation(int64_t nationid) const;                 // confederation code, -1 unknown
    std::string continent_name(int conf) const;                      // "Europe", else "Confederation <n>"
    int64_t league_of_team(int64_t teamid) const;                    // -1 none
    int64_t nation_of_team(int64_t teamid) const;                    // -1 none
    int continent_of_team(int64_t teamid) const;
    std::string league_name(int64_t leagueid) const;
    // For the filter combos, sorted by name
    std::vector<const GeoNation*> sorted_nations() const;
    std::vector<int> sorted_continents() const;
    std::vector<const GeoLeague*> sorted_leagues(int64_t nation = -1) const;   // nation >= 0: only that country's leagues
};

// Built from the open database, rebuilt when app.gen changes
const Geo& geo(App& app);

}  // namespace turbo
