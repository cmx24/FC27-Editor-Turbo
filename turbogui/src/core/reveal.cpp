// FC 27 LE Turbo GUI - "reveal player data" game call (see reveal.h and docs/re/development.md)
#include "reveal.h"

#include <cstdio>
#include <cstring>

#include "standings_refresh.h"  // svm::manager_at: the manager-table walk (slot 78 must hold the PDRM)

namespace turbo {
namespace pdrm {

const char* Fns::missing() const {
    if (!vtable) return "pdrm_vtable";
    if (!reveal_player) return "pdrm_reveal_player";
    if (!reveal_team) return "pdrm_reveal_team";
    return nullptr;
}

static std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

static Record decode(const uint8_t* p) {
    Record r;
    std::memcpy(&r.player, p + kRecPlayer, 4);
    std::memcpy(&r.scout, p + kRecScout, 2);
    std::memcpy(&r.flags, p + kRecFlags, 2);
    std::memcpy(&r.points, p + kRecPoints, 4);
    std::memcpy(&r.day, p + kRecDay, 4);
    return r;
}

std::string read_records(Memory& mem, uint64_t pdrm, std::vector<Record>& records) {
    records.clear();
    uint64_t begin = 0, end = 0, cap = 0;
    if (!mem.rd(pdrm + kRecords, begin) || !mem.rd(pdrm + kRecordsEnd, end) || !mem.rd(pdrm + kRecordsCap, cap))
        return "the reveal record vector (pdrm+0x790) is not readable";
    if (begin == 0 && end == 0) return "";  // never allocated: no records
    if (!is_ptr(begin, 4) || end < begin || cap < end) return "the reveal record vector has a bad shape (begin " + hex(begin) + ", end " + hex(end) + ", cap " + hex(cap) + ")";
    const uint64_t bytes = end - begin;
    if (bytes % kRecordSize != 0) return "the reveal record vector holds " + std::to_string(bytes) + " bytes, not whole 0x14-byte records";
    const uint64_t count = bytes / kRecordSize;
    if (count > kMaxRecords) return "the reveal record vector holds " + std::to_string(count) + " records (the game keeps at most 1500)";
    if (count == 0) return "";
    std::vector<uint8_t> raw;
    if (!mem.read_block(begin, static_cast<size_t>(bytes), raw)) return "the reveal records at " + hex(begin) + " are not readable";
    records.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) records.push_back(decode(raw.data() + i * kRecordSize));
    return "";
}

bool find_record(Memory& mem, uint64_t pdrm, int player, Record& out) {
    std::vector<Record> recs;
    if (!read_records(mem, pdrm, recs).empty()) return false;
    for (const Record& r : recs)
        if (r.player == player) {
            out = r;
            return true;
        }
    return false;
}

// *(slot) must be a readable manager object (its first 8 bytes, the vtable pointer, readable)
static std::string hub_slot_ok(Memory& mem, uint64_t hub, uint64_t slot, const char* what) {
    uint64_t mgr = mem.chain(hub, {slot, 0});
    if (!mgr || !mem.ptr(mgr)) return std::string("the career hub's ") + what + " (hub+" + hex(slot) + ") is not readable: is a career loaded?";
    return "";
}

std::string validate(Memory& mem, uint64_t pdrm, uint64_t vtable, uint64_t managers) {
    if (!is_ptr(pdrm, 8)) return "PlayerDataRevealManager address " + hex(pdrm) + " is not a pointer";
    uint64_t vt = 0;
    if (!mem.rd(pdrm, vt)) return "PlayerDataRevealManager at " + hex(pdrm) + " is not readable";
    if (vtable && vt != vtable)
        return "the object at " + hex(pdrm) + " is not the PlayerDataRevealManager (vtable " + hex(vt) + ", expected " + hex(vtable) + ")";
    std::vector<uint8_t> tail;
    if (!mem.read_block(pdrm + kSize - 16, 16, tail)) return "PlayerDataRevealManager at " + hex(pdrm) + " is cut off (0x7D0 bytes expected)";
    const uint64_t hub = mem.ptr(pdrm + kHub);
    if (!hub) return "PlayerDataRevealManager has no career hub (+0x8)";
    const uint64_t own = mem.chain(hub, {kHubPdrm, 0});
    if (own != pdrm)
        return "the career hub's reveal-manager slot (hub+0x9D8) holds " + hex(own) + ", not " + hex(pdrm) + ": wrong object or no career";
    std::string e = hub_slot_ok(mem, hub, kHubCalendar, "calendar manager");
    if (!e.empty()) return e;
    e = hub_slot_ok(mem, hub, kHubTeams, "teams manager");
    if (!e.empty()) return e;
    if (managers) {
        std::string err;
        const uint64_t obj = svm::manager_at(mem, managers, kTypeId, err);
        if (!obj) return "manager table: " + err;
        if (obj != pdrm) return "manager slot 78 holds " + hex(obj) + ", not " + hex(pdrm);
    }
    std::vector<Record> recs;
    e = read_records(mem, pdrm, recs);
    if (!e.empty()) return e;
    for (size_t i = 1; i < recs.size(); ++i)
        if (recs[i].player <= recs[i - 1].player)
            return "the reveal records are not sorted by player id (record " + std::to_string(i) + ": " + std::to_string(recs[i - 1].player) + " then " +
                   std::to_string(recs[i].player) + "): layout mismatch?";
    return "";
}

uint64_t choose(uint64_t lua_pdrm, uint64_t managers, uint64_t seen, std::string& err) {
    err.clear();
    if (lua_pdrm == 0 && seen == 0) {
        err = "the career's PlayerDataRevealManager is not known yet: load a Manager Career and let a day pass";
        return 0;
    }
    if (lua_pdrm == 0) return seen;
    if (seen != 0 && lua_pdrm != seen && managers == 0) {
        err = "PlayerDataRevealManager mismatch: Lua found " + hex(lua_pdrm) + " but the game's events come from " + hex(seen) +
              " (and no manager table to tell which is right)";
        return 0;
    }
    return lua_pdrm;
}

static Result fail(Result r, const char* stage, const std::string& msg) {
    r.ok = false;
    r.stage = stage;
    r.message = msg;
    return r;
}

Result reveal(Memory& mem, Caller& call, const Fns& fns, const Request& req) {
    Result r;
    if (const char* m = fns.missing()) return fail(r, "validate", std::string("game function ") + m + " is not resolved on this game build");
    if (req.id <= 0) return fail(r, "validate", std::string(req.mode == Mode::Team ? "team" : "player") + " id must be a positive number");
    std::string err = validate(mem, req.pdrm, fns.vtable, req.managers);
    if (!err.empty()) return fail(r, "validate", err);
    std::vector<Record> recs;
    read_records(mem, req.pdrm, recs);  // validated above
    r.records_before = static_cast<int>(recs.size());
    Record before;
    const bool had = req.mode == Mode::Player && find_record(mem, req.pdrm, req.id, before);
    if (had) r.points_before = before.points;
    if (req.mode == Mode::Player && had && before.points >= kFullPoints) {
        r.ok = true;
        r.stage = "done";
        r.points_after = before.points;
        r.day = before.day;
        r.records_after = r.records_before;
        r.message = "player " + std::to_string(req.id) + " is already fully revealed (scouting points " + std::to_string(before.points) + "/204, dated " +
                    std::to_string(before.day) + ")";
        return r;
    }
    // the game's own call
    if (req.mode == Mode::Player) {
        if (!call.reveal_player(req.pdrm, req.id, err)) return fail(r, "call", "RevealPlayerFully: " + err);
    } else {
        if (!call.reveal_team(req.pdrm, req.id, err)) return fail(r, "call", "RevealTeamFully: " + err);
    }
    // read back
    err = read_records(mem, req.pdrm, recs);
    if (!err.empty()) return fail(r, "check", "after the call: " + err);
    r.records_after = static_cast<int>(recs.size());
    int today = 0;
    std::string terr;
    const bool have_today = call.today(req.pdrm, today, terr);
    char buf[256];
    if (req.mode == Mode::Player) {
        Record after;
        if (!find_record(mem, req.pdrm, req.id, after))
            return fail(r, "check", "the game made no reveal record for player " + std::to_string(req.id) + ": is that player id in the players table?");
        r.points_after = after.points;
        r.day = after.day;
        if (after.points < kFullPoints)
            return fail(r, "check", "player " + std::to_string(req.id) + " has a reveal record but only " + std::to_string(after.points) + "/204 points");
        if (have_today && after.day != today)
            std::snprintf(buf, sizeof(buf), "player %d: data revealed fully (scouting points %d/204, dated %d, but the game's calendar says %d%s)", req.id,
                          after.points, after.day, today, had ? "; the record existed before" : "");
        else
            std::snprintf(buf, sizeof(buf), "player %d: data revealed fully (scouting points %d/204, dated %d%s)", req.id, after.points, after.day,
                          had ? (", was " + std::to_string(before.points)).c_str() : ", no record before");
    } else {
        int full = 0;
        for (const Record& x : recs)
            if (x.points >= kFullPoints && (!have_today || x.day == today)) ++full;
        if (r.records_after < r.records_before)
            std::snprintf(buf, sizeof(buf), "team %d: the game revealed its players (%d reveal records now, %d before: the oldest were evicted at the 1500 cap)",
                          req.id, r.records_after, r.records_before);
        else
            std::snprintf(buf, sizeof(buf), "team %d: the game revealed its players (%d reveal records now, %d before; %d fully revealed%s)", req.id,
                          r.records_after, r.records_before, full, have_today ? " today" : "");
    }
    r.ok = true;
    r.stage = "done";
    r.message = buf;
    return r;
}

}  // namespace pdrm
}  // namespace turbo
