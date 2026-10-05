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
// it is the user's team read from the UserManager exactly as the game reads it (type 129: +0x10 user count, +0x14
// active user index, +0x18 the users, a new[] array of 0x348-byte users whose array header holds the count; the active
// user's +0x1F4 is his club's team id: GetActiveUser 0x14154ADBC + GetUserClub 0x142B1DDEC(user, 0) +4, proven live).
//
// Block Offers (kActionBlockOffers / kActionUnblockOffers / kActionQueryBlock; docs/re/player_status_roles.md section 2,
// docs/re/transfer_lists.md section 9): the Squad hub's "Block Offers" is the helper's vtable slot 30,
//   UserActionsHandlingHelperImpl::ToggleTransferBlock(helper, int playerId)   0x147F688B8   (slot 30, right before the lists)
//     tm = [[helper+8]+0xFF8]; cache = [tm+0x2D38] (CachedTransferblockDaoImpl, vtable 0x14B005400);
//     blocked = cache->vf[1](pid) (the pid is in the cache's vector A); blocked ? cache->vf[7](pid) : cache->vf[4](pid) -> a TOGGLE;
//     blocking also unlists him (TransferManager::RemoveFromLists), erases his pending offers and posts event 0xBE.
// The record: the cache mirrors the TransferManager's block list, a vector at tm+0x2F50 / +0x2F58 of 8-byte {int32 playerId,
// u8 flag, pad[3]} entries (flag 0 = Block Offers, flag 1 = released player); the cache holds vector A (flag 0) at +0x10 / +0x18
// and vector B (flag 1) at +0x30 / +0x38, each of int32 ids, and the inner TransferblockDaoImpl (vtable 0x14B005630) at +8.
// Turbo never calls the toggle blind: block calls it only when the pid is absent from vector A, unblock only when present (when
// the state already is the wanted one it succeeds without calling and says so); the cache, its inner dao, both vectors and the
// block list are validated first, the cache and the list must agree about the player, and after the call the state is read
// back (block: in A AND {pid, 0} in the list; unblock: in neither). The player must be one of the user's players (same club
// rule as the list actions); a player on the transfer / loan list ends unlisted, which the game does itself (reported in the
// message and in Result::status_before / status_after). For these three actions Result::before / after carry the BLOCK STATE
// (0 = offers not blocked, 1 = blocked), not the contract status.
//
// Safety: every pointer is checked before the call (readable, vtables of the dao / helper / TransferManager /
// PlayerContractManager from the game's own constructors; the helper vtable's slots 31..33 must be the functions
// Turbo resolved by signature, so the vtable and the functions identify each other; the helper's manager table must
// be the one Lua published and the TransferManager / PlayerContractManager / UserManager must point back at it; the
// TransferManager's lists store and listeners, the event dispatcher's sink and the CalendarManager carry vtables in
// FC27.exe), the player's contract status is read before (eligibility, the game's own rule) and
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
constexpr int kTypeCalendarManager = 24;        // the CalendarManager (hub+0x318): AddTo*List reads today's date from it
constexpr uint64_t kCalendarDate = 0x34;        // CalendarManager +0x34: today (day, month, year: 3 x int32)
constexpr uint64_t kSlotSize = 0x20, kSlotType = 0x08, kSlotCount = 0x10, kSlotHolder = 0x18, kTypeFlag = 0x10;
constexpr uint64_t kCommOwner = 0x20;      // [comm+0x20] = the career-mode owner object
constexpr uint64_t kOwnerManagers = 0x10;  // [owner+0x10] = manager table
constexpr uint64_t kOwnerDao = 0x30;       // [owner+0x30] = CareerDaoFactoryImpl
// TransferManager / PlayerContractManager / UserManager +0x08 = the manager table: the add / remove functions reach the
// PlayerContractManager, the UserManager and the CalendarManager through [TransferManager+8]
constexpr uint64_t kMgrManagers = 0x08;
// CareerDaoFactoryImpl (0x13E8 bytes, vtable from its ctor 0x147F0EB10 "dao_vtable")
constexpr uint64_t kDaoSize = 0x13E8;
constexpr uint64_t kDaoHelper = 0x478;     // UserActionsHandlingHelperImpl sub-object: +0 vtable ("uah_vtable"), +8 managers
constexpr uint64_t kHelperManagers = 0x08;
constexpr uint64_t kHelperSlotTryRemove = 31, kHelperSlotAddTransfer = 32, kHelperSlotAddLoan = 33;
constexpr uint64_t kHelperSlotToggleBlock = 30;  // ToggleTransferBlock(helper, int pid): Block Offers (a toggle)
// TransferManager (0x2FA0 bytes, vtable from its ctor 0x147C26450 "tm_vtable"); +0x2B80 the lists store, +0x2BE0 and
// +0x2D38 the listeners the add / remove functions call (must be readable objects with a vtable)
constexpr uint64_t kTmSize = 0x2FA0, kTmListsStore = 0x2B80, kTmListener = 0x2BE0, kTmNotifier = 0x2D38;
// Block Offers record (docs/re/player_status_roles.md section 2.2, all [H]): TransferManager +0x2D38 (= kTmNotifier) is the
// CachedTransferblockDaoImpl (0x50 bytes, vtable "cachedblock_vtable"); +8 the inner TransferblockDaoImpl (0x18 bytes, vtable
// "blockdao_vtable"); +0x10 / +0x18 vector A (flag 0 = Block Offers) and +0x30 / +0x38 vector B (flag 1 = released) of int32 ids.
// The authoritative list is the TransferManager's own vector at +0x2F50 (begin) / +0x2F58 (end) of 8-byte entries {int32 id, u8 flag}.
constexpr uint64_t kTmBlockCache = kTmNotifier, kTmBlockListBegin = 0x2F50, kTmBlockListEnd = 0x2F58;
constexpr uint64_t kBlockCacheSize = 0x50, kBlockCacheInner = 0x08, kBlockCacheABegin = 0x10, kBlockCacheAEnd = 0x18,
                   kBlockCacheBBegin = 0x30, kBlockCacheBEnd = 0x38, kBlockDaoSize = 0x18;
