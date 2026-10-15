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
#pragma once
#include <string>
#include <vector>

namespace host {

void start_lua_loader_early();
// True once the loader has initialised MinHook or created a hook in it (never true without the opt-in file)
bool lua_loader_uses_minhook();
// Status tab lines; empty when the opt-in file was absent at start (the Status tab is then unchanged)
std::vector<std::string> lua_loader_status();

}  // namespace host
