// FC 27 LE Turbo GUI - the small display preferences of gui_settings.json "gui" (2026-10-11):
//   gui.preload_images  true (default): pictures (faces, 3D head renders at hand, tattoos, galleries, crests) load in the
//                       background from the moment the career connects (ui/preload.h); false: only what is on screen
//   gui.row_density     "compact" | "normal" (default) | "comfortable": the height of table rows and list rows
//                       (App::update_style multiplies CellPadding.y / ItemSpacing.y / FramePadding.y)
//   gui.advanced_mode   false (default): "Simple" view, only the screens with a proven in-game effect; true: "Advanced",
//                       everything (A6 entry point of the GUI simplification; see turbo_dev/research_20261016/GUI_AUDIT.md)
// Pure (json in / out), so the native tests can check the defaults and the round trip. The hotkey and the UI size keep
// their own code in App::load_gui_settings / save_gui_settings.
#pragma once
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
    return "Simple view: only features proven to work in the game. Tactics, Gameplay tuning, Match setup, Game settings and the raw Database are under Advanced.";
}
inline const char* unproven_banner_text() {
    return "Advanced: no proven in-game effect yet. Values can be written and read back, but nobody has shown the match plays differently.";
}

struct GuiPrefs {
    bool advanced_mode = false;
    bool preload_images = true;
    RowDensity row_density = RowDensity::Normal;

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
        return p;
    }
    // Into gui_settings.json["gui"] (other keys are kept)
    void save(nlohmann::json& gui_settings) const {
        if (!gui_settings.is_object()) gui_settings = nlohmann::json::object();
        nlohmann::json& g = gui_settings["gui"];
        if (!g.is_object()) g = nlohmann::json::object();
        g["advanced_mode"] = advanced_mode;
        g["preload_images"] = preload_images;
        g["row_density"] = row_density_name(row_density);
    }
};

}  // namespace turbo
