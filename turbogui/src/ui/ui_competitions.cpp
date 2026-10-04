// FC 27 LE Turbo GUI - Competitions tab. Two views: the game engine's live table rows (ui_standings.cpp, what FC 27's
// Standings screen shows) and the career database's copy (leagueteamlinks, kept in the save; FC 27's Standings screen
// does not read it - seen in game, 03-10-2026).
#include <algorithm>
#include <cstdio>
#include <map>

#include "app.h"
#include "imgui.h"

namespace turbo {

struct LeagueRow {
    uint64_t rec = 0;
    int64_t teamid = 0;
    std::string team;
    int w = 0, d = 0, l = 0, gf = 0, ga = 0, pts = 0, pos = 0, played = 0;
};

static std::vector<LeagueRow> league_rows(App& app, const Table& t, int64_t league) {
    std::vector<LeagueRow> out;
    Snapshot snap;
    if (!snap.load(app.db.memory(), t)) return out;
    for (uint32_t idx : snap.valid) {
        if (snap.get_int(idx, "leagueid", -1) != league) continue;
        LeagueRow r;
        r.rec = snap.addr(idx);
        r.teamid = snap.get_int(idx, "teamid", 0);
        if (r.teamid <= 0) continue;
        r.team = app.model.team_name(r.teamid);
        r.w = int(snap.get_int(idx, "homewins") + snap.get_int(idx, "awaywins"));
        r.d = int(snap.get_int(idx, "homedraws") + snap.get_int(idx, "awaydraws"));
        r.l = int(snap.get_int(idx, "homelosses") + snap.get_int(idx, "awaylosses"));
        r.gf = int(snap.get_int(idx, "homegf") + snap.get_int(idx, "awaygf"));
        r.ga = int(snap.get_int(idx, "homega") + snap.get_int(idx, "awayga"));
        r.pts = int(snap.get_int(idx, "points"));
        r.pos = int(snap.get_int(idx, "currenttableposition"));
        r.played = int(snap.get_int(idx, "nummatchesplayed"));
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(), [](const LeagueRow& a, const LeagueRow& b) {
        if (a.pos != b.pos) return a.pos < b.pos;
        return a.team < b.team;
    });
    return out;
}

// Points (3 per win, 1 per draw), played games, then table positions by points, goal difference, goals scored
bool recalc_league(App& app, int64_t league, bool positions, std::string* msg) {
    const Table* t = app.db.table("leagueteamlinks");
    if (!t) {
        if (msg) *msg = "This database has no leagueteamlinks table.";
        return false;
    }
    std::vector<LeagueRow> rows = league_rows(app, *t, league);
    if (rows.empty()) {
        if (msg) *msg = "no clubs in this league";
        return false;
    }
    int written = 0;
    for (auto& r : rows) {
        int pts = r.w * 3 + r.d, played = r.w + r.d + r.l;
        if (t->has("points") && r.pts != pts && app.db.set_int(*t, r.rec, "points", pts)) ++written;
        if (t->has("nummatchesplayed") && r.played != played && app.db.set_int(*t, r.rec, "nummatchesplayed", played)) ++written;
        r.pts = pts;
    }
    if (positions && t->has("currenttableposition")) {
        std::stable_sort(rows.begin(), rows.end(), [](const LeagueRow& a, const LeagueRow& b) {
            if (a.pts != b.pts) return a.pts > b.pts;
            if (a.gf - a.ga != b.gf - b.ga) return a.gf - a.ga > b.gf - b.ga;
            if (a.gf != b.gf) return a.gf > b.gf;
            return a.team < b.team;
        });
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].pos != int(i + 1) && app.db.set_int(*t, rows[i].rec, "currenttableposition", int64_t(i + 1))) ++written;
        }
    }
    ++app.gen;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%zu clubs checked, %d values written", rows.size(), written);
    if (msg) *msg = buf;
    return true;
}

static void draw_database_copy(App& app);

