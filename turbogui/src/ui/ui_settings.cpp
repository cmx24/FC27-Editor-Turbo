// FC 27 LE Turbo GUI - Settings area, the one page (3.0 track C): Look and feel, Pictures, Show / hide key, Layout (5.0b-B), the connection and
// health summary (technical details collapsed) and the log (collapsed). The health pieces are the old Status tab's, drawn by ui_tools.cpp.
#include <cstdio>

#include "ui_custom_layout.h"
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

// 5.0b-B "Layout": Turbo remembers the window, every table column and every resizable pane (core/layout_store.h writes the ini, the log says
// "layout saved:"). Reset layout asks first (Cancel is the default button) and keeps the old file as a backup, so Undo reset can put it back.
// Restore window to default is the Restore path of the title strip with the first-use size (App::restore_window_default).
void draw_layout(App& app) {
    const float bh = S(44.0f);  // buttons >= 44 px (accessibility), plain words
    ImGui::PushTextWrapPos(0.0f);  // the sentences wrap in a narrow panel instead of running off its edge
    note("Turbo remembers the size and place of this window, the width of every table column and the panes you drag, and brings them back when the game starts again.");
    if (app.layout.configured()) ImGui::TextDisabled("Layout saved %d time%s since the game started.", app.layout.saves_written(), app.layout.saves_written() == 1 ? "" : "s");
    {   // 5.0c E6: what the window is now and what is remembered for the next start (tells "never saved" from "saved but not applied" in game)
        const WindowPrefs& wp = app.prefs.window;
        char mem[96];
        if (wp.maximized) std::snprintf(mem, sizeof mem, "full screen");
        else if (!wp.has_rect) std::snprintf(mem, sizeof mem, "nothing yet (the first-use size)");
        else std::snprintf(mem, sizeof mem, "%.0f x %.0f at %.0f, %.0f", wp.rect.w, wp.rect.h, wp.rect.x, wp.rect.y);
        ImGui::TextDisabled("Window now: %.0f x %.0f. Remembered for the next start: %s. Pane widths remembered: %d.", app.main_window_size_.x,
                            app.main_window_size_.y, mem, static_cast<int>(app.prefs.splits.widths.size()));
    }
    ImGui::Spacing();
    // X 2.0: show / hide / resize / scale the menus and panels (ui_custom_layout.h; also Ctrl+L and the top bar's Layout button)
    if (ImGui::Button("Customize layout...###layout_customize", ImVec2(0, bh))) custom_layout::window_open() = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show, hide, resize and scale the top bar, the left menu, the page tabs and the panels (Ctrl+L)");
    ImGui::Spacing();
    if (ImGui::Button("Restore window to default###layout_window", ImVec2(0, bh))) app.restore_window_default();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The Turbo window goes back to its first-use size and place, not maximized");
    note("Puts only this window back to its first-use size and place. Columns and panes stay as you set them.");
    ImGui::Spacing();
    const bool can_reset = app.layout.configured() && !app.layout.can_undo();
    ImGui::BeginDisabled(!can_reset);
    if (ImGui::Button("Reset layout###layout_reset", ImVec2(0, bh))) ImGui::OpenPopup("Reset layout?###layout_reset_confirm");
    ImGui::EndDisabled();
    if (app.layout.can_undo()) {
        ImGui::SameLine();
        if (ImGui::Button("Undo reset###layout_undo", ImVec2(0, bh))) app.undo_reset_layout();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Puts your saved column widths and pane sizes back");
        theme::warn_text("Layout reset. The default sizes apply the next time the game starts. Until then your changes are not saved; Undo reset brings your layout back.");
    } else if (!app.layout.configured()) {
        note("Layout saving is not running, so there is nothing to reset.");
    } else {
        note("Puts every column width and pane size back to the default. Your current layout is kept as a backup file, and Undo reset brings it back.");
    }
    ImGui::PopTextWrapPos();
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Reset layout?###layout_reset_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Every column width and pane size goes back to the default the next time the game starts.");
        ImGui::TextUnformatted("Your current layout is kept as a backup. Undo reset puts it back while Turbo is open.");
        ImGui::Spacing();
        if (ImGui::Button("Cancel###layout_reset_cancel", ImVec2(S(140.0f), bh))) ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();  // the safe button has the keyboard focus
        ImGui::SameLine();
        if (ImGui::Button("Reset layout###layout_reset_go", ImVec2(S(160.0f), bh))) {
            app.reset_layout();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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
    if (collapsing_section(app, "settings.layout", "Layout", true)) {
        draw_layout(app);
        ImGui::Spacing();
    }
    if (collapsing_section(app, "settings.link", "Connection to the game", true)) {
        draw_game_link(app);
        ImGui::Spacing();
        draw_status_health(app);  // pictures, health, technical details (collapsed)
    }
    draw_log_section(app, "settings.log");
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
