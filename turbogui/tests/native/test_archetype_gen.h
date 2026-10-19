// Native tests: 2.20 R3, one archetype x one overall target x one tier fills a whole player (core/archetype_gen.h, the table
// turbo/package/turbo/data/archetypes.json, ui/ui_archetype_populate.cpp). Included by test_main.cpp: run_core() with the
// generator and table cases, run_ui() from test_ui with the Populate / Undo cases.
#pragma once
#include <bitset>
#include <fstream>
#include <set>
#include <sstream>

#include "core/archetype_gen.h"
#include "core/archetypes.h"
#include "core/field_labels.h"
#include "core/overall.h"
#include "fixture_player_codes.h"
#include "ui/ui_archetype_populate.h"

namespace archetype_gen_test {

namespace ag = turbo::archetype_gen;
namespace ar = turbo::archetypes;
namespace ov = turbo::overall;
namespace lb = turbo::labels;
using ag::Side;
using ag::Tier;

static const Tier kTiers[] = {Tier::Superstar, Tier::Starter, Tier::Rotation, Tier::Bench, Tier::Prospect};

inline size_t ix(int i) { return static_cast<size_t>(i); }

// the table as shipped (tests run from the turbogui folder)
static bool read_table_text(std::string& text) {
    std::ifstream f("../turbo/package/turbo/data/archetypes.json", std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    text = ss.str();
    return true;
}

static const ag::Table& shipped() {
    static ag::Table t;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::string text, err;
        if (read_table_text(text)) ag::parse_table(text, t, err);
    }
    return t;
}

static const std::vector<int64_t>* domain(const std::string& field) {
    for (const auto& c : kPlayerCodes)
        if (field == c.field) return &c.values;
    return nullptr;
}
static bool in_domain(const std::string& field, int64_t v) {
    const auto* d = domain(field);
    return d && std::find(d->begin(), d->end(), v) != d->end();
}

static ov::Attrs flat(int base) {
    ov::Attrs a{};
    a.fill(base);
    return a;
}
// a spread base: attributes between 30 and 80, so the walk has room both ways
static ov::Attrs spread() {
    ov::Attrs a{};
    for (int i = 0; i < ov::kAttrCount; ++i) a[ix(i)] = 30 + (i * 7 + 11) % 50;
    return a;
}

static std::string role_text(int id) {
    const auto* t = lb::named_codes("role1");
    const char* n = t ? lb::find_name(*t, id) : nullptr;
    return n ? n : "";
}

// "RB Fullback+" -> "RB"
static std::string role_pos_word(int id) {
    const std::string t = role_text(id);
    return t.substr(0, t.find(' '));
}

static int pop(int64_t v) { return static_cast<int>(std::bitset<64>(static_cast<uint64_t>(v)).count()); }

static void run_core() {
    run_case("archetype table: data/archetypes.json parses, one row per archetype in core/archetypes, every name, id and position is real", [&] {
        std::string text, err;
        CHECK(read_table_text(text), "turbo/package/turbo/data/archetypes.json is in the repository");
        ag::Table t;
        CHECK(ag::parse_table(text, t, err), "it parses: " + err);
        CHECK(t.rows.size() == ar::all().size(), "one row per archetype: " + std::to_string(t.rows.size()));
        bool names_ok = true, roles_ok = true, words_ok = true, bits_ok = true, pos_ok = true, group_ok = true, long_ok = true;
        std::string bad;
        for (const ar::Archetype& a : ar::all()) {
            const ag::Row* r = t.find(a.name);
            if (!r) {
                names_ok = false;
                bad = std::string("no row for ") + a.name;
                continue;
            }
            if (ov::group_of_position(r->positions[0]) != a.group) {
                group_ok = false;
                bad = r->name + ": first position is not in the archetype's group";
            }
            for (int p : r->positions)
                if (!lb::position_visible_code(p)) {
                    pos_ok = false;
                    bad = r->name + ": position " + std::to_string(p);
                }
            if (r->roles.size() != r->role_names.size()) {
                words_ok = false;
                bad = r->name + ": role_names do not match roles";
            }
            for (size_t i = 0; i < r->roles.size() && i < r->role_names.size(); ++i) {
                const int id = r->roles[i];
                if (role_text(id) != r->role_names[i] + "+") {
                    words_ok = false;
                    bad = r->name + ": role " + std::to_string(id) + " is '" + role_text(id) + "', the file says '" + r->role_names[i] + "'";
                }
                const std::string w = role_pos_word(id);
                bool among = false;
                for (int p : r->positions) among = among || w == lb::position_code_name(p);
                if (!among) {
                    roles_ok = false;
                    bad = r->name + ": role " + std::to_string(id) + " (" + role_text(id) + ") is not for one of his positions";
                }
                // each prefix of the list stays inside the squad file's value set of its slot (role1..role4)
                if (!in_domain("role" + std::to_string(i + 1), id) || (i == 0 && !in_domain("role1", id + 100) && id != 7 && id != 23)) {
                    roles_ok = false;
                    bad = r->name + ": role " + std::to_string(id) + " in slot " + std::to_string(i + 1) + " is outside the squad file's values";
                }
            }
            for (const ag::StyleBit& b : r->style_bits) bits_ok = bits_ok && b.bit >= 0 && b.bit < (b.second ? 17 : 30);
            for (const ag::StyleBit& b : r->trait_bits) bits_ok = bits_ok && b.second && b.bit != 11;
            if (r->playstyles.size() < (r->positions[0] == 0 ? 6u : 9u)) {
                long_ok = false;
                bad = r->name + ": the PlayStyle list is too short";
            }
        }
        CHECK(names_ok, "every archetype has a row: " + bad);
        CHECK(group_ok, "the first position is in the archetype's group: " + bad);
        CHECK(pos_ok, "positions are codes the game names: " + bad);
        CHECK(words_ok, "role_names are the game's own role names: " + bad);
        CHECK(roles_ok, "roles belong to the row's positions and to the squad file's values: " + bad);
        CHECK(bits_ok, "PlayStyle and trait bits are inside their fields, no Injury Prone");
        CHECK(long_ok, "PlayStyle lists are long enough (9 outfield, 6 goalkeeper): " + bad);
    });

    run_case("archetype table: a broken file is refused with a reason", [&] {
        std::string text, err;
        CHECK(read_table_text(text), "table");
        ag::Table t;
        auto replace = [&](const std::string& from, const std::string& to) {
            std::string s = text;
            const size_t at = s.find(from);
            if (at != std::string::npos) s.replace(at, from.size(), to);
            return s;
        };
        CHECK(!ag::parse_table("{}", t, err) && !err.empty() && t.rows.empty(), "no list: refused");
        CHECK(!ag::parse_table(replace("\"Finesse Shot\"", "\"Finesse Shotz\""), t, err) && err.find("Finesse Shotz") != std::string::npos, "unknown PlayStyle: " + err);
        CHECK(!ag::parse_table(replace("\"Leadership\"", "\"Injury Prone\""), t, err) && err.find("Injury Prone") != std::string::npos, "Injury Prone is never given: " + err);
        CHECK(!ag::parse_table(replace("\"roles\": [42, 41]", "\"roles\": [42, 3]"), t, err) && err.find("role 3") != std::string::npos, "a role of another position: " + err);
        CHECK(!ag::parse_table(replace("\"roles\": [42, 41]", "\"roles\": [142, 41]"), t, err), "a ++ id in the table: " + err);
        CHECK(!ag::parse_table(replace("\"positions\": [25]", "\"positions\": [25, 1]"), t, err), "SW is not a position the game names: " + err);
        CHECK(!ag::parse_table(replace("\"name\": \"Poacher\"", "\"name\": \"Target Man\""), t, err) && err.find("twice") != std::string::npos, "a name twice: " + err);
        CHECK(!ag::parse_table(replace("\"GK Far Reach\"", "\"Rapid\""), t, err), "an outfield PlayStyle on a keeper: " + err);
        CHECK(ag::parse_table(text, t, err) && t.rows.size() == 32, "the real file still parses after those tries");
        CHECK(!ag::load_table(std::filesystem::path("this-folder-does-not-exist"), t, err) && err.find("not installed") != std::string::npos, "no file: " + err);
    });

    run_case("archetype gen: Left mirrors positions and roles; the mirror of the mirror is the original", [&] {
        const ag::Table& t = shipped();
        CHECK(t.rows.size() == 32, "table loaded");
        bool twice = true, same_word = true;
        for (int id = 1; id <= 149; ++id) {
            if (id >= 50 && id < 101) continue;
            if (ag::mirror_role(ag::mirror_role(id)) != id) twice = false;
        }
        for (int p = 0; p < 28; ++p)
            if (ag::mirror_position(ag::mirror_position(p)) != p) twice = false;
        CHECK(twice, "mirror(mirror(x)) == x for every role id and position code");
        // a mirrored role id reads the same words on the other side: "RB Wingback+" <-> "LB Wingback+"
        for (int id : {3, 4, 5, 6, 46, 23, 24, 25, 26, 35, 36, 37}) {
            const std::string a = role_text(id), b = role_text(ag::mirror_role(id));
            if (a.substr(a.find(' ')) != b.substr(b.find(' ')) || a.substr(0, a.find(' ')) == b.substr(0, b.find(' '))) same_word = false;
        }
        CHECK(same_word, "the mirrored role has the same words on the other side");
        CHECK(ag::mirror_position(3) == 7 && ag::mirror_position(12) == 16 && ag::mirror_position(23) == 27 && ag::mirror_position(25) == 25, "positions");
    });

    run_case("archetype gen: every archetype x tier x overall 55..95 x side gives a valid spec", [&] {
        const ag::Table& t = shipped();
        CHECK(t.rows.size() == 32, "table loaded");
        long n = 0;
        std::string first_bad;
        auto bad = [&](const std::string& s) {
            if (first_bad.empty()) first_bad = s;
        };
        const ov::Attrs base = spread();
        for (const ar::Archetype& a : ar::all()) {
            const ag::Row* row = t.find(a.name);
            if (!row) continue;
            for (Tier tier : kTiers)
                for (int target = 55; target <= 95; ++target)
                    for (Side side : {Side::Right, Side::Left}) {
                        ag::Request rq;
                        rq.archetype = a.name;
                        rq.ovr_target = target;
                        rq.tier = tier;
                        rq.side = side;
                        rq.base = base;
                        rq.today_year = 2027;
                        ag::PlayerSpec s;
                        ++n;
                        const std::string who = std::string(a.name) + "/" + ag::tier_name(tier) + "/" + std::to_string(target) + (side == Side::Left ? "/L" : "/R");
                        if (!ag::populate(t, rq, s)) {
                            bad(who + " refused: " + s.why);
                            continue;
                        }
                        // overall by the formula of his first position
                        if (ov::compute_overall(s.positions[0], s.attrs) != target || s.overall != target) bad(who + " overall " + std::to_string(s.overall));
                        for (int v : s.attrs)
                            if (v < 1 || v > 99) bad(who + " attribute out of 1..99");
                        // positions
                        if (s.positions.empty() || s.positions.size() > 3 || s.positions.size() != row->positions.size()) bad(who + " position count");
                        if (ov::group_of_position(s.positions[0]) != a.group) bad(who + " first position outside the group");
                        for (size_t i = 0; i < s.positions.size(); ++i) {
                            if (!lb::position_visible_code(s.positions[i])) bad(who + " position code");
                            if (!in_domain("preferredposition" + std::to_string(i + 1), s.positions[i])) bad(who + " position outside the squad file's values");
                        }
                        if (side == Side::Left && (s.positions[0] == 3 || s.positions[0] == 12 || s.positions[0] == 23)) bad(who + " Left kept a right-hand position");
                        // roles: the game's ids, among his positions, each slot inside the squad file's values
                        const size_t want_roles = std::min<size_t>(static_cast<size_t>(ag::role_count(tier, target)), row->roles.size());
                        if (s.roles.size() != want_roles || s.roles.empty() || s.roles.size() > 4) bad(who + " role count " + std::to_string(s.roles.size()));
                        for (size_t i = 0; i < s.roles.size(); ++i) {
                            const int id = s.roles[i];
                            if (role_text(id).empty() || (id % 100) >= 50) bad(who + " role id " + std::to_string(id));
                            if (id > 100 && i != 0) bad(who + " ++ outside slot 1");
                            if (!in_domain("role" + std::to_string(i + 1), id)) bad(who + " role " + std::to_string(id) + " outside slot " + std::to_string(i + 1) + "'s values");
                            const std::string w = role_pos_word(id);
                            bool among = false;
                            for (int p : s.positions) among = among || w == lb::position_code_name(p);
                            if (!among) bad(who + " role " + role_text(id) + " not for his positions");
                        }
                        const bool want_pp = target >= 85 && (tier == Tier::Superstar || tier == Tier::Starter) && s.roles[0] % 100 != 7 && s.roles[0] % 100 != 23;
                        if ((s.roles[0] > 100) != want_pp) bad(who + " ++ role1 rule");
                        // squad role
                        if (s.squad_role != static_cast<int>(tier) + 1 || s.squad_role != ag::squad_role_of(tier)) bad(who + " squad role");
                        // PlayStyles
                        const int n_plus = pop(s.icontrait1) + pop(s.icontrait2);
                        const int n_norm = pop(s.trait1) + (row->positions[0] == 0 ? pop(s.trait2) : 0);
                        const int have = static_cast<int>(row->playstyles.size());
                        const int e_plus = std::min(ag::plus_count(tier, target, s.potential), have);
                        const int e_norm = std::min(ag::normal_count(tier, target, s.potential), have - e_plus);
                        if (n_plus != e_plus || n_norm != e_norm) bad(who + " PlayStyle counts " + std::to_string(n_plus) + "+" + std::to_string(n_norm));
                        if (tier == Tier::Superstar && n_plus < 2) bad(who + " superstar needs 2+ PlayStyle+");
                        if (tier == Tier::Starter && n_plus < 1) bad(who + " starter needs 1+ PlayStyle+");
                        if ((tier == Tier::Rotation || tier == Tier::Bench) && n_plus != 0) bad(who + " no PlayStyle+ below starter");
                        if (tier == Tier::Prospect && n_plus != (s.potential >= 88 ? 1 : 0)) bad(who + " prospect PlayStyle+ by potential");
                        if (tier == Tier::Bench && (n_norm < 1 || n_norm > 2)) bad(who + " bench 1-2 PlayStyles");
                        if (s.plus_names.size() != static_cast<size_t>(n_plus) || s.style_names.size() != static_cast<size_t>(n_norm)) bad(who + " names vs bits");
                        // disjoint, inside the fields' depth, nothing the table does not give
                        if ((s.trait1 & s.icontrait1) != 0 || (s.trait2 & s.icontrait2) != 0) bad(who + " normal and plus overlap");
                        if ((s.trait1 | s.icontrait1) >> 30 != 0 || (s.trait2 | s.icontrait2) >> 17 != 0) bad(who + " bit beyond the field");
                        if (((s.trait2 | s.icontrait2) >> 11 & 1) != 0) bad(who + " Injury Prone");
                        if (row->positions[0] == 0) {
                            if (s.trait1 != 0 || s.icontrait1 != 0 || ((s.trait2 | s.icontrait2) & ~int64_t(0x3F) & ~(int64_t(0x1F) << 8) & ~(int64_t(1) << 12)) != 0) bad(who + " keeper bits");
                        } else if (((s.trait2 | s.icontrait2) & 0x3F) != 0 || s.icontrait2 != 0) {
                            bad(who + " outfield player with keeper bits or trait+");
                        }
                        if (static_cast<int>(s.trait_names.size()) != std::min(ag::trait_count(tier, target), static_cast<int>(row->traits.size()))) bad(who + " trait count");
                        // potential, age, contract
                        if (s.potential < s.overall || s.potential > 99) bad(who + " potential " + std::to_string(s.potential));
                        if (tier == Tier::Prospect) {
                            const int lo = target + 8, hi = target + 15;
                            if (!((s.potential >= lo && s.potential <= hi) || (s.potential == 99 && lo > 99) || (s.potential == 99 && lo <= 99 && hi >= 99)))
                                bad(who + " prospect potential " + std::to_string(s.potential));
                            if (s.max_age != 21) bad(who + " prospect age cap");
                        } else if (s.max_age != 0) {
                            bad(who + " age cap on a grown player");
                        }
                        if (s.contract_until != 2027 + ag::contract_years_for(tier) || s.wage < 2000) bad(who + " contract/wage");
                        // same request, same spec
                        ag::PlayerSpec s2;
                        ag::populate(t, rq, s2);
                        if (s2.attrs != s.attrs || s2.roles != s.roles || s2.trait1 != s.trait1 || s2.icontrait1 != s.icontrait1 || s2.trait2 != s.trait2 ||
                            s2.icontrait2 != s.icontrait2 || s2.potential != s.potential || s2.positions != s.positions || s2.wage != s.wage)
                            bad(who + " not deterministic");
                    }
        }
        CHECK(first_bad.empty(), std::to_string(n) + " specs checked" + (first_bad.empty() ? "" : "; first problem: " + first_bad));
    });

    run_case("archetype gen: more overall and a higher tier never mean fewer PlayStyles, a bigger wage band or a lower squad role", [&] {
        bool normals_up = true, wage_up = true, tiers_ordered = true, plus_up = true;
        for (Tier t : kTiers) {
            int prev_n = 0, prev_w = 0, prev_p = 0;
            for (int o = 55; o <= 95; ++o) {
                const int pot = ag::potential_for(t, o);
                const int n = ag::normal_count(t, o, pot), p = ag::plus_count(t, o, pot), w = ag::wage_for(t, o);
                if (t != Tier::Prospect && n < prev_n) normals_up = false;
                if (t != Tier::Prospect && p < prev_p) plus_up = false;
                if (w < prev_w) wage_up = false;
                prev_n = n;
                prev_p = p;
                prev_w = w;
            }
        }
        for (int o = 55; o <= 95; ++o)
            if (!(ag::wage_for(Tier::Superstar, o) >= ag::wage_for(Tier::Starter, o) && ag::wage_for(Tier::Starter, o) >= ag::wage_for(Tier::Rotation, o) &&
                  ag::wage_for(Tier::Rotation, o) >= ag::wage_for(Tier::Prospect, o) && ag::wage_for(Tier::Prospect, o) >= ag::wage_for(Tier::Bench, o)))
                tiers_ordered = false;
        CHECK(normals_up && plus_up, "PlayStyle counts rise with the overall (per tier)");
        CHECK(wage_up, "the wage rises with the overall");
        CHECK(tiers_ordered, "at one overall: superstar >= starter >= rotation >= prospect >= bench");
        CHECK(ag::squad_role_of(Tier::Superstar) == 1 && ag::squad_role_of(Tier::Starter) == 2 && ag::squad_role_of(Tier::Rotation) == 3 &&
                  ag::squad_role_of(Tier::Bench) == 4 && ag::squad_role_of(Tier::Prospect) == 5,
              "tier -> squad role: crucial, important, rotation, sporadic, prospect");
        CHECK(ag::wage_for(Tier::Starter, 65) > 8000 && ag::wage_for(Tier::Starter, 65) < 14000 && ag::wage_for(Tier::Starter, 75) > 60000 &&
                  ag::wage_for(Tier::Starter, 75) < 100000 && ag::wage_for(Tier::Starter, 85) > 150000 && ag::wage_for(Tier::Starter, 85) < 230000,
              "a starter's wage lands on the squad file's medians (11k at 65, 82k at 75, 192k at 85)");
    });

    run_case("archetype gen: an overall that cannot be reached is refused with the closest; a name that is not in the table too", [&] {
        const ag::Table& t = shipped();
        ag::Request rq;
        rq.archetype = "Defensive Full-back";
        rq.ovr_target = 1;
        rq.tier = Tier::Bench;
        rq.base = spread();
        ag::PlayerSpec s;
        CHECK(!ag::populate(t, rq, s) && !s.why.empty() && s.closest > 1, "a full-back at 1: refused, closest " + std::to_string(s.closest) + ": " + s.why);
        rq.archetype = "Nobody";
        rq.ovr_target = 70;
        CHECK(!ag::populate(t, rq, s) && s.why.find("Nobody") != std::string::npos, "unknown archetype: " + s.why);
        rq.archetype = "Poacher";
        rq.ovr_target = 100;
        CHECK(!ag::populate(t, rq, s), "overall 100: refused");
        rq.ovr_target = 70;
        CHECK(ag::populate(t, rq, s) && s.warnings.empty(), "Poacher 70 starter: fine, no warning");
        rq.tier = Tier::Superstar;
        CHECK(ag::populate(t, rq, s) && !s.warnings.empty() && s.warnings[0].find("Superstar") != std::string::npos, "a 70 superstar: a warning, still populated");
        rq.tier = Tier::Prospect;
        rq.ovr_target = 95;
        CHECK(ag::populate(t, rq, s) && s.potential == 99, "a 95 prospect's potential is capped at 99");
    });

    run_case("archetype gen: the two W4 specs - Target Man 82 starter, Box to Box 75 rotation", [&] {
        const ag::Table& t = shipped();
        ag::Request rq;
        rq.base = flat(50);
        rq.archetype = "Target Man";
        rq.ovr_target = 82;
        rq.tier = Tier::Starter;
        ag::PlayerSpec s;
        CHECK(ag::populate(t, rq, s), "Target Man 82 starter: " + s.why);
        CHECK(s.positions == std::vector<int>({25}) && s.roles == std::vector<int>({44, 41}) && s.squad_role == 2 && s.overall == 82,
              "ST, roles Target Forward + Advanced Forward, important, overall 82");
        CHECK(s.plus_names == std::vector<std::string>({"Precision Header"}), "PlayStyle+ Precision Header");
        CHECK(s.style_names == std::vector<std::string>({"Bruiser", "Power Shot", "Enforcer"}), "plain: Bruiser, Power Shot, Enforcer");
        rq.archetype = "Box to Box";
        rq.ovr_target = 75;
        rq.tier = Tier::Rotation;
        CHECK(ag::populate(t, rq, s), "Box to Box 75 rotation: " + s.why);
        CHECK(s.positions == std::vector<int>({14, 10, 18}) && s.roles == std::vector<int>({18, 19, 21}) && s.squad_role == 3 && s.overall == 75 && s.plus_names.empty(),
              "CM CDM CAM, three roles, rotation, no PlayStyle+");
        CHECK(s.style_names.size() == 3, "three plain PlayStyles");
    });
}


// ---------------------------------------------------------------- UI (the panel and the Create entry, drawn in a window of the test)
static std::map<std::string, int64_t> snapshot(App& app, const Table& t, uint64_t rec) {
    std::map<std::string, int64_t> m;
    auto put = [&](const std::string& f) {
        if (t.field(f)) m[f] = app.db.get_int(t, rec, f, 0);
    };
    for (int i = 1; i <= 7; ++i) put("preferredposition" + std::to_string(i));
    for (int i = 0; i < ov::kAttrCount; ++i) put(ov::kAttrNames[i]);
    for (int i = 1; i <= 9; ++i) put("role" + std::to_string(i));
    for (const char* f : {"trait1", "icontrait1", "trait2", "icontrait2", "overallrating", "potential", "contractvaliduntil", "wage"}) put(f);
    return m;
}

static void run_ui(App& app, Ui& ui, SimMemory& mem, uint64_t kMb) {
    run_case("UI: Populate - player type, wanted overall and tier fill positions, attributes, roles, PlayStyles, traits, potential; Undo restores every field", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        const Table* pt = app.db.table("players");
        CHECK(pt != nullptr, "players table");
        if (!pt) return;
        // the shipped table where the screens look for it
        const fs::path dst = app.bridge.root() / "turbo" / "data";
        std::error_code ec;
        fs::create_directories(dst, ec);
        fs::copy_file("../turbo/package/turbo/data/archetypes.json", dst / "archetypes.json", fs::copy_options::overwrite_existing, ec);
        CHECK(!ec, "table copied into the test Live Editor folder");
        const int64_t pid = 2002;
        const uint64_t rec = app.db.find(*pt, "playerid", pid);
        CHECK(rec != 0, "player 2002");
        if (!rec) return;
        const std::map<std::string, int64_t> orig = snapshot(app, *pt, rec);
        const PlayerRow* row = app.model.player(pid);
        CHECK(row != nullptr, "his list row");
        if (!row) return;
        PlayerRow prow = *row;
        App* ap = &app;
        ui.extra_draw = [ap, pt, &prow]() {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1100, 700));
            ImGui::Begin("Populate test", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
            turbo::archetype_populate_panel(*ap, *pt, prow);
            ImGui::End();
        };
        ui.frames(4);
        const turbo::PopulateUiState& st = turbo::populate_ui_state();
        CHECK(st.table_ok, "the player type list loaded: " + st.table_error);
        CHECK(ui.find("##parch", "Populate test") && ui.find("##ptier", "Populate test") && ui.find("##ptarget", "Populate test") &&
                  ui.find("###popreview", "Populate test"),
              "the three controls and the review button");

        // Target Man, 82, Starter
        CHECK(ui.click("##parch", "Populate test") && ui.click("Target Man", "##Combo"), "pick Target Man");
        CHECK(ui.click("##ptier", "Populate test") && ui.click("Starter", "##Combo") && st.tier == static_cast<int>(Tier::Starter), "tier Starter");
        CHECK(ui.type_into(ui.find("##ptarget", "Populate test"), "82") && st.target == 82, "wanted overall 82");
        CHECK(ui.click("###popreview", "Populate test"), "review");
        ui.frames(2);
        CHECK(st.pend_ok && st.pend.overall == 82 && st.pend.positions == std::vector<int>({25}) && !st.pend_changes.empty(),
              "the review holds the spec: " + std::to_string(st.pend_changes.size()) + " changes");
        CHECK(ui.find("Populate##pop", "Fill the whole player") != nullptr && ui.find("Cancel##pop", "Fill the whole player") != nullptr, "review window with Populate");
        CHECK(snapshot(app, *pt, rec) == orig, "nothing written by the review");
        ag::PlayerSpec want;
        {
            ag::Request rq;
            rq.archetype = "Target Man";
            rq.ovr_target = 82;
            rq.tier = Tier::Starter;
            rq.side = Side::Right;
            for (int i = 0; i < ov::kAttrCount; ++i) rq.base[ix(i)] = static_cast<int>(app.db.get_int(*pt, rec, ov::kAttrNames[i], 0));
            CHECK(ag::populate(shipped(), rq, want), "the core's spec: " + want.why);
        }
        const size_t bulk0 = app.bulk_undo.size();
        CHECK(ui.click("Populate##pop", "Fill the whole player"), "Populate");
        ui.frames(2);
        const std::map<std::string, int64_t> after = snapshot(app, *pt, rec);
        CHECK(after.at("preferredposition1") == 25 && after.at("preferredposition2") == -1, "positions: ST only");
        CHECK(after.at("role1") == 44 && after.at("role2") == 41 && after.at("role3") == 0, "roles: Target Forward, Advanced Forward");
        CHECK(after.at("icontrait1") == (int64_t(1) << 4) && after.at("trait1") == ((int64_t(1) << 28) | (int64_t(1) << 2) | (int64_t(1) << 29)),
              "PlayStyle+ Precision Header, plain Bruiser, Power Shot, Enforcer");
        CHECK(((after.at("trait2") >> 12) & 1) == 1 && ((after.at("trait2") >> 8) & 1) == 1, "traits Leadership and Solid Player");
        CHECK(after.at("overallrating") == 82 && after.at("potential") == 84, "overall 82, potential 84");
        {
            ov::Attrs a{};
            for (int i = 0; i < ov::kAttrCount; ++i) a[ix(i)] = static_cast<int>(after.at(ov::kAttrNames[i]));
            CHECK(a == want.attrs && ov::compute_overall(25, a) == 82, "the attributes are the core's and give 82 by the ST formula");
        }
        CHECK(app.bulk_undo.size() == bulk0 + 1 && app.bulk_undo.back().playerid == pid, "one bulk undo entry");
        CHECK(st.applied_playerid == pid && !st.applied_label.empty(), "the applied label: " + st.applied_label);
        CHECK(ui.find("###popundo", "Populate test") != nullptr, "Undo shown after Populate");
        CHECK(ui.click("###popundo", "Populate test"), "Undo");
        ui.frames(2);
        const std::map<std::string, int64_t> back = snapshot(app, *pt, rec);
        CHECK(back == orig, "Undo: every field is as it was (" + std::to_string(orig.size()) + " fields compared)");
        CHECK(app.bulk_undo.size() == bulk0 && ui.find("###popundo", "Populate test") == nullptr, "the entry is used up");

        // keeper: the goalkeeper PlayStyles go to trait2/icontrait2 and his outfield bits are cleared; Undo restores
        CHECK(ui.click("##parch", "Populate test") && ui.click("Sweeper Keeper", "##Combo"), "pick Sweeper Keeper");
        CHECK(ui.click("##ptier", "Populate test") && ui.click("Superstar", "##Combo"), "tier Superstar");
        CHECK(ui.type_into(ui.find("##ptarget", "Populate test"), "88"), "overall 88");
        CHECK(ui.click("###popreview", "Populate test"), "review");
        ui.frames(2);
        CHECK(st.pend_ok && st.pend.positions == std::vector<int>({0}) && st.pend.roles.size() == 2 && st.pend.roles[0] == 102, "keeper spec: ++ Sweeper Keeper first");
        CHECK(ui.click("Populate##pop", "Fill the whole player"), "Populate");
        ui.frames(2);
        const std::map<std::string, int64_t> gk = snapshot(app, *pt, rec);
        CHECK(gk.at("preferredposition1") == 0 && gk.at("trait1") == 0 && gk.at("icontrait1") == 0 && (gk.at("icontrait2") & 0x3F) != 0,
              "keeper: PlayStyle+ in icontrait2, none in the outfield fields");
        CHECK(ui.click("###popundo", "Populate test") && snapshot(app, *pt, rec) == orig, "Undo restores the keeper change too");

        // a refusal writes nothing, offers the closest
        CHECK(ui.click("##parch", "Populate test") && ui.click("Defensive Full-back", "##Combo"), "pick Defensive Full-back");
        CHECK(ui.type_into(ui.find("##ptarget", "Populate test"), "1"), "overall 1");
        CHECK(ui.click("###popreview", "Populate test"), "review");
        ui.frames(2);
        CHECK(!st.pend_ok && st.pend.closest > 1, "refused, closest " + std::to_string(st.pend.closest));
        CHECK(ui.click("Populate##pop", "Fill the whole player"), "click the disabled Populate");
        CHECK(snapshot(app, *pt, rec) == orig, "refused: nothing written");
        const std::string use = "Use " + std::to_string(st.pend.closest) + "##popclosest";
        CHECK(ui.click(use, "Fill the whole player"), "Use the closest");
        ui.frames(2);
        CHECK(st.pend_ok && st.pend.overall == st.target, "now reachable");

        // stale: his data moves between the review and Populate
        std::string err;
        CHECK(app.db.set_int(*pt, rec, "finishing", orig.at("finishing") ^ 1, &err), "his finishing changes under the review");
        CHECK(ui.click("Populate##pop", "Fill the whole player"), "Populate on a stale review");
        ui.frames(2);
        std::map<std::string, int64_t> moved = orig;
        moved["finishing"] ^= 1;
        CHECK(snapshot(app, *pt, rec) == moved, "stale: nothing else written");
        CHECK(app.db.set_int(*pt, rec, "finishing", orig.at("finishing"), &err), "finishing back");
        ui.extra_draw = nullptr;
        ui.frames(2);
    });

    run_case("UI: Populate in a career also writes contract and wage; the changes list is what the write does", [&] {
        const Table* pt = app.db.table("players");
        if (!pt) return;
        const int64_t pid = 2002;
        const uint64_t rec = app.db.find(*pt, "playerid", pid);
        const PlayerRow* row = app.model.player(pid);
        CHECK(rec != 0 && row, "player 2002");
        if (!rec || !row) return;
        ag::Request rq;
        rq.archetype = "Box to Box";
        rq.ovr_target = 75;
        rq.tier = Tier::Rotation;
        rq.today_year = 2027;
        for (int i = 0; i < ov::kAttrCount; ++i) rq.base[ix(i)] = static_cast<int>(app.db.get_int(*pt, rec, ov::kAttrNames[i], 0));
        ag::PlayerSpec sp;
        CHECK(ag::populate(shipped(), rq, sp), "Box to Box 75 rotation: " + sp.why);
        const std::map<std::string, int64_t> orig = snapshot(app, *pt, rec);
        const std::vector<turbo::PopulateChange> off = turbo::populate_changes(app, *pt, *row, sp, false);
        const std::vector<turbo::PopulateChange> on = turbo::populate_changes(app, *pt, *row, sp, true);
        auto has = [](const std::vector<turbo::PopulateChange>& v, const char* f) {
            for (const auto& c : v)
                if (c.field == f) return true;
            return false;
        };
        const bool fields = pt->field("contractvaliduntil") && pt->field("wage");
        CHECK(!has(off, "wage") && !has(off, "contractvaliduntil"), "outside a career: no contract, no wage");
        CHECK(!fields || (has(on, "wage") && has(on, "contractvaliduntil")), "in a career: contract and wage are in the list");
        bool ok = false;
        CHECK(turbo::populate_write(app, *pt, *row, on, "test populate", &ok) == static_cast<int>(on.size()) && ok, "written and read back");
        if (fields) {
            CHECK(app.db.get_int(*pt, rec, "wage") == sp.wage && app.db.get_int(*pt, rec, "contractvaliduntil") == 2030, "wage " + std::to_string(sp.wage) + " until 2030");
        }
        CHECK(turbo::populate_undo(app, *pt, *row) && snapshot(app, *pt, rec) == orig, "Undo restores everything, contract and wage too");
    });

    run_case("UI: Populate in Create player fills the form's boxes; Undo puts the form back; Lua's create_player takes what is sent", [&] {
        const Table* pt = app.db.table("players");
        if (!pt) return;
        if (app.busy()) ui.click("Cancel");
        const int64_t pid = 1002;
        const uint64_t rec = app.db.find(*pt, "playerid", pid);
        const PlayerRow* row = app.model.player(pid);
        CHECK(rec != 0 && row, "template 1002");
        if (!rec || !row) return;
        PlayerRow trow = *row;
        const create_form::Form orig = create_form::load(app.db, *pt, rec, pid, create_form::Names{"A", "B", "", ""});
        create_form::Form cur = orig;
        bool clicked = false;
        App* ap = &app;
        ui.extra_draw = [ap, &trow, &cur, &clicked]() {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1100, 700));
            ImGui::Begin("Populate create test", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
            if (turbo::archetype_populate_create(*ap, trow, cur)) clicked = true;
            ImGui::End();
        };
        ui.frames(4);
        const turbo::PopulateUiState& st = turbo::populate_ui_state();
        CHECK(ui.click("##cparch", "Populate create test") && ui.click("Box to Box", "##Combo"), "pick Box to Box");
        CHECK(ui.click("##cptier", "Populate create test") && ui.click("Rotation", "##Combo"), "tier Rotation");
        CHECK(ui.type_into(ui.find("##cptarget", "Populate create test"), "75") && st.target == 75, "overall 75");
        CHECK(ui.click("###cpopreview", "Populate create test"), "review");
        ui.frames(2);
        CHECK(st.pend_ok && st.pend.overall == 75 && !st.pend_changes.empty(), "the review holds the spec");
        CHECK(cur.ints == orig.ints, "nothing filled by the review");
        CHECK(ui.click("Populate##cpop", "Fill the whole player"), "Populate");
        ui.frames(2);
        CHECK(clicked, "the caller is told (it clears its older player type choice)");
        CHECK(cur.get("preferredposition1") == 14 && cur.get("preferredposition2") == 10 && cur.get("preferredposition3") == 18 && cur.get("preferredposition4") == -1,
              "positions CM, CDM, CAM");
        CHECK(cur.get("role1") == 18 && cur.get("role2") == 19 && cur.get("role3") == 21 && cur.get("overallrating") == 75, "roles and overall in the form");
        ov::Attrs fa{};
        for (int i = 0; i < ov::kAttrCount; ++i) fa[ix(i)] = static_cast<int>(cur.get(ov::kAttrNames[i]));
        CHECK(ov::compute_overall(14, fa) == 75, "the form's attributes give 75 by the CM formula");
        // what a Create click sends
        const create_form::Overrides d = create_form::diff(orig, cur);
        const nlohmann::json cmd = create_form::command(orig, d, trow.club);
        CHECK(d.error.empty() && d.set.contains("role1") && d.set.contains("trait1") && d.set["overallrating"] == 75 && d.set["preferredposition1"] == 14,
              "the Create click sends roles, PlayStyles, overall and positions");
        // through Turbo's real Lua side: create_player accepts these fields
        nlohmann::json list = nlohmann::json::array();
        list.push_back({{"label", "Create player (populate)"}, {"cmd", cmd}});
        std::ofstream(g_out / "gui_commands.json") << list.dump(1);
        CHECK(run_lua("commands_attrs") == 0, "gui_world.lua commands_attrs");
        nlohmann::json results = read_json(g_out / "gui_commands_out.json");
        CHECK(results.size() == 1 && results[0]["ok"].get<bool>(),
              "Lua created the player: " + (results.size() == 1 ? results[0]["text"].get<std::string>() : std::string("?")));
        CHECK(ui.find("###cpopundo", "Populate create test") != nullptr, "Undo shown");
        CHECK(ui.click("###cpopundo", "Populate create test"), "Undo");
        ui.frames(2);
        CHECK(cur.ints == orig.ints, "Undo: the form is as it was");
        ui.extra_draw = nullptr;
        ui.frames(2);
        (void)mem;
        (void)kMb;
    });
}

}  // namespace archetype_gen_test
