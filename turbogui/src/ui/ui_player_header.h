// FC 27 LE Turbo GUI - Players > Player: the header card above the tabs (brief 3.0-shell, track B).
//
// The hero header for App::sel_player (Turbo X, DESIGN_SPEC_X.md): the miniface at 96 px (the same game picture the Appearance
// Editor's Looks card shows, draw_legacy_picture of legacy_path::player_miniface), the name at 1.6x in the semibold face, one muted
// line (club and "on loan", positions, age, nationality), three stat tiles OVR / POT / Calc (the overall the formula works out from
// the attributes, core/overall.h), the Shown OVR field inside the OVR tile, Undo at the right. draw_player_edit (ui_pages_216.cpp)
// draws it above the segmented control.
#pragma once
#include <cstdint>
#include <string>

namespace turbo {

class App;

// Draws the card for app.sel_player. False (nothing drawn) when no player is picked, the players table is missing or the
// player's record is no longer alive (another career loaded).
bool draw_player_header(App& app);

// Pit wall decision 10-25 15:35 (advisor): the "Shown OVR (display only)" row stayed OFF in the TEST build, because nothing in game
// reads it and it would pass for the in-game OVR mask that is blocked. 10-31 (tx-ovr, the owner's "OVR mask" ask): ON, so the row is
// reachable on Players > Player (the header card above the tabs); still display only, nothing in game reads it. Set false to hide it again.
constexpr bool kShowOvrMaskField = true;

// What the last call drew (plain text is not an ImGui item, so the tests read it here)
struct PlayerHeaderState {
    int64_t playerid = 0;       // 0 = no card drawn
    std::string name;
    std::string club_line;      // "Leicester City", "Leicester City (on loan from X)", "No club (free agent)"
    std::string details_line;   // "Leicester City . ST, CF . 36 . England" (middle dots; Turbo X: the one muted line under the name)
    int overall = 0, potential = 0;
    int calc_overall = -1;      // compute_overall of the first position; -1 when an attribute could not be read
    bool picture = false;       // the miniface was on screen
    int shown_ovr = 0;          // 3.2: the "Shown OVR (display only)" kept for him in this career (core/ovr_mask.h), 0 = none
    std::string shown_note;     // 3.2: what the last Set / Clear did or why it was refused, "" = nothing to say
};
const PlayerHeaderState& player_header_state();

// 3.2 "Shown OVR (display only)": text "1".."99" sets it, "" clears it, for the career Turbo has identified
// (turbo_output\careers\<id>\ovr_mask.json, core/ovr_mask.h). Nothing goes to the database or game memory.
// False + *msg when refused (no career, bad value).
bool shown_ovr_apply(App& app, int64_t pid, const std::string& text, std::string* msg);

}  // namespace turbo
