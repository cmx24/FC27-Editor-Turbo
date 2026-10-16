// Native tests (1.1.4): gear preview pictures (ui/ui_images.cpp GearPictureIndex) - which listed file a gallery shows
// for an item id. Synthetic paths shaped like Live Editor's legacy_filename_hash_list.csv. Included by test_main.cpp.
#pragma once
#include <memory>

#include <algorithm>
#include <vector>

#include "core/faces3d.h"
#include "core/gear_ids.h"
#include "core/hair_catalog.h"
#include "ui/ui_images.h"

static void test_gear_pictures() {
    run_case("gear pictures: file per id from the hash list (variants, colours, FC 27 shoe folder, outfits, none)", [&] {
        GearPictureIndex gx;
        CHECK(gx.empty() && gx.find("shoe", "shoe_", 1).empty(), "empty index finds nothing");
        for (const char* p : {"data/ui/imgAssets/shoe/shoe_192_0.dds", "data/ui/imgAssets/boots/item_1_0_0_0.dds",
                              "data/ui/imgAssets/boots/item_1_0_8_8.dds", "data/ui/imgAssets/boots/item_678_0.dds",
                              "data/ui/imgAssets/accessories/item_150_4.dds", "data/ui/imgAssets/accessories/item_150_3.dds",
                              "data/ui/imgAssets/accessories/item_52_0.dds", "data/ui/imgAssets/accessories/item_52_7.dds",
                              "data/ui/imgAssets/gkglove/gkglove_41.dds", "data/ui/imgAssets/outfit/item_6067.dds",
                              "data/ui/imgAssets/outfit/item_6001_0.dds", "data/ui/imgAssets/genericManagerOutfits/gmo_26540_2.dds",
                              "data/ui/imgAssets/tattoo/item__0.dds", "data/ui/imgAssets/tattoo/notfound.dds",
                              "data/ui/imgAssets/heads/sub/p1.dds", "data/ui/other/item_5_0.dds", "data/ui/imgAssets/kits/x.png"})
            gx.add(p);
        CHECK(gx.find("shoe", "shoe_", 192) == "data/ui/imgAssets/shoe/shoe_192_0.dds", "FC 27 boots: shoe/shoe_<id>_0");
        CHECK(gx.find("boots", "item_", 1) == "data/ui/imgAssets/boots/item_1_0_0_0.dds", "boots with long variants: first listed");
        CHECK(gx.find("boots", "item_", 192).empty() && gx.ids("boots", "item_") == (std::vector<int64_t>{1, 678}), "boots ids");
        CHECK(gx.find("accessories", "item_", 150) == "data/ui/imgAssets/accessories/item_150_3.dds", "no _0: lowest listed");
        CHECK(gx.find("accessories", "item_", 150, 4) == "data/ui/imgAssets/accessories/item_150_4.dds", "colour variant");
        CHECK(gx.find("accessories", "item_", 52, 9) == "data/ui/imgAssets/accessories/item_52_0.dds", "unlisted colour: _0");
        CHECK(gx.find("gkglove", "gkglove_", 41) == "data/ui/imgAssets/gkglove/gkglove_41.dds", "gloves without a variant");
        CHECK(gx.find("outfit", "item_", 6067) == "data/ui/imgAssets/outfit/item_6067.dds" &&
                  gx.find("outfit", "item_", 6001) == "data/ui/imgAssets/outfit/item_6001_0.dds",
              "manager outfits with and without _0");
        CHECK(gx.find("genericManagerOutfits", "gmo_", 26540) == "data/ui/imgAssets/genericManagerOutfits/gmo_26540_2.dds", "gmo");
        CHECK(gx.ids("tattoo", "item_").empty() && gx.files.count("heads/p") == 0 && gx.files.count("kits/x") == 0,
              "no id, notfound, sub folders, other roots and non-DDS ignored");
        gx.add("data/ui/imgAssets/craniumhair/Hair_12_0.dds");
        gx.add("data/ui/imgAssets/craniumhair/hair_12_1.dds");
        gx.add("data/ui/imgAssets/craniumhair/hair_13_2.dds");
        CHECK(gx.find("craniumhair", "hair_", 12) == "data/ui/imgAssets/craniumhair/Hair_12_0.dds" &&
                  gx.find("craniumhair", "hair_", 13) == "data/ui/imgAssets/craniumhair/hair_13_2.dds" &&
                  gx.ids("craniumhair", "HAIR_") == (std::vector<int64_t>{12, 13}),
              "FC 27 hair: any letter case, the name as listed");
        CHECK(gx.find("outfit", "gmo_", 6067).empty() && gx.find("nofolder", "item_", 1).empty(), "unknown folder / prefix: none");
    });
    // 2.10 (track F1): the faces3d picker shows the same game previews as Players > Appearance for the verified ids
    run_case("faces3d picker: verified hair/beard ids -> craniumhair / craniumfacialhair previews, length filter, text fallback", [&] {
        GearPictureIndex gx;
        const std::vector<int>& hair_ok = faces3d::ok_hair_ids();
        const std::vector<int>& beard_ok = faces3d::ok_beard_ids();
        for (int id : hair_ok)
            if (id != 5) gx.add("data/ui/imgAssets/craniumhair/hair_" + std::to_string(id) + "_0.dds");  // 5: the game has no preview
        for (int id : beard_ok) gx.add("data/ui/imgAssets/craniumfacialhair/Facial_hair_" + std::to_string(id) + "_0.dds");
        CHECK(!gx.empty() && std::binary_search(hair_ok.begin(), hair_ok.end(), 155) && std::binary_search(hair_ok.begin(), hair_ok.end(), 696) &&
                  std::binary_search(beard_ok.begin(), beard_ok.end(), 46),
              "hair 155 / 696 and beard 46 are verified ids");
        CHECK(gallery_pick_path(gx, "hairtypecode", 155) == "data/ui/imgAssets/craniumhair/hair_155_0.dds" &&
                  gallery_pick_path(gx, "hairtypecode", 696) == "data/ui/imgAssets/craniumhair/hair_696_0.dds" &&
                  gallery_pick_path(gx, "facialhairtypecode", 46) == "data/ui/imgAssets/craniumfacialhair/Facial_hair_46_0.dds",
              "listed previews: craniumhair/hair_<id>_0, craniumfacialhair/Facial_hair_<id>_0");
        CHECK(gallery_pick_path(gx, "hairtypecode", 5).empty() && gallery_pick_path(gx, "hairtypecode", 0).empty() &&
                  gallery_pick_path(gx, "facialhairtypecode", -3).empty() && gallery_pick_path(gx, "nosuchfield", 155).empty(),
              "no listed preview / none / negative id / unknown field: empty path (the cell shows the text label)");
        const GearPictureIndex none;  // no hash list at hand: the usual name, asked from the game
        CHECK(gallery_pick_path(none, "hairtypecode", 155) == "data/ui/imgAssets/craniumhair/hair_155_0.dds" &&
                  gallery_pick_path(none, "facialhairtypecode", 46) == "data/ui/imgAssets/craniumfacialhair/Facial_hair_46_0.dds",
              "without the list: the usual file names");
        const int len155 = hair::lookup(155).length;
        std::vector<int> want;
        for (int id : hair_ok)
            if (id > 0 && hair::lookup(id).length == len155) want.push_back(id);
        const std::vector<GalleryPick> by_len = gallery_pick_list(gx, "hairtypecode", hair_ok, len155);
        bool same = by_len.size() == want.size(), paths = true, has155 = false, has696 = false;
        for (size_t i = 0; same && i < by_len.size(); ++i) {
            same = by_len[i].id == want[i];
            paths = paths && by_len[i].path == gallery_pick_path(gx, "hairtypecode", by_len[i].id);
            has155 = has155 || by_len[i].id == 155;
            has696 = has696 || by_len[i].id == 696;
        }
        CHECK(same && paths && has155 && has696 == (hair::lookup(696).length == len155) && !by_len.empty() && by_len.size() < hair_ok.size(),
              "hair length filter: the verified ids of 155's length, in order, each with its path");
        const std::vector<GalleryPick> all = gallery_pick_list(gx, "hairtypecode", hair_ok);
        CHECK(all.size() == hair_ok.size() - (hair_ok.front() <= 0 ? 1 : 0), "no filter: every verified id (ids <= 0 dropped)");
        size_t no_pic = 0;
        for (const GalleryPick& p : all) no_pic += p.path.empty();
        CHECK(no_pic == 1, "id 5 (not listed) stays in the grid with an empty path: text label fallback");
        const std::vector<GalleryPick> beards = gallery_pick_list(gx, "facialhairtypecode", beard_ok, 2);  // the length applies to hair only
        CHECK(beards.size() == beard_ok.size() && std::any_of(beards.begin(), beards.end(), [](const GalleryPick& p) {
                  return p.id == 46 && p.path == "data/ui/imgAssets/craniumfacialhair/Facial_hair_46_0.dds";
              }),
              "beard list: every verified id, 46 with its facial-hair preview, no length filter");
    });
    // 2.1 (WP-08): the galleries read the game asset catalog through set_gear_catalog; a catalog that is not loaded (off,
    // missing, refused) leaves every gallery on its 2.0 source. The rules themselves: tests/catalog/run_gear.sh.
    run_case("gear pictures: game asset catalog not loaded = 2.0 galleries", [&] {
        auto none = std::make_shared<const turbo::assets::Catalog>();
        set_gear_catalog(none);
        CHECK(!gear::from_catalog(none.get(), assets::Family::Hair).active, "not loaded: inactive hair view");
        CHECK(gear::family_of_field("smallsidedshoetypecode") == assets::Family::Boots &&
                  gear::family_of_field("accessorycode1") == assets::Family::Accessory &&
                  !gear::from_catalog(none.get(), assets::Family::Accessory).active,
              "small-sided boots use the boots family; accessories read the catalog's accessory family, inactive (hash list) "
              "without a loaded catalog");
        set_gear_catalog(nullptr);
    });
}
