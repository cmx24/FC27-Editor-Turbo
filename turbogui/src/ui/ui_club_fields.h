// FC 27 LE Turbo GUI - 2.20 K3: the club, crowd, stadium and kit fields Turbo did not draw (brief 2.20-K3-club-fields).
// The field list, the value sets and the names of the values are core/club_fields.h; this file draws them as plain-English pickers and
// writes them. Three sections, each called by ONE line from ui_identity.cpp:
//   Crowd & presentation  the club row (Club & Kits > Stadium & extras)
//   Home ground           the stadium row of the club's home ground (same page)
//   More kit options      one kit row (Kits, inside each kit of "Colours & numbers")
// Writes go through App::edit_field (live memory, range-checked and refused outside the game's own value set). The club and stadium
// columns are kept by the career save itself (core/reapply.h), so no reapply row is made; the kit columns are read again at every career
// load and a kit edit lasts until then (the page says so). Every write is read back and logged; a small undo list of its own puts the last
// change back (App's Undo last covers players and kit imports only).
#pragma once
#include <cstdint>
#include <string>

#include "core/t3db.h"

namespace turbo {

class App;

void draw_club_crowd_section(App& app, const Table& teams, uint64_t rec);
void draw_home_ground_section(App& app, int64_t teamid);
void draw_kit_extras_section(App& app, const Table& teamkits, uint64_t rec);

// One write of a field of core/club_fields.h. `team` names the club for the undo list and the log (a stadium row has none of its own; for
// teams and teamkits rows it is read from the row when 0). False with a reason in *msg when refused or not read back as written.
bool write_club_field(App& app, const Table& t, uint64_t rec, const std::string& field, int64_t value, std::string* msg, int64_t team = 0);
// Puts the last change of club_field_undo_top_team() back; false with a reason when there is none or the row is gone
bool undo_club_field(App& app, std::string* msg);
size_t club_field_undo_depth();
int64_t club_field_undo_top_team();  // 0 when the list is empty
void club_field_undo_clear();

// The home ground of a club: the stadium row its teamstadiumlinks row points to
struct HomeGround {
    bool found = false;
    uint64_t rec = 0;
    int64_t stadiumid = -1;
    std::string name;     // the stadium's name, else "Stadium N"
    int shared_with = 0;  // other clubs with the same home ground
    std::string why;      // when not found
};
HomeGround home_ground_of(App& app, int64_t teamid);

}  // namespace turbo
