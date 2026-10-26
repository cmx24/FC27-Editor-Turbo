// Native tests (4.0 GUI overhaul, slice S1): the page template (ui/ui_page.h) and its worked example, Advanced > Data export
// (ui/ui_diagnostics_page.cpp). Included by test_main.cpp after test_page_changes.h and test_page_advanced.h; ui_page_test::ui_cases
// runs inside the UI block (a real App), drawing pages through the Ui hook g_ui_extra in a window "##uipage_test".
//
// What is guarded:
//   - the frame draws a title, ONE status line and an action bar whose buttons are visible and in the explicit order Apply, Undo, Back
//     (Apply only when the page has one);
//   - Undo is the Changes list's global Undo (a player edit made elsewhere is taken back from the bar; no undo store of the page);
//   - Back goes through App::back(), is disabled with no place to go back to, and is a button (not Escape-only);
//   - the status line is worded "Done: ..." / "Couldn't ...: ..." by the one helper;
//   - the worked example (Data export) draws the template's bar and keeps its folder button and the Exports buttons.
// The Data export case sits OUTSIDE the __has_include guard on purpose: it looks the bar up by its stable ids, so it FAILS on the
// 3.1.0 base (9a1e888, no template) and passes with the template. The fixture cases need ui_page.h and are compiled only with it.
#pragma once
#include "ui/ui_diagnostics_page.h"
#if __has_include("ui/ui_page.h")
#include "ui/ui_page.h"
#define TURBO_HAVE_UI_PAGE 1
#endif

