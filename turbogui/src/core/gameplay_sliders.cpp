// FC 27 LE Turbo GUI - per-side gameplay slider game variables and the A/B presets (see gameplay_sliders.h)
#include "gameplay_sliders.h"

#include <algorithm>
#include <set>

namespace turbo {
namespace gs {

namespace {
constexpr const char* kPrefix = "GAMEPLAY_CUSTOMIZATION/";
}

const std::vector<Slider>& family() {
    // Names and block indices: research.json report "research:gameplay-sliders", finding "GAMEPLAY_CUSTOMIZATION slider game
    // variables (exact names)"; every _USER / _CPU string confirmed by a string scan of turbo_output\fc27_image.bin on
    // 2026-10-11 (docs/re/gamevars_fc27.txt, section "GAMEPLAY_CUSTOMIZATION slider family").
    static const std::vector<Slider> f = {
        // basic group (error / speed), block +0x330 (USER) / +0x331 (CPU), stride 0x27 per set
        {"SPRINT_SPEED", "Sprint speed", "How fast players run flat out: higher is faster.", Group::Basic, 0},
        {"ACCELERATION", "Acceleration", "How quickly players reach top speed: higher is quicker.", Group::Basic, 1},
        {"SHOT_ERROR", "Shot error", "How far shots miss their target: higher is worse.", Group::Basic, 2},
        {"HEADER_SHOT_ACCURACY", "Header shot accuracy", "How accurate headed shots are: higher is better.", Group::Basic, 3},
        {"PASS_ERROR", "Pass error", "How often passes go astray: higher is worse.", Group::Basic, 4},
        {"HEADER_PASS_ACCURACY", "Header pass accuracy", "How accurate headed passes are: higher is better.", Group::Basic, 5},
        {"TRAP_ERROR", "First touch error", "How often the first touch goes wrong (the menu's First Touch Control Error): higher is worse.",
         Group::Basic, 6},
        {"INTERCEPT_TRAP_ERROR", "Interception trap error", "How often an intercepted ball bounces away: higher is worse.", Group::Basic, 7},
        {"TACKLE_ACCURACY", "Tackle accuracy", "How often tackles win the ball cleanly: higher is better.", Group::Basic, 8},
        {"DRIBBLE_ERROR", "Dribble error", "How often close control slips: higher is worse.", Group::Basic, 9},
        {"PASS_ANGLE_ERROR", "Pass angle error", "How far the direction of a pass is off: higher is worse.", Group::Basic, 10},
        {"BALL_DEFLECTION_ERROR", "Ball deflection error", "How unpredictable deflections are: higher is more.", Group::Basic, 11},
        {"SHOT_SPEED", "Shot speed", "How hard shots are struck: higher is harder.", Group::Basic, 12},
        {"PASS_SPEED", "Pass speed", "How hard the ball is played on a pass: higher is harder.", Group::Basic, 13},
        {"INJURY_FREQUENCY", "Injury frequency (_CPU name)", "How often players get injured. The proven injury override uses the _CPUAI name; this "
                                                            "is the _CPU name of the same block, the positive control of the A/B test.",
         Group::Basic, 14},
        {"INJURY_SEVERITY", "Injury severity (_CPU name)", "How serious injuries are (the _CPU name of the block, see injury frequency).",
         Group::Basic, 15},
        // team positioning group, block +0x1440 (USER) / +0x1441 (CPU), stride 7 per set
        {"POS_MARKING", "Marking", "How closely defenders stay with their man: higher is tighter.", Group::Positioning, 0},
        {"POS_LINE_LENGTH", "Line length", "How far apart the lines stand: higher is more stretched.", Group::Positioning, 1},
        {"POS_LINE_WIDTH", "Line width", "How wide the team spreads: higher is wider.", Group::Positioning, 2},
        {"POS_DEF_LINE_HEIGHT", "Defensive line height", "How high up the pitch the back line stands: higher is higher.", Group::Positioning, 3},
        {"POS_RUN_FREQUENCY", "Run frequency", "How often players run beyond the ball: higher is more.", Group::Positioning, 4},
        {"POS_FULL_BACK", "Fullback positioning", "How far forward the fullbacks go: higher is further.", Group::Positioning, 5},
        {"GK_ABILITY", "Goalkeeper ability", "How good goalkeepers are at saving shots: higher is better.", Group::Positioning, 6},
        // CPU behaviour group, block +0x1488 (USER = CPU teammates, ASSESSED) / +0x14A8 (CPU = CPU opponent)
        {"DEFENDING_AGGRESSION", "Defending aggression", "How hard the CPU closes down and tackles: higher is more.", Group::CpuBehaviour, 0},
        {"STAND_TACKLE_FREQUENCY", "Standing tackle frequency", "How often the CPU goes in standing: higher is more.", Group::CpuBehaviour, 1},
        {"SLIDE_TACKLE_FREQUENCY", "Slide tackle frequency", "How often the CPU slides in: higher is more.", Group::CpuBehaviour, 2},
        {"PROFESSIONAL_FOUL_FREQUENCY", "Professional foul frequency", "How often the CPU takes a man down: higher is more.", Group::CpuBehaviour, 3},
        {"NORMAL_SHOT_FREQUENCY", "Shot frequency", "How often the CPU decides to shoot: higher is more.", Group::CpuBehaviour, 4},
        {"BUILDUP_SPEED", "Build-up speed", "How quickly the CPU moves the ball forward: higher is quicker.", Group::CpuBehaviour, 5},
        {"SKILL_MOVE_FREQUENCY", "Skill move frequency", "How often the CPU tries a skill move: higher is more.", Group::CpuBehaviour, 6},
        {"NORMAL_CROSSING_FREQUENCY", "Crossing frequency", "How often the CPU crosses: higher is more.", Group::CpuBehaviour, 7},
        {"EARLY_CROSSING_FREQUENCY", "Early crossing frequency", "How often the CPU crosses early: higher is more.", Group::CpuBehaviour, 8},
        {"FIRST_TOUCH_PASS_FREQUENCY", "First-time pass frequency", "How often the CPU passes first time: higher is more.", Group::CpuBehaviour, 9},
        {"DRIBBLE_FREQUENCY", "Dribble frequency", "How often the CPU takes a defender on: higher is more.", Group::CpuBehaviour, 10},
    };
    return f;
}

const Slider* slider(const std::string& id) {
    for (const Slider& s : family())
        if (id == s.id) return &s;
    return nullptr;
}

const char* side_suffix(Side s) { return s == Side::User ? "USER" : "CPU"; }
const char* side_label(Side s) { return s == Side::User ? "your team" : "CPU team"; }

const char* group_name(Group g) {
    switch (g) {
        case Group::Basic: return "Basic";
        case Group::Positioning: return "Team positioning";
        case Group::CpuBehaviour: return "CPU behaviour";
    }
    return "Basic";
}

std::string var_name(const Slider& s, Side side) { return std::string(kPrefix) + s.id + "_" + side_suffix(side); }
std::string var_name(const std::string& id, Side side) { return std::string(kPrefix) + id + "_" + side_suffix(side); }

bool parse_var(const std::string& name, const Slider*& out, Side& side) {
    out = nullptr;
    const std::string pre = kPrefix;
    if (name.rfind(pre, 0) != 0) return false;
    std::string rest = name.substr(pre.size());
    for (Side s : {Side::User, Side::Cpu}) {
        const std::string suf = std::string("_") + side_suffix(s);
        if (rest.size() > suf.size() && rest.compare(rest.size() - suf.size(), suf.size(), suf) == 0) {
            const Slider* sl = slider(rest.substr(0, rest.size() - suf.size()));
            if (!sl) return false;
            out = sl;
            side = s;
            return true;
        }
    }
    return false;
}

std::vector<std::string> all_var_names() {
    std::vector<std::string> out;
    for (const Slider& s : family()) {
        out.push_back(var_name(s, Side::User));
        out.push_back(var_name(s, Side::Cpu));
    }
    return out;
}

std::string registry_key(const Slider& s, Side side) {
    std::string k = "match.";
    for (const char* p = s.id; *p; ++p) k += (*p >= 'A' && *p <= 'Z') ? char(*p - 'A' + 'a') : *p;
    k += side == Side::User ? "_user" : "_cpu";
    // the proven injury rows already own match.injury_frequency_user / _cpu (the _USER and _CPUAI names): the block's own
    // _USER / _CPU injury names get their own keys
    if (s.group == Group::Basic && (s.block_index == 14 || s.block_index == 15)) k += "_block";
    return k;
}

// ---------------------------------------------------------------- A/B presets
const std::vector<Preset>& presets() {
    static const std::vector<Preset> p = [] {
        std::vector<Preset> v;
        v.push_back({"baseline", "A/B: baseline (clear every override)",
                     "Arm A. Every game override is cleared and nothing is sent: the match is played exactly as the game decides. Note the "
                     "CPU's pace in foot races, its pass accuracy and shots on target in Match Facts, and how many CPU players get injured.",
                     true, {}});
        std::vector<Write> w;
        auto cpu = [&](const char* id, int32_t value) { w.push_back({var_name(id, Side::Cpu), value}); };
        cpu("SPRINT_SPEED", 0);
        cpu("ACCELERATION", 0);
        cpu("PASS_ERROR", 100);
        cpu("SHOT_ERROR", 100);
        cpu("TRAP_ERROR", 100);
        cpu("GK_ABILITY", 0);
        cpu("POS_DEF_LINE_HEIGHT", 100);
        cpu("POS_RUN_FREQUENCY", 0);
        cpu("INJURY_FREQUENCY", 100);  // positive control: the _CPU name of the block (the proven override uses _CPUAI)
        cpu("INJURY_SEVERITY", 100);
        v.push_back({"cpu_extremes", "A/B: CPU handicap extremes",
                     "Arm B. Every override is cleared first, then the CPU side gets extreme values: sprint speed and acceleration 0, pass, shot "
                     "and first touch error 100, goalkeeper ability 0, defensive line height 100, run frequency 0, and injury frequency and "
                     "severity 100 through the _CPU names as the positive control. If channel A reaches the pitch, CPU forwards are visibly slow "
                     "in replays, the CPU's pass accuracy in Match Facts is far below the baseline, their keeper lets soft shots in, their back "
                     "line sits near halfway, and 2 or more CPU players get injured in the first half. Your own team is untouched.",
                     true, std::move(w)});
        return v;
    }();
    return p;
}

const Preset* preset(const std::string& id) {
    for (const Preset& p : presets())
        if (id == p.id) return &p;
    return nullptr;
}

std::vector<std::string> describe(const Preset& p) {
    std::vector<std::string> out;
    if (p.clear_all_first) out.push_back(p.writes.empty() ? "clear every override, send nothing" : "clear every override first");
    for (const Write& w : p.writes) out.push_back(w.name + " = " + std::to_string(w.value));
    return out;
}

std::string plan(const Preset& p, bool tactics_off, bool gamevar_off, const std::string& gamevar_why, std::vector<Write>& out) {
    out.clear();
    if (tactics_off) return "Tactics are off: turbo_output\\tactics_off.txt exists, nothing is sent";
    if (gamevar_off) return "Game variables are off: " + (gamevar_why.empty() ? std::string("the host refuses them") : gamevar_why);
    for (const Write& w : p.writes) {
        const gv::KnownVar* kv = gv::known(w.name);
        if (!kv) return w.name + " is not a variable Turbo knows: preset refused";
        out.push_back({w.name, std::clamp<int32_t>(w.value, kv->min, kv->max)});
    }
    return "";
}

ApplyReport apply(msetup::Service& svc, const Preset& p, const std::vector<Write>& planned) {
    ApplyReport r;
    std::string why;
    if (p.clear_all_first) {
        if (svc.clear_all(why)) r.cleared_all = 1;
        else {
            ++r.refused;
            if (r.first_error.empty()) r.first_error = "clear all: " + why;
        }
    }
    for (const Write& w : planned) {
        const gv::KnownVar* kv = gv::known(w.name);
        if (!kv || w.value == kOff) {
            ++r.refused;
            if (r.first_error.empty()) r.first_error = w.name + (kv ? ": 'off' is never sent" : ": not a variable Turbo knows");
            continue;
        }
        const int32_t v = std::clamp<int32_t>(w.value, kv->min, kv->max);
        if (svc.set_var(w.name, v, why)) ++r.sent;
        else {
            ++r.refused;
            if (r.first_error.empty()) r.first_error = w.name + ": " + why;
        }
    }
    return r;
}

std::string validate() {
    std::string bad;
    std::set<std::string> ids, names;
    for (const Slider& s : family()) {
        if (!s.id || !*s.id || !s.label || !*s.label || !s.what || !*s.what) bad += "a slider has an empty id, label or text\n";
        if (!ids.insert(s.id ? s.id : "").second) bad += std::string("duplicate slider id ") + s.id + "\n";
        for (Side side : {Side::User, Side::Cpu}) {
            const std::string n = var_name(s, side);
            if (!gv::check_name(n).empty()) bad += n + ": " + gv::check_name(n) + "\n";
            if (!names.insert(n).second) bad += "duplicate name " + n + "\n";
            const Slider* back = nullptr;
            Side bs = Side::User;
            if (!parse_var(n, back, bs) || back != &s || bs != side) bad += n + ": parse_var does not round-trip\n";
        }
    }
    if (family().size() != 34) bad += "the family must hold 34 sliders\n";
    if (names.size() != 68) bad += "the family must give 68 names\n";
    if (presets().size() != 2 || !preset("baseline") || !preset("cpu_extremes")) bad += "two presets: baseline and cpu_extremes\n";
    for (const Preset& p : presets()) {
        if (!p.clear_all_first) bad += std::string(p.id) + ": must clear every override first\n";
        for (const Write& w : p.writes) {
            const Slider* s = nullptr;
            Side side = Side::User;
            if (!parse_var(w.name, s, side)) bad += std::string(p.id) + ": " + w.name + " is not a name of the family\n";
            else if (side != Side::Cpu) bad += std::string(p.id) + ": " + w.name + " is not a CPU-side name\n";
            if (w.value < kMin || w.value > kMax) bad += std::string(p.id) + ": " + w.name + " outside 0..100\n";
            if (w.value == kOff) bad += std::string(p.id) + ": " + w.name + " is 'off'\n";
            if (!gv::known(w.name)) bad += std::string(p.id) + ": " + w.name + " is not in gv::known_vars()\n";
        }
    }
    if (const Preset* b = preset("baseline"))
        if (!b->writes.empty()) bad += "the baseline sends nothing\n";
    return bad;
}

}  // namespace gs
}  // namespace turbo
