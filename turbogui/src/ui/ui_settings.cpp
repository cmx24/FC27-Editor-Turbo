// FC 27 LE Turbo GUI - Settings area, the one page (GUI_REVAMP_SPEC.md 1.1 "Status" row, track T1):
// preferences first (Advanced view, show/hide key, UI size, row density, background loading), then the health summary and the
// technical details (collapsed), then the log (collapsed). The pieces are the old Status tab's, drawn by ui_tools.cpp.
#include "app.h"
#include "imgui.h"
#include "ui_sections.h"
#include "ui_shell.h"
#include "ui_theme.h"

namespace turbo {

namespace {

// 2.16 step 2: the game link, the version and Refresh moved here from the top bar
void draw_game_link(App& app) {
    if (app.connected()) {
        const BridgeState& st = app.bridge.state();
        const std::string club = st.in_cm && st.user_team > 0 ? app.model.team_name(st.user_team) : std::string();
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1.0f), "Connected to your career%s%s.", club.empty() ? "" : ": ", club.c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Waiting for your career. Open it in the game; Turbo connects by itself.");
    }
    ImGui::TextDisabled("Turbo %s | Live Editor %s", kGuiVersion, app.bridge.state().le_version.empty() ? "?" : app.bridge.state().le_version.c_str());
    ImGui::BeginDisabled(app.busy());
    if (ImGui::Button("Reload from game###reload")) {
        app.bridge.poll_files();
        if (app.refresh()) app.notify("Lists reloaded from the game");
        else app.notify(app.db_error, true);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Read the lists from the game again");
}

void draw_settings_page(App& app) {
    if (collapsing_section(app, "settings.link", "Game link", true)) {
        draw_game_link(app);
        ImGui::Spacing();
    }
    if (collapsing_section(app, "settings.preferences", "Preferences", true)) {
        if (ImGui::Checkbox("Show advanced tools###advview", &app.prefs.advanced_mode) && !app.save_gui_settings())
            app.notify("cannot write gui_settings.json", true);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shows the Advanced section: raw tables, bulk edit, experimental tactics, match setup and gameplay tuning. "
                              "Off keeps only what is proven to work in the game.");
        draw_preferences(app);
        ImGui::Spacing();
    }
    draw_status_health(app);  // pictures, health, technical details (collapsed)
    if (collapsing_section(app, "settings.log", "Log", false)) {
        ImGui::BeginChild("##settings_log", ImVec2(0, S(260.0f)), ImGuiChildFlags_Borders);
        ImGuiListClipper clip;  // 2.16 gui/perf: only the lines in view are laid out
        clip.Begin(static_cast<int>(app.log_lines.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) ImGui::TextUnformatted(app.log_lines[static_cast<size_t>(i)].c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
}

}  // namespace

TURBO_REGISTER_PAGE(shell::Page::Settings, draw_settings_page);

}  // namespace turbo
