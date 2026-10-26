// Native tests (2.20 ia-team, brief 2.20-IA-pages, row "team"): the Team pages of ui/ui_team_players.cpp and ui/ui_team_transfers.cpp
// (declared in ui/ui_team_pages.h): Team > Players and Team > Transfers. Included by test_main.cpp after test_page_career.h;
// page_team_test::ui_cases runs inside the UI block (a real App on the simulated game). The pages are not registered with the shell
// yet (the wiring track does that), so each one is drawn by function through the Ui hook g_ui_extra, in a window of its own
// ("##teampage_test") that is tall enough to show a whole page.
//
// What is proven here: every card and its buttons are there; every action the Undo button cannot bring back ASKS first (Cancel sends
// nothing) and the confirmed request carries the same Lua module and the same keys the old panels send (player_moves with the old
// actions, squad_role, development, extend_user_contracts); a contract extension writes the contract end year (read back, and the
// Undo button takes it back); another club's page greys out what exists for the user's club only. Cases are named "Team-pages: ..."
// so TURBO_TEST_ONLY="team-pages" runs them alone.
//
// Not provable in this world (no table for them): the End loan button (the world has no playerloans rows) and the Form editor (no
// form column); morale of one player does not exist anywhere in Turbo (squad-wide only).
#pragma once
#include "ui/ui_team_pages.h"
#include "ui/ui_shell.h"

#include <cstring>

namespace page_team_test {

using namespace turbo;

static void (*g_page_fn)(App&) = nullptr;
// 0 = the page shows from its top; 1 = scrolled to its bottom (the test window is the size of the screen, a long page clips what is below it)
static int g_scroll_mode = 0;
static void apply_scroll() {
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        if (w->ParentWindow && std::strcmp(w->ParentWindow->Name, "##teampage_test") == 0) ImGui::SetScrollY(w, g_scroll_mode > 0 ? w->ScrollMax.y : 0.0f);
        // 4.0 S8: a page drawn in the page template (ui_page.h) scrolls in its body, two windows below the test window
        // (and the Career squad cards' own child, ##career_squad, which scrolls inside the body)
        else if (std::strncmp(w->Name, "##teampage_test/", 16) == 0) {
            const char* last = std::strrchr(w->Name, '/') + 1;
            if (std::strncmp(last, "##pg_body", 9) == 0 || std::strncmp(last, "##career_squad", 14) == 0)
                ImGui::SetScrollY(w, g_scroll_mode > 0 ? w->ScrollMax.y : 0.0f);
        }
    }
}
static void draw_extra(App& app) {
    if (!g_page_fn) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    // focusing the page window every frame would close the question (a modal popup) the page has just opened
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##teampage_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        g_page_fn(app);
        apply_scroll();
    }
    ImGui::End();
}
static void show(Ui& ui, void (*fn)(App&)) {
    g_page_fn = fn;
    g_ui_extra = draw_extra;
    ui.frames(4);
}
static void scroll_bottom(Ui& ui, bool bottom) {
    g_scroll_mode = bottom ? 1 : 0;
    ui.frames(4);
}
static void hide(Ui& ui) {
    g_scroll_mode = 0;
    g_page_fn = nullptr;
    g_ui_extra = nullptr;
    ui.frames(2);
}

// bridge_state.json rewritten the way the Career pages case does (each rewrite with a later time stamp so the bridge reads it again)
static void write_state(App& app, Ui& ui, const fs::path& le, json st) {
    static int bumps = 0;
    const fs::path state_file = le / "turbo_output" / "bridge_state.json";
    st["seq"] = st.value("seq", 0LL) + 1;
    {
        std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
        f << st.dump();
    }
    fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(900 + 2 * ++bumps));
    app.next_poll = 0.0;
    ui.frames(3);
}
static void lift(App& app, Ui& ui, const fs::path& le, const std::vector<std::string>& keys) {
    json st = read_json(le / "turbo_output" / "bridge_state.json");
    if (st.contains("unavailable") && st["unavailable"].is_object())
        for (const std::string& k : keys) st["unavailable"].erase(k);
    write_state(app, ui, le, st);
}

