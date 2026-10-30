// 5.0b integration (Integrator wiring, 2026-10-30). Statements, not functions: included inside the UI test function right after
// test_look_pickers.h (test_main.cpp), like it. Track A (5.0b/pickers) asked the Integrator for two wirings in ui_teams.cpp, a file its own merge
// gate kept it out of (5.0b-A RESULTS.md:80): (1) the manager tabs (##mtabs) follow shell::manager_edit_tab(), so Home's "Manager looks" tile
// lands on Appearance; (2) A's ONE Look card (looks::look_card) is drawn on the manager's Appearance tab.
// BEHAVIOURAL: on the tree before the wiring the click lands on the Details tab the tab bar opens with, no Look card is drawn there, and
// a later click on Details leaves manager_edit_tab() at 1 (nothing writes it back), so a second tile click would not move the tab.
run_case("test_integration_50b (w): Home's Manager looks opens the manager's Appearance tab with the Look card; the tab is free again after one frame (4.0 tree)", [&] {
    const bool was_tree = shell::tree40();
    const bool was_legacy = app.legacy_pages;
    const bool was_adv = app.prefs.advanced_mode;
    const int was_tab = shell::player_edit_tab();
    const int was_who = shell::player_edit_who();
    const int was_mtab = shell::manager_edit_tab();
    const int was_sel = app.sel_manager;
    // same registry handling as test_look_pickers (e): the unfiltered suite arrives with an emptied registry (test_gameplay_page.h:74)
    shell::PageFn saved[shell::kPageCount] = {};
    for (int i = 1; i < shell::kPageCount; ++i) saved[i] = shell::registered_page(static_cast<shell::Page>(i));
    shell::clear_registry_for_tests();
    shell::register_page(shell::Page::Home, draw_home);
    shell::register_page(shell::Page::LooksStudio, turbo::looks::draw_looks_studio);
    app.legacy_pages = false;
    app.prefs.advanced_mode = false;
    ui.big_window();
    shell::tree40() = true;
    CHECK(app.db.table("manager") != nullptr, "the test world has the manager table");
    app.sel_manager = 0;
    shell::player_edit_tab() = 0;
    shell::player_edit_who() = 0;   // start on the Players switch and the Details tab: a click that presets nothing is caught
    shell::manager_edit_tab() = 0;
    auto open_from_home = [&](const std::string& round) {
        app.go(shell::Page::Home);
        ui.frames(5);
        CHECK(ui.click("###home_manager_looks"), round + "click Home's Manager looks tile");
        ui.frames(6);
        CHECK(app.place() == shell::place_of(shell::Page::PlayerEdit), round + "it opened the Player page");
        CHECK(ui.find("Details", "##medit") != nullptr, round + "the manager's tab bar is drawn (Managers switch, manager 0 selected)");
    };
    open_from_home("round 1: ");
    CHECK(shell::manager_edit_tab() == 1, "round 1: Home asked for the Appearance tab");
    CHECK(ui.find("Choose a real face...", "##medit") != nullptr, "round 1: the Appearance tab is open (its real-face button is on screen)");
    const int cards0 = turbo::looks::studio_state().look_cards;
    ui.frames(3);
    CHECK(turbo::looks::studio_state().look_cards - cards0 >= 3, "round 1: A's Look card is drawn on the manager's Appearance tab, every frame");
    // the request is used once: the person can leave the tab, and the leaving is written back
    CHECK(ui.click("Details", "##medit"), "click the Details tab");
    ui.frames(3);
    CHECK(ui.find("Choose a real face...", "##medit") == nullptr, "Details is open after the click (Appearance is not forced every frame)");
    CHECK(shell::manager_edit_tab() == 0, "the tab the person picked is written back to manager_edit_tab()");
    open_from_home("round 2: ");
    CHECK(ui.find("Choose a real face...", "##medit") != nullptr, "round 2: the tile moves the tab to Appearance again");
    // leave the manager tabs on Details with nothing pending, for the cases after this one
    CHECK(ui.click("Details", "##medit"), "end: back to Details");
    ui.frames(3);
    shell::clear_registry_for_tests();
    for (int i = 1; i < shell::kPageCount; ++i)
        if (saved[i]) shell::register_page(static_cast<shell::Page>(i), saved[i]);
    shell::tree40() = was_tree;
    app.legacy_pages = was_legacy;
    app.prefs.advanced_mode = was_adv;
    shell::player_edit_tab() = was_tab;
    shell::player_edit_who() = was_who;
    shell::manager_edit_tab() = was_mtab;
    app.sel_manager = was_sel;
    turbo::looks::looks_studio_show_managers(false);
    app.go(shell::Page::Home);
    ui.frames(3);
});
