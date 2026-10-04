// FC 27 LE Turbo GUI - Tools tab, "Game editors": the switches that unlock the game's own player / manager editors and
// the status of the in-memory fallback (app.h draw_edit_unlock_hook, core/edit_unlock_rules.h, core/edit_unlock_hook.h)
#include <string>

#include "app.h"
#include "imgui.h"

namespace turbo {

namespace {

void save(App& app, const edit_unlock::Settings& s) {
    edit_unlock::settings_to_json(s, app.gui_settings);
    if (!app.save_gui_settings()) app.notify("cannot write turbo_output\\gui_settings.json", true);
    if (app.edit_unlock_hook) app.edit_unlock_hook->configure(s);
}

}  // namespace

void draw_edit_unlock_hook(App& app) {
    if (!ImGui::CollapsingHeader("Game editors (unlock the game's own Edit Player / Edit Manager)")) return;
    edit_unlock::Settings s = edit_unlock::settings_from_json(app.gui_settings);
    bool changed = false;
    changed |= ImGui::Checkbox("Game editors: unlock every field the game's editors can show", &s.enabled);
    ImGui::TextDisabled("Greyed-out fields become editable and hidden ones visible (names, nationality, birth date, height, "
                        "weight, position, role, foot, ...). TEAM and player GENDER stay locked.");
    if (!s.enabled) ImGui::BeginDisabled();
    changed |= ImGui::Checkbox("Unlock everything (experimental)", &s.experimental);
    ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
                       "Experimental: manager gender, the head editor for real players, Composure / Defensive Awareness, the "
                       "manager outfit picker and every celebration. Back up your save first; a field may show but not save.");
    if (ImGui::TreeNode("Details##edit_unlock")) {
        for (int i = 0; i < edit_unlock::kContextCount; ++i) {
            const edit_unlock::Context& c = edit_unlock::context_at(i);
            bool on = s.context_on(i);
            std::string label = std::string(c.label) + "##eu_" + c.name;
            if (ImGui::Checkbox(label.c_str(), &on)) {
                if (on) s.context_off &= ~(1u << i);
                else s.context_off |= 1u << i;
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("avatarcustomizationcfg_%s.json (%s)", c.name, edit_unlock::group_label(c.group));
        }
        changed |= ImGui::Checkbox("Also patch the editors in memory (fallback when Live Editor ignores the files)", &s.hook);
        ImGui::TreePop();
    }
    if (!s.enabled) ImGui::EndDisabled();
    if (changed) save(app, s);

    // the in-memory fallback
    if (app.edit_unlock_hook) {
        for (const auto& line : app.edit_unlock_hook->status()) ImGui::TextDisabled("%s", line.c_str());
    } else {
        ImGui::TextDisabled("Game editors hook: off (no game hooks in this host)");
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", edit_unlock::kCannotAddNote);
    ImGui::TextDisabled("Takes effect the next time an editor screen opens.");
    ImGui::PopTextWrapPos();
}

}  // namespace turbo
