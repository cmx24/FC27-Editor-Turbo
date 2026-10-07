// FC 27 LE Turbo GUI - tactics geometry and the preview model (see tactics.h)
#include "tactics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace turbo {

const char* const kModelChip = "Model view: Turbo's estimate, not game output";

const char* tier_name(Tier t) {
    switch (t) {
        case Tier::Exact: return "Exact";
        case Tier::Derived: return "Derived";
        case Tier::Modelled: return "Modelled";
    }
    return "Exact";
}

const char* tier_letter(Tier t) {
    switch (t) {
        case Tier::Exact: return "E";
        case Tier::Derived: return "D";
        case Tier::Modelled: return "M";
    }
    return "E";
}

// ------------------------------------------------------------------------------------------------ formations
PosGroup group_of_label(const std::string& l) {
    if (l == "GK") return PosGroup::GK;
    if (l == "CB" || l == "LB" || l == "RB" || l == "LWB" || l == "RWB" || l == "SW") return PosGroup::Def;
    if (l == "LW" || l == "RW" || l == "ST" || l == "CF" || l == "LF" || l == "RF") return PosGroup::Att;
    return PosGroup::Mid;
}

namespace {

struct Row {
    float y;
    std::vector<const char*> labels;
};

// x of the i-th of n players in a row: a row that starts with an L* and ends with an R* label is spread touchline to touchline, any other
// row is centred
float row_x(const std::vector<const char*>& labels, size_t i) {
    const size_t n = labels.size();
    if (n == 1) return 0.5f;
    const bool wide = n >= 3 && labels.front()[0] == 'L' && labels.back()[0] == 'R';
    if (wide) return 0.10f + 0.80f * static_cast<float>(i) / static_cast<float>(n - 1);
    const float step = n == 2 ? 0.24f : n == 3 ? 0.22f : 0.20f;
    return 0.5f + (static_cast<float>(i) - static_cast<float>(n - 1) / 2.0f) * step;
}

Formation make_fb(const char* id, std::initializer_list<Row> rows) {
    Formation f;
    f.id = id;
    f.name = id;
    f.slots.push_back({"GK", 0.5f, 0.06f, PosGroup::GK});
    for (const Row& r : rows)
        for (size_t i = 0; i < r.labels.size(); ++i) f.slots.push_back({r.labels[i], row_x(r.labels, i), r.y, group_of_label(r.labels[i])});
    return f;
}

std::vector<Formation> build_fallbacks() {
    const Row d4{0.25f, {"LB", "CB", "CB", "RB"}}, d3{0.24f, {"CB", "CB", "CB"}}, d5{0.25f, {"LWB", "CB", "CB", "CB", "RWB"}};
    return {
        make_fb("4-4-2", {d4, {0.52f, {"LM", "CM", "CM", "RM"}}, {0.82f, {"ST", "ST"}}}),
        make_fb("4-3-3", {d4, {0.50f, {"CM", "CDM", "CM"}}, {0.82f, {"LW", "ST", "RW"}}}),
        make_fb("4-2-3-1", {d4, {0.42f, {"CDM", "CDM"}}, {0.65f, {"LM", "CAM", "RM"}}, {0.85f, {"ST"}}}),
        make_fb("4-1-4-1", {d4, {0.38f, {"CDM"}}, {0.56f, {"LM", "CM", "CM", "RM"}}, {0.84f, {"ST"}}}),
        make_fb("4-5-1", {d4, {0.52f, {"LM", "CM", "CM", "CM", "RM"}}, {0.84f, {"ST"}}}),
        make_fb("4-4-1-1", {d4, {0.50f, {"LM", "CM", "CM", "RM"}}, {0.68f, {"CF"}}, {0.85f, {"ST"}}}),
        make_fb("4-3-2-1", {d4, {0.48f, {"CM", "CM", "CM"}}, {0.68f, {"CAM", "CAM"}}, {0.86f, {"ST"}}}),
        make_fb("4-1-2-1-2", {d4, {0.38f, {"CDM"}}, {0.52f, {"CM", "CM"}}, {0.66f, {"CAM"}}, {0.86f, {"ST", "ST"}}}),
        make_fb("3-5-2", {d3, {0.50f, {"LM", "CM", "CDM", "CM", "RM"}}, {0.83f, {"ST", "ST"}}}),
        make_fb("3-4-3", {d3, {0.50f, {"LM", "CM", "CM", "RM"}}, {0.82f, {"LW", "ST", "RW"}}}),
        make_fb("5-3-2", {d5, {0.50f, {"CM", "CM", "CM"}}, {0.83f, {"ST", "ST"}}}),
        make_fb("5-4-1", {d5, {0.52f, {"LM", "CM", "CM", "RM"}}, {0.84f, {"ST"}}}),
    };
}

float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

}  // namespace

