// Native tests (2.22 calc-ovr, brief 2.22-calc-ovr): the calculated overall next to the stored one. Included by test_main.cpp after
// test_squad_table_restore.h; calc_ovr_test::ui_cases runs inside the UI block (a real App on the simulated game).
//   (a) every PlayerRow.calc_overall equals overall::compute_overall(preferredposition1, the 34 attributes) read from the database,
//       -1 when the position is not a game position;
//   (b) an attribute edit followed by Model::refresh_player updates it (and puts it back); the stored overall is never written;
//   (c) the Players list sorts by Calc OVR and by Delta (calc - stored) through its Sort combo (Turbo X: was two sortable headers);
//   (d) the squad table has the same "Calc OVR" header, sortable, and orders its rows by it.
// Cases are named "Calc OVR: ..." so TURBO_TEST_ONLY="calc ovr" runs them alone.
#pragma once
#include "core/overall.h"
#include "ui/ui_team_pages.h"
#include "ui/ui_shell.h"
#include "ui/ui_player_profile.h"
#include "ui/ui_team_filter.h"

namespace calc_ovr_test {

using namespace turbo;

static int expected_calc(App& app, const PlayerRow& p) {
    const Table* t = app.db.table("players");
    if (!t || p.position < 0 || p.position > 27) return -1;
    overall::Attrs a{};
    for (int i = 0; i < overall::kAttrCount; ++i) {
        if (!t->has(overall::kAttrNames[i])) return -1;
        a[static_cast<size_t>(i)] = static_cast<int>(app.db.get_int(*t, p.rec, overall::kAttrNames[i], 0));
    }
    return overall::compute_overall(p.position, a);
}

// The player ids drawn in the Players list this frame, top to bottom (the row's Selectable is labelled with the ID)
static std::vector<int64_t> list_order() {
    std::vector<std::pair<float, int64_t>> rows;
    for (const auto& kv : g_items) {
        const ItemRec& r = kv.second;
        if (r.frame != g_frame || r.window.find("##players") == std::string::npos || r.label.empty()) continue;
        if (!std::all_of(r.label.begin(), r.label.end(), ::isdigit)) continue;
        rows.push_back({r.rect.Min.y, std::atoll(r.label.c_str())});
    }
    std::sort(rows.begin(), rows.end());
    std::vector<int64_t> out;
    for (const auto& r : rows) out.push_back(r.second);
    return out;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    using shell::Page;
    const int64_t old_sel_player = app.sel_player;
    const int64_t old_sel_team = app.sel_team;
    const int was_tab = shell::player_edit_tab();

    run_case("Calc OVR: every model row holds compute_overall(position, attributes), -1 when it cannot be worked out", [&] {
        CHECK(app.model.players().size() > 3, "the test world has players");
        int computable = 0;
        std::set<int> distinct;
        for (const PlayerRow& p : app.model.players()) {
            const int want = expected_calc(app, p);
            CHECK(p.calc_overall == want, fmt("player %lld: calc %d want %d", static_cast<long long>(p.playerid), p.calc_overall, want));
            if (const Table* pt = app.db.table("players"); pt && pt->has("modifier"))
                CHECK(p.ovr_modifier == static_cast<int>(app.db.get_int(*pt, p.rec, "modifier", 0)), "Mod column holds players.modifier");
            if (p.calc_overall >= 0) {
                ++computable;
                distinct.insert(p.calc_overall);
                CHECK(p.calc_overall >= 1 && p.calc_overall <= 99, "1..99");
            }
        }
        CHECK(computable > 3 && distinct.size() > 1, fmt("several players with different calc values: %d / %zu", computable, distinct.size()));
    });

    run_case("Calc OVR: an attribute edit then a refresh moves calc_overall; the stored overall is untouched", [&] {
        const PlayerRow* p = app.model.player(1005);
        const Table* t = app.db.table("players");
        CHECK(p != nullptr && t != nullptr && t->has("acceleration"), "player 1005 and the attribute");
        if (!p || !t) return;
        const uint64_t rec = p->rec;
        const int stored = p->overall, calc0 = p->calc_overall;
        const int64_t acc = app.db.get_int(*t, rec, "acceleration", 0);
        std::string err;
        const int64_t other = acc > 50 ? 1 : 99;
        CHECK(app.db.set_int(*t, rec, "acceleration", other, &err), "write acceleration: " + err);
        app.model.refresh_player(1005, app.today());
        const PlayerRow* q = app.model.player(1005);
        CHECK(q != nullptr && q->calc_overall == expected_calc(app, *q), "calc follows the new attribute");
        CHECK(q != nullptr && q->overall == stored, "the stored overall was not written");
        CHECK(app.db.set_int(*t, rec, "acceleration", acc, &err), "put it back: " + err);
        app.model.refresh_player(1005, app.today());
        CHECK(app.model.player(1005)->calc_overall == calc0, "back to the first value");
    });

    // Turbo X (2026-10-31, DESIGN_SPEC_X.md): the 11-column table is a list of people rows; its sortable headers are the "##psort" combo
    // (Name, OVR, POT, Age, Calc OVR, Delta, Club, ID; numbers best first), and Calc OVR / Delta / Mod are in the row's tooltip. The
    // "second click reverses" checks went with the headers.
    run_case("Calc OVR: the Players list sorts by Calc OVR and Delta (the Sort combo), best first", [&] {
        shell::player_edit_tab() = 0;
        app.sel_player = 1001;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        const ItemRec* sort = ui.find("##psort", "##select");
        CHECK(sort != nullptr, "the Sort combo is in the list pane");
        {
            const std::string help = kCalcOvrHelp;
            CHECK(help.find("Delta") != std::string::npos && help.find("-5") != std::string::npos && help.find("modifier") != std::string::npos, "tooltip explains Delta and the -5 modifier");
            for (char ch : help) CHECK(static_cast<unsigned char>(ch) < 128, "tooltip is ASCII");
            for (const char* tip : {kModColumnHelp, kOverallModifierLine, kOvrModifierHelp}) {
                const std::string t = tip;
                CHECK(!t.empty() && t.find("modifier") != std::string::npos, "Mod / Overall / OVR modifier tooltip is non-empty and names the modifier");
                for (char ch : t) CHECK(static_cast<unsigned char>(ch) < 128, "modifier tooltip is ASCII");
            }
        }
        if (!sort) return;
        auto key_calc = [&](int64_t id) { return app.model.player(id)->calc_overall; };
        auto key_delta = [&](int64_t id) { return calc_delta(*app.model.player(id)); };
        auto ordered = [&](bool desc, const std::function<int(int64_t)>& key) {
            const std::vector<int64_t> o = list_order();
            if (o.size() <= 3) return false;
            for (size_t i = 1; i < o.size(); ++i)
                if (desc ? key(o[i - 1]) < key(o[i]) : key(o[i - 1]) > key(o[i])) return false;
            return true;
        };
        CHECK(ui.click("##psort", "##select") && ui.click("Calc OVR", "##Combo"), "Sort: Calc OVR");
        ui.frames(3);
        CHECK(ordered(true, key_calc), "Calc OVR: best first");
        CHECK(ui.click("##psort", "##select") && ui.click("Delta", "##Combo"), "Sort: Delta");
        ui.frames(3);
        CHECK(ordered(true, key_delta), "Delta: largest calc - stored first");
        // leave the list on a plain sort for the cases after this one
        CHECK(ui.click("##psort", "##select") && ui.click("Name", "##Combo"), "Sort: Name");
        ui.frames(3);
    });

    // 1.0.1 R2-F (owner: filter permanence): the Players list's chips, filter form and sort, and the Clubs list's country / league, are kept in
    // gui_settings.json list_filters (the store of asset_scope) and come back after a restart; nothing stored = the defaults, nothing written.
    run_case("UI: 1.0.1 R2-F list filters: Players chips / filters / sort and Clubs country / league kept over a restart; first run = defaults", [&] {
        auto stored = [&](const char* list) {
            const auto& g = app.gui_settings;
            return g.is_object() && g.contains("list_filters") && g["list_filters"].is_object() && g["list_filters"].contains(list);
        };
        auto restart = [&] {  // the settings file read again and both lists' memory wiped: what a new process sees
            app.load_gui_settings();
            classic_player_list_set_filters(nlohmann::json::object());
            classic_player_list_reload_filters();
            club_list_set_filters(nlohmann::json::object());
            club_list_reload_filters();
        };
        auto draw_clubs = [&app]() {
            ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(900.0f, 500.0f), ImGuiCond_Always);
            if (ImGui::Begin("R2F clubs")) draw_teams(app);
            ImGui::End();
        };
        // first run: nothing stored
        if (app.gui_settings.is_object()) app.gui_settings.erase("list_filters");
        app.save_gui_settings();
        restart();
        shell::player_edit_tab() = 0;
        app.sel_player = 1001;
        app.go(Page::PlayerEdit);
        ui.extra_draw = draw_clubs;
        ui.frames(4);
        nlohmann::json pf = classic_player_list_filters();
        CHECK(pf["my_club"] == false && pf["group"] == -1 && pf["sort"] == 0 && pf["position"] == -1 && pf["free_agent"] == false && pf["club"] == 0 &&
                  pf["nationality"] == -1 && pf["loan"] == 0 && pf["age"] == nlohmann::json::array({0, 0}),
              "Players list, first run: the defaults (no chip, Sort Name, no filter): " + pf.dump());
        nlohmann::json cf = club_list_filters();
        CHECK(cf["country"] == -1 && cf["league"] == -1, "Clubs list, first run: any country, any league: " + cf.dump());
        CHECK(!stored("players") && !stored("clubs"), "drawing both lists wrote nothing to list_filters");

        // the person's clicks: Free agents, DEF, Sort OVR; and the form's position / age / nationality (as the form sets them)
        CHECK(ui.click("Free agents###pl_chip_free", "##select"), "click the Free agents chip");
        ui.frames(2);
        CHECK(ui.click("DEF###pl_chip_def", "##select"), "click the DEF chip");
        ui.frames(2);
        CHECK(ui.click("##psort", "##select") && ui.click("OVR", "##Combo"), "Sort: OVR");
        ui.frames(3);
        pf = classic_player_list_filters();
        CHECK(pf["free_agent"] == true && pf["group"] == 1 && pf["sort"] == 1, "the clicks took: " + pf.dump());
        CHECK(stored("players") && app.gui_settings["list_filters"]["players"] == pf, "the clicked chips and sort went into list_filters.players");
        pf["position"] = 5;
        pf["age"] = nlohmann::json::array({20, 30});
        pf["nationality"] = 14;
        classic_player_list_set_filters(pf);
        club_list_set_filters({{"country", 14}, {"league", 13}});
        ui.frames(3);
        const nlohmann::json want_p = classic_player_list_filters(), want_c = club_list_filters();
        CHECK(want_p["position"] == 5 && want_c["country"] == 14 && want_c["league"] == 13, "the form / combo values took");
        CHECK(stored("players") && stored("clubs") && app.gui_settings["list_filters"]["players"] == want_p && app.gui_settings["list_filters"]["clubs"] == want_c,
              "both lists' filters are in gui_settings list_filters");

        restart();
        ui.frames(4);
        CHECK(classic_player_list_filters() == want_p, "Players list after a restart: chips, form and sort came back: " + classic_player_list_filters().dump());
        CHECK(club_list_filters() == want_c, "Clubs list after a restart: country and league came back: " + club_list_filters().dump());
        CHECK(ui.find("##psort", "##select") != nullptr, "the list still draws");

        // put both back to the defaults (kept too), then remove the entry for the cases after this one
        classic_player_list_set_filters(nlohmann::json::object());
        club_list_set_filters(nlohmann::json::object());
        ui.frames(3);
        CHECK(stored("players") && app.gui_settings["list_filters"]["players"]["sort"] == 0 && app.gui_settings["list_filters"]["clubs"]["league"] == -1,
              "the cleared filters are kept as cleared");
        app.gui_settings.erase("list_filters");
        app.save_gui_settings();
        ui.extra_draw = nullptr;
        ui.frames(2);
    });

