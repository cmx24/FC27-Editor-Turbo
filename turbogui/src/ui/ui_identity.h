// FC 27 LE Turbo GUI - team identity: name (database + Live Editor's custom_team_names.csv), colours (teams and
// teamkits) and the club crest (custom legacy files in every size the game has). Panels live in ui_identity.cpp and
// are drawn inside the Teams tab; the helpers below are what the panels and the native tests call.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/image.h"
#include "core/legacy.h"
#include "core/t3db.h"

namespace turbo {

class App;

// ---- name
void team_name_editor(App& app, const Table& t, uint64_t rec, int64_t teamid);
// Write teams.teamname (validated) and the four Live Editor names (custom_team_names.csv, atomic, backed up).
// Empty abbreviations are derived from the full name. msg gets what happened.
bool apply_team_names(App& app, int64_t teamid, const std::string& full, const std::string& abbr3, const std::string& abbr10,
                      const std::string& abbr15, std::string* msg);

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
