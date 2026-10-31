// Native tests (Turbo X, FC Editor asset pictures: src/ui/ui_fcedit_pictures.h, wired into ui_visual_picker.cpp build_source and the
// Looks studio body card). FC Editor keeps a picture per code under <install>\assets\[<version>\]<field>\<id>.(png|jpg), body types as
// "<id> <name>.png". The resolver lists those folders once into an index (field -> id -> best path) in the order 27, 26, 25, 24, 17,
// then the plain field folder; png beats jpg in one folder.
//  - core_cases: a folder of the test's own with that layout: the order, the png / jpg tie, the body-type name match, junk ignored,
//    a missing folder = an empty index, id_of_name.
//  - ui_cases: with the resolver pointed at that folder, hair tiles with no game preview take the FC Editor picture (TileSource::FcEditor,
//    counted in VisualPickerUi::source_fcedit, tooltip word "FC Editor picture"); the game's preview still wins; a body-type tile with a
//    render is FcEditor, one without stays Figure; vp::has_picture turns on for a field the index has; the override lifted = nothing changes.
// Included by test_main.cpp after test_fcedit_previews.h; fcedit_pictures_test::core_cases() runs with the pure cases,
// fcedit_pictures_test::ui_cases(app, ui, le) right after fcedit_test::ui_cases. Nothing here touches a real FC Editor install.
#pragma once
#include <string>
#include <vector>

#include "core/asset_catalog.h"
#include "ui/ui_fcedit_pictures.h"
#include "ui/ui_visual_picker.h"

