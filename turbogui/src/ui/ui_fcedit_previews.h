// 5.0c E10-fcedit-previews (brief turbo_dev/PITWALL/briefs/5.0c/E10-12-hairbeard.md, "Delivery round (07:50-08:35, E10-fcedit-previews)").
// The hair and facial-hair pickers (ui_visual_picker.cpp build_source) use a third picture source: the lit renders of Cassio's FC Editor
// install, converted to PNG by turbo_dev\tools\fcedit_previews.ps1 into <turbo_output>\cache\previews\fcedit27\<field>\<code>.png
// (field = hairtypecode | facialhairtypecode; players and managers). The cache is local only: never packed into a Turbo zip.
// A code gets this picture only when the game's own preview, the RDBM26 picture and a colour swatch give none. The tile stays
// TileSource::Rdbm (same PNG decode path; ui_visual_picker.h is not touched by this track); these accessors tell the two apart.
// Declared here, not in ui_visual_picker.h, because another track owns that header in this round (the integrator may merge them).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace turbo {

// The note a tile with such a picture shows on hover.
inline constexpr const char* kFcEditPreviewNote = "Lit preview converted from your FC Editor install (local only, not shipped)";
// Tiles of the OPEN picker whose picture is an FC Editor preview (they are counted in VisualPickerUi::source_rdbm too).
size_t visual_picker_source_fcedit();
// The note of one tile of the open picker ("" for a tile whose picture is not an FC Editor preview, or a code not in the list).
std::string visual_picker_tile_note(int64_t code);

}  // namespace turbo
