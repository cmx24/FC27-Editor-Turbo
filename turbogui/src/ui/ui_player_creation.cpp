// FC 27 LE Turbo GUI - Players > Player Creation (see ui_player_creation.h). A short list on the left, the actions for the chosen player on
// the right, the CMTracker library on a second tab. Names and pictures instead of IDs (an ID shows on hover only).
#include "ui_player_creation.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

#include "app.h"
#include "core/model.h"
#include "imgui.h"
#include "move_rules.h"
#include "table_registry.h"
#include "ui_career_page.h"
#include "ui_player_page.h"
#include "ui_presets.h"
#include "ui_shell.h"
#include "ui_theme.h"

namespace turbo {

namespace {

using nlohmann::json;

PlayerCreationState g_state;

// the list's own state (one page, one list)
char g_search[64] = "";
bool g_my_club = false, g_free = false;
int g_pos = -1;
std::vector<int64_t> g_ids;  // the players the list shows, in order (ids, not pointers: the model may rebuild)
std::string g_list_key;
uint64_t g_list_version = ~uint64_t(0);

int64_t g_del_pid = 0;  // the player the Delete question is about

std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---------------------------------------------------------------- the list
void rebuild_list(App& app, const PlayerPageFilter& f, int64_t user_team) {
    std::vector<const PlayerRow*> rows;
    for (const PlayerRow& p : app.model.players())
        if (player_page_matches(p, f, user_team)) rows.push_back(&p);
    // a narrowed list (your club, free agents) reads best by rating; the whole game by name
    const bool by_rating = f.my_club || f.free_agents;
    std::stable_sort(rows.begin(), rows.end(), [&](const PlayerRow* a, const PlayerRow* b) {
        if (by_rating && a->overall != b->overall) return a->overall > b->overall;
        return lower_ascii(a->name) < lower_ascii(b->name);
    });
    g_ids.clear();
    g_ids.reserve(rows.size());
    for (const PlayerRow* r : rows) g_ids.push_back(r->playerid);
    // the pictures the list will want first (a few hundred is enough for the first screens)
    app.list_player_ids.assign(g_ids.begin(), g_ids.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(g_ids.size(), 300)));
}

// A filter chip: a small toggle button that lights up when on
bool chip(const char* label, bool* on) {
    const bool lit = *on;
    if (lit) ImGui::PushStyleColor(ImGuiCol_Button, theme::accent());
    const bool hit = ImGui::SmallButton(label);
    if (lit) ImGui::PopStyleColor();
    if (hit) *on = !*on;
    return hit;
}

void select_panel(App& app, void*) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##pcsearch", "Search by name or club", g_search, sizeof(g_search));
    const int64_t user = app.bridge.state().user_team;
    const bool had_mine = g_my_club;
    if (user > 0) {
        chip("My club###pc_chip_mine", &g_my_club);
        ImGui::SameLine();
    }
    chip("Free agents###pc_chip_free", &g_free);
    if (g_my_club && !had_mine) g_free = false;
    ImGui::SameLine();
    position_combo("##pcpos", g_pos, S(110.0f), "Any position");

    PlayerPageFilter f;
    f.text = lower_ascii(g_search);
    f.my_club = g_my_club && user > 0;
    f.free_agents = g_free && !f.my_club;
    f.position = g_pos;
    const std::string key = f.text + "|" + (f.my_club ? "m" : "-") + (f.free_agents ? "f" : "-") + "|" + std::to_string(f.position) + "|" +
                            std::to_string(user);
    if (key != g_list_key || app.model.version() != g_list_version) {
        rebuild_list(app, f, user);
        g_list_key = key;
        g_list_version = app.model.version();
    }
    g_state.listed = g_ids.size();
    theme::muted_text("%zu players", g_ids.size());

