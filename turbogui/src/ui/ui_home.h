// FC 27 LE Turbo GUI - the 3.0 Home page (brief 3.0-shell track A): the page the app opens on, the only page of the Home area.
//
//   (a) the title and a status chip: your club, career loaded / no career, the in-game date when the game reports one
//   (b) My squad: the sortable squad table of your club (squad_table, ui_team_pages.h) while a career is loaded, else one muted line
//   (c) Presets: My team preset (Club > Squad), Tactic presets (Tactics > Presets), Game sliders, Season pack (Career)
//   (d) Recent changes: the count of the Changes list (ui_changes.h), its Undo button, and a button to Career > Changes & undo
//   (e) one tile per area (Players, Looks, Club, Tactics, Career, Settings): a line of what is there, a click opens its first page
//
// Cheap per frame: no database scan beyond squad_table (it walks the club's links, as Club > Squad does). Registered from ui_home.cpp.
#pragma once
#include <cstddef>
#include <cstdint>

namespace turbo {

class App;

void draw_home(App& app);

// What the last draw_home call drew (headers and plain text are not ImGui items, so the tests read it here)
struct HomeState {
    bool status_chip = false;   // (a)
    bool squad = false;         // (b) the section header was drawn (the table itself only in a career)
    bool squad_table = false;   //     squad_table was called (career loaded, a user club)
    bool presets = false;       // (c)
    bool recent = false;        // (d)
    int tiles = 0;              // (e) tiles drawn (6)
    int sections = 0;           // sections drawn this frame (5 when everything is there)
    int64_t club = 0;           // the user's club the chip and the table show (0 = none)
    bool in_career = false;
    size_t changes = 0;         // the count shown under Recent changes
};
const HomeState& home_state();

}  // namespace turbo
