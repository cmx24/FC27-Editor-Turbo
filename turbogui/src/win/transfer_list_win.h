// FC 27 LE Turbo GUI - "transfer_list" game call inside FC27.exe (the Windows host of core/transfer_list.h).
//
//   * install_transfer_list(): after install_game_hooks(). Resolves the user-actions helper functions and the vtables
//     from the signature table (uah_add_transfer_list, uah_add_loan_list, uah_try_remove_from_list, uah_vtable,
//     dao_vtable, tm_vtable, pcm_vtable, um_vtable). No hook: the call runs on request only.
//   * transfer_list_request(req, seq): the call. Runs AT ONCE when the caller is the game thread (the thread the
//     dispatcher last ran on), else it is queued for the dispatcher (run_on_game_thread) and the result arrives in
//     the mailbox call block later. Never runs on the render thread.
//   * kill switch: turbo_output\call_transfer_list_off.txt (this call), plus every game-hook switch.
//   * Lua reaches it through turbo_game_call with op kCallOpTransferList (args: action, player id, comm service,
//     the player's club); outputs: status before, status after (core/transfer_list.h kStatus*).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/transfer_list.h"

namespace host {

void install_transfer_list();
// True when the call may run (hooks allowed, functions resolved, no kill switch); why not otherwise
bool transfer_list_ready(std::string* why);
// Status lines for the Status tab (game_calls_status appends them)
std::vector<std::string> transfer_list_status();
// Resolved functions (zeros when the build is unknown)
turbo::tl::Fns transfer_list_fns();
// The call. Synchronous when called on the game thread (result returned); otherwise queued (result.stage == "queued",
// the mailbox call block receives the outcome when the dispatcher runs it). `seq` tags the mailbox result (0 = none).
turbo::tl::Result transfer_list_request(turbo::tl::Request req, int32_t seq);

}  // namespace host
