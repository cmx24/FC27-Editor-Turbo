// FC 27 LE Turbo GUI - "player_create" game call: add a new player to the career database through the game's OWN database layer, so
// every index, observer and cache the game keeps sees him at once (Squad Hub, Team Management, player search, morale, contract) with
// no save / load (platform-independent part; src/win/player_create_win.cpp resolves the functions and runs the call on the game thread).
// Static research: docs/re/created_players.md (sections 1, 4 and the implementation plan of section 6, design C).
//
// Why: Live Editor's InsertDBTableRow writes the record into the table's memory but never into the table's indexes (T3db AddRecord is
// skipped), so the game's own queries (IsPlayerInTeam, the squad lists, the player search) miss the row until a save + load.
// What the game does when it creates a player itself (FC27.exe 1.0.140.64835, [H]):
//   Query::Init(&q, 4 INSERT, "players") / SetInt / SetString, provider->vf[1](provider, &q, &holder) (provider = *(DataController+0)),
//   count = holder->vf[1]() (CreatePlayer requires 1), ResultHolder::Release(&holder), Query::~Query(&q)   -> AddRecord: indexes,
//   observers, free list, capacity; then event 0x3A {+0 vtable 0x14AFF67C0, +8 refcount 0, +0x10 0x3A, +0x18 pid} allocated from the
//   global allocator *(0x14C269EA8) vf[2](size 0x20, name, 0) and posted with PostEvent(**(hub+0x4F8), 0x3A, ev); then
//   DataController::InsertTeamPlayer(dc, pid, 111592 Free Agents, 99, 29 reserve, 0) (event 0x5F) and TeamUtil::PlayerMoved(FA -> club)
//   (the game's own "create into a club" 0x147E0BC28). Names: INSERT editedplayernames (playerid, firstname, surname, commonname,
//   playerjerseyname) as SetEditedPlayerName does (44 bytes + NUL per name).
//
// What Turbo does (every pointer validated through turbo::Memory first, every call into the game behind Caller so the whole sequence
// runs on a synthetic game in the native tests):
//   validate: the payload (seq + playerid match the call, ints and strings only, known tables / columns, strings <= 44 bytes), the
//     objects (pm::locate: comm -> hub, DataController, TeamUtil, PCM, TM, dispatcher, calendar, ini, loans, user; plus the provider's
//     vtable slot 1 == the resolved Execute, the event allocator, the 0x3A event vtable), no match day running, the id is new (no
//     players / teamplayerlinks / editedplayernames row: the game's own SELECT), the target team (Free Agents or a club, never a
//     national or pseudo team: pm::check_team), the club's squad room (SquadCounts + 1 <= MAX_SQUAD_SIZE);
//   create: INSERT players (playerid + the first columns, count must be 1), UPDATE players for the remaining columns in statements of
//     at most kColumnsPerStatement (the game's own INSERT has ~59 setters, UpdatePlayerAttributes ~61), SELECT read-back (exactly 1
//     row, every int value compared); INSERT editedplayernames (count 1, read back); event 0x3A; InsertTeamPlayer(pid, FA, 99, 29, 0)
//     and the read-back IsPlayerInTeam(pid, FA) (the check that failed live with raw rows); for a club pm::run(MOVE, FA -> club,
//     months, wage) (op 11's whole chain: squad limits, contract record for the user's club, read-backs); the link extras (form)
//     through UPDATE teamplayerlinks WHERE playerid AND teamid, read back; IsPlayerInTeam(pid, final team).
//   A failure after the players INSERT reports what exists (written mask): nothing is deleted behind the user's back.
//
// Lua / mailbox contract (op kCallOpPlayerCreate = 12; Lua defines `TurboPlayerCreate(code, payload)` on top of it):
//   Lua writes turbo_output\turbo_player_create.json:
//     {"seq": n, "playerid": pid, "team": t, "months": m, "wage": w,
//      "players": {"col": int, ...}, "names": {"firstname": "..", ...}, "link": {"form": 3}}
//   args[0] = the FeFceGMCommService plugin, args[1] = code (1 create, 9 check only), args[2] = seq, args[3] = playerid
//   out[0] = written mask (kWrote*; 0 = nothing written; -1 = the call is OFF / not available: nothing was called, use the database
//            path), out[1] = IsPlayerInTeam(pid, final team) after the call (1 / 0 / -1 not read; code 9: 1 = the id is free)
//   text = the sentence for the user (at most 511 characters); status = kCallOk / kCallFailed / kCallQueued.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "player_move.h"
#include "t3db.h"

