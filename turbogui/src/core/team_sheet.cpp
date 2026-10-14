// FC 27 LE Turbo GUI - tactics channel E (see team_sheet.h)
#include "team_sheet.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

#include "nlohmann/json.hpp"

namespace turbo {
namespace tsheet {

namespace fs = std::filesystem;

namespace {

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

std::string mismatch(const std::string& check) { return "layout mismatch (" + check + ")"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

template <class T> bool rd(Memory& mem, uint64_t a, T& v) { return mem.rd(a, v); }

int32_t i32_at(Memory& mem, uint64_t a, bool& ok) {
    int32_t v = 0;
    if (!mem.rd(a, v)) ok = false;
    return v;
}

// Counts the reads made on the game's memory (the job reports them with its time)
class Counting : public Memory {
public:
    explicit Counting(Memory& m) : m_(m) {}
    bool read(uint64_t addr, void* out, size_t n) override {
        ++reads;
        return m_.read(addr, out, n);
    }
    bool write(uint64_t addr, const void* in, size_t n) override { return m_.write(addr, in, n); }
    int reads = 0;

private:
    Memory& m_;
};

constexpr uint64_t kCachePage = 0x1000;
constexpr size_t kCachePages = 64;

}  // namespace

// ------------------------------------------------------------------------------------------------ page cache
const uint8_t* PageCache::page(uint64_t base) {
    for (const Page& p : pages_)
        if (p.base == base) return p.ok ? p.bytes.data() : nullptr;
    if (pages_.size() >= kCachePages) return nullptr;
    Page p;
    p.base = base;
    p.bytes.assign(kCachePage, 0);
    ++reads_;
    p.ok = is_ptr(base) && m_.read(base, p.bytes.data(), kCachePage);
    pages_.push_back(std::move(p));
    return pages_.back().ok ? pages_.back().bytes.data() : nullptr;
}

bool PageCache::read(uint64_t addr, void* out, size_t n) {
    if (n == 0) return true;
    uint8_t* o = static_cast<uint8_t*>(out);
    uint64_t a = addr;
    size_t left = n;
    while (left) {
        const uint64_t base = a & ~(kCachePage - 1);
        const size_t off = size_t(a - base), take = std::min<size_t>(left, size_t(kCachePage) - off);
        const uint8_t* p = n <= kCachePage * 2 ? page(base) : nullptr;
        if (!p) {
            // an unreadable page (or a long block): the asked bytes straight from the memory, which fails where it must
            ++reads_;
            return m_.read(addr, out, n);
        }
        std::memcpy(o, p + off, take);
        o += take;
        a += take;
        left -= take;
    }
    return true;
}

bool PageCache::write(uint64_t addr, const void* in, size_t n) {
    pages_.clear();
    return m_.write(addr, in, n);
}

// ------------------------------------------------------------------------------------------------ small helpers
int line_band(int line) {
    if (line <= 30) return 0;
    if (line <= 60) return 1;
    if (line <= 90) return 2;
    return 3;
}

std::string line_band_text(int line) {
    static const char* const kRange[4] = {"1-30", "31-60", "61-90", "91-100"};
    const int b = line_band(line);
    return "band " + std::to_string(b) + " (" + kRange[b] + ")";
}

// ------------------------------------------------------------------------------------------------ the game's words
// Sources (turbo_output\CHANNEL_E2_IMPL.txt section 2): the 2026-10-12 Edit Tactic save (BALANCED / DEEP -> COUNTER / BALANCED wrote
// +0xB0 0 -> 1 and +0xB4 0 -> 1, line height 25 -> 50: findings 1), the game image's IQ_BUILDUP_0..2 / IQ_DEFENSIVE_0..3 strings and the
// band code's minDepthThresholdFor...DefensiveStyle order (docs/plans/tactics-2.1/research.json), EA's FC IQ notes (the four approaches
// with their line height bands and defaults), Live Editor's label list (loc/eng_us/localize.json: balanced, counter, short_passing).
const std::vector<EnumLabel>& build_up_styles() {
    static const std::vector<EnumLabel> v = {
        {0, "Balanced", true, -1, false, "seen in Edit Tactic (2026-10-12: BALANCED = 0)", true},
        {1, "Counter", true, -1, false, "seen in Edit Tactic (2026-10-12: COUNTER = 1)", true},
    };
    return v;
}

const std::vector<EnumLabel>& defensive_approaches() {
    static const std::vector<EnumLabel> v = {
        {0, "Deep", true, 25, true, "seen in Edit Tactic (2026-10-12: DEEP = 0, line height 25 at career load)", true},
        {1, "Balanced", true, 50, true, "seen in Edit Tactic (2026-10-12: BALANCED = 1, line height 50)", true},
        {2, "High", false, 70, false,
         "the game's band order (code and IQ_DEFENSIVE_2) and EA's list; the other clubs' records hold 2 with lines 65-90 (2026-10-13); default 70 "
         "from EA's notes; not yet seen in Edit Tactic",
         true},
        {3, "Aggressive", false, 95, false,
         "the game's band order (code and IQ_DEFENSIVE_3) and EA's list; no record of any club holds 3: not written until its stored value is "
         "decoded (X3)",
         false},
    };
    return v;
}

static const EnumLabel* find_label(const std::vector<EnumLabel>& v, int value) {
    for (const EnumLabel& e : v)
        if (e.value == value) return &e;
    return nullptr;
}

const EnumLabel* build_up_style(int value) { return find_label(build_up_styles(), value); }
const EnumLabel* defensive_approach(int value) { return find_label(defensive_approaches(), value); }

bool build_up_writable(int value) {
    const EnumLabel* e = build_up_style(value);
    return e && e->writable;
}

bool approach_writable(int value) {
    const EnumLabel* e = defensive_approach(value);
    return e && e->writable;
}

int max_writable_line() {
    static const int kTop[4] = {30, 60, 90, 100};
    int top = 0;
    for (int b = 0; b < 4; ++b)
        if (approach_writable(b)) top = kTop[b];
        else break;  // the bands are contiguous from Deep up
    return top;
}

std::string build_up_text(int value) {
    const EnumLabel* e = build_up_style(value);
    return e ? std::string(e->label) + " (" + std::to_string(value) + ")" : "unknown value " + std::to_string(value);
}

std::string approach_text(int value) {
    const EnumLabel* e = defensive_approach(value);
    return e ? std::string(e->label) + " (" + std::to_string(value) + ")" : "unknown value " + std::to_string(value);
}

std::string Switches::blocking() const {
    if (master) return master_switch_file();
    if (off) return kill_switch_file();
    return "";
}

std::string Switches::db_row_block() const {
    const char* f = db_row_off ? db_row_off_file() : db_row_off_legacy ? db_row_off_legacy_file() : db_off ? db_off_file() : nullptr;
    if (f) return std::string("kill switch turbo_output\\") + f + " is present";
    return "";
}

Switches read_switches(const fs::path& out) {
    Switches s;
    s.opt_in = file_exists(out / opt_in_file());
    s.off = file_exists(out / kill_switch_file());
    s.master = file_exists(out / master_switch_file());
    s.find_off = file_exists(out / find_off_file());
    s.db_row_off = file_exists(out / db_row_off_file());
    s.db_row_off_legacy = file_exists(out / db_row_off_legacy_file());
    s.db_off = file_exists(out / db_off_file());
    return s;
}

// ------------------------------------------------------------------------------------------------ anchors
const std::vector<const char*>& anchor_signatures() {
    static const std::vector<const char*> names = {"teamsheet_vt1",         "teamsheet_vt2",        "teamsheet_set",       "teamsheet_set_body",
                                                   "teamsheet_update",      "teamsheet_update_body", "teamsheet_active",    "teamsheet_active_body",
                                                   "service_registry",      "service_registry_this"};
    return names;
}

const char* Anchors::missing() const {
    const uint64_t v[] = {vt1, vt2, set, set_body, update, update_body, active, active_body, registry, registry_this};
    const auto& names = anchor_signatures();
    for (size_t i = 0; i < names.size(); ++i)
        if (!v[i]) return names[i];
    return nullptr;
}

bool in_image(const Anchors& a, uint64_t addr) {
    if (a.image_size == 0) return is_ptr(addr);
    return addr >= a.image_base && addr < a.image_base + a.image_size;
}

std::string check_anchors(Memory& mem, const Anchors& a) {
    if (a.build != kBuild)
        return "game build " + (a.build.empty() ? std::string("unknown") : a.build) + " is not " + kBuild +
               " (the only build channel E's layout was verified on)";
    if (const char* m = a.missing()) return std::string("signature ") + m + " was not found on this game build";
    if (a.set_body != a.set) return "teamsheet_set_body is not at SetSheet (" + hex(a.set_body) + " vs " + hex(a.set) + ")";
    if (a.update_body != a.update) return "teamsheet_update_body is not at UpdateSheet (" + hex(a.update_body) + " vs " + hex(a.update) + ")";
    if (a.active_body != a.active) return "teamsheet_active_body is not at GetActiveSheetId (" + hex(a.active_body) + " vs " + hex(a.active) + ")";
    if (a.registry != a.registry_this) return "the registry's two operands differ (" + hex(a.registry) + " vs " + hex(a.registry_this) + ")";
    for (uint64_t p : {a.vt1, a.vt2, a.set, a.update, a.active, a.registry})
        if (!in_image(a, p)) return "anchor " + hex(p) + " is outside FC27.exe";
    struct Slot {
        int index;
        uint64_t want;
        const char* name;
    };
    for (const Slot& s : {Slot{kVtSet, a.set, "SetSheet"}, Slot{kVtUpdate, a.update, "UpdateSheet"}, Slot{kVtActive, a.active, "GetActiveSheetId"}}) {
        uint64_t v = 0;
        if (!mem.rd(a.vt1 + 8 * uint64_t(s.index), v)) return "VT1 slot " + std::to_string(s.index) + " is not readable";
        if (v != s.want) return "VT1 slot " + std::to_string(s.index) + " is " + hex(v) + ", not " + s.name + " " + hex(s.want);
    }
    return "";
}

uint64_t registry_find_fn(Memory& mem, const Anchors& a, std::string& why) {
    uint64_t vt = 0, fn = 0;
    if (!a.registry || !mem.rd(a.registry, vt) || !in_image(a, vt)) {
        why = "the service registry's vtable is not inside FC27.exe";
        return 0;
    }
    if (!mem.rd(vt + kRegistryFindSlot, fn) || !in_image(a, fn)) {
        why = "the registry's Find (vtable +0x38) is not inside FC27.exe";
        return 0;
    }
    return fn;
}

// ------------------------------------------------------------------------------------------------ L1
std::string check_service(Memory& mem, uint64_t svc, const Anchors& a, ServiceHeader* out) {
    if (!is_ptr(svc, 8)) return mismatch("service " + hex(svc) + " is not a pointer");
    uint64_t v1 = 0, v2 = 0;
    if (!mem.rd(svc, v1) || !mem.rd(svc + 8, v2)) return mismatch("service " + hex(svc) + " is not readable");
    if (v1 != a.vt1 || v2 != a.vt2) return mismatch("service " + hex(svc) + ": vtables " + hex(v1) + " / " + hex(v2));
    ServiceHeader h;
    if (!mem.rd(svc + kSvcLoaded, h.loaded) || !mem.rd(svc + kSvcBucketCount, h.bucket_count) || !mem.rd(svc + kSvcNodeCount, h.node_count) ||
        !mem.rd(svc + kSvcBuckets, h.buckets) || !mem.rd(svc + kSvcLastTeam, h.last_team) || !mem.rd(svc + kSvcActiveSheet, h.active_sheet))
        return mismatch("service header not readable");
    if (h.loaded != 1) return mismatch("service loaded flag +0x1404 is " + std::to_string(h.loaded) + ", not 1");
    if (h.bucket_count < 1 || h.bucket_count > kMaxBucketCount) return mismatch("bucket count " + std::to_string(h.bucket_count));
    if (h.node_count < 1 || uint64_t(h.node_count) > 4ull * h.bucket_count) return mismatch("node count " + std::to_string(h.node_count));
    if (!is_ptr(h.buckets, 8)) return mismatch("bucket array " + hex(h.buckets));
    if (out) *out = h;
    return "";
}

std::vector<uint64_t> find_vtable_pairs(const uint8_t* buf, size_t len, uint64_t base, uint64_t vt1, uint64_t vt2, size_t max_hits) {
    std::vector<uint64_t> hits;
    if (!buf || len < 16 || max_hits == 0) return hits;
    size_t i = static_cast<size_t>((8 - (base % 8)) % 8);
    for (; i + 16 <= len; i += 8) {
        uint64_t a = 0, b = 0;
        std::memcpy(&a, buf + i, 8);
        if (a != vt1) continue;
        std::memcpy(&b, buf + i + 8, 8);
        if (b != vt2) continue;
        hits.push_back(base + i);
        if (hits.size() >= max_hits) break;
    }
    return hits;
}

std::string pick_service(Memory& mem, const std::vector<uint64_t>& hits, const Anchors& a, uint64_t& svc) {
    svc = 0;
    std::vector<uint64_t> live;
    std::string first_bad;
    for (uint64_t h : hits) {
        std::string why = check_service(mem, h, a);
        if (why.empty()) live.push_back(h);
        else if (first_bad.empty()) first_bad = why;
    }
    if (live.size() == 1) {
        svc = live[0];
        return "";
    }
    if (live.empty())
        return hits.empty() ? "no object with the service's two vtables in memory (is a career loaded?)"
                            : std::to_string(hits.size()) + " vtable-pair hit(s), none a live service (" + first_bad + ")";
    return std::to_string(live.size()) + " objects pass every service check (" + hex(live[0]) + ", " + hex(live[1]) + "): refused, not guessed";
}

std::string accept_find_result(Memory& mem, uint64_t r, const Anchors& a, uint64_t& svc) {
    svc = 0;
    if (!is_ptr(r, 8)) return "the registry returned " + hex(r);
    uint64_t q0 = 0, qm8 = 0;
    mem.rd(r, q0);
    uint64_t cand = 0;
    if (q0 == a.vt1) cand = r;
    else if (q0 == a.vt2 && mem.rd(r - 8, qm8) && qm8 == a.vt1) cand = r - 8;
    if (!cand) return "the registry returned " + hex(r) + ", not the team sheet service";
    std::string why = check_service(mem, cand, a);
    if (!why.empty()) return why;
    svc = cand;
    return "";
}

std::string user_from_psm(Memory& mem, uint64_t managers, uint64_t psm_vtable, int& user) {
    user = 0;
    if (!is_ptr(managers, 8)) return "the career's manager table is not known (bridge_state.json \"managers\")";
    if (!psm_vtable) return "signature psm_vtable was not found on this game build";
    const uint64_t slot = managers + 0x20 * 87;  // PlayerStatusManager, manager type 87 (report 2.4: hub +0xAF8)
    int32_t count = 0;
    if (!mem.rd(slot + 0x10, count) || count != 1) return "manager slot 87 holds " + std::to_string(count) + " instances (no career loaded?)";
    const uint64_t psm = mem.chain(slot, {0x18, 0});
    uint64_t vt = 0, hub = 0;
    if (!psm || !mem.rd(psm, vt) || vt != psm_vtable) return "the PlayerStatusManager " + hex(psm) + " has vtable " + hex(vt);
    if (!mem.rd(psm + 0x08, hub) || hub != managers)
        return "the PlayerStatusManager's manager table " + hex(hub) + " is not the career's " + hex(managers);
    int32_t team = 0;
    if (!mem.rd(psm + 0x10, team) || team <= 0) return "the PlayerStatusManager's club is " + std::to_string(team);
    user = team;
    return "";
}

// ------------------------------------------------------------------------------------------------ L2 - L4
std::string locate(Memory& mem, uint64_t svc, int user, const Anchors& a, Located& out) {
    out = Located();
    out.svc = svc;
    out.user = user;
    std::string why = check_service(mem, svc, a, &out.header);
    if (!why.empty()) return why;
    if (user <= 0) return mismatch("user club " + std::to_string(user));
    // L2: the node of the user's club
    const uint64_t bucket = out.header.buckets + 8 * (uint64_t(uint32_t(user)) % out.header.bucket_count);
    uint64_t node = 0;
    if (!mem.rd(bucket, node)) return mismatch("bucket " + hex(bucket) + " not readable");
    int hops = 0;
    while (node) {
        if (!is_ptr(node, 8)) return mismatch("node " + hex(node));
        if (++hops > kMaxHops) return mismatch("more than " + std::to_string(kMaxHops) + " nodes in one bucket");
        int32_t key = 0;
        if (!mem.rd(node + kNodeKey, key)) return mismatch("node " + hex(node) + " not readable");
        if (key == user) break;
        uint64_t next = 0;
        if (!mem.rd(node + kNodeNext, next)) return mismatch("node chain not readable");
        node = next;
    }
    if (!node) return mismatch("club " + std::to_string(user) + " has no sheet in the service");
    out.node = node;
    out.hops = hops;
    if (!mem.rd(node + kNodeBegin, out.vec_begin) || !mem.rd(node + kNodeEnd, out.vec_end)) return mismatch("sheet vector not readable");
    if (!is_ptr(out.vec_begin, 8) || out.vec_end < out.vec_begin || (out.vec_end - out.vec_begin) % kSheetStride != 0)
        return mismatch("sheet vector " + hex(out.vec_begin) + ".." + hex(out.vec_end));
    out.sheets = int((out.vec_end - out.vec_begin) / kSheetStride);
    if (out.sheets < 1 || out.sheets > kMaxSheets) return mismatch(std::to_string(out.sheets) + " sheets");
    // L3: exactly one current sheet of the club with 11 slots and a dirty flag 0 / 1
    int found = 0;
    for (int i = 0; i < out.sheets; ++i) {
        const uint64_t e = out.vec_begin + kSheetStride * uint64_t(i);
        int32_t team = 0, slots = 0;
        uint8_t cur = 0, dirty = 0;
        if (!mem.rd(e + kSheetTeam, team) || !mem.rd(e + kSheetCurrent, cur) || !mem.rd(e + kSheetSlotCount, slots) || !mem.rd(e + kSheetDirty, dirty))
            return mismatch("sheet " + std::to_string(i) + " not readable");
        if (team == user && cur != 0 && slots == kSlots && dirty <= 1) {
            ++found;
            out.sheet = e;
        }
    }
    if (found != 1) return mismatch(std::to_string(found) + " current sheets of club " + std::to_string(user) + " (exactly 1 needed)");
    // L4: the records, exactly one active, its 11 slots
    const uint64_t e = out.sheet;
    uint64_t rb = 0, re = 0;
    if (!mem.rd(e + kSheetRecBegin, rb) || !mem.rd(e + kSheetRecEnd, re)) return mismatch("record vector not readable");
    if (!is_ptr(rb, 8) || re < rb || (re - rb) % kRecStride != 0) return mismatch("record vector " + hex(rb) + ".." + hex(re));
    out.records = int((re - rb) / kRecStride);
    out.rec_begin = rb;
    if (out.records < 1 || out.records > kMaxRecords) return mismatch(std::to_string(out.records) + " tactic records");
    int active = 0;
    for (int i = 0; i < out.records; ++i) {
        const uint64_t r = rb + kRecStride * uint64_t(i);
        uint8_t act = 0;
        int32_t src = 0;
        if (!mem.rd(r + kRecActive, act) || !mem.rd(r + kRecSourceFormation, src)) return mismatch("record " + std::to_string(i) + " not readable");
        if (act == 1 && src > 0) {
            ++active;
            out.record = r;
            out.record_index = i;
        }
    }
    if (active != 1) return mismatch(std::to_string(active) + " active tactic records (exactly 1 needed)");
    const uint64_t r = out.record;
    int32_t src1 = 0, src2 = 0;
    if (!mem.rd(r + kRecSourceFormation, src1) || !mem.rd(r + kRecSourceFormation2, src2) || src1 != src2)
        return mismatch("sourceformationid +0xC4 " + std::to_string(src1) + " vs +0xD0 " + std::to_string(src2));
    uint64_t sb = 0, se = 0;
    if (!mem.rd(r + kRecSlotBegin, sb) || !mem.rd(r + kRecSlotEnd, se)) return mismatch("slot vector not readable");
    if (!is_ptr(sb, 8) || se < sb || se - sb != kSlotStride * kSlots) return mismatch("slot vector " + hex(sb) + ".." + hex(se) + " (11 x 0x48 needed)");
    out.slots = sb;
    std::set<int32_t> squad;
    for (int k = 0; k < kSheetPidCount; ++k) {
        int32_t pid = -1;
        if (!mem.rd(e + kSheetPids + 4 * uint64_t(k), pid)) return mismatch("sheet player list not readable");
        if (pid != -1) squad.insert(pid);
    }
    for (int s = 0; s < kSlots; ++s) {
        const uint64_t sl = sb + kSlotStride * uint64_t(s);
        int32_t pid = 0, role = 0, rid = 0, foc = 0;
        if (!mem.rd(sl + kSlotPid, pid) || !mem.rd(sl + kSlotRole, role) || !mem.rd(sl + kSlotRoleId, rid) || !mem.rd(sl + kSlotFocus, foc))
            return mismatch("slot " + std::to_string(s) + " not readable");
        if (!squad.count(pid)) return mismatch("slot " + std::to_string(s) + " player " + std::to_string(pid) + " is not in the sheet's player list");
        if (role_id_of(role) != rid || focus_of(role) != foc)
            return mismatch("slot " + std::to_string(s) + " role word " + std::to_string(role) + " vs role id " + std::to_string(rid) + " / focus " +
                            std::to_string(foc));
    }
    return "";
}

// ------------------------------------------------------------------------------------------------ read-out
static std::string read_name(Memory& mem, uint64_t r) {
    uint64_t p = 0;
    uint32_t n = 0;
    if (!mem.rd(r + kRecName, p) || !mem.rd(r + kRecNameSize, n) || n == 0 || n > 0x50 || !is_ptr(p)) return "";
    std::vector<uint8_t> b;
    if (!mem.read_block(p, n, b)) return "";
    std::string s;
    for (uint8_t c : b) s += (c >= 0x20 && c < 0x7F) ? char(c) : '?';
    return s;
}

std::string read_code(Memory& mem, uint64_t rec, std::string* why) {
    uint8_t b[kCodeLen + 1] = {};
    if (!mem.read(rec + kRecCode, b, sizeof(b))) {
        if (why) *why = "its tactic code is not readable";
        return "";
    }
    if (b[0] == 0) {
        if (why) *why = "no tactic code (an empty template)";
        return "";
    }
    for (int i = 0; i < kCodeLen; ++i)
        if (b[i] < 0x21 || b[i] > 0x7E) {
            if (why) *why = "its tactic code is not 12 printable characters";
            return "";
        }
    if (b[kCodeLen] != 0) {
        if (why) *why = "its tactic code is longer than 12 characters";
        return "";
    }
    return std::string(reinterpret_cast<const char*>(b), kCodeLen);
}

std::string WorkCopy::label() const { return "svc+" + hex(offset); }

const WorkCopy* LiveTactic::copy(uint64_t offset) const {
    for (const WorkCopy& c : copies)
        if (c.offset == offset) return &c;
    return nullptr;
}

int LiveTactic::matching_copies() const {
    int n = 0;
    for (const WorkCopy& c : copies) n += c.matches ? 1 : 0;
    return n;
}

// One inline record: a tactic record with a sane header (findings 2: code at +0x68, active flag, the map record's formation ids, 11 slots
// whose role words agree with their mirrors). Whether it is THIS tactic is the code (WorkCopy::matches)
static WorkCopy read_work_copy(Memory& mem, uint64_t svc, uint64_t offset, const LiveTactic& map) {
    WorkCopy c;
    c.offset = offset;
    c.addr = svc + offset;
    const uint64_t r = c.addr;
    auto bad = [&](const std::string& w) {
        c.why = w;
        return c;
    };
    uint64_t name = 0;
    uint32_t name_size = 0;
    if (!mem.rd(r + kRecName, name) || !mem.rd(r + kRecNameSize, name_size)) return bad("not readable");
    if (!(name == r + 0x10 || is_ptr(name, 1)) || name_size > 0x50) return bad("no tactic record header (name " + hex(name) + ")");
    std::string why;
    c.code = read_code(mem, r, &why);
    if (c.code.empty()) return bad(why);
    uint8_t act = 0;
    int32_t ffn = 0, sf = 0, aud = 0, sf2 = 0;
    bool ok = true;
    if (!mem.rd(r + kRecActive, act)) ok = false;
    ffn = i32_at(mem, r + kRecFormation, ok);
    sf = i32_at(mem, r + kRecSourceFormation, ok);
    aud = i32_at(mem, r + kRecAudio, ok);
    sf2 = i32_at(mem, r + kRecSourceFormation2, ok);
    c.line_height = i32_at(mem, r + kRecLineHeight, ok);
    c.build_up = i32_at(mem, r + kRecBuildUp, ok);
    c.approach = i32_at(mem, r + kRecApproach, ok);
    if (!ok) return bad("its fields are not readable");
    if (act != 1) return bad("not marked active (" + std::to_string(act) + ")");
    if (ffn != map.formation || sf != map.source_formation || sf2 != map.source_formation || aud != map.audio)
        return bad("another formation (" + std::to_string(ffn) + " / " + std::to_string(sf) + " / " + std::to_string(sf2) + " / " + std::to_string(aud) +
                   ", the map record's " + std::to_string(map.formation) + " / " + std::to_string(map.source_formation) + " / " + std::to_string(map.audio) + ")");
    uint64_t sb = 0, se = 0;
    if (!mem.rd(r + kRecSlotBegin, sb) || !mem.rd(r + kRecSlotEnd, se)) return bad("its slot vector is not readable");
    if (!is_ptr(sb, 8) || se < sb || se - sb != kSlotStride * kSlots) return bad("its slot vector " + hex(sb) + ".." + hex(se) + " is not 11 slots");
    c.slots = sb;
    for (int s = 0; s < kSlots; ++s) {
        const uint64_t sl = sb + kSlotStride * uint64_t(s);
        int32_t pid = 0, w = 0, rid = 0, foc = 0;
        if (!mem.rd(sl + kSlotPid, pid) || !mem.rd(sl + kSlotRole, w) || !mem.rd(sl + kSlotRoleId, rid) || !mem.rd(sl + kSlotFocus, foc))
            return bad("slot " + std::to_string(s) + " is not readable");
        if (pid <= 0) return bad("slot " + std::to_string(s) + " holds no player");
        if (role_id_of(w) != rid || focus_of(w) != foc)
            return bad("slot " + std::to_string(s) + " role word " + std::to_string(w) + " vs role id " + std::to_string(rid) + " / focus " + std::to_string(foc));
        c.pids[size_t(s)] = pid;
        c.roles[size_t(s)] = w;
        c.role_ids[size_t(s)] = rid;
        c.focuses[size_t(s)] = foc;
    }
    c.matches = !map.code.empty() && c.code == map.code;
    return c;
}

std::vector<WorkCopy> read_work_copies(Memory& mem, uint64_t svc, const LiveTactic& map) {
    std::vector<WorkCopy> out;
    for (uint64_t off : kWorkCopyOffsets) out.push_back(read_work_copy(mem, svc, off, map));
    return out;
}

std::string read_live(Memory& mem, uint64_t svc, int user, const Anchors& a, LiveTactic& out) {
    out = LiveTactic();
    std::string why = locate(mem, svc, user, a, out.at);
    if (!why.empty()) return why;
    const uint64_t e = out.at.sheet, r = out.at.record;
    bool ok = true;
    out.sheet_id = i32_at(mem, e + kSheetId, ok);
    out.mode = i32_at(mem, e + kSheetMode, ok);
    if (!mem.rd(e + kSheetDirty, out.dirty)) ok = false;
    out.preset = i32_at(mem, r + kRecPreset, ok);
    out.line_height = i32_at(mem, r + kRecLineHeight, ok);
    out.build_up = i32_at(mem, r + kRecBuildUp, ok);
    out.approach = i32_at(mem, r + kRecApproach, ok);
    out.formation = i32_at(mem, r + kRecFormation, ok);
    out.source_formation = i32_at(mem, r + kRecSourceFormation, ok);
    out.audio = i32_at(mem, r + kRecAudio, ok);
    for (int s = 0; s < kSlots; ++s) {
        const uint64_t sl = out.at.slots + kSlotStride * uint64_t(s);
        SlotView& v = out.slots[size_t(s)];
        v.pid = i32_at(mem, sl + kSlotPid, ok);
        v.position = i32_at(mem, sl + kSlotPosition, ok);
        v.role = i32_at(mem, sl + kSlotRole, ok);
        v.role_id = i32_at(mem, sl + kSlotRoleId, ok);
        v.focus = i32_at(mem, sl + kSlotFocus, ok);
        if (!mem.rd(sl + kSlotX, v.x) || !mem.rd(sl + kSlotY, v.y)) ok = false;
    }
    for (int k = 0; k < kSheetPidCount; ++k) {
        int32_t pid = -1;
        if (!mem.rd(e + kSheetPids + 4 * uint64_t(k), pid)) ok = false;
        if (pid != -1) out.squad.push_back(pid);
    }
    if (!ok) return mismatch("record fields not readable");
    out.name = read_name(mem, r);
    out.code = read_code(mem, r);
    out.copies = read_work_copies(mem, svc, out);
    return "";
}

// ------------------------------------------------------------------------------------------------ G5
bool active_sheet_id(Memory& mem, const Located& at, int32_t mode, int32_t& id) {
    id = -1;
    for (int i = 0; i < at.sheets; ++i) {
        const uint64_t e = at.vec_begin + kSheetStride * uint64_t(i);
        int32_t m = 0, sid = 0;
        uint8_t cur = 0;
        if (!mem.rd(e + kSheetMode, m) || !mem.rd(e + kSheetCurrent, cur) || !mem.rd(e + kSheetId, sid)) return false;
        if (m == mode && cur != 0) {
            id = sid;
            return true;
        }
    }
    return true;
}

std::string check_g5(Memory& mem, const LiveTactic& lt) {
    if (lt.mode == -1) return "";
    // 2.4 (decision WP-B1: "mode > -1 refused"): only your own sheet (mode -1, report 2.7) is written; a sheet with a mode has a save and
    // propagation path nobody traced, whether or not it is the active one of its mode
    int32_t active = -1;
    if (!active_sheet_id(mem, lt.at, lt.mode, active))
        return "GetActiveSheetId's loop could not read the club's sheets (mode " + std::to_string(lt.mode) + "): refused";
    if (lt.sheet_id != active)
        return "sheet mode " + std::to_string(lt.mode) + ": only mode -1 (your club's own sheet) is written (decision WP-B1: mode > -1 refused)";
    return "SetSheet would run its propagation step (vf 0xF8) for this sheet (mode " + std::to_string(lt.mode) + ", sheet " + std::to_string(lt.sheet_id) +
           " is the active one): refused until vf 0xF8 is traced";
}

// ------------------------------------------------------------------------------------------------ plans
const char* field_name(FieldKind k) {
    switch (k) {
        case FieldKind::LineHeight: return "lineheight";
        case FieldKind::BuildUp: return "buildupstyle";
        case FieldKind::Approach: return "defensiveapproach";
        case FieldKind::Role: return "role";
        case FieldKind::RoleId: return "roleid";
        case FieldKind::Focus: return "focus";
    }
    return "?";
}

const char* field_label(FieldKind k) {
    switch (k) {
        case FieldKind::LineHeight: return "Line Height";
        case FieldKind::BuildUp: return "Build-Up Style";
        case FieldKind::Approach: return "Defensive Approach";
        case FieldKind::Role: return "role word";
        case FieldKind::RoleId: return "role id";
        case FieldKind::Focus: return "focus";
    }
    return "?";
}

bool field_from_name(const std::string& s, FieldKind& k) {
    for (FieldKind f : {FieldKind::LineHeight, FieldKind::BuildUp, FieldKind::Approach, FieldKind::Role, FieldKind::RoleId, FieldKind::Focus})
        if (s == field_name(f)) {
            k = f;
            return true;
        }
    // the names of a 2.2 undo file
    if (s == "defensivedepth" || s == "buildupplay") {
        k = s == "defensivedepth" ? FieldKind::LineHeight : FieldKind::BuildUp;
        return true;
    }
    return false;
}

static uint64_t record_field_offset(FieldKind k) {
    return k == FieldKind::LineHeight ? kRecLineHeight : k == FieldKind::BuildUp ? kRecBuildUp : k == FieldKind::Approach ? kRecApproach : 0;
}

static uint64_t slot_field_offset(FieldKind k) { return k == FieldKind::Role ? kSlotRole : k == FieldKind::RoleId ? kSlotRoleId : kSlotFocus; }

uint64_t field_address(const LiveTactic& now, const FieldWrite& w) {
    uint64_t record = now.at.record, slots = now.at.slots;
    if (w.copy) {
        const WorkCopy* c = now.copy(w.copy);
        if (!c || !c->matches) return 0;
        record = c->addr;
        slots = c->slots;
    }
    if (record_field(w.kind)) return w.slot == -1 && record ? record + record_field_offset(w.kind) : 0;
    if (w.slot < 0 || w.slot >= kSlots || !slots) return 0;
    return slots + kSlotStride * uint64_t(w.slot) + slot_field_offset(w.kind);
}

static void fill_identity(const LiveTactic& now, Plan& out) {
    out.user = now.at.user;
    out.record_index = now.at.record_index;
    out.sheet_id = now.sheet_id;
    out.formation = now.formation;
    out.source_formation = now.source_formation;
    out.record_name = now.name;
    out.code = now.code;
    for (int s = 0; s < kSlots; ++s) out.pids[size_t(s)] = now.slots[size_t(s)].pid;
}

// The live value of a field: the map record's (copy 0) or a working copy's; INT32_MIN when there is none
static int32_t value_in(const LiveTactic& now, FieldKind k, int slot, uint64_t copy) {
    if (copy) {
        const WorkCopy* c = now.copy(copy);
        if (!c) return INT32_MIN;
        switch (k) {
            case FieldKind::LineHeight: return c->line_height;
            case FieldKind::BuildUp: return c->build_up;
            case FieldKind::Approach: return c->approach;
            case FieldKind::Role: return slot >= 0 && slot < kSlots ? c->roles[size_t(slot)] : INT32_MIN;
            case FieldKind::RoleId: return slot >= 0 && slot < kSlots ? c->role_ids[size_t(slot)] : INT32_MIN;
            case FieldKind::Focus: return slot >= 0 && slot < kSlots ? c->focuses[size_t(slot)] : INT32_MIN;
        }
        return INT32_MIN;
    }
    switch (k) {
        case FieldKind::LineHeight: return now.line_height;
        case FieldKind::BuildUp: return now.build_up;
        case FieldKind::Approach: return now.approach;
        case FieldKind::Role: return slot >= 0 && slot < kSlots ? now.slots[size_t(slot)].role : INT32_MIN;
        case FieldKind::RoleId: return slot >= 0 && slot < kSlots ? now.slots[size_t(slot)].role_id : INT32_MIN;
        case FieldKind::Focus: return slot >= 0 && slot < kSlots ? now.slots[size_t(slot)].focus : INT32_MIN;
    }
    return INT32_MIN;
}

// ------------------------------------------------------------------------------------------------ the role allow-list
std::vector<int> RoleAllowList::role_ids(int group) const {
    std::set<int> r;
    for (int32_t w : words)
        if (role_group(w) == group) r.insert(role_id_of(w));
    return std::vector<int>(r.begin(), r.end());
}

std::vector<int> RoleAllowList::focuses(int group, int role_id) const {
    std::set<int> r;
    for (int32_t w : words)
        if (role_group(w) == group && role_id_of(w) == role_id) r.insert(focus_of(w));
    return std::vector<int>(r.begin(), r.end());
}

std::string RoleAllowList::summary() const { return std::to_string(words.size()) + " triples from " + std::to_string(teams) + " teams"; }

std::string RoleAllowList::values_summary() const {
    std::string a, b;
    for (const auto& kv : approach_count) {
        auto it = approach_lines.find(kv.first);
        a += (a.empty() ? "" : ", ") + std::to_string(kv.first) + " x" + std::to_string(kv.second) +
             (it != approach_lines.end() ? " (line " + std::to_string(it->second.first) + "-" + std::to_string(it->second.second) + ")" : std::string());
    }
    for (const auto& kv : build_up_count) b += (b.empty() ? "" : ", ") + std::to_string(kv.first) + " x" + std::to_string(kv.second);
    return "approach " + (a.empty() ? std::string("none") : a) + "; build-up " + (b.empty() ? std::string("none") : b);
}

// One club's sheets (its own page cache): false when they do not look like sheets (the club is skipped)
static bool allow_team(Memory& mem, int32_t key, uint64_t begin, uint64_t end, RoleAllowList& out) {
    if (begin == 0 && end == 0) return true;  // a club without a sheet
    if (!is_ptr(begin, 8) || end < begin || (end - begin) % kSheetStride != 0) return false;
    const int n = int((end - begin) / kSheetStride);
    if (n < 1 || n > kMaxSheets) return false;
    std::set<int32_t> words;
    struct Vals {
        int32_t line, build_up, approach;
    };
    std::vector<Vals> vals;
    int sheets = 0, records = 0, slots = 0;
    for (int i = 0; i < n; ++i) {
        const uint64_t e = begin + kSheetStride * uint64_t(i);
        int32_t team = 0, count = 0;
        uint64_t rb = 0, re = 0;
        if (!mem.rd(e + kSheetTeam, team) || !mem.rd(e + kSheetSlotCount, count) || !mem.rd(e + kSheetRecBegin, rb) || !mem.rd(e + kSheetRecEnd, re)) return false;
        if (team != key || count != kSlots || !is_ptr(rb, 8) || re < rb || (re - rb) % kRecStride != 0) return false;
        const int nr = int((re - rb) / kRecStride);
        if (nr < 1 || nr > kMaxRecords) return false;
        ++sheets;
        for (int k = 0; k < nr; ++k) {
            const uint64_t r = rb + kRecStride * uint64_t(k);
            int32_t src = 0;
            uint64_t sb = 0, se = 0;
            if (!mem.rd(r + kRecSourceFormation, src) || !mem.rd(r + kRecSlotBegin, sb) || !mem.rd(r + kRecSlotEnd, se)) return false;
            if (src <= 0) continue;  // an empty template record
            if (!is_ptr(sb, 8) || se < sb || se - sb != kSlotStride * kSlots) return false;
            ++records;
            Vals v{0, 0, 0};
            if (mem.rd(r + kRecLineHeight, v.line) && mem.rd(r + kRecBuildUp, v.build_up) && mem.rd(r + kRecApproach, v.approach)) vals.push_back(v);
            for (int sl = 0; sl < kSlots; ++sl) {
                const uint64_t a = sb + kSlotStride * uint64_t(sl);
                int32_t pid = 0, w = 0, rid = 0, foc = 0;
                if (!mem.rd(a + kSlotPid, pid) || !mem.rd(a + kSlotRole, w) || !mem.rd(a + kSlotRoleId, rid) || !mem.rd(a + kSlotFocus, foc)) return false;
                if (pid <= 0 || !role_word_plain(w) || role_group(w) < 1 || w > kRoleWordMax || role_id_of(w) != rid || focus_of(w) != foc) continue;
                words.insert(w);
                ++slots;
            }
        }
    }
    out.words.insert(words.begin(), words.end());
    for (const Vals& v : vals) {
        ++out.build_up_count[v.build_up];
        ++out.approach_count[v.approach];
        auto it = out.approach_lines.find(v.approach);
        if (it == out.approach_lines.end()) out.approach_lines[v.approach] = {v.line, v.line};
        else it->second = {std::min(it->second.first, v.line), std::max(it->second.second, v.line)};
    }
    out.sheets += sheets;
    out.records += records;
    out.slots += slots;
    return true;
}

RoleAllowList build_role_allow_list(Memory& game, uint64_t svc, const Anchors& a, int exclude_team, const std::function<double()>& clock_us) {
    RoleAllowList out;
    ServiceHeader h;
    std::string why = check_service(game, svc, a, &h);
    if (!why.empty()) {
        out.why = why;
        return out;
    }
    std::vector<uint8_t> barr;
    if (!game.read_block(h.buckets, size_t(h.bucket_count) * 8, barr)) {
        out.why = "the bucket array " + hex(h.buckets) + " is not readable";
        return out;
    }
    const double t0 = clock_us ? clock_us() : 0.0;
    int nodes = 0;
    for (uint32_t b = 0; b < h.bucket_count; ++b) {
        uint64_t node = 0;
        std::memcpy(&node, barr.data() + 8 * size_t(b), 8);
        int hops = 0;
        while (node) {
            if (clock_us && clock_us() - t0 > kAllowBudgetUs) {
                out.why = "the walk ran out of time after " + std::to_string(nodes) + " nodes";
                return out;
            }
            if (++nodes > kAllowMaxNodes || uint64_t(nodes) > uint64_t(h.node_count)) {
                out.why = "more map nodes than the service's count (" + std::to_string(h.node_count) + "): the map changed during the walk?";
                return out;
            }
            if (!is_ptr(node, 8) || ++hops > kMaxHops) {
                out.why = "bucket " + std::to_string(b) + " has a bad node chain";
                return out;
            }
            PageCache pc(game);  // one club's node, sheets, records and slots
            int32_t key = 0;
            uint64_t begin = 0, end = 0, next = 0;
            if (!pc.rd(node + kNodeKey, key) || !pc.rd(node + kNodeBegin, begin) || !pc.rd(node + kNodeEnd, end) || !pc.rd(node + kNodeNext, next)) {
                out.why = "node " + hex(node) + " is not readable";
                return out;
            }
            if (key != exclude_team) {  // your own club is left out: its sheet may hold values Turbo wrote
                ++out.teams;
                if (!allow_team(pc, key, begin, end, out)) ++out.skipped;
            }
            node = next;
        }
    }
    if (out.words.empty()) {
        out.why = "no role word found in " + std::to_string(out.teams) + " teams";
        return out;
    }
    out.ok = true;
    return out;
}

// ------------------------------------------------------------------------------------------------ plans
// G6 for a value Turbo itself picks (not an undo): known and writable, or the value the map record holds now (written back unchanged)
static std::string enum_refusal(FieldKind k, int32_t v, const LiveTactic& now) {
    if (k == FieldKind::BuildUp) {
        if (v < kBuildUpMin || v > kBuildUpMax) return "Build-Up Style " + std::to_string(v) + " is outside 0..3";
        if (v != now.build_up && !build_up_style(v))
            return "Build-Up Style value " + std::to_string(v) + " has no known label (Short Passing is 2 or 3, not decoded yet): not written";
        if (v != now.build_up && !build_up_writable(v))
            return "Build-Up Style " + build_up_text(v) + ": its stored value is not decoded yet: not written";
    } else if (k == FieldKind::Approach) {
        if (v < kApproachMin || v > kApproachMax) return "Defensive Approach " + std::to_string(v) + " is outside 0..3";
        if (v != now.approach && !defensive_approach(v)) return "Defensive Approach value " + std::to_string(v) + " has no known label: not written";
        if (v != now.approach && !approach_writable(v))
            return "Defensive Approach " + approach_text(v) + ": its stored value is not decoded yet (no record of any club holds it): not written";
    } else if (k == FieldKind::LineHeight) {
        if (v < kLineMin || v > kLineMax) return "Line Height " + std::to_string(v) + " is outside 1..100";
        if (v != now.line_height && !approach_writable(line_band(v))) {
            const EnumLabel* e = defensive_approach(line_band(v));
            return "Line Height " + std::to_string(v) + " lies in " + (e ? std::string(e->label) : std::string("an unknown approach")) + "'s band (" +
                   line_band_text(v) + "), whose Defensive Approach is not written: Turbo writes up to " + std::to_string(max_writable_line()) +
                   " (the approach band-consistent with the line height)";
        }
    }
    return "";
}

// A non-undo write of the three record fields: the approach is the line height's band (Turbo writes them band-consistent, as the game's own
// CPU records are; X2 writes the inconsistent pair with the dev service, never Turbo). "" = consistent
static std::string band_refusal(int32_t line, int32_t approach) {
    if (approach == line_band(line)) return "";
    const EnumLabel* want = defensive_approach(line_band(line));
    return "Defensive Approach " + approach_text(approach) + " is not Line Height " + std::to_string(line) + "'s band (" + line_band_text(line) + ": " +
           (want ? std::string(want->label) : std::string("?")) + "): Turbo writes them band-consistent (pick the approach, or a line height in its band)";
}

std::string make_plan(const LiveTactic& now, const Request& req, Plan& out, const RoleAllowList* allow) {
    out = Plan();
    if (req.preset) return "the tactical preset is not written (preset ids are not decoded: TACTICS_CHANNEL_E_REPORT.txt section 3)";
    if (req.formation) return "the formation is not written (needs the formations row copied into all 11 slots)";
    const int32_t line = req.line_height == -1 ? now.line_height : req.line_height;
    const int32_t bu = req.build_up == -1 ? now.build_up : req.build_up;
    const int32_t ap = req.approach == -1 ? now.approach : req.approach;
    if (req.line_height != -1) {
        const std::string w = enum_refusal(FieldKind::LineHeight, line, now);
        if (!w.empty()) return w;
    }
    if (req.build_up != -1) {
        const std::string w = enum_refusal(FieldKind::BuildUp, bu, now);
        if (!w.empty()) return w;
    }
    if (req.approach != -1) {
        const std::string w = enum_refusal(FieldKind::Approach, ap, now);
        if (!w.empty()) return w;
    }
    const bool record_change = line != now.line_height || bu != now.build_up || ap != now.approach;
    if (record_change) {
        const std::string w = band_refusal(line, ap);
        if (!w.empty()) return w;
    }
    fill_identity(now, out);
    // Defensive Approach, Line Height and Build-Up Style go together (findings 3a): all three when one of them changes
    if (record_change) {
        out.writes.push_back({FieldKind::Approach, -1, now.approach, ap});
        out.writes.push_back({FieldKind::LineHeight, -1, now.line_height, line});
        out.writes.push_back({FieldKind::BuildUp, -1, now.build_up, bu});
    }
    std::set<int> seen;
    for (const SlotChange& c : req.slots) {
        if (c.slot < 0 || c.slot >= kSlots) return "slot " + std::to_string(c.slot) + " is outside 0..10";
        if (!seen.insert(c.slot).second) return "slot " + std::to_string(c.slot) + " is named twice";
        const SlotView& v = now.slots[size_t(c.slot)];
        if (!role_word_plain(v.role))
            return "slot " + std::to_string(c.slot) + ": role word " + std::to_string(v.role) + " has bits 3..5 set (an encoding not seen): not written";
        const int rid = c.role_id == -1 ? v.role_id : c.role_id;
        const int foc = c.focus == -1 ? v.focus : c.focus;
        if (rid < 0 || rid > kRoleIdMax) return "slot " + std::to_string(c.slot) + ": role id " + std::to_string(rid) + " is outside 0..63";
        if (foc < 0 || foc > kFocusMax) return "slot " + std::to_string(c.slot) + ": focus " + std::to_string(foc) + " is outside 0..7";
        const int32_t word = role_word(v.group(), rid, foc);
        if (word > kRoleWordMax)
            return "slot " + std::to_string(c.slot) + ": role word " + std::to_string(word) + " is above " + std::to_string(kRoleWordMax) +
                   " (the save could not store it)";
        if (role_group(word) != v.group()) return "slot " + std::to_string(c.slot) + ": the position group would change";
        if (word == v.role) continue;
        if (!allow || !allow->ok)
            return "slot " + std::to_string(c.slot) + ": role and focus are not editable: the role allow-list is not built" +
                   (allow && !allow->why.empty() ? " (" + allow->why + ")" : std::string());
        if (!allow->has(word))
            return "slot " + std::to_string(c.slot) + ": group " + std::to_string(v.group()) + " / role " + std::to_string(rid) + " / focus " +
                   std::to_string(foc) + " was never seen in the game's own sheets (role allow-list): not written";
        // the three role values together (report 4.4 step 3)
        out.writes.push_back({FieldKind::Role, c.slot, v.role, word});
        out.writes.push_back({FieldKind::RoleId, c.slot, v.role_id, rid});
        out.writes.push_back({FieldKind::Focus, c.slot, v.focus, foc});
    }
    return "";
}

// ------------------------------------------------------------------------------------------------ the history and the plan check
static std::string copy_label(uint64_t copy) { return copy ? "Edit Tactic copy svc+" + hex(copy) : std::string(); }

static std::string write_label(const FieldWrite& w) {
    return std::string(field_label(w.kind)) + (w.slot >= 0 ? " of slot " + std::to_string(w.slot) : std::string()) +
           (w.copy ? " in " + copy_label(w.copy) : std::string());
}

void WriteHistory::note(const Plan& p, const std::vector<FieldWrite>& written, const std::vector<DbRowWrite>& rows) {
    if (p.undo) return;
    if (p.user != user || p.record_index != record_index || p.load_gen != load_gen) {
        original.clear();
        last.clear();
        db_original.clear();
        db_last.clear();
        user = p.user;
        record_index = p.record_index;
        load_gen = p.load_gen;
    }
    for (const DbRowWrite& row : rows)
        for (const DbRowField& f : row.fields) {
            const auto key = std::make_tuple(row.table.name, row.key, f.name);
            auto o = db_original.find(key);
            if (o == db_original.end()) db_original.emplace(key, f.old_value);
            else {
                auto l = db_last.find(key);
                if (l != db_last.end() && l->second != f.old_value) o->second = f.old_value;  // the game's save wrote another value since
            }
            db_last[key] = f.new_value;
        }
    // a slot is one unit: it "changed since Turbo's write" when its role word does not hold what Turbo wrote last
    std::set<std::pair<uint64_t, int>> slot_changed;
    for (const FieldWrite& w : written) {
        if (w.kind != FieldKind::Role) continue;
        auto it = last.find(std::make_tuple(w.copy, int(w.kind), w.slot));
        if (it != last.end() && it->second != w.old_value) slot_changed.insert(std::make_pair(w.copy, w.slot));
    }
    for (const FieldWrite& w : written) {
        const auto key = std::make_tuple(w.copy, int(w.kind), w.slot);
        auto o = original.find(key);
        if (o == original.end()) {
            original.emplace(key, w.old_value);  // the first write's value
        } else {
            auto l = last.find(key);
            const bool changed = w.slot >= 0 ? slot_changed.count(std::make_pair(w.copy, w.slot)) != 0 : (l != last.end() && l->second != w.old_value);
            if (changed) o->second = w.old_value;  // the game put another value there since: that is what this write replaced
        }
        last[key] = w.new_value;
    }
}

std::string WriteHistory::check_undo(const Plan& p) const {
    if (original.empty() || p.user != user || p.record_index != record_index || p.load_gen != load_gen)
        return "Turbo has written nothing into this record in this career load (since Turbo started): nothing to put back";
    for (const FieldWrite& w : p.writes) {
        auto it = original.find(std::make_tuple(w.copy, int(w.kind), w.slot));
        if (it == original.end()) return write_label(w) + " was not written by Turbo in this career load: not put back";
        if (it->second != w.new_value)
            return write_label(w) + ": the undo would write " + std::to_string(w.new_value) + ", but the game held " + std::to_string(it->second) +
                   " before Turbo's first write: not put back";
    }
    for (const DbRowWrite& row : p.rows)
        for (const DbRowField& f : row.fields) {
            auto it = db_original.find(std::make_tuple(row.table.name, row.key, f.name));
            if (it == db_original.end()) return row.label() + " " + f.name + " was not written by Turbo in this career load: not put back";
            if (it->second != f.new_value)
                return row.label() + " " + f.name + ": the undo would write " + std::to_string(f.new_value) + ", but the row held " +
                       std::to_string(it->second) + " before Turbo's first write: not put back";
        }
    return "";
}

std::string check_plan(const LiveTactic& now, const Plan& plan, const JobChecks& chk) {
    std::set<std::tuple<uint64_t, int, int>> seen;
    std::map<std::pair<uint64_t, int>, std::array<const FieldWrite*, 3>> slots;  // (copy, slot) -> word, role id, focus
    for (const FieldWrite& w : plan.writes) {
        const std::string f = write_label(w);
        if (!seen.insert(std::make_tuple(w.copy, int(w.kind), w.slot)).second) return f + " is in the plan twice";
        if (w.copy) {
            // a working copy is named by an undo only (an Apply's job adds the copies itself), only a copy target (2.4: none by default), and
            // only while it still holds this tactic
            if (!plan.undo) return f + ": only an undo names a working copy";
            if (!copy_target(chk, w.copy)) return f + ": Turbo writes no Edit Tactic working copy (2.4): not put back";
            const WorkCopy* c = now.copy(w.copy);
            if (!c) return f + ": not one of Edit Tactic's working copies";
            if (!c->matches)
                return copy_label(w.copy) + " does not hold this tactic any more (" + (c->why.empty() ? "code " + c->code : c->why) + "): nothing put back";
        }
        if (record_field(w.kind)) {
            if (w.slot != -1) return f + ": not a slot field";
            if (plan.undo) {
                // the game's own values: within the field's range (the history says they were there)
                const bool in = w.kind == FieldKind::LineHeight ? (w.new_value >= kLineMin && w.new_value <= kLineMax)
                                : w.kind == FieldKind::BuildUp  ? (w.new_value >= kBuildUpMin && w.new_value <= kBuildUpMax)
                                                                : (w.new_value >= kApproachMin && w.new_value <= kApproachMax);
                if (!in)
                    return f + " " + std::to_string(w.new_value) + " is outside " +
                           (w.kind == FieldKind::LineHeight ? "1..100" : "0..3");
            } else {
                const std::string why = enum_refusal(w.kind, w.new_value, now);
                if (!why.empty()) return why;
            }
            continue;
        }
        if (w.slot < 0 || w.slot >= kSlots) return f + ": the slot is outside 0..10";
        auto& t = slots[std::make_pair(w.copy, w.slot)];  // value-initialised: three nullptrs
        t[w.kind == FieldKind::Role ? 0 : w.kind == FieldKind::RoleId ? 1 : 2] = &w;
    }
    for (const auto& kv : slots) {
        const uint64_t copy = kv.first.first;
        const int s = kv.first.second;
        const auto& t = kv.second;
        const std::string sl = "slot " + std::to_string(s) + (copy ? " of " + copy_label(copy) : std::string());
        if (!t[0] || !t[1] || !t[2]) return sl + ": the role word, role id and focus are written together or not at all";
        const int group = copy ? role_group(now.copy(copy)->roles[size_t(s)]) : now.slots[size_t(s)].group();
        const int32_t word = t[0]->new_value;
        const int rid = t[1]->new_value, foc = t[2]->new_value;
        if (rid < 0 || rid > kRoleIdMax) return sl + ": role id " + std::to_string(rid) + " is outside 0..63";
        if (foc < 0 || foc > kFocusMax) return sl + ": focus " + std::to_string(foc) + " is outside 0..7";
        if (!role_word_plain(word) || word > kRoleWordMax) return sl + ": role word " + std::to_string(word) + " is not a plain word up to 60000";
        if (role_group(word) != group)
            return sl + ": the position group would change (" + std::to_string(group) + " -> " + std::to_string(role_group(word)) + ")";
        if (word != role_word(group, rid, foc))
            return sl + ": role word " + std::to_string(word) + " is not group " + std::to_string(group) + " / role " + std::to_string(rid) + " / focus " +
                   std::to_string(foc);
        if (!plan.undo) {
            if (!chk.allow || !chk.allow->ok) return sl + ": role and focus are not written without the role allow-list";
            if (!chk.allow->has(word)) return sl + ": role word " + std::to_string(word) + " was never seen in the game's own sheets (role allow-list)";
        }
    }
    if (!plan.undo) {
        // the map record's three fields go together, the approach band-consistent with the line height (make_plan's rule, again in the job)
        const FieldWrite *ln = nullptr, *ap = nullptr, *bu = nullptr;
        for (const FieldWrite& w : plan.writes) {
            if (w.copy) continue;
            if (w.kind == FieldKind::LineHeight) ln = &w;
            else if (w.kind == FieldKind::Approach) ap = &w;
            else if (w.kind == FieldKind::BuildUp) bu = &w;
        }
        if ((ln || ap || bu) && !(ln && ap && bu)) return "Defensive Approach, Line Height and Build-Up Style are written together or not at all";
        if (ln && ap) {
            const std::string b = band_refusal(ln->new_value, ap->new_value);
            if (!b.empty()) return b;
        }
    }
    // the database rows: each field in its range; an Apply's field is the mirror of one of this plan's map-record writes, with its new value
    for (const DbRowWrite& row : plan.rows) {
        if (!row.rec) return row.label() + ": no record";
        std::set<std::string> names;
        for (const DbRowField& f : row.fields) {
            if (!names.insert(f.name).second) return row.label() + " " + f.name + " is in the plan twice";
            int64_t lo = 0, hi = 0;
            if (!db_row_field_range(f.name, lo, hi)) return row.label() + " " + f.name + " is not a field Turbo writes";
            if (f.new_value < lo || f.new_value > hi)
                return row.label() + " " + f.name + " " + std::to_string(f.new_value) + " is outside " + std::to_string(lo) + ".." + std::to_string(hi);
            if (plan.undo) continue;
            bool mirrored = false;
            for (const FieldWrite& w : plan.writes)
                mirrored = mirrored || (!w.copy && mirror_field_name(w) == f.name && int64_t(w.new_value) == f.new_value);
            if (!mirrored) return row.label() + " " + f.name + " = " + std::to_string(f.new_value) + " is not what the live record gets: not written";
        }
    }
    if (plan.undo) {
        if (!chk.history) return "an undo writes only values Turbo read from the game before its write (none known)";
        const std::string w = chk.history->check_undo(plan);
        if (!w.empty()) return w;
    }
    return "";
}

static std::string join_list(const std::vector<std::string>& v) {
    std::string r;
    for (const auto& x : v) r += (r.empty() ? "" : ", ") + x;
    return r;
}

std::vector<FieldWrite> copy_writes(const LiveTactic& now, const Plan& plan, std::string& note, const std::set<uint64_t>* targets) {
    std::vector<FieldWrite> out;
    note.clear();
    if (!targets || targets->empty()) return out;  // 2.4: no copy target (the E2 working copies are never written)
    std::vector<std::string> written, same, left;
    for (const WorkCopy& c : now.copies) {
        if (!c.matches || !targets->count(c.offset)) continue;
        // every slot of the copy must hold the map record's player (the code and the formation ids alone do not tell your club's tactic from
        // another team's or another line-up's with the same code), a slot of the plan the same position group, or the copy is left out
        std::string bad;
        for (int s = 0; s < kSlots && bad.empty(); ++s)
            if (c.pids[size_t(s)] != now.slots[size_t(s)].pid) bad = "its slot " + std::to_string(s) + " holds player " + std::to_string(c.pids[size_t(s)]);
        for (const FieldWrite& w : plan.writes) {
            if (!bad.empty()) break;
            if (w.copy || w.slot < 0 || w.slot >= kSlots) continue;
            if (role_group(c.roles[size_t(w.slot)]) != now.slots[size_t(w.slot)].group())
                bad = "its slot " + std::to_string(w.slot) + " is another position group";
        }
        // G6 for the copy: a value without a known label is written back unchanged into the map record only (the game put it there); a copy
        // holding another value does not get it (check_plan never sees the copies' writes of an Apply)
        for (const FieldWrite& w : plan.writes) {
            if (!bad.empty()) break;
            if (w.copy || (w.kind != FieldKind::BuildUp && w.kind != FieldKind::Approach)) continue;
            const bool known = w.kind == FieldKind::BuildUp ? build_up_style(w.new_value) != nullptr : defensive_approach(w.new_value) != nullptr;
            const int32_t cur = value_in(now, w.kind, -1, c.offset);
            if (!known && cur != w.new_value)
                bad = "its " + std::string(field_label(w.kind)) + " is " + std::to_string(cur) + "; the live record's " + std::to_string(w.new_value) +
                      " has no known label and is not copied";
        }
        if (!bad.empty()) {
            left.push_back(c.label() + " (" + bad + ")");
            continue;
        }
        const size_t before = out.size();
        std::set<int> slots_done;
        for (const FieldWrite& w : plan.writes) {
            if (w.copy) continue;
            if (record_field(w.kind)) {
                const int32_t cur = value_in(now, w.kind, -1, c.offset);
                if (cur != w.new_value) out.push_back({w.kind, -1, cur, w.new_value, c.offset});
                continue;
            }
            // a slot: its three role values together, when the copy's word is not the new one
            if (w.kind != FieldKind::Role || !slots_done.insert(w.slot).second) continue;
            if (c.roles[size_t(w.slot)] == w.new_value) continue;
            for (const FieldWrite& x : plan.writes)
                if (!x.copy && x.slot == w.slot) out.push_back({x.kind, x.slot, value_in(now, x.kind, x.slot, c.offset), x.new_value, c.offset});
        }
        (out.size() > before ? written : same).push_back(c.label());
    }
    if (written.empty() && same.empty() && left.empty()) {
        note = "no Edit Tactic working copy holds this tactic (code " + (now.code.empty() ? std::string("not readable") : now.code) +
               "): map record only; Edit Tactic shows its own copy until the next career load";
    } else {
        if (!written.empty()) note = "Edit Tactic copy " + join_list(written) + " (code " + now.code + ") written too";
        if (!same.empty()) note += (note.empty() ? "" : "; ") + std::string("Edit Tactic copy ") + join_list(same) + " already holds these values";
        if (!left.empty())
            note += (note.empty() ? "" : "; ") + std::string("Edit Tactic copy ") + join_list(left) +
                    " NOT written: Edit Tactic may show its own values until the next career load";
    }
    return out;
}

std::string describe_writes(int user, int record_index, const std::vector<FieldWrite>& ws) {
    std::string s = "team " + std::to_string(user) + " record " + std::to_string(record_index);
    std::vector<uint64_t> copies;
    for (const FieldWrite& w : ws) {
        if (w.copy) {
            if (std::find(copies.begin(), copies.end(), w.copy) == copies.end()) copies.push_back(w.copy);
            continue;
        }
        if (record_field(w.kind) && w.old_value == w.new_value) continue;  // written back unchanged (the three go together)
        switch (w.kind) {
            case FieldKind::LineHeight: s += " line height " + std::to_string(w.old_value) + "->" + std::to_string(w.new_value); break;
            case FieldKind::BuildUp: s += " build-up " + std::to_string(w.old_value) + "->" + std::to_string(w.new_value); break;
            case FieldKind::Approach: s += " approach " + std::to_string(w.old_value) + "->" + std::to_string(w.new_value); break;
            case FieldKind::Role:
                s += " slot " + std::to_string(w.slot) + " role " + std::to_string(w.old_value) + "->" + std::to_string(w.new_value);
                break;
            case FieldKind::RoleId:
            case FieldKind::Focus: break;  // the word says it
        }
    }
    if (!copies.empty()) {
        s += "; Edit Tactic cop" + std::string(copies.size() == 1 ? "y" : "ies");
        for (size_t i = 0; i < copies.size(); ++i) s += (i ? ", " : " ") + std::string("svc+") + hex(copies[i]);
        s += " too";
    }
    return s;
}

// "; cm_mentalities artificialkey 0 defensivedepth 25->90 buildupplay 0->1" (a row whose fields did not change: "... unchanged")
static std::string describe_rows(const std::vector<DbRowWrite>& rows) {
    std::string s;
    for (const DbRowWrite& row : rows) {
        s += "; " + row.label();
        if (row.fields.empty()) s += " already holds these values";
        for (const DbRowField& f : row.fields) s += " " + f.name + " " + std::to_string(f.old_value) + "->" + std::to_string(f.new_value);
    }
    return s;
}

// ------------------------------------------------------------------------------------------------ the database row
std::string DbRowWrite::label() const { return (table.name.empty() ? std::string("row") : table.name) + " artificialkey " + std::to_string(key); }

bool db_row_field_range(const std::string& name, int64_t& lo, int64_t& hi) {
    if (name == "defensivedepth") {
        lo = kLineMin;
        hi = kLineMax;
        return true;
    }
    if (name == "buildupplay") {
        lo = kBuildUpMin;
        hi = kBuildUpMax;
        return true;
    }
    // pos0role .. pos10role
    if (name.size() >= 8 && name.compare(0, 3, "pos") == 0 && name.compare(name.size() - 4, 4, "role") == 0) {
        const std::string n = name.substr(3, name.size() - 7);
        if (n.empty() || n.size() > 2 || !std::all_of(n.begin(), n.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
        if (std::stoi(n) >= kSlots) return false;
        lo = 0;
        hi = kRoleWordMax;
        return true;
    }
    return false;
}

std::string check_db_row(Memory& mem, const DbRowWrite& row) {
    Database db(mem);
    if (!row.rec) return "no record";
    if (!db.table_alive(row.table, row.rec)) return "the database changed (another save loaded?)";
    if (!db.record_valid(row.table, row.rec)) return "the record is deleted";
    for (const DbRowCheck& c : row.checks) {
        const Field* f = row.table.field(c.name);
        Value v;
        if (!f || !db.get(row.table, row.rec, *f, v)) return c.name + " is not readable";
        if (!(v == c.value)) return c.name + " is " + v.to_string() + ", not " + c.value.to_string() + " (another row now?)";
    }
    for (const DbRowField& fld : row.fields) {
        const Field* f = row.table.field(fld.name);
        Value v;
        if (!f || f->type != FieldType::Int || !db.get(row.table, row.rec, *f, v) || v.type != FieldType::Int) return fld.name + " is not readable";
        if (v.i != fld.old_value)
            return fld.name + " is " + std::to_string(v.i) + ", not " + std::to_string(fld.old_value) + " as when Apply was pressed (someone else wrote it)";
        int64_t lo = 0, hi = 0;
        if (!db_row_field_range(fld.name, lo, hi)) return fld.name + " is not a field Turbo writes";
        if (fld.new_value < lo || fld.new_value > hi) return fld.name + " " + std::to_string(fld.new_value) + " is outside " + std::to_string(lo) + ".." + std::to_string(hi);
        const std::string w = Database::validate(*f, Value::of_int(fld.new_value));
        if (!w.empty()) return fld.name + ": " + w;
    }
    return "";
}

// ------------------------------------------------------------------------------------------------ the write
ApplyResult apply_plan(Memory& game, uint64_t svc, const Anchors& a, const Plan& plan, const JobChecks& chk) {
    ApplyResult r;
    r.undo = plan.undo;
    // every read of the game is counted; the walks go through whole pages (one read per 4 KB), the writes, the read-back and the restore
    // through `mem` (each field from the game itself)
    Counting mem(game);
    auto fail = [&](const char* stage, const std::string& msg) {
        r.ok = false;
        r.stage = stage;
        r.message = msg;
        r.log_line = "channel E: " + std::string(plan.undo ? "undo " : "") + "refused (" + stage + "): " + msg;
        r.reads = mem.reads;
        return r;
    };
    try {
        // 1. L1-L4 again (pointers can move between frames), the working copies with them
        PageCache walk(mem);
        LiveTactic now;
        std::string why = read_live(walk, svc, plan.user, a, now);
        if (!why.empty()) return fail("locate", why);
        // G5 again
        why = check_g5(walk, now);
        if (!why.empty()) return fail("G5", why);
        // the record the plan was made for, with exactly the plan's old values
        if (now.at.record_index != plan.record_index || now.formation != plan.formation || now.source_formation != plan.source_formation ||
            now.sheet_id != plan.sheet_id)
            return fail("changed", "the active tactic is not the one Apply was pressed for (record " + std::to_string(now.at.record_index) + ", formation " +
                                       std::to_string(now.formation) + "): nothing written");
        if (!plan.code.empty() && now.code != plan.code)
            return fail("changed", "the tactic code is " + (now.code.empty() ? std::string("not readable") : now.code) + ", not " + plan.code +
                                       " as when Apply was pressed (Edit Tactic saved another tactic?): nothing written");
        for (int s = 0; s < kSlots; ++s)
            if (now.slots[size_t(s)].pid != plan.pids[size_t(s)])
                return fail("changed", "slot " + std::to_string(s) + " holds another player since Apply was pressed: nothing written");
        if (plan.writes.empty()) {
            r.ok = true;
            r.stage = "done";
            r.message = "nothing to change";
            r.after = now;
            r.log_line = "channel E: nothing to change";
            r.reads = mem.reads;
            return r;
        }
        // the plan on its own (ranges, known values, the slot's three values together, the group, the allow-list; an undo's values in the
        // history, its working copies still this tactic)
        why = check_plan(now, plan, chk);
        if (!why.empty()) return fail("G6", why + ": nothing written");
        // the same fields into Edit Tactic's working copies that hold this tactic (an undo names its copies itself)
        std::vector<FieldWrite> writes = plan.writes;
        if (!plan.undo) {
            std::string note;
            const std::vector<FieldWrite> cw = copy_writes(now, plan, note, chk.copy_targets);
            writes.insert(writes.end(), cw.begin(), cw.end());
            r.copies = note;
        }
        std::set<uint64_t> copies_touched;
        for (const FieldWrite& w : writes)
            if (w.copy) copies_touched.insert(w.copy);
        r.copies_written = int(copies_touched.size());
        if (plan.undo && !copies_touched.empty()) {
            std::string l;
            for (uint64_t c : copies_touched) l += (l.empty() ? "" : ", ") + std::string("svc+") + hex(c);
            r.copies = "Edit Tactic copy " + l + " put back too";
        }
        // the career database rows (the same job): the table, the record, the row's identity and the owner rule again
        for (const DbRowWrite& row : plan.rows) {
            const std::string w = check_db_row(mem, row);
            if (!w.empty()) return fail("db row", row.label() + ": " + w + ": nothing written");
        }
        // 2. snapshot (the old values, as they are in memory now)
        struct Snap {
            uint64_t addr;
            int32_t value;
        };
        std::vector<Snap> snap;
        for (const FieldWrite& w : writes) {
            const uint64_t ad = field_address(now, w);
            int32_t cur = 0;
            if (!ad || !mem.rd(ad, cur)) return fail("snapshot", write_label(w) + " is not readable: nothing written");
            if (cur != w.old_value)
                return fail("changed", write_label(w) + " is " + std::to_string(cur) + ", not " + std::to_string(w.old_value) +
                                           " as when Apply was pressed: nothing written");
            snap.push_back({ad, cur});
        }
        Database db(mem);
        struct RowSnap {
            size_t row, field;
            const Field* f;
            int64_t value, want;
        };
        std::vector<RowSnap> rsnap;
        for (size_t i = 0; i < plan.rows.size(); ++i) {
            const DbRowWrite& row = plan.rows[i];
            for (size_t k = 0; k < row.fields.size(); ++k) {
                const DbRowField& fld = row.fields[k];
                const Field* f = row.table.field(fld.name);
                Value v;
                if (!f || !db.get(row.table, row.rec, *f, v) || v.type != FieldType::Int)
                    return fail("snapshot", row.label() + " " + fld.name + " is not readable: nothing written");
                if (v.i != fld.old_value)
                    return fail("changed", row.label() + " " + fld.name + " is " + std::to_string(v.i) + ", not " + std::to_string(fld.old_value) +
                                               " as when Apply was pressed: nothing written");
                rsnap.push_back({i, k, f, v.i, fld.new_value});
            }
        }
        const uint64_t dirty_addr = now.at.sheet + kSheetDirty;
        if (!mem.rd(dirty_addr, r.dirty_before)) return fail("snapshot", "the dirty flag is not readable: nothing written");
        if (chk.cancel && chk.cancel->load()) return fail("cancelled", "the job was given up: nothing written");
        // 3. write (map record and copies); then the dirty flag as UpdateSheet does; then the database rows
        bool write_failed = false;
        for (size_t i = 0; i < writes.size(); ++i)
            if (!mem.wr(snap[i].addr, writes[i].new_value)) write_failed = true;
        const uint8_t one = 1;
        if (!write_failed && !mem.wr(dirty_addr, one)) write_failed = true;
        if (!write_failed)
            for (const RowSnap& x : rsnap)
                if (!db.set(plan.rows[x.row].table, plan.rows[x.row].rec, *x.f, Value::of_int(x.want))) write_failed = true;
        // 4. read back every written field
        std::string bad;
        for (size_t i = 0; i < writes.size(); ++i) {
            int32_t back = 0;
            if (!mem.rd(snap[i].addr, back) || back != writes[i].new_value) {
                bad = write_label(writes[i]) + " reads " + std::to_string(back) + ", not " + std::to_string(writes[i].new_value);
                break;
            }
        }
        uint8_t dirty_back = 0;
        if (bad.empty() && (!mem.rd(dirty_addr, dirty_back) || dirty_back != 1)) bad = "the dirty flag reads " + std::to_string(dirty_back);
        for (const RowSnap& x : rsnap) {
            if (!bad.empty()) break;
            Value v;
            if (!db.get(plan.rows[x.row].table, plan.rows[x.row].rec, *x.f, v) || v.type != FieldType::Int || v.i != x.want)
                bad = plan.rows[x.row].label() + " " + x.f->name + " reads " + (v.type == FieldType::Int ? std::to_string(v.i) : std::string("?")) + ", not " +
                      std::to_string(x.want);
        }
        if (write_failed && bad.empty()) bad = "a write was refused by the memory layer";
        // what the rows hold now (old = the snapshot): the result and the undo file name them
        std::vector<DbRowWrite> rows_done;
        for (const DbRowWrite& row : plan.rows) {
            DbRowWrite d = row;
            d.fields.clear();
            rows_done.push_back(std::move(d));
        }
        for (const RowSnap& x : rsnap) rows_done[x.row].fields.push_back({plan.rows[x.row].fields[x.field].name, x.value, x.want});
        r.rows_note = plan.rows_note;
        if (!bad.empty()) {
            // full undo from the snapshot (fields, the dirty flag, the rows): every field that does not hold its old value is written back, then
            // everything is read back (a field the write never reached already holds it)
            for (const Snap& s : snap) {
                int32_t cur = 0;
                if (!mem.rd(s.addr, cur) || cur != s.value) mem.wr(s.addr, s.value);
            }
            uint8_t d = 0;
            if (!mem.rd(dirty_addr, d) || d != r.dirty_before) mem.wr(dirty_addr, r.dirty_before);
            for (const RowSnap& x : rsnap) {
                Value v;
                if (!db.get(plan.rows[x.row].table, plan.rows[x.row].rec, *x.f, v) || v.type != FieldType::Int || v.i != x.value)
                    db.set(plan.rows[x.row].table, plan.rows[x.row].rec, *x.f, Value::of_int(x.value));
            }
            bool restored = true;
            for (const Snap& s : snap) {
                int32_t back = 0;
                if (!mem.rd(s.addr, back) || back != s.value) restored = false;
            }
            if (!mem.rd(dirty_addr, d) || d != r.dirty_before) restored = false;
            for (const RowSnap& x : rsnap) {
                Value v;
                if (!db.get(plan.rows[x.row].table, plan.rows[x.row].rec, *x.f, v) || v.type != FieldType::Int || v.i != x.value) restored = false;
            }
            r.undone = restored;
            r.restore_failed = !restored;
            r.session_off = true;
            r.writes = writes;
            r.rows = rows_done;
            r.ok = false;
            r.stage = "readback";
            r.message = "read-back mismatch (" + bad + "): " +
                        (restored ? "every field, the dirty flag and the database row were put back" : "PUTTING BACK FAILED") +
                        "; channel E is off for this session";
            r.log_line = "channel E: " + describe_writes(plan.user, plan.record_index, writes) + describe_rows(rows_done) + " READ-BACK MISMATCH (" + bad + "), " +
                         (restored ? "undone from the snapshot" : "undo FAILED");
            r.reads = mem.reads;
            return r;
        }
        r.dirty_after = dirty_back;
        r.ok = true;
        r.written = true;
        r.writes = writes;
        r.rows = rows_done;
        r.stage = "done";
        PageCache after(mem);
        std::string why_after = read_live(after, svc, plan.user, a, r.after);
        r.message = std::string(plan.undo ? "put back" : "written") + " and read back" + (why_after.empty() ? "" : " (read-out after: " + why_after + ")");
        // the copies' note when it says more than the write list: no copy written, or one left out
        const bool say = !r.copies.empty() && (r.copies_written == 0 || r.copies.find("NOT written") != std::string::npos);
        r.log_line = "channel E: " + std::string(plan.undo ? "undo " : "") + describe_writes(plan.user, plan.record_index, writes) + describe_rows(rows_done) +
                     " (read back ok)" + (say ? " [" + r.copies + "]" : std::string()) +
                     (plan.rows.empty() && !plan.rows_note.empty() ? " [cm_mentalities not written: " + plan.rows_note + "]" : std::string());
        if (why_after.empty())
            r.log_line += " [record now LH " + std::to_string(r.after.line_height) + " BU " + std::to_string(r.after.build_up) + " DA " +
                          std::to_string(r.after.approach) + "]";
        r.reads = mem.reads;
        return r;
    } catch (const std::exception& e) {
        return fail("exception", e.what());
    } catch (...) {
        return fail("exception", "unknown error");
    }
}

// ------------------------------------------------------------------------------------------------ the undo record
UndoRecord UndoRecord::from_plan(const Plan& p) {
    UndoRecord u;
    u.build = kBuild;
    u.team = p.user;
    u.record_index = p.record_index;
    u.record_name = p.record_name;
    u.code = p.code;
    u.sheet_id = p.sheet_id;
    u.formation = p.formation;
    u.source_formation = p.source_formation;
    u.pids = p.pids;
    u.writes = p.writes;
    u.state = "pending";
    return u;
}

std::string UndoRecord::to_json() const {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["turbo_channel_e_undo"] = 1;
    j["build"] = build;
    j["when"] = when;
    j["session"] = session;
    j["load_gen"] = load_gen;
    j["state"] = state;
    j["team"] = team;
    j["record_index"] = record_index;
    j["record_name"] = record_name;
    j["code"] = code;
    j["sheet_id"] = sheet_id;
    j["formation"] = formation;
    j["source_formation"] = source_formation;
    j["dirty_before"] = int(dirty_before);
    j["pids"] = pids;
    nlohmann::ordered_json ws = nlohmann::ordered_json::array();
    for (const FieldWrite& w : writes) {
        nlohmann::ordered_json x = nlohmann::ordered_json::object();
        x["field"] = field_name(w.kind);
        x["slot"] = w.slot;
        x["old"] = w.old_value;
        x["new"] = w.new_value;
        if (w.copy) x["copy"] = hex(w.copy);  // an Edit Tactic working copy (svc offset); absent = the map record
        ws.push_back(x);
    }
    j["fields"] = ws;
    j["edit_tactic_copies"] = copies;
    nlohmann::ordered_json m = nlohmann::ordered_json::object();
    m["state"] = mirror_state;
    m["note"] = mirror_note;
    m["table"] = mirror_table;
    m["artificialkey"] = mirror_row_key;
    nlohmann::ordered_json mf = nlohmann::ordered_json::array();
    for (const MirrorField& f : mirror) mf.push_back({{"field", f.field}, {"old", f.old_value}, {"new", f.new_value}});
    m["fields"] = mf;
    j["db_mirror"] = m;
    j["note"] = note;
    return j.dump(1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}

bool UndoRecord::parse(const std::string& text, UndoRecord& out, std::string* err) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("turbo_channel_e_undo") || !j["turbo_channel_e_undo"].is_number_integer()) {
        if (err) *err = "not a channel E undo file";
        return false;
    }
    try {
        UndoRecord u;
        u.build = j.value("build", std::string());
        u.when = j.value("when", std::string());
        u.session = j.value("session", std::string());
        u.load_gen = j.value("load_gen", -1LL);
        u.state = j.value("state", std::string());
        u.team = j.value("team", 0);
        u.record_index = j.value("record_index", -1);
        u.record_name = j.value("record_name", std::string());
        u.code = j.value("code", std::string());
        u.copies = j.value("edit_tactic_copies", std::string());
        u.sheet_id = j.value("sheet_id", 0);
        u.formation = j.value("formation", 0);
        u.source_formation = j.value("source_formation", 0);
        u.dirty_before = uint8_t(j.value("dirty_before", 0));
        if (j.contains("pids") && j["pids"].is_array() && j["pids"].size() == kSlots)
            for (int s = 0; s < kSlots; ++s) u.pids[size_t(s)] = j["pids"][size_t(s)].get<int32_t>();
        else {
            if (err) *err = "the undo file has no 11 slot players";
            return false;
        }
        if (j.contains("fields") && j["fields"].is_array())
            for (const auto& x : j["fields"]) {
                FieldWrite w;
                if (!x.is_object() || !field_from_name(x.value("field", std::string()), w.kind)) {
                    if (err) *err = "an unknown field in the undo file";
                    return false;
                }
                w.slot = x.value("slot", -1);
                const bool slot_field = w.kind == FieldKind::Role || w.kind == FieldKind::RoleId || w.kind == FieldKind::Focus;
                if (slot_field ? (w.slot < 0 || w.slot >= kSlots) : w.slot != -1) {
                    if (err) *err = std::string("the undo file names ") + field_name(w.kind) + " with slot " + std::to_string(w.slot);
                    return false;
                }
                w.old_value = x.value("old", 0);
                w.new_value = x.value("new", 0);
                if (x.contains("copy")) {
                    const std::string c = x["copy"].is_string() ? x["copy"].get<std::string>() : std::string();
                    char* end = nullptr;
                    w.copy = c.empty() ? 0 : std::strtoull(c.c_str(), &end, 16);
                    if (c.empty() || !end || *end || std::find(kWorkCopyOffsets.begin(), kWorkCopyOffsets.end(), w.copy) == kWorkCopyOffsets.end()) {
                        if (err) *err = "the undo file names a working copy " + c + " that is not one of Edit Tactic's";
                        return false;
                    }
                }
                u.writes.push_back(w);
            }
        if (j.contains("db_mirror") && j["db_mirror"].is_object()) {
            const auto& m = j["db_mirror"];
            u.mirror_state = m.value("state", std::string());
            u.mirror_note = m.value("note", std::string());
            u.mirror_table = m.value("table", std::string());
            u.mirror_row_key = m.value("artificialkey", -1LL);
            if (m.contains("fields") && m["fields"].is_array())
                for (const auto& f : m["fields"])
                    if (f.is_object()) u.mirror.push_back({f.value("field", std::string()), f.value("old", 0LL), f.value("new", 0LL)});
        }
        u.note = j.value("note", std::string());
        out = std::move(u);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = std::string("undo file: ") + e.what();
        return false;
    }
}

bool load_undo(const fs::path& p, UndoRecord& out, std::string* err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "no " + p.filename().string();
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return UndoRecord::parse(ss.str(), out, err);
}

bool save_undo(const fs::path& p, const UndoRecord& u, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f << u.to_json() << "\n";
        if (!f) {
            if (err) *err = "write failed";
            return false;
        }
    }
    fs::remove(p, ec);
    fs::rename(tmp, p, ec);
    if (ec) {
        if (err) *err = ec.message();
        return false;
    }
    return true;
}

static int32_t current_of(const LiveTactic& now, const FieldWrite& w) { return value_in(now, w.kind, w.slot, w.copy); }

std::string same_career(const UndoRecord& u, const std::string& session, long long load_gen) {
    if (u.session == session && u.load_gen == load_gen) return "";
    return "the undo file is from another career load (written " + (u.when.empty() ? std::string("?") : u.when) + ", load " + std::to_string(u.load_gen) +
           (u.session != session ? " of another Lua session" : "") + "; this is load " + std::to_string(load_gen) +
           "): the reload threw Turbo's in-memory change away (only an in-game save would have kept it); nothing put back";
}

std::string make_undo_plan(const LiveTactic& now, const UndoRecord& u, const std::string& session, long long load_gen, Plan& out,
                           std::vector<std::string>& skipped, const std::set<uint64_t>* copy_targets) {
    out = Plan();
    skipped.clear();
    if (u.writes.empty()) return "the undo file holds no field Turbo wrote";
    const std::string sc = same_career(u, session, load_gen);
    if (!sc.empty()) return sc;
    if (u.team != now.at.user) return "the undo file is for club " + std::to_string(u.team) + ", the live tactic is club " + std::to_string(now.at.user) + "'s";
    if (u.record_index != now.at.record_index || u.formation != now.formation || u.source_formation != now.source_formation || u.sheet_id != now.sheet_id)
        return "the active tactic is not the one Turbo wrote (record " + std::to_string(now.at.record_index) + ", formation " + std::to_string(now.formation) +
               "): nothing put back";
    if (!u.code.empty() && now.code != u.code)
        return "the tactic code is " + (now.code.empty() ? std::string("not readable") : now.code) + ", not " + u.code +
               " as when Turbo wrote it (Edit Tactic saved another tactic since?): nothing put back";
    for (int s = 0; s < kSlots; ++s)
        if (now.slots[size_t(s)].pid != u.pids[size_t(s)]) return "slot " + std::to_string(s) + " holds another player now: nothing put back";
    fill_identity(now, out);
    out.undo = true;
    out.load_gen = load_gen;
    // a working copy that no longer holds this tactic is left alone (its fields are Edit Tactic's now)
    std::set<uint64_t> copy_gone;
    for (const FieldWrite& w : u.writes) {
        if (!w.copy || copy_gone.count(w.copy)) continue;
        if (!copy_targets || !copy_targets->count(w.copy)) {
            // a working copy an earlier Turbo wrote (2.3's E2): 2.4 writes none, not even back
            copy_gone.insert(w.copy);
            skipped.push_back("Edit Tactic copy svc+" + hex(w.copy) + ": Turbo 2.4 writes no Edit Tactic working copy: left as it is");
            continue;
        }
        const WorkCopy* c = now.copy(w.copy);
        if (!c || !c->matches) {
            copy_gone.insert(w.copy);
            skipped.push_back("Edit Tactic copy svc+" + hex(w.copy) + " does not hold this tactic any more (" +
                              (!c ? std::string("not read") : c->why.empty() ? "code " + c->code : c->why) + "): left as it is");
        }
    }
    // per slot (of the map record or of one copy): all three role values hold Turbo's values, or none is put back
    std::set<std::pair<uint64_t, int>> slot_bad;
    for (const FieldWrite& w : u.writes)
        if (w.slot >= 0 && !copy_gone.count(w.copy) && current_of(now, w) != w.new_value) slot_bad.insert(std::make_pair(w.copy, w.slot));
    for (const FieldWrite& w : u.writes) {
        if (copy_gone.count(w.copy)) continue;
        const int32_t cur = current_of(now, w);
        const bool slot_field = w.slot >= 0;
        const bool holds = slot_field ? (slot_bad.count(std::make_pair(w.copy, w.slot)) == 0) : cur == w.new_value;
        const std::string where = w.copy ? " in Edit Tactic copy svc+" + hex(w.copy) : std::string();
        if (!holds) {
            if (!slot_field || w.kind == FieldKind::Role)
                skipped.push_back(std::string(field_label(w.kind)) + (slot_field ? " of slot " + std::to_string(w.slot) : std::string()) + where + " is " +
                                  std::to_string(cur) + ", not Turbo's " + std::to_string(w.new_value) + ": left as it is");
            continue;
        }
        if (!slot_field && cur == w.old_value) continue;  // nothing to put back (a slot's three role values go back together)
        out.writes.push_back({w.kind, w.slot, cur, w.old_value, w.copy});
    }
    if (out.writes.empty()) return skipped.empty() ? "every field already holds its value from before Turbo's write" : "no field still holds Turbo's value";
    return "";
}

bool same_record(const UndoRecord& u, const LiveTactic& now) {
    if (u.team != now.at.user || u.record_index != now.at.record_index || u.formation != now.formation || u.source_formation != now.source_formation ||
        u.sheet_id != now.sheet_id)
        return false;
    if (!u.code.empty() && u.code != now.code) return false;
    for (int s = 0; s < kSlots; ++s)
        if (u.pids[size_t(s)] != now.slots[size_t(s)].pid) return false;
    return true;
}

void add_job_writes(UndoRecord& u, const std::vector<FieldWrite>& job) {
    for (const FieldWrite& w : job) {
        if (!w.copy) continue;  // the map record's writes are the plan's (already in the record)
        FieldWrite* have = nullptr;
        for (FieldWrite& x : u.writes)
            if (x.copy == w.copy && x.kind == w.kind && x.slot == w.slot) have = &x;
        if (!have) u.writes.push_back(w);
        else {
            if (have->new_value != w.old_value) have->old_value = w.old_value;  // the copy changed in between: the latest value is what was replaced
            have->new_value = w.new_value;
        }
    }
}

UndoRecord merge_undo(const UndoRecord& prev, const UndoRecord& next) {
    if (prev.session != next.session || prev.load_gen != next.load_gen) return next;  // another career load: its change is gone anyway
    if (prev.team != next.team || prev.record_index != next.record_index || prev.formation != next.formation ||
        prev.source_formation != next.source_formation || prev.sheet_id != next.sheet_id || prev.pids != next.pids ||
        (!prev.code.empty() && !next.code.empty() && prev.code != next.code))
        return next;
    UndoRecord m = next;
    m.dirty_before = prev.dirty_before;
    auto find = [](const std::vector<FieldWrite>& ws, FieldKind k, int slot, uint64_t copy) -> const FieldWrite* {
        for (const FieldWrite& w : ws)
            if (w.kind == k && w.slot == slot && w.copy == copy) return &w;
        return nullptr;
    };
    // a slot is one unit: when the earlier write's role word is this plan's old word, all three take the earlier old values
    for (FieldWrite& w : m.writes) {
        const FieldWrite* p = find(prev.writes, w.kind, w.slot, w.copy);
        if (!p) continue;
        if (w.slot >= 0) {
            const FieldWrite* pr = find(prev.writes, FieldKind::Role, w.slot, w.copy);
            const FieldWrite* nr = find(next.writes, FieldKind::Role, w.slot, w.copy);
            if (pr && nr && pr->new_value == nr->old_value) w.old_value = p->old_value;
        } else if (p->new_value == w.old_value) {
            w.old_value = p->old_value;
        }
    }
    for (const FieldWrite& p : prev.writes)
        if (!find(next.writes, p.kind, p.slot, p.copy)) m.writes.push_back(p);
    m.mirror = prev.mirror;
    m.mirror_state = prev.mirror_state;
    m.mirror_table = prev.mirror_table;
    m.mirror_row_key = prev.mirror_row_key;
    m.mirror_note = prev.mirror_note;
    return m;
}

std::vector<std::string> reverted(const LiveTactic& now, const UndoRecord& u) {
    std::vector<std::string> out;
    if (u.state != "written") return out;
    if (u.team != now.at.user || u.record_index != now.at.record_index) {
        out.push_back("the active tactic record changed since Turbo wrote it");
        return out;
    }
    if (!u.code.empty() && now.code != u.code) {
        out.push_back("the tactic code " + u.code + " -> " + (now.code.empty() ? std::string("not readable") : now.code) +
                      " (Edit Tactic's save was committed to the team sheet?)");
        return out;
    }
    std::set<uint64_t> copy_said;
    for (const FieldWrite& w : u.writes) {
        if (w.kind == FieldKind::RoleId || w.kind == FieldKind::Focus) continue;  // the word says it
        if (w.copy) {
            const WorkCopy* c = now.copy(w.copy);
            if (!c || !c->matches) {
                if (copy_said.insert(w.copy).second)
                    out.push_back("Edit Tactic copy svc+" + hex(w.copy) + " holds another tactic now (" + (!c ? std::string("not read") : c->why.empty() ? "code " + c->code : c->why) + ")");
                continue;
            }
        }
        const int32_t cur = current_of(now, w);
        if (cur != w.new_value)
            out.push_back(std::string(w.kind == FieldKind::Role ? "slot " + std::to_string(w.slot) + " role" : field_label(w.kind)) +
                          (w.copy ? " in Edit Tactic copy svc+" + hex(w.copy) : std::string()) + " " + std::to_string(w.new_value) + " -> " + std::to_string(cur));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ overwrite detection and Re-apply
bool same_tactic_record(const UndoRecord& u, const LiveTactic& now) {
    return u.team == now.at.user && u.record_index == now.at.record_index && u.formation == now.formation &&
           u.source_formation == now.source_formation && u.sheet_id == now.sheet_id;
}

std::string replaced_prompt(const LiveTactic& now, const UndoRecord& u) {
    if (u.state != "written" || !same_tactic_record(u, now)) return "";
    bool replaced = false;
    for (const FieldWrite& w : u.writes) {
        if (w.copy || w.kind == FieldKind::RoleId || w.kind == FieldKind::Focus) continue;  // the map record's fields; a slot by its word
        if (w.kind == FieldKind::Role) {
            const int s = w.slot;
            if (s < 0 || s >= kSlots) continue;
            // a slot that holds another player now: Re-apply would leave it out, so it does not count as "replaced"
            if (now.slots[size_t(s)].pid == u.pids[size_t(s)] && now.slots[size_t(s)].role != w.new_value) replaced = true;
            continue;
        }
        if (value_in(now, w.kind, -1, 0) != w.new_value) replaced = true;
    }
    if (!replaced) return "";
    const EnumLabel* bu = build_up_style(now.build_up);
    const EnumLabel* ap = defensive_approach(now.approach);
    return "the game replaced your tactic (LH " + std::to_string(now.line_height) + " / " +
           (bu ? std::string(bu->label) : "build-up " + std::to_string(now.build_up)) + " / " +
           (ap ? std::string(ap->label) : "approach " + std::to_string(now.approach)) + "). Re-apply?";
}

std::string other_tactic_note(const LiveTactic& now, const UndoRecord& u) {
    if (u.state != "written" || u.team != now.at.user || same_tactic_record(u, now)) return "";
    return "the active tactic is another one now (record " + std::to_string(now.at.record_index) + ", formation " + std::to_string(now.formation) +
           "; Turbo wrote record " + std::to_string(u.record_index) + ", formation " + std::to_string(u.formation) +
           "): Turbo's values are not re-applied to it";
}

std::string reapply_check(const LiveTactic& now, const UndoRecord& u, const std::string& session, long long load_gen) {
    const std::string sc = same_career(u, session, load_gen);
    if (!sc.empty()) return "the prompt belongs to another career load: " + sc;
    if (u.team != now.at.user) return "the write was for club " + std::to_string(u.team) + ", the live tactic is club " + std::to_string(now.at.user) + "'s";
    if (!same_tactic_record(u, now)) return other_tactic_note(now, u);
    if (replaced_prompt(now, u).empty()) return "Turbo's values are in place again: nothing to re-apply";
    return "";
}

Request reapply_request(const LiveTactic& now, const UndoRecord& u, std::vector<std::string>& skipped) {
    Request r;
    skipped.clear();
    for (const FieldWrite& w : u.writes) {
        if (w.copy) continue;
        switch (w.kind) {
            case FieldKind::LineHeight: r.line_height = w.new_value; break;
            case FieldKind::BuildUp: r.build_up = w.new_value; break;
            case FieldKind::Approach: r.approach = w.new_value; break;
            case FieldKind::Role: {
                const int s = w.slot;
                if (s < 0 || s >= kSlots) break;
                const SlotView& v = now.slots[size_t(s)];
                if (v.pid != u.pids[size_t(s)]) {
                    skipped.push_back("slot " + std::to_string(s) + " holds player " + std::to_string(v.pid) + " now, not " + std::to_string(u.pids[size_t(s)]));
                    break;
                }
                if (v.group() != role_group(w.new_value)) {
                    skipped.push_back("slot " + std::to_string(s) + " is position group " + std::to_string(v.group()) + " now");
                    break;
                }
                r.slots.push_back({s, role_id_of(w.new_value), focus_of(w.new_value)});
                break;
            }
            case FieldKind::RoleId:
            case FieldKind::Focus: break;  // the word carries them
        }
    }
    return r;
}

// ------------------------------------------------------------------------------------------------ the cm_mentalities mirror's checks
std::string mirror_field_name(const FieldWrite& w) {
    if (w.copy) return "";  // the working copies have no row of their own
    switch (w.kind) {
        case FieldKind::BuildUp: return "buildupplay";
        case FieldKind::LineHeight: return "defensivedepth";
        case FieldKind::Role: return "pos" + std::to_string(w.slot) + "role";
        case FieldKind::Approach:  // cm_mentalities has no column for it (turbo\le27\fc27_db_schema.json)
        case FieldKind::RoleId:
        case FieldKind::Focus: return "";
    }
    return "";
}

std::string mirror_row_check(const std::function<bool(const std::string&, int64_t&)>& get, const LiveTactic& now, const std::vector<FieldWrite>& writes,
                             const std::function<bool(const std::string&, std::string&)>& get_str) {
    for (int s = 0; s < kSlots; ++s) {
        const std::string pf = "playerid" + std::to_string(s), qf = "position" + std::to_string(s);
        int64_t pid = 0, pos = 0;
        if (!get(pf, pid) || !get(qf, pos)) return "this save's cm_mentalities has no " + pf + " / " + qf + ": the row cannot be matched to the live slots";
        if (pid != now.slots[size_t(s)].pid)
            return pf + " is " + std::to_string(pid) + ", the live slot " + std::to_string(s) + " holds player " + std::to_string(now.slots[size_t(s)].pid);
        if (pos != now.slots[size_t(s)].position)
            return qf + " is " + std::to_string(pos) + ", the live slot " + std::to_string(s) + " is position " + std::to_string(now.slots[size_t(s)].position);
    }
    std::string code;
    if (get_str && !now.code.empty() && get_str("code", code) && !code.empty() && code != now.code)
        return "its code is " + code + ", the live record's " + now.code + ": the row is not the live tactic";
    for (const FieldWrite& w : writes) {
        const std::string name = mirror_field_name(w);
        if (name.empty()) continue;
        int64_t v = 0;
        if (!get(name, v)) return "this save's cm_mentalities has no field " + name;
        if (v != w.old_value)
            return name + " is " + std::to_string(v) + " in the database, the live value before the write was " + std::to_string(w.old_value) +
                   ": the row is not the live tactic";
    }
    return "";
}

std::vector<DbRowField> apply_row_fields(const Plan& plan, const std::function<bool(const std::string&, int64_t&)>& get) {
    std::vector<DbRowField> out;
    for (const FieldWrite& w : plan.writes) {
        const std::string name = mirror_field_name(w);
        if (name.empty()) continue;
        int64_t cur = 0;
        if (!get(name, cur) || cur == w.new_value) continue;
        out.push_back({name, cur, w.new_value});
    }
    return out;
}

std::string undo_row_check(const Switches& sw, const std::function<bool(const std::string&, int64_t&)>& get, const UndoRecord& u) {
    const std::string block = sw.db_row_block();
    if (!block.empty()) return block;
    int64_t team = -1, active = -1, src = -1, key = -1;
    const bool ok = get("teamid", team) && get("activetactic", active) && get("sourceformationid", src) && get("artificialkey", key);
    if (!ok || team != u.team || active != 1 || src != u.source_formation || key != u.mirror_row_key)
        return "row " + std::to_string(u.mirror_row_key) + " is not club " + std::to_string(u.team) + "'s active tactic with sourceformationid " +
               std::to_string(u.source_formation) + " (teamid " + std::to_string(team) + ", activetactic " + std::to_string(active) + ", sourceformationid " +
               std::to_string(src) + ", artificialkey " + std::to_string(key) + ")";
    return "";
}

std::vector<DbRowField> undo_row_fields(const UndoRecord& u, const Plan& undo_plan, const std::function<bool(const std::string&, int64_t&)>& get,
                                        std::vector<std::string>& left) {
    std::vector<DbRowField> out;
    left.clear();
    std::set<std::string> live_back;
    for (const FieldWrite& w : undo_plan.writes) {
        const std::string n = mirror_field_name(w);
        if (!n.empty()) live_back.insert(n);
    }
    for (const MirrorField& m : u.mirror) {
        if (!live_back.count(m.field)) {
            left.push_back(m.field + " (its live field is not put back)");
            continue;
        }
        int64_t cur = 0;
        if (!get(m.field, cur)) {
            left.push_back(m.field + " (not readable)");
            continue;
        }
        if (cur != m.new_value) {
            left.push_back(m.field + " is " + std::to_string(cur) + ", not Turbo's " + std::to_string(m.new_value));
            continue;
        }
        if (cur != m.old_value) out.push_back({m.field, cur, m.old_value});
    }
    return out;
}

void add_job_rows(UndoRecord& u, const std::vector<DbRowWrite>& rows, bool undo) {
    if (rows.empty()) return;
    if (undo) {
        u.mirror_state = "restored";
        return;
    }
    const DbRowWrite& row = rows.front();  // one cm_mentalities row per job
    if (u.mirror_row_key != row.key || u.mirror_table != row.table.name) u.mirror.clear();  // another row: its fields start over
    u.mirror_table = row.table.name;
    u.mirror_row_key = row.key;
    u.mirror_state = "written";
    u.mirror_note.clear();
    for (const DbRowField& f : row.fields) {
        auto it = std::find_if(u.mirror.begin(), u.mirror.end(), [&](const MirrorField& m) { return m.field == f.name; });
        if (it == u.mirror.end()) u.mirror.push_back({f.name, f.old_value, f.new_value});
        else {
            if (it->new_value != f.old_value) it->old_value = f.old_value;  // the game's save wrote another value since: that is what was replaced
            it->new_value = f.new_value;
        }
    }
}

// ------------------------------------------------------------------------------------------------ the kick-off check
std::vector<uint8_t> role_pattern(const std::array<int32_t, kSlots>& roles) {
    std::vector<uint8_t> p(4 * kSlots);
    for (int s = 0; s < kSlots; ++s) std::memcpy(p.data() + 4 * s, &roles[size_t(s)], 4);
    return p;
}

std::vector<uint64_t> find_bytes(const uint8_t* buf, size_t len, uint64_t base, const std::vector<uint8_t>& pat, size_t max_hits, size_t align) {
    std::vector<uint64_t> hits;
    if (!buf || pat.empty() || len < pat.size() || max_hits == 0) return hits;
    if (align == 0) align = 1;
    const uint8_t* p = buf;
    const uint8_t* end = buf + len - pat.size() + 1;
    while (p < end) {
        const void* f = std::memchr(p, pat[0], size_t(end - p));
        if (!f) break;
        const uint8_t* q = static_cast<const uint8_t*>(f);
        const uint64_t at = base + uint64_t(q - buf);
        if (at % align == 0 && std::memcmp(q, pat.data(), pat.size()) == 0) {
            hits.push_back(at);
            if (hits.size() >= max_hits) break;
        }
        p = q + 1;
    }
    return hits;
}

const std::vector<CopyField>& match_copy_fields() {
    static const std::vector<CopyField> v = {
        {"LH", 0x158, 1, true},
        {"BU", 0x15B, 1, true},
        {"DA", 0x15C, 1, true},
    };
    return v;
}

const char* match_copy_fields_source() {
    return "T2 static decode, old build: record +0xA4/+0xB0/+0xB4 -> +0x158/+0x15B/+0x15C; [A] here, a line naming Turbo's values proves it";
}

bool is_match_copy_at(Memory& mem, uint64_t out, int user, const std::array<int32_t, kSlots>& pids) {
    if (!is_ptr(out, 4)) return false;
    int32_t team = 0;
    if (!mem.rd(out + kCopyTeam, team) || team != user) return false;
    for (int s = 0; s < kSlots; ++s) {
        int32_t pid = 0, pos = -1;
        if (!mem.rd(out + kCopyPids + 4 * uint64_t(s), pid) || pid != pids[size_t(s)]) return false;
        if (!mem.rd(out + kCopyPositions + 4 * uint64_t(s), pos) || pos < 0 || pos > kMaxPosition || (s == 0 && pos != 0)) return false;
    }
    return true;
}

bool is_match_copy(Memory& mem, uint64_t hit, int user, const std::array<int32_t, kSlots>& pids) {
    if (hit < kCopyRoles) return false;
    return is_match_copy_at(mem, hit - kCopyRoles, user, pids);
}

std::string window_hex(const std::vector<uint8_t>& b) {
    std::string s;
    char x[4];
    for (size_t i = 0; i < b.size(); ++i) {
        std::snprintf(x, sizeof(x), "%02x", unsigned(b[i]));
        s += (i ? " " : "") + std::string(x);
    }
    return s;
}

KickoffRead decode_match_copy(const KickoffCopy& c, const KickoffArm& arm, const std::vector<CopyField>& fields) {
    KickoffRead r;
    r.out = c.out;
    auto i32 = [&](size_t o) {
        int32_t x = 0;
        if (o + 4 <= c.bytes.size()) std::memcpy(&x, c.bytes.data() + o, 4);
        return x;
    };
    r.team = i32(kCopyTeam);
    for (int s = 0; s < kSlots; ++s) {
        r.roles[size_t(s)] = i32(kCopyRoles + 4 * size_t(s));
        if (r.roles[size_t(s)] == arm.roles[size_t(s)]) ++r.roles_match;
    }
    for (const CopyField& f : fields) {
        if (f.width < 1 || f.width > 4 || size_t(f.offset) + f.width > c.bytes.size()) continue;
        // the raw bytes at the table's offset, known or not (X2 reads them off the line)
        char b[48];
        std::snprintf(b, sizeof(b), "%s +0x%X=", f.name, unsigned(f.offset));
        std::string raw = b;
        for (uint32_t k = 0; k < f.width; ++k) {
            std::snprintf(b, sizeof(b), "%02x", unsigned(c.bytes[f.offset + k]));
            raw += b;
        }
        r.raw += (r.raw.empty() ? "" : " ") + raw + (f.known ? "" : " (not decoded)");
        if (!f.known) continue;
        uint32_t v = 0;
        for (uint32_t k = 0; k < f.width; ++k) v |= uint32_t(c.bytes[f.offset + k]) << (8 * k);
        const int value = int(v);
        if (std::strcmp(f.name, "LH") == 0) r.lh = value;
        else if (std::strcmp(f.name, "BU") == 0) r.bu = value;
        else if (std::strcmp(f.name, "DA") == 0) r.da = value;
    }
    if (kCopyStyleWindow + kCopyStyleWindowLen <= c.bytes.size())
        r.window.assign(c.bytes.begin() + long(kCopyStyleWindow), c.bytes.begin() + long(kCopyStyleWindow + kCopyStyleWindowLen));
    const std::vector<size_t> at = kickoff_byte_offsets(c.bytes, arm.line_height);
    r.lh_bytes.assign(at.begin(), at.begin() + long(std::min<size_t>(at.size(), 6)));
    return r;
}

std::string kickoff_verdict(const KickoffArm& arm, const std::vector<KickoffRead>& reads, bool& ok) {
    ok = false;
    if (reads.empty()) return "= unknown (no copy)";
    std::vector<std::string> undecoded;
    for (const KickoffRead& r : reads) {
        if (r.lh < 0 && std::find(undecoded.begin(), undecoded.end(), "LH") == undecoded.end()) undecoded.push_back("LH");
        if (r.bu < 0 && std::find(undecoded.begin(), undecoded.end(), "BU") == undecoded.end()) undecoded.push_back("BU");
        if (r.da < 0 && std::find(undecoded.begin(), undecoded.end(), "DA") == undecoded.end()) undecoded.push_back("DA");
    }
    // what differs from the values looked for (Turbo's, or the hub's read when no Turbo write is in place), per copy
    std::string diff;
    for (size_t i = 0; i < reads.size(); ++i) {
        const KickoffRead& r = reads[i];
        std::string d;
        if (r.lh >= 0 && r.lh != arm.line_height) d += (d.empty() ? "" : ", ") + std::string("LH ") + std::to_string(r.lh) + " not " + std::to_string(arm.line_height);
        if (r.bu >= 0 && r.bu != arm.build_up) d += (d.empty() ? "" : ", ") + std::string("BU ") + std::to_string(r.bu) + " not " + std::to_string(arm.build_up);
        if (r.da >= 0 && r.da != arm.approach) d += (d.empty() ? "" : ", ") + std::string("DA ") + std::to_string(r.da) + " not " + std::to_string(arm.approach);
        if (r.roles_match != kSlots) d += (d.empty() ? "" : ", ") + std::string("roles ") + std::to_string(r.roles_match) + "/11";
        if (!d.empty()) diff += (diff.empty() ? "" : "; ") + (reads.size() > 1 ? "copy " + std::to_string(i + 1) + ": " : std::string()) + d;
    }
    if (!diff.empty()) return std::string("= game (differs") + (arm.turbo_values ? "" : " from the hub's read") + ": " + diff + ")";
    if (!undecoded.empty()) {
        std::string u;
        for (const auto& x : undecoded) u += (u.empty() ? "" : "/") + x;
        return "= unknown (" + u + " not decoded: see the raw window)";
    }
    if (!arm.turbo_values) return "= game (the hub's values; no Turbo write in place)";
    ok = true;
    return "= Turbo (ok)";
}

std::string kickoff_line(const KickoffArm& arm, const std::vector<KickoffRead>& reads, const std::string& details) {
    if (reads.empty()) return "channel E kick-off: no match copy" + (details.empty() ? std::string() : " | " + details);
    const KickoffRead& r = reads.front();
    auto num = [](int v) { return v < 0 ? std::string("?") : std::to_string(v); };
    bool ok = false;
    const std::string v = kickoff_verdict(arm, reads, ok);
    return "channel E kick-off: match copy team " + std::to_string(r.team) + " LH " + num(r.lh) + " BU " + num(r.bu) + " DA " + num(r.da) + " roles " +
           std::to_string(r.roles_match) + "/11 " + v + (details.empty() ? std::string() : " | " + details);
}

std::vector<size_t> kickoff_value_offsets(const std::vector<uint8_t>& bytes, int32_t v) {
    // the known blocks of the match-setup copy (report 2.12): 11 positions +0x30, x / y and the other float pairs +0x5C..+0x118, the role
    // words +0x118, the pids +0x168
    auto known = [](size_t o) { return (o >= 0x30 && o < 0x144) || (o >= kCopyPids && o < kCopyPids + 4 * kSlots); };
    std::vector<size_t> out;
    for (size_t o = 0; o + 4 <= bytes.size(); o += 4) {
        if (known(o)) continue;
        int32_t x = 0;
        std::memcpy(&x, bytes.data() + o, 4);
        if (x == v) out.push_back(o);
    }
    return out;
}

std::vector<size_t> kickoff_byte_offsets(const std::vector<uint8_t>& bytes, int v) {
    auto known = [](size_t o) { return (o >= 0x30 && o < 0x144) || (o >= kCopyPids && o < kCopyPids + 4 * kSlots); };
    std::vector<size_t> out;
    if (v < 0 || v > 255) return out;
    for (size_t o = 0; o < bytes.size(); ++o)
        if (!known(o) && bytes[o] == uint8_t(v)) out.push_back(o);
    return out;
}

std::string kickoff_index(const KickoffArm& arm, const std::vector<KickoffCopy>& copies, size_t hits) {
    auto i32 = [](const std::vector<uint8_t>& b, size_t o) {
        int32_t x = 0;
        if (o + 4 <= b.size()) std::memcpy(&x, b.data() + o, 4);
        return x;
    };
    auto list = [&](const std::vector<uint8_t>& b, size_t o) {
        std::string s;
        for (int k = 0; k < kSlots; ++k) s += (k ? " " : "") + std::to_string(i32(b, o + 4 * size_t(k)));
        return s;
    };
    std::ostringstream o;
    o << "Turbo channel E kick-off dump (read-only). " << copies.size() << " match-setup cop" << (copies.size() == 1 ? "y" : "ies") << " of " << hits
      << " hit(s) of the 11 role words; each block in " << kickoff_dump_file() << " is 0x" << std::hex << (kKickoffHead + kKickoffTail) << std::dec
      << " bytes from out+0x00 (0x" << std::hex << kKickoffHead << " bytes of the known layout + the 0x" << kKickoffTail << " bytes after it)"
      << std::dec << ", in this order.\n";
    o << "Looked for (" << arm.when << "): club " << arm.user << ", tactic code " << (arm.code.empty() ? "?" : arm.code) << ", Line Height "
      << arm.line_height << ", Build-Up Style " << build_up_text(arm.build_up) << ", Defensive Approach " << approach_text(arm.approach)
      << (arm.turbo_values ? " (Turbo's values)" : " (the game's values: no Turbo write in place)") << "\n";
    o << "roles " << [&] {
        std::string s;
        for (int k = 0; k < kSlots; ++k) s += (k ? " " : "") + std::to_string(arm.roles[size_t(k)]);
        return s;
    }() << "\n";
    std::vector<size_t> common;
    for (size_t i = 0; i < copies.size(); ++i) {
        const KickoffCopy& c = copies[i];
        const std::vector<size_t> at = kickoff_value_offsets(c.bytes, arm.line_height);
        if (i == 0) common = at;
        else {
            std::vector<size_t> keep;
            for (size_t x : common)
                if (std::find(at.begin(), at.end(), x) != at.end()) keep.push_back(x);
            common = keep;
        }
        o << "\nblock " << i << ": out 0x" << std::hex << std::uppercase << c.out << std::nouppercase << std::dec << " (file offset 0x" << std::hex
          << i * (kKickoffHead + kKickoffTail) << std::dec << ")\n";
        o << "  +0x1C formationfullnameid " << i32(c.bytes, 0x1C) << ", +0x20 / +0x24 sourceformationid " << i32(c.bytes, 0x20) << " / " << i32(c.bytes, 0x24)
          << ", +0x28 team " << i32(c.bytes, kCopyTeam) << ", +0x2C audio " << i32(c.bytes, 0x2C) << "\n";
        o << "  +0x30 positions " << list(c.bytes, 0x30) << "\n  +0x118 roles " << list(c.bytes, kCopyRoles) << "\n  +0x168 pids " << list(c.bytes, kCopyPids)
          << "\n";
        const KickoffRead d = decode_match_copy(c, arm);
        o << "  decode (match_copy_fields):";
        for (const CopyField& f : match_copy_fields())
            o << " " << f.name << " +0x" << std::hex << std::uppercase << f.offset << std::nouppercase << std::dec << " = "
              << (std::strcmp(f.name, "LH") == 0 ? d.lh : std::strcmp(f.name, "BU") == 0 ? d.bu : d.da);
        o << ", roles " << d.roles_match << "/11\n  +0x" << std::hex << std::uppercase << kCopyStyleWindow << std::nouppercase << std::dec
          << " window: " << window_hex(d.window) << "\n";
        o << "  i32 == Line Height " << arm.line_height << " outside the known blocks at:";
        if (at.empty()) o << " none";
        for (size_t x : at) o << " +0x" << std::hex << std::uppercase << x << std::nouppercase << std::dec;
        o << "\n  u8 == Line Height " << arm.line_height << " outside the known blocks at:";
        const std::vector<size_t> bt = kickoff_byte_offsets(c.bytes, arm.line_height);
        if (bt.empty()) o << " none";
        for (size_t i = 0; i < bt.size() && i < 24; ++i) o << " +0x" << std::hex << std::uppercase << bt[i] << std::nouppercase << std::dec;
        if (bt.size() > 24) o << " ...";
        o << "\n";
    }
    o << "\nLine Height " << arm.line_height << " at the same offset in every block:";
    if (common.empty() || copies.empty()) o << " none";
    for (size_t x : common) o << " +0x" << std::hex << std::uppercase << x << std::nouppercase << std::dec;
    o << "\n";
    return o.str();
}

// ------------------------------------------------------------------------------------------------ gates
bool Gates::pass() const {
    for (const Gate& g : list)
        if (!g.pass()) return false;
    return true;
}

std::string Gates::first() const {
    for (const Gate& g : list)
        if (!g.pass()) return g.name + ": " + g.why;
    return "";
}

Gates evaluate(const GateInput& in) {
    Gates gs;
    // G1
    {
        Gate g{"G1 switches", ""};
        const std::string block = in.sw.blocking();
        if (!in.undo && !block.empty()) g.why = "kill switch turbo_output\\" + block + " is present";
        else if (!in.undo && !in.sw.opt_in) g.why = std::string("off until turbo_output\\") + opt_in_file() + " exists (opt-in)";
        gs.list.push_back(g);
    }
    // G2
    {
        Gate g{"G2 career hub", ""};
        if (!in.bridge) g.why = "Turbo's Lua side has not reported the career yet";
        else if (!in.in_cm) g.why = "not in a career";
        else if (in.match_phase.empty()) g.why = "this Lua side does not report the match phase: update Turbo's Lua files";
        else if (in.match_phase == "unknown") g.why = "the match phase is not known yet (no hub, match or day event seen since Turbo started): advance a day or reload the career";
        else if (in.match_phase != "hub")
            g.why = "a match is being set up or played (Lua: " + in.match_phase + "); apply in the hub before the pre-match screen (after leaving the "
                    "pre-match screen: the next hub event, e.g. a day passed or the match completed)";
        else if (in.team_mgmt)
            g.why = "Team Management is open (Lua saw ENTERING_TEAM_MANAGEMENT): leave it first (answer No if the game asks to save your changes to "
                    "your team sheet: Yes autosaves the career), then apply in the hub";
        else if (in.save_age >= 0 && in.save_age < kSaveQuietSeconds) g.why = "a save started " + std::to_string(in.save_age) + " s ago: wait a moment";
        else if (in.load_gen < 0) g.why = "this Lua side does not report career loads: update Turbo's Lua files";
        else if (in.located_load_gen != in.load_gen) g.why = "the career was (re)loaded since the service was located: locating again";
        gs.list.push_back(g);
    }
    gs.list.push_back({"G3 build and anchors", in.anchors});
    gs.list.push_back({"G4 locate", in.locate});
    gs.list.push_back({"G5 no propagation", in.g5});
    gs.list.push_back({"G6 values", in.plan});
    gs.list.push_back({"session", in.undo ? std::string() : in.session_off});  // an undo still puts Turbo's values back
    return gs;
}

// ------------------------------------------------------------------------------------------------ the service
CoreService::CoreService(Host h) : h_(std::move(h)) {}

void CoreService::log(const std::string& s) {
    if (h_.log) h_.log(s);
}

std::string CoreService::anchors_status() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!anchors_checked_) {
        anchors_why_ = h_.mem ? check_anchors(*h_.mem, h_.anchors) : "no game memory";
        anchors_checked_ = true;
        log(anchors_why_.empty() ? "channel E: anchors ok on build " + h_.anchors.build + " (VT1 " + hex(h_.anchors.vt1) + ", SetSheet " + hex(h_.anchors.set) +
                                       ", UpdateSheet " + hex(h_.anchors.update) + ", registry " + hex(h_.anchors.registry) + ")"
                                 : "channel E: not available on this build (" + anchors_why_ + ")");
    }
    return anchors_why_;
}

uint64_t CoreService::located() {
    std::lock_guard<std::mutex> lock(mu_);
    return svc_;
}

long long CoreService::located_load_gen() {
    std::lock_guard<std::mutex> lock(mu_);
    return svc_ ? svc_gen_ : -1;
}

std::string CoreService::locate_status() {
    std::lock_guard<std::mutex> lock(mu_);
    if (svc_) return "located " + hex(svc_) + " (" + how_ + ", career load " + std::to_string(svc_gen_) + ")";
    if (cand_svc_)
        return "checking the object the scan found at " + hex(cand_svc_) + " again " + std::to_string(int(kScanConfirmUs / 1e6)) +
               " s after the scan (a freed copy keeps its header for a while) ...";
    if (locating_) return "locating the team sheet service (career load " + std::to_string(locating_gen_) + ") ...";
    if (!locate_why_.empty()) return "not located: " + locate_why_;
    return "not located yet";
}

void CoreService::relocate() {
    std::lock_guard<std::mutex> lock(mu_);
    svc_ = 0;
    svc_gen_ = -1;
    failed_gen_ = -1;  // the next read locates at once
    cand_svc_ = 0;
    cand_gen_ = -1;
    locate_why_.clear();
}

std::string CoreService::session_off() {
    std::lock_guard<std::mutex> lock(mu_);
    return session_off_;
}

bool CoreService::busy() {
    std::lock_guard<std::mutex> lock(mu_);
    if (busy_ && h_.clock_us && now() - busy_since_ > kJobLostUs) {
        if (cancel_) cancel_->store(true);  // given up: if the game thread still runs it, it writes nothing
        return false;
    }
    return busy_;
}

void CoreService::note_context(const Context& ctx) {
    cur_gen_.store(ctx.load_gen);
    cur_hub_.store(ctx.hub);
    std::string line;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (ctx.user > 0) user_ = ctx.user;
        if (ctx.phase != phase_) {
            const std::string was = phase_.empty() ? std::string("(none)") : phase_;
            phase_ = ctx.phase;
            if (phase_ == "match") {
                // the kick-off check: 20 s from now, on the game_tick listener (kickoff_tick), whether or not the card is drawn
                kick_pending_ = true;
                kick_due_ = now() + kKickoffDelayUs;
                kick_tries_ = 0;
                line = "channel E: Lua phase " + was + " -> match: kick-off check in " + std::to_string(int(kKickoffDelayUs / 1e6)) + " s (" +
                       (arm_.armed ? "the tactic " + arm_.when + ": Line Height " + std::to_string(arm_.line_height) + ", Build-Up Style " +
                                         build_up_text(arm_.build_up) + ", Defensive Approach " + approach_text(arm_.approach)
                                   : std::string("no live tactic was read in the hub before")) +
                       ")";
            } else {
                kick_pending_ = false;
                line = "channel E: Lua phase " + was + " -> " + (phase_.empty() ? std::string("(none)") : phase_);
            }
        }
    }
    if (!line.empty()) log(line);
}

std::shared_ptr<const RoleAllowList> CoreService::role_allow(long long load_gen) {
    std::lock_guard<std::mutex> lock(mu_);
    return allow_gen_ == load_gen ? allow_ : nullptr;
}

std::string CoreService::allow_status() {
    std::lock_guard<std::mutex> lock(mu_);
    if (allow_) return "role allow-list: " + allow_->summary() + " (career load " + std::to_string(allow_gen_) + ")";
    if (allow_running_) return "building the role allow-list ...";
    if (!allow_why_.empty()) return "role allow-list not built: " + allow_why_ + " (role and focus are not editable)";
    return "role allow-list not built yet (the service is not located)";
}

void CoreService::start_allow(long long gen, uint64_t svc) {
    int user = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (allow_gen_ == gen && (allow_ || allow_running_)) return;  // once per career load (a failed walk is tried again on the next locate)
        allow_gen_ = gen;
        allow_running_ = true;
        allow_.reset();
        allow_why_.clear();
        user = user_;
    }
    const Anchors a = h_.anchors;
    auto job = [this, gen, svc, user, a]() {
        RoleAllowList l;
        try {
            l = build_role_allow_list(*h_.mem, svc, a, user, h_.clock_us);
        } catch (const std::exception& e) {
            l = RoleAllowList();
            l.why = std::string("the walk failed: ") + e.what();
        } catch (...) {
            l = RoleAllowList();
            l.why = "the walk failed";
        }
        log(l.ok ? "channel E: role allow-list: " + l.summary() + " (" + std::to_string(l.sheets) + " sheets, " + std::to_string(l.records) + " records, " +
                       std::to_string(l.slots) + " slots; your club " + std::to_string(user) + " left out, " + std::to_string(l.skipped) + " club(s) skipped); "
                       "other clubs' tactics (records with a formation): " + l.values_summary()
                 : "channel E: role allow-list not built: " + l.why + " (role and focus are not editable; build-up and depth are)");
        std::lock_guard<std::mutex> lock(mu_);
        if (gen != allow_gen_) return;
        allow_running_ = false;
        if (l.ok) allow_ = std::make_shared<const RoleAllowList>(std::move(l));
        else allow_why_ = l.why;
    };
    if (h_.run_background) h_.run_background(job);
    else job();
}

void CoreService::finish_locate(long long gen, uint64_t svc, const std::string& how, const std::string& why) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (gen != locating_gen_) return;  // a newer career load asked again meanwhile (its locate is still running)
        locating_ = false;
        cand_svc_ = 0;
        cand_gen_ = -1;
        if (svc) {
            svc_ = svc;
            svc_gen_ = gen;
            how_ = how;
            failed_gen_ = -1;
            locate_why_.clear();
        } else {
            // not retried for this career load before kLocateRetryUs (the scan reads every private region: once per load, not every second)
            failed_gen_ = gen;
            failed_at_ = now();
            locate_why_ = why;
        }
    }
    if (svc) start_allow(gen, svc);
}

