// FC 27 LE Turbo GUI - asset picker pieces (ui_asset_browser.h): the badge, the scope combo and the paged browser.
#include "ui_asset_browser.h"

#include <algorithm>
#include <cstdio>

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

namespace {
AssetView g_view;
}  // namespace

void set_asset_view(AssetView v) { g_view = std::move(v); }
const AssetView& asset_view() { return g_view; }

bool asset_browser(const char* id, AssetBrowserState& st, const std::vector<AssetBrowserItem>& items, int64_t& picked,
                   const AssetTileDraw& draw_picture, int64_t selected, float height, bool team) {
    ImGui::PushID(id);
    const float em = ImGui::GetFontSize();
    bool changed = false;
    ImGui::SetNextItemWidth(em * 14.0f);
    if (ImGui::InputTextWithHint("##search", "name or ID", st.search, sizeof(st.search))) changed = true;
    ImGui::SameLine();
    if (asset_scope_combo("##scope", st.scope, em * 9.0f)) changed = true;
    if (changed) st.page = 0;

    const std::vector<size_t> shown = asset_filter(items, st.scope, st.search);
    const auto range = asset_page_range(shown.size(), st.page);
    const int pages = asset_page_count(shown.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(st.page <= 0);
    if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) --st.page;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("Page %d of %d", st.page + 1, pages);
    ImGui::SameLine();
    ImGui::BeginDisabled(st.page + 1 >= pages);
    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) ++st.page;
    ImGui::EndDisabled();
    size_t n_hidden = 0;
    for (const auto& it : items) n_hidden += it.hidden ? 1 : 0;
    ImGui::TextDisabled("%zu shown of %zu (%zu hidden); %d per page, pictures load as they come into view", shown.size(), items.size(),
                        n_hidden, kAssetBrowserPage);

    bool clicked_any = false;
    const float cell = em * 5.5f;
    const float line = ImGui::GetTextLineHeight();
    ImGui::BeginChild("##grid", ImVec2(0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    const ImGuiStyle& style = ImGui::GetStyle();
    const int cols = std::max(1, int((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / (cell + style.ItemSpacing.x)));
    const size_t on_page = range.second - range.first;
    const int nrows = int((on_page + size_t(cols) - 1) / size_t(cols));
    if (on_page == 0) ImGui::TextDisabled(items.empty() ? "(none in the catalog)" : "(nothing matches)");
    ImGuiListClipper clip;
    clip.Begin(nrows, cell + line + style.ItemSpacing.y);
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            for (int c = 0; c < cols; ++c) {
                const size_t k = range.first + size_t(r) * size_t(cols) + size_t(c);
                if (k >= range.second) break;
                const AssetBrowserItem& it = items[shown[k]];
                if (c) ImGui::SameLine();
                ImGui::PushID(static_cast<int>(k));
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                if (draw_picture) draw_picture(it, cell);
                else ImGui::Dummy(ImVec2(cell, cell));
                ImGui::SetCursorScreenPos(p0);
                if (ImGui::InvisibleButton("##tile", ImVec2(cell, cell + line))) {
                    picked = it.id;
                    clicked_any = true;
                }
                const bool hovered = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec4 clipr(p0.x, p0.y, p0.x + cell, p0.y + cell + line);
                dl->AddText(nullptr, 0.0f, ImVec2(p0.x + 2.0f, p0.y + cell), ImGui::GetColorU32(ImGuiCol_Text), it.name.c_str(), nullptr,
                            0.0f, &clipr);
                if (it.hidden) {
                    // small "Hidden" tag in the picture's top-left corner
                    const char* tag = asset_badge_label(AssetBadge::Hidden);
                    const ImVec2 ts = ImGui::CalcTextSize(tag);
                    dl->AddRectFilled(p0, ImVec2(p0.x + ts.x + 6.0f, p0.y + ts.y + 2.0f), IM_COL32(52, 78, 120, 230), 3.0f);
                    dl->AddText(ImVec2(p0.x + 3.0f, p0.y + 1.0f), IM_COL32(220, 232, 255, 255), tag);
                }
                if (selected == it.id || hovered)
                    dl->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell + line),
                                ImGui::GetColorU32(selected == it.id ? ImGuiCol_CheckMark : ImGuiCol_ButtonHovered), 0.0f, 2.0f);
                if (hovered) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s  (#%lld)", it.name.c_str(), static_cast<long long>(it.id));
                    const std::string src = assets::name_source_label(it.name_source);
                    if (!src.empty()) ImGui::TextDisabled("%s", src.c_str());
                    if (it.hidden)
                        ImGui::TextDisabled("Hidden: in the game files; no %s of this save uses it", team ? "team" : "player");
                    if (!it.path.empty()) ImGui::TextDisabled("%s", it.path.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopID();
    return clicked_any;
}

bool asset_browser(const char* id, AssetBrowserState& st, const std::vector<AssetBrowserItem>& items, int64_t& picked,
                   const std::function<void(const std::string& path)>& want) {
    return asset_browser(id, st, items, picked, [&want](const AssetBrowserItem& it, float side) {
        if (want && !it.path.empty()) want(it.path);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + side, p.y + side), ImGui::GetColorU32(ImGuiCol_Border));
        ImGui::Dummy(ImVec2(side, side));
    });
}

}  // namespace turbo
