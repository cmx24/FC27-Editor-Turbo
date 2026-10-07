// FC 27 LE Turbo GUI - the opposition variety solver (see opposition.h)
#include "opposition.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace turbo {

namespace {

uint64_t splitmix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

// smooth 0..1 ramp from lo to hi
double ramp(double x, double lo, double hi) {
    if (hi <= lo) return x >= hi ? 1.0 : 0.0;
    const double t = clampd((x - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
double inv_ramp(double x, double lo, double hi) { return 1.0 - ramp(x, lo, hi); }

// The direction each style family pushes each team setting, -1..1 (design proposals, not sourced values).
// columns: gegenpress, low_block, possession, wing_play, direct, balanced
struct Dir {
    const char* key;
    double v[kFamilyCount];
};
const Dir kDirs[] = {
    {"press_intensity", {1.0, -0.5, 0.2, 0.0, 0.0, 0.0}},
    {"engagement_height", {0.8, -0.9, 0.1, 0.0, 0.0, 0.0}},
    {"line_depth", {0.6, -1.0, 0.3, 0.0, 0.1, 0.0}},
    {"tempo", {0.6, -0.2, -0.5, 0.2, 0.5, 0.0}},
    {"directness", {0.0, 0.5, -0.8, 0.2, 1.0, 0.0}},
    {"width_att", {0.0, -0.3, 0.3, 1.0, -0.1, 0.0}},
    {"width_def", {0.0, -0.2, 0.0, 0.3, 0.0, 0.0}},
    {"counter_press", {1.0, -0.3, 0.3, 0.0, 0.0, 0.0}},
    {"counter_attack", {0.3, 1.0, -0.5, 0.0, 0.3, 0.0}},
    {"regroup_depth", {-0.5, 0.8, -0.3, 0.0, 0.0, 0.0}},
    {"buildup_short", {0.0, 0.0, 1.0, -0.2, -0.8, 0.0}},
    {"cross_freq", {0.0, 0.0, -0.3, 1.0, 0.3, 0.0}},
    {"shot_patience", {0.0, 0.0, 0.7, 0.0, -0.4, 0.0}},
    {"line_length", {0.4, 0.6, 0.2, 0.0, 0.0, 0.0}},
    {"forward_runs", {0.4, 0.0, -0.2, 0.4, 0.5, 0.0}},
};

std::string pm(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%+d", static_cast<int>(std::lround(v)));
    return b;
}

}  // namespace

double strength_ratio(const Fixture& f) { return f.user.ovr > 0.0 ? f.opp.ovr / f.user.ovr : 1.0; }

uint64_t fixture_seed(const FixtureId& id) {
    uint64_t h = splitmix(0x7475726230323030ULL);  // "turb0200"
    for (int64_t v : {id.career, id.season, id.matchday, id.opp_teamid, static_cast<int64_t>(id.salt)}) h = splitmix(h ^ static_cast<uint64_t>(v));
    return h ? h : 0x9E3779B97F4A7C15ULL;
}

SeededRng::SeededRng(uint64_t seed) : s_(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
uint64_t SeededRng::next() {
    s_ ^= s_ >> 12;
    s_ ^= s_ << 25;
    s_ ^= s_ >> 27;
    return s_ * 2685821657736338717ULL;
}
double SeededRng::unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
double SeededRng::signed_unit() { return unit() * 2.0 - 1.0; }

OppParams params_from_sliders(const SliderSet& s) {
    OppParams p;
    p.variety_strength = s.value_or_default("opp.variety_strength");
    p.quality_scaling = s.value_or_default("opp.quality_scaling");
    p.mirroring = s.value_or_default("opp.mirroring");
    p.underdog_bias = s.value_or_default("opp.underdog_bias");
    p.favourite_bias = s.value_or_default("opp.favourite_bias");
    p.jitter = s.value_or_default("opp.jitter");
    for (size_t i = 0; i < kFamilyCount; ++i) p.family_weight[i] = s.value_or_default(std::string("opp.family_weight_") + kFamilyKeys[i]);
    return p;
}

void score_families(const TeamFacts& t, double ratio, double out[kFamilyCount]) {
    const double young = inv_ramp(t.age, 24.0, 29.0);
    out[0] = 0.35 * ramp(t.pace, 60, 78) + 0.25 * young + 0.20 * ramp(t.mentality, 3, 5) + 0.20 * ramp(ratio, 0.9, 1.1);
    out[1] = 0.50 * ramp(1.0 - ratio, 0.0, 0.2) + 0.30 * inv_ramp(t.mentality, 1, 3) + 0.20 * ramp(t.pace, 60, 75);
    out[2] = 0.50 * ramp(t.passing, 60, 78) + 0.30 * ramp(t.ovr, 72, 85) + 0.20 * ramp(t.reputation, 50, 80);
    out[3] = 0.60 * ramp(t.n_wide, 0, 3) + 0.40 * (t.aerial ? 1.0 : 0.0);
    out[4] = 0.40 * inv_ramp(t.passing, 55, 70) + 0.30 * (t.aerial ? 1.0 : 0.0) + 0.30 * inv_ramp(t.ovr, 62, 74);
    double mx = 0.0;
    for (size_t i = 0; i < 5; ++i) {
        out[i] = clampd(out[i], 0.0, 1.0);
        mx = std::max(mx, out[i]);
    }
    out[5] = std::max(0.05, 1.0 - mx);  // balanced: the fallback when nothing stands out
}

FactSet facts_of(const Fixture& f, const double scores[kFamilyCount]) {
    FactSet x;
    x["strength_ratio"] = strength_ratio(f);
    x["user_ovr"] = f.user.ovr;
    x["opp_ovr"] = f.opp.ovr;
    x["reputation"] = f.opp.reputation;
    x["form"] = f.opp.form;
    x["age"] = f.opp.age;
    x["pace"] = f.opp.pace;
    x["passing"] = f.opp.passing;
    x["aerial"] = f.opp.aerial ? 1.0 : 0.0;
    x["home"] = f.opp_home ? 1.0 : 0.0;
    x["mentality"] = f.opp.mentality;
    x["n_def"] = f.opp.n_def;
    x["n_mid"] = f.opp.n_mid;
    x["n_att"] = f.opp.n_att;
    x["n_wide"] = f.opp.n_wide;
    for (size_t i = 0; i < kFamilyCount; ++i) x[std::string("family_") + kFamilyKeys[i]] = scores[i];
    return x;
}

OppProfile solve_opposition(const Fixture& f, const FixtureId& id, const OppParams& p, const RuleSet& rules) {
    OppProfile out;
    out.seed = fixture_seed(id);
    out.ratio = strength_ratio(f);
    if (!p.enabled || p.global_scale <= 0.0 || p.variety_strength <= 0) {
        out.explain.push_back("Opposition variety is off: the opponent keeps the game's own settings.");
        return out;
    }
    out.active = true;
    const int max_delta = std::max(0, std::min(100, p.max_delta));

    // 1. family scores from the facts, mirrored toward the user's own style, nudged for underdogs and favourites
    double sc[kFamilyCount];
    score_families(f.opp, out.ratio, sc);
    if (p.mirroring > 0) {
        double us[kFamilyCount];
        score_families(f.user, out.ratio > 0.0 ? 1.0 / out.ratio : 1.0, us);
        const double m = clampd(p.mirroring / 100.0, 0.0, 1.0) * 0.5;
        for (size_t i = 0; i < kFamilyCount; ++i) sc[i] = (1.0 - m) * sc[i] + m * us[i];
    }
    const double under = 1.0 + clampd(p.underdog_bias / 100.0, 0.0, 1.0) * ramp(0.9 - out.ratio, 0.0, 0.2);
    const double fav = 1.0 + clampd(p.favourite_bias / 100.0, 0.0, 1.0) * ramp(out.ratio - 1.1, 0.0, 0.2);
    sc[1] *= under;
    sc[4] *= under;
    sc[2] *= fav;
    sc[0] *= fav;
    for (size_t i = 0; i < kFamilyCount; ++i) out.scores[i] = sc[i];

    // 2. rules see the facts and the scores before rules
    const RuleOutcome ro = evaluate_rules(rules, facts_of(f, sc));
    out.hits = ro.hits;

    // 3. blend: score + rule boosts, times the per-family weight; a forced family takes everything
    double w[kFamilyCount], sum = 0.0;
    for (size_t i = 0; i < kFamilyCount; ++i) {
        w[i] = std::max(0.0, sc[i] + ro.family_boost[i]) * (clampd(p.family_weight[i], 0, 100) / 50.0);
        if (p.force_family >= 0) w[i] = static_cast<int>(i) == p.force_family ? 1.0 : 0.0;
        sum += w[i];
    }
    if (sum <= 0.0) {
        for (size_t i = 0; i < kFamilyCount; ++i) w[i] = i == kFamilyCount - 1 ? 1.0 : 0.0;
        sum = 1.0;
    }
    int dom = 0;
    for (size_t i = 0; i < kFamilyCount; ++i) {
        out.blend[i] = w[i] / sum;
        if (out.blend[i] > out.blend[static_cast<size_t>(dom)]) dom = static_cast<int>(i);
    }
    out.dominant = dom;

    // 4. quality sets the amplitude, not the style
    const double q = ramp((f.opp.ovr - 55.0) / 30.0, 0.0, 1.0);
    const double amp_q = 0.35 + 0.65 * q;
    const double qs = clampd(p.quality_scaling / 100.0, 0.0, 1.0);
    const double quality_amp = 1.0 - qs * (1.0 - amp_q);
    const double scale = (clampd(p.variety_strength, 0, 100) / 50.0) * p.global_scale * ro.amplitude_scale;
    out.amplitude = max_delta * quality_amp * scale;
    const double jit = clampd(p.jitter / 100.0, 0.0, 1.0) * (max_delta * 0.5) * quality_amp * (clampd(p.variety_strength, 0, 100) / 50.0) * p.global_scale;

    // 5. the changes: style direction x amplitude + a seeded wobble (one draw per table row, always, so the stream never depends on the facts)
    SeededRng rng(out.seed);
    std::map<std::string, double> raw;
    for (const Dir& d : kDirs) {
        double style = 0.0;
        for (size_t i = 0; i < kFamilyCount; ++i) style += out.blend[i] * d.v[i];
        raw[std::string("team.") + d.key] = style * out.amplitude + jit * rng.signed_unit();
    }
    for (const auto& kv : ro.add)
        if (kv.first.rfind("team.", 0) == 0) raw.emplace(kv.first, 0.0);
    for (const auto& kv : ro.set)
        if (kv.first.rfind("team.", 0) == 0) raw.emplace(kv.first, 0.0);
    for (auto& kv : raw) {
        double v = kv.second;
        auto s = ro.set.find(kv.first);
        if (s != ro.set.end()) {
            v = s->second;
        } else if (auto a = ro.add.find(kv.first); a != ro.add.end()) {
            v += a->second;
        }
        const long d = std::lround(clampd(v, -static_cast<double>(max_delta), static_cast<double>(max_delta)));
        if (d != 0) out.team_deltas[kv.first] = static_cast<int>(d);
    }
    // Live offsets (injury, difficulty) come from rules only; clamped to their slider range
    std::set<std::string> live;
    for (const auto& kv : ro.add)
        if (kv.first.rfind("opp.", 0) == 0) live.insert(kv.first);
    for (const auto& kv : ro.set)
        if (kv.first.rfind("opp.", 0) == 0) live.insert(kv.first);
    for (const std::string& k : live) {
        const SliderDef* d = find_slider(k);
        if (!d) continue;
        double v = 0.0;
        if (auto s = ro.set.find(k); s != ro.set.end()) v = s->second;
        else if (auto a = ro.add.find(k); a != ro.add.end()) v = a->second;
        const int c = clamp_slider(*d, static_cast<int>(std::lround(v)));
        if (c != 0) out.live_offsets[k] = c;
    }

    // 6. explain
    char buf[200];
    std::snprintf(buf, sizeof(buf), "Seed %016llx. Strength ratio %.2f (opponent / you).", static_cast<unsigned long long>(out.seed), out.ratio);
    out.explain.push_back(buf);
    std::string blend = "Style blend:";
    for (size_t i = 0; i < kFamilyCount; ++i)
        if (out.blend[i] >= 0.005) {
            std::snprintf(buf, sizeof(buf), " %s %d%%,", family_label(i), static_cast<int>(std::lround(out.blend[i] * 100.0)));
            blend += buf;
        }
    if (blend.back() == ',') blend.pop_back();
    out.explain.push_back(blend + ".");
    std::snprintf(buf, sizeof(buf), "Quality sets the size of the changes: up to %.1f points before the limit of %d.", out.amplitude, max_delta);
    out.explain.push_back(buf);
    for (const auto& kv : out.team_deltas) {
        const SliderDef* d = find_slider(kv.first);
        out.explain.push_back((d ? d->label : kv.first) + " " + pm(kv.second));
    }
    for (const auto& kv : out.live_offsets) {
        const SliderDef* d = find_slider(kv.first);
        out.explain.push_back((d ? d->label : kv.first) + " " + pm(kv.second));
    }
    if (out.hits.empty()) out.explain.push_back("No rule fired.");
    for (const RuleHit& h : out.hits) {
        std::string line = "Rule " + h.id + " (priority " + std::to_string(h.priority) + "):";
        for (size_t i = 0; i < h.effects.size(); ++i) line += (i ? "; " : " ") + h.effects[i];
        out.explain.push_back(line);
    }
    return out;
}

std::string explain_text(const OppProfile& p) {
    std::string s;
    for (const std::string& l : p.explain) s += l + "\n";
    return s;
}

}  // namespace turbo