void CoreService::scan_for_service(long long gen) {
    const Anchors a = h_.anchors;
    auto job = [this, gen, a]() {
        std::vector<uint64_t> hits;
        std::string why;
        try {
            const std::vector<Region> regs = h_.regions ? h_.regions() : std::vector<Region>();
            const size_t chunk = 1u << 20;
            std::vector<uint8_t> buf;
            const double t0 = h_.clock_us ? h_.clock_us() : 0.0;
            bool timed_out = false;
            auto add = [&](uint64_t hpos) {
                if (hits.size() < kScanMaxHits && std::find(hits.begin(), hits.end(), hpos) == hits.end()) hits.push_back(hpos);
            };
            for (const Region& g : regs) {
                for (uint64_t at = g.start; at < g.end && hits.size() < kScanMaxHits;) {
                    const size_t n = size_t(std::min<uint64_t>(chunk + 8, g.end - at));
                    if (n >= 16 && h_.mem->read_block(at, n, buf)) {
                        for (uint64_t hpos : find_vtable_pairs(buf.data(), n, at, a.vt1, a.vt2, kScanMaxHits)) add(hpos);
                    } else {
                        // an unreadable chunk: its 64 KB parts
                        for (uint64_t s = at; s < at + n && s < g.end; s += 0x10000) {
                            const size_t m = size_t(std::min<uint64_t>(0x10000 + 8, g.end - s));
                            if (m >= 16 && h_.mem->read_block(s, m, buf))
                                for (uint64_t hpos : find_vtable_pairs(buf.data(), m, s, a.vt1, a.vt2, kScanMaxHits)) add(hpos);
                        }
                    }
                    at += chunk;
                    if (h_.clock_us && h_.clock_us() - t0 > h_.scan_budget_s * 1e6) {
                        timed_out = true;
                        break;
                    }
                }
                if (timed_out) break;
            }
            uint64_t svc = 0;
            // fail closed: a scan that did not see all of memory, or hit its cap, may have missed the live service among the copies
            if (timed_out)
                why = "the scan ran out of time (" + std::to_string(int(h_.scan_budget_s)) + " s) with " + std::to_string(hits.size()) +
                      " vtable-pair hit(s): not located (fail closed)";
            else if (hits.size() >= kScanMaxHits)
                why = std::to_string(hits.size()) + " or more vtable-pair hits (the scan's cap): not located (fail closed)";
            else
                why = pick_service(*h_.mem, hits, a, svc);
            ++scans_;
            if (svc) {
                {
                    std::lock_guard<std::mutex> lock(mu_);
                    if (gen != locating_gen_) return;
                    cand_svc_ = svc;
                    cand_gen_ = gen;
                    cand_at_ = now();
                }
                log("channel E: the vtable-pair scan picked " + hex(svc) + " (" + std::to_string(hits.size()) + " hit(s), the others failed the header checks): " +
                    "checked again in " + std::to_string(int(kScanConfirmUs / 1e6)) + " s before it is used");
                return;  // still "locating" until read() re-checks it
            }
            log("channel E: service not located by the scan: " + why);
            finish_locate(gen, 0, "", why);
        } catch (const std::exception& e) {
            finish_locate(gen, 0, "", std::string("scan failed: ") + e.what());
        } catch (...) {
            finish_locate(gen, 0, "", "scan failed");
        }
    };
    if (h_.run_background) h_.run_background(job);
    else job();
}

