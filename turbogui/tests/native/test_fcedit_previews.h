// Native tests (5.0c track E10-fcedit-previews, brief turbo_dev/PITWALL/briefs/5.0c/E10-12-hairbeard.md "Delivery round (07:50-08:35,
// E10-fcedit-previews)"): the hair / facial-hair pickers use a third picture source, the lit renders of Cassio's FC Editor install
// converted to PNG into <turbo_output>/cache/previews/fcedit27/<field>/<code>.png (ui_visual_picker.cpp build_source, ui_fcedit_previews.h).
//  - A tile with no game preview, no RDBM26 picture and no swatch takes that file (TileSource::Rdbm, note kFcEditPreviewNote, counted by
//    visual_picker_source_fcedit()); the game's preview still wins; a missing folder or file changes nothing (the grey tile + its reason);
//    a file deleted and the picker reopened is grey again.
//  - The fixture folder is the test app's own turbo_output (never the live cache).
// The test world's catalogue (tests/fixtures/asset_catalog_v1_small.json): hair models 1 2 3 4, hair previews 1 2 5; beard models 1 7,
// beard preview 7. So hair 2 (a preview) must keep the game's picture even with a cache file; hair 3 and 4 and beard 1 take the cache.
// Included by test_main.cpp after test_hairbeard_pickers.h; fcedit_test::ui_cases(app, ui, le) is called right after hairbeard_test::ui_cases.
#pragma once
#include <string>
#include <vector>

#include "core/asset_catalog.h"
#include "ui/ui_fcedit_previews.h"
#include "ui/ui_visual_picker.h"

