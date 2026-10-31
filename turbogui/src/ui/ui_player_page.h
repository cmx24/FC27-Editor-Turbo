// FC 27 LE Turbo GUI - the player list filter of the 2.16 pages (plan TURBO_GUI_REVAMP_PLAN.md sections 3, 5.1 and 5.3, step 5; brief
// 2.16-gui-pages, track "player"). Create & clone (ui_player_creation.cpp) builds its list with player_page_matches.
#pragma once
#include <cstdint>
#include <string>

namespace turbo {

struct PlayerRow;

// The list filters. `text` is matched against the name and the club (any letter case); a text of digits only also
// matches the player with that exact ID (kept for people who know one; the page never prints IDs)
struct PlayerPageFilter {
    std::string text;
    bool my_club = false;       // only the players of the user's club
    bool free_agents = false;   // only players without a club
    int position = -1;          // -1 = any; else one of his preferred positions
};
// True when the player passes the filter. `user_team`: the user's club (0 = unknown). Pure: tests call it directly.
bool player_page_matches(const PlayerRow& p, const PlayerPageFilter& f, int64_t user_team);

}  // namespace turbo
