// FC 27 LE Turbo GUI - the loaded commentary bank's selection tables, read from the game's memory (docs/callnames.md §6).
//
// A commentary language pack is a set of Frostbite sound assets ("families": pSIMPLE_SURNAME, pPLAYER_NAMES_SIMPLE, ...).
// Each family carries a selection table: one row per recorded variation with the selector values the game matches
// against (FIFA Editor Tool shows them as the family's "Data Set": cm_sim, surname_ID or player_db_pID,
// player_intensity). In memory (FC27.exe build 1.0.140.64835, checked 2026-10-04) a row is a 64-byte EBX object,
// 16-byte aligned, inside a Frostbite array whose element count sits 4 bytes before the first row:
//   +0x00 u32 selector value   surname_ID (a commentary id, 900000..965000) or player_db_pID (a player id)
//   +0x04 u32 0
//   +0x08 ptr|3                the variation object (tagged pointer, low 2 bits set)
//   +0x10 u32 player_intensity (2 in every row seen)       +0x14 u32 0
//   +0x18 u32 variation hash   (FET's VariationId)         +0x1c u32 0x88 | row index << 8
//   +0x20 ptr|3, +0x28 ptr|3, +0x30 ptr|3                   selector objects (tagged pointers)
//   +0x38 u32 cm_sim (1)                                    +0x3c u32 0
// The rows of a family are contiguous, so a table is a run of rows 64 bytes apart whose array header agrees with
// the run length. A table whose values are all commentary ids is a surname (generic callname) family; one whose
// values are small ids is keyed by player (or team) id.
//
// capture_commentary_bank() scans readable memory for such rows (the game keeps the bank's EBX in private heap
// memory), groups them into tables and returns the spoken sets; the result is cached as JSON next to Turbo's
// other outputs (spoken_cache_path) so the Callname tab has it without a new scan.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mem.h"
#include "memmap.h"

namespace turbo {

constexpr size_t kBankRowSize = 64;
constexpr uint64_t kBankRowAlign = 16;
constexpr uint32_t kBankRowTag = 0x88;  // low byte of the u32 at +0x1c

struct BankRow {
    uint64_t addr = 0;
    uint32_t value = 0;      // surname_ID or player_db_pID
    uint32_t intensity = 0;  // player_intensity
    uint32_t variation = 0;  // variation hash (+0x18)
    uint32_t index = 0;      // row index from the tag (+0x1c >> 8)
    uint32_t cm = 0;         // cm_sim (+0x38)
};

// Signature check of 64 bytes; fills `out` (addr left to the caller) when they are a selection row
bool parse_bank_row(const uint8_t* p, BankRow& out);
// Every row in buf (its first byte at address `base`), tried at 16-byte steps; appended to `out`. Returns how many.
size_t find_bank_rows(const uint8_t* buf, size_t len, uint64_t base, std::vector<BankRow>& out);

enum class BankTableKind { Unknown, Surnames, Players, Index, Rejected };
const char* bank_table_kind_name(BankTableKind k);

struct BankTable {
    uint64_t start = 0, end = 0;  // [start, end) of the rows
    size_t rows = 0;
    size_t distinct = 0;          // distinct selector values
    uint32_t min_value = 0, max_value = 0;
    uint32_t header = 0;          // Frostbite array count read at start - 4 (bit 31 cleared), 0 when unreadable
    bool header_ok = false;       // header == rows
    BankTableKind kind = BankTableKind::Unknown;
};

// Which family a table belongs to, from its values alone: every value a commentary id -> Surnames; every value a
// small id (1..kBankMaxPlayerId) -> Players; anything else (mixed, tiny) -> Unknown. A run whose values cover more
// than kBankMaxDensity of their own range is an index of consecutive keys (the bank's sample map, seen 2026-10-04 with
// keys 851962..947313, two rows each), never a selection table: Index.
constexpr uint32_t kBankMaxPlayerId = 400000;
constexpr size_t kBankMinTableRows = 8;
constexpr double kBankMaxDensity = 0.5;
constexpr double kBankMinKnownShare = 0.9;  // share of a surname table's values that must be known commentary ids
BankTableKind classify_bank_table(uint32_t min_value, uint32_t max_value, size_t rows, size_t distinct);

// Sorted rows -> tables (runs of rows 64 bytes apart). With `mem` the array header before each run is read and
// compared with the run length; `row_table` (optional) receives the table index of every row. With `known` (the
// commentary ids of the database's commentarynames table) a surname table whose values are not at least
// kBankMinKnownShare known ids is Rejected.
std::vector<BankTable> group_bank_tables(std::vector<BankRow>& rows, Memory* mem, std::vector<size_t>* row_table,
                                         const std::unordered_set<int64_t>* known = nullptr);

struct BankCapture {
    bool ok = false;
    std::string note;                                 // why not ok, or a summary
    std::unordered_set<int64_t> surnames;             // commentary ids with a recording (surname families)
    std::unordered_map<int64_t, int> players;         // player id -> number of player-keyed tables it is in
    std::vector<BankTable> tables;
    size_t rejected = 0;                              // runs of 8+ rows that are an index or failed the known-id check
    size_t rows = 0;                                  // rows found (all tables, including unknown ones)
    size_t regions = 0;
    uint64_t bytes = 0;
    double seconds = 0.0;
    bool cancelled = false;
};

// Scan `regions` through `mem` in chunks (rows crossing a chunk border are caught by a 64-byte overlap), group the
// rows and build the spoken sets. `cancelled` (optional) is polled between chunks; `clock` (seconds) times the scan;
// `known` (optional) = the commentary ids of commentarynames, see group_bank_tables.
BankCapture capture_commentary_bank(Memory& mem, const std::vector<Region>& regions, const std::function<bool()>& cancelled = {},
                                    const std::function<double()>& clock = {}, size_t chunk = 1u << 20,
                                    const std::unordered_set<int64_t>* known = nullptr);

// Cache file: <Live Editor>\turbo_output\callnames\spoken_<lang>.json
std::filesystem::path spoken_cache_path(const std::filesystem::path& le_root, const std::string& lang);

struct BankCache {
    std::string lang;
    std::string when;    // ISO-like time stamp of the capture
    std::string source;  // one line for the UI ("live bank capture 2026-10-04 12:00: 3 tables")
    std::string build;   // game build key the capture was made on
    std::unordered_set<int64_t> surnames;
    std::unordered_map<int64_t, int> players;
    std::vector<BankTable> tables;
};
std::string bank_cache_json(const BankCapture& c, const std::string& lang, const std::string& when, const std::string& build);
// false (with err) when the text is not a cache written by Turbo or holds no ids
bool parse_bank_cache_json(const std::string& text, BankCache& out, std::string* err);

}  // namespace turbo
