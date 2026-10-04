// FC 27 LE Turbo GUI - live team names inside FC27.exe (the Windows host of core/teamname_override.h).
//
//   * install_team_names(): after install_game_hooks(). Installs ONE guarded hook, "team_names", on loc_lookup
//     (LocImpl::Lookup 0x1421E2120: int Lookup(this, eastl::string* out, const char* key, int mode), every localized
//     string's lookup, ABOVE Live Editor's own hook on StrTab::GetString 0x140B1C034), only when the build is known and
//     every signature and layout guard resolved:
//       loc_lookup               the hooked function (its prologue = the argument registers the detour relies on);
//       eastl_string_assign_cstr the game's eastl::string::assign(const char*) (0x1406C04D4), the only way `out` is
//                                written;
//       loc_lookup_out_assign    layout guard: Lookup's own "out = ..." call site, which must resolve to that very
//                                function and lie inside Lookup.
//     The entry must not carry another module's inline hook (refused and reported otherwise).
//   * the detour: the original first (the game and Live Editor answer as before), then, for a "TeamName[_AbbrN]_<id>"
//     key of a club in the published table, `out` = Turbo's name through the game's own assign, return 1 (found). The
//     key is matched before the call (it may live in `out`). Atomics only: no lock, no allocation, no file check, no
//     logging; the only game call is the assign. Tables are never freed (SnapshotSlot owned by a never-destroyed host).
//   * switches, cached and re-read every 2 s by the GUI tick (Service::refresh_switches):
//     turbo_output\team_names_hook_off.txt (this feature), hook_team_names_off.txt / game_hooks_off.txt /
//     TURBO_GUI_NO_GAME_HOOKS=1 (game_hooks' own). Off = the detour only calls the original; Turbo still writes Live Editor's CSV, so the name
//     shows after Live Editor's next start.
//   * report only: whether Live Editor hooks loc_strtab_get (StrTab::GetString), never touched.
#pragma once
#include <string>
#include <vector>

namespace turbo {
namespace tnames {
class Service;
}
}  // namespace turbo

namespace host {

// Installs the hook when every signature and guard resolved (once; logged). Never throws.
void install_team_names();
// The service the App talks to (App::team_names_service); never null, "off" until the hook is installed
turbo::tnames::Service* team_names_service();
// Status lines for the Status tab (appended to HookReport::calls)
std::vector<std::string> team_names_status();

}  // namespace host
