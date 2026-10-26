// FC 27 LE Turbo GUI - Settings area, the one page (3.0 track C): Look and feel, Pictures, Show / hide key, the connection and
// health summary (technical details collapsed), the log (collapsed), and at the bottom the switch that shows the Lab and the
// one for the old pages. The health pieces are the old Status tab's, drawn by ui_tools.cpp.
#include "app.h"
#include "hotkey_setting.h"
#include "imgui.h"
#include "ui_sections.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "ui_visual_picker.h"

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

// One plain sentence under a control
void note(const char* sentence) { theme::muted_text("%s", sentence); }

void save_or_complain(App& app) {
    if (!app.save_gui_settings()) app.notify("The setting could not be saved", true);
}

// 3.0 track C "Look and feel" (section key settings.preferences, so a section the user closed before stays closed): draw_look_and_feel is the
// one definition in ui_tools.cpp (2.22 resize: UI size + the Row height slider that replaced the 3-step combo; asks-1026 merge)

// "Pictures": the folder of the animations and pictures the pickers show, and loading them ahead of time
void draw_pictures(App& app) {
    visual_pictures_setting(app);  // the RDBM26 folder: the one place a folder path is shown
    ImGui::Spacing();
    if (ImGui::Checkbox("Load pictures ahead of time###preload", &app.prefs.preload_images)) save_or_complain(app);
    note("On: galleries open with their pictures ready. Off: pictures load when a screen shows them.");
}

// "Show / hide key": the key that shows and hides Turbo over the game
void draw_show_hide_key(App& app) {
    hotkey_setting(app);  // hotkey_setting.h: pick the key from the list or by pressing it
    note("The key that shows and hides Turbo while you play.");
}

// The two switches at the bottom: the Lab and the old pages
void draw_lab_switches(App& app) {
    ImGui::Separator();
    if (ImGui::Checkbox("Show the Lab (tools not proven in game)###advview", &app.prefs.advanced_mode)) save_or_complain(app);
    note("Adds the Lab to the left menu. It holds tools that are not proven to show up in your match.");
    if (app.prefs.advanced_mode) {
        if (ImGui::Checkbox("Use the old pages###legacypages", &app.legacy_pages)) save_or_complain(app);
        note("Shows the pages of earlier Turbo versions instead of the new ones, for as long as it is ticked.");
    }
}

void draw_settings_page(App& app) {
    if (collapsing_section(app, "settings.preferences", "Look and feel", true)) {
        draw_look_and_feel(app);
        ImGui::Spacing();
    }
    if (collapsing_section(app, "settings.pictures", "Pictures", true)) {
        draw_pictures(app);
        ImGui::Spacing();
    }
    if (collapsing_section(app, "settings.hotkey", "Show / hide key", true)) {
        draw_show_hide_key(app);
        ImGui::Spacing();
    }
    if (collapsing_section(app, "settings.link", "Connection to the game", true)) {
        draw_game_link(app);
        ImGui::Spacing();
        draw_status_health(app);  // pictures, health, technical details (collapsed)
    }
    draw_log_section(app, "settings.log");
    ImGui::Spacing();
    draw_lab_switches(app);
}

}  // namespace

// The log as a collapsing section, closed at first; Settings and the Diagnostics stand-in draw it (2.16 step 4)
void draw_log_section(App& app, const char* key) {
    if (collapsing_section(app, key, "Log", false)) {
        ImGui::BeginChild("##settings_log", ImVec2(0, S(260.0f)), ImGuiChildFlags_Borders);
        ImGuiListClipper clip;  // 2.16 gui/perf: only the lines in view are laid out
        clip.Begin(static_cast<int>(app.log_lines.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) ImGui::TextUnformatted(app.log_lines[static_cast<size_t>(i)].c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
}

TURBO_REGISTER_PAGE(shell::Page::Settings, draw_settings_page);

}  // namespace turbo
