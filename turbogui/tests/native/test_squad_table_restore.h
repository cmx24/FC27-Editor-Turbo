// Native tests (2.22 G-squad, brief 2.22-G-squad): the sortable squad table of 2.14 is back on both default pages. Included by test_main.cpp
// after test_page_team.h; squad_table_test::ui_cases runs inside the UI block (a real App on the simulated game).
//
// What is proven here, through the real shell (App::draw_pages, Settings "Use the old pages" OFF), the way a user reaches the pages:
//   (a) Players > My squad and Team > Players both open on a tab that draws squad_table (the default, non-legacy route);
//   (b) the table has the six columns Jersey, Player, Pos, OVR, Age, Line-up slot and is Sortable;
//   (c) a click on Age orders the rows by age (then reverses, then returns to the database order), a click on OVR orders them high to low;
//   (d) a click on a player's name goes to Players > Edit a player (Page::PlayerEdit) with that player, not through the legacy request_tab;
//   (e) a Jersey edit writes teamplayerlinks.jerseynumber of that player's row through App::edit (logged, no other row changes);
//   (f) outside a career My squad shows the club picker and the table of the picked club, never a bare "waiting" line;
//   (g) the second tab of My squad, Career tools, still holds the five cards (test_page_career.h walks them);
//   (h) four more v2.14 capabilities are on the default route: Team > Players > All clubs, Transfers & money > More tools, Managers & staff >
//       All managers (table), Changes & undo > kept player edits.
// Cases are named "Squad table: ..." so TURBO_TEST_ONLY="squad table" runs them alone.
#pragma once
#include "ui/ui_team_pages.h"
#include "ui/ui_shell.h"

#include <algorithm>
#include <cstring>

namespace squad_table_test {

using namespace turbo;

static void write_state(App& app, Ui& ui, const fs::path& le, json st) {
    static int bumps = 0;
    const fs::path state_file = le / "turbo_output" / "bridge_state.json";
    st["seq"] = st.value("seq", 0LL) + 1;
    {
        std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
        f << st.dump();
    }
    fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(1500 + 2 * ++bumps));
    app.next_poll = 0.0;
    ui.frames(3);
}

// The players of a club in the order the table draws them: the name items of the "##squad" table sorted by their y position
static std::vector<int64_t> drawn_order(App& app, int64_t club) {
    std::map<std::string, int64_t> by_name;
    for (const LinkRow& l : app.model.links_of_team(club)) by_name[app.model.player_name(l.playerid)] = l.playerid;
    std::vector<std::pair<float, int64_t>> rows;
    for (const auto& kv : g_items) {
        const ItemRec& r = kv.second;
        if (r.frame != g_frame || r.window.find("##squad") == std::string::npos) continue;
        auto it = by_name.find(r.label);
        if (it != by_name.end()) rows.push_back({r.rect.Min.y, it->second});
    }
    std::sort(rows.begin(), rows.end());
    std::vector<int64_t> out;
    for (const auto& r : rows) out.push_back(r.second);
    return out;
}

// The database order of a club, then stably sorted by `key` (the comparison the table uses: ascending, or descending for the second click)
static std::vector<int64_t> expected_order(App& app, int64_t club, bool by_age, bool ascending) {
    std::vector<const PlayerRow*> rows;
    for (const LinkRow& l : app.model.links_of_team(club))
        if (const PlayerRow* p = app.model.player(l.playerid)) rows.push_back(p);
    std::stable_sort(rows.begin(), rows.end(), [&](const PlayerRow* a, const PlayerRow* b) {
        const int x = by_age ? a->age : a->overall, y = by_age ? b->age : b->overall;
        return ascending ? x < y : x > y;
    });
    std::vector<int64_t> out;
    for (const PlayerRow* p : rows) out.push_back(p->playerid);
    return out;
}
static std::vector<int64_t> db_order(App& app, int64_t club) {
    std::vector<int64_t> out;
    for (const LinkRow& l : app.model.links_of_team(club)) out.push_back(l.playerid);
    return out;
}

static std::string ids(const std::vector<int64_t>& v) {
    std::string s;
    for (int64_t x : v) s += (s.empty() ? "" : ",") + std::to_string(x);
    return s;
}

// The ImGui table "##squad" drawn this frame (its inner window carries the name)
static ImGuiTable* squad_table_now() {
    ImGuiContext& g = *GImGui;
    for (int i = 0; i < g.Tables.GetMapSize(); ++i) {
        ImGuiTable* t = g.Tables.TryGetMapData(i);
        if (t && t->LastFrameActive >= g.FrameCount - 1 && t->InnerWindow && std::strstr(t->InnerWindow->Name, "##squad")) return t;
    }
    return nullptr;
}

