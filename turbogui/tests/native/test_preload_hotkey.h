// Native tests (1.1.1): the show/hide key setting (core/hotkey.h, ui/hotkey_setting.h) and the background loading
// (ui/preload.h, LegacyImages background list, TextureCache worker). Included by test_main.cpp after the Ui driver.
#pragma once
#include "core/hotkey.h"
#include "ui/preload.h"

// ---- pure parts (no ImGui)
static void test_hotkey_and_background() {
    run_case("show/hide key: names, modifiers, keys that type, keys a hotkey cannot use", [&] {
        CHECK(vk_name(0x77) == "F8" && vk_name('K') == "K" && vk_name(0x65) == "Num 5" && vk_name(0x21) == "Page Up", "names");
        CHECK(vk_name(0xE9).find("0xE9") != std::string::npos, "unknown keys by code");
        CHECK(hotkey_name(0x77, 0) == "F8" && hotkey_name('K', kHotkeyCtrl | kHotkeyShift) == "Ctrl+Shift+K", "with modifiers");
        CHECK(hotkey_matches(0x77, 0, 0x77, false, false, false), "plain F8");
        CHECK(!hotkey_matches(0x77, 0, 0x77, true, false, false), "Ctrl+F8 is not F8");
        CHECK(hotkey_matches('K', kHotkeyCtrl, 'K', true, false, false) && !hotkey_matches('K', kHotkeyCtrl, 'K', false, false, false),
              "Ctrl+K needs Ctrl");
        CHECK(!hotkey_valid_vk(0x1B) && !hotkey_valid_vk(0x11) && !hotkey_valid_vk(0x01) && !hotkey_valid_vk(0) && hotkey_valid_vk(0x77),
              "Escape, modifiers and mouse buttons refused");
        CHECK(hotkey_types_text('K', 0) && hotkey_types_text(0x20, kHotkeyShift) && !hotkey_types_text('K', kHotkeyCtrl) &&
                  !hotkey_types_text(0x77, 0),
              "keys that type text");
    });

    run_case("legacy files: background list after the screen's, peek, arrivals and missing ones leave it", [&] {
        fs::path le = g_out / "legacy_bg";
        fs::remove_all(le);
        LegacyImages L(le);
        const std::string a = legacy_path::player_miniface(501), b = legacy_path::player_miniface(502),
                          c = legacy_path::tattoo_preview(9);
        CHECK(L.want_background(a) && L.want_background(b), "two in the background");
        CHECK(!L.want_background(a) && !L.want_background("data/../x.dds"), "twice or invalid: refused");
        fs::path f;
        CHECK(L.peek(a, &f) == LegacyImages::State::Waiting && L.waiting() == 2, "peek does not ask again");
        CHECK(L.locate(c, &f) == LegacyImages::State::Waiting, "on screen");
        L.tick(0.0);
        std::string want = read_file(L.cache_dir() / "want.txt");
        CHECK(want.find(c + "\n" + a + "\n" + b + "\n") != std::string::npos, "screen first, then background: " + want);
        L.locate(b, &f);  // now on screen too: listed once, in front
        L.flush();
        want = read_file(L.cache_dir() / "want.txt");
        CHECK(want.find(b) == want.rfind(b) && want.find(b) < want.find(a), "listed once, in front: " + want);
        fs::create_directories(L.cache_dir() / "data" / "ui" / "imgAssets" / "heads");
        std::ofstream(L.cache_dir() / "data" / "ui" / "imgAssets" / "heads" / "p501.dds") << "x";
        std::ofstream(L.cache_dir() / "missing.txt") << c << "\n";
        L.tick(2.0);
        CHECK(L.peek(a, &f) == LegacyImages::State::Game && f.filename() == "p501.dds", "arrived");
        CHECK(L.peek(c) == LegacyImages::State::Missing, "missing");
        CHECK(L.waiting_background() == 1, fmt("arrived one left the background list (%zu)", L.waiting_background()));
        CHECK(!L.want_background(a), "a picture at hand is not asked for");
    });
}

