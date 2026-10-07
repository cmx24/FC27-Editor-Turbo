// FC 27 LE Turbo GUI - what the Tactics tab reads from the career database (Turbo 2.0, docs/TURBO_2_0_PLAN.md section 3).
//
// Plain reads through the Database layer (core/t3db.h): no ImGui, no writes, no clock. Two things:
//   1. The saved formation of a team, as a core/tactics.h Formation (read_team_formation): the formations / formationoffsets rows of the
//      formation the team uses, checked by make_formation. When anything is missing or does not look like a formation, the caller gets a
//      built-in shape and a plain reason, never an error: the pitch always has something honest to draw.
//   2. The rows of the mentality tables that belong to a team (team_rows), so the DB sliders of the team scope can be written.
// Nothing here knows whether the game reads these tables when a match starts: the UI says "saved data, in-game effect unverified".
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "t3db.h"
#include "tactics.h"

namespace turbo {

// A pitch label for a game position code (core/model.h position_name): RCB and LCB are both "CB" on the pitch, RDM / LDM "CDM", RCM / LCM
// "CM", RAM / LAM "CAM", RS / LS "ST". "?" for a code with no name.
std::string pitch_label_for_position(int code);

struct TeamFormation {
    Formation formation;   // always usable (from_db tells where it came from)
    bool saved = false;    // read from the database
    std::string source;    // "saved formation 10 (the team's style link)"; empty when not saved
    std::string note;      // plain reason a built-in shape is shown instead; empty when saved
};

// The formation of a team: the id comes from teamformationteamstylelinks, else from the sourceformationid of the team's active tactic
// (cm_mentalities, then mentalities), else from a formations row that carries the team's id. The positions and offsets come from that
// formations row; offsets that mean nothing there (all zero) are taken from formationoffsets. A y axis that runs the other way (the
// goalkeeper near the far goal line) is turned round. Never throws.
TeamFormation read_team_formation(Database& db, int64_t teamid);
// One formations row by formation id. False (with why) leaves `out` untouched.
bool read_formation_by_id(Database& db, int64_t formationid, Formation& out, std::string& source, std::string& why);

// The rows of a table (mentalities, cm_mentalities ...) whose teamid is this team: their record addresses, and which one is "the" row.
struct TeamRows {
    std::vector<uint64_t> recs;  // every valid record of the team, table order
    size_t chosen = 0;           // index into recs: the active tactic (activetactic == 1), else the first
    bool has_active = false;     // the chosen row is flagged as the active tactic
    bool key_missing = false;    // the table has no teamid field
};
TeamRows team_rows(Database& db, const Table& t, int64_t teamid);

}  // namespace turbo
