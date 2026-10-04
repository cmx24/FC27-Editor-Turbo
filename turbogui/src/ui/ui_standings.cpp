// FC 27 LE Turbo GUI - Competitions tab, "Live standings" view: the rows of the game's competition engine
// (FCE::DataManager StandingsDataList), which FC 27's own Standings screen shows. See core/fce_standings.h.
#include <algorithm>
#include <cstdio>
#include <map>

#include "app.h"
#include "core/fce_standings.h"
#include "imgui.h"

namespace turbo {

namespace {

struct LiveState {
    fce::Located loc;
    std::string err;
    uint64_t located_ifce = 0;
    int located_gen = -1;
    std::vector<fce::StandingRow> rows;
    std::vector<fce::Fixture> fixtures;
    std::map<uint16_t, std::vector<size_t>> groups;  // compobj -> row indexes (used rows only)
    std::map<uint16_t, std::string> group_names;
    uint16_t group = 0;
    bool have_group = false;
    int sel_row = -1;  // standing id
    fce::StandingRow edit;
    bool edit_loaded = false;
    int sel_fixture = -1;
    int new_home = 0, new_away = 0;
    fce::Points pts;
};

LiveState g_live;

// Group label: the league whose clubs (leagueteamlinks) make up most of the group's rows, else a generic name
std::string group_label(App& app, uint16_t compobj, const std::vector<size_t>& idx) {
    std::map<int64_t, int> leagues;
    const Table* lt = app.db.table("leagueteamlinks");
    Snapshot snap;
    if (lt && snap.load(app.db.memory(), *lt)) {
        std::map<int64_t, int64_t> team_league;
        for (uint32_t r : snap.valid) {
            int64_t tid = snap.get_int(r, "teamid", 0), lid = snap.get_int(r, "leagueid", -1);
            if (tid > 0 && lid >= 0 && !team_league.count(tid)) team_league[tid] = lid;
        }
        for (size_t i : idx) {
            auto it = team_league.find(int64_t(g_live.rows[i].teamid));
            if (it != team_league.end()) ++leagues[it->second];
        }
    }
    int64_t best = -1, best_n = 0;
    for (const auto& kv : leagues)
        if (kv.second > best_n) best = kv.first, best_n = kv.second;
    std::string name;
    if (best >= 0 && best_n * 10 >= int(idx.size()) * 8) {
        if (const Table* lg = app.db.table("leagues")) {
            Snapshot ls;
            const Field* nf = lg->field("leaguename");
            if (nf && ls.load(app.db.memory(), *lg))
                for (uint32_t r : ls.valid)
                    if (ls.get_int(r, "leagueid", -1) == best) name = ls.get_str(r, *nf);
        }
        if (name.empty()) name = "League " + std::to_string(best);
    }
    if (name.empty()) name = "Competition group";
    char buf[200];
    std::snprintf(buf, sizeof(buf), "%s (%zu clubs, comp %u)", name.c_str(), idx.size(), unsigned(compobj));
    return buf;
}

bool reload(App& app) {
    g_live.rows.clear();
    g_live.fixtures.clear();
    g_live.groups.clear();
    g_live.group_names.clear();
    g_live.edit_loaded = false;
    if (!g_live.loc.ok()) return false;
    if (!fce::read_rows(app.mem, g_live.loc, g_live.rows) || !fce::read_fixtures(app.mem, g_live.loc, g_live.fixtures)) {
        g_live.err = "the standings could not be read";
        g_live.loc = fce::Located();
        return false;
    }
    for (size_t i = 0; i < g_live.rows.size(); ++i)
        if (g_live.rows[i].used == 1 && g_live.rows[i].teamid > 0) g_live.groups[g_live.rows[i].compobj].push_back(i);
    for (const auto& kv : g_live.groups) g_live.group_names[kv.first] = group_label(app, kv.first, kv.second);
    if (!g_live.groups.count(g_live.group)) {
        // prefer the user's club's league: a group that is named after a league (leagueteamlinks majority) wins over a
        // cup / continental group that also lists the club (the Champions League phase has a lower comp id than Serie A)
        g_live.have_group = false;
        int64_t user = app.bridge.state().user_team;
        bool named = false;
        for (const auto& kv : g_live.groups) {
            bool has_user = false;
            for (size_t i : kv.second) has_user = has_user || (user > 0 && int64_t(g_live.rows[i].teamid) == user);
            if (!has_user) continue;
            bool is_league = g_live.group_names[kv.first].rfind("Competition group", 0) != 0;
            if (!g_live.have_group || (is_league && !named)) g_live.group = kv.first, g_live.have_group = true, named = is_league;
        }
        if (!g_live.have_group && !g_live.groups.empty()) g_live.group = g_live.groups.begin()->first, g_live.have_group = true;
    }
    return true;
}

// Locate (again) when the career changed, the chain broke or nothing was located yet
void ensure_located(App& app) {
    uint64_t ifce = app.bridge.state().ifce;
    bool fresh = g_live.loc.ok() && g_live.located_ifce == ifce && g_live.located_gen == app.gen &&
                 fce::validate(app.mem, g_live.loc, app.game_base);
    if (fresh) return;
    g_live.located_ifce = ifce;
    g_live.located_gen = app.gen;
    g_live.err = fce::locate(app.mem, ifce, app.game_base, g_live.loc);
    if (g_live.err.empty()) {
        if (reload(app)) app.log("live standings located: " + std::to_string(g_live.rows.size()) + " rows, " +
                                 std::to_string(g_live.fixtures.size()) + " fixtures");
    } else {
        g_live.rows.clear();
        g_live.groups.clear();
    }
}

void counter(const char* label, uint8_t& v) {
    ImGui::SetNextItemWidth(S(70.0f));
    uint8_t step = 1;
    ImGui::InputScalar(label, ImGuiDataType_U8, &v, &step, nullptr, "%u");
}

}  // namespace

void draw_live_standings(App& app) {
    ensure_located(app);
    LiveState& st = g_live;
    if (!st.loc.ok()) {
        ImGui::TextWrapped("The game's standings are not reachable: %s.", st.err.empty() ? "unknown reason" : st.err.c_str());
        ImGui::TextDisabled("A career must be loaded and Turbo's Lua side connected (bridge_state.json carries the FCE interface).");
        if (ImGui::Button("Try again")) st.located_gen = -1;
        return;
    }
    ImGui::SetNextItemWidth(S(420.0f));
    const char* cur = st.have_group ? st.group_names[st.group].c_str() : "none";
    if (ImGui::BeginCombo("Competition", cur, ImGuiComboFlags_HeightLarge)) {
        for (const auto& kv : st.group_names)
            if (ImGui::Selectable(kv.second.c_str(), kv.first == st.group)) {
                st.group = kv.first;
                st.have_group = true;
                st.sel_row = -1;
                st.sel_fixture = -1;
                st.edit_loaded = false;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) reload(app);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu rows, %zu fixtures", st.rows.size(), st.fixtures.size());
    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
                       "These are the game's own table rows: FC 27's Standings screen shows them the next time it opens. "
                       "Do not edit while a match or Sim To Date is running.");

    std::vector<size_t> idx = st.have_group ? st.groups[st.group] : std::vector<size_t>();
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        const fce::StandingRow &ra = st.rows[a], &rb = st.rows[b];
        if (ra.points != rb.points) return ra.points > rb.points;
        if (ra.gd() != rb.gd()) return ra.gd() > rb.gd();
        if (ra.gf() != rb.gf()) return ra.gf() > rb.gf();
        return app.model.team_name(ra.teamid) < app.model.team_name(rb.teamid);
    });

    ImGui::BeginChild("##lstable", ImVec2(S(560.0f), 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##live", 10, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        const char* heads[] = {"Pos", "Club", "P", "W", "D", "L", "GF", "GA", "GD", "Pts"};
        for (int i = 0; i < 10; ++i)
            ImGui::TableSetupColumn(heads[i], i == 1 ? ImGuiTableColumnFlags_WidthStretch : ImGuiTableColumnFlags_WidthFixed,
                                    i == 1 ? 0.0f : S(34.0f));
        ImGui::TableHeadersRow();
        int pos = 0;
        for (size_t i : idx) {
            const fce::StandingRow& r = st.rows[i];
            ++pos;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char idb[48];
            std::snprintf(idb, sizeof(idb), "%d##ls%u", pos, unsigned(r.id));
            if (ImGui::Selectable(idb, st.sel_row == int(r.id), ImGuiSelectableFlags_SpanAllColumns)) {
                st.sel_row = int(r.id);
                st.edit_loaded = false;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(app.model.team_name(r.teamid).c_str());
            int vals[] = {r.played(), r.wins(), r.draws(), r.losses(), r.gf(), r.ga(), r.gd(), int(r.points)};
            for (int v : vals) {
                ImGui::TableNextColumn();
                ImGui::Text("%d", v);
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##lsedit");
    const fce::StandingRow* cur_row = nullptr;
    if (st.sel_row >= 0 && size_t(st.sel_row) < st.rows.size() && st.rows[size_t(st.sel_row)].compobj == st.group)
        cur_row = &st.rows[size_t(st.sel_row)];
    if (!cur_row) {
        ImGui::TextDisabled("Select a club on the left to edit its line.");
    } else {
        if (!st.edit_loaded) {
            st.edit = *cur_row;
            st.edit_loaded = true;
        }
        ImGui::Text("%s", app.model.team_name(cur_row->teamid).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(row %u, team %u)", unsigned(cur_row->id), unsigned(cur_row->teamid));
        ImGui::Separator();
        ImGui::PushID("##lsgrid");
        ImGui::Columns(2, nullptr, false);
        counter("Home wins", st.edit.hw);
        counter("Home draws", st.edit.hd);
        counter("Home losses", st.edit.hl);
        counter("Home goals for", st.edit.hgf);
        counter("Home goals against", st.edit.hga);
        ImGui::NextColumn();
        counter("Away wins", st.edit.aw);
        counter("Away draws", st.edit.ad);
        counter("Away losses", st.edit.al);
        counter("Away goals for", st.edit.agf);
        counter("Away goals against", st.edit.aga);
        ImGui::Columns(1);
        ImGui::SetNextItemWidth(S(70.0f));
        int16_t pstep = 1;
        ImGui::InputScalar("Points", ImGuiDataType_S16, &st.edit.points, &pstep, nullptr, "%d");
        ImGui::SameLine();
        ImGui::TextDisabled("played %d, GD %+d", st.edit.played(), st.edit.gd());
        ImGui::PopID();
        ImGui::SetNextItemWidth(S(60.0f));
        ImGui::InputInt("Win", &st.pts.win, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(60.0f));
        ImGui::InputInt("Draw", &st.pts.draw, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(60.0f));
        ImGui::InputInt("Loss", &st.pts.loss, 0);
        ImGui::SameLine();
        if (ImGui::Button("Points from W/D/L")) {
            long long p = (long long)st.edit.wins() * st.pts.win + (long long)st.edit.draws() * st.pts.draw +
                          (long long)st.edit.losses() * st.pts.loss;
            st.edit.points = int16_t(std::max(-32768LL, std::min(32767LL, p)));
        }
        if (ImGui::Button("Apply to the game")) {
            std::string err = fce::write_row(app.mem, st.loc, st.edit);
            if (err.empty()) {
                app.notify("Standings: " + app.model.team_name(cur_row->teamid) + " updated in the game");
                app.log("live standings: row " + std::to_string(cur_row->id) + " team " + std::to_string(cur_row->teamid) + " written");
                reload(app);
            } else {
                app.notify("Standings: " + err, true);
                reload(app);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) st.edit_loaded = false;
        ImGui::TextDisabled("Played games and goal difference follow the counters; the position is sorted by the game.");

        ImGui::Separator();
        ImGui::TextUnformatted("Played results of this club");
        std::vector<const fce::Fixture*> played;
        for (const fce::Fixture& f : st.fixtures) {
            if (!f.played()) continue;
            if (f.home_sid != int(cur_row->id) && f.away_sid != int(cur_row->id)) continue;
            played.push_back(&f);
        }
        std::sort(played.begin(), played.end(), [](const fce::Fixture* a, const fce::Fixture* b) { return a->date > b->date; });
        ImGui::BeginChild("##lsfix", ImVec2(0, S(160.0f)), ImGuiChildFlags_Borders);
        for (const fce::Fixture* f : played) {
            const fce::StandingRow* h = size_t(f->home_sid) < st.rows.size() ? &st.rows[size_t(f->home_sid)] : nullptr;
            const fce::StandingRow* a = size_t(f->away_sid) < st.rows.size() ? &st.rows[size_t(f->away_sid)] : nullptr;
            char line[200];
            std::snprintf(line, sizeof(line), "%u  %s %d - %d %s##fx%u", unsigned(f->date), h ? app.model.team_name(h->teamid).c_str() : "?",
                          int(f->home_score), int(f->away_score), a ? app.model.team_name(a->teamid).c_str() : "?", unsigned(f->id));
            if (ImGui::Selectable(line, st.sel_fixture == int(f->id))) {
                st.sel_fixture = int(f->id);
                st.new_home = f->home_score;
                st.new_away = f->away_score;
            }
        }
        if (played.empty()) ImGui::TextDisabled("none");
        ImGui::EndChild();
        if (st.sel_fixture >= 0 && size_t(st.sel_fixture) < st.fixtures.size()) {
            ImGui::SetNextItemWidth(S(60.0f));
            ImGui::InputInt("Home", &st.new_home, 0);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(S(60.0f));
            ImGui::InputInt("Away", &st.new_away, 0);
            ImGui::SameLine();
            if (ImGui::Button("Change result")) {
                std::string err = fce::edit_result(app.mem, st.loc, uint16_t(st.sel_fixture), st.new_home, st.new_away, st.pts);
                if (err.empty()) {
                    app.notify("Result changed in the game (fixture and both table rows)");
                    app.log("live standings: fixture " + std::to_string(st.sel_fixture) + " set to " + std::to_string(st.new_home) + "-" +
                            std::to_string(st.new_away));
                } else {
                    app.notify("Result: " + err, true);
                }
                reload(app);
            }
            ImGui::TextDisabled("Old outcome removed from both rows, new one added (points per Win / Draw / Loss above).");
        }
    }
    ImGui::EndChild();
}

}  // namespace turbo
