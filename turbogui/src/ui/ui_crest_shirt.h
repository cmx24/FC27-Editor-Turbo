// FC 27 LE Turbo GUI - "Use my crest on the shirt" (2.20 K2, brief 2.20-K2-crest-lua). One checkbox per club on the Kits page
// (ui_kits_page.cpp more_section): the club goes on / off the shirt-crest list turbo_output\kits\crest2d.json (core/faces3d.h
// CrestList). At its next start the game's player_kit.lua is built with that list, and the listed club's jersey and shorts crest is its
// 2D crest (the crest file Turbo's crest editor writes). Global: every career and Kick Off. One step of undo (the list as it was).
#pragma once
#include <cstdint>
#include <string>

namespace turbo {

class App;

// The checkbox, its plain-words note and the Undo button (nothing drawn for teamid <= 0)
void draw_crest_shirt_toggle(App& app, int64_t teamid);

// The checkbox's work, without ImGui (native tests)
namespace crest_shirt {
bool is_on(const std::string& turbo_output_utf8, int64_t teamid);
// Puts the club on (on = true) or off the list; the list as it was is kept for undo(). false + message when nothing was written.
bool set(const std::string& turbo_output_utf8, int64_t teamid, bool on, std::string& message);
bool can_undo();
// Writes the list back as it was before the last set() (one step); false when there is nothing to undo or the write failed
bool undo(const std::string& turbo_output_utf8, std::string& message);
void reset_for_tests();  // forgets the undo step and the cached list
}  // namespace crest_shirt

}  // namespace turbo
