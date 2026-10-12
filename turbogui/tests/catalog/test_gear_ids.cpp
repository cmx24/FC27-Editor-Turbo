// Tests of core/gear_ids.h (WP-08: gear galleries from the game asset catalog) on the synthetic fixture
// tests/fixtures/asset_catalog_v1_small.json. g++ only, no game files: run with tests/catalog/run_gear.sh.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "core/asset_catalog.h"
#include "core/gear_ids.h"
#include "nlohmann/json.hpp"

using namespace turbo;
using gear::FamilyView;
using gear::IdSet;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what, int line) {
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s (line %d)\n", what.c_str(), line);
    }
}
#define CHECK(c, m) check((c), (m), __LINE__)

// Test-only loader: one gear family of the fixture into a FamilyView (the real one comes from assets::Catalog)
static FamilyView view_of(const nlohmann::json& cat, const char* family, bool current, bool hidden_off = false) {
    FamilyView v;
    const nlohmann::json& f = cat["families"][family];
    for (const auto& id : f["model"]) v.models.push_back(id.get<int64_t>());
    for (auto it = f["preview"].begin(); it != f["preview"].end(); ++it)
        v.previews.emplace(std::stoll(it.key()), it.value().get<std::string>());
    v.current = current;
    v.hidden_off = hidden_off;
    v.build_key = cat["build_key"].get<std::string>();
    v.active = !v.models.empty() || !v.previews.empty();
    return v;
}

static const gear::Item* find(const std::vector<gear::Item>& items, int64_t id) {
    for (const auto& it : items)
        if (it.id == id) return &it;
    return nullptr;
}

static std::vector<int64_t> ids_of(const std::vector<gear::Item>& items) {
    std::vector<int64_t> out;
    for (const auto& it : items) out.push_back(it.id);
    return out;
}

