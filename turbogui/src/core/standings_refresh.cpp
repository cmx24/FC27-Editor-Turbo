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

std::string locate(Memory& mem, uint64_t managers, uint64_t& out) {
    out = 0;
    std::string err;
    uint64_t svm = manager_at(mem, managers, kTypeId, err);
    if (!svm) return "StandingsViewManager: " + err;
    out = svm;
    return "";
}

std::string validate(Memory& mem, uint64_t svm, uint64_t managers, uint64_t vtable, uint64_t slot10) {
    if (!is_ptr(svm, 8)) return "StandingsViewManager address " + hex(svm) + " is not a pointer";
    uint64_t vt = 0;
    if (!mem.rd(svm, vt)) return "StandingsViewManager at " + hex(svm) + " is not readable";
    if (vtable && vt != vtable)
        return "the object at " + hex(svm) + " is not the StandingsViewManager (vtable " + hex(vt) + ", expected " + hex(vtable) + ")";
    if (slot10) {
        uint64_t fn = 0;
        if (!is_ptr(vt, 8) || !mem.rd(vt + kVtableSlot10, fn) || fn != slot10)
            return "the object at " + hex(svm) + " has another vtable (slot 10 " + hex(fn) + ", expected " + hex(slot10) + ")";
    }
    uint8_t tail = 0;
    if (!mem.rd(svm + kOffLastField, tail)) return "StandingsViewManager at " + hex(svm) + " is cut off (0x498 bytes expected)";
    uint64_t ctx = mem.ptr(svm + kOffCtx);
    if (!ctx) return "StandingsViewManager has no manager table pointer (+0x8)";
    if (managers && ctx != managers)
        return "StandingsViewManager " + hex(svm) + " belongs to manager table " + hex(ctx) + ", not " + hex(managers) + " (another career?)";
    std::string err;
    uint64_t back = manager_at(mem, ctx, kTypeId, err);
    if (!back) return "the manager table's slot 108 is not readable: " + err;
    if (back != svm) return "the manager table's slot 108 holds " + hex(back) + ", not " + hex(svm);
    return "";
}

std::string map_keys(Memory& mem, uint64_t svm, std::vector<int32_t>& keys) {
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
            uint64_t right = 0, left = 0, parent = 0;
            int32_t key = 0;
            if (!mem.rd(node + kNodeRight, right) || !mem.rd(node + kNodeLeft, left) || !mem.rd(node + kNodeParent, parent) ||
                !mem.rd(node + kNodeKey, key)) {
                keys.clear();
                return "standings map node " + hex(node) + " is not readable";
            }
            if (parent != expect_parent) {
                keys.clear();
                return "standings map node " + hex(node) + " has parent " + hex(parent) + ", expected " + hex(expect_parent);
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
    // 2. the SVM: published, else located through the table
    uint64_t svm = req.svm;
    if (!svm) {
        if (!managers)
            return fail(r, "validate",
                        "the career's StandingsViewManager is not known: load a career (Turbo's Lua side publishes it in bridge_state.json)");
        err = locate(mem, managers, svm);
        if (!err.empty()) return fail(r, "validate", err);
    }
    const uint64_t vtable = fns.vtable ? fns.vtable : (req.image_base ? req.image_base + kRvaVtable : 0);
    err = validate(mem, svm, managers, vtable, fns.slot10);
    if (!err.empty()) return fail(r, "validate", err);
    if (!managers) managers = mem.ptr(svm + kOffCtx);
    r.svm = svm;
    r.managers = managers;
    // 3. the FCE interface Turbo wrote to must be the career's (manager slot 1)
    if (req.ifce) {
        uint64_t career_ifce = manager_at(mem, managers, kIfceTypeId, err);
        if (career_ifce && career_ifce != req.ifce)
            return fail(r, "validate", "the FCE interface Turbo edited (" + hex(req.ifce) + ") is not the career's (" + hex(career_ifce) +
                                           "): the rows edited are not the ones the game shows");
    }
    uint8_t enabled = 1;
    if (mem.rd(svm + kOffEnabled, enabled)) r.enabled = enabled != 0;
    // 4. the keys the game itself requested with
    std::string walk = map_keys(mem, svm, r.keys);
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
        std::string again = map_keys(mem, svm, after);
        r.ok = true;
        r.stage = "done";
        r.message = what + "the game's standings view re-read " + std::to_string(r.refreshed) + " competition" +
                    (r.refreshed == 1 ? "" : "s") + " (comp ids " + list_keys(r.keys) + ")";
        if (!again.empty()) r.message += "; the map could not be re-read afterwards: " + again;
        else if (after.size() != r.keys.size()) r.message += "; the map now holds " + std::to_string(after.size()) + " competitions";
        if (!r.enabled) r.message += " (the view manager is disabled: the screen may not show them yet)";
        return r;
    }
    // 5. fallback: the game's own load-time refresh (event 29) when the map is empty or cannot be walked
    const std::string why = walk.empty() ? "the standings map is empty" : walk;
    if (!req.allow_fallback) return fail(r, "walk", what + why);
    if (!call.career_event(svm, kEventPostLoadPrepare, err)) return fail(r, "fallback", what + why + "; the full refresh failed: " + err);
    r.fallback = true;
    std::vector<int32_t> after;
    std::string again = map_keys(mem, svm, after);
    r.ok = true;
    r.stage = "done";
    r.message = what + why + ": the game's full standings refresh (POST_LOAD_PREPARE) was run instead";
    if (again.empty()) r.message += "; the map now holds " + std::to_string(after.size()) + " competition" + (after.size() == 1 ? "" : "s");
    if (!r.enabled) r.message += " (the view manager is disabled: the screen may not show them yet)";
    return r;
}

}  // namespace svm
}  // namespace turbo
