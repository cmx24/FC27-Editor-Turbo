// FC 27 LE Turbo GUI - shared pieces of every asset picker (docs/asset-catalog.md): the "Hidden" badge, the
// All / In this save / Hidden filter combo, and the paged browser for big picture families (game minifaces, youth faces,
// staff faces, legend pictures, other clubs' crests).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/asset_catalog.h"
#include "core/asset_pages.h"

namespace turbo {

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

// ---- the catalog the pickers read
// Published by the app (WP-06 wiring) whenever a catalog is swapped in or DbUse is rebuilt; read on the UI thread only.
// A null catalog, or one that is not loaded, means the pickers show no catalog sources (2.0 behaviour).
struct AssetView {
    std::shared_ptr<const assets::Catalog> catalog;
    std::shared_ptr<const assets::DbUse> use;  // null: nothing is badged hidden
    bool loaded() const { return catalog && catalog->loaded(); }
};
void set_asset_view(AssetView v);
const AssetView& asset_view();

// ---- paged browser: search (name or id), the All / In this save / Hidden filter, pages of kAssetBrowserPage tiles.
// Items, filter and paging are in core/asset_pages.h (pure, tested by tests/catalog/run_pages.sh).

// Draws one tile's picture in a side x side box (the caller asks the game for it: only tiles on screen are drawn)
using AssetTileDraw = std::function<void(const AssetBrowserItem& item, float side)>;

// Draws the browser in a child region of `height` (0: the space left; < 0: the space left minus that much). Returns
// true when an item was clicked (its id in `picked`). `selected` gets a frame. `team` words the Hidden tooltip for clubs.
bool asset_browser(const char* id, AssetBrowserState& st, const std::vector<AssetBrowserItem>& items, int64_t& picked,
                   const AssetTileDraw& draw_picture, int64_t selected = 0, float height = 0.0f, bool team = false);

// WP-01 form: `want(path)` is called for the pictures on screen only, and the tiles show their name in an empty box
bool asset_browser(const char* id, AssetBrowserState& st, const std::vector<AssetBrowserItem>& items, int64_t& picked,
                   const std::function<void(const std::string& path)>& want);

}  // namespace turbo
