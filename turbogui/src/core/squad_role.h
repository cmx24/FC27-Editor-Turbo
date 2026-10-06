// FC 27 LE Turbo GUI - "squad_role" game call: the squad role entries of the USER's club in the career's PlayerStatusManager,
// through the game's own functions. The Squad hub, morale, the contract code and the match load read the role from this table
// (the match load without a null check: a club player without an entry crashed it, FC27.exe+0x7FCCDF0, 2026-10-05). Players
// Turbo's old database moves brought in, or created, never got an entry (the game adds one only on PlayerJoinedTeam 0x5F).
// Platform-independent part; src/win/squad_role_win.cpp resolves the functions and runs the call on the game thread.
// Research: docs/re/player_status_roles.md sections 3 and 7.2; Lua's memory-only equivalent: features/squad_role.lua.
//
// The game (FC27.exe 1.0.140.64835, [H], re-read for this call):
//   PlayerStatusManager (manager type 87, 0x38 bytes, vtable 0x14AFFB2A0, slot 1 HandleEvent 0x147BF30C0): +0x08 hub, +0x10 the
//     "status data" sub = {+0 int32 team (the user's club, -1 before init), +4 int32 count of used entries 0..52, +8 begin, +0x10 end,
//     +0x18 cap} with end == cap == begin + 52 * 8 (a fixed table). Entry (8 bytes): {int32 pid (-1 = empty), int8 role (1 Crucial,
//     2 Important, 3 Rotation, 4 Sporadic, 5 Prospect, 0xFF none), u8 wasPromised, u8 renewalDismissed, pad}; used entries first.
//   Entry* Find(sub, pid) 0x147D8BADC: scans entries 0..51, stops at pid -1.
//   bool AddPlayer(psm, pid, uint8 role) 0x147BE7BB4: AddEntry(sub, pid) (no duplicate check; refuses at 52), then role 1..5 stored
//     with wasPromised = 1, or for 0 / 0xFF the game's ComputeSquadRole(hub, team, pid) 0x147E9DED8 (squad rank) with wasPromised 0;
//     renewalDismissed = 0. The 0x5F handler calls it for every player joining the user's club (loans included: no loan test), the
//     Squad screen's peek 0x147F4C4A0 with 0xFF for a player without an entry (then reads the role and Removes the entry again).
//   bool Remove(sub, pid) 0x147D90C80: erase by memmove inside the fixed table, count--, an empty entry appended (the 0x60 handler,
//     the same peek).
//   Nothing else writes entry+4; readers take it each time a screen builds its rows (reopen the Squad screen to see a change).
//   Event 0x17 (SEASON_RESET) rebuilds the whole table from the squad ranking: roles set by Turbo do not survive it.
// What Turbo does: validate everything (vtable + slot 1 + back pointer, the table's team == the user's club, count 0..52, the table
//   exactly 52 entries long and readable, used entries {pid > 0, role 1..5 or 0xFF}, empty entries pid -1, no pid twice), the player
//   in the user's club by the game's IsPlayerInTeam, the game's Find agreeing with Turbo's own scan; then
//   * code 1 (set), entry found: write ONE byte (entry+4), the two flags after it stay; read back through Find;
//   * code 1, no entry: AddPlayer(psm, pid, role), then wasPromised back to 0 (Turbo promises nothing: the state of the game's own
//     computed roles); read back: one entry more, at the end, Find returns it with the pid and the role;
//   * code 2 (ensure), no entry: AddPlayer(psm, pid, 0xFF): the game computes the role as for a signing without a contract role;
//     an existing entry is left alone;
//   * code 4 (remove stale): the entry of a player who is NOT at the club (IsPlayerInTeam false) through the game's Remove, as the
//     0x60 handler does; read back: one entry less and the pid gone. Lua picks which (never a player your club lent out).
//   A full table (52) is refused by codes 1 / 2 (Lua makes room with code 4 first). Creation and removal are off with
//   turbo_output\call_squad_role_create_off.txt (count only).
// No SimDayManager gate: the game itself rewrites this table in the middle of a day (0x5F / 0x60 during transfers, 0x17 at the
// season reset) on the same thread Turbo's call runs on.
//
// Lua / mailbox contract (op kCallOpSquadRole = 14; Lua defines `TurboSquadRole(code, pid, role)`):
//   args[0] = comm service, args[1] = code (1 set, 2 ensure entry, 3 count stale, 4 remove stale entry, 9 check only),
//   args[2] = pid (0 for code 3), args[3] = role 1..5 (code 1 only)
//   out[0] = codes 1 / 2: the role read back (1..5, 255 = none), -2 = no entry and none created (count only), -1 = not read;
//            code 9: the role now, -2 = no entry; code 3: entries of players no longer at the club; code 4: 1 removed, 0 no entry
//   out[1] = codes 1 / 2: how (0 the byte written, 1 entry created, 2 unchanged), -1 = nothing done; codes 3 / 4 / 9: used entries
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "mem.h"

