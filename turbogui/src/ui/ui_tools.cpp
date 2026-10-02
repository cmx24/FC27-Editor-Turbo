// FC 27 LE Turbo GUI - Turbo Tools tab (Turbo 0.1 features as buttons) and Status tab.
#include <algorithm>
#include <cstdio>

#include "app.h"
#include "imgui.h"

namespace turbo {

using nlohmann::json;

static bool run_button(App& app, const char* label, const char* module, const json& overrides, bool needs_cm) {
    bool cm = app.bridge.state().in_cm;
    bool disabled = app.busy() || !app.mailbox || (needs_cm && !cm);
    if (disabled) ImGui::BeginDisabled();
    bool clicked = ImGui::Button(label);
    if (disabled) ImGui::EndDisabled();
    if (disabled && needs_cm && !cm && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
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
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderInt("Form (0 = leave)", &form, 0, 100);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderInt("Morale (0 = leave)", &morale, 0, 100);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderInt("Fitness (0 = leave, 5-95)", &fitness, 0, 95);
        if (fitness > 0 && fitness < 5) fitness = 5;
        run_button(app, "Apply now##fm", "form_morale", {{"form", form}, {"morale", morale}, {"fitness", fitness}}, true);
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
        static int role = 3;
        static bool loaned = false;
        const char* roles[] = {"1 Crucial", "2 Important", "3 Rotation", "4 Sporadic", "5 Prospect"};
        ImGui::SetNextItemWidth(160.0f);
        ImGui::Combo("Squad role", &role, [](void* d, int i) { return static_cast<const char**>(d)[i]; }, roles, 5);
        if (role < 0) role = 2;
        ImGui::SameLine();
        ImGui::Checkbox("Include loaned-in", &loaned);
        ImGui::SameLine();
        run_button(app, "Set role for whole squad", "squad_role", {{"role", role + 1}, {"include_loaned_in", loaned}}, true);

        ImGui::Separator();
        static int user_years = 4, cpu_years = 5;
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("years##u", &user_years);
        user_years = std::max(1, std::min(user_years, 10));
        ImGui::SameLine();
        run_button(app, "Extend my squad's contracts", "extend_user_contracts", {{"years", user_years}}, true);
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("years##c", &cpu_years);
        cpu_years = std::max(1, std::min(cpu_years, 20));
        ImGui::SameLine();
        run_button(app, "Extend every other club's contracts", "extend_cpu_contracts", {{"years", cpu_years}}, false);
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
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Team ID (0 = mine)", &jersey_team, 0);
        ImGui::SameLine();
        run_button(app, "Jersey numbers", "team_jersey_numbers", {{"teamid", jersey_team}}, false);
        static char tables[256] = "teams";
        ImGui::SetNextItemWidth(260.0f);
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
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Ban until (YYYYMMDD)", &until, 0);
        ImGui::SameLine();
        ImGui::Checkbox("Exclude my club", &exclude_mine);
        run_button(app, "List bans", "transfer_bans", {{"mode", "list"}}, true);
        ImGui::SameLine();
        run_button(app, "Ban every team", "transfer_bans", {{"mode", "ban_all_teams"}, {"ban_until", until}, {"exclude_user_team", exclude_mine}}, true);
        ImGui::SameLine();
        run_button(app, "Remove all team bans", "transfer_bans", {{"mode", "unban_all_teams"}}, true);
    }

    // ---------------------------------------------------------------- maintenance
    if (ImGui::CollapsingHeader("Database maintenance")) {
        static int min_id = 460000;
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Generated players from ID", &min_id, 0);
        ImGui::SameLine();
        run_button(app, "Count", "delete_generated_players", {{"min_playerid", min_id}, {"confirm", false}}, false);
        ImGui::SameLine();
        if (ImGui::Button("Delete...")) ImGui::OpenPopup("##confirmdel");
        if (ImGui::BeginPopupModal("##confirmdel", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete every player with ID >= %d? This cannot be undone.", min_id);
            if (run_button(app, "Delete", "delete_generated_players", {{"min_playerid", min_id}, {"confirm", true}}, false))
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

void draw_status(App& app) {
    const auto& st = app.bridge.state();
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

    ImGui::SeparatorText("Settings");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("Show/hide key", key_name(app.toggle_vk))) {
        for (const auto& k : kKeys) {
            if (ImGui::Selectable(k.second, app.toggle_vk == k.first)) {
                app.toggle_vk = k.first;
                if (!app.save_gui_settings()) app.notify("cannot write gui_settings.json", true);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Log");
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const auto& l : app.log_lines) ImGui::TextUnformatted(l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

}  // namespace turbo
