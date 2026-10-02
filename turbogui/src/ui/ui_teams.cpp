// FC 27 LE Turbo GUI - Teams and Managers tabs.
#include <algorithm>
#include <cctype>
#include <cstdio>

#include "app.h"
#include "imgui.h"

namespace turbo {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static const std::vector<std::string>& team_overview_fields() {
    static const std::vector<std::string> f = {
        "teamname", "overallrating", "attackrating", "midfieldrating", "defenserating", "domesticprestige",
        "internationalprestige", "transferbudget", "clubworth", "profitability", "popularity", "youthdevelopment",
        "physioaccess_senior", "trait1", "trait2", "rivalteam", "cityid", "foundationyear", "leaguetitles",
        "domesticcups", "uefa_cl_wins", "uefa_el_wins"};
    return f;
}

static void team_list(App& app) {
    static char search[64] = "";
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputTextWithHint("##tsearch", "team name or ID", search, sizeof(search));
    if (app.bridge.state().user_team > 0) {
        ImGui::SameLine();
        if (ImGui::Button("My club")) app.sel_team = app.bridge.state().user_team;
    }
    std::string q = lower(search);
    if (ImGui::BeginTable("##teams", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, S(62.0f));
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("OVR", ImGuiTableColumnFlags_WidthFixed, S(36.0f));
        ImGui::TableHeadersRow();
        std::vector<const TeamRow*> rows;
        for (const auto& t : app.model.teams()) {
            if (!q.empty() && lower(t.name).find(q) == std::string::npos && std::to_string(t.teamid).find(q) != 0) continue;
            rows.push_back(&t);
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const TeamRow* t = rows[static_cast<size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char idbuf[32];
                std::snprintf(idbuf, sizeof(idbuf), "%lld", static_cast<long long>(t->teamid));
                if (ImGui::Selectable(idbuf, app.sel_team == t->teamid, ImGuiSelectableFlags_SpanAllColumns))
                    app.sel_team = t->teamid;
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(t->name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", t->overall);
            }
        }
        ImGui::EndTable();
    }
}

static void squad_table(App& app, int64_t teamid) {
    const Table* lt = app.db.table("teamplayerlinks");
    auto links = app.model.links_of_team(teamid);
    ImGui::TextDisabled("%zu players linked", links.size());
    if (!lt) return;
    if (ImGui::BeginTable("##squad", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Jersey", ImGuiTableColumnFlags_WidthFixed, S(80.0f));
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, S(38.0f));
        ImGui::TableSetupColumn("OVR", ImGuiTableColumnFlags_WidthFixed, S(36.0f));
        ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, S(34.0f));
        ImGui::TableSetupColumn("Line-up slot", ImGuiTableColumnFlags_WidthFixed, S(90.0f));
        ImGui::TableHeadersRow();
        for (const auto& l : links) {
            const PlayerRow* p = app.model.player(l.playerid);
            ImGui::PushID(static_cast<int>(l.rec & 0x7FFFFFFF));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (const Field* jf = lt->field("jerseynumber")) field_editor(app, *lt, l.rec, *jf, nullptr, S(70.0f));
            ImGui::TableNextColumn();
            std::string name = p ? p->name : app.model.player_name(l.playerid);
            if (ImGui::Selectable(name.c_str(), false)) {
                app.sel_player = l.playerid;
                app.request_tab = 0;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p ? position_name(p->position) : "");
            ImGui::TableNextColumn();
            if (p) ImGui::Text("%d", p->overall);
            ImGui::TableNextColumn();
            if (p && p->age >= 0) ImGui::Text("%d", p->age);
            ImGui::TableNextColumn();
            if (const Field* pf = lt->field("position")) field_editor(app, *lt, l.rec, *pf, nullptr, S(80.0f));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void team_editor(App& app) {
    const Table* t = app.db.table("teams");
    const TeamRow* tr = app.model.team(app.sel_team);
    if (!t || !tr) {
        ImGui::TextDisabled("Select a team on the left.");
        return;
    }
    if (!app.db.table_alive(*t, tr->rec)) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "The database changed (another save loaded?). Press Refresh.");
        return;
    }
    ImGui::Text("%s", tr->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("ID %lld | OVR %d%s", static_cast<long long>(tr->teamid), tr->overall,
                        app.model.is_national_team(tr->teamid) ? " | national team" : "");
    ImGui::Separator();
    if (ImGui::BeginTabBar("##ttabs")) {
        if (ImGui::BeginTabItem("Overview")) {
            ImGui::BeginChild("##tov");
            field_grid(app, *t, tr->rec, team_overview_fields(), "##tgrid", 2);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Squad")) {
            squad_table(app, tr->teamid);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("All fields")) {
            all_fields(app, *t, tr->rec, "##tall");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void draw_teams(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    ImGui::BeginChild("##tlist", ImVec2(S(360.0f), 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    team_list(app);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##tedit", ImVec2(0, 0));
    team_editor(app);
    ImGui::EndChild();
}

// ---------------------------------------------------------------- managers
void draw_managers(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    const Table* t = app.db.table("manager");
    if (!t) {
        ImGui::TextDisabled("FC 27's database has no \"manager\" table under that name. Use the Database tab.");
        return;
    }
    const auto& mgrs = app.model.managers();
    ImGui::BeginChild("##mlist", ImVec2(S(360.0f), 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    static char search[64] = "";
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputTextWithHint("##msearch", "name or team", search, sizeof(search));
    std::string q = lower(search);
    if (ImGui::BeginTable("##mgrs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, S(62.0f));
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Team");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < mgrs.size(); ++i) {
            const auto& m = mgrs[i];
            std::string team = m.teamid > 0 ? app.model.team_name(m.teamid) : "";
            if (!q.empty() && lower(m.name).find(q) == std::string::npos && lower(team).find(q) == std::string::npos) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char idbuf[48];
            std::snprintf(idbuf, sizeof(idbuf), "%lld##m%zu", static_cast<long long>(m.managerid), i);
            if (ImGui::Selectable(idbuf, app.sel_manager == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns))
                app.sel_manager = static_cast<int>(i);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(team.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##medit", ImVec2(0, 0));
    if (app.sel_manager >= 0 && app.sel_manager < static_cast<int>(mgrs.size())) {
        const auto& m = mgrs[static_cast<size_t>(app.sel_manager)];
        if (!app.db.table_alive(*t, m.rec)) {
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "The database changed. Press Refresh.");
        } else {
            ImGui::Text("%s", m.name.c_str());
            ImGui::Separator();
            // Names as in EA's manager table (bodytypecode / headassetid; checked against the official db meta)
            field_grid(app, *t, m.rec, {"firstname", "surname", "commonname", "teamid", "nationality", "managerid",
                                         "personalityid", "bodytypecode", "headassetid", "outfitid", "skintonecode",
                                         "height", "weight"},
                       "##mgrid", 2);
            ImGui::SeparatorText("All fields");
            all_fields(app, *t, m.rec, "##mall");
        }
    } else {
        ImGui::TextDisabled("Select a manager on the left.");
    }
    ImGui::EndChild();
}

}  // namespace turbo
