// FC 27 LE Turbo GUI - the small display preferences of gui_settings.json "gui" (2026-10-11):
//   gui.preload_images  true (default): pictures (faces, 3D head renders at hand, tattoos, galleries, crests) load in the
//                       background from the moment the career connects (ui/preload.h); false: only what is on screen
//   gui.row_height      (2.22) number, unscaled px of one table row at UI size 1 (kRowHeightMin..kRowHeightMax, default kRowHeightNormal):
//                       the "Row height" slider of Settings > Look and feel; App::update_style turns it into CellPadding.y / ItemSpacing.y /
//                       FramePadding.y. gui.row_density ("compact" | "normal" | "comfortable") is still written (the nearest step, for an older
//                       build) and read when gui.row_height is missing
//   gui.splits          (2.22) {"<page>/<pane id>": width}: the width, in unscaled px, the person dragged a list pane to (ui_split.h); the
//                       divider moves, the width is kept per page and across restarts (written once, when the drag has rested)
//   gui.advanced_mode   false (default): "Simple" view, only the screens with a proven in-game effect; true: "Advanced",
//                       everything (A6 entry point of the GUI simplification; see turbo_dev/research_20261016/GUI_AUDIT.md)
//   gui.window          {x, y, w, h, maximized}: the main panel's place, size and maximized state (2.20; see WindowPrefs below)
//   gui.sections_open   {"<page>.<section>": true|false}: which collapsing sections of the revamped pages are open (spec 2.1);
//                       a section not listed uses the page's own default
// Pure (json in / out), so the native tests can check the defaults and the round trip. The hotkey and the UI size keep
// their own code in App::load_gui_settings / save_gui_settings.
#pragma once
#include <cmath>
#include <map>
#include <string>

#include "nlohmann/json.hpp"

