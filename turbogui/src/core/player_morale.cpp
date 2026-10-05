// FC 27 LE Turbo GUI - "player_morale" game call (see player_morale.h and docs/re/player_status_roles.md section 4)
#include "player_morale.h"

#include <cstdio>
#include <cstring>

#include "standings_refresh.h"
#include "transfer_list.h"

namespace turbo {
namespace morale {

const char* action_name(int action) {
    switch (action) {
        case kActionVeryHappy: return "very happy";
        case kActionValue: return "value";
        case kActionStale: return "count stale";
        case kActionCheck: return "check";
        default: return "unknown action";
    }
}

const char* Fns::missing() const {
    if (!pmm_vtable) return "pmm_vtable";
    if (!pmm_handle_event) return "pmm_handle_event";
    if (!find_record) return "pmm_find_record";
    if (!set_total) return "pmm_set_total";
    if (!is_player_in_team) return "dc_is_player_in_team";
    if (!um_vtable) return "um_vtable";
    return nullptr;
}

static std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}
static std::string num(long long v) { return std::to_string(v); }

static bool in_image(uint64_t p, const Request& req) {
    if (!req.image_base || !req.image_size) return true;
    return p >= req.image_base && p < req.image_base + req.image_size;
}

static const char* level_name(long long l) {
    static const char* n[] = {"very unhappy", "unhappy", "content", "happy", "very happy", "complacent"};
    return l >= 0 && l < kLevelCount ? n[l] : "unknown";
}

static Result fail(Result r, const char* stage, const std::string& msg) {
    r.ok = false;
    r.stage = stage;
    r.message = msg.size() < 511 ? msg : msg.substr(0, 507) + "...";
    return r;
}

static uint64_t manager(Memory& mem, uint64_t managers, int type) {
    int32_t count = 0;
    const uint64_t slot = managers + tl::kSlotSize * static_cast<uint64_t>(type);
    if (!mem.rd(slot + tl::kSlotCount, count) || count != 1) return 0;
    return mem.chain(slot, {tl::kSlotHolder, 0});
}

int very_happy_target(Caller& call, uint64_t pmm, int emotion, bool have_level, std::string& note) {
    if (!have_level) {
        note = "the game's level function is not resolved: " + num(kFallbackTotal) + " used";
        return kFallbackTotal;
    }
    int best = -1;
    for (int t = 0; t <= kMaxTotal; ++t) {
        int l = -1;
        std::string err;
        if (!call.level(pmm, t, emotion, l, err)) {
            note = "the game's level function failed (" + err + "): " + num(kFallbackTotal) + " used";
            return kFallbackTotal;
        }
        if (l == kLevelVeryHappy) best = t;
    }
    if (best < 0) {
        note = "no total 0..120 gives the very happy level for emotion " + num(emotion) + ": " + num(kFallbackTotal) + " used";
        return kFallbackTotal;
    }
    return best;
}

Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req) {
    Result r;
    if (!req.bad_args.empty()) return fail(r, "validate", req.bad_args);
    if (!valid_action(req.action)) return fail(r, "validate", "unknown player_morale code " + num(req.action) + " (1 very happy, 2 value, 3 count stale, 9 check)");
    const bool stale = req.action == kActionStale;
    if (!stale && req.player <= 0) return fail(r, "validate", "player id must be a positive number");
    if (req.action == kActionValue && (req.value < 0 || req.value > kMaxTotal))
        return fail(r, "validate", "morale " + num(req.value) + " is out of range (0 to " + num(kMaxTotal) + ")");
    if (const char* m = fns.missing()) return fail(r, "validate", std::string("game function ") + m + " is not resolved on this game build");
    for (uint64_t p : {fns.pmm_vtable, fns.pmm_handle_event, fns.find_record, fns.set_total, fns.is_player_in_team, fns.um_vtable})
        if (!in_image(p, req)) return fail(r, "validate", "resolved address " + hex(p) + " is outside FC27.exe");
    if (fns.level && !in_image(fns.level, req)) return fail(r, "validate", "resolved address " + hex(fns.level) + " is outside FC27.exe");
    // comm -> owner -> manager table
    if (!is_ptr(req.comm, 8)) return fail(r, "validate", "the career's comm service is not known (" + hex(req.comm) + "): is a career loaded?");
    const uint64_t owner = mem.ptr(req.comm + tl::kCommOwner), managers = owner ? mem.ptr(owner + tl::kOwnerManagers) : 0;
    if (!managers) return fail(r, "validate", "the comm service leads to no manager table: is a career loaded?");
    // PlayerMoraleManager: vtable, slot 1 = HandleEvent, back pointer, whole object readable
    r.pmm = manager(mem, managers, kTypeMorale);
    if (!is_ptr(r.pmm, 8)) return fail(r, "validate", "manager slot 83 (PlayerMoraleManager) holds no object: is a career loaded?");
    uint64_t vt = 0, slot1 = 0;
    if (!mem.rd(r.pmm, vt) || vt != fns.pmm_vtable)
        return fail(r, "validate", "the object in slot 83 is not the PlayerMoraleManager (vtable " + hex(vt) + ", expected " + hex(fns.pmm_vtable) + ")");
    if (!mem.rd(fns.pmm_vtable + 8, slot1) || slot1 != fns.pmm_handle_event)
        return fail(r, "validate", "the PlayerMoraleManager's vtable slot 1 (" + hex(slot1) + ") is not the HandleEvent Turbo resolved: layout mismatch");
    if (mem.ptr(r.pmm + kPmmHub) != managers) return fail(r, "validate", "the PlayerMoraleManager's manager table is not the career's");
    uint8_t gate = 0xFF;
    if (!mem.rd(r.pmm + kGate, gate)) return fail(r, "validate", "the PlayerMoraleManager is cut off (gate byte +0x554 not readable)");
    if (gate != 0)
        return fail(r, "validate", "the morale store is not initialised yet (gate byte " + num(gate) + ", set after a job change until the next season start): nothing was written");
    // the user's club and the DataController
    const uint64_t um = manager(mem, managers, kTypeUserManager);
    std::string err = tl::user_team(mem, um, fns.um_vtable, r.user_team);
    if (!err.empty()) return fail(r, "validate", err);
    r.dc = manager(mem, managers, kTypeDataController);
    if (!is_ptr(r.dc, 8) || mem.ptr(r.dc + 0x10) != managers) return fail(r, "validate", "the DataController (slot 32) is not the career's");
    // the store: team, vector of 0x60-byte records, at most 52
    const uint64_t store = r.pmm + kStore;
    int32_t store_team = 0;
    uint64_t b = 0, e = 0;
    if (!mem.rd(store + kStoreTeam, store_team) || !mem.rd(store + kStoreBegin, b) || !mem.rd(store + kStoreEnd, e))
        return fail(r, "validate", "the morale store (PlayerMoraleManager+0x518) is not readable");
    if (store_team != r.user_team)
        return fail(r, "validate", "the morale store belongs to team " + num(store_team) + ", not your club " + num(r.user_team) + ": nothing was written");
    if (b > e || (e - b) % kRecSize != 0 || (e - b) / kRecSize > static_cast<uint64_t>(kMaxRecords) || (e != b && !is_ptr(b, 4)))
        return fail(r, "validate", "the morale store's record vector is corrupt (" + hex(b) + ".." + hex(e) + ", layout mismatch?)");
    r.records = static_cast<int>((e - b) / kRecSize);
    std::vector<uint8_t> data;
    if (r.records > 0 && !mem.read_block(b, static_cast<size_t>(e - b), data)) return fail(r, "validate", "the morale records are not readable");
    err = svm::sim_busy(mem, managers);
    if (!err.empty()) return fail(r, "validate", err);

    if (stale) {
        // count only: records whose player the game no longer sees in your club (no write: no callable erase was verified)
        r.stale = 0;
        for (int i = 0; i < r.records; ++i) {
            int32_t pid = 0;
            std::memcpy(&pid, data.data() + static_cast<size_t>(i) * kRecSize + kRecPid, sizeof(pid));
            bool in = false;
            if (!call.in_team(r.dc, pid, r.user_team, in, err)) return fail(r, "check", "IsPlayerInTeam(" + num(pid) + "): " + err);
            if (!in) ++r.stale;
        }
        r.total = r.stale;
        r.level = r.records;
        r.ok = true;
        r.stage = "done";
        r.message = num(r.stale) + " of " + num(r.records) + " morale records belong to players no longer at your club (counted only, nothing removed; the store holds at most 52)";
        return r;
    }
    const std::string who = "player " + num(req.player);
    bool in = false;
    if (!call.in_team(r.dc, req.player, r.user_team, in, err)) return fail(r, "validate", "IsPlayerInTeam: " + err);
    if (!in) return fail(r, "validate", who + " is not in your club (team " + num(r.user_team) + "): morale exists for your players only (nothing was written)");
    if (!call.find_record(store, req.player, r.rec, err)) return fail(r, "validate", "MoraleStore::Find: " + err);
    if (!r.rec) {
        r.total = kNoRecord;
        return fail(r, "validate", who + " has no morale record (the game shows \"Unknown\"; a club move that skipped the game's events): not created by this version, counted only");
    }
    if (r.rec < b || r.rec >= e || (r.rec - b) % kRecSize != 0) return fail(r, "validate", "the game's Find returned " + hex(r.rec) + ", outside the record vector");
    int32_t rpid = 0, emotion = -1, before = -1;
    if (!mem.rd(r.rec + kRecPid, rpid) || rpid != req.player || !mem.rd(r.rec + kRecEmotion, emotion) || !mem.rd(r.rec + kRecTotal, before))
        return fail(r, "validate", "the record " + hex(r.rec) + " does not hold " + who);
    r.before = before;
    std::string note;
    r.target = req.action == kActionValue ? req.value : very_happy_target(call, r.pmm, emotion, fns.level != 0, note);
    if (req.action == kActionCheck) {
        r.ok = true;
        r.stage = "done";
        r.total = before;
        int l = -1;
        if (fns.level && call.level(r.pmm, before, emotion, l, err)) r.level = l;
        r.message = who + ": morale " + num(before) + " (" + level_name(r.level) + "), very happy target " + num(r.target) + (note.empty() ? "" : " (" + note + ")") + " (checked only)";
        return r;
    }
    if (!call.set_total(r.pmm, r.rec, r.target, err)) return fail(r, "call", "SetTotalMorale: " + err);
    r.called = true;
    int32_t after = -1;
    if (!mem.rd(r.rec + kRecTotal, after)) return fail(r, "check", "SetTotalMorale ran but the record is not readable");
    r.total = after;
    int l = -1;
    if (fns.level && call.level(r.pmm, after, emotion, l, err)) r.level = l;
    if (after != r.target) return fail(r, "check", who + ": the game kept morale " + num(after) + " (asked " + num(r.target) + ")");
    r.ok = true;
    r.stage = "done";
    r.message = who + ": morale " + num(before) + " -> " + num(after) + " (" + level_name(r.level) + ")" + (note.empty() ? "" : "; " + note);
    return r;
}

Request request_from_args(const int64_t args[4]) {
    Request q;
    q.comm = static_cast<uint64_t>(args[0]);
    if (args[1] < 0 || args[1] > 0xFF) q.bad_args = "player_morale: the code word has unknown bits set";
    q.action = static_cast<int>(args[1] & 0xFF);
    if (args[2] < 0 || args[2] > 0x7FFFFFFF) q.bad_args = "player id must be a positive number";
    q.player = static_cast<int>(args[2] & 0x7FFFFFFF);
    if (args[3] < 0 || args[3] > kMaxTotal) {
        if (q.bad_args.empty() && q.action == kActionValue) q.bad_args = "morale value out of range (0 to 120)";
    }
    q.value = static_cast<int>(args[3] >= 0 && args[3] <= kMaxTotal ? args[3] : -1);
    return q;
}

void args_from_request(const Request& req, int64_t args[4]) {
    args[0] = static_cast<int64_t>(req.comm);
    args[1] = req.action;
    args[2] = req.player;
    args[3] = req.value;
}

bool killed(const std::filesystem::path& turbo_output) {
    std::error_code ec;
    return std::filesystem::exists(turbo_output / kill_switch_name(), ec);
}

}  // namespace morale
}  // namespace turbo
