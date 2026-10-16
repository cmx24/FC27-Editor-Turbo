// FC 27 LE Turbo GUI - season update pack importer (B3): a folder with the transfers and manager changes of a real-world
// window, previewed row by row against the live career, applied in pages of at most 50 rows and undone from a record.
//
//   <pack folder>/pack.json      {"name": "...", "season": "...", "source": "..."}     (name defaults to the folder name)
//                 transfers.csv  playerid, from (team id or exact name), to (team id or exact name); optional: player
//                                (display name), fee (read and IGNORED: no money moves); header names are case-insensitive
//                 managers.csv   managerid, to (team id or exact name, 0 = free agent); optional: manager (display name)
//                 kits/*.zip     optional; only listed in the preview (kits are imported in Teams > Kits, not here)
//
// CMTracker rules (memory cmtracker-rules.md): the pack is a folder the user put there by hand, e.g. built from the CSV
// pages downloaded from cmtracker.net or from community pack files. Nothing here touches the network, scrapes, or lifts
// the 50-row cap: a pack is applied in pages of kPageRows rows, one page per user click.
//
// Platform independent (the Windows GUI and the Linux tests). Core never calls the game: it validates against a plain
// Snapshot of the live model and emits the JSON the window sends to the Lua modules that already exist:
//   player moves:   {"op":"run","module":"player_moves","overrides":{"actions":[{"action":"transfer","playerid","to_teamid",
//                   "fee":0,"wage":0}, ...]}}                       (features/player_moves.lua; wage 0 = he keeps his wage)
//   manager moves:  {"op":"run","module":"manager_move","overrides":{"enabled":true,"managerid","teamid","confirm":true
//                   [,"replacement"]}}                              (features/manager_move.lua; one manager per send; it SWAPS
//                   with the target club's current manager and refuses the user's club)
// Rows that touch the user's club are rejected (the game pays compensation for those moves and manager_move refuses them,
// so an undo would not be clean).
//
// A player who was a free agent when the pack signed him goes back with the game's own release action.
// Undo restores CLUB LINKS (players) and manager.teamid only. It does not restore the join date, the previous team or the
// contract record a PlayerMoved call writes. A row is undone only while its subject is still where the pack put him.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace turbo {

class Model;

