// FC 27 LE Turbo GUI - the player list filter of the 2.16 pages (see ui_player_page.h): Create & clone builds its list with it.
#include "ui_player_page.h"

#include <cctype>
#include <string>

#include "core/model.h"
#include "move_rules.h"

namespace turbo {

namespace {

bool digits_only(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

}  // namespace

bool player_page_matches(const PlayerRow& p, const PlayerPageFilter& f, int64_t user_team) {
    if (f.my_club && (user_team <= 0 || p.club != user_team)) return false;
    if (f.free_agents && p.club > 0 && p.club != move_rules::kFreeAgents) return false;
    if (f.position >= 0) {
        bool any = false;
        for (int k = 0; k < 7 && !any; ++k) any = p.positions[k] == f.position;
        if (!any) return false;
    }
    if (!f.text.empty()) {
        const std::string q = fold_search(f.text);
        if (digits_only(q) && std::to_string(p.playerid) == q) return true;
        if (!fold_find(p.name_fold, p.name, q) && !fold_find(p.club_fold, p.club_name, q)) return false;
    }
    return true;
}

}  // namespace turbo
