// FC 27 LE Turbo GUI - the body-type figure (5.0c E8-body): one renderer, drawn in code, for every body-type tile of both pickers
// (the Look card's grid in ui_looks_studio.cpp and the visual picker's tiles in ui_visual_picker.cpp), players AND managers.
// No picture file, no generator run: the figure comes from core/body_shape.h on ONE common height scale, so a tall body reads taller than a
// short one tile by tile. Colours are fixed here so the contrast is testable (tests/native/test_body_picker.h T5): the figure against its
// tile at 3:1 or better (WCAG 1.4.11), the name text at 4.5:1 is the picker's own text colour.
#pragma once
#include "imgui.h"

#include "core/body_shape.h"

namespace turbo::bodyfig {

// WCAG relative luminance contrast of two opaque colours (1 .. 21)
float contrast_ratio(ImU32 a, ImU32 b);

// The tile's ground and the figure's fill in each state: normal, selected (the value the record holds) and hovered
ImU32 tile_ground(bool selected, bool hovered);
ImU32 figure_fill(bool selected);
ImU32 selected_ring();

// Draws the tile ground and the figure into [p0, p0 + size]. Counts nothing: the caller counts the figures it drew.
void draw_body_figure(ImDrawList* dl, ImVec2 p0, ImVec2 size, const bodyshape::BodyShape& shape, bool selected, bool hovered);

}  // namespace turbo::bodyfig
