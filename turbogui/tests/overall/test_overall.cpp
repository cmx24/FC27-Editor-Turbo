// Test of core/overall.cpp (formula accuracy on real players, +1 / -1 round trips) and core/archetypes.cpp (the overall is kept).
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/archetypes.h"
#include "core/overall.h"

using namespace turbo;
using namespace turbo::overall;

static int g_fail = 0;
#define CHECK(c, ...)                                              \
    do {                                                           \
        if (!(c)) {                                                \
            if (++g_fail <= 40) {                                  \
                std::printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
                std::printf(__VA_ARGS__);                          \
                std::printf("\n");                                 \
            }                                                      \
        }                                                          \
    } while (0)

struct P {
    int pos = 0, ovr = 0;
    Attrs a{};
};

static std::vector<P> load(const std::string& path) {
    std::vector<P> out;
    std::ifstream f(path, std::ios::binary);
    if (!f) return out;
    std::string line;
    std::getline(f, line);
    std::vector<std::string> head;
    std::stringstream hs(line);
    std::string c;
    while (std::getline(hs, c, '\t')) head.push_back(c);
    std::map<std::string, size_t> col;
    for (size_t i = 0; i < head.size(); ++i) col[head[i]] = i;
    if (!col.count("overallrating") || !col.count("preferredposition1")) return out;
    while (std::getline(f, line)) {
        std::vector<std::string> v;
        std::stringstream ls(line);
        while (std::getline(ls, c, '\t')) v.push_back(c);
        if (v.size() < head.size()) continue;
        P p;
        p.pos = std::atoi(v[col["preferredposition1"]].c_str());
        p.ovr = std::atoi(v[col["overallrating"]].c_str());
        bool ok = true;
        for (int i = 0; i < kAttrCount; ++i) {
            auto it = col.find(kAttrNames[i]);
            if (it == col.end()) { ok = false; break; }
            p.a[static_cast<size_t>(i)] = std::atoi(v[it->second].c_str());
        }
        if (ok && p.ovr > 40) out.push_back(p);   // rows with ovr <= 40 are placeholder players with no real formula
    }
    return out;
}

