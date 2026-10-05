// FC 27 LE Turbo GUI - CMTracker player library: the players CSV files a user downloads from cmtracker.net (the site's own
// CSV button, 50 rows per page) are read from one folder, merged by player id, searched, and one player at a time turned
// into a Turbo player preset (the same JSON the Import / Create dialogs read) for Lua's create_player.
// Platform independent (the Windows GUI and the Linux tests). Nothing here touches the network.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace turbo {

struct CmtPlayer {
    int64_t id = 0;
    int db_version = 0;                         // the game of the miniface URL (".../DB/27/heads/p<id>.png"), 0 = unknown
    std::string name;                           // display name ("known as", else first + last)
    std::string club;
    int64_t club_id = 0;
    int overall = 0, potential = 0;
    std::string position;                       // primary_position text ("ST")
    std::string nation;
    int age = 0;
    std::map<std::string, std::string> cols;    // every CSV column of the row (lower-case header -> text)
    std::string source;                         // file the row came from
};

class CmtLibrary {
public:
    // Read every *.csv under dir (not recursive) that has a CMTracker players header. Newer rows (by file time, then
    // later in the file) replace older ones of the same player id. Returns the number of players.
    size_t load(const std::filesystem::path& dir);
    size_t size() const { return players_.size(); }
    size_t files() const { return files_; }
    const std::vector<std::string>& problems() const { return problems_; }  // files that were skipped, with the reason
    const CmtPlayer* find(int64_t id) const;
    // Players whose name, club or nation contains every word of `query` (case and accents ignored for plain letters),
    // or whose id equals a number in the query; best first (id match, then overall). At most `limit`.
    std::vector<const CmtPlayer*> search(const std::string& query, size_t limit = 100) const;
    const std::vector<CmtPlayer>& all() const { return players_; }

    // Add the rows of CSV text (testing and load()); returns rows added, "" in err on success
    size_t add_csv(const std::string& text, const std::string& source, std::string* err = nullptr);

private:
    std::vector<CmtPlayer> players_;
    std::map<int64_t, size_t> by_id_;
    std::vector<std::string> problems_;
    size_t files_ = 0;
};

// Parse CSV text (RFC 4180, quoted fields, CRLF, UTF-8 BOM) into rows of cells
std::vector<std::vector<std::string>> parse_csv_text(const std::string& text);

// The miniface URL for a player: https://cmtracker.fra1.cdn.digitaloceanspaces.com/DB/<version>/heads/p<id>.png, built from
// the validated id and version only (never from the text in a file). "" when the id or version is not valid.
std::string cmt_head_url(int64_t id, int db_version);

// What the preset needs beyond the CSV row
struct CmtPresetOptions {
    int64_t teamid = 0;      // club link (0 = none: Lua's create_player takes its own teamid)
    int jersey = 0;
    bool real_face = true;   // keep the real face (headclasscode 0, headassetid = CMTracker id) when the CSV says it has one
};

// A Turbo player preset ("turbo-player-preset") for the player: every players field CMTracker has, the names, defaults for
// what it has not (see cmtracker.cpp), and "cmtracker" notes (source ids, real face request, what was left out). Values
// are not range-checked here: Lua's create_player validates each one against the game's field metadata.
nlohmann::json cmt_to_preset(const CmtPlayer& p, const CmtPresetOptions& o);

// Bit of a playstyle name in trait1 (index < 30) or trait2 (index < 17), for "Game Changer" / "Gamechanger" / "GK Far Reach":
// returns -1 when unknown. which = 1 or 2 is set to the trait column.
int cmt_playstyle_bit(const std::string& name, int* which);

// Position name (GK, ST, CAM, ...) to the game's preferredposition code, -1 when unknown
int cmt_position_code(const std::string& name);

}  // namespace turbo
