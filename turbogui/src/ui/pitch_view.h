// FC 27 LE Turbo GUI - the mini pitch of the Tactics tab (Turbo 2.0, docs/TURBO_2_0_PLAN.md section 3 "Visual honesty rules").
//
// One reusable widget: an InvisibleButton region of fixed 105:68 aspect (so Dear ImGui owns the input capture and the input shield
// works) drawn with ImDrawList only, at most kPitchPrimitiveCap primitives. Every drawn thing carries a provenance tier:
//   Exact   (E) straight from stored data (formation dots, labels): solid fills and lines
//   Derived (D) monotonic geometry from a slider (defensive line, press line, width band): dashed, thin, labelled "62/100 (schematic)"
//   Modelled(M) Turbo's own estimate (heat grid): hatched, monochrome ramp labelled "relative intensity (arbitrary)", no numbers, off by default
// No metres, no outcome language, no animation. A persistent chip says "Model view: Turbo's estimate, not game output". When a kill
// switch is on (greyed) the effect layers (D and M) turn grey. Pure drawing: no file I/O, no game memory, never throws.
#pragma once
#include <string>
#include <vector>

#include "imgui.h"

namespace turbo {

constexpr int kPitchPrimitiveCap = 400;
constexpr int kUiHeatCols = 6, kUiHeatRows = 4;

enum class PitchTier { Exact, Derived, Modelled };

// One formation dot. x runs along the pitch (0 = own goal, 1 = opponent goal), y across it (0 = one touchline, 1 = the other).
struct PitchDot {
    float x = 0.5f, y = 0.5f;
    std::string label;  // position name ("CB"), may be empty
};

// A derived layer: `value` 0..100 (-1 = not set, nothing drawn), `label` the plain name ("Defensive line")
struct PitchLayer {
    int value = -1;
    std::string label;
};

struct PitchView {
    std::vector<PitchDot> dots;  // Exact
    std::string dots_note;       // "saved data, in-game effect unverified" (shown under the pitch; empty = none)
    PitchLayer def_line, press_line, width_band;  // Derived (drawn only when value >= 0)
    bool show_derived = true;
    bool show_heat = false;      // Modelled layer, off by default
    std::vector<float> heat;     // kUiHeatCols * kUiHeatRows values 0..1, row-major; empty = nothing to draw
    bool greyed = false;         // a kill switch is on: D and M layers drawn grey
};

// What the last draw did (the tests read this; plain text is not an ImGui item)
struct PitchViewResult {
    int primitives = 0;          // draw-list primitives used
    bool capped = false;         // the cap was hit and the rest was left out
    int hovered_dot = -1;        // index in PitchView::dots under the mouse
    int clicked_dot = -1;        // index of the dot clicked this frame
    float width = 0.0f, height = 0.0f;  // size of the region (height / width == 68 / 105)
    std::vector<std::string> labels;     // every text drawn on D and M layers
    std::vector<std::string> exact_labels;  // every text drawn on E things (dot names)
    std::string chip;            // the honesty chip text
};

const char* pitch_chip_text();  // "Model view: Turbo's estimate, not game output"
// "62/100 (schematic)" for a derived layer
std::string pitch_derived_label(const PitchLayer& l);
// true when a layer label carries a distance unit ("19 m", "48 metres", "yd", "km"): the honesty rule forbids it (tests)
bool pitch_label_has_unit(const std::string& s);
// The primitives the layout would use for this view, without drawing (a cheap upper bound check for tests)
int pitch_primitive_estimate(const PitchView& v);

// Draw the pitch in the current window, `width` wide (<= 0: the available width). Never throws.
PitchViewResult draw_pitch_view(const char* id, const PitchView& v, float width);
// The result of the most recent draw_pitch_view
const PitchViewResult& pitch_view_last();

}  // namespace turbo
