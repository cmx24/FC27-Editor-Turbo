// FC 27 LE Turbo GUI - tactics channel E inside FC27.exe (core/team_sheet.h): the anchors from the signature table, the game's memory,
// Turbo's game-thread dispatcher (the game_tick hook only: a job that would wait for the next career event is refused), a background
// thread for the scans, the registry Find.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/team_sheet.h"

namespace host {

// After install_game_hooks(): resolves the anchors (build 6AC07E31-2145C000 only) and checks them against the vtable. Logs either way.
void install_team_sheet();
std::shared_ptr<turbo::tsheet::Service> team_sheet_service();
// Status tab lines ("team_sheet (channel E): ready | ..."
std::vector<std::string> team_sheet_status();

}  // namespace host
