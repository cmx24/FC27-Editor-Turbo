// FC 27 LE Turbo GUI - Competitions tab, "Live standings" view: the rows of the game's competition engine
// (FCE::DataManager StandingsDataList), which FC 27's own Standings screen shows. See core/fce_standings.h.
#include <algorithm>
#include <cstdio>
#include <map>

#include "app.h"
#include "core/fce_standings.h"
#include "core/standings_refresh.h"
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
    std::vector<fce::CompObj> compobjs;              // the competition tree (empty when the list is not there)
    std::map<uint16_t, fce::GroupInfo> infos;        // group -> where it sits in the tree
    std::vector<svm::ShownGroup> shown;              // what the game's standings view holds (docs/re/standings-ui-path.md 0c)
    std::map<uint16_t, int32_t> shown_by;            // group -> the competition id the view shows it under
    std::string shown_err;                           // why the view could not be read ("" = read)
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

// The league of most of a group's clubs (leagueteamlinks), -1 when none makes up 80 %
int64_t majority_league(App& app, const std::vector<size_t>& idx) {
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
    return (best >= 0 && best_n * 10 >= int(idx.size()) * 8) ? best : -1;
}

std::string league_name(App& app, int64_t leagueid) {
    std::string name;
    if (const Table* lg = app.db.table("leagues")) {
        Snapshot ls;
        const Field* nf = lg->field("leaguename");
        if (nf && ls.load(app.db.memory(), *lg))
            for (uint32_t r : ls.valid)
                if (ls.get_int(r, "leagueid", -1) == leagueid) name = ls.get_str(r, *nf);
    }
    return name;
}