constexpr uint64_t kBlockDaoHub = 0x10;  // the inner dao +0x10 = the career's manager table (SetBlock 0x147D73D48 reads [[dao+0x10]+0xFF8] for the TransferManager)
constexpr uint64_t kBlockIdSize = 4, kBlockEntrySize = 8, kBlockEntryFlag = 4;
constexpr int kMaxBlockElements = 2000;  // a vector holding more is not the layout Turbo knows
constexpr uint8_t kBlockFlagOffers = 0, kBlockFlagReleased = 1;
// PlayerContractManager (0x458 bytes, vtable from its ctor 0x147E54FB0 "pcm_vtable"): hash table of contract records
// keyed by player id at +0x3D8 (node** buckets, bucketCount + 1 entries, the last is the end sentinel) / +0x3E0 (u32)
constexpr uint64_t kPcmSize = 0x458, kPcmBuckets = 0x3D8, kPcmBucketCount = 0x3E0;
constexpr uint64_t kPcmNodeKey = 0x00, kPcmNodeStatus = 0x34, kPcmNodeNext = 0xB8, kPcmNodeSize = 0xC0;
constexpr uint32_t kMaxBuckets = 1u << 20;
constexpr int kMaxChain = 4096;
// UserManager (0xB20 bytes, vtable from its ctor 0x147AB2EB8 "um_vtable"; docs/re/transfer_lists.md section 1b):
//   +0x10 int32 user count: the ctor sets -1; SetUserCount 0x147AC3530 writes it, then allocates the users
//         ("UserManager::mUser", count * 0x348 + 0x10 bytes) and writes the count again in the array header
//   +0x14 int32 index of the active user (-1 = none); every setter (0x147AB6628, 0x147AC34B4) keeps it below the count
//   +0x18 User* the users: header at -0x10 (u64 = the count), then `count` users of 0x348 bytes
//   (+0x20 is another pointer, NOT the end of the users: reading it as one refused every call in a live career)
// A user's clubs: two 0x74-byte records at +0x1F0 (0x142B1DDEC(user, slot)): +4 the team id (slot 0 = his club at user
// +0x1F4, slot 1 = his national team at +0x268, -1 = none), +8 the league id, +0xC the team name.
constexpr uint64_t kUmSize = 0xB20, kUmCount = 0x10, kUmIndex = 0x14, kUmUsers = 0x18, kUsersHeader = 0x10, kUserSize = 0x348,
                   kUserTeam = 0x1F4;
constexpr int kMaxUsers = 64;
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
// Block Offers (before / after = the block state 0 / 1, see the file comment)
constexpr int kActionBlockOffers = 7;     // block incoming offers for one of the user's players (the game's toggle, called only when he is not blocked)
constexpr int kActionUnblockOffers = 8;   // unblock them (called only when he is blocked)
constexpr int kActionQueryBlock = 9;      // read the block state only (nothing is called; any player)
inline bool valid_action(int action) { return action >= kActionTransferList && action <= kActionQueryBlock; }
inline bool is_block_action(int action) { return action >= kActionBlockOffers && action <= kActionQueryBlock; }
inline bool changes_block(int action) { return action == kActionBlockOffers || action == kActionUnblockOffers; }
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
    // Block Offers (only the block actions need them; the list actions run without them)
    uint64_t toggle_block = 0;        // void (Helper*, int playerId)             "uah_toggle_transfer_block" (vtable slot 30)
    uint64_t cachedblock_vtable = 0;  // CachedTransferblockDaoImpl vtable        "cachedblock_vtable"
    uint64_t blockdao_vtable = 0;     // TransferblockDaoImpl vtable              "blockdao_vtable"
    // Name of the first required entry that is missing, nullptr when all are there (every entry is required: each
    // one is a check the call must make)
    const char* missing() const;
    // The same for the Block Offers entries (the toggle only matters for the actions that call it)
    const char* missing_block(bool need_toggle) const;
};

