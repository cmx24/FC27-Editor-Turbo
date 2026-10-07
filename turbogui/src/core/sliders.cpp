// FC 27 LE Turbo GUI - the tactics slider registry (see sliders.h and docs/TURBO_2_0_PLAN.md section 3)
#include "sliders.h"

#include <algorithm>
#include <cstdlib>

namespace turbo {

const char* scope_name(SliderScope s) {
    switch (s) {
        case SliderScope::Match: return "match";
        case SliderScope::Team: return "team";
        case SliderScope::Position: return "position";
        case SliderScope::Opposition: return "opposition";
    }
    return "match";
}

const char* status_name(SliderStatus s) {
    switch (s) {
        case SliderStatus::Live: return "Live";
        case SliderStatus::DB: return "DB";
        case SliderStatus::Local: return "Local";
        case SliderStatus::Preview: return "Preview";
        case SliderStatus::RE: return "RE";
    }
    return "RE";
}

const char* status_badge_help(SliderStatus s) {
    switch (s) {
        case SliderStatus::Live: return "Sent to the game as a game variable; takes effect in played matches.";
        case SliderStatus::DB: return "Written to the career database.";
        case SliderStatus::Local: return "A Turbo setting: it shapes Turbo's own calculation and is not sent to the game.";
        case SliderStatus::Preview: return "Shown in the preview only: no way to write it to the game is proven yet.";
        case SliderStatus::RE: return "Shown in the preview only: nobody knows yet where the game reads this setting.";
    }
    return "";
}

namespace {

SliderScope scope_of_key(const std::string& key) {
    if (key.rfind("team.", 0) == 0) return SliderScope::Team;
    if (key.rfind("pos.", 0) == 0) return SliderScope::Position;
    if (key.rfind("opp.", 0) == 0) return SliderScope::Opposition;
    return SliderScope::Match;
}

const char* scope_prefix(SliderScope s) {
    switch (s) {
        case SliderScope::Match: return "match.";
        case SliderScope::Team: return "team.";
        case SliderScope::Position: return "pos.";
        case SliderScope::Opposition: return "opp.";
    }
    return "match.";
}

struct Builder {
    std::vector<SliderDef> v;

