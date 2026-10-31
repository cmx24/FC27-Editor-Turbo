// FC 27 LE Turbo GUI - Career tools pages (GUI revamp 2.13, track T5; GUI_REVAMP_SPEC.md 1.1 and 4).
// The pages register themselves with the shell (ui_shell.h TURBO_REGISTER_PAGE) from ui_tools.cpp, ui_cmtracker.cpp
// and ui_competitions.cpp; this header only names what the tests and the legacy tab need.
#pragma once
#include <cstddef>

#include "ui_shell.h"

namespace turbo {

class App;

// Draws one of the pages that live in ui_tools.cpp (Transfers, JobOffer, ChangesUndo) exactly as the shell does. False for any other page.
bool draw_career_tools_page(App& app, shell::Page p);

// Changes & undo: the "Undo (n)" number: the rows of Turbo's re-apply log (kept kit edits, kept callnames and kept player
// edits, turbo_output\reapply_edits.json). The status bar of the shell shows the same number (ui_reapply.cpp).
size_t changes_undo_count(const App& app);
// The page's lead lines: "Undo (n)" with the three counts, what Forget does, the last re-apply summary
void changes_undo_summary(App& app);

// The career database copy of the league tables (ui_competitions.cpp; the legacy Competitions panel draws it)
void draw_career_database_copy(App& app);

// Career tools > CMTracker (ui_cmtracker.cpp): the library page; the same body as the "From CMTracker..." dialog of the player page
void draw_cmtracker_page(App& app);

// Pages whose body is drawn by the Squad track (T2): the transfers panel (Teams > Transfers + Mass actions) and the job offer
// panel. Weak references: until T2 exports them the pages show one muted line instead of failing to link; once it does,
// the real functions win with no change here (T2: define them in namespace turbo, void f(App&)).
#if defined(__GNUC__)
#define TURBO_WEAK __attribute__((weak))
#else
#define TURBO_WEAK
#endif
void draw_transfers(App& app) TURBO_WEAK;
void draw_job_offer(App& app) TURBO_WEAK;

}  // namespace turbo
