// FC 27 LE Turbo GUI - player callnames (the name the commentary speaks) for the commentary language the game loaded.
//
// Game rule (FC 27 database, see docs/callnames.md):
//   playernamemap (playerid -> commentaryid) is a player-specific callname and wins; otherwise the commentary id of the
//   player's common name (players.commonnameid -> playernames.commentaryid), else of his last name (lastnameid).
//   commentaryid 900000 means "no callname". The ids live in 900000..965000 (commentarynames lists them all, for every
//   language); which of them have spoken audio depends on the commentary language pack the game loaded. Some banks also
//   have generic names above that range (the user's FC 26 lists: eng_us 980001..980034, ita_it 999931..999952, spa_es
//   and dut_nl 9999xx), used by playernamemap rows; Turbo never asks the game about them, only the FC 26 list knows.
//
// Language packs on disk: <game>\commentary\commentaryfull_<lang>\ (a language the user downloaded, e.g. ita_it) and
// <game>\Data\Win32\commentaryfull_<lang>.toc (the base language, eng_us). Which ids are spoken comes, in this order:
//   1. a hand-made list <Live Editor>\turbo\callnames\spoken_<lang>.txt (one commentary id per line, e.g. from a FIFA
//      Editor Tool export of pSIMPLE_SURNAME) - an override that always wins when present;
//   2. the set the game itself answered (core/commentary_audio.h: every commentary id asked through the audio service
//      the way the Create Player list is filtered, every player id asked for its own recordings) or, as a diagnostic,
//      Turbo's scan of the loaded bank's memory (core/commentary_bank.h); either is cached in
//      <Live Editor>\turbo_output\callnames\spoken_<lang>.json (its "source" field says which);
//   3. else every commentary id used by playernames counts as spoken, and the UI says so.
//
// Players with their OWN recording (bound to the player id in the bank; the game says them whatever playernamemap or
// the name ids give, docs/callnames.md section 1 step 0) come from two sources, and either one is enough:
//   - the game's audio service (SpokenSet::players; its list is known to be incomplete);
//   - the master list of the language <Live Editor>\turbo\callnames\masters\<lang>.json (MasterList), made by
//     turbo/tools/import_callname_masters.py from a master workbook ('real' rows): an FC 27 master built from the
//     game's own data (<name>_master_fc27.xlsm, "game": "fc27", the UI says "FC 27 master") when there is one, else
//     the user's FC 26 list (<name>_master.xlsm, "game": "fc26", "your FC 26 list"; FC 27 mostly reuses those
//     recordings).
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "commentary_bank.h"
#include "model.h"
#include "t3db.h"

