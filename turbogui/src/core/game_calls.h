// FC 27 LE Turbo GUI - calls into the game's own career code (platform-independent part; the Windows host
// src/win/game_calls_win.cpp resolves the functions and runs the calls on the game thread).
//
// First call: "create job offer" (docs/re/job_offer.md). The career's JobMarketManager (JMM, Live Editor manager
// type 53) keeps the user's job applications and the offers that answer them in one hash table keyed by team id.
// Turbo makes the game do the work with its own functions:
//   1. JobMarketManager::ApplyForJob(jmm, team) unless HasApplication(jmm, team): the game computes the wage, creates
//      the node and posts the "applied" action;
//   2. the node is found by the same bucket walk HasApplication does; offer.mTeamId / offer.mWage are set exactly as
//      OnResponseDue does before it makes an offer; the countdown is zeroed so DAY_PASSED does not answer again;
//   3. JobMarketManager::MakeOffer(jmm, &node.offer): stamps mJobOfferSentDay = today and posts the JobOfferAction
//      event that the inbox / Job Offers screen react to.
// Every read and write goes through turbo::Memory with bounds checks; nothing is allocated by Turbo. The calls
// themselves are behind GameCaller so the sequence is tested on synthetic memory with a fake game.
//
// Lua reaches the call through the mailbox "call block" (bridge.h, +0x2020): Lua writes op + arguments, calls the
// exported turbo_game_call() (package.loadlib), the DLL runs the call on the game thread (at once when the caller is
// the game thread, else queued for the dispatcher) and writes status / outputs / text back.
#pragma once
#include <cstdint>
#include <string>

#include "mem.h"

