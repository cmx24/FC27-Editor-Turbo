// FC 27 LE Turbo GUI - Players > Player Creation (2.20 IA restructure, brief 2.20-IA-pages, track "creation"): ONE page for making and
// removing players. Create (the form that starts as the chosen player's own data), Clone, Import, Export, Delete, and the CMTracker
// library as a second tab. Everything is drawn by the dialogs that already exist (ui_presets.h player_preset_buttons, ui_cmtracker.cpp
// draw_cmtracker_page); only Delete is drawn here, with the same command the Edit a player panel sends.
//
// This file is NEW and does NOT register itself with the shell: the wiring owner (brief 2.20-IA-wiring) adds
//     TURBO_REGISTER_NEW_PAGE(shell::Page::CreatePlayer, draw_player_creation);
// in ui_pages_216.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace turbo {

class App;

// What the last frame of the page drew (plain text is not an ImGui item, so the tests read it here)
struct PlayerCreationState {
    int tab = 0;                // 0 = From a player, 1 = From the CMTracker library
    int64_t playerid = 0;       // the player the editor shows (0 = none picked)
    size_t listed = 0;          // players in the list after the filters
    std::string headline;       // "ST, FC Goa, overall 57, potential 57" (no ID)
    std::string delete_why;     // why Delete cannot run for him ("" = it can)
    std::string confirm_text;   // the text of the Delete confirmation drawn this frame ("" = none)
};
const PlayerCreationState& player_creation_state();

// The page. Draws a hint instead while the game is not connected.
void draw_player_creation(App& app);

}  // namespace turbo
