// FC 27 LE Turbo GUI - asset picker pieces (ui_asset_browser.h). The badge and the scope combo are complete; the paged
// browser is a stub until WP-09.
#include "ui_asset_browser.h"

#include "imgui.h"

namespace turbo {

namespace {
struct BadgeStyle {
    const char* label;
    const char* tooltip;
    ImU32 bg, fg;
};
const BadgeStyle& badge_style(AssetBadge b) {
    static const BadgeStyle kStyles[] = {
        {"Hidden", "In the game files; no player of this save uses it", IM_COL32(52, 78, 120, 255), IM_COL32(220, 232, 255, 255)},
        {"3D only", "The game has the 3D model but no preview picture", IM_COL32(60, 60, 66, 255), IM_COL32(215, 215, 220, 255)},
        {"Preview only", "Only a preview picture: the game has no 3D model for it, so it cannot be used",
         IM_COL32(70, 66, 50, 255), IM_COL32(200, 190, 150, 255)},
        {"Not in game", "Not in the game files of this build", IM_COL32(120, 40, 40, 255), IM_COL32(255, 215, 215, 255)},
        {"No recording", "No recording of this name in the active commentary language", IM_COL32(70, 66, 50, 255),
         IM_COL32(200, 190, 150, 255)},
        {"Not in tattoo table", "In the game files, but not in the save's tattoo table", IM_COL32(70, 66, 50, 255),
         IM_COL32(200, 190, 150, 255)},
    };
    const size_t i = static_cast<size_t>(b);
    return kStyles[i < sizeof(kStyles) / sizeof(kStyles[0]) ? i : 0];
}
}  // namespace

const char* asset_badge_label(AssetBadge b) { return badge_style(b).label; }
const char* asset_badge_tooltip(AssetBadge b) { return badge_style(b).tooltip; }

bool asset_badge(AssetBadge b, const char* tooltip) {
    const BadgeStyle& s = badge_style(b);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 ts = ImGui::CalcTextSize(s.label);
    const float pad = ImGui::GetStyle().FramePadding.x * 0.5f;
    const ImVec2 size(ts.x + 2.0f * pad, ImGui::GetTextLineHeight());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), s.bg, 3.0f);
    dl->AddText(ImVec2(p.x + pad, p.y + (size.y - ts.y) * 0.5f), s.fg, s.label);
    ImGui::Dummy(size);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetTooltip("%s", tooltip ? tooltip : s.tooltip);
    return hovered;
}

bool asset_scope_combo(const char* id, AssetScope& scope, float width) {
    const AssetScope before = scope;
    if (width > 0.0f) ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, assets::scope_label(scope))) {
        for (AssetScope s : {AssetScope::All, AssetScope::InSave, AssetScope::Hidden}) {
            if (ImGui::Selectable(assets::scope_label(s), s == scope)) scope = s;
            if (s == scope) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hidden: in the game files, but no player or team of this save uses it");
    return scope != before;
}

bool asset_browser(const char* id, AssetBrowserState& st, const std::vector<AssetBrowserItem>& items, int64_t& picked,
                   const std::function<void(const std::string& path)>& want) {
    ImGui::PushID(id);
    ImGui::TextDisabled("Game asset browser: not available in this build (%d items)", static_cast<int>(items.size()));
    ImGui::PopID();
    return false;
}

}  // namespace turbo