namespace turbo {

enum class RowDensity : int { Compact = 0, Normal = 1, Comfortable = 2 };

inline const char* row_density_name(RowDensity d) {
    switch (d) {
        case RowDensity::Compact: return "compact";
        case RowDensity::Comfortable: return "comfortable";
        default: return "normal";
    }
}
inline const char* row_density_label(RowDensity d) {
    switch (d) {
        case RowDensity::Compact: return "Compact";
        case RowDensity::Comfortable: return "Comfortable";
        default: return "Normal";
    }
}
inline RowDensity row_density_from_name(const std::string& s, RowDensity fallback = RowDensity::Normal) {
    if (s == "compact") return RowDensity::Compact;
    if (s == "comfortable") return RowDensity::Comfortable;
    if (s == "normal") return RowDensity::Normal;
    return fallback;
}
// ---- Row height (2.22 G-resize; Cassio 10-19: "STILL cant resize ... rows") -------------------------------------------------
// Dear ImGui cannot drag a row's edge, so the height of every table / list row is a slider. The value is the height of one table row in
// unscaled px at UI size 1: text height (the 17 px font) + 2 * CellPadding.y. Normal = Dear ImGui's own style (CellPadding.y 2).
constexpr float kRowHeightMin = 19.0f;     // compact: CellPadding.y 1
constexpr float kRowHeightNormal = 21.0f;  // CellPadding.y 2: the style as it was before the setting existed
constexpr float kRowHeightMax = 41.0f;     // roomy: CellPadding.y 12
constexpr float kRowFontPx = 17.0f;        // the overlay's font size (win/overlay_dx12.cpp); text height of a row
inline float clamp_row_height(float v) {
    if (!std::isfinite(v)) return kRowHeightNormal;
    return v < kRowHeightMin ? kRowHeightMin : v > kRowHeightMax ? kRowHeightMax : v;
}
// CellPadding.y in unscaled px for a row height, given the font's px height
inline float row_height_cell_padding(float row_height, float font_px = kRowFontPx) {
    const float p = (clamp_row_height(row_height) - font_px) * 0.5f;
    return p < 0.5f ? 0.5f : p;
}
// Multipliers on the style's other vertical spacings (list rows = text + ItemSpacing.y, widget rows = text + 2 * FramePadding.y): 1 at
// normal, 0.8 / 0.85 at compact, 1.4 / 1.3 at CellPadding.y 4, capped so a roomy table does not blow up every button
inline float row_height_item_spacing_mult(float row_height, float font_px = kRowFontPx) {
    const float m = row_height_cell_padding(row_height, font_px) / 2.0f;
    const float v = 1.0f + (m - 1.0f) * 0.4f;
    return v > 1.6f ? 1.6f : v;
}
inline float row_height_frame_padding_mult(float row_height, float font_px = kRowFontPx) {
    const float m = row_height_cell_padding(row_height, font_px) / 2.0f;
    const float v = 1.0f + (m - 1.0f) * 0.3f;
    return v > 1.5f ? 1.5f : v;
}
inline float row_height_of_density(RowDensity d) { return d == RowDensity::Compact ? kRowHeightMin : d == RowDensity::Comfortable ? 25.0f : kRowHeightNormal; }
inline RowDensity density_of_row_height(float h) { return h <= 20.0f ? RowDensity::Compact : h >= 24.0f ? RowDensity::Comfortable : RowDensity::Normal; }

// Multipliers on Dear ImGui's (scaled) sizes: table rows are text height + 2 * CellPadding.y, list rows (Selectable)
// text height + ItemSpacing.y, widget rows FrameHeight (= text + 2 * FramePadding.y)
inline float row_density_cell_padding(RowDensity d) { return d == RowDensity::Compact ? 0.5f : d == RowDensity::Comfortable ? 2.0f : 1.0f; }
inline float row_density_item_spacing(RowDensity d) { return d == RowDensity::Compact ? 0.6f : d == RowDensity::Comfortable ? 1.4f : 1.0f; }
inline float row_density_frame_padding(RowDensity d) { return d == RowDensity::Compact ? 0.75f : d == RowDensity::Comfortable ? 1.3f : 1.0f; }

// Simple / Advanced view. The top-level tab indices are fixed (App::request_tab): the view only hides tabs, never renumbers.
// Hidden in Simple: Database (raw tables), Game settings (sliders are stored and shown on the EA screen, effect on play NOT proven).
enum TopTab : int { TabPlayers = 0, TabTeams, TabManagers, TabCompetitions, TabDatabase, TabTools, TabStatus, TabGameSettings, TabCount };
enum class SubTab : int { TeamsTactics, CompetitionsMatchSetup, CompetitionsGameplayTuning };

inline bool top_tab_visible(int tab, bool advanced) {
    if (advanced) return true;
    return tab != TabDatabase && tab != TabGameSettings;
}
// Panels inside a visible tab with no proven in-game effect (tactics, gameplay tuning, match setup switches): Advanced only
inline bool sub_tab_visible(SubTab, bool advanced) { return advanced; }
// Tabs the Advanced view marks with a banner (no proven in-game effect yet); sub tabs draw the banner themselves
inline bool top_tab_unproven(int tab) { return tab == TabGameSettings; }
inline const char* simple_banner_text() {
    return "Simple view: only features proven to work in the game. Experimental tactics, Gameplay tuning, Match setup, raw rows and bulk edit are under Advanced (Settings > Advanced view).";
}
inline const char* unproven_banner_text() {
    return "Advanced: no proven in-game effect yet. Values can be written and read back, but nobody has shown the match plays differently.";
}

// ---- The main window (2.20 S3-window; Cassio 10-19: "why doesn't it just start maximized") ---------------------------------
// gui_settings.json  gui.window = {x, y, w, h, maximized}, pixels of the game viewport:
//   missing block / maximized true / a rectangle that is off-screen or smaller than 800x600  ->  the window covers the whole
//   viewport ("maximized"); maximized false with a usable rectangle  ->  that rectangle. x,y,w,h are the last NON-maximized
//   rectangle (what Restore returns to). THE SETTING WINS over Dear ImGui's turbo_gui_layout_v2.ini for the main panel: the
//   panel is created with NoSavedSettings and App::draw_main sets its place and size with ImGuiCond_Always on the first frame.
// Pure (no ImGui), so the native tests check the round trip and the fall-backs.
struct WindowRect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool operator==(const WindowRect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const WindowRect& o) const { return !(*this == o); }
};
constexpr float kWindowMinW = 800.0f, kWindowMinH = 600.0f;      // below this a stored rectangle is not restored at start
constexpr float kWindowSoftMinW = 480.0f, kWindowSoftMinH = 320.0f;  // what the panel itself allows (SetNextWindowSizeConstraints): Restore accepts it

