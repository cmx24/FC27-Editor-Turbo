// Native tests (2.16 GUI revamp, brief 2.16-gui-pages, track "advanced"): Advanced > Diagnostics & logs and Advanced > Data export
// (ui/ui_diagnostics_page.cpp). Included by test_main.cpp after test_kit_share.h; page_advanced_test::ui_cases runs inside the UI block
// (a real App). The pages are not registered with the shell yet (no page ids until the navigation re-cut), so they are drawn through
// the Ui hook g_ui_extra: a separate window "##advpage_test" drawn after the app's own frame, as test_club_page.h does.
//
// What is guarded: the page draws without a hook report; it gathers its facts at most once a second (the gui/perf rule: no file read,
// no folder listing, no hook report in the frames in between); the switch files of turbo_output appear within a second and
// disappear again; the log box follows App::log_lines; the Data export page keeps the proven Exports buttons.
#pragma once
#include "ui/ui_diagnostics_page.h"

namespace page_advanced_test {

using namespace turbo;

static void (*g_page_fn)(App&) = nullptr;
static void draw_extra(App& app) {
    if (!g_page_fn) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##advpage_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) g_page_fn(app);
    ImGui::End();
}
static void show(Ui& ui, void (*fn)(App&)) {
    g_page_fn = fn;
    g_ui_extra = draw_extra;
    ui.frames(3);
}
static void hide(Ui& ui) {
    g_page_fn = nullptr;
    g_ui_extra = nullptr;
    ui.frames(2);
}
static bool has_switch(const std::string& file, bool* opt_in = nullptr, std::string* what = nullptr) {
    for (const DiagSwitch& s : diagnostics_state().switches)
        if (s.file == file) {
            if (opt_in) *opt_in = s.opt_in;
            if (what) *what = s.what;
            return true;
        }
    return false;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("Advanced > Diagnostics: the plain words for switch files (pure)", [&] {
        CHECK(!diagnostics_switch_what("reapply_off.txt").empty() && !diagnostics_switch_what("kits3d_on.txt").empty() &&
                  !diagnostics_switch_what("tactics_off.txt").empty() && !diagnostics_switch_what("game_hooks_off.txt").empty(),
              "the known switches have a description");
        CHECK(diagnostics_switch_what("hook_game_tick_off.txt").find("game_tick") != std::string::npos, "hook_<name>_off.txt names the hook");
        CHECK(diagnostics_switch_what("call_job_offer_off.txt").find("job_offer") != std::string::npos, "call_<name>_off.txt names the call");
        CHECK(!diagnostics_switch_what("callname_voice_log_on.txt").empty(), "a *_log_on.txt file is described");
        CHECK(diagnostics_switch_what("zz_unknown_off.txt").empty() && diagnostics_switch_what("hook__off.txt").empty(), "an unknown name has no description");
    });

    run_case("Advanced > Diagnostics & logs: draws with no hook report; the cards are there", [&] {
        const auto saved_hook = app.hook_report;
        app.hook_report = nullptr;
        diagnostics_reset_for_tests();
        show(ui, draw_diagnostics_page);
        const DiagnosticsState& st = diagnostics_state();
        CHECK(st.refreshes >= 1 && st.refreshed_at >= 0.0, "a snapshot was gathered");
        CHECK(!st.health_line.empty() && !st.database_line.empty() && !st.catalog_line.empty(), "health, database and catalog lines: " + st.health_line);
        CHECK(st.hook_rows == 0, "no hook report: no hook rows");
        for (const char* id : {"###diaglink", "###diagswitches", "###diaghooks", "###diagdetails", "###diaglogcard"})
            CHECK(ui.find(id, "##advpage_test") != nullptr, std::string("card header: ") + id);
        CHECK(ui.find("Reload catalog##diagasset", "##advpage_test") != nullptr, "the catalog buttons sit in the Game link card");
        CHECK(ui.find("Open the folder##diagswitch", "##advpage_test") != nullptr, "the switch folder button");
        CHECK(ui.find("Copy log##diaglog", "##advpage_test") != nullptr, "the log button");
        hide(ui);
        app.hook_report = saved_hook;
    });

    run_case("Advanced > Diagnostics & logs: the hook report is asked once a second at most, not in every frame", [&] {
        const auto saved_hook = app.hook_report;
        int calls = 0;
        app.hook_report = [&]() {
            ++calls;
            HookReport r;
            r.build = "6AB9813C-211EF000";
            r.table_source = "built-in";
            r.enabled = true;
            r.signatures.push_back({"found_one", SigState::Found, 0x140001000ULL, 0x140002000ULL, 1, ""});
            r.signatures.push_back({"two", SigState::Ambiguous, 0, 0, 2, "more than one match"});
            HookStatus h;
            h.name = "found_one";
            h.active = true;
            h.target = 0x140002000ULL;
            h.calls = 5;
            r.hooks.push_back(h);
            r.dispatcher_pumps = 2;
            return r;
        };
        // the page hidden: what the rest of the app asks for by itself
        diagnostics_reset_for_tests();
        g_page_fn = nullptr;
        g_ui_extra = nullptr;
        ui.frames(3);
        calls = 0;
        ui.frames(120);
        const int hidden = calls;
        // the page shown for the same 120 frames (2 seconds of App::now)
        show(ui, draw_diagnostics_page);
        const long long r0 = diagnostics_state().refreshes;
        calls = 0;
        ui.frames(120);
        const int shown = calls;
        const long long gathered = diagnostics_state().refreshes - r0;
        CHECK(shown - hidden <= 3, fmt("120 frames: %d report calls with the page, %d without (at most 3 more: one per second)", shown, hidden));
        CHECK(gathered >= 1 && gathered <= 3, fmt("120 frames = 2 seconds: %lld snapshots", gathered));
        const DiagnosticsState& st = diagnostics_state();
        CHECK(st.hook_rows == 3, fmt("hook rows: 2 signatures + 1 hook = %zu", st.hook_rows));
        CHECK(!st.health_good && st.health_line.find("Needs attention") == 0, "one code spot is ambiguous: needs attention: " + st.health_line);
        // 30 frames (half a second): no new snapshot
        const long long r1 = diagnostics_state().refreshes;
        ui.frames(20);
        CHECK(diagnostics_state().refreshes - r1 <= 1, "20 frames later: at most the one snapshot that fell due");
        hide(ui);
        app.hook_report = saved_hook;
    });

    run_case("Advanced > Diagnostics & logs: switch files appear within a second and go again; the listing is not made in every frame", [&] {
        const auto saved_hook = app.hook_report;
        app.hook_report = nullptr;
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        fs::create_directories(out, ec);
        const fs::path off = out / "zz_advpage_test_off.txt", on = out / "zz_advpage_test_on.txt";
        fs::remove(off, ec);
        fs::remove(on, ec);
        diagnostics_reset_for_tests();
        show(ui, draw_diagnostics_page);
        ui.frames(2);
        CHECK(!has_switch("zz_advpage_test_off.txt") && !has_switch("zz_advpage_test_on.txt"), "neither file yet");
        { std::ofstream(off) << "x"; std::ofstream(on) << "x"; }
        ui.frames(20);  // a third of a second after the last snapshot
        CHECK(!has_switch("zz_advpage_test_off.txt"), "20 frames later the folder was not listed again (cached for a second)");
        ui.frames(70);
        bool opt = true;
        std::string what = "x";
        CHECK(has_switch("zz_advpage_test_off.txt", &opt) && !opt, "the _off file is listed as a kill switch");
        CHECK(has_switch("zz_advpage_test_on.txt", &opt, &what) && opt && what.empty(), "the _on file is listed as an opt-in; no description for an unknown name");
        fs::remove(off, ec);
        fs::remove(on, ec);
        ui.frames(70);
        CHECK(!has_switch("zz_advpage_test_off.txt") && !has_switch("zz_advpage_test_on.txt"), "removed files are gone from the list");
        hide(ui);
        app.hook_report = saved_hook;
    });

    run_case("Advanced > Diagnostics & logs: the log box follows App::log_lines and Copy log copies them", [&] {
        const size_t n0 = app.log_lines.size();
        app.log_lines.push_back("advpage test log line one");
        app.log_lines.push_back("advpage test log line two");
        show(ui, draw_diagnostics_page);
        CHECK(diagnostics_state().log_lines == app.log_lines.size() && app.log_lines.size() >= n0 + 2, "the box holds every line (clipped when drawn)");
        const ItemRec* copy = ui.find("Copy log##diaglog", "##advpage_test");
        CHECK(copy != nullptr && ui.click(copy), "click Copy log");
        ui.frames(2);
        const char* clip = ImGui::GetClipboardText();
        CHECK(clip && std::string(clip).find("advpage test log line two") != std::string::npos, "the clipboard holds the log");
        CHECK(ui.toast_contains("Log copied"), "toast");
        app.log_lines.pop_back();
        app.log_lines.pop_back();
        hide(ui);
        CHECK(app.log_lines.size() == n0, "log restored");
    });

    run_case("Advanced > Data export: the title, the folder button and the Exports buttons of the proven page", [&] {
        diagnostics_reset_for_tests();
        show(ui, draw_data_export_page);
        CHECK(diagnostics_state().export_page_drawn, "the page ran");
        CHECK(ui.find("Open the export folder##dataexport", "##advpage_test") != nullptr, "the folder button");
        if (app.mailbox) {
            for (const char* b : {"Season stats", "Fixtures & results", "Transfer history", "Jersey numbers", "Export tables", "Probe report"})
                CHECK(ui.find(b, "##advpage_test") != nullptr, std::string("export button: ") + b);
        }
        CHECK(diagnostics_state().refreshes == 0, "the export page gathers no diagnostics");
        hide(ui);
    });
}

}  // namespace page_advanced_test
