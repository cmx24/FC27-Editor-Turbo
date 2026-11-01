// FC 27 LE Turbo GUI - the one palette and the spacing grid of the revamped GUI (GUI_REVAMP_SPEC.md 2.2).
// Header-only. Colours are ImVec4 values; sizes are UNSCALED pixels: wrap them in S() at the use site (S(theme::kSelectWidth)).
// Panels use the helpers below instead of ImGui::TextColored(ImVec4(...)) literals; the sweep of existing call sites is a later step.
#pragma once
#include <cfloat>
#include <cstdarg>
#include <cstdio>

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

// ---- Turbo X (2026-10-31, DESIGN_SPEC_X.md "Players > Player"): people rows, stat tiles, the segmented control, empty states.
// Sizes passed in are SCALED px; everything derived from the font scales by itself.

// Moves the cursor right so that an item `w` px wide sits in the middle of the available width.
inline void center_x(float w) {
    const float room = ImGui::GetContentRegionAvail().x - w;
    if (room > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room * 0.5f);
}

// A small rounded number pill drawn at `p` (top-left): accent fill when `key`, else the frame colour. Returns its size.
inline ImVec2 number_pill(ImDrawList* dl, const ImVec2& p, const char* text, bool key) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 size(ts.x + fs * 0.8f, ts.y + fs * 0.2f);
    const ImU32 bg = key ? ImGui::ColorConvertFloat4ToU32(kAccentFill) : ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 fg = key ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, size.y * 0.5f);
    dl->AddText(ImVec2(p.x + fs * 0.4f, p.y + fs * 0.1f), fg, text);
    return size;
}