static bool click_header(Ui& ui, const char* name) {
    const ItemRec* h = ui.find(name, "##squad");
    if (!h) return false;
    ui.click(h);
    ui.frames(3);
    return true;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    using shell::Page;
    const json original_state = read_json(le / "turbo_output" / "bridge_state.json");
    const bool was_legacy = app.legacy_pages;
    const bool was_advanced = app.prefs.advanced_mode;
    const int64_t old_sel_team = app.sel_team;
    const int64_t old_sel_player = app.sel_player;
    const int was_tab = shell::player_edit_tab();
    const int64_t mine = app.bridge.state().user_team;
    app.prefs.advanced_mode = false;
    app.legacy_pages = false;  // the new pages (the cases of the other tracks run with the old ones)

    run_case("Squad table: Team > Players opens on the Squad list tab (any club), six sortable columns", [&] {
        const TeamRow* inter = app.model.team(241);
        CHECK(inter != nullptr && app.model.links_of_team(241).size() == 3, "the test world has Inter with three players");
        app.sel_team = 241;
        CHECK(ui.open_page("Club", "Squad") && app.place() == shell::place_of(Page::TeamPlayers), "Team > Players opens");
        ui.frames(3);
        const SquadTableState& st = squad_table_state();
        CHECK(st.table_drawn && st.club == 241 && st.rows == 3, fmt("the Squad list tab is the one shown: club %lld rows %zu", static_cast<long long>(st.club), st.rows));
        CHECK(ui.find("###tptabsquadlist", "FC 27 LE Turbo") != nullptr, "the tab is called Squad list");
        const ImGuiTable* t = squad_table_now();
        CHECK(t != nullptr, "the ##squad table is on screen");
        if (t) {
            CHECK(t->ColumnsCount == 6 && st.columns == 6, fmt("six columns: %d", t->ColumnsCount));
            CHECK((t->Flags & ImGuiTableFlags_Sortable) != 0 && (t->Flags & ImGuiTableFlags_SortTristate) != 0, "Sortable and tristate");
            CHECK((t->Flags & ImGuiTableFlags_Resizable) != 0 && (t->Flags & ImGuiTableFlags_Reorderable) != 0 && (t->Flags & ImGuiTableFlags_Hideable) != 0,
                  "column resize / reorder / hide are on");
            const char* want[] = {"Jersey", "Player", "Pos", "OVR", "Age", "Line-up slot"};
            for (int i = 0; i < 6 && i < t->ColumnsCount; ++i) CHECK(std::string(ImGui::TableGetColumnName(t, i)) == want[i], std::string("column ") + want[i]);
        }
        CHECK(drawn_order(app, 241) == db_order(app, 241), "no header clicked: the database order");
    });

    run_case("Squad table: Players > My squad opens on the Squad tab; Age sorts, reverses, resets; OVR sorts high to low", [&] {
        app.prefs.advanced_mode = true;  // 3.0.1: My squad (old page) is a Lab page since 3.0 (c497923a, ui_shell.cpp:66): the Lab is shown only with Advanced on (b153e489)
        ui.frames(2);  // the rail draws the Lab from the next frame on
        CHECK(mine > 0 && app.model.links_of_team(mine).size() >= 5, "the test world has my club with a squad");
        CHECK(ui.open_page("Lab", "My squad (old page)") && app.place() == shell::place_of(Page::Squad), "Players > My squad opens");
        ui.frames(3);
        const SquadTableState& st = squad_table_state();
        CHECK(st.table_drawn && st.club == mine && st.rows == app.model.links_of_team(mine).size(),
              fmt("the Squad tab shows my club: club %lld rows %zu", static_cast<long long>(st.club), st.rows));
        CHECK(ui.find("###sqtabsquad", "FC 27 LE Turbo") != nullptr && ui.find("###sqtabtools", "FC 27 LE Turbo") != nullptr, "tabs Squad and Career tools");
        CHECK(squad_table_now() != nullptr && squad_table_now()->ColumnsCount == 6, "the six-column table is the first thing on the page");
        CHECK(st.sort_column == -1, "database order until a header is clicked");
        const std::vector<int64_t> base = db_order(app, mine);
        CHECK(drawn_order(app, mine) == base, "rows in database order: " + ids(drawn_order(app, mine)));
        // Age: first click ascending, second descending, third back to the database order (tristate)
        CHECK(click_header(ui, "Age"), "the Age header");
        CHECK(st.sort_column == 4 && st.sort_ascending, fmt("sorted on Age ascending: column %d", st.sort_column));
        CHECK(drawn_order(app, mine) == expected_order(app, mine, true, true), "by age, youngest first: " + ids(drawn_order(app, mine)) + " want " + ids(expected_order(app, mine, true, true)));
        CHECK(click_header(ui, "Age"), "Age again");
        CHECK(st.sort_column == 4 && !st.sort_ascending, "reversed");
        CHECK(drawn_order(app, mine) == expected_order(app, mine, true, false), "by age, oldest first: " + ids(drawn_order(app, mine)));
        CHECK(click_header(ui, "Age"), "Age a third time");
        CHECK(st.sort_column == -1 && drawn_order(app, mine) == base, "a third click returns to the database order");
        // OVR: the first click is high to low
        CHECK(click_header(ui, "OVR"), "the OVR header");
        CHECK(st.sort_column == 3 && !st.sort_ascending, fmt("OVR sorts descending first: column %d asc %d", st.sort_column, st.sort_ascending));
        CHECK(drawn_order(app, mine) == expected_order(app, mine, false, false), "by overall, best first: " + ids(drawn_order(app, mine)) + " want " + ids(expected_order(app, mine, false, false)));
        CHECK(click_header(ui, "OVR"), "OVR again");
        CHECK(click_header(ui, "OVR"), "OVR a third time");
        CHECK(st.sort_column == -1 && drawn_order(app, mine) == base, "back to the database order");
    });

    run_case("Squad table: a click on a name opens Players > Edit a player with that player (the page route, not request_tab)", [&] {
        app.prefs.advanced_mode = true;  // 3.0.1: My squad (old page) is a Lab page since 3.0 (c497923a, ui_shell.cpp:66): the Lab is shown only with Advanced on (b153e489)
        ui.frames(2);  // the rail draws the Lab from the next frame on
        app.request_tab = -1;
        CHECK(ui.open_page("Lab", "My squad (old page)"), "Players > My squad");
        ui.frames(3);
        const std::string nm = app.model.player_name(1005);
        CHECK(ui.find(nm, "##squad") != nullptr, "Gabriel Jesus is in the table: " + nm);
        CHECK(ui.click(nm, "##squad"), "click his name");
        ui.frames(4);
        CHECK(app.sel_player == 1005, "he is the selected player");
        CHECK(app.place() == shell::place_of(Page::PlayerEdit), "Edit a player is the page now");
        CHECK(app.request_tab == -1, "the legacy tab index was not used");
        // the same from Team > Players > Squad list, for another club
        app.sel_team = 7;
        CHECK(ui.open_page("Club", "Squad"), "Team > Players");
        ui.frames(3);
        CHECK(squad_table_state().club == 7 && squad_table_state().rows == app.model.links_of_team(7).size(), "the Squad list shows Everton");
        const std::string pick = app.model.player_name(2001);
        CHECK(ui.click(pick, "##squad"), "click Everton's goalkeeper");
        ui.frames(4);
        CHECK(app.sel_player == 2001 && app.place() == shell::place_of(Page::PlayerEdit) && app.request_tab == -1, "he opens in Edit a player");
        // Everton: the sort works there too
        CHECK(ui.open_page("Club", "Squad"), "Team > Players again");
        ui.frames(3);
        CHECK(click_header(ui, "OVR"), "OVR on the Squad list");
        CHECK(drawn_order(app, 7) == expected_order(app, 7, false, false), "Everton by overall: " + ids(drawn_order(app, 7)));
        CHECK(click_header(ui, "OVR"), "OVR again");
        CHECK(click_header(ui, "OVR"), "OVR a third time (reset)");
        app.sel_team = mine;
    });

    run_case("Squad table: a Jersey edit writes teamplayerlinks.jerseynumber of that player's row only, through App::edit", [&] {
        app.prefs.advanced_mode = true;  // 3.0.1: My squad (old page) is a Lab page since 3.0 (c497923a, ui_shell.cpp:66): the Lab is shown only with Advanced on (b153e489)
        ui.frames(2);  // the rail draws the Lab from the next frame on
        CHECK(ui.open_page("Lab", "My squad (old page)"), "Players > My squad");
        ui.frames(3);
        const Table* lt = app.db.table("teamplayerlinks");
        CHECK(lt != nullptr, "the links table");
        if (!lt) return;
        std::map<int64_t, int64_t> before;  // playerid -> jersey number of every link of my club
        uint64_t rec1005 = 0;
        for (const LinkRow& l : app.model.links_of_team(mine)) {
            before[l.playerid] = app.db.get_int(*lt, l.rec, "jerseynumber", -1);
            if (l.playerid == 1005) rec1005 = l.rec;
        }
        CHECK(rec1005 != 0, "he has a link row");
        const ItemRec* name = ui.find(app.model.player_name(1005), "##squad");
        CHECK(name != nullptr, "his row is drawn");
        if (!name || !rec1005) return;
        const ItemRec* box = nullptr;
        for (const auto& kv : g_items) {
            const ItemRec& r = kv.second;
            if (r.frame != g_frame || r.label != "##v" || r.window.find("##squad") == std::string::npos) continue;
            if (std::fabs(r.rect.GetCenter().y - name->rect.GetCenter().y) < 3.0f && r.rect.Max.x <= name->rect.Min.x + 1.0f) box = &r;
        }
        CHECK(box != nullptr, "the Jersey box of his row");
        if (!box) return;
        const uint64_t pushes = app.undo_pushes;
        const int64_t number = before[1005] == 77 ? 78 : 77;
        CHECK(ui.type_into(box, std::to_string(number)), "type a number");
        ui.frames(3);
        CHECK(app.db.get_int(*lt, rec1005, "jerseynumber", -1) == number, fmt("his number is %lld now", static_cast<long long>(number)));
        for (const LinkRow& l : app.model.links_of_team(mine))
            if (l.playerid != 1005) CHECK(app.db.get_int(*lt, l.rec, "jerseynumber", -1) == before[l.playerid], "nobody else changed: " + std::to_string(l.playerid));
        {   // App::edit_field logs every write with the table and field in brackets
            const std::string want = "[teamplayerlinks.jerseynumber = " + std::to_string(number) + "]";
            bool logged = false;
            for (const std::string& l : app.log_lines) logged = logged || l.find(want) != std::string::npos;
            CHECK(logged, "the edit went through App::edit (log line " + want + ")");
        }
        CHECK(app.undo_pushes == pushes, "no player undo step: a link edit never had one (no new write path)");
        // put his number back the way a user would: type it again
        const ItemRec* box2 = nullptr;
        const ItemRec* name2 = ui.find(app.model.player_name(1005), "##squad");
        for (const auto& kv : g_items) {
            const ItemRec& r = kv.second;
            if (r.frame != g_frame || r.label != "##v" || r.window.find("##squad") == std::string::npos || !name2) continue;
            if (std::fabs(r.rect.GetCenter().y - name2->rect.GetCenter().y) < 3.0f && r.rect.Max.x <= name2->rect.Min.x + 1.0f) box2 = &r;
        }
        CHECK(box2 != nullptr && ui.type_into(box2, std::to_string(before[1005])), "type his number back");
        ui.frames(3);
        CHECK(app.db.get_int(*lt, rec1005, "jerseynumber", -1) == before[1005], "his number is back");
        ui.frames(3);
    });

    run_case("Squad table: outside a career My squad shows the club picker and the table of the picked club", [&] {
        app.prefs.advanced_mode = true;  // 3.0.1: My squad (old page) is a Lab page since 3.0 (c497923a, ui_shell.cpp:66): the Lab is shown only with Advanced on (b153e489)
        ui.frames(2);  // the rail draws the Lab from the next frame on
        json st = original_state;
        st["user_team"] = 0;
        st["in_cm"] = false;
        write_state(app, ui, le, st);
        CHECK(app.bridge.state().user_team == 0, "no club of his own");
        app.sel_team = 241;
        CHECK(ui.open_page("Lab", "My squad (old page)"), "Players > My squad");
        ui.frames(3);
        CHECK(ui.find("##sqclub", "FC 27 LE Turbo") != nullptr || ui.find("###sqtabsquad", "FC 27 LE Turbo") != nullptr, "the page is drawn");
        CHECK(squad_table_state().table_drawn && squad_table_state().club == 241 && squad_table_state().rows == 3,
              fmt("the table of the picked club: %lld", static_cast<long long>(squad_table_state().club)));
        bool picker = false;
        for (const auto& kv : g_items)
            if (kv.second.frame == g_frame && kv.second.label.find("##sqclub") != std::string::npos) picker = true;
        CHECK(picker, "the club picker is inline");
        write_state(app, ui, le, original_state);
        CHECK(app.bridge.state().user_team == mine, "the career is back");
    });

    run_case("Squad table: the Career tools tab keeps the five cards", [&] {
        app.prefs.advanced_mode = true;  // 3.0.1: My squad (old page) is a Lab page since 3.0 (c497923a, ui_shell.cpp:66): the Lab is shown only with Advanced on (b153e489)
        ui.frames(2);  // the rail draws the Lab from the next frame on
        CHECK(ui.open_page("Lab", "My squad (old page)"), "Players > My squad");
        ui.frames(3);
        CHECK(ui.click("###sqtabtools", "FC 27 LE Turbo"), "the Career tools tab");
        ui.frames(4);
        int cards = 0;
        for (const char* c : {"##career_form", "##career_roles", "##career_contracts", "##career_growth", "##career_tools"})
            for (const auto& kv : g_items)
                if (kv.second.frame == g_frame && kv.second.window.find(c) != std::string::npos) {
                    ++cards;
                    break;
                }
        CHECK(cards == 5, fmt("the five cards are there: %d", cards));
        CHECK(ui.click("###sqtabsquad", "FC 27 LE Turbo"), "back to the Squad tab");
        ui.frames(3);
    });

    run_case("Squad table: four more v2.14 capabilities are on the default route (All clubs, More tools, All managers, kept player edits)", [&] {
        app.prefs.advanced_mode = false;  // 3.0.1: back to the Simple view (the My squad cases above need the Lab)
        const ImVec2 old_display = ImGui::GetIO().DisplaySize;
        ImGui::GetIO().DisplaySize = ImVec2(1280.0f, 3400.0f);  // a whole page fits
        ui.frames(3);
        // Team > Players > All clubs = the club browser (draw_teams): the search box of its sortable team list
        app.sel_team = mine;
        CHECK(ui.open_page("Club", "Squad"), "Team > Players");
        ui.frames(3);
        CHECK(ui.find("###tptabclubs", "FC 27 LE Turbo") != nullptr, "the All clubs tab is there");
        CHECK(ui.click("###tptabclubs", "FC 27 LE Turbo"), "click All clubs");
        ui.frames(4);
        CHECK(ui.find("##tsearch") != nullptr, "the club browser's search box");
        CHECK(ui.click("###tptabsquadlist", "FC 27 LE Turbo"), "back to the Squad list tab");
        ui.frames(3);
        // Career > Transfers & money > More tools = the old Turbo Tools panel (draw_tools)
        CHECK(ui.open_page("Career", "Money & transfers") && app.place() == shell::place_of(Page::Transfers), "Career > Transfers & money");
        ui.frames(3);
        CHECK(ui.click("More tools###ctmore"), "open More tools");
        ui.frames(4);
        // the panel lies below the visible part of the page: scroll the page with the wheel, as a user would
        for (int step = 0; step < 8 && !ui.find("Your squad"); ++step) {
            const ItemRec* more = ui.find("More tools###ctmore");
            if (!more) break;
            ImGui::GetIO().AddMousePosEvent(more->rect.Min.x + 20.0f, more->rect.GetCenter().y);
            ui.frame();
            ImGui::GetIO().AddMouseWheelEvent(0.0f, -5.0f);
            ui.frames(3);
        }
        ui.dump("More tools open");
        CHECK(ui.find("Your squad") != nullptr, "the old tools panel is drawn under More tools");
        CHECK(ui.click("More tools###ctmore"), "close it again");
        ui.frames(2);
        // Players > Managers & staff > All managers (table) = draw_managers: its search box
        CHECK(ui.open_page("Players", "Staff") && app.place() == shell::place_of(Page::Staff), "Players > Managers & staff");
        ui.frames(3);
        CHECK(ui.click("All managers (table)###csmgr"), "open All managers");
        ui.frames(4);
        CHECK(ui.find("##msearch") != nullptr, "the manager table's search box");
        CHECK(ui.click("All managers (table)###csmgr"), "close it again");
        ui.frames(2);
        // Career > Changes & undo: the list of kept player edits
        CHECK(ui.open_page("Career", "Changes & undo") && app.place() == shell::place_of(Page::ChangesUndo), "Career > Changes & undo");
        ui.frames(3);
        CHECK(ui.find("###keptedits") != nullptr, "the kept player edits section is on the page");
        ImGui::GetIO().DisplaySize = old_display;
        ui.frames(3);
    });

    app.legacy_pages = was_legacy;
    app.prefs.advanced_mode = was_advanced;
    app.sel_team = old_sel_team;
    app.sel_player = old_sel_player;
    shell::player_edit_tab() = was_tab;
    app.go(Page::PlayerEdit);
    ui.frames(3);
}

}  // namespace squad_table_test
