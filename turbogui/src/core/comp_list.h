// FC 27 LE Turbo GUI - the competition pickers of the Competitions tab: readable names, kinds, countries, grouping of a
// competition's internal stages under it, search and sorting. Pure (no ImGui, no memory): the native tests run it.
//
// Names (1.1.1): FC 27 keeps no competition name in the database except leagues.leaguename, and Live Editor 27.1.2 has
// neither GetGameLocString nor GetCompetitionNameByObjID (globals dump), so a cup / continental competition such as the
// tree node "C223" (TrophyName_Abbr15_223) has no name Turbo can read. Order used: leagues.leaguename, then Turbo's
// built-in list below (FC's competition ids, the same in FC 26's compobj.txt), then a label built from the tree
// ("Italy cup 210", "UEFA competition 980"). Countries come from the tree's nation node ("NationName_27" -> nations
// table) or its confederation ("UEFA"); the kind from the built-in list, the leagues table and the tree's shape.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fce_standings.h"

namespace turbo {
namespace comps {

// Section order of the pickers: your club's competitions, then leagues, cups, continental, the rest
enum class Kind { League = 0, Cup, SuperCup, Playoff, Continental, International, Friendly, Other };
inline const char* kind_word(Kind k) {
    switch (k) {
        case Kind::League: return "league";
        case Kind::Cup: return "cup";
        case Kind::SuperCup: return "super cup";
        case Kind::Playoff: return "playoff";
        case Kind::Continental: return "continental";
        case Kind::International: return "international";
        case Kind::Friendly: return "friendly";
        default: return "other";
    }
}
enum Section { kSectionYours = 0, kSectionLeagues, kSectionCups, kSectionContinental, kSectionOther, kSectionCount };
inline const char* section_title(int s) {
    static const char* t[] = {"Your club's competitions", "Leagues", "Cups", "Continental", "Other"};
    return s >= 0 && s < kSectionCount ? t[s] : "";
}
inline int section_of(Kind k) {
    switch (k) {
        case Kind::League: return kSectionLeagues;
        case Kind::Cup: case Kind::SuperCup: case Kind::Playoff: return kSectionCups;
        case Kind::Continental: return kSectionContinental;
        default: return kSectionOther;
    }
}

// Turbo's built-in names of FC's non-league competitions (leagues are named by the database). Ids from the competition
// tree (C<id>); kinds as FC's competition settings give them (CUP / SUPERCUP / PLAYOFF / INTERCUP ...).
struct Known {
    int id;
    const char* name;
    Kind kind;
};
inline const std::vector<Known>& known_competitions() {
    static const std::vector<Known> k = {
        {223, "UEFA Champions League", Kind::Continental}, {224, "UEFA Europa League", Kind::Continental},
        {226, "UEFA Conference League", Kind::Continental}, {227, "UEFA Women's Champions League", Kind::Continental},
        {232, "UEFA Super Cup", Kind::Continental}, {980, "UEFA club competition", Kind::Continental},
        {981, "UEFA qualifying", Kind::Continental},
        {1003, "CONMEBOL Libertadores", Kind::Continental}, {1014, "CONMEBOL Sudamericana", Kind::Continental},
        {1015, "CONMEBOL Recopa", Kind::Continental}, {3005, "AFC Champions League", Kind::Continental},
        {990, "Intercontinental club cup", Kind::International}, {2136, "International club cup", Kind::International},
        {900, "Friendlies", Kind::Friendly}, {901, "International friendlies", Kind::Friendly},
        {201, "FA Cup", Kind::Cup}, {202, "Carabao Cup", Kind::Cup}, {203, "EFL Trophy", Kind::Cup},
        {228, "FA Community Shield", Kind::SuperCup}, {414, "Championship play-off", Kind::Playoff},
        {360, "League One play-off", Kind::Playoff}, {361, "League Two play-off", Kind::Playoff},
        {204, "Coupe de France", Kind::Cup}, {233, "Trophee des Champions", Kind::SuperCup}, {451, "Ligue 1 play-off", Kind::Playoff},
        {206, "DFB-Pokal", Kind::Cup}, {207, "DFL-Supercup", Kind::SuperCup}, {21, "Bundesliga relegation play-off", Kind::Playoff},
        {22, "2. Bundesliga relegation play-off", Kind::Playoff},
        {210, "Coppa Italia", Kind::Cup}, {211, "Supercoppa Italiana", Kind::SuperCup}, {33, "Serie B play-off", Kind::Playoff},
        {208, "Copa del Rey", Kind::Cup}, {225, "Supercopa de Espana", Kind::SuperCup}, {354, "LaLiga 2 play-off", Kind::Playoff},
        {314, "KNVB Beker", Kind::Cup}, {218, "Taca de Portugal", Kind::Cup}, {219, "Scottish Cup", Kind::Cup},
        {213, "Belgian Cup", Kind::Cup}, {214, "Belgian Pro League play-off", Kind::Playoff},
        {212, "OFB Cup", Kind::Cup}, {81, "Austrian Bundesliga play-off", Kind::Playoff},
        {215, "Danish Cup", Kind::Cup}, {216, "Danish Superliga play-off", Kind::Playoff},
        {365, "FAI Cup", Kind::Cup}, {217, "Norwegian Cup", Kind::Cup}, {366, "Polish Cup", Kind::Cup},
        {1004, "Romanian Cup", Kind::Cup}, {5060, "Romanian SuperLiga play-off", Kind::Playoff},
        {220, "Svenska Cupen", Kind::Cup}, {221, "Swiss Cup", Kind::Cup}, {268, "Turkish Cup", Kind::Cup},
        {234, "MLS Cup", Kind::Playoff},
    };
    return k;
}
inline const Known* known(int id) {
    for (const Known& k : known_competitions())
        if (k.id == id) return &k;
    return nullptr;
}

inline std::string confederation_region(const std::string& short_name) {
    if (short_name == "UEFA") return "Europe (UEFA)";
    if (short_name == "CNBL") return "South America (CONMEBOL)";
    if (short_name == "CCAF") return "North America (CONCACAF)";
    if (short_name == "AFC") return "Asia (AFC)";
    if (short_name == "CAF") return "Africa (CAF)";
    if (short_name == "OFC") return "Oceania (OFC)";
    return short_name;
}

// What the database knows: leagues (name, level, country) and nations (name)
struct NameSources {
    struct League {
        std::string name;
        int level = 0;
        int64_t country = -1;
    };
    std::map<int64_t, League> leagues;
    std::map<int64_t, std::string> nations;
    std::string nation(int64_t id) const {
        auto it = nations.find(id);
        return it == nations.end() ? std::string() : it->second;
    }
};

// Where a node of the competition tree sits (its competition, stage, group, nation / confederation)
struct TreeInfo {
    bool found = false;
    int comp = -1;           // competition number (C<id>), -1 unknown
    int comp_node = -1;      // the competition's node id
    std::string stage_desc;  // "FCE_League_Stage", "FCE_Setup_Stage", "FCE_Round_of_16_Pots" ...
    int stage_node = -1;
    std::string group_short; // "G1"
    int64_t nation_id = -1;  // from "NationName_27"
    std::string nation_short, confed_short;
    bool under_root = false; // the competition hangs straight under the root (friendlies, intercontinental cups)
};
inline TreeInfo tree_info(const std::vector<fce::CompObj>& objs, uint16_t node) {
    TreeInfo out;
    if (node >= objs.size() || objs[node].used != 1 || objs[node].id != node) return out;
    out.found = true;
    uint16_t cur = node;
    for (int depth = 0; depth < 10; ++depth) {
        const fce::CompObj& c = objs[cur];
        if (c.type == fce::kCompTypeGroup && out.group_short.empty()) out.group_short = c.short_name;
        else if (c.type == fce::kCompTypeStage && out.stage_node < 0) out.stage_node = cur, out.stage_desc = c.desc;
        else if (c.type == fce::kCompTypeCompetition && out.comp_node < 0) {
            out.comp_node = cur;
            out.comp = c.comp_number();
            const uint16_t p = c.parent;
            out.under_root = p < objs.size() && objs[p].type == fce::kCompTypeRoot;
        } else if (c.type == fce::kCompTypeNation && out.nation_short.empty()) {
            out.nation_short = c.short_name;
            if (c.desc.rfind("NationName_", 0) == 0) {
                const std::string n = c.desc.substr(11);
                if (!n.empty() && n.size() < 9 && n.find_first_not_of("0123456789") == std::string::npos) out.nation_id = std::stoll(n);
            }
        } else if (c.type == 1 && out.confed_short.empty()) {
            out.confed_short = c.short_name;  // type 1: confederation ("UEFA", "CNBL")
        }
        const uint16_t parent = c.parent;
        if (parent == 0xFFFF || parent == cur || parent >= objs.size() || objs[parent].used != 1 || objs[parent].id != parent) break;
        cur = parent;
    }
    return out;
}

// "FCE_Setup_Stage" -> "setup stage", "FCE_Round_of_16_Pots" -> "round of 16 pots"
inline std::string stage_words(const std::string& desc) {
    std::string w = desc.rfind("FCE_", 0) == 0 ? desc.substr(4) : desc;
    for (char& c : w) {
        if (c == '_') c = ' ';
        else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return w;
}

// One pickable table (a standings group of the game, a league of the database copy, a competition of your fixtures)
struct Entry {
    int64_t key = -1;          // what the caller selects (group node id, league id, ...)
    int comp = -1;             // competition number, -1 unknown
    int parent_key = -1;       // entries with the same parent_key are one competition (stages collapsed under it)
    std::string name;          // "UEFA Champions League"
    std::string country;       // "Italy", "Europe (UEFA)"
    int64_t nation = -1;       // nations.nationid of the country (tree nation, else the league's), -1 = none (continental, world)
    std::string stage;         // "" = the competition's own table; else "round of 16 pots", "setup stage G2"
    std::string note;          // "shown by the game", "not shown by the game"
    Kind kind = Kind::Other;
    int level = 0;             // league tier (leagues.level), 0 unknown
    int clubs = 0;
    bool main_stage = false;   // the competition's table (league stage)
    bool setup_stage = false;  // a cup's pool: never shown as a table by the game
    bool user = false;         // your club plays in it
    bool shown = false;        // the game's Standings screen shows it
    bool named = false;        // the name is real (database or built-in), not built from the id
};

inline Kind kind_from(const NameSources& src, int comp, const TreeInfo* t) {
    if (const Known* k = known(comp)) return k->kind;
    if (comp >= 0 && src.leagues.count(comp)) return Kind::League;
    if (t && t->found) {
        if (!t->confed_short.empty() && t->nation_short.empty()) return Kind::Continental;
        if (t->under_root) return Kind::International;
        if (!t->nation_short.empty()) return Kind::Cup;
    }
    return Kind::Other;
}

// Name, country, kind and stage of a tree node (a standings group or a fixture's node). `clubs` is filled by the caller.
inline Entry describe(const NameSources& src, const TreeInfo& t, int64_t key) {
    Entry e;
    e.key = key;
    e.comp = t.comp;
    e.parent_key = t.comp >= 0 ? t.comp : (t.comp_node >= 0 ? 1000000 + t.comp_node : int(-1 - key));
    e.kind = kind_from(src, t.comp, &t);
    auto lg = t.comp >= 0 ? src.leagues.find(t.comp) : src.leagues.end();
    if (lg != src.leagues.end()) {
        e.level = lg->second.level;
        if (!lg->second.name.empty()) e.name = lg->second.name, e.named = true;
    }
    if (e.name.empty())
        if (const Known* k = known(t.comp)) e.name = k->name, e.named = true;
    // country: the tree's nation (nations table, else its short code), its confederation, the league's country
    if (t.nation_id >= 0) e.country = src.nation(t.nation_id);
    if (e.country.empty() && !t.nation_short.empty()) e.country = t.nation_short;
    if (e.country.empty() && !t.confed_short.empty()) e.country = confederation_region(t.confed_short);
    if (e.country.empty() && lg != src.leagues.end() && lg->second.country >= 0) e.country = src.nation(lg->second.country);
    if (e.country.empty() && t.under_root) e.country = "World";
    e.nation = t.nation_id >= 0 ? t.nation_id : (lg != src.leagues.end() ? lg->second.country : -1);
    if (e.name.empty()) {
        // no name anywhere: "Italy cup 210", "UEFA competition 980", "Competition 5000"
        const std::string where = !t.confed_short.empty() && t.nation_short.empty() ? t.confed_short : e.country;
        const bool domestic = e.kind == Kind::League || e.kind == Kind::Cup || e.kind == Kind::SuperCup || e.kind == Kind::Playoff;
        const std::string id = t.comp >= 0 ? std::to_string(t.comp) : "group " + std::to_string(key);
        e.name = where.empty() ? "Competition " + id : where + " " + (domestic ? kind_word(e.kind) : "competition") + " " + id;
    }
    e.main_stage = t.stage_desc == "FCE_League_Stage";
    e.setup_stage = t.stage_desc.rfind("FCE_Setup_Stage", 0) == 0;
    if (!t.stage_desc.empty() && !e.main_stage) e.stage = stage_words(t.stage_desc);
    return e;
}

// Several groups in one stage ("group stage" G1..G8): the group's short name tells them apart
inline void tag_groups(std::vector<Entry>& entries, const std::vector<std::string>& group_shorts) {
    std::map<std::pair<int, std::string>, int> count;
    for (const Entry& e : entries) ++count[{e.parent_key, e.stage}];
    for (size_t i = 0; i < entries.size() && i < group_shorts.size(); ++i)
        if (count[{entries[i].parent_key, entries[i].stage}] > 1 && !group_shorts[i].empty())
            entries[i].stage += (entries[i].stage.empty() ? "" : " ") + group_shorts[i];
}

// One competition with its tables: `primary` is the table the competition's row selects, `children` the internal stages
struct Parent {
    int parent_key = -1;
    size_t primary = 0;
    std::vector<size_t> children;  // other entries of the competition (stages), in stage order
    bool user = false, shown = false;
    int clubs = 0;
};

// The table a competition's row picks: the one the game shows, else the league stage, else the biggest non-setup
// table, else the first
inline std::vector<Parent> build_parents(const std::vector<Entry>& entries) {
    std::vector<Parent> out;
    std::map<int, size_t> at;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto it = at.find(entries[i].parent_key);
        if (it == at.end()) {
            at[entries[i].parent_key] = out.size();
            Parent p;
            p.parent_key = entries[i].parent_key;
            p.primary = i;
            out.push_back(p);
        } else {
            out[it->second].children.push_back(i);
        }
    }
    auto score = [&](size_t i) {
        const Entry& e = entries[i];
        return (e.shown ? 1000000 : 0) + (e.main_stage ? 100000 : 0) + (e.stage.empty() ? 50000 : 0) + (e.setup_stage ? 0 : 10000) + e.clubs;
    };
    for (Parent& p : out) {
        std::vector<size_t> all = p.children;
        all.push_back(p.primary);
        size_t best = p.primary;
        for (size_t i : all)
            if (score(i) > score(best) || (score(i) == score(best) && i < best)) best = i;
        p.children.clear();
        for (size_t i : all)
            if (i != best) p.children.push_back(i);
        std::sort(p.children.begin(), p.children.end(), [&](size_t a, size_t b) {
            if (entries[a].stage != entries[b].stage) return entries[a].stage < entries[b].stage;
            return entries[a].key < entries[b].key;
        });
        p.primary = best;
        for (size_t i : all) {
            p.user = p.user || entries[i].user;
            p.shown = p.shown || entries[i].shown;
            p.clubs = std::max(p.clubs, entries[i].clubs);
        }
    }
    return out;
}

// Lower case, Latin-1 accents folded (UTF-8 "Süper Lig" -> "super lig"), for the search
inline std::string fold(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            // U+00C0..U+00FF: upper (second byte 0x80..0x9F) and lower case (0xA0..0xBF) share one table
            const unsigned char d = static_cast<unsigned char>(s[++i]);
            static const char* map = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
            out += d == 0x9F ? 's' : map[d & 0x1F];
            continue;
        }
        out += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : char(c);
    }
    return out;
}

