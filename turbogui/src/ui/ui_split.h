// FC 27 LE Turbo GUI - split panes the person can resize (2.22 G-resize; Cassio 10-19: "STILL cant resize columns, rows and frames").
//
// One helper for every side-by-side (or stacked) pair of panes:
//     if (split::pane_x("##select", 300.0f)) { ...left pane... }   // returns BeginChild's result
//     ImGui::EndChild();                                           // always, like BeginChild
//     ImGui::SameLine();
//     ImGui::BeginChild("##editor", ImVec2(0, 0), ImGuiChildFlags_Borders); ...   // the rest of the row
// What it adds to a plain BeginChild:
//   * ImGuiChildFlags_ResizeX (pane_y: ResizeY): the right (bottom) border of the pane is a divider, grabbed within +-4 px at UI size 1
//     (ImGui's WindowBorderHoverPadding, scaled with the UI size); the mouse cursor turns into the resize arrow and the border
//     lights up while the pointer is on it (ImGui draws both)
//   * a visible grip line in the gap between the two panes (the panes themselves cover the border), brighter on hover / drag
//   * the width is clamped: at least min_w (default 180 px) and at most max_frac (default 70 %) of the panel it sits in
//   * the width is remembered PER PAGE: gui_settings.json gui.splits["<page key>/<child id>"], unscaled px (gui_prefs.h SplitStore).
//     It is written once, the frame after the mouse is released (5.0c E6; was: after 0.4 s of rest), never per frame. The page key is set by App::draw_pages
//     (split::set_page); the child's own ID is the literal given here and the ID stack is untouched, so tables inside the pane keep
//     their saved column layout.
// The width is applied again (ImGuiCond_Always for one frame) whenever the page or the UI size changed; between those frames ImGui's
// own manual resize owns the size. G-ia's ##lscards and G-pickers' ##vpgrid call this after merge.
#pragma once
#include <cfloat>
#include <cstdio>
#include <map>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"

#include "gui_prefs.h"

namespace turbo {
extern float g_ui_scale;  // ui/app.h S()
}

