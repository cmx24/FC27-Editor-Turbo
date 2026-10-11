// FC 27 LE Turbo GUI - club chooser: one text box for a club's name or ID, with a dropdown of the matching clubs under it
// (Players > Transfer / Loan..., the Import / Clone / Create / CMTracker dialogs, Turbo Tools). Defined in ui_club_search.cpp.
#pragma once
#include <cstdint>

namespace turbo {

class App;

// A text box that takes a club's ID or part of its name. While it is typed in, a dropdown under it lists the matching
// clubs (name, ID, league; national teams marked) best first: a number lists the club with exactly that ID, then the
// IDs starting with it; words list the names starting with them, then the names containing them. A click on a row picks
// that club; Up / Down move the highlighted row and Enter picks it, and so does leaving the box after typing (Escape
// keeps the club chosen before). A number no club has is kept, so the caller can say why it is refused. When idle the
// box shows the chosen club's name and the text after it its ID and league ("national team" for one); teamid 0 = none,
// the box then shows `hint` (default "club name or ID"). Returns true when the choice changed.
bool club_search_box(App& app, const char* label, int64_t& teamid, float width, const char* hint = nullptr);
bool club_search_box(App& app, const char* label, int& teamid, float width, const char* hint = nullptr);

}  // namespace turbo
