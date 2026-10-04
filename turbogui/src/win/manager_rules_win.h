// FC 27 LE Turbo GUI - the "manager_rules" game call and the unsackable hook inside FC27.exe (the Windows host of
// core/manager_rules.h; docs/re/manager_rules.md).
//
//   * install_manager_rules(): after install_game_hooks(). Resolves com_vtable / jsm_vtable (the managers'
//     constructors' lea; both constructors proven by the hub builder's name strings), com_update_job_security (called)
//     and jsm_sack_manager (hooked) from the signature table; installs two pass-through capture hooks
//     (com_update_job_security records the ClubObjectivesManager, jsm_handle_event the JobSwitchManager, both
//     vtable-checked: an independent check of the pointers Lua finds through mem.manager(133) / mem.manager(54)) and
//     the guarded hook on JobSwitchManager::SackManager.
//   * the SackManager hook is Turbo's only refusing hook: while "unsackable" is on (switched through the game call;
//     off at every game start) its detour counts the attempt, clears mPendingSack and returns WITHOUT calling the
//     original, so the user's manager is not sacked. With unsackable off, or with the kill switch
//     call_manager_rules_off.txt / hook_jsm_sack_manager_off.txt / game_hooks_off.txt present, it only calls the
//     original.
//   * manager_rules_request(sub, addr, value, team, seq): the game call (core/manager_rules.h sub-ops). Runs AT ONCE
//     when the caller is the game thread, else queued for the dispatcher; the mailbox call block receives the outcome.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/manager_rules.h"

namespace host {

void install_manager_rules();
// True when the call may run (hooks allowed, entries resolved, no kill switch); why not otherwise
bool manager_rules_ready(std::string* why);
// Status lines for the Status tab (game_calls_status appends them)
std::vector<std::string> manager_rules_status();
turbo::ManagerRulesFns manager_rules_fns();
// The managers the capture hooks last saw (0 = none yet)
uint64_t com_seen();
uint64_t jsm_seen();
bool unsackable_on();

// Run / queue a sub-op. `seq` tags the mailbox call block result.
turbo::ManagerRulesResult manager_rules_request(int64_t sub, uint64_t addr, int64_t value, int64_t team, int32_t seq);

}  // namespace host
