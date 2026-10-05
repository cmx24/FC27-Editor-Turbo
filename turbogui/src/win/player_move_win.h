// FC 27 LE Turbo GUI - "player_move" game call inside FC27.exe (the Windows host of core/player_move.h).
//
//   * install_player_move(): after install_game_hooks() (install_game_calls calls it). Resolves the game's club-change, release and
//     contract-record functions plus the read-only calls and vtables the validation needs from the signature table
//     (teamutil_player_moved, dc_is_player_in_team, dc_squad_counts, dc_get_league_of_team, is_international_league,
//     pcm_add_contract_record, ctm_release_player, pcm_vtable, tm_vtable, um_vtable, ctm_vtable; morale_vtable / morale_handle_event
//     only for the morale gate note). No hook: the call runs on request only.
//   * player_move_request(req, seq): the call. Runs AT ONCE when the caller is the game thread, else it is queued for the
//     game-thread dispatcher (run_on_game_thread) and the result arrives in the mailbox call block later. Never runs on the render
//     thread. One call at a time: a second request while one is queued or running is refused.
//   * kill switch: turbo_output\call_player_move_off.txt (this call), plus every game-hook switch.
//   * Lua reaches it through turbo_game_call with op kCallOpPlayerMove (11): args = comm service, code | months << 8,
//     pid | wage << 32, from | to << 32; outputs = from_ok, to_ok (core/player_move.h has the whole contract).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/player_move.h"

namespace host {

void install_player_move();
// True when the call may run (hooks allowed, functions resolved, no kill switch); why not otherwise
bool player_move_ready(std::string* why);
// Status lines for the Status tab (game_calls_status appends them)
std::vector<std::string> player_move_status();
// Resolved functions (zeros when the build is unknown)
turbo::pm::Fns player_move_fns();
// The call. Synchronous when called on the game thread (result returned); otherwise queued (result.stage == "queued", the mailbox
// call block receives the outcome when the dispatcher runs it). `seq` tags the mailbox result (0 = none).
turbo::pm::Result player_move_request(turbo::pm::Request req, int32_t seq);

}  // namespace host
