// FC 27 LE Turbo GUI - Lua loader hook, Windows host (core/lua_loader.h has the design and the file switches).
//
//   * start_lua_loader_early(): called from DllMain (only creates a thread). Without turbo_output\lua_loader_on.txt the thread
//     returns at once: nothing scanned, hooked or logged. With it: honours turbo_gui_disable.txt / TURBO_GUI_DISABLE=1,
//     game_hooks_off.txt / TURBO_GUI_NO_GAME_HOOKS=1, hook_lua_load_off.txt and its own crash flag lua_loader_start.flag;
//     then scans FC27.exe's executable sections for lua_load every 250 ms (the code may still be encrypted while the game
//     boots) for up to 60 s (TURBO_LUA_LOADER_WAIT_MS), and installs only when lua_load, its cross-check (luaL_loadstring's
//     call) agree and lua_load does not start with a jump. Signature missing = "lua_loader: signature not found (build X)",
//     nothing installed.
//   * MinHook: the loader may initialise MinHook long before the overlay does; the overlay then accepts
//     MH_ERROR_ALREADY_INITIALIZED and, on a failed start, removes only its own hooks (lua_loader_uses_minhook()).
//   * faces3d_on.txt (GUI, turbo::lualoader::set_faces3d_opt_in) = opt-in + SUBSTITUTE; faces3d_off.txt = kill switch. The stock
//     player_kit.lua is captured from the verified one-shot reader on every compile; the first capture is cached by the watcher
//     thread (LOG-ONLY, or md5 == the row's) in turbo_output\faces3d\stock_player_kit.cache.
#pragma once
#include <string>
#include <vector>

#include "core/lua_loader.h"

namespace host {

void start_lua_loader_early();
// True once the loader has initialised MinHook or created a hook in it (never true without the opt-in file)
bool lua_loader_uses_minhook();
// Status tab lines; empty when the opt-in file was absent at start (the Status tab is then unchanged)
std::vector<std::string> lua_loader_status();
// For the Status tab / Appearance page (all fields default when not opted in)
turbo::lualoader::Faces3dStatus faces3d_status();

}  // namespace host
