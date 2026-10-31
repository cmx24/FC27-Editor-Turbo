// Native tests (2026-10-17, GUI revamp step 5, brief 2.16-gui-pages track "player"): the player list filter (ui/ui_player_page.h) and the
// 3.1 N2 / N3 navigation cases on the real App. Included by test_main.cpp after test_club_page.h; player_page_test::ui_cases runs inside
// the UI block, a real App.
// 2026-10-31 (5.0c menus step 6): the card page itself (draw_player_page, "Card view (beta)") was deleted with the classic tab; its cases
// (list / chips / cards, Overall +1, More) went with it.
#pragma once
#include "ui/ui_player_page.h"
#include "ui/ui_shell.h"

namespace turbo {
bool manager_chooser_on_manager_heads();  // ui_faces.cpp (3.1 N3)
}

namespace player_page_test {

using namespace turbo;

static void hide(Ui& ui) {
    g_ui_extra = nullptr;
    ui.frames(2);
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    const int64_t saved_sel = app.sel_player;

    run_case("PlayerPage: the filter is pure (name, club, free agents, position, a typed ID) and shows no ID column", [&] {
        PlayerRow p;
        p.playerid = 4242;
        p.name = "Jamie Vardy";
        p.club = 7;
        p.club_name = "Leicester City";
        p.positions[0] = 25;
        PlayerPageFilter f;
        CHECK(player_page_matches(p, f, 0), "no filter: passes");
        f.text = "vard";
        CHECK(player_page_matches(p, f, 0), "part of the name, any case");
        f.text = "LEICESTER";
        CHECK(player_page_matches(p, f, 0), "part of the club, any case");
        f.text = "nobody";
        CHECK(!player_page_matches(p, f, 0), "no match");
        f.text = "4242";
        CHECK(player_page_matches(p, f, 0), "a typed ID still finds him");
        f.text = "42";
        CHECK(!player_page_matches(p, f, 0), "a part of an ID is not a match");
        f = PlayerPageFilter();
        f.my_club = true;
        CHECK(!player_page_matches(p, f, 0) && !player_page_matches(p, f, 9) && player_page_matches(p, f, 7), "my club: his club is the user's");
        f = PlayerPageFilter();
        f.free_agents = true;
        CHECK(!player_page_matches(p, f, 7), "free agents: he has a club");
        p.club = 0;
        CHECK(player_page_matches(p, f, 7), "free agents: no club passes");
        f = PlayerPageFilter();
        f.position = 25;
        CHECK(player_page_matches(p, f, 0), "position: one of his positions");
        f.position = 3;
        CHECK(!player_page_matches(p, f, 0), "position: not his");
    });

    // 3.1 N2 (Defect 3, navigation loops): the top-bar search lands on Attributes & roles (CHANGE-4), whatever Player tab was open before
    run_case("Shell 3.1 N2: a player found with the top-bar search opens on tab 1 (Attributes & roles), not the tab left open", [&] {
        hide(ui);
        app.go(shell::Page::Home);
        ui.frames(3);
        shell::player_edit_tab() = 3;  // the Callname tab was left open
        const PlayerRow* pick = nullptr;
        for (const PlayerRow& p : app.model.players())
            if (p.name.size() >= 4) { pick = &p; break; }
        CHECK(pick != nullptr, "a player with a name to search");
        if (!pick) return;
        std::string q = pick->name;
        for (char& c : q) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        const std::string want_name = pick->name;
        app.sel_player = 0;
        CHECK(ui.type_into(ui.find("##gsearch"), q), "type into the search");
        ui.frames(3);
        const PlayerRow* got = app.model.player(app.sel_player);
        CHECK(app.place() == shell::place_of(shell::Page::PlayerEdit) && got != nullptr, "the search opened Players > Player with " + want_name);
        CHECK(shell::player_edit_tab() == 1, "the tab is Attributes & roles: " + std::to_string(shell::player_edit_tab()));
        shell::player_edit_tab() = 0;
        app.go(shell::Page::Home);
        ui.frames(3);
    });

    // 3.1 N3: the chooser opened for a manager starts on the managers' heads (the radio "Manager heads" was a click away)
    run_case("Shell 3.1 N3: Looks > Managers > Choose a real face opens the chooser on Manager heads", [&] {
        hide(ui);
        if (app.model.managers().empty()) {
            CHECK(false, "the test world has a manager");
            return;
        }
        app.sel_manager = 0;
        app.go(shell::Page::LooksStudio);
        ui.frames(3);
        CHECK(ui.click("Managers###lsmanagers"), "Managers");
        ui.frames(3);
        CHECK(ui.click("###lsface"), "Choose a real face...");
        ui.frames(3);  // with 3.1/render R4 the click opens the chooser at once: no question (###lsaskok) before it (merge 3.1/gui)
        // 5.0b-C: "the chooser is open" is read from Close: it sits in the bar under the grid, outside the scrolling part (the radio "Manager heads"
        // moved into the Filters popover)
        CHECK(ui.find("Close##mfaces") != nullptr, "the manager chooser is open");
        CHECK(manager_chooser_on_manager_heads(), "the chooser shows Manager heads");
        if (ui.find("Close##mfaces")) ui.click("Close##mfaces");
        ui.frames(2);
        ui.click("Players###lsplayers");
        app.go(shell::Page::Home);
        ui.frames(3);
    });

    // 3.1 N3b: Looks > Managers (3.1 R4) begins the chooser's window itself first in the frame, for the warning line, and manager_real_face_picker
    // appends to it. ImGui recomputes IsWindowAppearing() on every Begin, so the second Begin reads false: the chooser must open on Manager heads
    // anyway, and again after it was closed with Player heads picked. Drawn here in that order, after the app's own frame.
    run_case("Shell 3.1 N3b: a chooser whose window the page begins first in the frame opens on Manager heads, and again after Player heads + Close", [&] {
        hide(ui);
        if (app.model.managers().empty()) {
            CHECK(false, "the test world has a manager");
            return;
        }
        const int64_t mid = app.model.managers()[0].managerid;
        bool open_it = false;
        ui.extra_draw = [&] {
            if (open_it) {
                ImGui::OpenPopup(kManagerFacePopup);
                open_it = false;
            }
            ImGui::SetNextWindowSize(ImVec2(S(880.0f), S(640.0f)), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal(kManagerFacePopup, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::TextUnformatted("the page's warning line");
                ImGui::EndPopup();
            }
            manager_real_face_picker(app, mid);
        };
        open_it = true;
        ui.frames(3);
        CHECK(ui.find("Close##mfaces") != nullptr, "the chooser is open (first time)");
        CHECK(manager_chooser_on_manager_heads(), "it shows Manager heads the first time");
        CHECK(fcl_open_filters(ui) && ui.click("Player heads"), "Filters, then Player heads");
        ui.frames(2);
        CHECK(!manager_chooser_on_manager_heads(), "Player heads was picked");
        fcl_close_filters(ui);
        CHECK(ui.click("Close##mfaces"), "Close");
        ui.frames(3);
        CHECK(ui.find("Close##mfaces") == nullptr, "the chooser is closed");
        open_it = true;
        ui.frames(3);
        CHECK(ui.find("Close##mfaces") != nullptr, "the chooser is open (second time)");
        CHECK(manager_chooser_on_manager_heads(), "it shows Manager heads the second time");
        if (ui.find("Close##mfaces")) ui.click("Close##mfaces");
        ui.frames(3);
        ui.extra_draw = nullptr;
        ui.frames(2);
    });

    // 3.1 N3c: closed in frame F and opened again in frame F+1. The 57d9f9f counter ("the chooser was not drawn last frame") read that as still open
    // and left the chooser on Player heads; the open itself (OpenPopup, ImGuiPopupData::OpenFrameCount) is what counts. One Begin here (no R4 double
    // Begin), so the consecutive-frame reopen is the only variable.
    run_case("Shell 3.1 N3c: a chooser closed and opened again on the very next frame opens on Manager heads again", [&] {
        hide(ui);
        if (app.model.managers().empty()) {
            CHECK(false, "the test world has a manager");
            return;
        }
        const int64_t mid = app.model.managers()[0].managerid;
        bool open_it = false, was_open = false, reopen_on_close = false;
        ui.extra_draw = [&] {
            if (open_it) {
                ImGui::OpenPopup(kManagerFacePopup);
                open_it = false;
            }
            manager_real_face_picker(app, mid);
            const bool is_open = ImGui::IsPopupOpen(kManagerFacePopup);
            if (was_open && !is_open && reopen_on_close) {  // closed in this very frame: OpenPopup in the next one
                open_it = true;
                reopen_on_close = false;
            }
            was_open = is_open;
        };
        open_it = true;
        ui.frames(3);
        CHECK(ui.find("Close##mfaces") != nullptr, "the chooser is open (first time)");
        CHECK(manager_chooser_on_manager_heads(), "it shows Manager heads the first time");
        CHECK(fcl_open_filters(ui) && ui.click("Player heads"), "Filters, then Player heads");
        ui.frames(2);
        CHECK(!manager_chooser_on_manager_heads(), "Player heads was picked");
        fcl_close_filters(ui);
        reopen_on_close = true;
        CHECK(ui.click("Close##mfaces"), "Close");
        ui.frames(3);
        CHECK(!reopen_on_close, "the chooser was closed and reopened on consecutive frames");
        CHECK(ui.find("Close##mfaces") != nullptr, "the chooser is open again");
        CHECK(manager_chooser_on_manager_heads(), "it shows Manager heads again after a reopen on the very next frame");
        if (ui.find("Close##mfaces")) ui.click("Close##mfaces");
        ui.frames(3);
        ui.extra_draw = nullptr;
        ui.frames(2);
    });
    app.sel_player = saved_sel;
    app.lua_queue.clear();
}

}  // namespace player_page_test
