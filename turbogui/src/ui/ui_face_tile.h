// FC 27 LE Turbo GUI - the real-face chooser's tile, pure part (5.0c E7-realface). No ImGui, no App: the decisions of
// ui_faces.cpp's cell loop live here so a native test and a g++ runner (tests/catalog/run_face_tile.sh) can prove them.
//
//   choose_source  : which picture a tile shows (cached 3D render, game miniface, legend picture, a render in progress, or an
//                    honest placeholder). The word "no picture" is never a source.
//   layout_badges  : where the chips on a tile go (Hidden, name match, 3D pending / failed, HQ). One placement function for every
//                    chip, so two chips can never be drawn on top of each other (5.0b TEST: "Hidden" and "name match" were two
//                    unbacked AddText calls on the same row of a 96 px cell, about 37 px of overlap).
//   Breaker        : hidden game heads are rendered through the manager route; if the first 12 renders all fail the route is
//                    switched off for the session (brief section 4, P3).
//   initials       : the letters of the placeholder.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace turbo::face_tile {

// ---------------------------------------------------------------- the picture of a tile
enum class Source : uint8_t {
    Render3dP,    // turbo_output\cache\faces3d\p_<playerid>.dds
    Render3dM,    // ...\m_<headid>.dds (a manager's head, and every hidden game head)
    Miniface,     // the game's miniface (or the save head's flat picture), still loading or drawn
    Legend,       // the game's legend picture
    Rendering,    // a 3D render is queued or in flight: a spinner, never the word "no picture"
    Unavailable,  // nothing to show and nothing coming: initials and the reason
};

struct TileIn {
    bool cached_p = false, cached_m = false;
    bool has_mini = false;      // a miniface picture is expected (a save head always; a game head when the catalog says so)
    bool mini_missing = false;  // ... and the game answered "none" (LegacyImages::State::Missing)
    bool has_legend = false, legend_missing = false;
    bool can_render = false;    // 3D heads on (capture hooks running) and the head's render route not switched off
    bool given_up = false;      // this head failed kMaxHeadFails times
};

inline Source choose_source(const TileIn& t) {
    if (t.cached_m) return Source::Render3dM;
    if (t.cached_p) return Source::Render3dP;
    if (t.has_mini && !t.mini_missing) return Source::Miniface;
    if (t.has_legend && !t.legend_missing) return Source::Legend;
    if (t.can_render && !t.given_up) return Source::Rendering;
    return Source::Unavailable;
}

// ---------------------------------------------------------------- badges
enum class BadgeKind : uint8_t { Hidden, NameMatch, RenderPending, RenderFailed, Hq };

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};
struct ChipColors {
    Rgb bg, fg;
};

inline const char* badge_text(BadgeKind k) {
    switch (k) {
        case BadgeKind::Hidden: return "Hidden";
        case BadgeKind::NameMatch: return "name match";
        case BadgeKind::RenderPending: return "3D pending";
        case BadgeKind::RenderFailed: return "3D failed";
        case BadgeKind::Hq: return "HQ";
    }
    return "";
}

// Backed chips: the text colour is chosen against its own backing, never against the picture under it. Hidden uses the
// asset badge's colours (ui_asset_browser.cpp kStyles), the rest follow the same dark-backing / light-text scheme.
inline ChipColors chip_colors(BadgeKind k) {
    switch (k) {
        case BadgeKind::Hidden: return {{52, 78, 120}, {220, 232, 255}};
        case BadgeKind::NameMatch: return {{96, 64, 12}, {255, 232, 170}};
        case BadgeKind::RenderPending: return {{60, 60, 66}, {225, 225, 230}};
        case BadgeKind::RenderFailed: return {{120, 40, 40}, {255, 215, 215}};
        case BadgeKind::Hq: return {{24, 84, 56}, {220, 255, 232}};
    }
    return {{0, 0, 0}, {255, 255, 255}};
}

