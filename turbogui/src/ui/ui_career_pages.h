// FC 27 LE Turbo GUI - the Career pages of the GUI revamp (brief 2.16-gui-pages, track career = plan step 10;
// TURBO_GUI_REVAMP_PLAN.md sections 4.3 and 5.3). New pages built beside the old panels: each is a self-contained
// void draw_<page>(App&) that the navigation re-cut (gui/revamp-2 step 4) registers under its page. Nothing here is
// registered with the shell yet, and no old panel was edited: the controls call the same Lua modules, the same App
// fields and the same existing draw functions the old Tools / Squad / Staff panels use.
//
//   draw_career_transfers_money  Career > Transfers & money   Budget, Contracts, Moves of my club, Transfer bans
//   draw_career_manager_jobs     Career > Manager jobs        Job offer, Stop the club sacking me, Move a manager
//   draw_career_my_squad         Players > My squad           2.22: tab Squad (the sortable squad table, squad_table) and tab Career tools:
//                                                             Form & fitness, Squad roles, Contracts, Growth & youth, Squad tools
//   draw_career_staff            Players > Managers & staff   the manager list, Details, Face, Scouts (rules and market left)
#pragma once
#include <cstdint>
#include <string>

namespace turbo {

class App;

void draw_career_transfers_money(App& app);
void draw_career_manager_jobs(App& app);
void draw_career_my_squad(App& app);
// 2.22: the five cards of My squad without the tab bar and the table (Team > Players > Whole squad draws these)
void draw_career_squad_cards(App& app);
void draw_career_staff(App& app);

// What the last frame of each page drew (plain text is not an ImGui item, so the tests read it here)
struct CareerPageState {
    int64_t club = 0;          // the club the page worked on (my club)
    int64_t manager_club = 0;  // Manager jobs: the club picked for a job offer
    int64_t move_club = 0;     // Manager jobs: the club picked for a manager move
    bool waiting = false;      // the page showed "Waiting for your career" instead of its cards
    int cards = 0;             // cards drawn this frame
    int tags = 0;              // of them with the "Not tested yet" tag
};
const CareerPageState& career_page_state();

}  // namespace turbo
