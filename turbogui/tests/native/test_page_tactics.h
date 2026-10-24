// Native tests (2.16 GUI revamp, brief 2.16-gui-pages, track "tactics", plan step 8): the three Tactics pages of ui_tactics_page.cpp -
// Tactic presets (the frame around the existing panel), Team sheet & bench, Game sliders. Included by test_main.cpp after
// test_gameplay_page.h; page_tactics_test::ui_cases runs in the UI block right after gameplay_page_test::ui_cases (a real App, the
// synthetic settings file of test_game_settings.h, the simulated game of test_team_sheet.h). The pages are not registered with the shell
// (that is the wiring step), so each case draws the page function in its own window.
//   Game sliders:     list = presets with a sliders block only; Prepare stages (sliders only, no tactic job, no last-load file, settings
//                     folder untouched); the status never says installed/applied; Discard takes the staged file back
//   Team sheet:       the bench toggle writes/removes the same switch file the live tactic card uses; states in plain words; the saved
//                     order and the captain / takers read-out in names; honest tag
//   Tactic presets:   proof tag + one line; "tactic now" in plain words; the existing panel (its Apply button) is the body
#pragma once
#include <cstring>
#include <string>
#include <vector>

#include "ui/ui_shell.h"
#include "ui/ui_tactic_presets.h"
#include "ui/ui_tactics_page.h"
#include "ui/ui_theme.h"

