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
//     R2-R(b): every drag also writes gui.splits["last/<child id>"], and a page with no width of its own opens at that one (the last width
//     the person dragged this pane to, on any page) instead of the default: the shell's "##select" is drawn on ~30 pages, and the base
//     reset it to the default on each page not yet dragged. A page the person dragged keeps its own width.
//     It is written once, the frame after the mouse is released (5.0c E6; was: after 0.4 s of rest), never per frame, and only after the
//     person dragged the divider (R2-R(a): a width ImGui's constraints clamped, e.g. the main window made narrower, is never stored).
//     The page key is set by App::draw_pages
//     (split::set_page); the child's own ID is the literal given here and the ID stack is untouched, so tables inside the pane keep
//     their saved column layout.
// The width is applied again (ImGuiCond_Always for one frame) whenever the page, the UI size or the panel's size changed (R2-R(a): a pane the
// narrower window clamped grows back to its stored width when the window does); between those frames ImGui's
// own manual resize owns the size. G-ia's ##lscards and G-pickers' ##vpgrid call this after merge.
#pragma once
#include <cfloat>
#include <cmath>
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
    ImGuiID applied_child = 0;  // R2-R(b): the ImGui window (child ID) the width was applied to: sub-tabs of one page share the key, not the window
    ImRect rect;              // last frame's screen rect of the pane (for the grip line)
    float applied_panel = -1.0f;  // R2-R(a): the panel size (along the axis, scaled px) the width was last applied or seen for
    bool dragging = false;    // R2-R(a): the person holds this pane's border (since the press); only such a change is stored
    bool border_active = false;
    bool have_rect = false;
};
inline std::map<std::string, Runtime>& runtimes() {
    static std::map<std::string, Runtime> m;
    return m;
}
inline void reset_runtime_for_tests() { runtimes().clear(); }
// R2-R(b): the key of the width this pane id was last dragged to on any page (the fallback of a page with no width of its own)
inline std::string last_key(const char* id) { return std::string("last/") + id; }

// The scaled size to give the pane on a forced frame: stored (or default) unscaled px, clamped
inline float width_to_apply(const std::string& key, float default_w, float min_w, float panel_px, float max_frac, float scale) {
    const float un = clamp_split_width(store().get(key, default_w), min_w, panel_px / (scale > 0.0f ? scale : 1.0f), max_frac);
    return un * scale;
}

