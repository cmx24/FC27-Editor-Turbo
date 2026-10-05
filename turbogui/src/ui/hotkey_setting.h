// FC 27 LE Turbo GUI - the show/hide key control (top bar, Turbo Tools, Status > Settings): a list of common keys, and
// Change..., which waits for the key to be pressed (core/hotkey.h). Header-only (app.cpp, ui_tools.cpp). While the
// capture window is open App::hotkey_capture is set: the Windows host then passes the current show/hide key to ImGui
// instead of hiding Turbo, so it can be picked again.
#pragma once
#include "app.h"
#include "core/hotkey.h"
#include "imgui.h"

namespace turbo {

// Windows virtual-key code of an ImGui key (0 = none: modifiers, mouse, gamepad, keys a hotkey cannot use)
inline int vk_from_imgui_key(ImGuiKey k) {
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return '0' + (k - ImGuiKey_0);
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return 'A' + (k - ImGuiKey_A);
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F24) return 0x70 + (k - ImGuiKey_F1);
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return 0x60 + (k - ImGuiKey_Keypad0);
    switch (k) {
        case ImGuiKey_Tab: return 0x09;
        case ImGuiKey_LeftArrow: return 0x25;
        case ImGuiKey_RightArrow: return 0x27;
        case ImGuiKey_UpArrow: return 0x26;
        case ImGuiKey_DownArrow: return 0x28;
        case ImGuiKey_PageUp: return 0x21;
        case ImGuiKey_PageDown: return 0x22;
        case ImGuiKey_Home: return 0x24;
        case ImGuiKey_End: return 0x23;
        case ImGuiKey_Insert: return 0x2D;
        case ImGuiKey_Delete: return 0x2E;
        case ImGuiKey_Backspace: return 0x08;
        case ImGuiKey_Space: return 0x20;
        case ImGuiKey_Enter: return 0x0D;
        case ImGuiKey_KeypadEnter: return 0x0D;
        case ImGuiKey_Apostrophe: return 0xDE;
        case ImGuiKey_Comma: return 0xBC;
        case ImGuiKey_Minus: return 0xBD;
        case ImGuiKey_Period: return 0xBE;
        case ImGuiKey_Slash: return 0xBF;
        case ImGuiKey_Semicolon: return 0xBA;
        case ImGuiKey_Equal: return 0xBB;
        case ImGuiKey_LeftBracket: return 0xDB;
        case ImGuiKey_Backslash: return 0xDC;
        case ImGuiKey_RightBracket: return 0xDD;
        case ImGuiKey_GraveAccent: return 0xC0;
        case ImGuiKey_CapsLock: return 0x14;
        case ImGuiKey_ScrollLock: return 0x91;
        case ImGuiKey_NumLock: return 0x90;
        case ImGuiKey_PrintScreen: return 0x2C;
        case ImGuiKey_Pause: return 0x13;
        case ImGuiKey_KeypadDecimal: return 0x6E;
        case ImGuiKey_KeypadDivide: return 0x6F;
        case ImGuiKey_KeypadMultiply: return 0x6A;
        case ImGuiKey_KeypadSubtract: return 0x6D;
        case ImGuiKey_KeypadAdd: return 0x6B;
        default: return 0;
    }
}

// Set and save the show/hide key; false (toast) when the key cannot be used or the file cannot be written
inline bool set_toggle_key(App& app, int vk, int mods) {
    if (!hotkey_valid_vk(vk)) {
        app.notify("That key cannot be the show/hide key", true);
        return false;
    }
    app.toggle_vk = vk;
    app.toggle_mods = mods & 7;
    if (!app.save_gui_settings()) {
        app.notify("cannot write gui_settings.json", true);
        return false;
    }
    app.notify("Show/hide key: " + hotkey_name(app.toggle_vk, app.toggle_mods));
    return true;
}

// "Show/hide key: [F8 v] [Change...]": the list picks a common key without pressing it, Change... opens the capture
// window (hotkey_popup). Drawn in the top bar, Turbo Tools and Status > Settings; `where` keeps their IDs apart.
inline constexpr const char* kHotkeyLabel = "Show/hide key:";
inline float hotkey_combo_width() { return S(130.0f); }
inline float hotkey_control_width() {
    const ImGuiStyle& s = ImGui::GetStyle();
    return ImGui::CalcTextSize(kHotkeyLabel).x + hotkey_combo_width() + ImGui::CalcTextSize("Change...").x +
           s.FramePadding.x * 2.0f + s.ItemSpacing.x * 2.0f;
}