enum class Sort { Country = 0, Name, Clubs, Id };
struct View {
    std::string search;
    bool leagues_only = true;  // ignored while searching: the search looks at every kind
    Sort sort = Sort::Country;
    std::function<bool(const Entry&)> filter;  // optional (country / continent filters): a competition shows only when it passes
};

inline std::vector<std::string> tokens(const std::string& q) {
    std::vector<std::string> t;
    std::string cur;
    for (char c : fold(q)) {
        if (c == ' ' || c == ',' || c == '\t') {
            if (!cur.empty()) t.push_back(cur), cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) t.push_back(cur);
    return t;
}
// Every word of the search is in the entry's name, country, kind, stage, competition id or key
inline bool matches(const Entry& e, const std::vector<std::string>& toks, bool with_stage) {
    if (toks.empty()) return true;
    std::string hay = fold(e.name) + " | " + fold(e.country) + " | " + kind_word(e.kind) + " | " +
                      (e.comp >= 0 ? std::to_string(e.comp) : "") + " | " + std::to_string(e.key);
    if (with_stage) hay += " | " + fold(e.stage);
    for (const std::string& t : toks)
        if (hay.find(t) == std::string::npos) return false;
    return true;
}

// What a picker draws: sections of competitions, each competition with the stages to list under it
struct Shown {
    size_t parent = 0;                // index into parents
    std::vector<size_t> children;     // matching stages (all of them without a search)
    bool open_by_search = false;      // a stage matched the search: drawn open
    std::string group;                // the country sub-heading (leagues sorted by country), "" otherwise
};
struct Arranged {
    std::vector<Shown> sections[kSectionCount];
    size_t count() const {
        size_t n = 0;
        for (const auto& s : sections) n += s.size();
        return n;
    }
};

inline Arranged arrange(const std::vector<Entry>& entries, const std::vector<Parent>& parents, const View& v) {
    Arranged out;
    const std::vector<std::string> toks = tokens(v.search);
    const bool searching = !toks.empty();
    for (size_t pi = 0; pi < parents.size(); ++pi) {
        const Parent& p = parents[pi];
        const Entry& e = entries[p.primary];
        if (v.filter && !v.filter(e)) continue;
        Shown s;
        s.parent = pi;
        bool self = matches(e, toks, false) || (searching && matches(e, toks, true));
        for (size_t c : p.children) {
            // a stage is listed when the competition matches, or when the stage itself matches (the competition is then
            // drawn open)
            if (!searching || self) s.children.push_back(c);
            else if (matches(entries[c], toks, true)) s.children.push_back(c), s.open_by_search = true;
        }
        if (!self && !s.open_by_search) continue;
        const int sec = section_of(e.kind);
        if (p.user) out.sections[kSectionYours].push_back(s);
        if (!searching && v.leagues_only && sec != kSectionLeagues) continue;
        out.sections[sec].push_back(s);
    }
    auto less = [&](const Shown& a, const Shown& b) {
        const Entry& x = entries[parents[a.parent].primary];
        const Entry& y = entries[parents[b.parent].primary];
        const std::string nx = fold(x.name), ny = fold(y.name);
        switch (v.sort) {
            case Sort::Name:
                if (nx != ny) return nx < ny;
                break;
            case Sort::Clubs:
                if (parents[a.parent].clubs != parents[b.parent].clubs) return parents[a.parent].clubs > parents[b.parent].clubs;
                break;
            case Sort::Id:
                if (x.comp != y.comp) return x.comp < y.comp;
                break;
            case Sort::Country: {
                const std::string cx = fold(x.country), cy = fold(y.country);
                if (cx != cy) return cx.empty() != cy.empty() ? !cx.empty() : cx < cy;
                const int lx = x.level > 0 ? x.level : 99, ly = y.level > 0 ? y.level : 99;
                if (lx != ly) return lx < ly;
                break;
            }
        }
        if (nx != ny) return nx < ny;
        return x.key < y.key;
    };
    for (int s = 0; s < kSectionCount; ++s) {
        auto& list = out.sections[s];
        if (s == kSectionYours) {
            // your league first, then cups, continental, the rest
            std::stable_sort(list.begin(), list.end(), [&](const Shown& a, const Shown& b) {
                const Entry& x = entries[parents[a.parent].primary];
                const Entry& y = entries[parents[b.parent].primary];
                if (section_of(x.kind) != section_of(y.kind)) return section_of(x.kind) < section_of(y.kind);
                return less(a, b);
            });
            continue;
        }
        std::stable_sort(list.begin(), list.end(), less);
        if (s == kSectionLeagues && v.sort == Sort::Country)
            for (Shown& sh : list) sh.group = entries[parents[sh.parent].primary].country;
    }
    return out;
}

// "UEFA Champions League - round of 16 pots" (the stage only for an internal stage)
inline std::string title(const Entry& e) { return e.stage.empty() ? e.name : e.name + " - " + e.stage; }
// One-line description for the closed picker: "Serie A (Italy, league, 20 clubs)"
inline std::string summary(const Entry& e) {
    std::string s = title(e) + " (";
    if (!e.country.empty()) s += e.country + ", ";
    s += kind_word(e.kind);
    s += e.clubs > 0 ? ", " + std::to_string(e.clubs) + " clubs)" : std::string(")");
    if (!e.note.empty()) s += " [" + e.note + "]";
    return s;
}

// The remembered choice (gui_settings.json competitions.<picker>): same key and competition, else the same
// competition's stage, else the competition's own table; -1 when the competition is not in the list
inline int find_remembered(const std::vector<Entry>& entries, int64_t key, int comp, const std::string& stage) {
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].key == key && entries[i].comp == comp) return int(i);
    if (comp < 0) return -1;
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].comp == comp && entries[i].stage == stage) return int(i);
    const std::vector<Parent> ps = build_parents(entries);
    for (const Parent& p : ps)
        if (entries[p.primary].comp == comp) return int(p.primary);
    return -1;
}

}  // namespace comps
}  // namespace turbo
