// Native tests (4.0 GUI overhaul, slice S7): Players > Profile with the Callname row, Player creation and Bulk edit inside the page
// template (ui/ui_page.h). Included by test_main.cpp after test_page_bulk.h; profile40_test::ui_cases runs inside the UI block, a real App,
// drawing each page through the Ui hook g_ui_extra in a window "##profile40_test".
//
// What is guarded:
//   - the Profile page shows the Callname row: the section "###pf_callname" is on the page and the EXISTING commentary-name picker
//     (callname_editor: ui_callnames.cpp, ui_callname_pick.h; its own Preview and Apply) was drawn for the picked player. NOTE: this row
//     is already on the base (ui_player_profile.cpp pf_callname, labelled "What the commentator says"); the case is a guard, not a fix.
//   - Player creation and Bulk edit construct page::Frame: page::last_record() holds their id and title and the bar in the explicit order
//     Back (S7b: no bar Undo; neither has a frame Apply: Create like him... and the staged Apply of Bulk edit stay where they were), with every
//     control of the old page still on it (searched by the ids the 2.20 tests use).
// The two Frame cases FAIL on ce664c6b (no record: the pages draw no frame) and pass with S7.
#pragma once
#include "ui/ui_bulk_edit.h"
#include "ui/ui_page.h"
#include "ui/ui_player_creation.h"
#include "ui/ui_player_profile.h"

namespace profile40_test {

using namespace turbo;

enum class Which { Profile, Creation, Bulk };
static Which g_which = Which::Profile;

static void draw_extra(App& app) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##profile40_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        switch (g_which) {
            case Which::Profile: draw_player_profile(app); break;
            case Which::Creation: draw_player_creation(app); break;
            case Which::Bulk: draw_bulk_edit(app); break;
        }
    }
    ImGui::End();
}
static void show(Ui& ui, Which w) {
    g_which = w;
    g_ui_extra = draw_extra;
    ui.frames(3);
}
static void hide(Ui& ui) {
    g_ui_extra = nullptr;
    ui.frames(2);
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    const char* W = "##profile40_test";

    run_case("Profile 4.0: the Profile page shows the Callname row (the existing commentary picker, drawn for the picked player)", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        app.sel_player = 1005;
        show(ui, Which::Profile);
        const PlayerProfileState& st = player_profile_state();
        CHECK(st.playerid == 1005, "the editor is drawn for the picked player");
        CHECK(ui.find("###pf_callname", W) != nullptr, "the Callname row is on the page");
        CHECK(st.callname_shown, "the existing commentary-name picker (callname_editor) was drawn in it");
        hide(ui);
        app.sel_player = 0;
    });

    run_case("Player creation 4.0: the page is inside page::Frame (Back in the bar) and keeps every control", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        app.sel_player = 0;
        page::reset_for_tests();
        show(ui, Which::Creation);
        const page::Record* r = page::last_record("player_creation");
        CHECK(r != nullptr, "the page drew a page::Frame with the id player_creation");
        if (r) {
            CHECK(r->title == "Player creation", "title: " + r->title);
            CHECK(!r->has_apply, "no frame Apply (Create like him... keeps its place)");
            CHECK(r->bar.size() == 1 && r->bar[0] == "###pg_back" && !r->undo_enabled, "the bar is Back only (S7b: no bar Undo)");
        }
        CHECK(ui.find("###pg_back", W) != nullptr && ui.find("###pg_undo", W) == nullptr, "Back is a button on the page, the bar Undo is not (S7b)");
        CHECK(ui.find("###pc_tab_player", W) != nullptr && ui.find("###pc_tab_cmt", W) != nullptr, "both tabs");
        CHECK(ui.find("##pcsearch", W) != nullptr && ui.find("###pc_chip_free", W) != nullptr && ui.find("##pcpos", W) != nullptr, "search, chip, position filter");
        CHECK(ui.find("Create like him...", W) != nullptr, "the footer action");
        hide(ui);
    });

    run_case("Bulk edit 4.0: the page is inside page::Frame (Back in the bar) and keeps every control", [&] {
        if (app.busy()) ui.click("Cancel");
        bulkedit::reset();
        page::reset_for_tests();
        show(ui, Which::Bulk);
        const page::Record* r = page::last_record("bulk_edit");
        CHECK(r != nullptr, "the page drew a page::Frame with the id bulk_edit");
        if (r) {
            CHECK(r->title == "Bulk edit", "title: " + r->title);
            CHECK(!r->has_apply, "no frame Apply (the staged Apply with its preview stays in section 3)");
            CHECK(r->bar.size() == 1 && r->bar[0] == "###pg_back" && !r->undo_enabled, "the bar is Back only (S7b: no bar Undo)");
        }
        CHECK(ui.find("###pg_back", W) != nullptr && ui.find("###pg_undo", W) == nullptr, "Back is a button on the page, the bar Undo is not (S7b)");
        CHECK(ui.find("##bk_search", W) != nullptr, "search box");
        CHECK(ui.find("Add field###bk_addfield", W) != nullptr, "Add field");
        CHECK(ui.find("###bk_apply", W) != nullptr, "the staged Apply");
        CHECK(ui.find("###bk_undo", W) != nullptr, "the page's own Undo of the last bulk edit");
        hide(ui);
        bulkedit::reset();
    });
}

}  // namespace profile40_test
