// FC 27 LE Turbo GUI - "standings_refresh" game call inside FC27.exe (the Windows host of core/standings_refresh.h).
//
//   * install_standings_refresh(): after install_game_hooks(). Resolves svm_refresh_comp / svm_listener (required) and
//     svm_vtable / svm_slot10 (validation anchors) from the signature table. Nothing is hooked.
//   * standings_refresh_request(req, seq): the call. Runs AT ONCE when the caller is the game thread (the thread the
//     dispatcher last ran on), else it is queued for the dispatcher (run_on_game_thread) and the outcome arrives later:
//     in the mailbox call block when seq != 0 (Lua), and always through the RefreshService's poll() (the overlay shows
//     it as a toast). Never runs on the render thread.
//   * kill switches: turbo_output\call_standings_refresh_off.txt (this call), plus every game-hook switch (a build
//     that is not in the table or game_hooks_off.txt turns the calls off as well).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/standings_refresh.h"

namespace host {

void install_standings_refresh();
// True when the call may run (hooks allowed, functions resolved, no kill switch); why not otherwise
bool standings_refresh_ready(std::string* why);
// Status lines for the Status tab (game_calls_status appends them)
std::vector<std::string> standings_refresh_status();
// Resolved functions (zeros when the build is unknown)
turbo::svm::Fns standings_refresh_fns();
// Run / queue the refresh. Synchronous on the game thread (result returned, stage "done" / an error stage); otherwise
// queued (stage "queued") and the outcome is published later. `seq` tags the mailbox call block result (0 = none).
turbo::svm::Result standings_refresh_request(const turbo::svm::Request& req, int32_t seq);
// The App's service (core/standings_refresh.h RefreshService) over the functions above
std::shared_ptr<turbo::svm::RefreshService> standings_refresh_service();

}  // namespace host
