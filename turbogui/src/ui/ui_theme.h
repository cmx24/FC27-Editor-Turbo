// FC 27 LE Turbo GUI - the one palette and the spacing grid of the revamped GUI (GUI_REVAMP_SPEC.md 2.2).
// Header-only. Colours are ImVec4 values; sizes are UNSCALED pixels: wrap them in S() at the use site (S(theme::kSelectWidth)).
// Panels use the helpers below instead of ImGui::TextColored(ImVec4(...)) literals; the sweep of existing call sites is a later step.
#pragma once
#include <cfloat>
#include <cstdarg>

#include "imgui.h"

namespace turbo::theme {

// ---- palette
// Turbo X (2026-10-31): the X palette. Mint-teal accent (the game's own highlight hue), deeper slate surfaces, one semibold
// title font. The names stay: every page reads them through the helpers below.
constexpr ImVec4 kAccent{0.125f, 0.800f, 0.680f, 1.0f};  // text, check marks, grabs, outlines: mint teal
constexpr ImVec4 kOk{0.45f, 0.88f, 0.55f, 1.0f};         // proven / applied status text
constexpr ImVec4 kWarn{1.00f, 0.78f, 0.25f, 1.0f};       // "not proven", pending jobs
constexpr ImVec4 kErr{1.00f, 0.48f, 0.36f, 1.0f};        // errors
constexpr ImVec4 kMuted{0.62f, 0.66f, 0.72f, 1.0f};      // hints: equals the style's TextDisabled (ui_style.cpp)
constexpr ImVec4 kWindowBg{0.039f, 0.051f, 0.071f, 0.985f};
constexpr ImVec4 kCardBg{0.074f, 0.094f, 0.130f, 1.0f};  // a card sits one step above the window
constexpr ImVec4 kRule{0.22f, 0.27f, 0.34f, 0.60f};      // thin separator / divider line
constexpr ImVec4 kRailBg{0.030f, 0.040f, 0.056f, 1.0f};  // the rail is one step below the window
constexpr ImVec4 kRailHover{1.0f, 1.0f, 1.0f, 0.06f};    // hover wash on a transparent rail button
constexpr ImVec4 kRailActive{1.0f, 1.0f, 1.0f, 0.10f};
constexpr ImVec4 kTableBorderStrong{0.40f, 0.46f, 0.56f, 1.0f};
constexpr ImVec4 kTableBorderLight{0.28f, 0.33f, 0.42f, 1.0f};

inline ImVec4 accent() { return kAccent; }
inline ImVec4 accent_hover() { return ImVec4(kAccent.x * 1.15f, kAccent.y * 1.15f, kAccent.z * 1.05f, 1.0f); }
inline ImVec4 accent_active() { return ImVec4(kAccent.x * 0.85f, kAccent.y * 0.85f, kAccent.z * 0.85f, 1.0f); }
// 5.0c E4 (reviews/5.0b-design-review.md D-4): the FILL of an accent button. kAccent (0.26, 0.52, 0.96) is a text / glyph colour on the dark window
// (reads there at >= 4.5:1) but as a fill under the 0.96 text it drew 3.27:1 at rest and 2.65:1 hovered. The fill tokens give the text >= 4.5:1 in all
// three states (white too, for the rail), keep the hue, stay >= 3:1 against the window, popup and card (WCAG 1.4.11), and hover is the lightest step
// and active the darkest (test_e3_e4_dupes.h E4a). accent() / accent_hover() / accent_active() stay for text, check marks, grabs and outlines.
constexpr ImVec4 kAccentFill{0.075f, 0.600f, 0.505f, 1.0f};        // the FILL of an accent button: darker than the text accent
constexpr ImVec4 kAccentFillHover{0.095f, 0.680f, 0.575f, 1.0f};
constexpr ImVec4 kAccentFillActive{0.060f, 0.500f, 0.420f, 1.0f};
inline ImVec4 accent_fill() { return kAccentFill; }
inline ImVec4 accent_fill_hover() { return kAccentFillHover; }
inline ImVec4 accent_fill_active() { return kAccentFillActive; }
inline ImVec4 ok() { return kOk; }
inline ImVec4 warn() { return kWarn; }
inline ImVec4 err() { return kErr; }
inline ImVec4 muted() { return kMuted; }
inline ImVec4 card_bg() { return kCardBg; }
inline ImVec4 rule() { return kRule; }
inline ImVec4 accent_alpha(float a) { return ImVec4(kAccent.x, kAccent.y, kAccent.z, a); }

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
constexpr float kFontTitle = 1.35f;        // page title, card titles: the semibold face (title_font) at this size
constexpr float kFontSmall = 0.85f;        // hints, status
constexpr float kFontMark = 1.45f;         // the product mark on the rail

// Turbo X: the second face. The overlay loads Segoe UI Semibold next to the body font and stores it here; nullptr = the body font
// (PushFont(nullptr, size) keeps the current face), so every title helper works without it.
inline ImFont*& title_font() {
    static ImFont* f = nullptr;
    return f;
}
inline void push_title_font(float mult = kFontTitle) { ImGui::PushFont(title_font(), ImGui::GetStyle().FontSizeBase * mult); }
inline void pop_title_font() { ImGui::PopFont(); }

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
    ImGui::PushStyleColor(ImGuiCol_Button, accent_fill());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent_fill_hover());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_fill_active());
    const bool hit = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return hit;
}

