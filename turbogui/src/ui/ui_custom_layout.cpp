// FC 27 LE Turbo GUI - Customize layout (Turbo X 2.0). See ui_custom_layout.h.
#include "ui_custom_layout.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"

#include "app.h"
#include "gui_prefs.h"
#include "ui_shell.h"
#include "ui_split.h"

namespace turbo {
namespace custom_layout {

bool& window_open() {
    static bool open = false;
    return open;
}

RegionScale::RegionScale(float scale) {
    const float s = CustomLayout::clamp_scale(scale);
    if (s == 1.0f || ImGui::GetCurrentContext() == nullptr) return;
    ImGuiStyle& st = ImGui::GetStyle();
    saved_ = st.FontScaleMain;
    st.FontScaleMain = saved_ * s;
    // the current base size, re-applied: ImGui computes the final size again with the new main scale (and so does every window and
    // font push inside the scope)
    ImGui::PushFont(nullptr, ImGui::GetCurrentContext()->FontSizeBase);
    on_ = true;
}

RegionScale::~RegionScale() {
    if (!on_) return;
    ImGui::GetStyle().FontScaleMain = saved_;
    ImGui::PopFont();  // back to the size the scope started with
}

bool save(App& app) {
    const bool ok = app.save_gui_settings();
    if (!ok) app.notify("cannot save the layout", true);
    return ok;
}

void reset(App& app) {
    app.prefs.custom = CustomLayout();
    app.prefs.splits.folded.clear();
    app.prefs.splits.dirty = true;
    app.prefs.rail_collapsed = false;
    save(app);
    app.notify("Layout back to the default: every menu, tab and panel shows, all text sizes 100 %");
}

void handle_shortcut(App& app) {
    if (!app.visible || app.hotkey_capture) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false)) window_open() = !window_open();
}