void CoreService::start_locate(const Context& ctx) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        const double t = now();
        if (locating_ && locating_gen_ == ctx.load_gen) {
            if (!h_.clock_us || t - locating_since_ <= kLocateLostUs) return;
            log("channel E: the locate of career load " + std::to_string(ctx.load_gen) + " did not finish in " + std::to_string(int(kLocateLostUs / 1e6)) +
                " s (the registry Find job was not run?): locating again");
        } else if (!locating_ && failed_gen_ == ctx.load_gen && (!h_.clock_us || t - failed_at_ < kLocateRetryUs)) {
            return;  // this career load's last locate failed a moment ago: "Locate again" (relocate) or the next career load tries again
        }
        locating_ = true;
        locating_gen_ = ctx.load_gen;
        locating_since_ = t;
        cand_svc_ = 0;
        cand_gen_ = -1;
        locate_why_.clear();
    }
    const long long gen = ctx.load_gen;
    // preferred: the registry's Find on the game thread (the call the match setup makes), only with the opt-in, in the hub, unless off
    std::string why;
    const bool find_off = !h_.out_dir.empty() && file_exists(h_.out_dir / find_off_file());
    const uint64_t fn = (h_.call_find && !find_off && ctx.allow_find && ctx.hub) ? registry_find_fn(*h_.mem, h_.anchors, why) : 0;
    if (fn && h_.run_on_game_thread) {
        const Anchors a = h_.anchors;
        const double queued = now();
        const std::string via = h_.run_on_game_thread([this, gen, fn, a, queued]() {
            uint64_t returned = 0, svc = 0;
            std::string w;
            if (h_.clock_us && now() - queued > kJobDeadlineUs) {
                w = "the game thread ran the Find job late";
            } else if (!cur_hub_.load() || cur_gen_.load() != gen) {
                w = "not in the career hub of this career load any more";  // G2 again: the UI's latest view
            } else {
                try {
                    ++finds_;
                    returned = h_.call_find(fn, a.registry, kServiceId);
                    w = accept_find_result(*h_.mem, returned, a, svc);
                } catch (...) {
                    w = "the registry Find threw";
                }
            }
            if (svc) {
                log("channel E: service located by the registry Find at " + hex(svc) + " (Find returned " + hex(returned) + ")");
                finish_locate(gen, svc, "registry Find", "");
            } else {
                log("channel E: registry Find not used (" + w + "): scanning instead");
                scan_for_service(gen);
            }
        });
        if (!via.empty()) return;
        log("channel E: no game-thread dispatcher for the registry Find: scanning instead");
    } else if (find_off) {
        log(std::string("channel E: registry Find off (turbo_output\\") + find_off_file() + "): scanning");
    } else if (!ctx.allow_find) {
        log("channel E: registry Find not called (channel E is not switched on): locating by the read-only scan");
    } else if (!ctx.hub) {
        log("channel E: registry Find not called outside the career hub: locating by the read-only scan");
    } else if (!why.empty()) {
        log("channel E: registry Find not used (" + why + "): scanning");
    }
    scan_for_service(gen);
}

