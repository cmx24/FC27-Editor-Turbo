// FC 27 LE Turbo GUI - game calls inside FC27.exe (the Windows host of core/game_calls.h).
//
//   * install_game_calls(mailbox): after install_game_hooks(). Resolves the job-offer functions from the signature
//     table (jmm_vtable, jmm_has_application, jmm_apply_for_job, jmm_make_offer, calendar_today_int) and installs the
//     guarded capture hook jmm_handle_event, whose detour only records the JobMarketManager pointer the game passes
//     (an independent check of the pointer Lua finds through mem.manager(53)).
//   * job_offer_request(jmm, team): the call itself. Runs AT ONCE when the caller is the game thread (the thread the
//     dispatcher last ran on), else it is queued for the dispatcher (run_on_game_thread) and the result arrives in the
//     mailbox call block later. Never runs on the render thread.
//   * kill switches: turbo_output\call_job_offer_off.txt (this call), plus every game-hook switch (a build that is
//     not in the table or game_hooks_off.txt turns the calls off as well).
//   * turbo_game_call (exported, package.loadlib): Lua's entry. Reads op + args from the mailbox call block
//     (core/game_calls.h), runs or queues the call, writes status / outputs / text back. Returns 0, never touches the
//     Lua state.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/game_calls.h"

namespace host {

void install_game_calls(uint64_t mailbox);
// The JobMarketManager the capture hook last saw (0 = none yet) and how many of its events were seen
uint64_t jmm_seen();
long long jmm_events();
// Resolved functions (zeros when the build is unknown)
turbo::JobMarketFns job_market_fns();
// True when the job-offer call may run (hooks allowed, functions resolved, no kill switch); why not otherwise
bool job_offer_ready(std::string* why);
// Status lines for the Status tab (game_hooks_report appends them)
std::vector<std::string> game_calls_status();
// Create a job offer from `team` through the game's own code. jmm = 0 uses the captured manager. Synchronous when
// called on the game thread (result returned); otherwise queued (result.stage == "queued", the mailbox call block
// receives the outcome when the dispatcher runs it). `seq` tags the mailbox result.
turbo::JobOfferResult job_offer_request(uint64_t jmm, int team, int32_t seq);

}  // namespace host

extern "C" __declspec(dllexport) int turbo_game_call(void* lua_state);
