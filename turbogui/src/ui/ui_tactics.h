// FC 27 LE Turbo GUI - Teams > Tactics (Turbo 2.0, docs/TURBO_2_0_PLAN.md section 3): scope pills, preset browser, the mini pitch and
// the slider cards, all driven by the slider registry (core/sliders.h). HONESTY: only Live (game variables) and DB (saved data)
// sliders are ever written; Preview and RE sliders are drawn at reduced opacity with "not applied to game". Kill switch:
// turbo_output\tactics_off.txt (blocks every write and greys the effect layers of the pitch).
#pragma once
#include <string>
#include <vector>

namespace turbo {

class App;

// What the panel drew in its last frame (plain text is not an ImGui item, so the tests read it here)
struct TacticsTabState {
    struct Row {
        std::string key;
        std::string status;       // "Live" "DB" "Local" "Preview" "RE"
        std::string tag;          // the short line under the label ("not applied to game", "saved data, in-game effect unverified", ...)
        bool enabled = false;     // switched on (the game does not decide)
        bool dimmed = false;      // drawn at reduced opacity
        int value = 0;
    };
    int scope = 0;                // SliderScope index
    std::vector<Row> rows;
    std::string preview_strip;    // "12 of 20 visible settings are preview-only"
    std::string banner;           // "Game overrides are active ..." ("" = none)
    std::string off_line;         // "Tactics are off: ..." ("" = switch not present)
    std::string receive_title;    // "What the game will receive"
    std::vector<std::string> receive;  // one line per Live / DB write
    int changed = 0;              // "N changed"
    bool off = false;
    bool dry_run = false;
    std::string error;            // inline error of the exception-proof draw ("" = none)
    std::string chip;             // the pitch's honesty chip
    int pitch_primitives = 0;
    std::vector<std::string> pitch_labels;  // text drawn on the pitch's derived and modelled layers
};
const TacticsTabState& tactics_tab_state();

// The Tactics tab body (inside Teams > ##ttabs). Never throws: a failure shows an inline error.
void draw_tactics(App& app);
// The kill switch file exists (checked at once, never cached): the writes use this
bool tactics_off_now(App& app);
// Clear the game variables this panel set (the host unloading Turbo; nothing is written otherwise). Never throws.
void tactics_clear_on_exit(App& app);

}  // namespace turbo
