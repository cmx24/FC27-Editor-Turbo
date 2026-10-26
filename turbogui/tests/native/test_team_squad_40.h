// Native tests (4.0 GUI overhaul, slice S4): Team > Squad (ui/ui_team_squad_page.h, draw_team_squad_page in ui/ui_teams.cpp).
// Included by test_main.cpp after test_ui_page.h; team_squad_40_test::ui_cases runs inside the UI block (a real App on the simulated
// game) and draws the page through the Ui hook g_ui_extra of test_ui_page.h, so it does not need the 4.0 shell tree (S3 registers it).
//
// What is guarded (offline; nothing here is proven in game):
//   - the page is a page::Frame page ("team_squad", title "Squad", no Apply, bar Undo + Back) holding the club picker and squad_table;
//   - the table is the existing one: table "##squad", seven columns Jersey, Player, Pos, OVR, Age, Line-up slot, Calc OVR (4.0 calc-ovr rebase: 2.22 calc-ovr appended the seventh), Sortable + tristate,
//     resize / reorder / hide on; Age and OVR sort as on the old pages (test_squad_table_restore.h tests the table itself);
//   - the club picker drives the table (My club, a row of the list) and an unknown club defaults to the user's club;
//   - a click on a player's name opens Players > Edit a player;
//   - the table id is the same for any club and across re-opening the page, and ImGui's layout settings hold the table after a sort,
//     which is how widths / order / sort reach turbo_gui_layout_v2.ini. This is an offline approximation: a reboot is NOT run here.
// The cases sit behind __has_include on purpose: on the base (64242633, no page function) the #else case FAILS; with the page they pass.
#pragma once
#if __has_include("ui/ui_team_squad_page.h")
#include "ui/ui_team_squad_page.h"
#define TURBO_HAVE_TEAM_SQUAD_40 1
#endif

namespace team_squad_40_test {

using namespace turbo;

#ifdef TURBO_HAVE_TEAM_SQUAD_40
static void page_fn(App& app) { draw_team_squad_page(app); }

// squad_table_state() is reset only inside squad_table(), so it is read as "this frame's" only while the ##squad table is on screen
static bool on_screen() { return squad_table_test::squad_table_now() != nullptr; }

static std::string ini_text() {
    size_t n = 0;
    const char* s = ImGui::SaveIniSettingsToMemory(&n);
    return std::string(s ? s : "", n);
}
static std::string table_section_header(const ImGuiTable* t) {
    char b[48];
    std::snprintf(b, sizeof(b), "[Table][0x%08X,%d]", static_cast<unsigned>(t->ID), t->ColumnsCount);
    return b;
}
#endif

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
#ifdef TURBO_HAVE_TEAM_SQUAD_40
    using shell::Page;
    const bool was_advanced = app.prefs.advanced_mode;
    const bool was_legacy = app.legacy_pages;
    const int64_t old_sel_team = app.sel_team;
    const int64_t old_sel_player = app.sel_player;
    const int64_t mine = app.bridge.state().user_team;
    app.prefs.advanced_mode = false;
    app.legacy_pages = false;
    if (!app.connected()) ui.frames(5);

    run_case("Team squad 40: the page is a page::Frame page (Squad, no Apply, bar Undo + Back) with the picker and the seven-column table", [&] {
        page::reset_for_tests();
        app.go(Page::Standings);  // a shell page that draws no squad table, so only the page under test does
        ui.frames(2);
        app.sel_team = 241;
        ui_page_test::show(ui, page_fn);
        const page::Record* rec = page::last_record("team_squad");
        CHECK(rec != nullptr && rec->title == "Squad" && !rec->has_apply, "the frame recorded itself: title Squad, no Apply");
        CHECK(rec && rec->bar.size() == 2 && rec->bar[0] == "###pg_undo" && rec->bar[1] == "###pg_back", "the bar is Undo then Back");
        const SquadTableState& st = squad_table_state();
        CHECK(on_screen() && st.table_drawn && st.club == 241 && st.rows == 3, fmt("the table is on screen and shows the picked club: club %lld rows %zu", static_cast<long long>(st.club), st.rows));
        CHECK(ui.find("My club", "##uipage_test") != nullptr, "the club picker is on the page (My club)");
        const ImGuiTable* t = squad_table_test::squad_table_now();
        CHECK(t != nullptr, "the ##squad table is on screen");
        if (t) {
            CHECK(t->ColumnsCount == 7 && st.columns == 7, fmt("seven columns (4.0 calc-ovr rebase: Calc OVR appended): %d", t->ColumnsCount));
            CHECK((t->Flags & ImGuiTableFlags_Sortable) != 0 && (t->Flags & ImGuiTableFlags_SortTristate) != 0, "Sortable and tristate");
            CHECK((t->Flags & ImGuiTableFlags_Resizable) != 0 && (t->Flags & ImGuiTableFlags_Reorderable) != 0 && (t->Flags & ImGuiTableFlags_Hideable) != 0,
                  "column resize / reorder / hide are on");
            const char* want[] = {"Jersey", "Player", "Pos", "OVR", "Age", "Line-up slot", "Calc OVR"};
            for (int i = 0; i < 7 && i < t->ColumnsCount; ++i)
                CHECK(std::string(ImGui::TableGetColumnName(t, i)) == want[i], std::string("column ") + want[i]);
        }
        CHECK(squad_table_test::drawn_order(app, 241) == squad_table_test::db_order(app, 241), "no header clicked: the database order");
        ui_page_test::hide(ui);
        page::reset_for_tests();
    });

