// FC 27 LE Turbo GUI - "player_morale" game call inside FC27.exe (the Windows host of core/player_morale.h).
//   * install_player_morale(): resolves pmm_vtable, pmm_handle_event, pmm_find_record, pmm_set_total, pmm_get_level (optional),
//     dc_is_player_in_team, um_vtable. No hook: the call runs on request only, on the game thread.
//   * ON by default (it only touches the morale store of the user's club, after every check passed);
//     turbo_output\call_player_morale_off.txt turns it off (kill switch), plus every game-hook switch. While off the call answers status failed with
//     the text "player_morale: off (...)" and Lua keeps Live Editor's SetPlayerMorale.
//   * Lua reaches it through turbo_game_call with op kCallOpPlayerMorale (13).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/player_morale.h"

namespace host {

void install_player_morale();
bool player_morale_ready(std::string* why);
std::vector<std::string> player_morale_status();
turbo::morale::Result player_morale_request(turbo::morale::Request req, int32_t seq);

}  // namespace host
