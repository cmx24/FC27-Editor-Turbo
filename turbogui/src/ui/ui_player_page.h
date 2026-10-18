// FC 27 LE Turbo GUI - the Player page of the revamped GUI (plan TURBO_GUI_REVAMP_PLAN.md sections 3, 5.1 and 5.3, step 5; brief
// 2.16-gui-pages, track "player"): "Who do I change?". A short list on the left (search by name, three chips, no ID column), cards on
// the right (Looks, Skills, Position & style, Contract & club, Name & commentary) and one More section for the moves, the lists and the
// player files. It replaces the ten tabs and thirteen buttons of the old Player edit panel (ui_players.cpp, which stays untouched).
//
// This file is NEW and self-contained: draw_player_page(App&) calls the editors that already exist (field editors, the real-face
// picker, the names and callname editors, the hair and beard section, the preset dialogs) and the same Turbo commands the old panel
// sent. It does NOT register itself with the shell: the navigation re-cut (revamp-2 step 4) wires it to shell::Page::PlayerEdit.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace turbo {

class App;
struct PlayerRow;

// The list filters of the page. `text` is matched against the name and the club (any letter case); a text of digits only also
// matches the player with that exact ID (kept for people who know one; the page never prints IDs)
struct PlayerPageFilter {
    std::string text;
    bool my_club = false;       // only the players of the user's club
    bool free_agents = false;   // only players without a club
    int position = -1;          // -1 = any; else one of his preferred positions
};
// True when the player passes the filter. `user_team`: the user's club (0 = unknown). Pure: tests call it directly.
bool player_page_matches(const PlayerRow& p, const PlayerPageFilter& f, int64_t user_team);

// What the last frame of the page drew (plain text is not an ImGui item, so the tests read it here)
struct PlayerPageState {
    int64_t playerid = 0;      // the player shown in the cards (0 = none picked)
    size_t listed = 0;         // players in the list after the filters
    std::string headline;      // "ST, FC Goa, overall 57, potential 57"
    std::string face_line;     // "Real face" / "Generic face"
    std::string confirm_text;  // the text of the confirmation dialog drawn this frame ("" = none)
    bool more_open = false;    // the More section is open
};
const PlayerPageState& player_page_state();

// Test hook: the confirmation dialog (if open) closes on the next frame as if Cancel was pressed
void player_page_cancel_dialog();

// The page: [list | cards]. Draws a hint instead while the game is not connected.
void draw_player_page(App& app);

}  // namespace turbo