namespace fcedit_test {

// A real 1x1 RGB PNG (69 bytes)
inline std::vector<uint8_t> one_pixel_png() {
    return {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x38, 0x31, 0x21, 0x00,
            0x00, 0x03, 0xCC, 0x01, 0xA9, 0x08, 0x72, 0x24, 0x23, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
}

inline void ui_cases(turbo::App& app, Ui& ui, const fs::path& le) {
    using namespace turbo;
    (void)le;
    const Table* p = app.db.table("players");
    const uint64_t rec = p ? app.db.find(*p, "playerid", 2002) : 0;
    auto draw_section = [&app, p, rec]() {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(900.0f, 700.0f), ImGuiCond_Always);
        if (ImGui::Begin("FC Editor previews test")) visual_pickers_section(app, *p, rec);
        ImGui::End();
    };
    const fs::path cache = app.turbo_output() / "cache" / "previews" / "fcedit27";  // the test app's own turbo_output
    auto put = [&](const char* field, const std::string& name) { visual_picker_test::write_bytes(cache / field / name, one_pixel_png()); };
    auto wipe = [&]() {
        std::error_code ec;
        fs::remove_all(cache, ec);
    };
    auto open = [&](const char* field) {
        app.request_tab = 0;
        ui.frames(2);
        visual_picker_request(field);
        ui.frames(3);
    };
    auto reopen = [&](const char* field) {
        visual_picker_close();
        ui.frames(2);
        open(field);
    };
    auto wind_down = [&]() {
        visual_picker_close();
        ui.frames(2);
        visual_pictures_override("");
        set_gear_catalog(nullptr);
        ui.extra_draw = nullptr;
        wipe();
        ui.frames(2);
    };
    auto catalogue_on = [&]() {
        auto cat = std::make_shared<assets::Catalog>();
        std::string cerr;
        CHECK(cat->load_file("tests/fixtures/asset_catalog_v1_small.json", "0000AAAA-0000BBBB", &cerr) && cat->current(), "catalog fixture loaded: " + cerr);
        set_gear_catalog(cat);
    };

    run_case("UI: 5.0c E10 fcedit previews (catalogue on): hair and beard tiles with no other picture take the cache; the game preview still wins", [&] {
        if (!p || !rec || !p->has("hairtypecode")) {
            CHECK(false, "the test world has hairtypecode");
            return;
        }
        catalogue_on();
        wipe();
        visual_pictures_override((test_tmp_dir("fcedit_on") / "nowhere").u8string());  // no RDBM26 folder of this PC in the way
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;

        // 1. no cache folder at all: nothing changes (the grey tile, its reason, a zero count)
        open("hairtypecode");
        CHECK(visual_picker_ui().open && visual_picker_ui().field == "hairtypecode", "the hair grid is open");
        CHECK(visual_picker_tile_source(3) == TileSource::Placeholder && visual_picker_tile_source(4) == TileSource::Placeholder,
              "no cache folder: hair 3 and 4 (a 3D model, no picture) are grey");
        CHECK(visual_picker_source_fcedit() == 0 && visual_picker_tile_note(3).empty(), "no cache folder: no FC Editor preview is counted");
        CHECK(visual_picker_tile_reason(3).find("3D model") != std::string::npos, "the grey reason is the old one: " + visual_picker_tile_reason(3));

        // 2. the cache has pictures for hair 2 (the game has a preview), 3 and 4; junk names are ignored
        put("hairtypecode", "2.png");
        put("hairtypecode", "3.png");
        put("hairtypecode", "4.png");
        put("hairtypecode", "notes.png");
        put("hairtypecode", "5.txt");
        reopen("hairtypecode");
        CHECK(visual_picker_tile_source(3) == TileSource::Rdbm && visual_picker_tile_source(4) == TileSource::Rdbm,
              "hair 3 and 4 take the cached picture (not Placeholder / NamesOnly)");
        CHECK(visual_picker_tile_source(2) == TileSource::Game, "hair 2: the game's own preview wins over the cache");
        CHECK(visual_picker_tile_source(5) == TileSource::Game, "hair 5: a game preview and no cache file is unchanged");
        CHECK(visual_picker_source_fcedit() == 2, "summary: source_fcedit == 2 (hair 3 and 4): " + std::to_string(visual_picker_source_fcedit()));
        CHECK(visual_picker_ui().source_rdbm >= 2, "the two tiles are counted as Rdbm too");
        CHECK(visual_picker_tile_note(3) == kFcEditPreviewNote && visual_picker_tile_note(4) == kFcEditPreviewNote, "the tile note is the FC Editor text");
        CHECK(visual_picker_tile_note(2).empty() && visual_picker_tile_note(5).empty(), "a game-preview tile has no FC Editor note");
        CHECK(visual_picker_tile_reason(3).empty(), "a tile with a picture has no grey reason");
        std::string why;
        CHECK(visual_picker_tile_writable(3, &why), "the tile still writes its code (the catalogue gate is unchanged): " + why);

        // 3. a file deleted, picker reopened: grey again
        std::error_code ec;
        fs::remove(cache / "hairtypecode" / "4.png", ec);
        reopen("hairtypecode");
        CHECK(visual_picker_tile_source(4) == TileSource::Placeholder && visual_picker_tile_source(3) == TileSource::Rdbm && visual_picker_source_fcedit() == 1,
              "hair 4's file deleted + reopened: grey again, hair 3 keeps its picture, count 1");
        visual_picker_close();
        ui.frames(2);

        // 4. facial hair: beard 7 has a game preview, beard 1 only a model
        if (p->has("facialhairtypecode")) {
            put("facialhairtypecode", "7.png");
            put("facialhairtypecode", "1.png");
            open("facialhairtypecode");
            CHECK(visual_picker_ui().open && visual_picker_ui().field == "facialhairtypecode", "the beard grid is open");
            CHECK(visual_picker_tile_source(1) == TileSource::Rdbm, "beard 1 (a model, no picture) takes the cached picture");
            CHECK(visual_picker_tile_source(7) == TileSource::Game, "beard 7: the game's preview wins");
            CHECK(visual_picker_source_fcedit() == 1 && visual_picker_tile_note(1) == kFcEditPreviewNote, "beard summary: source_fcedit == 1 with the FC Editor note");
        }
        wind_down();
    });

    run_case("UI: 5.0c E10 fcedit previews (catalogue off): an unlisted hair code takes the cache", [&] {
        if (!p || !rec || !p->has("hairtypecode")) return;
        set_gear_catalog(nullptr);
        wipe();
        visual_pictures_override((test_tmp_dir("fcedit_off") / "nowhere").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        int64_t b = 0;  // a named hair code that is not in the game's hash list: grey without the cache
        open("hairtypecode");
        for (int64_t c : visual_picker_codes())
            if (c > 5 && c != 3261 && c != 1053 && visual_picker_tile_source(c) == TileSource::Placeholder) {
                b = c;
                break;
            }
        CHECK(b > 0, "the hair list has a grey code to put a cached picture on");
        if (b > 0) {
            put("hairtypecode", std::to_string(b) + ".png");
            reopen("hairtypecode");
            CHECK(visual_picker_tile_source(b) == TileSource::Rdbm && visual_picker_tile_note(b) == kFcEditPreviewNote, "hair " + std::to_string(b) + " takes the cached picture");
            CHECK(visual_picker_source_fcedit() == 1, "source_fcedit == 1");
        }
        wind_down();
    });
}

}  // namespace fcedit_test
