// FC 27 LE Turbo GUI - Career > Changes and undo (GUI revamp plan step 9, TURBO_GUI_REVAMP_PLAN.md sections 5.3 and 5.4).
//
// One place answers "what did I change, and how do I take it back":
//   - ONE global "Undo: <what>" (draw_status_undo, for the status bar; undo_last for a caller of its own) that takes back the newest
//     change a feature can undo by itself;
//   - the Changes list (rows()): what Turbo saw change while it was open, each row with the way back: Undo (the global button can
//     do it), Open (the feature's own page holds the Undo, a button there), or Never (it cannot be undone, and the row says so);
//   - what Turbo puts back at every career load (the kept edits), each with "Stop doing this" behind a confirm.
// Nothing is rewritten here: the player undo is App::undo, the kit undo is undo_kit_import, the tactic undo stays a button of the
// Tactic presets page. The list is built by poll(), which reads only in-memory state (and, once a second, whether a kit of the club
// was imported); the wiring should call poll(app) from App::tick so a change made on any page is seen when it happens.
//
// Confirms for actions that have neither an Undo nor a confirm today (plan 5.4): ask() is a reusable modal; note_irreversible()
// puts the action in the list as "Can't be undone". The pages that own those actions call them (this file does not edit them).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ui_shell.h"

namespace turbo {

class App;

namespace changes {

// How a row is taken back
enum class How {
    Undo,   // the global Undo button can do it (the feature's own undo function)
    Open,   // the feature's page has the button; the row opens that page
    Never   // cannot be undone: the row says so
};
enum class Source { PlayerEdit, KitImport, TacticLoad, Callname, Other };

struct Change {
    uint64_t seq = 0;       // grows with every row; stable while the row lives
    std::string when;       // "14:07", or "earlier" when Turbo saw it on its first look
    std::string what;       // "Pace for Jamie Vardy"
    std::string note;       // Never: why not; Open: what to press there
    Source source = Source::Other;
    How how = How::Never;
    int64_t id = 0;         // PlayerEdit: player id; KitImport: club id
    int slot = -1;          // KitImport: the kit slot (kits::Slot as int)
    int count = 1;          // PlayerEdit: how many undo steps this change added
    int bulk = 0;           // PlayerEdit: bulk-undo entries it added (removed with it, so the player page does not offer them again)
    shell::Page page = shell::Page::None;  // Open: where the Undo is
};

// ---- the list (newest first)
const std::vector<Change>& rows();
size_t count();  // rows in the list: the number of "Changes (n)"
// Looks at the features' undo state and adds / drops rows. Cheap and safe to call every frame. force: do not wait for the
// once-a-second kit look (tests).
void poll(App& app, bool force = false);
// Forget the list and its baselines (tests, a new simulated game)
void reset_for_tests();

// ---- undo
// Can the row be taken back right now? Undo rows only; a player's older change waits for his newer one.
bool can_undo_row(const App& app, const Change& c);
// The newest row the global Undo takes back (nullptr = none)
const Change* last_undoable(const App& app);
// "Undo: Pace for Jamie V..." ("Nothing to undo" when there is none). The change is cut to 22 characters (a label is 28 at most, plan R3);
// the full text is the button's tooltip.
std::string undo_label(const App& app);
// Take one row back through its feature's own undo; false with the reason in *msg. The row leaves the list when it worked.
bool undo_row(App& app, uint64_t seq, std::string* msg = nullptr);
bool undo_last(App& app, std::string* msg = nullptr);

// ---- rows for actions that cannot be undone (called by the page that owns the action, after its confirm)
void note_irreversible(App& app, const std::string& what, const std::string& why);

// ---- a confirm for an action that has no Undo (plan 5.2, "Confirm (no undo)"): title "<Verb> <who>?", the body says what
// happens and that the Undo button cannot bring it back. open() on the click, run() every frame after it; run() is true on the
// frame the person confirms. The id must be stable and unique on its page.
struct Ask {
    std::string id;     // "release_42"
    std::string title;  // "Release Jamie Vardy?"
    std::string body;   // "He becomes a free agent. The Undo button cannot bring him back."
    std::string ok;     // "Release"
};
void ask_open(const Ask& a);
bool ask_run(const Ask& a);

// ---- drawing
// The small button for the status bar: "Undo: <what>" or a disabled "Nothing to undo". Polls first.
void draw_status_undo(App& app);
// The page: Career > Changes and undo. Registered by the wiring (this file does not take over Page::ChangesUndo on its own:
// the old page is still registered by ui_tools.cpp).
void draw_changes_page(App& app);

}  // namespace changes
}  // namespace turbo
