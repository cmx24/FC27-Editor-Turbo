// Native tests (1.1.4): gear preview pictures (ui/ui_images.cpp GearPictureIndex) - which listed file a gallery shows
// for an item id. Synthetic paths shaped like Live Editor's legacy_filename_hash_list.csv. Included by test_main.cpp.
#pragma once
#include <memory>

#include "core/gear_ids.h"
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
    // 2.1 (WP-08): the galleries read the game asset catalog through set_gear_catalog; a catalog that is not loaded (off,
    // missing, refused) leaves every gallery on its 2.0 source. The rules themselves: tests/catalog/run_gear.sh.
    run_case("gear pictures: game asset catalog not loaded = 2.0 galleries", [&] {
        auto none = std::make_shared<const turbo::assets::Catalog>();
        set_gear_catalog(none);
        CHECK(!gear::from_catalog(none.get(), assets::Family::Hair).active, "not loaded: inactive hair view");
        CHECK(gear::family_of_field("smallsidedshoetypecode") == assets::Family::Boots &&
                  gear::family_of_field("accessorycode1") == assets::Family::Count,
              "small-sided boots use the boots family, accessories keep the hash list");
        set_gear_catalog(nullptr);
    });
}
