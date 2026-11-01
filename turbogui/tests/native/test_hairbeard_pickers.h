// Native tests (5.0c track E10-12-hairbeard, brief turbo_dev/PITWALL/briefs/5.0c/E10-12-hairbeard.md, Verifier section):
// the hair and beard grids of the ONE visual picker (ui_visual_picker.cpp).
//
//  E11  the hair thumbnails were green / blue strand flow maps: 326 of the 788 files in RDBM26's Resources\hairstyle are PNG colour
//       type 3 (palette) flow maps, and build_source gave an RDBM26 file precedence over the game's own lit preview
//       (craniumhair\hair_<id>_0.dds, which the game ships for 325 of those 326). Now: a palette PNG of the hair / beard field never
//       beats a game preview; with no preview the tile is the grey one, saying why. A colour type 2 (lit) PNG, a short or unreadable
//       file and every other set (boots and gloves are palette pictures too) are unchanged.
//  E10  the grey beard tiles are catalogue ids with a 3D model and no picture anywhere ("3D only"): they stay grey (a lit picture
//       would need an in-game capture, the brief's D3) but every grey tile now carries its reason (visual_picker_tile_reason),
//       and the reason does not claim "no 3D model" for them.
//  E12  hair 0 is "Bald" in the list, writable on a generic head.
//  Turbo X bald: the real-face chooser (vp::open_choice) has tile 0 "Bald" / "No beard" AND "His scanned hair" / "His scanned beard"
//       (the own code); it writes nothing (the 3D section writes 9998 / 510 / the band code). The in-game render is not tested here.
//
// Both catalogue branches are tested (catalogue ON = Cassio's GUI: gallery_entries from the asset catalogue; catalogue OFF = the hash list).
// Included by test_main.cpp after test_visual_picker.h; hairbeard_test::ui_cases(app, ui, le) is called right after
// visual_picker_test::ui_cases.
#pragma once
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "core/asset_catalog.h"
#include "core/rdbm_resources.h"
#include "ui/ui_images.h"
#include "ui/ui_visual_picker.h"

namespace hairbeard_test {

namespace vis = turbo::visual;

// A PNG header that says colour type `colour_type` (3 = palette, 2 = RGB) at file offset 25, as RDBM26's files do: the 8-byte
// signature, the IHDR chunk (13 bytes: width, height, bit depth, colour type, compression, filter, interlace) and a CRC. No image data:
// the picker only reads the header to decide which picture wins, the worker's later decode of it fails quietly like the 4-byte stub's.
inline std::vector<uint8_t> png_header(int colour_type, int w = 256, int h = 256) {
    std::vector<uint8_t> b = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    for (int v : {w, h})
        for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>((v >> s) & 255));
    b.push_back(8);
    b.push_back(static_cast<uint8_t>(colour_type));
    b.push_back(0);
    b.push_back(0);
    b.push_back(0);
    for (int i = 0; i < 4; ++i) b.push_back(0);  // CRC (not checked)
    return b;
}

// The RDBM26 file of `code` in the set, written into <root>/Resources/<dir>/
inline fs::path put_picture(const fs::path& root, const char* set_id, int64_t code, const std::vector<uint8_t>& bytes) {
    const vis::SetDef* s = vis::find_set(set_id);
    const vis::CodeDef* c = s ? vis::find_code(*s, code) : nullptr;
    if (!s || !c || !c->file) return {};
    std::error_code ec;
    fs::create_directories(root / "Resources" / "hairstyle", ec);  // the folder only counts as RDBM26's when it holds a picture set (rdbm_resources.cpp holds_sets)
    const fs::path p = root / "Resources" / s->dir / c->file;
    visual_picker_test::write_bytes(p, bytes);
    return p;
}

inline bool has_word(const std::string& text, const char* word) {
    std::string a = text, b = word;
    for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a.find(b) != std::string::npos;
}