// The calls into the game. The Windows host calls the resolved functions (game thread only); tests fake them.
class Caller {
public:
    virtual ~Caller() = default;
    virtual bool add_transfer(uint64_t helper, int player, std::string& err) = 0;
    virtual bool add_loan(uint64_t helper, int player, std::string& err) = 0;
    // removed = what TryToRemoveFromList returned
    virtual bool try_remove(uint64_t helper, int player, bool loan_list, bool& removed, std::string& err) = 0;
    // ToggleTransferBlock(helper, player): flips the player's Block Offers state (Turbo calls it only when it must flip)
    virtual bool toggle_block(uint64_t helper, int player, std::string& err) = 0;
};

struct Request {
    int action = 0;         // kAction*
    int player = 0;         // player id (must be one of the user's players: see `club`; the game checks the status)
    int club = 0;           // the player's club (Lua: his teamplayerlinks row); must be the user's team for every action
                            // but kActionQuery / kActionQueryBlock (0 = unknown: refused)
    uint64_t comm = 0;      // the FeFceGMCommService plugin (bridge_state.json comm_service)
    uint64_t managers = 0;  // the manager table Lua published (bridge_state.json managers); 0 = take [[comm+0x20]+0x10]
    uint64_t image_base = 0, image_size = 0;  // FC27.exe range: vtables and function pointers must fall inside (0 = skip)
};

// The objects the call runs on, after validation
struct Located {
    uint64_t owner = 0, managers = 0, dao = 0, helper = 0, tm = 0, pcm = 0, um = 0;
    uint64_t block_cache = 0, block_dao = 0;  // the Block Offers cache and its inner dao (block actions, after validation)
    int user_team = 0;  // the user's team id read from the UserManager
};

struct Result {
    bool ok = false;
    std::string stage;    // "validate", "status", "call", "check", "done" (host: "off", "queued")
    std::string message;  // for the user (toast / log / Lua)
    Located at;
    // The list actions: the contract status before / after the call (-1 = no record). The Block Offers actions
    // (kActionBlockOffers / kActionUnblockOffers / kActionQueryBlock): the BLOCK STATE before / after, 0 = offers not blocked,
    // 1 = blocked (-1 = not read); the contract status is then in status_before / status_after.
    int32_t before = -1;
    int32_t after = -1;
    int32_t status_before = -1, status_after = -1;  // Block Offers actions only: contract status around the call (-1 = no record / not read)
    bool found = false;   // the player has a contract record
    bool called = false;  // a game function ran
};

// The Block Offers state of one player read from the cache and the TransferManager's block list (block_state)
struct BlockState {
    uint64_t cache = 0, dao = 0;  // the validated CachedTransferblockDaoImpl and its inner TransferblockDaoImpl
    bool in_cache = false;        // the pid is in the cache's vector A = what the game's IsBlocked / the squad row's HasOffersBlocked say
    bool in_list = false;         // the block list holds {pid, flag 0}
    bool released = false;        // the block list holds {pid, flag 1} (a released player, not "blocked by you")
    size_t cache_count = 0, list_count = 0;  // elements of vector A / of the block list
};

// Find and validate everything the call dereferences (see the file comment). "" = fine, else the reason.
std::string locate(Memory& mem, const Request& req, const Fns& fns, Located& out);
// The user's team id from the UserManager object, read the way the game does (GetActiveUser + slot 0 of his clubs),
// validated: vtable, user count 1..kMaxUsers, active index below it, the users' array header equal to the count, the
// active user readable, team id > 0. "" = fine.
std::string user_team(Memory& mem, uint64_t um, uint64_t um_vtable, int& team);
// The player's contract status from the PlayerContractManager's hash table (the walk GetPlayerInfo 0x147E61F20 does:
// bucket = (int64)player % bucketCount, chain through +0xB8; node key +0 must be the player). found = false with ""
// when there is no record; "" and the status otherwise; a reason when the table is unreadable or inconsistent.
std::string contract_status(Memory& mem, uint64_t pcm, int player, bool& found, int32_t& status, uint64_t* node = nullptr);
// Validates the Block Offers record before anything is called: [tm+0x2D38] readable with the cachedblock vtable, its inner dao
// (+8) with the dao vtable, vector A, vector B and the TransferManager's block list with begin <= end, whole elements (4 / 4 /
// 8 bytes), at most kMaxBlockElements, every byte readable, and the inner dao's hub is `managers` (0 = skip that check); then
// reports the player's state. "" = fine, else the reason.
std::string block_state(Memory& mem, const Fns& fns, uint64_t tm, uint64_t managers, int player, BlockState& out);
// "blocked" / "not blocked" for a block state value (0 / 1; -1 = unknown)
const char* block_name(int32_t state);
// The whole call (see the file comment). Never throws.
Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req);

}  // namespace tl
}  // namespace turbo