    run_case("Calc OVR: the squad table has a sortable Calc OVR header and sorts by it", [&] {
        app.sel_team = 7;
        CHECK(ui.open_page("Club", "Squad"), "Club > Squad (Team > Players)");
        ui.frames(3);
        const ImGuiTable* t = squad_table_test::squad_table_now();
        CHECK(t != nullptr && t->ColumnsCount == 7 && std::string(ImGui::TableGetColumnName(t, 6)) == "Calc OVR", "the seventh column is Calc OVR");
        CHECK(squad_table_test::click_header(ui, "Calc OVR"), "click it");
        const SquadTableState& st = squad_table_state();
        CHECK(st.sort_column == 6 && !st.sort_ascending, fmt("sorted on column 6, best first: %d", st.sort_column));
        const std::vector<int64_t> o = squad_table_test::drawn_order(app, 7);
        CHECK(o.size() == app.model.links_of_team(7).size(), "all rows drawn");
        bool desc = true;
        for (size_t i = 1; i < o.size(); ++i) desc = desc && app.model.player(o[i - 1])->calc_overall >= app.model.player(o[i])->calc_overall;
        CHECK(desc, "rows by Calc OVR, best first");
        CHECK(squad_table_test::click_header(ui, "Calc OVR") && squad_table_test::click_header(ui, "Calc OVR") && squad_table_state().sort_column == -1,
              "a third click returns to the database order");
    });

    app.sel_player = old_sel_player;
    app.sel_team = old_sel_team;
    shell::player_edit_tab() = was_tab;
    ui.frames(2);
}

}  // namespace calc_ovr_test