inline void ui_cases(turbo::App& app, Ui& ui, const fs::path& le) {
    using namespace turbo;
    const Table* p = app.db.table("players");
    const uint64_t rec = p ? app.db.find(*p, "playerid", 2002) : 0;
    auto draw_section = [&app, p, rec]() {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(900.0f, 700.0f), ImGuiCond_Always);
        if (ImGui::Begin("Hair beard pickers test")) visual_pickers_section(app, *p, rec);
        ImGui::End();
    };
    auto reason_of = [](int64_t code) { return visual_picker_tile_reason(code); };
    // T4 for every picker the cases open: no grey tile is left without its reason
    auto every_grey_tile_has_a_reason = [&](const char* what) {
        int grey = 0, bare = 0;
        std::string first;
        for (int64_t c : visual_picker_codes()) {
            if (visual_picker_tile_source(c) != TileSource::Placeholder) continue;
            ++grey;
            if (reason_of(c).empty()) {
                ++bare;
                if (first.empty()) first = std::to_string(c);
            }
        }
        CHECK(grey > 0, std::string(what) + ": the list has grey tiles to check (" + std::to_string(grey) + ")");
        CHECK(bare == 0, std::string(what) + ": every grey tile has a reason (" + std::to_string(bare) + " without, first code " + first + ")");
    };
    auto catalogue_on = [&]() {
        auto cat = std::make_shared<assets::Catalog>();
        std::string cerr;
        CHECK(cat->load_file("tests/fixtures/asset_catalog_v1_small.json", "0000AAAA-0000BBBB", &cerr) && cat->current(), "catalog fixture loaded: " + cerr);
        set_gear_catalog(cat);
    };
    auto wind_down = [&]() {
        visual_picker_close();
        ui.frames(2);
        visual_pictures_override("");
        set_gear_catalog(nullptr);
        ui.extra_draw = nullptr;
        ui.frames(2);
    };

    // ---------------------------------------------------------------- catalogue ON (Cassio's GUI)
    // fixture catalogue (tests/fixtures/asset_catalog_v1_small.json): hair models 1 2 3 4, hair previews 1 2 5 (craniumhair\hair_<id>_0.dds);
    // beard models 1 7, beard preview 7
    run_case("UI: 5.0c E11 hair grid (catalogue on): a palette strand-map PNG never beats the game's lit preview; lit PNGs stay", [&] {
        if (!p || !rec || !p->has("hairtypecode")) {
            CHECK(false, "the test world has hairtypecode");
            return;
        }
        catalogue_on();
        const fs::path root = test_tmp_dir("hb_hair_on");
        const fs::path pal1 = put_picture(root, "hairstyle", 1, png_header(3));   // palette, the game has a preview + model: the preview
        put_picture(root, "hairstyle", 2, png_header(2));                         // lit RGB, preview too: the game preview wins (brief D1, review E10-12)
        put_picture(root, "hairstyle", 0, png_header(2));                         // Bald, lit RGB, no game lookup for code 0: the RDBM26 picture stays
        put_picture(root, "hairstyle", 4, png_header(3));                         // palette, model only (no preview): grey, "strand map"
        CHECK(!pal1.empty(), "hairstyle codes 1 / 2 / 4 have RDBM26 file names");
        visual_pictures_override((root / "Resources").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        app.request_tab = 0;
        ui.frames(2);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && visual_picker_ui().field == "hairtypecode", "the hair grid is open");
        CHECK(visual_picker_tile_source(1) == TileSource::Game, "hair 1: palette strand map + the game's lit preview -> the game preview (E11)");
        CHECK(visual_picker_tile_source(2) == TileSource::Game, "hair 2: a lit RDBM26 picture AND a game preview -> the game preview (brief D1: game first for hair)");
        CHECK(visual_picker_tile_source(0) == TileSource::Rdbm, "hair 0 (Bald): its lit RDBM26 picture is kept (no game preview is looked up for 0)");
        CHECK(visual_picker_tile_source(4) == TileSource::Placeholder, "hair 4: palette strand map and NO lit preview -> the grey tile, not the strand map");
        const std::string r4 = reason_of(4);
        CHECK(has_word(r4, "strand map"), "hair 4 says why it is grey: " + r4);
        CHECK(visual_picker_tile_source(3) == TileSource::Placeholder, "hair 3: a 3D model, no picture anywhere -> grey");
        const std::string r3 = reason_of(3);
        CHECK(has_word(r3, "3D model") && !has_word(r3, "no 3D model"), "hair 3: the reason names the 3D model the game ships and does not deny it: " + r3);
        CHECK(visual_picker_tile_source(5) == TileSource::Game, "hair 5: a preview, no RDBM26 file -> the game preview (unchanged)");
        std::string why;
        CHECK(visual_picker_tile_writable(4, &why), "a grey palette-fallback tile still writes its code (hair 4 has a model): " + why);
        const VisualPickerUi& vu = visual_picker_ui();
        CHECK(vu.source_game >= 3 && vu.source_rdbm >= 1, fmt("one grid, game previews and RDBM26 pictures together (rdbm %d, game %d, grey %d)", vu.source_rdbm, vu.source_game, vu.source_blank));
        every_grey_tile_has_a_reason("hair grid, catalogue on");
        // a code the catalogue does not list at all
        int64_t unlisted = 0;
        for (int64_t c : visual_picker_codes())
            if (c > 5 && visual_picker_tile_source(c) == TileSource::Placeholder) {
                unlisted = c;
                break;
            }
        CHECK(unlisted > 0 && !reason_of(unlisted).empty(), "a named hair code the game files do not list: grey with a reason: " + reason_of(unlisted));
        wind_down();
    });

    run_case("UI: 5.0c E12 hair 0 is Bald: in the list, named, writable on a generic head (regression guard)", [&] {
        if (!p || !rec || !p->has("hairtypecode")) return;
        catalogue_on();
        const vis::SetDef* hs = vis::find_set("hairstyle");
        const vis::CodeDef* z = hs ? vis::find_code(*hs, 0) : nullptr;
        CHECK(z && z->name && std::string(z->name) == "Bald", std::string("the name table says hair 0 is Bald: ") + (z && z->name ? z->name : "(none)"));
        visual_pictures_override((test_tmp_dir("hb_bald_none") / "nowhere").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        app.request_tab = 0;
        ui.frames(2);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        bool listed = false;
        for (int64_t c : visual_picker_codes()) listed = listed || c == 0;
        CHECK(listed, "code 0 is a tile of the hair grid");
        CHECK(visual_picker_tile_writable(0), "hair 0 can be applied");
        const int64_t was = app.db.get_int(*p, rec, "hairtypecode", -1);
        visual_picker_select(0);
        CHECK(visual_picker_apply(app) && app.db.get_int(*p, rec, "hairtypecode", -1) == 0, "Apply writes hair 0 (read back): " + visual_picker_ui().last_result);
        visual_apply(app, *p, rec, "hairtypecode", was);
        wind_down();
    });

    run_case("UI: Turbo X bald: the real-face chooser offers Bald / No beard (0) AND his scanned hair / beard (own code); nothing is written", [&] {
        if (!p || !rec || !p->has("hairtypecode")) return;
        catalogue_on();
        visual_pictures_override((test_tmp_dir("hb_choice_bald") / "nowhere").u8string());
        visual_pictures_refresh(app);
        const int64_t was = app.db.get_int(*p, rec, "hairtypecode", -1);
        vp::open_choice(app, *p, rec, "hairtypecode", false, {155, 250}, faces3d::kHairOwnCode);
        const std::vector<int64_t> codes = visual_picker_codes();
        CHECK(codes.size() >= 2 && codes[0] == 0 && codes[1] == faces3d::kHairOwnCode, "hair chooser: Bald first, then his scanned hair");
        CHECK(visual_picker_tile_name(0) == "Bald" && visual_picker_tile_name(faces3d::kHairOwnCode) == "His scanned hair",
              "hair tiles named exactly: " + visual_picker_tile_name(0) + " / " + visual_picker_tile_name(faces3d::kHairOwnCode));
        bool only = true;
        for (int64_t c : codes) only = only && (c == 0 || c == faces3d::kHairOwnCode || c == 155 || c == 250);
        CHECK(only, "only the band ids besides the two own tiles");
        CHECK(visual_picker_ui().choice && visual_picker_ui().current == faces3d::kHairOwnCode, "opened as the chooser on his scanned hair");
        visual_picker_select(0);
        int64_t got = -1;
        CHECK(visual_picker_apply(app) && vp::take_choice("hairtypecode", &got) && got == 0, "Bald hands back tile 0 (the section writes 9998)");
        visual_picker_select(faces3d::kHairOwnCode);
        CHECK(visual_picker_apply(app) && vp::take_choice("hairtypecode", &got) && got == faces3d::kHairOwnCode, "his scanned hair hands back the own code");
        CHECK(app.db.get_int(*p, rec, "hairtypecode", -1) == was, "the chooser writes nothing");
        visual_picker_close();
        if (p->has("facialhairtypecode")) {
            vp::open_choice(app, *p, rec, "facialhairtypecode", false, {46, 47}, 0);
            CHECK(visual_picker_tile_name(0) == "No beard" && visual_picker_tile_name(faces3d::kBeardOwnCode) == "His scanned beard",
                  "beard tiles named exactly: " + visual_picker_tile_name(0) + " / " + visual_picker_tile_name(faces3d::kBeardOwnCode));
            CHECK(visual_picker_ui().current == 0, "opened on No beard");
            every_grey_tile_has_a_reason("real-face beard chooser");
        }
        wind_down();
    });

    run_case("UI: 5.0c E10 beard grid (catalogue on): palette beard PNG yields to the preview; grey tiles say why", [&] {
        if (!p || !rec || !p->has("facialhairtypecode")) {
            CHECK(true, "the test world has no facialhairtypecode: the beard cases are skipped");
            return;
        }
        // the small fixture lists beard ids 1 and 7, which RDBM26's beard names do not carry: a copy of it that lists RDBM26 ids
        // 27 .. 31 (27 has a preview and a model, 28 a model, 29 a model, 30 a model and no file; 31 nothing)
        std::string cat_text = read_file("tests/fixtures/asset_catalog_v1_small.json");
        const std::string old_line = "\"facialhair\": { \"model\": [1, 7], \"preview\": { \"7\": \"data/ui/imgAssets/facialhair/facialhair_7_0.dds\" } },";
        const size_t at = cat_text.find(old_line);
        CHECK(at != std::string::npos, "the fixture's beard line is where the test expects it");
        if (at == std::string::npos) return;
        cat_text.replace(at, old_line.size(),
                         "\"facialhair\": { \"model\": [1, 7, 27, 28, 29, 30], \"preview\": { \"27\": \"data/ui/imgAssets/facialhair/facialhair_27_0.dds\" } },");
        const fs::path root = test_tmp_dir("hb_beard_on");
        const fs::path catfile = root / "asset_catalog_beard.json";
        visual_picker_test::write_bytes(catfile, std::vector<uint8_t>(cat_text.begin(), cat_text.end()));
        auto cat = std::make_shared<assets::Catalog>();
        std::string cerr;
        CHECK(cat->load_file(catfile.u8string(), "0000AAAA-0000BBBB", &cerr) && cat->current(), "beard catalog loaded: " + cerr);
        set_gear_catalog(cat);
        put_picture(root, "facialhair", 27, png_header(3));  // palette + the game's preview -> the game preview
        put_picture(root, "facialhair", 28, png_header(2));  // lit RGB -> the RDBM26 picture stays
        put_picture(root, "facialhair", 29, png_header(3));  // palette, a model and no preview -> grey, "strand map"
        visual_pictures_override((root / "Resources").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        app.request_tab = 0;
        ui.frames(2);
        visual_picker_request("facialhairtypecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && visual_picker_ui().field == "facialhairtypecode", "the beard grid is open");
        CHECK(visual_picker_tile_source(27) == TileSource::Game, "beard 27: palette PNG + game preview -> the game preview");
        CHECK(visual_picker_tile_source(28) == TileSource::Rdbm, "beard 28: a lit RDBM26 picture is kept");
        CHECK(visual_picker_tile_source(29) == TileSource::Placeholder && has_word(reason_of(29), "strand map"),
              "beard 29: palette and no preview -> grey, saying strand map: " + reason_of(29));
        CHECK(visual_picker_tile_source(30) == TileSource::Placeholder, "beard 30: a model, no picture -> grey");
        const std::string r30 = reason_of(30);
        CHECK(has_word(r30, "3D model") && !has_word(r30, "no 3D model"), "beard 30: the reason names the 3D model and does not deny it: " + r30);
        CHECK(visual_picker_tile_source(31) == TileSource::Placeholder && !reason_of(31).empty(), "beard 31: not in the catalogue -> grey with a reason: " + reason_of(31));
        CHECK(visual_picker_tile_writable(29) && visual_picker_tile_writable(30), "grey beard tiles with a model still write their code");
        every_grey_tile_has_a_reason("beard grid, catalogue on");
        wind_down();
    });

    // ---------------------------------------------------------------- 5.0c E10 lit capture (brief D3): Turbo's own capture is the last source before the drawn icon
    run_case("UI: 5.0c E10 lit capture: a hair_<id>.png in cache/hairbeard_thumbs is the tile of a model-only hair; the game preview still wins", [&] {
        if (!p || !rec || !p->has("hairtypecode")) return;
        catalogue_on();
        const fs::path root = test_tmp_dir("hb_lit_capture");
        visual_pictures_override((root / "Resources").u8string());
        const fs::path cache = app.turbo_output() / "cache" / "hairbeard_thumbs";
        std::error_code ec;
        fs::create_directories(cache, ec);
        auto put = [&](const char* name) {
            std::ofstream f(cache / name, std::ios::binary | std::ios::trunc);
            const std::vector<uint8_t> h = png_header(6);
            f.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size()));
        };
        put("hair_3.png");  // hair 3: a 3D model, no picture anywhere (the drawn icon until now)
        put("hair_1.png");  // hair 1: the game has a lit preview: it stays the tile
        // The picker reads the folder through hairbeard_thumbs::thumb(), whose file list is kept up to 5 s. An earlier case (the beard grid just
        // before this one) scanned the folder while it was still empty, so without this the new PNGs stay unseen and hair 3 is grey (the
        // merged-tree failure of 5.0c integration p2r1, test_hairbeard_pickers.h:251). The production writers call it the same way (hairbeard_thumbs.cpp:289/321).
        hairbeard_thumbs::invalidate();
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        app.request_tab = 0;
        ui.frames(2);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && visual_picker_ui().field == "hairtypecode", "the hair grid is open");
        CHECK(visual_picker_tile_source(3) == TileSource::Rdbm, "hair 3 (model only): Turbo's lit capture is its tile, not the drawn icon");
        CHECK(visual_picker_tile_source(1) == TileSource::Game, "hair 1: the game's preview still beats Turbo's capture");
        CHECK(visual_picker_tile_source(5) == TileSource::Game, "hair 5: unchanged (game preview)");
        wind_down();
        fs::remove(cache / "hair_3.png", ec);
        fs::remove(cache / "hair_1.png", ec);
        hairbeard_thumbs::invalidate();  // later cases must not see the two files that are gone
    });

    // ---------------------------------------------------------------- the probe is remembered per folder, and forgotten when the folder is looked at again
    run_case("UI: 5.0c E11 the palette probe follows the file: rewritten as a lit picture and refreshed, the RDBM26 picture wins again", [&] {
        if (!p || !rec || !p->has("hairtypecode")) return;
        catalogue_on();
        const fs::path root = test_tmp_dir("hb_probe_refresh");
        put_picture(root, "hairstyle", 4, png_header(3));  // hair 4: a model, no game preview (so it is probed)
        // a 0-byte file and a 4-byte stub are not palette pictures: unreadable files keep today's behaviour
        put_picture(root, "hairstyle", 3, {});
        visual_pictures_override((root / "Resources").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        app.request_tab = 0;
        ui.frames(2);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        CHECK(visual_picker_tile_source(4) == TileSource::Placeholder, "palette, no preview: the grey tile");
        CHECK(visual_picker_tile_source(3) == TileSource::Rdbm, "a 0-byte file is not a palette picture: the RDBM26 file as before");
        visual_picker_close();
        ui.frames(2);
        put_picture(root, "hairstyle", 4, png_header(2));
        visual_pictures_refresh(app);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        CHECK(visual_picker_tile_source(4) == TileSource::Rdbm, "rewritten as a lit picture + folder looked at again: the RDBM26 picture");
        wind_down();
    });

    // ---------------------------------------------------------------- catalogue OFF (release users: the game's hash list)
    run_case("UI: 5.0c E11 hair grid (catalogue off, hash list): a palette PNG yields to the listed preview; unlisted -> grey with its reason", [&] {
        if (!p || !rec || !p->has("hairtypecode")) return;
        set_gear_catalog(nullptr);
        const vis::SetDef* hs = vis::find_set("hairstyle");
        CHECK(hs != nullptr, "the hair names set");
        if (!hs) return;
        int64_t a = 0, b = 0;  // two named hair codes: a is in the hash list, b is not
        const vis::CodeDef* cb = vis::codes_begin(*hs);
        for (int i = 0; i < hs->count && !(a && b); ++i) {
            const int64_t c = cb[i].code;
            if (c <= 0 || c == 3261 || c == 1053) continue;
            (a ? b : a) = c;
        }
        const fs::path hash = le / "legacy_filename_hash_list.csv";
        const std::string hash_before = fs::exists(hash) ? read_file(hash) : std::string();
        {
            std::ofstream h(hash);
            h << "hash;file_name\n1;data/ui/imgAssets/hairstyle/item_" << a << "_0.dds\n";
        }
        app.request_tab = 0;
        ui.frames(2);
        const fs::path root = test_tmp_dir("hb_hair_off");
        put_picture(root, "hairstyle", a, png_header(3));
        put_picture(root, "hairstyle", b, png_header(3));
        visual_pictures_override((root / "Resources").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        ui.frames(2);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && !visual_picker_ui().names_only, "the hair grid is open with pictures");
        CHECK(visual_picker_tile_source(a) == TileSource::Game, "a listed hair code with a palette PNG: the game preview");
        CHECK(visual_picker_tile_source(b) == TileSource::Placeholder, "an unlisted hair code with a palette PNG: grey, not the strand map");
        CHECK(has_word(reason_of(b), "strand map"), "... and it says why: " + reason_of(b));
        every_grey_tile_has_a_reason("hair grid, catalogue off");
        visual_picker_close();
        ui.frames(2);
        if (hash_before.empty()) {
            std::error_code ec;
            fs::remove(hash, ec);
        } else {
            std::ofstream(hash, std::ios::binary | std::ios::trunc) << hash_before;
        }
        app.request_tab = 0;
        ui.frames(2);
        wind_down();
    });

    // ---------------------------------------------------------------- the cost of the probe on the first open (one 26-byte read per hair file, 788 of them in RDBM26)
    run_case("UI: 5.0c E11 PERF: the first open of a hair grid with 788 palette files (every hair file probed once), the second open reads none", [&] {
        if (!p || !rec || !p->has("hairtypecode")) return;
        set_gear_catalog(nullptr);
        const vis::SetDef* hs = vis::find_set("hairstyle");
        CHECK(hs != nullptr, "the hair names set");
        if (!hs) return;
        const fs::path root = test_tmp_dir("hb_perf");
        int written = 0;
        const vis::CodeDef* cb = vis::codes_begin(*hs);
        for (int i = 0; i < hs->count; ++i)
            if (cb[i].file) {
                visual_picker_test::write_bytes(root / "Resources" / hs->dir / cb[i].file, png_header(3));
                ++written;
            }
        visual_pictures_override((root / "Resources").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        app.request_tab = 0;
        ui.frames(2);
        auto ms_of = [](std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
        auto t0 = std::chrono::steady_clock::now();
        visual_picker_request("hairtypecode");
        ui.frame();
        ui.frame();
        const double first = ms_of(t0);
        CHECK(visual_picker_ui().open, "the hair grid opened");
        visual_picker_close();
        ui.frames(2);
        t0 = std::chrono::steady_clock::now();
        visual_picker_request("hairtypecode");
        ui.frame();
        ui.frame();
        const double second = ms_of(t0);
        std::printf("  PERF hair grid with %d palette files: first open (2 frames) %.1f ms, second open %.1f ms\n", written, first, second);
        CHECK(first < 400.0, fmt("opening the hair grid with every file probed stays far below a second (%.1f ms)", first));
        // review E10-12 (brief D1, the 16 ms rule of test_visual_picker.h): a hair code the game has a preview for is never probed.
        // Every named hair code listed in the hash list: the first open reads at most code 0's header (788 reads on bd051b23, ~30 ms).
        {
            visual_picker_close();
            ui.frames(2);
            const fs::path hash = le / "legacy_filename_hash_list.csv";
            const std::string hash_before = fs::exists(hash) ? read_file(hash) : std::string();
            {
                std::ofstream h(hash);
                h << "hash;file_name\n";
                for (int i = 0; i < hs->count; ++i)
                    if (cb[i].code > 0) h << (i + 1) << ";data/ui/imgAssets/hairstyle/item_" << cb[i].code << "_0.dds\n";
            }
            // the game's lists are looked at once a second of ImGui time at most (ui_images.cpp gear_pictures): 65 frames at 1/60 s
            // make the re-read certain (with 2 frames the index was stale in the full 5.0c run and the build saw no preview)
            app.request_tab = 0;
            ui.frames(65);
            const fs::path root2 = test_tmp_dir("hb_perf_previewed");
            for (int i = 0; i < hs->count; ++i)
                if (cb[i].file) visual_picker_test::write_bytes(root2 / "Resources" / hs->dir / cb[i].file, png_header(3));
            visual_pictures_override((root2 / "Resources").u8string());
            visual_pictures_refresh(app);
            ui.extra_draw = draw_section;
            ui.frames(2);
            const size_t probes0 = visual_picker_png_probes();
            visual_picker_request("hairtypecode");
            ui.frames(3);
            const size_t probes = visual_picker_png_probes() - probes0;
            int64_t listed = 0;
            for (int i = 0; i < hs->count && !listed; ++i)
                if (cb[i].code > 0 && cb[i].file) listed = cb[i].code;
            CHECK(visual_picker_ui().open && visual_picker_tile_source(listed) == TileSource::Game, fmt("a listed hair code shows the game preview (%lld)", static_cast<long long>(listed)));
            CHECK(probes <= 1, fmt("hair codes with a game preview are not probed: %zu PNG headers read on the first open (at most code 0's)", probes));
            visual_picker_close();
            ui.frames(2);
            if (hash_before.empty()) {
                std::error_code ec;
                fs::remove(hash, ec);
            } else {
                std::ofstream(hash, std::ios::binary | std::ios::trunc) << hash_before;
            }
            app.request_tab = 0;
            ui.frames(65);  // the restored list is re-read before the next case (once a second, see above)
        }
        wind_down();
    });

    // ---------------------------------------------------------------- the other sets are untouched: boots and gloves are palette pictures in RDBM26 (364 + 95 files)
    run_case("UI: 5.0c the palette rule is for hair and beard only: a palette boots picture is still its RDBM26 picture", [&] {
        if (!p || !rec || !p->has("shoetypecode")) return;
        const vis::SetDef* bs = vis::find_set("boots");
        const vis::CodeDef* bc = nullptr;
        if (bs) {
            const vis::CodeDef* cb = vis::codes_begin(*bs);
            for (int i = 0; i < bs->count && !bc; ++i)
                if (cb[i].code > 0 && cb[i].file) bc = &cb[i];
        }
        if (!bc) {
            CHECK(true, "no boots names in this build: skipped");
            return;
        }
        set_gear_catalog(nullptr);
        const fs::path root = test_tmp_dir("hb_boots");
        std::error_code mk;
        fs::create_directories(root / "Resources" / "hairstyle", mk);
        visual_picker_test::write_bytes(root / "Resources" / bs->dir / bc->file, png_header(3));
        visual_pictures_override((root / "Resources").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        app.request_tab = 0;
        ui.frames(2);
        visual_picker_request("shoetypecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && visual_picker_tile_source(bc->code) == TileSource::Rdbm, "a palette boots picture stays the RDBM26 picture");
        wind_down();
    });
    // ---------------------------------------------------------------- fix round (2026-10-31): every hair and beard tile has a picture
    // The grey tiles stay TileSource::Placeholder (no game picture) but are DRAWN from their look class (visual_picker_tile_drawn), so no hair
    // or beard tile is left as a bare "no picture yet" box; a lit render of the model-only ids needs an in-game capture lease (brief D3).
    auto every_tile_has_a_picture = [&](const char* what, int need_drawn) {
        int bare = 0, drawn = 0, total = 0;
        std::string first;
        for (int64_t c : visual_picker_codes()) {
            ++total;
            const TileSource s = visual_picker_tile_source(c);
            const bool d = visual_picker_tile_drawn(c);
            drawn += d;
            if (s == TileSource::Placeholder && !d) {
                ++bare;
                if (first.empty()) first = std::to_string(c);
            }
        }
        CHECK(total > 0, std::string(what) + ": the grid has tiles");
        CHECK(drawn >= need_drawn, std::string(what) + ": the tiles with no game picture are drawn (" + std::to_string(drawn) + ", need " + std::to_string(need_drawn) + ")");
        CHECK(bare == 0, std::string(what) + ": no tile is left as a bare no-picture box (" + std::to_string(bare) + " bare, first code " + first + ")");
    };

    run_case("UI: 5.0c E10 fix round: hair grid (catalogue on, no pictures folder): every tile has a picture; a model-only id is a drawn class icon", [&] {
        if (!p || !rec || !p->has("hairtypecode")) return;
        catalogue_on();
        visual_pictures_override((test_tmp_dir("hb_fix_hair_on") / "nowhere").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        app.request_tab = 0;
        ui.frames(2);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && visual_picker_ui().field == "hairtypecode", "the hair grid is open");
        // the fixture lists hair models 1 2 3 4 and previews 1 2 5: 3 and 4 are model-only
        CHECK(visual_picker_tile_source(3) == TileSource::Placeholder && visual_picker_tile_drawn(3), "hair 3 (3D model, no picture): drawn from its class");
        CHECK(visual_picker_tile_source(4) == TileSource::Placeholder && visual_picker_tile_drawn(4), "hair 4 (3D model, no picture): drawn from its class");
        CHECK(visual_picker_tile_source(1) == TileSource::Game && !visual_picker_tile_drawn(1), "hair 1 (a game preview): its preview, never replaced by an icon");
        CHECK(visual_picker_tile_drawn(0), "hair 0 (bald): a drawn bald head");
        CHECK(visual_picker_tile_icon(0) != visual_picker_tile_icon(4), "bald and hair 4 are different icons");
        every_tile_has_a_picture("hair grid, catalogue on", 3);
        every_grey_tile_has_a_reason("hair grid, catalogue on, drawn");
        CHECK(has_word(reason_of(3), "drawn icon"), "the tooltip says the tile is a drawn icon, not a picture of the model: " + reason_of(3));
        CHECK(visual_picker_ui().source_drawn >= 3, "the opening log counts the drawn tiles: " + std::to_string(visual_picker_ui().source_drawn));
        wind_down();
    });

    run_case("UI: 5.0c E10 fix round: beard grid (catalogue on): the 3D-only beards and code 0 are drawn; classes differ; previews stay", [&] {
        if (!p || !rec || !p->has("facialhairtypecode")) {
            CHECK(true, "the test world has no facialhairtypecode: skipped");
            return;
        }
        std::string cat_text = read_file("tests/fixtures/asset_catalog_v1_small.json");
        const std::string old_line = "\"facialhair\": { \"model\": [1, 7], \"preview\": { \"7\": \"data/ui/imgAssets/facialhair/facialhair_7_0.dds\" } },";
        const size_t at = cat_text.find(old_line);
        CHECK(at != std::string::npos, "the fixture's beard line is where the test expects it");
        if (at == std::string::npos) return;
        cat_text.replace(at, old_line.size(),
                         "\"facialhair\": { \"model\": [1, 7, 27, 28, 29, 30, 31], \"preview\": { \"27\": \"data/ui/imgAssets/facialhair/facialhair_27_0.dds\" } },");
        const fs::path root = test_tmp_dir("hb_fix_beard_on");
        const fs::path catfile = root / "asset_catalog_beard.json";
        visual_picker_test::write_bytes(catfile, std::vector<uint8_t>(cat_text.begin(), cat_text.end()));
        auto cat = std::make_shared<assets::Catalog>();
        std::string cerr;
        CHECK(cat->load_file(catfile.u8string(), "0000AAAA-0000BBBB", &cerr) && cat->current(), "beard catalog loaded: " + cerr);
        set_gear_catalog(cat);
        visual_pictures_override((root / "nowhere").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        app.request_tab = 0;
        ui.frames(2);
        visual_picker_request("facialhairtypecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && visual_picker_ui().field == "facialhairtypecode", "the beard grid is open");
        CHECK(visual_picker_tile_source(27) == TileSource::Game && !visual_picker_tile_drawn(27), "beard 27 (a game preview): its preview");
        for (int64_t c : {28, 29, 30, 31})
            CHECK(visual_picker_tile_drawn(c), "beard " + std::to_string(c) + " (3D model, no picture): drawn from its class");
        CHECK(visual_picker_tile_drawn(0), "beard 0 (clean-shaven): a drawn clean face");
        CHECK(visual_picker_tile_icon(30) != visual_picker_tile_icon(31), "a short beard (30) and a full beard (31) are different icons");
        CHECK(visual_picker_tile_icon(0) != visual_picker_tile_icon(31), "clean-shaven and a full beard are different icons");
        every_tile_has_a_picture("beard grid, catalogue on", 4);
        CHECK(has_word(reason_of(30), "drawn icon") && has_word(reason_of(30), "3D model"), "beard 30's tooltip: a 3D model, drawn icon, not a picture of it: " + reason_of(30));
        wind_down();
    });

    run_case("UI: 5.0c E12 fix round: hair 0 reads Bald with and without the RDBM26 folder; beard 0 does not", [&] {
        if (!p || !rec || !p->has("hairtypecode")) return;
        catalogue_on();
        visual_pictures_override((test_tmp_dir("hb_fix_bald") / "nowhere").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        app.request_tab = 0;
        ui.frames(2);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        CHECK(visual_picker_tile_name(0) == "Bald", "no pictures folder: hair 0 is named Bald (not None): " + visual_picker_tile_name(0));
        visual_picker_close();
        ui.frames(2);
        const fs::path root = test_tmp_dir("hb_fix_bald_rdbm");
        put_picture(root, "hairstyle", 0, png_header(2));
        visual_pictures_override((root / "Resources").u8string());
        visual_pictures_refresh(app);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        CHECK(visual_picker_tile_name(0) == "Bald", "with the RDBM26 folder: hair 0 is named Bald: " + visual_picker_tile_name(0));
        if (p->has("facialhairtypecode")) {
            visual_picker_close();
            ui.frames(2);
            visual_picker_request("facialhairtypecode");
            ui.frames(3);
            CHECK(visual_picker_tile_name(0) != "Bald" && !visual_picker_tile_name(0).empty(), "beard 0 keeps its own name: " + visual_picker_tile_name(0));
        }
        wind_down();
    });
}

}  // namespace hairbeard_test
