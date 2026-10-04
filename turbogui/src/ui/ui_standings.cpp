// FC 27 LE Turbo GUI - Competitions tab, "Live standings" view: the rows of the game's competition engine
// (FCE::DataManager StandingsDataList), which FC 27's own Standings screen shows. See core/fce_standings.h.
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <map>
#include <set>

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
    // inline editing: the cell with an input box (double-click), the edit to apply once the table is drawn
    struct CellEdit {
        bool open = false, focus = false, seen_active = false, drawn = false;
        uint16_t row = 0;
        fce::Cell cell = fce::Cell::W;
        int value = 0;
    } cell;
    struct Pending {
        bool due = false;
        uint16_t row = 0;
        fce::Cell cell = fce::Cell::W;
        int value = 0;
    } pending;
    // the last edit, for Undo: the row as it was and as Turbo wrote it
    struct Undo {
        bool valid = false;
        fce::StandingRow before, after;
        bool typed_before = false;
    } undo;
    std::set<uint16_t> typed_pts;  // rows whose Pts the user typed: kept when W / D / L change
    std::string status;            // one line: "Torino FC: W 2 -> 3, Pts 0 -> 3 (applied)"
    bool status_error = false;
    bool home_away = false;        // the table shows the home / away counters
    bool adv_open = false;         // the Advanced section was open last frame
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
    if (g_live.located_ifce != ifce) {
        // another career: the last edit cannot be undone, typed points belong to the old rows (Undo itself also checks
        // that the row still holds what Turbo wrote)
        g_live.undo = LiveState::Undo();
        g_live.typed_pts.clear();
        g_live.status.clear();
    }
    g_live.cell = LiveState::CellEdit();
    g_live.pending = LiveState::Pending();
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

void set_status(const std::string& s, bool error) {
    g_live.status = s;
    g_live.status_error = error;
}

// The row as the game holds it now (the cached copy may be a match day old). false + status when it is not the same row
bool row_now(App& app, uint16_t id, fce::StandingRow& out) {
    LiveState& st = g_live;
    if (size_t(id) >= st.rows.size()) return false;
    const fce::StandingRow& cached = st.rows[id];
    uint8_t buf[fce::kStandingSize];
    if (!app.mem.read(cached.addr, buf, sizeof(buf)) || !fce::decode_row(buf, cached.addr, out) || out.id != cached.id ||
        out.teamid != cached.teamid || out.compobj != cached.compobj || out.used != 1) {
        set_status("The table changed in the game (new season or save loaded?): reloaded, try again.", true);
        reload(app);
        return false;
    }
    return true;
}

// Write `after` over the row that holds `before`, keep it for Undo, report it in one line, re-read and refresh the view
bool apply_row(App& app, const fce::StandingRow& before, const fce::StandingRow& after, const std::string& extra, bool typed_before) {
    LiveState& st = g_live;
    const std::string team = app.model.team_name(before.teamid);
    const std::string err = fce::write_row(app.mem, st.loc, after);
    if (!err.empty()) {
        set_status(team + ": not written: " + err, true);
        app.notify("Standings: " + team + ": " + err, true);
        reload(app);
        return false;
    }
    st.undo.valid = true;
    st.undo.before = before;
    st.undo.after = after;
    st.undo.typed_before = typed_before;
    const std::string what = fce::describe_change(before, after);
    set_status(team + ": " + (what.empty() ? "no change" : what) + extra + " (applied)", false);
    app.log("live standings: row " + std::to_string(before.id) + " team " + std::to_string(before.teamid) + " written: " + what + extra);
    const std::vector<uint16_t> written = {before.id};
    reload(app);
    queue_refresh(app, team + " row", written);
    return true;
}

int group_size(const LiveState& st, uint16_t compobj) {
    auto it = st.groups.find(compobj);
    return it == st.groups.end() ? 0 : int(it->second.size());
}

// The typed value of a table cell, applied once the table is drawn (a write re-reads the rows the table iterates)
void commit_cell(App& app, uint16_t id, fce::Cell c, int value) {
    LiveState& st = g_live;
    fce::StandingRow before;
    if (!row_now(app, id, before)) return;
    const std::string team = app.model.team_name(before.teamid);
    if (fce::cell_value(before, c) == value) {
        set_status(team + ": " + fce::cell_name(c) + " stays " + std::to_string(value) + " (no change)", false);
        return;
    }
    fce::StandingRow after = before;
    const bool typed = st.typed_pts.count(id) != 0;
    const bool keep = typed && c != fce::Cell::Pts;
    const std::string err = fce::set_cell(after, c, value, st.pts, fce::fixture_cap(st.fixtures, id, group_size(st, before.compobj)), keep);
    if (!err.empty()) {
        set_status(team + ": " + fce::cell_name(c) + " not changed: " + err + ".", true);
        return;
    }
    const bool results_moved = after.wins() != before.wins() || after.draws() != before.draws() || after.losses() != before.losses();
    if (!apply_row(app, before, after, keep && results_moved ? ", Pts kept at " + std::to_string(after.points) + " (typed)" : "", typed)) return;
    if (c == fce::Cell::Pts) st.typed_pts.insert(id);
}

