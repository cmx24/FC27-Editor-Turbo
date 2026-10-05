// FC 27 LE Turbo GUI - "player_move" game call (see player_move.h and docs/re/realtime_transfers.md)
#include "player_move.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "standings_refresh.h"
#include "transfer_list.h"

namespace turbo {
namespace pm {

const char* action_name(int action) {
    switch (action) {
        case kActionMove: return "move";
        case kActionRelease: return "release";
        case kActionCheck: return "check";
        default: return "unknown action";
    }
}

const char* Fns::missing(int action) const {
    if (!player_moved) return "teamutil_player_moved";
    if (!is_player_in_team) return "dc_is_player_in_team";
    if (!squad_counts) return "dc_squad_counts";
    if (!league_of_team) return "dc_get_league_of_team";
    if (!is_international) return "is_international_league";
    if (!pcm_vtable) return "pcm_vtable";
    if (!tm_vtable) return "tm_vtable";
    if (!um_vtable) return "um_vtable";
    if (action == kActionRelease) {
        if (!release_player) return "ctm_release_player";
        if (!ctm_vtable) return "ctm_vtable";
    } else if (!add_contract) {
        return "pcm_add_contract_record";
    }
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

// A readable object of `size` bytes whose first word is `vtable`
static std::string check_object(Memory& mem, uint64_t obj, uint64_t size, uint64_t vtable, const char* what) {
    if (!is_ptr(obj, 8)) return std::string(what) + " pointer " + hex(obj) + " is not a pointer";
    uint64_t vt = 0;
    if (!mem.rd(obj, vt)) return std::string(what) + " at " + hex(obj) + " is not readable";
    if (vt != vtable)
        return std::string("the object at ") + hex(obj) + " is not the " + what + " (vtable " + hex(vt) + ", expected " + hex(vtable) + ")";
    std::vector<uint8_t> tail;
    if (size > tl::kTailCheck && !mem.read_block(obj + size - tl::kTailCheck, tl::kTailCheck, tail))
        return std::string(what) + " at " + hex(obj) + " is cut off (" + hex(size) + " bytes expected)";
    return "";
}

// A manager whose +0x08 must be the career's manager table
static std::string same_managers(Memory& mem, uint64_t obj, uint64_t managers, const char* what) {
    const uint64_t m = mem.ptr(obj + tl::kMgrManagers);
    if (m != managers) return std::string("the ") + what + "'s manager table (" + hex(m) + ") is not the career's (" + hex(managers) + ")";
    return "";
}

// The object in manager slot `type` of the table: instance count 1, holder array -> object, and (strict) the type descriptor flag 1 the
// transfer_list / standings calls check. The slots that were never read live (DataController, TeamUtil, ContractTerminationManager,
// IniSettings, LoansManager, the release counter, PlayerMoraleManager) are not strict: the hub builder registers every manager the same
// way (object stored at array[count], then count + 1: e.g. the ContractTerminationManager at 0x147F180DA..0x147F180FB), and each of them is proven by its own back pointer / vtable /
// plausibility check instead, so a descriptor that differs from the proven slots' cannot refuse a call the object checks accept.
static std::string manager_at(Memory& mem, uint64_t managers, int type, uint64_t& out, const char* what, bool strict = true) {
    out = 0;
    const uint64_t slot = managers + tl::kSlotSize * static_cast<uint64_t>(type);
    int32_t count = 0, flag = 0;
    if (!mem.rd(slot + tl::kSlotCount, count)) return std::string("manager slot ") + num(type) + " (" + what + ") is not readable";
    if (count != 1) return std::string("manager slot ") + num(type) + " (" + what + ") holds " + num(count) + " objects (1 expected)";
    const uint64_t mtype = mem.ptr(slot + tl::kSlotType);
    if (strict && (!mtype || !mem.rd(mtype + tl::kTypeFlag, flag) || flag != 1))
        return std::string("manager slot ") + num(type) + " (" + what + ") has no type descriptor";
    out = mem.chain(slot, {tl::kSlotHolder, 0});
    if (!out) return std::string("manager slot ") + num(type) + " (" + what + ") holds no object: is a career loaded?";
    return "";
}

// One eastl vector of `elem`-byte records at obj + off_begin / obj + off_end: begin <= end, whole records, at most `max_records`,
// every byte readable (an empty vector may hold null pointers). "" = fine.
static std::string read_vec(Memory& mem, uint64_t obj, uint64_t off_begin, uint64_t off_end, uint64_t elem, int max_records, const std::string& what,
                            std::vector<uint8_t>& data) {
    data.clear();
    uint64_t b = 0, e = 0;
    if (!mem.rd(obj + off_begin, b) || !mem.rd(obj + off_end, e)) return what + " is not readable";
    if (b > e) return what + " is corrupt: begin " + hex(b) + " is after end " + hex(e) + " (layout mismatch?)";
    const uint64_t bytes = e - b;
    if (bytes % elem != 0)
        return what + " is corrupt: " + num(static_cast<long long>(bytes)) + " bytes is not a whole number of " + num(static_cast<long long>(elem)) +
               "-byte records (layout mismatch?)";
    if (bytes / elem > static_cast<uint64_t>(max_records))
        return what + " is corrupt: " + num(static_cast<long long>(bytes / elem)) + " records (at most " + num(max_records) + " expected, layout mismatch?)";
    if (bytes == 0) return "";
    if (!is_ptr(b, 4)) return what + " is corrupt: its records pointer " + hex(b) + " is not a pointer";
    if (!mem.read_block(b, static_cast<size_t>(bytes), data)) return what + " at " + hex(b) + " is not readable (" + num(static_cast<long long>(bytes)) + " bytes)";
    return "";
}

// A function slot of a vtable that must hold a pointer inside the image (the game calls it)
static std::string fn_slot(Memory& mem, uint64_t vt, int slot, const Request& req, const char* what) {
    uint64_t fn = 0;
    if (!mem.rd(vt + static_cast<uint64_t>(slot) * 8, fn) || !is_ptr(fn, 1) || !in_image(fn, req))
        return std::string(what) + " vtable " + hex(vt) + " slot " + num(slot) + " is not a function inside FC27.exe";
    return "";
}

std::string loan_state(Memory& mem, uint64_t loans, int pid, bool& loaned, int& club) {
    loaned = false;
    club = 0;
    std::vector<uint8_t> data;
    std::string err = read_vec(mem, loans, kLoansBegin, kLoansEnd, kLoanRecord, kMaxLoanRecords, "the LoansManager's loan list (+0x40)", data);
    if (!err.empty()) return err;
    for (size_t off = 0; off + kLoanRecord <= data.size(); off += kLoanRecord) {
        int32_t id = 0, team = 0;
        std::memcpy(&id, data.data() + off, sizeof(id));
        std::memcpy(&team, data.data() + off + 4, sizeof(team));
        if (id == pid) {
            loaned = true;
            club = team;
            return "";
        }
    }
    return "";
}

std::string locate(Memory& mem, const Request& req, const Fns& fns, Located& out) {
    out = Located();
    if (const char* m = fns.missing(req.action)) return std::string("game function ") + m + " is not resolved on this game build";
    const bool release = req.action == kActionRelease;
    std::vector<uint64_t> addresses = {fns.player_moved, fns.is_player_in_team, fns.squad_counts, fns.league_of_team, fns.is_international,
                                       fns.pcm_vtable,   fns.tm_vtable,         fns.um_vtable};
    if (release) {
        addresses.push_back(fns.release_player);
        addresses.push_back(fns.ctm_vtable);
    } else {
        addresses.push_back(fns.add_contract);
    }
    if (fns.morale_vtable) addresses.push_back(fns.morale_vtable);
    if (fns.morale_handle_event) addresses.push_back(fns.morale_handle_event);
    for (uint64_t p : addresses)
        if (!in_image(p, req)) return "resolved address " + hex(p) + " is outside FC27.exe";
    // comm -> owner -> hub, re-walked on every call
    if (!is_ptr(req.comm, 8)) return "the career's comm service is not known (" + hex(req.comm) + "): is the Turbo GUI running in a career?";
    out.owner = mem.ptr(req.comm + tl::kCommOwner);
    if (!out.owner) return "comm service " + hex(req.comm) + " has no career-mode owner (+0x20): is a career loaded?";
    out.managers = mem.ptr(out.owner + tl::kOwnerManagers);
    if (!out.managers) return "the career-mode owner " + hex(out.owner) + " has no manager table (+0x10)";
    if (req.managers && req.managers != out.managers)
        return "manager table mismatch: Lua published " + hex(req.managers) + " but the comm service leads to " + hex(out.managers);
    std::string err;
    // DataController: no vtable of its own; +0x10 is the hub, +0 the db provider the queries run on (vtable slot 1 = Execute)
    err = manager_at(mem, out.managers, kTypeDataController, out.dc, "DataController", false);
    if (!err.empty()) return err;
    std::vector<uint8_t> whole;
    if (!is_ptr(out.dc, 8) || !mem.read_block(out.dc, kDcTail, whole)) return "the DataController at " + hex(out.dc) + " is not readable";
    if (mem.ptr(out.dc + kDcHub) != out.managers)
        return "the DataController's manager table (" + hex(mem.ptr(out.dc + kDcHub)) + ") is not the career's (" + hex(out.managers) + ")";
    {
        const uint64_t provider = mem.ptr(out.dc + kDcProvider), pvt = provider ? mem.ptr(provider) : 0;
        if (!provider || !pvt || !in_image(pvt, req)) return "the DataController at " + hex(out.dc) + " has no db provider with a vtable in FC27.exe";
        err = fn_slot(mem, pvt, 1, req, "the DataController's db provider");
        if (!err.empty()) return err;
    }
    // TeamUtil: a 0x10-byte object {+0 hub, +8 ?} without a vtable
    err = manager_at(mem, out.managers, kTypeTeamUtil, out.team_util, "TeamUtil", false);
    if (!err.empty()) return err;
    if (!is_ptr(out.team_util, 8) || !mem.read_block(out.team_util, kTeamUtilSize, whole)) return "the TeamUtil at " + hex(out.team_util) + " is not readable";
    if (mem.ptr(out.team_util + kTeamUtilHub) != out.managers)
        return "the TeamUtil's manager table (" + hex(mem.ptr(out.team_util + kTeamUtilHub)) + ") is not the career's (" + hex(out.managers) + ")";
    // PlayerContractManager (AddContractRecord / the record read-back) and TransferManager (SquadCounts reads its pre-signed list)
    err = manager_at(mem, out.managers, tl::kTypePlayerContractManager, out.pcm, "PlayerContractManager");
    if (!err.empty()) return err;
    err = check_object(mem, out.pcm, tl::kPcmSize, fns.pcm_vtable, "PlayerContractManager");
    if (!err.empty()) return err;
    err = same_managers(mem, out.pcm, out.managers, "PlayerContractManager");
    if (!err.empty()) return err;
    err = manager_at(mem, out.managers, tl::kTypeTransferManager, out.tm, "TransferManager");
    if (!err.empty()) return err;
    err = check_object(mem, out.tm, tl::kTmSize, fns.tm_vtable, "TransferManager");
    if (!err.empty()) return err;
    err = same_managers(mem, out.tm, out.managers, "TransferManager");
    if (!err.empty()) return err;
    {
        std::vector<uint8_t> pending;
        err = read_vec(mem, out.tm, kTmPendingBegin, kTmPendingEnd, kTmPendingRecord, kMaxPendingRecords, "the TransferManager's pre-signed deals (+0x2F30)", pending);
        if (!err.empty()) return err;
    }
    // the event dispatcher PlayerMoved posts through: its first word is the event sink, whose first word is a vtable in FC27.exe
    err = manager_at(mem, out.managers, tl::kTypeDispatcher, out.dispatcher, "event dispatcher");
    if (!err.empty()) return err;
    {
        const uint64_t sink = mem.ptr(out.dispatcher), sink_vt = sink ? mem.ptr(sink) : 0;
        if (!sink_vt || !in_image(sink_vt, req)) return "the event dispatcher at " + hex(out.dispatcher) + " has no event sink with a vtable in FC27.exe";
    }
    // the calendar: PlayerMoved stamps the join date from it and AddContractRecord takes its date storage
    err = manager_at(mem, out.managers, tl::kTypeCalendarManager, out.calendar, "CalendarManager");
    if (!err.empty()) return err;
    {
        const uint64_t cal_vt = mem.ptr(out.calendar);
        int32_t d[3] = {0, 0, 0};
        if (!cal_vt || !in_image(cal_vt, req) || !mem.rd(out.calendar + tl::kCalendarDate, d[0]) || !mem.rd(out.calendar + tl::kCalendarDate + 4, d[1]) ||
            !mem.rd(out.calendar + tl::kCalendarDate + 8, d[2]))
            return "the CalendarManager at " + hex(out.calendar) + " is not readable (no vtable in FC27.exe or no date)";
        if (d[0] < 1 || d[0] > 31 || d[1] < 1 || d[1] > 12 || d[2] < 1900 || d[2] > 2300)
            return "the CalendarManager's date (" + num(d[0]) + "/" + num(d[1]) + "/" + num(d[2]) + ") is not a date (layout mismatch?)";
        out.date = out.calendar + tl::kCalendarDate;
    }
    // the squad limits the game enforces after a move: IniSettings MAX_SQUAD_SIZE / MIN_SQUAD_SIZE
    err = manager_at(mem, out.managers, kTypeIni, out.ini, "IniSettingsManager", false);
    if (!err.empty()) return err;
    {
        int32_t mx = 0, mn = 0;
        if (!is_ptr(out.ini, 8) || !mem.read_block(out.ini, kIniSize, whole) || !mem.rd(out.ini + kIniMax, mx) || !mem.rd(out.ini + kIniMin, mn))
            return "the IniSettings at " + hex(out.ini) + " are not readable";
        if (mn < kMinSquadFloor || mx < mn || mx > kMaxSquadCeiling)
            return "the squad limits read from the ini (MIN_SQUAD_SIZE " + num(mn) + ", MAX_SQUAD_SIZE " + num(mx) + ") are not plausible (layout mismatch?)";
        out.min_squad = mn;
        out.max_squad = mx;
    }
    // the runtime loan list SquadCounts walks (and the loan rule reads)
    err = manager_at(mem, out.managers, kTypeLoans, out.loans, "LoansManager", false);
    if (!err.empty()) return err;
    {
        const uint64_t lvt = is_ptr(out.loans, 8) ? mem.ptr(out.loans) : 0;
        if (!lvt || !in_image(lvt, req)) return "the LoansManager at " + hex(out.loans) + " has no vtable in FC27.exe";
        bool loaned = false;
        int club = 0;
        err = loan_state(mem, out.loans, 0, loaned, club);
        if (!err.empty()) return err;
    }
    // the user: his club (UserManager, read exactly as transfer_list does)
    err = manager_at(mem, out.managers, tl::kTypeUserManager, out.um, "UserManager");
    if (!err.empty()) return err;
    err = tl::user_team(mem, out.um, fns.um_vtable, out.user_team);
    if (!err.empty()) return err;
    err = same_managers(mem, out.um, out.managers, "UserManager");
    if (!err.empty()) return err;
    // the morale gate: informational (a missing / odd morale manager does not stop a move), but a morale manager that is not the
    // class Turbo knows is refused (the slot does not hold what the research says)
    {
        uint64_t morale = 0;
        const std::string merr = manager_at(mem, out.managers, kTypeMorale, morale, "PlayerMoraleManager", false);
        if (!merr.empty()) {
            out.morale_note = "morale gate not read: " + merr;
        } else if (!fns.morale_vtable) {
            out.morale_note = "morale gate not read: signature morale_vtable was not found on this game build";
        } else {
            err = check_object(mem, morale, 0x20, fns.morale_vtable, "PlayerMoraleManager");
            if (!err.empty()) return err;
            if (fns.morale_handle_event) {
                // the vtable and the function identify each other: slot 1 of the PlayerMoraleManager vtable is its HandleEvent
                uint64_t slot1 = 0;
                if (!mem.rd(fns.morale_vtable + 8, slot1)) return "the PlayerMoraleManager's vtable " + hex(fns.morale_vtable) + " is not readable";
                if (slot1 != fns.morale_handle_event)
                    return "the PlayerMoraleManager's vtable slot 1 (" + hex(slot1) + ") is not the HandleEvent Turbo resolved (" + hex(fns.morale_handle_event) +
                           "): layout mismatch";
            }
            uint8_t gate = 0;
            if (!mem.rd(morale + kMoraleGate, gate)) {
                out.morale_note = "morale gate not read: the PlayerMoraleManager is cut off";
            } else if (gate > 1) {
                out.morale_note = "morale gate not read: the byte holds " + num(gate) + " (0 or 1 expected)";
            } else {
                out.morale = morale;
                out.morale_gate = gate;
            }
        }
    }
    // release: the objects ReleasePlayer touches besides the ones above
    if (release) {
        err = manager_at(mem, out.managers, kTypeCtm, out.ctm, "ContractTerminationManager", false);
        if (!err.empty()) return err;
        err = check_object(mem, out.ctm, kCtmSize, fns.ctm_vtable, "ContractTerminationManager");
        if (!err.empty()) return err;
        err = same_managers(mem, out.ctm, out.managers, "ContractTerminationManager");
        if (!err.empty()) return err;
        uint64_t counter = 0;
        err = manager_at(mem, out.managers, kTypeReleaseCounter, counter, "the release counter manager", false);
        if (!err.empty()) return err;
        uint8_t cb = 0;
        if (!is_ptr(counter, 8) || !mem.rd(counter + kCounterByte, cb)) return "the manager in slot 60 at " + hex(counter) + " is cut off (ReleasePlayer bumps a counter at +0xC5)";
        // the user's finance object: ReleasePlayer pays the compensation through [activeUser+0x2F0]'s vtable (slots 0 / 1 / 2)
        int32_t idx = -1;
        const uint64_t users = mem.ptr(out.um + tl::kUmUsers);
        if (!mem.rd(out.um + tl::kUmIndex, idx) || !users || idx < 0) return "the UserManager has no active user to pay the compensation";
        const uint64_t user = users + static_cast<uint64_t>(idx) * tl::kUserSize;
        const uint64_t fin = mem.ptr(user + kUserFinance), fvt = fin ? mem.ptr(fin) : 0;
        if (!fvt || !in_image(fvt, req)) return "the user's finance object (user+0x2F0) has no vtable in FC27.exe";
        for (int s = 0; s < 3; ++s) {
            err = fn_slot(mem, fvt, s, req, "the user's finance object");
            if (!err.empty()) return err;
        }
    }
    return "";
}

static Result fail(Result r, const char* stage, const std::string& msg) {
    r.ok = false;
    r.stage = stage;
    r.message = msg;
    return r;
}

// A club or the Free Agents team. "" = fine (is_club says which), else the reason.
std::string check_team(Caller& call, const Located& at, int team, const char* role, bool& is_club) {
    is_club = false;
    const std::string t = std::string("team ") + num(team) + " (" + role + ")";
    if (team == kTeamFreeAgents) return "";
    if (is_special_team(team))
        return t + " is a pseudo team or another free-agent pool, not a club (only Free Agents " + num(kTeamFreeAgents) + " is supported)";
    std::string err;
    int league = -1;
    if (!call.league_of_team(at.dc, team, league, err)) return "GetLeagueOfTeam: " + err;
    if (league == -1) return t + ": the game knows no league for it: not a club of this career";
    bool national = false;
    if (!call.is_international(league, national, err)) return "IsInternationalLeague: " + err;
    if (national) return t + " is a national team (league " + num(league) + "): Turbo moves players between clubs and Free Agents only";
    is_club = true;
    return "";
}

static std::string count_squad(Caller& call, uint64_t dc, int team, int& rows, int& loaned, int& pending) {
    rows = loaned = pending = -1;
    std::string err;
    if (!call.squad_counts(dc, team, rows, loaned, pending, err)) return "SquadCounts(team " + num(team) + "): " + err;
    if (rows < 0 || loaned < 0 || pending < 0) return "SquadCounts(team " + num(team) + ") left its outputs unset";
    return "";
}

static std::string squad_text(int team, int rows, int loaned, int pending) {
    return "team " + num(team) + " rows " + num(rows) + " + loaned out " + num(loaned) + " + pre-signed " + num(pending);
}

// The shared part of the move / check / AI-release path: teams, presence, loan rule and squad limits (nothing is called).
// from_club / to_club say whether each side is a club (not Free Agents); the counts go into r.
static std::string check_move(Memory& mem, Caller& call, const Request& req, Result& r, int from, int to, bool& from_club, bool& to_club, bool& in_from,
                              bool& in_to, bool& presence_read) {
    presence_read = false;
    const Located& at = r.at;
    const std::string who = "player " + num(req.player);
    std::string err = check_team(call, at, from, "from", from_club);
    if (!err.empty()) return err;
    err = check_team(call, at, to, "to", to_club);
    if (!err.empty()) return err;
    std::string e2;
    if (!call.in_team(at.dc, req.player, to, in_to, e2)) return "IsPlayerInTeam(" + who + ", team " + num(to) + "): " + e2;
    if (!call.in_team(at.dc, req.player, from, in_from, e2)) return "IsPlayerInTeam(" + who + ", team " + num(from) + "): " + e2;
    presence_read = true;
    if (in_to) return who + " is already in team " + num(to) + " (nothing was called)";
    if (!in_from) return who + " is not in team " + num(from) + " (the game would write his join date and previous team and move nobody): wrong `from`? (nothing was called)";
    bool loaned = false;
    int loan_club = 0;
    err = loan_state(mem, at.loans, req.player, loaned, loan_club);
    if (!err.empty()) return err;
    if (loaned)
        return who + " is on loan (the LoansManager's list has him, returning club " + num(loan_club) + "): end the loan first (nothing was called)";
    if (to_club) {
        err = count_squad(call, at.dc, to, r.rows_to, r.loaned_to, r.pending_to);
        if (!err.empty()) return err;
        const int total = r.rows_to + r.loaned_to + r.pending_to;
        if (total + 1 > at.max_squad)
            return "team " + num(to) + "'s squad is full: " + squad_text(to, r.rows_to, r.loaned_to, r.pending_to) + " = " + num(total) +
                   ", the limit is MAX_SQUAD_SIZE " + num(at.max_squad) + ": the game would release his lowest-value reserve (nothing was called)";
    }
    if (from_club) {
        int loaned_from = 0, pending_from = 0;
        err = count_squad(call, at.dc, from, r.rows_from, loaned_from, pending_from);
        if (!err.empty()) return err;
        if (r.rows_from - 1 < at.min_squad)
            return "team " + num(from) + " has only " + num(r.rows_from) + " players and the minimum is MIN_SQUAD_SIZE " + num(at.min_squad) +
                   ": the game would sign filler players (nothing was called)";
    }
    return "";
}

// The squad counts after a call: a warning when the game changed a squad beyond the move itself
static std::string squad_warning(Caller& call, const Located& at, int to, bool to_club, int rows_to_before, int from, bool from_club, int rows_from_before) {
    std::string w;
    int rows = 0, loaned = 0, pending = 0;
    if (to_club && rows_to_before >= 0 && count_squad(call, at.dc, to, rows, loaned, pending).empty() && rows != rows_to_before + 1)
        w += "; WARNING: team " + num(to) + " now has " + num(rows) + " players (expected " + num(rows_to_before + 1) + "): the game changed that squad too";
    if (from_club && rows_from_before >= 0 && count_squad(call, at.dc, from, rows, loaned, pending).empty() && rows != rows_from_before - 1)
        w += "; WARNING: team " + num(from) + " now has " + num(rows) + " players (expected " + num(rows_from_before - 1) + "): the game changed that squad too";
    return w;
}

static std::string morale_text(const Located& at, bool user_arrival) {
    if (!user_arrival) return "";
    if (at.morale_gate == 1) return "; WARNING: the morale gate is closed (job switch): the game creates no morale entry for him until the next season reset";
    if (at.morale_gate < 0) return "; (" + at.morale_note + ")";
    return "";
}

static std::string pool_name(int team) { return team == kTeamFreeAgents ? "Free Agents" : "team " + num(team); }

static Result run_move(Memory& mem, Caller& call, const Request& req, Result r) {
    const Located& at = r.at;
    const bool check = req.action == kActionCheck;
    const std::string who = "player " + num(req.player);
    bool from_club = false, to_club = false, in_from = false, in_to = false, presence_read = false;
    std::string err = check_move(mem, call, req, r, req.from, req.to, from_club, to_club, in_from, in_to, presence_read);
    // the read-back values of a check are the preconditions (he IS in from, he is NOT in to)
    if (check && presence_read) {
        r.from_ok = in_from ? 1 : 0;
        r.to_ok = in_to ? 0 : 1;
    }
    if (!err.empty()) return fail(r, "validate", err);
    const bool user_arrival = req.to == at.user_team;
    const bool want_record = user_arrival && req.months > 0;
    bool had_record = false;
    int32_t rec_status = 0;
    uint64_t node = 0;
    if (want_record) {
        err = tl::contract_status(mem, at.pcm, req.player, had_record, rec_status, &node);
        if (!err.empty()) return fail(r, "validate", err);
    }
    std::string note;
    if (!user_arrival && req.months > 0) note = "; months / wage ignored (contract records exist for the user's club only)";
    if (check) {
        r.ok = true;
        r.stage = "done";
        std::string sq;
        if (to_club) sq += "; " + squad_text(req.to, r.rows_to, r.loaned_to, r.pending_to) + " (limit " + num(at.max_squad) + ")";
        if (from_club) sq += "; team " + num(req.from) + " rows " + num(r.rows_from) + " (minimum " + num(at.min_squad) + ")";
        r.message = who + ": " + pool_name(req.from) + " -> " + pool_name(req.to) + " is allowed (checked only, nothing was called)" + sq +
                    (user_arrival ? std::string("; arrival at your club") + (want_record ? (had_record ? ", a contract record exists already (none would be added)"
                                                                                                     : ", a contract record would be added")
                                                                         : ", no contract record asked")
                                  : std::string()) +
                    morale_text(at, user_arrival) + note;
        return r;
    }
    // the call: the game's own PlayerMoved on the game's own objects
    if (!call.player_moved(at.team_util, req.player, req.from, req.to, err)) return fail(r, "call", "TeamUtil::PlayerMoved: " + err);
    r.called = true;
    bool now_in_to = false, now_in_from = true;
    std::string e1, e2;
    if (!call.in_team(at.dc, req.player, req.to, now_in_to, e1) || !call.in_team(at.dc, req.player, req.from, now_in_from, e2))
        return fail(r, "check", "PlayerMoved ran but the read-back failed: " + (e1.empty() ? e2 : e1));
    r.to_ok = now_in_to ? 1 : 0;
    r.from_ok = now_in_from ? 0 : 1;
    if (!now_in_to || now_in_from)
        return fail(r, "check", who + ": the game did not move him as asked (read back: in team " + num(req.to) + " " + (now_in_to ? "yes" : "no") + ", still in team " +
                                    num(req.from) + " " + (now_in_from ? "yes" : "no") + ")");
    std::string text = who + " moved from " + pool_name(req.from) + " to " + pool_name(req.to) + " (read back: left " + num(req.from) + ", in " + num(req.to) + ")";
    // the contract record of a user arrival (the game's own signing code creates it; PlayerMoved does not)
    if (want_record) {
        bool found_now = false;
        int32_t st = 0;
        uint64_t node_now = 0;
        err = tl::contract_status(mem, at.pcm, req.player, found_now, st, &node_now);
        if (!err.empty()) return fail(r, "check", text + "; the contract table could not be read: " + err);
        if (found_now) {
            int32_t rteam = 0, rwage = 0;
            mem.rd(node_now + kPcmNodeTeam, rteam);
            mem.rd(node_now + kPcmNodeWage, rwage);
            text += "; a contract record exists already (team " + num(rteam) + ", wage " + num(rwage) + "): none added" +
                    (rteam != req.to ? " (WARNING: it names another team)" : "");
        } else {
            if (!call.add_contract(at.pcm, req.player, req.to, req.months, req.wage, kContractByte, at.date, kContractStatus, err))
                return fail(r, "call", text + "; AddContractRecord: " + err);
            r.contract_called = true;
            err = tl::contract_status(mem, at.pcm, req.player, found_now, st, &node_now);
            if (!err.empty()) return fail(r, "check", text + "; AddContractRecord ran but the contract table could not be read: " + err);
            if (!found_now) return fail(r, "check", text + "; AddContractRecord ran but the PlayerContractManager has no record for him");
            int32_t rteam = 0, rwage = 0;
            mem.rd(node_now + kPcmNodeTeam, rteam);
            mem.rd(node_now + kPcmNodeWage, rwage);
            text += "; contract record added: " + num(req.months) + " months, wage " + num(req.wage);
            if (rteam != req.to || rwage != req.wage)
                text += " (WARNING: the record reads team " + num(rteam) + ", wage " + num(rwage) + ")";
        }
    }
    text += squad_warning(call, at, req.to, to_club, r.rows_to, req.from, from_club, r.rows_from);
    text += morale_text(at, user_arrival) + note;
    r.ok = true;
    r.stage = "done";
    r.message = text;
    return r;
}

static Result run_release(Memory& mem, Caller& call, const Request& req, Result r) {
    const Located& at = r.at;
    const std::string who = "player " + num(req.player);
    if (req.from == kTeamFreeAgents) return fail(r, "validate", who + " is a free agent already (nothing was called)");
    bool from_club = false, in_from = false, in_fa = false;
    std::string err = check_team(call, at, req.from, "his club", from_club);
    if (!err.empty()) return fail(r, "validate", err);
    std::string e2;
    if (!call.in_team(at.dc, req.player, kTeamFreeAgents, in_fa, e2)) return fail(r, "validate", "IsPlayerInTeam: " + e2);
    if (!call.in_team(at.dc, req.player, req.from, in_from, e2)) return fail(r, "validate", "IsPlayerInTeam: " + e2);
    if (in_fa) return fail(r, "validate", who + " is in the Free Agents team already (nothing was called)");
    if (!in_from) return fail(r, "validate", who + " is not in team " + num(req.from) + " (wrong club? nothing was called)");
    bool loaned = false;
    int loan_club = 0;
    err = loan_state(mem, at.loans, req.player, loaned, loan_club);
    if (!err.empty()) return fail(r, "validate", err);
    if (loaned) return fail(r, "validate", who + " is on loan (returning club " + num(loan_club) + "): end the loan first (nothing was called)");
    int loaned_from = 0, pending_from = 0;
    err = count_squad(call, at.dc, req.from, r.rows_from, loaned_from, pending_from);
    if (!err.empty()) return fail(r, "validate", err);
    if (r.rows_from - 1 < at.min_squad)
        return fail(r, "validate", "team " + num(req.from) + " has only " + num(r.rows_from) + " players and the minimum is MIN_SQUAD_SIZE " + num(at.min_squad) +
                                       ": the game would not release him (nothing was called)");
    const bool user_club = req.from == at.user_team;
    std::string text;
    if (user_club) {
        int code = -1;
        if (!call.release_player(at.ctm, req.player, code, err)) return fail(r, "call", "ContractTerminationManager::ReleasePlayer: " + err);
        r.called = true;
        r.release_code = code;
        if (code == 1)
            return fail(r, "call", who + " was not released: the game says your budget cannot pay the compensation (ReleasePlayer returned 1)");
        if (code == 2)
            return fail(r, "call", who + " was not released: the game says your squad would drop below the minimum (ReleasePlayer returned 2)");
        if (code != 0) return fail(r, "call", who + " was not released: ReleasePlayer returned " + num(code) + " (not a code Turbo knows)");
        text = who + " released by the game (compensation paid, ReleasePlayer returned 0)";
    } else {
        if (!call.player_moved(at.team_util, req.player, req.from, kTeamFreeAgents, err)) return fail(r, "call", "TeamUtil::PlayerMoved: " + err);
        r.called = true;
        text = who + " released from team " + num(req.from) + " to Free Agents (no compensation: not your club)";
    }
    bool still = true, in_pool = false;
    std::string e1;
    if (!call.in_team(at.dc, req.player, req.from, still, e1)) return fail(r, "check", text + "; the read-back failed: " + e1);
    r.from_ok = still ? 0 : 1;
    for (int pool : {kTeamFreeAgents, kTeamFreeAgentsB, kTeamFreeAgentsC}) {
        bool in_this = false;
        if (!call.in_team(at.dc, req.player, pool, in_this, e1)) return fail(r, "check", text + "; the read-back failed: " + e1);
        if (in_this) {
            in_pool = true;
            if (pool != kTeamFreeAgents) text += " (into free-agent pool " + hex(static_cast<uint64_t>(pool)) + ")";
            break;
        }
    }
    r.to_ok = in_pool ? 1 : 0;
    if (still || !in_pool)
        return fail(r, "check", who + ": the release did not do what was asked (read back: still in team " + num(req.from) + " " + (still ? "yes" : "no") +
                                    ", in a free-agent pool " + (in_pool ? "yes" : "no") + ")");
    if (user_club) {
        bool found = false;
        int32_t st = 0;
        if (tl::contract_status(mem, at.pcm, req.player, found, st).empty()) text += found ? "; his contract record is still there" : "; his contract record is gone";
    }
    text += squad_warning(call, at, 0, false, -1, req.from, from_club, r.rows_from);
    r.ok = true;
    r.stage = "done";
    r.message = text;
    return r;
}

Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req) {
    Result r;
    if (!req.bad_args.empty()) return fail(r, "validate", req.bad_args);
    if (!valid_action(req.action)) return fail(r, "validate", "unknown player_move code " + num(req.action) + " (1 move, 2 release, 9 check only)");
    const bool release = req.action == kActionRelease;
    if (req.player <= 0) return fail(r, "validate", "player id must be a positive number");
    if (req.player == kPlayerCareer)
        return fail(r, "validate", "player " + num(req.player) + " is the player-career player: the game special-cases him (jersey, contract), Turbo does not move him");
    if (req.from <= 0) return fail(r, "validate", "team id of `from` must be a positive number");
    if (!release) {
        if (req.to <= 0) return fail(r, "validate", "team id of `to` must be a positive number");
        if (req.from == req.to) return fail(r, "validate", "from and to are the same team (" + num(req.from) + ")");
    }
    if (req.months < 0 || req.months > kMaxMonths)
        return fail(r, "validate", "contract length " + num(req.months) + " months is out of range (0 to " + num(kMaxMonths) + ")");
    if (req.wage < 0 || req.wage > kMaxWage) return fail(r, "validate", "wage " + num(req.wage) + " is out of range (0 to " + num(kMaxWage) + ")");
    std::string err = locate(mem, req, fns, r.at);
    if (!err.empty()) return fail(r, "validate", err);
    err = svm::sim_busy(mem, r.at.managers);
    if (!err.empty()) return fail(r, "validate", err);
    if (release) return run_release(mem, call, req, r);
    return run_move(mem, call, req, r);
}

// ---------------------------------------------------------------- mailbox words and the kill switch
Request request_from_args(const int64_t args[4]) {
    Request q;
    q.comm = static_cast<uint64_t>(args[0]);
    const uint64_t mode = static_cast<uint64_t>(args[1]);
    if ((mode >> 16) != 0) q.bad_args = "player_move: the mode word has unknown bits set (code | months << 8 expected)";
    q.action = static_cast<int>(mode & 0xFF);
    q.months = static_cast<int>((mode >> 8) & 0xFF);
    const uint64_t w2 = static_cast<uint64_t>(args[2]), w3 = static_cast<uint64_t>(args[3]);
    q.player = static_cast<int32_t>(w2 & 0xFFFFFFFFu);
    q.wage = static_cast<int32_t>(w2 >> 32);
    q.from = static_cast<int32_t>(w3 & 0xFFFFFFFFu);
    q.to = static_cast<int32_t>(w3 >> 32);
    return q;
}

void args_from_request(const Request& req, int64_t args[4]) {
    args[0] = static_cast<int64_t>(req.comm);
    args[1] = static_cast<int64_t>(static_cast<uint64_t>(req.action & 0xFF) | (static_cast<uint64_t>(req.months & 0xFF) << 8));
    args[2] = static_cast<int64_t>(static_cast<uint64_t>(static_cast<uint32_t>(req.player)) | (static_cast<uint64_t>(static_cast<uint32_t>(req.wage)) << 32));
    args[3] = static_cast<int64_t>(static_cast<uint64_t>(static_cast<uint32_t>(req.from)) | (static_cast<uint64_t>(static_cast<uint32_t>(req.to)) << 32));
}

bool killed(const std::filesystem::path& turbo_output) {
    std::error_code ec;
    return std::filesystem::exists(turbo_output / kill_switch_name(), ec);
}

}  // namespace pm
}  // namespace turbo
