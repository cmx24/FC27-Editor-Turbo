// FC 27 LE Turbo GUI - Players > Player: the header card above the tabs (brief 3.0-shell, track B).
//
// One read-only card for App::sel_player: the miniface (the same game picture the Appearance Editor's Looks card shows,
// draw_legacy_picture of legacy_path::player_miniface), the name in the title font, club (and "on loan"), age, positions,
// nationality, OVR and POT big, and the overall the formula works out from the attributes (core/overall.h) muted beside them.
// Nothing in it is editable. draw_player_edit (ui_pages_216.cpp) draws it above its tab bar.
#pragma once
#include <cstdint>
#include <string>

namespace turbo {

class App;

// Draws the card for app.sel_player. False (nothing drawn) when no player is picked, the players table is missing or the
// player's record is no longer alive (another career loaded).
bool draw_player_header(App& app);

// Pit wall decision 10-25 15:35 (advisor): the "Shown OVR (display only)" row stays OFF in the TEST build, because nothing in game
// reads it and it would pass for the in-game OVR mask that is blocked. Flip to true only on Cassio's later F8 decision.
constexpr bool kShowOvrMaskField = false;

// What the last call drew (plain text is not an ImGui item, so the tests read it here)
struct PlayerHeaderState {
    int64_t playerid = 0;       // 0 = no card drawn
    std::string name;
    std::string club_line;      // "Leicester City", "Leicester City (on loan from X)", "No club (free agent)"
    std::string details_line;   // "Age 36  |  ST, CF  |  England"
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