// A rectangle worth restoring on a viewport dw x dh: finite, at least 800x600, not bigger than the viewport (a layout saved on a
// bigger screen), and its title strip reachable (top edge inside the viewport, at least 200 px of width and 100 px of height visible)
inline bool window_rect_usable(const WindowRect& r, float dw, float dh, float min_w = kWindowMinW, float min_h = kWindowMinH) {
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) || !std::isfinite(r.h)) return false;
    if (!(dw > 0.0f) || !(dh > 0.0f)) return false;
    if (r.w < min_w || r.h < min_h) return false;
    if (r.w > dw + 2.0f || r.h > dh + 2.0f) return false;
    if (r.y < 0.0f || r.y > dh - 100.0f) return false;
    if (r.x + r.w < 200.0f || r.x > dw - 200.0f) return false;
    return true;
}

struct WindowPrefs {
    bool maximized = true;   // default: the whole viewport
    bool has_rect = false;   // x,y,w,h below were stored (or observed)
    WindowRect rect;         // the last non-maximized rectangle

    static WindowPrefs load(const nlohmann::json& gui_settings) {
        WindowPrefs p;
        if (!gui_settings.is_object()) return p;
        auto g = gui_settings.find("gui");
        if (g == gui_settings.end() || !g->is_object()) return p;
        auto w = g->find("window");
        if (w == g->end() || !w->is_object()) return p;
        auto num = [&](const char* k, float& out) {
            auto it = w->find(k);
            if (it == w->end() || !it->is_number()) return false;
            const double v = it->get<double>();
            if (!std::isfinite(v)) return false;
            out = static_cast<float>(v);
            return true;
        };
        WindowRect r;
        const bool all = num("x", r.x) & num("y", r.y) & num("w", r.w) & num("h", r.h);  // (all four are read)
        if (all) { p.rect = r; p.has_rect = true; }
        auto m = w->find("maximized");
        if (m != w->end() && m->is_boolean()) p.maximized = m->get<bool>();
        else if (!all) p.maximized = true;  // a block without a rectangle or a flag is no choice at all
        return p;
    }
    // Into gui_settings.json["gui"]["window"] (other keys are kept; no rectangle known yet: only the flag)
    void save(nlohmann::json& gui_settings) const {
        if (!gui_settings.is_object()) gui_settings = nlohmann::json::object();
        nlohmann::json& g = gui_settings["gui"];
        if (!g.is_object()) g = nlohmann::json::object();
        nlohmann::json w = nlohmann::json::object();
        if (has_rect) {
            w["x"] = std::round(rect.x);
            w["y"] = std::round(rect.y);
            w["w"] = std::round(rect.w);
            w["h"] = std::round(rect.h);
        }
        w["maximized"] = maximized;
        g["window"] = w;
    }
};

