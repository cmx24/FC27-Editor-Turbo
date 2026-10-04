// FC 27 LE Turbo GUI - match setup and gameplay switches inside FC27.exe (the Windows host of core/match_setup.h).
//
//   * install_match_setup(): after install_game_hooks(). Resolves the game-variable anchors (gamevar_get_int,
//     gamevar_set_int, gamevar_object, gamevar_table_slot, gamevar_lock, gamevar_set_lock; they must agree with each
//     other) and the two FCE result handlers (fce_sched_handle_message, fce_standings_handle_message). Nothing is hooked
//     at start.
//   * variables: set / clear run on the game thread (run_on_game_thread; at once when the caller is the game thread)
//     through core gv::apply / gv::clear: the game's own SetInt is called after the store, the table, the arena room
//     and the free list were checked; the outcome arrives through the service's poll(). Kill switch
//     turbo_output\call_gamevar_off.txt.
//   * result fixing (opt-in): the first fix installs the guarded hooks "fce_result_sched" / "fce_result_standings" on
//     the two HandleMessage functions. A detour rewrites the goals of a RequestUpdateMatchResult whose fixture is in the
//     fix table (regular-time results only, `this` must carry the manager's vtable), then calls the original; every
//     other message passes untouched. Kill switches turbo_output\call_match_fix_off.txt (no rewrite, no install) and
//     the per-hook turbo_output\hook_fce_result_sched_off.txt / hook_fce_result_standings_off.txt.
//   * a build in no signature table leaves everything off.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/match_setup.h"

namespace host {

void install_match_setup();
// The App's service (core/match_setup.h msetup::Service) over the functions above
std::shared_ptr<turbo::msetup::Service> match_setup_service();
// Status lines for the Status tab (game_calls_status appends them)
std::vector<std::string> match_setup_status();

}  // namespace host
