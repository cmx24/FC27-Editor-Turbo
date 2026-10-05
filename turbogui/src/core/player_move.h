// FC 27 LE Turbo GUI - "player_move" game call: move one player between clubs, or release him, the way the game does it,
// so squads, morale, form, status, roles and team sheets are consistent at once with no save / load
// (platform-independent part; src/win/player_move_win.cpp resolves the functions and runs the call on the game thread).
// Static research: docs/re/realtime_transfers.md (sections 1.3, 1.4, 2.1, 3.1-3.3, 4 and the player_move section at the end).
//
// What the game does (FC27.exe 1.0.140.64835, every offset [H], re-checked in the image for this implementation):
//   TeamUtil::PlayerMoved(teamUtil, pid, from, to)                          0x147DEE368   (__fastcall, game thread)
//     DataController::SetPlayerJoinDate(dc, pid, today), SetPreviousTeam(dc, pid, from)  -- BEFORE any check (a wrong `from`
//     silently changes those two fields), then TeamUtil::MovePlayerLink 0x147DEE4BC, a no-op unless IsPlayerInTeam(pid, from) and
//     not IsPlayerInTeam(pid, to); otherwise WriteTeamPlayersLinks 0x147BA08CC: UPDATE teamplayerlinks, event 0x60 (removed from
//     `from`) then 0x5F (added to `to`) -> every manager listener (morale, form, status, squad ranking, value, development plan,
//     fitness set, FCE statistics team ...) runs inside the call; then DataController::EnforceSquadLimits 0x147B99BFC; then the
//     runtime team sheet of `from` is repaired. No contract record is created (the game's signing code does that).
//   PlayerContractManager::AddContractRecord(pcm, pid, team, months, wage, 100, &calendar+0x34, status)   0x147E5BF9C
//     the arguments are the ones the game's own CreateContract passes (months = 12 * k, wage = PlayerWageManager::GetWage,
//     100 = a byte of the record, the date is the calendar's own storage, status 0 = a normal contract).
//   ContractTerminationManager::ReleasePlayer(ctm, pid)                      0x147B94630   returns CanRelease: 0 ok, 1 your budget
//     cannot pay the compensation, 2 the club has fewer NON-LOANED players than MIN_SQUAD_SIZE before the release (0x147DEC2F8; at
//     exactly the minimum the game releases him and then signs a filler: Turbo refuses that itself). It releases the player's CURRENT club
//     (DataController::GetPlayerTeam 0x14154C5C4: his teamplayerlinks row that is not a national team) to Free Agents with
//     PlayerMoved, and pays the user's finance. The free-agent pool it picks is 0x1B3E8 (111592) unless the club is in the sorted
//     id vector at DataController+0x108 / +0x110, then 0x20128.
//   Squad housekeeping inside PlayerMoved (EnforceSquadLimits; Free Agents, the youth pool 0x1B688 and the pseudo teams 0x1B29D /
//   0x1B72C are skipped on both sides): after the move, while rows + loaned out + pending signed of `to` exceed IniSettings
//   MAX_SQUAD_SIZE ([[hub+0x658]]+0x58) the game RELEASES the lowest-value reserve of `to` (the user's club included); when
//   `from` has fewer teamplayerlinks rows than MIN_SQUAD_SIZE (+0x64) it recalls CPU loans or SIGNS FILLER PLAYERS. Turbo refuses
//   the call when either would happen (DataController::SquadCounts 0x147B7225C reads the three counts).
//
// What Turbo adds around the game's functions (everything is checked BEFORE anything is called, every read goes through
// turbo::Memory, the calls are behind Caller so the whole sequence is tested on a synthetic game):
//   * the hub is re-walked from the comm service ([[comm+0x20]+0x10]); every function / vtable is resolved by signature and lies
//     inside FC27.exe; dc = **(hub+0x418) (no vtable of its own: +0x10 == hub, +0 a db provider whose vtable and slot 1 are in
//     the image), teamUtil = **(hub+0xF18) (+0 == hub), PlayerContractManager / TransferManager / UserManager by vtable + back
//     pointer, ContractTerminationManager (release only) by vtable + back pointer, the event dispatcher's sink, the
//     CalendarManager's date, IniSettings (MAX / MIN_SQUAD_SIZE), the LoansManager's runtime loan list and the TransferManager's
//     pre-signed list (bounded vectors), the SimDayManager idle (no match day being processed);
//   * the player: IsPlayerInTeam(pid, from) true and (pid, to) false (the game's own SELECT), from != to, neither team is a
//     special pseudo team (youth pool, generator, the other free-agent pools) and neither is a NATIONAL team (the game's own
//     predicate: IsInternationalLeague(GetLeagueOfTeam(dc, team)), leagues 78 / 2136 / 3004), the player is not on loan
//     (LoansManager list), he is not the player-career player 0x7AA7; Free Agents 111592 is allowed on either side;
//   * the squad limits above (read from the ini and SquadCounts at run time, never hard-coded);
//   * user arrivals (`to` is the club the UserManager says is the user's) with months > 0: after PlayerMoved the game's
//     AddContractRecord, unless the PlayerContractManager already holds a record for him (then none is added); the record is read
//     back through the same hash-table walk as transfer_list's contract_status. The morale gate byte
//     (PlayerMoraleManager+0x554, set by the job-switch event until the next season reset) is read and reported: while it is 1
//     the game creates no morale entry for new arrivals;
//   * read-backs: IsPlayerInTeam(pid, to) true and (pid, from) false after the move, the squad counts after it, the contract record.
//
// Lua / mailbox contract (op kCallOpPlayerMove = 11; Lua defines `TurboPlayerMove(code, pid, from, to, months, wage)` -> `ok, text,
// status, from_ok, to_ok` on top of it; the call block is the one of core/game_calls.h):
//   args[0] = the FeFceGMCommService plugin (as op 10)
//   args[1] = code | (months << 8)    code 1 MOVE, 2 RELEASE, 9 CHECK ONLY; months 0..120 (0 = no contract record), nothing above bit 15
//   args[2] = pid | (wage << 32)      pid > 0; wage 0..10,000,000 (weekly wage, int32)
//   args[3] = from | (to << 32)       team ids > 0; `to` is ignored by code 2 (a release goes to Free Agents), `from` is the club
//   (all fields are non-negative, so the packing is the same in Lua integers and in int64)
//   out[0] = from_ok, out[1] = to_ok: 1 = true, 0 = false, -1 = not read (the call was refused before the read-back)
//     code 1: from_ok = he is NO LONGER in `from`, to_ok = he IS in `to` (the two IsPlayerInTeam read-backs)
//     code 2: from_ok = he is no longer in his club, to_ok = he is in a free-agent pool
//     code 9: from_ok = he IS in `from`, to_ok = he is NOT in `to` (the two preconditions, nothing was called)
//   text = the sentence for the user (at most 511 characters); status = kCallOk / kCallFailed / kCallQueued.
// ok means the whole sequence and every read-back succeeded. A failure after PlayerMoved ran (stage "check") is reported with the
// read-backs that were taken: the game may have moved the player.
//
// Not in scope (documented follow-ups): deleting players (PlayerUtil::DeletePlayer), created players (events 0x3A / 0x5F),
// loans (TeamUtil 0x147DEC5DC + LoansManager::AddLoan), women's careers (the free-agent pool differs, see above), fees and
// budgets, the 0x4D / 0x4E transfer events.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include "game_calls.h"
#include "mem.h"

