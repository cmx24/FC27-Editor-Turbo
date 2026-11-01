// FC 27 LE Turbo GUI - game model on top of the T3DB tables: player names, clubs, ages, lists.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "t3db.h"
#include "text_fold.h"

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
// The positions FC 27 names in its own screens. Evidence: the "role" table of core/field_labels.h (Live Editor's loc strings
// player_role_N for players.role1..9, codes 1..52 and 101..152): every role name starts with one of exactly twelve positions,
// GK RB LB CB CDM CM RM LM CAM RW LW ST, in that order (1-2 GK, 3-6 RB, 7-10 LB, 11-13 CB, 14-17 CDM, 18-22 CM, 23-26 RM,
// 27-30 LM, 31-34 CAM, 35-37 RW, 38-40 LW, 41-44 ST, 45-52 further roles for GK, RB, LB, CB, CDM, ST, RW, LW). The 28-entry
// code table behind position_name is Live Editor's CONST__PLAYER_PRIMARY_POS_NAME (lua\libs\v2\imports\other\consts.lua,
// codes 0..27); its other 16 codes (SW, RWB, LWB, RCB/LCB, RDM/LDM, RCM/LCM, RAM/LAM, RF/CF/LF, RS/LS) appear in formations and
// preferredposition1..7 but have no role of their own. Lists default to these twelve; the full table stays behind a "show all
// internal positions" toggle.
const std::vector<int>& visible_positions();  // codes in the game's role order
bool position_visible(int pos);
// The visible position a stored code is laid out as: a visible code gives itself; the paired internal codes give their visible
// position (RCB/LCB -> CB, RDM/LDM -> CDM, RCM/LCM -> CM, RAM/LAM -> CAM, RS/LS -> ST, the pairing of core/tactics_db.cpp
// pitch_label_for_position); SW, RWB, LWB, RF, CF, LF (no visible counterpart), -1 and codes outside the table give -1.
int position_plays_as(int pos);
// Text for a stored position: "CB" for a visible one, "RCB (internal, plays as CB)" (position_plays_as) or "SW (internal)" for
// an internal code, "None" for -1 (preferredposition2..7 empty), "Unknown position N" outside the table. Never a bare number.
std::string position_descriptor(int pos);
// The Position filter's rule for one stored code: `stored` matches the chosen `wanted` when the codes are equal, or, unless
// `exact` (the "Show all internal positions" toggle is on), when position_plays_as(stored) is `wanted`: a player stored as RCB
// passes the CB filter. -1 on either side matches nothing.
bool position_matches(int stored, int wanted, bool exact);

constexpr int kNoOvrModifier = -1000;

struct PlayerRow {
    int64_t playerid = 0;
    uint32_t idx = 0;        // record index in players snapshot
    uint64_t rec = 0;        // record address
    std::string name;
    int64_t club = 0;        // club team id (0 = none / free agent)
    std::string club_name;
    std::string name_fold, club_fold;  // fold_search(name), fold_search(club_name) (core/text_fold.h): what the search boxes compare
    int overall = 0, potential = 0, position = -1, age = -1;
    // used by the Players list filters
    int positions[7] = {-1, -1, -1, -1, -1, -1, -1};  // preferredposition1..7 (-1 = none or field absent)
    bool retiring = false;                           // isretiring
    uint64_t playstyles = 0, playstyles_plus = 0;    // trait1 / icontrait1 bits
    int gender = 0;                                  // players.gender: 0 men, 1 women
    int calc_overall = -1;                           // overall::compute_overall(preferredposition1, the 34 attributes); -1 when a field is missing (2.22 calc-ovr). Read-only, never written to the DB
    int ovr_modifier = kNoOvrModifier;               // players.modifier (OVR modifier), read with the filter fields; kNoOvrModifier when the field is absent (2.22 calc-ovr)
    bool on_loan = false;                            // a playerloans row (read at rebuild)
    int64_t loaned_from = 0;                         // playerloans.teamidloanedfrom: the parent club of a player on loan
};

// 2.22 calc-ovr: the Δ column (calculated minus stored overall) and the tooltip the two columns share. kNoCalcDelta sorts the
// rows without a calculated overall below every real difference.
constexpr int kNoCalcDelta = -1000;
inline int calc_delta(const PlayerRow& p) { return p.calc_overall < 0 ? kNoCalcDelta : p.calc_overall - p.overall; }
constexpr const char* kCalcOvrHelp =
    "Calc OVR is the overall the attributes give for his main position (Turbo's formula). OVR is the stored overall, the one the game shows.\n"
    "Delta = Calc OVR minus OVR. It is normally 0 for an OVR modifier of 0 to +2.\n"
    "An OVR modifier of -5 typically puts the stored OVR about 5 below Calc OVR (stock database, 23 players, +-2).\n"
    "A kept Overall or attribute edits also move the gap. Nothing here is written to the game.";
