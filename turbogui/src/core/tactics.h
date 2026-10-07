// FC 27 LE Turbo GUI - tactics geometry and the preview model (Turbo 2.0): what the mini pitch draws, as plain data.
//
// Pure functions of a formation and a few slider values: no ImGui, no game memory, no clock, no random numbers. The UI (a later track)
// only turns a PreviewModel into ImDrawList calls. HONESTY RULES (docs/TURBO_2_0_PLAN.md section 3): every item carries a provenance tier,
//   Exact   (E): straight from stored data (formation dots, position labels): solid
//   Derived (D): a monotonic picture of a slider (defensive line, press line, width band, run arrows): dashed, labelled "62/100 (schematic)",
//                never in metres or any other unit
//   Modelled(M): the heat grid, roam rings and exposure hint: stippled, labelled "relative ... (arbitrary)", never with a number
// Nothing here claims what the game will do: no outcome language, no units, no animation.
//
// Pitch coordinates: x 0..1 across (touchline to touchline), y 0..1 from the own goal line (0) to the opposing goal line (1).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "sliders.h"

namespace turbo {

enum class PosGroup { GK, Def, Mid, Att };
enum class Tier { Exact, Derived, Modelled };
enum class Phase { WithBall, WithoutBall, Overall };
const char* tier_name(Tier t);  // "Exact" "Derived" "Modelled"
const char* tier_letter(Tier t);  // "E" "D" "M"

struct FormationSlot {
    std::string label;  // "CB", "LW", ...
    float x = 0.5f, y = 0.5f;
    PosGroup group = PosGroup::Mid;
};
struct Formation {
    std::string id;    // "4-3-3" for the built-ins; the formations row id (as text) for a database one
    std::string name;
    std::vector<FormationSlot> slots;  // exactly 11 once validated, slot 0 the goalkeeper
    bool from_db = false;
};

// The built-in table used when the schema probe finds no formations table: 4-4-2, 4-3-3, 4-2-3-1, 4-1-4-1, 4-5-1, 4-4-1-1, 4-3-2-1,
// 4-1-2-1-2, 3-5-2, 3-4-3, 5-3-2, 5-4-1.
const std::vector<Formation>& fallback_formations();
const Formation* find_fallback_formation(const std::string& id);
// Checks a formation read from the database: 11 slots, slot 0 a goalkeeper and the only one, coordinates clamped into 0..1. False (with
// err) leaves `out` untouched and the caller on the fallback table.
bool make_formation(const std::string& id, const std::string& name, std::vector<FormationSlot> slots, Formation& out, std::string* err = nullptr);
PosGroup group_of_label(const std::string& label);  // "GK" Gk, "CB"/"LB"/"RB"/"LWB"/"RWB" Def, "LW"/"RW"/"ST"/"CF" Att, the rest Mid

// ---- preview model
struct PreviewDot {          // Exact
    int slot = 0;
    std::string label;
    float x = 0, y = 0;
    PosGroup group = PosGroup::Mid;
    bool selected = false;
};
struct PreviewLine {         // Derived: a horizontal line across the pitch
    std::string id;          // "def_line" "press_line" "front_line"
    Tier tier = Tier::Derived;
    float y = 0;
    float weight = 0;        // 0..1 drawing weight (thin to thick); not a measurement
    std::string label;       // "Defensive line 62/100 (schematic)"
};
struct PreviewBand {         // Derived: the width the team spreads over
    std::string id;          // "width_band"
    Tier tier = Tier::Derived;
    float x_lo = 0, x_hi = 1;
    std::string label;
};
struct PreviewArrow {        // Derived: a forward run
    int slot = 0;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    Tier tier = Tier::Derived;
};
struct PreviewRing {         // Modelled: how freely a player roams (relative)
    int slot = 0;
    float radius = 0;        // pitch share, drawn stippled
};
struct HeatGrid {            // Modelled: relative intensity, 0..1 per cell (1 = the busiest cell), row-major, row 0 at the own goal
    int cols = 0, rows = 0;
    std::vector<float> cell;
    float at(int c, int r) const { return cell[static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(c)]; }
};

struct PreviewOptions {
    Phase phase = Phase::Overall;
    bool show_heat = false;       // Modelled layers are off unless asked for
    bool show_roam = false;
    bool show_risk = false;
    int selected_slot = -1;       // the pitch dot whose position sliders apply (-1 = none)
    bool effects_greyed = false;  // a kill switch is on: the UI draws every effect layer grey
};

struct PreviewModel {
    Formation formation;
    Phase phase = Phase::Overall;
    std::vector<PreviewDot> dots;
    std::vector<PreviewLine> lines;
    std::vector<PreviewBand> bands;
    std::vector<PreviewArrow> arrows;
    std::vector<PreviewRing> rings;     // only when show_roam
    HeatGrid heat;                      // empty unless show_heat
    std::string heat_label;             // "Relative intensity (arbitrary)" when the grid exists
    float exposure = 0;                 // 0..1, only when show_risk
    std::string exposure_label;         // "Relative exposure (arbitrary)"
    std::string arrows_label;           // "Run arrows 40/100 (schematic)" when there are arrows
    std::string ring_label;             // "Roaming range (arbitrary)" when there are rings
    std::string model_chip;             // always "Model view: Turbo's estimate, not game output"
    bool greyed = false;
    size_t visible_total = 0, visible_preview_only = 0;
    std::string preview_strip;          // "N of M visible settings are preview-only"
    // every text label of a Derived or Modelled item (the "no unit strings" test walks it)
    std::vector<std::string> derived_modelled_labels() const;
    // drawing primitives the UI will issue (dots, lines, bands, arrows, rings, heat cells): stays under kPrimitiveCap
    size_t primitive_count() const;
};
constexpr size_t kPrimitiveCap = 400;
constexpr int kHeatCols = 20, kHeatRows = 13;
extern const char* const kModelChip;

// `team` holds the team.* sliders, `pos` the position.* sliders of the selected dot. A slider the set has a value for is used whether it
// is enabled or not (the preview shows what the slider would do); one without a value uses the registry default. The dots never depend
// on any slider and the lines, band and arrows never depend on the formation (formation isolation).
PreviewModel build_preview(const Formation& f, const SliderSet& team, const SliderSet& pos, const PreviewOptions& opt);
uint64_t preview_key(const Formation& f, const SliderSet& team, const SliderSet& pos, const PreviewOptions& opt, uint64_t gen);

// Rebuilds only when the key changes (formation, slider values, options or `gen` = App::gen). The reference stays valid until the next get().
class PreviewCache {
public:
    const PreviewModel& get(const Formation& f, const SliderSet& team, const SliderSet& pos, const PreviewOptions& opt, uint64_t gen);
    size_t builds() const { return builds_; }
private:
    bool valid_ = false;
    uint64_t key_ = 0;
    size_t builds_ = 0;
    PreviewModel model_;
};

// ---- role / duty presets (FM style: the duty seeds the position sliders)
enum class Duty { Defend, Support, Attack };
const char* duty_name(Duty d);
// An enabled set of position.* sliders for the duty. Monotone in the duty: Defend <= Support <= Attack for the attacking sliders.
SliderSet duty_preset(Duty d);

}  // namespace turbo
