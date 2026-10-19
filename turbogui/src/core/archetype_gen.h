// FC 27 LE Turbo GUI - 2.20 R3: one archetype x one overall target x one player tier define a whole player.
// populate() turns the three choices into a PlayerSpec: the positions, the 34 attributes (archetypes::apply_target, the
// profile's shape kept), the roles for those positions, the PlayStyles and traits, the squad role, the potential, and the
// contract and wage. Platform independent and deterministic (the same request gives the same spec); the screens write the
// spec through the existing edit paths (ui_archetype_populate.cpp).
//
// What each tier means (the numbers are in the .cpp, each with its source):
//   Superstar  crucial squad role,  2+ PlayStyle+ (3 from 90), the full set of normal PlayStyles, up to 4 roles
//   Starter    important,           1  PlayStyle+, normal PlayStyles by overall
//   Rotation   rotation,            no PlayStyle+, one more normal PlayStyle than a starter
//   Bench      sporadic,            no PlayStyle+, 1-2 normal PlayStyles, at most 2 roles
//   Prospect   prospect,            potential = overall + 8..15 (age 21 at most), PlayStyles by the potential
// The archetype row (positions, role ids, ordered PlayStyle and trait names) comes from data/archetypes.json.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/archetypes.h"
#include "core/overall.h"

namespace turbo {
namespace archetype_gen {

enum class Tier { Superstar, Starter, Rotation, Bench, Prospect };
constexpr int kTierCount = 5;
const char* tier_name(Tier t);        // "Superstar", "Starter", "Rotation", "Bench", "Prospect"
const char* tier_blurb(Tier t);       // one line for the tooltip
// The squad role the game stores for a tier: 1 Crucial, 2 Important, 3 Rotation, 4 Sporadic, 5 Prospect
int squad_role_of(Tier t);
// The overall range in which a tier is believable (outside it Populate still works, with a warning in the preview)
void tier_fit_range(Tier t, int& lo, int& hi);

// Full-backs, wide midfielders and wingers exist as a right and a left player: the table holds the right-hand form, Left mirrors
// the positions and the role ids
enum class Side { Right, Left };

// Which of the two PlayStyle word lists a bit belongs to (ui/playstyles.h): trait1/icontrait1 (30 bits) or trait2/icontrait2 (17)
struct StyleBit {
    bool second = false;   // false: trait1 / icontrait1, true: trait2 / icontrait2
    int bit = 0;
};

// One line of data/archetypes.json
struct Row {
    std::string name;                       // equals an archetypes::all() name
    std::vector<int> positions;             // position codes (GK 0, RB 3, CB 5, LB 7, CDM 10, RM 12, CM 14, LM 16, CAM 18, RW 23, ST 25, LW 27); the
                                            // first one is the position his overall is worked out for; right-hand forms
    std::vector<int> roles;                 // the "+" role ids in slot order (role1 first), right-hand forms, each among the positions' roles
    std::vector<std::string> role_names;    // the roles in words (for the reader of the file; checked against the game's label table)
    std::vector<std::string> playstyles;    // PlayStyle names, strongest first: the PlayStyle+ come from the front, the normal ones follow
    std::vector<std::string> traits;        // trait names (trait2), most typical first
    std::vector<StyleBit> style_bits;       // `playstyles` resolved to bits
    std::vector<StyleBit> trait_bits;       // `traits` resolved to bits (all second == true)
};

struct Table {
    std::vector<Row> rows;
    const Row* find(const std::string& name) const;
};

// Parse data/archetypes.json. false with `err` set when the text is not that file (a name that is no PlayStyle, a role id that
// does not belong to the row's positions, a missing row ...); `out` is then empty.
bool parse_table(const std::string& json_text, Table& out, std::string& err);
// <LE folder>\turbo\data\archetypes.json
std::filesystem::path table_path(const std::filesystem::path& le_root);
bool load_table(const std::filesystem::path& le_root, Table& out, std::string& err);

struct Request {
    std::string archetype;                 // an archetypes::all() name
    int ovr_target = 75;                   // 1..99
    Tier tier = Tier::Starter;
    Side side = Side::Right;
    overall::Attrs base{};                 // his attributes now (outside the archetype's profile they stay as they are)
    int today_year = 0;                    // the game's year; 0 = no contract is worked out (not in a career)
};

struct PlayerSpec {
    std::string archetype;
    Tier tier = Tier::Starter;
    int ovr_target = 0;
    Side side = Side::Right;

    std::vector<int> positions;            // 1..3 codes, [0] = preferredposition1
    overall::Attrs attrs{};
    archetypes::TargetResult fit;          // the attribute walk (fit.overall_after == the overall of positions[0])
    int overall = 0;                       // compute_overall(positions[0], attrs)
    std::vector<int> roles;                // role ids, slot order (role1 first), <= 4
    int64_t trait1 = 0, icontrait1 = 0;    // PlayStyles, PlayStyles+ (disjoint)
    int64_t trait2 = 0, icontrait2 = 0;    // traits / goalkeeper PlayStyles, and their + form (disjoint)
    std::vector<std::string> style_names;  // the PlayStyles chosen, in words (normal)
    std::vector<std::string> plus_names;   // the PlayStyle+ chosen, in words
    std::vector<std::string> trait_names;  // the traits chosen, in words
    int squad_role = 0;                    // 1..5
    int potential = 0;                     // >= overall, <= 99
    int max_age = 0;                       // 21 for a prospect (0 = no limit)
    int contract_years = 0;
    int contract_until = 0;                // the year (today_year + contract_years); 0 when the request had no year
    int wage = 0;                          // per week (the game's units), 0 when there is no contract
    std::vector<std::string> warnings;     // things to see in the preview (tier outside the usual overall range ...)
    std::string why;                       // refused: the reason in words
    int closest = 0;                       // refused because the overall cannot be reached: the nearest one that can
};

// Fills `out`. false (out.why set) when the archetype has no row, or the overall cannot be reached for it.
bool populate(const Table& table, const Request& req, PlayerSpec& out);

// ---- the numbers, exposed so the tests and the preview say the same as the generator
int normal_count(Tier t, int ovr, int potential);   // normal PlayStyles wanted (before the archetype's list length caps them)
int plus_count(Tier t, int ovr, int potential);     // PlayStyle+ wanted
int role_count(Tier t, int ovr);                    // roles wanted
int trait_count(Tier t, int ovr);                   // traits wanted
int potential_for(Tier t, int ovr);                 // 1..99
int contract_years_for(Tier t);
int wage_for(Tier t, int ovr);                      // weekly wage, [A] a band, not a game rule
// The left-hand form of a right-hand position code / role id (unchanged for the ones that have no mirror)
int mirror_position(int code);
int mirror_role(int id);                            // "+" and "++" ids

// Names -> bits in ui/playstyles.h's two lists (false when the name is in neither)
bool style_bit(const std::string& name, StyleBit& out);

}  // namespace archetype_gen
}  // namespace turbo