struct WindowPlan {
    bool maximized = true;
    WindowRect rect;
};
// What to put on screen. `restoring` = the Restore button: the stored rectangle if usable, else `dflt`, and never maximized.
// (Restore accepts any rectangle the panel allows, 480x320 and up.) Otherwise (start): maximized unless the stored choice is a usable rectangle.
inline WindowPlan window_plan(const WindowPrefs& p, float dw, float dh, bool restoring, const WindowRect& dflt) {
    WindowPlan out;
    const WindowRect full{0.0f, 0.0f, dw, dh};
    if (restoring) {
        out.maximized = false;
        out.rect = p.has_rect && window_rect_usable(p.rect, dw, dh, kWindowSoftMinW, kWindowSoftMinH) ? p.rect : dflt;
        return out;
    }
    if (p.maximized || !p.has_rect || !window_rect_usable(p.rect, dw, dh)) {
        out.maximized = true;
        out.rect = full;
    } else {
        out.maximized = false;
        out.rect = p.rect;
    }
    return out;
}
// The window already covers the viewport (within a pixel or two)
inline bool window_is_full(const WindowRect& r, float dw, float dh) {
    return std::fabs(r.x) <= 1.5f && std::fabs(r.y) <= 1.5f && std::fabs(r.w - dw) <= 2.0f && std::fabs(r.h - dh) <= 2.0f;
}

// Run-time state of the main window (not stored): what was applied, what was last seen, the debounce of the save.
struct WindowTracker {
    bool apply_pending = true;      // set the place and size with ImGuiCond_Always on the next frame (start, Maximize / Restore, viewport change)
    bool restoring = false;         // the pending apply is a Restore
    bool save_on_apply = false;     // write gui_settings.json once the pending apply has settled the state (Maximize / Restore)
    float disp_w = 0.0f, disp_h = 0.0f;  // viewport the window was last fitted to
    bool have_seen = false;
    WindowRect seen;                // last observed rectangle (not maximized)
    bool pending_save = false;
    double changed_at = 0.0;
    static constexpr double kDebounce = 0.6;  // seconds the rectangle must rest (and the mouse button be up) before the file is written

    // Does the window need to be placed this frame?
    bool need_apply(bool maximized, float dw, float dh) const {
        return apply_pending || (maximized && (dw != disp_w || dh != disp_h));
    }
    void applied(float dw, float dh) {
        apply_pending = false;
        restoring = false;
        disp_w = dw;
        disp_h = dh;
        have_seen = false;  // the next observation is the baseline, not a change
    }
    // Called every frame the non-maximized window is drawn; true = write gui_settings.json now. Never per frame: the rectangle must
    // change, then rest for kDebounce seconds with the mouse up (a drag or a resize in progress changes it every frame).
    bool observe(const WindowRect& r, double now, bool mouse_down) {
        if (!have_seen) { seen = r; have_seen = true; return false; }
        if (r != seen) { seen = r; pending_save = true; changed_at = now; return false; }
        if (pending_save && !mouse_down && now - changed_at >= kDebounce) { pending_save = false; return true; }
        return false;
    }
};

// ---- Split panes (2.22 G-resize, ui/ui_split.h) ----------------------------------------------------------------------------
// gui.splits = {"<page key>/<child id>": width}, unscaled px. Pure (no ImGui): the native tests check the round trip and the clamp.
constexpr float kSplitMinW = 180.0f;     // a list pane never gets narrower than this (unscaled px)
constexpr float kSplitMaxFrac = 0.70f;   // ... nor wider than this share of the panel it sits in
inline float clamp_split_width(float w, float min_w, float panel_w, float max_frac) {
    float hi = panel_w * max_frac;
    if (hi < min_w) hi = min_w;  // a tiny panel: the minimum wins
    if (!std::isfinite(w)) return min_w;
    return w < min_w ? min_w : w > hi ? hi : w;
}
struct SplitStore {
    std::map<std::string, float> widths;  // unscaled px
    bool dirty = false;                   // changed since the last save: the app writes gui_settings.json (once, after the drag rested)