namespace turbo {

constexpr int64_t kNoCallname = 900000;      // playernames.commentaryid "no callname"
constexpr int64_t kCallnameMin = 900000;     // commentary ids of player names
constexpr int64_t kCallnameMax = 965000;

// A commentary language pack found on disk
struct CommentaryPack {
    std::string code;         // "ita_it"
    bool downloaded = false;  // under <game>\commentary (a language the user added) rather than the base game data
};

// Packs on disk, sorted by code. Reads nothing but folder names (the game files stay untouched).
std::vector<CommentaryPack> installed_commentary_packs(const std::filesystem::path& game_root);

// Which language the game most likely loaded: an explicit choice (gui_settings.json callnames.language) when it is
// installed, else the single downloaded pack, else eng_us, else the first pack, else "". `why` explains the choice.
std::string pick_commentary_language(const std::vector<CommentaryPack>& packs, const std::string& chosen, std::string* why);

// Spoken-id list text: optional header "#turbo-spoken <lang> <count>", then one commentary id per line; text after a
// tab, space or '#' is a comment. Returns false (with err) when no id was read or the count does not match.
bool parse_spoken_list(const std::string& text, std::unordered_set<int64_t>& out, std::string* lang = nullptr,
                       std::string* err = nullptr);
// <Live Editor>\turbo\callnames\spoken_<lang>.txt
std::filesystem::path spoken_list_path(const std::filesystem::path& le_root, const std::string& lang);

struct SpokenSet {
    std::string lang;
    std::unordered_set<int64_t> ids;              // commentary ids (surnames) with a recording
    std::unordered_map<int64_t, int> players;     // player ids with their own recordings -> events / tables they are in
    bool verified = false;  // true: from a list file, the game's audio service or a bank capture; false: fallback
    enum class From { None, ListFile, GameAudio, BankCapture, Fallback } from = From::None;
    // where `players` came from (GameAudio or BankCapture; a hand-made list never holds players) and how many player
    // ids that build asked about (0 = not known)
    From players_from = From::None;
    size_t players_checked = 0;
    std::string source;     // where the set came from (shown in the UI)
    bool spoken(int64_t id) const { return id != kNoCallname && ids.count(id) > 0; }
    // the bank has recordings of this player's own name (spoken even when the rule above says "none")
    bool real(int64_t playerid) const { return players.count(playerid) > 0; }
};

// The master list for one language (turbo\callnames\masters\<lang>.json, read-only for Turbo): an FC 27 master built
// from the game, or the user's FC 26 list
struct MasterList {
    std::string lang;
    std::string game = "fc26";  // "fc27" (an FC 27 master) or "fc26" (the user's FC 26 list; also a file without "game")
    std::string file;    // the JSON read
    std::string source;  // the workbook it was made from
    std::string built;   // when it was made (ISO time, as written by the tool)
    std::unordered_set<int64_t> real_players;          // player ids with their own ('real') recording
    std::unordered_set<int64_t> generic_ids;           // commentary ids of generic surname recordings
    std::unordered_map<int64_t, std::string> names;    // playerid -> the name the list gives his recording
    bool loaded() const { return !real_players.empty() || !generic_ids.empty(); }
    bool real(int64_t playerid) const { return real_players.count(playerid) > 0; }
    bool fc27() const { return game == "fc27"; }
    // What the UI calls it: "the FC 27 master" / "your FC 26 list" ("FC 27 master" / "Your FC 26 list" at the start
    // of a line)
    std::string label(bool line_start = false) const {
        if (fc27()) return line_start ? "FC 27 master" : "the FC 27 master";
        return line_start ? "Your FC 26 list" : "your FC 26 list";
    }
};
// Parses the JSON the import tool writes. Entries of the wrong type are skipped; a file without any id, with the
// wrong "language" (when `lang` is not empty) or that is not JSON is refused (err says why). Never throws.
bool parse_master_list_json(const std::string& text, const std::string& lang, MasterList& out, std::string* err = nullptr);
// <Live Editor>\turbo\callnames\masters\<lang>.json
std::filesystem::path master_list_path(const std::filesystem::path& le_root, const std::string& lang);

// Who says a player has his own recording (bit mask)
constexpr int kOwnFromGame = 1;     // the game's audio service or the bank capture (SpokenSet::players)
constexpr int kOwnFromMasters = 2;  // the master list (MasterList::real_players)
// "the game's audio service", "your FC 26 list", "the game's audio service and your FC 26 list" ("" for 0);
// `masters` = MasterList::label() of the list loaded ("FC 27 master" for an FC 27 master)
std::string own_recording_source_name(int own, SpokenSet::From game_from = SpokenSet::From::GameAudio,
                                      const std::string& masters = "your FC 26 list");

enum class CallnameSource { None, PlayerSpecific, CommonName, LastName };
const char* callname_source_name(CallnameSource s);

struct CallnameInfo {
    int64_t commentaryid = kNoCallname;
    CallnameSource source = CallnameSource::None;
    int64_t nameid = 0;  // the name whose commentary id is used (common or last name), 0 for player-specific/none
    bool real = false;   // the player has his own recording (either source below): the game says it, not the rule
    int own = 0;         // kOwnFromGame | kOwnFromMasters: which source says so
};

// The game rule above, on plain maps (playernamemap: playerid -> commentaryid; name_commentary: nameid -> commentaryid).
// A common name whose commentary id is "no callname" does not hide the last name's callname.
CallnameInfo resolve_callname(int64_t playerid, int64_t commonnameid, int64_t lastnameid,
                              const std::unordered_map<int64_t, int64_t>& playernamemap,
                              const std::unordered_map<int64_t, int64_t>& name_commentary);

// Picker rows
struct NameChoice {
    int64_t nameid = 0;
    std::string name;
    int64_t commentaryid = 0;
    int users = 0;  // players whose common or last name it is
};
struct PlayerChoice {
    int64_t playerid = 0;
    std::string name;
    std::string club;
    int64_t commentaryid = 0;
};

// Case-insensitive substring match used by the type-ahead pickers (also matches an id prefix when `text` is a number)
bool callname_filter_match(const std::string& text, const std::string& name, int64_t id);

// What Turbo knows about a commentary id being spoken in the loaded language (Callnames::spoken_answer)
enum class SpokenAnswer {
    Spoken,   // the spoken set or the FC 26 list's generic names hold it
    Silent,   // the source that covers its range lacks it (see Callnames::spoken_answer)
    Unknown,  // nothing that covers it was loaded: Turbo cannot tell, so it counts as spoken wherever a callname could be lost
};

// One playernamemap row as read from the table
struct NameMapRow {
    int64_t playerid = 0;
    int64_t commentaryid = 0;
    uint64_t rec = 0;  // record address
};

// A playernamemap row that can be given to another player when the table is full (FC 27: 106 of 106 rows, and Live
// Editor crashes the game when asked to add a row to a full table). Callnames::spare_playernamemap_row.
enum class SpareWhy {
    None,
    NoPlayer,    // its player is not in the database: nobody needs the row
    NoCallname,  // its commentary id is "no callname" (900000, 0, -1): the row gives its player nothing
    NotSpoken,   // its callname has no recording in the loaded language (SpokenAnswer::Silent): its player hears nothing from it
};
struct SpareRow {
    uint64_t rec = 0;  // 0 = no row can be taken over
    int64_t playerid = 0;
    int64_t commentaryid = 0;
    SpareWhy why = SpareWhy::None;
    int spoken = 0;   // rows kept because their callname is spoken in the loaded language
    int unknown = 0;  // rows kept because Turbo cannot tell (SpokenAnswer::Unknown)
};

// Everything the Callname editor needs, rebuilt from the database when the model changes
struct CallnameIndex {
    std::unordered_map<int64_t, int64_t> playernamemap;        // playerid -> commentaryid
    std::unordered_map<int64_t, uint64_t> playernamemap_rec;   // playerid -> record address (direct edits)
    std::unordered_map<int64_t, int64_t> name_commentary;      // nameid -> commentaryid (playernames)
    std::unordered_map<int64_t, int> name_users;               // nameid -> players using it (common or last name)
    std::unordered_set<int64_t> used_ids;                      // every commentary id playernames uses (fallback set)
    std::vector<NameChoice> names;                             // spoken names, sorted by name
    std::vector<PlayerChoice> players;                         // players with a spoken player-specific callname
    uint64_t model_version = 0;
    bool built = false;
    void clear();
};

class Callnames {
public:
    // Find the packs, pick the language and load its spoken set: the hand-made list, else the bank capture cache;
    // plus the master list of players with their own recording (absent or bad = not used, the reason kept).
    // `chosen` = gui_settings callnames.language ("" = auto).
    void refresh(const std::filesystem::path& le_root, const std::filesystem::path& game_root, const std::string& chosen);
    // A spoken set just arrived (the game's audio service, core/commentary_audio.h, or a bank capture,
    // core/commentary_bank.h): cache it (turbo_output\callnames\spoken_<lang>.json) and use it unless a hand-made list
    // overrides it. Returns false (with err) when not usable.
    bool apply_capture(const BankCapture& c, const std::filesystem::path& le_root, const std::string& when, const std::string& build,
                       std::string* err);
    // Rebuild the index from the live tables (names: nameid -> name, from the model)
    void build_index(Database& db, const Model& model, const std::unordered_map<int64_t, std::string>& names);
    CallnameInfo resolve(const PlayerRow& p, Database& db) const;
    // kOwnFromGame | kOwnFromMasters for this player (0 = no own recording known: the callname rule decides)
    int own_recording(int64_t playerid) const {
        return (spoken.real(playerid) ? kOwnFromGame : 0) | (masters.real(playerid) ? kOwnFromMasters : 0);
    }
    std::string own_source(int own) const { return own_recording_source_name(own, spoken.players_from, masters.label()); }
    // Only the user's FC 26 list (FC 26 data) says the player has his own recording, while the game's audio service
    // checked players and did not list him: the UI says so (the service's list is known to miss players, so the
    // list's answer still counts)
    bool own_unconfirmed(int own) const {
        return own == kOwnFromMasters && !masters.fc27() && spoken.players_from == SpokenSet::From::GameAudio && spoken.players_checked > 0;
    }
    // Is commentary id `cid` spoken in the loaded language? Spoken when the spoken set or the FC 26 list's generic names
    // hold it. Silent only where a loaded source covers the id and lacks it: 900001..965000 with a verified spoken set
    // (the game's audio service, a hand-made list or a bank capture; Turbo asks the game about this range only), any
    // other id with the FC 26 list loaded (the user mapped every recording of the language there; e.g. eng_us has
    // generic names 980001..980034, ita_it 999931..999952). Else Unknown (the fallback set; an id above 965000 without
    // a list). "No callname" ids (<= 900000) are Silent. "FC 26 list" here and below: the master list, either kind.
    SpokenAnswer spoken_answer(int64_t cid) const;
    // For a Silent id: which source says it has no recording ("the game's audio service and your FC 26 list")
    std::string silent_source(int64_t cid) const;
    // The row to take over for `for_player` when playernamemap is full, so that no other player loses a callname he
    // hears: first a row whose player is not in the database, then a row with no callname, then a row whose callname
    // is Silent; rows whose callname is Spoken or Unknown are never taken. `in_database` says whether a player id is in
    // the database. rec 0 when there is none (spoken / unknown count the rows kept).
    SpareRow spare_playernamemap_row(const std::vector<NameMapRow>& rows, const std::function<bool(int64_t)>& in_database,
                                     int64_t for_player) const;

    std::vector<CommentaryPack> packs;
    std::string lang, lang_why;
    SpokenSet spoken;
    std::string list_path;   // the list file looked for
    std::string list_error;  // why it was not used ("" = used or none present)
    std::string cache_path;  // the bank capture cache looked for
    std::string cache_error; // why it was not used ("" = used or none present)
    MasterList masters;          // the master list for the language (empty when absent or refused)
    std::string masters_path;    // the list looked for
    std::string masters_error;   // why it was not used ("" = used or none present)
    CallnameIndex index;
    bool refreshed = false;
    bool no_game_root = false;  // refresh() was given no game folder
};

// Folder of the running game (FC27.exe) on Windows; empty elsewhere
std::filesystem::path game_root_from_process();

}  // namespace turbo
