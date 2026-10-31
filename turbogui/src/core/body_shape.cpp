// FC 27 LE Turbo GUI - body shapes (5.0c E8-body). See body_shape.h.
#include "core/body_shape.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/field_labels.h"
#include "core/looks_fields_data.h"

namespace turbo::bodyshape {
namespace {

namespace data = turbo::looks::data;

data::LookSubject subject_of(bool manager) { return manager ? data::LookSubject::Manager : data::LookSubject::Player; }

const data::LookValue* look_value(bool manager, int64_t code) {
    const data::LookField* f = data::find_look_field(subject_of(manager), "bodytypecode");
    return f ? data::find_look_value(*f, static_cast<int32_t>(code)) : nullptr;
}

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

// The size the NAME of a game-named code says (field_labels.h / Live Editor: Short, Average, Tall, Very tall x lean, normal, stocky)
bool nominal(int64_t code, float* height, float* build) {
    constexpr float kShort = 160.0f, kAverage = 176.0f, kTall = 190.0f, kVeryTall = 198.0f;
    constexpr float kLean = 0.15f, kNormal = 0.50f, kStocky = 0.85f;
    switch (code) {
        case 1: *height = kAverage; *build = kLean; return true;
        case 2: *height = kAverage; *build = kNormal; return true;
        case 3: *height = kAverage; *build = kStocky; return true;
        case 4: *height = kTall; *build = kLean; return true;
        case 5: *height = kTall; *build = kNormal; return true;
        case 6: *height = kTall; *build = kStocky; return true;
        case 7: *height = kShort; *build = kLean; return true;
        case 8: *height = kShort; *build = kNormal; return true;
        case 9: *height = kShort; *build = kStocky; return true;
        case 11: *height = kVeryTall; *build = kLean; return true;  // no census row anywhere: the label is all there is
        default: return false;
    }
}

// build 0..1 from the midpoint height (cm) and weight (kg): BMI 21 -> 0.15, 24.5 -> 0.5, 28 -> 0.85
float build_of(float cm, float kg) {
    if (cm <= 0.0f || kg <= 0.0f) return 0.5f;
    const float m = cm / 100.0f, bmi = kg / (m * m);
    return clampf(0.15f + (bmi - 21.0f) / 7.0f * 0.7f, 0.05f, 0.95f);
}

const char* height_word(float cm) {
    if (cm < 172.0f) return "short";
    if (cm < 183.0f) return "medium";
    if (cm < 192.0f) return "tall";
    return "very tall";
}
const char* build_word(float b) {
    if (b < 0.34f) return "slim";
    if (b < 0.67f) return "regular";
    return "broad";
}

std::string manager_base(const BodyShape& s) { return std::string("Manager, ") + height_word(s.height_cm) + ", " + build_word(s.build); }

std::string manager_set_name(int64_t code) {
    const BodyShape me = body_shape(true, code, nullptr);
    const std::string base = manager_base(me);
    // the codes of the set that read the same, shortest first (then by code): "(2 of 3)"; fixed data, so a code always reads the same
    std::vector<std::pair<float, int64_t>> same;
    for (int64_t c = 81; c <= 89; ++c) {
        const BodyShape o = body_shape(true, c, nullptr);
        if (manager_base(o) == base) same.push_back({o.height_cm, c});
    }
    if (same.size() < 2) return base;
    std::sort(same.begin(), same.end());
    for (size_t i = 0; i < same.size(); ++i)
        if (same[i].second == code) return base + " (" + std::to_string(i + 1) + " of " + std::to_string(same.size()) + ")";
    return base;
}

}  // namespace

bool manager_set(int64_t code) { return code >= 81 && code <= 89; }

bool risky_for(bool manager, const bodytype::Catalog& cat, int64_t code, int head_class) {
    if (manager && manager_set(code)) return false;
    return bodytype::risky_pairing(cat, code, head_class);
}

BodyShape body_shape(bool manager, int64_t code, const bodytype::Catalog* cat) {
    BodyShape s;
    s.code = code;
    float h = 0.0f, b = 0.0f;
    if (nominal(code, &h, &b)) {
        s.height_cm = h;
        s.build = b;
        s.src = Source::Nominal;
        return s;
    }
    if (const data::LookValue* v = look_value(manager, code); v && v->used > 0 && v->h_max > 0) {
        s.height_cm = (v->h_min + v->h_max) * 0.5f;
        s.build = build_of(s.height_cm, v->w_max > 0 ? (v->w_min + v->w_max) * 0.5f : 0.0f);
        s.src = Source::Census;
        return s;
    }
    if (cat)
        if (const bodytype::Entry* e = cat->find(code); e && e->has_height()) {
            s.height_cm = (e->height_min + e->height_max) * 0.5f;
            s.build = build_of(s.height_cm, e->has_weight() ? (e->weight_min + e->weight_max) * 0.5f : 0.0f);
            s.src = Source::Census;
            return s;
        }
    return s;  // Estimated: an average, normal figure
}

std::string body_name(bool manager, int64_t code, const bodytype::Catalog* cat) {
    // the words where they name the body: Live Editor's / track F's ("Tall, lean"); the manager set by its size
    if (const data::LookValue* v = look_value(manager, code); v && v->src != data::LookSrc::StyleN && v->src != data::LookSrc::Census) return v->label;
    if (manager && manager_set(code)) return manager_set_name(code);
    if (cat && cat->named(code)) return cat->name(code);
    if (const labels::NamedCodes* n = labels::named_codes("bodytypecode"))
        if (const char* name = labels::find_name(*n, code)) return name;
    if (manager) return std::string();  // a manager's own style (12, 131, 381 ...): "players like X" means nothing for him
    std::string first;
    if (const labels::data::Users* u = labels::find_users("bodytypecode", code)) first = u->names;
    else if (cat) {
        if (const bodytype::Entry* e = cat->find(code); e && !e->examples.empty()) first = e->examples.front();
    }
    const size_t comma = first.find(',');
    if (comma != std::string::npos) first.resize(comma);
    return first.empty() ? std::string() : "Like " + first;
}

std::string body_detail(bool manager, int64_t code, const bodytype::Catalog* cat) {
    const BodyShape s = body_shape(manager, code, cat);
    std::string out;
    char b[96];
    if (s.src == Source::Nominal) {
        out = "The figure follows its name (not a measured size)";
    } else if (s.src == Source::Census) {
        std::snprintf(b, sizeof(b), "Figure drawn at about %d cm (estimated from the players or managers who have it)", static_cast<int>(s.height_cm + 0.5f));
        out = b;
    } else {
        out = "No size known: an average figure is drawn";
    }
    const data::LookValue* v = look_value(manager, code);
    if (v && v->used > 0 && v->h_max > 0) {
        std::snprintf(b, sizeof(b), "\nHeight %d-%d cm, weight %d-%d kg, used by %d %s", v->h_min, v->h_max, v->w_min, v->w_max, v->used,
                      manager ? "managers" : "players");
        out += b;
    } else if (cat)
        if (const bodytype::Entry* e = cat->find(code); e && e->has_height()) {
            std::snprintf(b, sizeof(b), "\nHeight %d-%d cm, weight %d-%d kg, used by %lld players", e->height_min, e->height_max, e->weight_min, e->weight_max,
                          static_cast<long long>(e->players));
            out += b;
        }
    return out;
}

std::vector<Polygon> silhouette(const BodyShape& s) {
    constexpr float kFeet = 0.98f, kWidth = 1.25f;  // kWidth: horizontal stretch so a figure fills a square tile better
    const float t = clampf((s.height_cm - kMinHeightCm) / (kMaxHeightCm - kMinHeightCm), 0.0f, 1.0f);
    const float fh = 0.62f + 0.34f * t;  // 150 cm -> 62 % of the tile, 210 cm -> 96 %: ONE scale for every tile
    const float top = kFeet - fh, cx = 0.5f, b = clampf(s.build, 0.0f, 1.0f);
    const float r = fh * 0.072f;
    const float sh_y = top + fh * 0.20f, hip_y = top + fh * 0.52f, hand_y = top + fh * 0.50f;
    const float sw = fh * (0.12f + 0.06f * b) * kWidth, ww = fh * (0.085f + 0.075f * b) * kWidth, aw = fh * 0.032f * kWidth;
    const float nw = fh * 0.03f * kWidth, gap = fh * 0.012f * kWidth;
    std::vector<Polygon> out;
    {  // head: an octagon (convex, 8 points)
        Polygon p;
        for (int i = 0; i < 8; ++i) {
            const float a = (static_cast<float>(i) + 0.5f) * 3.14159265f / 4.0f;
            p.push_back({cx + r * std::cos(a), top + r + r * std::sin(a)});
        }
        out.push_back(p);
    }
    out.push_back({{cx - nw, top + 2.0f * r - fh * 0.01f}, {cx + nw, top + 2.0f * r - fh * 0.01f}, {cx + nw, sh_y + fh * 0.01f}, {cx - nw, sh_y + fh * 0.01f}});
    out.push_back({{cx - sw, sh_y}, {cx + sw, sh_y}, {cx + ww, hip_y}, {cx - ww, hip_y}});  // torso
    out.push_back({{cx - sw - 1.3f * aw, sh_y}, {cx - sw - 0.1f * aw, sh_y}, {cx - sw - 0.1f * aw, hand_y}, {cx - sw - 1.3f * aw, hand_y}});  // left arm
    out.push_back({{cx + sw + 0.1f * aw, sh_y}, {cx + sw + 1.3f * aw, sh_y}, {cx + sw + 1.3f * aw, hand_y}, {cx + sw + 0.1f * aw, hand_y}});  // right arm
    out.push_back({{cx - ww, hip_y}, {cx - gap, hip_y}, {cx - gap, kFeet}, {cx - ww * 0.85f, kFeet}});  // left leg
    out.push_back({{cx + gap, hip_y}, {cx + ww, hip_y}, {cx + ww * 0.85f, kFeet}, {cx + gap, kFeet}});  // right leg
    return out;
}

}  // namespace turbo::bodyshape