// "FCE_Setup_Stage" -> "setup stage", "FCE_Round_of_16" -> "round of 16"
std::string stage_words(const std::string& desc) {
    std::string w = desc.rfind("FCE_", 0) == 0 ? desc.substr(4) : desc;
    for (char& c : w) {
        if (c == '_') c = ' ';
        else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return w;
}

// Group label. With the competition tree (FCE CompObjectDataList) the group's competition names it: "C31" is the
// league 31 of the database (Serie A), "C210" the Coppa Italia whose setup pool holds the same 20 clubs. Without the tree
// the league whose clubs make up most of the group (leagueteamlinks) is used. The game's standings view shows only the
// groups it holds (st.shown_by): those are marked, the others warned about, because a row of any other group never
// reaches the Standings screen (04-10-2026: the edit of pool 1066 stayed invisible while the screen read group 1120).
std::string group_label(App& app, uint16_t compobj, const std::vector<size_t>& idx) {
    std::string name, stage;
    auto it = g_live.infos.find(compobj);
    if (it != g_live.infos.end() && it->second.comp) {
        const fce::GroupInfo& gi = it->second;
        if (gi.comp_number >= 0) name = league_name(app, gi.comp_number);
        if (name.empty()) {
            int64_t lid = majority_league(app, idx);
            if (lid >= 0 && gi.league_stage()) name = league_name(app, lid);
        }
        if (name.empty()) name = "Competition " + (gi.comp_number >= 0 ? std::to_string(gi.comp_number) : gi.comp_short);
        if (!gi.league_stage()) stage = " - " + stage_words(gi.stage_desc);
    } else {
        int64_t lid = majority_league(app, idx);
        if (lid >= 0) name = league_name(app, lid);
        if (lid >= 0 && name.empty()) name = "League " + std::to_string(lid);
    }
    if (name.empty()) name = "Competition group";
    std::string mark;
    auto sh = g_live.shown_by.find(compobj);
    if (sh != g_live.shown_by.end()) mark = " [shown by the game as competition " + std::to_string(sh->second) + "]";
    else if (g_live.shown_err.empty() && !g_live.shown.empty()) mark = " [not shown by the game]";
    char buf[260];
    std::snprintf(buf, sizeof(buf), "%s%s (%zu clubs, comp %u)%s", name.c_str(), stage.c_str(), idx.size(), unsigned(compobj), mark.c_str());
    return buf;
}

// The rows the game's standings view holds (core/standings_refresh.h shown_groups): read-only, through the published
// StandingsViewManager; the node vtables come from the host's signatures, else from the image base
void read_shown(App& app) {
    g_live.shown.clear();
    g_live.shown_by.clear();
    const BridgeState& st = app.bridge.state();
    if (!st.svm) {
        g_live.shown_err = "the career's StandingsViewManager is not published (bridge_state.json svm)";
        return;
    }
    svm::Fns fns;
    if (app.standings_refresh) fns = app.standings_refresh->fns();
    const uint64_t co = fns.compobj_vtable ? fns.compobj_vtable : (app.game_base ? app.game_base + svm::kRvaCompObjectVtable : 0);
    const uint64_t sl = fns.standinglist_vtable ? fns.standinglist_vtable : (app.game_base ? app.game_base + svm::kRvaStandingListVtable : 0);
    const uint64_t live = app.game_base ? app.game_base + svm::kRvaLiveStandingsVtable : 0;
    std::string err = svm::validate(app.mem, st.svm, st.managers, fns.vtable ? fns.vtable : (app.game_base ? app.game_base + svm::kRvaVtable : 0),
                                    fns.listener);
    if (err.empty()) err = svm::shown_groups(app.mem, st.svm, live, co, sl, g_live.shown);
    g_live.shown_err = err;
    for (const svm::ShownGroup& g : g_live.shown)
        if (!g_live.shown_by.count(g.group)) g_live.shown_by[g.group] = g.key;
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
    g_live.compobjs.clear();
    g_live.infos.clear();
    if (g_live.loc.compobj_count && fce::read_compobjs(app.mem, g_live.loc, g_live.compobjs))
        for (const auto& kv : g_live.groups) {
            fce::GroupInfo gi;
            if (fce::describe_group(g_live.compobjs, kv.first, gi)) g_live.infos[kv.first] = gi;
        }
    read_shown(app);
    for (const auto& kv : g_live.groups) g_live.group_names[kv.first] = group_label(app, kv.first, kv.second);
    if (!g_live.groups.count(g_live.group)) {
        // the user's club's table: the group the game's standings view shows wins, then a league stage of the tree, then a
        // group named after a league (leagueteamlinks majority); a cup's setup pool lists the same clubs but is never shown
        g_live.have_group = false;
        int64_t user = app.bridge.state().user_team;
        int best_rank = -1;
        for (const auto& kv : g_live.groups) {
            bool has_user = false;
            for (size_t i : kv.second) has_user = has_user || (user > 0 && int64_t(g_live.rows[i].teamid) == user);
            if (!has_user) continue;
            int rank = 0;
            auto info = g_live.infos.find(kv.first);
            const bool in_tree = info != g_live.infos.end() && info->second.comp;
            if (g_live.shown_by.count(kv.first)) rank = 3;
            else if (in_tree && info->second.league_stage()) rank = 2;
            else if (!in_tree && g_live.group_names[kv.first].rfind("Competition group", 0) != 0) rank = 1;
            if (rank > best_rank) g_live.group = kv.first, g_live.have_group = true, best_rank = rank;
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

// After a row / result write: make the game's Standings screen and Office tile re-read the rows. The screen shows a
// cache (StandingsViewManager::mLiveStandings) that the game rebuilds only on match days and a few career events, so
// Turbo asks the manager to re-request the standings on the game thread (core/standings_refresh.h). The outcome comes
// back through App::tick (toast + log); the row write itself is done either way.
void queue_refresh(App& app, const std::string& label, const std::vector<uint16_t>& rows) {
    if (!app.standings_refresh) {
        app.standings_refresh_status = "no refresh service: the game's Standings screen shows the rows after the next match day";
        app.log("live standings: " + app.standings_refresh_status);
        return;
    }
    const BridgeState& st = app.bridge.state();
    svm::Request req;
    req.svm = st.svm;
    req.managers = st.managers;
    req.comm = st.comm_service;
    req.ifce = st.ifce;
    req.image_base = app.game_base;
    req.label = label;
    req.rows = rows;  // the outcome says whether the game's standings view shows them
    std::string why;
    if (app.standings_refresh->request(req, why)) {
        app.standings_refresh_status = "refresh of the game's standings view queued";
        app.log("live standings: " + app.standings_refresh_status + " (" + label + ")");
    } else {
        app.standings_refresh_status = "failed: " + why;
        app.notify("Standings refresh: " + why + " (the rows are written; the screen shows them after the next match day)", true);
        app.log("live standings: refresh not run: " + why);
    }
}

}  // namespace

std::string live_standings_view_line() {
    const LiveState& st = g_live;
    if (!st.loc.ok()) return "";
    if (!st.shown_err.empty()) return "The game's standings view could not be read (" + st.shown_err + ").";
    if (st.shown.empty()) return "The game's standings view holds no competition yet.";
    std::string what;
    for (const svm::ShownGroup& g : st.shown)
        what += (what.empty() ? "" : ", ") + std::to_string(g.key) + " -> group " + std::to_string(g.group) + " (" + std::to_string(g.rows.size()) + " rows)";
    return "The game's Standings screen reads: " + what;
}

std::string live_standings_view_warning() {
    const LiveState& st = g_live;
    if (!st.loc.ok() || !st.shown_err.empty() || st.shown.empty() || !st.have_group || st.shown_by.count(st.group)) return "";
    return "This group is not one the game's Standings screen shows: an edit here stays invisible there. Pick a group marked [shown by the game].";
}

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
                       "These are the game's own table rows. After an edit Turbo makes the game's standings view re-read them, "
                       "so the Standings screen and the Office tile show the change. Do not edit while a match or Sim To Date is running.");
    if (!app.standings_refresh_status.empty()) ImGui::TextDisabled("Standings view: %s", app.standings_refresh_status.c_str());
    {
        const std::string line = live_standings_view_line(), warning = live_standings_view_warning();
        if (!line.empty()) ImGui::TextDisabled("%s", line.c_str());
        if (!warning.empty()) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "%s", warning.c_str());
    }

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
                const std::string team = app.model.team_name(cur_row->teamid);
                app.notify("Standings: " + team + " updated in the game");
                app.log("live standings: row " + std::to_string(cur_row->id) + " team " + std::to_string(cur_row->teamid) + " written");
                const std::vector<uint16_t> written = {cur_row->id};
                reload(app);
                queue_refresh(app, team + " row", written);
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
                const fce::Fixture& fx = st.fixtures[size_t(st.sel_fixture)];
                std::vector<uint16_t> written;
                if (fx.home_sid >= 0) written.push_back(uint16_t(fx.home_sid));
                if (fx.away_sid >= 0) written.push_back(uint16_t(fx.away_sid));
                std::string err = fce::edit_result(app.mem, st.loc, uint16_t(st.sel_fixture), st.new_home, st.new_away, st.pts);
                if (err.empty()) {
                    app.notify("Result changed in the game (fixture and both table rows)");
                    app.log("live standings: fixture " + std::to_string(st.sel_fixture) + " set to " + std::to_string(st.new_home) + "-" +
                            std::to_string(st.new_away));
                    reload(app);
                    queue_refresh(app, "fixture " + std::to_string(st.sel_fixture), written);
                } else {
                    app.notify("Result: " + err, true);
                    reload(app);
                }
            }
            ImGui::TextDisabled("Old outcome removed from both rows, new one added (points per Win / Draw / Loss above).");
        }
    }
    ImGui::EndChild();
}

}  // namespace turbo
