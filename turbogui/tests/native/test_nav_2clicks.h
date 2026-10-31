// 5.0c E16 two-clicks (2026-10-31). Statements, not functions: included inside the UI test function right after test_integration_50b.h
// (test_main.cpp), like it. The navigation-capability check (tools/nav_capability_check.py, NAVCAP) is a static scan and cannot count
// clicks, so the 2-click bound of the game-gui-architect TURBO OVERRIDE rule 6 is asserted here by WALKING it, with real clicks on a real frame:
//   (1) every page of every visible area is reached from Home in at most 2 clicks (the rail, then the page strip) and shows exactly one Back
//       (the shell's "< Back to X###nav_back" or the page frame's ###pg_back, E16-G1 D2);
//   (2) every row of briefs\5.0c\INGAME_CHECKS.md that names a Home route is walked from Home with its own clicks, the clicks are counted and
//       the count must be <= 2, and the feature's marker must be on screen after the last click.
// BEHAVIOURAL: before this change the manager's 3D-model miniface row was Home > Manager looks > Miniface > The 3D model (3 clicks, INGAME_CHECKS
// E9, "one over the override"). Home had no "Manager miniface (3D)" tile, so row E9 below cannot find its first click and fails.
// 2026-10-31 (5.0c menus step 6): the 4.0 tree (shell::tree40(), the Players / Managers card) is gone; Home's looks tiles open the Appearance
// Editor (ui_home.cpp open_looks), whose cards hold the real-face chooser and the whole miniface editor (test_ia_222.h).
run_case("test_nav_2clicks: every INGAME_CHECKS Home route is <= 2 clicks and lands on its marker; every page is <= 2 clicks from Home with one Back", [&] {
    const int was_tab = shell::player_edit_tab();
    const int was_mtab = shell::manager_edit_tab();
    const bool was_req = shell::miniface_3d_request();
    const int was_sel = app.sel_manager;
    const shell::Place was_back = shell::back_place();
    shell::PageFn saved[shell::kPageCount] = {};
    for (int i = 1; i < shell::kPageCount; ++i) saved[i] = shell::registered_page(static_cast<shell::Page>(i));
    shell::clear_registry_for_tests();  // as test_integration_50b.h: the unfiltered suite arrives with an emptied registry, a filtered run must prove the same path
    shell::register_page(shell::Page::Home, draw_home);
    shell::register_page(shell::Page::LooksStudio, turbo::looks::draw_looks_studio);
    ui.big_window();
    app.sel_manager = 0;
    shell::miniface_3d_request() = false;

    // ---- (1) every page: <= 2 clicks from Home, one Back
    int walked = 0, over_two = 0;
    for (int a = 0; a < shell::kAreaCount; ++a) {
        const shell::Area ar = static_cast<shell::Area>(a);
        if (!shell::area_visible(ar)) continue;
        for (shell::Page p : shell::pages_of(ar)) {
            if (p == shell::Page::Home) continue;
            if (!shell::page_visible(p)) continue;
            const std::string what = std::string(shell::area_label(ar)) + " > " + shell::page_label(p) + ": ";
            app.go(shell::Page::Home);
            ui.frames(4);
            int clicks = 0;
            // click 1: the rail entry of the area; click 2 (only when the page is not the area's first): the page in the strip
            CHECK(ui.click(shell::area_label(ar), "FC 27 LE Turbo", "##shell"), what + "the rail entry is on screen");
            ++clicks;
            ui.frames(3);
            if (!(app.place() == shell::place_of(p))) {
                CHECK(ui.click(shell::page_label(p), "FC 27 LE Turbo", "##pages"), what + "the page is in the strip of its area");
                ++clicks;
                ui.frames(3);
            }
            CHECK(app.place() == shell::place_of(p), what + "the clicks landed on the page");
            CHECK(clicks <= 2, what + "reached in " + std::to_string(clicks) + " clicks (bound 2)");
            if (clicks > 2) ++over_two;
            // E16-G1 D2: Back is on every page that was reached from somewhere (the frame's Back or the shell's, exactly one)
            const bool framed = ui.find("###pg_back") != nullptr;
            const bool shell_back = ui.find("###nav_back") != nullptr;
            CHECK(framed || shell_back, what + "a Back is on screen (frame's " + (framed ? "yes" : "no") + ", shell's " + (shell_back ? "yes" : "no") + ")");
            CHECK(!(framed && shell_back), what + "exactly one Back, not two");
            ++walked;
        }
    }
    CHECK(walked >= 10, "the sweep walked the pages of the visible areas (" + std::to_string(walked) + ")");
    CHECK(over_two == 0, "no page is more than 2 clicks from Home (" + std::to_string(over_two) + " over)");

    // ---- (2) INGAME_CHECKS rows with a Home route (id, the clicks from Home, a marker on screen after the last click)
    struct Step {
        const char* label;
        const char* win;
        const char* scope;
    };
    struct Row {
        const char* id;       // INGAME_CHECKS id
        std::vector<Step> clicks;
        shell::Page lands;    // the page the route ends on
        const char* marker;   // a label on screen after the last click ("" = the page alone)
        const char* marker_win;
    };
    const std::vector<Row> rows = {
        {"S2 Player looks", {{"###home_player_looks", "", ""}}, shell::Page::LooksStudio, "###lsmine", ""},
        {"S2 Manager looks", {{"###home_manager_looks", "", ""}}, shell::Page::LooksStudio, "", ""},
        {"E7 manager real-face picker button (Manager looks lands on the Appearance Editor)", {{"###home_manager_looks", "", ""}}, shell::Page::LooksStudio, "Choose a real face...###lsface", ""},
        // E9: was Home > Manager looks > Miniface > The 3D model (3 clicks); now one tile that opens the Appearance Editor with the Miniface card's 3D-model source asked for
        {"E9 manager miniface, The 3D model", {{"###home_manager_miniface", "", ""}}, shell::Page::LooksStudio, "", ""},
        {"V0 Settings version line", {{shell::area_label(shell::Area::Settings), "FC 27 LE Turbo", "##shell"}}, shell::Page::Settings, "", ""},
    };
    for (const Row& r : rows) {
        const std::string what = std::string(r.id) + ": ";
        shell::manager_edit_tab() = 0;
        shell::player_edit_tab() = 0;
        shell::miniface_3d_request() = false;
        app.go(shell::Page::Home);
        ui.frames(5);
        int clicks = 0;
        bool ok = true;
        for (const Step& s : r.clicks) {
            if (!ui.click(s.label, s.win, s.scope)) ok = false;
            ++clicks;
            ui.frames(6);
        }
        CHECK(ok, what + "every click of the route found its control on screen");
        CHECK(clicks <= 2, what + "route is " + std::to_string(clicks) + " clicks from Home (bound 2)");
        CHECK(app.place() == shell::place_of(r.lands), what + "the route ended on its page");
        if (r.marker[0]) CHECK(ui.find(r.marker, r.marker_win) != nullptr, what + "its marker '" + r.marker + "' is on screen after the last click");
    }

    shell::clear_registry_for_tests();
    for (int i = 1; i < shell::kPageCount; ++i)
        if (saved[i]) shell::register_page(static_cast<shell::Page>(i), saved[i]);
    shell::player_edit_tab() = was_tab;
    shell::manager_edit_tab() = was_mtab;
    shell::miniface_3d_request() = was_req;
    shell::back_place() = was_back;
    app.sel_manager = was_sel;
    turbo::looks::looks_studio_show_managers(false);
    app.go(shell::Page::Home);
    ui.frames(3);
});