int main(int argc, char** argv) {
    // ---- synthetic checks
    {
        Attrs a;
        a.fill(70);
        CHECK(compute_overall(25, a) >= 69 && compute_overall(25, a) <= 72, "flat 70 striker = %d", compute_overall(25, a));
        Attrs hi;
        hi.fill(99);
        CHECK(compute_overall(25, hi) == 99, "all 99 -> 99");
        CHECK(!adjust(25, hi, 1), "no +1 at 99");
        Attrs lo;
        lo.fill(1);
        CHECK(compute_overall(25, lo) == 1, "all 1 -> 1");
        CHECK(!adjust(25, lo, -1), "no -1 at 1");
        Attrs cp = a;
        CHECK(adjust(25, cp, 1) && compute_overall(25, cp) == compute_overall(25, a) + 1, "synthetic +1");
        CHECK(group_of_position(0) == GroupGK && group_of_position(5) == GroupCB && group_of_position(24) == GroupST &&
                  group_of_position(23) == GroupWide && group_of_position(10) == GroupDM && group_of_position(18) == GroupAM &&
                  group_of_position(2) == GroupFB && group_of_position(14) == GroupCM, "groups");
        for (int g = 0; g < kGroupCount; ++g)
            CHECK(!archetypes::of_group(static_cast<Group>(g)).empty(), "group %d has no archetype", g);
    }
    const std::string path = argc > 1 ? argv[1] : "C:/FC_Tools/FC Editor/_temp/players.txt";
    std::vector<P> ps = load(path);
    if (ps.empty()) {
        std::printf("players TSV not found (%s): real-player checks skipped\n", path.c_str());
    } else {
        // ---- formula accuracy
        long exact = 0, within1 = 0, total = 0;
        double abs_err = 0;
        std::map<int, std::array<long, 3>> by_pos;   // total, exact, within1
        for (const P& p : ps) {
            const int c = compute_overall(p.pos, p.a);
            const int d = std::abs(c - p.ovr);
            ++total;
            exact += d == 0;
            within1 += d <= 1;
            abs_err += d;
            auto& b = by_pos[p.pos];
            ++b[0];
            b[1] += d == 0;
            b[2] += d <= 1;
        }
        std::printf("formula on %ld real players: mean abs error %.3f, exact %.1f%%, within +-1 %.2f%%\n", total, abs_err / total,
                    100.0 * exact / total, 100.0 * within1 / total);
        for (auto& b : by_pos)
            std::printf("  pos %2d: n=%5ld exact %.1f%% within1 %.2f%%\n", b.first, b.second[0], 100.0 * b.second[1] / b.second[0],
                        100.0 * b.second[2] / b.second[0]);
        CHECK(100.0 * within1 / total > 99.0, "formula within +-1 only %.2f%%", 100.0 * within1 / total);
        CHECK(100.0 * exact / total > 85.0, "formula exact only %.1f%%", 100.0 * exact / total);

        // ---- +1 / -1 on 200+ real players (spread over the whole list)
        long tried = 0, up_ok = 0, down_ok = 0, roundtrip = 0, refused_up = 0, refused_down = 0;
        const size_t step = std::max<size_t>(1, ps.size() / 600);
        for (size_t i = 0; i < ps.size(); i += step) {
            const P& p = ps[i];
            const int base = compute_overall(p.pos, p.a);
            ++tried;
            Attrs u = p.a;
            if (adjust(p.pos, u, 1)) {
                ++up_ok;
                CHECK(compute_overall(p.pos, u) == base + 1, "+1 landed on %d, base %d", compute_overall(p.pos, u), base);
                for (int k = 0; k < kAttrCount; ++k) CHECK(u[static_cast<size_t>(k)] >= 1 && u[static_cast<size_t>(k)] <= 99, "range");
                Attrs d = u;
                if (adjust(p.pos, d, -1)) {
                    CHECK(compute_overall(p.pos, d) == base, "+1 then -1 gave %d, base %d", compute_overall(p.pos, d), base);
                    roundtrip += compute_overall(p.pos, d) == base;
                }
            } else {
                ++refused_up;
                CHECK(base >= 98, "refused +1 at computed overall %d", base);
            }
            Attrs d2 = p.a;
            if (adjust(p.pos, d2, -1)) {
                ++down_ok;
                CHECK(compute_overall(p.pos, d2) == base - 1, "-1 landed on %d, base %d", compute_overall(p.pos, d2), base);
                Attrs u2 = d2;
                if (adjust(p.pos, u2, 1)) CHECK(compute_overall(p.pos, u2) == base, "-1 then +1 gave %d, base %d", compute_overall(p.pos, u2), base);
            } else {
                ++refused_down;
            }
        }
        std::printf("+-1 on %ld players: +1 ok %ld (refused %ld), -1 ok %ld (refused %ld), +1/-1 round trips ok %ld\n", tried, up_ok,
                    refused_up, down_ok, refused_down, roundtrip);
        CHECK(tried >= 200, "tried only %ld", tried);
        CHECK(up_ok > 0.95 * tried && down_ok > 0.95 * tried, "too many refusals");
        // ten clicks in a row still land exactly
        {
            const P& p = ps[ps.size() / 3];
            Attrs a = p.a;
            const int base = compute_overall(p.pos, a);
            int n = 0;
            while (n < 10 && adjust(p.pos, a, 1)) ++n;
            CHECK(compute_overall(p.pos, a) == base + n && n == std::min(10, 99 - base), "10 clicks: n=%d ovr %d from %d", n, compute_overall(p.pos, a), base);
        }

        // ---- archetypes: every archetype of the player's group keeps his overall
        long apps = 0, exact_apps = 0, exact_hi = 0, apps_hi = 0, changed = 0;
        for (size_t i = 0; i < ps.size(); i += std::max<size_t>(1, ps.size() / 400)) {
            const P& p = ps[i];
            for (const archetypes::Archetype* ar : archetypes::of_group(group_of_position(p.pos))) {
                archetypes::Result r;
                CHECK(archetypes::apply(*ar, p.pos, p.a, r), "apply %s", ar->name);
                ++apps;
                exact_apps += r.exact;
                if (p.ovr <= 90) {
                    ++apps_hi;
                    exact_hi += r.exact;
                }
                changed += r.attrs != p.a;
                for (int k = 0; k < kAttrCount; ++k) CHECK(r.attrs[static_cast<size_t>(k)] >= 1 && r.attrs[static_cast<size_t>(k)] <= 99, "range");
            }
        }
        std::printf("archetypes: %ld applications, overall kept exactly in %.2f%% (%.2f%% for players up to 90), attributes changed in %.1f%%\n", apps,
                    100.0 * exact_apps / apps, 100.0 * exact_hi / apps_hi, 100.0 * changed / apps);
        CHECK(100.0 * exact_hi / apps_hi > 99.0, "archetype keeps the overall in only %.2f%%", 100.0 * exact_hi / apps_hi);
        // one example, for the eye
        const P& ex = ps[ps.size() / 2];
        const auto list = archetypes::of_group(group_of_position(ex.pos));
        if (!list.empty()) {
            archetypes::Result r;
            archetypes::apply(*list.front(), ex.pos, ex.a, r);
            std::printf("example: pos %d ovr %d -> %s (%d -> %d):", ex.pos, ex.ovr, list.front()->name, r.overall_before, r.overall_after);
            for (int k = 0; k < kAttrCount; ++k)
                if (r.attrs[static_cast<size_t>(k)] != ex.a[static_cast<size_t>(k)]) std::printf(" %s %d>%d", kAttrNames[k], ex.a[static_cast<size_t>(k)], r.attrs[static_cast<size_t>(k)]);
            std::printf("\n");
        }
    }
    if (g_fail) std::printf("%d FAILED\n", g_fail);
    else std::printf("all checks passed\n");
    return g_fail ? 1 : 0;
}
