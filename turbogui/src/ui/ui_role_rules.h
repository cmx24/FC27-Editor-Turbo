// Turbo 2.0: the squad role rule editor (Teams > team edit > Mass actions > Squad roles).
// Rules (age band, OVR rank band, position group -> squad role) and per-player pins are edited here; the Lua side
// (features/team_mass.lua, action squad_roles) judges and writes. Preview asks Lua for the rows and reads
// turbo_output\role_preview.json ONCE when the answer arrives (on_result), never per frame.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

namespace turbo {
struct App;
struct TeamRow;

namespace rolerules {

constexpr int kMaxRules = 32;  // team_mass.lua ROLE_RULES_MAX
constexpr int kRoleCount = 5;  // 1 Crucial .. 5 Prospect
const char* role_name(int role);  // 1..5 -> "Crucial".."Prospect", anything else "-"
const char* pos_group_name(int bit);  // 0..3 -> GK, DEF, MID, ATT

struct Rule {
    std::string name;
    int role = 3;                                    // 1..5
    int age_min = 0, age_max = 0;                    // 0 = no limit
    int rank_min = 0, rank_max = 0;                  // OVR rank inside the squad, 1 = best; 0 = no limit
    unsigned pos = 0;                                // bit 0 GK, 1 DEF, 2 MID, 3 ATT; 0 = every position
};

struct Pin {
    int64_t pid = 0;
    std::string name;
    int role = 3;  // 0 = leave this player alone, 1..5
};

struct PreviewRow {
    int64_t pid = 0;
    std::string name, group, rule, skip;  // rule: "1" (index) or "pin"
    int age = -1, ovr = -1, rank = -1, old_role = -1, new_role = -1;
};

struct Preview {
    bool loaded = false;
    int64_t team = 0;
    std::vector<std::pair<std::string, int>> counts;  // reason -> players
    int loaned = 0;
    size_t no_entry = 0, outside = 0;
    std::vector<PreviewRow> rows;
};

struct State {
    State();
    std::vector<Rule> rules;
    std::vector<Pin> pins;
    bool save_rule = true;          // remember the rule for the re-apply after the game's season reset
    Preview preview;
    std::string status;             // the last outcome (answer from Lua or a validation message)
    // files (checked when an answer arrives and then at most every 2 s, never per frame)
    bool killswitch = false;        // turbo_output\role_reapply_off.txt exists
    bool rule_saved = false;        // turbo_output\role_rule.json exists
    double files_checked = -100.0;
    // pin picker
    int pick_role = 3;
    int64_t pick_pid = 0;
    std::string confirm;            // "apply" / "clear" waiting for the confirmation
};

std::vector<Rule> default_rules();  // the 1.2.4 behaviour: 19 and older Rotation, younger Prospect
bool add_rule(State& s);            // false at the 32 rules limit
bool remove_rule(State& s, size_t i);  // false when it is the last rule (at least one is needed) or out of range
bool move_rule(State& s, size_t i, int dir);  // dir -1 up, +1 down; false at the ends
void set_pin(State& s, int64_t pid, const std::string& name, int role);  // add or change
bool remove_pin(State& s, int64_t pid);
std::string validate(const State& s);  // "" when the rules can be sent, else what is wrong (matches Lua's checks)

// The team_mass overrides: {teamid, actions:["squad_roles"], role_rules, role_pins, role_preview?, role_save?}
nlohmann::json overrides(const State& s, int64_t teamid, bool preview);
nlohmann::json clear_overrides(int64_t teamid);
// role_preview.json -> Preview. name_of fills the player names (may be null). false on unreadable text.
bool parse_preview(const std::string& text, Preview& out, std::string (*name_of)(void*, int64_t) = nullptr, void* ctx = nullptr);

void refresh_files(App& app, bool force);  // the two status files
void on_result(App& app, const std::string& label, bool ok, const std::string& result);  // App::tick, result arrival
void draw(App& app, const TeamRow& tr, bool own, bool disabled);  // the section inside Mass actions

}  // namespace rolerules
}  // namespace turbo
