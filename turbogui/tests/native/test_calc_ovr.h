// Native tests (2.22 calc-ovr, brief 2.22-calc-ovr): the calculated overall next to the stored one. Included by test_main.cpp after
// test_squad_table_restore.h; calc_ovr_test::ui_cases runs inside the UI block (a real App on the simulated game).
//   (a) every PlayerRow.calc_overall equals overall::compute_overall(preferredposition1, the 34 attributes) read from the database,
//       -1 when the position is not a game position;
//   (b) an attribute edit followed by Model::refresh_player updates it (and puts it back); the stored overall is never written;
//   (c) the Players list table has the sortable headers "Calc OVR" and "Delta"; Calc OVR sorts the rows by it, Delta by calc - stored;
//   (d) the squad table has the same "Calc OVR" header, sortable, and orders its rows by it.
// Cases are named "Calc OVR: ..." so TURBO_TEST_ONLY="calc ovr" runs them alone.
#pragma once
#include "core/overall.h"
#include "ui/ui_team_pages.h"
#include "ui/ui_shell.h"

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

// The player ids drawn in the Players list table this frame, top to bottom (the ID column is the row's Selectable)
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

    run_case("Calc OVR: the Players list has sortable Calc OVR and Delta headers and sorts by them", [&] {
        shell::player_edit_tab() = 0;
        app.sel_player = 1001;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        const ItemRec* h1 = ui.find("Calc OVR", "##players");
        const ItemRec* h2 = ui.find("Delta", "##players");
        CHECK(h1 != nullptr && h2 != nullptr, "both headers are in the table");
        ImGuiTable* tb = nullptr;
        {
            ImGuiContext& g = *GImGui;
            for (int i = 0; i < g.Tables.GetMapSize(); ++i) {
                ImGuiTable* t = g.Tables.TryGetMapData(i);
                if (t && t->LastFrameActive >= g.FrameCount - 1 && t->InnerWindow && std::strstr(t->InnerWindow->Name, "##players")) tb = t;
            }
        }
        CHECK(tb != nullptr && tb->ColumnsCount == 11 && (tb->Flags & ImGuiTableFlags_Sortable), "eleven sortable columns");
        if (tb && tb->ColumnsCount == 11)
            CHECK(std::string(ImGui::TableGetColumnName(tb, 8)) == "Calc OVR" && std::string(ImGui::TableGetColumnName(tb, 9)) == "Delta",
                  "appended after Age");
        if (tb && tb->ColumnsCount == 11)
            CHECK(std::string(ImGui::TableGetColumnName(tb, 10)) == "Mod", "Mod appended after Delta");
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
        if (!h1 || !h2) return;
        auto key_calc = [&](int64_t id) { return app.model.player(id)->calc_overall; };
        auto key_delta = [&](int64_t id) { return calc_delta(*app.model.player(id)); };
        auto ordered = [&](bool desc, const std::function<int(int64_t)>& key) {
            const std::vector<int64_t> o = list_order();
            if (o.size() <= 3) return false;
            for (size_t i = 1; i < o.size(); ++i)
                if (desc ? key(o[i - 1]) < key(o[i]) : key(o[i - 1]) > key(o[i])) return false;
            return true;
        };
        ui.click(h1);
        ui.frames(3);
        CHECK(ordered(true, key_calc), "Calc OVR: best first (the first click of a number column)");
        ui.click(ui.find("Calc OVR", "##players"));
        ui.frames(3);
        CHECK(ordered(false, key_calc), "Calc OVR: second click reverses");
        ui.click(ui.find("Delta", "##players"));
        ui.frames(3);
        CHECK(ordered(true, key_delta), "Delta: largest calc - stored first");
        ui.click(ui.find("Delta", "##players"));
        ui.frames(3);
        CHECK(ordered(false, key_delta), "Delta: reversed");
        // leave the list on a plain sort for the cases after this one
        ui.click(ui.find("Name", "##players"));
        ui.frames(3);
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
