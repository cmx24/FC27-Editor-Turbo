// Native tests (2026-10-17, GUI revamp step 5, brief 2.16-gui-pages track "player"): the Player page (ui/ui_player_page.cpp).
// Included by test_main.cpp after test_club_page.h; player_page_test::ui_cases runs inside the UI block, a real App. The page is drawn
// through the Ui hook g_ui_extra (a separate window "##playerpage_test" after the app's own frame), as the Club & Kits page test does:
// the page is not registered with the shell yet (revamp-2 step 4 wires it), so it is drawn directly.
#pragma once
#include "ui/ui_player_page.h"
#include "ui/ui_shell.h"

namespace turbo {
bool manager_chooser_on_manager_heads();  // ui_faces.cpp (3.1 N3)
}

namespace player_page_test {

using namespace turbo;

static void draw_extra(App& app) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) ImGui::SetNextWindowFocus();  // a modal keeps the focus it needs
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##playerpage_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) draw_player_page(app);
    ImGui::End();
}
static void show(Ui& ui) {
    g_ui_extra = draw_extra;
    ui.frames(3);
}
static void hide(Ui& ui) {
    g_ui_extra = nullptr;
    ui.frames(2);
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    const char* W = "##playerpage_test";
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

    run_case("PlayerPage: list, search, chips, cards, no old tabs and no field search", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        app.sel_player = 1001;
        show(ui);
        CHECK(ui.find("##ppsearch", W) != nullptr, "search box");
        CHECK(ui.find("Free agents###pp_chip_free", W) != nullptr, "chip: free agents");
        CHECK(ui.find("##pppos", W) != nullptr, "position filter");
        const PlayerPageState& st = player_page_state();
        CHECK(st.listed > 3, "the list holds players: " + std::to_string(st.listed));
        CHECK(ui.find("ID", W) == nullptr && ui.find("Player ID", W) == nullptr, "no ID column or ID label on the page");
        CHECK(ui.find("###pp1001", W) != nullptr && ui.find("###pp1005", W) != nullptr, "players listed by name, one row each");
        // pick a player from the list
        app.sel_player = 0;
        ui.frames(2);
        CHECK(player_page_state().playerid == 0, "nobody picked: no cards");
        CHECK(ui.click("###pp1005", W), "click a row");
        ui.frames(2);
        CHECK(app.sel_player == 1005 && st.playerid == 1005, "the click picked him");
        const PlayerRow* pr = app.model.player(1005);
        CHECK(pr && st.headline.find("overall") != std::string::npos && st.headline.find(std::to_string(pr->overall)) != std::string::npos,
              "headline: " + st.headline);
        CHECK(st.headline.find("ID") == std::string::npos && st.headline.find("1005") == std::string::npos, "the headline names no ID");
        // the cards, in order
        CHECK(ui.find("###pp_face", "pp_looks") != nullptr, "Looks: Choose a real face");
        CHECK(st.face_line == "Real face" || st.face_line == "Generic face", "Looks: a plain face line: " + st.face_line);
        const ItemRec* hair = ui.find("###pp_hair", "pp_looks");
        CHECK(hair != nullptr, "Looks: hair and beard fold-out");
        CHECK(hair && hair->label == "Hair and beard###pp_hair", "the fold says no restart is needed: " + (hair ? hair->label : std::string("?")));
        CHECK(ui.find("+1###pp_ovr_up", "pp_skills") != nullptr && ui.find("-1###pp_ovr_down", "pp_skills") != nullptr, "Skills: overall +1 / -1");
        CHECK(ui.find("###pp_sk_pace", "pp_skills") != nullptr && ui.find("###pp_sk_def", "pp_skills") != nullptr, "Skills: the groups");
        CHECK(ui.find("###pp_sk_gk", "pp_skills") == nullptr || pr->positions[0] == 0, "Skills: Goalkeeping only for a goalkeeper");
        CHECK(ui.find("###pp_otherpos", "pp_position") != nullptr, "Position & style: other positions fold-out");
        CHECK(ui.find("###pp_names", "pp_name") != nullptr && ui.find("###pp_callname", "pp_name") != nullptr, "Name & commentary: two fold-outs");
        // the old panel's clutter is gone
        for (const char* gone : {"Profile", "Names", "Attributes", "PlayStyles", "Appearance", "Miniface", "Contract & Clubs", "Callname", "All fields", "Growth",
                                 "Transfer / Loan...", "Terminate loan", "Repair names"})
            CHECK(ui.find(gone, W) == nullptr, std::string("no old tab or button: ") + gone);
        CHECK(ui.find("##fieldsearch", W) == nullptr && ui.find("##ptabs", W) == nullptr, "no field search and no tab bar");
        // the moves wait inside More
        CHECK(ui.find("###pp_release", W) == nullptr && ui.find("###pp_delete", W) == nullptr, "Release and Delete are not on the page until More is opened");
        CHECK(ui.find("###pp_more", W) != nullptr, "More");
        // search narrows the list
        CHECK(ui.type_into(ui.find("##ppsearch", W), "zzzz-no-such-player"), "type a search");
        ui.frames(2);
        CHECK(st.listed == 0, "nobody matches");
        CHECK(ui.type_into(ui.find("##ppsearch", W), ""), "clear the search");
        ui.frames(2);
        CHECK(st.listed > 3, "everybody again");
        hide(ui);
    });

    run_case("PlayerPage: Overall +1 goes through the shared overall edit and Undo takes it back", [&] {
        app.sel_player = 1001;
        show(ui);
        const Table* t = app.db.table("players");
        const PlayerRow* pr = app.model.player(1001);
        CHECK(t && pr, "player 1001");
        if (!t || !pr) {
            hide(ui);
            return;
        }
        const int64_t before = app.db.get_int(*t, pr->rec, "overallrating", 0);
        const size_t undo0 = app.undo_count(1001);
        CHECK(ui.click("+1###pp_ovr_up", W), "click +1");
        ui.frames(2);
        const int64_t after = app.db.get_int(*t, pr->rec, "overallrating", 0);
        CHECK(after == before + 1, "overall " + std::to_string(before) + " -> " + std::to_string(after));
        CHECK(app.undo_count(1001) > undo0, "an undo step was kept");
        CHECK(ui.find("###pp_undo", W) != nullptr, "the Undo button is on the page");
        CHECK(ui.click("###pp_undo", W), "click Undo");
        ui.frames(2);
        CHECK(app.db.get_int(*t, pr->rec, "overallrating", 0) == before, "Undo put the overall back");
        hide(ui);
    });

    run_case("PlayerPage: More holds the moves, every one that cannot be undone asks first and says so", [&] {
        app.sel_player = 1002;
        app.lua_queue.clear();
        if (app.busy()) ui.click("Cancel");
        show(ui);
        CHECK(ui.click("###pp_more", W), "open More");
        ui.frames(2);
        CHECK(player_page_state().more_open, "More is open");
        for (const char* l : {"###pp_move", "###pp_release", "###pp_endloan", "###pp_tlist", "###pp_llist", "###pp_unlist", "###pp_delete"})
            CHECK(ui.find(l, W) != nullptr, std::string("in More: ") + l);
        CHECK(ui.find("Export...", W) != nullptr && ui.find("Clone...", W) != nullptr && ui.find("Import...", W) != nullptr, "the player files are in More");
        const bool in_cm = app.bridge.state().in_cm;
        if (in_cm) {
            // Release: a confirmation first, nothing sent until it is accepted
            ui.click("###pp_release", W);
            ui.frames(3);
            const std::string c = player_page_state().confirm_text;
            if (!c.empty()) {
                CHECK(c.find("Release") != std::string::npos && c.find("cannot bring this back") != std::string::npos, "the dialog says it cannot be undone: " + c);
                CHECK(!app.busy(), "nothing sent before the answer");
                CHECK(ui.click("Cancel###pp_confirm_no"), "Cancel");
                ui.frames(2);
                CHECK(!app.busy() && player_page_state().confirm_text.empty(), "cancelled: nothing sent, dialog gone");
            } else {
                // the rules refuse this release for him: the button only explained why
                CHECK(!app.busy(), "refused by the move rules: nothing sent");
            }
        }
        hide(ui);
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
        ui.frames(2);
        CHECK(ui.click("###lsaskok"), "Choose a face");
        ui.frames(3);
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
        CHECK(ui.click("Player heads"), "Player heads");
        ui.frames(2);
        CHECK(!manager_chooser_on_manager_heads(), "Player heads was picked");
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
        CHECK(ui.click("Player heads"), "Player heads");
        ui.frames(2);
        CHECK(!manager_chooser_on_manager_heads(), "Player heads was picked");
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
