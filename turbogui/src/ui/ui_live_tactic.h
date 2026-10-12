// FC 27 LE Turbo GUI - Teams > Tactics > My team: your club's LIVE tactic (tactics channel E, core/team_sheet.h). A summary line of the
// live record (always, read-only) and the card "Edit the live tactic (channel E)": badges (written: live team sheet, seen: untested), the
// safety line (Yes in the game's team-sheet prompt autosaves), the opt-in, the gates G1..G6 with the first reason, the read-out with Edit
// Tactic's working copies, the editor (Defensive Approach / Line Height / Build-Up Style in the game's words, role id / focus per slot as
// raw numbers), Apply (asks first), Undo, and what the last job, the cm_mentalities mirror, the read-back 5 s later and the kick-off check
// said. The undo file is turbo_output\channel_e_undo.json.
#pragma once
#include <string>
#include <vector>

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
    std::vector<std::string> copies;      // one line per Edit Tactic working copy
};
const LiveTacticState& live_tactic_state();

// The summary line and the card (Tactics tab, My team scope). Never throws.
void draw_live_tactic(App& app);
// Every frame (App::tick): job results (undo file, the cm_mentalities mirror), the read-back 5 s after a write and when the window is
// shown again, the kick-off check once a match is set up after a write. Never throws.
void live_tactic_tick(App& app);
// Tests: forget the card's state (a new simulated game)
void live_tactic_reset();

}  // namespace turbo
