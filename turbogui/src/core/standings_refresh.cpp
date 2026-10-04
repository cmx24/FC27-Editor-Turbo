// FC 27 LE Turbo GUI - "standings_refresh" game call (see standings_refresh.h and docs/re/standings-ui-path.md)
#include "standings_refresh.h"

#include <cstdio>

namespace turbo {
namespace svm {

const char* Fns::missing() const {
    if (!refresh_comp) return "svm_refresh_comp";
    if (!listener) return "svm_listener";
    return nullptr;
}

static std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

uint64_t manager_table(Memory& mem, uint64_t comm) {
    if (!is_ptr(comm, 8)) return 0;
    return mem.chain(comm, {kCommManagersA, kCommManagersB});
}

uint64_t manager_at(Memory& mem, uint64_t managers, int type_id, std::string& err) {
    err.clear();
    if (!is_ptr(managers, 8) || type_id < 0 || type_id > 1024) {
        err = "manager table " + hex(managers) + " is not a pointer";
        return 0;
    }
    const uint64_t slot = managers + kSlotSize * static_cast<uint64_t>(type_id);
    int32_t count = 0;
    if (!mem.rd(slot + kSlotCount, count)) {
        err = "manager slot " + std::to_string(type_id) + " is not readable (table " + hex(managers) + ")";
        return 0;
    }
    if (count != 1) {
        err = "manager slot " + std::to_string(type_id) + " holds " + std::to_string(count) + " instances (no career loaded?)";
        return 0;
    }
    uint64_t type = mem.ptr(slot + kSlotType);
    int32_t flag = 0;
    if (!type || !mem.rd(type + kTypeFlag, flag) || flag != 1) {
        err = "manager slot " + std::to_string(type_id) + " has no type descriptor";
        return 0;
    }
    uint64_t obj = mem.chain(slot, {kSlotHolder, 0});
    if (!obj || !is_ptr(obj, 8)) {
        err = "manager slot " + std::to_string(type_id) + " holder does not point at an object";
        return 0;
    }
    return obj;
}

// Slot 108 only. The game's manager factory stores the StandingsViewManager it constructs in slot 108 (holder array
// [table+0xd98], count [table+0xd90]); searching other slots for "an object with the SVM vtable" is what found the
// StaffManager on 04-10-2026 when the vtable constant was wrong, so a mismatch here is reported, never worked around.
std::string locate(Memory& mem, uint64_t managers, uint64_t& out, uint64_t vtable) {
    out = 0;
    std::string err;
    uint64_t obj = manager_at(mem, managers, kTypeId, err);
    if (!obj) return "StandingsViewManager: " + err;
    uint64_t vt = 0;
    if (vtable) {
        if (!mem.rd(obj, vt)) return "the object in manager slot " + std::to_string(kTypeId) + " (" + hex(obj) + ") is not readable";
        if (vt != vtable)
            return "the object in manager slot " + std::to_string(kTypeId) + " (" + hex(obj) + ") is not the StandingsViewManager (vtable " +
                   hex(vt) + ", expected " + hex(vtable) + "): the layout of this game build differs";
    }
    out = obj;
    return "";
}

// An initialised, free CRITICAL_SECTION: LockCount -1, no recursion, no owner. The game thread owns the map's section
// only inside its own calls, so at the time of Turbo's call (after the frame body) it must be free; anything else is
// either another thread inside the SVM or memory that is no critical section (the StaffManager's +0x280 read
// LockCount 0x38547280 in the crash: EnterCriticalSection then waited on a garbage "semaphore").
static std::string check_critical_section(Memory& mem, uint64_t cs) {
    int32_t lock = 0, rec = 0;
    uint64_t owner = 0;
    if (!mem.rd(cs + kCsLockCount, lock) || !mem.rd(cs + kCsRecursion, rec) || !mem.rd(cs + kCsOwner, owner))
        return "the standings map's critical section at " + hex(cs) + " is not readable";
    if (lock != -1 || rec != 0 || owner != 0)
        return "the standings map's critical section at " + hex(cs) + " is not an initialised, free critical section (LockCount " +
               std::to_string(lock) + ", RecursionCount " + std::to_string(rec) + ", owner " + hex(owner) + ")";
    return "";
}

std::string validate(Memory& mem, uint64_t svm, uint64_t managers, uint64_t vtable, uint64_t listener) {
    if (!is_ptr(svm, 8)) return "StandingsViewManager address " + hex(svm) + " is not a pointer";
    uint64_t vt = 0;
    if (!mem.rd(svm, vt)) return "StandingsViewManager at " + hex(svm) + " is not readable";
    if (vtable && vt != vtable)
        return "the object at " + hex(svm) + " is not the StandingsViewManager (vtable " + hex(vt) + ", expected " + hex(vtable) + ")";
    if (listener) {
        uint64_t fn = 0;
        if (!is_ptr(vt, 8) || !mem.rd(vt + kVtableSlotListener, fn) || fn != listener)
            return "the vtable of the object at " + hex(svm) + " (" + hex(vt) + ") does not hold the SVM's career-event listener in slot 1 (" +
                   hex(fn) + ", expected " + hex(listener) + ")";
    }
    uint8_t tail = 0;
    if (!mem.rd(svm + kObjectSize - 1, tail)) return "StandingsViewManager at " + hex(svm) + " is cut off (0x4B8 bytes expected)";
    uint64_t ctx = mem.ptr(svm + kOffCtx);
    if (!ctx) return "StandingsViewManager has no manager table pointer (+0x8)";
    if (managers && ctx != managers)
        return "StandingsViewManager " + hex(svm) + " belongs to manager table " + hex(ctx) + ", not " + hex(managers) + " (another career?)";
    std::string err;
    if (manager_at(mem, ctx, kTypeId, err) != svm)
        return "manager slot " + std::to_string(kTypeId) + " of the table " + hex(ctx) + " does not hold " + hex(svm) + (err.empty() ? "" : " (" + err + ")");
    return check_critical_section(mem, svm + kOffCritSec);
}

static bool in_image(uint64_t p, uint64_t base, uint64_t size) {
    if (!is_ptr(p)) return false;
    if (!base || !size) return true;
    return p >= base && p < base + size;
}

std::string validate_request_path(Memory& mem, uint64_t svm, uint64_t ifce_expected, uint64_t iface_post, uint64_t allocator,
                                  uint64_t image_base, uint64_t image_size) {
    // refresh_comp: rax = [svm+8]; rcx = [rax+0x38]; rdi = [rcx]; ... call [[rdi]+0x20](rdi, request)
    const uint64_t ctx = mem.ptr(svm + kOffCtx);
    if (!ctx) return "StandingsViewManager has no manager table pointer (+0x8)";
    const uint64_t holder = mem.ptr(ctx + kCtxIfceHolder);
    if (!holder) return "the manager table " + hex(ctx) + " has no FCE interface holder at +0x38 (slot 1)";
    const uint64_t ifce = mem.ptr(holder);
    if (!ifce) return "the FCE interface holder " + hex(holder) + " does not point at an object";
    if (ifce_expected && ifce != ifce_expected)
        return "the FCE interface the request would go through (" + hex(ifce) + ") is not the one Turbo edited (" + hex(ifce_expected) +
               "): the rows edited are not the ones the game shows";
    const uint64_t ivt = mem.ptr(ifce);
    if (!ivt) return "the FCE interface " + hex(ifce) + " has no vtable";
    uint64_t post = 0;
    if (!mem.rd(ivt + kIfceVtablePost, post) || !in_image(post, image_base, image_size))
        return "the FCE interface vtable " + hex(ivt) + " has no function in slot 4 (" + hex(post) + ")";
    if (iface_post && post != iface_post)
        return "the FCE interface vtable " + hex(ivt) + " slot 4 is " + hex(post) + ", not the game's Post function " + hex(iface_post);
    // the request allocation: rcx = [allocator global]; rax = [rcx]; call [rax+0x10]
    if (allocator) {
        const uint64_t alloc = mem.ptr(allocator);
        if (!alloc) return "the game allocator global " + hex(allocator) + " holds no object";
        const uint64_t avt = mem.ptr(alloc);
        uint64_t fn = 0;
        if (!avt || !mem.rd(avt + kAllocVtableAlloc, fn) || !in_image(fn, image_base, image_size))
            return "the game allocator " + hex(alloc) + " has no allocate function in its vtable (" + hex(fn) + ")";
    }
    return "";
}

std::string map_keys(Memory& mem, uint64_t svm, std::vector<int32_t>& keys, uint64_t live_vtable) {
    keys.clear();
    if (!is_ptr(svm, 8)) return "no StandingsViewManager";
    const uint64_t anchor = svm + kOffMap;
    uint64_t root = 0;
    if (!mem.rd(anchor + kNodeParent, root)) return "the standings map is not readable";
    uint32_t size = 0;
    if (!mem.rd(svm + kOffMapSize, size)) return "the standings map size is not readable";
    if (root == 0) {
        if (size != 0) return "the standings map has no root but counts " + std::to_string(size) + " entries";
        return "";
    }
    if (size > kMaxKeys) return "the standings map counts " + std::to_string(size) + " entries (layout mismatch?)";
    struct Frame {
        uint64_t node, right;
        int32_t key;
    };
    std::vector<Frame> stack;
    std::vector<int32_t> out;
    uint64_t node = root, expect_parent = anchor;
    size_t visited = 0;
    bool first = true;
    int32_t last = 0;
    while (node != 0 || !stack.empty()) {
        while (node != 0) {
            if (!is_ptr(node, 8)) {
                keys.clear();
                return "the standings map holds a bad node pointer " + hex(node);
            }
            uint64_t right = 0, left = 0, parent = 0, value = 0;
            int32_t key = 0;
            if (!mem.rd(node + kNodeRight, right) || !mem.rd(node + kNodeLeft, left) || !mem.rd(node + kNodeParent, parent) ||
                !mem.rd(node + kNodeKey, key) || !mem.rd(node + kNodeValue, value)) {
                keys.clear();
                return "standings map node " + hex(node) + " is not readable";
            }
            if (parent != expect_parent) {
                keys.clear();
                return "standings map node " + hex(node) + " has parent " + hex(parent) + ", expected " + hex(expect_parent);
            }
            uint64_t vvt = 0;
            if (!is_ptr(value, 8) || !mem.rd(value, vvt) || (live_vtable && vvt != live_vtable)) {
                keys.clear();
                return "standings map node " + hex(node) + " (comp " + std::to_string(key) + ") does not hold a LiveStandings object (" + hex(value) +
                       ", vtable " + hex(vvt) + ")";
            }
            if (++visited > kMaxKeys) {
                keys.clear();
                return "the standings map has more than " + std::to_string(kMaxKeys) + " nodes (cycle or layout mismatch)";
            }
            stack.push_back({node, right, key});
            expect_parent = node;
            node = left;
        }
        Frame f = stack.back();
        stack.pop_back();
        if (!first && f.key <= last) {
            keys.clear();
            return "standings map keys are not ascending (" + std::to_string(last) + " then " + std::to_string(f.key) + ")";
        }
        first = false;
        last = f.key;
        out.push_back(f.key);
        expect_parent = f.node;
        node = f.right;
    }
    if (out.size() != size) {
        keys.clear();
        return "the standings map counts " + std::to_string(size) + " entries but " + std::to_string(out.size()) + " were walked";
    }
    keys = out;
    return "";
}

std::string sim_busy(Memory& mem, uint64_t managers) {
    std::string err;
    const uint64_t sdm = manager_at(mem, managers, kSimDayTypeId, err);
    if (!sdm) return "the SimDayManager is not available (" + err + ")";
    int32_t state = 0;
    if (!mem.rd(sdm + kOffSimDayState, state)) return "the SimDayManager " + hex(sdm) + " is not readable";
    if (state != 0) return "the game is processing a match day (SimDayManager state " + std::to_string(state) + "): try again when the hub is back";
    return "";
}

static Result fail(Result r, const char* stage, const std::string& msg) {
    r.ok = false;
    r.stage = stage;
    r.message = msg;
    return r;
}

static std::string list_keys(const std::vector<int32_t>& keys) {
    std::string s;
    for (size_t i = 0; i < keys.size(); ++i) s += (i ? ", " : "") + std::to_string(keys[i]);
    return s;
}

Result refresh(Memory& mem, Caller& call, const Fns& fns, const Request& req) {
    Result r;
    std::string err;
    if (const char* m = fns.missing())
        return fail(r, "validate", std::string("game function ") + m + " is not resolved on this game build");
    // 1. the manager table: published, else from the comm service
    uint64_t managers = req.managers;
    if (!managers && req.comm) managers = manager_table(mem, req.comm);
    const uint64_t vtable = fns.vtable ? fns.vtable : (req.image_base ? req.image_base + kRvaVtable : 0);
    const uint64_t live_vtable = req.image_base ? req.image_base + kRvaLiveStandingsVtable : 0;
    // 2. the SVM: the object of manager slot 108, which must be the published one when both are known
    uint64_t svm = 0;
    if (managers) {
        err = locate(mem, managers, svm, vtable);
        if (!err.empty()) return fail(r, "validate", err);
        if (req.svm && req.svm != svm)
            return fail(r, "validate", "the published StandingsViewManager " + hex(req.svm) + " is not the object of manager slot " +
                                           std::to_string(kTypeId) + " (" + hex(svm) + "): bridge_state.json is stale (reload the career)");
    } else if (req.svm) {
        svm = req.svm;  // its own table is checked below
    } else {
        return fail(r, "validate",
                    "the career's StandingsViewManager is not known: load a career (Turbo's Lua side publishes it in bridge_state.json)");
    }
    err = validate(mem, svm, managers, vtable, fns.listener);
    if (!err.empty()) return fail(r, "validate", err);
    if (!managers) managers = mem.ptr(svm + kOffCtx);
    r.svm = svm;
    r.managers = managers;
    // 3. everything refresh_comp dereferences on its way to FCE: the career's interface (slot 1), its Post function,
    //    the allocator; and the interface must be the one Turbo wrote to
    uint64_t ifce_expected = req.ifce;
    if (!ifce_expected) {
        const uint64_t career_ifce = manager_at(mem, managers, kIfceTypeId, err);
        if (career_ifce) ifce_expected = career_ifce;
    }
    err = validate_request_path(mem, svm, ifce_expected, fns.iface_post, fns.allocator, req.image_base, req.image_size);
    if (!err.empty()) return fail(r, "validate", err);
    // 4. never while the game processes a match day
    err = sim_busy(mem, managers);
    if (!err.empty()) return fail(r, "busy", err);
    // 5. the keys the game itself requested with
    std::string walk = map_keys(mem, svm, r.keys, live_vtable);
    const std::string what = req.label.empty() ? "" : req.label + ": ";
    if (walk.empty() && !r.keys.empty()) {
        for (int32_t k : r.keys) {
            if (!call.refresh_comp(svm, k, err))
                return fail(r, "refresh", what + "standings re-requested for " + std::to_string(r.refreshed) + " of " +
                                              std::to_string(r.keys.size()) + " competitions, then RequestStandingsSync(" +
                                              std::to_string(k) + ") failed: " + err);
            ++r.refreshed;
        }
        std::vector<int32_t> after;
        std::string again = map_keys(mem, svm, after, live_vtable);
        r.ok = true;
        r.stage = "done";
        r.message = what + "the game's standings view re-read " + std::to_string(r.refreshed) + " competition" +
                    (r.refreshed == 1 ? "" : "s") + " (comp ids " + list_keys(r.keys) + ")";
        if (!again.empty()) r.message += "; the map could not be re-read afterwards: " + again;
        else if (after.size() != r.keys.size()) r.message += "; the map now holds " + std::to_string(after.size()) + " competitions";
        return r;
    }
    // 6. an empty or inconsistent map: reported. The game's own load-time refresh (event 29 through the listener) runs
    //    only when the request opts in; a walk inconsistency means the layout is not what Turbo knows and nothing is called.
    const std::string why = walk.empty() ? "the standings map is empty" : walk;
    if (!walk.empty()) return fail(r, "walk", what + why + " (nothing was called)");
    if (!req.allow_fallback)
        return fail(r, "walk", what + why + ": nothing to re-request (the game fills it when a career loads; the full refresh is "
                                            "opt-in: turbo_output\\call_standings_refresh_full.txt)");
    if (!call.career_event(svm, kEventPostLoadPrepare, err)) return fail(r, "fallback", what + why + "; the full refresh failed: " + err);
    r.fallback = true;
    std::vector<int32_t> after;
    std::string again = map_keys(mem, svm, after, live_vtable);
    r.ok = true;
    r.stage = "done";
    r.message = what + why + ": the game's full standings refresh (POST_LOAD_PREPARE) was run instead";
    if (again.empty()) r.message += "; the map now holds " + std::to_string(after.size()) + " competition" + (after.size() == 1 ? "" : "s");
    return r;
}

// ---------------------------------------------------------------- one-shot gate
bool OneShotGate::arm(const std::string& label, std::string& why) {
    if (running_) {
        why = "a standings refresh is still running (" + label_ + ")";
        return false;
    }
    armed_ = true;
    label_ = label;
    return true;
}

bool OneShotGate::take(std::string& why) {
    if (running_) {
        why = "a standings refresh is still running (" + label_ + ")";
        return false;
    }
    if (!armed_) {
        why = "no edit armed a standings refresh";
        return false;
    }
    armed_ = false;
    running_ = true;
    return true;
}

void OneShotGate::done() { running_ = false; }

}  // namespace svm
}  // namespace turbo
