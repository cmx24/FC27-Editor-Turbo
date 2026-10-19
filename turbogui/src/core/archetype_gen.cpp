// FC 27 LE Turbo GUI - 2.20 R3: archetype x overall target x tier -> a whole player (see archetype_gen.h).
#include "core/archetype_gen.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include "nlohmann/json.hpp"
#include "playstyles.h"

namespace turbo {
namespace archetype_gen {

using json = nlohmann::json;

// ---------------------------------------------------------------------------------------------------------------- tiers

const char* tier_name(Tier t) {
    switch (t) {
        case Tier::Superstar: return "Superstar";
        case Tier::Starter: return "Starter";
        case Tier::Rotation: return "Rotation";
        case Tier::Bench: return "Bench";
        case Tier::Prospect: return "Prospect";
    }
    return "?";
}

const char* tier_blurb(Tier t) {
    switch (t) {
        case Tier::Superstar: return "The club's crucial player: two or more PlayStyle+, the full set of PlayStyles, several roles";
        case Tier::Starter: return "An important first-team player: one PlayStyle+, PlayStyles by his overall";
        case Tier::Rotation: return "Plays when the starters rest: no PlayStyle+, a few more plain PlayStyles";
        case Tier::Bench: return "Sporadic minutes: one or two PlayStyles, two roles at most";
        case Tier::Prospect: return "Young and growing: potential 8 to 15 above his overall, age 21 at most";
    }
    return "";
}

// squad_role.h: 1 Crucial, 2 Important, 3 Rotation, 4 Sporadic, 5 Prospect
int squad_role_of(Tier t) { return static_cast<int>(t) + 1; }

// The overall range in which each tier is believable. [A]: read off the squad file's overall spread per squad role (a starter
// of a top club is rarely below 68; a bench player of one rarely above 70); not a game rule, only a warning.
void tier_fit_range(Tier t, int& lo, int& hi) {
    switch (t) {
        case Tier::Superstar: lo = 82; hi = 99; return;
        case Tier::Starter: lo = 68; hi = 90; return;
        case Tier::Rotation: lo = 60; hi = 80; return;
        case Tier::Bench: lo = 50; hi = 72; return;
        case Tier::Prospect: lo = 40; hi = 78; return;
    }
    lo = 1;
    hi = 99;
}

// ---------------------------------------------------------------------------------------------------------------- counts
// All [G] from the squad file (FINAL\Squads20261018000000001, 22456 players): the mean number of normal PlayStyles by overall is
// 0.7 at 65, 1.2 at 70, 1.9 at 75, 2.9 at 80, 4.3 at 85 (+0.7 PlayStyle+), 5.3 at 90 (+0.9 PlayStyle+): lround((ovr-58)/7) tracks it
// from 65 to 90. Roles filled: mean 2.5 to 3, slots 1 to 4 used.

static int base_normals(int ovr) { return std::max(0, std::min(5, static_cast<int>(std::lround((ovr - 58) / 7.0)))); }

int normal_count(Tier t, int ovr, int potential) {
    const int b = base_normals(ovr);
    switch (t) {
        case Tier::Superstar:
        case Tier::Starter: return b;
        case Tier::Rotation: return std::min(5, b + 1);
        case Tier::Bench: return std::max(1, std::min(2, b - 1));
        case Tier::Prospect: {
            const int pb = static_cast<int>(std::lround((potential - 58) / 7.0)) - 1;
            return std::max(1, std::min(4, pb));
        }
    }
    return b;
}

int plus_count(Tier t, int ovr, int potential) {
    switch (t) {
        case Tier::Superstar: return ovr >= 90 ? 3 : 2;
        case Tier::Starter: return 1;
        case Tier::Prospect: return potential >= 88 ? 1 : 0;
        default: return 0;
    }
}

int role_count(Tier t, int ovr) {
    const int n = 1 + (ovr >= 65) + (ovr >= 75) + (ovr >= 85);
    if (t == Tier::Bench || t == Tier::Prospect) return std::min(n, 2);
    return std::min(n, 4);
}

int trait_count(Tier t, int ovr) {
    switch (t) {
        case Tier::Superstar: return 2;
        case Tier::Starter: return ovr >= 65 ? 2 : 1;
        case Tier::Rotation:
        case Tier::Bench: return 1;
        case Tier::Prospect: return 0;
    }
    return 0;
}

// potential = overall + the gap. [G] age <= 21: mean gap 14.4 (15 at overall 50s, 14 at 60s, 11 at 70s); age 28: about 2. The
// brief fixes the prospect's gap at 8..15; the other tiers are grown players.
int potential_for(Tier t, int ovr) {
    int gap = 0;
    switch (t) {
        case Tier::Superstar: gap = 1; break;
        case Tier::Starter: gap = 2; break;
        case Tier::Rotation:
        case Tier::Bench: gap = 3; break;
        case Tier::Prospect: gap = std::max(8, std::min(15, 15 - (ovr - 55) / 4)); break;
    }
    return std::max(1, std::min(99, ovr + gap));
}

int contract_years_for(Tier t) {
    switch (t) {
        case Tier::Superstar: return 5;
        case Tier::Starter: return 4;
        case Tier::Rotation: return 3;
        case Tier::Bench: return 2;
        case Tier::Prospect: return 4;
    }
    return 3;
}

// [A] The wage band. [G] the squad file's nonzero wages (497 rows; 98% of players have 0 there): median 11k at 65-74, 82k at 75-84,
// 192k at 85+. The curve joins those three points geometrically (and continues the same slope at both ends); the tier scales it
// (a prospect earns less than his overall suggests, a superstar more). Weekly, the game's units. Nothing here is a game rule.
int wage_for(Tier t, int ovr) {
    const double k[3][2] = {{65, 11000.0}, {75, 82000.0}, {85, 192000.0}};
    int i = ovr < 75 ? 0 : 1;
    const double slope = (std::log(k[i + 1][1]) - std::log(k[i][1])) / (k[i + 1][0] - k[i][0]);
    double w = std::exp(std::log(k[i][1]) + slope * (ovr - k[i][0]));
    double mult = 1.0;
    switch (t) {
        case Tier::Superstar: mult = 1.25; break;
        case Tier::Starter: mult = 1.0; break;
        case Tier::Rotation: mult = 0.7; break;
        case Tier::Bench: mult = 0.45; break;
        case Tier::Prospect: mult = 0.5; break;
    }
    w *= mult;
    const long long r = std::llround(w / 100.0) * 100;
    return static_cast<int>(std::max(2000LL, std::min(100000000LL, r)));
}

// ---------------------------------------------------------------------------------------------------------------- mirroring

int mirror_position(int code) {
    switch (code) {
        case 3: return 7;    // RB <-> LB
        case 7: return 3;
        case 12: return 16;  // RM <-> LM
        case 16: return 12;
        case 23: return 27;  // RW <-> LW
        case 27: return 23;
        default: return code;
    }
}

int mirror_role(int id) {
    const int plus = id >= 100 ? 100 : 0;
    const int b = id - plus;
    int m = b;
    if (b >= 3 && b <= 6) m = b + 4;          // RB Fullback.. -> LB
    else if (b >= 7 && b <= 10) m = b - 4;
    else if (b == 46) m = 47;
    else if (b == 47) m = 46;
    else if (b >= 23 && b <= 26) m = b + 4;   // RM -> LM
    else if (b >= 27 && b <= 30) m = b - 4;
    else if (b >= 35 && b <= 37) m = b + 3;   // RW -> LW
    else if (b >= 38 && b <= 40) m = b - 3;
    else if (b == 51) m = 52;
    else if (b == 52) m = 51;
    return m + plus;
}

// The position a role id belongs to (the "role" table of core/field_labels.h names each id "RB Fullback+" ...); -1 for an id the
// game has none for
static int role_position(int id) {
    const int b = id >= 100 ? id - 100 : id;
    if (b == 1 || b == 2 || b == 45) return 0;
    if ((b >= 3 && b <= 6) || b == 46) return 3;
    if ((b >= 7 && b <= 10) || b == 47) return 7;
    if ((b >= 11 && b <= 13) || b == 48) return 5;
    if ((b >= 14 && b <= 17) || b == 49) return 10;
    if (b >= 18 && b <= 22) return 14;
    if (b >= 23 && b <= 26) return 12;
    if (b >= 27 && b <= 30) return 16;
    if (b >= 31 && b <= 34) return 18;
    if ((b >= 35 && b <= 37) || b == 51) return 23;
    if ((b >= 38 && b <= 40) || b == 52) return 27;
    if ((b >= 41 && b <= 44) || b == 50) return 25;
    return -1;
}

static bool visible_position(int c) {
    for (int v : {0, 3, 5, 7, 10, 12, 14, 16, 18, 23, 25, 27})
        if (v == c) return true;
    return false;
}

// ---------------------------------------------------------------------------------------------------------------- the table

bool style_bit(const std::string& name, StyleBit& out) {
    const auto& a = playstyle1_names();
    for (size_t i = 0; i < a.size(); ++i)
        if (name == a[i]) {
            out = {false, static_cast<int>(i)};
            return true;
        }
    const auto& b = playstyle2_names();
    for (size_t i = 0; i < b.size(); ++i)
        if (name == b[i]) {
            out = {true, static_cast<int>(i)};
            return true;
        }
    return false;
}

const Row* Table::find(const std::string& name) const {
    for (const Row& r : rows)
        if (r.name == name) return &r;
    return nullptr;
}

std::filesystem::path table_path(const std::filesystem::path& le_root) { return le_root / "turbo" / "data" / "archetypes.json"; }

bool parse_table(const std::string& text, Table& out, std::string& err) {
    out.rows.clear();
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("archetypes") || !j["archetypes"].is_array()) {
        err = "the archetype table is not the expected file (no archetypes list)";
        return false;
    }
    std::set<std::string> seen;
    for (const json& r : j["archetypes"]) {
        Row row;
        if (!r.is_object() || !r.contains("name") || !r["name"].is_string()) {
            err = "an archetype row has no name";
            out.rows.clear();
            return false;
        }
        row.name = r["name"].get<std::string>();
        auto fail = [&](const std::string& why) {
            err = row.name + ": " + why;
            out.rows.clear();
            return false;
        };
        if (!seen.insert(row.name).second) return fail("listed twice");
        auto ints = [&](const char* key, std::vector<int>& dst) {
            if (!r.contains(key) || !r[key].is_array()) return false;
            for (const json& v : r[key]) {
                if (!v.is_number_integer()) return false;
                dst.push_back(v.get<int>());
            }
            return true;
        };
        auto strs = [&](const char* key, std::vector<std::string>& dst) {
            if (!r.contains(key) || !r[key].is_array()) return false;
            for (const json& v : r[key]) {
                if (!v.is_string()) return false;
                dst.push_back(v.get<std::string>());
            }
            return true;
        };
        if (!ints("positions", row.positions) || row.positions.empty() || row.positions.size() > 3) return fail("positions: one to three codes");
        for (int p : row.positions)
            if (!visible_position(p)) return fail("position " + std::to_string(p) + " is not a position the game names");
        if (!ints("roles", row.roles) || row.roles.empty() || row.roles.size() > 4) return fail("roles: one to four ids");
        std::set<int> ids;
        for (int id : row.roles) {
            if (id < 1 || id > 49) return fail("role " + std::to_string(id) + " is not a '+' role id the game uses (1..49)");
            if (!ids.insert(id).second) return fail("role " + std::to_string(id) + " listed twice");
            const int pos = role_position(id);
            if (std::find(row.positions.begin(), row.positions.end(), pos) == row.positions.end())
                return fail("role " + std::to_string(id) + " belongs to a position the row does not list");
        }
        strs("role_names", row.role_names);  // optional annotation, checked by the tests against the label table
        if (!strs("playstyles", row.playstyles) || row.playstyles.empty()) return fail("playstyles: a list of names");
        strs("traits", row.traits);
        const bool keeper = row.positions[0] == 0;
        std::set<std::string> names;
        for (const std::string& n : row.playstyles) {
            StyleBit b;
            if (!style_bit(n, b)) return fail("'" + n + "' is not a PlayStyle name");
            if (!names.insert(n).second) return fail("PlayStyle '" + n + "' listed twice");
            if (keeper ? !(b.second && b.bit <= 5) : b.second) return fail("PlayStyle '" + n + "' does not belong to " + (keeper ? "a goalkeeper" : "an outfield player"));
            row.style_bits.push_back(b);
        }
        for (const std::string& n : row.traits) {
            StyleBit b;
            // trait2's named bits: 8 Solid Player, 9 Team Player, 10 One Club Player, 12 Leadership, 13 Super Sub. Never Injury Prone (11).
            if (!style_bit(n, b) || !b.second || !(b.bit == 8 || b.bit == 9 || b.bit == 10 || b.bit == 12 || b.bit == 13))
                return fail("'" + n + "' is not a trait this table may give");
            if (!names.insert(n).second) return fail("'" + n + "' listed twice");
            row.trait_bits.push_back(b);
        }
        out.rows.push_back(std::move(row));
    }
    if (out.rows.empty()) {
        err = "the archetype table has no rows";
        return false;
    }
    return true;
}

bool load_table(const std::filesystem::path& le_root, Table& out, std::string& err) {
    std::ifstream f(table_path(le_root), std::ios::binary);
    if (!f) {
        err = "the player type list is not installed with this build";
        out.rows.clear();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_table(ss.str(), out, err);
}

// ---------------------------------------------------------------------------------------------------------------- populate

static void set_bit(const StyleBit& b, bool plus, PlayerSpec& s) {
    const int64_t m = int64_t(1) << b.bit;
    if (!b.second) (plus ? s.icontrait1 : s.trait1) |= m;
    else (plus ? s.icontrait2 : s.trait2) |= m;
}

bool populate(const Table& table, const Request& req, PlayerSpec& s) {
    s = PlayerSpec{};
    s.archetype = req.archetype;
    s.tier = req.tier;
    s.ovr_target = req.ovr_target;
    s.side = req.side;
    const Row* row = table.find(req.archetype);
    const archetypes::Archetype* arch = nullptr;
    for (const archetypes::Archetype& a : archetypes::all())
        if (req.archetype == a.name) arch = &a;
    if (!row || !arch) {
        s.why = "no archetype '" + req.archetype + "' in the table";
        return false;
    }
    if (req.ovr_target < 1 || req.ovr_target > 99) {
        s.why = "the overall must be between 1 and 99";
        return false;
    }

    // positions (the table holds the right-hand forms)
    for (int p : row->positions) s.positions.push_back(req.side == Side::Left ? mirror_position(p) : p);

    // attributes: the existing walk, shape kept
    if (!archetypes::apply_target(*arch, s.positions[0], req.base, req.ovr_target, s.fit)) {
        s.why = s.fit.why.empty() ? "the overall cannot be reached for this archetype" : s.fit.why;
        s.closest = s.fit.closest;
        return false;
    }
    s.attrs = s.fit.attrs;
    s.overall = overall::compute_overall(s.positions[0], s.attrs);

    s.potential = potential_for(req.tier, req.ovr_target);
    s.max_age = req.tier == Tier::Prospect ? 21 : 0;
    s.squad_role = squad_role_of(req.tier);

    // roles: the table's order, the first ++ only from overall 85 for a superstar or a starter (91% of the squad file's 85+ players
    // have a ++ first role, 19% at 80-84, 3% below). The game's role1 never holds 107 and 123 in the squad file, so those two stay "+".
    const int want_roles = std::min(role_count(req.tier, req.ovr_target), static_cast<int>(row->roles.size()));
    for (int i = 0; i < want_roles; ++i) s.roles.push_back(req.side == Side::Left ? mirror_role(row->roles[static_cast<size_t>(i)]) : row->roles[static_cast<size_t>(i)]);
    if (!s.roles.empty() && req.ovr_target >= 85 && (req.tier == Tier::Superstar || req.tier == Tier::Starter) && s.roles[0] != 7 && s.roles[0] != 23)
        s.roles[0] += 100;

    // PlayStyles: the plus ones from the front of the list, the normal ones after them
    const int want_plus = plus_count(req.tier, req.ovr_target, s.potential);
    const int want_norm = normal_count(req.tier, req.ovr_target, s.potential);
    const int have = static_cast<int>(row->playstyles.size());
    const int n_plus = std::min(want_plus, have);
    const int n_norm = std::min(want_norm, have - n_plus);
    for (int i = 0; i < n_plus; ++i) {
        set_bit(row->style_bits[static_cast<size_t>(i)], true, s);
        s.plus_names.push_back(row->playstyles[static_cast<size_t>(i)]);
    }
    for (int i = n_plus; i < n_plus + n_norm; ++i) {
        set_bit(row->style_bits[static_cast<size_t>(i)], false, s);
        s.style_names.push_back(row->playstyles[static_cast<size_t>(i)]);
    }
    if (n_plus < want_plus || n_norm < want_norm)
        s.warnings.push_back("This archetype lists only " + std::to_string(have) + " PlayStyles: " + std::to_string(n_plus) + " plus and " +
                             std::to_string(n_norm) + " plain are given");

    // traits (trait2; never a PlayStyle+ form of them)
    const int n_tr = std::min(trait_count(req.tier, req.ovr_target), static_cast<int>(row->traits.size()));
    for (int i = 0; i < n_tr; ++i) {
        set_bit(row->trait_bits[static_cast<size_t>(i)], false, s);
        s.trait_names.push_back(row->traits[static_cast<size_t>(i)]);
    }

    // contract and wage
    s.contract_years = contract_years_for(req.tier);
    s.wage = wage_for(req.tier, req.ovr_target);
    if (req.today_year > 0) s.contract_until = std::min(2120, req.today_year + s.contract_years);

    int lo = 0, hi = 0;
    tier_fit_range(req.tier, lo, hi);
    if (req.ovr_target < lo || req.ovr_target > hi)
        s.warnings.push_back(std::string("A ") + tier_name(req.tier) + " is usually " + std::to_string(lo) + " to " + std::to_string(hi) + " overall");
    return true;
}

}  // namespace archetype_gen
}  // namespace turbo
