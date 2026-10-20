// FC 27 LE Turbo GUI - Players > Edit player > Profile and Positions, attributes & PlayStyles (brief 2.20-IA-pages, track "profile").
//
// Two of the three sections Cassio asked for in player editing (the third, Appearance, is the Appearance Editor). Both pages are NEW and call the
// editors that already exist (names, commentary name, the field editors, the position box, the sliders, the PlayStyle bits); nothing is
// re-implemented and no old page is touched. They do NOT register themselves with the shell: the wiring owner (brief 2.20-IA-wiring) registers
// draw_player_profile / draw_player_ability under the Edit player sub-pages.
//
// Each page comes in two forms: the whole page ([player list | editor]) and the editor alone for the player in App::sel_player, for a shell
// that draws one shared list above its sub-pages.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "ui_shell.h"

namespace turbo {

class App;
class Table;
struct PlayerRow;

// ---- Profile: names, commentary name, personal details, club and contract (read only, with a link to Team > Players)
void draw_player_profile(App& app);         // [list | editor]
void draw_player_profile_editor(App& app);  // the editor alone, for App::sel_player

// ---- Positions, attributes & PlayStyles: overall and potential, positions, playing style, roles, attributes, PlayStyles
void draw_player_ability(App& app);         // [list | editor]
void draw_player_ability_editor(App& app);  // the editor alone, for App::sel_player

// The page the Profile's "Open Team > Players" button goes to. Page::Squad today (the career squad page that exists); the wiring owner points
// it at the new Team > Players page when that page has an id.
shell::Page& profile_team_players_page();

// 2.22: the Classic editor's player list (ui_players.cpp): eight sortable columns and the full filter grid; the Profile and Positions tabs of Edit a
// player draw the same one (one list, one set of filters, one selection). classic_player_list_rows() = rows of its last frame.
void draw_classic_player_list(App& app);
size_t classic_player_list_rows();

// The player-type editor (Overall +1/-1, role, wanted overall, preview, Apply, Undo) is archetype_bar() in ui_players.cpp, which is `static`
// there, so a new file cannot link to it. The Integrator sets this pointer ONCE, from ui_players.cpp, with one line at the end of that file:
//     static const bool s_ability_hook = (g_ability_archetype_bar = archetype_bar, true);
// (constant-initialised to nullptr here, so the order of static initialisers does not matter). While it is null the page draws a smaller
// stand-in: Overall +1 / -1 and Undo last big change, and says the full editor comes with the integration.
using ArchetypeBarFn = void (*)(App&, const Table&, const PlayerRow&);
extern ArchetypeBarFn g_ability_archetype_bar;

// What the last frame of each page drew (plain text is not an ImGui item, so the tests read it here)
struct PlayerProfileState {
    int64_t playerid = 0;        // the player shown (0 = none picked)
    size_t listed = 0;           // players in the list after the filters (whole page only)
    std::string headline;        // "ST, FC Goa, overall 57, potential 57"
    std::string club_line;       // "Club: FC Goa" / "Club: none (free agent)" / "... on loan from X"
    std::string contract_line;   // "Contract until 2029, wage 25,000" (words only)
    bool team_link_shown = false;
    bool callname_shown = false;  // the commentary-name editor was drawn this frame
};
const PlayerProfileState& player_profile_state();

struct PlayerAbilityState {
    int64_t playerid = 0;
    size_t listed = 0;
    std::string headline;
    bool archetype_bar_external = false;  // the full player-type editor (g_ability_archetype_bar) was drawn
    bool stand_in_shown = false;          // the smaller stand-in was drawn
    bool type_tier_shown = false;         // 2.22: the card "Player type & tier" (the first thing on the page) was drawn open this frame
    int bar_calls = 0;                    // 2.22: archetype_bar (or its stand-in) calls this frame: one, in that card
    int position_rows = 0;                // position boxes drawn
    int attribute_sliders = 0;            // attribute sliders drawn
    int playstyle_boxes = 0;              // PlayStyle / trait check boxes drawn
};
const PlayerAbilityState& player_ability_state();

}  // namespace turbo
