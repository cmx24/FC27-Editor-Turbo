// FC 27 LE Turbo GUI - the spoken set asked from the game's audio service, inside FC27.exe (the Windows host of
// core/commentary_audio.h; docs/callnames.md section 5).
//
//   * install_commentary_audio(app): after install_game_hooks(). Resolves the signature set (fn_signatures()) and gives
//     the App its caudio::Service. Nothing is hooked: the feature only calls game functions, on the game thread.
//   * a build (Service::request) is a chain of jobs on the game-thread dispatcher (run_on_game_thread): every job
//     runs one Build::step (one batch of ids through the game's FilterNames, or one batch of player ids through
//     Turbo-built SpeechQuerys), then queues the next step for the next frame; the batch adapts to the time a step
//     took (BuildRequest::tick_budget). The finished result waits for the App's poll (render thread).
//   * kill switches: turbo_output\call_commentary_audio_off.txt (this call), plus every game-hook switch (a build that
//     is not in the signature table or game_hooks_off.txt turns the call off).
//   * every step runs inside HOOK_BODY (C++ exceptions swallowed and counted) and every game pointer is read through
//     ProcessMemory with the chain checks of core/commentary_audio.h before the game is called.
#pragma once
#include <string>
#include <vector>

namespace turbo {
class App;
}

namespace host {

void install_commentary_audio(turbo::App& app);
// Status lines for the Status tab (appended to HookReport::calls)
std::vector<std::string> commentary_audio_status();

}  // namespace host