namespace season_pack {

constexpr size_t kPageRows = 50;          // the CMTracker page cap, kept: a pack applies this many rows per click
constexpr size_t kMaxPackRows = 5000;     // a sane bound on one pack (rows beyond it are ignored with a problem line)
constexpr int64_t kFreeAgents = 111592;   // the Free Agents team
constexpr int kMinSquad = 18, kMaxSquad = 52;

struct PackInfo {
    std::string name, season, source;
};

struct TransferIn {
    size_t line = 0;                      // 1-based line of transfers.csv (header = line 1)
    int64_t playerid = 0;
    std::string from_text, to_text;       // team id or name, as written
    std::string player_text;              // optional display name from the CSV
};
struct ManagerIn {
    size_t line = 0;
    int64_t managerid = 0;
    std::string to_text;
    std::string manager_text;
};
struct RejectedIn {                       // rows that could not even be read (bad id, missing column)
    std::string file;
    size_t line = 0;
    std::string reason;
};

struct Pack {
    PackInfo info;
    std::vector<TransferIn> transfers;
    std::vector<ManagerIn> managers;
    std::vector<std::string> kits;        // kits/*.zip file names (listed only)
    std::vector<RejectedIn> unreadable;   // rows rejected while reading
    std::vector<std::string> problems;    // whole-file problems (missing file, row bound, bad pack.json)
};

// Read a pack folder. false (with err) when it is not a pack folder (no transfers.csv and no managers.csv). A bad row
// goes to Pack::unreadable and does not stop the rest. Text is UTF-8; a BOM and CRLF are handled (parse_csv_text).
bool read_pack(const std::filesystem::path& dir, Pack& out, std::string* err);

// What the live model looks like, as plain data (validation never touches the game)
struct TeamInfo {
    std::string name;
    bool national = false;
};
struct Snapshot {
    std::map<int64_t, int64_t> player_club;       // playerid -> club team id (0 or kFreeAgents = free agent)
    std::map<int64_t, std::string> player_name;
    std::map<int64_t, int64_t> on_loan;           // playerid -> parent club, for players on loan (their club link is the loan club)
    std::map<int64_t, TeamInfo> teams;
    std::map<int64_t, int64_t> manager_team;      // managerid -> team id (0 = free agent)
    std::map<int64_t, std::string> manager_name;
    int64_t user_team = 0;                        // 0 = not known: every row is rejected (the rule needs it)
    std::map<int64_t, int> squad_sizes() const;   // players per club (free agents not counted)
    std::string team_label(int64_t id) const;     // "Name" or "team <id>"
    int64_t find_team(const std::string& text) const;   // numeric id or exact (case-insensitive) unique name; -1 = none
};
// A Snapshot of the model as of its last rebuild (the window's Pack tab)
Snapshot snapshot_from_model(const Model& m, int64_t user_team);

enum class Kind { Transfer, Manager };

struct PreviewRow {
    Kind kind = Kind::Transfer;
    std::string file;                     // "transfers.csv" / "managers.csv"
    size_t line = 0;
    int64_t id = 0;                       // playerid / managerid
    int64_t from = 0, to = 0;             // clubs (manager: team id, 0 = free agent)
    std::string subject;                  // player or manager name
    bool ok = false;
    std::string reason;                   // why it was rejected ("" when ok)
    std::string text;                     // "Name: A -> B" (+ "; swaps with ...")
    int64_t displaced = 0;                // manager swap: the manager who takes the mover's old job (0 = none)
};
struct Preview {
    PackInfo info;
    std::vector<PreviewRow> rows;         // every pack row in order (transfers, then managers); ok = false ones are rejected
    size_t accepted = 0, rejected = 0;
    std::vector<std::string> kits;
    std::vector<std::string> notes;       // fixed honesty lines (fee ignored, kits listed only, undo scope)
    size_t pages() const;                 // accepted rows / kPageRows, rounded up
};

// Validate every row, in order, on a working copy of the snapshot (a later row sees the earlier rows). Rejects: unknown
// or duplicate player, unknown or national club, a `from` that is not his live club, a player already at the target,
// a club that would drop below 18 players or pass 52, any row that touches the user's club, and the manager equivalents
// (unknown manager, national or user club, already there). Transfers come before managers.
Preview preview(const Pack& pack, const Snapshot& live);

// The accepted rows as pages of at most kPageRows rows; each page is the list of "send" JSONs (see the top of this file):
// one player_moves send holding the page's transfers, then one manager_move send per manager row.
struct Page {
    std::vector<size_t> rows;             // indexes into Preview::rows (accepted rows only)
    std::vector<nlohmann::json> sends;
};
std::vector<Page> make_pages(const Preview& pv);

// ---------------------------------------------------------------- undo record
struct UndoRow {
    Kind kind = Kind::Transfer;
    int64_t id = 0;
    int64_t from = 0, to = 0;             // the club he was in when the pack was applied, and where the pack put him
    int64_t displaced = 0;                // manager swap partner
    std::string subject;
};
struct UndoSet {
    std::string pack, season, when;
    std::vector<UndoRow> rows;            // in the order they were sent
};
// The record of what a page is about to do (accepted rows of the page only, ACTUAL `from` of the preview)
UndoSet undo_rows_of(const Preview& pv, const Page& page);

struct UndoStore {
    std::vector<UndoSet> sets;            // one per pack name
    // Add the rows of `s` to the set of the same pack name (created when missing)
    void add(const UndoSet& s);
    bool remove(const std::string& pack);
    const UndoSet* find(const std::string& pack) const;
};
std::string undo_json(const UndoStore& s);
bool parse_undo_json(const std::string& text, UndoStore& out, std::string* err);
// <Live Editor>\turbo_output\season_pack_undo.json (a file of its own: reapply_edits.json is never rewritten by a pack)
std::filesystem::path undo_store_path(const std::filesystem::path& le_root);
bool load_undo_store(const std::filesystem::path& p, UndoStore& out, std::string* err);   // missing file = empty store
bool save_undo_store(const std::filesystem::path& p, const UndoStore& s, std::string* err);   // tmp + rename

// What Undo does for a pack set against the live snapshot: rows still where the pack put them are reversed (last row first);
// the others are skipped with the reason. `sends` are in the same shape as make_pages' (at most kPageRows rows per page).
struct UndoPlan {
    std::vector<Page> pages;
    std::vector<std::string> done;        // "Name: B -> A"
    std::vector<std::string> skipped;     // "Name: moved since (now at X)"
};
UndoPlan make_undo(const UndoSet& set, const Snapshot& live);

}  // namespace season_pack
}  // namespace turbo
