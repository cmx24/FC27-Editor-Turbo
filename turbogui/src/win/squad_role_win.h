// FC 27 LE Turbo GUI - "squad_role" game call inside FC27.exe (the Windows host of core/squad_role.h).
//   * install_squad_role(): resolves psm_vtable, psm_handle_event, psm_get_entry, dc_is_player_in_team, um_vtable, plus psm_add_player
//     (creating a missing entry) and psm_remove_entry (removing a stale one). No hook: the call runs on request only, on the game thread.
//   * ON by default (it only touches the squad role table of the user's club, after every check passed);
//     turbo_output\call_squad_role_off.txt turns it off (kill switch), plus every game-hook switch; turbo_output\call_squad_role_create_off.txt
//     keeps it from creating / removing entries (count only). While off the call answers status failed with the text
//     "squad_role: off (...)" and Lua keeps its memory-only path (features/squad_role.lua).
//   * Lua reaches it through turbo_game_call with op kCallOpSquadRole (14).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/squad_role.h"

namespace host {

void install_squad_role();
bool squad_role_ready(std::string* why);
std::vector<std::string> squad_role_status();
turbo::roles::Result squad_role_request(turbo::roles::Request req, int32_t seq);

}  // namespace host