bool CoreService::read(const Context& ctx, LiveTactic& out, std::string& why) {
    note_context(ctx);
    why = anchors_status();
    if (!why.empty()) return false;
    if (!ctx.in_cm || ctx.user <= 0) {
        why = "not in a career";
        return false;
    }
    // L2's user: the game's PlayerStatusManager must name the same club as Lua's bridge
    {
        int game_user = 0;
        std::string w = user_from_psm(*h_.mem, ctx.managers, h_.psm_vtable, game_user);
        if (!w.empty()) {
            why = "your club: " + w;
            return false;
        }
        if (game_user != ctx.user) {
            why = "the game's club " + std::to_string(game_user) + " is not the bridge's " + std::to_string(ctx.user);
            return false;
        }
    }
    // the scan's pick, kScanConfirmUs later: located only when it still passes every header check
    {
        uint64_t cand = 0;
        long long cgen = -1;
        double at = 0.0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            cand = cand_svc_;
            cgen = cand_gen_;
            at = cand_at_;
        }
        if (cand && cgen == ctx.load_gen && (!h_.clock_us || now() - at >= kScanConfirmUs)) {
            const std::string w = check_service(*h_.mem, cand, h_.anchors);
            if (w.empty()) {
                log("channel E: service located by the vtable-pair scan at " + hex(cand) + " (it passed its header checks again " +
                    std::to_string(int(kScanConfirmUs / 1e6)) + " s after the scan)");
                finish_locate(cgen, cand, "vtable-pair scan", "");
            } else {
                log("channel E: the object the scan picked at " + hex(cand) + " failed its header checks " + std::to_string(int(kScanConfirmUs / 1e6)) +
                    " s later (a freed copy?): " + w);
                finish_locate(cgen, 0, "",
                              "the object the scan found at " + hex(cand) + " failed its header checks again " + std::to_string(int(kScanConfirmUs / 1e6)) +
                                  " s later (a freed copy?): " + w);
            }
        }
    }
    uint64_t svc = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (svc_ && svc_gen_ == ctx.load_gen) svc = svc_;
    }
    if (!svc) {
        start_locate(ctx);
        std::lock_guard<std::mutex> lock(mu_);
        if (svc_ && svc_gen_ == ctx.load_gen) svc = svc_;
        else {
            why = cand_svc_ ? "checking the service the scan found again (" + std::to_string(int(kScanConfirmUs / 1e6)) + " s after the scan) ..."
                  : locating_ ? "locating the team sheet service ..."
                              : (locate_why_.empty() ? "not located" : locate_why_);
            return false;
        }
    }
    why = read_live(*h_.mem, svc, ctx.user, h_.anchors, out);
    if (why.empty()) out.g5 = check_g5(*h_.mem, out);
    if (why.empty() && ctx.hub) {
        // what the kick-off check of the next match looks for
        std::lock_guard<std::mutex> lock(mu_);
        arm_from(out, ctx.load_gen, "read in the hub");
    }
    if (!why.empty() && !check_service(*h_.mem, svc, h_.anchors).empty()) {
        // the service itself is gone (freed / moved): find it again on the next read
        log("channel E: the located service " + hex(svc) + " failed its header checks (" + why + "): locating again");
        relocate();
    }
    return why.empty();
}

