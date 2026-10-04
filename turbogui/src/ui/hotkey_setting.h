// FC 27 LE Turbo GUI - Status tab > Settings: the show/hide key, captured by pressing it (core/hotkey.h).
// Header-only (included by ui_tools.cpp). While "Change" waits for a key, App::hotkey_capture is set: the Windows host
// then passes the current show/hide key to ImGui instead of hiding Turbo, so it can be picked again.
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

inline void hotkey_setting(App& app) {
    app.hotkey_capture_frame = ImGui::GetFrameCount();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Show/hide key: %s", hotkey_name(app.toggle_vk, app.toggle_mods).c_str());
    ImGui::SameLine();
    if (!app.hotkey_capture) {
        if (ImGui::SmallButton("Change##hotkey")) app.hotkey_capture = true;
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Press the new key (Ctrl / Alt / Shift allowed), Esc cancels");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel##hotkey")) app.hotkey_capture = false;
        const ImGuiIO& io = ImGui::GetIO();
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
            set_toggle_key(app, vk, hotkey_mods(io.KeyCtrl, io.KeyAlt, io.KeyShift));
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(app.toggle_vk == kDefaultToggleVk && app.toggle_mods == 0);
    if (ImGui::SmallButton("Reset to F8")) {
        app.hotkey_capture = false;
        set_toggle_key(app, kDefaultToggleVk, 0);
    }
    ImGui::EndDisabled();
    if (hotkey_types_text(app.toggle_vk, app.toggle_mods))
        ImGui::TextDisabled("While you type in a Turbo text box this key types; click outside the box to use it.");
}

}  // namespace turbo
