// FC 27 LE Turbo GUI - the opposition variety solver (Turbo 2.0): from the facts of a fixture to a bounded set of setting changes.
//
// Pure C++: no ImGui, no game memory, no clock, no rand(), no os.time. The same fixture and seed always give the same profile.
// Style families are scored 0..1 from the facts and BLENDED (never hard-assigned); the quality of the squad sets the AMPLITUDE of the
// changes, not the style; every change is clamped to +/-max_delta (default 15 on a 0..100 scale) so a team stays recognisable.
// Rules (opp_rules.h) can boost families, add to or pin changes, and scale the amplitude; the explain output lists what fired.
// HONESTY: today only the Live opposition offsets (injury, difficulty) reach the game; the team.* changes are a preview of what a tactic
// channel would receive once one is proven. Do not describe this as delivering style variety in 2.0.0 (docs/TURBO_2_0_PLAN.md section 4).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "opp_rules.h"
#include "sliders.h"

namespace turbo {

struct TeamFacts {
    double ovr = 70;          // squad overall band: mean of the best 14 players
    int reputation = 50;      // 0..100
    int form = 50;            // 0..100
    double age = 26;          // mean age
    double pace = 60;         // mean pace rating
    double passing = 60;      // mean passing rating
    bool aerial = false;      // a clearly tall / aerial striker line
    int n_def = 4, n_mid = 4, n_att = 2;  // formation shape counts
    int n_wide = 0;           // wide forwards plus wing-backs
    int mentality = 3;        // 0..6
};
struct Fixture {
    TeamFacts user, opp;
    bool opp_home = true;     // the opponent plays at home
};
// strength_ratio = opponent / user (1 when the user's band is not positive)
double strength_ratio(const Fixture& f);

// The identity of a fixture inside a career. The same identity gives the same profile; `salt` is bumped by "re-roll".
struct FixtureId {
    int64_t career = 0, season = 0, matchday = 0, opp_teamid = 0;
    int salt = 0;
};
uint64_t fixture_seed(const FixtureId& id);  // never 0

// xorshift64*: deterministic, no global state
class SeededRng {
public:
    explicit SeededRng(uint64_t seed);
    uint64_t next();
    double unit();      // [0, 1)
    double signed_unit();  // [-1, 1)
private:
    uint64_t s_;
};

struct OppParams {
    bool enabled = true;          // false = the "none" mode: an empty, inactive profile
    double global_scale = 1.0;    // 0 = off, 1 = as configured
    int variety_strength = 50;    // 0..100, 50 = the reference amplitude
    int quality_scaling = 50;     // 0..100
    int mirroring = 0;            // 0..100
    int underdog_bias = 50;       // 0..100
    int favourite_bias = 50;      // 0..100
    int jitter = 20;              // 0..100
    int family_weight[kFamilyCount] = {50, 50, 50, 50, 50, 50};  // 0..100, 50 = neutral
    int force_family = -1;        // a family index to pin the style to (one-off per fixture / team preset); -1 = blend
    int max_delta = 15;           // clamp of every team.* change
};
// Reads the opp.* sliders of a set (an unset slider keeps the OppParams default, which is the registry default)
OppParams params_from_sliders(const SliderSet& s);

struct OppProfile {
    bool active = false;
    uint64_t seed = 0;
    double ratio = 1.0;
    double scores[kFamilyCount] = {0, 0, 0, 0, 0, 0};   // raw family scores from the facts
    double blend[kFamilyCount] = {0, 0, 0, 0, 0, 0};    // normalised weights after bias, rules and weights (sum 1 when active)
    int dominant = -1;
    double amplitude = 0;                    // the size of the biggest possible change before the clamp, in slider points
    std::map<std::string, int> team_deltas;  // team.* key -> signed change, |change| <= max_delta, zero changes left out
    std::map<std::string, int> live_offsets; // opp.* Live keys -> signed offset (injury, difficulty), clamped to the slider range
    std::vector<RuleHit> hits;               // rules that fired, in the order applied
    std::vector<std::string> explain;        // the lines explain_text prints
};
// The solver. `rules` is the merged rule set (builtin_rules() merged with the user's).
OppProfile solve_opposition(const Fixture& f, const FixtureId& id, const OppParams& p, const RuleSet& rules);
// Plain-language multi-line explanation: facts, family blend, amplitude, each change, each rule that fired and what it did.
std::string explain_text(const OppProfile& p);
// The fact set the rules see for this fixture (after mirroring and biases: family_* are the scores before rules)
FactSet facts_of(const Fixture& f, const double scores[kFamilyCount]);
// raw 0..1 family scores of one team's facts against the other's (ratio = this team / the other)
void score_families(const TeamFacts& t, double ratio, double out[kFamilyCount]);

}  // namespace turbo