// ---- in the UI (Status tab, top bar), after the dry-run case
static void ui_cases_preload_hotkey(App& app, Ui& ui, const fs::path& le) {
    run_case("UI: show/hide key picked by pressing it, with Ctrl; Esc cancels; Reset to F8", [&] {
        CHECK(ui.click("Status"), "Status tab");
        CHECK(ui.click("Change##hotkey"), "Change");
        CHECK(app.hotkey_capture, "waiting for a key");
        ui.key(ImGuiKey_Escape);
        CHECK(!app.hotkey_capture && app.toggle_vk == 0x74, "Esc cancels, the key stays");
        CHECK(ui.click("Change##hotkey"), "Change again");
        ui.key(ImGuiKey_K, true);
        CHECK(!app.hotkey_capture, "captured");
        CHECK(app.toggle_vk == 'K' && app.toggle_mods == kHotkeyCtrl, fmt("Ctrl+K (%d, %d)", app.toggle_vk, app.toggle_mods));
        json gs = read_json(le / "turbo_output" / "gui_settings.json");
        CHECK(gs["gui"]["toggle_key"].get<int>() == 'K' && gs["gui"]["toggle_mods"].get<int>() == kHotkeyCtrl, "saved: " + gs["gui"].dump());
        CHECK(ui.toast_contains("Show/hide key: Ctrl+K"), "toast");
        // a new window reads it back
        app.toggle_vk = 0;
        app.toggle_mods = 0;
        app.load_gui_settings();
        CHECK(app.toggle_vk == 'K' && app.toggle_mods == kHotkeyCtrl, "loaded from gui_settings.json");
        CHECK(ui.click("Reset to F8"), "Reset to F8");
        CHECK(app.toggle_vk == 0x77 && app.toggle_mods == 0, "F8 again");
        gs = read_json(le / "turbo_output" / "gui_settings.json");
        CHECK(gs["gui"]["toggle_key"].get<int>() == 0x77 && gs["gui"]["toggle_mods"].get<int>() == 0, "F8 saved");
        // waiting for a key, then another tab: the wait ends (F8 hides Turbo again)
        CHECK(ui.click("Change##hotkey"), "Change");
        CHECK(app.hotkey_capture, "waiting");
        CHECK(ui.click("Players"), "Players tab");
        CHECK(!app.hotkey_capture, "the wait ends on another tab");
        CHECK(ui.click("Status"), "back to Status");
    });

    run_case("UI: background loading starts by itself: pictures asked from Lua, decoded on the worker, progress line", [&] {
        CHECK(app.preloader && app.preloader->started(), "started at the first show");
        ui.frames(2);
        CHECK(app.preloader->collected() && app.preloader->total() >= 6, fmt("pictures listed: %zu", app.preloader->total()));
        // a few frames: every path is looked at (64 per frame) and the missing ones asked for in the background
        ui.frames(int(app.preloader->total() / Preloader::kChecksPerFrame) + 3);
        app.legacy.flush();
        const std::string want = read_file(le / "turbo_output" / "cache" / "legacy" / "want.txt");
        const std::string face = legacy_path::player_miniface(1004);
        if (app.legacy.peek(face) == LegacyImages::State::Waiting)
            CHECK(want.find(face) != std::string::npos, "real-face miniface asked without opening the picker: " + want);
        CHECK(app.lua_images_wanted() == (app.preloader->loading() && app.legacy.waiting_background() > 0),
              "Lua is nudged while pictures wait and Turbo is shown");
        app.visible = false;
        CHECK(!app.lua_images_wanted(), "not while hidden (the game runs)");
        app.visible = true;
        if (app.preloader->settled() < app.preloader->total())
            CHECK(app.preload_line().rfind("Loading pictures: ", 0) == 0, "progress line: " + app.preload_line());
        // the game's file arrives (Lua exported it): decoded on the worker, then shown with an upload only
        fs::path cached = le / "turbo_output" / "cache" / "legacy" / "data" / "ui" / "imgAssets" / "heads" / "p1004.dds";
        const bool had = fs::exists(cached);
        if (!had) {
            fs::create_directories(cached.parent_path());
            std::vector<uint8_t> dds = encode_dds_dxt5(solid(180, 180, 200, 40, 40));
            std::ofstream(cached, std::ios::binary).write(reinterpret_cast<const char*>(dds.data()), std::streamsize(dds.size()));
        }
        const size_t settled0 = app.preloader->settled();
        ui.frames(int(app.preloader->total() / Preloader::kChecksPerFrame) + 3);
        CHECK(app.preloader->settled() > settled0 || had, "the arrived picture counts");
        CHECK(app.textures.preload_wait(5.0), "worker idle");
        CHECK(app.textures.preload_decoded() >= 1 && app.textures.preload_done() >= 1, fmt("decoded in memory: %zu", app.textures.preload_decoded()));
        TextureCache::Pic pic = app.textures.file(cached, int(std::ceil(S(96.0f))));
        CHECK(pic.tex != nullptr && pic.w <= int(std::ceil(S(100.0f))), fmt("drawn from the decoded copy (%d px)", pic.w));
        if (!had) {
            app.textures.forget(cached);
            fs::remove(cached);  // the pictures case later exports it through Lua
        }
    });
}
