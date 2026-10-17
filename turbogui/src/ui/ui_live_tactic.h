// FC 27 LE Turbo GUI - Teams > Tactics > My team: your club's LIVE tactic (tactics channel E, core/team_sheet.h). A summary line of the
// live record (always, read-only), the prompt after Team Management when the game replaced Turbo's values (one-click Re-apply, never
// automatic), and the card "Edit the live tactic (channel E)": badges (written: live team sheet, seen: untested), the safety line (Yes in the
// game's team-sheet prompt autosaves), the opt-in, the gates G1..G6 with the first reason, the read-out, the editor (Defensive Approach /
// Line Height / Build-Up Style in the game's words, the approach band-consistent with the line, role id / focus per slot as raw numbers),
// Apply (asks first; the live record and its cm_mentalities row in one job), Undo, and what the last job, the row, the read-back 5 s later
// and the kick-off check said. The undo file is turbo_output\channel_e_undo.json.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "core/team_sheet.h"

namespace turbo {

class App;

// What the card drew in its last frame (tests read it here)
struct LiveTacticState {
    bool summary_shown = false;           // the summary line was drawn
    std::string summary;                  // "Your club's live tactic (read now): \"433\", depth 25, build-up 0"
    bool card_open = false;               // the card's header is open
    std::string written, seen, tag;       // the badges ("live team sheet", "untested") and the tag line
    bool opt_in = false;
    std::string status;                   // "ready" or the first failing gate
    std::vector<std::string> gates;       // "G1 switches: ok" / "G2 career hub: ..."
    std::string locate;                   // the service's locate status
    bool read_ok = false;
    std::string read_error;
    std::string readout;                  // the record line
    std::vector<std::string> slots;       // one line per slot
    std::string not_written;              // "Not written in 2.2: preset 1, formation 27"
    std::vector<std::string> plan;        // what Apply will write
    std::string plan_error;               // G6
    bool can_apply = false, can_undo = false, busy = false;
    bool confirm_open = false;
    std::vector<std::string> confirm_lines;
    std::string last, mirror, kickoff, undo_state;
    std::vector<std::string> overwritten; // "Edit Tactic overwrote the change: depth 75 -> 25"
    int edit_slot = 0;
    std::string allow;                    // the role allow-list's status line
    std::vector<int> role_choices, focus_choices;  // the role ids / focuses the editor offers for the edited slot
    std::vector<int> approach_choices, build_up_choices;  // the Defensive Approach / Build-Up Style values offered (the known ones)
    std::string line_hint;                // the Line Height's band against the chosen approach
    std::string safety;                   // the safety line ("... AUTOSAVES your career.")
    std::string replaced;                 // after Team Management: "the game replaced your tactic (LH 50 / Balanced / Balanced). Re-apply?"
    bool can_reapply = false;             // the one-click Re-apply button is enabled
    std::string other_note;               // "the active tactic is another one now (...)" (no Re-apply)
    std::string match_status;             // 2.5: "The next match will use these values: Turbo's change is in place."
    bool details_open = false;            // 2.5: the "More" node (roles per slot, the live record, details) is open
    std::string edit_tactic_badge;        // WP-B2 branch (c) only: "Edit Tactic shows these values after the next career load" (off: "")
    bool bench_keep_on = false;           // 2.15: turbo_output\bench_keep_on.txt is there (the toggle's state)
    std::string bench_keep;               // 2.15: the bench keep line ("on: waiting for Team Management", "off (...)", "on: put back: ...")
};
const LiveTacticState& live_tactic_state();

// The summary line and the card (Tactics tab, My team scope). Never throws.
void draw_live_tactic(App& app);
// Every frame (App::tick): job results (undo file, the cm_mentalities row), the re-read after Team Management, the read-back 5 s after a
// write and when the window is shown again, the kick-off check once a match is set up after a write. Never throws.
void live_tactic_tick(App& app);
// Tests: forget the card's state (a new simulated game)
void live_tactic_reset();
// Tests: the wall clock (seconds) the Team Management watch uses (null = the system clock; Turbo's QPC clock runs ~3x in game)
// Tests: open / close the "More" node (closed in the app until the person opens it)
void live_tactic_set_details_open(bool open);

// 2.12 tactic presets (ui_tactic_presets.cpp): the panel reuses the card's own read, gates and guarded game-thread job (nothing here writes
// by another route). All of them are safe to call every frame except live_tactic_submit.
// One read of the live record ("" in `why` = read). false: why it cannot be read (no service, not in the hub, gates of the locate step ...)
bool live_tactic_read(App& app, tsheet::LiveTactic& out, std::string& why);
// "" = a write of `req` is allowed right now (every gate G1..G6 passes, the plan changes something), else the first failing gate's text
std::string live_tactic_gate(App& app, const tsheet::Request& req);
// The card's Apply without its confirmation (the panel asks first): the undo file, the live record and its cm_mentalities row in one job.
// true = queued; false = refused (`why`; the notice is raised too)
bool live_tactic_submit(App& app, const tsheet::Request& req, std::string& why);
// The card's Undo (channel_e_undo.json): puts back every value Turbo wrote in this career load, from the history (the game's role allow-list does
// not apply to values the game itself held). true = queued
bool live_tactic_undo(App& app, std::string& why);
bool live_tactic_undo_ready(App& app);  // the undo file holds a write of this career load that can be put back
bool live_tactic_busy();  // a job is in flight
struct LiveJobResult {
    long long seq = 0;  // counts the results that came back (never reset)
    bool ok = false, written = false, undo = false;
    std::string line;
};
LiveJobResult live_tactic_result();  // the latest job's result
bool live_tactic_opt_in(App& app);   // turbo_output\call_team_sheet_on.txt
void live_tactic_set_opt_in(App& app, bool on);
void live_tactic_test_wall_clock(std::function<double()> wall);

}  // namespace turbo
