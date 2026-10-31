// Native tests, 4.0 sliders-rw (evidence\2026-10-28\SLIDERS_WRITE\PLAN.md): (a) the slider map per game build, (c) the live write into the
// game's Custom sliders (gsettings::live) on simulated memory (SimMemory: every read/write goes through Memory, as in the game through
// Read/WriteProcessMemory), (b) the Game settings tab's Load applies in the game instead of a silent stage. No real settings folder, no
// turbo_output of a real install. Included by test_main.cpp after test_game_settings.h. SLIDERS_RW_OLD_CODE (the fail-old run on
// cc02b820) compiles only the UI case, which uses no new API.
#pragma once

#ifdef _WIN32
#include <windows.h>  // VirtualAlloc / VirtualProtect for live_write_readonly_page_refused
#endif

namespace sliders_rw_test {

using namespace turbo;
using namespace turbo::gsettings;

// a simulated heap: [count u32 at block-0x10][125 bytes] at kBase + off
constexpr uint64_t kBase = 0x6F8000000ull;
constexpr size_t kHeap = 3u << 20;  // 3 MiB: the scan crosses 1 MiB chunk edges
static void put_block(SimMemory& mem, uint64_t addr, const std::vector<uint8_t>& b, uint32_t count) {
    mem.write(addr - 0x10, &count, 4);
    mem.write(addr, b.data(), b.size());
}

#ifndef SLIDERS_RW_OLD_CODE
static void core() {
    std::printf("native sliders-rw: map per build, live block locator and write (4.0)\n");

    run_case("sliders-rw: map_for_build_6AD922D1 (the 10-27 title update keeps the 6AC07E31 layout) / unknown build refused", [] {
        CHECK(map_file_for("6AD922D1") == map_file(), "6AD922D1 -> " + map_file_for("6AD922D1"));
        CHECK(map_file_for("6ad922d1") == map_file(), "case-insensitive");
        CHECK(map_file_for("6AC07E31") == map_file(), "6AC07E31");
        CHECK(map_file_for("").compare(map_file()) == 0, "build not known (the apply tool, game closed) = the default map");
        CHECK(map_file_for("DEADBEEF").empty(), "an unknown build has no map");
        // 10-28: game_build_key() is "<exe id>-<image size>"; the full key matched nothing (Cassio's page: "no slider map")
        CHECK(map_file_for("6AD922D1-221A8000") == map_file(), "the running game's key 6AD922D1-221A8000 -> " + map_file_for("6AD922D1-221A8000"));
        CHECK(map_file_for("6AC07E31-2145C000") == map_file(), "6AC07E31-2145C000");
        CHECK(map_file_for("DEADBEEF-221A8000").empty(), "an unknown build with a size suffix has no map");
    });

    run_case("sliders-rw: live_locator_unique_hit (count 0x7D at -0x10 + the file's 125 bytes; a copy without the count is ignored; a block across a 1 MiB chunk edge is found once)", [] {
        std::vector<uint8_t> want(125, 50);
        want[0x24] = 63;
        {
            SimMemory mem;
            mem.map(kBase, kHeap);
            put_block(mem, kBase + 0x400, want, 125);
            mem.write(kBase + 0x9000, want.data(), want.size());  // the loaded file buffer's copy: no count before it
            const live::Found f = live::find_block(mem, {{kBase, kBase + kHeap}}, want.data(), want.size());
            CHECK(f.ok && f.hits == 1 && f.addr == kBase + 0x400, fmt("hits %d why %s", f.hits, f.why.c_str()));
        }
        for (uint64_t off : {uint64_t(live::kChunk) - 0x40, uint64_t(live::kChunk) - 0x8, uint64_t(live::kChunk) + 0x8, uint64_t(live::kChunk) * 2 - 0x50}) {
            SimMemory mem;
            mem.map(kBase, kHeap);
            put_block(mem, kBase + off, want, 125);
            const live::Found f = live::find_block(mem, {{kBase, kBase + kHeap}}, want.data(), want.size());
            CHECK(f.ok && f.hits == 1 && f.addr == kBase + off, fmt("edge +0x%llx: hits %d", (unsigned long long)off, f.hits));
        }
    });

    run_case("sliders-rw: live_locator_ambiguous_refused / no hit refused / unreadable span skipped", [] {
        std::vector<uint8_t> want(125, 50);
        SimMemory two;
        two.map(kBase, kHeap);
        put_block(two, kBase + 0x400, want, 125);
        put_block(two, kBase + 0x800, want, 125);
        const live::Found t = live::find_block(two, {{kBase, kBase + kHeap}}, want.data(), want.size());
        CHECK(!t.ok && t.hits == 2 && t.addr == 0 && t.why.find("refused") != std::string::npos, t.why);
        SimMemory e;
        e.map(kBase, kHeap);
        put_block(e, kBase + 0x400, want, 124);  // wrong count
        const live::Found none = live::find_block(e, {{kBase, kBase + kHeap}}, want.data(), want.size());
        CHECK(!none.ok && none.hits == 0 && none.why.find("not found") != std::string::npos, none.why);
        CHECK(!live::find_block(e, {}, want.data(), want.size()).ok, "no spans");
        // a span the game freed (not mapped): failed reads, no crash, the mapped block still found
        SimMemory g;
        g.map(kBase, kHeap);
        put_block(g, kBase + 0x2000, want, 125);
        const live::Found gf = live::find_block(g, {{kBase - (4u << 20), kBase - (1u << 20)}, {kBase, kBase + kHeap}}, want.data(), want.size());
        CHECK(gf.ok && gf.addr == kBase + 0x2000 && g.failed_reads > 0, fmt("hits %d failed reads %d", gf.hits, g.failed_reads));
    });

    run_case("sliders-rw: live_write_readback (Custom entries only; the u32-only entry is skipped; the journal holds old values)", [] {
        SliderMap m;
        std::string err;
        CHECK(SliderMap::parse(gsettings_test::custom_map(true), m, &err), err);
        SimMemory mem;
        mem.map(kBase, 0x2000);
        std::vector<uint8_t> want(125, 50);
        put_block(mem, kBase + 0x400, want, 125);
        const uint64_t a = kBase + 0x400;
        const live::Result r = live::write_block(mem, a, m, {{"user.sprint_speed", 37}, {"user.acceleration", 41}, {"cpu.sprint_speed", 63}, {"nope.x", 9}});
        std::vector<uint8_t> b;
        CHECK(mem.read_block(a - 0x10, 0x10 + 125, b), "read back");
        CHECK(r.ok && b[0x10 + 0x50] == 37 && b[0x10 + 0x51] == 41 && b[0x10 + 0x24] == 63 && b[0x10 + 0x25] == 50, r.message);
        CHECK(r.changed.size() == 3 && r.changed.at(0x24) == std::make_pair(50, 63) && r.skipped == std::vector<std::string>({"nope.x"}), "changed/skipped");
        uint32_t count = 0;
        std::memcpy(&count, b.data(), 4);
        CHECK(count == 125, "the count is not touched");
        const std::string j = live::journal_json(r, "P", "t");
        CHECK(j.find("\"36\"") != std::string::npos && j.find("\"old\": 50") != std::string::npos, j);
    });

    // private_regions lists READ-ONLY private pages too (memmap_win.cpp readable()), and WriteProcessMemory would unprotect them
    // a write that fails half-way puts back every slider written before it (the page must never say NOT applied over a changed game)
    run_case("sliders-rw: live_write_partial_failure_restores_all (2nd write fails: the 1st is put back, nothing reported changed)", [] {
        struct FailAt : Memory {
            SimMemory& in;
            uint64_t bad;
            FailAt(SimMemory& m, uint64_t b) : in(m), bad(b) {}
            bool read(uint64_t a, void* o, size_t n) override { return in.read(a, o, n); }
            bool write(uint64_t a, const void* i, size_t n) override { return a <= bad && bad < a + n ? false : in.write(a, i, n); }
        };
        SliderMap m;
        std::string err;
        CHECK(SliderMap::parse(gsettings_test::custom_map(true), m, &err), err);
        SimMemory sim;
        sim.map(kBase, 0x2000);
        std::vector<uint8_t> want(125, 50);
        const uint64_t a = kBase + 0x400;
        put_block(sim, a, want, 125);
        FailAt mem(sim, a + 0x51);  // user.acceleration; values are written in key order: cpu.sprint_speed (0x24) first
        const live::Result r = live::write_block(mem, a, m, {{"user.sprint_speed", 37}, {"user.acceleration", 41}, {"cpu.sprint_speed", 63}});
        std::vector<uint8_t> b;
        CHECK(sim.read_block(a, 125, b), "read back");
        CHECK(!r.ok && r.changed.empty(), "not ok, nothing reported changed: " + r.message);
        CHECK(b == want, fmt("every slider is back at its old value (0x24=%u 0x50=%u 0x51=%u)", b[0x24], b[0x50], b[0x51]));
    });

    run_case("sliders-rw: live_write_readonly_page_refused (page_writable on a real read-only page; write_block writes nothing)", [] {
        SliderMap m;
        std::string err;
        CHECK(SliderMap::parse(gsettings_test::custom_map(true), m, &err), err);
#ifdef _WIN32
        uint8_t* pg = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        CHECK(pg != nullptr, "VirtualAlloc");
        if (pg) {
            const uint64_t a = uint64_t(reinterpret_cast<uintptr_t>(pg));
            CHECK(live::page_writable(a + 0x400, 125), "a read-write page is writable");
            DWORD old = 0;
            CHECK(VirtualProtect(pg + 4096, 4096, PAGE_READONLY, &old) != 0, "VirtualProtect");
            CHECK(!live::page_writable(a + 4096 + 0x400, 125), "a read-only page is not writable");
            CHECK(!live::page_writable(a + 4096 - 0x40, 125), "a block that runs into a read-only page is not writable");
            VirtualFree(pg, 0, MEM_RELEASE);
        }
#endif
        SimMemory mem;
        mem.map(kBase, 0x2000);
        std::vector<uint8_t> want(125, 50);
        put_block(mem, kBase + 0x400, want, 125);
        const live::Result r = live::write_block(mem, kBase + 0x400, m, {{"cpu.sprint_speed", 63}}, [](uint64_t, size_t) { return false; });
        uint8_t now = 0;
        mem.read(kBase + 0x400 + 0x24, &now, 1);
        CHECK(!r.ok && r.changed.empty() && now == 50 && r.message.find("not on a writable page") != std::string::npos, r.message);
    });

    // the game's own file on 6AD922D1 (Cassio's markers 10-28 20:41, flag byte 00): the shipped map reads it, the live write round-trips
    run_case("sliders-rw: marker_2041_roundtrip_6AD922D1 (shipped map reads 42/41/43, flag 00, locator unique, write 37/41/63, his other 122 untouched)", [] {
        const char* e = std::getenv("TURBO_SLIDERS_MARKER_DIR");
        const fs::path dir = e ? fs::path(e) : fs::path("C:/FC 27 Live Editor/turbo_dev/evidence/2026-10-28/SLIDERS_READ/marker_2041");
        const char* nm = "Settings20261028204116853";
        std::vector<uint8_t> f;
        if (!read_bytes(dir / nm, f, 1u << 20)) {
            std::printf("  SKIP (NOT A PASS): the marker_2041 evidence file is missing under %s (set TURBO_SLIDERS_MARKER_DIR)\n", dir.u8string().c_str());
            return;
        }
        CHECK(md5_hex(f) == "d345f9d45abbef3266e3f9b021c85fca", "the recorded marker file");
        const Check c = validate(f);
        CHECK(c.ok && c.crc_stored == c.crc_calc, "the game's file validates: " + c.why);
        SliderMap m;
        std::string err;
        CHECK(load_map(fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file_for("6AD922D1"), m, &err), err);
        const fs::path d = g_out / "gsettings" / "marker_2041" / "settings";
        std::error_code ec;
        fs::remove_all(d.parent_path(), ec);
        gsettings_test::write_file(d / nm, f);
        const gsettings::Snapshot s = read_current(d, m);
        CHECK(s.ok && s.custom_slots.size() == 125, s.why);
        if (!s.ok || s.custom_slots.size() != 125) return;
        CHECK(s.custom_slots[0x24].second == 42 && s.custom_slots[0x50].second == 41 && s.custom_slots[0x51].second == 43,
              fmt("markers 0x24=%u 0x50=%u 0x51=%u", s.custom_slots[0x24].second, s.custom_slots[0x50].second, s.custom_slots[0x51].second));
        CHECK(s.custom_prefix.size() == 5 && s.custom_prefix[4] == 0, "the flag byte before the Custom block is 00 in the game's file");
        std::vector<uint8_t> want(125);
        for (size_t k = 0; k < 125; ++k) want[k] = uint8_t(s.custom_slots[k].second);
        SimMemory mem;
        mem.map(kBase, 0x4000);
        put_block(mem, kBase + 0x1000, want, 125);
        const live::Found fd = live::find_block(mem, {{kBase, kBase + 0x4000}}, want.data(), want.size());
        CHECK(fd.ok && fd.hits == 1, fd.why);
        if (!fd.ok) return;
        Preset p;
        p.name = "m";
        p.values = {{"user.sprint_speed", 37}, {"user.acceleration", 41}, {"cpu.sprint_speed", 63}};
        const Plan plan = plan_preset(p, m);
        CHECK(plan.values.size() == 3, fmt("the 3 verified sliders are planned (%zu)", plan.values.size()));
        const live::Result r = live::write_block(mem, fd.addr, m, plan.values);
        std::vector<uint8_t> b;
        CHECK(mem.read_block(fd.addr, 125, b), "read back");
        CHECK(r.ok && b[0x50] == 37 && b[0x51] == 41 && b[0x24] == 63, r.message);
        size_t other = 0;
        for (size_t k = 0; k < 125; ++k)
            if (k != 0x24 && k != 0x50 && k != 0x51 && b[k] != want[k]) ++other;
        CHECK(other == 0, "his other 122 values are untouched");
        CHECK(r.changed.at(0x24) == std::make_pair(42, 63) && r.changed.at(0x50) == std::make_pair(41, 37), "the journal holds his values");
        fs::remove_all(d.parent_path(), ec);
    });
}
#endif

// all-sliders prep: the "verified_by_build" key is checked when the map is read (a bad list is refused, never half-used); only SliderMap::parse,
// so it also runs on 2f16eb08, where every refusal below is accepted (the key was ignored) and fails.
static void vbb_parse() {
    run_case("sliders-rw: verified_by_build_parse_refusals (unknown id, twice, no proof, bad build id, not a list)", [&] {
        auto with = [](const std::string& vb) {
            std::string t = gsettings_test::custom_map(true);
            const size_t at = t.rfind('}');
            return t.substr(0, at) + ", \"verified_by_build\": " + vb + "}";
        };
        SliderMap m;
        std::string err;
        CHECK(SliderMap::parse(with(R"({"6AD922D1": {"proof": "", "ids": []}})"), m, &err), "an empty list (the shipped state) reads: " + err);
        CHECK(SliderMap::parse(with(R"({"6ad922d1": {"proof": "check sheet", "ids": ["cpu.acceleration"]}})"), m, &err), "a valid list reads: " + err);
        const Entry* e = m.entry("cpu.acceleration");
        CHECK(e && !e->verified, "reading the list changes no entry's own flag (6AC07E31 stays as it is)");
        auto refused = [&](const std::string& vb, const char* want) {
            SliderMap x;
            std::string er;
            CHECK(!SliderMap::parse(with(vb), x, &er) && er.find(want) != std::string::npos, std::string(want) + ": " + er);
        };
        refused(R"({"6AD922D1": {"proof": "p", "ids": ["cpu.nope"]}})", "is not an entry");
        refused(R"({"6AD922D1": {"proof": "p", "ids": ["cpu.acceleration", "cpu.acceleration"]}})", "listed twice");
        refused(R"({"6AD922D1": {"proof": "", "ids": ["cpu.acceleration"]}})", "needs its proof");
        refused(R"({"6AD922": {"proof": "p", "ids": []}})", "not a build id");
        refused(R"({"6AD922D1": {"proof": "p", "ids": "cpu.acceleration"}})", "not a list");
    });
}

// (b): the tab's Load writes into the running game (regions_hook = the host's memory lister) and stages nothing; on today's code
// (cc02b820) Load only staged turbo_output\settings_pending.json and the game's memory was never written. The file holds 00 before
// the Custom block, as Cassio's files do (marker_2041): with no game memory nothing is staged either (--apply refuses such a file).
static void ui_case(App& app, Ui& ui, const fs::path& le) {
    run_case("sliders-rw UI: preset_load_applies_in_game (Load writes the game's Custom sliders, no silent stage, flag-00 file)", [&] {
        const fs::path root = g_out / "gsettings" / "ui_live";
        std::error_code ec;
        fs::remove_all(root, ec);
        const fs::path settings = root / "settings";
        std::vector<uint8_t> fx = gsettings_test::fixture();  // Custom block 125 x 50
        fx[kPayloadOff + 0xB8976 - 1] = 0;                     // the flag byte as in Cassio's files (00)
        fix_crc(fx);
        gsettings_test::write_file(settings / gsettings_test::kName, fx);
        const fs::path map = root / "slider_map.json";
        std::ofstream(map, std::ios::binary) << gsettings_test::custom_map(true);
        const fs::path out = le / "turbo_output";
        fs::remove_all(out / presets_dir(), ec);
        fs::remove(out / pending_file(), ec);
        fs::remove(out / "settings_live_journal.json", ec);
        // the game's block in (simulated) memory: count 125 at -0x10, the file's 125 bytes
        SimMemory& mem = static_cast<SimMemory&>(app.mem);
        const uint64_t blk = kBase + 0x400;
        CHECK(!mem.pages.count(kBase / SimMemory::kPage), "the test heap is free in the simulated memory");
        mem.map(kBase, 0x2000);
        put_block(mem, blk, std::vector<uint8_t>(125, 50), 125);
        auto at = [&](size_t k) {
            uint8_t v = 0;
            mem.read(blk + k, &v, 1);
            return int(v);
        };
        const auto saved_hook = app.regions_hook;
        app.regions_hook = []() { return std::vector<::turbo::Region>{{kBase, kBase + 0x2000}}; };
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
        {   // the saved preset (50/50/50 from the file) edited to the marker values, then read again (Refresh re-reads the presets folder)
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
        CHECK(at(0x50) == 37 && at(0x51) == 41 && at(0x24) == 63 && at(0x25) == 50,
              fmt("the game's Custom sliders were written (0x50=%d 0x51=%d 0x24=%d): %s", at(0x50), at(0x51), at(0x24), s.preset_line.c_str()));
        CHECK(!fs::exists(out / pending_file(), ec), "nothing staged when it applied in game");
        CHECK(s.preset_line.find("Applied in game now") != std::string::npos && fs::exists(out / "settings_live_journal.json", ec), s.preset_line);
        // a second Load finds the block again although it no longer equals the file (the game has not saved yet)
        CHECK(ui.click("###gsload"), "Load again");
        ui.frames(2);
        CHECK(s.preset_line.find("Applied in game now") != std::string::npos, "second load: " + s.preset_line);
        // no game memory: it says it was NOT applied; nothing is staged for a file --apply refuses (flag 00)
        app.regions_hook = nullptr;
        CHECK(ui.click("###gsload"), "Load without memory");
        ui.frames(2);
        CHECK(s.preset_line.find("NOT applied in the game") != std::string::npos && s.preset_line.find("Nothing staged") != std::string::npos &&
                  !fs::exists(out / pending_file(), ec),
              s.preset_line);
        app.regions_hook = saved_hook;
        for (uint64_t p = kBase / SimMemory::kPage; p <= (kBase + 0x2000) / SimMemory::kPage; ++p) mem.pages.erase(p);  // free the test heap: the next case checks it is free
        fs::remove(out / pending_file(), ec);
        fs::remove(out / "settings_live_journal.json", ec);
        fs::remove_all(out / presets_dir(), ec);
        game_settings_set_paths({}, {});
        game_settings_reset();
        app.request_tab = 0;
        ui.frames(2);
    });

    // all-sliders prep (Pit wall 21:15): the 6AD922D1 proof list in the map ("verified_by_build", one data edit) makes every listed slider
    // verified for that build only. Save current on 6AC07E31 still holds the 3 TEST_31 sliders; on 6AD922D1 it holds all 125 and Load
    // writes all 125 into the game's block. Fails on 2f16eb08 (the key is ignored: 3 saved, 3 written).
    run_case("sliders-rw UI: verified_by_build_load_writes_every_verified_slider (6AD922D1 list: all 125 saved and written; 6AC07E31 keeps its 3)", [&] {
        const fs::path root = g_out / "gsettings" / "ui_vbb";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
        const fs::path settings = root / "settings";
        std::vector<uint8_t> fx = gsettings_test::fixture();  // Custom block 125 x 50
        fx[kPayloadOff + 0xB8976 - 1] = 0;
        fix_crc(fx);
        gsettings_test::write_file(settings / gsettings_test::kName, fx);
        std::string text, err;
        {
            std::ifstream in(fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file_for("6AD922D1"), std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        CHECK(j.is_object() && j.contains("entries") && j["entries"].is_array(), "the shipped map reads");
        if (!j.is_object() || !j.contains("entries")) return;
        nlohmann::json ids = nlohmann::json::array();
        for (const auto& e : j["entries"])
            if (e.contains("custom_index") && !e["custom_index"].is_null()) ids.push_back(e["id"]);
        CHECK(ids.size() == 125, fmt("125 Custom-block entries in the shipped map (%zu)", ids.size()));
        j["verified_by_build"] = {{"6AD922D1", {{"proof", "test: the check sheet answer"}, {"ids", ids}}}};
        const fs::path map = root / "slider_map.json";
        std::ofstream(map, std::ios::binary) << j.dump(1);
        SliderMap m;
        CHECK(load_map(map, m, &err), err);
        const fs::path out = le / "turbo_output";
        fs::remove_all(out / presets_dir(), ec);
        fs::remove(out / pending_file(), ec);
        fs::remove(out / "settings_live_journal.json", ec);
        SimMemory& mem = static_cast<SimMemory&>(app.mem);
        const uint64_t blk = kBase + 0x400;
        CHECK(!mem.pages.count(kBase / SimMemory::kPage), "the test heap is free in the simulated memory");
        mem.map(kBase, 0x2000);
        put_block(mem, blk, std::vector<uint8_t>(125, 50), 125);
        const auto saved_hook = app.regions_hook;
        const auto saved_report = app.hook_report;
        std::string build = "6AC07E31-2145C000";
        app.regions_hook = []() { return std::vector<::turbo::Region>{{kBase, kBase + 0x2000}}; };
        app.hook_report = [&]() {
            HookReport r;
            r.build = build;
            return r;
        };
        auto open_tab = [&]() {
            ui.t += 3.0;  // the hook report is cached for 1 s of the UI clock (Ui::frame sets app.now = ui.t, so app.tick(app.now + 3) alone is undone by the next frame)
            app.tick(ui.t);
            game_settings_set_paths(settings, map);
            game_settings_reset();
            app.request_tab = 7;
            ui.frames(3);
            if (!game_settings_view_state().drawn) {
                parity_test::scroll_into_view(ui, "Game settings file and slider values (Advanced)##gpadv");
                CHECK(ui.click("Game settings file and slider values (Advanced)##gpadv"), "open the Game settings section");
                ui.frames(3);
            }
        };
        auto save_as = [&](const char* name) -> Preset {
            const ItemRec* box = ui.find("##gsname");
            CHECK(box && ui.type_into(box, name), "name typed");
            CHECK(ui.click("Save current##gssave"), "Save current");
            ui.frames(2);
            for (const Preset& p : load_presets(out / presets_dir(), nullptr))
                if (p.name == name) return p;
            CHECK(false, std::string("preset saved: ") + name + ": " + game_settings_view_state().preset_line);
            return Preset();
        };
        open_tab();
        const Preset a = save_as("B6AC");
        CHECK(a.values.size() == 3 && a.values.count("user.sprint_speed") && a.values.count("user.acceleration") && a.values.count("cpu.sprint_speed"),
              fmt("6AC07E31: the 3 TEST_31 sliders only (%zu)", a.values.size()));
        build = "6AD922D1-221A8000";  // the key the hook report carries in game (10-28: the bare id hid the mismatch)
        open_tab();
        Preset b = save_as("B6AD");
        CHECK(b.values.size() == 125, fmt("6AD922D1: every listed slider is verified and saved (%zu of 125)", b.values.size()));
        std::map<int, int> want;  // custom index -> the value Load must write
        b.values.clear();
        for (const Entry& e : m.entries)
            if (e.custom_index >= 0) {
                const int v = e.min + ((e.custom_index * 7 + 3) % (e.max - e.min + 1));
                b.values[e.id] = v;
                want[e.custom_index] = v;
            }
        CHECK(write_preset_file(out / presets_dir(), b, &err), err);
        parity_test::scroll_into_view(ui, "Refresh##gsrefresh");
        CHECK(ui.click("Refresh##gsrefresh"), "Refresh");
        ui.frames(2);
        CHECK(ui.click("###gsload"), "Load");
        ui.frames(2);
        int wrong = 0, first_wrong = -1;
        for (const auto& [k, v] : want) {
            uint8_t got = 0;
            mem.read(blk + uint64_t(k), &got, 1);
            if (int(got) != v && wrong++ == 0) first_wrong = k;
        }
        const GameSettingsViewState& s = game_settings_view_state();
        CHECK(want.size() == 125 && wrong == 0,
              fmt("Load wrote every verified slider (%d of %zu wrong, first at 0x%X): %s", wrong, want.size(), unsigned(first_wrong), s.preset_line.c_str()));
        CHECK(s.preset_line.find("Applied in game now") != std::string::npos && !fs::exists(out / pending_file(), ec), s.preset_line);
        app.regions_hook = saved_hook;
        for (uint64_t p = kBase / SimMemory::kPage; p <= (kBase + 0x2000) / SimMemory::kPage; ++p) mem.pages.erase(p);  // free the test heap: the next case checks it is free
        app.hook_report = saved_report;
        ui.t += 3.0;  // the next case reads its own hook report, not this one's cached 6AD922D1
        app.tick(ui.t);
        fs::remove(out / pending_file(), ec);
        fs::remove(out / "settings_live_journal.json", ec);
        fs::remove_all(out / presets_dir(), ec);
        game_settings_set_paths({}, {});
        game_settings_reset();
        app.request_tab = 0;
        ui.frames(2);
    });

    // 10-28 findability (game-gui-architect override 6, briefs/NEXT_GUI_FINDABILITY.md step 1): the left menu has a "Game settings" area that holds
    // the page; Home's first row opens it in one click; with the Lab and Advanced mode OFF the page shows the preset row with "Load into the game";
    // "Settings" is "Turbo settings". Fails on cc02b820 (no such area, Home has no such button, the page is a pointer to the Lab).
    run_case("sliders-rw UI: findability_game_settings_area (left menu 'Game settings', Home 1 click, Load into the game)", [&] {
        namespace sh = turbo::shell;
        app.go(sh::Page::Home);
        ui.frames(3);
        CHECK(ui.find("Game settings", "##rail", "##shell") != nullptr, "the left menu lists 'Game settings'");
        CHECK(ui.find("Turbo settings", "##rail", "##shell") != nullptr, "Turbo's own settings are 'Turbo settings' in the left menu");
        CHECK(std::string(sh::area_label(sh::area_of(sh::Page::GameplayPresets))) == "Game settings", "the page is in the Game settings area, not under Tactics");
        CHECK(ui.click("###home_game_settings"), "Home: the 'Game settings & CPU sliders' button");
        ui.frames(3);
        CHECK(app.place() == sh::place_of(sh::Page::GameplayPresets), "one click from Home opens the page");
        CHECK(ui.find("Load into the game###gsload") != nullptr && ui.find("Save current##gssave") != nullptr,
              "the preset row (Save current, Load into the game) is on the page with Advanced mode off");
        CHECK(ui.find("###gsmoved") == nullptr, "no pointer to the Lab");
        app.go(sh::Page::Home);
        ui.frames(2);
        CHECK(ui.click("Game settings", "##rail", "##shell") || ui.click("Game settings"), "left menu: Game settings");
        ui.frames(3);
        CHECK(app.place() == sh::place_of(sh::Page::GameplayPresets), "the left menu opens the page (1 click)");
        app.go(sh::Page::Home);
        ui.frames(2);
    });
}

}  // namespace sliders_rw_test
