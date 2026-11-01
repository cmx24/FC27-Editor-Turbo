// FC 27 LE Turbo GUI - Customize layout (Turbo X 2.0; Cassio 2026-11-01: "only add ability to resize, scale, show/hide all the main menus,
// frames and etc. i want to be able to customize the layout / visualization").
//
// What the person can change (all saved in gui_settings.json gui.custom_layout / gui.folded_panes / gui.splits, gui_prefs.h CustomLayout):
//   * the three main menus - top bar, left menu, page tabs: shown or hidden, and each one's text size (70 % .. 160 %)
//   * every item inside them: each top-bar button, each left-menu area, each page tab
//   * the left menu's width (a drag handle on its right edge, or the slider) and narrow / wide (the existing "<" / ">")
//   * the page content's text size, and the whole UI size (the existing Ctrl + mouse wheel / Ctrl + 0)
//   * the frames: the list / preview panes can be folded away and back (right-click on their divider, or here) and dragged wider or
//     narrower (ui_split.h); "Reset frame sizes" forgets the dragged widths
// The way back always stays: Ctrl+L opens Customize layout from anywhere in Turbo, Turbo settings is never hidden from the left menu,
// and when both the top bar and the left menu are hidden a small "Menus" button stays at the top left of the page.
#pragma once

namespace turbo {
struct App;

namespace custom_layout {

bool& window_open();                 // the Customize layout window (Ctrl+L, the top bar's Layout button, Turbo settings > Layout)
void handle_shortcut(App& app);      // Ctrl+L: open / close the window (called every frame while Turbo is shown)
void draw_window(App& app);          // the window itself (called once per frame, outside the main panel)
void reset(App& app);                // every layout choice of this module back to the default (and saved)
bool save(App& app);                 // gui_settings.json now; a toast on failure

// The text size of one region: style.FontScaleMain times `scale` for every window and font push inside the scope (child windows and the
// pages' own PushFont(nullptr, FontSizeBase * x) included), restored at the end of the scope. scale == 1: nothing is touched.
class RegionScale {
public:
    explicit RegionScale(float scale);
    ~RegionScale();
    RegionScale(const RegionScale&) = delete;
    RegionScale& operator=(const RegionScale&) = delete;

private:
    float saved_ = 1.0f;
    bool on_ = false;
};

}  // namespace custom_layout
}  // namespace turbo
