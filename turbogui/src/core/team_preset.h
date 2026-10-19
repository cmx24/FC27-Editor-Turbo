// FC 27 LE Turbo GUI - the "My team" preset (brief 2.20-S2-team-defaults; Cassio 2026-10-19 10:56).
//
// "my preset mass edit command, applicable to all players in my team, should be: tight shirts, extend contract 60mo, very happy morale,
// squad roles (rotation and prospect), block offers, and nothing else. stuff like extra xp development and does not decline should
// always be OFF by default."
//
// The preset sets exactly the five fields of fields() for every player of the user's club and nothing else:
//   jersey_fit   players.jerseyfit = 1 (Tight), written by the window through App::edit_field (one undo step per player)
//   contract     60 months from the career date (team_mass action long_contract; loaned-in players keep their parent club's contract)
//   morale       "very happy" for each player's emotion type (team_mass action morale, the game's own level function)
//   squad_role   Rotation or Prospect by the rule below (team_mass action squad_roles with one pin per player)
//   block_offers the Squad hub's Block Offers status (team_mass action block_offers; players already blocked stay blocked)
// The team_mass command lists these actions explicitly, never "all" (that adds form, match XP and dev bonus) and never saves a role
// rule (role_save false: no re-apply after the game's season reset, no sixth effect).
//
// SQUAD ROLE RULE (one rule, signed at G1 11:34): Prospect = age under 23 AND overall below the club median; Rotation = every other
// player of the club. Median = the middle overall of the club's players (links of the team, loaned-in included, as team_mass sees
// them); with an even count the mean of the two middle ones. "Below" is strictly below, so a player exactly at the median is
// Rotation, and so is a player of 23 or more below it. A player below the median whose age is unknown (no birth date) is pinned 0 =
// left alone (the rule cannot be judged), and the result says so.
//
// GROWTH DEFAULTS: "Extra XP development" (the Development action of bulk edit, the weekly forced growth) and "Does not decline"
// (no_decline) are OFF in every place they appear. growth_default_* ignore the saved gui_settings on purpose: a saved value can never
// turn the default on (an older Turbo wrote no_decline = true there).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace turbo {

class App;

namespace teampreset {

struct Field {
    const char* id;      // stable key (livecheck, tests)
    const char* title;   // plain words for the button's tooltip and the result
    const char* action;  // the team_mass action that does it; "" = the window writes it itself (jersey fit)
};
constexpr size_t kFieldCount = 5;
// THE field list: a test fails when a sixth appears or one changes
const std::array<Field, kFieldCount>& fields();

constexpr int kJerseyFitTight = 1;       // field_labels.h jerseyfit: 0 Normal, 1 Tight, 2 Team Kit Fit
constexpr int kContractMonths = 60;      // team_mass.lua CONTRACT_MONTHS
constexpr int kRoleRotation = 3, kRoleProspect = 5, kRoleLeaveAlone = 0;  // team_mass.lua ROLE_ROTATION / ROLE_PROSPECT; pin 0 = leave alone
constexpr int kProspectBelowAge = 23;    // Prospect: age under this ...
constexpr size_t kMaxCommandChars = 3900;  // the mailbox command limit the bulk edit section checks too
inline const char* label() { return "Mass actions: My team preset"; }  // routes the result to App::mass_status (app.cpp)

// ---- growth defaults (every site that draws these boxes starts from here)
constexpr bool kExtraXpDefault = false;
constexpr bool kNoDeclineDefault = false;
// gui_settings is passed so the tests can prove it is ignored
bool growth_default_extra_xp(const nlohmann::json& gui_settings);
bool growth_default_no_decline(const nlohmann::json& gui_settings);

// ---- the squad role rule
struct SquadPlayer {
    int64_t playerid = 0;
    int ovr = 0;
    int age = -1;  // whole years today; -1 = unknown
};
// Median overall; 0 for no players
double median_ovr(const std::vector<int>& ovrs);
// kRoleRotation, kRoleProspect, or kRoleLeaveAlone (below the median with an unknown age)
int squad_role_for(int age, int ovr, double median);
// {playerid: role} for every player (the team_mass "role_pins")
nlohmann::json role_pins(const std::vector<SquadPlayer>& squad);

// The team_mass overrides: teamid, the explicit action list, the pins, role_save false
nlohmann::json team_mass_overrides(int64_t teamid, const std::vector<SquadPlayer>& squad);

}  // namespace teampreset

// Applies the preset to the user's club `teamid` (namespace turbo, plain name: the IA pages declare it as
// `bool team_preset_apply(App&, int64_t);`). Needs a loaded career, the command channel, and teamid == the user's club (roles and block
// offers exist for your own club only). Writes the jersey fit now, sends the rest as one team_mass command (teampreset::label()).
// False (toast says why) when nothing was sent.
bool team_preset_apply(App& app, int64_t teamid);
}  // namespace turbo
