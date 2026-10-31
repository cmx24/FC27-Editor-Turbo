// Native tests (2.20, brief 2.20-T-tactics): the Tactics pages against the store the player really has, and the live read's honesty.
// Included by test_main.cpp after test_page_tactics.h; tactics_page_test::run in the core block, ::ui_cases in the UI block right after
// page_tactics_test::ui_cases.
//
// Why: until 2.20 every fixture of the preset tests was written by write_preset (short notes), so the suite was green while Cassio's
// store (the five 10-16 community slider presets, notes of 833..1208 characters) was refused whole by validate() (note limit 300): both
// preset lists were empty and Refresh re-read the same refusal. tests/fixtures/tactic_presets_synthetic holds five SYNTHETIC files of the
// same shape (note lengths in bytes, slider counts, the top-level include_unverified); values are a fixed pattern, no author's values or credits.
//   core:  the five store-shaped files load (5 listed, 0 skipped); the note limit; the plain
//          reason of a failed live read; the scan log line
//   UI:    the registered pages (the new registry: draw_tactics_presets_page / draw_game_sliders_page) list the five presets from that store,
//          a click picks one (and logs it), the Custom sliders block is read and shown, Refresh reads everything again and is counted;
//          a career whose tactic record is not active ("0 active tactic records", Cassio's Ajax and Sparta careers on 10-18 / 10-19)
//          says so plainly instead of "go to the hub"; Refresh re-reads a changed mocked record (line height 25 -> 28)
#pragma once
#include <string>
#include <vector>

#include "core/tactic_presets.h"
#include "ui/ui_shell.h"
#include "ui/ui_tactic_presets.h"
#include "ui/ui_tactics_page.h"

namespace tactics_page_test {

using namespace turbo;
namespace tp = turbo::tpreset;
using team_sheet_test::SheetWorld;

static const char* kWin = "T20 tactics page";

static fs::path fixture_dir() { return gui_labels_test::root_dir() / "tests" / "fixtures" / "tactic_presets_synthetic"; }

static bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

static void show(Ui& ui, void (*fn)(App&)) {
    App* app = &ui.app;
    ui.extra_draw = [app, fn]() {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1500, 800));
        ImGui::Begin(kWin, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
        fn(*app);
        ImGui::End();
    };
}

// The five synthetic store-shaped files into <dir>; false when the fixture folder is missing
static bool copy_fixtures(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    int n = 0;
    for (fs::directory_iterator it(fixture_dir(), ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".json") continue;
        fs::copy_file(it->path(), dir / it->path().filename(), fs::copy_options::overwrite_existing, ec);
        if (!ec) ++n;
    }
    return n == 5;
}

static bool hooked_has(const std::vector<std::string>& hooked, const std::string& needle) {
    for (const std::string& h : hooked)
        if (has(h, needle)) return true;
    return false;
}

