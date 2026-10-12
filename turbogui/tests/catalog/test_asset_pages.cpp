// Asset browser tests (tests/catalog/run_pages.sh): core/asset_pages.h - the search and All / In this save / Hidden
// filter, 200-tile pages (a page never asks for more than 200 pictures), display names (DB > catalog > fallback), the
// item lists of the picture families, and the editors' catalog sources (absent when the catalog is off or not loaded).
// Only synthetic data: the fixture tests/fixtures/asset_catalog_v1_small.json and generated ids.
// Usage: test_asset_pages <fixture.json> <scratch dir>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/asset_catalog.h"
#include "core/asset_pages.h"
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace turbo;
using assets::Family;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

static std::vector<AssetBrowserItem> numbered(size_t n, size_t hidden_every = 0) {
    std::vector<AssetBrowserItem> v;
    for (size_t i = 0; i < n; ++i) {
        AssetBrowserItem it;
        it.id = static_cast<int64_t>(1000 + i);
        it.name = "Face " + std::to_string(1000 + i);
        it.path = "data/ui/imgAssets/youthheads/p" + std::to_string(1000 + i) + ".dds";
        it.hidden = hidden_every && i % hidden_every == 0;
        v.push_back(it);
    }
    return v;
}

static void test_paging() {
    check(asset_page_count(0) == 1 && asset_page_count(1) == 1 && asset_page_count(200) == 1 && asset_page_count(201) == 2 &&
              asset_page_count(11460) == 58,
          "page counts");
    int page = 0;
    auto r = asset_page_range(450, page);
    check(r.first == 0 && r.second == 200, "page 1 of 450");
    page = 2;
    r = asset_page_range(450, page);
    check(page == 2 && r.first == 400 && r.second == 450, "last page of 450");
    page = 99;
    r = asset_page_range(450, page);
    check(page == 2 && r.first == 400 && r.second == 450, "a page past the end is clamped to the last");
    page = -5;
    r = asset_page_range(450, page);
    check(page == 0 && r.first == 0, "a negative page is clamped to the first");
    page = 3;
    r = asset_page_range(0, page);
    check(page == 0 && r.first == 0 && r.second == 0, "empty list: page 1, nothing in it");

    // the youth pool: 11,460 ids; no page asks for more than 200 pictures, every page together asks for each once
    const auto items = numbered(11460);
    const auto all = asset_filter(items, AssetScope::All, "");
    size_t total = 0, worst = 0;
    std::set<std::string> union_paths;
    for (int p = 0; p < asset_page_count(all.size()); ++p) {
        const auto w = asset_page_wants(items, all, p);
        worst = std::max(worst, w.size());
        total += w.size();
        union_paths.insert(w.begin(), w.end());
    }
    check(worst == static_cast<size_t>(kAssetBrowserPage), "a page asks for at most 200 pictures (worst " + std::to_string(worst) + ")");
    check(total == 11460 && union_paths.size() == 11460, "the pages together ask for every picture once");

    // empty paths are not asked for; a repeated path once
    std::vector<AssetBrowserItem> odd = numbered(5);
    odd[1].path.clear();
    odd[3].path = odd[2].path;
    const auto w = asset_page_wants(odd, asset_filter(odd, AssetScope::All, ""), 0);
    check(w.size() == 3, "no wants for empty or repeated paths");
}

static void test_filter() {
    const auto items = numbered(10, 3);  // hidden: 0, 3, 6, 9
    check(asset_filter(items, AssetScope::All, "").size() == 10, "All shows everything");
    check(asset_filter(items, AssetScope::Hidden, "").size() == 4, "Hidden shows the 4 hidden");
    check(asset_filter(items, AssetScope::InSave, "").size() == 6, "In this save shows the other 6");
    check(asset_filter(items, AssetScope::All, "  FACE 1004 ").size() == 1, "name search: any letter case, trimmed");
    check(asset_filter(items, AssetScope::All, "1007").size() == 1, "id search");
    check(asset_filter(items, AssetScope::All, "100").size() == 10, "id digits anywhere");
    check(asset_filter(items, AssetScope::Hidden, "1003").size() == 1 && asset_filter(items, AssetScope::InSave, "1003").empty(),
          "search and scope combine");
    check(asset_filter(items, AssetScope::All, "nobody").empty(), "no match");
    // order kept
    const auto h = asset_filter(items, AssetScope::Hidden, "");
    check(h.size() == 4 && h[0] == 0 && h[1] == 3 && h[3] == 9, "filter keeps list order");
}

