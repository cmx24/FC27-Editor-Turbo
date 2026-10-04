// FC 27 LE Turbo GUI - team identity: name (live through Turbo's hook, plus the database and Live Editor's
// custom_team_names.csv), colours (teams and teamkits) and the club crest (custom legacy files in every size the game
// has). Panels live in ui_identity.cpp and are drawn inside the Teams tab; the helpers below are what the panels and
// the native tests call.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/image.h"
#include "core/legacy.h"
#include "core/t3db.h"

namespace turbo {

class App;

// ---- name: one form (Display name, Long name, Short name, Abbreviation) and one Save
void team_name_editor(App& app, const Table& t, uint64_t rec, int64_t teamid);
struct TeamNameSave {
    bool ok = false;       // saved: shown in the game now (live), or after Live Editor's next start (its file written)
    bool live = false;     // given to the game at once by Turbo's hook
    bool warning = false;  // saved, but something was not written (named in `line`)
    std::string line;      // the one line next to Save (also the toast)
    std::string detail;    // what was written where (GUI log)
};
// Save: the names are published to the hook first (the game's next lookup shows them) and kept in
// turbo_output\team_names.json, then teams.teamname (validated) and Live Editor's custom_team_names.csv (atomic, backed
// up; what shows after Live Editor's next start when the hook is off) are written. An empty short name / code is made
// from the name; the 10-letter form from the short name. Nothing is written when the name is empty or refused. The long
// name is kept in Turbo's store only (FC 27 has no string for it: the game never shows it); empty = the same as the name.
TeamNameSave save_team_name(App& app, int64_t teamid, const std::string& name, const std::string& short_name, const std::string& code,
                            const std::string& long_name = std::string());
// A short form of a name: whole when it fits, else cut at a word end when that keeps half the letters, else cut
std::string team_short_form(const std::string& name, size_t max_chars);
// The first three letters or digits of a name, upper case ("Everton Blues" -> "EVE")
std::string team_code_from(const std::string& name);
// What the Name tab drew in its last frame (plain text is not an ImGui item, so the tests read it here)
struct TeamNameTabState {
    int64_t teamid = 0;
    bool live = false;        // live names on
    std::string mode_line;    // "Live names are on: ..." / "Live names are off (<why>): ... after Live Editor's next start."
    std::string result_line;  // the last Save's line ("" before a Save)
    std::string screen_line;  // after a live Save: an open game screen shows the name once it is built again
    std::string long_line;    // the Long name box's note: where the game shows it (nowhere: FC 27 has no string for it)
};
const TeamNameTabState& team_name_tab_state();

// ---- colours
void team_colours_editor(App& app, const Table& t, uint64_t rec, int64_t teamid);
// <prefix>r / <prefix>g / <prefix>b fields of a record (teams.teamcolor1, teamkits.teamcolorprim, ...)
bool read_colour(App& app, const Table& t, uint64_t rec, const std::string& prefix, uint8_t rgb[3]);
// Validated writes of the three fields (each through Database::set); false with a reason when a field is missing or refused
bool write_colour(App& app, const Table& t, uint64_t rec, const std::string& prefix, const uint8_t rgb[3], std::string* msg);
// The kit types FC 27 uses (teamkits.teamkittypetechid)
const char* kit_type_name(int64_t type);

// ---- crest
void crest_editor(App& app, int64_t teamid);
struct CrestPlan {
    struct Item {
        CrestVariant variant;
        LegacyImages::State game;   // state of the game's own file (never the custom one)
        bool has_custom = false;
        DdsFormat fmt;              // of the game's file when game == Game and readable
        std::string note;           // why it cannot be written
    };
    std::vector<Item> items;
    int writable = 0;   // variants whose game file is known (format read)
    int waiting = 0;    // still being exported
    int missing = 0;    // the game has no such file
    bool ready() const { return waiting == 0; }
};
// Which crest files the game has for the team and in which format (asks the Lua side for the ones not exported yet)
CrestPlan crest_plan(App& app, int64_t teamid);
// Write the picture as a custom crest in EVERY variant the game has, each in the original file's size, pixel format
// and mip count. Backups go to turbo_output\crest_backups. Needs plan.ready(); msg says what was written or why not.
bool apply_crest(App& app, int64_t teamid, const Rgba& src, const Framing& framing, bool remove_bg, int tolerance, std::string* msg);
// Give `to` the crest files of `from` (byte copies per variant where both clubs have the file)
bool copy_crest(App& app, int64_t from_team, int64_t to_team, std::string* msg);
// Remove every custom crest file of the team (backups first)
bool remove_crest(App& app, int64_t teamid, std::string* msg);

}  // namespace turbo