int main(int argc, char** argv) {
    const std::string fixture = argc > 1 ? argv[1] : "tests/fixtures/asset_catalog_v1_small.json";
    std::ifstream in(fixture, std::ios::binary);
    if (!in) {
        std::printf("cannot open %s\n", fixture.c_str());
        return 2;
    }
    const nlohmann::json cat = nlohmann::json::parse(in);
    const auto t0 = std::chrono::steady_clock::now();

    // fields -> families
    CHECK(gear::family_of_field("hairtypecode") == assets::Family::Hair, "hair field");
    CHECK(gear::family_of_field("facialhairtypecode") == assets::Family::Facialhair, "facial hair field");
    CHECK(gear::family_of_field("shoetypecode") == assets::Family::Boots &&
              gear::family_of_field("smallsidedshoetypecode") == assets::Family::Boots,
          "boots and small-sided boots share the boots family");
    CHECK(gear::family_of_field("gkglovetypecode") == assets::Family::Gkglove, "gk gloves");
    CHECK(gear::family_of_field("tattooleftarm") == assets::Family::Tattoo &&
              gear::family_of_field("tattoohead") == assets::Family::Tattoo,
          "tattoo fields (arm sleeves = the arm tattoo fields)");
    CHECK(gear::family_of_field("tattooid") == assets::Family::Count && gear::family_of_field("accessorycode1") == assets::Family::Count &&
              gear::family_of_field("outfitid") == assets::Family::Count && gear::family_of_field("sockstylecode") == assets::Family::Count,
          "accessories, outfits, socks and the tattoo table id keep their 2.0 source");

    // no catalog = 2.0 behaviour: inactive view, the gate never refuses
    {
        const FamilyView none = gear::from_catalog(nullptr, assets::Family::Hair);
        CHECK(!none.active && gear::writable(none, {}, 9999) && gear::code_note(none, {}, 9999).empty(), "no catalog: inactive, no gate");
        assets::Catalog not_loaded;  // nothing loaded (catalog off / missing / error): same as no catalog
        CHECK(!gear::from_catalog(&not_loaded, assets::Family::Hair).active, "catalog not loaded: inactive");
        CHECK(!gear::from_catalog(&not_loaded, assets::Family::Count).active, "no family: inactive");
        FamilyView empty;
        empty.current = true;
        CHECK(!empty.active && gear::items(empty, {}).empty(), "an empty family stays inactive");
    }

    // hair: models 1-4, previews 1, 2 (Hair_2_0 letter case), 5 (preview only); the save uses 1
    const IdSet used{1};
    {
        const FamilyView v = view_of(cat, "hair", true);
        const auto items = gear::items(v, used);
        CHECK(ids_of(items) == (std::vector<int64_t>{1, 2, 3, 4, 5}), "ids = models + previews, sorted, no none");
        const gear::Item* i1 = find(items, 1);
        const gear::Item* i2 = find(items, 2);
        const gear::Item* i3 = find(items, 3);
        const gear::Item* i5 = find(items, 5);
        CHECK(i1 && i1->in_save && !i1->hidden && i1->writable && i1->preview == "data/ui/imgAssets/craniumhair/hair_1_0.dds",
              "used id: in the save, writable, its listed picture");
        CHECK(i2 && i2->hidden && i2->writable && i2->preview == "data/ui/imgAssets/craniumhair/Hair_2_0.dds",
              "hidden model id: writable on a current catalog, exact listed letter case");
        CHECK(i3 && i3->model && i3->preview.empty() && i3->hidden && i3->writable, "model-only id: listed (3D only placeholder), writable");
        CHECK(i5 && !i5->model && !i5->writable && i5->why.find("preview") != std::string::npos && i5->hidden,
              "preview-only id: listed greyed, never writable");
        std::string why;
        CHECK(!find(items, 99) && !gear::writable(v, used, 99, &why) && why.find("Not in the game files") != std::string::npos &&
                  why.find("0000AAAA-0000BBBB") != std::string::npos,
              "a csv id the game does not ship is gone and refused");
        CHECK(gear::writable(v, used, 0), "0 = none is always writable");
        CHECK(gear::combo_codes(v, used) == (std::vector<int64_t>{0, 1, 2, 3, 4}), "combo: none + writable ids (no preview-only)");
        CHECK(gear::code_note(v, used, 1).empty() && gear::code_note(v, used, 2) == "Hidden" &&
                  gear::code_note(v, used, 5) == "preview only" && gear::code_note(v, used, 99) == "not in the game files" &&
                  gear::code_note(v, used, 0).empty(),
              "combo notes");
        CHECK(gear::preload_paths(v, used).size() == 3, "preload: every listed preview");
        // scope filter (All / In this save / Hidden)
        size_t in_save = 0, hidden = 0;
        for (const auto& it : items) {
            in_save += assets::scope_shows(assets::Scope::InSave, it.hidden);
            hidden += assets::scope_shows(assets::Scope::Hidden, it.hidden);
        }
        CHECK(in_save == 1 && hidden == 4, "scope filter splits in-save and hidden ids");
    }

    // stale catalog: lists stay, hidden ids refused with the reason, DB-linked ids keep working
    {
        const FamilyView v = view_of(cat, "hair", false);
        const auto items = gear::items(v, used);
        CHECK(items.size() == 5, "stale: lists stay");
        const gear::Item* i2 = find(items, 2);
        CHECK(i2 && !i2->writable && i2->why.find("another game build") != std::string::npos, "stale: hidden id refused with the reason");
        CHECK(find(items, 1)->writable && gear::writable(v, used, 0), "stale: used ids and none still writable");
        CHECK(gear::combo_codes(v, used) == (std::vector<int64_t>{0, 1}), "stale: combo offers none + used ids only");
    }

    // asset_hidden_off.txt: hidden ids neither listed nor offered
    {
        const FamilyView v = view_of(cat, "hair", true, true);
        CHECK(ids_of(gear::items(v, used)) == (std::vector<int64_t>{1}), "hidden off: only ids the save uses");
        CHECK(gear::combo_codes(v, used) == (std::vector<int64_t>{0, 1}) && !gear::writable(v, used, 2),
              "hidden off: hidden ids not offered");
        CHECK(gear::preload_paths(v, used) == (std::vector<std::string>{"data/ui/imgAssets/craniumhair/hair_1_0.dds"}),
              "hidden off: only used pictures preloaded");
    }

    // boots: preview-only 13 greyed; a used preview-only id (the DB proves it) stays writable
    {
        const FamilyView v = view_of(cat, "boots", true);
        CHECK(!gear::writable(v, {}, 13) && gear::writable(v, {13}, 13), "boots: preview-only refused unless the save uses it");
        CHECK(ids_of(gear::items(v, {})) == (std::vector<int64_t>{10, 11, 12, 13}), "boots ids");
    }

    // tattoos: catalog models 30, 31 + the DB tattoo table (40, not in the catalog); arm sleeves use the same rows
    {
        const FamilyView v = view_of(cat, "tattoo", true);
        const IdSet table{30, 40};
        const auto items = gear::items(v, {}, &table, {40});
        CHECK(ids_of(items) == (std::vector<int64_t>{30, 31, 40}), "tattoos: catalog + table ids");
        const gear::Item* t31 = find(items, 31);
        const gear::Item* t40 = find(items, 40);
        CHECK(t31 && t31->hidden && !t31->known && t31->writable && t31->preview.empty(), "model-only tattoo not in the table: hidden, writable");
        CHECK(t40 && t40->known && !t40->hidden && t40->writable, "table tattoo the catalog does not list: kept (the DB knows it)");
        const FamilyView stale = view_of(cat, "tattoo", false, true);
        const auto off = gear::items(stale, {}, &table, {40});
        CHECK(ids_of(off) == (std::vector<int64_t>{30, 40}) && find(off, 30)->writable && find(off, 40)->writable,
              "tattoos, stale + hidden off: table tattoos stay listed and writable");
    }

    // a 50k-id family is listed fast (the galleries rebuild their rows per frame while open)
    {
        FamilyView big;
        big.active = big.current = true;
        IdSet bused;
        for (int64_t i = 1; i <= 50000; ++i) {
            big.models.push_back(i);
            if (i % 3 == 0) big.previews.emplace(i, "data/ui/imgAssets/x/item_" + std::to_string(i) + "_0.dds");
            if (i % 7 == 0) bused.insert(i);
        }
        const auto b0 = std::chrono::steady_clock::now();
        const auto items = gear::items(big, bused);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - b0).count();
        CHECK(items.size() == 50000, "50k ids listed");
        std::printf("  50k-id family listed in %.1f ms\n", ms);
    }

    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("gear ids: %d passed, %d failed (%.0f ms)\n", g_pass, g_fail, total);
    return g_fail == 0 ? 0 : 1;
}
