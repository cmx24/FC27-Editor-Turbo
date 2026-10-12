// "Game heads" of the real-face chooser (core/game_heads.h) and its menus of at most 5 choices (core/face_filter.h),
// tested in seconds with g++ only (tests/catalog/run_game_heads.sh). Input: the synthetic schema v1 fixture; no game file.
// Usage: test_game_heads <fixture.json>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "core/face3d_looks.h"
#include "core/face_filter.h"
#include "core/game_heads.h"
#include "nlohmann/json.hpp"

using nlohmann::json;
using namespace turbo;
using game_heads::HeadCatalog;
using game_heads::HeadInfo;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

// Test-only loader: the head3d family, miniface ids and player names of a schema v1 file, as from_catalog() copies them
// from a loaded assets::Catalog (the real loader is WP-05's)
static HeadCatalog load_fixture(const std::string& path, assets::State state) {
    std::ifstream in(path, std::ios::binary);
    const json j = json::parse(in);
    HeadCatalog c;
    c.state = state;
    c.build_key = j.at("build_key").get<std::string>();
    const json& f = j.at("families");
    const json& h = f.at("head3d");
    std::vector<int64_t> minis = f.at("miniface").at("ids").get<std::vector<int64_t>>();
    std::vector<int64_t> generic = h.at("generic").get<std::vector<int64_t>>();
    for (int64_t id : h.at("ids").get<std::vector<int64_t>>()) {
        HeadInfo i;
        i.id = id;
        const std::string k = std::to_string(id);
        if (h.at("vars").contains(k)) i.vars = h.at("vars").at(k).get<std::vector<int>>();
        if (h.at("toc").contains(k)) i.toc = h.at("toc").at(k).get<std::string>();
        i.generic = std::find(generic.begin(), generic.end(), id) != generic.end();
        i.miniface = std::find(minis.begin(), minis.end(), id) != minis.end();
        const json& names = j.at("names").at("player");
        if (names.contains(k)) {
            i.name = names.at(k).at(0).get<std::string>();
            i.source = names.at(k).at(1).get<std::string>();
        }
        c.heads.push_back(i);
    }
    std::sort(c.heads.begin(), c.heads.end(), [](const HeadInfo& a, const HeadInfo& b) { return a.id < b.id; });
    return c;
}

static void test_game_heads(const std::string& fixture) {
    std::printf("game heads: list, names, gate\n");
    HeadCatalog cur = load_fixture(fixture, assets::State::Current);
    check(cur.heads.size() == 5 && cur.find(90002) && cur.find(90002)->vars.size() == 2 && cur.find(90100)->generic &&
              cur.find(90001)->miniface && !cur.find(90003)->miniface && !cur.find(41),
          "fixture heads");
    // synthetic DB: 90002 is a player's head, 90004 a manager's (any class counts as used)
    const game_heads::UsedHeads used = {90002, 90004, 123};
    std::vector<const HeadInfo*> hid = game_heads::hidden_heads(cur, used);
    check(hid.size() == 2 && hid[0]->id == 90001 && hid[1]->id == 90003, "hidden heads: not used by a row, generic 90100 left out");
    check(game_heads::display_name(*cur.find(90001)) == "Test Head 90001" && game_heads::display_name(*cur.find(90003)) == "Test Head 90003 #90003" &&
              game_heads::display_name(*cur.find(90004)) == "Head #90004",
          "names: catalog name, a game file name carries the id, fallback Head #id");
    std::string why;
    check(game_heads::may_give(cur, 90001, &why) && why.empty(), "a hidden catalog head may be given");
    check(!game_heads::may_give(cur, 41, &why) && why.find("not a 3D head") != std::string::npos, "an id outside the catalog is refused");
    check(!game_heads::may_give(cur, 90100, &why) && why.find("generic") != std::string::npos, "a generic head is refused");
    const std::vector<game_heads::FieldWrite> w = game_heads::head_writes(*cur.find(90004));
    check(w.size() == 4 && std::string(w[0].field) == "headassetid" && w[0].value == 90004 && w[1].value == 0 && w[2].value == 1 &&
              std::string(w[3].field) == "headvariation" && w[3].value == 2,
          "writes: head id, class 0, high quality, the first shipped variation (90004: 2)");
    HeadInfo novars;
    novars.id = 5;
    check(game_heads::head_writes(novars)[3].value == 0, "no variation listed: 0");

    HeadCatalog stale = load_fixture(fixture, assets::State::Stale);
    check(game_heads::hidden_heads(stale, used).size() == 2, "stale: the list stays");
    check(!game_heads::may_give(stale, 90001, &why) && why.find("another game build") != std::string::npos &&
              why.find(assets::rebuild_command()) != std::string::npos,
          "stale: Apply refused with the reason and the rebuild command");

    HeadCatalog hoff = load_fixture(fixture, assets::State::Current);
    hoff.hidden_off = true;
    check(game_heads::hidden_heads(hoff, used).empty() && !game_heads::may_give(hoff, 90001, &why) &&
              why.find("asset_hidden_off.txt") != std::string::npos,
          "asset_hidden_off.txt: nothing listed or offered");
    HeadCatalog off;
    off.state = assets::State::Off;
    check(game_heads::hidden_heads(off, used).empty() && !game_heads::may_give(off, 90001, &why) &&
              why.find("asset_catalog_off.txt") != std::string::npos,
          "asset_catalog_off.txt: 2.0 behaviour");
    HeadCatalog none;
    check(game_heads::hidden_heads(none, used).empty() && !game_heads::may_give(none, 90001, &why), "no catalog: nothing");

    // from_catalog on a catalog that is not loaded: its state, no heads (the loader itself is WP-05's)
    assets::Catalog empty;
    HeadCatalog fc = game_heads::from_catalog(empty);
    check(!fc.loaded() && fc.heads.empty() && game_heads::hidden_heads(fc, used).empty(), "from_catalog: not loaded");
}