bool CoreService::submit(const Context& ctx, const Plan& plan_in, std::string& why) {
    why.clear();
    note_context(ctx);
    if (!h_.run_on_game_thread) {
        why = "no game-thread dispatcher";
        return false;
    }
    Plan plan = plan_in;
    if (plan.load_gen < 0) plan.load_gen = ctx.load_gen;
    if (plan.load_gen != ctx.load_gen) {
        why = "the plan was made in career load " + std::to_string(plan.load_gen) + ", this is load " + std::to_string(ctx.load_gen);
        return false;
    }
    if (!ctx.hub) {
        why = "not in the career hub (G2)";
        return false;
    }
    uint64_t svc = 0;
    int seq = 0;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::shared_ptr<const RoleAllowList> allow;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!session_off_.empty() && !plan.undo) {
            why = "channel E is off for this session: " + session_off_;
            return false;
        }
        if (busy_) {
            if (h_.clock_us && now() - busy_since_ > kJobLostUs) {
                if (cancel_) cancel_->store(true);
                log("channel E: the last job did not come back in " + std::to_string(int(kJobLostUs / 1e6)) +
                    " s (dropped by the game-thread queue?): given up (cancelled: if it still runs it writes nothing)");
                busy_ = false;
            } else {
                why = "a channel E job is still queued or running";
                return false;
            }
        }
        if (!svc_ || svc_gen_ != ctx.load_gen) {
            why = "the team sheet service is not located for this career load";
            return false;
        }
        svc = svc_;
        busy_ = true;
        busy_since_ = now();
        seq = ++job_seq_;
        cancel_ = std::make_shared<std::atomic<bool>>(false);
        cancel = cancel_;
        if (allow_gen_ == plan.load_gen) allow = allow_;
    }
    const Anchors a = h_.anchors;
    // the switch files once more right before queuing (read here, not on the game thread: no file I/O inside the job, which runs within
    // kJobDeadlineUs of this or writes nothing). An undo puts the game's values back and is not blocked by a kill switch.
    std::string switch_block;
    if (!plan.undo && !h_.out_dir.empty()) {
        if (file_exists(h_.out_dir / master_switch_file())) switch_block = master_switch_file();
        else if (file_exists(h_.out_dir / kill_switch_file())) switch_block = kill_switch_file();
        else if (!file_exists(h_.out_dir / opt_in_file())) switch_block = std::string("no ") + opt_in_file();
    }
    // the database row's switches at the same moment: an Apply or an undo leaves the row out while one of them exists (the live record is
    // still written; the log and the undo file say so)
    if (!plan.rows.empty() && !h_.out_dir.empty()) {
        const std::string db = read_switches(h_.out_dir).db_row_block();
        if (!db.empty()) {
            plan.rows.clear();
            plan.rows_note = db;
        }
    }
    const double queued = now();
    const uint64_t managers = ctx.managers;
    const bool find_ok = ctx.allow_find;
    const std::string via = h_.run_on_game_thread([this, svc, a, plan, switch_block, queued, seq, cancel, allow, managers, find_ok]() {
        ApplyResult r;
        const double t0 = now();
        std::string stage, block;
        if (cancel->load()) {
            stage = "cancelled";
            block = "the job was given up before the game thread ran it";
        } else if (h_.clock_us && t0 - queued > kJobDeadlineUs) {
            char b[96];
            std::snprintf(b, sizeof(b), "the game thread ran the job %.1f s after Apply", (t0 - queued) / 1e6);
            stage = "late";
            block = b;
        } else if (!switch_block.empty()) {
            stage = "G1";
            block = "turbo_output\\" + switch_block;
        } else if (!cur_hub_.load()) {
            stage = "G2";
            block = "not in the career hub any more";
        } else if (cur_gen_.load() != plan.load_gen) {
            stage = "G2";
            block = "the career was reloaded since Apply (load " + std::to_string(cur_gen_.load()) + ", the plan's " + std::to_string(plan.load_gen) + ")";
        } else {
            std::lock_guard<std::mutex> lock(mu_);
            if (svc_ != svc || svc_gen_ != plan.load_gen) {
                stage = "locate";
                block = "the located service changed since Apply";
            }
        }
        // the user again, through the game's PlayerStatusManager
        if (block.empty()) {
            int u = 0;
            const std::string w = user_from_psm(*h_.mem, managers, h_.psm_vtable, u);
            if (!w.empty()) {
                stage = "user";
                block = "your club: " + w;
            } else if (u != plan.user) {
                stage = "user";
                block = "the game's club is " + std::to_string(u) + ", the plan's " + std::to_string(plan.user);
            }
        }
        // the registry hands out the same service (when it may be called)
        if (block.empty() && find_ok && h_.call_find) {
            std::string w;
            const uint64_t fn = registry_find_fn(*h_.mem, a, w);
            if (fn) {
                ++finds_;
                uint64_t again = 0;
                uint64_t returned = 0;
                try {
                    returned = h_.call_find(fn, a.registry, kServiceId);
                    w = accept_find_result(*h_.mem, returned, a, again);
                } catch (...) {
                    w = "the registry Find threw";
                }
                if (!w.empty() || again != svc) {
                    stage = "locate";
                    block = "the registry Find does not return the located service " + hex(svc) + " (" + (w.empty() ? hex(again) : w) + ")";
                }
            }
        }
        if (!block.empty()) {
            r.stage = stage;
            r.message = block + ": nothing written";
            r.log_line = "channel E: " + std::string(plan.undo ? "undo " : "") + "refused (" + stage + "): " + r.message;
            r.undo = plan.undo;
        } else {
            WriteHistory hist;
            {
                std::lock_guard<std::mutex> lock(mu_);
                hist = history_;
            }
            JobChecks chk;
            chk.allow = allow.get();
            chk.history = &hist;
            chk.cancel = cancel.get();
            r = apply_plan(*h_.mem, svc, a, plan, chk);
            if (!plan.undo && (r.written || r.undone || r.restore_failed)) {
                std::lock_guard<std::mutex> lock(mu_);
                history_.note(plan, r.writes, r.rows);
            }
        }
        r.micros = h_.clock_us ? h_.clock_us() - t0 : 0.0;
        std::lock_guard<std::mutex> lock(mu_);
        if (r.session_off) session_off_ = r.message;
        if (r.ok && r.written && r.after.at.record) {
            if (plan.undo) turbo_arm_ = KickoffArm();  // the game's values are back
            arm_from(r.after, plan.load_gen, plan.undo ? "after Turbo's undo" : "after Turbo's write");
            if (!plan.undo) {
                turbo_arm_ = arm_;  // what this write left in place (read back)
                arm_.turbo_values = turbo_arm_.turbo_values = true;
            }
        }
        results_.push_back(r);
        if (seq == job_seq_) busy_ = false;  // a lost job's late result leaves a newer job's wait alone
    });
    if (via.empty()) {
        cancel->store(true);  // the dispatcher may still hold it: it must never write
        std::lock_guard<std::mutex> lock(mu_);
        if (seq == job_seq_) busy_ = false;
        why = "no game-thread dispatcher (the game_tick hook is off and Turbo's Lua side does not pump)";
        return false;
    }
    return true;
}