namespace turbo {
namespace pc {

constexpr int kActionCreate = 1;  // the whole sequence
constexpr int kActionCheck = 9;   // the whole validation chain, nothing written
inline bool valid_action(int a) { return a == kActionCreate || a == kActionCheck; }
const char* action_name(int action);

constexpr int kFaJersey = 99, kFaPosition = 29;      // InsertTeamPlayer(pid, FA, 99, 29, 0) as the game's create-into-club does
constexpr int kEventPlayerInserted = 0x3A;            // PlayerInsertedIntoPlayers
constexpr uint64_t kEventSize = 0x20;                 // {+0 vtable, +8 refcount, +0x10 id, +0x18 pid}
constexpr uint64_t kEvVtable = 0x00, kEvRefcount = 0x08, kEvId = 0x10, kEvPid = 0x18;
constexpr int kMaxPlayerId = 460000;                  // exclusive: 460000+ is the game's own generated range
constexpr size_t kMaxString = 44;                     // editedplayernames columns: 360 bits = 44 bytes + NUL
constexpr size_t kColumnsPerStatement = 48;           // below the game's own ~59 setters in one INSERT
constexpr size_t kMaxColumns = 400;                   // players has 152 columns; anything far above is not a players row
constexpr size_t kMaxPayloadBytes = 256 * 1024;
constexpr int64_t kIntMin = -2147483648LL, kIntMax = 2147483647LL;

// written mask (out[0])
constexpr int kWrotePlayers = 1, kWroteNames = 2, kWroteEvent = 4, kWroteFaLink = 8, kWroteMoved = 16, kWroteContract = 32, kWroteLink = 64;
constexpr int64_t kOutOff = -1;  // out[0] when the call is off / not available (nothing was called: Lua uses the database path)

// The editedplayernames columns the game writes (SetEditedPlayerName); the only names a payload may carry
const std::vector<std::string>& name_columns();
// The teamplayerlinks columns a payload's "link" may set after the move (UPDATE ... WHERE playerid AND teamid)
const std::vector<std::string>& link_columns();

struct Value {
    std::string column;
    bool is_string = false;
    int64_t i = 0;
    std::string s;
};
using Row = std::vector<Value>;
using Where = std::vector<std::pair<std::string, int>>;  // AND-joined "col = value"

struct Payload {
    int seq = 0, playerid = 0, team = 0, months = 0, wage = 0;
    Row players;  // playerid first, then the other columns (alphabetical: nlohmann's object order)
    Row names;    // only non-empty names
    Row link;
};
// Parses the payload file's text. "" = fine, else the reason (floats, booleans, nulls, unknown keys, long strings, ...)
std::string parse_payload(const std::string& text, Payload& out);

// The database columns the DLL knows (from bridge_meta.json, long names): table -> columns. Empty = not known (the call is refused)
using Columns = std::map<std::string, std::set<std::string>>;
Columns columns_from_meta(const DbMeta& meta);

// Game functions / anchors (0 = unknown). Names are the signature table's.
struct Fns {
    pm::Fns move;                    // op 11's set (PlayerMoved, IsPlayerInTeam, SquadCounts, ... AddContractRecord)
    uint64_t query_init = 0;         // void Query::Init(Query*, int type, const char* table)              "db_query_init"
    uint64_t query_set_int = 0;      // void Query::SetInt(Query*, const char* col, int)                   "db_query_set_int"
    uint64_t query_set_string = 0;   // void Query::SetString(Query*, const char* col, const char*)        "db_query_set_string"
    uint64_t query_select = 0;       // void Query::Select(Query*, const char* col)                        "db_query_select_field"
    uint64_t query_where_int = 0;    // void Query::Where(Query*, const char* col, u8 op, int)             "db_query_where_int"
    uint64_t query_destroy = 0;      // void Query::~Query(Query*)                                         "db_query_destroy"
    uint64_t result_free = 0;        // void ResultHolder::Release(void** holder)                          "db_result_free"
    uint64_t provider_execute = 0;   // bool Provider::Execute(provider, Query*, void** holder) = vf[1]     "db_provider_execute"
    uint64_t event_allocator = 0;    // the global holding the allocator events come from (0x14C269EA8)    "event_allocator_global"
    uint64_t event_base_vtable = 0;  // the event base vtable CreatePlayer writes first                     "event_base_vtable"
    uint64_t inserted_vtable = 0;    // event 0x3A PlayerInsertedIntoPlayers vtable                         "player_inserted_event_vtable"
    uint64_t insert_team_player = 0; // int DataController::InsertTeamPlayer(dc, pid, team, jersey, pos, suppress)  "dc_insert_team_player"
    uint64_t post_event = 0;         // void PostEvent(dispatcher, int id, Event*)                         "post_career_event"
    // Name of the first entry that is missing, nullptr when all are there
    const char* missing() const;
};

// The calls into the game (pm::Caller's reads and moves plus the database layer and the events). The Windows host calls the resolved
// functions (game thread only); tests fake them over a synthetic game. Each returns false (and err) when the call could not run.
class Caller : public pm::Caller {
public:
    // INSERT INTO table (cols) VALUES (...) through the DataController's provider; count = rows affected (the game requires 1)
    virtual bool insert_row(uint64_t dc, const std::string& table, const Row& cols, int& count, std::string& err) = 0;
    // UPDATE table SET cols WHERE ... (the game does not check an UPDATE's count: Turbo reads back instead)
    virtual bool update_row(uint64_t dc, const std::string& table, const Row& cols, const Where& where, std::string& err) = 0;
    // SELECT cols FROM table WHERE ...: rows = the row count, values = the first row's ints (cols empty: count only)
    virtual bool select_ints(uint64_t dc, const std::string& table, const std::vector<std::string>& cols, const Where& where, int& rows,
                             std::vector<int64_t>& values, std::string& err) = 0;
    // The game's event allocator (vf[2](size, name, 0)): ev = the new block (0 = out of memory)
    virtual bool alloc_event(uint64_t size, uint64_t& ev, std::string& err) = 0;
    // PostEvent(dispatcher, id, ev): the listeners run inside; the event is released by the dispatcher
    virtual bool post_event(uint64_t dispatcher, int id, uint64_t ev, std::string& err) = 0;
    // DataController::InsertTeamPlayer(dc, pid, team, jersey, position, suppress) -> its return value
    virtual bool insert_team_player(uint64_t dc, int pid, int team, int jersey, int position, int suppress, int& ret, std::string& err) = 0;
};

struct Request {
    int action = 0;
    int seq = 0, player = 0;   // from the mailbox words: must equal the payload's
    uint64_t comm = 0, managers = 0, image_base = 0, image_size = 0;
    Payload payload;
    std::string payload_error;  // set by the host when the file could not be read / parsed: the call is refused with it
    Columns columns;            // the known database columns (bridge_meta.json)
    std::string bad_args;
};

struct Result {
    bool ok = false;
    std::string stage;    // "validate", "call", "check", "done" (host: "off", "queued", "busy")
    std::string message;
    pm::Located at;
    int written = 0;      // kWrote* mask
    int in_team = -1;     // IsPlayerInTeam(pid, final team) after the call: 1 / 0 / -1 not read (code 9: 1 = the id is free)
    int final_team = 0;
    std::vector<std::string> calls;  // the game calls made, in order (log)
};

// Find and validate everything the call dereferences. "" = fine, else the reason.
std::string locate(Memory& mem, const Request& req, const Fns& fns, pm::Located& out);
// The whole call (see the file comment). Never throws.
Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req);

// ---------------------------------------------------------------- mailbox words, the payload file, the switches
Request request_from_args(const int64_t args[4]);
void args_from_request(const Request& req, int64_t args[4]);
inline const char* payload_name() { return "turbo_player_create.json"; }
// The call is OFF unless this file exists (opt-in until the first live test passes)
inline const char* opt_in_name() { return "call_player_create_on.txt"; }
// ... and off whenever this one exists (kill switch, as every game call)
inline const char* kill_switch_name() { return "call_player_create_off.txt"; }
bool opted_in(const std::filesystem::path& turbo_output);
bool killed(const std::filesystem::path& turbo_output);
// Reads + parses the payload file into req (payload_error on failure)
void load_payload(const std::filesystem::path& turbo_output, Request& req);

}  // namespace pc
}  // namespace turbo
