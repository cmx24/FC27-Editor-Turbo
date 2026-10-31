// ui/ui_face_tile.h (pure part of the real-face chooser's tile, 5.0c E7-realface): T3 the picture ladder, T4 the badge layout
// (every combination of chips, three cell sizes, two font scales), the breaker and the placeholder initials.
#include <cstdio>
#include <string>
#include <vector>

#include "ui/ui_face_tile.h"

using namespace turbo::face_tile;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    if (ok) ++g_pass;
    else {
        ++g_fail;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

// Advances of the labels at the overlay's 17 px font (about 8 px per character; the native test uses CalcTextSize)
static float text_w(BadgeKind k, float scale) { return 8.0f * scale * static_cast<float>(std::string(badge_text(k)).size()); }

static void test_ladder() {
    TileIn t;
    t.can_render = true;
    check(choose_source(t) == Source::Rendering, "T3: nothing cached, no picture shipped, rendering allowed -> Rendering (never a 'no picture' word)");
    TileIn u;
    check(choose_source(u) == Source::Unavailable, "T3: nothing, 3D heads off -> Unavailable");
    t.given_up = true;
    check(choose_source(t) == Source::Unavailable, "T3: the head failed twice -> Unavailable");
    t = TileIn();
    t.can_render = true;
    t.has_mini = true;
    check(choose_source(t) == Source::Miniface, "T3: a miniface that is loading or drawn -> Miniface");
    t.mini_missing = true;
    check(choose_source(t) == Source::Rendering, "T3: legacy Missing + a render possible -> Rendering, never Unavailable");
    t.can_render = false;
    check(choose_source(t) == Source::Unavailable, "T3: legacy Missing + no render route -> Unavailable");
    t = TileIn();
    t.has_mini = true;
    t.mini_missing = true;
    t.has_legend = true;
    check(choose_source(t) == Source::Legend, "T3: miniface missing, legend shipped -> Legend");
    t.legend_missing = true;
    check(choose_source(t) == Source::Unavailable, "T3: both missing, nothing else -> Unavailable");
    t = TileIn();
    t.has_mini = true;
    t.has_legend = true;
    check(choose_source(t) == Source::Miniface, "T3: miniface before legend");
    t.cached_p = true;
    check(choose_source(t) == Source::Render3dP, "T3: a cached player render beats the miniface");
    t.cached_m = true;
    check(choose_source(t) == Source::Render3dM, "T3: a cached head render (m_) is the first choice");
    t = TileIn();
    t.can_render = true;
    t.cached_m = true;
    t.mini_missing = true;
    check(choose_source(t) == Source::Render3dM, "T3: a cached render wins over everything");
}

static void test_layout() {
    const BadgeKind kinds[] = {BadgeKind::Hidden, BadgeKind::NameMatch, BadgeKind::RenderPending, BadgeKind::Hq};
    int combos = 0, bad = 0;
    for (float cell : {72.0f, 96.0f, 144.0f}) {
        for (float scale : {1.0f, 1.5f}) {
            for (int mask = 1; mask < 16; ++mask) {
                std::vector<BadgeIn> in;
                for (int i = 0; i < 4; ++i)
                    if (mask & (1 << i)) in.push_back({kinds[i], text_w(kinds[i], scale)});
                BadgeMetrics m;
                m.cell = cell;
                m.line_h = 17.0f * scale;
                m.pad = 4.0f;
                m.plus_w = 16.0f * scale;
                const std::vector<BadgeRect> out = layout_badges(m, in);
                ++combos;
                bool ok = !out.empty();
                std::string why;
                int shown = 0, folded = 0;
                for (size_t i = 0; i < out.size(); ++i) {
                    const BadgeRect& r = out[i];
                    if (r.x < 0 || r.y < 0 || r.x + r.w > cell + 0.001f) {
                        ok = false;
                        why += " outside";
                    }
                    if (!r.plus && !r.clipped && std::string(badge_text(r.kind)).size() * 8.0f * scale > r.text_max_w + 0.001f) {
                        ok = false;
                        why += " text wider than chip";
                    }
                    if (r.plus) folded += r.plus_n;
                    else ++shown;
                    for (size_t j = i + 1; j < out.size(); ++j)
                        if (rects_intersect(r, out[j])) {
                            ok = false;
                            why += " overlap";
                        }
                }
                if (shown + folded != static_cast<int>(in.size())) {
                    ok = false;
                    why += " a chip was lost";
                }
                if (!ok) {
                    ++bad;
                    std::printf("  layout cell %.0f scale %.1f mask %d:%s\n", cell, scale, mask, why.c_str());
                }
            }
        }
    }
    check(bad == 0, "T4: every combination of Hidden / name match / 3D pending / HQ at cell 72, 96, 144 and font scale 1, 1.5: inside, no overlap, nothing lost (" +
                        std::to_string(combos) + " layouts)");

    // the reported defect, 5.0b TEST: Hidden and name match
    {
        BadgeMetrics m;
        std::vector<BadgeIn> in = {{BadgeKind::Hidden, text_w(BadgeKind::Hidden, 1.0f)}, {BadgeKind::NameMatch, text_w(BadgeKind::NameMatch, 1.0f)}};
        const std::vector<BadgeRect> out = layout_badges(m, in);
        check(out.size() == 2 && !rects_intersect(out[0], out[1]) && out[0].y < out[1].y && out[0].x == out[1].x,
              "T4: Hidden + name match on a 96 px cell: two stacked chips that do not touch");
        // the old placement (ui_faces.cpp 5.0b): Hidden at x = 3, name match right-anchored at cell - w - 3, both at y = 2
        const float hw = text_w(BadgeKind::Hidden, 1.0f), nw = text_w(BadgeKind::NameMatch, 1.0f);
        BadgeRect a, b;
        a.x = 3.0f; a.y = 2.0f; a.w = hw; a.h = 17.0f;
        b.x = 96.0f - nw - 3.0f; b.y = 2.0f; b.w = nw; b.h = 17.0f;
        check(rects_intersect(a, b), "T4: the OLD placement of the same two labels does intersect (documented defect)");
        // the second collision the brief found: "3D pending" and HQ on a save tile
        const float pw = text_w(BadgeKind::RenderPending, 1.0f), qw = text_w(BadgeKind::Hq, 1.0f);
        BadgeRect c, d;
        c.x = 3.0f; c.y = 2.0f; c.w = pw; c.h = 17.0f;
        d.x = 96.0f - qw - 3.0f; d.y = 2.0f; d.w = qw; d.h = 17.0f;
        check(rects_intersect(c, d), "T4: the OLD placement of '3D pending' + HQ also intersects at 96 px");
        in = {{BadgeKind::RenderPending, pw}, {BadgeKind::Hq, qw}};
        const std::vector<BadgeRect> out2 = layout_badges(m, in);
        check(out2.size() == 2 && !rects_intersect(out2[0], out2[1]), "T4: '3D pending' + HQ now do not intersect");
    }
    // the worst case folds into "+n"
    {
        BadgeMetrics m;
        std::vector<BadgeIn> in = {{BadgeKind::Hidden, text_w(BadgeKind::Hidden, 1.0f)}, {BadgeKind::NameMatch, text_w(BadgeKind::NameMatch, 1.0f)},
                                   {BadgeKind::RenderPending, text_w(BadgeKind::RenderPending, 1.0f)}};
        const std::vector<BadgeRect> out = layout_badges(m, in);
        int total = 0;
        for (const BadgeRect& r : out) total += r.plus ? r.plus_n : 1;
        check(total == 3 && out.size() <= 3, "T4: three left chips on 96 px: shown or folded into '+n', none lost");
        m.cell = 144.0f;
        const std::vector<BadgeRect> big = layout_badges(m, in);
        check(big.size() == 3 && !big[2].plus, "T4: all three fit a 144 px cell");
    }
    // a label wider than the cell is clipped, not drawn out of it
    {
        BadgeMetrics m;
        m.cell = 72.0f;
        std::vector<BadgeIn> in = {{BadgeKind::NameMatch, 200.0f}};
        const std::vector<BadgeRect> out = layout_badges(m, in);
        check(out.size() == 1 && out[0].clipped && out[0].x + out[0].w <= 72.0f - m.margin + 0.001f, "T4: a label wider than the cell is clipped inside it");
    }
    for (BadgeKind k : {BadgeKind::Hidden, BadgeKind::NameMatch, BadgeKind::RenderPending, BadgeKind::RenderFailed, BadgeKind::Hq}) {
        const ChipColors c = chip_colors(k);
        check(contrast_ratio(c.bg, c.fg) >= 4.5, std::string("T4: contrast of the '") + badge_text(k) + "' chip >= 4.5:1");
    }
}

static void test_breaker() {
    Breaker b;
    bool tripped = false;
    for (int i = 0; i < kBreakerFails - 1; ++i) tripped = tripped || b.failure();
    check(!tripped && !b.off, "T2: 11 failures do not trip the breaker");
    check(b.failure() && b.off, "T2: the 12th failure trips it");
    check(!b.failure(), "T2: it trips once");
    Breaker c;
    c.success();
    for (int i = 0; i < 50; ++i) c.failure();
    check(!c.off, "T2: one success in the session keeps the route on");
    b.reset();
    check(!b.off && b.failed == 0, "T2: reset (a new catalog, or Render all pressed again)");
}

static void test_initials() {
    check(initials("Gareth Southgate") == "GS", "initials: two words");
    check(initials("pedro") == "P", "initials: one word, upper-cased");
    check(initials("Head #90003") == "?", "initials: an unnamed head");
    check(initials("") == "?", "initials: empty");
    check(initials("Test Head Three #90003") == "TT", "initials: the id token is skipped");
    check(initials("\xC3\x89mile Zola") == "\xC3\x89Z", "initials: a UTF-8 letter is kept whole");
}

int main() {
    test_ladder();
    test_layout();
    test_breaker();
    test_initials();
    std::printf("face_tile: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