const std::vector<Formation>& fallback_formations() {
    static const std::vector<Formation> f = build_fallbacks();
    return f;
}

const Formation* find_fallback_formation(const std::string& id) {
    for (const Formation& f : fallback_formations())
        if (f.id == id) return &f;
    return nullptr;
}

bool make_formation(const std::string& id, const std::string& name, std::vector<FormationSlot> slots, Formation& out, std::string* err) {
    if (slots.size() != 11) {
        if (err) *err = "a formation needs 11 slots";
        return false;
    }
    if (slots[0].label != "GK") {
        if (err) *err = "slot 0 must be the goalkeeper";
        return false;
    }
    for (size_t i = 1; i < slots.size(); ++i)
        if (slots[i].label == "GK") {
            if (err) *err = "only slot 0 can be the goalkeeper";
            return false;
        }
    for (FormationSlot& s : slots) {
        s.x = std::isfinite(s.x) ? clamp01(s.x) : 0.5f;
        s.y = std::isfinite(s.y) ? clamp01(s.y) : 0.5f;
        s.group = group_of_label(s.label);
    }
    out.id = id;
    out.name = name.empty() ? id : name;
    out.slots = std::move(slots);
    out.from_db = true;
    return true;
}

// ------------------------------------------------------------------------------------------------ preview model
namespace {

// the slider as 0..1 of its own range (mentality: 0..6)
double sv(const SliderSet& s, const char* key) {
    const SliderDef* d = find_slider(key);
    const int v = s.value_or_default(key);
    if (!d || d->max <= d->min) return 0.5;
    return clampd(static_cast<double>(v - d->min) / static_cast<double>(d->max - d->min), 0.0, 1.0);
}

std::string d_label(const char* name, const SliderSet& s, const char* key) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s %d/100 (schematic)", name, s.value_or_default(key));
    return buf;
}

double group_weight(PosGroup g) {
    switch (g) {
        case PosGroup::Att: return 1.0;
        case PosGroup::Mid: return 0.6;
        case PosGroup::Def: return 0.2;
        case PosGroup::GK: return 0.0;
    }
    return 0.0;
}

struct Geo {
    double def_y, press_y, front_y;
    double hw_att, hw_def;
    double depth, eng, runs, mentality, fluid, press, tempo, direct, regroup, compact;
};

Geo geometry(const SliderSet& t) {
    Geo g{};
    g.depth = sv(t, "team.line_depth");
    g.eng = sv(t, "team.engagement_height");
    g.press = sv(t, "team.press_intensity");
    g.compact = sv(t, "team.line_length");
    g.runs = sv(t, "team.forward_runs");
    g.mentality = sv(t, "team.mentality");
    g.fluid = sv(t, "team.fluidity");
    g.tempo = sv(t, "team.tempo");
    g.direct = sv(t, "team.directness");
    g.regroup = sv(t, "team.regroup_depth");
    g.def_y = 0.18 + 0.32 * g.depth;
    g.press_y = clampd(0.30 + 0.45 * g.eng, g.def_y + 0.05, 0.95);
    g.front_y = std::min(0.98, g.def_y + 0.60 - 0.30 * g.compact);
    g.hw_att = 0.18 + 0.30 * sv(t, "team.width_att");
    g.hw_def = 0.18 + 0.30 * sv(t, "team.width_def");
    return g;
}

HeatGrid heat_grid(const Formation& f, const Geo& g, const SliderSet& pos, int selected, Phase phase, const SliderSet& team) {
    HeatGrid h;
    h.cols = kHeatCols;
    h.rows = kHeatRows;
    h.cell.assign(static_cast<size_t>(h.cols) * static_cast<size_t>(h.rows), 0.0f);
    const double block = 0.5 * g.depth + 0.5 * g.eng;                 // how high the team defends
    const double w_with = sv(team, "team.width_att"), w_without = sv(team, "team.width_def");
    for (size_t i = 0; i < f.slots.size(); ++i) {
        const FormationSlot& s = f.slots[i];
        const double aw = group_weight(s.group);
        const double back_shift = (block - 0.5) * 0.30 * (s.group == PosGroup::Att ? 0.6 : 1.0);
        double runs = g.runs;
        double roam = g.fluid;
        if (static_cast<int>(i) == selected) {
            if (pos.has("pos.forward_runs")) runs = sv(pos, "pos.forward_runs");
            if (pos.has("pos.roam")) roam = sv(pos, "pos.roam");
        }
        const double fwd_shift = (g.mentality - 0.5) * 0.12 + runs * 0.10 * aw;
        double y = s.y, wph;
        if (phase == Phase::WithBall) {
            y += fwd_shift;
            wph = w_with;
        } else if (phase == Phase::WithoutBall) {
            y += back_shift;
            wph = w_without;
        } else {
            y += 0.5 * (fwd_shift + back_shift);
            wph = 0.5 * (w_with + w_without);
        }
        if (s.group == PosGroup::GK) y = s.y;
        y = clampd(y, 0.03, 0.97);
        const double x = clampd(0.5 + (s.x - 0.5) * (0.8 + 0.4 * wph), 0.02, 0.98);
        const double sigma = 0.08 + 0.10 * roam;
        const double inv = 1.0 / (2.0 * sigma * sigma);
        for (int r = 0; r < h.rows; ++r) {
            const double cy = (r + 0.5) / h.rows;
            for (int c = 0; c < h.cols; ++c) {
                const double cx = (c + 0.5) / h.cols;
                const double dx = (cx - x) * 0.65, dy = cy - y;  // the pitch is wider along y: weight the axes by 68:105
                h.cell[static_cast<size_t>(r) * static_cast<size_t>(h.cols) + static_cast<size_t>(c)] +=
                    static_cast<float>(std::exp(-(dx * dx + dy * dy) * inv));
            }
        }
    }
    float mx = 0.0f;
    for (float v : h.cell) mx = std::max(mx, v);
    if (mx > 0.0f)
        for (float& v : h.cell) v /= mx;
    return h;
}

}  // namespace

