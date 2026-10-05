// Native tests (1.1.1): the show/hide key setting (core/hotkey.h, ui/hotkey_setting.h) and the background loading
// (ui/preload.h, LegacyImages background list, TextureCache worker). Included by test_main.cpp after the Ui driver.
#pragma once
#include <algorithm>
#include <set>

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
        CHECK(hotkey_hides_event(0x77, 0x77, false) && !hotkey_hides_event(0x77, 0x77, true) &&
                  !hotkey_hides_event(0x77, 0x41, false) && !hotkey_hides_event(0, 0, false),
              "input shield hides the key's key-downs only (never its key-up, nothing while no key is hidden)");
    });

    run_case("show/hide key (1.1.4): mouse side buttons allowed, list of common keys", [&] {
        CHECK(hotkey_valid_vk(0x05) && hotkey_valid_vk(0x06) && !hotkey_valid_vk(0x02) && !hotkey_valid_vk(0x04),
              "side buttons yes; left / right / middle button no");
        CHECK(vk_name(0x05) == "Mouse 4 (back)" && vk_name(0x06) == "Mouse 5 (forward)" && vk_name(0xC0) == "` ~", "names");
        CHECK(!hotkey_types_text(0x05, 0), "a mouse button never types");
        const std::vector<int>& keys = hotkey_common_keys();
        std::set<std::string> names;
        bool all_valid = true;
        for (int vk : keys) {
            all_valid = all_valid && hotkey_valid_vk(vk);
            names.insert(vk_name(vk));
        }
        CHECK(all_valid && names.size() == keys.size(), fmt("%zu keys, all usable, named apart", keys.size()));
        for (int vk : {0x70, 0x77, 0x7B, 0x2D, 0x24, 0x23, 0x21, 0x22, 0x13, 0x91, 0x60, 0x69, 0x6B, 0xC0, 0x05, 0x06})
            CHECK(std::find(keys.begin(), keys.end(), vk) != keys.end(), "listed: " + vk_name(vk));
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
    run_case("UI: show/hide key picked by pressing it, with Ctrl or a mouse side button; Esc / Cancel; Reset to F8", [&] {
        const char* change = "Change...##hotkey";
        CHECK(ui.click("Status"), "Status tab");
        CHECK(ui.click(change, "", "status"), "Change... in Status > Settings");
        CHECK(app.hotkey_capture, "waiting for a key");
        CHECK(ui.find("Cancel##hotkey", "hkcap") != nullptr, "the capture window is open");
        ui.key(ImGuiKey_Escape);
        CHECK(!app.hotkey_capture && app.toggle_vk == 0x74, "Esc cancels, the key stays");
        ui.frame();
        CHECK(ui.find("Cancel##hotkey") == nullptr, "the capture window closed");
        CHECK(ui.click(change, "", "status"), "Change again");
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
        CHECK(ui.click(change, "FC 27 LE Turbo", "bar"), "Change... in the top bar");
        CHECK(ui.click("Reset to F8", "hkcap"), "Reset to F8");
        CHECK(app.toggle_vk == 0x77 && app.toggle_mods == 0 && !app.hotkey_capture, "F8 again");
        gs = read_json(le / "turbo_output" / "gui_settings.json");
        CHECK(gs["gui"]["toggle_key"].get<int>() == 0x77 && gs["gui"]["toggle_mods"].get<int>() == 0, "F8 saved");
        // Cancel: the wait ends, the key stays (F8 hides Turbo again)
        CHECK(ui.click(change, "", "status"), "Change");
        CHECK(app.hotkey_capture, "waiting");
        CHECK(ui.click("Cancel##hotkey", "hkcap"), "Cancel");
        CHECK(!app.hotkey_capture && app.toggle_vk == 0x77, "the wait ends, F8 stays");
        // a mouse side button (Mouse 4)
        CHECK(ui.click(change, "", "status"), "Change for a mouse button");
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent(3, true);
        ui.frame();
        io.AddMouseButtonEvent(3, false);
        ui.frame();
        CHECK(!app.hotkey_capture && app.toggle_vk == 0x05 && app.toggle_mods == 0, fmt("Mouse 4 (%d)", app.toggle_vk));
        gs = read_json(le / "turbo_output" / "gui_settings.json");
        CHECK(gs["gui"]["toggle_key"].get<int>() == 0x05, "Mouse 4 saved");
        CHECK(ui.toast_contains("Show/hide key: Mouse 4 (back)"), "toast");
        // the capture window is not drawn while Turbo is hidden: the wait ends there too
        CHECK(ui.click(change, "", "status"), "Change");
        app.visible = false;
        ui.frame();
        app.visible = true;
        ui.frames(2);
        CHECK(!app.hotkey_capture, "hidden: the wait ended");
    });

    run_case("UI: show/hide key control on every tab from its first frame; picked from the list, saved, in effect", [&] {
        const char* tabs[] = {"Players", "Teams", "Managers", "Competitions", "Database", "Turbo Tools", "Status"};
        for (int i = 0; i < 7; ++i) {
            app.request_tab = i;
            ui.frame();
            CHECK(ui.find("Change...##hotkey", "FC 27 LE Turbo", "bar") != nullptr && ui.find("##hkpick", "FC 27 LE Turbo", "bar") != nullptr,
                  std::string("top bar control on the first frame of ") + tabs[i]);
        }
        app.request_tab = 5;
        ui.frames(2);
        CHECK(ui.find("Change...##hotkey", "", "tools") != nullptr && ui.find("##hkpick", "", "tools") != nullptr,
              "Turbo Tools: the control is in its first section");
        CHECK(ui.click("##hkpick", "FC 27 LE Turbo", "bar"), "open the list");
        CHECK(ui.find("F12", "##Combo") != nullptr && ui.find("Num 0", "##Combo") != nullptr, "F keys and Num keys listed");
        CHECK(ui.click("F9", "##Combo"), "pick F9");
        CHECK(app.toggle_vk == 0x78 && app.toggle_mods == 0 && !app.hotkey_capture, fmt("F9 in effect (%d)", app.toggle_vk));
        json gs = read_json(le / "turbo_output" / "gui_settings.json");
        CHECK(gs["gui"]["toggle_key"].get<int>() == 0x78 && gs["gui"]["toggle_mods"].get<int>() == 0, "saved: " + gs["gui"].dump());
        CHECK(ui.toast_contains("Show/hide key: F9"), "toast");
        app.toggle_vk = 0;
        app.load_gui_settings();
        CHECK(app.toggle_vk == 0x78, "read back");
        // the narrow window: the control moves to a line of its own, still there
        ImGuiWindow* win = ImGui::FindWindowByName("FC 27 LE Turbo");
        CHECK(win != nullptr, "main window");
        if (win) {
            const ImVec2 size = win->Size;
            ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(S(560.0f), size.y));
            ui.frames(2);
            CHECK(ui.find("##hkpick", "FC 27 LE Turbo", "bar") != nullptr, "narrow window: still shown");
            ImGui::SetWindowSize("FC 27 LE Turbo", size);
            ui.frames(2);
        }
        CHECK(ui.click("##hkpick", "FC 27 LE Turbo", "bar"), "open the list again");
        CHECK(ui.click("F8 (default)", "##Combo"), "back to F8");
        CHECK(app.toggle_vk == 0x77 && app.toggle_mods == 0, "F8");
        CHECK(ui.click("Status"), "Status tab");
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
