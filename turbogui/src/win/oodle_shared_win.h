// FC 27 LE Turbo GUI - the ONE detour on the game's Oodle decompressor, Windows host (core/oodle_dispatch.h).
//
// OodleLZ_Decompress of oo2core_9_win64.dll (the game folder's own copy) carries one MinHook detour for the whole process. Two features
// register a handler in it: the 3D kit swap (slot Kits, kit_swap_win.cpp) and the attribulator EBX patch (slot Attrib, attrib_ebx_win.cpp).
// Whichever needs the hook first installs it; the other attaches to it. Each keeps its own way in:
//   * attrib: oodle_shared_install_early(), from its DllMain thread, with the checks it always had (module in the game folder, export
//     present, no jump at the entry, MinHook initialised here when nobody did);
//   * 3D kits: oodle_shared_install_for_kits(), through game_hooks' install_game_dll_export_hook("kit_oodle", ...) (hooks allowed, per-hook
//     kill switch, the export's bytes equal the DLL file's), or, when the attrib patch installed the detour first, through
//     attach_game_hook_record("kit_oodle", ...) with the same refusals. Either way "kit_oodle" is a hook record: hook_kit_oodle_off.txt,
//     game_hooks_off.txt and the Status tab keep working, and they gate the kit handler only (never the attrib handler).
// Who installs is decided by a flag under one mutex (never by reading the entry's bytes: each installer would refuse the other's jump).
// The detour never takes that mutex. Turbo's own reads of the game's blocks (both features' OodleFn) call oodle_shared_decompress(): the
// ORIGINAL (the trampoline), never the hooked export, so a block Turbo reads is never rewritten by the detour.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/oodle_dispatch.h"

namespace host {

// Register / retire a feature's handler (slot = turbo::oodle::kSlotKits / kSlotAttrib). false: the slot was used before.
bool oodle_shared_register(int slot, const turbo::oodle::Handler& h);
void oodle_shared_retire(int slot);

// Attrib's early install. Waits (up to 120 s, outside the lock) for the game's own copy of the DLL. true = the detour is there (`how` says
// who installed it); false = `why`. Registering the handler first is the caller's job (the detour may run at once).
bool oodle_shared_install_early(std::string* how, std::string* why);
// The 3D kit swap's install (game_hooks): `how` as above. false = `why` (hooks off, kill switch, bytes differ, MinHook failed ...).
bool oodle_shared_install_for_kits(std::string* how, std::string* why);

// The game's original decompress on one block (Turbo's own reads). The trampoline once a detour exists, else the DLL's export.
bool oodle_shared_decompress(const uint8_t* src, size_t n, uint8_t* dst, size_t dn);

bool oodle_shared_installed();
bool oodle_shared_uses_minhook();  // MinHook was initialised by (or the detour lives in) this module: the overlay's failure path must not tear it down
std::string oodle_shared_installer();  // "attrib EBX patch" / "3D kit swap" / "" before any install
// One line for the logs: who installed it, which handlers are registered, per-slot recognised / overwritten / skipped / errors
std::string oodle_shared_summary();
// Counters of one slot (tests and logs)
turbo::oodle::SlotStats oodle_shared_stats(int slot);

}  // namespace host
