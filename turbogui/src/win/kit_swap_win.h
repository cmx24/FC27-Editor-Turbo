// FC 27 LE Turbo GUI - 3D kits on the pitch, Windows host (core/kit_swap.h): the detour on the game's Oodle decompressor.
//
//   * install_kit_swap(app): once, after install_game_hooks(). Creates the Runtime (state turbo_output\kits\swap_state.json), gives the
//     App its pointer, installs ONE guarded hook on OodleLZ_Decompress of oo2core_9_win64.dll (the DLL in the game folder that FC27.exe
//     imports; install_game_dll_export_hook checks the module path, the export and its bytes against the DLL file) and starts the
//     Runtime (a table is built in the background for every kit left active in the last session).
//   * 2.11: the detour is the process's ONE Oodle detour (win/oodle_shared_win.cpp, core/oodle_dispatch.h), shared with the attrib EBX patch.
//     Whoever needs it first installs it, the other attaches (for the kits: a "kit_oodle" hook record, same kill switches, same refusals).
//     This file registers the kit swap's handler in slot Kits, which runs first.
//   * the detour calls the original first, then, when it returned the full raw size and the kit hook is live, asks the Runtime whether the
//     compressed block is one of a swapped kit's blocks (sizes, first bytes, FNV-1a 64) and, in swap mode, overwrites the output with
//     the staged texture bytes. It reads atomics only: no lock, no allocation, no file access, no game call.
//   * kill switches: turbo_output\kits3d_off.txt (the feature, re-read every 2 s by a watcher thread), hook_kit_oodle_off.txt (the
//     hook, honoured at install and at run time), game_hooks_off.txt, env TURBO_GUI_NO_GAME_HOOKS=1.
#pragma once
#include <string>
#include <vector>

namespace turbo {
class App;
}

namespace host {

void install_kit_swap(turbo::App& app);
std::vector<std::string> kit_swap_status();  // lines for the Status tab

}  // namespace host