static json norm(json j) {
    if (!j.is_object()) j = json::object();
    if (!j.contains("overrides") || !j["overrides"].is_object()) j["overrides"] = json::object();
    if (!j["overrides"].contains("scope") || !j["overrides"]["scope"].is_object()) j["overrides"]["scope"] = json::object();
    if (!j["overrides"].contains("actions") || !j["overrides"]["actions"].is_array()) j["overrides"]["actions"] = json::array();
    return j;
}
static json sent(SimMemory& mem, uint64_t kMb) { return norm(json::parse(mem.read_cstr(kMb + kMbCmd, kMbTextSize), nullptr, false)); }
static json first_action(const json& j) { return j["overrides"]["actions"].empty() ? json::object() : j["overrides"]["actions"][0]; }

static bool has_item(Ui& ui, const std::string& label, const char* win) { return ui.find(label, win) != nullptr; }
static int count_items(const char* win) {
    int n = 0;
    for (const auto& kv : g_items)
        if (kv.second.frame == g_frame && kv.second.window.find(win) != std::string::npos) ++n;
    return n;
}

// Ask first: click `button`, Cancel sends nothing, click again and confirm with `ok`; returns the request sent
static json ask_and_confirm(App& app, Ui& ui, SimMemory& mem, uint64_t kMb, const std::string& button, const char* page_win, const std::string& ok,
                            std::string& why, const char* popup = "###teamask") {
    why.clear();
    app.mailbox->cancel();
    if (!ui.click(button, page_win)) {
        why = "button not found: " + button;
        return norm(json::object());
    }
    ui.frames(3);
    if (!ui.find("Cancel", popup)) {
        why = "no question after " + button + " (in_cm=" + std::to_string(app.bridge.state().in_cm) + " busy=" + std::to_string(app.busy()) + ")";
        return norm(json::object());
    }
    ui.click("Cancel", popup);
    ui.frames(3);
    if (app.mailbox->pending()) {
        why = "Cancel still sent a request for " + button;
        return norm(json::object());
    }
    ui.click(button, page_win);
    ui.frames(3);
    if (!ui.click(ok, popup)) {
        why = "no confirm button " + ok;
        return norm(json::object());
    }
    ui.frames(3);
    if (!app.mailbox->pending()) {
        why = "confirmed but nothing was sent";
        return norm(json::object());
    }
    json j = sent(mem, kMb);
    app.mailbox->cancel();
    ui.frames(2);
    return j;
}

// A click on a button that must do nothing (greyed out): no question, nothing sent
static bool does_nothing(App& app, Ui& ui, const std::string& button, const char* win) {
    app.mailbox->cancel();
    if (!ui.find(button, win)) return false;
    ui.click(button, win);
    ui.frames(3);
    return !ui.find("Cancel", "###teamask") && !app.mailbox->pending();
}

static void s8_cases(App& app, Ui& ui, SimMemory& mem, uint64_t kMb, int64_t mine);  // test_team_players_40.h (4.0 S8)