    bool has(const std::string& key) const { return widths.find(key) != widths.end(); }
    float get(const std::string& key, float dflt) const {
        auto it = widths.find(key);
        return it == widths.end() ? dflt : it->second;
    }
    void set(const std::string& key, float w) {
        if (!std::isfinite(w) || w < 1.0f) return;
        const float r = std::round(w);
        auto it = widths.find(key);
        if (it != widths.end() && it->second == r) return;
        widths[key] = r;
        dirty = true;
    }
    bool take_dirty() {
        const bool d = dirty;
        dirty = false;
        return d;
    }
    void load(const nlohmann::json& gui) {
        widths.clear();
        auto so = gui.find("splits");
        if (so == gui.end() || !so->is_object()) return;
        for (auto it = so->begin(); it != so->end(); ++it)
            if (it.value().is_number() && std::isfinite(it.value().get<double>()) && it.value().get<double>() >= 1.0)
                widths[it.key()] = static_cast<float>(it.value().get<double>());
    }
    void save(nlohmann::json& gui) const {
        nlohmann::json o = nlohmann::json::object();
        for (const auto& kv : widths) o[kv.first] = kv.second;
        gui["splits"] = o;
    }
};

struct GuiPrefs {
    WindowPrefs window;        // gui.window (the main panel's place, size and maximized state)
    WindowTracker window_rt;   // run time only
    bool advanced_mode = false;
    bool preload_images = true;
    RowDensity row_density = RowDensity::Normal;  // the nearest step of row_height (kept for an older build's file)
    float row_height = kRowHeightNormal;         // 2.22: the Row height slider (unscaled px of a table row)
    SplitStore splits;                            // 2.22: the widths of the list panes the person dragged
    std::map<std::string, bool> sections_open;  // "<page>.<section>" -> open (pages ask section_open(id, default))

    bool section_open(const std::string& id, bool default_open) const {
        auto it = sections_open.find(id);
        return it == sections_open.end() ? default_open : it->second;
    }
    // true when the stored state changed (the caller then saves gui_settings.json)
    bool set_section_open(const std::string& id, bool open) {
        auto it = sections_open.find(id);
        if (it != sections_open.end() && it->second == open) return false;
        sections_open[id] = open;
        return true;
    }

    // From the whole gui_settings.json object (anything missing or malformed keeps the default)
    static GuiPrefs load(const nlohmann::json& gui_settings) {
        GuiPrefs p;
        if (!gui_settings.is_object()) return p;
        auto g = gui_settings.find("gui");
        if (g == gui_settings.end() || !g->is_object()) return p;
        auto am = g->find("advanced_mode");
        if (am != g->end() && am->is_boolean()) p.advanced_mode = am->get<bool>();
        auto pi = g->find("preload_images");
        if (pi != g->end() && pi->is_boolean()) p.preload_images = pi->get<bool>();
        auto rd = g->find("row_density");
        if (rd != g->end() && rd->is_string()) p.row_density = row_density_from_name(rd->get<std::string>());
        p.row_height = row_height_of_density(p.row_density);  // an older file: the step it names
        auto rh = g->find("row_height");
        if (rh != g->end() && rh->is_number() && std::isfinite(rh->get<double>())) p.row_height = clamp_row_height(static_cast<float>(rh->get<double>()));
        p.row_density = density_of_row_height(p.row_height);
        p.splits.load(*g);
        p.splits.dirty = false;
        p.window = WindowPrefs::load(gui_settings);
        auto so = g->find("sections_open");
        if (so != g->end() && so->is_object())
            for (auto it = so->begin(); it != so->end(); ++it)
                if (it.value().is_boolean()) p.sections_open[it.key()] = it.value().get<bool>();
        return p;
    }
    // Into gui_settings.json["gui"] (other keys are kept)
    void save(nlohmann::json& gui_settings) const {
        if (!gui_settings.is_object()) gui_settings = nlohmann::json::object();
        nlohmann::json& g = gui_settings["gui"];
        if (!g.is_object()) g = nlohmann::json::object();
        g["advanced_mode"] = advanced_mode;
        g["preload_images"] = preload_images;
        g["row_density"] = row_density_name(density_of_row_height(row_height));
        g["row_height"] = std::round(clamp_row_height(row_height));
        splits.save(g);
        nlohmann::json so = nlohmann::json::object();
        for (const auto& kv : sections_open) so[kv.first] = kv.second;
        g["sections_open"] = so;
        window.save(gui_settings);
    }
};

}  // namespace turbo