namespace turbo::split {

inline SplitStore*& bound_store() {
    static SplitStore* s = nullptr;
    return s;
}
// The store the App keeps in GuiPrefs (App binds it every frame); a private one when nothing is bound (a test that draws a pane alone)
inline SplitStore& store() {
    static SplitStore fallback;
    SplitStore* b = bound_store();
    return b ? *b : fallback;
}
inline void bind(SplitStore* s) { bound_store() = s; }
inline std::string& page_key() {
    static std::string k;
    return k;
}
inline void set_page(const char* key) { page_key() = key ? key : ""; }

constexpr double kRestSeconds = 0.4;  // 2.22 value, kept for the tests that name it; 5.0c E6: the width is committed as soon as the mouse is up

// Run-time state per child ID (not stored)
struct Runtime {
    std::string applied_key;  // page key + id the width was last applied for
    float applied_scale = 0.0f;
    float seen = 0.0f;        // last observed size along the axis, unscaled px
    bool pending = false;
    double changed_at = 0.0;
    ImRect rect;              // last frame's screen rect of the pane (for the grip line)
    bool border_active = false;
    bool have_rect = false;
};
inline std::map<std::string, Runtime>& runtimes() {
    static std::map<std::string, Runtime> m;
    return m;
}
inline void reset_runtime_for_tests() { runtimes().clear(); }

// The scaled size to give the pane on a forced frame: stored (or default) unscaled px, clamped
inline float width_to_apply(const std::string& key, float default_w, float min_w, float panel_px, float max_frac, float scale) {
    const float un = clamp_split_width(store().get(key, default_w), min_w, panel_px / (scale > 0.0f ? scale : 1.0f), max_frac);
    return un * scale;
}

namespace detail {
// Has ImGui already made the child window of this pane (drawn earlier in this ImGui context)? A window that the layout file seeds is
// created at its first Begin, so before that first frame this is false; later it is true. (Same name BeginChildEx builds.)
inline bool window_exists(const char* id) {
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    char name[512];
    std::snprintf(name, sizeof name, "%s/%s_%08X", parent->Name, id, static_cast<unsigned>(parent->GetID(id)));
    return ImGui::FindWindowByName(name) != nullptr;
}
inline bool pane(bool along_x, const char* id, float default_size, float min_size, float max_frac, float cross, ImGuiChildFlags extra) {
    const float sc = g_ui_scale > 0.0f ? g_ui_scale : 1.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float panel = along_x ? avail.x : avail.y;
    const std::string key = page_key() + "/" + id;
    Runtime& rt = runtimes()[id];

    // the grip line in the gap after the pane (last frame's rectangle: the panes cover their own borders)
    if (rt.have_rect) {
        const ImGuiStyle& st = ImGui::GetStyle();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float th = 2.0f * sc;
        const ImU32 col = ImGui::GetColorU32(rt.border_active ? ImGuiCol_SeparatorActive : ImGuiCol_Separator, rt.border_active ? 1.0f : 0.75f);
        if (along_x) {
            const float x = rt.rect.Max.x + st.ItemSpacing.x * 0.5f;
            dl->AddRectFilled(ImVec2(x - th * 0.5f, rt.rect.Min.y), ImVec2(x + th * 0.5f, rt.rect.Max.y), col, th * 0.5f);
        } else {
            const float y = rt.rect.Max.y + st.ItemSpacing.y * 0.5f;
            dl->AddRectFilled(ImVec2(rt.rect.Min.x, y - th * 0.5f), ImVec2(rt.rect.Max.x, y + th * 0.5f), col, th * 0.5f);
        }
    }

    // 5.0c E6: the stored width is forced (ImGuiCond_Always) only when there IS one, or when the UI size changed. With no entry in
    // gui.splits the pane keeps what ImGui's layout file holds for it (else the default as the first-use size): forcing the default there
    // stamped it over the width the person had dragged and the layout file had saved (gui_settings.json starts with no splits).
    // It IS forced when the page changed under the same child id (the pane keeps one ImGui window across pages: each page starts at its
    // own default), when the UI size changed, and when ImGui already holds a window for the pane that the layout file did not make.
    const bool changed = rt.applied_key != key || rt.applied_scale != sc;
    const bool scale_moved = rt.applied_scale != 0.0f && rt.applied_scale != sc;
    const bool first = rt.applied_key.empty();
    const bool key_moved = !first && rt.applied_key != key;
    const bool force = changed && (store().has(key) || scale_moved || key_moved || (first && window_exists(id)));
    const float want = width_to_apply(key, default_size, min_size, panel, max_frac, sc);
    if (force) ImGui::SetNextWindowSize(along_x ? ImVec2(want, 0.0f) : ImVec2(0.0f, want), ImGuiCond_Always);
    const float hi = (panel * max_frac) > min_size * sc ? panel * max_frac : min_size * sc;
    if (along_x) ImGui::SetNextWindowSizeConstraints(ImVec2(min_size * sc, 0.0f), ImVec2(hi, FLT_MAX));
    else ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, min_size * sc), ImVec2(FLT_MAX, hi));
    const bool open = along_x ? ImGui::BeginChild(id, ImVec2(want, cross), extra | ImGuiChildFlags_ResizeX)
                              : ImGui::BeginChild(id, ImVec2(cross, want), extra | ImGuiChildFlags_ResizeY);

    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float cur = (along_x ? w->Size.x : w->Size.y) / sc;
    rt.rect = ImRect(w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y));
    rt.border_active = w->ResizeBorderHeld != -1 || w->ResizeBorderHovered != -1;
    rt.have_rect = true;
    const double now = ImGui::GetTime();
    if (changed) {
        rt.applied_key = key;
        rt.applied_scale = sc;
        rt.seen = cur;
        rt.pending = false;
    } else if (cur != rt.seen) {
        rt.seen = cur;
        rt.pending = true;
        rt.changed_at = now;
    } else if (rt.pending && !ImGui::IsMouseDown(0)) {
        // 5.0c E6: committed the frame after the mouse is released, not 0.4 s later: hiding the panel or closing the game inside that
        // window lost the drag (the pane is not drawn while the panel is hidden, so nothing could commit it afterwards)
        rt.pending = false;
        store().set(key, cur);
    }
    return open;
}
}  // namespace detail

// A pane whose RIGHT border is a divider. default_w / min_w in unscaled px; height as BeginChild's size.y (0 = rest, -n = leave n px).
inline bool pane_x(const char* id, float default_w, float min_w = kSplitMinW, float max_frac = kSplitMaxFrac, float height = 0.0f,
                   ImGuiChildFlags extra = ImGuiChildFlags_Borders) {
    return detail::pane(true, id, default_w, min_w, max_frac, height, extra);
}
// A pane whose BOTTOM border is a divider (two panes stacked). default_h / min_h in unscaled px; width as BeginChild's size.x.
inline bool pane_y(const char* id, float default_h, float min_h = 80.0f, float max_frac = kSplitMaxFrac, float width = 0.0f,
                   ImGuiChildFlags extra = ImGuiChildFlags_Borders) {
    return detail::pane(false, id, default_h, min_h, max_frac, width, extra);
}

}  // namespace turbo::split
