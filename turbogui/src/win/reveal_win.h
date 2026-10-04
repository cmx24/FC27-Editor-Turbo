// FC 27 LE Turbo GUI - "reveal player data" game call inside FC27.exe (the Windows host of core/reveal.h).
//
//   * install_reveal(): after install_game_hooks(). Resolves pdrm_vtable, pdrm_reveal_player, pdrm_reveal_team and
//     calendar_today_int from the signature table and installs the guarded capture hook pdrm_handle_event, whose
//     detour only records the PlayerDataRevealManager pointer the game passes (an independent check of the pointer
//     Lua finds through mem.manager(78)).
//   * reveal_request(req, seq): the call itself. Runs AT ONCE when the caller is the game thread (the thread the
//     dispatcher last ran on), else it is queued for the dispatcher (run_on_game_thread) and the result arrives in the
//     mailbox call block later. Never runs on the render thread.
//   * kill switches: turbo_output\call_reveal_off.txt (this call), plus every game-hook switch.
//   * Lua reaches it through turbo_game_call (game_calls_win.cpp) with op 3: args = pdrm, mode (0 player / 1 team),
//     id, manager table; outputs = points after (player) or records after (team), records before.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/reveal.h"

namespace host {

void install_reveal();
// The PlayerDataRevealManager the capture hook last saw (0 = none yet) and how many of its events were seen
uint64_t pdrm_seen();
long long pdrm_events();
turbo::pdrm::Fns reveal_fns();
// True when the call may run (hooks allowed, functions resolved, no kill switch); why not otherwise
bool reveal_ready(std::string* why);
// Status lines for the Status tab (game_calls_status appends them)
std::vector<std::string> reveal_status();
// Reveal a player's or a club's data through the game's own code. req.pdrm = 0 uses the captured manager. Synchronous
// when called on the game thread (result returned); otherwise queued (result.stage == "queued", the mailbox call block
// receives the outcome when the dispatcher runs it). `seq` tags the mailbox result (0 = no mailbox result).
turbo::pdrm::Result reveal_request(turbo::pdrm::Request req, int32_t seq);

}  // namespace host