static void ui_cases(App& app, Ui& ui, SimMemory& mem, uint64_t kMb, const fs::path& le) {
    const ImVec2 old_display = ImGui::GetIO().DisplaySize;
    const json original_state = read_json(le / "turbo_output" / "bridge_state.json");
    const int64_t old_sel_team = app.sel_team;
    const int64_t old_sel_player = app.sel_player;
    ImGui::GetIO().DisplaySize = ImVec2(1280.0f, 3400.0f);  // a whole page fits: nothing is scrolled out of reach
    ui.frames(3);
    app.mailbox->cancel();
    lift(app, ui, le,
         {"move_transfer", "move_loan", "move_release", "move_terminate_loan", "move_transfer_list", "move_loan_list", "move_unlist", "move_list_status",
          "squad_role", "reveal", "development", "form_morale", "team_mass", "extend_user_contracts"});
    const int64_t mine = app.bridge.state().user_team;
    const PlayerRow* ten = app.model.player(1005);  // a field player of my club (never the only goalkeeper)
    std::string why;
    app.sel_team = mine;

    run_case("Team-pages: Players - the list, the four cards of one player, the role and growth buttons send the old requests", [&] {
        CHECK(app.connected() && app.bridge.state().in_cm && mine > 0, "the test world is connected and in a career");
        CHECK(ten != nullptr && ten->club == mine, "player 1005 plays for my club");
        app.sel_player = 1005;
        show(ui, draw_team_players);
        // 2.22: the page opens on the Squad list tab (test_squad_table_restore.h); the player list and cards are the Player tab
        CHECK(ui.click("Player###tptabplayer", "##teampage_test"), "the Player tab");
        ui.frames(4);
        const TeamPageState& s = team_players_state();
        CHECK(!s.waiting && s.club == mine && s.own_club, "the page works on my club");
        CHECK(s.listed == app.model.links_of_team(mine).size() && s.listed >= 5, fmt("the list shows my squad: %zu", s.listed));
        CHECK(s.player == 1005, "the picked player is shown");
        {   // the live check (tools/team_pages_livecheck.py) reads this line back from turbo_gui.log
            const std::string want = "Team page Players: club " + std::to_string(mine) + " (" + app.model.team_name(mine) + "), " + std::to_string(app.model.links_of_team(mine).size()) + " players";
            bool logged = false;
            for (const std::string& l : app.log_lines) logged = logged || l.find(want) != std::string::npos;
            CHECK(logged, "the page logs how many players it lists: " + want);
        }
        CHECK(s.cards == 4, fmt("contract, form, role and growth cards: %d", s.cards));
        CHECK(s.headline.find("overall 80") != std::string::npos && s.headline.find("contract until 20") != std::string::npos, "the headline: " + s.headline);
        CHECK(count_items("##tp_contract_") >= 2, fmt("the contract fields are on the page: %d items", count_items("##tp_contract_")));
        for (const char* l : {"Player ID", "playerid", "teamid", "Lua", "txt"}) CHECK(!has_item(ui, l, "##teampage_test"), std::string("no code on the page: ") + l);
        // the list: a click picks another player
        const std::string row = app.model.player(1002)->name + "##tpl1002";
        CHECK(ui.click(row, "##team_pl_rows"), "a player of the list can be clicked");
        ui.frames(3);
        CHECK(app.sel_player == 1002 && team_players_state().player == 1002, "clicking a row shows that player's cards");
        app.sel_player = 1005;
        ui.frames(3);
        // the role: the old panel's section when exposed, else the page's own buttons (same request)
        if (!squad_role_section) {
            app.mailbox->cancel();
            CHECK(ui.click("Set role###tpsetrole", "##tp_role_"), "Set role");
            ui.frames(3);
            CHECK(app.mailbox->pending(), "Set role sends at once (it is reversible in the game)");
            json j = sent(mem, kMb);
            CHECK(j.value("module", "") == "squad_role" && j["overrides"].value("playerid", 0LL) == 1005 && j["overrides"].value("role", 0) == 3,
                  "squad_role for the player, role 3 (Rotation): " + j.dump());
            app.mailbox->cancel();
            ui.frames(2);
            CHECK(ui.click("Read role###tpreadrole", "##tp_role_"), "Read role");
            ui.frames(3);
            j = sent(mem, kMb);
            CHECK(j.value("module", "") == "squad_role" && j["overrides"].value("playerid", 0LL) == 1005 && j["overrides"].value("mode", "") == "check",
                  "Read role sends squad_role mode check: " + j.dump());
            app.mailbox->cancel();
            ui.frames(2);
        }
        // growth: develop and add points ask first
        if (!career_dev_tab) {
            json j = ask_and_confirm(app, ui, mem, kMb, "###tpdevpot", "##tp_growth_", "Develop him", why);
            CHECK(why.empty(), "develop: " + why);
            CHECK(j.value("module", "") == "development" && j["overrides"].value("mode", "") == "to_potential" && j["overrides"]["scope"].value("playerid", 0LL) == 1005,
                  "development to_potential for the player: " + j.dump());
            j = ask_and_confirm(app, ui, mem, kMb, "Add points###tpaddpts", "##tp_growth_", "Change them", why);
            CHECK(why.empty(), "add points: " + why);
            CHECK(j.value("module", "") == "development" && j["overrides"].value("mode", "") == "add" && j["overrides"].value("delta", 0) == 1 &&
                      j["overrides"]["scope"].value("playerid", 0LL) == 1005,
                  "development add, delta 1: " + j.dump());
            app.mailbox->cancel();
            ui.click("Reveal his data", "##tp_growth_");
            ui.frames(3);
            j = sent(mem, kMb);
            CHECK(j.value("module", "") == "reveal" && j["overrides"]["scope"].value("playerid", 0LL) == 1005, "Reveal his data sends reveal for the player: " + j.dump());
            app.mailbox->cancel();
            ui.frames(2);
        }
        hide(ui);
    });

    run_case("Team-pages: Players - Whole squad tab holds the Career squad cards; another club's page greys out the own-club controls", [&] {
        app.sel_player = 1005;
        show(ui, draw_team_players);
        CHECK(ui.click("Whole squad###tptabsquad", "##teampage_test"), "the Whole squad tab");
        ui.frames(4);
        for (const char* l : {"Apply to my squad", "Set for my squad", "Extend my squad's contracts", "Develop to potential", "Reveal my players"})
            CHECK(has_item(ui, l, "team_players_"), std::string("the Career card control is on Whole squad: ") + l);  // 4.0 S8: ##page_team_players_ inside the template
        scroll_bottom(ui, true);  // 4.0 S8: the page body (##pg_body) is shorter than the page, the last card is below its first screen
        {
            std::string dbg;
            for (const auto& kv : g_items)
                if (kv.second.label.find("Run for") != std::string::npos || kv.second.label.find("Reveal my") != std::string::npos)
                    dbg += " [" + kv.second.label + " @" + kv.second.window + " f" + std::to_string(kv.second.frame) + "/" + std::to_string(g_frame) + " y=" + std::to_string(kv.second.rect.Min.y) + "]";
            CHECK(has_item(ui, "Run for my squad", "team_players_"), "the Career card control is on Whole squad: Run for my squad" + dbg);
        }
        scroll_bottom(ui, false);
        if (!team_preset_apply) CHECK(!has_item(ui, "Apply the My team preset", "##teampage_test"), "no preset button while the preset is not built in");
        json j = ask_and_confirm(app, ui, mem, kMb, "Set for my squad", "##career_roles", "Set roles", why, "###careerask");
        CHECK(why.empty() && j.value("module", "") == "squad_role" && j["overrides"].value("role", 0) == 3, "a Career card works inside the Team page: " + why + j.dump());
        // another club: Everton (7)
        const TeamRow* other = app.model.team(7);
        CHECK(other != nullptr && other->teamid != mine, "Everton is a club of the test world");
        app.sel_team = 7;
        ui.frames(4);
        CHECK(!team_players_state().own_club && team_players_state().club == 7, "the page works on Everton");
        CHECK(count_items("##career_squad") == 0, "no Career squad cards for another club");
        CHECK(ui.click("Player###tptabplayer", "##teampage_test"), "back to the Player tab");
        ui.frames(4);
        app.sel_player = 2001;
        ui.frames(3);
        CHECK(team_players_state().player == 2001 && team_players_state().cards == 4, fmt("Everton's player has his four cards: %d", team_players_state().cards));
        CHECK(!has_item(ui, "Set role###tpsetrole", "##teampage_test"), "no Set role for another club");
        CHECK(!has_item(ui, "Read role###tpreadrole", "##teampage_test"), "no Read role for another club");
        // my club button brings the page back
        CHECK(ui.click("My club###" + std::string("##teamplclub") + "mine", "##teampage_test"), "the My club button");
        ui.frames(3);
        CHECK(app.sel_team == mine && team_players_state().own_club, "My club selects my club");
        app.sel_player = 1005;
        hide(ui);
    });

    run_case("Team-pages: Transfers - the six cards; transfer, loan and release ask first and send the old player_moves actions", [&] {
        app.sel_team = mine;
        TeamMoveInputs& in = team_move_inputs();
        in = TeamMoveInputs{};
        in.player = 1005;
        app.sel_player = 1005;
        in.to_team = 7;
        in.fee = 2500000;
        in.wage = 30000;
        in.months = 48;
        in.loan_months = 6;
        show(ui, draw_team_transfers);
        const TeamPageState& s = team_transfers_state();
        CHECK(!s.waiting && s.club == mine && s.own_club, "the page works on my club");
        CHECK(s.cards == 6, fmt("six cards: moves, loans, move, sign, extensions, roles: %d", s.cards));
        CHECK(s.player == 1005, "the move card works on the picked player");
        {
            const std::string want = "Team page Transfers: club " + std::to_string(mine) + " (" + app.model.team_name(mine) + "), " + std::to_string(app.model.links_of_team(mine).size()) + " players";
            bool logged = false;
            for (const std::string& l : app.log_lines) logged = logged || l.find(want) != std::string::npos;
            CHECK(logged, "the page logs how many players it lists: " + want);
        }
        for (const char* l : {"Transfer###tpmvtransfer", "Loan###tpmvloan", "Release###tpmvrelease"}) CHECK(has_item(ui, l, "##tt_move_"), std::string("on the page: ") + l);
        for (const char* l : {"Player ID", "playerid", "to_teamid", "Lua", "txt", "teamplayerlinks"}) CHECK(!has_item(ui, l, "##teampage_test"), std::string("no code on the page: ") + l);
        json j = ask_and_confirm(app, ui, mem, kMb, "Transfer###tpmvtransfer", "##tt_move_", "Transfer", why);
        CHECK(why.empty(), "transfer: " + why);
        json a = first_action(j);
        CHECK(j.value("module", "") == "player_moves" && a.value("action", "") == "transfer" && a.value("playerid", 0LL) == 1005 && a.value("to_teamid", 0LL) == 7 &&
                  a.value("fee", 0LL) == 2500000 && a.value("wage", 0LL) == 30000 && a.value("months", 0) == 48,
              "transfer sends player_moves with the player, club, fee, wage and months: " + j.dump());
        j = ask_and_confirm(app, ui, mem, kMb, "Loan###tpmvloan", "##tt_move_", "Loan him", why);
        a = first_action(j);
        CHECK(why.empty() && a.value("action", "") == "loan" && a.value("playerid", 0LL) == 1005 && a.value("to_teamid", 0LL) == 7 && a.value("months", 0) == 6,
              "loan sends player_moves loan with 6 months: " + why + j.dump());
        j = ask_and_confirm(app, ui, mem, kMb, "Release###tpmvrelease", "##tt_move_", "Release him", why);
        a = first_action(j);
        CHECK(why.empty() && a.value("action", "") == "release" && a.value("playerid", 0LL) == 1005, "release sends player_moves release: " + why + j.dump());
        // no club picked / his own club: the buttons are greyed out and send nothing
        in.to_team = 0;
        ui.frames(3);
        CHECK(does_nothing(app, ui, "Transfer###tpmvtransfer", "##tt_move_"), "no club picked: Transfer does nothing");
        CHECK(does_nothing(app, ui, "Loan###tpmvloan", "##tt_move_"), "no club picked: Loan does nothing");
        in.to_team = mine;
        ui.frames(3);
        CHECK(does_nothing(app, ui, "Transfer###tpmvtransfer", "##tt_move_"), "his own club: Transfer does nothing");
        in.to_team = 7;
        // an unavailable move is greyed out with the build's plain reason
        json st = read_json(le / "turbo_output" / "bridge_state.json");
        st["unavailable"]["move_release"] = "No release call in this build.";
        write_state(app, ui, le, st);
        ui.frames(2);
        CHECK(does_nothing(app, ui, "Release###tpmvrelease", "##tt_move_"), "an unavailable release cannot be sent");
        lift(app, ui, le, {"move_release"});
        hide(ui);
    });

    run_case("Team-pages: Transfers - the transfer and loan lists send the Transfer Hub actions; sign a player asks first", [&] {
        app.sel_team = mine;
        TeamMoveInputs& in = team_move_inputs();
        in = TeamMoveInputs{};
        in.player = 1005;
        app.sel_player = 1005;
        show(ui, draw_team_transfers);
        if (!list_buttons) {
            const std::pair<const char*, const char*> lists[] = {{"Transfer list###tplst1", "transfer_list"},
                                                                   {"Loan list###tplst2", "loan_list"},
                                                                   {"Remove from lists###tplst3", "unlist"},
                                                                   {"List status###tplst4", "list_status"}};
            for (const auto& l : lists) {
                app.mailbox->cancel();
                CHECK(ui.click(l.first, "##tt_move_"), std::string("button: ") + l.first);
                ui.frames(3);
                json j = sent(mem, kMb);
                json a = first_action(j);
                CHECK(app.mailbox->pending() && j.value("module", "") == "player_moves" && a.value("action", "") == l.second && a.value("playerid", 0LL) == 1005,
                      std::string("sends player_moves ") + l.second + ": " + j.dump());
                app.mailbox->cancel();
                ui.frames(2);
            }
        }
        // sign a player: find by name, ask first, the old transfer action to my club
        const PlayerRow* target = app.model.player(2002);  // not 2001: he is Everton's only goalkeeper, the squad rules would refuse him
        CHECK(target != nullptr && target->club == 7, "player 2002 plays for Everton");
        if (target) {
            in.sign_pick = 2002;
            in.sign_fee = 1000000;
            in.sign_months = 36;
            ui.frames(3);
            json j = ask_and_confirm(app, ui, mem, kMb, "Sign him###tpsignbtn", "##tt_sign_", "Sign him", why);
            json a = first_action(j);
            CHECK(why.empty() && a.value("action", "") == "transfer" && a.value("playerid", 0LL) == 2002 && a.value("to_teamid", 0LL) == mine &&
                      a.value("fee", 0LL) == 1000000 && a.value("months", 0) == 36,
                  "sign sends player_moves transfer to my club: " + why + j.dump());
        }
        in = TeamMoveInputs{};
        hide(ui);
    });

    run_case("Team-pages: Transfers - a contract extension writes the new end year (Undo takes it back); the whole squad asks first", [&] {
        app.sel_team = mine;
        TeamMoveInputs& in = team_move_inputs();
        in = TeamMoveInputs{};
        in.player = 1005;
        app.sel_player = 1005;
        show(ui, draw_team_transfers);
        const Table* pt = app.db.table("players");
        CHECK(pt != nullptr && pt->has("contractvaliduntil"), "the players table has the contract end year");
        if (pt && ten) {
            const int64_t before = app.db.get_int(*pt, ten->rec, "contractvaliduntil", 0);
            CHECK(before >= 2028, fmt("the player's contract ends in %lld", static_cast<long long>(before)));
            in.extend_pick = 2;  // 3 years
            scroll_bottom(ui, true);  // 3.0.1: the 3.0 theme (3db038b7) made the page taller; the Extend rows are below the first screen, as the whole-squad button already was (39d9254f)
            ui.frames(3);
            CHECK(ui.click("Extend###tpext1005", "##tt_extend_"), "Extend, 3 years");
            ui.frames(3);
            const int64_t after = app.db.get_int(*pt, ten->rec, "contractvaliduntil", 0);
            CHECK(after == before + 3, fmt("the contract end year grew by 3: %lld -> %lld", static_cast<long long>(before), static_cast<long long>(after)));
            app.mailbox->cancel();  // the edit may queue the game's plan sync: not what this case looks at
            CHECK(app.undo(1005), "the Undo button has a step for him");
            ui.frames(2);
            CHECK(app.db.get_int(*pt, ten->rec, "contractvaliduntil", 0) == before, "Undo brought the contract end year back");
            CHECK(team_transfers_state().extend_line.find("contract until") != std::string::npos, "the page names the result: " + team_transfers_state().extend_line);
        }
        CHECK(team_transfers_state().own_club, fmt("still my club: club=%lld own=%d in_cm=%d user=%lld sel=%lld", static_cast<long long>(team_transfers_state().club), team_transfers_state().own_club,
                                                    app.bridge.state().in_cm, static_cast<long long>(app.bridge.state().user_team), static_cast<long long>(app.sel_team)));
        // the whole squad: asks first, the old module (the button is below the first screen: scroll down)
        scroll_bottom(ui, true);
        json j = ask_and_confirm(app, ui, mem, kMb, "Whole squad to 3 years###tpextall", "##tt_extend_", "Extend contracts", why);
        CHECK(why.empty() && j.value("module", "") == "extend_user_contracts" && j["overrides"].value("years", 0) == 3,
              "the whole squad sends extend_user_contracts, 3 years: " + why + j.dump());
        in = TeamMoveInputs{};
        hide(ui);
    });

    run_case("Team-pages: Transfers - squad roles: Set and Read send squad_role for that player", [&] {
        app.sel_team = mine;
        TeamMoveInputs& in = team_move_inputs();
        in = TeamMoveInputs{};
        in.role_pick[1005] = 0;  // Crucial
        show(ui, draw_team_transfers);
        scroll_bottom(ui, true);
        app.mailbox->cancel();
        CHECK(ui.click("Set###tprole1005", "##tt_roles_"), "Set role of 1005");
        ui.frames(3);
        json j = sent(mem, kMb);
        CHECK(app.mailbox->pending() && j.value("module", "") == "squad_role" && j["overrides"].value("playerid", 0LL) == 1005 && j["overrides"].value("role", 0) == 1,
              "Set sends squad_role, role 1 (Crucial): " + j.dump());
        app.mailbox->cancel();
        ui.frames(2);
        CHECK(ui.click("Read###tproleread1005", "##tt_roles_"), "Read role of 1005");
        ui.frames(3);
        j = sent(mem, kMb);
        CHECK(j.value("module", "") == "squad_role" && j["overrides"].value("playerid", 0LL) == 1005 && j["overrides"].value("mode", "") == "check",
              "Read sends squad_role mode check: " + j.dump());
        app.mailbox->cancel();
        ui.frames(2);
        in = TeamMoveInputs{};
        hide(ui);
    });

    run_case("Team-pages: Transfers - another club: the lists and roles are own-club only, the extension and the moves still work", [&] {
        app.sel_team = 7;
        TeamMoveInputs& in = team_move_inputs();
        in = TeamMoveInputs{};
        in.player = 2001;
        app.sel_player = 2001;
        in.to_team = mine;
        show(ui, draw_team_transfers);
        const TeamPageState& s = team_transfers_state();
        CHECK(s.club == 7 && !s.own_club, "the page works on Everton");
        CHECK(s.cards == 6, fmt("the six cards are there: %d", s.cards));
        scroll_bottom(ui, true);
        CHECK(count_items("##tt_roles_") <= 2, "no role rows for another club");
        CHECK(!has_item(ui, "Set###tprole2001", "##teampage_test") && !has_item(ui, "Transfer list###tplst1", "##teampage_test"), "no roles and no lists for another club");
        CHECK(!has_item(ui, "Whole squad to 1 year###tpextall", "##teampage_test"), "no whole-squad extension for another club");
        const Table* pt = app.db.table("players");
        const PlayerRow* p2001 = app.model.player(2001);
        if (pt && p2001) {
            const int64_t before = app.db.get_int(*pt, p2001->rec, "contractvaliduntil", 0);
            CHECK(ui.click("Extend###tpext2001", "##tt_extend_"), "Extend works for another club's player");
            ui.frames(3);
            CHECK(app.db.get_int(*pt, p2001->rec, "contractvaliduntil", 0) == before + 1, "his contract end year grew by 1");
            app.undo(2001);
            app.mailbox->cancel();
            ui.frames(2);
        }
        in = TeamMoveInputs{};
        app.sel_team = mine;
        hide(ui);
    });

    run_case("Team-pages: a visible label is short and has no code in it", [&] {
        app.sel_team = mine;
        app.sel_player = 1005;
        for (auto fn : {draw_team_players, draw_team_transfers}) {
            show(ui, fn);
            int checked = 0;
            for (const auto& kv : g_items) {
                const ItemRec& r = kv.second;
                if (r.frame != g_frame || r.window.find("##teampage_test") == std::string::npos || r.label.empty()) continue;
                if (r.window.find("##tt_moves_list") != std::string::npos) continue;  // the moves table is the read-only panel of the parity page
                const size_t hash = r.label.find("##");
                const std::string visible = r.label.substr(0, hash == std::string::npos ? r.label.size() : hash);
                if (visible.empty() || r.label.find("##tpl") != std::string::npos) continue;  // a player row is a name, not a label
                ++checked;
                CHECK(visible.size() <= 34, "label too long (" + std::to_string(visible.size()) + "): " + visible);
                for (const char* bad : {"ID ", "Lua", "hook", "channel", "json", "txt", "md5", "turbo_output", "0x"})
                    CHECK(visible.find(bad) == std::string::npos, std::string("developer word '") + bad + "' in label: " + visible);
            }
            CHECK(checked >= 6, fmt("labels checked: %d", checked));
        }
        hide(ui);
    });

    s8_cases(app, ui, mem, kMb, mine);

    // put the simulated game back as the other cases expect it
    app.mailbox->cancel();
    team_move_inputs() = TeamMoveInputs{};
    app.sel_team = old_sel_team;
    app.sel_player = old_sel_player;
    write_state(app, ui, le, original_state);
    app.mailbox->cancel();
    ImGui::GetIO().DisplaySize = old_display;
    ui.frames(3);
}

}  // namespace page_team_test
