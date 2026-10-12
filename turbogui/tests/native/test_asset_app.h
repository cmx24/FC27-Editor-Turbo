// Native tests (2.1, asset catalog integration): the app hands a loaded catalog to every picker that reads it (real-face
// chooser, gear galleries, callnames, miniface / crest browsers), writes lua_gate.txt with its own session, and takes it
// all back when the catalog goes away. Synthetic catalog: tests/fixtures/asset_catalog_v1_small.json. Included by
// test_main.cpp; the rules themselves are tested by tests/catalog/run*.sh.
#pragma once
#include <chrono>
#include <thread>

#include "core/asset_catalog.h"
#include "ui/ui_asset_browser.h"
#include "ui/ui_faces.h"
#include "ui/ui_images.h"

static void test_asset_app_ui(App& app, Ui& ui, const fs::path& le) {
    run_case("UI: asset catalog wiring (pickers get the catalog, lua_gate.txt with the GUI session, all dropped without it)", [&] {
        const fs::path out = le / "turbo_output";
        const fs::path dir = assets::catalog_dir(out);
        const std::string key = "0000AAAA-0000BBBB";  // the fixture's build
        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::copy_file("tests/fixtures/asset_catalog_v1_small.json", dir / ("catalog_" + key + ".json"), fs::copy_options::overwrite_existing, ec);
        CHECK(!ec, "fixture copied: " + ec.message());
        auto saved_hook = app.hook_report;
        app.hook_report = [&]() {
            HookReport r;
            r.build = key;
            return r;
        };
        // a load runs on a worker thread; frames until it is swapped in
        auto load = [&](const char* why) {
            for (int i = 0; i < 400 && !app.asset_reload(why); ++i) {
                ui.frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            for (int i = 0; i < 400 && app.asset_loading(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ui.frames(2);
        };
        load("test: catalog added");
        const auto cat = app.asset_catalog();
        CHECK(cat->loaded() && cat->state() == assets::State::Current, "current catalog: " + app.asset_load_note);
        CHECK(asset_view().loaded() && asset_view().catalog == cat && asset_view().use != nullptr,
              "the browsers' view has the catalog and what the save uses");
        std::string gate;
        {
            std::ifstream f(assets::lua_gate_path(out), std::ios::binary);
            gate.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        CHECK(gate.find("#build " + key + "\n#session " + app.session + "\n") != std::string::npos && gate.find("head3d 90001\n") != std::string::npos,
              "lua_gate.txt: build, this GUI's session (asset_gate.lua trusts only its own), head ids");
        CHECK(app.callnames.catalog == cat, "callnames read the catalog's bank");
        size_t hidden_heads = 0;
        for (const auto& f : game_faces(app)) hidden_heads += f.game_head ? 1 : 0;
        CHECK(hidden_heads == 4, fmt("real-face chooser: the 4 non-generic game heads no row uses (%zu)", hidden_heads));
        const std::vector<int64_t> hair = gallery_codes(app, "hairtypecode");
        auto has = [&](int64_t id) { return std::find(hair.begin(), hair.end(), id) != hair.end(); };
        CHECK(has(0) && has(3) && has(4) && !has(5), "hair gallery: none + the 3D models (model-only 3 and 4), not preview-only 5");
        // a rebuilt DB view (model.version) republishes what the save uses
        const auto use_before = asset_view().use;
        app.model.rebuild(app.today());
        ui.frames(2);
        CHECK(asset_view().use && asset_view().use != use_before, "new DbUse after a model rebuild");

        // the catalog is rebuilt under the SAME build key after a content change (folder poll / Reload catalog): every
        // picker gets the new copy, not only lua_gate.txt and the browsers
        {
            std::ifstream in("tests/fixtures/asset_catalog_v1_small.json", std::ios::binary);
            nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            CHECK(!j.is_discarded(), "fixture parsed");
            j["built_at"] = "2026-10-12T09:00:00-04:00";
            j["families"]["hair"]["model"] = nlohmann::json::array({1, 2, 3});                     // hair 4 dropped
            j["families"]["head3d"]["ids"] = nlohmann::json::array({90001, 90002, 90003, 90100});  // head 90004 dropped
            std::ofstream out_f(dir / ("catalog_" + key + ".json"), std::ios::binary | std::ios::trunc);
            out_f << j.dump();
        }
        load("test: rebuilt with the same key");
        const auto cat2 = app.asset_catalog();
        CHECK(cat2 != cat && cat2->current() && cat2->status().build_key == key, "the rebuilt catalog is loaded: " + app.asset_load_note);
        CHECK(app.callnames.catalog == cat2 && asset_view().catalog == cat2, "callnames and the browsers read the rebuilt catalog");
        const std::vector<int64_t> hair2 = gallery_codes(app, "hairtypecode");
        CHECK(std::find(hair2.begin(), hair2.end(), 3) != hair2.end() && std::find(hair2.begin(), hair2.end(), 4) == hair2.end(),
              "hair gallery and gate: 4 is gone after the rebuild");
        size_t hidden_heads2 = 0;
        for (const auto& f : game_faces(app)) hidden_heads2 += f.game_head ? 1 : 0;
        CHECK(hidden_heads2 == 3, fmt("real-face chooser: the dropped head is gone (%zu)", hidden_heads2));

        // the catalog goes away: every picker is back on its 2.0 source
        fs::remove(dir / ("catalog_" + key + ".json"), ec);
        load("test: catalog removed");
        CHECK(!app.asset_catalog()->loaded(), "no catalog");
        CHECK(!asset_view().catalog && !asset_view().use, "browsers: no catalog sources");
        CHECK(!fs::exists(assets::lua_gate_path(out)), "lua_gate.txt deleted");
        CHECK(app.callnames.catalog == nullptr, "callnames: no bank");
        CHECK(game_faces(app).empty(), "real-face chooser: no game heads");
        app.hook_report = saved_hook;
    });
}