namespace page_tactics_test {

using namespace turbo;
namespace tp = turbo::tpreset;
using team_sheet_test::SheetWorld;

static const char* kWin = "T8 test page";

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

static bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

// words a player must not meet on these pages (the plan's R9 list); the states hold every string the pages build at run time
// The one exact command the page must give (3.0.1): turbo_settings_apply.exe with no argument only prints its usage, so every step names --apply.
static const char* const kApplyCmd = "turbo\\turbo_settings_apply.exe --apply";
static bool clean(std::string s) {
    for (size_t at; (at = s.find(kApplyCmd)) != std::string::npos;) s.erase(at, std::strlen(kApplyCmd));  // the command is the user-facing words here, not a developer word
    for (const char* w : {"Lua", "hook", "channel", "json", ".txt", "md5", "turbo_output", ".exe", "\\"})
        if (has(s, w)) return false;
    return true;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("T8 pages: Game sliders - Prepare stages the sliders only, honest status, Discard takes it back, settings folder untouched", [&] {
        tactic_presets_reset();
        live_tactic_reset();
        tactics_pages_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {tp::last_load_file(), gsettings::pending_file()}) fs::remove(out / f, ec);
        fs::remove_all(tp::presets_dir(out), ec);
        const fs::path root = g_out / "t8sliders";
        fs::remove_all(root, ec);
        const fs::path settings = root / "settings";
        gsettings_test::write_file(settings / gsettings_test::kName, gsettings_test::fixture());
        const fs::path map = root / "slider_map.json";
        std::ofstream(map, std::ios::binary) << gsettings_test::test_map();
        game_settings_set_paths(settings, map);
        game_settings_reset();
        const auto folder_before = gsettings_test::md5s(settings);
        std::map<std::string, int> values;
        std::string layout, mb, why;
        CHECK(game_settings_current(app, values, layout, mb, why) && !values.empty(), "the synthetic settings file reads: " + why);
        std::string err;
        tp::Preset sl = tp::make_preset("SLIDERS", "", "2026-10-17 20:00:00", nullptr, &values, layout, mb, nullptr, true);
        CHECK(sl.settings.present && tp::write_preset(tp::presets_dir(out), sl, &err), "SLIDERS written: " + err);
        tsheet::LiveTactic lt;
        lt.at.user = 1;
        lt.name = "433";
        lt.formation = 27;
        lt.source_formation = 10;
        lt.line_height = 40;
        const tp::Preset tc = tp::make_preset("ONLYTAC", "", "2026-10-17 20:01:00", &lt, nullptr, layout, mb, nullptr, true);
        tp::Preset tc2 = tc;
        CHECK(!tc2.settings.present && tp::write_preset(tp::presets_dir(out), tc2, &err), "ONLYTAC written: " + err);

        const bool was_advanced = app.prefs.advanced_mode;
        app.prefs.advanced_mode = false;
        ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(1900, 1000));
        show(ui, draw_game_sliders_page);
        ui.frames(4);
        const GameSlidersPageState& s = game_sliders_page_state();
        CHECK(s.drawn && s.names == std::vector<std::string>({"SLIDERS"}), "the list holds the presets with sliders only (the tactic-only one stays out)");
        CHECK(s.tag == "Effect in matches: not tested yet", "the honest tag: " + s.tag);
        const ItemRec* prep = ui.find("Prepare###gsprepare", kWin);
        CHECK(prep != nullptr && !s.can_prepare, "one primary action, off until a preset is picked");
        CHECK(ui.click("SLIDERS  -  " + std::to_string(values.size()) + " sliders###gsel0", kWin), "picked SLIDERS");
        ui.frames(3);
        CHECK(s.selected == 0 && s.can_prepare && !s.can_discard, "selected: Prepare on, Discard off (" + s.counts_line + ")");
        CHECK(has(s.counts_line, "confirmed on the Custom screen") && has(s.counts_line, "left out"), "the counts are stated plainly: " + s.counts_line);
        CHECK(s.status == "Not prepared yet." && !s.prepared, "status before: " + s.status);
        CHECK(has(s.steps, "1. Press Prepare.") && has(s.steps, "Gameplay Type: Custom") && has(s.steps, "Close the game"), "the three steps are stated");
        CHECK(has(s.steps, kApplyCmd) && has(s.steps, "Close the game"), "step 2 gives the exact command, game closed first: " + s.steps);
        // Prepare
        CHECK(ui.click("Prepare###gsprepare", kWin), "Prepare");
        ui.frames(3);
        CHECK(fs::exists(out / gsettings::pending_file(), ec), "the sliders are staged");
        CHECK(!fs::exists(out / tp::last_load_file(), ec), "no tactic job and no last-load record: the page stages sliders only");
        CHECK(s.prepared && has(s.status, "Prepared.") && s.can_discard, "status after: " + s.status);
        CHECK(has(s.status, kApplyCmd), "the Prepared status line gives the exact command: " + s.status);
        CHECK(has(s.message, "Gameplay Type: Custom") && has(s.message, "Prepared"), "message: " + s.message);
        CHECK(has(s.message, kApplyCmd), "the Prepare message gives the exact command: " + s.message);
        for (const std::string* str : {&s.status, &s.message, &s.counts_line, &s.steps}) {
            CHECK(!has(*str, "Installed") && !has(*str, "installed"), "never claims an install that did not happen: " + *str);
            CHECK(!has(*str, "applied") && !has(*str, "Applied"), "never says applied for a staged preset: " + *str);
            CHECK(clean(*str), "no developer words in: " + *str);
        }
        CHECK(gsettings_test::md5s(settings) == folder_before, "the settings folder is unchanged (nothing is written to the game's own files)");
        // Discard
        CHECK(ui.click("Discard###gsdiscard", kWin), "Discard");
        ui.frames(3);
        CHECK(!fs::exists(out / gsettings::pending_file(), ec) && !s.prepared && s.status == "Not prepared yet.", "Discard takes the staged file back: " + s.status);
        CHECK(has(s.message, "Discarded"), "message: " + s.message);
        // Advanced: the old Game settings view hangs under the page (so its one call site survives the wiring)
        app.prefs.advanced_mode = true;
        ui.frames(3);
        CHECK(ui.click("Game settings file and slider values (Advanced)###gsadv", kWin), "the Advanced section header");
        ui.frames(3);
        CHECK(game_settings_view_state().drawn, "Advanced view: the Game settings view is drawn inside the page");
        app.prefs.advanced_mode = was_advanced;
        ui.extra_draw = nullptr;
        ui.frames(2);
        tactics_pages_reset();
        tactic_presets_reset();
        game_settings_set_paths(fs::path(), fs::path());
        game_settings_reset();
        fs::remove_all(tp::presets_dir(out), ec);
        fs::remove(out / gsettings::pending_file(), ec);
    });

    run_case("T8 pages: Tactic presets frame + Team sheet & bench - plain words, the bench toggle, the saved order, captain and takers (simulated game)", [&] {
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
        int bumps = 1500;
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
        const bool was_advanced = app.prefs.advanced_mode;
        app.prefs.advanced_mode = false;
        ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(1900, 1000));

        // ---- Tactic presets frame: the opt-in is off -> "not read" with what to do
        show(ui, draw_tactics_presets_page);
        ui.frames(4);
        const TacticsPresetsPageState& p1 = tactics_presets_page_state();
        CHECK(p1.drawn && p1.tag == "Not tested yet" && has(p1.proof_line, "not tested yet"), "the proof tag and line: " + p1.tag + " | " + p1.proof_line);
        CHECK(has(p1.store_line, "shared with Game sliders"), "one preset store, explained: " + p1.store_line);
        CHECK(!p1.opt_in && !p1.tactic_read && has(p1.tactic_now, "Turn on"), "opt-in off: " + p1.tactic_now);
        CHECK(tactic_presets_state().drawn, "the existing panel is the body of the page (its state was drawn)");
        CHECK(ui.find("Apply preset##tpload", kWin) != nullptr, "the existing panel's own primary action is on the page");
        // opt-in on -> the live tactic in plain words
        live_tactic_set_opt_in(app, true);
        rig.clock += 6e6;
        ui.frames(130);
        CHECK(p1.opt_in && p1.tactic_read, "read now: " + p1.tactic_now);
        CHECK(has(p1.tactic_now, "Deep, line height 25, build-up Balanced - shape 4-3-3"), "plain words: " + p1.tactic_now);
        for (const std::string* str : {&p1.tag, &p1.proof_line, &p1.store_line, &p1.tactic_now}) CHECK(clean(*str), "no developer words in: " + *str);

        // ---- Team sheet & bench
        show(ui, draw_team_sheet_page);
        ui.frames(70);
        const TeamSheetPageState& p2 = team_sheet_page_state();
        CHECK(p2.drawn && p2.tag == "New, not tested yet", "the honest tag: " + p2.tag);
        CHECK(!p2.bench_on && has(p2.bench_status, "Off. The game decides your bench order."), "off by default: " + p2.bench_status);
        CHECK(p2.sheet_read && p2.starting.size() == 11 && p2.bench.size() == 20, "the saved order: " + std::to_string(p2.starting.size()) + " + " + std::to_string(p2.bench.size()) +
                                                                                   " (" + p2.sheet_why + ")");
        CHECK(!p2.starting.empty() && p2.starting[0].find("100") == std::string::npos, "names, not ids: " + (p2.starting.empty() ? std::string("-") : p2.starting[0]));
        CHECK(ui.click("Keep my bench order###tsbench", kWin), "the toggle clicked");
        ui.frames(70);
        CHECK(fs::exists(out / bench::keep_on_file(), ec) && p2.bench_on && has(p2.bench_status, "On. Each time Team Management opens"), "on: the same switch file the live tactic card uses: " + p2.bench_status);
        CHECK(p2.bench_hint.empty(), "the opt-in is on: no hint");
        bool logged = false;
        for (const std::string& h : hooked)
            if (has(h, "bench keep: switched on in the Turbo window")) logged = true;
        CHECK(logged, "the switch is logged");
        // a safety switch overrides the plain line
        { std::ofstream(out / tsheet::master_switch_file(), std::ios::binary) << "off\n"; }
        ui.frames(70);
        CHECK(has(p2.bench_status, "Switched off by a safety switch"), "kill switch: " + p2.bench_status);
        fs::remove(out / tsheet::master_switch_file(), ec);
        ui.frames(70);
        // off again
        CHECK(ui.click("Keep my bench order###tsbench", kWin), "the toggle clicked: off");
        ui.frames(70);
        CHECK(!fs::exists(out / bench::keep_on_file(), ec) && !p2.bench_on && has(p2.bench_status, "Off."), "off: the switch file is gone: " + p2.bench_status);
        // the opt-in off while bench keep is on -> the hint
        CHECK(team_sheet_set_bench_keep(app, true), "set on by the function");
        live_tactic_set_opt_in(app, false);
        ui.frames(70);
        CHECK(has(p2.bench_hint, "Let Turbo change my tactic"), "hint when the opt-in is off: " + p2.bench_hint);
        live_tactic_set_opt_in(app, true);
        CHECK(team_sheet_set_bench_keep(app, false), "set off by the function");
        // captain and takers: read-only, in names
        CHECK(!p2.setpieces_read && has(p2.setpieces_line, "Read from the game"), "nothing read yet: " + p2.setpieces_line);
        {
            std::ofstream f(out / tp::setpieces_current_file(), std::ios::binary | std::ios::trunc);
            f << R"({"turbo_setpieces":1,"team":1,"values":{"captainid":100003,"penaltytakerid":-1,"leftcornerkicktakerid":100005}})";
        }
        ui.frames(130);
        CHECK(p2.setpieces_read, "the read-out is used: " + p2.setpieces_line);
        bool cap = false, pen = false, cor = false;
        for (const auto& kv : p2.takers) {
            if (kv.first == "Captain") cap = !kv.second.empty() && kv.second != "Nobody set";
            if (kv.first == "Penalty taker") pen = kv.second == "Nobody set";
            if (kv.first == "Corner taker (left)") cor = !kv.second.empty();
        }
        CHECK(cap && pen && cor, "captain and takers by name ('Nobody set' for -1)");
        for (const std::string* str : {&p2.tag, &p2.bench_status, &p2.bench_hint, &p2.setpieces_line}) CHECK(clean(*str), "no developer words in: " + *str);

        // clean up
        fs::remove(out / tp::setpieces_current_file(), ec);
        for (const char* f : {tsheet::opt_in_file(), bench::keep_on_file()}) fs::remove(out / f, ec);
        app.team_sheet = nullptr;
        app.log_hook = nullptr;
        app.prefs.advanced_mode = was_advanced;
        ui.extra_draw = nullptr;
        st = saved;
        st["seq"] = saved.value("seq", 0LL) + 60;
        write_state();
        live_tactic_reset();
        tactic_presets_reset();
        tactics_pages_reset();
    });
}

}  // namespace page_tactics_test
