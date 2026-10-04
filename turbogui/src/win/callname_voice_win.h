// FC 27 LE Turbo GUI - voice swaps inside FC27.exe (the Windows host of core/callname_voice.h; docs/callnames.md
// section 12, turbo_dev/research/real_callnames_plan.md sections 5.1-5.3).
//
//   * install_callname_voice(): from install_commentary_audio, next to install_speech_log(). Installs two guarded hooks,
//     only when the build is known and all 5 signatures resolved (the 2 prologues and the 3 layout guards):
//       callname_voice   on speech_query_preprocess (Preprocess 0x1414A90C8): the original first, then
//                        voice::process_query on the published table (Turbo's own audit queries left alone);
//       callname_kickoff on commentary_get_callname (GetCallname 0x14294A0F4): the original first, then
//                        voice::kickoff_override.
//     A missing signature or a failed install leaves both detours pass-through ("game build" / "hooks off").
//   * the detours read atomics only (no lock, no allocation, no file check, no game call): the published table
//     (std::atomic<const Table*>, old tables never freed), game_hook_live() and the cached switches below.
//   * switches, refreshed by Service::refresh_switches() (GUI tick, at most every 2 s) and on publish:
//     turbo_output\callname_voice_off.txt (the feature), hook_callname_voice_off.txt / hook_callname_kickoff_off.txt,
//     game_hooks_off.txt, env TURBO_GUI_NO_GAME_HOOKS=1 (game_hooks' own).
//   * observe mode: turbo_output\callname_voice_log_on.txt -> both detours fill a lock-free ring of 4,096 entries; a
//     background thread appends it to turbo_output\callname_voice_log.txt every 5 s (scripts/callname_voice_log.py
//     reads it). The file is no longer written above 64 MB.
#pragma once
#include <string>
#include <vector>

namespace turbo {
namespace voice {
class Service;
}
}  // namespace turbo

namespace host {

// Installs both hooks when every signature resolved (once; logged). Never throws.
void install_callname_voice();
// The service the App talks to (App::voice_service); never null, "off" until both hooks are installed
turbo::voice::Service* callname_voice_service();
// Status lines for the Status tab (appended to HookReport::calls)
std::vector<std::string> callname_voice_status();

}  // namespace host
