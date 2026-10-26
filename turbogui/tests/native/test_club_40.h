// Native tests (2026-10-25, Turbo 4.0 GUI overhaul, slice S9, brief 4.0-gui-pages.md): Team > Kits and Team > Club & crest in the page
// template. Included by test_main.cpp before test_page_kits.h; club_40_test::ui_cases is called at the end of page_kits_test::ui_cases
// (same UI block, a real connected App, Everton = club 7). Both pages are drawn through the Ui hook g_ui_extra in a window of their own.
//   - both pages construct a page::Frame (page::last_record has their id and title; Kits has Apply, Club & crest has none);
//   - Club & crest has the tabs Identity | Stadium & crowd | Staff, and the Staff tab draws the existing draw_career_staff;
//   - a coded field is shown as a descriptor: Everton's tifo code 2 reads "Style 2" (the name table of ui_club_fields.cpp), never a bare 2;
//   - nothing is written: the cases only look (the club fields keep their values).
#pragma once
#include "ui/ui_club_page.h"
#include "ui/ui_kits.h"
#include "ui/ui_kits_page.h"
#include "ui/ui_page.h"

namespace club_40_test {

using namespace turbo;

static void (*g_fn)(App&) = nullptr;
static void draw_extra(App& app) {
    if (!g_fn) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##club40_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) g_fn(app);
    ImGui::End();
}
static void show(Ui& ui, void (*fn)(App&)) {
    g_fn = fn;
    g_ui_extra = draw_extra;
    ui.frames(3);
}
static void hide(Ui& ui) {
    g_fn = nullptr;
    g_ui_extra = nullptr;
    ui.frames(2);
}
// The text of one frame, page window included (ImGui's text log)
static std::string page_text(Ui& ui) {
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ++g_frame;
    ImGui::LogToBuffer();
    ui.app.tick(ui.t);
    ui.app.draw();
    if (g_ui_extra) g_ui_extra(ui.app);
    std::string text = GImGui->LogEnabled ? std::string(GImGui->LogBuffer.c_str()) : std::string();
    ImGui::LogFinish();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    ui.t += 1.0 / 60.0;
    return text;
}
static bool has(const std::string& text, const char* s) { return text.find(s) != std::string::npos; }
static bool bar_has(const page::Record* r, const char* id) {
    if (!r) return false;
    for (const std::string& b : r->bar)
        if (b == id) return true;
    return false;
}

static void ui_cases(App& app, Ui& ui, const fs::path& /*le*/) {
    // under TURBO_TEST_ONLY the case that connects the app is skipped: connect here (a no-op in a full run)
    ui.frames(3);
    if (!app.connected()) app.refresh();
    ui.frames(3);
    run_case("Club 40: Team > Kits is a page::Frame page (title, Apply kit, Undo, Back), the slot list and editor sit inside its body", [&] {
        app.sel_team = 7;
        page::reset_for_tests();
        show(ui, draw_kits_page);
        const page::Record* r = page::last_record("kits");
        CHECK(r != nullptr, "the Kits page constructed a page::Frame with the id kits");
        if (r) {
            CHECK(r->title == "Kits", "the frame title is Kits: " + r->title);
            CHECK(r->has_apply, "Kits has an Apply in the bar");
            CHECK(!r->bar.empty() && r->bar.front() == "###pg_apply" && bar_has(r, "###pg_undo") && bar_has(r, "###pg_back"),
                  "the bar is Apply, Undo, Back (Back is a visible button, never an Escape-only exit)");
        }
        CHECK(ui.find("###pg_apply", "##club40_test") != nullptr, "the Apply kit button is the frame's");
        CHECK(ui.find("###pg_back", "##club40_test") != nullptr, "the Back button is drawn");
        CHECK(ui.find("Home##kpslot", "##select") != nullptr, "the slot list is still in the select panel");
        CHECK(ui.find("Choose kit file...##kpfile", "##editor") != nullptr, "the editor panel is still there");
        // the old page had its own primary button under the editor: the frame's Apply replaces it, there must not be two
        CHECK(ui.find("Apply kit", "##club40_test") == nullptr, "no second Apply kit button next to the frame's");
        hide(ui);
    });

    run_case("Club 40: Team > Club & crest is a page::Frame page with the tabs Identity, Stadium & crowd and Staff", [&] {
        app.sel_team = 7;
        page::reset_for_tests();
        show(ui, draw_club_identity_page);
        const page::Record* r = page::last_record("club_crest");
        CHECK(r != nullptr, "the Club & crest page constructed a page::Frame with the id club_crest");
        if (r) {
            CHECK(r->title == "Club & crest", "the frame title is Club & crest: " + r->title);
            CHECK(!r->has_apply, "Club & crest has no Apply (each part keeps its own Save)");
            CHECK(bar_has(r, "###pg_undo") && bar_has(r, "###pg_back"), "the bar has Undo and Back");
        }
        for (const char* tab : {"###kctabid", "###kctabstadium", "###kctabstaff"})
            CHECK(ui.find(tab, "##club40_test") != nullptr, std::string("the tab is drawn: ") + tab);
        // the first tab is the identity: name, colours and crest cards, and the honest line about Undo
        CHECK(ui.find("##nfull", "##kpname") != nullptr, "Identity: the club name box is in the Name card");
        const std::string txt = page_text(ui);
        CHECK(has(txt, "have no Undo yet"), "Identity: the page says that name and colour changes have no Undo yet");
        hide(ui);
    });

    run_case("Club 40: Club & crest > Staff tab draws the existing Managers & staff page", [&] {
        app.sel_team = 7;
        show(ui, draw_club_identity_page);
        CHECK(ui.find("All managers (table)###csmgr", "##club40_test") == nullptr, "the staff page is not drawn on the Identity tab");
        CHECK(ui.click("###kctabstaff", "##club40_test"), "click the Staff tab");
        ui.frames(3);
        CHECK(ui.find("All managers (table)###csmgr", "##club40_test") != nullptr, "Staff: the All managers header of draw_career_staff is there");
        CHECK(ui.find("###pg_back", "##club40_test") != nullptr, "the frame's bar stays under the tab");
        CHECK(ui.click("###kctabid", "##club40_test"), "back to the Identity tab");
        ui.frames(3);
        hide(ui);
    });

    run_case("Club 40: Club & crest > Stadium & crowd shows a coded field as a descriptor (tifo code 2 reads Style 2), and writes nothing", [&] {
        app.sel_team = 7;
        const Table* t = app.db.table("teams");
        const uint64_t rec = t ? app.db.find(*t, "teamid", 7) : 0;
        const bool have = t && t->field("hastifo");
        CHECK(have, "the test world's teams table has the tifo column");
        const int before = have ? (int)app.db.get_int(*t, rec, "hastifo") : -1;
        show(ui, draw_club_identity_page);
        CHECK(ui.click("###kctabstadium", "##club40_test"), "click the Stadium & crowd tab");
        ui.frames(3);
        CHECK(ui.find("Crowd & presentation###cfcrowd", "##club40_test") != nullptr, "the Crowd & presentation section is drawn");
        CHECK(ui.find("Home ground###cfhome", "##club40_test") != nullptr, "the Home ground section is drawn");
        CHECK(ui.find("##cf", "##club40_test", "hastifo") != nullptr, "the tifo picker is a named-choice combo");
        const std::string txt = page_text(ui);
        CHECK(has(txt, "Style 2"), "the stored tifo code 2 is shown as its name Style 2");
        if (have) CHECK((int)app.db.get_int(*t, rec, "hastifo") == before, "looking at the tab wrote nothing");
        CHECK(ui.click("###kctabid", "##club40_test"), "back to the Identity tab");
        ui.frames(3);
        hide(ui);
    });
}

}  // namespace club_40_test
