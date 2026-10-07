// FC 27 LE Turbo GUI - the words the panels use for things that are not a database field: what a queued command is doing,
// what a match switch did, how a difficulty level reads. Platform independent (docs/TURBO_2_0_PLAN.md section 6: codes to
// descriptions). Field values go through core/field_labels.h describe().
#pragma once
#include <string>

namespace turbo::usertext {

// "Transfer" / "Loan" -> "Moving player": the verb for the label a command was sent with (the label itself stays in the log);
// a label with no entry is returned as it is
std::string pending_text(const std::string& label);

// The match switches (core/match_setup.h gv::known_vars): what a Set / Clear did, in words
//   "Injury frequency (your team) set to 30; applies at kick-off"
//   "Injury frequency (your team): the game decides again"
// `name` is the game variable's name; an unknown name is described by the name itself.
std::string match_var_set(const std::string& name, int value);
std::string match_var_clear(const std::string& name);

// The outcome of a Set / Clear that ran on the game thread (msetup::VarResult): the sentence above when it worked, else the
// refusal with the game variable's name replaced by the switch's label
std::string match_var_outcome(bool ok, bool cleared, const std::string& name, int value, const std::string& message);

// A match difficulty level: 0 and 5 are the ends the game names ("Beginner", "Legendary"); the levels between are shown as
// "level 2 of 5" because their names are not verified
std::string difficulty_text(int level);

// One line for the Status tab: hooks / signatures / channel in short ("All 12 game hooks active" / "2 of 12 ...")
std::string health_line(int signatures_found, int signatures_total, int hooks_active, int hooks_killed, bool dispatcher_ok,
                        bool lua_answering);

}  // namespace turbo::usertext