// Turbo X 2.0 layout (Cassio 11-01: show / hide the frames): the list and preview panes on the LEFT of a divider can be folded away (the last
// pane of a row fills the rest and is never folded). Right-click on the divider: "Hide this panel"; the folded pane is a slim strip with a
// ">" button that brings it back; Customize layout (Ctrl+L) lists them too. One choice per pane id, for every page (SplitStore::folded).
inline bool foldable(const char* id) {
    const std::string s = id ? id : "";
    return s == "##select" || s == "##lslist" || s == "##lspreview";
}
inline const char* fold_label(const std::string& id) {  // the words the person reads (Customize layout, the tooltips)
    if (id == "##select") return "List beside the editor (players, staff, clubs)";
    if (id == "##lslist") return "Looks: people list";
    if (id == "##lspreview") return "Looks: preview";
    return "panel";
}
constexpr float kFoldedStripW = 26.0f;  // unscaled px

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
    const std::string lkey = last_key(id);
    Runtime& rt = runtimes()[id];

    // Turbo X 2.0: a folded pane is a slim strip with a ">" that shows it again; the caller's content is skipped (pane returns false) and the
    // caller still ends it with EndChild. The strip has its own child id, so the pane's own window (and its saved width) is untouched.
    if (foldable(id) && store().is_folded(id)) {
        rt.have_rect = false;
        const std::string sid = std::string(id) + "_folded";
        ImGui::BeginChild(sid.c_str(), along_x ? ImVec2(kFoldedStripW * sc, cross) : ImVec2(cross, kFoldedStripW * sc),
                          ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const std::string bl = std::string(along_x ? ">" : "v") + "###unfold" + id;
        if (ImGui::Button(bl.c_str(), ImVec2(-FLT_MIN, 0.0f))) store().set_folded(id, false);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show the %s", fold_label(id));
        return false;
    }

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

    // Turbo X 2.0: right-click on the divider (the pane's resize border, last frame) folds the pane away
    if (foldable(id)) {
        const std::string pop = std::string("##foldmenu") + id;
        if (rt.have_rect && rt.border_active) {
            ImGui::SetTooltip("Drag to resize. Right-click to hide this panel.");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup(pop.c_str());
        }
        if (ImGui::BeginPopup(pop.c_str())) {
            const std::string hl = std::string("Hide this panel###fold") + id;
            if (ImGui::MenuItem(hl.c_str())) store().set_folded(id, true);
            ImGui::TextDisabled("The > strip brings it back (or Ctrl+L, Customize layout)");
            ImGui::EndPopup();
        }
    }

    // 5.0c E6: the stored width is forced (ImGuiCond_Always) only when there IS one, or when the UI size changed. With no entry in
    // gui.splits the pane keeps what ImGui's layout file holds for it (else the default as the first-use size): forcing the default there
    // stamped it over the width the person had dragged and the layout file had saved (gui_settings.json starts with no splits).
    // It IS forced when the page changed under the same child id (the pane keeps one ImGui window across pages: each page starts at its
    // own default), when the UI size changed, and when ImGui already holds a window for the pane that the layout file did not make.
    // R2-R(b): the width to apply is the page's own, else the last dragged one of this id ("last/<id>"), else the default
    const bool own = store().has(key);
    const bool stored = own || store().has(lkey);
    const std::string& src = own ? key : lkey;
    // R2-R(b): two sub-tabs drawing this pane under one page key are two ImGui windows: showing the other one is a change too (the base read
    // its old size as a new width and stored it over the page's)
    const ImGuiID child = ImGui::GetCurrentWindow()->GetID(id);
    const bool changed = rt.applied_key != key || rt.applied_scale != sc || rt.applied_child != child;
    const bool scale_moved = rt.applied_scale != 0.0f && rt.applied_scale != sc;
    const bool first = rt.applied_key.empty();
    const bool key_moved = !first && rt.applied_key != key;
    // R2-R(a): the panel changed size (the main window made narrower or wider, placed, restored): the stored width is applied again, clamped
    // to the new panel, so a pane that a narrower window clamped gets its stored width back (never while the person drags this divider)
    const bool panel_moved = !first && rt.applied_panel >= 0.0f && std::fabs(panel - rt.applied_panel) > 0.5f && !rt.dragging;
    const bool force = (changed && (stored || scale_moved || key_moved || (first && window_exists(id)))) || (panel_moved && stored);
    const float want = width_to_apply(src, default_size, min_size, panel, max_frac, sc);
    if (force) ImGui::SetNextWindowSize(along_x ? ImVec2(want, 0.0f) : ImVec2(0.0f, want), ImGuiCond_Always);
    const float hi = (panel * max_frac) > min_size * sc ? panel * max_frac : min_size * sc;
    if (along_x) ImGui::SetNextWindowSizeConstraints(ImVec2(min_size * sc, 0.0f), ImVec2(hi, FLT_MAX));
    else ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, min_size * sc), ImVec2(FLT_MAX, hi));
    const bool open = along_x ? ImGui::BeginChild(id, ImVec2(want, cross), extra | ImGuiChildFlags_ResizeX)
                              : ImGui::BeginChild(id, ImVec2(cross, want), extra | ImGuiChildFlags_ResizeY);

    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float cur = (along_x ? w->Size.x : w->Size.y) / sc;
    rt.rect = ImRect(w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y));
    const bool held = w->ResizeBorderHeld != -1;
    rt.border_active = held || w->ResizeBorderHovered != -1;
    rt.have_rect = true;
    rt.applied_panel = panel;
    const double now = ImGui::GetTime();
    if (changed || force) {
        rt.applied_key = key;
        rt.applied_scale = sc;
        rt.applied_child = child;
        rt.seen = cur;
        rt.pending = false;
        rt.dragging = false;
        return open;
    }
    // R2-R(a): only a size the person dragged is stored. ImGui's own clamps (SetNextWindowSizeConstraints: the 70 % of a panel that got
    // narrower) change the size too; the base stored ANY change once the mouse was up, so shrinking the main window overwrote the width.
    if (held) rt.dragging = true;
    if (cur != rt.seen) {
        rt.seen = cur;
        if (rt.dragging) {
            rt.pending = true;
            rt.changed_at = now;
        }
    }
    if (!held && !ImGui::IsMouseDown(0)) {
        // 5.0c E6: committed the frame after the mouse is released, not 0.4 s later: hiding the panel or closing the game inside that
        // window lost the drag (the pane is not drawn while the panel is hidden, so nothing could commit it afterwards)
        if (rt.pending) {
            store().set(key, cur);
            store().set(lkey, cur);  // R2-R(b): the width the pages not yet dragged open at
        }
        rt.pending = false;
        rt.dragging = false;
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
