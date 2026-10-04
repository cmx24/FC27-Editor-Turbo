// FC 27 LE Turbo GUI - Ctrl + mouse wheel zoom of the whole Turbo window.
//
// The zoom is the user's UI size factor (App::ui_scale_user, the Turbo Tools "UI size" slider, gui_settings.json
// gui.ui_scale). App::update_style rebuilds the style from Dear ImGui's unscaled sizes for the new scale and sets
// style.FontScaleDpi: this Dear ImGui (1.92, dynamic fonts, the DX12 backend has RendererHasTextures) bakes the glyphs
// for the new size on demand, so nothing rebuilds the whole font atlas and nothing stalls. Pictures follow because every
// fixed size goes through S().
#pragma once

namespace turbo {

class App;

constexpr float kZoomStep = 0.1f;  // one wheel notch

// The UI size factor after `notches` wheel notches (positive = bigger): 0.1 per notch, rounded to 0.05, kept in
// kUiScaleMin .. kUiScaleMax (app.h)
float zoom_after(float user, float notches);

// Once per frame from App::draw (after NewFrame). While Turbo is shown: Ctrl + wheel over a Turbo window zooms in or out,
// Ctrl + 0 goes back to 1.00x. The new size shows in a toast and is written to gui_settings.json once the wheel has
// rested for a second (also while hidden). True when the size changed this frame.
bool zoom_input(App& app);

}  // namespace turbo
