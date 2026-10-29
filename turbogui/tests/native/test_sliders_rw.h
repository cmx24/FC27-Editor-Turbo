// Native tests, 4.0 sliders-rw (evidence\2026-10-28\SLIDERS_WRITE\PLAN.md): (a) the slider map per game build, (c) the live write into the
// game's Custom sliders (gsettings::live) on a synthetic heap block, (b) the Game settings tab's Load applies in the game instead of a
// silent stage. Synthetic memory only (a buffer of this process); no real settings folder, no turbo_output of a real install.
// Included by test_main.cpp after test_game_settings.h. SLIDERS_RW_OLD_CODE (the fail-old run on cc02b820) compiles only the UI case,
// which uses no new API.
#pragma once

namespace sliders_rw_test {

using namespace turbo;
using namespace turbo::gsettings;

#ifndef SLIDERS_RW_OLD_CODE
static void core() {
    std::printf("native sliders-rw: map per build, live block locator and write (4.0)\n");

    run_case("sliders-rw: map_for_build_6AD922D1 (the 10-27 title update keeps the 6AC07E31 layout) / unknown build refused", [] {
        CHECK(map_file_for("6AD922D1") == map_file(), "6AD922D1 -> " + map_file_for("6AD922D1"));
        CHECK(map_file_for("6ad922d1") == map_file(), "case-insensitive");
        CHECK(map_file_for("6AC07E31") == map_file(), "6AC07E31");
        CHECK(map_file_for("").compare(map_file()) == 0, "build not known (the apply tool, game closed) = the default map");
        CHECK(map_file_for("DEADBEEF").empty(), "an unknown build has no map");
    });

    // a fake heap: [count u32 at block-0x10][125 bytes]
    struct Heap {
        std::vector<uint64_t> words = std::vector<uint64_t>(1024, 0);  // 8 KiB, 8-aligned
        uint8_t* base() { return reinterpret_cast<uint8_t*>(words.data()); }
        live::Span span() { return {uint64_t(reinterpret_cast<uintptr_t>(base())), uint64_t(reinterpret_cast<uintptr_t>(base())) + words.size() * 8}; }
        void put(size_t block_off, const std::vector<uint8_t>& b, uint32_t count) {
            std::memcpy(base() + block_off - live::kCountAt, &count, 4);
            std::memcpy(base() + block_off, b.data(), b.size());
        }
    };

    run_case("sliders-rw: live_locator_unique_hit (count 0x7D at -0x10 + the file's 125 bytes; a copy without the count is ignored)", [] {
        Heap h;
        std::vector<uint8_t> want(125, 50);
        want[0x24] = 63;
        h.put(0x400, want, 125);
        std::memcpy(h.base() + 0x1000, want.data(), want.size());  // the loaded file buffer's copy: no count before it
        const live::Found f = live::find_block({h.span()}, want.data(), want.size());
        CHECK(f.ok && f.hits == 1 && f.addr == h.span().start + 0x400, fmt("hits %d why %s", f.hits, f.why.c_str()));
    });

    run_case("sliders-rw: live_locator_ambiguous_refused / no hit refused", [] {
        Heap h;
        std::vector<uint8_t> want(125, 50);
        h.put(0x400, want, 125);
        h.put(0x800, want, 125);
        const live::Found two = live::find_block({h.span()}, want.data(), want.size());
        CHECK(!two.ok && two.hits == 2 && two.addr == 0 && two.why.find("refused") != std::string::npos, two.why);
        Heap e;
        e.put(0x400, want, 124);  // wrong count
        const live::Found none = live::find_block({e.span()}, want.data(), want.size());
        CHECK(!none.ok && none.hits == 0 && none.why.find("not found") != std::string::npos, none.why);
        const live::Found empty = live::find_block({}, want.data(), want.size());
        CHECK(!empty.ok, "no spans");
    });

    run_case("sliders-rw: live_write_readback (Custom entries only; the u32-only entry is skipped; the journal holds old values)", [] {
        SliderMap m;
        std::string err;
        CHECK(SliderMap::parse(gsettings_test::custom_map(true), m, &err), err);
        Heap h;
        std::vector<uint8_t> want(125, 50);
        h.put(0x400, want, 125);
        const uint64_t a = h.span().start + 0x400;
        const live::Result r = live::write_block(a, m, {{"user.sprint_speed", 37}, {"user.acceleration", 41}, {"cpu.sprint_speed", 63}, {"nope.x", 9}});
        const uint8_t* b = h.base() + 0x400;
        CHECK(r.ok && b[0x50] == 37 && b[0x51] == 41 && b[0x24] == 63 && b[0x25] == 50, r.message);
        CHECK(r.changed.size() == 3 && r.changed.at(0x24) == std::make_pair(50, 63) && r.skipped == std::vector<std::string>({"nope.x"}), "changed/skipped");
        uint32_t count = 0;
        std::memcpy(&count, b - live::kCountAt, 4);
        CHECK(count == 125, "the count is not touched");
        const std::string j = live::journal_json(r, "P", "t");
        CHECK(j.find("\"36\"") != std::string::npos && j.find("\"old\": 50") != std::string::npos, j);
    });
}
#endif

// (b): the tab's Load writes into the running game (regions_hook = the host's memory lister) and stages nothing; on today's code
// (cc02b820) Load only staged turbo_output\settings_pending.json and the game's memory was never written
static void ui_case(App& app, Ui& ui, const fs::path& le) {
    run_case("sliders-rw UI: preset_load_applies_in_game (Load writes the game's Custom sliders, no silent stage)", [&] {
        const fs::path root = g_out / "gsettings" / "ui_live";
        std::error_code ec;
        fs::remove_all(root, ec);
        const fs::path settings = root / "settings";
        gsettings_test::write_file(settings / gsettings_test::kName, gsettings_test::fixture());  // Custom block 125 x 50
        const fs::path map = root / "slider_map.json";
        std::ofstream(map, std::ios::binary) << gsettings_test::custom_map(true);
        const fs::path out = le / "turbo_output";
        fs::remove_all(out / presets_dir(), ec);
        fs::remove(out / pending_file(), ec);
        fs::remove(out / "settings_live_journal.json", ec);
        // the game's block in "memory": count 125 at -0x10, the file's 125 bytes
        std::vector<uint64_t> heap(1024, 0);
        uint8_t* hb = reinterpret_cast<uint8_t*>(heap.data());
        const uint32_t cnt = 125;
        std::memcpy(hb + 0x400 - 0x10, &cnt, 4);
        std::memset(hb + 0x400, 50, 125);
        const uint64_t lo = uint64_t(reinterpret_cast<uintptr_t>(hb));
        app.regions_hook = [lo]() { return std::vector<::turbo::Region>{{lo, lo + 8192}}; };
        game_settings_set_paths(settings, map);
        game_settings_reset();
        app.request_tab = 7;
        ui.frames(3);
        if (!game_settings_view_state().drawn) {
            parity_test::scroll_into_view(ui, "Game settings file and slider values (Advanced)##gpadv");
            CHECK(ui.click("Game settings file and slider values (Advanced)##gpadv"), "open the Game settings section of Gameplay presets");
            ui.frames(3);
        }
        const GameSettingsViewState& s = game_settings_view_state();
        const ItemRec* box = ui.find("##gsname");
        CHECK(box && ui.type_into(box, "Live"), "name typed");
        CHECK(ui.click("Save current##gssave"), "Save current");
        ui.frames(2);
        // the saved preset (50/50/50 from the file) edited to the marker values, then read again (Refresh re-reads the presets folder)
        {
            std::vector<Preset> all = load_presets(out / presets_dir(), nullptr);
            CHECK(all.size() == 1, "one preset saved: " + s.preset_line);
            if (all.size() == 1) {
                all[0].values = {{"user.sprint_speed", 37}, {"user.acceleration", 41}, {"cpu.sprint_speed", 63}};
                std::string err;
                CHECK(write_preset_file(out / presets_dir(), all[0], &err), err);
            }
        }
        parity_test::scroll_into_view(ui, "Refresh##gsrefresh");
        CHECK(ui.click("Refresh##gsrefresh"), "Refresh");
        ui.frames(2);
        CHECK(ui.click("###gsload"), "Load");
        ui.frames(2);
        CHECK(hb[0x400 + 0x50] == 37 && hb[0x400 + 0x51] == 41 && hb[0x400 + 0x24] == 63 && hb[0x400 + 0x25] == 50,
              fmt("the game's Custom sliders were written (0x50=%d 0x51=%d 0x24=%d): %s", hb[0x450], hb[0x451], hb[0x424], s.preset_line.c_str()));
        CHECK(!fs::exists(out / pending_file(), ec), "nothing staged when it applied in game");
        CHECK(s.preset_line.find("Applied in game now") != std::string::npos && fs::exists(out / "settings_live_journal.json", ec), s.preset_line);
        // a second Load finds the block again although it no longer equals the file (the game has not saved yet)
        CHECK(ui.click("###gsload"), "Load again");
        ui.frames(2);
        CHECK(s.preset_line.find("Applied in game now") != std::string::npos, "second load: " + s.preset_line);
        // no game memory (outside FC 27): it says it was NOT applied and stages it for the apply tool, loudly
        app.regions_hook = nullptr;
        CHECK(ui.click("###gsload"), "Load without memory");
        ui.frames(2);
        CHECK(s.preset_line.find("NOT applied in the game") != std::string::npos && fs::exists(out / pending_file(), ec), s.preset_line);
        fs::remove(out / pending_file(), ec);
        fs::remove(out / "settings_live_journal.json", ec);
        fs::remove_all(out / presets_dir(), ec);
        game_settings_set_paths({}, {});
        game_settings_reset();
        app.request_tab = 0;
        ui.frames(2);
    });
}

}  // namespace sliders_rw_test
