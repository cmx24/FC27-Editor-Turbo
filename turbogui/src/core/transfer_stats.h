// FC 27 LE Turbo GUI - transfer columns for the Teams list and a club's Transfers tab (FC 26 LE v26.3.5 "teams list
// transfer columns", row 21 of docs/fc26-parity.md). Read only: nothing here writes to the game.
// Two sources, merged: Turbo's own transfer history export (turbo_output\turbo_transfer_history_*.csv, written by
// features/export_transfer_history.lua from the game's TransferManager, verified in game) and the database "transfers"
// table (playerid, sellingteamid, buyingteamid, transferamount, transferdate). The export wins when both have a move.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "t3db.h"

namespace turbo::xfer {

struct Move {
    std::string kind;      // the export's "type" ("transfer", "loan", ...); "transfer" for a database row
    int date = 0;          // YYYYMMDD, 0 = unknown
    int64_t playerid = 0;
    std::string player;    // name from the export ("" = database row: the GUI's model names him)
    int64_t from = 0, to = 0;
    int64_t fee = 0;       // the export's total deal value (fee + exchange), else the fee
    std::string source;    // "export" or "database"
};

struct TeamStats {
    int in = 0, out = 0;              // permanent moves (free agents included)
    int loans_in = 0, loans_out = 0;
    int64_t spent = 0, received = 0;  // fees of permanent moves
    int64_t net() const { return received - spent; }
};

// A loan row (kind starts with "loan", any case)
bool is_loan(const Move& m);
// Parse the transfer history export. Rows without a valid player or club id are skipped and counted in *skipped.
// Unknown header layouts give no rows (*skipped = -1).
std::vector<Move> parse_export_csv(const std::string& text, int* skipped = nullptr);
// The newest turbo_transfer_history_*.csv in dir by name (the name carries the date), "" when there is none
std::filesystem::path newest_export(const std::filesystem::path& dir);
// Rows of the database "transfers" table (empty when the table or its fields are missing)
std::vector<Move> read_db_transfers(Database& db);
// a plus the rows of b that a does not have (same player, from, to; dates within 3 days or unknown)
std::vector<Move> merge(std::vector<Move> a, const std::vector<Move>& b);
// Per club: moves in and out, fees spent and received. Free agents (111592) and club id <= 0 are never a row.
std::unordered_map<int64_t, TeamStats> per_team(const std::vector<Move>& moves);
// The moves of one club (in or out), newest first
std::vector<Move> of_team(const std::vector<Move>& moves, int64_t teamid);
// "4.2M", "850K", "12K", "0", "-1.5M"
std::string money_short(int64_t v);
// "2026-07-02" ("?" for 0)
std::string date_text(int yyyymmdd);

constexpr int64_t kFreeAgents = 111592;

}  // namespace turbo::xfer
