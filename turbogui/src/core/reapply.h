// FC 27 LE Turbo GUI - edits the game forgets at every career load, kept by Turbo and written again.
//
// FC 27 reloads the tables teamkits and playernamemap from its base data every time a career loads (measured on
// 1.0.140.64835, 2026-10-04: kit colour edits and a playernamemap row edit were gone after save + reload, while
// teams.teamcolor1/2, players.lastnameid and editedplayernames were kept). So Turbo keeps
//   - the kit colours written through Teams > Colours (teamkits <prefix>r/g/b of the colour channels only, keyed by
//     teamtechid + teamkittypetechid + teamkitid: two kits of one type are two entries),
//   - the player-specific callnames written through Players > Callname (playernamemap, keyed by playerid),
// in <Live Editor>\turbo_output\reapply_edits.json and writes them again when it connects to a newly loaded career
// (App::reapply_stored_edits). This header is the store only: plain data and its JSON, no game memory.
//
// Since 1.2.5 it also keeps the Players editor's edits of a player's attributes, overall, potential, work rates, skill
// moves and weak foot (keyed by playerid + field, with the value written and the value it replaced). FC 27 saves the
// players table, but for the user's club the game's development plan outranks it (features/development.lua header),
// and every save gets a new file name (CmMgrC<timestamp>): an edit made before the plan was synced came back with its
// old value in every later save. Such a field is written again at a career load, whatever the save is called, but ONLY
// while it holds the value the edit replaced (the game put the old value back); a field the game moved since (its own
// growth or decline, another career's player) is left alone. On / off: "keep_players" in the file.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace turbo {

// The colours Teams > Colours writes in teamkits (each as <prefix>r, <prefix>g, <prefix>b): the only teamkits fields the
// store keeps and the re-apply writes
const std::vector<std::string>& kit_colour_prefixes();
// "teamcolorprimr" yes; "teamtechid", "teamcolorprimpercent", "TeamColor" no
bool kit_colour_field(const std::string& field);
// 2.3: the other teamkits fields a kit import (Teams > Kits, a FIFA Kit Creator settings.json) writes and the store
// keeps: teamcolorprimpercent, teamcolorsecpercent, teamcolortertpercent, jerseycollargeometrytype,
// jerseynamelayouttype, shortstyle
const std::vector<std::string>& kit_setting_fields();
// 2.11: the teamkits fonts Teams > Colours writes and the store keeps (jerseynamefonttype, numberfonttype,
// shortsnumberfonttype, jerseynameoutlinewidth). Kept like the settings but NOT read from a settings.json: the kit
// import (kit_import.cpp parse_settings) uses kit_setting_fields() alone, so its keys and 0..255 range are unchanged
const std::vector<std::string>& kit_font_fields();
bool kit_font_field(const std::string& field);
// kit_setting_fields() or kit_font_fields(): what the UI counts as "a kit setting" (not a colour)
bool kit_setting_field(const std::string& field);
// What a kit entry may hold and a career load may write: kit_colour_field or kit_setting_field (never a key column)
bool kit_kept_field(const std::string& field);

// One teamkits row's kept fields (Teams > Colours writes the three channels of a colour at a time)
struct KitEdit {
    int64_t teamtechid = 0;
    int64_t kittype = 0;     // teamkittypetechid (0 home, 1 away, ...)
    int64_t teamkitid = -1;  // the row that was edited: tells two rows of one type apart (-1 = not known)
    std::string team;        // club name when it was edited (shown in the UI only)
    std::string when;        // "2026-10-04 11:40" of the last edit
    std::map<std::string, int64_t> fields;  // field name -> value
};

// One player's kept player-specific callname (playernamemap.commentaryid)
struct CallnameEdit {
    int64_t playerid = 0;
    int64_t commentaryid = 0;
    std::string player;  // player name when it was assigned (UI only)
    std::string from;    // whose callname it is / where it came from (UI only)
    std::string when;
};

// The players fields the game's development plan holds for the user's players (Live Editor DOC.MD: all 34 attributes,
// the work rates, skill moves, weak foot; Lua core/moves.lua PLAN_FIELDS): an edit of one of them must reach the plan
bool plan_field(const std::string& field);
// The players fields a Players editor edit is kept for (written again at a career load): the plan fields, overallrating
// and potential
bool kept_player_field(const std::string& field);

// One kept players field: what Turbo wrote and the value it replaced
struct PlayerFieldEdit {
    int64_t value = 0;
    int64_t was = 0;
};
// One player's kept fields
struct PlayerEdit {
    int64_t playerid = 0;
    std::string player;  // player name at the last edit (UI only)
    std::string when;    // "2026-10-05 22:40" of the last edit
    std::map<std::string, PlayerFieldEdit> fields;
};
// What a career load does with one kept field, by the value it loaded with: Write (it holds the replaced value: the game
// put the old value back), InPlace (it holds the kept value), Moved (anything else: the game's own growth or decline,
// another career's player; never written over)
enum class KeptField { Write, InPlace, Moved };
KeptField kept_field_state(const PlayerFieldEdit& e, int64_t loaded);

