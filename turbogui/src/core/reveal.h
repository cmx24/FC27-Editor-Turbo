// FC 27 LE Turbo GUI - "reveal player data" game call: show a player's (or a whole club's) true attributes and
// potential in the game's own screens, like FC 26 Live Editor's "Reveal player data" (platform-independent part;
// src/win/reveal_win.cpp resolves the functions and runs the call on the game thread).
//
// How the game hides data (docs/re/development.md): the career's PlayerDataRevealManager (PDRM, Live Editor manager
// type id 78, 0x7D0 bytes, hub slot +0x9D8) keeps one 0x14-byte record per player the user has knowledge of, in a
// sorted vector at pdrm+0x790 (begin / end / capacity, at most 1500 records: the game evicts the oldest beyond that):
//   +0x00 int  playerId      +0x04 i16 scoutId (-1 none)   +0x06 u16 flags
//   +0x08 int  points        scouting progress; 204 (= 6 levels x 34 attributes) is "fully revealed"
//   +0x0C int  day           yyyymmdd the record was made     +0x10 u8 -1
// Every screen asks PDRM::GetPlayerRevealData(pdrm, pid, ...) (0x147E38D54), which turns the record into the per-
// attribute "exact / range / hidden" state the Player Bio and the GTN show. Without a record (or with few points) the
// potential is a "?" and the attributes are ranges.
//
// What Turbo calls: the game's own
//   PDRM::RevealPlayerFully(pdrm, playerId)   0x147E325F0   record = {pid, -1, 0, 204, today, -1} if the player exists
//   PDRM::RevealTeamFully(pdrm, teamId)       0x147E32678   RevealPlayerFully for every player of the club
// which the game itself runs from the PDRM's event handler when a player joins the user's club (events 0x3E/0x4A/
// 0x60) and when the user takes a club. Nothing is allocated or laid out by Turbo: the record is written by the game's
// own insert. Turbo validates the manager first (vtable, size, the hub's own slot for it, the record vector's shape
// and order) and reads the record back afterwards (204 points, dated today) as the proof.
// Every read goes through turbo::Memory; the calls are behind RevealCaller so the sequence is tested on synthetic memory.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mem.h"

namespace turbo {
namespace pdrm {

// PlayerDataRevealManager layout, FC27.exe 1.0.140.64835 (docs/re/development.md, every offset [H])
constexpr int kTypeId = 78;              // ENUM_FCEGameModesFCECareerModePlayerDataRevealManager
constexpr uint64_t kSize = 0x7D0;        // allocation size in the hub builder (0x147F19119: mov ecx, 0x7D0)
constexpr uint64_t kHub = 0x08;          // CareerHub*
constexpr uint64_t kHubPdrm = 0x9D8;     // hub slot (Manager**) that holds this manager: [[hub+0x9D8]] == pdrm
constexpr uint64_t kHubCalendar = 0x318; // TodayInt reads *(slot) + 0x34
constexpr uint64_t kHubTeams = 0x418;    // RevealPlayerFully checks the player row through the teams manager
constexpr uint64_t kCalendarDate = 0x34;
constexpr uint64_t kRecords = 0x790;     // record* begin
constexpr uint64_t kRecordsEnd = 0x798;  // record* end
constexpr uint64_t kRecordsCap = 0x7A0;  // record* capacity end
constexpr uint64_t kRecordSize = 0x14;
constexpr uint64_t kRecPlayer = 0x00, kRecScout = 0x04, kRecFlags = 0x06, kRecPoints = 0x08, kRecDay = 0x0C, kRecTail = 0x10;
constexpr int32_t kFullPoints = 0xCC;    // 204: level 7 of 7 (6 levels x 34 attributes), the game's "fully scouted"
constexpr uint32_t kMaxRecords = 1500;   // 0x5DC: the game's own cap (ReleasePlayersForInstruction evicts beyond it)
constexpr uint64_t kRvaVtable = 0xB01D5B0;  // the constructor's lea (0x147E2C113)

// Game functions resolved by signature on the running build (0 = unknown)
struct Fns {
    uint64_t vtable = 0;         // PlayerDataRevealManager vtable (the pointer check)
    uint64_t reveal_player = 0;  // void (PDRM*, int playerId)
    uint64_t reveal_team = 0;    // void (PDRM*, int teamId)
    uint64_t today_int = 0;      // int (CalendarDate*) -> yyyymmdd (optional: only for the date check)
    // Name of the first required function that is missing, nullptr when all are there
    const char* missing() const;
};

// One reveal record as the game stores it
struct Record {
    int32_t player = 0;
    int16_t scout = -1;
    uint16_t flags = 0;
    int32_t points = 0;
    int32_t day = -1;
};

// The calls into the game. The Windows host calls the resolved functions (game thread only); tests fake them.
class Caller {
public:
    virtual ~Caller() = default;
    virtual bool reveal_player(uint64_t pdrm, int player, std::string& err) = 0;
    virtual bool reveal_team(uint64_t pdrm, int team, std::string& err) = 0;
    // Today's date as the game's calendar sees it (yyyymmdd); false when not available (the check is then weaker)
    virtual bool today(uint64_t pdrm, int& yyyymmdd, std::string& err) = 0;
};

enum class Mode : int { Player = 0, Team = 1 };

struct Request {
    uint64_t pdrm = 0;      // PlayerDataRevealManager (Lua's mem.manager(78) or the capture hook's pointer)
    uint64_t managers = 0;  // the manager table it must sit in (slot 78); 0 = skip that cross-check
    Mode mode = Mode::Player;
    int id = 0;             // player id (Player) or team id (Team)
};

struct Result {
    bool ok = false;
    std::string stage;    // where it stopped: "validate", "call", "check", "done" (host: "off", "queued")
    std::string message;  // for the user (toast / log / Lua)
    int records_before = 0, records_after = 0;  // reveal records the manager holds
    int points_before = -1, points_after = -1;  // the player's record (Player mode; -1 = no record)
    int day = -1;                               // the record's date after the call (Player mode)
};

// The record vector: begin / count; "" when its shape is sane (begin <= end <= cap, whole records, <= 1500, readable),
// else the reason. `records` receives the records in stored order (empty when count is 0).
std::string read_records(Memory& mem, uint64_t pdrm, std::vector<Record>& records);
// The record of a player (the game's lower_bound over the sorted vector); false when absent or unreadable
bool find_record(Memory& mem, uint64_t pdrm, int player, Record& out);
// The pointer is a readable PlayerDataRevealManager with the expected vtable (0 = skip), its hub's slot +0x9D8 holds
// it, the hub slots the game's own code dereferences during the call are readable, `managers` slot 78 holds it
// (0 = skip), and the record vector is sane and sorted. "" = fine, else the reason. Nothing is written.
std::string validate(Memory& mem, uint64_t pdrm, uint64_t vtable, uint64_t managers);
// Which manager the call uses: Lua's pointer (found through the manager table) or the one the capture hook saw in the
// game's own events. 0 = refuse (`err` says why). A Lua pointer that differs from the captured one is accepted only
// with the manager table along (validate then proves it through slot 78: the captured pointer can be the previous
// career's until the new one sends an event); without it the two must agree.
uint64_t choose(uint64_t lua_pdrm, uint64_t managers, uint64_t seen, std::string& err);
// The whole call (see the file comment). Never throws.
Result reveal(Memory& mem, Caller& call, const Fns& fns, const Request& req);

}  // namespace pdrm
}  // namespace turbo
