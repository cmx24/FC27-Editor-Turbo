// FC 27 LE Turbo GUI - Turbo Tools tab (Turbo 0.1 features as buttons) and Status tab.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app.h"
#include "imgui.h"
#include "ui_edit_unlock.h"
#include "ui_images.h"

namespace turbo {

using nlohmann::json;

// need: tool key from Lua's core/caps.lua; the button is disabled (with the reason as tooltip) when this Live Editor
// build cannot run that tool
static bool run_button(App& app, const char* label, const char* module, const json& overrides, bool needs_cm,
                       const char* need = nullptr) {
    const BridgeState& st = app.bridge.state();
    bool cm = st.in_cm;
    const std::string* missing = st.unavailable_reason(need);
    bool disabled = app.busy() || !app.mailbox || (needs_cm && !cm) || missing;
    if (disabled) ImGui::BeginDisabled();
    bool clicked = ImGui::Button(label);
    if (disabled) ImGui::EndDisabled();
    if (missing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", missing->c_str());
    else if (disabled && needs_cm && !cm && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Needs a loaded career save");
    if (clicked) app.send({{"op", "run"}, {"module", module}, {"overrides", overrides}}, label);
    return clicked;
}

static json& auto_cfg(App& app, const char* name) {
    json& a = app.gui_settings["auto"][name];
    if (!a.is_object()) a = json::object();
    return a;
}

static void apply_auto(App& app, const char* label) {
    if (app.save_gui_settings()) app.send({{"op", "boot"}}, label);
    else app.notify("cannot write turbo_output\\gui_settings.json", true);
}

void draw_tools(App& app) {
    if (!app.mailbox) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Command channel unavailable; Turbo Tools need it.");
        return;
    }
    const BridgeState& st = app.bridge.state();
    ImGui::TextDisabled("These run through Live Editor's Lua engine on the next career-mode event (usually within a second in career mode).");
    if (!st.in_cm)
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
                           "No career loaded: Live Editor sends Lua no events here. After clicking a tool, run "
                           "lua\\scripts\\turbo_exec.lua in Live Editor's Lua Engine to execute it.");
    else if (!app.lua_alive())
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Turbo's Lua side has not answered yet: advance a day or open a menu.");
    ImGui::BeginChild("##tools");

    // ---------------------------------------------------------------- squad
    if (ImGui::CollapsingHeader("Your squad", ImGuiTreeNodeFlags_DefaultOpen)) {
        json& fm = auto_cfg(app, "form_morale");
        // Start from the GUI's saved values, else the effective config reported by Lua
        static bool init = false;
        static int form = 100, morale = 100, fitness = 0;
        if (!init && (fm.contains("form") || st.has_settings)) {
            form = fm.contains("form") ? fm.value("form", 100) : st.auto_form;
            morale = fm.contains("morale") ? fm.value("morale", 100) : st.auto_morale;
            fitness = fm.contains("fitness") ? fm.value("fitness", 0) : st.auto_fitness;
            init = true;
        }
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::SliderInt("Form (0 = leave)", &form, 0, 100);
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::SliderInt("Morale (0 = leave)", &morale, 0, 100);
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::SliderInt("Fitness (0 = leave, 5-95)", &fitness, 0, 95);
        if (fitness > 0 && fitness < 5) fitness = 5;
        run_button(app, "Apply now##fm", "form_morale", {{"form", form}, {"morale", morale}, {"fitness", fitness}}, true,
                   "form_morale");
        ImGui::SameLine();
        bool keep = fm.value("enabled", st.auto_form_enabled);
        if (ImGui::Checkbox("Keep every day (auto)##fm", &keep)) {
            fm["enabled"] = keep;
            fm["form"] = form;
            fm["morale"] = morale;
            fm["fitness"] = fitness;
            apply_auto(app, "Auto form/morale");
        }

        ImGui::Separator();
        static int role = 2;  // combo index: 2 = "3 Rotation", Turbo's default squad role
        static bool loaned = false;
        const char* roles[] = {"1 Crucial", "2 Important", "3 Rotation", "4 Sporadic", "5 Prospect"};
        ImGui::SetNextItemWidth(S(160.0f));
        ImGui::Combo("Squad role", &role, [](void* d, int i) { return static_cast<const char**>(d)[i]; }, roles, 5);
        if (role < 0) role = 2;
        ImGui::SameLine();
        ImGui::Checkbox("Include loaned-in", &loaned);
        ImGui::SameLine();
        run_button(app, "Set role for whole squad", "squad_role", {{"role", role + 1}, {"include_loaned_in", loaned}}, true);

        ImGui::Separator();
        static int user_years = 4, cpu_years = 5;
        ImGui::SetNextItemWidth(S(100.0f));
        ImGui::InputInt("years##u", &user_years);
        user_years = std::max(1, std::min(user_years, 10));
        ImGui::SameLine();
        run_button(app, "Extend my squad's contracts", "extend_user_contracts", {{"years", user_years}}, true);
        ImGui::SetNextItemWidth(S(100.0f));
        ImGui::InputInt("years##c", &cpu_years);
        cpu_years = std::max(1, std::min(cpu_years, 20));
        ImGui::SameLine();
        run_button(app, "Extend every other club's contracts", "extend_cpu_contracts", {{"years", cpu_years}}, false);
    }

    // ---------------------------------------------------------------- scouting, development, youth academy
    if (ImGui::CollapsingHeader("Scouting, development and youth academy")) {
        // reveal player data (features/reveal.lua -> Turbo.dll -> the game's PlayerDataRevealManager)
        static int reveal_team = 0, reveal_league = 0;
        static bool reveal_confirm = false;
        run_button(app, "Reveal my club##rv", "reveal", {{"scope", {{"user_team", true}}}}, true, "reveal");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##rvteam", &reveal_team, 0);
        ImGui::SameLine();
        if (reveal_team > 0) run_button(app, "Reveal club ID##rv", "reveal", {{"scope", {{"teamid", reveal_team}}}}, true, "reveal");
        else ImGui::TextDisabled("club ID");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##rvleague", &reveal_league, 0);
        ImGui::SameLine();
        ImGui::Checkbox("every club of the league##rvc", &reveal_confirm);
        ImGui::SameLine();
        if (reveal_league > 0 && reveal_confirm)
            run_button(app, "Reveal league ID##rv", "reveal", {{"scope", {{"leagueid", reveal_league}}}, {"confirm", true}}, true, "reveal");
        else ImGui::TextDisabled("league ID + tick to reveal a league");
        ImGui::TextDisabled("The game keeps at most 1500 scouted players: Turbo refuses a reveal that would make it forget older reports.");

        ImGui::Separator();
        // development (features/development.lua): squad-wide forced growth
        json& dv = auto_cfg(app, "development");
        static int squad_weekly = 1;
        static bool squad_no_decline = true;
        ImGui::SetNextItemWidth(S(160.0f));
        ImGui::SliderInt("Points per week##dv", &squad_weekly, 1, 5);
        ImGui::SameLine();
        ImGui::Checkbox("No decline##dv", &squad_no_decline);
        bool squad = dv.value("user_team", false);
        if (ImGui::Checkbox("Grow my whole squad every week (auto)##dv", &squad)) {
            dv["user_team"] = squad;
            dv["weekly"] = squad_weekly;
            dv["no_decline"] = squad_no_decline;
            const bool any = dv.contains("players") && dv["players"].is_array() && !dv["players"].empty();
            dv["enabled"] = squad || any;
            apply_auto(app, "Weekly growth");
        }
        run_button(app, "Develop my squad to potential now##dv", "development",
                   {{"scope", {{"user_team", true}}}, {"mode", "to_potential"}, {"confirm", true}}, true, "development");
        ImGui::TextDisabled("One player: Players tab > Develop to potential / Growth... (weekly growth per player).");

        ImGui::Separator();
        // youth academy (features/youth.lua)
        static int yid = 0, ypot = 0, ypos = -1, ytier = -1, yvar = -1;
        run_button(app, "List my youth academy##yt", "youth", {{"mode", "list"}}, true);
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Youth player ID##yt", &yid, 0);
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Potential (0 = leave)##yt", &ypot, 0);
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Position 0-27 (-1 = leave)##yt", &ypos, 0);
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Tier (-1 = leave)##yt", &ytier, 0);
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Potential range width (0 = exact, -1 = leave)##yt", &yvar, 0);
        if (yid > 0)
            run_button(app, "Set youth player##yt", "youth",
                       {{"mode", "set"}, {"playerid", yid}, {"potential", ypot}, {"position", ypos}, {"tier", ytier}, {"variance", yvar}}, true);
    }

    // ---------------------------------------------------------------- club budget
    if (ImGui::CollapsingHeader("Your club: transfer budget", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::string* no_budget = st.unavailable_reason("transfer_budget");
        if (no_budget)
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Not possible with this Live Editor build: %s", no_budget->c_str());
        else if (st.transfer_budget >= 0)
            ImGui::Text("Current budget: %lld", static_cast<long long>(st.transfer_budget));
        else
            ImGui::TextDisabled("Current budget: shown once a career is loaded");
        static long long amount = 50000000;
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputScalar("amount##tb", ImGuiDataType_S64, &amount);
        amount = std::max(0LL, std::min(amount, 2000000000LL));
        ImGui::SameLine();
        run_button(app, "Set budget", "transfer_budget", {{"mode", "set"}, {"amount", amount}}, true, "transfer_budget");
        ImGui::SameLine();
        run_button(app, "Add to budget", "transfer_budget", {{"mode", "add"}, {"amount", amount}}, true, "transfer_budget");
        ImGui::TextDisabled("FC 27 keeps the budget in the career, not in the teams table: set it here (Live Editor's "
                            "SetUserTransferBudget).");
    }

    // ---------------------------------------------------------------- player career
    if (ImGui::CollapsingHeader("Player Career")) {
        run_button(app, "Give my player every PlayStyle", "pap_playstyles", json::object(), true);
        ImGui::SameLine();
        json& ps = auto_cfg(app, "pap_playstyles");
        bool keep = ps.value("enabled", st.auto_playstyles_enabled);
        if (ImGui::Checkbox("Re-apply automatically##ps", &keep)) {
            ps["enabled"] = keep;
            apply_auto(app, "Auto PlayStyles");
        }
    }

    // ---------------------------------------------------------------- exports
    if (ImGui::CollapsingHeader("Exports (CSV in turbo_output)")) {
        static bool only_mine = false;
        ImGui::Checkbox("Only my club", &only_mine);
        ImGui::SameLine();
        run_button(app, "Season stats", "export_season_stats", {{"only_user_team", only_mine}}, true);
        ImGui::SameLine();
        run_button(app, "Fixtures & results", "export_fixtures", json::object(), true);
        ImGui::SameLine();
        run_button(app, "Transfer history", "export_transfer_history", json::object(), true);
        static int jersey_team = 0;
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Team ID (0 = mine)", &jersey_team, 0);
        ImGui::SameLine();
        run_button(app, "Jersey numbers", "team_jersey_numbers", {{"teamid", jersey_team}}, false);
        static char tables[256] = "teams";
        ImGui::SetNextItemWidth(S(260.0f));
        ImGui::InputText("tables (comma separated)", tables, sizeof(tables));
        ImGui::SameLine();
        json list = json::array();
        {
            std::string s = tables, cur;
            for (char c : s + ",") {
                if (c == ',') {
                    size_t a = cur.find_first_not_of(' '), b = cur.find_last_not_of(' ');
                    if (a != std::string::npos) list.push_back(cur.substr(a, b - a + 1));
                    cur.clear();
                } else {
                    cur += c;
                }
            }
        }
        run_button(app, "Export tables", "export_table", {{"tables", list}}, false);
        run_button(app, "Probe report", "probe", json::object(), false);
    }

    // ---------------------------------------------------------------- transfers
    if (ImGui::CollapsingHeader("Transfer bans")) {
        static int until = 20990101;
        static bool exclude_mine = false;
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Ban until (YYYYMMDD)", &until, 0);
        ImGui::SameLine();
        ImGui::Checkbox("Exclude my club", &exclude_mine);
        if (const std::string* r = st.unavailable_reason("transfer_bans"))
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Not possible with this Live Editor build: %s", r->c_str());
        run_button(app, "List bans", "transfer_bans", {{"mode", "list"}}, true, "transfer_bans");
        ImGui::SameLine();
        run_button(app, "Ban every team", "transfer_bans", {{"mode", "ban_all_teams"}, {"ban_until", until}, {"exclude_user_team", exclude_mine}}, true,
                   "transfer_bans");
        ImGui::SameLine();
        run_button(app, "Remove all team bans", "transfer_bans", {{"mode", "unban_all_teams"}}, true, "transfer_bans");
    }

    // ---------------------------------------------------------------- bulk edit (FC 26 LE v26.3.2 / v26.3.5)
    if (ImGui::CollapsingHeader("Bulk edit players")) {
        static int scope = 0;  // 0 my squad, 1 players shown in the Players list, 2 team IDs, 3 all players
        static char teams[128] = "";
        static bool confirm_all = false;
        static char fnames[4][40] = {};
        static int fvals[4] = {};
        static bool use_fit = false, use_form = false, use_morale = false, use_dev = false, no_decline = false;
        static int fit = 95, form = 100, morale = 100, bonus_xp = 0;
        static float xp_mult = 2.0f;

        ImGui::RadioButton("My squad##be", &scope, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Players shown in the Players list##be", &scope, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Team IDs##be", &scope, 2);
        ImGui::SameLine();
        ImGui::RadioButton("All players##be", &scope, 3);
        if (scope == 1)
            ImGui::TextDisabled("%zu players: choose them with the Players tab's search and filters", app.list_player_ids.size());
        if (scope == 2) {
            ImGui::SetNextItemWidth(S(200.0f));
            ImGui::InputTextWithHint("##beteams", "e.g. 1, 241", teams, sizeof(teams));
        }
        if (scope == 3) ImGui::Checkbox("Yes, every player in the database##be", &confirm_all);

        ImGui::SeparatorText("Set fields (players table, range-checked)");
        for (int i = 0; i < 4; ++i) {
            char nl[16], vl[16];
            std::snprintf(nl, sizeof(nl), "##fn%d", i);
            std::snprintf(vl, sizeof(vl), "##fv%d", i);
            ImGui::SetNextItemWidth(S(200.0f));
            ImGui::InputTextWithHint(nl, "field, e.g. potential", fnames[i], sizeof(fnames[i]));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(S(100.0f));
            ImGui::InputInt(vl, &fvals[i], 0);
        }
        ImGui::SeparatorText("Career actions");
        ImGui::Checkbox("Fitness##be", &use_fit);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(160.0f));
        ImGui::SliderInt("##befit", &fit, 5, 95);
        ImGui::Checkbox("Form##be", &use_form);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(160.0f));
        ImGui::SliderInt("##beform", &form, 0, 100);
        ImGui::Checkbox("Morale##be", &use_morale);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(160.0f));
        ImGui::SliderInt("##bemorale", &morale, 0, 100);
        ImGui::Checkbox("Development##be", &use_dev);
        if (use_dev) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(S(90.0f));
            ImGui::InputFloat("XP multiplier##be", &xp_mult, 0.0f, 0.0f, "%.1f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(S(90.0f));
            ImGui::InputInt("Bonus XP##be", &bonus_xp, 0);
            ImGui::SameLine();
            ImGui::Checkbox("No decline##be", &no_decline);
        }

        // Build the command; explain why it cannot be sent yet
        json sc, set = json::object(), actions = json::object();
        std::string why;
        if (scope == 0) {
            sc = {{"user_team", true}};
        } else if (scope == 1) {
            sc = {{"playerids", app.list_player_ids}};
            if (app.list_player_ids.empty()) why = "the Players list shows no players";
        } else if (scope == 2) {
            json ids = json::array();
            std::string cur;
            for (char c : std::string(teams) + ",") {
                if (c == ',' || c == ' ') {
                    if (!cur.empty()) ids.push_back(std::atoll(cur.c_str()));
                    cur.clear();
                } else if (c >= '0' && c <= '9') {
                    cur += c;
                } else {
                    why = "team IDs: numbers separated by commas";
                }
            }
            if (ids.empty() && why.empty()) why = "enter at least one team ID";
            sc = {{"teamids", ids}};
        } else {
            sc = {{"all", true}};
            if (!confirm_all) why = "tick the confirmation to edit every player";
        }
        for (int i = 0; i < 4; ++i)
            if (fnames[i][0]) set[fnames[i]] = fvals[i];
        if (use_fit) actions["fitness"] = fit;
        if (use_form) actions["form"] = form;
        if (use_morale) actions["morale"] = morale;
        if (use_dev) actions["development"] = {{"xp_multiplier", xp_mult}, {"bonus_xp", bonus_xp}, {"no_decline", no_decline}};
        if (why.empty() && set.empty() && actions.empty()) why = "set a field or tick an action";
        if (why.empty() && use_dev)
            if (const std::string* r = st.unavailable_reason("development_xp")) why = "Development: " + *r;
        if (why.empty() && (use_fit || use_form || use_morale))
            if (const std::string* r = st.unavailable_reason("form_morale")) why = *r;
        json overrides = {{"scope", sc}, {"filters", json::object()}, {"set", set}, {"actions", actions},
                          {"confirm_all", scope == 3 && confirm_all}};
        json cmd = {{"op", "run"}, {"module", "bulk_edit"}, {"overrides", overrides}};
        if (why.empty() && cmd.dump().size() > 3900)
            why = "too many players for one command: narrow the Players list (about 400 at most) or use All players";
        if (!why.empty()) ImGui::BeginDisabled();
        run_button(app, "Apply bulk edit", "bulk_edit", overrides, false);
        if (!why.empty()) {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", why.c_str());
        }
    }

