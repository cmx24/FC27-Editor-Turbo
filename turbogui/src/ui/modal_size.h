// 5.0c E6 fix round: a dialog (popup modal) remembers the size the person gave it. The base opened every picker at its fixed default
// (SetNextWindowSize with ImGuiCond_Appearing and NoSavedSettings), so a resize lasted until the dialog closed: "resizing not saved".
// The size is kept in the pane-width store (gui.splits in gui_settings.json, unscaled px, so a UI size change keeps the proportion)
// under "modal/<id>/w" and "modal/<id>/h"; the App already writes that file when the store changes.
//   modal_size::set_next("vispicker", ImVec2(1000, 680));   // before BeginPopupModal, in unscaled px
//   if (BeginPopupModal(..., NoSavedSettings)) { modal_size::track("vispicker", ImVec2(1000, 680)); ... }
#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "imgui.h"
#include "ui/gui_prefs.h"
#include "ui/ui_split.h"

namespace turbo::modal_size {

inline std::string key(const char* id, char axis) { return std::string("modal/") + id + "/" + axis; }

// The size the dialog opens at: the stored one (unscaled px x the UI size, never larger than the game screen), else `dflt` (unscaled px).
inline ImVec2 wanted(const char* id, ImVec2 dflt) {
    const float sc = g_ui_scale > 0.0f ? g_ui_scale : 1.0f;
    SplitStore& st = split::store();
    float w = st.get(key(id, 'w'), dflt.x) * sc, h = st.get(key(id, 'h'), dflt.y) * sc;
    if (!st.has(key(id, 'w')) && !st.has(key(id, 'h'))) return ImVec2(w, h);  // nothing stored: the dialog's own default, as before
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (ds.x > 0.0f) w = std::min(w, ds.x);
    if (ds.y > 0.0f) h = std::min(h, ds.y);
    return ImVec2(std::max(w, 200.0f), std::max(h, 150.0f));
}

inline void set_next(const char* id, ImVec2 dflt) { ImGui::SetNextWindowSize(wanted(id, dflt), ImGuiCond_Appearing); }

// Called inside BeginPopupModal: the current size is stored once the mouse button is up (a resize in progress changes it every frame).
// Nothing is written while the size is still the default and nothing is stored (opening a dialog must not rewrite the file).
inline void track(const char* id, ImVec2 dflt) {
    if (ImGui::IsMouseDown(0)) return;
    const float sc = g_ui_scale > 0.0f ? g_ui_scale : 1.0f;
    const ImVec2 sz = ImGui::GetWindowSize();
    if (!split::store().has(key(id, 'w')) && !split::store().has(key(id, 'h')) && std::abs(sz.x - dflt.x * sc) < 1.5f &&
        std::abs(sz.y - dflt.y * sc) < 1.5f)
        return;
    split::store().set(key(id, 'w'), sz.x / sc);
    split::store().set(key(id, 'h'), sz.y / sc);
}

}  // namespace turbo::modal_size