inline void hotkey_control(App& app, const char* where) {
    ImGui::PushID(where);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(kHotkeyLabel);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The key that shows and hides Turbo in game. Pick one from the list, or press Change... and "
                          "press the key (Ctrl / Alt / Shift and the mouse side buttons work too).");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(hotkey_combo_width());
    if (ImGui::BeginCombo("##hkpick", hotkey_name(app.toggle_vk, app.toggle_mods).c_str(), ImGuiComboFlags_HeightLargest)) {
        for (int vk : hotkey_common_keys()) {
            const bool sel = app.toggle_vk == vk && app.toggle_mods == 0;
            const std::string name = vk_name(vk) + (vk == kDefaultToggleVk ? " (default)" : "");
            if (ImGui::Selectable(name.c_str(), sel) && !sel) set_toggle_key(app, vk, 0);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Change...##hotkey")) {
        app.hotkey_capture = true;
        app.hotkey_capture_frame = ImGui::GetFrameCount();
    }
    ImGui::PopID();
}

// Top bar: the control right-aligned before `right_x` when it fits on the status line (true); otherwise the caller
// draws it on a line of its own
inline bool hotkey_bar(App& app, float right_x) {
    ImGui::SameLine();
    const float w = hotkey_control_width();
    if (ImGui::GetCursorPosX() + w + S(12.0f) > right_x) return false;
    ImGui::SameLine(right_x - w - S(8.0f));
    hotkey_control(app, "bar");
    return true;
}

// Status tab > Settings
inline void hotkey_setting(App& app) {
    hotkey_control(app, "status");
    if (hotkey_types_text(app.toggle_vk, app.toggle_mods))
        ImGui::TextDisabled("While you type in a Turbo text box this key types; click outside the box to use it.");
}

// The capture window, drawn once per frame by App::draw (main window): while it is open App::hotkey_capture is set and
// the Windows host passes the show/hide key to ImGui instead of toggling. Esc (pressed by the user) or Cancel closes it.
inline void hotkey_popup(App& app) {
    const char* id = "Show/hide key##hkcap";
    if (!app.hotkey_capture && !ImGui::IsPopupOpen(id)) return;
    // not drawn last frame (Turbo hidden): the wait ended; the window still open is closed below
    if (app.hotkey_capture && app.hotkey_capture_frame < ImGui::GetFrameCount() - 1) app.hotkey_capture = false;
    if (app.hotkey_capture) {
        app.hotkey_capture_frame = ImGui::GetFrameCount();
        if (!ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    }
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (!app.hotkey_capture) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::Text("Now: %s", hotkey_name(app.toggle_vk, app.toggle_mods).c_str());
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Press the new key (Ctrl / Alt / Shift allowed, mouse side buttons too)");
    ImGui::TextDisabled("Esc cancels");
    const ImGuiIO& io = ImGui::GetIO();
    const int mods = hotkey_mods(io.KeyCtrl, io.KeyAlt, io.KeyShift);
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && app.hotkey_capture; ++k) {
        const ImGuiKey key = static_cast<ImGuiKey>(k);
        if (!ImGui::IsKeyPressed(key, false)) continue;
        if (key == ImGuiKey_Escape) {
            app.hotkey_capture = false;
            break;
        }
        const int vk = vk_from_imgui_key(key);
        if (!vk) continue;  // modifiers alone, mouse, gamepad
        app.hotkey_capture = false;
        set_toggle_key(app, vk, mods);
    }
    for (int b = 3; b <= 4 && app.hotkey_capture; ++b) {  // mouse side buttons: VK_XBUTTON1 / VK_XBUTTON2
        if (!ImGui::IsMouseClicked(b)) continue;
        app.hotkey_capture = false;
        set_toggle_key(app, b == 3 ? 0x05 : 0x06, mods);
    }
    if (app.hotkey_capture) {
        if (ImGui::Button("Cancel##hotkey")) app.hotkey_capture = false;
        ImGui::SameLine();
        ImGui::BeginDisabled(app.toggle_vk == kDefaultToggleVk && app.toggle_mods == 0);
        if (ImGui::Button("Reset to F8")) {
            app.hotkey_capture = false;
            set_toggle_key(app, kDefaultToggleVk, 0);
        }
        ImGui::EndDisabled();
    }
    if (!app.hotkey_capture) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}  // namespace turbo
