// FC 27 LE Turbo GUI - the one palette and the spacing grid of the revamped GUI (GUI_REVAMP_SPEC.md 2.2).
// Header-only. Colours are ImVec4 values; sizes are UNSCALED pixels: wrap them in S() at the use site (S(theme::kSelectWidth)).
// Panels use the helpers below instead of ImGui::TextColored(ImVec4(...)) literals; the sweep of existing call sites is a later step.
#pragma once
#include <cstdarg>

#include "imgui.h"

namespace turbo::theme {

// ---- palette
constexpr ImVec4 kAccent{0.26f, 0.52f, 0.96f, 1.0f};  // primary buttons, selected row: the Turbo blue
constexpr ImVec4 kOk{0.45f, 0.85f, 0.50f, 1.0f};      // proven / applied status text
constexpr ImVec4 kWarn{1.00f, 0.78f, 0.25f, 1.0f};    // "not proven" banner, pending jobs
constexpr ImVec4 kErr{1.00f, 0.50f, 0.30f, 1.0f};     // errors: the existing orange-red, kept
constexpr ImVec4 kMuted{0.70f, 0.72f, 0.76f, 1.0f};   // hints: equals the style's TextDisabled (app.cpp)

// 2.22: table column dividers (ImGuiCol_TableBorderStrong / Light): clearly visible on the dark window; there is one theme (dark)
constexpr ImVec4 kTableBorderStrong{0.52f, 0.58f, 0.70f, 1.0f};
constexpr ImVec4 kTableBorderLight{0.40f, 0.45f, 0.56f, 1.0f};

inline ImVec4 accent() { return kAccent; }
inline ImVec4 accent_hover() { return ImVec4(kAccent.x * 1.15f, kAccent.y * 1.15f, kAccent.z * 1.05f, 1.0f); }
inline ImVec4 accent_active() { return ImVec4(kAccent.x * 0.85f, kAccent.y * 0.85f, kAccent.z * 0.85f, 1.0f); }
inline ImVec4 ok() { return kOk; }
inline ImVec4 warn() { return kWarn; }
inline ImVec4 err() { return kErr; }
inline ImVec4 muted() { return kMuted; }

// ---- spacing grid (unscaled px; 8 px grid)
constexpr float kGrid = 8.0f;              // S(8)
constexpr float kSectionPad = 12.0f;       // section padding
constexpr float kFieldRowHeight = 26.0f;   // one field row
constexpr float kFieldLabelWidth = 160.0f;
constexpr float kSelectWidth = 300.0f;     // left select panel
constexpr float kRailWidth = 36.0f;        // the select panel collapsed
constexpr float kNarrowWindow = 900.0f;    // below this the select panel collapses to the rail
// ---- type scale (multipliers of the body font; one font, no bold)
constexpr float kFontBody = 1.0f;
constexpr float kFontTitle = 1.25f;        // page title only
constexpr float kFontSmall = 0.85f;        // hints, status

// ---- text helpers (printf style)
inline void text_colored_v(const ImVec4& c, const char* fmt, va_list args) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
}
inline void ok_text(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    text_colored_v(kOk, fmt, a);
    va_end(a);
}
inline void warn_text(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    text_colored_v(kWarn, fmt, a);
    va_end(a);
}
inline void err_text(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    text_colored_v(kErr, fmt, a);
    va_end(a);
}
inline void muted_text(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    text_colored_v(kMuted, fmt, a);
    va_end(a);
}

// The one primary action of a page: a filled accent button. Returns true when clicked. `size` is in scaled px (0,0 = auto).
inline bool primary_button(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, accent());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent_hover());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_active());
    const bool hit = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return hit;
}

}  // namespace turbo::theme