// WCAG 2.x relative luminance / contrast ratio
inline double luminance(Rgb c) {
    auto ch = [](uint8_t v) {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b);
}
inline double contrast_ratio(Rgb a, Rgb b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

struct BadgeIn {
    BadgeKind kind = BadgeKind::Hidden;
    float text_w = 0.0f;  // the label's advance (ImGui::CalcTextSize)
};
struct BadgeMetrics {
    float cell = 96.0f;      // the picture's side
    float line_h = 17.0f;    // text height
    float pad = 4.0f;        // horizontal padding inside a chip
    float margin = 3.0f;     // distance to the cell edge
    float gap = 2.0f;        // between chips
    float plus_w = 14.0f;    // the advance of "+9" (the chip that folds what does not fit)
    float max_h_frac = 0.6f; // chips may use this share of the cell's height, no more
};
struct BadgeRect {
    BadgeKind kind = BadgeKind::Hidden;
    bool plus = false;       // the "+n" chip: n chips are folded into the tooltip
    int plus_n = 0;
    float x = 0, y = 0, w = 0, h = 0;  // the backing, in cell coordinates (0,0 = the picture's top-left)
    float text_x = 0, text_y = 0;      // where the label starts
    float text_max_w = 0;              // the label may use this much; a longer one is cut with an ellipsis
    bool clipped = false;
};

// Left column from the top-left (Hidden, name match, 3D pending / failed), one chip per row; HQ goes right-aligned on the
// first row where it does not collide with that row's left chip. A chip wider than its row is clipped (ellipsis). More rows
// than max_h_frac allows: the last allowed row becomes "+n" and the rest is in the tooltip.
inline std::vector<BadgeRect> layout_badges(const BadgeMetrics& m, const std::vector<BadgeIn>& in) {
    std::vector<BadgeIn> left;
    bool have_hq = false, have_render = false;
    BadgeIn hq;
    for (const BadgeIn& b : in) {
        if (b.kind == BadgeKind::Hq) {
            if (!have_hq) hq = b;
            have_hq = true;
        } else if (b.kind == BadgeKind::RenderPending || b.kind == BadgeKind::RenderFailed) {
            if (!have_render) left.push_back(b);
            have_render = true;
        } else {
            left.push_back(b);
        }
    }
    auto order = [](BadgeKind k) { return k == BadgeKind::Hidden ? 0 : k == BadgeKind::NameMatch ? 1 : 2; };
    std::stable_sort(left.begin(), left.end(), [&](const BadgeIn& a, const BadgeIn& b) { return order(a.kind) < order(b.kind); });

    const float vpad = std::max(1.0f, std::floor(m.pad * 0.5f));
    const float chip_h = m.line_h + 2.0f * vpad;
    const float stride = chip_h + m.gap;
    const float avail = m.cell - 2.0f * m.margin;
    auto chip_w = [&](float tw) { return tw + 2.0f * m.pad; };

    size_t hq_row = left.size();  // after the left chips unless it fits beside one
    if (have_hq) {
        for (size_t r = 0; r < left.size(); ++r)
            if (chip_w(left[r].text_w) + m.gap + chip_w(hq.text_w) <= avail) {
                hq_row = r;
                break;
            }
    }
    const int max_rows = std::max(1, static_cast<int>((m.cell * m.max_h_frac - m.margin + m.gap) / stride));
    const size_t total_rows = std::max(left.size(), have_hq ? hq_row + 1 : size_t(0));
    size_t visible = total_rows;
    size_t fold_from = left.size();  // left chips from this index on are folded
    bool hq_folded = false;
    int folded = 0;
    if (total_rows > static_cast<size_t>(max_rows)) {
        visible = static_cast<size_t>(max_rows);
        fold_from = visible - 1;
        folded = static_cast<int>(left.size() > fold_from ? left.size() - fold_from : 0);
        if (have_hq && hq_row >= fold_from) {
            hq_folded = true;
            ++folded;
        }
    }

    std::vector<BadgeRect> out;
    for (size_t r = 0; r < visible; ++r) {
        const float y = m.margin + static_cast<float>(r) * stride;
        const bool hq_here = have_hq && !hq_folded && hq_row == r;
        if (r < left.size() && r < fold_from) {
            const float row_avail = avail - (hq_here ? chip_w(hq.text_w) + m.gap : 0.0f);
            BadgeRect b;
            b.kind = left[r].kind;
            b.w = std::min(chip_w(left[r].text_w), row_avail);
            b.clipped = chip_w(left[r].text_w) > row_avail;
            b.x = m.margin;
            b.y = y;
            b.h = chip_h;
            b.text_x = b.x + m.pad;
            b.text_y = y + vpad;
            b.text_max_w = std::max(0.0f, b.w - 2.0f * m.pad);
            out.push_back(b);
        } else if (folded > 0 && r + 1 == visible) {
            BadgeRect b;
            b.kind = left.empty() ? BadgeKind::Hidden : left[std::min(r, left.size() - 1)].kind;
            b.plus = true;
            b.plus_n = folded;
            b.w = chip_w(m.plus_w);
            b.x = m.margin;
            b.y = y;
            b.h = chip_h;
            b.text_x = b.x + m.pad;
            b.text_y = y + vpad;
            b.text_max_w = m.plus_w;
            out.push_back(b);
        }
        if (hq_here) {
            BadgeRect b;
            b.kind = BadgeKind::Hq;
            b.w = chip_w(hq.text_w);
            b.x = m.cell - m.margin - b.w;
            b.y = y;
            b.h = chip_h;
            b.text_x = b.x + m.pad;
            b.text_y = y + vpad;
            b.text_max_w = hq.text_w;
            out.push_back(b);
        }
    }
    return out;
}

inline bool rects_intersect(const BadgeRect& a, const BadgeRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// ---------------------------------------------------------------- hidden-head render breaker
// A hidden game head is requested through the manager route (head id, manager = true). Whether the game renders a head id
// no manager row uses is unproven until the in-game probe (brief section 3.1, P-E7); if the first kBreakerFails renders of a
// session all fail or time out, stop asking: the game is not stalled by a loop on heads that will not render.
constexpr int kBreakerFails = 12;
struct Breaker {
    int ok = 0, failed = 0;
    bool off = false;
    void success() { ++ok; }
    // true when this failure trips the breaker
    bool failure() {
        ++failed;
        if (!off && ok == 0 && failed >= kBreakerFails) {
            off = true;
            return true;
        }
        return false;
    }
    void reset() { *this = Breaker(); }
};

// ---------------------------------------------------------------- placeholder text
// "Gareth Southgate" -> "GS", "Pedro" -> "P", "Head #90003" or "" -> "?". Words starting with '#' or '(' are skipped; a
// UTF-8 letter is kept whole.
inline std::string initials(const std::string& name) {
    auto first_char = [](const std::string& w) {
        size_t n = 1;
        const unsigned char c = static_cast<unsigned char>(w[0]);
        if (c >= 0xF0) n = 4;
        else if (c >= 0xE0) n = 3;
        else if (c >= 0xC0) n = 2;
        std::string s = w.substr(0, std::min(n, w.size()));
        if (s.size() == 1 && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
        return s;
    };
    if (name.compare(0, 6, "Head #") == 0) return "?";
    std::vector<std::string> words;
    std::string cur;
    for (char ch : name + " ") {
        if (ch == ' ') {
            if (!cur.empty() && cur[0] != '#' && cur[0] != '(') words.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (words.empty()) return "?";
    std::string s = first_char(words.front());
    if (words.size() > 1) s += first_char(words.back());
    return s;
}

}  // namespace turbo::face_tile