static void test_five_per_menu() {
    std::printf("real-face chooser: at most 5 choices per menu\n");
    using namespace faces;
    // every value of each database facet's FC 27 range lands in a named group, at most 5 groups
    const int64_t kMaxRaw[kDbFacetCount] = {16000, 100, 50, 10000, 512, 50, 64, 1};
    for (int i = 0; i < kDbFacetCount; ++i) {
        const Facet fc = static_cast<Facet>(i);
        std::vector<Face> every;
        std::vector<size_t> idx;
        bool named = true;
        for (int64_t raw = 0; raw <= kMaxRaw[i]; ++raw) {
            Face f;
            f.raw[fc] = raw;
            every.push_back(f);
            idx.push_back(idx.size());
            const std::string l = key_label(fc, facet_key(fc, raw));
            named = named && l.rfind("Group ", 0) != 0 && l.rfind("Gender ", 0) != 0;
        }
        const std::vector<Count> cs = facet_counts(every, idx, Filter(), fc);
        int listed = 0;
        for (const auto& c : cs) listed += c.n;
        check(named && cs.size() <= size_t(kMaxBuckets) && listed == int(every.size()) && !facet_exact(fc),
              std::string(facet_title(fc)) + ": " + std::to_string(cs.size()) + " choices");
    }
    // the 3D facets: every look value in a group, at most 5 groups, no "not classified yet" choice
    for (int t = 0; t < face3d::kTraitCount; ++t) {
        const Facet fc = static_cast<Facet>(kSkin3d + t);
        std::vector<Face> every;
        std::vector<size_t> idx;
        for (int v = 0; v < face3d::value_count(t); ++v) {
            Face f;
            f.raw[fc] = v;
            every.push_back(f);
            idx.push_back(idx.size());
        }
        Face un;
        un.raw[fc] = kUnclassified;
        every.push_back(un);
        idx.push_back(idx.size());
        const std::vector<Count> cs = facet_counts(every, idx, Filter(), fc);
        int listed = 0;
        bool unclassified = false;
        for (const auto& c : cs) {
            listed += c.n;
            unclassified = unclassified || c.key == kUnclassified;
        }
        check(cs.size() <= size_t(kMaxBuckets) && int(cs.size()) == group_count3d(fc) && listed == face3d::value_count(t) && !unclassified,
              std::string("3D ") + face3d::trait_title(t) + ": " + std::to_string(cs.size()) + " choices");
    }
    check(kSortCount <= kMaxBuckets, "sort menu");
    // groups and exact tooltips
    auto group = [](Facet f, int64_t raw) { return key_label(f, facet_key(f, raw)); };
    check(group(kSkin, 10) == group(kSkin, 20) && group(kSkin, 20) != group(kSkin, 30) && group(kSkin, 100).rfind("Dark", 0) == 0 &&
              trait_label(kSkin, 20) == "Caucasian 2" && trait_label(kSkin, 55) == "Skin tone 55",
          "skin tone: pairs, exact tooltip");
    check(group(kHairColour, 0) == "Black" && group(kHairColour, 21) == "Black" && group(kHairColour, 3) == "Brown" &&
              group(kHairColour, 16) == "Blonde" && group(kBeardColour, 7) == "Red & ginger" && group(kHairColour, 40) == "Grey, white & dyed" &&
              trait_label(kHairColour, 21) == "Jet Black" && trait_label(kBeardColour, 16) == "Golden Blonde",
          "hair colours: 5 groups, exact tooltip");
    check(group(kEyes, 1) == "Blue" && group(kEyes, 8) == "Blue" && group(kEyes, 10) == "Green" && group(kEyes, 5) == "Hazel & light brown" &&
              group(kEyes, 9) == "Brown" && group(kEyes, 40) == "Other" && trait_label(kEyes, 5) == "Hazel",
          "eyes: 5 groups, exact tooltip");
    check(group(kHairType3d, face3d::kWavy) == "Wavy & curly" && group(kHairType3d, face3d::kCurly) == "Wavy & curly" &&
              group(kHeadwear3d, face3d::kHat) == "Hat / cap" && group(kHairColour3d, face3d::kHairDarkBrown) == "Brown" &&
              trait_label(kHairColour3d, face3d::kHairDarkBrown) == "Dark brown" && group(kFacialHair3d, face3d::kLongBeard) == "Full & long beard" &&
              key_label(kFacialHair3d, kUnclassified) == "not classified yet",
          "3D groups, exact tooltip");
    // a filter on a group matches every value of it; heads without a look never match
    Face a, b, c;
    a.raw[kHairType3d] = face3d::kWavy;
    b.raw[kHairType3d] = face3d::kCurly;
    c.raw[kHairType3d] = kUnclassified;
    Filter flt;
    flt.sel[kHairType3d] = facet_key(kHairType3d, face3d::kWavy);
    check(matches(a, flt) && matches(b, flt) && !matches(c, flt), "group filter");
    // a game head (no database fields, no look) never matches a facet filter
    Face g;
    g.game_head = true;
    flt.clear();
    flt.sel[kGender] = 0;
    check(!matches(g, flt) && matches(g, Filter()), "game head: only unfiltered");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: test_game_heads <fixture.json>\n");
        return 2;
    }
    try {
        test_game_heads(argv[1]);
        test_five_per_menu();
    } catch (const std::exception& e) {
        std::printf("  FAIL exception: %s\n", e.what());
        ++g_fail;
    }
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
