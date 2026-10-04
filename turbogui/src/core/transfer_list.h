// FC 27 LE Turbo GUI - "transfer_list" game call: put one of YOUR players on the transfer list or the loan list, or
// take him off, exactly as the game's Transfer Hub does (platform-independent part; src/win/transfer_list_win.cpp
// resolves the functions and runs the call on the game thread). Replaces Live Editor's natives
// cAddPlayerToTransferList / cAddPlayerToLoanList / cRemovePlayerFromLists / cIsPlayer*Listed that FC 27 Live Editor
// v27.1.2 does not ship (docs/re/transfer_lists.md).
//
// What the game does when the user picks "Add to transfer list" on a player (FC27.exe 1.0.140.64835, every offset [H]):
//   UserActionsHandlingHelperImpl::AddToTransferList(helper, playerId)       0x147F68368 (vtable slot 32)
//     -> TransferManager::AddToTransferList(tm, playerId, kind 0, flag 0)     0x147C3FB60
//          the player's contract status (PlayerContractManager, type 77) must be 0 / 7 / 8; the user's team id comes
//          from the UserManager (type 129); the player is added to the user team's listed-players table and his
//          status becomes 7 (transfer listed) or 9 (both lists) with today's date; the shortlist / UI caches are told
//     -> PostEvent(dispatcher, 0x79 UserTransferlisted {playerId, listed = 1})  the Transfer Hub list, the news and
//        Live Editor's USER_TRANSFERLISTED event all come from this event
//   AddToLoanList is the same with 0x147C3F95C / status 8 (9 when also transfer listed) / event 0x7A UserLoanlisted.
//   TryToRemoveFromList(helper, playerId, bool loanList)                      0x147F8E300 (slot 31) -> bool
//     loanList only picks the list the player must be on (entry flag bit 1) before anything is done; then
//     -> TransferManager::RemoveFromLists(tm, playerId, &removedT, &removedL, mask 1) 0x147C3F588: bit 1 is cleared in
//        BOTH lists (an entry left with no bit is erased), his status becomes 0 (7 / 8 / 9 only if a list entry kept
//        another bit, which the helper's own adds never set), and UserTransferlisted / UserLoanlisted is posted with
//        listed = 0. So the game's remove always takes him off both lists: a single-list removal of a player on both
//        lists is refused by Turbo (it would also unlist him from the other one).
// The helper is a sub-object (+0x478) of the career's CareerDaoFactoryImpl, reached as [[comm+0x20]+0x30] where comm
// is the FeFceGMCommService plugin Live Editor exposes and [[comm+0x20]+0x10] is the manager table ("hub") Turbo
// already walks (core/standings_refresh.h). helper+0x8 holds that manager table: the three functions read
// [helper+8] -> slot 127 (+0xFF8) for the TransferManager and slot 39 (+0x4F8) for the event dispatcher.
// Turbo calls the game's own helper functions on the game's own objects: nothing is allocated or laid out by Turbo,
// the asking price does not exist in FC 27's own screens (the listing has none; AI clubs make offers that the user
// negotiates), the "generic" loan listing is what the game's own action does.
//
// The helper lists the player on the USER's club (the team id comes from the UserManager) whoever he plays for, so the
// player's club must be checked first: Lua passes the club of his teamplayerlinks row and the call is refused unless
// it is the user's team read from the UserManager (type 129: +0x14 user index, +0x18 users of 0x348 bytes, user
// +0x1F4 team id, what 0x14154ADBC / 0x142B1DDEC read).
//
// Safety: every pointer is checked before the call (readable, vtables of the dao / helper / TransferManager /
// PlayerContractManager from the game's own constructors; the helper vtable's slots 31..33 must be the functions
// Turbo resolved by signature, so the vtable and the functions identify each other; the helper's manager table must
// be the one Lua published), the player's contract status is read before (eligibility, the game's own rule) and
// after (the expected transition) through the PlayerContractManager's own hash table, and the call runs on the game
// thread only. Every read goes through turbo::Memory; the calls are behind Caller so the sequence is tested on
// synthetic memory with a fake game.
#pragma once
#include <cstdint>
#include <string>

#include "mem.h"

