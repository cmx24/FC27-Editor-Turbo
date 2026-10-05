// FC 27 LE Turbo GUI - "transfer_list" game call (see transfer_list.h and docs/re/transfer_lists.md)
#include "transfer_list.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace turbo {
namespace tl {

const char* Fns::missing() const {
    if (!add_transfer) return "uah_add_transfer_list";
    if (!add_loan) return "uah_add_loan_list";
    if (!try_remove) return "uah_try_remove_from_list";
    if (!helper_vtable) return "uah_vtable";
    if (!dao_vtable) return "dao_vtable";
    if (!tm_vtable) return "tm_vtable";
    if (!pcm_vtable) return "pcm_vtable";
    if (!um_vtable) return "um_vtable";
    return nullptr;
}

const char* Fns::missing_block(bool need_toggle) const {
    if (need_toggle && !toggle_block) return "uah_toggle_transfer_block";
    if (!cachedblock_vtable) return "cachedblock_vtable";
    if (!blockdao_vtable) return "blockdao_vtable";
    return nullptr;
}

const char* action_name(int action) {
    switch (action) {
        case kActionTransferList: return "transfer list";
        case kActionLoanList: return "loan list";
        case kActionUnlist: return "remove from lists";
        case kActionUnlistTransfer: return "remove from the transfer list";
        case kActionUnlistLoan: return "remove from the loan list";
        case kActionQuery: return "list status";
        case kActionBlockOffers: return "block offers";
        case kActionUnblockOffers: return "unblock offers";
        case kActionQueryBlock: return "block status";
        default: return "unknown action";
    }
}

const char* block_name(int32_t state) {
    switch (state) {
        case 0: return "not blocked";
        case 1: return "blocked";
        default: return "unknown block state";
    }
}

const char* status_name(int32_t status) {
    switch (status) {
        case kStatusNone: return "not listed";
        case kStatusTransferListed: return "transfer listed";
        case kStatusLoanListed: return "loan listed";
        case kStatusBothLists: return "transfer and loan listed";
        case -1: return "no contract record";
        default: return "another contract status";
    }
}

static std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

static bool in_image(uint64_t p, const Request& req) {
    if (!req.image_base || !req.image_size) return true;
    return p >= req.image_base && p < req.image_base + req.image_size;
}

// A readable object of `size` bytes whose first word is `vtable`
static std::string check_object(Memory& mem, uint64_t obj, uint64_t size, uint64_t vtable, const char* what) {
    if (!is_ptr(obj, 8)) return std::string(what) + " pointer " + hex(obj) + " is not a pointer";
    uint64_t vt = 0;
    if (!mem.rd(obj, vt)) return std::string(what) + " at " + hex(obj) + " is not readable";
    if (vt != vtable)
        return std::string("the object at ") + hex(obj) + " is not the " + what + " (vtable " + hex(vt) + ", expected " + hex(vtable) + ")";
    std::vector<uint8_t> tail;
    if (size > kTailCheck && !mem.read_block(obj + size - kTailCheck, kTailCheck, tail))
        return std::string(what) + " at " + hex(obj) + " is cut off (" + hex(size) + " bytes expected)";
    return "";
}

// A manager whose +0x08 must be the career's manager table (the game reaches the other managers through it)
static std::string same_managers(Memory& mem, uint64_t obj, uint64_t managers, const char* what) {
    const uint64_t m = mem.ptr(obj + kMgrManagers);
    if (m != managers) return std::string("the ") + what + "'s manager table (" + hex(m) + ") is not the career's (" + hex(managers) + ")";
    return "";
}

// The object in manager slot `type` of the table: instance count 1, type flag 1, holder -> object
static std::string manager_at(Memory& mem, uint64_t managers, int type, uint64_t& out, const char* what) {
    out = 0;
    const uint64_t slot = managers + kSlotSize * static_cast<uint64_t>(type);
    int32_t count = 0, flag = 0;
    if (!mem.rd(slot + kSlotCount, count)) return std::string("manager slot ") + std::to_string(type) + " (" + what + ") is not readable";
    if (count != 1) return std::string("manager slot ") + std::to_string(type) + " (" + what + ") holds " + std::to_string(count) + " objects (1 expected)";
    uint64_t mtype = mem.ptr(slot + kSlotType);
    if (!mtype || !mem.rd(mtype + kTypeFlag, flag) || flag != 1)
        return std::string("manager slot ") + std::to_string(type) + " (" + what + ") has no type descriptor";
    out = mem.chain(slot, {kSlotHolder, 0});
    if (!out) return std::string("manager slot ") + std::to_string(type) + " (" + what + ") holds no object: is a career loaded?";
    return "";
}

std::string locate(Memory& mem, const Request& req, const Fns& fns, Located& out) {
    out = Located();
    if (const char* m = fns.missing()) return std::string("game function ") + m + " is not resolved on this game build";
    // Block Offers: its own signatures (the list actions never need them); the toggle only for the actions that call it
    const bool block = is_block_action(req.action), toggles = changes_block(req.action);
    if (block)
        if (const char* m = fns.missing_block(toggles)) return std::string("game function ") + m + " is not resolved on this game build";
    std::vector<uint64_t> addresses = {fns.add_transfer, fns.add_loan,  fns.try_remove, fns.helper_vtable,
                                       fns.dao_vtable,   fns.tm_vtable, fns.pcm_vtable, fns.um_vtable};
    if (block) {
        addresses.push_back(fns.cachedblock_vtable);
        addresses.push_back(fns.blockdao_vtable);
    }
    if (toggles) addresses.push_back(fns.toggle_block);
    for (uint64_t p : addresses)
        if (!in_image(p, req)) return "resolved address " + hex(p) + " is outside FC27.exe";
    if (!is_ptr(req.comm, 8)) return "the career's comm service is not known (" + hex(req.comm) + "): is the Turbo GUI running in a career?";
    out.owner = mem.ptr(req.comm + kCommOwner);
    if (!out.owner) return "comm service " + hex(req.comm) + " has no career-mode owner (+0x20): is a career loaded?";
    out.managers = mem.ptr(out.owner + kOwnerManagers);
    if (!out.managers) return "the career-mode owner " + hex(out.owner) + " has no manager table (+0x10)";
    if (req.managers && req.managers != out.managers)
        return "manager table mismatch: Lua published " + hex(req.managers) + " but the comm service leads to " + hex(out.managers);
    out.dao = mem.ptr(out.owner + kOwnerDao);
    if (!out.dao) return "the career-mode owner " + hex(out.owner) + " has no CareerDaoFactoryImpl (+0x30): is a career loaded?";
    std::string err = check_object(mem, out.dao, kDaoSize, fns.dao_vtable, "CareerDaoFactoryImpl");
    if (!err.empty()) return err;
    out.helper = out.dao + kDaoHelper;
    uint64_t hvt = 0;
    if (!mem.rd(out.helper, hvt)) return "the user-actions helper at " + hex(out.helper) + " is not readable";
    if (hvt != fns.helper_vtable)
        return "the object at " + hex(out.helper) + " is not the UserActionsHandlingHelperImpl (vtable " + hex(hvt) + ", expected " +
               hex(fns.helper_vtable) + ")";
    // the vtable's slots are the functions resolved by signature: the vtable and the functions identify each other
    struct Slot { uint64_t index, fn; const char* name; };
    std::vector<Slot> slots = {
        {kHelperSlotTryRemove, fns.try_remove, "TryToRemoveFromList"}, {kHelperSlotAddTransfer, fns.add_transfer, "AddToTransferList"},
        {kHelperSlotAddLoan, fns.add_loan, "AddToLoanList"}};
    if (toggles) slots.push_back({kHelperSlotToggleBlock, fns.toggle_block, "ToggleTransferBlock"});
    for (const auto& s : slots) {
        uint64_t fn = 0;
        if (!mem.rd(hvt + s.index * 8, fn)) return "the helper vtable at " + hex(hvt) + " is not readable";
        if (fn != s.fn)
            return std::string("helper vtable slot ") + std::to_string(s.index) + " (" + s.name + ") is " + hex(fn) + ", not the function Turbo resolved (" +
                   hex(s.fn) + "): layout mismatch, nothing is called";
    }
    uint64_t hm = mem.ptr(out.helper + kHelperManagers);
    if (hm != out.managers)
        return "the helper's manager table (" + hex(hm) + ") is not the career's (" + hex(out.managers) + ")";
    // what the helper and the TransferManager functions dereference
    err = manager_at(mem, out.managers, kTypeTransferManager, out.tm, "TransferManager");
    if (!err.empty()) return err;
    err = check_object(mem, out.tm, kTmSize, fns.tm_vtable, "TransferManager");
    if (!err.empty()) return err;
    err = same_managers(mem, out.tm, out.managers, "TransferManager");
    if (!err.empty()) return err;
    for (uint64_t off : {kTmListsStore, kTmListener, kTmNotifier}) {
        uint64_t obj = mem.ptr(out.tm + off), vt = obj ? mem.ptr(obj) : 0;
        if (!vt || !in_image(vt, req)) return "TransferManager+" + hex(off) + " holds no object with a vtable in FC27.exe";
    }
    err = manager_at(mem, out.managers, kTypePlayerContractManager, out.pcm, "PlayerContractManager");
    if (!err.empty()) return err;
    err = check_object(mem, out.pcm, kPcmSize, fns.pcm_vtable, "PlayerContractManager");
    if (!err.empty()) return err;
    err = same_managers(mem, out.pcm, out.managers, "PlayerContractManager");
    if (!err.empty()) return err;
    // PostEvent 0x14060124C calls [[dispatcher]]'s vfunc +0x30: the dispatcher's first word is its event sink, whose
    // first word is a vtable in FC27.exe
    uint64_t disp = 0;
    err = manager_at(mem, out.managers, kTypeDispatcher, disp, "event dispatcher");
    if (!err.empty()) return err;
    const uint64_t sink = mem.ptr(disp), sink_vt = sink ? mem.ptr(sink) : 0;
    if (!sink_vt || !in_image(sink_vt, req)) return "the event dispatcher at " + hex(disp) + " has no event sink with a vtable in FC27.exe";
    // AddTo*List stamps the listing with today's date: [CalendarManager+0x34] (day, month, year)
    uint64_t cal = 0;
    err = manager_at(mem, out.managers, kTypeCalendarManager, cal, "CalendarManager");
    if (!err.empty()) return err;
    const uint64_t cal_vt = mem.ptr(cal);
    std::vector<uint8_t> date;
    if (!cal_vt || !in_image(cal_vt, req) || !mem.read_block(cal + kCalendarDate, 12, date))
        return "the CalendarManager at " + hex(cal) + " is not readable (no vtable in FC27.exe or no date)";
    err = manager_at(mem, out.managers, kTypeUserManager, out.um, "UserManager");
    if (!err.empty()) return err;
    err = user_team(mem, out.um, fns.um_vtable, out.user_team);
    if (!err.empty()) return err;
    return same_managers(mem, out.um, out.managers, "UserManager");
}

std::string user_team(Memory& mem, uint64_t um, uint64_t um_vtable, int& team) {
    team = 0;
    std::string err = check_object(mem, um, kUmSize, um_vtable, "UserManager");
    if (!err.empty()) return err;
    // the game: GetActiveUser 0x14154ADBC = index -1 ? null : [um+0x18] + index * 0x348; the users are bounded by the
    // count at +0x10 (GetUser 0x147ABD994, FindUserByTeam 0x147ABD9B4) and carry it in their new[] array header
    int32_t count = 0, idx = -1;
    uint64_t users = 0, header = 0;
    if (!mem.rd(um + kUmCount, count) || !mem.rd(um + kUmIndex, idx) || !mem.rd(um + kUmUsers, users))
        return "the UserManager at " + hex(um) + " is not readable";
    if (count <= 0) return "the UserManager has no users (count " + std::to_string(count) + "): is a career loaded?";
    if (count > kMaxUsers) return "the UserManager's user count " + std::to_string(count) + " is out of range (layout mismatch?)";
    if (idx < 0) return "the UserManager has no active user: is a career loaded?";
    if (idx >= count) return "the UserManager's user index " + std::to_string(idx) + " is out of range (" + std::to_string(count) + " users)";
    if (!is_ptr(users, 8)) return "the UserManager's users pointer " + hex(users) + " is not a pointer";
    if (!mem.rd(users - kUsersHeader, header)) return "the UserManager's users at " + hex(users) + " are not readable";
    if (header != static_cast<uint64_t>(count))
        return "the UserManager's users at " + hex(users) + " are not its array of " + std::to_string(count) + " users (array header " + hex(header) +
               ", layout mismatch?)";
    const uint64_t user = users + static_cast<uint64_t>(idx) * kUserSize;
    std::vector<uint8_t> whole;
    if (!mem.read_block(user, kUserSize, whole)) return "the active user at " + hex(user) + " is not readable";
    int32_t t = 0;
    if (!mem.rd(user + kUserTeam, t)) return "the user's team id is not readable";
    if (t <= 0) return "the user's team id (" + std::to_string(t) + ") is not valid";
    team = t;
    return "";
}

std::string contract_status(Memory& mem, uint64_t pcm, int player, bool& found, int32_t& status, uint64_t* node_out) {
    found = false;
    status = -1;
    if (node_out) *node_out = 0;
    if (player <= 0) return "player id must be positive";
    uint32_t count = 0;
    if (!mem.rd(pcm + kPcmBucketCount, count) || count == 0 || count > kMaxBuckets)
        return "contract table bucket count " + std::to_string(count) + " is out of range";
    uint64_t buckets = mem.ptr(pcm + kPcmBuckets);
    if (!buckets) return "contract table buckets are not readable";
    const uint64_t idx = static_cast<uint64_t>(static_cast<int64_t>(player)) % count;
    uint64_t node = 0, sentinel = 0;
    if (!mem.rd(buckets + idx * 8, node) || !mem.rd(buckets + static_cast<uint64_t>(count) * 8, sentinel))
        return "contract bucket " + std::to_string(idx) + " is not readable";
    for (int hop = 0; hop < kMaxChain; ++hop) {
        if (node == 0 || node == sentinel) return "";
        if (!is_ptr(node, 4)) return "contract chain holds a bad pointer " + hex(node);
        int32_t key = 0, st = 0;
        uint64_t next = 0;
        if (!mem.rd(node + kPcmNodeKey, key) || !mem.rd(node + kPcmNodeStatus, st) || !mem.rd(node + kPcmNodeNext, next))
            return "contract record " + hex(node) + " is not readable";
        if (key == player) {
            found = true;
            status = st;
            if (node_out) *node_out = node;
            return "";
        }
        node = next;
    }
    return "contract chain longer than " + std::to_string(kMaxChain) + " records (layout mismatch?)";
}

// One eastl vector of `elem`-byte elements whose begin / end pointers sit at obj + off_begin / obj + off_end: begin <= end, a whole
// number of elements, at most kMaxBlockElements, every byte readable (an empty vector may hold null pointers). "" = fine.
static std::string read_vector(Memory& mem, uint64_t obj, uint64_t off_begin, uint64_t off_end, uint64_t elem, const std::string& what,
                               std::vector<uint8_t>& data) {
    data.clear();
    uint64_t b = 0, e = 0;
    if (!mem.rd(obj + off_begin, b) || !mem.rd(obj + off_end, e)) return what + " is not readable";
    if (b > e) return what + " is corrupt: begin " + hex(b) + " is after end " + hex(e) + " (layout mismatch?)";
    const uint64_t bytes = e - b;
    if (bytes % elem != 0)
        return what + " is corrupt: " + std::to_string(bytes) + " bytes is not a whole number of " + std::to_string(elem) + "-byte elements (layout mismatch?)";
    if (bytes / elem > static_cast<uint64_t>(kMaxBlockElements))
        return what + " is corrupt: " + std::to_string(bytes / elem) + " elements (at most " + std::to_string(kMaxBlockElements) + " expected, layout mismatch?)";
    if (bytes == 0) return "";
    if (!is_ptr(b, 4)) return what + " is corrupt: its elements pointer " + hex(b) + " is not a pointer";
    if (!mem.read_block(b, static_cast<size_t>(bytes), data)) return what + " at " + hex(b) + " is not readable (" + std::to_string(bytes) + " bytes)";
    return "";
}

std::string block_state(Memory& mem, const Fns& fns, uint64_t tm, uint64_t managers, int player, BlockState& out) {
    out = BlockState();
    const uint64_t cache = mem.ptr(tm + kTmBlockCache);
    std::string err = check_object(mem, cache, kBlockCacheSize, fns.cachedblock_vtable, "CachedTransferblockDaoImpl (the block-offers cache, TransferManager+0x2D38)");
    if (!err.empty()) return err;
    std::vector<uint8_t> whole;
    if (!mem.read_block(cache, kBlockCacheSize, whole)) return "the block-offers cache at " + hex(cache) + " is not readable";
    const uint64_t dao = mem.ptr(cache + kBlockCacheInner);
    err = check_object(mem, dao, kBlockDaoSize, fns.blockdao_vtable, "TransferblockDaoImpl (the block-offers cache's inner dao)");
    if (!err.empty()) return err;
    // the inner dao reaches the TransferManager through its hub ([[dao+0x10]+0xFF8]): it must be this career's manager table
    if (managers) {
        const uint64_t hub = mem.ptr(dao + kBlockDaoHub);
        if (hub != managers)
            return "the block-offers cache's inner dao belongs to another manager table (" + hex(hub) + ", the career's is " + hex(managers) + ")";
    }
    std::vector<uint8_t> a, b, list;
    err = read_vector(mem, cache, kBlockCacheABegin, kBlockCacheAEnd, kBlockIdSize, "the block-offers cache's vector A (blocked players)", a);
    if (!err.empty()) return err;
    err = read_vector(mem, cache, kBlockCacheBBegin, kBlockCacheBEnd, kBlockIdSize, "the block-offers cache's vector B (released players)", b);
    if (!err.empty()) return err;
    err = read_vector(mem, tm, kTmBlockListBegin, kTmBlockListEnd, kBlockEntrySize, "the TransferManager's block list (+0x2F50)", list);
    if (!err.empty()) return err;
    out.cache = cache;
    out.dao = dao;
    out.cache_count = a.size() / kBlockIdSize;
    out.list_count = list.size() / kBlockEntrySize;
    for (size_t i = 0; i < out.cache_count; ++i) {
        int32_t id = 0;
        std::memcpy(&id, a.data() + i * kBlockIdSize, sizeof(id));
        if (id == player) out.in_cache = true;
    }
    for (size_t i = 0; i < out.list_count; ++i) {
        int32_t id = 0;
        std::memcpy(&id, list.data() + i * kBlockEntrySize, sizeof(id));
        if (id != player) continue;
        const uint8_t flag = list[i * kBlockEntrySize + kBlockEntryFlag];
        if (flag == kBlockFlagOffers) out.in_list = true;
        else if (flag == kBlockFlagReleased) out.released = true;
    }
    return "";
}

static Result fail(Result r, const char* stage, const std::string& msg) {
    r.ok = false;
    r.stage = stage;
    r.message = msg;
    return r;
}

static std::string yes_no(bool v) { return v ? "yes" : "no"; }

// The Block Offers actions (r already holds the located objects and the contract status before in r.before / r.found)
static Result run_block(Memory& mem, Caller& call, const Fns& fns, const Request& req, Result r) {
    const std::string who = "player " + std::to_string(req.player);
    r.status_before = r.before;
    BlockState st;
    std::string err = block_state(mem, fns, r.at.tm, r.at.managers, req.player, st);
    if (!err.empty()) return fail(r, "validate", err);
    r.at.block_cache = st.cache;
    r.at.block_dao = st.dao;
    r.before = r.after = st.in_cache ? 1 : 0;
    r.status_after = r.status_before;
    // the game's cache and the TransferManager's list mirror each other: a player they disagree about is not a state Turbo
    // reasons about (the toggle follows the cache, the save holds the list), so nothing is called
    const std::string split = who + ": the block-offers cache says " + block_name(r.before) + " but the TransferManager's block list says " +
                              (st.in_list ? "blocked" : "not blocked") + " (cache " + yes_no(st.in_cache) + ", list " + yes_no(st.in_list) + ")";
    if (req.action == kActionQueryBlock) {
        r.ok = true;
        r.stage = "done";
        r.message = who + ": offers are " + block_name(r.before);
        if (st.in_cache != st.in_list) r.message += " (note: the block list disagrees, it says " + std::string(block_name(st.in_list ? 1 : 0)) + ")";
        else if (!st.in_cache && st.released) r.message += " (he is on the released-player block list, which is not Block Offers)";
        return r;
    }
    if (st.in_cache != st.in_list) return fail(r, "validate", split + ": nothing is called");
    const bool wanted = req.action == kActionBlockOffers;
    if (st.in_cache == wanted) {
        r.ok = true;
        r.stage = "done";
        r.message = who + ": offers are " + (wanted ? "already blocked" : "not blocked") + " (nothing was called)";
        return r;
    }
    // the call: the game's own toggle on the game's own objects; it flips, and the state says it must
    if (!call.toggle_block(r.at.helper, req.player, err)) return fail(r, "call", "ToggleTransferBlock: " + err);
    r.called = true;
    BlockState after;
    err = block_state(mem, fns, r.at.tm, r.at.managers, req.player, after);
    if (!err.empty()) return fail(r, "check", "the call ran but the block state could not be read back: " + err);
    r.after = after.in_cache ? 1 : 0;
    bool found2 = false;
    int32_t st2 = -1;
    if (contract_status(mem, r.at.pcm, req.player, found2, st2).empty() && found2) r.status_after = st2;
    const bool good = wanted ? (after.in_cache && after.in_list) : (!after.in_cache && !after.in_list);
    if (!good) {
        if (after.in_cache == st.in_cache && after.in_list == st.in_list)
            return fail(r, "check", std::string("the game refused: ") + who + " stayed " + block_name(r.before) + " (is he one of your players?)");
        return fail(r, "check", who + ": the block state is inconsistent after the call (cache " + yes_no(after.in_cache) + ", list " + yes_no(after.in_list) +
                                    ", expected both " + yes_no(wanted) + ")");
    }
    r.ok = true;
    r.stage = "done";
    r.message = who + ": " + action_name(req.action) + " done, offers are " + (wanted ? "now blocked" : "no longer blocked");
    // the game unlists a player when it blocks his offers (TransferManager::RemoveFromLists): reported, not an error
    if (wanted && (is_transfer_listed(r.status_before) || is_loan_listed(r.status_before))) {
        if (r.status_after == kStatusNone)
            r.message += "; he was " + std::string(status_name(r.status_before)) + " and the game took him off the list, as it does when offers are blocked";
        else
            r.message += "; he was " + std::string(status_name(r.status_before)) + " and is now " + status_name(r.status_after) + " (the game normally unlists him)";
    }
    return r;
}

Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req) {
    Result r;
    if (!valid_action(req.action)) return fail(r, "validate", "unknown transfer-list action " + std::to_string(req.action));
    if (req.player <= 0) return fail(r, "validate", "player id must be a positive number");
    std::string err = locate(mem, req, fns, r.at);
    if (!err.empty()) return fail(r, "validate", err);
    // (the block-state query reads only the block record: no contract status needed)
    if (req.action != kActionQueryBlock) {
        err = contract_status(mem, r.at.pcm, req.player, r.found, r.before);
        if (!err.empty()) return fail(r, "status", err);
    }
    const std::string who = "player " + std::to_string(req.player);
    // the helper lists the player on the user's club whoever he plays for: only the user's own players
    if (req.action != kActionQuery && req.action != kActionQueryBlock) {
        if (req.club <= 0) return fail(r, "validate", who + ": his club is not known (the database has no club link for him)");
        if (req.club != r.at.user_team)
            return fail(r, "validate", who + " plays for team " + std::to_string(req.club) + ", not your club (team " + std::to_string(r.at.user_team) +
                                           "): the game " + (is_block_action(req.action) ? "blocks offers only for" : "lists only") + " your own players");
    }
    if (is_block_action(req.action)) return run_block(mem, call, fns, req, r);
    // List status of a player the PlayerContractManager has no record of (another club's player, or one Turbo moved
    // since the career was loaded): the listed state lives only in that record, so he is on none of the game's lists
    if (!r.found && req.action == kActionQuery) {
        r.ok = true;
        r.stage = "done";
        r.before = r.after = kStatusNone;
        r.message = who + " is not listed (the career keeps no contract record for him)";
        return r;
    }
    if (!r.found)
        return fail(r, "status", who + " has no contract record in the career (not a player of this career, or not under contract)");
    const bool tl = is_transfer_listed(r.before), ll = is_loan_listed(r.before);
    if (req.action == kActionQuery) {
        r.ok = true;
        r.stage = "done";
        r.after = r.before;
        r.message = who + " is " + status_name(r.before);
        return r;
    }
    // eligibility: the game's own rule in AddTo*List is status 0 / 7 / 8 (another status = on loan, pre-signed, retiring...)
    if (req.action == kActionTransferList || req.action == kActionLoanList) {
        if (r.before != kStatusNone && r.before != kStatusTransferListed && r.before != kStatusLoanListed)
            return fail(r, "status", who + " cannot be listed: his contract status is " + std::to_string(r.before) + " (" + status_name(r.before) +
                                         "); the game only lists players with status 0, 7 or 8");
        if ((req.action == kActionTransferList && tl) || (req.action == kActionLoanList && ll))
            return fail(r, "status", who + " is already " + status_name(r.before));
    } else {
        if (!tl && !ll) return fail(r, "status", who + " is not on the transfer list or the loan list");
        if (req.action == kActionUnlistTransfer && !tl) return fail(r, "status", who + " is not on the transfer list");
        if (req.action == kActionUnlistLoan && !ll) return fail(r, "status", who + " is not on the loan list");
        if ((req.action == kActionUnlistTransfer || req.action == kActionUnlistLoan) && tl && ll)
            return fail(r, "status", who + " is on both lists and the game's remove takes him off both: use remove from lists");
    }
    // the call: the game's own helper on the game's own objects
    int32_t expect = r.before;
    bool removed = false;
    switch (req.action) {
        case kActionTransferList:
            if (!call.add_transfer(r.at.helper, req.player, err)) return fail(r, "call", "AddToTransferList: " + err);
            expect = ll ? kStatusBothLists : kStatusTransferListed;
            break;
        case kActionLoanList:
            if (!call.add_loan(r.at.helper, req.player, err)) return fail(r, "call", "AddToLoanList: " + err);
            expect = tl ? kStatusBothLists : kStatusLoanListed;
            break;
        default:  // the removals: the game's remove takes him off both lists; the flag names a list he is on
            if (!call.try_remove(r.at.helper, req.player, !tl, removed, err)) return fail(r, "call", "TryToRemoveFromList: " + err);
            expect = kStatusNone;
            break;
    }
    r.called = true;
    bool found2 = false;
    err = contract_status(mem, r.at.pcm, req.player, found2, r.after);
    if (!err.empty()) return fail(r, "check", "the call ran but the status could not be read back: " + err);
    if (!found2) return fail(r, "check", "the call ran but the player's contract record is gone");
    if (r.after != expect) {
        if (r.after == r.before)
            return fail(r, "check", std::string("the game refused: ") + who + " stayed " + status_name(r.before) +
                                        (req.action <= kActionLoanList ? " (is he one of your players, with the list screen available?)" : ""));
        return fail(r, "check", who + " is now " + status_name(r.after) + " (expected " + status_name(expect) + ")");
    }
    r.ok = true;
    r.stage = "done";
    r.message = who + ": " + action_name(req.action) + " done, he is now " + status_name(r.after);
    if (req.action >= kActionUnlist && !removed) r.message += " (the game reported nothing removed)";
    return r;
}

}  // namespace tl
}  // namespace turbo