namespace turbo {

// JobMarketManager layout, FC27.exe 1.0.140.64835 (docs/re/job_offer.md, every offset [H])
namespace jmm {
constexpr uint64_t kSize = 0xC90;
constexpr uint64_t kHub = 0x08;              // CareerHub*
constexpr uint64_t kBuckets = 0x8F0;         // node** (bucketCount + 1 entries; the last one is the end sentinel)
constexpr uint64_t kBucketCount = 0x8F8;     // u32
constexpr uint64_t kElementCount = 0x8FC;    // u32
// application node (bucket chain element)
constexpr uint64_t kNodeKey = 0x00;          // int teamId
constexpr uint64_t kNodeTeam = 0x04;         // int value.teamId
constexpr uint64_t kNodeOffer = 0x08;        // JobOffer (0x18 bytes)
constexpr uint64_t kNodeWage = 0x20;         // int (set by ApplyForJob)
constexpr uint64_t kNodeCountdown = 0x24;    // int days until the club answers
constexpr uint64_t kNodeNext = 0x28;         // node*
constexpr uint64_t kNodeSize = 0x30;
// JobOffer
constexpr uint64_t kOfferTeam = 0x00;        // int mTeamId
constexpr uint64_t kOfferLeague = 0x04;      // int mLeagueId
constexpr uint64_t kOfferSentDay = 0x08;     // int yyyymmdd, -1 = application pending
constexpr uint64_t kOfferWage = 0x0C;        // int mWage
constexpr uint64_t kOfferAccepted = 0x14;    // u8 mIsAccepted
// CareerHub slots the sequence touches (each a Manager**)
constexpr uint64_t kHubX198 = 0x198;         // rng owner (AddApplication)
constexpr uint64_t kHubCalendar = 0x318;     // TodayInt reads *(slot) + 0x34
constexpr uint64_t kHubTeams = 0x418;        // ApplyForJob: team -> league
constexpr uint64_t kHubDispatcher = 0x4F8;   // PostEvent target
constexpr uint64_t kHubX6d8 = 0x6D8;         // ApplyForJob reads [obj+0x1e1]
constexpr uint64_t kHubXf38 = 0xF38;         // listener: (*slot + 0x98)->vtable[1 / 2]
constexpr uint64_t kCalendarDate = 0x34;
constexpr uint32_t kMaxBuckets = 1u << 20;
constexpr int kMaxChain = 1024;
}  // namespace jmm

// Game functions the job-offer call needs (resolved by signature on the running build; 0 = unknown)
struct JobMarketFns {
    uint64_t vtable = 0;           // JobMarketManager vtable (the pointer check)
    uint64_t has_application = 0;  // bool (JMM*, int team)
    uint64_t apply_for_job = 0;    // void (JMM*, int team)
    uint64_t make_offer = 0;       // void (JMM*, JobOffer*)
    uint64_t today_int = 0;        // int (CalendarDate*) -> yyyymmdd (optional: only for the success check)
    // Name of the first required function that is missing, nullptr when all are there
    const char* missing() const;
};

// The calls into the game. The Windows host calls the resolved functions (on the game thread only); tests fake them
// over synthetic memory. Each returns false when it could not run (the sequence stops with the host's reason).
class GameCaller {
public:
    virtual ~GameCaller() = default;
    virtual bool has_application(uint64_t jmm, int team, bool& out, std::string& err) = 0;
    virtual bool apply_for_job(uint64_t jmm, int team, std::string& err) = 0;
    virtual bool make_offer(uint64_t jmm, uint64_t offer, std::string& err) = 0;
    // Today's date as the game's calendar sees it (yyyymmdd); false when not available (the check is then weaker)
    virtual bool today(uint64_t jmm, int& yyyymmdd, std::string& err) = 0;
};

struct JobOfferRequest {
    uint64_t jmm = 0;  // JobMarketManager (from Lua's mem.manager(53) or the HandleEvent capture)
    int team = 0;      // club that makes the offer
};

struct JobOfferResult {
    bool ok = false;
    std::string message;  // for the user (toast / log / Lua)
    std::string stage;    // where it stopped: "validate", "apply", "find", "write", "offer", "check", "done"
    bool applied = false; // ApplyForJob was called (no application existed)
    uint64_t node = 0;    // the application node
    int wage = 0;         // weekly wage the game computed for the job
    int sent_day = -1;    // offer.mJobOfferSentDay after MakeOffer
};

// The pointer is a readable JobMarketManager with the expected vtable, and the hub slots the game's own code will
// dereference during the sequence are readable too. Nothing is written.
bool validate_jmm(Memory& mem, uint64_t jmm, uint64_t vtable, std::string& err);

// The application node for the team by the same bucket walk HasApplication does. 0 when absent; err is set when the
// table itself is unreadable or out of bounds (then 0 is returned too).
uint64_t find_application(Memory& mem, uint64_t jmm, int team, std::string& err);

// The whole sequence (see the file comment). Never throws.
JobOfferResult job_offer_create(Memory& mem, GameCaller& call, const JobMarketFns& fns, const JobOfferRequest& req);

// ---------------------------------------------------------------- mailbox call block (bridge.h: +0x2020 .. +0x2260)
constexpr uint64_t kMbCall = 0x2020;
constexpr uint64_t kMbCallOp = kMbCall + 0x00;         // i32 op (kCallOpJobOffer)
constexpr uint64_t kMbCallStatus = kMbCall + 0x04;     // i32 kCall* (written by the DLL)
constexpr uint64_t kMbCallSeq = kMbCall + 0x08;        // i32 request number (written by Lua)
constexpr uint64_t kMbCallResultSeq = kMbCall + 0x0C;  // i32 request the status / outputs belong to (DLL)
constexpr uint64_t kMbCallArgs = kMbCall + 0x10;       // i64[4] arguments (Lua)
constexpr uint64_t kMbCallOut = kMbCall + 0x30;        // i64[2] outputs (DLL)
constexpr uint64_t kMbCallText = kMbCall + 0x40;       // char[0x200] result text (DLL)
constexpr uint64_t kMbCallTextSize = 0x200;
constexpr uint64_t kMbCallEnd = kMbCallText + kMbCallTextSize;  // 0x2260

constexpr int32_t kCallIdle = 0, kCallOk = 1, kCallFailed = -1, kCallQueued = 2, kCallRunning = 3;
constexpr int32_t kCallOpJobOffer = 1;
// standings_refresh (core/standings_refresh.h): args = svm, managers, comm service, ifce (each 0 = let the DLL find
// it); outputs = competitions re-requested, map keys found
constexpr int32_t kCallOpStandingsRefresh = 2;
// transfer_list (core/transfer_list.h): args = action (kAction*), player id, comm service, the player's club (team id
// from his teamplayerlinks row; must be the user's team); outputs = contract status before, contract status after.
// (10, not the next free number: the parallel game-call tracks each took their own range)
constexpr int32_t kCallOpTransferList = 10;

struct GameCallBlock {
    int32_t op = 0, status = 0, seq = 0, result_seq = 0;
    int64_t args[4] = {0, 0, 0, 0};
    int64_t out[2] = {0, 0};
    std::string text;
};
bool read_call_block(Memory& mem, uint64_t mailbox, GameCallBlock& out);
// Writes outputs + text first, then result_seq, then status (a reader that sees the status sees the rest)
bool write_call_result(Memory& mem, uint64_t mailbox, int32_t seq, int32_t status, int64_t out0, int64_t out1,
                       const std::string& text);
// Lua's side of the protocol (tests): writes op / args / seq and resets the status to idle
bool write_call_request(Memory& mem, uint64_t mailbox, int32_t seq, int32_t op, const int64_t args[4]);

}  // namespace turbo