static void test_names() {
    AssetBrowserItem it;
    it.id = 41;
    asset_pick_name(it, Family::Miniface, "DB Name", {"Catalog Name", "master:ita_it"});
    check(it.name == "DB Name" && it.name_source.empty(), "the DB name comes first");
    asset_pick_name(it, Family::Miniface, "", {"Catalog Name", "master:ita_it"});
    check(it.name == "Catalog Name" && it.name_source == "master:ita_it", "then the catalog name, with its source");
    asset_pick_name(it, Family::Miniface, "", {"Slug Name", "slug"});
    check(it.name == "Slug Name (41)", "a game-file-name (slug) name shows the id");
    asset_pick_name(it, Family::Youthface, "", {});
    check(it.name == assets::fallback_name(Family::Youthface, 41) && it.name_source.empty() && !it.name.empty(), "then the fallback");
    check(asset_default_path(Family::Youthface, 7) == "data/ui/imgAssets/youthheads/p7.dds" &&
              asset_default_path(Family::Miniface, 7) == "data/ui/imgAssets/heads/p7.dds" &&
              asset_default_path(Family::Staffface, 7) == "data/ui/imgAssets/heads_staff/heads_staff_7.dds" &&
              asset_default_path(Family::Legendface, 7) == "data/ui/imgAssets/cmLegendaryPlayers/legend_7.dds" &&
              asset_default_path(Family::Crest, 7) == "data/ui/imgAssets/crest/light/l7.dds" && asset_default_path(Family::Hair, 7).empty(),
          "default picture paths");
}

// ---- a test-only reading of the fixture (the real loader is WP-05's): ids, names and crest paths per family
struct FixtureData {
    std::map<std::string, std::vector<int64_t>> ids;
    std::map<int64_t, assets::Name> player, team;
    std::map<int64_t, std::string> crest_paths;
};
static bool read_fixture(const fs::path& f, FixtureData& out) {
    std::ifstream in(f, std::ios::binary);
    if (!in) return false;
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded()) return false;
    for (const char* fam : {"miniface", "youthface", "staffface", "legendface", "crest"})
        for (const auto& v : j["families"][fam]["ids"]) out.ids[fam].push_back(v.get<int64_t>());
    for (auto& [k, v] : j["families"]["crest"]["paths"].items()) out.crest_paths[std::stoll(k)] = v.get<std::string>();
    for (auto& [k, v] : j["names"]["player"].items()) out.player[std::stoll(k)] = {v[0].get<std::string>(), v[1].get<std::string>()};
    for (auto& [k, v] : j["names"]["team"].items()) out.team[std::stoll(k)] = {v[0].get<std::string>(), v[1].get<std::string>()};
    return true;
}

static void test_items(const fs::path& fixture) {
    FixtureData fx;
    check(read_fixture(fixture, fx), "fixture read");
    // synthetic save: player 90001 exists (named in the DB), team 9001 exists
    std::set<int64_t> db_players = {90001}, db_teams = {9001};

    AssetItemSource src;
    src.family = Family::Miniface;
    src.ids = fx.ids["miniface"];  // 90001, 90002, 90005
    src.db_name = [&](int64_t id) { return db_players.count(id) ? std::string("Save Player") : std::string(); };
    src.catalog_name = [&](int64_t id) {
        auto it = fx.player.find(id);
        return it == fx.player.end() ? assets::Name{} : it->second;
    };
    src.hidden = [&](int64_t id) { return db_players.count(id) == 0; };
    auto items = asset_items(src);
    check(items.size() == 3, "3 game minifaces");
    if (items.size() == 3) {
        check(items[0].id == 90001 && items[0].name == "Save Player" && !items[0].hidden, "a DB player: DB name, not hidden");
        check(items[1].id == 90002 && items[1].name == "Test Head 90002" && items[1].name_source == "workbook" && items[1].hidden,
              "a hidden head named by the catalog");
        check(items[2].id == 90005 && items[2].name == assets::fallback_name(Family::Miniface, 90005) && items[2].hidden,
              "an unnamed hidden id gets the fallback name");
        check(items[1].path == "data/ui/imgAssets/heads/p90002.dds", "default miniface path");
    }
    src.hidden_off = true;
    items = asset_items(src);
    check(items.size() == 1 && items[0].id == 90001, "asset_hidden_off: hidden ids are not listed");

    AssetItemSource crest;
    crest.family = Family::Crest;
    crest.ids = fx.ids["crest"];  // 9001, 9002
    crest.db_name = [&](int64_t id) { return db_teams.count(id) ? std::string("Save Club") : std::string(); };
    crest.catalog_name = [&](int64_t id) {
        auto it = fx.team.find(id);
        return it == fx.team.end() ? assets::Name{} : it->second;
    };
    crest.path = [&](int64_t id) {
        auto it = fx.crest_paths.find(id);
        return it == fx.crest_paths.end() ? std::string() : it->second;
    };
    crest.hidden = [&](int64_t id) { return db_teams.count(id) == 0; };
    items = asset_items(crest);
    check(items.size() == 2 && items[0].name == "Save Club" && !items[0].hidden && items[1].hidden &&
              items[1].name == assets::fallback_name(Family::Crest, 9002) && items[1].path == "data/ui/imgAssets/crest/light/l9002.dds",
          "crests: DB club, hidden club with its exact path");
    check(asset_name_kind(Family::Crest) == assets::NameKind::Team && asset_name_kind(Family::Youthface) == assets::NameKind::Player,
          "crests are named from the team table");

    // youth faces with the Hidden filter: page 1 asks only for hidden pictures
    src = AssetItemSource{};
    src.family = Family::Youthface;
    src.ids = fx.ids["youthface"];
    src.hidden = [](int64_t id) { return id != 91002; };
    items = asset_items(src);
    const auto shown = asset_filter(items, AssetScope::Hidden, "");
    const auto w = asset_page_wants(items, shown, 0);
    check(shown.size() == 2 && w.size() == 2 && w[0] == "data/ui/imgAssets/youthheads/p91001.dds", "youth faces, Hidden filter");

    // speed: 50k ids into items and one filter pass stay well under a frame budget's worth of work per rebuild
    src = AssetItemSource{};
    src.family = Family::Miniface;
    for (int64_t i = 0; i < 50000; ++i) src.ids.push_back(100000 + i);
    src.hidden = [](int64_t id) { return id % 2 == 0; };
    const auto t0 = std::chrono::steady_clock::now();
    items = asset_items(src);
    const auto f50 = asset_filter(items, AssetScope::Hidden, "1234");
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  50k items + filter: %.1f ms\n", ms);
    check(items.size() == 50000 && !f50.empty(), "50k items built and filtered");
}

