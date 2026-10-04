// FC 27 LE Turbo GUI - Teams and Managers tabs.
#include <algorithm>
#include <cctype>
#include <cstdio>

#include "app.h"
#include "imgui.h"
#include "ui_faces.h"
#include "ui_identity.h"
#include "ui_images.h"

namespace turbo {

using nlohmann::json;

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
    // crest thumbnail (custom file first: what the game shows)
    draw_legacy_picture(app, crest_main_path(tr->teamid), S(40.0f), true);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("%s", tr->name.c_str());
    ImGui::TextDisabled("ID %lld | OVR %d%s", static_cast<long long>(tr->teamid), tr->overall,
                        app.model.is_national_team(tr->teamid) ? " | national team" : "");
    ImGui::EndGroup();
    ImGui::Separator();
    if (ImGui::BeginTabBar("##ttabs")) {
        if (ImGui::BeginTabItem("Overview")) {
            ImGui::BeginChild("##tov");
            field_grid(app, *t, tr->rec, team_overview_fields(), "##tgrid", 2);
            if (!app.model.is_national_team(tr->teamid)) transfer_ban_section(app, "team", tr->teamid);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Squad")) {
            squad_table(app, tr->teamid);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Name")) {
            ImGui::BeginChild("##tname");
            team_name_editor(app, *t, tr->rec, tr->teamid);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Colours")) {
            ImGui::BeginChild("##tcolours");
            team_colours_editor(app, *t, tr->rec, tr->teamid);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Crest")) {
            ImGui::BeginChild("##tcrest");
            crest_editor(app, tr->teamid);
            ImGui::EndChild();
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

// ---------------------------------------------------------------- managers > job offers
// A job offer for your manager from a club of your choice. The GUI only picks the club and sends the Lua module
// features/job_offer.lua (op run, module job_offer); Lua validates it and calls Turbo.dll's game call on the game
// thread (core/game_calls.h): the game's own JobMarketManager applies for the job and makes the club answer at
// once, so the inbox email and the Job Offers screen come from the game itself.
static void job_offers_section(App& app) {
    const BridgeState& st = app.bridge.state();
    ImGui::PushID("joboffers");
    if (ImGui::CollapsingHeader("Job offers (Manager Career)")) {  // closed by default: the manager editor stays in view
        ImGui::TextWrapped("Pick a club: Turbo applies for its manager job through the game's own job market and makes the "
                           "club answer at once. The offer arrives in your inbox and on the Job Offers screen, where you "
                           "accept or decline it. The game sets the wage from the club's star rating and league.");
        const std::string* missing = st.unavailable_reason("job_offer");
        if (missing) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Not available: %s", missing->c_str());
        ImGui::SetNextItemWidth(S(220.0f));
        ImGui::InputTextWithHint("##josearch", "club name or ID", app.job_offer_search, sizeof(app.job_offer_search));
        std::string q = lower(app.job_offer_search);
        if (ImGui::BeginTable("##joclubs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0, S(150.0f)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, S(62.0f));
            ImGui::TableSetupColumn("Club");
            ImGui::TableSetupColumn("League", ImGuiTableColumnFlags_WidthFixed, S(60.0f));
            ImGui::TableHeadersRow();
            std::vector<const TeamRow*> rows;
            for (const auto& t : app.model.teams()) {
                // clubs in a league only: national teams and teams outside every league cannot hire you
                if (t.league < 0 || app.model.is_national_team(t.teamid)) continue;
                if (t.teamid == st.user_team) continue;
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
                    char idbuf[40];
                    std::snprintf(idbuf, sizeof(idbuf), "%lld##jo", static_cast<long long>(t->teamid));
                    if (ImGui::Selectable(idbuf, app.job_offer_team == t->teamid, ImGuiSelectableFlags_SpanAllColumns))
                        app.job_offer_team = t->teamid;
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(t->name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", static_cast<long long>(t->league));
                }
            }
            ImGui::EndTable();
        }
        std::string pick = app.job_offer_team > 0 ? app.model.team_name(app.job_offer_team) : "";
        if (app.job_offer_team > 0) ImGui::Text("Club: %s (%lld)", pick.c_str(), static_cast<long long>(app.job_offer_team));
        else ImGui::TextDisabled("Select a club above.");
        const bool own = app.job_offer_team > 0 && app.job_offer_team == st.user_team;
        const bool disabled = app.busy() || !app.mailbox || !st.in_cm || missing || app.job_offer_team <= 0 || own;
        if (disabled) ImGui::BeginDisabled();
        bool clicked = ImGui::Button("Create job offer");
        if (disabled) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (missing) ImGui::SetTooltip("%s", missing->c_str());
            else if (!st.in_cm) ImGui::SetTooltip("Needs a loaded Manager Career");
            else if (own) ImGui::SetTooltip("That is your own club");
            else if (app.job_offer_team <= 0) ImGui::SetTooltip("Pick a club first");
            else if (!disabled) ImGui::SetTooltip("Asks the game to make %s offer you its manager job", pick.c_str());
        }
        if (clicked) ImGui::OpenPopup("Create job offer?");
        if (ImGui::BeginPopupModal("Create job offer?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Create a job offer from %s (%lld)?\nThis changes the running career (save first if in "
                               "doubt). The club answers at once; accept or decline it in the game's Job Offers screen.",
                               pick.c_str(), static_cast<long long>(app.job_offer_team));
            if (ImGui::Button("Create")) {
                json overrides = {{"enabled", true}, {"teamid", app.job_offer_team}, {"confirm", true}};
                app.send({{"op", "run"}, {"module", "job_offer"}, {"overrides", overrides}}, "Job offer from " + pick);
                app.job_offer_status = "Requested: the game answers on the next career-mode event (open a screen or advance a day).";
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (!app.job_offer_status.empty()) {
            ImGui::SameLine();
            ImGui::TextWrapped("%s", app.job_offer_status.c_str());
        }
        if (app.hook_report) {  // the native side: functions resolved, JobMarketManager seen, last outcome
            HookReport r = app.hook_report();
            for (const auto& c : r.calls) ImGui::TextDisabled("%s", c.c_str());
        }
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------- managers > manager rules / manager market
// Job security and unsackable for YOUR manager (features/manager_rules.lua: Turbo.dll's game call writes the saved
// addon and runs the game's own UpdateJobSecurityScore; the SackManager hook refuses the sack; docs/re/manager_rules.md)
// and the manager market for AI managers (features/manager_move.lua: the manager table of the career database). Every
// button sends a Lua module run; the outcome shows in place.
static void manager_rules_section(App& app) {
    const BridgeState& st = app.bridge.state();
    ImGui::PushID("managerrules");
    if (ImGui::CollapsingHeader("Manager rules: job security, unsackable (Manager Career)")) {
        const std::string* missing = st.unavailable_reason("manager_rules");
        if (missing) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Not available: %s", missing->c_str());
        const bool can = !app.busy() && app.mailbox && st.in_cm && !missing;
        if (!st.in_cm) ImGui::TextDisabled("Load a Manager Career first.");
        auto send_rules = [&](json overrides, const std::string& what) {
            overrides["enabled"] = true;
            overrides["confirm"] = true;
            app.send({{"op", "run"}, {"module", "manager_rules"}, {"overrides", overrides}}, "Manager rules: " + what);
            app.manager_rules_status = "Requested: " + what + " (the game answers on the next career-mode event)";
        };
        ImGui::SeparatorText("Job security");
        if (st.job_security_score >= 0) {
            ImGui::Text("Score %d/100: %s", st.job_security_score, st.job_security_level.empty() ? "?" : st.job_security_level.c_str());
            if (!st.job_security_locked.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "(locked %s by Turbo)", st.job_security_locked.c_str());
            }
            if (st.job_security_safe >= 0)
                ImGui::TextDisabled("addon %d | the game's bands: insecure from %d, okay from %d, safe from %d", st.job_security_addon,
                                    st.job_security_insecure, st.job_security_okay, st.job_security_safe);
        } else {
            ImGui::TextDisabled("Score: not read yet (needs a loaded Manager Career and the Turbo GUI's memory map)");
        }
        ImGui::TextWrapped("The game computes your score from the board's objectives plus a saved addon. Turbo sets the addon and "
                           "asks the game to recompute, so the board screen shows the game's own result. Safe and Very insecure "
                           "stay locked (saved with the career until you take a new job); Okay / Insecure / a score are a nudge "
                           "the objectives keep moving. \"Game's own\" removes the addon.");
        if (!can) ImGui::BeginDisabled();
        const char* levels[] = {"safe", "okay", "insecure", "very insecure"};
        const char* labels[] = {"Safe (locked)", "Okay", "Insecure", "Very insecure (locked)"};
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine();
            if (ImGui::Button(labels[i])) send_rules({{"job_security", levels[i]}}, std::string("job security ") + levels[i]);
        }
        ImGui::SameLine();
        if (ImGui::Button("Game's own")) send_rules({{"job_security", "game"}}, "job security back to the game's own score");
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputInt("##jsscore", &app.manager_rules_score, 0, 0);
        app.manager_rules_score = std::clamp(app.manager_rules_score, 0, 100);
        ImGui::SameLine();
        if (ImGui::Button("Set score")) send_rules({{"job_security", app.manager_rules_score}}, "job security score " + std::to_string(app.manager_rules_score));
        if (!can) ImGui::EndDisabled();
        if (st.sacked) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "The game has already marked you as sacked.");
        else if (st.sack_pending) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "A sack is pending: the game sacks you on the next day. Switch unsackable on to cancel it.");
        ImGui::SeparatorText("Unsackable");
        ImGui::TextWrapped("While this is on Turbo refuses the game's sack (JobSwitchManager::SackManager: low job security and an "
                           "ended contract both go through it) and cancels a pending one. Turbo.dll starts every game with it off; "
                           "\"keep\" switches it on again in every session.%s",
                           st.keep_unsackable ? " Kept: on." : "");
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button("Unsackable ON (keep)")) send_rules({{"unsackable", true}, {"keep", true}}, "unsackable on");
        ImGui::SameLine();
        if (ImGui::Button("ON (this session)")) send_rules({{"unsackable", true}, {"keep", false}}, "unsackable on for this session");
        ImGui::SameLine();
        if (ImGui::Button("OFF")) send_rules({{"unsackable", false}, {"keep", false}}, "unsackable off");
        if (!can) ImGui::EndDisabled();
        ImGui::TextDisabled("Endless career: FC 27 has no season limit or forced end for your manager to switch off (docs/re/manager_rules.md).");
        if (!app.manager_rules_status.empty()) ImGui::TextWrapped("%s", app.manager_rules_status.c_str());
        if (app.hook_report) {  // the native side: entries resolved, managers seen, unsackable state, last outcome
            HookReport r = app.hook_report();
            bool mine = false;  // the "  last:" line that follows the manager_rules line belongs to it
            for (const auto& c : r.calls) {
                if (c.rfind("manager_rules", 0) == 0) mine = true;
                else if (c.rfind("  ", 0) != 0) mine = false;
                if (mine) ImGui::TextDisabled("%s", c.c_str());
            }
        }
    }
    ImGui::PopID();
}

static void manager_market_section(App& app, const ManagerRow* selected) {
    const BridgeState& st = app.bridge.state();
    ImGui::PushID("managermarket");
    if (ImGui::CollapsingHeader("Manager market: move a manager, make one available")) {
        ImGui::TextWrapped("Career database edits the game itself reads (its AI hires free agents from the manager table; free agent "
                           "= no club). Every club keeps one manager: the club's manager takes the mover's old job, and a free agent "
                           "takes the club of a manager you make available. Your own club is never touched.");
        if (!selected) {
            ImGui::TextDisabled("Select a manager on the left.");
        } else {
            ImGui::Text("%s (%lld), %s", selected->name.c_str(), static_cast<long long>(selected->managerid),
                        selected->teamid > 0 ? app.model.team_name(selected->teamid).c_str() : "free agent");
            const bool own_club = selected->teamid > 0 && selected->teamid == st.user_team;
            ImGui::SetNextItemWidth(S(220.0f));
            ImGui::InputTextWithHint("##mmsearch", "club name or ID", app.manager_move_search, sizeof(app.manager_move_search));
            const std::string q = lower(app.manager_move_search);
            if (ImGui::BeginTable("##mmclubs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                                  ImVec2(0, S(120.0f)))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, S(62.0f));
                ImGui::TableSetupColumn("Club");
                ImGui::TableSetupColumn("League", ImGuiTableColumnFlags_WidthFixed, S(60.0f));
                ImGui::TableHeadersRow();
                std::vector<const TeamRow*> rows;
                for (const auto& t : app.model.teams()) {
                    if (t.league < 0 || app.model.is_national_team(t.teamid) || t.teamid == st.user_team || t.teamid == selected->teamid) continue;
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
                        char idbuf[40];
                        std::snprintf(idbuf, sizeof(idbuf), "%lld##mm", static_cast<long long>(t->teamid));
                        if (ImGui::Selectable(idbuf, app.manager_move_team == t->teamid, ImGuiSelectableFlags_SpanAllColumns))
                            app.manager_move_team = t->teamid;
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(t->name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%lld", static_cast<long long>(t->league));
                    }
                }
                ImGui::EndTable();
            }
            const bool base_ok = !app.busy() && app.mailbox && st.in_cm && !own_club;
            const bool can_move = base_ok && app.manager_move_team > 0 && app.manager_move_team != selected->teamid && app.manager_move_team != st.user_team;
            if (!can_move) ImGui::BeginDisabled();
            const bool move = ImGui::Button("Move to the picked club");
            if (!can_move) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (own_club) ImGui::SetTooltip("That is your own club's manager: your job changes through job offers");
                else if (!st.in_cm) ImGui::SetTooltip("Load a career first");
                else if (app.manager_move_team <= 0) ImGui::SetTooltip("Pick a club above");
                else ImGui::SetTooltip("manager.teamid in the career database; the club's manager takes the old job");
            }
            ImGui::SameLine();
            const bool can_free = base_ok && selected->teamid > 0;
            if (!can_free) ImGui::BeginDisabled();
            const bool release = ImGui::Button("Make available (free agent)");
            if (!can_free) ImGui::EndDisabled();
            if (move) ImGui::OpenPopup("Move manager?");
            if (release) ImGui::OpenPopup("Release manager?");
            if (ImGui::BeginPopupModal("Move manager?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Move %s to %s (%lld)?\nThat club's manager takes %s. Career database edit (saved with the career).",
                                   selected->name.c_str(), app.model.team_name(app.manager_move_team).c_str(),
                                   static_cast<long long>(app.manager_move_team),
                                   selected->teamid > 0 ? app.model.team_name(selected->teamid).c_str() : "the free-agent list");
                if (ImGui::Button("Move")) {
                    json overrides = {{"enabled", true}, {"managerid", selected->managerid}, {"teamid", app.manager_move_team}, {"confirm", true}};
                    app.send({{"op", "run"}, {"module", "manager_move"}, {"overrides", overrides}}, "Manager move: " + selected->name);
                    app.manager_move_status = "Requested: move " + selected->name;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("Release manager?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Make %s a free agent?\nA free-agent manager takes %s. Career database edit (saved with the career).",
                                   selected->name.c_str(), app.model.team_name(selected->teamid).c_str());
                if (ImGui::Button("Make available")) {
                    json overrides = {{"enabled", true}, {"managerid", selected->managerid}, {"teamid", 0}, {"confirm", true}};
                    app.send({{"op", "run"}, {"module", "manager_move"}, {"overrides", overrides}}, "Manager move: release " + selected->name);
                    app.manager_move_status = "Requested: make " + selected->name + " a free agent";
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        if (!app.manager_move_status.empty()) ImGui::TextWrapped("%s", app.manager_move_status.c_str());
    }
    ImGui::PopID();
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
    const ManagerRow* selected = (app.sel_manager >= 0 && app.sel_manager < static_cast<int>(mgrs.size()))
                                     ? &mgrs[static_cast<size_t>(app.sel_manager)] : nullptr;
    manager_rules_section(app);
    manager_market_section(app, selected);
    job_offers_section(app);
    ImGui::Separator();
    if (app.sel_manager >= 0 && app.sel_manager < static_cast<int>(mgrs.size())) {
        const auto& m = mgrs[static_cast<size_t>(app.sel_manager)];
        if (!app.db.table_alive(*t, m.rec)) {
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "The database changed. Press Refresh.");
        } else {
            ImGui::Text("%s", m.name.c_str());
            ImGui::Separator();
            if (ImGui::BeginTabBar("##mtabs")) {
                if (ImGui::BeginTabItem("Details")) {
                    ImGui::BeginChild("##mdet");
                    // Names as in EA's manager table (bodytypecode / headassetid; checked against the official db meta)
                    field_grid(app, *t, m.rec, {"firstname", "surname", "commonname", "teamid", "nationality", "managerid",
                                                 "personalityid", "bodytypecode", "headassetid", "outfitid", "skintonecode",
                                                 "height", "weight"},
                               "##mgrid", 2);
                    ImGui::SeparatorText("All fields");
                    all_fields(app, *t, m.rec, "##mall");
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Appearance")) {
                    manager_appearance(app, *t, m);  // real-face chooser (ui_faces.cpp)
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Miniface")) {
                    ImGui::BeginChild("##mmface");
                    MinifaceTarget mt;
                    mt.manager = true;
                    mt.headassetid = app.db.get_int(*t, m.rec, "headassetid", 0);
                    mt.path = legacy_path::staff_miniface(mt.headassetid);
                    mt.id = m.managerid;
                    mt.teamid = m.teamid;
                    miniface_editor(app, mt);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
    } else {
        ImGui::TextDisabled("Select a manager on the left.");
    }
    ImGui::EndChild();
}

}  // namespace turbo