namespace turbo {
namespace tl {

// Manager table slots (0x20 bytes each: +0x08 type, +0x10 count, +0x18 holder -> object), by Live Editor's type ids
constexpr int kTypeTransferManager = 127;       // ENUM_FCEGameModesFCECareerModeTransferManager
constexpr int kTypePlayerContractManager = 77;  // ENUM_FCEGameModesFCECareerModePlayerContractManager
constexpr int kTypeDispatcher = 39;             // the career-event dispatcher the helper posts through (hub+0x4F8)
constexpr int kTypeUserManager = 129;           // ENUM_FCEGameModesFCECareerModeUserManager (hub+0x1038)
constexpr uint64_t kSlotSize = 0x20, kSlotType = 0x08, kSlotCount = 0x10, kSlotHolder = 0x18, kTypeFlag = 0x10;
constexpr uint64_t kCommOwner = 0x20;      // [comm+0x20] = the career-mode owner object
constexpr uint64_t kOwnerManagers = 0x10;  // [owner+0x10] = manager table
constexpr uint64_t kOwnerDao = 0x30;       // [owner+0x30] = CareerDaoFactoryImpl
// CareerDaoFactoryImpl (0x13E8 bytes, vtable from its ctor 0x147F0EB10 "dao_vtable")
constexpr uint64_t kDaoSize = 0x13E8;
constexpr uint64_t kDaoHelper = 0x478;     // UserActionsHandlingHelperImpl sub-object: +0 vtable ("uah_vtable"), +8 managers
constexpr uint64_t kHelperManagers = 0x08;
constexpr uint64_t kHelperSlotTryRemove = 31, kHelperSlotAddTransfer = 32, kHelperSlotAddLoan = 33;
// TransferManager (0x2FA0 bytes, vtable from its ctor 0x147C26450 "tm_vtable"); +0x2B80 the lists store, +0x2BE0 and
// +0x2D38 the listeners the add / remove functions call (must be readable objects with a vtable)
constexpr uint64_t kTmSize = 0x2FA0, kTmListsStore = 0x2B80, kTmListener = 0x2BE0, kTmNotifier = 0x2D38;
// PlayerContractManager (0x458 bytes, vtable from its ctor 0x147E54FB0 "pcm_vtable"): hash table of contract records
// keyed by player id at +0x3D8 (node** buckets, bucketCount + 1 entries, the last is the end sentinel) / +0x3E0 (u32)
constexpr uint64_t kPcmSize = 0x458, kPcmBuckets = 0x3D8, kPcmBucketCount = 0x3E0;
constexpr uint64_t kPcmNodeKey = 0x00, kPcmNodeStatus = 0x34, kPcmNodeNext = 0xB8, kPcmNodeSize = 0xC0;
constexpr uint32_t kMaxBuckets = 1u << 20;
constexpr int kMaxChain = 4096;
// UserManager (0xB20 bytes, vtable from its ctor 0x147AB2EB8 "um_vtable"): +0x14 index of the active user, +0x18 / +0x20
// begin / end of the users (0x348 bytes each); a user's team id is at +0x1F4 (0x142B1DDEC: +0x1F0 + slot 0 * 0x74, +4)
constexpr uint64_t kUmSize = 0xB20, kUmIndex = 0x14, kUmUsersBegin = 0x18, kUmUsersEnd = 0x20, kUserSize = 0x348, kUserTeam = 0x1F4;
constexpr int kMaxUsers = 16;
// Contract status (PlayerContractManager record +0x34) as the add / remove functions read and write it
constexpr int32_t kStatusNone = 0, kStatusTransferListed = 7, kStatusLoanListed = 8, kStatusBothLists = 9;
// Manager object size checks read these many bytes at the end of the object
constexpr uint64_t kTailCheck = 16;

// What the call does
constexpr int kActionTransferList = 1;    // add to the transfer list
constexpr int kActionLoanList = 2;        // add to the loan list
constexpr int kActionUnlist = 3;          // remove from both lists (the game's remove always clears both)
constexpr int kActionUnlistTransfer = 4;  // remove from the transfer list (refused when he is on both lists)
constexpr int kActionUnlistLoan = 5;      // remove from the loan list (refused when he is on both lists)
constexpr int kActionQuery = 6;           // read the status only (nothing is called)
const char* action_name(int action);
// "transfer listed" / "loan listed" / ... for a status value
const char* status_name(int32_t status);
inline bool is_transfer_listed(int32_t s) { return s == kStatusTransferListed || s == kStatusBothLists; }
inline bool is_loan_listed(int32_t s) { return s == kStatusLoanListed || s == kStatusBothLists; }

// Game functions / anchors resolved by signature on the running build (0 = unknown)
struct Fns {
    uint64_t add_transfer = 0;   // void (Helper*, int playerId)                 "uah_add_transfer_list"
    uint64_t add_loan = 0;       // void (Helper*, int playerId)                 "uah_add_loan_list"
    uint64_t try_remove = 0;     // bool (Helper*, int playerId, bool loanList)  "uah_try_remove_from_list"
    uint64_t helper_vtable = 0;  // UserActionsHandlingHelperImpl vtable         "uah_vtable"
    uint64_t dao_vtable = 0;     // CareerDaoFactoryImpl vtable                  "dao_vtable"
    uint64_t tm_vtable = 0;      // TransferManager vtable                       "tm_vtable"
    uint64_t pcm_vtable = 0;     // PlayerContractManager vtable                 "pcm_vtable"
    uint64_t um_vtable = 0;      // UserManager vtable                           "um_vtable"
    // Name of the first required entry that is missing, nullptr when all are there (every entry is required: each
    // one is a check the call must make)
    const char* missing() const;
};

// The calls into the game. The Windows host calls the resolved functions (game thread only); tests fake them.
class Caller {
public:
    virtual ~Caller() = default;
    virtual bool add_transfer(uint64_t helper, int player, std::string& err) = 0;
    virtual bool add_loan(uint64_t helper, int player, std::string& err) = 0;
    // removed = what TryToRemoveFromList returned
    virtual bool try_remove(uint64_t helper, int player, bool loan_list, bool& removed, std::string& err) = 0;
};

struct Request {
    int action = 0;         // kAction*
    int player = 0;         // player id (must be one of the user's players: see `club`; the game checks the status)
    int club = 0;           // the player's club (Lua: his teamplayerlinks row); must be the user's team for every action
                            // but kActionQuery (0 = unknown: refused)
    uint64_t comm = 0;      // the FeFceGMCommService plugin (bridge_state.json comm_service)
    uint64_t managers = 0;  // the manager table Lua published (bridge_state.json managers); 0 = take [[comm+0x20]+0x10]
    uint64_t image_base = 0, image_size = 0;  // FC27.exe range: vtables and function pointers must fall inside (0 = skip)
};

// The objects the call runs on, after validation
struct Located {
    uint64_t owner = 0, managers = 0, dao = 0, helper = 0, tm = 0, pcm = 0, um = 0;
    int user_team = 0;  // the user's team id read from the UserManager
};

struct Result {
    bool ok = false;
    std::string stage;    // "validate", "status", "call", "check", "done" (host: "off", "queued")
    std::string message;  // for the user (toast / log / Lua)
    Located at;
    int32_t before = -1;  // contract status before the call (-1 = no record)
    int32_t after = -1;   // contract status after the call
    bool found = false;   // the player has a contract record
    bool called = false;  // a game function ran
};

// Find and validate everything the call dereferences (see the file comment). "" = fine, else the reason.
std::string locate(Memory& mem, const Request& req, const Fns& fns, Located& out);
// The user's team id from the UserManager object (validated: vtable, user index within the users). "" = fine.
std::string user_team(Memory& mem, uint64_t um, uint64_t um_vtable, int& team);
// The player's contract status from the PlayerContractManager's hash table (the walk GetPlayerInfo 0x147E61F20 does:
// bucket = (int64)player % bucketCount, chain through +0xB8; node key +0 must be the player). found = false with ""
// when there is no record; "" and the status otherwise; a reason when the table is unreadable or inconsistent.
std::string contract_status(Memory& mem, uint64_t pcm, int player, bool& found, int32_t& status, uint64_t* node = nullptr);
// The whole call (see the file comment). Never throws.
Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req);

}  // namespace tl
}  // namespace turbo
