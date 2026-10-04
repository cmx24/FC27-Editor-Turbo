// FC 27 LE Turbo GUI - the game editors' in-memory fallback inside FC27.exe: the post-hook on the editor config loader
// (core/edit_unlock_hook.h, research/edit_unlock_plan.md section 5)
#pragma once
#include <string>
#include <vector>

#include "core/edit_unlock_hook.h"

namespace host {

// Called from start_overlay after install_game_hooks: installs the "edit_unlock" hook only when the loader signature,
// every layout guard and the four keep-list attribute ids resolved (each id's name string checked in the game's own
// name -> id table). Never throws; everything is logged.
void install_edit_unlock_hook();
// What the GUI talks to (never nullptr; reports "off (...)" when not installed)
turbo::edit_unlock::HookService* edit_unlock_hook_service();
// Status tab lines
std::vector<std::string> edit_unlock_hook_status();

}  // namespace host
