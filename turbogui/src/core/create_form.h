// FC 27 LE Turbo GUI - the Create player form (2.19, ui_presets.cpp create_dialog): every box the dialog shows, read from the
// template's database row when the dialog opens, and the overrides of a Create click: only the boxes the user CHANGED. Lua's
// create_player (features/create_player.lua) copies the template's whole players row and its editedplayernames row first, so a
// box left as it was is the template's exact value without being sent. Header only, platform independent (native tests).
#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/overall.h"
#include "nlohmann/json.hpp"

namespace turbo {
namespace create_form {

// The players fields of the Profile section, in the dialog's order (a field this game's table lacks is not shown)
inline const std::vector<const char*>& profile_fields() {
    static const std::vector<const char*> v = {"overallrating",      "potential",          "preferredposition1", "preferredposition2",
                                               "preferredposition3", "preferredposition4", "nationality",        "preferredfoot",
                                               "height",             "weight"};
    return v;
}

// The four names the game shows (ui_names.h shown_name: his editedplayernames row, else his name ids' texts)
struct Names {
    std::string first, last, common, jersey;
    bool operator==(const Names& o) const { return first == o.first && last == o.last && common == o.common && jersey == o.jersey; }
    bool operator!=(const Names& o) const { return !(*this == o); }
};

struct Form {
    int64_t template_pid = 0;
    Names names;
    bool has_birthdate = false;
    GameDate birth{};                        // day precision
    std::map<std::string, int64_t> ints;     // every profile field and attribute the players table has (integer fields)
    bool has(const std::string& f) const { return ints.count(f) != 0; }
    int64_t get(const std::string& f, int64_t def = 0) const {
        auto it = ints.find(f);
        return it == ints.end() ? def : it->second;
    }
};

// Every integer field of `players` the form shows: the profile fields, then the 34 attributes
inline std::vector<std::string> shown_fields(const Table& players) {
    std::vector<std::string> out;
    auto add = [&](const char* f) {
        const Field* fl = players.field(f);
        if (fl && fl->type == FieldType::Int) out.emplace_back(f);
    };
    for (const char* f : profile_fields()) add(f);
    for (int i = 0; i < overall::kAttrCount; ++i) add(overall::kAttrNames[i]);
    return out;
}

// The form of a template player: his row as the database holds it now
inline Form load(Database& db, const Table& players, uint64_t rec, int64_t pid, const Names& names) {
    Form f;
    f.template_pid = pid;
    f.names = names;
    for (const std::string& name : shown_fields(players)) f.ints[name] = db.get_int(players, rec, name, 0);
    if (const Field* b = players.field("birthdate"); b && b->type == FieldType::Int) {
        f.has_birthdate = true;
        f.birth = date_from_gregorian_days(db.get_int(players, rec, "birthdate", 0));
    }
    return f;
}

// What a Create click sends on top of the template: "set" = the changed players fields; "names" = the four shown names when
// any of them changed (the whole set, so that Lua's "the shirt follows a new surname" rule never rewrites a shirt name the user
// sees in the box); "clear_names" = names the user emptied that Lua must remove (only the common name: an empty first name,
// surname or shirt name keeps the template's). `attrs_from_archetype`: the 34 attributes previewed by an archetype, sent as a
// whole (choosing an archetype is the user's change).
struct Overrides {
    nlohmann::json set = nlohmann::json::object();
    nlohmann::json names = nlohmann::json::object();
    std::vector<std::string> clear_names;
    std::string error;  // a box the click cannot send (an impossible date): nothing is sent
};

inline bool same_date(const GameDate& a, const GameDate& b) { return a.year == b.year && a.month == b.month && a.day == b.day; }

inline Overrides diff(const Form& orig, const Form& cur, const overall::Attrs* attrs_from_archetype = nullptr) {
    Overrides o;
    for (const auto& kv : cur.ints) {
        if (attrs_from_archetype && overall::attr_index(kv.first) >= 0) continue;
        auto it = orig.ints.find(kv.first);
        if (it == orig.ints.end() || it->second != kv.second) o.set[kv.first] = kv.second;
    }
    if (attrs_from_archetype)
        for (int i = 0; i < overall::kAttrCount; ++i)
            if (cur.has(overall::kAttrNames[i])) o.set[overall::kAttrNames[i]] = (*attrs_from_archetype)[static_cast<size_t>(i)];
    if (cur.has_birthdate && !same_date(orig.birth, cur.birth)) {
        if (!cur.birth.valid() || !is_real_date(cur.birth)) {
            char b[64];
            std::snprintf(b, sizeof(b), "%04d-%02d-%02d is not a real date", cur.birth.year, cur.birth.month, cur.birth.day);
            o.error = b;
            return o;
        }
        o.set["birthdate"] = gregorian_days_from_date(cur.birth);
    }
    if (cur.names != orig.names) {
        auto put = [&](const char* key, const std::string& now) {
            if (!now.empty()) o.names[key] = now;
        };
        put("firstname", cur.names.first);
        put("surname", cur.names.last);
        put("commonname", cur.names.common);
        put("playerjerseyname", cur.names.jersey);
        if (cur.names.common.empty() && !orig.names.common.empty()) o.clear_names.push_back("commonname");
    }
    return o;
}

// The overrides of create_player for this form: source = the template, the club, and only what changed
inline nlohmann::json command(const Form& orig, const Overrides& d, int64_t teamid) {
    nlohmann::json o = {{"source", {{"playerid", orig.template_pid}}}, {"teamid", teamid}, {"set", d.set}};
    if (!d.names.empty()) o["names"] = d.names;
    if (!d.clear_names.empty()) o["clear_names"] = d.clear_names;
    return o;
}

// A player from outside (a CMTracker row, a preset file) that is already in this database: the same player id, else exactly one
// player with the same shown name and the same birth date. 0 = none. `rows` = (playerid, shown name, birthdate days).
struct DbPlayer {
    int64_t playerid = 0;
    std::string name;
    int64_t birthdate = 0;
};
inline std::string trim_name(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}
inline int64_t match(const std::vector<DbPlayer>& rows, int64_t id, const std::string& name, int64_t birthdate) {
    if (id > 0)
        for (const DbPlayer& r : rows)
            if (r.playerid == id) return r.playerid;
    const std::string n = trim_name(name);
    if (n.empty() || birthdate <= 0) return 0;
    int64_t found = 0;
    for (const DbPlayer& r : rows)
        if (r.birthdate == birthdate && trim_name(r.name) == n) {
            if (found) return 0;  // two players with that name and birth date: not one match
            found = r.playerid;
        }
    return found;
}

}  // namespace create_form
}  // namespace turbo
