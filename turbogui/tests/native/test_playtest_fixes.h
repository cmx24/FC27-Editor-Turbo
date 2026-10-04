// Native UI tests for the 1.1.1 playtest fixes (included by test_main.cpp, run inside test_ui with its App and Ui):
// Players list club filter, readable names for clubs stored as an unresolved key, Ctrl + wheel zoom, scrollbar sizes.
#pragma once
#include "core/teamnames.h"
#include "ui/ui_team_filter.h"
#include "ui/ui_zoom.h"

static void playtest_fix_cases(App& app, Ui& ui, const fs::path& le) {
    auto sorted_ids = [&] {
        std::vector<int64_t> got = app.list_player_ids;
        std::sort(got.begin(), got.end());
        return got;
    };
    auto expect_ids = [&](const std::function<bool(const PlayerRow&)>& keep) {
        std::vector<int64_t> want;
        for (const auto& p : app.model.players())
            if (keep(p)) want.push_back(p.playerid);
        std::sort(want.begin(), want.end());
        return want;
    };
    auto ids_text = [](const std::vector<int64_t>& v) {
        std::string s;
        for (int64_t x : v) s += std::to_string(x) + " ";
        return s;
    };

    run_case("UI 1.1.1: Players list club filter (pick, type-ahead + Enter, combined with the other filters)", [&] {
        app.request_tab = 0;
        ui.frames(3);
        CHECK(ui.type_into(ui.find("##psearch", "##plist"), ""), "search cleared");
        CHECK(ui.click("Clear##filters", "##plist"), "Clear filters");
        const size_t all = app.model.players().size();
        if (app.list_player_ids.size() != all) ui.click("My club", "##plist");  // left ticked by an earlier case
        CHECK(app.list_player_ids.size() == all, fmt("no filter: %zu of %zu", app.list_player_ids.size(), all));
        CHECK(ui.click("##fteam", "##plist"), "club combo opens");
        CHECK(ui.find("Any club", "##Combo") != nullptr, "Any club entry");
        // oracle: every player linked to the club (club or national team), from the links table
        auto linked = [&](int64_t tid) {
            std::set<int64_t> s;
            for (const LinkRow& l : app.model.links_of_team(tid)) s.insert(l.playerid);
            return s;
        };
        const std::set<int64_t> everton = linked(7), england = linked(1318), inter = linked(241);
        CHECK(everton.count(2001) && england.count(1001) && england.count(2001) && !inter.empty(), "test world links");
        CHECK(ui.click("Everton (7)##team7", "##Combo"), "pick Everton");
        CHECK(sorted_ids() == expect_ids([&](const PlayerRow& p) { return everton.count(p.playerid) > 0; }) && sorted_ids().size() < all,
              "Everton's players: " + ids_text(sorted_ids()));
        // type-ahead: the search box has the focus when the popup opens; Enter picks the first match
        CHECK(ui.click("##fteam", "##plist"), "club combo opens again");
        ui.frames(2);
        ImGui::GetIO().AddInputCharactersUTF8("engl");
        ui.frames(2);
        CHECK(ui.find("England (1318)##team1318", "##Combo") != nullptr && ui.find("Everton (7)##team7", "##Combo") == nullptr,
              "typing narrows the list");
        ui.key(ImGuiKey_Enter);
        ui.frames(2);
        CHECK(sorted_ids() == expect_ids([&](const PlayerRow& p) { return england.count(p.playerid) > 0; }),
              "national team players (club elsewhere): " + ids_text(sorted_ids()));
        // by ID, and combined with Min OVR
        CHECK(ui.click("##fteam", "##plist"), "club combo by ID");
        ui.frames(2);
        ImGui::GetIO().AddInputCharactersUTF8("241");
        ui.frames(2);
        ui.key(ImGuiKey_Enter);
        ui.frames(2);
        CHECK(sorted_ids() == expect_ids([&](const PlayerRow& p) { return inter.count(p.playerid) > 0; }), "club by ID: " + ids_text(sorted_ids()));
        CHECK(ui.type_into(ui.find("Min OVR", "##plist"), "80"), "Min OVR 80");
        CHECK(sorted_ids() == expect_ids([&](const PlayerRow& p) { return inter.count(p.playerid) > 0 && p.overall >= 80; }),
              "club + Min OVR: " + ids_text(sorted_ids()));
        // "My club" replaces the club pick (and the other way round)
        const int64_t mine = app.bridge.state().user_team;
        if (mine > 0) {
            CHECK(ui.click("My club", "##plist"), "My club");
            CHECK(sorted_ids() == expect_ids([&](const PlayerRow& p) { return p.club == mine && p.overall >= 80; }),
                  "My club + Min OVR (club pick dropped): " + ids_text(sorted_ids()));
        }
        CHECK(ui.click("##fteam", "##plist"), "club combo while My club");
        CHECK(ui.click("Everton (7)##team7", "##Combo"), "pick Everton unticks My club");
        CHECK(sorted_ids() == expect_ids([&](const PlayerRow& p) { return everton.count(p.playerid) > 0 && p.overall >= 80; }),
              "Everton + Min OVR, My club off: " + ids_text(sorted_ids()));
        CHECK(ui.click("##fteam", "##plist") && ui.click("Any club", "##Combo"), "Any club");
        CHECK(ui.click("Clear##filters", "##plist"), "Clear");
        CHECK(app.list_player_ids.size() == all, "everyone again");
        // the matcher
        TeamRow t;
        t.teamid = 112264;
        t.name = "KS Cracovia";
        CHECK(team_matches(t, "") && team_matches(t, "crac") && team_matches(t, "1122") && !team_matches(t, "2264") &&
                  !team_matches(t, "legia"),
              "matches part of the name or the start of the ID");
    });

    run_case("UI 1.1.1: clubs stored as an unresolved key (*TeamName_Abbr15_<id>) show a readable name", [&] {
        CHECK(is_unresolved_team_name("*TeamName_Abbr15_112264") && is_unresolved_team_name("*TeamName_112775") &&
                  is_unresolved_team_name("TeamName_Abbr15_7"),
              "keys recognised");
        CHECK(!is_unresolved_team_name("Arsenal") && !is_unresolved_team_name("Inter, Milano") && !is_unresolved_team_name("*") &&
                  !is_unresolved_team_name("*Not a key") && !is_unresolved_team_name("") && !is_unresolved_team_name("TeamName_"),
              "names left alone");
        {
            TeamNamesCsv csv;
            fs::path f = g_out / "readable_names.csv";
            std::ofstream(f, std::ios::binary) << "key;value\nTeamName_Abbr3_7;EVE\nTeamName_Abbr15_7;Everton FC\nTeamName_241;Inter\n"
                                                  "TeamName_Abbr15_241;Inter Milano\nTeamName_Abbr10_99;*TeamName_Abbr10_99\nOther_5;x\n";
            CHECK(csv.load(f), "csv read");
            auto names = readable_team_names(csv);
            CHECK(names.size() == 2 && names[7] == "Everton FC" && names[241] == "Inter",
                  fmt("full name first, then Abbr15 / 10 / 3; keys as values skipped (%zu)", names.size()));
        }
        const fs::path csvf = team_names_file(le);
        const bool had_csv = fs::exists(csvf);
        const std::string csv_before = had_csv ? read_file(csvf) : std::string();
        const Table* t = app.db.table("teams");
        CHECK(t != nullptr && t->field("teamname") != nullptr, "teams.teamname");
        if (!t || !t->field("teamname")) return;
        const uint64_t rec = app.db.find(*t, "teamid", 7);
        Value v;
        CHECK(app.db.get(*t, rec, *t->field("teamname"), v), "read Everton's name");
        const std::string original = v.s;
        std::string err;
        CHECK(app.db.set(*t, rec, *t->field("teamname"), Value::of_str("*TeamName_Abbr15_7"), &err), "store the key: " + err);
        fs::create_directories(csvf.parent_path());
        std::ofstream(csvf, std::ios::binary) << "key;value\nTeamName_1318;England\n";  // nothing for team 7
        CHECK(app.refresh(), "refresh: " + app.db_error);
        CHECK(app.model.team_name(7) == "Team 7", "no other name: Team <id> (" + app.model.team_name(7) + ")");
        const PlayerRow* p = app.model.player(2002);
        CHECK(p && p->club_name == "Team 7", "player list Club column: " + (p ? p->club_name : std::string("?")));
        bool any_key = false;
        for (const auto& tr : app.model.teams()) any_key = any_key || is_unresolved_team_name(tr.name);
        CHECK(!any_key, "no key in the Teams list");
        std::ofstream(csvf, std::ios::binary) << "key;value\nTeamName_1318;England\nTeamName_Abbr15_7;Everton FC\n";
        CHECK(app.refresh(), "refresh with Live Editor's custom name");
        CHECK(app.model.team_name(7) == "Everton FC", "custom team name used: " + app.model.team_name(7));
        p = app.model.player(2002);
        CHECK(p && p->club_name == "Everton FC", "Club column: " + (p ? p->club_name : std::string("?")));
        CHECK(app.model.team_name(1) == "Arsenal", "a readable stored name wins over the file");
        // the Players list and Teams list draw these names
        app.request_tab = 0;
        ui.frames(3);
        app.request_tab = 1;
        ui.frames(3);
        // restore (a refresh rebuilds the table list: look the table up again)
        t = app.db.table("teams");
        const uint64_t rec_now = t ? app.db.find(*t, "teamid", 7) : 0;
        CHECK(t && t->field("teamname") && rec_now && app.db.set(*t, rec_now, *t->field("teamname"), Value::of_str(original), &err),
              "restore the name: " + err);
        if (had_csv) std::ofstream(csvf, std::ios::binary) << csv_before;
        else fs::remove(csvf);
        CHECK(app.refresh() && app.model.team_name(7) == original, "restored: " + app.model.team_name(7));
        app.request_tab = 0;
        ui.frames(2);
    });

    run_case("UI 1.1.1: UI size changes rebuild the style from unscaled sizes (thin scrollbars at every size)", [&] {
        app.ui_scale_user = 1.0f;
        ui.frames(3);
        const ImGuiStyle& st = ImGui::GetStyle();
        const float s0 = app.ui_scale_applied;
        const float sb0 = st.ScrollbarSize, rd0 = st.ScrollbarRounding, gm0 = st.GrabMinSize, wp0 = st.WindowPadding.x;
        CHECK(std::fabs(sb0 - std::floor(12.0f * s0)) < 0.01f && std::fabs(rd0 - std::floor(3.0f * s0)) < 0.01f,
              fmt("thin bars at %.2fx: size %.1f rounding %.1f", s0, sb0, rd0));
        for (float u : {2.0f, 1.3f, 0.7f, 2.5f, 1.15f, 2.0f, 1.0f}) {
            app.ui_scale_user = u;
            ui.frames(2);
            const float s = app.ui_scale_applied;
            CHECK(std::fabs(st.ScrollbarSize - std::floor(12.0f * s)) < 0.01f && std::fabs(st.GrabMinSize - std::floor(12.0f * s)) < 0.01f &&
                      st.ScrollbarRounding <= st.ScrollbarSize * 0.5f,
                  fmt("scale %.2f: scrollbar %.1f grab %.1f rounding %.1f", s, st.ScrollbarSize, st.GrabMinSize, st.ScrollbarRounding));
            CHECK(std::fabs(st.FontScaleDpi - s) < 0.001f && st.FontSizeBase > 0.0f, fmt("font follows %.2f", s));
        }
        CHECK(st.ScrollbarSize == sb0 && st.ScrollbarRounding == rd0 && st.GrabMinSize == gm0 && st.WindowPadding.x == wp0,
              fmt("back at %.2fx: the same sizes as before (scrollbar %.1f, was %.1f)", app.ui_scale_applied, st.ScrollbarSize, sb0));
    });

    run_case("UI 1.1.1: Ctrl + mouse wheel zooms, Ctrl + 0 resets, the size is saved", [&] {
        CHECK(std::fabs(zoom_after(1.0f, 1.0f) - 1.1f) < 0.001f && std::fabs(zoom_after(1.0f, -2.0f) - 0.8f) < 0.001f &&
                  std::fabs(zoom_after(1.0f, 0.5f) - 1.05f) < 0.001f && zoom_after(2.45f, 3.0f) == kUiScaleMax &&
                  zoom_after(0.7f, -9.0f) == kUiScaleMin && zoom_after(1.0f, std::nanf("")) == 1.0f,
              "steps of 0.1, clamped");
        ImGuiIO& io = ImGui::GetIO();
        app.ui_scale_user = 1.0f;
        ui.frames(3);
        auto over_main = [&] {
            ImGuiWindow* w = ImGui::FindWindowByName("FC 27 LE Turbo");
            if (!w) return false;
            io.AddMousePosEvent(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->Size.y * 0.5f);
            ui.frame();
            return true;
        };
        auto wheel = [&](float d) {
            over_main();
            io.AddMouseWheelEvent(0.0f, d);
            ui.frame();
        };
        CHECK(over_main(), "main window");
        wheel(1.0f);
        CHECK(app.ui_scale_user == 1.0f, "the wheel without Ctrl does not zoom");
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        ui.frame();
        wheel(1.0f);
        CHECK(std::fabs(app.ui_scale_user - 1.1f) < 0.001f, fmt("Ctrl + wheel up: 1.10x (%.2f)", app.ui_scale_user));
        ui.frame();
        CHECK(std::fabs(app.ui_scale_applied - 1.1f) < 0.001f && std::fabs(g_ui_scale - 1.1f) < 0.001f &&
                  std::fabs(ImGui::GetStyle().FontScaleDpi - 1.1f) < 0.001f && std::fabs(S(56.0f) - 61.6f) < 0.01f,
              "style, font and picture sizes follow");
        CHECK(ui.toast_contains("UI size 1.10x"), "toast shows the size");
        wheel(2.0f);
        CHECK(std::fabs(app.ui_scale_user - 1.3f) < 0.001f, fmt("two notches: 1.30x (%.2f)", app.ui_scale_user));
        wheel(-5.0f);
        CHECK(std::fabs(app.ui_scale_user - 0.8f) < 0.001f, fmt("five down: 0.80x (%.2f)", app.ui_scale_user));
        size_t zoom_toasts = 0;
        for (const auto& tt : app.toasts) zoom_toasts += tt.text.rfind("UI size ", 0) == 0;
        CHECK(zoom_toasts == 1 && ui.toast_contains("UI size 0.80x"), "one toast, updated in place");
        for (int i = 0; i < 25; ++i) wheel(1.0f);
        CHECK(app.ui_scale_user == kUiScaleMax, fmt("clamped at %.2fx", app.ui_scale_user));
        for (int i = 0; i < 30; ++i) wheel(-1.0f);
        CHECK(app.ui_scale_user == kUiScaleMin, fmt("clamped at %.2fx", app.ui_scale_user));
        // not over a Turbo window: nothing
        ui.frames(2);
        ImGuiWindow* w = ImGui::FindWindowByName("FC 27 LE Turbo");
        CHECK(w && w->Pos.x + w->Size.x < 1900.0f && w->Pos.y + w->Size.y < 1070.0f, "main window leaves the corner free");
        io.AddMousePosEvent(1910.0f, 1075.0f);
        ui.frame();
        io.AddMouseWheelEvent(0.0f, 1.0f);
        ui.frame();
        CHECK(app.ui_scale_user == kUiScaleMin, "Ctrl + wheel outside Turbo does not zoom");
        // Ctrl + 0
        over_main();
        ui.key(ImGuiKey_0, true);  // releases Ctrl too
        CHECK(app.ui_scale_user == 1.0f, fmt("Ctrl + 0: 1.00x (%.2f)", app.ui_scale_user));
        // the Windows host hands over the wheel (Ctrl polled there) and Ctrl + 0
        over_main();
        app.zoom.wheel = 2.0f;
        ui.frame();
        CHECK(std::fabs(app.ui_scale_user - 1.2f) < 0.001f, fmt("host wheel: 1.20x (%.2f)", app.ui_scale_user));
        // saved once the wheel rests for a second
        ui.frames(75);
        json saved = read_json(le / "turbo_output" / "gui_settings.json");
        CHECK(std::fabs(saved["gui"]["ui_scale"].get<double>() - 1.2) < 0.001, "saved: " + saved["gui"].dump());
        CHECK(app.zoom.save_at == 0.0, "nothing more to save");
        app.zoom.reset = true;
        ui.frame();
        CHECK(app.ui_scale_user == 1.0f, "host Ctrl + 0");
        ui.frames(75);
        saved = read_json(le / "turbo_output" / "gui_settings.json");
        CHECK(std::fabs(saved["gui"]["ui_scale"].get<double>() - 1.0) < 0.001, "reset saved: " + saved["gui"].dump());
        // in game the host polls Ctrl itself: Dear ImGui's Ctrl state is then not used for the zoom
        app.zoom.by_host = true;
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        ui.frame();
        wheel(1.0f);
        ui.key(ImGuiKey_0, true);
        CHECK(app.ui_scale_user == 1.0f, fmt("host mode: Dear ImGui's Ctrl + wheel ignored (%.2f)", app.ui_scale_user));
        over_main();
        app.zoom.wheel = -1.0f;
        ui.frame();
        CHECK(std::fabs(app.ui_scale_user - 0.9f) < 0.001f, fmt("host mode: the host's wheel zooms (%.2f)", app.ui_scale_user));
        app.zoom.reset = true;
        ui.frame();
        CHECK(app.ui_scale_user == 1.0f, "host mode: the host's Ctrl + 0");
        app.zoom.by_host = false;
        ui.frames(75);
        // hidden: the host's wheel is dropped
        app.visible = false;
        app.zoom.wheel = 3.0f;
        ui.frame();
        app.visible = true;
        ui.frames(2);
        CHECK(app.ui_scale_user == 1.0f && app.zoom.wheel == 0.0f, "no zoom while hidden");
        App again(app.mem, le, 0, "zoom reload");
        CHECK(std::fabs(again.ui_scale_user - 1.0f) < 0.001f, "loaded at start");
    });
}