namespace ui_page_test {

using namespace turbo;

static void (*g_page_fn)(App&) = nullptr;
static void draw_extra(App& app) {
    if (!g_page_fn) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##uipage_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) g_page_fn(app);
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

#ifdef TURBO_HAVE_UI_PAGE
static int g_apply_hits = 0;
static bool g_fixture_with_apply = true;
static void fixture_page(App& app) {
    page::Frame f(app, {"fixture", "Fixture page", "One grey help line.", g_fixture_with_apply ? "Apply changes" : nullptr});
    ImGui::Button("Body button##fx");
    if (f.end()) {
        ++g_apply_hits;
        page::done("fixture", "applied the fixture");
    }
}
// x of an item's left edge, or -1 when it is not on screen
static float left_of(Ui& ui, const char* stable_id) {
    const ItemRec* r = ui.find(stable_id, "##uipage_test");
    return r ? r->rect.Min.x : -1.0f;
}
#endif

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    if (!app.connected()) ui.frames(5);
#ifdef TURBO_HAVE_UI_PAGE
    run_case("Page template: the status line is worded by one helper (pure)", [&] {
        page::reset_for_tests();
        CHECK(page::status_text("p").empty(), "no status at first");
        page::done("p", "saved 3 files");
        CHECK(page::status_text("p") == "Done: saved 3 files", "done: " + page::status_text("p"));
        page::couldnt(app, "p", "open the folder", "it does not exist yet");
        CHECK(page::status_text("p") == "Couldn't open the folder: it does not exist yet", "couldnt: " + page::status_text("p"));
        CHECK(page::status_text("other").empty(), "one status line per page");
        page::reset_for_tests();
    });

    run_case("Page template: title, help, body and the bar Apply / Undo / Back in that order", [&] {
        changes_page_test::Sandbox sb(app, ui);
        page::reset_for_tests();
        g_fixture_with_apply = true;
        show(ui, fixture_page);
        const page::Record* rec = page::last_record("fixture");
        CHECK(rec != nullptr && rec->title == "Fixture page" && rec->has_apply, "the frame recorded itself with an Apply");
        const float xa = left_of(ui, "###pg_apply"), xu = left_of(ui, "###pg_undo"), xb = left_of(ui, "###pg_back");
        CHECK(xa >= 0 && xu >= 0 && xb >= 0, fmt("all three buttons are on screen (%.0f %.0f %.0f)", xa, xu, xb));
        CHECK(xa < xu && xu < xb, "left to right: Apply, Undo, Back");
        CHECK(rec && rec->bar.size() == 3 && rec->bar[0] == "###pg_apply" && rec->bar[1] == "###pg_undo" && rec->bar[2] == "###pg_back", "drawing (= Tab) order");
        CHECK(ui.find("Body button##fx", "##uipage_test") != nullptr, "the body is drawn inside the frame");
        // the bar is below the body: it never scrolls away with it
        const ItemRec* body = ui.find("Body button##fx", "##uipage_test");
        const ItemRec* bar = ui.find("###pg_back", "##uipage_test");
        CHECK(body && bar && bar->rect.Min.y > body->rect.Max.y, "the bar sits under the body");
        // Apply is returned to the page and the page words the status line
        g_apply_hits = 0;
        CHECK(ui.click("###pg_apply", "##uipage_test"), "click Apply");
        ui.frames(2);
        CHECK(g_apply_hits == 1, fmt("Apply reached the page once (%d)", g_apply_hits));
        CHECK(page::status_text("fixture") == "Done: applied the fixture", "the status line: " + page::status_text("fixture"));
        // a page with nothing to apply has no Apply button
        g_fixture_with_apply = false;
        page::reset_for_tests();
        ui.frames(3);
        rec = page::last_record("fixture");
        CHECK(rec && !rec->has_apply && rec->bar.size() == 2 && ui.find("###pg_apply", "##uipage_test") == nullptr, "no Apply when the page has none");
        g_fixture_with_apply = true;
        hide(ui);
        page::reset_for_tests();
    });

    run_case("Page template: Undo is the Changes list's global Undo (no undo store of the page)", [&] {
        changes_page_test::Sandbox sb(app, ui);
        page::reset_for_tests();
        changes::poll(app, true);
        show(ui, fixture_page);
        const page::Record* rec = page::last_record("fixture");
        CHECK(rec && !rec->undo_enabled, "nothing to undo: the button is there and disabled");
        const int64_t pot0 = changes_page_test::read_int(app, 1002, "potential");
        CHECK(changes_page_test::write_int(app, 1002, "potential", pot0 == 70 ? 71 : 70), "make a change (as another page would)");
        changes::poll(app, true);
        ui.frames(2);
        rec = page::last_record("fixture");
        CHECK(rec && rec->undo_enabled, "the change enables Undo on a page that did nothing itself");
        const ItemRec* u = ui.find("###pg_undo", "##uipage_test");
        CHECK(u && u->label.find("Undo: ") == 0 && u->label.size() <= 28 + 12, "the label names the change: " + (u ? u->label : std::string("?")));
        CHECK(ui.click("###pg_undo", "##uipage_test"), "click Undo");
        ui.frames(2);
        CHECK(changes_page_test::read_int(app, 1002, "potential") == pot0, "the change was taken back");
        CHECK(page::status_text("fixture").find("Done: ") == 0, "the status line says so: " + page::status_text("fixture"));
        CHECK(changes::rows().empty(), "its row left the Changes list");
        hide(ui);
        page::reset_for_tests();
    });

    run_case("Page template: Back is a visible button; it goes through App::back and is disabled with no place to go back to", [&] {
        changes_page_test::Sandbox sb(app, ui);
        page::reset_for_tests();
        shell::back_place() = shell::Place{};
        show(ui, fixture_page);
        const page::Record* rec = page::last_record("fixture");
        CHECK(rec && !rec->back_enabled && ui.find("###pg_back", "##uipage_test") != nullptr, "drawn and disabled with nothing to go back to");
        const shell::Place here = app.place();
        const shell::Place target = here.page == shell::Page::Standings ? shell::Place{shell::Area::Squad, shell::Page::Squad}
                                                                         : shell::Place{shell::Area::Career, shell::Page::Standings};
        shell::back_place() = target;
        ui.frames(2);
        rec = page::last_record("fixture");
        CHECK(rec && rec->back_enabled, "enabled once a place to go back to exists");
        CHECK(ui.click("###pg_back", "##uipage_test"), "click Back");
        g_page_fn = nullptr;  // the fixture window must not take focus from the page we land on
        g_ui_extra = nullptr;
        ui.frames(4);
        CHECK(app.place().page == target.page, "Back landed on the page it came from");
        CHECK(!shell::back_place().valid(), "one level: the back place is cleared");
        page::reset_for_tests();
    });

    // Round 1 item 3: Data export's error paths used to go through App::notify(text, true), which also writes "ERROR: ..." to the log
    // (app.cpp notify). The template's couldnt() must leave the same trace.
    run_case("Page template: couldnt() writes the same ERROR line to the App log that notify(error) wrote", [&] {
        page::reset_for_tests();
        page::couldnt(app, "p", "open the folder", "it does not exist yet (log-test-1)");
        CHECK(!app.log_lines.empty() && app.log_lines.back() == "ERROR: Couldn't open the folder: it does not exist yet (log-test-1)",
              "log line: " + (app.log_lines.empty() ? std::string("<empty>") : app.log_lines.back()));
        app.notify("parity (log-test-2)", true);
        CHECK(!app.log_lines.empty() && app.log_lines.back() == "ERROR: parity (log-test-2)", "notify(error) wrote the same prefix: " + app.log_lines.back());
        const std::string last = app.log_lines.back();
        page::done("p", "saved 3 files");
        CHECK(app.log_lines.back() == last, "done() writes no log line (notify(text) did not either)");
        page::reset_for_tests();
    });
#endif

    // Outside the guard: FAILS on the 3.1.0 base (no template, no bar), passes with it
    run_case("Advanced > Data export (template page): the bar with Undo and Back, no Apply; the folder button and Exports buttons stay", [&] {
        changes_page_test::Sandbox sb(app, ui);
        diagnostics_reset_for_tests();
        shell::back_place() = shell::Place{};
        show(ui, draw_data_export_page);
        CHECK(diagnostics_state().export_page_drawn, "the page ran");
        CHECK(ui.find("###pg_undo", "##uipage_test") != nullptr, "the template's Undo button");
        CHECK(ui.find("###pg_back", "##uipage_test") != nullptr, "the template's Back button");
        CHECK(ui.find("###pg_apply", "##uipage_test") == nullptr, "no Apply: this page stages nothing");
        CHECK(ui.find("Open the export folder##dataexport", "##uipage_test") != nullptr, "the folder button");
        if (app.mailbox) {
            for (const char* b : {"Season stats", "Fixtures & results", "Transfer history", "Jersey numbers", "Export tables", "Probe report"})
                CHECK(ui.find(b, "##uipage_test") != nullptr, std::string("export button: ") + b);
        }
#ifdef TURBO_HAVE_UI_PAGE
        const page::Record* rec = page::last_record("data_export");
        CHECK(rec && rec->title == "Data export" && !rec->has_apply && rec->bar.size() == 2, "the frame recorded the page");
#endif
        hide(ui);
    });
}

}  // namespace ui_page_test
