// FC 27 LE Turbo GUI - Ctrl + mouse wheel zoom (ui_zoom.h).
#include "ui_zoom.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.h"
#include "imgui.h"

namespace turbo {

float zoom_after(float user, float notches) {
    if (!std::isfinite(notches)) notches = 0.0f;
    if (!std::isfinite(user)) user = 1.0f;
    const float v = std::round((user + notches * kZoomStep) * 20.0f) / 20.0f;
    return std::max(kUiScaleMin, std::min(kUiScaleMax, v));
}

// One toast for the zoom, updated in place while the wheel turns
static void zoom_toast(App& app) {
    char text[64];
    std::snprintf(text, sizeof(text), "UI size %.2fx (Ctrl + 0: 1.00x)", static_cast<double>(app.ui_scale_user));
    for (auto& t : app.toasts) {
        if (t.text.rfind("UI size ", 0) == 0) {
            t.text = text;
            t.until = app.now + 2.0;
            return;
        }
    }
    app.toasts.push_back({text, false, app.now + 2.0});
    if (app.toasts.size() > 5) app.toasts.erase(app.toasts.begin());
}

bool zoom_input(App& app) {
    ImGuiIO& io = ImGui::GetIO();
    float notches = app.zoom.wheel;
    bool reset = app.zoom.reset;
    app.zoom.wheel = 0.0f;
    app.zoom.reset = false;
    bool changed = false;
    if (app.visible) {
        // Without a host that polls Ctrl itself (tests), Dear ImGui's Ctrl + wheel / Ctrl + 0; it does not scroll while
        // Ctrl is down
        const bool ctrl = !app.zoom.by_host && io.KeyCtrl && !io.KeyAlt;
        if (ctrl && io.MouseWheel != 0.0f) notches += io.MouseWheel;
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false))) reset = true;
        // The wheel zooms only over a Turbo window
        if (notches != 0.0f && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) notches = 0.0f;
        const float before = app.ui_scale_user;
        const float after = reset ? 1.0f : notches != 0.0f ? zoom_after(before, notches) : before;
        if (after != before) {
            app.ui_scale_user = after;
            app.zoom.save_at = app.now + 1.0;
            changed = true;
        }
        if (reset || notches != 0.0f) zoom_toast(app);  // also at the limit, so the user sees why nothing grows
    }
    if (app.zoom.save_at > 0.0 && app.now >= app.zoom.save_at) {
        app.zoom.save_at = 0.0;
        if (!app.save_gui_settings()) app.notify("cannot write gui_settings.json", true);
    }
    return changed;
}

}  // namespace turbo