class ReapplyStore {
public:
    using KitKey = std::tuple<int64_t, int64_t, int64_t>;  // teamtechid, teamkittypetechid, teamkitid (-1 = not known)
    std::map<KitKey, KitEdit> kits;
    std::map<int64_t, CallnameEdit> callnames;
    std::map<int64_t, PlayerEdit> players;
    // The switch for the player edits ("keep_players"): off = new edits are not kept and the kept ones are not written
    // at a career load (they stay in the file until forgotten)
    bool keep_players = true;

    bool empty() const { return kits.empty() && callnames.empty() && players.empty(); }
    size_t size() const { return kits.size() + callnames.size() + players.size(); }
    // An edit of a kept players field from `before` to `after`. A field already kept whose kept value is `before` keeps
    // its first replaced value (an edit continued); an edit back to that replaced value drops the field (nothing left to
    // keep; a player with no field left is dropped). Any other edit keeps {after, before}. `player` / `when` refresh the
    // entry (UI only). Fields that are not kept_player_field are ignored. True when the store changed.
    bool note_player_edit(int64_t playerid, const std::string& player, const std::string& when, const std::string& field, int64_t before,
                          int64_t after);
    bool forget_player_field(int64_t playerid, const std::string& field);  // false when there was no such field
    bool forget_player(int64_t playerid);                                  // every field of his; false when none
    const PlayerEdit* player(int64_t playerid) const;
    size_t player_fields() const;  // kept fields of every player
    // Upsert of one field of one kit row: the entry is created when missing; team / when are refreshed, other fields are
    // kept. Two kits of one type (another teamkitid) are separate entries, so one kit's colours never go to the other.
    void set_kit_field(int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& team, const std::string& when,
                       const std::string& field, int64_t value);
    bool forget_kit(int64_t teamtechid, int64_t kittype, int64_t teamkitid);  // false when there was no entry
    // One field of that kit entry (the entry goes when it was its last field); false when it was not kept
    bool forget_kit_field(int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& field);
    // The entry of that kit row; for a known teamkitid with no entry of its own, an entry kept without a kit id
    const KitEdit* kit(int64_t teamtechid, int64_t kittype, int64_t teamkitid) const;
    size_t kits_of_type(int64_t teamtechid, int64_t kittype) const;  // entries of (teamtechid, kittype), any teamkitid
    void set_callname(int64_t playerid, int64_t commentaryid, const std::string& player, const std::string& from, const std::string& when);
    bool forget_callname(int64_t playerid);
    const CallnameEdit* callname(int64_t playerid) const;
};

// {"turbo_reapply": 1, "note": "...", "kits": [{"teamtechid", "teamkittypetechid", "teamkitid", "team", "when",
//  "fields": {"teamcolorprimr": 12, ...}}], "playernamemap": [{"playerid", "commentaryid", "player", "from", "when"}],
//  "keep_players": true, "players": [{"playerid", "player", "when", "fields": {"composure": {"value": 85, "was": 80}}}]}
std::string reapply_json(const ReapplyStore& s);
// false (with err) when the text is not a Turbo store (not JSON, not an object, no "turbo_reapply": 1). Entries with a
// wrong shape (missing ids, non-integer values, a commentary id outside 900001..965000, a kit field that is not one of
// the colour channels, kit_colour_field, a players field that is not kept_player_field or whose value equals the value
// it replaced) are dropped and counted in *dropped; the rest is kept. No "keep_players" (an older store) = on.
// Kit fields: kit_kept_field (the colour channels and, since 2.3, the kit import's settings).
bool parse_reapply_json(const std::string& text, ReapplyStore& out, std::string* err, size_t* dropped = nullptr);

// <Live Editor>\turbo_output\reapply_edits.json
std::filesystem::path reapply_store_path(const std::filesystem::path& le_root);
// A missing file is an empty store (true). An unreadable or malformed file gives false with the reason and an empty
// store; the file is left alone so that save_reapply_store can set it aside instead of overwriting it.
bool load_reapply_store(const std::filesystem::path& p, ReapplyStore& out, std::string* err, size_t* dropped = nullptr);
// Written as <file>.tmp + rename. set_aside: the file on disk could not be read when it was loaded; it is renamed to
// reapply_edits.unreadable.json (replacing an older copy) before the new store is written, so nothing is lost silently.
bool save_reapply_store(const std::filesystem::path& p, const ReapplyStore& s, std::string* err, bool set_aside = false);
// Where an unreadable store goes (save_reapply_store with set_aside)
std::filesystem::path reapply_unreadable_path(const std::filesystem::path& p);

}  // namespace turbo