    SliderDef& base(const std::string& key, const std::string& group, const std::string& label, const std::string& desc) {
        v.emplace_back();
        SliderDef& d = v.back();
        d.key = key;
        d.scope = scope_of_key(key);
        d.group = group;
        d.label = label;
        d.description = desc;
        return d;
    }
    // NEEDS-RE 0..100 slider: shown as a preview, never written
    SliderDef& re(const std::string& key, const char* group, const char* label, const char* desc, const char* tech = "") {
        SliderDef& d = base(key, group, label, desc);
        d.technical = tech;
        d.status = SliderStatus::RE;
        d.evidence = SliderEvidence::NeedsRe;
        return d;
    }
    SliderDef& re_enum(const std::string& key, const char* group, const char* label, const char* desc, std::vector<std::string> names,
                       int def) {
        SliderDef& d = re(key, group, label, desc);
        d.kind = SliderKind::Enum;
        d.min = 0;
        d.max = static_cast<int>(names.size()) - 1;
        d.def = def;
        d.labels = std::move(names);
        d.labels_provisional = true;
        return d;
    }
    // proven game variable (REAL)
    SliderDef& gv(const std::string& key, const char* group, const char* label, const char* desc, const char* var, int lo, int hi, int def) {
        SliderDef& d = base(key, group, label, desc);
        d.technical = var;
        d.min = lo;
        d.max = hi;
        d.def = def;
        d.status = SliderStatus::Live;
        d.evidence = SliderEvidence::Real;
        d.binding = {BindingKind::GameVar, {var}};
        return d;
    }
    // career database field that exists in the real FC 27 schema (turbo/le27/fc27_db_schema.json); `evidence` Plausible = the field and range
    // are proven, an in-game effect is not. Targets are "table.field"; "{n}" stands for a slot number 0..10.
    SliderDef& db(const std::string& key, const char* group, const char* label, const char* desc, std::vector<std::string> targets, int lo, int hi,
                  int def, SliderEvidence ev = SliderEvidence::Plausible) {
        SliderDef& d = base(key, group, label, desc);
        d.technical = targets.empty() ? "" : targets.front();
        d.min = lo;
        d.max = hi;
        d.def = def;
        d.status = SliderStatus::DB;
        d.evidence = ev;
        d.binding = {BindingKind::DbField, std::move(targets)};
        return d;
    }
    SliderDef& local(const std::string& key, const char* group, const std::string& label, const char* desc, int lo, int hi, int def) {
        SliderDef& d = base(key, group, label, desc);
        d.min = lo;
        d.max = hi;
        d.def = def;
        d.status = SliderStatus::Local;
        d.evidence = SliderEvidence::Real;
        d.binding = {BindingKind::Turbo, {key}};
        return d;
    }
};

std::vector<SliderDef> build() {
    Builder b;
    const std::vector<std::string> off_on = {"Off", "On"};

    // ------------------------------------------------------------------ match settings (both teams); REAL = game variables
    b.gv("match.injury_frequency_user", "Injuries", "Injury frequency (your team)", "How often players of your team get injured in played matches.",
         "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER", 0, 100, 50).per_side = true;
    b.gv("match.injury_frequency_cpu", "Injuries", "Injury frequency (CPU team)", "How often players of the CPU team get injured in played matches.",
         "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI", 0, 100, 50).per_side = true;
    b.gv("match.injury_severity_user", "Injuries", "Injury severity (your team)", "How serious the injuries of your team are in played matches.",
         "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_USER", 0, 100, 50).per_side = true;
    b.gv("match.injury_severity_cpu", "Injuries", "Injury severity (CPU team)", "How serious the injuries of the CPU team are in played matches.",
         "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_CPUAI", 0, 100, 50).per_side = true;
    {
        auto& d = b.gv("match.never_injure", "Injuries", "Injuries off", "Nobody gets injured in played matches.", "NEVER_INJURE", 0, 1, 1);
        d.kind = SliderKind::Toggle;
        d.labels = off_on;
    }
    b.gv("match.weather", "Conditions", "Weather", "The weather of the next played matches.", "OVERRIDE/WEATHER", 0, 8, 0).kind = SliderKind::Enum;
    {
        auto& d = b.gv("match.time_of_day", "Conditions", "Time of day", "The time of day of the next played matches.", "OVERRIDE/TOD", 0, 4, 0);
        d.kind = SliderKind::Enum;
        d.allowed = {0, 1, 3, 4};  // the game ignores 2 (match_setup.cpp)
    }
    {
        auto& d = b.gv("match.difficulty", "Conditions", "Difficulty", "The difficulty of the next played matches.", "OVERRIDE_MATCH_DIFFICULTY", 0, 5, 3);
        d.kind = SliderKind::Enum;
        d.labels = {"Beginner", "Amateur", "Semi-Pro", "Professional", "World Class", "Legendary"};
    }
    {
        auto& d = b.gv("match.cpu_subs_off", "Conditions", "CPU makes no substitutions", "The CPU team keeps its starting eleven.",
                       "DISABLE_CPU_SUBSTITUTION", 0, 1, 1);
        d.kind = SliderKind::Toggle;
        d.labels = off_on;
    }
    // NEEDS-RE
    b.re("match.sprint_speed", "Player speed", "Sprint speed", "How fast players run flat out.");
    b.re("match.acceleration", "Player speed", "Acceleration", "How quickly players reach top speed.");
    b.re("match.pass_error", "Passing", "Pass error", "How often passes go astray.");
    b.re("match.pass_speed", "Passing", "Pass speed", "How hard the ball is played on a pass.");
    b.re("match.shot_error", "Shooting", "Shot error", "How far shots miss their target.");
    b.re("match.shot_speed", "Shooting", "Shot speed", "How hard shots are struck.");
    b.re("match.shot_frequency", "Shooting", "Shot frequency", "How often players decide to shoot.");
    b.re("match.first_touch_error", "Ball control", "First touch error", "How often the first touch goes wrong.");
    b.re("match.trap_error", "Ball control", "Trapping error", "How often a received ball bounces away.");
    b.re("match.ball_control_error", "Ball control", "Ball control error", "How often dribbling and close control slip.");
    b.re("match.gk_ability", "Goalkeepers", "Goalkeeper ability", "How good goalkeepers are at saving shots.");
    // the referee table (every referee row); the schema gives the range 0..2, not which end is the strict one
    b.db("match.foul_strictness", "Referee", "Foul strictness", "How readily the referee whistles a challenge as a foul.", {"referee.foulstrictness"}, 0, 2, 1)
        .kind = SliderKind::Enum;
    b.db("match.card_strictness", "Referee", "Card strictness", "How readily the referee shows cards.", {"referee.cardstrictness"}, 0, 2, 1).kind =
        SliderKind::Enum;

    // ------------------------------------------------------------------ team settings
    {
        auto& d = b.base("team.formation", "Shape", "Formation", "The formation of the team.");
        d.kind = SliderKind::Enum;
        d.min = 0;
        d.max = 0;
        d.def = 0;  // the list comes from the formations table at run time, or the built-in table
        d.range_from_meta = true;
        d.technical = "formations table";
        d.status = SliderStatus::Preview;
        d.evidence = SliderEvidence::Plausible;
        d.binding = {BindingKind::DbField, {"formations"}};
    }
    b.re_enum("team.mentality", "Shape", "Mentality", "How attacking the team plays overall.",
              {"Very defensive", "Defensive", "Cautious", "Balanced", "Positive", "Attacking", "Very attacking"}, 3)
        .technical = "cm_mentalities";
    b.db("team.line_depth", "Defensive shape", "Defensive line depth", "How high up the pitch the back line stands.",
         {"teams.defensivedepth", "cm_mentalities.defensivedepth"}, 1, 100, 50);
    b.re("team.line_length", "Defensive shape", "Compactness", "How tightly the lines stay together: higher is tighter.", "line length");
    b.db("team.width_def", "Width", "Defensive width", "How wide the team spreads without the ball.", {"mentalities.defensivewidth"}, 1, 100, 50);
    b.db("team.width_att", "Width", "Attacking width", "How wide the team spreads with the ball.", {"mentalities.offensivewidth"}, 1, 100, 50);
    b.re("team.marking", "Defensive shape", "Marking tightness", "How closely defenders stay with their man.", "defence positioning");
    b.re("team.press_intensity", "Pressing", "Pressure intensity", "How hard the team closes the ball down.");
    b.re("team.press_trigger", "Pressing", "Press trigger", "How easily a loose touch sets the press off.");
    b.re("team.engagement_height", "Pressing", "Engagement height", "How far up the pitch the team starts to press.", "line of engagement");
    b.re("team.forward_runs", "Attacking", "Forward runs", "How often players run beyond the ball.");
    b.db("team.players_in_box_cross", "Attacking", "Players in the box (crosses)", "How many players join the attack in the box on a cross.",
         {"mentalities.playersinboxcross"}, 0, 9, 4);
    b.db("team.players_in_box_corner", "Set pieces", "Players in the box (corners)", "How many players go into the box on a corner.",
         {"mentalities.playersinboxcorner"}, 0, 4, 2);
    b.db("team.players_in_box_fk", "Set pieces", "Players in the box (free kicks)", "How many players go into the box on a free kick.",
         {"mentalities.playersinboxfk"}, 0, 4, 2);
    b.re("team.tempo", "Attacking", "Tempo", "How quickly the team moves the ball forward.");
    b.re("team.directness", "Attacking", "Pass directness", "Short and patient, or long and direct.");
    b.re("team.buildup_short", "Attacking", "Short build-up", "How much the team builds out from the back with short passes.");
    b.re("team.fluidity", "Attacking", "Creative freedom", "How free players are to leave their positions.", "fluidity");
    b.re("team.cross_freq", "Attacking", "Cross frequency", "How often the team crosses.");
    b.re("team.shot_patience", "Attacking", "Shot patience", "How long players work for a better chance before shooting.");
    b.re("team.dribble_freq", "Attacking", "Dribble frequency", "How often players take on a defender.");
    b.re("team.counter_press", "Transitions", "Counter-press", "How hard the team presses right after losing the ball.");
    b.re("team.counter_attack", "Transitions", "Counter-attack", "How quickly the team breaks after winning the ball.");
    b.re("team.regroup_depth", "Transitions", "Regroup depth", "How deep the team drops to regroup.");
    b.re("team.offside_trap", "Defending", "Offside trap", "How often the back line steps up to catch attackers offside.");
    b.re("team.tackle_aggr", "Defending", "Tackle aggression", "How readily defenders go in for a tackle.");
    // the four style enums: the schema gives the ranges, not the names of the steps, so no labels are shown
    b.db("team.buildup_play", "Style", "Build-up play", "How the team builds out of defence.", {"teams.buildupplay", "cm_mentalities.buildupplay"}, 0, 3, 0)
        .kind = SliderKind::Enum;
    b.db("team.chance_creation", "Style", "Chance creation", "How the team creates chances.", {"mentalities.chancecreation"}, 0, 3, 0).kind =
        SliderKind::Enum;
    b.db("team.offensive_style", "Style", "Attacking style", "How the team attacks.", {"mentalities.offensivestyle"}, 0, 3, 0).kind = SliderKind::Enum;
    b.db("team.defensive_style", "Style", "Defensive style", "How the team defends.", {"mentalities.defensivestyle"}, 0, 4, 0).kind = SliderKind::Enum;

    // ------------------------------------------------------------------ position / role settings
    for (int i = 1; i <= 9; ++i) {
        const std::string n = std::to_string(i);
        auto& d = b.base("pos.role" + n, "Roles", "Role " + n, "One of the roles of the player, as the game stores it.");
        d.kind = SliderKind::Enum;
        d.min = 0;
        d.max = 199;  // players.role1..9 in the FC 27 schema
        d.def = 0;
        d.range_from_meta = true;
        d.technical = "players.role" + n;
        d.status = SliderStatus::DB;
        d.evidence = SliderEvidence::Real;
        d.binding = {BindingKind::DbField, {"players.role" + n}};
    }
    for (int k = 0; k < 2; ++k) {
        const bool pp = k == 0;
        auto& d = b.base(pp ? "pos.preferred_positions" : "pos.traits", "Roles", pp ? "Preferred positions" : "Traits",
                         pp ? "The positions the player is best at." : "The special traits of the player.");
        d.kind = pp ? SliderKind::Enum : SliderKind::Range;  // traits: a bit field
        d.min = 0;
        d.max = pp ? 31 : 1073741823;
        d.def = 0;
        d.range_from_meta = true;
        d.technical = pp ? "players.preferredposition1" : "players.trait1";
        d.status = SliderStatus::DB;
        d.evidence = SliderEvidence::Real;
        d.binding = {BindingKind::DbField, {d.technical}};
    }
    for (int k = 0; k < 2; ++k) {
        const bool x = k == 0;
        // formations.offset0x..offset10y: the schema has the fields but no range, so the slider is a share of the pitch (0..100) that the
        // apply code converts; the formation row may be overwritten by the game's team sheet at save
        auto& d = b.db(x ? "pos.formation_x" : "pos.formation_y", "Slot", x ? "Slot offset, across" : "Slot offset, up the pitch",
                       "Where the formation slot of this player stands, as a share of the pitch.",
                       {x ? "formations.offset{n}x" : "formations.offset{n}y"}, 0, 100, 50);
        d.range_from_meta = true;
    }
    b.re("pos.attack_bias", "Duty", "Attack bias", "How much this player favours attacking over defending (a duty seeds it).");
    b.re("pos.forward_runs", "Movement", "Forward runs", "How often this player runs beyond the ball.", "get further forward");
    b.re("pos.depth_bias", "Movement", "Depth bias", "How far up or back this player stands.", "stay back");
    b.re("pos.roam", "Movement", "Roam from position", "How freely this player leaves his position.");
    b.re("pos.hold_pos", "Movement", "Hold position", "How strictly this player keeps his position.");
    b.re("pos.width_bias", "Movement", "Width bias", "How wide this player stands.");
    b.re("pos.close_down", "Defending", "Close down", "How quickly this player closes the ball down.");
    b.re("pos.tackle_aggr", "Defending", "Tackle aggression", "How readily this player tackles.");
    b.re("pos.mark_tight", "Defending", "Mark tight", "How closely this player marks his man.");
    b.re("pos.shot_freq", "Attacking", "Shoot more or less", "How often this player shoots.");
    b.re("pos.dribble_freq", "Attacking", "Dribble more or less", "How often this player dribbles.");
    b.re("pos.risk_passing", "Attacking", "Risk passing", "How ambitious this player's passes are.");
    b.re("pos.cross_depth", "Attacking", "Cross from deep or byline", "Where this player crosses from: deep (low) or the byline (high).");

    // ------------------------------------------------------------------ opposition (offsets are signed: 0 = no offset)
    b.gv("opp.injury_frequency_offset", "Injuries", "Injury frequency offset (CPU)",
         "Added to the CPU team's injury frequency when the opposition is set up.", "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI", -50, 50, 0)
        .per_side = true;
    b.gv("opp.injury_severity_offset", "Injuries", "Injury severity offset (CPU)",
         "Added to the CPU team's injury severity when the opposition is set up.", "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_CPUAI", -50, 50, 0)
        .per_side = true;
    b.gv("opp.difficulty_offset", "Difficulty", "Difficulty offset",
         "Steps added to the difficulty for this opponent. The game applies it to every match, not to one team.", "OVERRIDE_MATCH_DIFFICULTY", -5, 5, 0);
    b.local("opp.variety_strength", "Solver", "Variety strength", "How far opponents move away from the game's own style: 0 = not at all.", 0, 100, 50);
    b.local("opp.quality_scaling", "Solver", "Quality scaling", "How much a stronger squad widens the changes: 0 = every squad gets the same amount.", 0, 100, 50);
    b.local("opp.mirroring", "Solver", "Mirroring", "How much the opponent copies the style of your own team.", 0, 100, 0);
    b.local("opp.underdog_bias", "Solver", "Underdog bias", "How strongly weaker opponents drop back and play direct.", 0, 100, 50);
    b.local("opp.favourite_bias", "Solver", "Favourite bias", "How strongly stronger opponents press and keep the ball.", 0, 100, 50);
    b.local("opp.jitter", "Solver", "Jitter", "A small seeded wobble on every change, so two teams of one style still differ.", 0, 100, 20);
    for (const char* fam : {"gegenpress", "low_block", "possession", "wing_play", "direct", "balanced"}) {
        std::string label = fam;
        for (char& c : label)
            if (c == '_') c = ' ';
        label[0] = static_cast<char>(label[0] - 'a' + 'A');
        b.local(std::string("opp.family_weight_") + fam, "Style families", label + " weight",
                "How likely this style family is to be picked: 50 = neutral, 0 = never.", 0, 100, 50);
    }
    b.re_enum("opp.formation", "Opponent", "Opponent formation", "The formation the CPU team starts with.", {"Game decides"}, 0)
        .range_from_meta = true;
    b.re("opp.cpu_error_offset", "Opponent", "CPU error offset", "How much the CPU's passing and shooting errors change by quality tier.");
    b.re("opp.cpu_aggr_offset", "Opponent", "CPU aggression offset", "How much the CPU's tackling and pressing change by quality tier.");
    b.re("opp.adaptation", "Opponent", "Mid-match adaptation", "How quickly the CPU changes its approach during a match.");
    return b.v;
}

}  // namespace

const std::vector<SliderDef>& slider_registry() {
    static const std::vector<SliderDef> r = build();
    return r;
}

const SliderDef* find_slider(const std::string& key) {
    static const std::map<std::string, size_t> idx = [] {
        std::map<std::string, size_t> m;
        const auto& r = slider_registry();
        for (size_t i = 0; i < r.size(); ++i) m.emplace(r[i].key, i);  // first wins on a (test-caught) duplicate
        return m;
    }();
    auto it = idx.find(key);
    return it == idx.end() ? nullptr : &slider_registry()[it->second];
}

std::vector<const SliderDef*> sliders_in_scope(SliderScope scope) {
    std::vector<const SliderDef*> out;
    for (const SliderDef& d : slider_registry())
        if (d.scope == scope) out.push_back(&d);
    return out;
}

std::string validate_registry() {
    std::string bad;
    std::set<std::string> seen;
    auto fail = [&](const SliderDef& d, const std::string& why) { bad += d.key + ": " + why + "\n"; };
    for (const SliderDef& d : slider_registry()) {
        if (d.key.empty()) {
            fail(d, "empty key");
            continue;
        }
        if (!seen.insert(d.key).second) fail(d, "duplicate key");
        if (d.key.rfind(scope_prefix(d.scope), 0) != 0) fail(d, "key prefix does not match the scope");
        if (d.label.empty() || d.description.empty() || d.group.empty()) fail(d, "label, description or group missing");
        if (d.min > d.max) fail(d, "min > max");
        if (d.def < d.min || d.def > d.max) fail(d, "default outside the range");
        if (d.step < 1) fail(d, "step < 1");
        for (int a : d.allowed)
            if (a < d.min || a > d.max) fail(d, "allowed value outside the range");
        if (!d.allowed.empty() && std::find(d.allowed.begin(), d.allowed.end(), d.def) == d.allowed.end()) fail(d, "default not an allowed value");
        if (!d.labels.empty() && static_cast<int>(d.labels.size()) != d.max - d.min + 1) fail(d, "labels do not cover the range");
        if (d.kind == SliderKind::Toggle && (d.min != 0 || d.max != 1)) fail(d, "toggle must be 0..1");
        const bool gv = d.binding.kind == BindingKind::GameVar, db = d.binding.kind == BindingKind::DbField;
        if (d.status == SliderStatus::Live && (!gv || d.binding.targets.empty())) fail(d, "Live needs a game variable binding");
        if (d.status == SliderStatus::DB && (!db || d.binding.targets.empty())) fail(d, "DB needs a database binding");
        if (d.status == SliderStatus::Local && d.binding.kind != BindingKind::Turbo) fail(d, "Local needs a Turbo binding");
        if (d.status == SliderStatus::RE && d.binding.kind != BindingKind::None) fail(d, "RE must have no binding");
        if (d.status == SliderStatus::RE && d.evidence != SliderEvidence::NeedsRe) fail(d, "RE must be NEEDS-RE");
        if (d.status == SliderStatus::Preview && d.evidence == SliderEvidence::Real) fail(d, "a REAL slider is not Preview");
        if (d.status == SliderStatus::Live && d.evidence != SliderEvidence::Real) fail(d, "Live must be REAL");
        if (d.status == SliderStatus::DB && d.evidence == SliderEvidence::NeedsRe) fail(d, "DB cannot be NEEDS-RE");
    }
    return bad;
}

int clamp_slider(const SliderDef& d, int v) {
    if (d.min >= d.max) return d.min;
    if (!d.allowed.empty()) {
        int best = d.allowed.front();
        long bd = std::labs(static_cast<long>(v) - best);
        for (int a : d.allowed) {
            const long dist = std::labs(static_cast<long>(v) - a);
            if (dist < bd || (dist == bd && a < best)) {
                best = a;
                bd = dist;
            }
        }
        return best;
    }
    v = std::max(d.min, std::min(d.max, v));
    if (d.step > 1) {
        const long off = v - d.min;
        const long lo = (off / d.step) * d.step, hi = lo + d.step;
        const long pick = (off - lo) < (hi - off) ? lo : hi;
        v = static_cast<int>(std::min<long>(d.max, d.min + pick));
    }
    return v;
}

bool slider_value_ok(const SliderDef& d, int v) { return clamp_slider(d, v) == v; }

bool SliderSet::set(const std::string& key, int value, bool enable) {
    const SliderDef* d = find_slider(key);
    if (!d) return false;
    values[key] = clamp_slider(*d, value);
    if (enable) enabled.insert(key);
    return true;
}

int SliderSet::value_or_default(const std::string& key) const {
    auto it = values.find(key);
    if (it != values.end()) return it->second;
    const SliderDef* d = find_slider(key);
    return d ? d->def : 0;
}

bool SliderSet::set_enabled(const std::string& key, bool on) {
    const SliderDef* d = find_slider(key);
    if (!d) return false;
    if (on) {
        values.emplace(key, d->def);
        enabled.insert(key);
    } else {
        enabled.erase(key);
    }
    return true;
}

void SliderSet::reset(const std::string& key) {
    values.erase(key);
    enabled.erase(key);
    unknown_values.erase(key);
    unknown_enabled.erase(key);
}

void SliderSet::clear() {
    values.clear();
    enabled.clear();
    unknown_values.clear();
    unknown_enabled.clear();
}

std::vector<std::string> SliderSet::enabled_keys(SliderScope scope) const {
    std::vector<std::string> out;
    for (const SliderDef& d : slider_registry())
        if (d.scope == scope && enabled.count(d.key)) out.push_back(d.key);
    return out;
}

std::vector<SliderChange> diff_sliders(const SliderSet& a, const SliderSet& b) {
    std::vector<SliderChange> out;
    for (const SliderDef& d : slider_registry()) {
        SliderChange c;
        c.key = d.key;
        c.has_a = a.is_enabled(d.key);
        c.has_b = b.is_enabled(d.key);
        c.a = a.value_or_default(d.key);
        c.b = b.value_or_default(d.key);
        if (c.has_a != c.has_b || (c.has_a && c.a != c.b)) out.push_back(c);
    }
    std::set<std::string> keys;
    for (const auto& kv : a.unknown_values) keys.insert(kv.first);
    for (const auto& kv : b.unknown_values) keys.insert(kv.first);
    for (const std::string& k : keys) {
        auto ia = a.unknown_values.find(k);
        auto ib = b.unknown_values.find(k);
        const bool ea = ia != a.unknown_values.end() && a.unknown_enabled.count(k) > 0;
        const bool eb = ib != b.unknown_values.end() && b.unknown_enabled.count(k) > 0;
        const bool same = (ia == a.unknown_values.end()) == (ib == b.unknown_values.end()) &&
                          (ia == a.unknown_values.end() || ia->second == ib->second);
        if (ea != eb || (ea && !same)) {
            SliderChange c;
            c.key = k;
            c.has_a = ea;
            c.has_b = eb;
            out.push_back(c);
        }
    }
    return out;
}

std::vector<SliderWrite> game_writes(const SliderSet& s) {
    std::vector<SliderWrite> out;
    for (const SliderDef& d : slider_registry()) {
        if (!writes_game(d) || !s.is_enabled(d.key)) continue;
        for (const std::string& t : d.binding.targets) out.push_back({d.key, t, s.value_or_default(d.key), d.status});
    }
    return out;
}

size_t count_preview_only(const std::vector<const SliderDef*>& visible) {
    size_t n = 0;
    for (const SliderDef* d : visible)
        if (d && preview_only(*d)) ++n;
    return n;
}

}  // namespace turbo
