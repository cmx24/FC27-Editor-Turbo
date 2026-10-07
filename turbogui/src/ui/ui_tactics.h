// FC 27 LE Turbo GUI - Teams > Tactics (Turbo 2.0, docs/TURBO_2_0_PLAN.md section 3): scope pills, preset browser, the mini pitch and
// the slider cards, all driven by the slider registry (core/sliders.h). The pitch draws the core model (core/tactics.h PreviewModel,
// built through its PreviewCache) of the selected team's SAVED formation (core/tactics_db.h; Turbo's built-in shape when the save has none).
// The Opposition scope adds the solver's card (core/opposition.h). HONESTY: only Live (game variables) and DB (saved data) sliders are
// ever written, and only on Apply; Preview and RE sliders are drawn at reduced opacity with "not applied to game". Kill switch:
// turbo_output\tactics_off.txt (blocks every write and greys the effect layers of the pitch).
#pragma once
#include <cstdint>
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
        std::string was;          // "was 62" / "was: game decides" ("" = nothing to say): against the last Apply, or against the loaded preset in compare mode
        bool enabled = false;     // switched on (the game does not decide)
        bool dimmed = false;      // drawn at reduced opacity
        int value = 0;
    };
    int scope = 0;                // SliderScope index
    std::vector<Row> rows;
    std::string preview_strip;    // "12 of 20 visible settings are preview-only" (the sliders of this scope)
    std::string banner;           // "Game overrides are active ..." ("" = none)
    std::string off_line;         // "Tactics are off: ..." ("" = switch not present)
    std::string receive_title;    // "What the game will receive"
    std::vector<std::string> receive;      // one line per Live / DB write that will be sent
    std::vector<std::string> not_written;  // switched-on settings this build does not write ("formations.offset{n}x: ...")
    std::vector<std::string> writes_done;  // what the last Apply did, one line per DB row written ("cm_mentalities.defensivedepth = 70: row 2 of 2 ...")
    int changed = 0;              // "N changed"
    bool off = false;
    bool dry_run = false;
    std::string error;            // inline error of the exception-proof draw ("" = none)
    // ---- the pitch
    std::string chip;             // the pitch's honesty chip
    int pitch_primitives = 0;
    bool pitch_capped = false;
    int heat_stride = 0;
    std::vector<std::string> pitch_labels;  // text drawn on the pitch's derived and modelled layers
    std::string pitch_strip;      // the model's own "N of M visible settings are preview-only"
    std::string formation_name;   // the formation drawn ("4-3-3")
    bool formation_saved = false; // drawn from the save (else Turbo's built-in shape)
    std::string formation_source; // "saved formation 10 (the team's style link)" ("" = built-in)
    std::string formation_note;   // why a built-in shape is drawn, or what the preview is ("" = saved data)
    int selected_slot = -1;       // the pitch dot whose position sliders apply (-1 = none)
    int phase = 2;                // 0 with the ball, 1 without, 2 overall
    size_t preview_builds = 0;    // how often the core preview model was built (it is cached: an unchanged frame does not rebuild it)
    // ---- presets
    std::string selected_profile; // id of the preset last loaded ("" = none)
    std::vector<std::string> preset_names;  // the presets listed in this scope, in order
    bool compare = false;         // compare mode: "was X" is the loaded preset's value
    std::vector<std::string> compare_lines; // "Defensive line depth: was 62, now 70"
    bool import_open = false;     // the import plan dialog is open
    size_t import_items = 0;      // presets in that plan
    // ---- the opposition card (Opposition scope)
    struct OppCard {
        bool shown = false;       // the card was drawn
        bool active = false;      // the solver produced a profile (not "none" mode, an opponent is picked)
        std::string note;         // why there is no profile ("select the opponent in the club list" ...), else ""
        std::string fixture_line; // "You: Arsenal (squad 86) against Everton (squad 79): strength ratio 0.92, Everton at home"
        std::string facts_line;   // what was read from the save and what was not
        std::string seed_text;    // "Seed 00ab..." (deterministic from the fixture)
        uint64_t seed = 0;
        double ratio = 1.0;
        double user_ovr = 0, opp_ovr = 0;
        int n_def = 0, n_mid = 0, n_att = 0, n_wide = 0;  // the opponent's formation shape
        std::vector<std::string> blend;    // "Possession 41%" (style families, blended, never hard-assigned)
        std::vector<std::string> changes;  // "Defensive line depth -4 (preview only, not sent to the game)"
        std::vector<std::string> live;     // "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI = 58  (base 50 + offset +8)"
        std::string explain;      // the solver's explanation, one line per fact, change and rule that fired
        bool can_apply = false;   // a Live offset exists and Apply is not blocked
    } opp;
};
const TacticsTabState& tactics_tab_state();

// The Tactics tab body (inside Teams > ##ttabs). Never throws: a failure shows an inline error.
void draw_tactics(App& app);
// The kill switch file exists (checked at once, never cached): the writes use this
bool tactics_off_now(App& app);
// Clear the game variables this panel set (the host unloading Turbo; nothing is written otherwise). Never throws.
void tactics_clear_on_exit(App& app);

}  // namespace turbo