bool CoreService::poll(ApplyResult& out) {
    ApplyResult r;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (results_.empty()) return false;
        r = results_.front();
        results_.erase(results_.begin());
    }
    if (!r.log_line.empty()) {
        char t[64];
        std::snprintf(t, sizeof(t), " [job %.0f us, %d reads]", r.micros, r.reads);
        log(r.log_line + t);
    }
    out = r;
    return true;
}

void CoreService::arm_from(const LiveTactic& lt, long long gen, const char* when) {
    arm_.armed = true;
    arm_.user = lt.at.user;
    arm_.load_gen = gen;
    for (int s = 0; s < kSlots; ++s) {
        arm_.roles[size_t(s)] = lt.slots[size_t(s)].role;
        arm_.pids[size_t(s)] = lt.slots[size_t(s)].pid;
    }
    arm_.line_height = lt.line_height;
    arm_.build_up = lt.build_up;
    arm_.approach = lt.approach;
    arm_.code = lt.code;
    // "Turbo's values" only while the tactic read is exactly what Turbo's last write of this career load left (after a reload, an undo,
    // Edit Tactic's Yes or the game's own change these are the game's values)
    const KickoffArm& t = turbo_arm_;
    arm_.turbo_values = t.armed && t.load_gen == gen && t.user == arm_.user && t.code == arm_.code && t.roles == arm_.roles && t.pids == arm_.pids &&
                        t.line_height == arm_.line_height && t.build_up == arm_.build_up && t.approach == arm_.approach;
    arm_.when = when;
}