    run_case("Team squad 40: the picker drives the table; an unknown club opens on the user's club", [&] {
        app.go(Page::Standings);
        ui.frames(2);
        app.sel_team = 0;
        ui_page_test::show(ui, page_fn);
        CHECK(mine > 0 && app.sel_team == mine && on_screen() && squad_table_state().club == mine, fmt("no club picked: my club is shown (%lld)", static_cast<long long>(squad_table_state().club)));
        // a row of the club list picks that club
        const ItemRec* row = ui.find("241", "##teams");
        CHECK(row != nullptr, "Inter is a row of the club list on the page");
        if (row) {
            ui.click(row);
            ui.frames(3);
            CHECK(app.sel_team == 241 && on_screen() && squad_table_state().club == 241 && squad_table_state().rows == 3, "a click on the row shows that club's squad");
        }
        CHECK(ui.click("My club", "##uipage_test"), "click My club");
        ui.frames(3);
        CHECK(app.sel_team == mine && on_screen() && squad_table_state().club == mine, "My club returns to the user's club");
        ui_page_test::hide(ui);
        page::reset_for_tests();
    });

    run_case("Team squad 40: Age and OVR sort on the page as on the old pages; a click on a name opens Edit a player", [&] {
        app.go(Page::Standings);
        ui.frames(2);
        app.sel_team = mine;
        ui_page_test::show(ui, page_fn);
        const SquadTableState& st = squad_table_state();
        CHECK(on_screen() && st.table_drawn && st.club == mine && st.rows >= 5, "my club's squad is on the page");
        const std::vector<int64_t> base = squad_table_test::db_order(app, mine);
        CHECK(squad_table_test::drawn_order(app, mine) == base, "database order until a header is clicked");
        CHECK(squad_table_test::click_header(ui, "Age"), "the Age header");
        CHECK(st.sort_column == 4 && st.sort_ascending, "sorted on Age ascending");
        CHECK(squad_table_test::drawn_order(app, mine) == squad_table_test::expected_order(app, mine, true, true), "by age, youngest first");
        CHECK(squad_table_test::click_header(ui, "Age") && !st.sort_ascending, "Age again reverses");
        CHECK(squad_table_test::drawn_order(app, mine) == squad_table_test::expected_order(app, mine, true, false), "by age, oldest first");
        CHECK(squad_table_test::click_header(ui, "Age") && st.sort_column == -1, "a third click returns to the database order");
        CHECK(squad_table_test::click_header(ui, "OVR"), "the OVR header");
        CHECK(st.sort_column == 3 && !st.sort_ascending, "OVR sorts high to low first");
        CHECK(squad_table_test::drawn_order(app, mine) == squad_table_test::expected_order(app, mine, false, false), "by overall, best first");
        CHECK(squad_table_test::click_header(ui, "OVR"), "OVR again");
        CHECK(squad_table_test::click_header(ui, "OVR"), "OVR a third time");
        CHECK(st.sort_column == -1 && squad_table_test::drawn_order(app, mine) == base, "back to the database order");
        // a click on a name goes to Players > Edit a player
        const std::string nm = app.model.player_name(1005);
        CHECK(ui.find(nm, "##squad") != nullptr, "Gabriel Jesus is in the table: " + nm);
        CHECK(ui.click(nm, "##squad"), "click his name");
        ui.frames(4);
        CHECK(app.sel_player == 1005 && app.place() == shell::place_of(Page::PlayerEdit), "Edit a player opens with him");
        ui_page_test::hide(ui);
        page::reset_for_tests();
    });

