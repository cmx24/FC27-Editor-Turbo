// Native tests (4.0 GUI overhaul, slice S3 shell; cut down 2026-10-31): shell::page_has_frame against the pages that really draw the
// page template's bar. Included by test_main.cpp after test_ui_page.h; shell40_test::ui_cases(app, ui) runs inside the UI block (a real App,
// the pages drawn through the new-page registry, so before test_shell_app clears the old one). Cases are named "Shell 4.0: ..." so
// TURBO_TEST_ONLY="shell 4.0" runs them alone.
//
// 2026-10-31 (5.0c menus step 6): the 4.0 preview tree (shell::tree40(), the Players / Managers card, Team > Squad), "Use the old pages"
// (App::legacy_pages) and the Lab (GuiPrefs::advanced_mode) were deleted, and with them every case that switched them on. What is left is
// the one guard ui_shell.cpp asks for next to page_has_frame: a page listed there draws "###pg_back", a page not listed draws none.
#pragma once
#include <string>

#include "ui/ui_page.h"
#include "ui/ui_shell.h"

namespace shell40_test {

using namespace turbo;
using shell::Area;
using shell::Page;

// Restores every process-wide value a case touches, so the cases after this one (and the rest of the suite) see what they saw before.
struct State {
    App& app;
    Ui& ui;
    int tab;
    shell::Place back;
    State(App& a, Ui& u) : app(a), ui(u), tab(shell::player_edit_tab()), back(shell::back_place()) {}
    ~State() {
        shell::player_edit_tab() = tab;
        shell::back_place() = back;
        app.go(Page::Home);
        ui.frames(3);
    }
};

static void ui_cases(App& app, Ui& ui) {
    using namespace shell;

    run_case("Shell 4.0: page_has_frame matches the pages that draw the frame's bar", [&] {
        State st(app, ui);
        int compared = 0;
        for (int a = 0; a < kAreaCount; ++a) {
            const Area ar = static_cast<Area>(a);
            if (!area_visible(ar)) continue;
            for (Page p : pages_of(ar)) {
                app.go(Page::Home);
                ui.frames(3);
                app.go(p);
                ui.frames(4);
                if (!(app.place() == place_of(p))) continue;  // a page App::go does not land on is not drawn: nothing to compare
                ++compared;
                const bool framed = ui.find("###pg_back") != nullptr;
                CHECK(framed == page_has_frame(p), std::string(page_label(p)) + ": page_has_frame says " + (page_has_frame(p) ? "yes" : "no") +
                                                       ", the page draws " + (framed ? "a frame bar" : "none"));
            }
        }
        CHECK(compared > 5, "the sweep compared pages (" + std::to_string(compared) + ")");
    });
}

}  // namespace shell40_test
