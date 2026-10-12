// FC 27 LE Turbo GUI - your career's scouts (FC 26 LE v26.1.4 "coaches, scouts, perfect staff", row 17 of
// docs/fc26-parity.md). FC 27's career database has a scouts table (career_scouts: firstname, lastname, experience,
// knowledge, nationality, regionid, state, scoutid) and no coaches table, so only scouts are covered.
// Experience and knowledge are 3-bit fields, but FC 27's schema (turbo/le27/fc27_db_schema.json) gives them the range 0..4:
// Turbo never writes 5..7 (kMaxLevel), whatever the storage could hold.
// Writes are database edits of that table only, made on a click (never automatic):
//   - "Set every scout" sets experience and knowledge of every scout to one level (0..4) and a scout's own cells edit one
//     value; before either writes, the current values of every scout not yet in the backup are added to
//     turbo_output\turbo_scouts_backup.json. Entries are keyed by your club (bridge user_team), scout id and names, so a
//     second career (or a scout hired later) gets its own originals instead of finding another career's
//   - "Restore" writes this club's backed-up values back (matched by scout id and names), removes the restored entries and
//     keeps the other careers' entries (the file goes when it is empty)
// Kill switch: turbo_output\scouts_off.txt (every write here refuses while it exists).
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "t3db.h"

namespace turbo::scouts {

struct Scout {
    uint64_t rec = 0;
    int64_t scoutid = -1;
    std::string firstname, lastname;
    int64_t experience = 0, knowledge = 0, nationality = 0, regionid = 0, state = 0;
    std::string name() const;  // "First Last" (or "Scout <id>")
};

constexpr int kMaxLevel = 4;  // the game's range for experience / knowledge (schema max 4); the 3-bit field could hold 7
constexpr int kBestLevel = kMaxLevel;

// The highest value Turbo writes to this field: the game's range, never the bit range (min(field max, kMaxLevel))
int64_t level_max(const Field& f);
// v clamped to [max(field min, 0), level_max(f)]
int64_t clamp_level(const Field& f, int64_t v);

// Every valid row of career_scouts (empty: no table, e.g. outside a career)
std::vector<Scout> read(Database& db);
std::filesystem::path backup_path(const std::filesystem::path& out_dir);
std::filesystem::path kill_switch_path(const std::filesystem::path& out_dir);
bool killed(const std::filesystem::path& out_dir);
// {"format":"turbo-scouts-backup","version":2,"scouts":[{"team","scoutid","firstname","lastname","experience","knowledge"}]}
nlohmann::json backup_json(const std::vector<Scout>& s, int64_t team);
// Read and check the backup: every entry must have the right types and experience / knowledge 0..kMaxLevel. A missing file
// gives true with no entries. False (and *err) when the file is unreadable, not a Turbo scouts backup or has a bad entry.
bool load_backup(const std::filesystem::path& out_dir, nlohmann::json& entries, std::string* err);
// Entries of the backup that belong to `team` (0 when there is no backup or it is damaged)
int backed_up(const std::filesystem::path& out_dir, int64_t team);
// Add the current values of every scout of `team` that has no entry yet (team, scout id, names). Returns the entries added,
// -1 (and *msg) when the backup is damaged or cannot be written. Nothing is written when every scout already has an entry.
int ensure_backup(Database& db, const std::filesystem::path& out_dir, int64_t team, std::string* msg);

// Set experience and knowledge of every scout to `level` (clamped to 0..kMaxLevel and the field). Backs the originals up
// first (ensure_backup); refuses (0, msg) when that fails or the kill switch exists. Returns the scouts changed.
int make_best(Database& db, int level, const std::filesystem::path& out_dir, int64_t team, std::string* msg);
// One scout's field (experience / knowledge) to v (clamped), after ensure_backup. False (and *msg) when refused.
bool set_level(Database& db, uint64_t rec, const std::string& field, int64_t v, const std::filesystem::path& out_dir, int64_t team,
               std::string* msg);
// Write this club's backed-up values back (every entry is checked before the first write). Returns the scouts restored;
// restored entries leave the backup, which is removed when nothing is left in it.
int restore(Database& db, const std::filesystem::path& out_dir, int64_t team, std::string* msg);

}  // namespace turbo::scouts
