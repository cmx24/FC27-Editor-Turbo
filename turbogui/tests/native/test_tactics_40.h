// Native tests (4.0 GUI overhaul, slice S10): the three Tactics pages of ui/ui_tactics_page.cpp inside page::Frame. Included by
// test_main.cpp after test_ui_page.h; tactics40_test::ui_cases runs in the UI block (a real App), drawing each page function in its own
// window "##t40_page" through the Ui hook g_ui_extra (the pages are shared by the old and the 4.0 menus; no registry is needed).
//
// What is guarded (the three page cases FAIL on the base 64242633 = ce664c6b plus a tools text file, where no Tactics page constructs a
// page::Frame; the Lab-copy case is a regression guard and PASSES on the base). Pit wall decision 2026-10-25, replacing brief S10 lines
// 176-178: Game sliders stays a pointer to the Lab (GUI30K, 3.0.1), because `turbo_settings_apply.exe --apply` fails at cold boot.
//   - Tactics > Presets, Team sheet & bench and Game sliders each construct page::Frame with their own id and title, have no Apply
//     (Apply preset stays inside its panel) and carry the bar Undo, Back;
//   - Team sheet & bench still shows the captain and takers read-out (the existing button "Read from the game") and the bench option;
//   - Game sliders is the GUI30K pointer drawn inside the frame: where the section went, the honest "no effect seen in game" label
//     verbatim, the link (###gsmoved), and NO slider staging (no list, no Refresh, no Prepare);
//   - the Lab page still draws the section (heading and label).
#pragma once
#include "ui/ui_page.h"
#include "ui/ui_tactic_presets.h"
#include "ui/ui_tactics_page.h"

namespace tactics40_test {

using namespace turbo;

static void (*g_fn)(App&) = nullptr;
static std::string g_text;  // the text the page drew in its last frame (ImGui's text log; logged_frame does not call g_ui_extra)
static void draw_extra(App& app) {
    if (!g_fn) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    ImGui::LogToBuffer();
    if (ImGui::Begin("##t40_page", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) g_fn(app);
    ImGui::End();
    g_text = GImGui->LogEnabled ? std::string(GImGui->LogBuffer.c_str()) : std::string();
    ImGui::LogFinish();
}
static void show(Ui& ui, void (*fn)(App&), int frames = 6) {
    g_fn = fn;
    g_ui_extra = draw_extra;
    ui.frames(frames);
}
static void hide(Ui& ui) {
    g_fn = nullptr;
    g_ui_extra = nullptr;
    ui.frames(2);
}
static const ItemRec* find(Ui& ui, const char* id) { return ui.find(id, "##t40_page"); }

static void check_frame(const char* id, const char* title) {
    const page::Record* rec = page::last_record(id);
    CHECK(rec != nullptr, std::string("the page constructs page::Frame with the id ") + id);
    if (!rec) return;
    CHECK(rec->title == title, std::string("title '") + title + "', got '" + rec->title + "'");
    CHECK(!rec->has_apply, std::string(id) + ": no Apply in the bar (its action stays inside the panel)");
    CHECK(rec->bar.size() == 2 && rec->bar[0] == "###pg_undo" && rec->bar[1] == "###pg_back", std::string(id) + ": the bar is Undo, Back");
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    if (!app.connected()) ui.frames(5);

    run_case("S10 Tactics > Presets: inside page::Frame; the proven panel and its Apply preset stay in the body", [&] {
        changes_page_test::Sandbox sb(app, ui);
        tactic_presets_reset();
        tactics_pages_reset();
        page::reset_for_tests();
        show(ui, draw_tactics_presets_page);
        check_frame("tactics_presets", "Presets");
        CHECK(tactics_presets_page_state().drawn, "the page body ran");
        CHECK(tactic_presets_state().drawn, "the existing panel is drawn inside the frame");
        CHECK(find(ui, "###pg_undo") != nullptr && find(ui, "###pg_back") != nullptr, "the bar buttons are on screen");
        CHECK(find(ui, "Apply preset##tpload") != nullptr, "Apply preset (the panel's own primary action) is still there");
        hide(ui);
        page::reset_for_tests();
    });

    run_case("S10 Tactics > Team sheet & bench: inside page::Frame; bench option and captain / takers read-out stay", [&] {
        changes_page_test::Sandbox sb(app, ui);
        tactics_pages_reset();
        page::reset_for_tests();
        show(ui, draw_team_sheet_page, 70);
        check_frame("team_sheet", "Team sheet & bench");
        CHECK(team_sheet_page_state().drawn, "the page body ran");
        CHECK(find(ui, "Keep my bench order###tsbench") != nullptr, "the bench option is there");
        // the set-piece takers are reachable from this page (q2 'y'): the read-out button and its lines
        CHECK(find(ui, "Read from the game###tsreadsp") != nullptr, "captain and takers: the existing read-out button is on the page");
        CHECK(!team_sheet_page_state().setpieces_line.empty() || team_sheet_page_state().setpieces_read, "the read-out line is worded");
        hide(ui);
        page::reset_for_tests();
    });

    run_case("S10 + 10-28 findability: Game settings & CPU sliders inside page::Frame draws the preset row (no Lab pointer, no Tactics staging)", [&] {
        const bool was_adv = app.prefs.advanced_mode;
        for (bool adv : {false, true}) {
            changes_page_test::Sandbox sb(app, ui);
            app.prefs.advanced_mode = adv;
            tactics_pages_reset();
            page::reset_for_tests();
            show(ui, draw_game_sliders_page, 8);
            const std::string how = adv ? " (Lab on)" : " (Lab off)";
            check_frame("game_sliders", "Game settings & CPU sliders");
            const GameSlidersPageState& s = game_sliders_page_state();
            CHECK(s.drawn, "the page body ran" + how);
            CHECK(!s.moved_note && find(ui, "###gsmoved") == nullptr, "no pointer to the Lab" + how);
            CHECK(find(ui, "Load into the game###gsload") != nullptr, "the preset row: Load into the game" + how);
            CHECK(find(ui, "###gsrefresh") == nullptr && find(ui, "Prepare###gsprepare") == nullptr && find(ui, "###gsel0") == nullptr,
                  "no list, no Refresh, no Prepare on the Tactics page" + how);
            CHECK(s.lab_heading.empty() && s.lab_label.empty(), "the offline slider section was not drawn here" + how);
            const std::string& txt = g_text;
            CHECK(txt.find("Game sliders moved to the Lab") == std::string::npos, "no 'moved to the Lab' pointer text" + how);
            hide(ui);
            page::reset_for_tests();
        }
        app.prefs.advanced_mode = was_adv;
    });

    run_case("S10 Lab > Tactics (experimental) keeps its own copy of the section until the in-game PASS", [&] {
        changes_page_test::Sandbox sb(app, ui);
        tactics_pages_reset();
        show(ui, draw_lab_tactics_page, 8);
        const GameSlidersPageState& s = game_sliders_page_state();
        CHECK(s.lab_heading == kOfflineSlidersHeading && s.lab_label == kOfflineSlidersLabel, "the Lab page draws the section heading and the label");
        hide(ui);
    });
}

}  // namespace tactics40_test
