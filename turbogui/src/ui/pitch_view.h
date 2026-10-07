// FC 27 LE Turbo GUI - the mini pitch of the Tactics tab (Turbo 2.0, docs/TURBO_2_0_PLAN.md section 3 "Visual honesty rules").
//
// One reusable widget that draws a core/tactics.h PreviewModel: an InvisibleButton region of fixed 105:68 aspect (so Dear ImGui owns the
// input capture and the input shield works) drawn with ImDrawList only, at most kPitchPrimitiveCap primitives. The geometry (dots, lines,
// band, arrows, rings, heat grid, exposure) all comes from the model; nothing is computed here but pixels. Every drawn thing carries the
// provenance tier the model gave it:
//   Exact   (E) formation dots and position names (saved data, or Turbo's fallback table): solid fills and lines
//   Derived (D) defensive / press / front line, width band, run arrows: dashed, thin, labelled "62/100 (schematic)"
//   Modelled(M) heat grid, roaming rings, exposure hint: hatched or stippled, monochrome ramp, labelled "(arbitrary)", no numbers; the UI
//               turns them on one by one and they are off by default
// No metres, no outcome language, no animation. A persistent chip says "Model view: Turbo's estimate, not game output". When the model is
// greyed (a kill switch is on) the effect layers (D and M) turn grey. The pitch is landscape: the model's y (own goal to the opponent's)
// runs left to right, its x (across) top to bottom. Pure drawing: no file I/O, no game memory, never throws.
#pragma once
#include <set>
#include <string>
#include <vector>

#include "core/tactics.h"
#include "imgui.h"

namespace turbo {

constexpr int kPitchPrimitiveCap = 400;  // the budget the model plans for (core/tactics.h kPrimitiveCap)
static_assert(kPitchPrimitiveCap == static_cast<int>(kPrimitiveCap), "the pitch and the model agree on the primitive cap");

// What of the model is drawn. The model itself says which Modelled layers exist (it only builds them when asked).
struct PitchDrawOptions {
    bool show_derived = true;       // the dashed layers
    // Derived items left out because the slider behind them is "the game decides": "def_line" "press_line" "front_line" "width_band" "arrows".
    // A line for a slider nobody switched on would claim a value nobody chose.
    std::set<std::string> hidden;
    std::string dots_note;          // dim line under the pitch ("Saved formation 10 ...", "Built-in shape ..."); empty = none
};

// What the last draw did (the tests read this; plain text is not an ImGui item)
struct PitchViewResult {
    int primitives = 0;          // draw-list primitives used
    bool capped = false;         // the cap was hit and the rest was left out
    int heat_stride = 0;         // 0 = no heat grid drawn, 1 = every cell, 2 = every second cell each way (kept under the cap)
    int hovered_dot = -1;        // formation slot under the mouse
    int clicked_dot = -1;        // formation slot clicked this frame
    float width = 0.0f, height = 0.0f;  // size of the region (height / width == 68 / 105)
    std::vector<std::string> labels;     // every text drawn on D and M layers
    std::vector<std::string> exact_labels;  // every text drawn on E things (dot names)
    std::string chip;            // the honesty chip text
};

const char* pitch_chip_text();  // "Model view: Turbo's estimate, not game output" (core/tactics.h kModelChip)
// true when a layer label carries a distance unit ("19 m", "48 metres", "yd", "km"): the honesty rule forbids it (tests)
bool pitch_label_has_unit(const std::string& s);
// The primitives the model needs without its heat grid (the grid takes what is left of the cap, coarser when needed)
int pitch_primitive_estimate(const PreviewModel& m, const PitchDrawOptions& o);

// Draw the model in the current window, `width` wide (<= 0: the available width). Never throws.
PitchViewResult draw_pitch_view(const char* id, const PreviewModel& m, const PitchDrawOptions& o, float width);
// The result of the most recent draw_pitch_view
const PitchViewResult& pitch_view_last();

}  // namespace turbo
