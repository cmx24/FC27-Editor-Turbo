// FC 27 LE Turbo GUI - Competitions tab. Two views: the game engine's live table rows (ui_standings.cpp, what FC 27's
// Standings screen shows) and the career database's copy (leagueteamlinks, kept in the save; FC 27's Standings screen
// does not read it - seen in game, 03-10-2026).
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "app.h"
#include "comp_picker.h"
#include "imgui.h"

namespace turbo {

// ---------------------------------------------------------------- the searchable competition picker (comp_picker.h)

const comps::NameSources& comp_name_sources(App& app) {
    static comps::NameSources src;
    static int built_gen = -1;
    if (built_gen == app.gen) return src;
    built_gen = app.gen;
    src = comps::NameSources();
    if (const Table* lg = app.db.table("leagues")) {
        Snapshot ls;
        const Field* nf = lg->field("leaguename");
        if (ls.load(app.db.memory(), *lg))
            for (uint32_t r : ls.valid) {
                const int64_t id = ls.get_int(r, "leagueid", -1);
                if (id < 0) continue;
                comps::NameSources::League l;
                if (nf) l.name = ls.get_str(r, *nf);
                l.level = int(ls.get_int(r, "level", 0));
                l.country = ls.get_int(r, "countryid", -1);
                src.leagues[id] = l;
            }
    }
    if (const Table* nt = app.db.table("nations")) {
        Snapshot ns;
        const Field* nf = nt->field("nationname");
        if (nf && ns.load(app.db.memory(), *nt))
            for (uint32_t r : ns.valid) {
                const int64_t id = ns.get_int(r, "nationid", -1);
                if (id >= 0) src.nations[id] = ns.get_str(r, *nf);
            }
    }
    return src;
}

namespace {

nlohmann::json& picker_settings(App& app, const char* key) {
    nlohmann::json& g = app.gui_settings;
    if (!g.is_object()) g = nlohmann::json::object();
    if (!g.contains("competitions") || !g["competitions"].is_object()) g["competitions"] = nlohmann::json::object();
    nlohmann::json& c = g["competitions"];
    if (!c.contains(key) || !c[key].is_object()) c[key] = nlohmann::json::object();
    return c[key];
}

void save_picker(App& app, const char* what) {
    if (!app.save_gui_settings()) app.notify(std::string("cannot write gui_settings.json (") + what + ")", true);
}

// One line of the open list: a heading (not selectable) or something to pick
struct PickRow {
    enum Kind { All, Parent, Child, Section, Group } kind = All;
    size_t entry = 0;   // Parent / Child
    int parent_key = 0; // Parent
    int section = 0;    // ID scope: a competition can be listed twice (your club's and its section)
    bool has_children = false, open = false;
    std::string text;   // Section / Group
};

}  // namespace

int comp_picker_restore(App& app, const CompPicker& p, const std::vector<comps::Entry>& entries) {
    const nlohmann::json& s = picker_settings(app, p.settings_key);
    if (!s.contains("comp")) return -1;
    const int64_t key = s.value("key", int64_t(-1));
    const int comp = s.value("comp", -1);
    return comps::find_remembered(entries, key, comp, s.value("stage", std::string()));
}

bool comp_picker(App& app, CompPicker& p, const char* label, const std::vector<comps::Entry>& entries, int& selected, float width,
                 const char* all_label) {
    if (!p.loaded) {
        p.loaded = true;
        const nlohmann::json& s = picker_settings(app, p.settings_key);
        p.view.leagues_only = s.value("leagues_only", true);
        p.view.sort = comps::Sort(std::max(0, std::min(3, s.value("sort", 0))));
    }
    const bool have_sel = selected >= 0 && size_t(selected) < entries.size();
    const std::string preview = have_sel ? comps::summary(entries[size_t(selected)]) : (all_label ? all_label : "none");
    ImGui::SetNextItemWidth(width);
    const ImVec2 list_size(S(600.0f), S(380.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(list_size.x + S(20.0f), 0.0f), ImVec2(S(1200.0f), S(640.0f)));
    if (!ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLargest)) {
        p.cursor = -1;
        return false;
    }
    bool changed = false;
    const bool appearing = ImGui::IsWindowAppearing();
    if (appearing || p.refocus) ImGui::SetKeyboardFocusHere();
    p.refocus = false;
    const bool had_search = p.search[0] != 0;  // the box reverts on Esc before the key is seen below
    ImGui::SetNextItemWidth(S(230.0f));
    // CallbackHistory: the box keeps Up / Down (else keyboard navigation moves the focus away), read below as the cursor
    const bool typed = ImGui::InputTextWithHint("##compsearch", "Search: name, country, id", p.search, sizeof(p.search),
                                                ImGuiInputTextFlags_CallbackHistory, [](ImGuiInputTextCallbackData*) { return 0; });
    const std::vector<comps::Parent> parents = comps::build_parents(entries);
    bool all_yours = !parents.empty();
    for (const comps::Parent& pa : parents) all_yours = all_yours && pa.user;
    if (!all_yours) {
        ImGui::SameLine();
        if (ImGui::Checkbox("Leagues only", &p.view.leagues_only)) {
            picker_settings(app, p.settings_key)["leagues_only"] = p.view.leagues_only;
            save_picker(app, "leagues only");
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Sort:");
    const char* sorts[] = {"Country", "Name", "Clubs", "Id"};
    for (int i = 0; i < 4; ++i) {
        ImGui::SameLine();
        if (ImGui::RadioButton(sorts[i], int(p.view.sort) == i)) {
            p.view.sort = comps::Sort(i);
            picker_settings(app, p.settings_key)["sort"] = i;
            save_picker(app, "sort");
        }
    }
    p.view.search = p.search;
    const comps::Arranged arr = comps::arrange(entries, parents, p.view);

    // the rows of the list, headings included (the keyboard cursor skips them)
    std::vector<PickRow> rows;
    if (all_label) rows.push_back(PickRow());
    for (int s = 0; s < comps::kSectionCount; ++s) {
        if (arr.sections[s].empty() || (all_yours && s != comps::kSectionYours)) continue;
        PickRow h;
        h.kind = PickRow::Section;
        h.text = all_yours ? "Your club's competitions" : comps::section_title(s);
        rows.push_back(h);
        std::string group = "\x01";
        for (const comps::Shown& sh : arr.sections[s]) {
            if (!sh.group.empty() && sh.group != group) {
                PickRow g;
                g.kind = PickRow::Group;
                g.text = sh.group;
                rows.push_back(g);
            }
            group = sh.group;
            const comps::Parent& pa = parents[sh.parent];
            PickRow r;
            r.kind = PickRow::Parent;
            r.section = s;
            r.entry = pa.primary;
            r.parent_key = pa.parent_key;
            r.has_children = !sh.children.empty();
            r.open = r.has_children && (sh.open_by_search || p.open.count(pa.parent_key));
            rows.push_back(r);
            if (!r.open) continue;
            for (size_t c : sh.children) {
                PickRow cr;
                cr.kind = PickRow::Child;
                cr.section = s;
                cr.entry = c;
                cr.parent_key = pa.parent_key;
                rows.push_back(cr);
            }
        }
    }
    auto pickable = [&](int i) { return i >= 0 && size_t(i) < rows.size() && rows[size_t(i)].kind <= PickRow::Child; };
    auto is_selected = [&](const PickRow& r) {
        if (r.kind == PickRow::All) return !have_sel;
        return (r.kind == PickRow::Parent || r.kind == PickRow::Child) && have_sel && r.entry == size_t(selected);
    };
    // keyboard: the cursor starts on the chosen row, a new search puts it on the first match
    bool moved = false;
    if (appearing || typed || !pickable(p.cursor)) {
        p.cursor = -1;
        for (int i = 0; i < int(rows.size()) && appearing && p.cursor < 0; ++i)
            if (pickable(i) && is_selected(rows[size_t(i)])) p.cursor = i;
        for (int i = 0; i < int(rows.size()) && p.cursor < 0; ++i)
            if (pickable(i) && (rows[size_t(i)].kind != PickRow::All || p.search[0] == 0)) p.cursor = i;
        moved = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        for (int i = p.cursor + 1; i < int(rows.size()); ++i)
            if (pickable(i)) {
                p.cursor = i, moved = true;
                break;
            }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        for (int i = p.cursor - 1; i >= 0; --i)
            if (pickable(i)) {
                p.cursor = i, moved = true;
                break;
            }
    }
    int pick = -2;  // -2 nothing, -1 the "all" row, else an entry
    if (pickable(p.cursor)) {
        const PickRow& cr = rows[size_t(p.cursor)];
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && cr.kind == PickRow::Parent && cr.has_children && p.search[0] == 0) p.open.insert(cr.parent_key);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && cr.kind != PickRow::All && p.search[0] == 0) p.open.erase(cr.parent_key);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
            pick = cr.kind == PickRow::All ? -1 : int(cr.entry);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (had_search) {
            p.search[0] = 0;
            p.refocus = true;
        } else {
            ImGui::CloseCurrentPopup();
        }
    }

    if (ImGui::BeginChild("##complist", list_size, ImGuiChildFlags_Borders)) {
        if (rows.empty() || (rows.size() == 1 && all_label)) ImGui::TextDisabled("No competition matches '%s'.", p.search);
        if (ImGui::BeginTable("##comptable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Competition", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Country", ImGuiTableColumnFlags_WidthFixed, S(150.0f));
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, S(80.0f));
            ImGui::TableSetupColumn("Clubs", ImGuiTableColumnFlags_WidthFixed, S(40.0f));
            for (size_t i = 0; i < rows.size(); ++i) {
                const PickRow& r = rows[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (r.kind == PickRow::Section) {
                    ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", r.text.c_str());
                    continue;
                }
                if (r.kind == PickRow::Group) {
                    ImGui::TextDisabled("  %s", r.text.c_str());
                    continue;
                }
                const bool hl = is_selected(r) || int(i) == p.cursor;
                if (int(i) == p.cursor && moved) ImGui::SetScrollHereY(0.5f);
                if (r.kind == PickRow::All) {
                    if (ImGui::Selectable(all_label, hl, ImGuiSelectableFlags_SpanAllColumns)) pick = -1;
                    continue;
                }
                const comps::Entry& e = entries[r.entry];
                char id[48];
                ImGui::PushID(r.section);
                if (r.kind == PickRow::Parent) {
                    if (r.has_children) {
                        std::snprintf(id, sizeof(id), "%s##op%d", r.open ? "-" : "+", r.parent_key);
                        if (ImGui::SmallButton(id)) {
                            if (r.open) p.open.erase(r.parent_key);
                            else p.open.insert(r.parent_key);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(r.open ? "Hide the stages" : "Show the stages (pots, rounds, setup pool)");
                        ImGui::SameLine();
                    } else {
                        ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), 1.0f));
                        ImGui::SameLine();
                    }
                    std::snprintf(id, sizeof(id), "##cp%lld", static_cast<long long>(e.key));
                    std::string text = comps::title(e) + (e.note.empty() ? "" : "  [" + e.note + "]") + id;
                    if (ImGui::Selectable(text.c_str(), hl, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                        pick = int(r.entry);
                } else {
                    ImGui::Indent(ImGui::GetFrameHeight() * 1.5f);
                    std::snprintf(id, sizeof(id), "##cc%lld", static_cast<long long>(e.key));
                    std::string text = (e.stage.empty() ? std::string("main table") : e.stage) + (e.note.empty() ? "" : "  [" + e.note + "]") + id;
                    if (ImGui::Selectable(text.c_str(), hl, ImGuiSelectableFlags_SpanAllColumns)) pick = int(r.entry);
                    ImGui::Unindent(ImGui::GetFrameHeight() * 1.5f);
                }
                ImGui::PopID();
                ImGui::TableNextColumn();
                if (r.kind == PickRow::Parent) ImGui::TextUnformatted(e.country.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", r.kind == PickRow::Child ? "stage" : comps::kind_word(e.kind));
                ImGui::TableNextColumn();
                if (e.clubs > 0) ImGui::Text("%d", e.clubs);
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("Type to search (every kind); Up / Down, Enter picks, Right / Left show / hide the stages, Esc clears / closes.");
    if (pick != -2) {
        if (pick != selected) {
            selected = pick;
            changed = true;
        }
        nlohmann::json& s = picker_settings(app, p.settings_key);
        if (pick >= 0) {
            const comps::Entry& e = entries[size_t(pick)];
            s["key"] = e.key;
            s["comp"] = e.comp;
            s["stage"] = e.stage;
        } else {
            s["key"] = -1;
            s["comp"] = -1;
            s["stage"] = "";
        }
        save_picker(app, "last competition");
        p.search[0] = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndCombo();
    return changed;
}

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
        if (ImGui::BeginTabItem("Match setup")) {
            draw_match_setup(app);
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
    static int64_t built_user = -1;
    static std::vector<comps::Entry> entries;  // one per league of leagueteamlinks
    static int sel_entry = -1;
    static CompPicker picker("database");
    const int64_t user = app.bridge.state().user_team;
    if (built_gen != app.gen || built_user != user) {
        built_gen = app.gen;
        built_user = user;
        entries.clear();
        std::map<int64_t, std::pair<int, bool>> clubs;  // league -> clubs, your club in it
        Snapshot snap;
        if (snap.load(app.db.memory(), *t)) {
            for (uint32_t idx : snap.valid) {
                int64_t id = snap.get_int(idx, "leagueid", -1);
                if (id < 0) continue;
                auto& c = clubs[id];
                ++c.first;
                c.second = c.second || (user > 0 && snap.get_int(idx, "teamid", 0) == user);
            }
        }
        const comps::NameSources& src = comp_name_sources(app);
        for (const auto& kv : clubs) {
            comps::Entry e;
            e.key = kv.first;
            e.comp = int(kv.first);
            e.parent_key = int(kv.first);
            e.kind = comps::kind_from(src, e.comp, nullptr);
            e.clubs = kv.second.first;
            e.user = kv.second.second;
            e.main_stage = true;
            auto lg = src.leagues.find(kv.first);
            if (lg != src.leagues.end()) {
                e.level = lg->second.level;
                e.country = src.nation(lg->second.country);
                if (!lg->second.name.empty()) e.name = lg->second.name, e.named = true;
            }
            if (e.name.empty()) e.name = "League " + std::to_string(kv.first);
            entries.push_back(e);
        }
        sel_entry = -1;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].key == league) sel_entry = int(i);
        if (sel_entry < 0) sel_entry = comp_picker_restore(app, picker, entries);
        if (sel_entry < 0)
            for (size_t i = 0; i < entries.size() && sel_entry < 0; ++i)
                if (entries[i].user && entries[i].kind == comps::Kind::League) sel_entry = int(i);
        if (sel_entry < 0 && !entries.empty()) sel_entry = 0;
        league = sel_entry >= 0 ? entries[size_t(sel_entry)].key : -1;
    }
    if (comp_picker(app, picker, "League", entries, sel_entry, S(380.0f)) && sel_entry >= 0) {
        league = entries[size_t(sel_entry)].key;
        sel_team = 0;
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