namespace turbo {
namespace roles {

constexpr int kActionSet = 1, kActionEnsure = 2, kActionStale = 3, kActionRemoveStale = 4, kActionCheck = 9;
inline bool valid_action(int a) {
    return a == kActionSet || a == kActionEnsure || a == kActionStale || a == kActionRemoveStale || a == kActionCheck;
}
const char* action_name(int action);
const char* role_name(int role);  // 1 "Crucial" .. 5 "Prospect", 0xFF "none"

constexpr int kRoleMin = 1, kRoleMax = 5;
constexpr uint8_t kRoleNone = 0xFF;
constexpr int kTypeDataController = 32, kTypeStatus = 87, kTypeUserManager = 129;
constexpr uint64_t kPsmHub = 0x08, kPsmSub = 0x10, kPsmTeam = 0x10, kPsmCount = 0x14, kPsmBegin = 0x18, kPsmEnd = 0x20, kPsmCap = 0x28,
                   kPsmSize = 0x38;
constexpr uint64_t kEntrySize = 8, kEntryPid = 0, kEntryRole = 4, kEntryPromised = 5, kEntryDismissed = 6;
constexpr int kMaxEntries = 52;
constexpr uint64_t kTableBytes = kEntrySize * kMaxEntries;  // 0x1A0
constexpr int64_t kNoEntry = -2;
constexpr int kHowWritten = 0, kHowCreated = 1, kHowUnchanged = 2;

struct Fns {
    uint64_t psm_vtable = 0;         // "psm_vtable"
    uint64_t psm_handle_event = 0;   // "psm_handle_event" (vtable slot 1)
    uint64_t find = 0;               // "psm_get_entry"
    uint64_t is_player_in_team = 0;  // "dc_is_player_in_team"
    uint64_t um_vtable = 0;          // "um_vtable"
    // optional: a missing entry is created only with AddPlayer, a stale one removed only with Remove
    uint64_t add_player = 0;         // "psm_add_player"
    uint64_t remove = 0;             // "psm_remove_entry"
    const char* missing() const;
};

class Caller {
public:
    virtual ~Caller() = default;
    virtual bool in_team(uint64_t dc, int pid, int team, bool& in, std::string& err) = 0;
    virtual bool find(uint64_t sub, int pid, uint64_t& entry, std::string& err) = 0;
    // the defaults refuse, so a caller without them never creates / removes
    virtual bool add_player(uint64_t, int, int, bool&, std::string& err) { return (err = "not supported", false); }
    virtual bool remove(uint64_t, int, bool&, std::string& err) { return (err = "not supported", false); }
};

struct Request {
    int action = 0, player = 0, role = 0;
    uint64_t comm = 0, image_base = 0, image_size = 0;
    bool create_off = false;  // turbo_output\call_squad_role_create_off.txt: no entry created, none removed (count only)
    std::string bad_args;
};

struct Entry {
    int32_t pid = -1;
    uint8_t role = kRoleNone, promised = 0, dismissed = 0;
};

// The validated table (section 7.2): 52 entries, `count` used
struct Table {
    uint64_t begin = 0;
    int count = 0;
    std::vector<Entry> entries;
    int index_of(int pid) const;  // -1 when absent (used entries only)
};

struct Result {
    bool ok = false;
    std::string stage, message;
    uint64_t psm = 0, dc = 0, entry = 0;
    int user_team = 0;
    int count = -1, stale = -1;
    int before = -1;     // the role before (codes 1 / 2 / 9), -2 = no entry
    int64_t role = -1;   // out[0]
    int how = -1;        // kHow* (codes 1 / 2)
    bool written = false, created = false, removed = false;
};

// Reads and validates the table of `psm` (its team must be `user_team`); "" when it is the layout Turbo knows
std::string read_table(Memory& mem, uint64_t psm, int user_team, Table& out);
Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req);
int64_t out1(const Request& req, const Result& r);  // the second output word (see the contract above)

Request request_from_args(const int64_t args[4]);
void args_from_request(const Request& req, int64_t args[4]);
inline const char* kill_switch_name() { return "call_squad_role_off.txt"; }
bool killed(const std::filesystem::path& turbo_output);
inline const char* create_kill_switch_name() { return "call_squad_role_create_off.txt"; }
bool create_killed(const std::filesystem::path& turbo_output);

}  // namespace roles
}  // namespace turbo