// Undo: the row back as it was, only while it still holds what Turbo wrote (a match day in between would be lost)
void undo_last(App& app) {
    LiveState& st = g_live;
    if (!st.undo.valid) return;
    const LiveState::Undo u = st.undo;
    st.undo.valid = false;
    fce::StandingRow now;
    if (!row_now(app, u.before.id, now)) return;
    const std::string team = app.model.team_name(u.before.teamid);
    if (!fce::same_counters(now, u.after)) {
        set_status(team + ": cannot undo, the row changed since the edit (a match was played?).", true);
        return;
    }
    const std::string err = fce::write_row(app.mem, st.loc, u.before);
    if (!err.empty()) {
        set_status(team + ": undo not written: " + err, true);
        reload(app);
        return;
    }
    if (u.typed_before) st.typed_pts.insert(u.before.id);
    else st.typed_pts.erase(u.before.id);
    set_status(team + ": undone, " + fce::describe_change(u.after, u.before) + " (applied)", false);
    app.log("live standings: row " + std::to_string(u.before.id) + " undone");
    const std::vector<uint16_t> written = {u.before.id};
    reload(app);
    queue_refresh(app, team + " row (undo)", written);
}

// One number of the table: a selectable (click = select the club, double-click = edit), or the input box while edited.
// Enter or a click elsewhere commits (once the table is drawn), Esc cancels.
void draw_cell(const fce::StandingRow& r, fce::Cell c, bool selected) {
    LiveState& st = g_live;
    if (st.cell.open && st.cell.row == r.id && st.cell.cell == c) {
        st.cell.drawn = true;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (st.cell.focus) {
            ImGui::SetKeyboardFocusHere();
            st.cell.focus = false;
        }
        ImGui::InputInt("##cell", &st.cell.value, 0, 0, ImGuiInputTextFlags_AutoSelectAll);
        const bool active = ImGui::IsItemActive();
        if (active) {
            st.cell.seen_active = true;
        } else if (st.cell.seen_active) {
            // Esc reverts and leaves the box in the same frame; Enter or a click elsewhere leaves it with the typed value
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                st.pending.due = true;
                st.pending.row = r.id;
                st.pending.cell = c;
                st.pending.value = st.cell.value;
            }
            st.cell.open = false;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                   (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered())) {
            st.cell.open = false;  // the box never took the focus: Esc or a click elsewhere closes it
        }
        return;
    }
    char label[48];
    std::snprintf(label, sizeof(label), "%d##c%u_%s", fce::cell_value(r, c), unsigned(r.id), fce::cell_code(c));
    if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
        if (st.sel_row != int(r.id)) st.edit_loaded = false;
        st.sel_row = int(r.id);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (!fce::cell_editable(c)) {
                set_status(c == fce::Cell::P ? "P is W + D + L (home + away): change those, P follows."
                                             : "GD is GF - GA: change those, GD follows.",
                           false);
            } else {
                st.cell = LiveState::CellEdit();
                st.cell.open = st.cell.focus = true;
                st.cell.drawn = true;  // its club is in the table this frame: the box is drawn from the next one
                st.cell.row = r.id;
                st.cell.cell = c;
                st.cell.value = fce::cell_value(r, c);
            }
        }
    }
}

// Sort keys of the table columns (ColumnUserID): the cells, plus the position and the club name
constexpr int kColPos = 100, kColClub = 101;

}  // namespace

