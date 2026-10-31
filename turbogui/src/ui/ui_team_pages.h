// FC 27 LE Turbo GUI - the Team pages of the 2.20 navigation (brief 2.20-IA-pages, track "team"): Team > Players and Team > Transfers.
// New pages built beside the old panels: each is a self-contained void draw_<page>(App&) that the wiring owner (brief 2.20-IA-wiring)
// registers under its page. Nothing here registers itself with the shell and no old panel was edited: every control sends the same
// Lua module with the same override keys, or calls the same App function, as the old Players / Career / Tools panels.
//
//   draw_team_players     Team > Players     the career-only side of a club: tab Player (contract, form, squad role, growth of one player)
//                                            and tab Whole squad (the My team preset + the squad cards, draw_career_squad_cards)
//   draw_team_transfers   Team > Transfers   moves of the club, loans in and out, moving and signing players, the transfer and loan
//                                            lists, contract extensions and the squad roles grid
//
// The club is App::sel_team (shared with Team > Identity / Kits; the user's club when none is chosen).
//
// Weak declarations (TURBO_WEAK, the precedent of draw_transfers in ui_career_page.h): functions the old pages keep file-static. While one is
// null the page draws a fallback that sends the same request; the wiring owner turns one on by giving the function external linkage in
// namespace turbo with exactly this signature (remove `static` / take it out of the anonymous namespace). Listed in the track's report.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace turbo {

class App;
struct PlayerRow;

#ifndef TURBO_WEAK
#if defined(__GNUC__)
#define TURBO_WEAK __attribute__((weak))
#else
#define TURBO_WEAK
#endif
#endif

void draw_team_players(App& app);
void draw_team_transfers(App& app);

// 2.22 G-squad: the sortable squad table of 2.14 (ui_teams.cpp; table "##squad": Jersey (editable), Player (click = Players > Edit a player),
// Pos, OVR, Age, Line-up slot (editable), Calc OVR (2.22 calc-ovr); every header sorts, a third click returns to the database order). It takes the club as given, so it
// serves any club: Players > My squad (the user's club), Team > Players tab "Squad list" (the club picked there) and the old Teams panel.
void squad_table(App& app, int64_t teamid);
// What the last squad_table call drew (plain text and sort arrows are not ImGui items, so the tests read it here)
struct SquadTableState {
    int64_t club = 0;           // the club the table was asked for
    size_t rows = 0;            // players linked to it
    int columns = 0;            // 7 once the table was drawn
    bool table_drawn = false;   // false when the game has no teamplayerlinks table or the table was clipped away
    int sort_column = -1;       // the column the sort is on (-1 = database order)
    bool sort_ascending = true;
    bool stale_notice = false;  // 4.0 S4: Team > Squad showed "The database changed ... Press Refresh" instead of the table
};
const SquadTableState& squad_table_state();

// What the last frame of each page drew (plain text is not an ImGui item, so the tests read it here)
struct TeamPageState {
    int64_t club = 0;          // the club the page worked on (0 = none)
    int64_t player = 0;        // Players: the player shown in the cards
    bool waiting = false;      // the page showed "Waiting for your career" instead of its cards
    bool own_club = false;     // the club is the user's club (roles, morale, lists and the preset exist for it only)
    size_t listed = 0;         // Players: rows in the player list; Transfers: rows in the contract table
    size_t loans_in = 0;       // Transfers: players on loan at the club
    size_t loans_out = 0;      // Transfers: players the club lent out
    int cards = 0;             // cards drawn this frame
    std::string headline;      // Players: "ST, overall 80, contract until 2029"
    std::string extend_line;   // Transfers: what an extension of the picked player would give ("Contract until 2030")
};
const TeamPageState& team_players_state();
const TeamPageState& team_transfers_state();

// What the user typed in the Transfers forms (one set for the page; the tests fill it in place of typing)
struct TeamMoveInputs {
    int64_t player = 0;        // Move a player: the player picked (0 = the one of Team > Players, else the best one)
    int64_t to_team = 0;       // Move a player: the club he goes to
    int64_t sign_pick = 0;     // Sign a player: the player picked in the search
    int fee = 0, wage = 0, months = 60, loan_months = 12;
    int sign_fee = 0, sign_wage = 0, sign_months = 60;
    int extend_pick = 0;       // Contract extensions: 0..4 = 1 to 5 years
    std::map<int64_t, int> role_pick;  // Squad roles: the role picked per player, 0..4 (Crucial .. Prospect); a player without an entry is Rotation
};
TeamMoveInputs& team_move_inputs();

// ---- weak declarations: exact signatures of the functions to expose (file : line at base 1320785 -> action)
// ui_players.cpp:595   static void squad_role_section(App&, const PlayerRow&)          -> remove `static`
void squad_role_section(App& app, const PlayerRow& p) TURBO_WEAK;
// ui_players.cpp:676   static void career_dev_tab(App&, const PlayerRow&)              -> remove `static`
void career_dev_tab(App& app, const PlayerRow& p) TURBO_WEAK;
// ui_players.cpp:533   static void list_buttons(App&, const PlayerRow&, bool)          -> remove `static`
void list_buttons(App& app, const PlayerRow& p, bool with_status) TURBO_WEAK;
// core/team_preset.h (track S2, brief 2.20-S2-team-defaults): the "My team" preset of the user's club; false when nothing was sent
bool team_preset_apply(App& app, int64_t teamid) TURBO_WEAK;

}  // namespace turbo