// The Mod column header, the Overall tooltip's extra line and the OVR modifier label/box on the Profile card (2.22 calc-ovr)
constexpr const char* kModColumnHelp =
    "Mod = the OVR modifier (players.modifier). It shifts the stored overall: about -5 for a modifier of -5. Calc OVR ignores it.";
constexpr const char* kOverallModifierLine =
    "The OVR modifier shifts the stored overall too: a modifier of -5 typically puts it about 5 below Calc OVR (stock data, 23 players).";
constexpr const char* kOvrModifierHelp =
    "OVR modifier shifts the stored overall (about -5 for -5); Calc OVR ignores it.";

// The Players list Position filter: any of the player's 7 preferred positions matches `wanted` (position_matches).
bool player_passes_position(const PlayerRow& p, int wanted, bool exact);

// The Players list Loans filter. A player on loan plays for p.club (teamplayerlinks) and belongs to p.loaned_from.
// LoanIn: on loan at `club`; LoanOut: lent out by `club`; club <= 0 (no club known): both mean "on loan".
enum LoanFilter { LoanAny = 0, LoanOn = 1, LoanIn = 2, LoanOut = 3 };
bool player_passes_loan(const PlayerRow& p, int mode, int64_t club);

struct TeamRow {
    int64_t teamid = 0;
    uint64_t rec = 0;
    std::string name;
    std::string name_fold;   // fold_search(name) (core/text_fold.h)
    int overall = 0;
    int64_t league = -1;
};

struct ManagerRow {
    int64_t managerid = 0;
    uint64_t rec = 0;
    std::string name;
    std::string name_fold;   // fold_search(name) (core/text_fold.h)
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
    // false when this database has no playerloans table (the Loans filter is then off)
    bool loans_known() const { return loans_known_; }
    // The game's runtime loan list (pid -> returning club, core/player_move.h runtime_loans), read before a rebuild. When set (non-empty), a
    // playerloans row counts as a loan only while the game still has that loan: ended loans and permanent moves can leave their row
    // behind. Takes effect on the next rebuild; nullptr = the table alone decides.
    void set_runtime_loans(std::shared_ptr<const std::unordered_map<int64_t, int64_t>> loans) { runtime_loans_ = std::move(loans); }
    // playerloans rows the last rebuild did not count as loans (stale: his own club as the lender, or no loan in the game's list)
    size_t stale_loan_rows() const { return stale_loan_rows_; }
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
    const std::string& team_name_fold(int64_t tid) const;  // the club row's fold_search(name); "" when there is no row
    std::vector<LinkRow> links_of_player(int64_t pid) const;
    std::vector<LinkRow> links_of_team(int64_t tid) const;
    bool is_national_team(int64_t tid) const;

    // Re-read one cached row after an edit
    void refresh_player(int64_t pid, const GameDate& today);
    // Re-read one player's shown name after a name edit: his editedplayernames row, else his name ids (as rebuild does)
    void refresh_player_name(int64_t pid);
    void refresh_team(int64_t tid);
    void reload_links() { build_links(); }

    // Text that explains which name source worked (for the status panel)
    const std::string& name_source() const { return name_source_; }
    // nameid -> name (playernames / dcplayernames / bridge_names.txt), as of the last rebuild
    const std::unordered_map<int64_t, std::string>& names_by_id() const { return name_by_nameid_; }

    // Names Live Editor decoded for Turbo (bridge_names.txt). Used for name tables whose text is compressed in memory
    // (playernames.name in FC 27). Takes effect on the next rebuild.
    void set_extra_names(std::shared_ptr<const NameMap> names) { extra_names_ = std::move(names); }
    // teamid -> readable club name for teams whose teams.teamname is an unresolved localization key
    // ("*TeamName_Abbr15_112264", core/teamnames.h); without one such a club shows as "Team <id>". Next rebuild.
    void set_team_name_fallback(std::shared_ptr<const NameMap> names) { team_fallback_ = std::move(names); }
    // The Live Editor custom_team_names.csv name of a club ("" = none); the donor list of the club commentary picker reads it for clubs with no teams row
    std::string fallback_team_name(int64_t tid) const;
    // How many names the fallback map holds (part of the donor list's cache key)
    size_t fallback_team_names() const { return team_fallback_ ? team_fallback_->size() : 0; }
    // The name shown for a club whose stored name is `stored` (the stored name unless it is empty or a key)
    std::string readable_team_name(int64_t tid, const std::string& stored) const;

private:
    void build_names();
    void build_teams();
    void build_links();
    void build_players(const GameDate& today);
    void build_managers();

    Database& db_;
    bool built_ = false;
    bool loans_known_ = false;
    std::shared_ptr<const std::unordered_map<int64_t, int64_t>> runtime_loans_;
    size_t stale_loan_rows_ = 0;
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
    std::shared_ptr<const NameMap> team_fallback_;
};

}  // namespace turbo