namespace fcedit_pictures_test {

// The fixture: <root>/assets with the layout of a real install (a 1x1 PNG in every file; the content is never decoded by the index)
inline fs::path make_fixture(const char* name) {
    using namespace turbo;
    const fs::path root = test_tmp_dir(name);
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::vector<uint8_t> png = fcedit_test::one_pixel_png();
    auto put = [&](const fs::path& rel) { visual_picker_test::write_bytes(root / "assets" / rel, png); };
    // hair: 3 in 27 and 25 and plain (27 wins); 4 in 25 only; 9 in plain only; 2 in 26 as jpg; 7 as jpg AND png in 27 (png wins)
    put(fs::path("27") / "hairtypecode" / "3.png");
    put(fs::path("25") / "hairtypecode" / "3.png");
    put(fs::path("hairtypecode") / "3.png");
    put(fs::path("25") / "hairtypecode" / "4.png");
    put(fs::path("hairtypecode") / "9.png");
    put(fs::path("26") / "hairtypecode" / "2.jpg");
    put(fs::path("27") / "hairtypecode" / "7.jpg");
    put(fs::path("27") / "hairtypecode" / "7.png");
    put(fs::path("27") / "hairtypecode" / "notes.png");  // junk: no id
    put(fs::path("27") / "hairtypecode" / "12abc.png");  // junk: not "<id>" nor "<id> <name>"
    put(fs::path("27") / "hairtypecode" / "5.txt");      // junk: not a picture
    // beards in 26 only; boots plain and 27; gloves plain
    put(fs::path("26") / "facialhairtypecode" / "1.png");
    put(fs::path("27") / "shoetypecode" / "40.png");
    put(fs::path("shoetypecode") / "41.jpg");
    put(fs::path("gkglovetypecode") / "5.png");
    // body types: "<id> <name>.png" in the plain folder
    put(fs::path("bodytypecode") / "1 Average and Lean.png");
    put(fs::path("bodytypecode") / "10 Lionel Messi.png");
    put(fs::path("bodytypecode") / "4.png");
    return root;
}

inline void core_cases() {
    using namespace turbo;
    run_case("fc editor pictures: id_of_name reads \"<id>\" and \"<id> <name>\", nothing else", [] {
        CHECK(fcedit::id_of_name("123") == 123, "123");
        CHECK(fcedit::id_of_name("10 Lionel Messi") == 10, "10 Lionel Messi");
        CHECK(fcedit::id_of_name("1 Average and Lean") == 1, "1 Average and Lean");
        CHECK(fcedit::id_of_name("notes") == -1, "notes");
        CHECK(fcedit::id_of_name("12abc") == -1, "12abc");
        CHECK(fcedit::id_of_name("") == -1, "empty");
        CHECK(fcedit::id_of_name("190030") == 190030, "an outfit id");
    });

    run_case("fc editor pictures: the index honours the order 27 > 26 > 25 > 24 > 17 > plain, png over jpg, body types by their leading number", [] {
        const fs::path root = make_fixture("fcedit_pictures_core");
        const fcedit::Index ix = fcedit::build_index(root);
        CHECK(ix.found, "the assets folder is found");
        CHECK(ix.fields.size() == 5, "five fields indexed (hair, beard, boots, gloves, body): " + std::to_string(ix.fields.size()));
        CHECK(ix.pictures == 5 + 1 + 2 + 1 + 3, "every id once: " + std::to_string(ix.pictures));
        auto ends_with = [](const std::string& s, const std::string& tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; };
        auto has_part = [](const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; };
        const std::string h3 = ix.picture("hairtypecode", 3);
        CHECK(has_part(h3, "27") && ends_with(h3, "3.png"), "hair 3: the 27 folder wins over 25 and plain: " + h3);
        const std::string h4 = ix.picture("hairtypecode", 4);
        CHECK(has_part(h4, "25") && ends_with(h4, "4.png"), "hair 4: 25 when 27 and 26 have none: " + h4);
        const std::string h9 = ix.picture("hairtypecode", 9);
        CHECK(!has_part(h9, "27") && !has_part(h9, "25") && ends_with(h9, "9.png"), "hair 9: the plain folder last: " + h9);
        CHECK(ends_with(ix.picture("hairtypecode", 2), "2.jpg"), "hair 2: a jpg counts");
        CHECK(ends_with(ix.picture("hairtypecode", 7), "7.png"), "hair 7: png beats jpg in one folder: " + ix.picture("hairtypecode", 7));
        CHECK(ix.picture("hairtypecode", 5).empty() && ix.picture("hairtypecode", 12).empty(), "junk names and a .txt are not pictures");
        CHECK(ix.picture("hairtypecode", 999).empty() && ix.picture("eyebrowcode", 3).empty(), "an unknown id or field: none");
        CHECK(ends_with(ix.picture("bodytypecode", 1), "1 Average and Lean.png"), "body 1 by its leading number: " + ix.picture("bodytypecode", 1));
        CHECK(ends_with(ix.picture("bodytypecode", 10), "10 Lionel Messi.png"), "body 10 (not body 1): " + ix.picture("bodytypecode", 10));
        CHECK(ends_with(ix.picture("bodytypecode", 4), "4.png"), "a plain \"<id>.png\" body picture");
        CHECK(ends_with(ix.picture("shoetypecode", 40), "40.png") && ends_with(ix.picture("shoetypecode", 41), "41.jpg"), "boots from 27 and plain");
        CHECK(ix.has("gkglovetypecode") && !ix.has("outfitid"), "has(): a field with pictures, not one without");
        CHECK(ix.ms >= 0.0 && ix.root == root.u8string(), "the index says where it came from and how long it took");
    });

    run_case("fc editor pictures: a missing folder is an empty index, not an error", [] {
        const fcedit::Index none = fcedit::build_index(test_tmp_dir("fcedit_pictures_core") / "nowhere");
        CHECK(!none.found && none.pictures == 0 && none.fields.empty(), "nothing found");
        const fcedit::Index empty = fcedit::build_index(fs::path());
        CHECK(!empty.found && empty.pictures == 0, "an empty root: nothing");
    });
}

inline void ui_cases(turbo::App& app, Ui& ui, const fs::path& le) {
    using namespace turbo;
    (void)le;
    const Table* p = app.db.table("players");
    const uint64_t rec = p ? app.db.find(*p, "playerid", 2002) : 0;
    auto draw_section = [&app, p, rec]() {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(900.0f, 700.0f), ImGuiCond_Always);
        if (ImGui::Begin("FC Editor pictures test")) visual_pickers_section(app, *p, rec);
        ImGui::End();
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
        fcedit::override_dir("");
        set_gear_catalog(nullptr);
        ui.extra_draw = nullptr;
        ui.frames(2);
    };