namespace turbo {
namespace pm {

// What the call does (Lua's `code`)
constexpr int kActionMove = 1;     // PlayerMoved(pid, from, to) (+ AddContractRecord for a user arrival with months > 0)
constexpr int kActionRelease = 2;  // ReleasePlayer for the user's club, PlayerMoved(pid, club, Free Agents) for any other
constexpr int kActionCheck = 9;    // the whole validation chain and the read-backs of a MOVE, nothing is called
inline bool valid_action(int action) { return action == kActionMove || action == kActionRelease || action == kActionCheck; }
const char* action_name(int action);

// Teams and players the game treats specially (docs/re/realtime_transfers.md section 7, IsFreeAgentTeam 0x14154BBE0)
constexpr int kTeamFreeAgents = 111592;  // 0x1B3E8: the free-agent pool of a men's career; allowed on either side
constexpr int kTeamFreeAgentsB = 0x20128, kTeamFreeAgentsC = 0x20260;  // the other free-agent pools: not supported (refused)
constexpr int kTeamYouthPool = 0x1B688, kTeamGenerator = 0x1B29D, kTeamPseudo = 0x1B72C;  // pseudo teams: refused
constexpr int kPlayerCareer = 0x7AA7;  // 31399: the player-career player (the jersey chooser and the contract manager special-case him)
inline bool is_free_agent_pool(int team) { return team == kTeamFreeAgents || team == kTeamFreeAgentsB || team == kTeamFreeAgentsC; }
// A pseudo team or another free-agent pool: not a club, and not the Free Agents team Turbo moves players to
inline bool is_special_team(int team) {
    return team == kTeamFreeAgentsB || team == kTeamFreeAgentsC || team == kTeamYouthPool || team == kTeamGenerator || team == kTeamPseudo;
}
constexpr int kMaxMonths = 120;        // the contract length is a signed byte in the record (the game writes 12 * k, at most 60)
constexpr int kMaxWage = 10000000;     // weekly wage bound: a typo, not a wage, above it
constexpr int kContractByte = 100;     // AddContractRecord's sixth argument in the game's own CreateContract
constexpr int kContractStatus = 0;     // AddContractRecord's last argument: 0 = a normal contract

// Manager table slots (Live Editor's type ids; slot i at hub + 0x20 * i, holder at +0x18, object = **holder). The slots shared
// with transfer_list are tl::kType* (core/transfer_list.h)
constexpr int kTypeCtm = 30;            // ContractTerminationManager (hub+0x3D8)
constexpr int kTypeDataController = 32;  // DataController (hub+0x418)
constexpr int kTypeIni = 50;            // IniSettingsManager (hub+0x658)
constexpr int kTypeLoans = 57;          // LoansManager (hub+0x738)
constexpr int kTypeReleaseCounter = 60;  // the manager ReleasePlayer bumps a counter of (+0xC5), hub+0x798
constexpr int kTypeMorale = 83;         // PlayerMoraleManager (hub+0xA78)
constexpr int kTypeTeamUtil = 120;      // TeamUtil (hub+0xF18)

// Object layouts (docs/re/realtime_transfers.md section 1; [H] unless marked)
constexpr uint64_t kDcProvider = 0x00, kDcHub = 0x10, kDcTail = 0x18;  // DataController: no vtable of its own; +0 db provider, +0x10 hub
constexpr uint64_t kTeamUtilHub = 0x00, kTeamUtilSize = 0x10;           // TeamUtil: {+0 hub, +8 unknown}
constexpr uint64_t kCtmSize = 0x18;                                      // ContractTerminationManager {+0 vtable, +8 hub, +0x10}
constexpr uint64_t kIniMax = 0x58, kIniMin = 0x64, kIniSize = 0x70;      // IniSettings MAX_SQUAD_SIZE / MIN_SQUAD_SIZE (read to +0x6C)
constexpr uint64_t kLoansBegin = 0x40, kLoansEnd = 0x48, kLoanRecord = 0x20;  // LoansManager runtime loans: {+0 pid, +4 returning club}
constexpr uint64_t kTmPendingBegin = 0x2F30, kTmPendingEnd = 0x2F38, kTmPendingRecord = 0x88;  // pre-signed deals: {+4 buyer team}
constexpr uint64_t kMoraleGate = 0x554;                                  // PlayerMoraleManager u8: 1 = no morale entries for new arrivals
constexpr uint64_t kCounterByte = 0xC5;                                  // the manager in slot 60: the byte ReleasePlayer increments
constexpr uint64_t kUserFinance = 0x2F0;                                 // User +0x2F0: the finance object ReleasePlayer calls (vtable slots 0 / 1 / 2)
constexpr uint64_t kPcmNodeTeam = 0x08, kPcmNodeWage = 0x0C;             // contract record: team and weekly wage
constexpr int kMaxLoanRecords = 20000, kMaxPendingRecords = 5000;
constexpr int kMinSquadFloor = 1, kMaxSquadCeiling = 1000;               // sanity bounds of the ini limits

// Game functions / anchors resolved by signature on the running build (0 = unknown). Names are the signature table's.
struct Fns {
    uint64_t player_moved = 0;          // void (TeamUtil*, int pid, int from, int to)                   "teamutil_player_moved"
    uint64_t is_player_in_team = 0;     // bool (DataController*, int pid, int team)                     "dc_is_player_in_team"
    uint64_t squad_counts = 0;          // void (DataController*, int team, int* rows, int* loaned, int* pending)   "dc_squad_counts"
    uint64_t league_of_team = 0;        // int (DataController*, int team) -> leagueid, -1 unknown       "dc_get_league_of_team"
    uint64_t is_international = 0;      // bool (int leagueid): leagues 78 / 2136 / 3004                 "is_international_league"
    uint64_t add_contract = 0;          // void (PCM*, pid, team, months, wage, 100, Date*, status)      "pcm_add_contract_record"
    uint64_t release_player = 0;        // int (CTM*, int pid) -> CanRelease code                        "ctm_release_player"
    uint64_t pcm_vtable = 0;            // PlayerContractManager vtable                                  "pcm_vtable"
    uint64_t tm_vtable = 0;             // TransferManager vtable                                        "tm_vtable"
    uint64_t um_vtable = 0;             // UserManager vtable                                            "um_vtable"
    uint64_t ctm_vtable = 0;            // ContractTerminationManager vtable (release only)             "ctm_vtable"
    uint64_t morale_vtable = 0;         // PlayerMoraleManager vtable (optional: only the gate byte)     "morale_vtable"
    uint64_t morale_handle_event = 0;   // PlayerMoraleManager::HandleEvent = vtable slot 1 (optional)   "morale_handle_event"
    // Name of the first entry the action needs that is missing, nullptr when all are there
    const char* missing(int action) const;
};

// The calls into the game. The Windows host calls the resolved functions (game thread only); tests fake them over a synthetic
// game. Each returns false (and err) when the call could not run.
class Caller {
public:
    virtual ~Caller() = default;
    // DataController::IsPlayerInTeam (a read-only SELECT)
    virtual bool in_team(uint64_t dc, int pid, int team, bool& in, std::string& err) = 0;
    // DataController::SquadCounts (read-only): teamplayerlinks rows, players loaned out whose returning club is `team`, pre-signed deals
    // naming `team` as buyer. The outputs are preset to -1 by the caller: a team <= 0 leaves them untouched
    virtual bool squad_counts(uint64_t dc, int team, int& rows, int& loaned, int& pending, std::string& err) = 0;
    // DataController::GetLeagueOfTeam (-1 = the game knows no league for the team) and the game's IsInternationalLeague(league)
    virtual bool league_of_team(uint64_t dc, int team, int& league, std::string& err) = 0;
    virtual bool is_international(int league, bool& out, std::string& err) = 0;
    // TeamUtil::PlayerMoved
    virtual bool player_moved(uint64_t team_util, int pid, int from, int to, std::string& err) = 0;
    // PlayerContractManager::AddContractRecord; `date` is the address of the calendar's own date (CalendarManager+0x34)
    virtual bool add_contract(uint64_t pcm, int pid, int team, int months, int wage, int k, uint64_t date, int status, std::string& err) = 0;
    // ContractTerminationManager::ReleasePlayer; code = what it returned (CanRelease: 0 ok, 1 budget, 2 squad minimum)
    virtual bool release_player(uint64_t ctm, int pid, int& code, std::string& err) = 0;
};

struct Request {
    int action = 0;
    int player = 0;
    int from = 0, to = 0;     // team ids (to is ignored by kActionRelease)
    int months = 0;           // contract length for a user arrival (0 = no contract record)
    int wage = 0;             // weekly wage for that record
    uint64_t comm = 0;        // the FeFceGMCommService plugin (bridge_state.json comm_service)
    uint64_t managers = 0;    // the manager table Lua published, 0 = take [[comm+0x20]+0x10] (tests; the mailbox carries no such field)
    uint64_t image_base = 0, image_size = 0;  // FC27.exe range: vtables and function pointers must fall inside (0 = skip)
    std::string bad_args;     // set by request_from_args when the mailbox words are malformed: the call is refused with it
};

// The objects the call runs on, after validation
struct Located {
    uint64_t owner = 0, managers = 0, dc = 0, team_util = 0, pcm = 0, ctm = 0, dispatcher = 0, calendar = 0, ini = 0, loans = 0, tm = 0, um = 0,
             morale = 0;
    uint64_t date = 0;         // the calendar's date storage (CalendarManager+0x34): AddContractRecord's start date
    int user_team = 0;         // the user's club read from the UserManager
    int max_squad = 0, min_squad = 0;
    int morale_gate = -1;      // PlayerMoraleManager+0x554: 0 / 1, -1 = not read (morale_note says why)
    std::string morale_note;
};

struct Result {
    bool ok = false;
    std::string stage;    // "validate", "check", "call", "done" (host: "off", "queued")
    std::string message;  // for the user (toast / log / Lua)
    Located at;
    int from_ok = -1, to_ok = -1;         // the two read-backs (see the file comment): 1 / 0 / -1 not read
    bool called = false;                  // PlayerMoved / ReleasePlayer ran
    bool contract_called = false;         // AddContractRecord ran
    int release_code = -1;                // ReleasePlayer's return value (-1 = not called)
    int rows_to = -1, loaned_to = -1, pending_to = -1, rows_from = -1;  // the squad counts read before the call (-1 = not read)
};

// Find and validate everything the call dereferences (see the file comment). "" = fine, else the reason.
std::string locate(Memory& mem, const Request& req, const Fns& fns, Located& out);
// Is the pid in the LoansManager's runtime loan list (his team in the record's +4 is reported)? "" = fine, else the reason.
std::string loan_state(Memory& mem, uint64_t loans, int pid, bool& loaned, int& club);
// The whole call (see the file comment). Never throws.
Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req);

// ---------------------------------------------------------------- mailbox words and the kill switch
// Decodes args[4] of a call block (layout above); a malformed word sets Request::bad_args
Request request_from_args(const int64_t args[4]);
// The inverse (tests, documentation): what Lua writes
void args_from_request(const Request& req, int64_t args[4]);
// File whose presence turns this call off: <turbo_output>/call_player_move_off.txt
inline const char* kill_switch_name() { return "call_player_move_off.txt"; }
bool killed(const std::filesystem::path& turbo_output);

}  // namespace pm
}  // namespace turbo
