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

void draw_settings_page(App& app) {
    if (collapsing_section(app, "settings.preferences", "Preferences", true)) {
        if (ImGui::Checkbox("Advanced view##settings", &app.prefs.advanced_mode) && !app.save_gui_settings())
            app.notify("cannot write gui_settings.json", true);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off (Simple): only features proven to work in the game. On: also the Advanced area (raw rows, bulk edit, "
                              "experimental tactics, match setup, gameplay tuning)");
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