    // ---------------------------------------------------------------- maintenance
    if (ImGui::CollapsingHeader("Database maintenance")) {
        static int min_id = 460000;
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Generated players from ID", &min_id, 0);
        ImGui::SameLine();
        run_button(app, "Count", "delete_generated_players", {{"min_playerid", min_id}, {"confirm", false}}, false);
        ImGui::SameLine();
        const std::string* no_delete = st.unavailable_reason("delete_players");
        if (no_delete) ImGui::BeginDisabled();
        if (ImGui::Button("Delete...")) ImGui::OpenPopup("##confirmdel");
        if (no_delete) {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", no_delete->c_str());
        }
        if (ImGui::BeginPopupModal("##confirmdel", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete every player with ID >= %d? This cannot be undone.", min_id);
            if (run_button(app, "Delete", "delete_generated_players", {{"min_playerid", min_id}, {"confirm", true}}, false,
                           "delete_players"))
                ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Separator();
        static bool generic = true;
        run_button(app, "Capture real-face list", "headmodels", {{"mode", "capture"}}, false);
        ImGui::SameLine();
        ImGui::Checkbox("Generic head for others", &generic);
        ImGui::SameLine();
        run_button(app, "Apply real-face list", "headmodels", {{"mode", "apply"}, {"generic_for_others", generic}}, false);
    }

    // ---------------------------------------------------------------- safety
    if (ImGui::CollapsingHeader("Safety")) {
        json& t = app.gui_settings["turbo"];
        if (!t.is_object()) t = json::object();
        bool dry = t.value("dry_run", st.dry_run);
        if (ImGui::Checkbox("Dry run (Turbo Tools report only, write nothing)", &dry)) {
            t["dry_run"] = dry;
            if (!app.save_gui_settings()) app.notify("cannot write gui_settings.json", true);
        }
        ImGui::TextDisabled("Editors in the other tabs always write immediately, after range checks.");
    }
    draw_game_editors(app);  // FC 27's own edit screens unlocked (ui_edit_unlock.cpp)
    ImGui::EndChild();
}

// ---------------------------------------------------------------- status
static const std::pair<int, const char*> kKeys[] = {
    {0x70, "F1"}, {0x71, "F2"}, {0x72, "F3"}, {0x73, "F4"}, {0x74, "F5"}, {0x75, "F6"}, {0x76, "F7"},
    {0x77, "F8"}, {0x78, "F9"}, {0x7A, "F11"}, {0x7B, "F12"}, {0x2D, "Insert"}, {0x24, "Home"}, {0x23, "End"},
    {0x21, "Page Up"}, {0x22, "Page Down"}, {0x13, "Pause"}};

const char* key_name(int vk) {
    for (const auto& k : kKeys) if (k.first == vk) return k.second;
    return "?";
}

// Game-code hooks (src/win/game_hooks.cpp): build, signature table, every signature's status, hooks, dispatcher
void draw_hook_status(App& app) {
    ImGui::SeparatorText("Game hooks");
    if (!app.hook_report) {
        ImGui::TextDisabled("not available in this build");
        return;
    }
    const HookReport r = app.hook_report();
    ImGui::Text("Game build: %s | signature table: %s | hooks: %s", r.build.empty() ? "unknown" : r.build.c_str(),
                r.table_source.empty() ? "none" : r.table_source.c_str(), r.enabled ? "allowed" : "off");
    if (!r.note.empty()) ImGui::TextWrapped("  %s", r.note.c_str());
    int found = 0;
    for (const auto& s : r.signatures)
        if (s.state == SigState::Found) ++found;
    ImGui::Text("Signatures: %d of %zu found", found, r.signatures.size());
    for (const auto& s : r.signatures) {
        if (s.state == SigState::Found)
            ImGui::Text("  %s: found at 0x%llX", s.name.c_str(), static_cast<unsigned long long>(s.address));
        else
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "  %s: %s (%s)", s.name.c_str(), sig_state_name(s.state), s.error.c_str());
    }
    ImGui::Text("Hooks active: %zu", r.hooks.size());
    for (const auto& h : r.hooks)
        ImGui::Text("  %s: %s at 0x%llX | calls %lld | errors %lld%s%s", h.name.c_str(),
                    h.killed ? "KILLED (hook_<name>_off.txt)" : h.active ? "active" : "not active",
                    static_cast<unsigned long long>(h.target), h.calls, h.errors, h.note.empty() ? "" : " | ",
                    h.note.c_str());
    ImGui::Text("Game-thread dispatcher: %s | ticks %lld | Lua pumps %lld | jobs run %lld | failed %lld | dropped %lld | queued %zu",
                r.dispatcher_hooked ? "prompt (game_tick hook)" : "on career-mode events (Lua pump)", r.dispatcher_ticks,
                r.dispatcher_pumps, r.dispatcher_ran, r.dispatcher_failed, r.dispatcher_dropped, r.queued);
    ImGui::Text("  threads: tick %lu | Lua pump %lu | last drain %lu%s", static_cast<unsigned long>(r.tick_thread_id),
                static_cast<unsigned long>(r.pump_thread_id), static_cast<unsigned long>(r.game_thread_id),
                (r.tick_thread_id && r.pump_thread_id && r.tick_thread_id != r.pump_thread_id) ? "  (job-pool threads: both change from frame to frame)" : "");
    ImGui::TextWrapped("Prompt Lua commands (synthetic career event): %s | sent %lld | Lua pumped %lld",
                       r.lua_trigger.empty() ? "not available" : r.lua_trigger.c_str(), r.lua_triggers, r.lua_trigger_pumps);
    // Miniface from the 3D model (src/win/player_capture_win.cpp)
    if (app.capture) {
        const capture::Status cs = app.capture->status();
        ImGui::Text("3D-model capture: %s | %s | game captures seen %d (descriptor %s) | Turbo renders %d ok, %d failed%s%s",
                    cs.installed ? "installed" : "off", cs.busy ? ("busy: " + cs.busy_label).c_str() : cs.reason.c_str(), cs.seen,
                    cs.learned ? "learned" : "not learned", cs.done, cs.failed, cs.last_format.empty() ? "" : " | last picture ",
                    cs.last_format.c_str());
    }
    if (!r.calls.empty()) {
        ImGui::Text("Game calls (Managers > Job offers, Competitions > Live standings, Players > Callname):");
        for (const auto& c : r.calls) ImGui::TextWrapped("  %s", c.c_str());
    }
    // the spoken-callname set (core/commentary_audio.h SpokenWatch): built where the game binds its commentary bank
    if (app.commentary_audio) {
        if (app.callnames.refreshed && app.callnames.spoken.verified)
            ImGui::TextWrapped("  spoken callnames (%s): %s", app.callnames.lang.c_str(), app.callnames.spoken.source.c_str());
        else if (!app.spoken_watch_line().empty())
            ImGui::TextWrapped("  spoken callnames (%s): %s | probes %lld", app.callnames.lang.empty() ? "no language" : app.callnames.lang.c_str(),
                               app.spoken_watch_line().c_str(), app.spoken_watch.probes());
    }
}

void draw_status(App& app) {
    const auto& st = app.bridge.state();
    images_status(app);
    ImGui::SeparatorText("Connection");
    ImGui::Text("Live Editor folder: %s", app.bridge.root().string().c_str());
    ImGui::Text("Bridge files: %s", app.bridge.dir().string().c_str());
    ImGui::Text("Database meta: %s", app.bridge.meta_loaded() ? (std::to_string(app.bridge.meta().tables.size()) + " tables").c_str()
                                                              : (app.bridge.meta_error().empty() ? "not received" : app.bridge.meta_error().c_str()));
    ImGui::Text("Lua state: %s", st.loaded ? "received" : "not received");
    if (st.loaded) {
        ImGui::Text("  LE %s | session %s | update #%lld", st.le_version.c_str(), st.session.c_str(), st.seq);
        ImGui::Text("  DB service %s | career loaded: %s | your club: %lld", hex_addr(st.db_service).c_str(),
                    st.in_cm ? "yes" : "no", static_cast<long long>(st.user_team));
        if (st.date.valid()) ImGui::Text("  In-game date: %04d-%02d-%02d", st.date.year, st.date.month, st.date.day);
        if (!st.unavailable.empty()) {
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "  Not possible with this Live Editor build (%zu):",
                               st.unavailable.size());
            for (const auto& u : st.unavailable) ImGui::TextWrapped("    %s: %s", u.first.c_str(), u.second.c_str());
        }
    }
    ImGui::Text("Database: %s", app.db.ready() ? (std::to_string(app.db.table_names().size()) + " tables found").c_str()
                                               : (app.db_error.empty() ? "not connected" : app.db_error.c_str()));
    if (app.model.built()) ImGui::Text("Player names from: %s", app.model.name_source().c_str());
    if (app.mailbox) {
        ImGui::Text("Command channel: %s | Lua %s", hex_addr(app.mailbox->addr()).c_str(),
                    app.lua_alive() ? "answering" : "not seen yet (needs a career-mode event)");
    } else {
        ImGui::Text("Command channel: unavailable");
    }
    draw_hook_status(app);
    // Voice swaps (Players > Callname > All callnames / Voice swaps; core/callname_voice.h)
    if (app.voice_available()) ImGui::Text("%s", app.voice_status_line().c_str());
    else ImGui::TextDisabled("%s", app.voice_status_line().c_str());
    if (!app.voice_error.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "  %s", app.voice_error.c_str());

    ImGui::SeparatorText("Settings");
    ImGui::SetNextItemWidth(S(140.0f));
    if (ImGui::BeginCombo("Show/hide key", key_name(app.toggle_vk))) {
        for (const auto& k : kKeys) {
            if (ImGui::Selectable(k.second, app.toggle_vk == k.first)) {
                app.toggle_vk = k.first;
                if (!app.save_gui_settings()) app.notify("cannot write gui_settings.json", true);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(S(140.0f));
    ImGui::SliderFloat("UI size", &app.ui_scale_user, 0.6f, 2.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit() && !app.save_gui_settings()) app.notify("cannot write gui_settings.json", true);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset size")) {
        app.ui_scale_user = 1.0f;
        if (!app.save_gui_settings()) app.notify("cannot write gui_settings.json", true);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("now %.2fx (window height %.0f px)", static_cast<double>(app.ui_scale_applied),
                        static_cast<double>(ImGui::GetIO().DisplaySize.y));

    ImGui::SeparatorText("Log");
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const auto& l : app.log_lines) ImGui::TextUnformatted(l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

}  // namespace turbo