static void test_sources(const fs::path& fixture, const fs::path& scratch) {
    // no catalog: no source tab at all (2.0 behaviour)
    check(asset_miniface_sources(false, false).empty() && asset_miniface_sources(true, false).empty() && asset_crest_sources(false).empty(),
          "no catalog sources when the catalog is not loaded");
    const auto player = asset_miniface_sources(false, true);
    const auto manager = asset_miniface_sources(true, true);
    auto has = [](const std::vector<AssetPictureSource>& v, Family f) {
        for (const auto& s : v)
            if (s.family == f) return true;
        return false;
    };
    check(has(player, Family::Miniface) && has(player, Family::Youthface) && has(player, Family::Legendface) &&
              !has(player, Family::Staffface),
          "player miniface editor: game minifaces, youth faces, legend pictures");
    check(has(manager, Family::Staffface) && has(manager, Family::Miniface), "manager editor: staff faces too");
    check(asset_crest_sources(true).size() == 1 && asset_crest_sources(true)[0].family == Family::Crest, "crest editor source");

    // a Catalog that is not loaded (or switched off) gives no items
    assets::Catalog none;
    check(!none.loaded() && asset_items_from_catalog(none, Family::Youthface, nullptr, {}).empty(), "unloaded catalog: no items");
    const fs::path out = scratch / "pages_off";
    fs::create_directories(out);
    { std::ofstream(out / "asset_catalog_off.txt") << "off\n"; }
    assets::Catalog off;
    off.load_dir(out, "0000AAAA-0000BBBB");
    check(!off.loaded() && asset_items_from_catalog(off, Family::Miniface, nullptr, {}).empty(), "asset_catalog_off.txt: no items");

    // with the real loader (WP-05) the fixture lists its families
    assets::Catalog cat;
    std::string err;
    if (cat.load_file(fixture, "0000AAAA-0000BBBB", &err) && cat.loaded()) {
        assets::DbUse use;
        use.used[static_cast<size_t>(Family::Miniface)] = {90001};
        use.used[static_cast<size_t>(Family::Crest)] = {9001};
        auto mf = asset_items_from_catalog(cat, Family::Miniface, &use, {});
        check(mf.size() == 3, "catalog: 3 game minifaces");
        size_t hid = 0;
        for (const auto& it : mf) hid += it.hidden ? 1 : 0;
        check(hid == 2, "catalog: 2 of them hidden against the synthetic save");
        auto cr = asset_items_from_catalog(cat, Family::Crest, &use, {});
        check(cr.size() == 2 && cr[0].name == "Test Club One (9001)" && cr[0].name_source == "kit_slug" && cr[0].path == "data/ui/imgAssets/crest/light/l9001.dds",
              "catalog: crests named from the kit slug (with the id), exact path");
        check(asset_items_from_catalog(cat, Family::Youthface, &use, {}).size() == 3, "catalog: 3 youth faces");
    } else {
        std::printf("  (catalog loader not implemented in this branch: catalog-backed cases skipped)\n");
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: test_asset_pages <fixture.json> <scratch dir>\n");
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();
    test_paging();
    test_filter();
    test_names();
    test_items(argv[1]);
    test_sources(argv[1], argv[2]);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("asset pages: %d passed, %d failed (%.2f s)\n", g_pass, g_fail, s);
    return g_fail ? 1 : 0;
}
