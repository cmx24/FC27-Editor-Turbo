// FC 27 LE Turbo GUI - shared pieces of every asset picker (docs/asset-catalog.md): the "Hidden" badge, the
// All / In this save / Hidden filter combo, and the paged browser for big picture families (game minifaces, youth faces,
// staff faces, legend pictures, other clubs' crests).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/asset_catalog.h"

namespace turbo {

using AssetScope = assets::Scope;

enum class AssetBadge : uint8_t {
    Hidden,       // in the game files; no player (team) of this save uses it
    ModelOnly,    // "3D only": a 3D model and no preview picture
    PreviewOnly,  // a preview picture and no 3D model: not writable
    NotInGame,    // not in the game files (catalog <key>): expert grids show it, pickers drop it
    NoRecording,  // callnames: no recording in the active language / team
    NotInTable,   // tattoos: not in the DB tattoo table
};
const char* asset_badge_label(AssetBadge b);    // "Hidden", "3D only", "Preview only", "Not in game", "No recording", ...
const char* asset_badge_tooltip(AssetBadge b);  // the hover text ("In the game files; no player of this save uses it" ...)

// Small framed label on the current line (call ImGui::SameLine() first to put it after a name). `tooltip` replaces the
// default hover text (e.g. "... no team of this save uses it" for crests). Returns true when hovered.
bool asset_badge(AssetBadge b, const char* tooltip = nullptr);

// Combo "All" / "In this save" / "Hidden". width <= 0: ImGui's default. Returns true when the pick changed.
bool asset_scope_combo(const char* id, AssetScope& scope, float width = 0.0f);

// ---- paged browser (WP-09 implements it; the stub shows a one-line note)
constexpr int kAssetBrowserPage = 200;  // tiles per page: only what is on screen asks for its picture

struct AssetBrowserItem {
    int64_t id = 0;
    std::string name;         // display name (DB > catalog > fallback)
    std::string name_source;  // assets::Name::source ("" = DB name)
    std::string path;         // picture path (legacy file), "" = placeholder
    bool hidden = false;
};

struct AssetBrowserState {
    AssetScope scope = AssetScope::All;
    int page = 0;
    char search[64] = "";
};

// Draws the browser. `want(path)` is called for the pictures on screen only. Returns true when an item was picked
// (its id in `picked`).
bool asset_browser(const char* id, AssetBrowserState& st, const std::vector<AssetBrowserItem>& items, int64_t& picked,
                   const std::function<void(const std::string& path)>& want);

}  // namespace turbo