void draw_competitions(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    if (ImGui::BeginTabBar("##compviews")) {
        if (ImGui::BeginTabItem("Live standings (game)")) {
            draw_live_standings(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Career database copy")) {
            draw_database_copy(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

static void draw_database_copy(App& app) {
    const Table* t = app.db.table("leagueteamlinks");
    if (!t) {
        ImGui::TextDisabled("This database has no leagueteamlinks table.");
        return;
    }
    static int64_t league = -1;
    static int64_t sel_team = 0;
    static int built_gen = -1;
    static std::map<int64_t, std::string> leagues;
    if (built_gen != app.gen) {
        built_gen = app.gen;
        leagues.clear();
        Snapshot snap;
        if (snap.load(app.db.memory(), *t)) {
            for (uint32_t idx : snap.valid) {
                int64_t id = snap.get_int(idx, "leagueid", -1);
                if (id >= 0) leagues[id] = "";
            }
        }
        if (const Table* lt = app.db.table("leagues")) {
            Snapshot ls;
            const Field* nf = lt->field("leaguename");
            if (nf && ls.load(app.db.memory(), *lt)) {
                for (uint32_t idx : ls.valid) {
                    int64_t id = ls.get_int(idx, "leagueid", -1);
                    auto it = leagues.find(id);
                    if (it != leagues.end()) it->second = ls.get_str(idx, *nf);
                }
            }
        }
        if (!leagues.count(league)) league = leagues.empty() ? -1 : leagues.begin()->first;
    }
    auto label = [&](int64_t id) {
        auto it = leagues.find(id);
        std::string n = it != leagues.end() && !it->second.empty() ? it->second : "League";
        return n + " (" + std::to_string(id) + ")";
    };
    ImGui::SetNextItemWidth(S(320.0f));
    if (ImGui::BeginCombo("League", league >= 0 ? label(league).c_str() : "none", ImGuiComboFlags_HeightLarge)) {
        for (const auto& kv : leagues)
            if (ImGui::Selectable(label(kv.first).c_str(), kv.first == league)) {
                league = kv.first;
                sel_team = 0;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Recalculate points")) {
        std::string msg;
        bool ok = recalc_league(app, league, false, &msg);
        app.notify("Points: " + msg, !ok);
    }
    ImGui::SameLine();
    if (ImGui::Button("Write table positions")) {
        std::string msg;
        bool ok = recalc_league(app, league, true, &msg);
        app.notify("Table positions: " + msg, !ok);
    }
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                       "These are the career database's league numbers (kept in the save). FC 27's own Standings screen shows "
                       "the 'Live standings (game)' view instead.");

    std::vector<LeagueRow> rows = league >= 0 ? league_rows(app, *t, league) : std::vector<LeagueRow>();
    ImGui::BeginChild("##ltable", ImVec2(S(560.0f), 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##league", 10, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        const char* heads[] = {"Pos", "Club", "P", "W", "D", "L", "GF", "GA", "GD", "Pts"};
        for (int i = 0; i < 10; ++i)
            ImGui::TableSetupColumn(heads[i], i == 1 ? ImGuiTableColumnFlags_WidthStretch : ImGuiTableColumnFlags_WidthFixed,
                                    i == 1 ? 0.0f : S(34.0f));
        ImGui::TableHeadersRow();
        for (const auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char idb[48];
            std::snprintf(idb, sizeof(idb), "%d##lt%lld", r.pos, static_cast<long long>(r.teamid));
            if (ImGui::Selectable(idb, sel_team == r.teamid, ImGuiSelectableFlags_SpanAllColumns)) sel_team = r.teamid;
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.team.c_str());
            int vals[] = {r.played, r.w, r.d, r.l, r.gf, r.ga, r.gf - r.ga, r.pts};
            for (int v : vals) {
                ImGui::TableNextColumn();
                ImGui::Text("%d", v);
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##lteam");
    const LeagueRow* cur = nullptr;
    for (const auto& r : rows)
        if (r.teamid == sel_team) cur = &r;
    if (!cur) {
        ImGui::TextDisabled("Select a club on the left to edit its line.");
    } else {
        ImGui::Text("%s", cur->team.c_str());
        ImGui::Separator();
        field_grid(app, *t, cur->rec,
                   {"homewins", "awaywins", "homedraws", "awaydraws", "homelosses", "awaylosses", "homegf", "awaygf", "homega",
                    "awayga", "points", "nummatchesplayed", "currenttableposition", "teamform", "lastgameresult"},
                   "##ltgrid", 2);
        ImGui::TextDisabled("After editing wins / draws / losses: Recalculate points, then Write table positions.");
    }
    ImGui::EndChild();
}

}  // namespace turbo