    run_case("Team squad 40: one stable table id for any club; ImGui's layout settings hold the table after a sort (offline, no reboot)", [&] {
        app.go(Page::Standings);
        ui.frames(2);
        app.sel_team = mine;
        ui_page_test::show(ui, page_fn);
        const ImGuiTable* t0 = squad_table_test::squad_table_now();
        CHECK(t0 != nullptr, "the table is drawn");
        if (!t0) { ui_page_test::hide(ui); return; }
        const ImGuiID id0 = t0->ID;
        // the same table for another club
        app.sel_team = 241;
        ui.frames(3);
        const ImGuiTable* t1 = squad_table_test::squad_table_now();
        CHECK(t1 != nullptr && t1->ID == id0, "another club: the same table id (one saved layout for every club)");
        // sort, then the layout settings (what turbo_gui_layout_v2.ini is written from) hold this table with its column count
        app.sel_team = mine;
        ui.frames(3);
        CHECK(squad_table_test::click_header(ui, "OVR"), "sort on OVR");
        const ImGuiTable* t2 = squad_table_test::squad_table_now();
        CHECK(t2 != nullptr && t2->ID == id0, "after the sort: still the same id");
        const std::string ini = ini_text();
        CHECK(t2 != nullptr && ini.find(table_section_header(t2)) != std::string::npos, "the layout settings hold the table: " + (t2 ? table_section_header(t2) : std::string("?")));
        // the page closed and opened again: same id, and the sort is still there
        ui_page_test::hide(ui);
        ui_page_test::show(ui, page_fn);
        const ImGuiTable* t3 = squad_table_test::squad_table_now();
        CHECK(t3 != nullptr && t3->ID == id0, "re-opened: the same id");
        CHECK(on_screen() && squad_table_state().sort_column == 3 && !squad_table_state().sort_ascending, "re-opened: the sort on OVR is still on");
        CHECK(squad_table_test::click_header(ui, "OVR"), "OVR again");
        CHECK(squad_table_test::click_header(ui, "OVR"), "OVR a third time (reset)");
        ui_page_test::hide(ui);
        page::reset_for_tests();
    });

    run_case("Team squad 40: the stale-data guard - with the teams table moved (another save loaded) no squad table is drawn and the page says Press Refresh", [&] {
        app.go(Page::Standings);
        ui.frames(2);
        app.sel_team = 241;
        ui_page_test::show(ui, page_fn);
        CHECK(on_screen() && squad_table_state().club == 241 && !squad_table_state().stale_notice, "database alive: the table is on screen, no notice");
        const Table* tt = app.db.table("teams");
        const TeamRow* tr = app.model.team(241);
        CHECK(tt != nullptr && tr != nullptr, "the teams table and Inter's row exist");
        if (!tt || !tr) { ui_page_test::hide(ui); return; }
        // the way the stale-table test of test_main.cpp moves the database: the header no longer describes the table
        uint32_t shortname = 0;
        app.mem.rd(tt->header + 0x40, shortname);
        const uint32_t other = 0x58585858;  // "XXXX"
        app.mem.wr(tt->header + 0x40, other);
        ui.frames(3);
        const bool alive_now = app.db.table_alive(*tt, tr->rec);
        const bool table_on_screen = on_screen();
        const SquadTableState after = squad_table_state();
        app.mem.wr(tt->header + 0x40, shortname);  // put the world back before any check can end the case
        CHECK(!alive_now, "the header changed: the team row is not alive");
        CHECK(!table_on_screen, "no ##squad table is drawn on a database that moved (its Jersey and Line-up slot cells would write into freed memory)");
        CHECK(after.stale_notice && after.club == 241 && !after.table_drawn, "the page shows the Refresh message instead of the table");
        ui.frames(3);
        CHECK(app.db.table_alive(*tt, tr->rec) && on_screen() && !squad_table_state().stale_notice, "database back: the table is drawn again, no notice");
        ui_page_test::hide(ui);
        page::reset_for_tests();
    });

    app.sel_team = old_sel_team;
    app.sel_player = old_sel_player;
    app.legacy_pages = was_legacy;
    app.prefs.advanced_mode = was_advanced;
#else
    run_case("Team squad 40: the page function exists (ui/ui_team_squad_page.h, draw_team_squad_page)", [&] {
        CHECK(false, "ui/ui_team_squad_page.h is missing: the base has no Team > Squad page function");
    });
    (void)app;
    (void)ui;
#endif
}

}  // namespace team_squad_40_test
