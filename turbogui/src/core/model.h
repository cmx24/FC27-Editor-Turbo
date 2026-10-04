// FC 27 LE Turbo GUI - game model on top of the T3DB tables: player names, clubs, ages, lists.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "t3db.h"

namespace turbo {

// nameid -> player name (from bridge_names.txt)
using NameMap = std::unordered_map<int64_t, std::string>;

struct GameDate {
    int year = 0, month = 0, day = 0;
    bool valid() const { return year >= 1900 && month >= 1 && month <= 12 && day >= 1 && day <= 31; }
    int as_int() const { return year * 10000 + month * 100 + day; }
};

// birthdate / playerjointeamdate fields: Lilian day numbers (day 1 = 1582-10-15)
GameDate date_from_gregorian_days(int64_t days);
int64_t gregorian_days_from_date(const GameDate& d);
// A calendar date that exists (rejects 2027-02-30 and the like)
bool is_real_date(const GameDate& d);
int age_on(const GameDate& birth, const GameDate& today);

const char* position_name(int pos);  // 0..27, "?" otherwise
int position_count();

struct PlayerRow {
    int64_t playerid = 0;
    uint32_t idx = 0;        // record index in players snapshot
    uint64_t rec = 0;        // record address
    std::string name;
    int64_t club = 0;        // club team id (0 = none / free agent)
    std::string club_name;
    int overall = 0, potential = 0, position = -1, age = -1;
    // used by the Players list filters
    int positions[7] = {-1, -1, -1, -1, -1, -1, -1};  // preferredposition1..7 (-1 = none or field absent)
    bool retiring = false;                           // isretiring
    uint64_t playstyles = 0, playstyles_plus = 0;    // trait1 / icontrait1 bits
};

struct TeamRow {
    int64_t teamid = 0;
    uint64_t rec = 0;
    std::string name;
    int overall = 0;
    int64_t league = -1;
};

struct ManagerRow {
    int64_t managerid = 0;
    uint64_t rec = 0;
    std::string name;
    int64_t teamid = 0;
};

struct LinkRow {
    uint64_t rec = 0;
    int64_t teamid = 0, playerid = 0;
    int jersey = -1, position = -1;
};

class Model {
public:
    explicit Model(Database& db) : db_(db) {}

    // Rebuild every cache from live memory. Returns false when the database is not ready.
    bool rebuild(const GameDate& today);
    bool built() const { return built_; }
    // Changes on every rebuild: views holding PlayerRow/TeamRow pointers must re-collect them
    uint64_t version() const { return version_; }

    const std::vector<PlayerRow>& players() const { return players_; }
    const std::vector<TeamRow>& teams() const { return teams_; }
    const std::vector<ManagerRow>& managers() const { return managers_; }

    std::string player_name(int64_t pid) const;
    std::string team_name(int64_t tid) const;
    // Text of a playernames/dcplayernames id ("#<id>" when unknown)
    std::string name_text(int64_t nameid) const {
        auto it = name_by_nameid_.find(nameid);
        return it == name_by_nameid_.end() ? "#" + std::to_string(nameid) : it->second;
    }
    bool has_name(int64_t nameid) const { return name_by_nameid_.count(nameid) > 0; }
    const PlayerRow* player(int64_t pid) const;
    const TeamRow* team(int64_t tid) const;
    std::vector<LinkRow> links_of_player(int64_t pid) const;
    std::vector<LinkRow> links_of_team(int64_t tid) const;
    bool is_national_team(int64_t tid) const;

    // Re-read one cached row after an edit
    void refresh_player(int64_t pid, const GameDate& today);
    void refresh_team(int64_t tid);
    void reload_links() { build_links(); }

    // Text that explains which name source worked (for the status panel)
    const std::string& name_source() const { return name_source_; }
    // nameid -> name (playernames / dcplayernames / bridge_names.txt), as of the last rebuild
    const std::unordered_map<int64_t, std::string>& names_by_id() const { return name_by_nameid_; }

    // Names Live Editor decoded for Turbo (bridge_names.txt). Used for name tables whose text is compressed in memory
    // (playernames.name in FC 27). Takes effect on the next rebuild.
    void set_extra_names(std::shared_ptr<const NameMap> names) { extra_names_ = std::move(names); }

private:
    void build_names();
    void build_teams();
    void build_links();
    void build_players(const GameDate& today);
    void build_managers();

    Database& db_;
    bool built_ = false;
    uint64_t version_ = 0;
    std::vector<PlayerRow> players_;
    std::vector<TeamRow> teams_;
    std::vector<ManagerRow> managers_;
    std::vector<LinkRow> links_;
    std::unordered_map<int64_t, size_t> player_index_;
    std::unordered_map<int64_t, size_t> team_index_;
    std::unordered_map<int64_t, std::string> name_by_nameid_;
    std::unordered_map<int64_t, std::string> edited_names_;
    std::unordered_map<int64_t, int64_t> team_league_;
    std::string name_source_;
    std::shared_ptr<const NameMap> extra_names_;
};

}  // namespace turbo
