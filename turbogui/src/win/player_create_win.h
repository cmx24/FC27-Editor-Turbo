// FC 27 LE Turbo GUI - "player_create" game call inside FC27.exe (the Windows host of core/player_create.h).
//
//   * install_player_create(): after install_player_move() (install_game_calls calls it). Resolves the game's query layer
//     (db_query_init / _set_int / _set_string / _select_field / _where_int / _destroy, db_result_free, db_provider_execute), the event
//     anchors (event_allocator_global, event_base_vtable, player_inserted_event_vtable), dc_insert_team_player, post_career_event and
//     op 11's set. No hook: the call runs on request only.
//   * OFF BY DEFAULT: the call runs only while turbo_output\call_player_create_on.txt exists (opt-in until the first live test
//     passes); turbo_output\call_player_create_off.txt turns it off again (kill switch), plus every game-hook switch. While it is off
//     the call answers out[0] = -1 and Lua keeps its database path.
//   * player_create_request(req, seq): reads turbo_output\turbo_player_create.json (seq + playerid checked against the words) and the
//     database columns (bridge_meta.json), then runs AT ONCE on the game thread or is queued for the game-thread dispatcher. One call
//     at a time.
//   * Lua reaches it through turbo_game_call with op kCallOpPlayerCreate (12): args = comm service, code (1 create, 9 check), seq,
//     playerid; outputs = written mask (-1 = off), IsPlayerInTeam(pid, final team).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/player_create.h"

namespace host {

void install_player_create();
// True when the call may run (hooks allowed, functions resolved, opted in, no kill switch); why not otherwise
bool player_create_ready(std::string* why);
// Status lines for the Status tab (game_calls_status appends them)
std::vector<std::string> player_create_status();
turbo::pc::Fns player_create_fns();
// The call (see above). `seq` tags the mailbox result (0 = none).
turbo::pc::Result player_create_request(turbo::pc::Request req, int32_t seq);

}  // namespace host