// A person row: [picture] name (semibold) / one muted line, and an OVR pill at the right (accent when >= 85). `id` is the
// Selectable's label and so its test id (the player's ID, say): its own text is invisible, the row is drawn by hand, so the ID
// is never on screen. Selected = accent bar at the left + accent wash. `draw_picture()` is called with the cursor at the
// picture slot (`picture` px square, vertically centred); it draws an item of that size. Returns true when clicked; *hovered
// (optional) says whether the row is under the mouse (the caller's tooltip; the picture item is the last item, not the row).
template <class DrawPicture>
inline bool person_row(const char* id, const char* name, const char* line, int ovr, bool selected, float height, float picture,
                       DrawPicture draw_picture, bool* hovered = nullptr) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float fs = ImGui::GetFontSize();
    const float pad = fs * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Header, accent_alpha(0.16f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, accent_alpha(selected ? 0.22f : 0.08f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, accent_alpha(0.28f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));  // the label is the id only
    const bool hit = ImGui::Selectable(id, selected, ImGuiSelectableFlags_None, ImVec2(w, height));
    ImGui::PopStyleColor(4);
    if (hovered) *hovered = ImGui::IsItemHovered();
    const ImVec2 after = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddRectFilled(ImVec2(p.x, p.y + fs * 0.2f), ImVec2(p.x + fs * 0.18f, p.y + height - fs * 0.2f), ImGui::ColorConvertFloat4ToU32(kAccent), fs * 0.09f);
    // the pill, right-aligned and vertically centred
    char num[16];
    std::snprintf(num, sizeof num, "%d", ovr);
    const ImVec2 ps = ImGui::CalcTextSize(num);
    const ImVec2 pill_size(ps.x + fs * 0.8f, ps.y + fs * 0.2f);
    const ImVec2 pill_pos(p.x + w - pad - pill_size.x, p.y + (height - pill_size.y) * 0.5f);
    number_pill(dl, pill_pos, num, ovr >= 85);
    // the two text lines, clipped before the pill
    const float x = p.x + pad + picture + fs * 0.6f;
    const float two = fs * 2.25f;  // name line + muted line
    const float y = p.y + (height - two) * 0.5f;
    dl->PushClipRect(ImVec2(x, p.y), ImVec2(pill_pos.x - fs * 0.4f, p.y + height), true);
    push_title_font(1.0f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), name);
    pop_title_font();
    dl->AddText(ImVec2(x, y + fs * 1.2f), ImGui::ColorConvertFloat4ToU32(kMuted), line);
    dl->PopClipRect();
    // the picture, in its slot
    ImGui::SetCursorScreenPos(ImVec2(p.x + pad, p.y + (height - picture) * 0.5f));
    draw_picture();
    // back to where the row ended, through a zero-size item placed one item-spacing higher: ImGui wants an item after a
    // SetCursorScreenPos that moves the cursor (the "SetCursorPos() to extend window boundaries" error box of the Looks list)
    ImGui::SetCursorScreenPos(ImVec2(after.x, after.y - ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    return hit;
}

// A stat tile: a card `width` px wide, the label small and muted, the value big in the semibold face (`value_col` null = the
// text colour). stat_tile_begin / stat_tile_end let the caller add a line under the value (the Shown OVR field); stat_tile
// is the plain tile. Always pair stat_tile_begin with stat_tile_end (it is BeginChild). height 0 = as tall as its content.
inline bool stat_tile_begin(const char* id, const char* label, const char* value, float width, float height = 0.0f, const ImVec4* value_col = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
    ImGui::PushStyleColor(ImGuiCol_Border, kRule);
    const ImGuiChildFlags flags = ImGuiChildFlags_Borders | (height <= 0.0f ? ImGuiChildFlags_AutoResizeY : 0);
    const bool open = ImGui::BeginChild(id, ImVec2(width, height), flags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kFontSmall);
    muted_text("%s", label);
    ImGui::PopFont();
    push_title_font(1.8f);
    if (value_col) ImGui::PushStyleColor(ImGuiCol_Text, *value_col);
    ImGui::TextUnformatted(value);
    if (value_col) ImGui::PopStyleColor();
    pop_title_font();
    return open;
}
inline void stat_tile_end() { ImGui::EndChild(); }
inline void stat_tile(const char* id, const char* label, const char* value, float width, float height = 0.0f, const ImVec4* value_col = nullptr) {
    stat_tile_begin(id, label, value, width, height, value_col);
    stat_tile_end();
}
// The height a stat tile takes with `extra_lines` frame-high lines under the value (so tiles in a row can be given one height).
inline float stat_tile_height(int extra_lines = 0) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    return st.WindowPadding.y * 2.0f + fs * kFontSmall + fs * 1.8f + st.ItemSpacing.y * (1 + extra_lines) + ImGui::GetFrameHeight() * extra_lines + 2.0f;
}

// A segmented control: `n` pill buttons on one line, the selected one accent-tinted. Each label keeps its own ImGui id
// (write them "Text###id"). Returns true when the selection changed; `selected` is updated.
inline bool segmented(const char* const* labels, int n, int& selected) {
    bool changed = false;
    const float fs = ImGui::GetFontSize();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, fs);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(fs * 0.8f, ImGui::GetStyle().FramePadding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(fs * 0.35f, ImGui::GetStyle().ItemSpacing.y));
    for (int i = 0; i < n; ++i) {
        const bool on = i == selected;
        if (i > 0) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, on ? accent_alpha(0.22f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? accent_alpha(0.30f) : kRailHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_alpha(0.36f));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? kAccent : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        if (ImGui::Button(labels[i]) && !on) {
            selected = i;
            changed = true;
        }
        ImGui::PopStyleColor(4);
    }
    ImGui::PopStyleVar(3);
    return changed;
}

// An empty state: a third of the way down the available space, `title` centred in the title face and, when given, one muted
// line under it. The caller draws its two actions after this (center_x to centre them).
inline void empty_state(const char* title, const char* line = nullptr) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(0.0f, avail.y * 0.3f));
    push_title_font();
    center_x(ImGui::CalcTextSize(title).x);
    ImGui::TextUnformatted(title);
    pop_title_font();
    if (line && *line) {
        center_x(ImGui::CalcTextSize(line).x);
        muted_text("%s", line);
    }
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.5f));
}

}  // namespace turbo::theme
