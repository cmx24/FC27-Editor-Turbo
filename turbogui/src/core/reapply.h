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

class ReapplyStore {
public:
    using KitKey = std::tuple<int64_t, int64_t, int64_t>;  // teamtechid, teamkittypetechid, teamkitid (-1 = not known)
    std::map<KitKey, KitEdit> kits;
    std::map<int64_t, CallnameEdit> callnames;

    bool empty() const { return kits.empty() && callnames.empty(); }
    size_t size() const { return kits.size() + callnames.size(); }
    // Upsert of one field of one kit row: the entry is created when missing; team / when are refreshed, other fields are
    // kept. Two kits of one type (another teamkitid) are separate entries, so one kit's colours never go to the other.
    void set_kit_field(int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& team, const std::string& when,
                       const std::string& field, int64_t value);
    bool forget_kit(int64_t teamtechid, int64_t kittype, int64_t teamkitid);  // false when there was no entry
    // The entry of that kit row; for a known teamkitid with no entry of its own, an entry kept without a kit id
    const KitEdit* kit(int64_t teamtechid, int64_t kittype, int64_t teamkitid) const;
    size_t kits_of_type(int64_t teamtechid, int64_t kittype) const;  // entries of (teamtechid, kittype), any teamkitid
    void set_callname(int64_t playerid, int64_t commentaryid, const std::string& player, const std::string& from, const std::string& when);
    bool forget_callname(int64_t playerid);
    const CallnameEdit* callname(int64_t playerid) const;
};

// {"turbo_reapply": 1, "note": "...", "kits": [{"teamtechid", "teamkittypetechid", "teamkitid", "team", "when",
//  "fields": {"teamcolorprimr": 12, ...}}], "playernamemap": [{"playerid", "commentaryid", "player", "from", "when"}]}
std::string reapply_json(const ReapplyStore& s);
// false (with err) when the text is not a Turbo store (not JSON, not an object, no "turbo_reapply": 1). Entries with a
// wrong shape (missing ids, non-integer values, a commentary id outside 900001..965000, a kit field that is not one of
// the colour channels, kit_colour_field) are dropped and counted in *dropped; the rest is kept.
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