void CoreService::kickoff_tick() {
    // the game thread never waits for Turbo: a busy lock is tried again on the next frame
    std::unique_lock<std::mutex> lock(mu_, std::try_to_lock);
    if (!lock.owns_lock()) return;
    if (!kick_pending_ || kickoff_running_) return;
    if (h_.clock_us && now() < kick_due_) return;
    kick_pending_ = false;
    const int attempt = ++kick_tries_;
    const KickoffArm arm = arm_;
    kickoff_running_ = true;
    kickoff_ = "kick-off check running ...";
    lock.unlock();
    run_kickoff(arm, attempt, cur_gen_.load());  // everything else (the switch files, the log, the scan, the dump) away from the game thread
}

void CoreService::run_kickoff(KickoffArm arm, int attempt, long long gen) {
    auto job = [this, arm, attempt, gen]() {
        std::string line;
        bool none = true, skipped = false;
        // read-only, yet it reads all of memory during a match: only for a tester who switched channel E on, never while a kill switch says
        // channel E is off (the Status tab says "off" then), and with its own off switch
        const Switches sw = h_.out_dir.empty() ? Switches() : read_switches(h_.out_dir);
        std::string why_not;
        if (!h_.out_dir.empty() && file_exists(h_.out_dir / kickoff_off_file())) why_not = std::string("off: turbo_output\\") + kickoff_off_file();
        else if (!sw.blocking().empty()) why_not = "kill switch turbo_output\\" + sw.blocking() + " is present (channel E is off)";
        else if (!h_.out_dir.empty() && !sw.opt_in)
            why_not = std::string("channel E is not switched on (turbo_output\\") + opt_in_file() + ")";
        else if (!arm.armed)
            why_not = "the live tactic was not read in the career hub before this match (Apply, or open Teams > Tactics > My team once in the hub)";
        else if (arm.load_gen != gen)
            // the tactic of an earlier career load (the reload rebuilt the record from the save): not what this match was set up from
            why_not = "the live tactic was last read in career load " + std::to_string(arm.load_gen) + ", this match is in load " + std::to_string(gen) +
                      " (open Teams > Tactics > My team once in the hub)";
        if (!why_not.empty()) {
            line = "channel E kick-off: not checked (" + why_not + ")";
            skipped = true;
        }
        if (!skipped) try {
            // the match-setup copies: by the 11 player ids (out+0x168) and by the 11 role words (out+0x118), the layout checked at out
            const std::vector<uint8_t> pid_pat = role_pattern(arm.pids), role_pat = role_pattern(arm.roles);
            const std::vector<Region> regs = h_.regions ? h_.regions() : std::vector<Region>();
            const size_t chunk = 1u << 20;
            std::vector<uint8_t> buf;
            std::vector<uint64_t> outs;  // candidate copy starts
            size_t pid_hits = 0, role_hits = 0;
            const double t0 = h_.clock_us ? h_.clock_us() : 0.0;
            bool timed_out = false;
            auto add = [&](uint64_t out) {
                if (std::find(outs.begin(), outs.end(), out) == outs.end()) outs.push_back(out);
            };
            for (const Region& g : regs) {
                for (uint64_t at = g.start; at < g.end && outs.size() < 128;) {
                    const size_t n = size_t(std::min<uint64_t>(chunk + pid_pat.size(), g.end - at));
                    if (n >= pid_pat.size() && h_.mem->read_block(at, n, buf)) {
                        for (uint64_t x : find_bytes(buf.data(), n, at, pid_pat, 64, 4))
                            if (x >= kCopyPids) {
                                ++pid_hits;
                                add(x - kCopyPids);
                            }
                        for (uint64_t x : find_bytes(buf.data(), n, at, role_pat, 64, 4))
                            if (x >= kCopyRoles) {
                                ++role_hits;
                                add(x - kCopyRoles);
                            }
                    }
                    at += chunk;
                    if (h_.clock_us && h_.clock_us() - t0 > h_.scan_budget_s * 1e6) {
                        timed_out = true;
                        break;
                    }
                }
                if (timed_out) break;
            }
            std::sort(outs.begin(), outs.end());
            std::vector<KickoffCopy> copies;
            std::string where;
            bool short_tail = false;
            for (uint64_t out : outs) {
                if (copies.size() >= kKickoffMaxCopies) break;
                if (!is_match_copy_at(*h_.mem, out, arm.user, arm.pids)) continue;
                KickoffCopy c;
                c.out = out;
                if (!h_.mem->read_block(c.out, kKickoffHead + kKickoffTail, c.bytes)) {
                    // the bytes after the known layout may cross into memory that is not readable: the layout alone, zero-filled after it
                    if (!h_.mem->read_block(c.out, kKickoffHead, c.bytes)) continue;
                    c.bytes.resize(kKickoffHead + kKickoffTail, 0);
                    short_tail = true;
                }
                where += (where.empty() ? "" : ", ") + hex(c.out);
                copies.push_back(std::move(c));
            }
            none = copies.empty();
            std::string dump;
            if (!copies.empty()) {
                if (h_.out_dir.empty()) {
                    dump = "not dumped (no turbo_output folder)";
                } else {
                    std::error_code ec;
                    fs::create_directories(h_.out_dir, ec);
                    std::ofstream bin(h_.out_dir / kickoff_dump_file(), std::ios::binary | std::ios::trunc);
                    for (const KickoffCopy& c : copies) bin.write(reinterpret_cast<const char*>(c.bytes.data()), std::streamsize(c.bytes.size()));
                    const bool ok_bin = bool(bin);
                    bin.close();
                    std::ofstream txt(h_.out_dir / kickoff_index_file(), std::ios::binary | std::ios::trunc);
                    txt << kickoff_index(arm, copies, pid_hits + role_hits);
                    const bool ok_txt = bool(txt);
                    char b[96];
                    std::snprintf(b, sizeof(b), "%zu x 0x%zX bytes", copies.size(), size_t(kKickoffHead + kKickoffTail));
                    dump = std::string(ok_bin && ok_txt ? "dumped to turbo_output\\" : "dump FAILED: turbo_output\\") + kickoff_dump_file() + " (" + b +
                           (short_tail ? ", a tail zero-filled" : "") + ")";
                }
            }
            const std::string looked = "looked for the tactic " + arm.when + " (LH " + std::to_string(arm.line_height) + " BU " + std::to_string(arm.build_up) +
                                       " DA " + std::to_string(arm.approach) + (arm.turbo_values ? ", Turbo's values)" : ", the game's values)");
            const std::string found = std::to_string(pid_hits) + " pid / " + std::to_string(role_hits) + " role-word hit(s)" + (timed_out ? ", scan ran out of time" : "");
            if (copies.empty()) {
                line = "channel E kick-off: no match copy found (" + found + ", 0 in the match-setup layout); " + looked;
            } else {
                std::vector<KickoffRead> reads;
                for (const KickoffCopy& c : copies) reads.push_back(decode_match_copy(c, arm));
                std::string fields;
                for (const CopyField& f : match_copy_fields()) {
                    char b[40];
                    std::snprintf(b, sizeof(b), "%s +0x%X%s", f.name, unsigned(f.offset), f.known ? "" : " (unknown)");
                    fields += (fields.empty() ? "" : " ") + std::string(b);
                }
                // the raw bytes always (known or not: the line doubles as X2's readout), where the Line Height looked for sits as a byte
                std::string lhb;
                for (size_t o : reads.front().lh_bytes) {
                    char b[16];
                    std::snprintf(b, sizeof(b), " +0x%zX", o);
                    lhb += b;
                }
                const std::string details = std::to_string(copies.size()) + (copies.size() == 1 ? " copy (" : " copies (") + where + "; " + found + "); decode " +
                                            fields + " u8 (" + match_copy_fields_source() + "); raw " + reads.front().raw + "; u8 " +
                                            std::to_string(arm.line_height) + " at" + (lhb.empty() ? std::string(" none") : lhb) + "; window +0x150: " +
                                            window_hex(reads.front().window) + "; " + dump + "; " + looked;
                line = kickoff_line(arm, reads, details);
            }
            if (attempt > 1) line += " (second look)";
        } catch (const std::exception& e) {
            line = std::string("channel E kick-off: check failed: ") + e.what();
        } catch (...) {
            line = "channel E kick-off: check failed";
        }
        if (skipped) none = false;  // nothing to look for again
        bool again = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            kickoff_ = line;
            kickoff_running_ = false;
            // no copy yet: once more 30 s later (the copy may be made after the first look), while the match lasts
            if (none && attempt < 2 && phase_ == "match") {
                kick_pending_ = true;
                kick_due_ = now() + kKickoffRetryUs;
                again = true;
            }
        }
        log(line + (again ? "; looking again in 30 s" : ""));
    };
    if (h_.run_background) h_.run_background(job);
    else job();
}

std::string CoreService::kickoff_status() {
    std::lock_guard<std::mutex> lock(mu_);
    return kickoff_;
}

}  // namespace tsheet
}  // namespace turbo