// ---- layout helpers (3.0 theme). Sizes passed in are SCALED px, like primary_button's.

// A card: a bordered, slightly lighter, rounded panel. height 0 = grows with its content (AutoResizeY), else a fixed scaled height.
// Always pair with card_end(), whatever card_begin returns (it is BeginChild). The lighter ChildBg is pushed only around BeginChild
// (the child reads its background there), so a child nested in the card keeps the plain transparent ChildBg.
inline bool card_begin(const char* id, float height = 0.0f) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
    const ImGuiChildFlags flags = ImGuiChildFlags_Borders | (height <= 0.0f ? ImGuiChildFlags_AutoResizeY : 0);
    const bool open = ImGui::BeginChild(id, ImVec2(0.0f, height), flags, ImGuiWindowFlags_None);
    ImGui::PopStyleColor();
    return open;
}
inline void card_end() { ImGui::EndChild(); }

// A section heading: title-size text, then a thin muted rule across the available width.
inline void section_title(const char* text) {
    push_title_font();
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + w, p.y), ImGui::ColorConvertFloat4ToU32(kRule), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
}

// Turbo X (2026-10-31): a card title: a short accent bar, the title in the semibold face, a thin rule under it. Replaces SeparatorText
// inside cards. Sizes derive from the font size, so it scales with the UI size without S().
inline void card_title(const char* text) {
    push_title_font(1.12f);
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, p.y + fs * 0.14f), ImVec2(p.x + fs * 0.18f, p.y + fs * 0.96f), ImGui::ColorConvertFloat4ToU32(kAccent), fs * 0.09f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fs * 0.55f);
    ImGui::TextUnformatted(text);
    pop_title_font();
    const ImVec2 q = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(q.x, q.y + 1.0f), ImVec2(q.x + ImGui::GetContentRegionAvail().x, q.y + 1.0f), ImGui::ColorConvertFloat4ToU32(kRule), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
}

// Turbo X: a Home tile: a card-coloured rounded button with the area name in the semibold face and one muted line under it.
// `id` keeps the test id (###home_tile_n). Returns true when clicked.
inline bool tile(const char* id, const char* name, const char* line, const ImVec2& size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Button, kCardBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(kCardBg.x + 0.03f, kCardBg.y + 0.035f, kCardBg.z + 0.045f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_alpha(0.18f));
    ImGui::PushStyleColor(ImGuiCol_Border, kRule);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));  // the button's own label is invisible: the text below is drawn by hand
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool hit = ImGui::Button(id, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    const float fs = ImGui::GetFontSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, p.y + fs * 0.9f), ImVec2(p.x + fs * 0.18f, p.y + size.y - fs * 0.9f), ImGui::ColorConvertFloat4ToU32(kAccent), fs * 0.09f);
    push_title_font(1.12f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + fs * 0.9f, p.y + fs * 0.75f), ImGui::GetColorU32(ImGuiCol_Text), name);
    pop_title_font();
    dl->AddText(ImVec2(p.x + fs * 0.9f, p.y + fs * 0.75f + fs * 1.45f), ImGui::ColorConvertFloat4ToU32(kMuted), line);
    return hit;
}

// A rounded filled pill with text (status chip). No interaction; takes the place of one item on the line (SameLine works).
// Text is dark on a light `bg`, light on a dark one.
inline void chip(const char* text, const ImVec4& bg) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float padx = st.FramePadding.x, pady = st.FramePadding.y * 0.4f;
    const ImVec2 size(ts.x + padx * 2.0f, ts.y + pady * 2.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    const float lum = 0.299f * bg.x + 0.587f * bg.y + 0.114f * bg.z;
    const ImVec4 fg = lum > 0.60f ? ImVec4(0.05f, 0.06f, 0.08f, 1.0f) : ImVec4(0.96f, 0.96f, 0.97f, 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::ColorConvertFloat4ToU32(bg), size.y * 0.5f);
    dl->AddText(ImVec2(p.x + padx, p.y + pady), ImGui::ColorConvertFloat4ToU32(fg), text);
}

// A navigation-rail entry: full-width (width <= 0 = the whole line), left-aligned text. Selected = accent fill, white text;
// otherwise transparent with a hover wash. Returns true when clicked. The label keeps its ImGui id, so ##/### suffixes work.
inline bool rail_button(const char* label, bool selected, float width) {
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, accent_fill());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent_fill_hover());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_fill_active());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRailHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kRailActive);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    }
    const bool hit = ImGui::Button(label, ImVec2(width > 0.0f ? width : -FLT_MIN, 0.0f));
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return hit;
}

}  // namespace turbo::theme
