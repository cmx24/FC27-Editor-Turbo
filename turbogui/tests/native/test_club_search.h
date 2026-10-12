// Native tests: the Players list's Gender filter and the club chooser (ui_club_search.h: one box for a club's name or ID
// with a dropdown) in Transfer / Loan..., the Clone dialog and Turbo Tools. Included by test_main.cpp, run from test_ui
// after the transfer list / ban cases (a loaded career, your club Arsenal).
#pragma once

static void ui_cases_club_search(App& app, Ui& ui, SimMemory& mem, uint64_t kMb) {
    auto last_cmd = [&]() { return json::parse(mem.read_cstr(kMb + 0x20, 0x1000), nullptr, false); };
    auto sorted_list = [&]() {
        std::vector<int64_t> v = app.list_player_ids;
        std::sort(v.begin(), v.end());
        return v;
    };
    // what the list must show with this gender filter (-1 = any) and minimum overall
    auto expect_gender = [&](int g, int min_ovr) {
        std::vector<int64_t> want;
        for (const auto& p : app.model.players())
            if ((g < 0 || p.gender == g) && p.overall >= min_ovr) want.push_back(p.playerid);
        std::sort(want.begin(), want.end());
        return want == sorted_list();
    };

    run_case("UI: Players list: Gender filter (Any gender / Men / Women), with the other filters, reset by Clear", [&] {
        if (app.busy()) ui.click("Cancel");
        app.request_tab = 0;
        ui.frames(3);
        CHECK(ui.type_into(ui.find("##psearch", "##plist"), ""), "search cleared");
        CHECK(ui.click("Clear##filters", "##plist"), "Clear");
        const size_t all = app.model.players().size();
        CHECK(app.model.player(1005) && app.model.player(1005)->gender == 1 && app.model.player(1001)->gender == 0,
              "players.gender read into the rows (1005 is the world's one woman)");
        CHECK(expect_gender(-1, 0) && app.list_player_ids.size() == all, "Any gender: everyone");
        CHECK(ui.click("##fgender", "##plist"), "gender combo");
        CHECK(ui.click("Women", "##Combo"), "Women");
        CHECK(expect_gender(1, 0) && app.list_player_ids.size() == 1 && app.list_player_ids[0] == 1005, "Women: 1005 only");
        CHECK(ui.click("##fgender", "##plist") && ui.click("Men", "##Combo"), "Men");
        CHECK(expect_gender(0, 0) && app.list_player_ids.size() == all - 1, "Men: everyone else");
        CHECK(ui.type_into(ui.find("##ovr_from", "##plist"), "87"), "OVR from 87 (filter grid)");
        CHECK(expect_gender(0, 87) && !app.list_player_ids.empty(), "Men with OVR >= 87");
        CHECK(ui.click("##fgender", "##plist") && ui.click("Women", "##Combo"), "Women again");
        CHECK(app.list_player_ids.empty(), "no woman with OVR >= 87 (1005 is 80)");
        CHECK(ui.click("Clear##filters", "##plist"), "Clear");
        CHECK(app.list_player_ids.size() == all, "Clear resets the gender filter with the others");
    });

    run_case("UI: Transfer / Loan... destination by club name or ID: dropdown and click, Enter, Up / Down, leaving the box, Esc", [&] {
        if (app.busy()) ui.click("Cancel");
        app.request_tab = 0;
        ui.frames(2);
        auto open_moves = [&]() {  // the popup's club box (opens the popup when it is closed)
            if (!ui.find("To club")) {
                CHECK(ui.click("Transfer / Loan...", "##pedit"), "moves popup");
                ui.frames(2);
            }
            return ui.find("To club");
        };
        auto type_in = [&](const ItemRec* r, const char* text) {  // click the box, select its text, type (no Enter)
            CHECK(ui.click(r), std::string("click the box for '") + text + "'");
            ui.key(ImGuiKey_A, true);
            ImGui::GetIO().AddInputCharactersUTF8(text);
            ui.frames(3);
        };
        auto transfer_to = [&]() -> int64_t {  // press Transfer: the destination sent, or -1 when nothing was sent
            CHECK(ui.click("Transfer", "##Popup"), "Transfer");
            if (!app.busy()) return -1;
            const json cmd = last_cmd();
            ui.click("Cancel");
            return cmd.is_discarded() ? -2 : cmd["overrides"]["actions"][0].value("to_teamid", int64_t(-3));
        };
        CHECK(ui.click("2002", "##plist"), "Everton player 2002");
        // part of a name: the dropdown lists the matching clubs only; a click on a row picks it
        const ItemRec* box = open_moves();
        CHECK(box != nullptr, "the club box");
        type_in(box, "mila");
        CHECK(ui.find("Inter, Milano##club241") != nullptr, "Inter, Milano listed");
        CHECK(!ui.find("Everton##club7") && !ui.find("Arsenal##club1") && !ui.find("England##club1318"), "only the clubs matching 'mila'");
        CHECK(ui.click("Inter, Milano##club241"), "click Inter's row");
        ui.frames(2);
        CHECK(!ui.find("Inter, Milano##club241"), "the dropdown closed with the pick");
        CHECK(ui.find("Transfer", "##Popup") != nullptr, "the moves popup stays open");
        CHECK(transfer_to() == 241, "transfer to Inter (241)");
        // Enter takes the best match: a number lists the club with exactly that ID first (1 before 1318 and 111592)
        open_moves();
        CHECK(ui.type_into(ui.find("To club"), "1"), "type 1, Enter");
        CHECK(transfer_to() == 1, "transfer to Arsenal (ID 1)");
        open_moves();
        CHECK(ui.type_into(ui.find("To club"), "ars"), "type ars, Enter");
        CHECK(transfer_to() == 1, "a name start: Arsenal");
        // Up / Down move the highlighted row; 'a': Arsenal (name starts with it), then England, Free Agents, Inter, Milano
        type_in(open_moves(), "a");
        CHECK(ui.find("Arsenal##club1") && ui.find("England##club1318") && ui.find("Free Agents##club111592") && !ui.find("Everton##club7"),
              "the clubs with an 'a'");
        ui.key(ImGuiKey_DownArrow);
        ui.key(ImGuiKey_DownArrow);
        ui.key(ImGuiKey_UpArrow);
        ui.key(ImGuiKey_Enter);
        ui.frames(2);
        // England is a national team: still picked by name, flagged, and the move says why (nothing sent)
        app.toasts.clear();
        CHECK(transfer_to() == -1, "national team: nothing sent");
        CHECK(ui.toast_contains("That is a national team"), "the reason is shown");
        ui.frames(2);
        // leaving the box after typing (here a click outside the popup) takes the highlighted club
        type_in(open_moves(), "inter");
        CHECK(ui.click("1002", "##plist"), "a click outside closes the popup");
        ui.frames(2);
        CHECK(ui.click("2002", "##plist"), "2002 again");
        open_moves();
        CHECK(transfer_to() == 241, "the club typed before leaving: Inter");
        // Escape keeps the club chosen before
        type_in(open_moves(), "ars");
        CHECK(ui.find("Arsenal##club1") != nullptr, "Arsenal listed");
        ui.key(ImGuiKey_Escape);
        ui.frames(2);
        CHECK(!ui.find("Arsenal##club1"), "Esc closed the dropdown");
        open_moves();
        CHECK(transfer_to() == 241, "Esc: still Inter");
        // a number no club has is kept and refused with the reason
        open_moves();
        CHECK(ui.type_into(ui.find("To club"), "99999"), "type 99999, Enter");
        app.toasts.clear();
        CHECK(transfer_to() == -1 && ui.toast_contains("no team has this ID"), "unknown ID: nothing sent, says why");
        ui.click("2001", "##plist");
        ui.frames(2);
        CHECK(ui.click("2001", "##plist"), "close the popup, select 2001");
        ui.frames(2);
    });

    run_case("UI: the club box in the Clone dialog and in Turbo Tools (reveal, jersey numbers, bulk edit team list)", [&] {
        if (app.busy()) ui.click("Cancel");
        app.request_tab = 0;
        ui.frames(2);
        CHECK(ui.click("1002", "##plist"), "player 1002");
        CHECK(ui.click("Clone...", "##pedit"), "Clone dialog");
        ui.frames(2);
        const ItemRec* club = ui.find("Club", "##pclone");
        CHECK(club != nullptr, "the club box of the dialog");
        CHECK(ui.click(club), "click it");
        ui.key(ImGuiKey_A, true);
        ImGui::GetIO().AddInputCharactersUTF8("ever");
        ui.frames(3);
        CHECK(ui.click("Everton##club7"), "pick Everton in the dropdown");
        ui.frames(2);
        CHECK(ui.click("Clone player", "##pclone"), "Clone player");
        CHECK(app.busy() && last_cmd()["overrides"].value("teamid", int64_t(0)) == 7, "clone into Everton: " + last_cmd().dump());
        ui.click("Cancel");
        // back to the default (Free Agents) for the cases after this one, by ID
        CHECK(ui.click("Clone...", "##pedit"), "Clone dialog again");
        ui.frames(2);
        CHECK(ui.type_into(ui.find("Club", "##pclone"), "111592"), "Free Agents by ID");
        CHECK(ui.click("Cancel##pclone", "##pclone"), "close the dialog");
        ui.frames(2);

        CHECK(ui.click("Turbo Tools"), "Tools tab");
        ui.frames(2);
        CHECK(ui.click("Scouting, development and youth academy"), "open Scouting");
        ui.frames(2);
        CHECK(ui.find("Reveal club##rv") == nullptr, "no Reveal club button before a club is picked");
        CHECK(ui.type_into(ui.find("##rvteam"), "inter"), "reveal: type inter, Enter");
        CHECK(ui.click("Reveal club##rv"), "Reveal club");
        CHECK(app.busy() && last_cmd()["overrides"]["scope"].value("teamid", int64_t(0)) == 241, "reveal Inter: " + last_cmd().dump());
        ui.click("Cancel");
        CHECK(ui.click("Scouting, development and youth academy"), "close Scouting");
        CHECK(ui.click("Exports (CSV in turbo_output)"), "open Exports");
        ui.frames(2);
        CHECK(ui.type_into(ui.find("##jerseyteam"), "everton"), "jersey numbers: Everton");
        CHECK(ui.click("Jersey numbers"), "Jersey numbers");
        CHECK(app.busy() && last_cmd()["overrides"].value("teamid", int64_t(-1)) == 7, "jersey numbers of Everton");
        ui.click("Cancel");
        CHECK(ui.type_into(ui.find("##jerseyteam"), ""), "emptied: your club again");
        CHECK(ui.click("Jersey numbers"), "Jersey numbers");
        CHECK(app.busy() && last_cmd()["overrides"].value("teamid", int64_t(-1)) == 0, "jersey numbers of your club (0)");
        ui.click("Cancel");
        CHECK(ui.click("Exports (CSV in turbo_output)"), "close Exports");
        // bulk edit: a club picked by name joins the team ID list once (the field and action of the earlier case are still set)
        CHECK(ui.click("Bulk edit players"), "open Bulk edit");
        ui.frames(2);
        CHECK(ui.click("Team IDs##be"), "Team IDs scope");
        ui.frames(2);
        CHECK(ui.type_into(ui.find("##beaddteam"), "everton"), "add Everton");
        CHECK(ui.type_into(ui.find("##beaddteam"), "ars"), "add Arsenal");
        CHECK(ui.type_into(ui.find("##beaddteam"), "7"), "Everton again (kept once)");
        CHECK(ui.click("Apply bulk edit"), "Apply bulk edit");
        CHECK(app.busy() && last_cmd()["overrides"]["scope"]["teamids"] == json::array({7, 1}), "team list 7, 1: " + last_cmd().dump());
        ui.click("Cancel");
        app.toasts.clear();
        CHECK(ui.type_into(ui.find("##beaddteam"), "99999"), "an ID no club has");
        CHECK(ui.toast_contains("no club has ID 99999"), "says so, adds nothing");
        CHECK(ui.type_into(ui.find("##beteams"), ""), "list emptied");
        CHECK(ui.click("My squad##be"), "back to My squad");
        CHECK(ui.click("Bulk edit players"), "close Bulk edit");
        app.request_tab = 0;
        ui.frames(2);
    });
}
