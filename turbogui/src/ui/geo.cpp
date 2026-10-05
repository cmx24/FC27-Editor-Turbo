// FC 27 LE Turbo GUI - geography of the game database (geo.h)
#include "ui/geo.h"

#include <algorithm>
#include <cctype>

#include "app.h"

namespace turbo {

std::string Geo::nation_name(int64_t id) const {
    auto it = nations.find(id);
    return it == nations.end() ? std::string() : it->second.name;
}

int Geo::continent_of_nation(int64_t id) const {
    auto it = nations.find(id);
    return it == nations.end() ? -1 : it->second.conf;
}

std::string Geo::continent_name(int conf) const {
    auto it = continents.find(conf);
    return it != continents.end() ? it->second : "Confederation " + std::to_string(conf);
}

int64_t Geo::league_of_team(int64_t teamid) const {
    auto it = team_league.find(teamid);
    return it == team_league.end() ? -1 : it->second;
}

int64_t Geo::nation_of_team(int64_t teamid) const {
    auto it = team_nation.find(teamid);
    return it == team_nation.end() ? -1 : it->second;
}

int Geo::continent_of_team(int64_t teamid) const { return continent_of_nation(nation_of_team(teamid)); }

std::string Geo::league_name(int64_t id) const {
    auto it = leagues.find(id);
    return it == leagues.end() ? std::string() : it->second.name;
}

static std::string lower_s(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<const GeoNation*> Geo::sorted_nations() const {
    std::vector<const GeoNation*> v;
    for (auto& n : nations)
        if (!n.second.name.empty()) v.push_back(&n.second);
    std::sort(v.begin(), v.end(), [](const GeoNation* a, const GeoNation* b) { return lower_s(a->name) < lower_s(b->name); });
    return v;
}

std::vector<int> Geo::sorted_continents() const {
    std::vector<int> v;
    for (auto& n : nations)
        if (n.second.conf >= 0 && std::find(v.begin(), v.end(), n.second.conf) == v.end()) v.push_back(n.second.conf);
    std::sort(v.begin(), v.end(), [&](int a, int b) { return continent_name(a) < continent_name(b); });
    return v;
}

std::vector<const GeoLeague*> Geo::sorted_leagues(int64_t nation) const {
    std::vector<const GeoLeague*> v;
    for (auto& l : leagues)
        if (!l.second.name.empty() && (nation < 0 || l.second.nation == nation)) v.push_back(&l.second);
    std::sort(v.begin(), v.end(), [](const GeoLeague* a, const GeoLeague* b) { return lower_s(a->name) < lower_s(b->name); });
    return v;
}

const Geo& geo(App& app) {
    static Geo g;
    static int built_gen = -1;
    if (built_gen == app.gen) return g;
    built_gen = app.gen;
    g = Geo();
    if (const Table* nt = app.db.table("nations")) {
        Snapshot ns;
        const Field* nf = nt->field("nationname");
        if (nf && ns.load(app.db.memory(), *nt))
            for (uint32_t r : ns.valid) {
                GeoNation n;
                n.id = ns.get_int(r, "nationid", -1);
                if (n.id < 0) continue;
                n.name = ns.get_str(r, *nf);
                n.conf = static_cast<int>(ns.get_int(r, "confederation", -1));
                g.nations[n.id] = n;
            }
    }
    if (const Table* lt = app.db.table("leagues")) {
        Snapshot ls;
        const Field* nf = lt->field("leaguename");
        if (ls.load(app.db.memory(), *lt))
            for (uint32_t r : ls.valid) {
                GeoLeague l;
                l.id = ls.get_int(r, "leagueid", -1);
                if (l.id < 0) continue;
                if (nf) l.name = ls.get_str(r, *nf);
                l.nation = ls.get_int(r, "countryid", -1);
                l.level = static_cast<int>(ls.get_int(r, "level", 0));
                l.international = ls.get_int(r, "isinternationalleague", 0) != 0;
                g.leagues[l.id] = l;
            }
    }
    // a club's domestic league: its non-international league with the lowest level
    if (const Table* tl = app.db.table("leagueteamlinks")) {
        Snapshot ts;
        if (ts.load(app.db.memory(), *tl))
            for (uint32_t r : ts.valid) {
                const int64_t team = ts.get_int(r, "teamid", -1), league = ts.get_int(r, "leagueid", -1);
                auto lg = g.leagues.find(league);
                if (team < 0 || lg == g.leagues.end() || lg->second.international) continue;
                auto cur = g.team_league.find(team);
                if (cur == g.team_league.end() || lg->second.level < g.leagues[cur->second].level) g.team_league[team] = league;
            }
    }
    for (auto& tl : g.team_league) {
        auto lg = g.leagues.find(tl.second);
        if (lg != g.leagues.end() && lg->second.nation >= 0) g.team_nation[tl.first] = lg->second.nation;
    }
    if (const Table* tn = app.db.table("teamnationlinks")) {   // national teams, and any club the game ties to a nation
        Snapshot ns;
        if (ns.load(app.db.memory(), *tn))
            for (uint32_t r : ns.valid) {
                const int64_t team = ns.get_int(r, "teamid", -1), nation = ns.get_int(r, "nationid", -1);
                if (team >= 0 && nation >= 0 && !g.team_nation.count(team)) g.team_nation[team] = nation;
            }
    }
    // continent labels: the confederation code is the game's; its name is read off well-known members, so no code table
    // has to be guessed (England / France / Germany -> Europe, Brazil / Argentina -> South America, ...)
    static const std::pair<const char*, const char*> kAnchors[] = {
        {"England", "Europe"}, {"France", "Europe"}, {"Germany", "Europe"}, {"Brazil", "South America"},
        {"Argentina", "South America"}, {"Mexico", "North & Central America"}, {"United States", "North & Central America"},
        {"Nigeria", "Africa"}, {"Egypt", "Africa"}, {"Senegal", "Africa"}, {"Japan", "Asia"}, {"Korea Republic", "Asia"},
        {"Saudi Arabia", "Asia"}, {"New Zealand", "Oceania"}};
    std::map<int, std::map<std::string, int>> votes;
    for (auto& n : g.nations)
        for (auto& a : kAnchors)
            if (n.second.conf >= 0 && n.second.name == a.first) ++votes[n.second.conf][a.second];
    for (auto& v : votes) {
        auto best = std::max_element(v.second.begin(), v.second.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        g.continents[v.first] = best->first;
    }
    // two codes can vote for one label: suffix the code so two entries never look the same
    std::map<std::string, int> seen;
    for (auto& c : g.continents) ++seen[c.second];
    for (auto& c : g.continents)
        if (seen[c.second] > 1) c.second += " (" + std::to_string(c.first) + ")";
    return g;
}

}  // namespace turbo
