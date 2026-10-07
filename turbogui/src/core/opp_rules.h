// FC 27 LE Turbo GUI - the opposition rules (Turbo 2.0): "when this fixture looks like that, then change these settings", as JSON data.
//
// Built-in rules ship as data in the same format as the user's file (<Live Editor>\turbo_output\opp_rules.json); a user rule with the same
// id replaces the built-in one (an `"enabled": false` copy switches it off). Evaluation is a pure function of a fact set: no game memory, no
// clock, no random numbers. See opposition.h for the solver that feeds the facts and applies the outcome.
//
//   {"turbo_opp_rules": 1, "rules": [
//     {"id": "underdog_low_block", "priority": 60, "enabled": true, "stop": false, "note": "...",
//      "when": {"all": [{"fact": "strength_ratio", "op": "<", "value": 0.85}], "any": []},
//      "then": [{"op": "boost_family", "target": "low_block", "value": 0.35}, {"op": "add", "target": "team.line_depth", "value": -4}]}]}
//
// "when" may also be a plain array (= all). Actions: add (adds to the change of a setting), set (pins the change of a setting, only when no
// rule of higher priority touched it), boost_family (adds to a style family's score before blending), scale_amplitude (multiplies the overall size
// of the changes).
// Priority: higher number first (ties by id); a rule with "stop": true ends the evaluation after it fired.
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace turbo {

constexpr int kOppRulesVersion = 1;
constexpr size_t kFamilyCount = 6;
// gegenpress, low_block, possession, wing_play, direct, balanced (the index order everywhere)
extern const char* const kFamilyKeys[kFamilyCount];
const char* family_label(size_t i);  // "Gegenpress", "Low block counter", ...
int family_index(const std::string& key);  // -1 when unknown

using FactSet = std::map<std::string, double>;
// The fact names a rule may test: strength_ratio, user_ovr, opp_ovr, reputation, form, age, pace, passing, aerial (0/1), home (0/1: the
// opponent plays at home), mentality, n_def, n_mid, n_att, n_wide, and family_<key> (the style scores before rules).
const std::vector<std::string>& known_facts();

struct RuleCond {
    std::string fact, op;  // op: < <= > >= == !=
    double value = 0;
};
struct RuleAction {
    std::string op;      // add set boost_family scale_amplitude
    std::string target;  // a slider key (add / set) or a family key (boost_family); empty for scale_amplitude
    double value = 0;
};
struct OppRule {
    std::string id, note;
    int priority = 0;
    bool enabled = true;
    bool stop = false;
    bool builtin = false;
    std::vector<RuleCond> all, any;
    std::vector<RuleAction> then;
};
struct RuleSet {
    int version = kOppRulesVersion;
    bool read_only = false;  // the file is newer than this build
    std::vector<OppRule> rules;
};

const RuleSet& builtin_rules();
// Built-ins overridden by id with the user's rules; user rules with new ids come after. The result is what the solver evaluates.
RuleSet merge_rules(const RuleSet& builtin, const RuleSet& user);

struct RuleHit {
    std::string id;
    int priority = 0;
    std::vector<std::string> effects;  // plain sentences: "Low block counter +0.35", "Defensive line depth -4", "ignored: ..."
};
struct RuleOutcome {
    std::map<std::string, double> add, set;  // per slider key: change to add / pinned change
    double family_boost[kFamilyCount] = {0, 0, 0, 0, 0, 0};
    double amplitude_scale = 1.0;
    std::vector<RuleHit> hits;  // in the order they were applied (priority order)
};
bool eval_condition(const RuleCond& c, const FactSet& facts);
bool rule_matches(const OppRule& r, const FactSet& facts);
// Evaluates the enabled rules of `rs` against the facts, highest priority first.
RuleOutcome evaluate_rules(const RuleSet& rs, const FactSet& facts);

// JSON of the USER rules (the built-ins are code)
std::string opp_rules_json(const RuleSet& user);
// False (with err) when the text is not a rules file. Rules with no id, an unknown op or fact, an unknown or non-opposition target, or no
// valid action are dropped and counted in *dropped; the rest is kept. A newer version loads read-only.
bool parse_opp_rules_json(const std::string& text, RuleSet& out, std::string* err, size_t* dropped = nullptr);

std::filesystem::path opp_rules_path(const std::filesystem::path& le_root);        // <le_root>\turbo_output\opp_rules.json
std::filesystem::path opp_rules_unreadable_path(const std::filesystem::path& p);   // opp_rules.unreadable.json
bool load_opp_rules(const std::filesystem::path& p, RuleSet& out, std::string* err, size_t* dropped = nullptr);  // missing file = empty set
bool save_opp_rules(const std::filesystem::path& p, const RuleSet& user, std::string* err, bool set_aside = false);

}  // namespace turbo