std::vector<std::string> PreviewModel::derived_modelled_labels() const {
    std::vector<std::string> out;
    for (const PreviewLine& l : lines) out.push_back(l.label);
    for (const PreviewBand& b : bands) out.push_back(b.label);
    if (!arrows_label.empty()) out.push_back(arrows_label);
    if (!ring_label.empty()) out.push_back(ring_label);
    if (!heat_label.empty()) out.push_back(heat_label);
    if (!exposure_label.empty()) out.push_back(exposure_label);
    return out;
}

size_t PreviewModel::primitive_count() const {
    return dots.size() + lines.size() + bands.size() + arrows.size() + rings.size() + heat.cell.size();
}

PreviewModel build_preview(const Formation& f, const SliderSet& team, const SliderSet& pos, const PreviewOptions& opt) {
    PreviewModel m;
    m.formation = f;
    m.phase = opt.phase;
    m.model_chip = kModelChip;
    m.greyed = opt.effects_greyed;
    const Geo g = geometry(team);

    // Exact: the stored formation, whatever the sliders say
    for (size_t i = 0; i < f.slots.size(); ++i)
        m.dots.push_back({static_cast<int>(i), f.slots[i].label, f.slots[i].x, f.slots[i].y, f.slots[i].group, static_cast<int>(i) == opt.selected_slot});

    // Derived: lines (without the ball), width band, run arrows (with the ball)
    const bool with = opt.phase != Phase::WithoutBall, without = opt.phase != Phase::WithBall;
    if (without) {
        m.lines.push_back({"def_line", Tier::Derived, static_cast<float>(g.def_y), static_cast<float>(0.4 + 0.6 * sv(team, "team.marking")),
                           d_label("Defensive line", team, "team.line_depth")});
        m.lines.push_back({"press_line", Tier::Derived, static_cast<float>(g.press_y), static_cast<float>(g.press),
                           d_label("Press line", team, "team.engagement_height")});
        m.lines.push_back({"front_line", Tier::Derived, static_cast<float>(g.front_y), 0.5f, d_label("Front line", team, "team.line_length")});
    }
    {
        const double hw = opt.phase == Phase::WithBall ? g.hw_att : opt.phase == Phase::WithoutBall ? g.hw_def : 0.5 * (g.hw_att + g.hw_def);
        PreviewBand b;
        b.id = "width_band";
        b.x_lo = static_cast<float>(0.5 - hw);
        b.x_hi = static_cast<float>(0.5 + hw);
        const char* key = opt.phase == Phase::WithoutBall ? "team.width_def" : "team.width_att";
        b.label = d_label(opt.phase == Phase::WithoutBall ? "Width without the ball" : opt.phase == Phase::WithBall ? "Width with the ball" : "Width", team, key);
        m.bands.push_back(b);
    }
    if (with) {
        for (size_t i = 1; i < f.slots.size(); ++i) {
            const FormationSlot& s = f.slots[i];
            double runs = g.runs;
            if (static_cast<int>(i) == opt.selected_slot && pos.has("pos.forward_runs")) runs = sv(pos, "pos.forward_runs");
            const double len = 0.04 + 0.22 * runs * group_weight(s.group);
            if (len < 0.03) continue;  // a back-line player with little running: no arrow rather than a stub
            m.arrows.push_back({static_cast<int>(i), s.x, s.y, s.x, static_cast<float>(std::min(0.98, s.y + len)), Tier::Derived});
        }
        if (!m.arrows.empty()) m.arrows_label = d_label("Run arrows", team, "team.forward_runs");
    }

    // Modelled: off unless asked for
    if (opt.show_roam) {
        for (size_t i = 1; i < f.slots.size(); ++i) {
            double roam = g.fluid;
            if (static_cast<int>(i) == opt.selected_slot && pos.has("pos.roam")) roam = sv(pos, "pos.roam");
            m.rings.push_back({static_cast<int>(i), static_cast<float>(0.03 + 0.12 * roam)});
        }
        m.ring_label = "Roaming range (arbitrary)";
    }
    if (opt.show_heat) {
        m.heat = heat_grid(f, g, pos, opt.selected_slot, opt.phase, team);
        m.heat_label = "Relative intensity (arbitrary)";
    }
    if (opt.show_risk) {
        m.exposure = static_cast<float>(clampd(0.35 * g.depth + 0.20 * g.press + 0.15 * g.tempo + 0.15 * g.direct + 0.15 * g.mentality, 0.0, 1.0));
        m.exposure_label = "Relative exposure (arbitrary)";
    }

    // the honesty strip: how many of the settings in view are preview-only
    std::vector<const SliderDef*> visible = sliders_in_scope(SliderScope::Team);
    if (opt.selected_slot >= 0)
        for (const SliderDef* d : sliders_in_scope(SliderScope::Position)) visible.push_back(d);
    m.visible_total = visible.size();
    m.visible_preview_only = count_preview_only(visible);
    m.preview_strip = std::to_string(m.visible_preview_only) + " of " + std::to_string(m.visible_total) + " visible settings are preview-only";
    return m;
}