std::string live_standings_status() { return g_live.status; }

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
    // One line of help; the technical lines (what the game's standings view reads, the last refresh) in its tooltip
    ImGui::Checkbox("Home / away columns", &st.home_away);
    ImGui::SameLine();
    ImGui::TextDisabled("Double-click a number to change it: Enter applies it to the game, Esc cancels. (?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(S(560.0f));
        ImGui::TextUnformatted("These are the game's own table rows. After an edit Turbo makes the game's standings view re-read them, so the "
                               "Standings screen and the Office tile show the change. Do not edit while a match or Sim To Date is running.");
        ImGui::TextUnformatted("P follows W + D + L and GD follows GF - GA. Pts moves with W / D / L (points per Win / Draw / Loss under "
                               "Advanced) unless you typed Pts yourself. Click a column header to sort.");
        if (!app.standings_refresh_status.empty()) ImGui::Text("Standings view: %s", app.standings_refresh_status.c_str());
        const std::string line = live_standings_view_line();
        if (!line.empty()) ImGui::TextUnformatted(line.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    {
        const std::string warning = live_standings_view_warning();
        if (!warning.empty()) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "%s", warning.c_str());
    }
    if (!st.status.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(st.status_error ? ImVec4(0.95f, 0.55f, 0.45f, 1.0f) : ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "%s", st.status.c_str());
        if (ImGui::IsItemHovered() && !app.standings_refresh_status.empty())
            ImGui::SetTooltip("Standings view: %s", app.standings_refresh_status.c_str());
    }
    if (st.undo.valid && st.undo.before.compobj == st.group) {
        if (!st.status.empty()) ImGui::SameLine();
        if (ImGui::Button("Undo##lsundo")) undo_last(app);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Put %s's line back as it was before the last edit", app.model.team_name(st.undo.before.teamid).c_str());
    }

    std::vector<size_t> idx = st.have_group ? st.groups[st.group] : std::vector<size_t>();
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        const fce::StandingRow &ra = st.rows[a], &rb = st.rows[b];
        if (ra.points != rb.points) return ra.points > rb.points;
        if (ra.gd() != rb.gd()) return ra.gd() > rb.gd();
        if (ra.gf() != rb.gf()) return ra.gf() > rb.gf();
        return app.model.team_name(ra.teamid) < app.model.team_name(rb.teamid);
    });
    std::map<uint16_t, int> pos_of;  // standing id -> position (points, goal difference, goals for)
    for (size_t k = 0; k < idx.size(); ++k) pos_of[st.rows[idx[k]].id] = int(k) + 1;

    using fce::Cell;
    static const Cell kCells[] = {Cell::P, Cell::W, Cell::D, Cell::L, Cell::GF, Cell::GA, Cell::GD, Cell::Pts};
    static const Cell kCellsHomeAway[] = {Cell::P,  Cell::HW, Cell::HD, Cell::HL,  Cell::HGF, Cell::HGA, Cell::AW,
                                          Cell::AD, Cell::AL, Cell::AGF, Cell::AGA, Cell::GD,  Cell::Pts};
    const Cell* cells = st.home_away ? kCellsHomeAway : kCells;
    const int ncells = st.home_away ? int(sizeof(kCellsHomeAway) / sizeof(Cell)) : int(sizeof(kCells) / sizeof(Cell));

    // the whole table when it fits; with Advanced open it shares the height with it
    const ImGuiStyle& style = ImGui::GetStyle();
    const float need = (float(idx.size()) + 1.0f) * (ImGui::GetTextLineHeight() + 2.0f * style.CellPadding.y) + ImGui::GetFrameHeight();
    const float avail = ImGui::GetContentRegionAvail().y;
    const float room = st.adv_open ? std::max(avail * 0.5f, S(160.0f)) : avail - ImGui::GetFrameHeightWithSpacing() - style.ItemSpacing.y;
    const float table_h = std::max(S(120.0f), std::min(need, room));

    st.cell.drawn = false;
    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                               ImGuiTableFlags_Sortable;
    if (ImGui::BeginTable(st.home_away ? "##live_ha" : "##live", 2 + ncells, tf, ImVec2(0, table_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, S(34.0f), ImGuiID(kColPos));
        ImGui::TableSetupColumn("Club", ImGuiTableColumnFlags_WidthStretch, 0.0f, ImGuiID(kColClub));
        for (int k = 0; k < ncells; ++k)
            ImGui::TableSetupColumn(fce::cell_code(cells[k]), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending,
                                    S(40.0f), ImGuiID(static_cast<int>(cells[k])));
        ImGui::TableHeadersRow();
        // click a header to sort by it (the position first); the position column always shows the table position
        int sort_col = kColPos;
        bool desc = false;
        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
            if (ss->SpecsCount > 0) {
                sort_col = int(ss->Specs[0].ColumnUserID);
                desc = ss->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }
            ss->SpecsDirty = false;
        }
        std::vector<size_t> order = idx;
        if (sort_col != kColPos || desc)
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                const fce::StandingRow &ra = st.rows[a], &rb = st.rows[b];
                const int pa = pos_of[ra.id], pb = pos_of[rb.id];
                if (sort_col == kColClub) {
                    const std::string na = app.model.team_name(ra.teamid), nb = app.model.team_name(rb.teamid);
                    if (na != nb) return desc ? nb < na : na < nb;
                    return pa < pb;
                }
                const int va = sort_col == kColPos ? pa : fce::cell_value(ra, Cell(sort_col));
                const int vb = sort_col == kColPos ? pb : fce::cell_value(rb, Cell(sort_col));
                if (va != vb) return desc ? va > vb : va < vb;
                return pa < pb;
            });
        for (size_t i : order) {
            const fce::StandingRow& r = st.rows[i];
            const bool sel = st.sel_row == int(r.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char idb[48];
            std::snprintf(idb, sizeof(idb), "%d##ls%u", pos_of[r.id], unsigned(r.id));
            if (ImGui::Selectable(idb, sel)) {
                if (!sel) st.edit_loaded = false;
                st.sel_row = int(r.id);
            }
            ImGui::TableNextColumn();
            char club[160];
            std::snprintf(club, sizeof(club), "%s##club%u", app.model.team_name(r.teamid).c_str(), unsigned(r.id));
            if (ImGui::Selectable(club, sel)) {
                if (!sel) st.edit_loaded = false;
                st.sel_row = int(r.id);
            }
            for (int k = 0; k < ncells; ++k) {
                ImGui::TableNextColumn();
                draw_cell(r, cells[k], sel);
            }
        }
        ImGui::EndTable();
    }
    if (st.cell.open && !st.cell.drawn) st.cell.open = false;  // its club left the table (another competition picked)
    if (st.pending.due) {
        st.pending.due = false;
        commit_cell(app, st.pending.row, st.pending.cell, st.pending.value);
    }

    // Advanced (closed by default): the precise home / away counters, the points per result and the played results
    st.adv_open = ImGui::CollapsingHeader("Advanced: home / away counters, points per result, played results##lsadv");
    if (!st.adv_open) return;
    ImGui::BeginChild("##lsedit", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(S(60.0f));
    ImGui::InputInt("Win", &st.pts.win, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(60.0f));
    ImGui::InputInt("Draw", &st.pts.draw, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(60.0f));
    ImGui::InputInt("Loss", &st.pts.loss, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("points per result (table edits and result changes)");
    const fce::StandingRow* cur_row = nullptr;
    if (st.sel_row >= 0 && size_t(st.sel_row) < st.rows.size() && st.rows[size_t(st.sel_row)].compobj == st.group)
        cur_row = &st.rows[size_t(st.sel_row)];
    if (!cur_row) {
        ImGui::TextDisabled("Click a club in the table to see its home / away counters here.");
        ImGui::EndChild();
        return;
    }
    if (!st.edit_loaded) {
        st.edit = *cur_row;
        st.edit_loaded = true;
    }
    const uint16_t cur_id = cur_row->id;
    ImGui::Separator();
    ImGui::Text("%s", app.model.team_name(cur_row->teamid).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(row %u, team %u)", unsigned(cur_row->id), unsigned(cur_row->teamid));
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
    if (ImGui::Button("Points from W/D/L")) {
        long long p = (long long)st.edit.wins() * st.pts.win + (long long)st.edit.draws() * st.pts.draw +
                      (long long)st.edit.losses() * st.pts.loss;
        st.edit.points = int16_t(std::max(-32768LL, std::min(32767LL, p)));
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply to the game")) {
        fce::StandingRow before;
        if (row_now(app, cur_id, before)) {
            fce::StandingRow after = before;
            after.hw = st.edit.hw, after.hd = st.edit.hd, after.hl = st.edit.hl, after.hgf = st.edit.hgf, after.hga = st.edit.hga;
            after.aw = st.edit.aw, after.ad = st.edit.ad, after.al = st.edit.al, after.agf = st.edit.agf, after.aga = st.edit.aga;
            after.points = st.edit.points;
            apply_row(app, before, after, "", st.typed_pts.count(cur_id) != 0);
        }
        ImGui::EndChild();
        return;  // the rows were re-read: cur_row is not to be used any more this frame
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) st.edit_loaded = false;

    ImGui::Separator();
    ImGui::TextUnformatted("Played results of this club");
    std::vector<const fce::Fixture*> played;
    for (const fce::Fixture& f : st.fixtures) {
        if (!f.played()) continue;
        if (f.home_sid != int(cur_id) && f.away_sid != int(cur_id)) continue;
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
                st.undo.valid = false;  // both rows and the fixture changed: Undo would put back only one row
                set_status("Result changed to " + std::to_string(st.new_home) + " - " + std::to_string(st.new_away) +
                               " (fixture and both table rows, applied)",
                           false);
                app.log("live standings: fixture " + std::to_string(st.sel_fixture) + " set to " + std::to_string(st.new_home) + "-" +
                        std::to_string(st.new_away));
                reload(app);
                queue_refresh(app, "fixture " + std::to_string(st.sel_fixture), written);
            } else {
                set_status("Result not changed: " + err, true);
                app.notify("Result: " + err, true);
                reload(app);
            }
        }
        ImGui::TextDisabled("Old outcome removed from both rows, new one added (points per Win / Draw / Loss above).");
    }
    ImGui::EndChild();
}

}  // namespace turbo