namespace {

// A text-size slider in percent; true when the person let go of it after a change (the caller saves then, not on every frame of a drag)
bool percent_slider(const char* label, float& scale) {
    int pct = static_cast<int>(std::lround(CustomLayout::clamp_scale(scale) * 100.0f));
    ImGui::SetNextItemWidth(S(220.0f));
    if (ImGui::SliderInt(label, &pct, static_cast<int>(CustomLayout::kScaleMin * 100.0f + 0.5f), static_cast<int>(CustomLayout::kScaleMax * 100.0f + 0.5f),
                         "%d %%", ImGuiSliderFlags_AlwaysClamp))
        scale = CustomLayout::clamp_scale(static_cast<float>(pct) / 100.0f);
    return ImGui::IsItemDeactivatedAfterEdit();
}

void note(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

}  // namespace

void draw_window(App& app) {
    if (!window_open()) return;
    using namespace shell;
    CustomLayout& c = app.prefs.custom;
    bool save_now = false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(580.0f), S(680.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Customize layout###custom_layout", &window_open(), ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    note("Show, hide, resize and scale Turbo's menus and panels. Every change shows at once and is saved. Ctrl+L opens this window from anywhere in Turbo.");

    ImGui::SeparatorText("Main menus");
    if (ImGui::Checkbox("Top bar###cl_top_bar", &c.top_bar)) save_now = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The row at the top: connection, search, Refresh, Undo, Changes");
    ImGui::SameLine(S(240.0f));
    if (percent_slider("Text###cl_scale_top_bar", c.scale_top_bar)) save_now = true;

    if (ImGui::Checkbox("Left menu###cl_rail", &c.rail)) save_now = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Home, Players, Looks, Club, Tactics, Game settings, Career, Turbo settings");
    ImGui::SameLine(S(240.0f));
    if (percent_slider("Text###cl_scale_rail", c.scale_rail)) save_now = true;
    {
        ImGui::Indent(S(24.0f));
        bool narrow = app.prefs.rail_collapsed;
        if (ImGui::Checkbox("Narrow (first letters only)###cl_rail_narrow", &narrow)) {
            app.prefs.rail_collapsed = narrow;
            save_now = true;
        }
        ImGui::SameLine(S(240.0f) - S(24.0f));
        int w = static_cast<int>(std::lround(CustomLayout::clamp_rail(c.rail_width)));
        ImGui::SetNextItemWidth(S(220.0f));
        ImGui::BeginDisabled(narrow);
        if (ImGui::SliderInt("Width###cl_rail_width", &w, static_cast<int>(CustomLayout::kRailMinW), static_cast<int>(CustomLayout::kRailMaxW), "%d px",
                             ImGuiSliderFlags_AlwaysClamp))
            c.rail_width = CustomLayout::clamp_rail(static_cast<float>(w));
        if (ImGui::IsItemDeactivatedAfterEdit()) save_now = true;
        ImGui::EndDisabled();
        ImGui::Unindent(S(24.0f));
    }

    if (ImGui::Checkbox("Page tabs###cl_page_tabs", &c.page_tabs)) save_now = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The tabs under the top bar (Player, Create & clone, ...) and the Back button");
    ImGui::SameLine(S(240.0f));
    if (percent_slider("Text###cl_scale_tabs", c.scale_tabs)) save_now = true;
    if (!c.top_bar && !c.rail) note("Top bar and left menu are both hidden: a small Menus button stays at the top left of the page.");

    ImGui::SeparatorText("Page");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Page content");
    ImGui::SameLine(S(240.0f));
    if (percent_slider("Text###cl_scale_content", c.scale_content)) save_now = true;
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Whole Turbo (UI size)");
        ImGui::SameLine(S(240.0f));
        float us = app.ui_scale_user;
        ImGui::SetNextItemWidth(S(220.0f));
        if (ImGui::SliderFloat("###cl_ui_size", &us, kUiScaleMin, kUiScaleMax, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
            app.ui_scale_user = std::max(kUiScaleMin, std::min(kUiScaleMax, us));
        if (ImGui::IsItemDeactivatedAfterEdit()) save_now = true;
        note("Also Ctrl + mouse wheel over Turbo, Ctrl + 0 for 1.00x.");
    }

    ImGui::SeparatorText("Panels");
    for (const char* id : {"##select", "##lslist", "##lspreview"}) {
        bool shown = !app.prefs.splits.is_folded(id);
        const std::string lbl = std::string(split::fold_label(id)) + "###cl_pane" + id;
        if (ImGui::Checkbox(lbl.c_str(), &shown)) {
            app.prefs.splits.set_folded(id, !shown);
            save_now = true;
        }
    }
    note("Drag a panel's divider to resize it; right-click the divider to hide the panel; the > strip brings it back. Table columns are resized by dragging their header edges.");
    if (ImGui::Button("Reset panel sizes###cl_reset_panels")) {
        app.prefs.splits.widths.clear();
        app.prefs.splits.dirty = true;
        save_now = true;
        app.notify("Panel sizes back to the default");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every list and preview panel goes back to its first-use width (column widths stay)");

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Top bar buttons###cl_sec_top")) {
        ImGui::BeginDisabled(!c.top_bar);
        for (const auto& it : CustomLayout::kTopItems) {
            bool shown = c.top_item(it.key);
            const std::string lbl = std::string(it.label) + "###cl_top_" + it.key;
            if (ImGui::Checkbox(lbl.c_str(), &shown) && CustomLayout::set_in(c.hidden_top, it.key, !shown)) save_now = true;
        }
        ImGui::EndDisabled();
        note("The Layout button always stays on the top bar.");
    }
    if (ImGui::CollapsingHeader("Left menu areas###cl_sec_areas")) {
        ImGui::BeginDisabled(!c.rail);
        for (int ai = 0; ai < kAreaCount; ++ai) {
            const Area a = static_cast<Area>(ai);
            if (!area_visible(a)) continue;
            const std::string name = area_label(a);
            const bool settings = (a == Area::Settings);
            bool shown = c.area_shown(name);
            const std::string lbl = name + "###cl_area_" + std::to_string(ai);
            ImGui::BeginDisabled(settings);
            if (ImGui::Checkbox(lbl.c_str(), &shown) && c.set_area_hidden(name, !shown)) save_now = true;
            ImGui::EndDisabled();
            if (settings && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Turbo settings always stays: it holds this window and the way back");
        }
        ImGui::EndDisabled();
        note("A hidden area still opens from the search and from links on other pages.");
    }
    if (ImGui::CollapsingHeader("Page tabs###cl_sec_pages")) {
        ImGui::BeginDisabled(!c.page_tabs);
        for (int ai = 0; ai < kAreaCount; ++ai) {
            const Area a = static_cast<Area>(ai);
            if (!area_visible(a)) continue;
            const auto& strip = pages_of(a);
            int n = 0;
            for (Page p : strip)
                if (p != Page::GameplayPresets && page_visible(p)) ++n;
            if (n < 2) continue;  // an area with one page has no tabs
            ImGui::PushID(ai);
            if (ImGui::TreeNode("##cl_area_pages", "%s", area_label(a))) {
                for (Page p : strip) {
                    if (p == Page::GameplayPresets || !page_visible(p)) continue;
                    bool shown = c.page_shown(page_key(p));
                    const std::string lbl = std::string(page_label(p)) + "###cl_page_" + page_key(p);
                    if (ImGui::Checkbox(lbl.c_str(), &shown) && CustomLayout::set_in(c.hidden_pages, page_key(p), !shown)) save_now = true;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        note("The tab of the page you are on always shows.");
    }

    ImGui::Separator();
    if (ImGui::Button("Reset to default###cl_reset")) ImGui::OpenPopup("Reset the layout?###cl_reset_confirm");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Every menu, tab and panel shows again, all text sizes 100 %, the left menu wide");
    ImGui::SameLine();
    if (ImGui::Button("Close###cl_close")) window_open() = false;
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Reset the layout?###cl_reset_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Every menu, tab and panel shows again and every text size goes back to 100 %.");
        ImGui::TextUnformatted("Panel widths, column widths and the window size are not changed.");
        if (ImGui::Button("Cancel###cl_reset_cancel", ImVec2(S(140.0f), 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Reset###cl_reset_go", ImVec2(S(140.0f), 0.0f))) {
            reset(app);
            save_now = false;  // reset() saved
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
    if (save_now) save(app);
}

}  // namespace custom_layout
}  // namespace turbo