// ------------------------------------------------------------------------------------------------ cache
namespace {
struct Hasher {
    uint64_t h = 1469598103934665603ULL;
    void bytes(const void* p, size_t n) {
        const unsigned char* c = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= c[i];
            h *= 1099511628211ULL;
        }
    }
    void u64(uint64_t v) { bytes(&v, sizeof(v)); }
    void str(const std::string& s) {
        u64(s.size());
        bytes(s.data(), s.size());
    }
    void flt(float f) {
        uint32_t b;
        std::memcpy(&b, &f, sizeof(b));
        u64(b);
    }
    void set(const SliderSet& s) {
        u64(s.values.size());
        for (const auto& kv : s.values) {
            str(kv.first);
            u64(static_cast<uint64_t>(static_cast<int64_t>(kv.second)));
        }
    }
};
}  // namespace

uint64_t preview_key(const Formation& f, const SliderSet& team, const SliderSet& pos, const PreviewOptions& opt, uint64_t gen) {
    Hasher h;
    h.u64(gen);
    h.str(f.id);
    h.u64(f.from_db ? 1 : 0);
    for (const FormationSlot& s : f.slots) {
        h.str(s.label);
        h.flt(s.x);
        h.flt(s.y);
    }
    h.set(team);
    h.set(pos);
    h.u64(static_cast<uint64_t>(opt.phase));
    h.u64((opt.show_heat ? 1u : 0u) | (opt.show_roam ? 2u : 0u) | (opt.show_risk ? 4u : 0u) | (opt.effects_greyed ? 8u : 0u));
    h.u64(static_cast<uint64_t>(static_cast<int64_t>(opt.selected_slot)));
    return h.h;
}

const PreviewModel& PreviewCache::get(const Formation& f, const SliderSet& team, const SliderSet& pos, const PreviewOptions& opt, uint64_t gen) {
    const uint64_t k = preview_key(f, team, pos, opt, gen);
    if (!valid_ || k != key_) {
        model_ = build_preview(f, team, pos, opt);
        key_ = k;
        valid_ = true;
        ++builds_;
    }
    return model_;
}

// ------------------------------------------------------------------------------------------------ duty presets
const char* duty_name(Duty d) {
    switch (d) {
        case Duty::Defend: return "Defend";
        case Duty::Support: return "Support";
        case Duty::Attack: return "Attack";
    }
    return "Support";
}

SliderSet duty_preset(Duty d) {
    const int i = d == Duty::Defend ? 0 : d == Duty::Support ? 1 : 2;
    static const int attack_bias[3] = {20, 50, 80}, fwd[3] = {20, 50, 80}, depth[3] = {25, 50, 75}, shoot[3] = {25, 50, 75}, close[3] = {70, 50, 35},
                     mark[3] = {70, 50, 30};
    SliderSet s;
    s.set("pos.attack_bias", attack_bias[i]);
    s.set("pos.forward_runs", fwd[i]);
    s.set("pos.depth_bias", depth[i]);
    s.set("pos.shot_freq", shoot[i]);
    s.set("pos.close_down", close[i]);
    s.set("pos.mark_tight", mark[i]);
    return s;
}

}  // namespace turbo
