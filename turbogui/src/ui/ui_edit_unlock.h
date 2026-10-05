// FC 27 LE Turbo GUI - "Game editors" (Turbo Tools tab): unlock FC 27's own edit screens (core/edit_unlock.h).
#pragma once

namespace turbo {

class App;
namespace eu {
class EditUnlock;
struct Options;
}  // namespace eu

// App::tick, every second: while the switch is on (the default) the game's originals are collected and the unlocked
// files are written again whenever an original or a switch changed
void edit_unlock_tick(App& app);
// Turbo Tools tab: the one "Game editors" section (file override, career settings, in-memory fallback's switch and status)
void draw_game_editors(App& app);
// The switches (eu::Options, gui_settings.json "edit_unlock"; stage 1 and the in-memory fallback on, career settings and
// experiments off by default)
bool edit_unlock_enabled(const App& app);
eu::Options edit_unlock_options(const App& app);
// The service for this App (tests)
eu::EditUnlock& edit_unlock_service(App& app);
// Status line and toast when this App's pass wrote a file: Live Editor read mods\legacy when the game started (the early
// pass, eu::early_pass in Turbo.dll's start thread, writes before that), so the new file needs one game restart
extern const char* const kEditUnlockRestartNote;
bool edit_unlock_restart_needed(App& app);

}  // namespace turbo
