// Native tests (2026-10-16, GUI revamp track T4, GUI_REVAMP_SPEC.md 3.1 / 2.3): the Tactics & Gameplay pages that ui_tactic_presets.cpp,
// ui_standings.cpp, ui_tactics.cpp, ui_match.cpp and ui_tuning.cpp register with the shell (ui_shell.h), and the pages' wording.
// Included by test_main.cpp after test_tactic_presets.h; gameplay_page_test::ui_cases runs in the UI block right after
// tactic_presets_test::ui_cases (a real App, the synthetic settings file of test_game_settings.h). Chrome only: nothing here proves a
// writer; Apply goes through the same start_load as the Tactic presets page (its confirm dialog is the proof).
#pragma once
#include <string>
#include <vector>

#include "ui/ui_shell.h"
#include "ui/ui_tactic_presets.h"
#include "ui/ui_tactics_pages.h"
#include "ui/ui_theme.h"

namespace gameplay_page_test {

using namespace turbo;
namespace tp = turbo::tpreset;

static bool has_text(const std::vector<std::string>& v, const std::string& x) {
    for (const std::string& s : v)
        if (s.find(x) != std::string::npos) return true;
    return false;
}

// Draws `fn` in its own window, over the app's, for every frame the Ui steps
static void show_page(Ui& ui, void (*fn)(App&)) {
    App* app = &ui.app;
    ui.extra_draw = [app, fn]() {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1100, 700));
        ImGui::Begin("T4 test page", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
        fn(*app);
        ImGui::End();
    };
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("T4 pages: registered with the shell at start-up (Tactic presets, Gameplay presets, Standings)", [&] {
        CHECK(tactic_pages_registered_at_start(), "Tactic presets and Gameplay presets registered themselves");
        // the other pages register from their own .cpp the same way; the registry was cleared by test_shell_app, so register by hand and draw
        // (2026-10-31, 5.0c menus step 6: the three Advanced pages, page_unproven and the amber line went with the Lab)
        using namespace turbo::shell;
        clear_registry_for_tests();
        CHECK(register_page(Page::Standings, draw_standings_page) && register_page(Page::GameplayPresets, draw_gameplay_presets) &&
                  register_page(Page::TacticPresets, draw_tactic_presets),
              "the page functions are the registrable ones");
        CHECK(page_effect_unmeasured(Page::GameplayPresets) && !page_effect_unmeasured(Page::TacticPresets) && !page_effect_unmeasured(Page::Standings),
              "Gameplay presets is the mixed page (write proven, effect on play not measured)");
        clear_registry_for_tests();  // as test_shell_app left it
    });

    run_case("T4 pages: Gameplay presets - the slider presets only, plain wording, Apply goes through start_load, Game settings & CPU sliders on the page", [&] {
        tactic_presets_reset();
        live_tactic_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {tp::last_load_file(), gsettings::pending_file()}) fs::remove(out / f, ec);
        fs::remove_all(tp::presets_dir(out), ec);
        // the synthetic settings file and slider map of test_game_settings.h (never written to)
        const fs::path root = g_out / "gpage";
        fs::remove_all(root, ec);
        const fs::path settings = root / "settings";
        gsettings_test::write_file(settings / gsettings_test::kName, gsettings_test::fixture());
        const fs::path map = root / "slider_map.json";
        std::ofstream(map, std::ios::binary) << gsettings_test::test_map();
        game_settings_set_paths(settings, map);
        game_settings_reset();
        std::map<std::string, int> values;
        std::string layout, mb, why;
        CHECK(game_settings_current(app, values, layout, mb, why) && !values.empty(), "the synthetic settings file reads: " + why);
        // two presets: SLIDERS (a Custom sliders block) and ONLYTAC (a tactic, no sliders)
        std::string err;
        tp::Preset sl = tp::make_preset("SLIDERS", "", "2026-10-16 10:00:00", nullptr, &values, layout, mb, nullptr, true);
        CHECK(sl.settings.present && tp::write_preset(tp::presets_dir(out), sl, &err), "SLIDERS written: " + err);
        tsheet::LiveTactic lt;
        lt.at.user = 1;
        lt.name = "433";
        lt.formation = 27;
        lt.source_formation = 10;
        lt.line_height = 40;
        const tp::Preset tc = tp::make_preset("ONLYTAC", "", "2026-10-16 10:01:00", &lt, nullptr, layout, mb, nullptr, true);
        tp::Preset tc2 = tc;
        CHECK(!tc2.settings.present && tp::write_preset(tp::presets_dir(out), tc2, &err), "ONLYTAC written: " + err);

        show_page(ui, draw_gameplay_presets);
        ui.frames(4);
        const GameplayPageState& g = gameplay_page_state();
        CHECK(g.drawn && g.names == std::vector<std::string>({"SLIDERS"}), "the list holds the presets with a sliders block only");
        CHECK(g.note.find("Write + EA screen read-back proven, on-pitch effect not measured") != std::string::npos, "the one plain sentence: " + g.note);
        CHECK(ui.find("Apply preset##gpload", "T4 test page") != nullptr && !g.can_apply, "one primary action, off until a preset is picked");
        CHECK(ui.click("SLIDERS  -  " + std::to_string(values.size()) + " sliders##gpsel0", "T4 test page"), "picked SLIDERS");
        ui.frames(3);
        CHECK(g.selected == 0 && g.can_apply, "selected, Apply is enabled");
        CHECK(ui.click("Apply preset##gpload", "T4 test page"), "Apply preset");
        ui.frames(3);
        const TacticPresetsState& s = tactic_presets_state();
        CHECK(s.confirm_open && has_text(s.confirm_lines, "sliders:"), "Apply asks first, through the same Load dialog (start_load): " + s.status + " | " + (s.confirm_lines.empty() ? std::string("-") : s.confirm_lines[0]));
        CHECK(ui.click("Cancel##tpno", "Load tactic preset"), "cancelled");
        ui.frames(3);
        CHECK(!s.confirm_open && !fs::exists(out / gsettings::pending_file(), ec), "Cancel stages nothing");
        // 10-28 findability: no second copy; one link line to Game settings > Game settings & CPU sliders
        ui.frames(3);
        CHECK(game_settings_view_state().drawn && ui.find("###gslink_gp", "T4 test page") == nullptr,
              "the page draws Game settings & CPU sliders itself, no link to itself");
        // the Tactic presets page lists both presets; the same Apply button id family, one primary action
        show_page(ui, draw_tactic_presets);
        ui.frames(4);
        CHECK(tactic_presets_state().names.size() == 2 && ui.find("Apply preset##tpload", "T4 test page") != nullptr, "Tactic presets page: both presets, its own primary action");
        ui.extra_draw = nullptr;
        ui.frames(2);
        tactic_presets_reset();
        game_settings_set_paths(fs::path(), fs::path());
        game_settings_reset();
        fs::remove_all(tp::presets_dir(out), ec);
    });
}

}  // namespace gameplay_page_test
