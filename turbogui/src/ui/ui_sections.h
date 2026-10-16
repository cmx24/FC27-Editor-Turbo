// FC 27 LE Turbo GUI - collapsing sections whose open / closed state is remembered (GUI_REVAMP_SPEC.md 2.1: "Sections are
// CollapsingHeaders; the default-open set is per page and persisted in gui_settings.json gui.sections_open").
// Header-only. The key is "<page>.<section>" (e.g. "settings.preferences"); the state lives in App::prefs.sections_open.
#pragma once
#include <string>

#include "app.h"
#include "imgui.h"

namespace turbo {

// A CollapsingHeader that opens as the user left it (default_open the first time). True while open. A click saves
// gui_settings.json; nothing is written on the frames in between.
inline bool collapsing_section(App& app, const char* key, const char* label, bool default_open) {
    ImGui::SetNextItemOpen(app.prefs.section_open(key, default_open), ImGuiCond_Always);
    const bool open = ImGui::CollapsingHeader(label);
    if (ImGui::IsItemToggledOpen() && app.prefs.set_section_open(key, open) && !app.save_gui_settings())
        app.notify("cannot write gui_settings.json", true);
    return open;
}

}  // namespace turbo
