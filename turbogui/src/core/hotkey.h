// FC 27 LE Turbo GUI - the show/hide key (Status tab > Settings): any key, optionally with Ctrl / Alt / Shift.
// Kept in gui_settings.json as gui.toggle_key (Windows virtual-key code) and gui.toggle_mods (bit 1 Ctrl, 2 Alt, 4 Shift).
// Header-only: used by the App (settings), the Status tab (capture) and the Windows host (WndProc, input shield).
#pragma once
#include <cstdio>
#include <string>

namespace turbo {

constexpr int kHotkeyCtrl = 1, kHotkeyAlt = 2, kHotkeyShift = 4;
constexpr int kDefaultToggleVk = 0x77;  // F8

// Keys that are only modifiers (never the key itself)
inline bool hotkey_modifier_vk(int vk) {
    return vk == 0x10 || vk == 0x11 || vk == 0x12 || (vk >= 0xA0 && vk <= 0xA5) || vk == 0x5B || vk == 0x5C;
}

// Keys a hotkey may use: 1..254, not a mouse button, not a modifier, not Escape (Escape cancels the capture)
inline bool hotkey_valid_vk(int vk) {
    if (vk <= 0 || vk >= 255) return false;
    if (vk == 0x01 || vk == 0x02 || vk == 0x04 || vk == 0x05 || vk == 0x06) return false;  // mouse buttons
    if (vk == 0x1B) return false;
    return !hotkey_modifier_vk(vk);
}

// "F8", "K", "7", "Num 5", "Page Up", ... ("Key 0xNN" for the rest)
inline std::string vk_name(int vk) {
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x70 + 1);
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    if (vk >= 0x60 && vk <= 0x69) return "Num " + std::to_string(vk - 0x60);
    switch (vk) {
        case 0x08: return "Backspace";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case 0x13: return "Pause";
        case 0x14: return "Caps Lock";
        case 0x20: return "Space";
        case 0x21: return "Page Up";
        case 0x22: return "Page Down";
        case 0x23: return "End";
        case 0x24: return "Home";
        case 0x25: return "Left";
        case 0x26: return "Up";
        case 0x27: return "Right";
        case 0x28: return "Down";
        case 0x2C: return "Print Screen";
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";
        case 0x6A: return "Num *";
        case 0x6B: return "Num +";
        case 0x6D: return "Num -";
        case 0x6E: return "Num .";
        case 0x6F: return "Num /";
        case 0x90: return "Num Lock";
        case 0x91: return "Scroll Lock";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xC0: return "`";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        default: break;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "Key 0x%02X", vk & 0xFF);
    return buf;
}

// "Ctrl+Shift+F8"
inline std::string hotkey_name(int vk, int mods) {
    std::string s;
    if (mods & kHotkeyCtrl) s += "Ctrl+";
    if (mods & kHotkeyAlt) s += "Alt+";
    if (mods & kHotkeyShift) s += "Shift+";
    return s + vk_name(vk);
}

inline int hotkey_mods(bool ctrl, bool alt, bool shift) {
    return (ctrl ? kHotkeyCtrl : 0) | (alt ? kHotkeyAlt : 0) | (shift ? kHotkeyShift : 0);
}

// The pressed key is the show/hide key: same key, and exactly the chosen modifiers held
inline bool hotkey_matches(int want_vk, int want_mods, int vk, bool ctrl, bool alt, bool shift) {
    return vk == want_vk && hotkey_mods(ctrl, alt, shift) == (want_mods & 7);
}

// A key without Ctrl / Alt that also types text (letters, digits, space, punctuation): while a Turbo text field has
// the keyboard it goes to the field instead of hiding Turbo
inline bool hotkey_types_text(int vk, int mods) {
    if (mods & (kHotkeyCtrl | kHotkeyAlt)) return false;
    return vk == 0x20 || (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') || (vk >= 0x60 && vk <= 0x6F) ||
           (vk >= 0xBA && vk <= 0xC0) || (vk >= 0xDB && vk <= 0xDE);
}

// Input shield (raw input, DirectInput buffered data): a key event the game must not get. Only key-downs of the
// hidden show/hide key: the press that shows Turbo reaches the game before Turbo is shown, so hiding its key-up would
// leave that key held down in the game (an extra key-up is harmless).
inline bool hotkey_hides_event(int hidden_key, int key, bool key_up) {
    return hidden_key > 0 && key == hidden_key && !key_up;
}

}  // namespace turbo
