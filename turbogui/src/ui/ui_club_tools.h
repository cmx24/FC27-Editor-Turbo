// FC 27 LE Turbo GUI - Turbo Tools sections "Your club: customisation hub" and "Career settings unlock (advanced)"
// (Turbo 1.1.1; core/hub_customise.h and core/career_settings.h). Drawn by draw_tools (ui_tools.cpp).
#pragma once
#include <string>

namespace turbo {

class App;

void draw_club_tools(App& app);

// What the sections drew / did last (plain text is not an ImGui item, so the tests read it here)
struct ClubToolsState {
    std::string hub_line;         // the one-line status of the customisation hub
    std::string hub_result;       // the last Reopen outcome ("" = none)
    bool hub_ready = false;       // the MainHubManager was located and checked
    std::string settings_line;    // career settings: status line
    std::string settings_result;  // the last Apply / Restore outcome
    bool settings_on = false;     // Turbo's override is in place
    std::string backup_result;    // the last save backup outcome
};
const ClubToolsState& club_tools_state();

// The button actions, without the popups (the tests call them; the GUI calls them after its confirmations)
bool club_reopen(App& app, bool licensed_too);
bool career_settings_set(App& app, bool on, bool squad_settings);

}  // namespace turbo
