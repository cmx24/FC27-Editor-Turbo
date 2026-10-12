// FC 27 LE Turbo GUI - your career's scouts (FC 26 LE v26.1.4 "coaches, scouts, perfect staff", row 17 of
// docs/fc26-parity.md). FC 27's career database has a scouts table (career_scouts: firstname, lastname, experience,
// knowledge, nationality, regionid, state, scoutid) and no coaches table, so only scouts are covered.
// Writes are database edits of that table only, made on a click (never automatic):
//   - "Best scouts" sets experience and knowledge of every scout to one level (the game shows 1 to 5 stars), after saving
//     the original values once to turbo_output\turbo_scouts_backup.json (an existing backup is kept: it holds the originals)
//   - "Restore" writes the backed-up values back (matched by scout id and name) and then removes the backup
// Kill switch: turbo_output\scouts_off.txt (both buttons refuse while it exists).
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

constexpr int kBestLevel = 5;  // five stars in the game's scout screens

// Every valid row of career_scouts (empty: no table, e.g. outside a career)
std::vector<Scout> read(Database& db);
std::filesystem::path backup_path(const std::filesystem::path& out_dir);
std::filesystem::path kill_switch_path(const std::filesystem::path& out_dir);
bool killed(const std::filesystem::path& out_dir);
nlohmann::json backup_json(const std::vector<Scout>& s);

// Set experience and knowledge of every scout to `level` (clamped to each field's range). Writes the backup first when
// none exists; refuses (0, msg) when the backup cannot be written or the kill switch exists. Returns the scouts changed.
int make_best(Database& db, int level, const std::filesystem::path& out_dir, std::string* msg);
// Write the backed-up values back. Returns the scouts restored; the backup is removed when every entry was matched.
int restore(Database& db, const std::filesystem::path& out_dir, std::string* msg);

}  // namespace turbo::scouts
