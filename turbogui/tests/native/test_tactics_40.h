// Native tests (4.0 GUI overhaul, slice S10): the three Tactics pages of ui/ui_tactics_page.cpp inside page::Frame. Included by
// test_main.cpp after test_ui_page.h; tactics40_test::ui_cases runs in the UI block (a real App), drawing each page function in its own
// window "##t40_page" through the Ui hook g_ui_extra (the pages are shared by the old and the 4.0 menus; no registry is needed).
//
// What is guarded (the three page cases FAIL on the base 64242633 = ce664c6b plus a tools text file, where no Tactics page constructs a
// page::Frame and Game sliders is only a pointer to the Lab; the Lab-copy case is a regression guard and PASSES on the base):
//   - Tactics > Presets, Team sheet & bench and Game sliders each construct page::Frame with their own id and title, have no Apply
//     (Apply preset / Prepare stay inside their panels) and carry the bar Undo, Back;
//   - Team sheet & bench still shows the captain and takers read-out (the existing button "Read from the game") and the bench option;
//   - Game sliders draws the offline slider section (the same function the Lab draws): its list Refresh, Prepare, the heading and the
//     honest "no effect seen in game" label verbatim; the old pointer (moved_note, "Open Lab ...") is gone;
//   - the Lab page still draws its own copy of the section (deletion target until the in-game PASS).
#pragma once
#include "ui/ui_page.h"
#include "ui/ui_tactic_presets.h"
#include "ui/ui_tactics_page.h"

namespace tactics40_test {

using namespace turbo;

static void (*g_fn)(App&) = nullptr;
static void draw_extra(App& app) {
    if (!g_fn) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##t40_page", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) g_fn(app);
    ImGui::End();
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

    run_case("S10 Tactics > Game sliders: inside page::Frame; draws the offline slider section with the honest label, no pointer", [&] {
        changes_page_test::Sandbox sb(app, ui);
        tactics_pages_reset();
        page::reset_for_tests();
        show(ui, draw_game_sliders_page, 8);
        check_frame("game_sliders", "Game sliders");
        const GameSlidersPageState& s = game_sliders_page_state();
        CHECK(s.drawn, "the page body ran");
        CHECK(!s.moved_note, "no pointer page any more");
        CHECK(find(ui, "###gsmoved") == nullptr, "no 'Open Lab' button");
        CHECK(s.lab_heading == kOfflineSlidersHeading && s.lab_label == kOfflineSlidersLabel, "the section heading and the label were drawn verbatim");
        CHECK(s.lab_label.find("No effect seen in game") == 0, "the label still says no effect was seen in game");
        CHECK(find(ui, "###gsrefresh") != nullptr, "the list's Refresh is on the page");
        CHECK(find(ui, "Prepare###gsprepare") != nullptr, "Prepare is on the page (it stays inside the section)");
        hide(ui);
        page::reset_for_tests();
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