    // "##players" is the registered ID of the player list (table_registry.h); this table lives in another window, so its saved layout is its own
    if (ImGui::BeginTable("##players", 3, tbl::kData | ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 0);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, S(34.0f));
        ImGui::TableSetupColumn("Rating", ImGuiTableColumnFlags_WidthFixed, S(30.0f));
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_ids.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const PlayerRow* p = app.model.player(g_ids[static_cast<size_t>(i)]);
                if (!p) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char lbl[200];
                std::snprintf(lbl, sizeof(lbl), "%s###pc%lld", p->name.c_str(), static_cast<long long>(p->playerid));
                if (ImGui::Selectable(lbl, app.sel_player == p->playerid, ImGuiSelectableFlags_SpanAllColumns)) app.sel_player = p->playerid;
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s%s\nPlayer ID %lld", p->club_name.empty() ? "No club" : p->club_name.c_str(), p->on_loan ? " (on loan)" : "",
                                      static_cast<long long>(p->playerid));
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(position_name(p->position));
                ImGui::TableNextColumn();
                ImGui::Text("%d", p->overall);
            }
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------- Delete
// What the window knows about the player for the move rules (the same facts the Edit a player panel gathers; those helpers are private
// to ui_players.cpp and ui_player_page.cpp, so this is a local copy of the same few lines)
move_rules::Facts delete_facts(App& app, const PlayerRow& p) {
    move_rules::Facts f;
    f.user_team = app.bridge.state().user_team;
    f.club = p.club;
    if (const Table* lt = app.db.table("playerloans")) {
        if (uint64_t rec = app.db.find(*lt, "playerid", p.playerid)) {
            f.on_loan = true;
            f.loaned_from = app.db.get_int(*lt, rec, "teamidloanedfrom", 0);
        }
    } else {
        f.loans_known = false;
    }
    if (p.club > 0) {
        const auto squad = app.model.links_of_team(p.club);
        f.squad = static_cast<int>(squad.size());
        if (p.positions[0] == 0) {
            f.last_keeper = true;
            for (const auto& l : squad) {
                const PlayerRow* m = l.playerid != p.playerid ? app.model.player(l.playerid) : nullptr;
                if (m && m->positions[0] == 0) {
                    f.last_keeper = false;
                    break;
                }
            }
        }
    }
    return f;
}

const char* const kDeletePopup = "Delete this player?###pc_delconfirm";

void delete_section(App& app, const PlayerRow& p) {
    ImGui::SeparatorText("Delete this player");
    ImGui::PushTextWrapPos(0.0f);
    theme::warn_text("Can't be undone: Turbo's Undo button cannot bring a deleted player back.");
    ImGui::PopTextWrapPos();
    const bool cm = app.bridge.state().in_cm;
    const std::string why = move_rules::why_not("delete", delete_facts(app, p));
    g_state.delete_why = why;
    if (!cm) ImGui::BeginDisabled();
    // greyed out only when this game build cannot delete at all; when it cannot run for THIS player the label dims and hovering says why
    const std::string* missing = app.bridge.state().unavailable_reason("delete_players");
    if (missing) ImGui::BeginDisabled();
    if (!why.empty() && !missing) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool clicked = ImGui::Button("Delete player...###pc_delete");
    if (!why.empty() && !missing) ImGui::PopStyleColor();
    if (missing) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", missing->c_str());
    } else if (!why.empty()) {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Not possible for him: %s", why.c_str());
        if (clicked) app.notify("Delete player: " + why, true);
    } else {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Removes him and his loan records from the game");
        if (clicked) {
            g_del_pid = p.playerid;
            ImGui::OpenPopup(kDeletePopup);
        }
    }
    if (!cm) {
        ImGui::EndDisabled();
        theme::muted_text("Open a career first: players can only be deleted inside one.");
    }
}

