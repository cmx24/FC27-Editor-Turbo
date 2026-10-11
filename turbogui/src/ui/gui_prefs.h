// FC 27 LE Turbo GUI - the small display preferences of gui_settings.json "gui" (2026-10-11):
//   gui.preload_images  true (default): pictures (faces, 3D head renders at hand, tattoos, galleries, crests) load in the
//                       background from the moment the career connects (ui/preload.h); false: only what is on screen
//   gui.row_density     "compact" | "normal" (default) | "comfortable": the height of table rows and list rows
//                       (App::update_style multiplies CellPadding.y / ItemSpacing.y / FramePadding.y)
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

struct GuiPrefs {
    bool preload_images = true;
    RowDensity row_density = RowDensity::Normal;

    // From the whole gui_settings.json object (anything missing or malformed keeps the default)
    static GuiPrefs load(const nlohmann::json& gui_settings) {
        GuiPrefs p;
        if (!gui_settings.is_object()) return p;
        auto g = gui_settings.find("gui");
        if (g == gui_settings.end() || !g->is_object()) return p;
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
        g["preload_images"] = preload_images;
        g["row_density"] = row_density_name(row_density);
    }
};

}  // namespace turbo
