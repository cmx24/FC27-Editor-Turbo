// FC 27 LE Turbo GUI - "transfer_list" game call (see transfer_list.h and docs/re/transfer_lists.md)
#include "transfer_list.h"

#include <cstdio>
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

const char* action_name(int action) {
    switch (action) {
        case kActionTransferList: return "transfer list";
        case kActionLoanList: return "loan list";
        case kActionUnlist: return "remove from lists";
        case kActionUnlistTransfer: return "remove from the transfer list";
        case kActionUnlistLoan: return "remove from the loan list";
        case kActionQuery: return "list status";
        default: return "unknown action";
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
    for (uint64_t p : {fns.add_transfer, fns.add_loan, fns.try_remove, fns.helper_vtable, fns.dao_vtable, fns.tm_vtable, fns.pcm_vtable,
                       fns.um_vtable})
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
    struct Slot { uint64_t index, fn; const char* name; } slots[] = {
        {kHelperSlotTryRemove, fns.try_remove, "TryToRemoveFromList"}, {kHelperSlotAddTransfer, fns.add_transfer, "AddToTransferList"},
        {kHelperSlotAddLoan, fns.add_loan, "AddToLoanList"}};
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
    for (uint64_t off : {kTmListsStore, kTmListener, kTmNotifier}) {
        uint64_t obj = mem.ptr(out.tm + off);
        if (!obj || !mem.ptr(obj)) return "TransferManager+" + hex(off) + " holds no object with a vtable";
    }
    err = manager_at(mem, out.managers, kTypePlayerContractManager, out.pcm, "PlayerContractManager");
    if (!err.empty()) return err;
    err = check_object(mem, out.pcm, kPcmSize, fns.pcm_vtable, "PlayerContractManager");
    if (!err.empty()) return err;
    uint64_t disp = 0;
    err = manager_at(mem, out.managers, kTypeDispatcher, disp, "event dispatcher");
    if (!err.empty()) return err;
    if (!mem.ptr(disp)) return "the event dispatcher at " + hex(disp) + " has no vtable";
    err = manager_at(mem, out.managers, kTypeUserManager, out.um, "UserManager");
    if (!err.empty()) return err;
    return user_team(mem, out.um, fns.um_vtable, out.user_team);
}

std::string user_team(Memory& mem, uint64_t um, uint64_t um_vtable, int& team) {
    team = 0;
    std::string err = check_object(mem, um, kUmSize, um_vtable, "UserManager");
    if (!err.empty()) return err;
    int32_t idx = -1;
    uint64_t begin = 0, end = 0;
    if (!mem.rd(um + kUmIndex, idx) || !mem.rd(um + kUmUsersBegin, begin) || !mem.rd(um + kUmUsersEnd, end))
        return "the UserManager at " + hex(um) + " is not readable";
    if (idx < 0) return "the UserManager has no active user: is a career loaded?";
    if (!is_ptr(begin, 8) || end < begin || (end - begin) % kUserSize != 0 || (end - begin) / kUserSize > static_cast<uint64_t>(kMaxUsers))
        return "the UserManager's users (" + hex(begin) + ".." + hex(end) + ") are not a list of 0x348-byte users (layout mismatch?)";
    if (static_cast<uint64_t>(idx) >= (end - begin) / kUserSize)
        return "the UserManager's user index " + std::to_string(idx) + " is out of range";
    int32_t t = 0;
    if (!mem.rd(begin + static_cast<uint64_t>(idx) * kUserSize + kUserTeam, t)) return "the user's team id is not readable";
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

static Result fail(Result r, const char* stage, const std::string& msg) {
    r.ok = false;
    r.stage = stage;
    r.message = msg;
    return r;
}

Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req) {
    Result r;
    if (req.action < kActionTransferList || req.action > kActionQuery)
        return fail(r, "validate", "unknown transfer-list action " + std::to_string(req.action));
    if (req.player <= 0) return fail(r, "validate", "player id must be a positive number");
    std::string err = locate(mem, req, fns, r.at);
    if (!err.empty()) return fail(r, "validate", err);
    err = contract_status(mem, r.at.pcm, req.player, r.found, r.before);
    if (!err.empty()) return fail(r, "status", err);
    const std::string who = "player " + std::to_string(req.player);
    // the helper lists the player on the user's club whoever he plays for: only the user's own players
    if (req.action != kActionQuery) {
        if (req.club <= 0) return fail(r, "validate", who + ": his club is not known (the database has no club link for him)");
        if (req.club != r.at.user_team)
            return fail(r, "validate", who + " plays for team " + std::to_string(req.club) + ", not your club (team " + std::to_string(r.at.user_team) +
                                           "): the game lists only your own players");
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