static void run() {
    run_case("T20 tactics: five slider presets shaped like Cassio's store (notes 833..1208 bytes) load (5 listed, 0 skipped)", [] {
        const fs::path dir = test_tmp_dir("t20_store");
        std::error_code ec;
        fs::remove_all(dir, ec);
        CHECK(copy_fixtures(dir), "the five fixture files are there: " + fixture_dir().u8string());
        std::vector<std::string> errors;
        const std::vector<tp::Preset> v = tp::load_presets(dir, &errors);
        std::string why;
        for (const std::string& e : errors) why += e + " | ";
        CHECK(v.size() == 5 && errors.empty(), "5 listed, 0 skipped (until 2.20: 0 listed, 5 skipped for the note): " + std::to_string(v.size()) + " / " + why);
        size_t longest = 0;
        for (const tp::Preset& p : v) {
            CHECK(p.settings.present && !p.tactic.present && p.settings.values.size() >= 70, p.name + ": a sliders block (" + std::to_string(p.settings.values.size()) + ")");
            // unchanged (2.12 rule): the flag is read from the settings block only; these files carry it at the top level, so Prepare stages
            // the screen-proven sliders only. Whether the top-level flag should count is an open question for the Pit wall, not this track.
            CHECK(!p.settings.include_unverified, p.name + ": the top-level include_unverified is not read (unchanged)");
            CHECK(tp::summary(p).find(p.note.substr(0, 20)) == std::string::npos, p.name + ": the list line never shows the note");
            longest = std::max(longest, p.note.size());
        }
        CHECK(longest > 1000, "the long notes are kept whole: " + std::to_string(longest));
        CHECK(!v.empty() && v.front().name == "Synthetic sliders 1", "sorted by name: " + (v.empty() ? std::string("-") : v.front().name));
        const std::string line = tp::scan_line(v.size(), errors, dir.u8string());
        CHECK(line.find("store scan: 5 listed, 0 skipped in ") == 0, "the scan log line: " + line);
        fs::remove_all(dir, ec);
    });

    run_case("T20 tactics: note limit 4000; include_unverified still read from the settings block only (unchanged)", [] {
        tsheet::LiveTactic lt;
        lt.at.user = 1;
        lt.name = "433";
        lt.formation = 27;
        lt.source_formation = 10;
        lt.line_height = 25;
        std::map<std::string, int> sliders = {{"user.sprint_speed", 50}};
        tp::Preset p = tp::make_preset("N", std::string(tp::kMaxNoteLen, 'n'), "2026-10-19 12:00:00", &lt, &sliders, gsettings::layout_name(), "6AC07E31", nullptr, false);
        CHECK(tp::validate(p).empty(), "a note of exactly " + std::to_string(tp::kMaxNoteLen) + " characters: " + tp::validate(p));
        p.note += "n";
        CHECK(has(tp::validate(p), "the note is longer than 4000"), "one more is refused: " + tp::validate(p));
        p.note = "line one\nline two";
        CHECK(has(tp::validate(p), "control characters"), "control characters stay refused: " + tp::validate(p));
        // the flag: the settings block only, as in 2.12 (the top level is ignored)
        const std::string base = R"({"turbo_tactic_preset":1,"name":"X","game_build":")" + std::string(tsheet::kBuild) +
                                 R"(","settings":{"layout":")" + gsettings::layout_name() + R"(","values":{"user.sprint_speed":50}SETFLAG}TOPFLAG})";
        auto parse = [&](const std::string& setflag, const std::string& topflag, tp::Preset& out) {
            std::string t = base;
            t.replace(t.find("SETFLAG"), 7, setflag);
            t.replace(t.find("TOPFLAG"), 7, topflag);
            std::string err;
            const bool ok = tp::Preset::parse(t, out, &err);
            CHECK(ok, "parses: " + err + " | " + t);
            return ok;
        };
        tp::Preset q;
        if (parse("", "", q)) CHECK(!q.settings.include_unverified, "no flag anywhere: off");
        if (parse("", R"(,"include_unverified":true)", q)) CHECK(!q.settings.include_unverified, "top-level true only: still off (unchanged)");
        if (parse(R"(,"include_unverified":true)", "", q)) CHECK(q.settings.include_unverified, "the settings block says true: on");
    });

    run_case("T20 tactics: call sites - every proven preset / read function is called from the code the two registered pages draw", [] {
        // the registered pages (UI case below): TacticPresets -> draw_tactics_presets_page (ui_tactics_page.cpp) whose body is the panel
        // draw_tactic_presets (ui_tactic_presets.cpp); GameplayPresets -> draw_game_sliders_page (ui_tactics_page.cpp). A proven function
        // with no call in these two files would be lost wiring (the 2.13 standings class of bug).
        auto code_of = [](const char* f) {
            std::ifstream in(gui_labels_test::root_dir() / "src" / "ui" / f, std::ios::binary);
            std::string out, line;
            while (std::getline(in, line)) {
                const size_t c = line.find("//");
                out += (c == std::string::npos ? line : line.substr(0, c)) + "\n";
            }
            return out;
        };
        const std::string panel = code_of("ui_tactic_presets.cpp"), page = code_of("ui_tactics_page.cpp");
        CHECK(!panel.empty() && !page.empty(), "the two page sources are read from " + gui_labels_test::root_dir().u8string());
        auto calls = [](const std::string& code, const std::string& fn) {
            for (size_t at = code.find(fn + "("); at != std::string::npos; at = code.find(fn + "(", at + 1)) {
                const char before = at == 0 ? ' ' : code[at - 1];
                if (std::isalnum((unsigned char)before) || before == '_') continue;
                // not the definition: a definition line starts with a return type at column 0
                const size_t bol = code.rfind('\n', at) + 1;
                if (bol < code.size() && code[bol] != ' ' && code[bol] != '\t') continue;
                return true;
            }
            return false;
        };
        CHECK(calls(page, "draw_tactic_presets"), "the Tactic presets page draws the proven panel");
        for (const char* fn : {"live_tactic_read", "live_tactic_gate", "live_tactic_submit", "live_tactic_undo", "game_settings_current",
                               "game_settings_plan", "game_settings_stage", "game_settings_unstage", "load_presets", "last_load_path",
                               "block_from_live", "request_for", "undo_request"})
            CHECK(calls(panel, fn) || calls(page, fn), std::string("a call of ") + fn + " in the pages' code");
        for (const char* fn : {"load_presets", "game_settings_current", "game_settings_stage", "live_tactic_read"})
            CHECK(calls(page, fn), std::string("the Game sliders page / the frame calls ") + fn + " itself");
    });

    run_case("T20 tactics: cause B probe - every record's active flag and source formation, read only, in the reason and on a good read", [] {
        // the known-good: one record active with a source formation (LEICESTER until 10-18 09:34)
        {
            SheetWorld w;
            tsheet::Located at;
            const std::string why = tsheet::locate(w.mem, SheetWorld::kSvc, 95, w.anchors(), at);
            CHECK(why.empty(), "the simulated sheet reads: " + why);
            CHECK(at.records_probe.rfind("5 record(s), 1 with act 1 and src > 0, 1 with act 1; #0 act 1 src 36/36 fmt 27 preset 1 lh 25 b77 3 name '433' raw7C ", 0) == 0,
                  "a good read keeps the record vector: " + at.records_probe);
            CHECK(has(at.records_probe, "; #4 act ") && !has(at.records_probe, "; #5 "), "every record, no more: " + at.records_probe);
        }
        // Cassio's careers: no record active (hypothesis "the flag")
        {
            SheetWorld w;
            w.mem.wr(SheetWorld::kRecs + tsheet::kRecActive, uint8_t(0));
            tsheet::Located at;
            const std::string why = tsheet::locate(w.mem, SheetWorld::kSvc, 95, w.anchors(), at);
            CHECK(why.rfind("layout mismatch (0 active tactic records (exactly 1 needed)) probe: 5 record(s), 0 with act 1 and src > 0, 0 with act 1; #0 act 0 src 36/36", 0) == 0,
                  "the reason carries the probe: " + why);
            CHECK(has(tp::live_why_plain(why), "cannot read this career's tactic") && !has(tp::live_why_plain(why), "probe"), "the plain line stays plain");
        }
        // the record is active but has no source formation (hypothesis "the source formation")
        {
            SheetWorld w;
            w.mem.wr(SheetWorld::kRecs + tsheet::kRecSourceFormation, int32_t(0));
            tsheet::Located at;
            const std::string why = tsheet::locate(w.mem, SheetWorld::kSvc, 95, w.anchors(), at);
            CHECK(has(why, "probe: 5 record(s), 0 with act 1 and src > 0, 1 with act 1; #0 act 1 src 0/36"), "told apart from the flag case: " + why);
        }
        // read only: the probe writes nothing (the simulated memory is byte-equal before and after)
        {
            SheetWorld w;
            std::vector<uint8_t> before, after;
            CHECK(w.mem.read_block(SheetWorld::kRecs, 5 * tsheet::kRecStride, before), "records readable");
            const std::string p = tsheet::records_probe(w.mem, SheetWorld::kRecs, 5);
            CHECK(w.mem.read_block(SheetWorld::kRecs, 5 * tsheet::kRecStride, after) && before == after, "no byte changed: " + p);
            CHECK(has(tsheet::records_probe(w.mem, 0x10, 2), "#0 act ? src ?/? fmt ? preset ? lh ? b77 ? name '' raw7C ?"), "unreadable memory prints ?");
            CHECK(!has(tsheet::records_probe(w.mem, SheetWorld::kRecs, 99), "#8 "), "at most 8 records");
        }
    });

    run_case("T20 tactics: a failed live read in plain words (never 'go to the hub' for a layout the career does not match)", [] {
        const std::string active = tp::live_why_plain("layout mismatch (0 active tactic records (exactly 1 needed))");
        CHECK(has(active, "cannot read this career's tactic") && !has(active, "main hub"), "0 active records: " + active);
        const std::string other = tp::live_why_plain("layout mismatch (1 sheets)");
        CHECK(has(other, "not laid out the way Turbo knows") && has(other, "Close Team Management"), "another layout check: " + other);
        CHECK(has(tp::live_why_plain("not in a career"), "Load your career"), "not in a career");
        CHECK(has(tp::live_why_plain("locating the team sheet service ..."), "still looking"), "locating");
        CHECK(has(tp::live_why_plain("checking the service the scan found again (5 s after the scan) ..."), "still looking"), "checking");
        CHECK(has(tp::live_why_plain("your club: manager slot 87 holds 0 instances (no career loaded?)"), "does not know your club"), "club unknown");
        CHECK(has(tp::live_why_plain("something new"), "main hub"), "anything else keeps the old hint");
        CHECK(tp::live_why_plain("").empty(), "nothing for an empty reason");
        for (const char* w : {"layout mismatch (0 active tactic records (exactly 1 needed))", "not in a career", "locating", "your club: x", "x"}) {
            const std::string s = tp::live_why_plain(w);
            for (const char* bad : {"Lua", "hook", "channel", "json", ".txt", "turbo_output", "0x"}) CHECK(!has(s, bad), std::string("no developer word '") + bad + "' in: " + s);
        }
        std::vector<std::string> errs = {"a.json: " + std::string(300, 'x'), "b.json: why"};
        const std::string line = tp::scan_line(0, errs, "D");
        CHECK(has(line, "store scan: 0 listed, 2 skipped in D; skipped: a.json: ") && has(line, "... | b.json: why") && line.size() < 260, "skipped files named, cut: " + line);
    });
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("T20 tactics: the registered pages list Cassio's store, a click picks, the sliders block is shown, Refresh reads again", [&] {
        // reachability: the pages the navigation draws (the new registry is asked first, App::draw_pages) are the ones tested below
        CHECK(shell::registered_new_page(shell::Page::TacticPresets) == draw_tactics_presets_page, "Tactic presets: the new page is registered");
        CHECK(shell::registered_new_page(shell::Page::GameplayPresets) == draw_game_sliders_page, "Game sliders: the new page is registered");
        tactic_presets_reset();
        live_tactic_reset();
        tactics_pages_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {tp::last_load_file(), gsettings::pending_file(), tsheet::opt_in_file()}) fs::remove(out / f, ec);
        fs::remove_all(tp::presets_dir(out), ec);
        CHECK(copy_fixtures(tp::presets_dir(out)), "five store-shaped files in the store");
        const fs::path root = g_out / "t20sliders";
        fs::remove_all(root, ec);
        const fs::path settings = root / "settings";
        gsettings_test::write_file(settings / gsettings_test::kName, gsettings_test::fixture());
        const fs::path map = root / "slider_map.json";
        std::ofstream(map, std::ios::binary) << gsettings_test::test_map();
        game_settings_set_paths(settings, map);
        game_settings_reset();
        std::vector<std::string> hooked;
        app.log_hook = [&hooked](const std::string& x) { hooked.push_back(x); };
        ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(1900, 1000));

        // ---- Tactic presets (the page the navigation draws)
        show(ui, draw_tactics_presets_page);
        ui.frames(4);
        const TacticPresetsState& s = tactic_presets_state();
        CHECK(s.drawn && s.names.size() == 5 && s.errors.empty(), "the list holds the five presets: " + std::to_string(s.names.size()) + " listed, " + std::to_string(s.errors.size()) + " skipped");
        CHECK(hooked_has(hooked, "tactic presets: store scan: 5 listed, 0 skipped"), "the scan is logged");
        CHECK(hooked_has(hooked, "tactic presets: live read (open): tactic not read: "), "the first look reads the live tactic and logs why it is not read");
        CHECK(s.sliders_ok && has(s.sliders_line, "Custom sliders now: ") && has(s.sliders_line, " values (sprint speed you "), "the Custom sliders block is read: " + s.sliders_line);
        CHECK(hooked_has(hooked, "custom sliders ok: "), "and logged");
        CHECK(!s.live_ok && !s.live_plain.empty(), "no career: the plain reason: " + s.live_plain);
        const std::string label0 = s.names[0] + "  -  " + s.summaries[0] + "##tpsel0";
        CHECK(ui.click(label0, kWin), "click the first preset: " + label0);
        ui.frames(3);
        CHECK(s.selected == 0 && s.can_load, "picked, Apply is on");
        CHECK(hooked_has(hooked, "tactic presets: picked \"Synthetic sliders 1\": 88 sliders"), "the pick is logged");
        CHECK(ui.click("Refresh##tprefresh", kWin), "Refresh");
        ui.frames(3);
        CHECK(s.refreshes == 1 && has(s.status, "Read again: 5 presets"), "Refresh counted and said: " + s.status);
        CHECK(hooked_has(hooked, "tactic presets: live read (refresh): tactic not read: "), "Refresh reads the live tactic again");
        CHECK(s.names.size() == 5 && s.selected == 0, "the list and the pick survive Refresh");

        // (2026-10-31, 5.0c menus step 6: the offline slider staging section, draw_offline_slider_section on Lab > Tactics (experimental), was deleted
        // with the Lab; Game settings > Game settings & CPU sliders writes live, test_sliders_rw.h)

        app.log_hook = nullptr;
        ui.extra_draw = nullptr;
        ui.frames(2);
        tactics_pages_reset();
        tactic_presets_reset();
        game_settings_set_paths(fs::path(), fs::path());
        game_settings_reset();
        fs::remove_all(tp::presets_dir(out), ec);
        fs::remove(out / gsettings::pending_file(), ec);
    });

    run_case("T20 tactics: a career whose tactic record is not active says so; Refresh re-reads a changed record (simulated game)", [&] {
        live_tactic_reset();
        tactic_presets_reset();
        tactics_pages_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {tsheet::opt_in_file(), tsheet::kill_switch_file(), tsheet::master_switch_file(), tsheet::undo_file(), bench::keep_on_file(),
                              bench::keep_off_file(), tp::setpieces_current_file(), tp::last_load_file(), gsettings::pending_file()})
            fs::remove(out / f, ec);
        fs::remove_all(tp::presets_dir(out), ec);
        SheetWorld w(1, 10);
        team_sheet_test::ServiceRig rig(w, out);
        rig.find_returns = SheetWorld::kSvc;
        rig.jobs.inline_run = true;
        app.team_sheet = rig.svc;
        double wall = 2.0e9;
        live_tactic_test_wall_clock([&wall]() { return wall; });
        std::vector<std::string> hooked;
        app.log_hook = [&hooked](const std::string& x) { hooked.push_back(x); };
        const fs::path state_file = out / "bridge_state.json";
        json st = read_json(state_file);
        const json saved = st;
        int bumps = 2500;
        auto write_state = [&]() {
            {
                std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                f << st.dump();
            }
            fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
            app.next_poll = 0.0;
            ui.frames(3);
        };
        st["match_phase"] = "hub";
        st["team_mgmt"] = false;
        st["save_t"] = (long long)(std::time(nullptr)) - 3600;
        st["managers"] = hex_addr(SheetWorld::kManagers);
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(1900, 1000));
        live_tactic_set_opt_in(app, true);
        // Cassio's careers: the club's tactic record is not marked active
        w.mem.wr(SheetWorld::kRecs + tsheet::kRecActive, uint8_t(0));
        show(ui, draw_tactics_presets_page);
        rig.clock += 6e6;
        ui.frames(130);
        const TacticsPresetsPageState& p1 = tactics_presets_page_state();
        const TacticPresetsState& s = tactic_presets_state();
        CHECK(p1.opt_in && !p1.tactic_read, "not read: " + p1.tactic_now);
        CHECK(has(p1.tactic_now, "cannot read this career's tactic") && !has(p1.tactic_now, "main hub"), "the frame says why, plainly: " + p1.tactic_now);
        CHECK(!s.live_ok && has(s.live_plain, "cannot read this career's tactic"), "the panel too: " + s.live_plain);
        show(ui, draw_team_sheet_page);
        ui.frames(70);
        const TeamSheetPageState& p2 = team_sheet_page_state();
        CHECK(!p2.sheet_read && has(p2.sheet_why, "cannot read this career's tactic"), "Team sheet too: " + p2.sheet_why);
        // the record is active again (a career the layout matches): Refresh reads it
        w.mem.wr(SheetWorld::kRecs + tsheet::kRecActive, uint8_t(1));
        show(ui, draw_tactics_presets_page);
        ui.frames(3);
        CHECK(ui.click("Refresh##tprefresh", kWin), "Refresh");
        ui.frames(3);
        CHECK(s.live_ok && has(s.live_line, "Line Height 25"), "read after Refresh: " + s.live_line);
        CHECK(hooked_has(hooked, "tactic presets: live read (refresh): tactic ok: \"433\" team 1, approach 0, line height 25"), "logged with the values");
        // the line height changed in game (Edit Tactic's Yes, or another write): Refresh shows the new value at once
        w.mem.wr(SheetWorld::kRecs + tsheet::kRecLineHeight, int32_t(28));
        CHECK(ui.click("Refresh##tprefresh", kWin), "Refresh again");
        ui.frames(3);
        CHECK(s.live_ok && has(s.live_line, "Line Height 28") && s.refreshes == 2, "Refresh re-reads the changed record: " + s.live_line);
        CHECK(hooked_has(hooked, "tactic presets: live read (refresh): tactic ok: \"433\" team 1, approach 0, line height 28"), "the new value is logged");

        w.mem.wr(SheetWorld::kRecs + tsheet::kRecLineHeight, int32_t(25));
        fs::remove(out / tsheet::opt_in_file(), ec);
        app.team_sheet = nullptr;
        app.log_hook = nullptr;
        ui.extra_draw = nullptr;
        st = saved;
        st["seq"] = saved.value("seq", 0LL) + 60;
        write_state();
        live_tactic_reset();
        tactic_presets_reset();
        tactics_pages_reset();
    });
}

}  // namespace tactics_page_test
