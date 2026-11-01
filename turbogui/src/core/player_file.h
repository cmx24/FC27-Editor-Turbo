// FC 27 LE Turbo GUI - ".player" files: the one-player export of FC Editor (decoruiz) and of the EA FC Player Creator. UTF-16LE
// with a BOM, tab-separated, CRLF, a header row and one data row: about 146 players-table columns (FC Editor adds gameversion and a
// few more), the names as TEXT in the four *nameid columns, and "number" = the shirt number. Read here (any BOM, UTF-16 LE/BE or
// UTF-8, quoted cells, unknown columns kept) and turned into a Turbo player preset ("turbo-player-preset", the JSON Lua's
// create_player reads; the same shape as cmtracker.h cmt_to_preset): every column that is a players field goes to "players" (values
// outside the field's range are clamped, text that is no number is left out, both with a note), the names to "names" (Lua writes
// them to editedplayernames), the club link when the file has one, and a "player_file" object saying what was left out and why.
// Platform independent (the Windows GUI and the Linux tests). Nothing here touches the game.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace turbo {

// A players field and its range (the game's min .. min + 2^depth - 1)
struct PlayersFieldRange {
    std::string name;
    int64_t min = 0, max = 0;
};
// FC 27's players fields (152, from turbo_output\turbo_table_players_fields.csv of FC 27 TU build 1.0, Live Editor 27.1). The GUI
// passes the live table's own list when a career is loaded (players_fields_of in ui_cmtracker.cpp); this one is the fallback.
const std::vector<PlayersFieldRange>& fc27_players_fields();

struct PlayerFile {
    bool ok = false;
    std::string error;
    std::string encoding;               // "UTF-16LE", "UTF-16BE", "UTF-8" or "ANSI" (bytes that are no UTF-8, read as Windows-1252)
    std::vector<std::string> header;    // column names, trimmed, lower case
    std::vector<std::string> values;    // the data row used (the last one), same length as header
    int rows = 0;                       // data rows in the file
    std::vector<std::string> notes;     // duplicate columns, short rows, extra rows
    // the cell of a column ("" when absent); the first column of that name counts
    std::string get(const std::string& column) const;
    bool has(const std::string& column) const;
};

// The text of a file's bytes as UTF-8: BOM FF FE / FE FF = UTF-16 LE / BE, EF BB BF = UTF-8; no BOM: UTF-16LE when every second
// byte of the start is 0, else UTF-8, else Windows-1252. Surrogate pairs are joined; a lone surrogate becomes U+FFFD.
std::string player_file_text(const std::string& bytes, std::string* encoding = nullptr);
// Tab- (or comma-, when the first line has no tab) separated text into rows of cells: "quoted" cells (with "" inside), CRLF or LF;
// empty lines are skipped
std::vector<std::vector<std::string>> parse_player_table(const std::string& text);
PlayerFile parse_player_file(const std::string& bytes);
PlayerFile read_player_file(const std::filesystem::path& file);

struct PlayerFileOptions {
    int64_t teamid = 0;      // the club the user picked (0 = none: no link in the preset; Lua's create_player takes its own teamid)
    int jersey = -1;         // shirt number for the link (-1 = the file's "number", 0 = the first free one)
    const std::vector<PlayersFieldRange>* fields = nullptr;  // nullptr = fc27_players_fields()
    std::string source;      // file name, for the preset's "source"
};

struct PlayerFilePreset {
    nlohmann::json preset;               // the turbo-player-preset
    std::string name;                    // common name, else first name + surname, else "player <id>"
    int64_t playerid = 0;                // the file's playerid (the new player gets a free id from Lua)
    int overall = 0, potential = 0;
    int position = -1;                   // preferredposition1 (core/model.h position_name)
    int jersey = 0;                      // the file's "number" (0 = none or out of 1..99)
    int64_t teamid = 0;                  // the file's teamid column (0 = none)
    int filled = 0;                      // players fields set from the file
    std::vector<std::string> left_out;   // "column=value (why)"
    std::vector<std::string> clamped;    // "field: 200 -> 128 (range 1..128)"
    std::vector<std::string> defaulted;  // players fields the file lacks that got the new-player default (cmtracker.cpp's list)
};
PlayerFilePreset player_file_to_preset(const PlayerFile& f, const PlayerFileOptions& o);

// 2.0 Edit Profiles & RDBM safety: Clamps rating attributes in preset "players" to 1..99.
void clamp_preset_ratings(nlohmann::json& preset);

}  // namespace turbo