// Drawn every frame, next to the button that opens it (same ID stack)
void delete_popup(App& app) {
    if (ImGui::BeginPopupModal(kDeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const PlayerRow* d = app.model.player(g_del_pid);
        if (!d) {
            ImGui::TextUnformatted("That player is gone.");
            if (ImGui::Button("Close###pc_delclose")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        char title[200];
        std::snprintf(title, sizeof(title), "Delete %s?", d->name.c_str());
        const char* what = "Turbo releases him from his club first, then removes him and his loan records.";
        const char* warn = "Can't be undone. Turbo's Undo button cannot bring him back.";
        g_state.confirm_text = std::string(title) + " " + what + " " + warn;
        ImGui::TextUnformatted(title);
        ImGui::PushTextWrapPos(S(380.0f));  // a wrapped line in a self-sizing dialog needs a width of its own
        ImGui::TextUnformatted(what);
        ImGui::PopTextWrapPos();
        theme::warn_text("%s", warn);
        if (ImGui::Button("Delete###pc_delgo")) {
            json a = {{"action", "delete"}, {"playerid", d->playerid}, {"confirm", true}};
            app.send({{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", json::array({a})}}}}, "Delete player");
            g_del_pid = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel###pc_delno")) {
            g_del_pid = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- the actions for the chosen player
void explain(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("%s", text);
    ImGui::PopTextWrapPos();
}

void editor_panel(App& app, void*) {
    g_state.confirm_text.clear();
    g_state.delete_why.clear();
    const Table* t = app.db.table("players");
    const PlayerRow* p = app.model.player(app.sel_player);
    if (!t || !p) {
        g_state.playerid = 0;
        g_state.headline.clear();
        theme::muted_text("Pick a player on the left.");
        explain("Create, Clone, Import and Export start from a player. Create makes the new player from him: every box starts as his own data.");
        return;
    }
    if (!app.db.table_alive(*t, p->rec)) {
        theme::err_text("The game data changed (another career was loaded?). Use Refresh at the top.");
        return;
    }
    g_state.playerid = p->playerid;
    char head[200];
    std::snprintf(head, sizeof(head), "%s, %s, overall %d, potential %d", position_name(p->position),
                  p->club_name.empty() ? "no club" : p->club_name.c_str(), p->overall, p->potential);
    g_state.headline = head;

    ImGui::TextUnformatted(p->name.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Player ID %lld", static_cast<long long>(p->playerid));
    ImGui::SameLine();
    theme::muted_text("%s", head);

    ImGui::SeparatorText("Make a new player");
    explain("Create player: a new player built from him. Every box starts as his own data (names, birth date, ratings, positions, attributes); "
            "only what you change is different.");
    explain("Clone: an exact copy in the club you choose.");
    explain("Import: a saved player file, onto him or as a new player. Importing onto him replaces the parts you tick, so back up your save first.");
    explain("Export: saves him to a file you can share or import later.");
    explain("From CMTracker: a ready-made player from the CMTracker library (the second tab above has the same list).");
    ImGui::Spacing();
    player_preset_buttons(app, *p);  // Export... / Import... / Clone... / Create player... / From CMTracker... / Repair names... and their dialogs

    delete_section(app, *p);
    delete_popup(app);
}

void draw_from_player(App& app) {
    shell::page::Primary pr;
    pr.label = "Create like him...";
    pr.enabled = app.model.player(app.sel_player) != nullptr;
    shell::page::frame(app, select_panel, editor_panel, nullptr, &pr);
    // the same dialog as the Create player... button of the editor, opened from the footer
    if (pr.clicked && pr.enabled) request_create_from(app, app.sel_player);
}

}  // namespace

const PlayerCreationState& player_creation_state() { return g_state; }

void draw_player_creation(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    if (ImGui::BeginTabBar("##pc_tabs")) {
        if (ImGui::BeginTabItem("From a player###pc_tab_player")) {
            g_state.tab = 0;
            draw_from_player(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("From the CMTracker library###pc_tab_cmt")) {
            g_state.tab = 1;
            draw_cmtracker_page(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

}  // namespace turbo
