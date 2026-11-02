// FC 27 LE Turbo GUI - "squad_role" game call (see squad_role.h and docs/re/player_status_roles.md sections 3 and 7.2)
#include "squad_role.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <set>

#include "transfer_list.h"

namespace turbo {
namespace roles {

const char* action_name(int action) {
    switch (action) {
        case kActionSet: return "set";
        case kActionEnsure: return "ensure entry";
        case kActionStale: return "count stale";
        case kActionRemoveStale: return "remove stale";
        case kActionCheck: return "check";
        default: return "unknown action";
    }
}

const char* role_name(int role) {
    switch (role) {
        case 1: return "Crucial";
        case 2: return "Important";
        case 3: return "Rotation";
        case 4: return "Sporadic";
        case 5: return "Prospect";
        case kRoleNone: return "none";
        default: return "unknown";
    }
}

const char* Fns::missing() const {
    if (!psm_vtable) return "psm_vtable";
    if (!psm_handle_event) return "psm_handle_event";
    if (!find) return "psm_get_entry";
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

static Result fail(Result r, const char* stage, const std::string& msg) {
    r.ok = false;
    r.stage = stage;
    r.message = msg.size() < 511 ? msg : msg.substr(0, 507) + "...";
    return r;
}

static Result done(Result r, const std::string& msg) {
    r.ok = true;
    r.stage = "done";
    r.message = msg.size() < 511 ? msg : msg.substr(0, 507) + "...";
    return r;
}

static uint64_t manager(Memory& mem, uint64_t managers, int type) {
    int32_t count = 0;
    const uint64_t slot = managers + tl::kSlotSize * static_cast<uint64_t>(type);
    if (!mem.rd(slot + tl::kSlotCount, count) || count != 1) return 0;
    return mem.chain(slot, {tl::kSlotHolder, 0});
}

int Table::occurrences(int pid) const {
    int n = 0;
    for (int i = 0; i < count && i < static_cast<int>(entries.size()); ++i)
        if (entries[static_cast<size_t>(i)].pid == pid) ++n;
    return n;
}

int Table::index_of(int pid) const {
    for (int i = 0; i < count && i < static_cast<int>(entries.size()); ++i)
        if (entries[static_cast<size_t>(i)].pid == pid) return i;
    return -1;
}

std::string read_table(Memory& mem, uint64_t psm, int user_team, Table& t) {
    int32_t team = 0, count = -1;
    uint64_t b = 0, e = 0, cap = 0;
    if (!mem.rd(psm + kPsmTeam, team) || !mem.rd(psm + kPsmCount, count) || !mem.rd(psm + kPsmBegin, b) || !mem.rd(psm + kPsmEnd, e) ||
        !mem.rd(psm + kPsmCap, cap))
        return "the squad role table (PlayerStatusManager+0x10..+0x30) is not readable";
    if (team == -1) return "the squad role table is not set up yet (team -1; the game fills it at the season start): nothing was written";
    if (team != user_team) return "the squad role table belongs to team " + num(team) + ", not your club " + num(user_team) + ": nothing was written";
    if (count < 0 || count > kMaxEntries) return "the squad role table's count " + num(count) + " is out of range (0 to 52): layout mismatch?";
    if (!is_ptr(b, 4) || e != b + kTableBytes || cap != b + kTableBytes)
        return "the squad role table is not the fixed table of 52 entries (" + hex(b) + ".." + hex(e) + ", capacity " + hex(cap) + "): layout mismatch?";
    std::vector<uint8_t> data;
    if (!mem.read_block(b, kTableBytes, data)) return "the squad role entries at " + hex(b) + " are not readable";
    t.begin = b;
    t.count = count;
    t.entries.assign(kMaxEntries, Entry{});
    std::set<int32_t> seen;
    for (int i = 0; i < kMaxEntries; ++i) {
        const uint8_t* p = data.data() + static_cast<size_t>(i) * kEntrySize;
        Entry en;
        std::memcpy(&en.pid, p + kEntryPid, sizeof(en.pid));
        en.role = p[kEntryRole];
        en.promised = p[kEntryPromised];
        en.dismissed = p[kEntryDismissed];
        t.entries[static_cast<size_t>(i)] = en;
        if (i < count) {
            if (en.pid <= 0) return "squad role entry " + num(i) + " of " + num(count) + " holds player id " + num(en.pid) + ": layout mismatch?";
            if (!((en.role >= kRoleMin && en.role <= kRoleMax) || en.role == kRoleNone))
                return "squad role entry " + num(i) + " (player " + num(en.pid) + ") holds role " + num(en.role) + ": layout mismatch?";
            // X 2.0: a second entry of one player is recorded, not refused; run() refuses only a player of your club with two entries
            if (!seen.insert(en.pid).second && std::find(t.dup_pids.begin(), t.dup_pids.end(), en.pid) == t.dup_pids.end()) t.dup_pids.push_back(en.pid);
        } else if (en.pid != -1) {
            return "squad role entry " + num(i) + " after the " + num(count) + " used ones holds player id " + num(en.pid) + " (-1 expected): layout mismatch?";
        }
    }
    return "";
}

int64_t out1(const Request& req, const Result& r) {
    if (req.action == kActionSet || req.action == kActionEnsure) return r.how;
    return r.count;
}

Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req) {
    Result r;
    if (!req.bad_args.empty()) return fail(r, "validate", req.bad_args);
    if (!valid_action(req.action))
        return fail(r, "validate", "unknown squad_role code " + num(req.action) + " (1 set, 2 ensure entry, 3 count stale, 4 remove stale, 9 check)");
    const bool stale = req.action == kActionStale, check = req.action == kActionCheck, ensure = req.action == kActionEnsure;
    if (!stale && req.player <= 0) return fail(r, "validate", "player id must be a positive number");
    if (req.action == kActionSet && (req.role < kRoleMin || req.role > kRoleMax))
        return fail(r, "validate", "squad role " + num(req.role) + " is out of range (1 Crucial to 5 Prospect)");
    if (const char* m = fns.missing()) return fail(r, "validate", std::string("game function ") + m + " is not resolved on this game build");
    for (uint64_t p : {fns.psm_vtable, fns.psm_handle_event, fns.find, fns.is_player_in_team, fns.um_vtable})
        if (!in_image(p, req)) return fail(r, "validate", "resolved address " + hex(p) + " is outside FC27.exe");
    for (uint64_t p : {fns.add_player, fns.remove})
        if (p && !in_image(p, req)) return fail(r, "validate", "resolved address " + hex(p) + " is outside FC27.exe");
    // comm -> owner -> manager table
    if (!is_ptr(req.comm, 8)) return fail(r, "validate", "the career's comm service is not known (" + hex(req.comm) + "): is a career loaded?");
    const uint64_t owner = mem.ptr(req.comm + tl::kCommOwner), managers = owner ? mem.ptr(owner + tl::kOwnerManagers) : 0;
    if (!managers) return fail(r, "validate", "the comm service leads to no manager table: is a career loaded?");
    // PlayerStatusManager: vtable, slot 1 = HandleEvent, back pointer, whole object readable
    r.psm = manager(mem, managers, kTypeStatus);
    if (!is_ptr(r.psm, 8)) return fail(r, "validate", "manager slot 87 (PlayerStatusManager) holds no object: is a career loaded?");
    uint64_t vt = 0, slot1 = 0;
    if (!mem.rd(r.psm, vt) || vt != fns.psm_vtable)
        return fail(r, "validate", "the object in slot 87 is not the PlayerStatusManager (vtable " + hex(vt) + ", expected " + hex(fns.psm_vtable) + ")");
    if (!mem.rd(fns.psm_vtable + 8, slot1) || slot1 != fns.psm_handle_event)
        return fail(r, "validate", "the PlayerStatusManager's vtable slot 1 (" + hex(slot1) + ") is not the HandleEvent Turbo resolved: layout mismatch");
    if (mem.ptr(r.psm + kPsmHub) != managers) return fail(r, "validate", "the PlayerStatusManager's manager table is not the career's");
    std::vector<uint8_t> whole;
    if (!mem.read_block(r.psm, kPsmSize, whole)) return fail(r, "validate", "the PlayerStatusManager at " + hex(r.psm) + " is cut off (0x38 bytes expected)");
    // the user's club and the DataController
    const uint64_t um = manager(mem, managers, kTypeUserManager);
    std::string err = tl::user_team(mem, um, fns.um_vtable, r.user_team);
    if (!err.empty()) return fail(r, "validate", err);
    r.dc = manager(mem, managers, kTypeDataController);
    if (!is_ptr(r.dc, 8) || mem.ptr(r.dc + 0x10) != managers) return fail(r, "validate", "the DataController (slot 32) is not the career's");
    // the table (section 7.2): nothing is written unless it is exactly the layout Turbo knows
    Table t;
    err = read_table(mem, r.psm, r.user_team, t);
    if (!err.empty()) return fail(r, "validate", err);
    r.count = t.count;
    const uint64_t sub = r.psm + kPsmSub;
    // X 2.0: a player with two entries is tolerated only when he is no longer at your club (a stale leftover: the game reads the entries of
    // its own players only). Two entries of one of YOUR players = a table Turbo does not understand: nothing is written.
    for (int dup : t.dup_pids) {
        bool dup_in = false;
        if (!call.in_team(r.dc, dup, r.user_team, dup_in, err)) return fail(r, "validate", "IsPlayerInTeam(" + num(dup) + "): " + err);
        if (dup_in) return fail(r, "validate", "player " + num(dup) + " of your club has two squad role entries: nothing was written");
    }

    if (stale) {
        // count only: entries whose player the game no longer sees in your club (he left without a PlayerLeftTeam event)
        r.stale = 0;
        for (int i = 0; i < t.count; ++i) {
            const int pid = t.entries[static_cast<size_t>(i)].pid;
            bool in = false;
            if (!call.in_team(r.dc, pid, r.user_team, in, err)) return fail(r, "check", "IsPlayerInTeam(" + num(pid) + "): " + err);
            if (!in) ++r.stale;
        }
        r.role = r.stale;
        return done(r, num(r.stale) + " of " + num(t.count) + " squad role entries belong to players no longer at your club (counted only; the table holds at most 52)");
    }
    const std::string who = "player " + num(req.player);
    bool in = false;
    if (!call.in_team(r.dc, req.player, r.user_team, in, err)) return fail(r, "validate", "IsPlayerInTeam: " + err);
    if (req.action == kActionRemoveStale && in)
        return fail(r, "validate", who + " is at your club: his squad role entry is not stale (nothing removed)");
    if (req.action != kActionRemoveStale && !in)
        return fail(r, "validate", who + " is not in your club (team " + num(r.user_team) + "): squad roles exist for your players only (nothing was written)");
    // the game's Find must agree with Turbo's own scan of the validated table
    const int idx = t.index_of(req.player);
    const uint64_t expect = idx >= 0 ? t.begin + static_cast<uint64_t>(idx) * kEntrySize : 0;
    uint64_t found = 0;
    if (!call.find(sub, req.player, found, err)) return fail(r, "validate", "PlayerStatusData::Find: " + err);
    if (found != expect)
        return fail(r, "validate", "the game's Find returned " + hex(found) + " for " + who + " but the table holds " +
                                       (idx >= 0 ? "his entry at " + hex(expect) : std::string("no entry for him")) + ": nothing was written");

    if (req.action == kActionRemoveStale) {
        // the entry of a player who left the club without a PlayerLeftTeam event: the game's Remove, as its 0x60 handler does
        if (idx < 0) {
            r.role = 0;
            return done(r, who + " has no squad role entry (nothing to remove)");
        }
        if (req.create_off)
            return fail(r, "validate", who + ": stale squad role entry not removed (kill switch turbo_output\\" + std::string(create_kill_switch_name()) + " is present)");
        if (!fns.remove) return fail(r, "validate", who + ": stale squad role entry not removed (signature psm_remove_entry not found on this game build)");
        bool removed = false;
        const int occ = t.occurrences(req.player);  // X 2.0: a stale player may hold two entries; one Remove takes one of them
        if (!call.remove(sub, req.player, removed, err)) return fail(r, "call", "PlayerStatusData::Remove: " + err);
        Table t2;
        err = read_table(mem, r.psm, r.user_team, t2);
        if (!err.empty()) return fail(r, "check", "after the game's Remove of " + who + ": " + err);
        if (!removed || t2.count != t.count - 1 || t2.occurrences(req.player) != occ - 1)
            return fail(r, "check", "the game's Remove of the stale entry of " + who + " freed no slot (returned " + num(removed) + ", count " + num(t.count) +
                                        " -> " + num(t2.count) + ")");
        uint64_t again = 1;
        if (!call.find(sub, req.player, again, err) || (occ - 1 == 0 ? again != 0 : again == 0))
            return fail(r, "check", who + ": the game's Find does not agree with the table after Remove");
        r.removed = true;
        r.role = 1;
        r.count = t2.count;
        return done(r, who + ": stale squad role entry removed (he is no longer at your club; " + num(t2.count) + " of 52 entries used)");
    }

    if (idx >= 0) {
        r.entry = found;
        r.before = t.entries[static_cast<size_t>(idx)].role;
        r.role = r.before;
        if (check) return done(r, who + ": squad role " + role_name(r.before) + " (entry " + num(idx + 1) + " of " + num(t.count) + "; checked only)");
        if (ensure || r.before == req.role) {
            r.how = kHowUnchanged;
            return done(r, who + ": squad role " + role_name(r.before) + (ensure ? " (he has an entry: left alone)" : " already (nothing written)"));
        }
        // ONE byte: wasPromised and renewalDismissed after it stay as the game left them
        const uint8_t v = static_cast<uint8_t>(req.role);
        if (!mem.wr(found + kEntryRole, v)) return fail(r, "write", "cannot write the role byte of " + who + " at " + hex(found + kEntryRole));
        r.written = true;
        uint64_t again = 0;
        uint8_t after = 0;
        if (!call.find(sub, req.player, again, err)) return fail(r, "check", "PlayerStatusData::Find after the write: " + err);
        if (again != found || !mem.rd(found + kEntryRole, after)) return fail(r, "check", who + ": the entry moved or cannot be read back after the write");
        r.role = after;
        if (after != v) return fail(r, "check", who + ": the game kept squad role " + role_name(after) + " (asked " + role_name(v) + ")");
        r.how = kHowWritten;
        return done(r, who + ": squad role " + role_name(r.before) + " -> " + role_name(after));
    }

    // no entry: the Squad screen computes a role each time it opens (0x147F4C4A0); the match load and morale see none
    r.before = static_cast<int>(kNoEntry);
    r.role = kNoEntry;
    const bool full = t.count >= kMaxEntries;
    if (check) return done(r, who + ": no squad role entry (" + num(t.count) + " of 52 used" + (full ? ", the table is full" : "") + "; checked only)");
    const std::string why = req.create_off ? std::string("creation is off: kill switch turbo_output\\") + create_kill_switch_name() + " is present"
                            : !fns.add_player ? std::string("creation is off: signature psm_add_player not found on this game build")
                            : full            ? std::string("the table is full, 52 of 52: entries of players who left the club must be removed first")
                                              : std::string();
    if (!why.empty())
        return fail(r, "validate", who + " has no squad role entry (a club move that skipped the game's events): not created (" + why + "), counted only");
    bool added = false;
    const int arg = ensure ? static_cast<int>(kRoleNone) : req.role;
    if (!call.add_player(r.psm, req.player, arg, added, err)) return fail(r, "call", "PlayerStatusManager::AddPlayer: " + err);
    Table t3;
    err = read_table(mem, r.psm, r.user_team, t3);
    if (!err.empty()) {
        r.role = -1;
        return fail(r, "check", "after the game's AddPlayer for " + who + ": " + err);
    }
    const int ni = t3.index_of(req.player);
    if (!added || t3.count != t.count + 1 || ni != t.count) {
        r.role = ni < 0 ? kNoEntry : -1;
        return fail(r, "check", "the game's AddPlayer did not add the entry of " + who + " (returned " + num(added) + ", count " + num(t.count) + " -> " +
                                    num(t3.count) + ")");
    }
    const uint64_t e = t3.begin + static_cast<uint64_t>(ni) * kEntrySize;
    r.created = true;
    r.entry = e;
    r.count = t3.count;
    r.role = -1;
    const Entry& ne = t3.entries[static_cast<size_t>(ni)];
    if (!ensure && ne.role != req.role)
        return fail(r, "check", who + ": the game's AddPlayer stored squad role " + role_name(ne.role) + " (asked " + role_name(req.role) + ")");
    // code 1: wasPromised back to 0. The game's AddPlayer marks an explicit role as promised (a contract promise); Turbo promises
    // nothing, so the entry looks like the game's own computed ones (and like the entries code 1 only rewrites)
    if (!ensure && ne.promised != 0) {
        const uint8_t zero = 0;
        if (!mem.wr(e + kEntryPromised, zero)) return fail(r, "write", "cannot clear the promised flag of " + who + " at " + hex(e + kEntryPromised));
    }
    uint64_t again = 0;
    uint8_t after = 0, promised = 1;
    if (!call.find(sub, req.player, again, err)) return fail(r, "check", "PlayerStatusData::Find after AddPlayer: " + err);
    if (again != e || !mem.rd(e + kEntryRole, after) || !mem.rd(e + kEntryPromised, promised))
        return fail(r, "check", who + ": the game's Find returns " + hex(again) + ", not the created entry " + hex(e));
    r.role = after;
    if (!ensure && (after != req.role || promised != 0))
        return fail(r, "check", who + ": the created entry reads back squad role " + role_name(after) + ", promised " + num(promised));
    r.how = kHowCreated;
    return done(r, who + (ensure ? ": squad role entry created, the game chose " : ": squad role entry created with ") + role_name(after) + " (he had none)");
}

Request request_from_args(const int64_t args[4]) {
    Request q;
    q.comm = static_cast<uint64_t>(args[0]);
    if (args[1] < 0 || args[1] > 0xFF) q.bad_args = "squad_role: the code word has unknown bits set";
    q.action = static_cast<int>(args[1] & 0xFF);
    if ((args[2] < 0 || args[2] > 0x7FFFFFFF) && q.bad_args.empty()) q.bad_args = "player id must be a positive number";
    q.player = static_cast<int>(args[2] & 0x7FFFFFFF);
    if ((args[3] < 0 || args[3] > kRoleMax) && q.bad_args.empty() && q.action == kActionSet)
        q.bad_args = "squad role out of range (1 Crucial to 5 Prospect)";
    q.role = static_cast<int>(args[3] >= 0 && args[3] <= kRoleMax ? args[3] : -1);
    return q;
}

void args_from_request(const Request& req, int64_t args[4]) {
    args[0] = static_cast<int64_t>(req.comm);
    args[1] = req.action;
    args[2] = req.player;
    args[3] = req.role;
}

bool killed(const std::filesystem::path& turbo_output) {
    std::error_code ec;
    return std::filesystem::exists(turbo_output / kill_switch_name(), ec);
}

bool create_killed(const std::filesystem::path& turbo_output) {
    std::error_code ec;
    return std::filesystem::exists(turbo_output / create_kill_switch_name(), ec);
}

}  // namespace roles
}  // namespace turbo
