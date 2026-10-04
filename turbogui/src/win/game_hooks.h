// FC 27 LE Turbo GUI - game-code hooks inside FC27.exe (the foundation for Turbo features that need game functions).
//
// What it does (docs/re/game_thread.md):
//   * finds FC27.exe (the process's main module), reads its PE header (TimeDateStamp, SizeOfImage -> build key, e.g.
//     "6AB9813C-211EF000") and its executable sections;
//   * loads the signature table turbo\signatures_<build>.json (next to Turbo.dll), else the built-in table for that
//     build (core/sigscan.cpp). A build in no table disables every game hook: a title update turns the hooks off
//     instead of crashing the game;
//   * scans every signature once (unique match required), logs the status of each to turbo_gui.log and publishes it to
//     the Status tab (game_hooks_report);
//   * installs guarded MinHook hooks: install_game_hook / install_game_hook_at. A detour must wrap its body in
//     HOOK_BODY(name, ...) (every C++ exception swallowed and counted) and must always call the original;
//   * kill switches: turbo_output\game_hooks_off.txt (everything), turbo_output\hook_<name>_off.txt (one hook; also
//     honoured at run time: the detour then only calls the original), env TURBO_GUI_NO_GAME_HOOKS=1;
//   * the game-thread dispatcher: run_on_game_thread(fn) queues work that runs on the game's own thread, promptly
//     (within a frame, a bounded batch per tick) through the game_tick hook on the game's per-frame MainLoop frame body
//     when that signature is known, otherwise on the next career-mode event: Turbo's Lua side calls the exported
//     turbo_game_pump() from its event handler, which runs on the thread that posts career events;
//   * prompt Lua commands: while the GUI has a mailbox command waiting (want_lua_pump), the tick hands the game's
//     hooked career-event entry (the dispatcher's Dispatch(id, event) on Live Editor v27.1.2, else the PostEvent shell)
//     a synthetic event whose dispatcher and event objects are Turbo's own no-op objects, so Live Editor's hook on that
//     entry runs Turbo's Lua handler (which polls the mailbox) and the game itself sees nothing. Only after a real
//     career-mode event ran Turbo's Lua side, only while one of those entries carries another module's inline hook
//     (jmp rel32 / jmp [rip] / movabs+jmp / push+ret / Live Editor's lea-push-movabs-ret stub), at most four times a
//     second (docs/re/game_thread.md section 4).
//
// Nothing here runs before start_overlay has initialised MinHook; hooks are enabled one at a time (MH_EnableHook on the
// target, never MH_ALL_HOOKS).
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

#include "core/sigscan.h"

namespace host {

// Called from start_overlay after install_input_shield(): locate the game, load the table, scan, install the
// dispatcher hook. Never throws; everything is logged.
void install_game_hooks();

// True when the build is known and the global kill switch is off (hooks may be installed)
bool game_hooks_allowed();
// Resolved address of a signature (0 when it is missing, ambiguous, skipped or the build is unknown)
uint64_t game_signature(const char* name);
// Install a hook on the function a signature resolved to. Refused (false, logged) when hooks are not allowed, the
// signature is not Found, the per-hook kill switch exists, or MinHook fails. `original` receives the trampoline.
bool install_game_hook(const char* name, const char* signature, void* detour, void** original);
// Install a hook at an address the caller resolved itself (must be inside FC27.exe's executable sections).
bool install_game_hook_at(const char* name, void* target, void* detour, void** original);
// Per-hook kill switch (turbo_output\hook_<name>_off.txt), cached and re-read every 2 s; also false when the global
// switch is on. A detour calls this first and, when it returns false, only calls the original.
bool game_hook_enabled(const char* name);
// Counts an exception caught inside a detour (HOOK_BODY uses it)
void game_hook_error(const char* name, const char* what);
void game_hook_called(const char* name);

// Queue work for the game's own thread. Returns what will run it: "hook" (next game_tick), "lua" (next career-mode
// event through turbo_game_pump), or "" when nothing can run it yet (kept queued; at most 256 jobs, the oldest dropped).
const char* run_on_game_thread(std::function<void()> fn);
// True while the game_tick hook is active (prompt dispatch)
bool game_thread_hooked();
// Thread id of the game thread the queue last ran on (0 = never)
uint32_t game_thread_id();
// The GUI has (or no longer has) a mailbox command waiting for Turbo's Lua side: the next ticks send the synthetic
// career event so Live Editor runs the Lua handler at once (overlay_dx12.cpp calls this every frame with App::busy()).
void want_lua_pump(bool wanted);
// Snapshot for the Status tab / log
turbo::HookReport game_hooks_report();
// Build key of the running game ("" before install_game_hooks)
std::string game_build_key();
// Image base of FC27.exe (0 before install_game_hooks located it)
uint64_t game_image_base();

// Body of every detour: swallow and count every C++ exception so a bug in Turbo never unwinds into game code.
// Usage:  HOOK_BODY("my_hook", { ...work... });  then call the original.
#define HOOK_BODY(name, ...)                                                      \
    do {                                                                          \
        try {                                                                     \
            __VA_ARGS__;                                                          \
        } catch (const std::exception& e_) {                                      \
            ::host::game_hook_error((name), e_.what());                           \
        } catch (...) {                                                           \
            ::host::game_hook_error((name), "unknown exception");                 \
        }                                                                         \
    } while (0)

}  // namespace host

// Lua-callable entry point (package.loadlib(turbo\Turbo.dll, "turbo_game_pump")): runs the queued game-thread work on
// the calling thread. Turbo's Lua side calls it from its career-mode event handler. Returns 0 (no Lua results) and
// never touches the Lua state.
extern "C" __declspec(dllexport) int turbo_game_pump(void* lua_state);