    run_case("UI: fc editor pictures fill the tiles with no other picture (hair, body type); the game's preview still wins; the row turns visual", [&] {
        if (!p || !rec || !p->has("hairtypecode")) {
            CHECK(false, "the test world has hairtypecode");
            return;
        }
        auto cat = std::make_shared<assets::Catalog>();
        std::string cerr;
        CHECK(cat->load_file("tests/fixtures/asset_catalog_v1_small.json", "0000AAAA-0000BBBB", &cerr) && cat->current(), "catalog fixture loaded: " + cerr);
        set_gear_catalog(cat);  // hair models 1 2 3 4, previews 1 2 5: hair 3 and 4 have no game picture
        visual_pictures_override((test_tmp_dir("fcedit_pictures_ui") / "nowhere").u8string());  // no RDBM26 folder of this PC in the way
        visual_pictures_refresh(app);
        std::error_code ec;
        fs::remove_all(app.turbo_output() / "cache" / "previews" / "fcedit27", ec);  // no converted previews either
        ui.extra_draw = draw_section;

        // 1. the resolver points nowhere: the tiles are as before
        fcedit::override_dir((test_tmp_dir("fcedit_pictures_ui") / "nowhere").u8string());
        open("hairtypecode");
        CHECK(visual_picker_ui().open && visual_picker_ui().field == "hairtypecode", "the hair grid is open");
        CHECK(visual_picker_tile_source(3) == TileSource::Placeholder && visual_picker_ui().source_fcedit == 0, "no FC Editor folder: hair 3 is grey, nothing counted");
        CHECK(!vp::has_picture(app, *p, "shoetypecode", true), "no FC Editor folder: a manager's boots have no picture source");

        // 2. the fixture install: hair 3 (27 folder) and 4 (25 folder) take the picture, hair 2 keeps the game's preview
        const fs::path root = make_fixture("fcedit_pictures_ui_install");
        fcedit::override_dir(root.u8string());
        reopen("hairtypecode");
        CHECK(visual_picker_tile_source(3) == TileSource::FcEditor && visual_picker_tile_source(4) == TileSource::FcEditor, "hair 3 and 4 are FC Editor pictures");
        CHECK(visual_picker_tile_source(2) == TileSource::Game && visual_picker_tile_source(5) == TileSource::Game, "hair 2 and 5: the game's own preview wins");
        CHECK(visual_picker_ui().source_fcedit == 2, "source_fcedit == 2: " + std::to_string(visual_picker_ui().source_fcedit));
        CHECK(visual_picker_tile_reason(3).empty() && visual_picker_tile_note(3).empty(), "a tile with an FC Editor picture has no grey reason and is not a converted preview");
        std::string why;
        CHECK(visual_picker_tile_writable(3, &why), "the catalogue gate is unchanged: " + why);
        CHECK(!visual_picker_ui().names_only, "a picture grid, not a names list");
        CHECK(vp::has_picture(app, *p, "shoetypecode", true) && vp::has_picture(app, *p, "gkglovetypecode", true), "a manager's boots and gloves rows now have a picture source");
        CHECK(!vp::has_picture(app, *p, "bodytypecode", true), "the body type keeps its own card");
        visual_picker_close();
        ui.frames(2);

        // 3. body types: a code with a render is an FC Editor tile (the figure is its fallback), one without stays a figure
        if (p->has("bodytypecode")) {
            open("bodytypecode");
            CHECK(visual_picker_ui().open && visual_picker_ui().field == "bodytypecode", "the body grid is open");
            const std::vector<int64_t> codes = visual_picker_codes();
            const bool has1 = std::find(codes.begin(), codes.end(), int64_t(1)) != codes.end();
            const bool has10 = std::find(codes.begin(), codes.end(), int64_t(10)) != codes.end();
            if (has1) CHECK(visual_picker_tile_source(1) == TileSource::FcEditor, "body 1 (\"1 Average and Lean.png\") is an FC Editor tile");
            if (has10) CHECK(visual_picker_tile_source(10) == TileSource::FcEditor, "body 10 (\"10 Lionel Messi.png\") is an FC Editor tile");
            int figures = 0, fce = 0;
            for (int64_t c : codes) {
                const TileSource ts = visual_picker_tile_source(c);
                figures += ts == TileSource::Figure;
                fce += ts == TileSource::FcEditor;
            }
            CHECK(fce >= 1 && figures >= 1 && figures + fce == static_cast<int>(codes.size()),
                  fmt("every body tile is a picture or a figure (%d FC Editor, %d figures of %zu)", fce, figures, codes.size()));
            CHECK(visual_picker_ui().source_fcedit == fce, "source_fcedit counts the body renders");
            visual_picker_close();
            ui.frames(2);
        }

        // 4. the override lifted: the setting (empty = the default folder, absent on this machine) is back
        fcedit::override_dir("");
        reopen("hairtypecode");
        CHECK(visual_picker_tile_source(3) == TileSource::Placeholder || visual_picker_tile_source(3) == TileSource::FcEditor,
              "back to the setting: hair 3 is grey (no install here) or the real install's picture");
        wind_down();
    });
}

}  // namespace fcedit_pictures_test
