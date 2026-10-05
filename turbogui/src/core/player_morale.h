// FC 27 LE Turbo GUI - "player_morale" game call: set one player of the USER's club to the game's own "very happy" level for his
// emotion type (or an explicit total) through PlayerMoraleManager::SetTotalMorale, which also refreshes the dynamic overall's morale
// modifier; and count the stale morale records (players no longer at the club). Platform-independent part; src/win/player_morale_win.cpp
// resolves the functions and runs the call on the game thread. Research: docs/re/player_status_roles.md section 4.
//
// Why: Live Editor's SetPlayerMorale(pid, 100) writes 100, which is the TOP level "Complacent" (+0 OVR), not "Very happy".
// The game (FC27.exe 1.0.140.64835, [H], read for this call):
//   int GetMoraleLevel(pmm, int total, int emotion) 0x147D837D8: e = clamp(emotion, 0, 4); t = (int*)(pmm + (e + 2) * 0x18) (six ints, the
//     MORALE_LEVELS_%s ini values per emotion type); level = 5 when total >= t[5], else (first i in 1..5 with total < t[i]) - 1.
//   Levels (0x147F51504 builds the text from it): 0 CM_Morale_VeryUnhappy, 1 CM_Morale_Unhappy, 2 content, 3 happy, 4 VERY HAPPY,
//     5 complacent; ini keys VERY_LOW, LOW, NORMAL, HIGH, VERY_HIGH, COMPLACENT (0x1405F982C).
//   void SetTotalMorale(pmm, rec, total) 0x147D960A8: rec+0x2C = total; when the level changed: DynamicOverallManager refresh (no clamp).
//   MoraleRecord* MoraleStore::Find(store = pmm+0x518, pid) 0x147D8B5E8.
// What Turbo does: validate everything (PlayerMoraleManager vtable + slot 1 HandleEvent + back pointer, store team == the user's club,
// gate byte +0x554 == 0, the 0x60-byte record vector sane, at most 52 records, the player in the user's club by the game's IsPlayerInTeam,
// the record found by the game's Find lies inside the vector and holds the pid), pick the target (mode very happy: the highest total in
// 0..120 whose level from the game's own level function is 4 for HIS emotion (rec+4); 85 when the level function is not resolved or no
// total gives level 4, said in the text), SetTotalMorale, read back rec+0x2C and the level.
// NOT done (count only): creating a missing record (players Turbo's old moves brought in have none: the game shows "Unknown") and
// removing stale records (the 0x60 handler erases inline; no callable erase was verified). Both are reported, nothing is written.
//
// Lua / mailbox contract (op kCallOpPlayerMorale = 13; Lua defines `TurboPlayerMorale(code, pid, value)`):
//   args[0] = comm service, args[1] = code (1 very happy, 2 explicit value, 3 count stale records, 9 check only), args[2] = pid
//   (0 for code 3), args[3] = value (code 2: 0..120)
//   out[0] = total read back after the call (code 9: before; code 3: stale record count), -1 = not read; -2 = no record (count only)
//   out[1] = level read back (0..5; code 3: records in the store), -1 = not read
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "mem.h"

namespace turbo {
namespace morale {

constexpr int kActionVeryHappy = 1, kActionValue = 2, kActionStale = 3, kActionCheck = 9;
inline bool valid_action(int a) { return a == kActionVeryHappy || a == kActionValue || a == kActionStale || a == kActionCheck; }
const char* action_name(int action);

constexpr int kLevelVeryHappy = 4;        // GetMoraleLevel bucket of "very happy" (0x147F51504: index 4 = morale_summary_veryhappy)
constexpr int kLevelCount = 6;
constexpr int kMaxTotal = 120, kFallbackTotal = 85;
constexpr int kTypeDataController = 32, kTypeMorale = 83, kTypeUserManager = 129;
constexpr uint64_t kPmmHub = 0x08, kStore = 0x518, kStoreTeam = 0x00, kStoreBegin = 0x10, kStoreEnd = 0x18, kGate = 0x554, kPmmSize = 0x5B8;
constexpr uint64_t kRecSize = 0x60, kRecPid = 0x00, kRecEmotion = 0x04, kRecTotal = 0x2C;
constexpr int kMaxRecords = 52;
constexpr int64_t kNoRecord = -2;

struct Fns {
    uint64_t pmm_vtable = 0;      // "pmm_vtable"
    uint64_t pmm_handle_event = 0; // "pmm_handle_event" (vtable slot 1)
    uint64_t find_record = 0;     // "pmm_find_record"
    uint64_t set_total = 0;       // "pmm_set_total"
    uint64_t level = 0;           // "pmm_get_level" (optional: 85 without it)
    uint64_t is_player_in_team = 0;  // "dc_is_player_in_team"
    uint64_t um_vtable = 0;       // "um_vtable"
    const char* missing() const;
};

class Caller {
public:
    virtual ~Caller() = default;
    virtual bool in_team(uint64_t dc, int pid, int team, bool& in, std::string& err) = 0;
    virtual bool find_record(uint64_t store, int pid, uint64_t& rec, std::string& err) = 0;
    virtual bool level(uint64_t pmm, int total, int emotion, int& out, std::string& err) = 0;
    virtual bool set_total(uint64_t pmm, uint64_t rec, int total, std::string& err) = 0;
};

struct Request {
    int action = 0, player = 0, value = 0;
    uint64_t comm = 0, image_base = 0, image_size = 0;
    std::string bad_args;
};

struct Result {
    bool ok = false;
    std::string stage, message;
    uint64_t pmm = 0, dc = 0, rec = 0;
    int user_team = 0;
    int records = -1, stale = -1;
    int64_t total = -1, level = -1;  // out[0] / out[1]
    int target = -1, before = -1;
    bool called = false;
};

// The target total for `emotion`: the highest t in 0..120 with level(t) == 4; fallback 85 (note says why)
int very_happy_target(Caller& call, uint64_t pmm, int emotion, bool have_level, std::string& note);
Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req);

Request request_from_args(const int64_t args[4]);
void args_from_request(const Request& req, int64_t args[4]);
inline const char* kill_switch_name() { return "call_player_morale_off.txt"; }
bool killed(const std::filesystem::path& turbo_output);

}  // namespace morale
}  // namespace turbo
