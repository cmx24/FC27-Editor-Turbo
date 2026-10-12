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
int depth_band(int depth) {
    if (depth <= 30) return 0;
    if (depth <= 60) return 1;
    if (depth <= 90) return 2;
    return 3;
}

std::string depth_band_text(int depth) {
    static const char* const kRange[4] = {"1-30", "31-60", "61-90", "91-100"};
    const int b = depth_band(depth);
    return "band " + std::to_string(b) + " (" + kRange[b] + ")";
}

std::string Switches::blocking() const {
    if (master) return master_switch_file();
    if (off) return kill_switch_file();
    return "";
}

std::string Switches::mirror_block() const {
    const char* f = master ? master_switch_file() : off ? kill_switch_file() : db_off ? db_off_file() : mirror_off ? mirror_off_file() : nullptr;
    if (f) return std::string("kill switch turbo_output\\") + f + " is present";
    if (!mirror_on)
        return std::string("off until turbo_output\\") + mirror_on_file() +
               " exists (opt-in in 2.2: your in-game save writes the live tactic into the career database anyway)";
    return "";
}

Switches read_switches(const fs::path& out) {
    Switches s;
    s.opt_in = file_exists(out / opt_in_file());
    s.off = file_exists(out / kill_switch_file());
    s.master = file_exists(out / master_switch_file());
    s.find_off = file_exists(out / find_off_file());
    s.mirror_off = file_exists(out / mirror_off_file());
    s.db_off = file_exists(out / db_off_file());
    s.mirror_on = file_exists(out / mirror_on_file());
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
    out.depth = i32_at(mem, r + kRecDepth, ok);
    out.build_up = i32_at(mem, r + kRecBuildUp, ok);
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
    int32_t active = -1;
    if (!active_sheet_id(mem, lt.at, lt.mode, active))
        return "GetActiveSheetId's loop could not read the club's sheets (mode " + std::to_string(lt.mode) + "): refused";
    if (lt.sheet_id != active) return "";
    return "SetSheet would run its propagation step (vf 0xF8) for this sheet (mode " + std::to_string(lt.mode) + ", sheet " + std::to_string(lt.sheet_id) +
           " is the active one): refused until vf 0xF8 is traced";
}

// ------------------------------------------------------------------------------------------------ plans
const char* field_name(FieldKind k) {
    switch (k) {
        case FieldKind::Depth: return "defensivedepth";
        case FieldKind::BuildUp: return "buildupplay";
        case FieldKind::Role: return "role";
        case FieldKind::RoleId: return "roleid";
        case FieldKind::Focus: return "focus";
    }
    return "?";
}

bool field_from_name(const std::string& s, FieldKind& k) {
    for (FieldKind f : {FieldKind::Depth, FieldKind::BuildUp, FieldKind::Role, FieldKind::RoleId, FieldKind::Focus})
        if (s == field_name(f)) {
            k = f;
            return true;
        }
    return false;
}

uint64_t field_address(const Located& at, const FieldWrite& w) {
    switch (w.kind) {
        case FieldKind::Depth: return at.record + kRecDepth;
        case FieldKind::BuildUp: return at.record + kRecBuildUp;
        case FieldKind::Role:
        case FieldKind::RoleId:
        case FieldKind::Focus: {
            if (w.slot < 0 || w.slot >= kSlots || !at.slots) return 0;
            const uint64_t sl = at.slots + kSlotStride * uint64_t(w.slot);
            return sl + (w.kind == FieldKind::Role ? kSlotRole : w.kind == FieldKind::RoleId ? kSlotRoleId : kSlotFocus);
        }
    }
    return 0;
}

static void fill_identity(const LiveTactic& now, Plan& out) {
    out.user = now.at.user;
    out.record_index = now.at.record_index;
    out.sheet_id = now.sheet_id;
    out.formation = now.formation;
    out.source_formation = now.source_formation;
    out.record_name = now.name;
    for (int s = 0; s < kSlots; ++s) out.pids[size_t(s)] = now.slots[size_t(s)].pid;
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

// One club's sheets (its own page cache): false when they do not look like sheets (the club is skipped)
static bool allow_team(Memory& mem, int32_t key, uint64_t begin, uint64_t end, RoleAllowList& out) {
    if (begin == 0 && end == 0) return true;  // a club without a sheet
    if (!is_ptr(begin, 8) || end < begin || (end - begin) % kSheetStride != 0) return false;
    const int n = int((end - begin) / kSheetStride);
    if (n < 1 || n > kMaxSheets) return false;
    std::set<int32_t> words;
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
std::string make_plan(const LiveTactic& now, const Request& req, Plan& out, const RoleAllowList* allow) {
    out = Plan();
    if (req.preset) return "the tactical preset is not written in 2.2 (preset ids are not decoded: TACTICS_CHANNEL_E_REPORT.txt section 3)";
    if (req.formation) return "the formation is not written in 2.2 (needs the formations row copied into all 11 slots: 2.3)";
    if (req.build_up != -1 && (req.build_up < kBuildUpMin || req.build_up > kBuildUpMax))
        return "build-up " + std::to_string(req.build_up) + " is outside 0..3";
    if (req.depth != -1 && (req.depth < kDepthMin || req.depth > kDepthMax)) return "defensive depth " + std::to_string(req.depth) + " is outside 1..100";
    fill_identity(now, out);
    if (req.build_up != -1 && req.build_up != now.build_up) out.writes.push_back({FieldKind::BuildUp, -1, now.build_up, req.build_up});
    if (req.depth != -1 && req.depth != now.depth) out.writes.push_back({FieldKind::Depth, -1, now.depth, req.depth});
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
static std::string field_label(const FieldWrite& w) {
    return std::string(field_name(w.kind)) + (w.slot >= 0 ? " of slot " + std::to_string(w.slot) : std::string());
}

void WriteHistory::note(const Plan& p) {
    if (p.undo) return;
    if (p.user != user || p.record_index != record_index || p.load_gen != load_gen) {
        original.clear();
        user = p.user;
        record_index = p.record_index;
        load_gen = p.load_gen;
    }
    for (const FieldWrite& w : p.writes) original.emplace(std::make_pair(int(w.kind), w.slot), w.old_value);  // the first write's value stays
}

std::string WriteHistory::check_undo(const Plan& p) const {
    if (original.empty() || p.user != user || p.record_index != record_index || p.load_gen != load_gen)
        return "Turbo has written nothing into this record in this career load (since Turbo started): nothing to put back";
    for (const FieldWrite& w : p.writes) {
        auto it = original.find(std::make_pair(int(w.kind), w.slot));
        if (it == original.end()) return field_label(w) + " was not written by Turbo in this career load: not put back";
        if (it->second != w.new_value)
            return field_label(w) + ": the undo would write " + std::to_string(w.new_value) + ", but the game held " + std::to_string(it->second) +
                   " before Turbo's first write: not put back";
    }
    return "";
}

std::string check_plan(const LiveTactic& now, const Plan& plan, const JobChecks& chk) {
    std::set<std::pair<int, int>> seen;
    std::map<int, std::array<const FieldWrite*, 3>> slots;
    for (const FieldWrite& w : plan.writes) {
        const std::string f = field_label(w);
        if (!seen.insert(std::make_pair(int(w.kind), w.slot)).second) return f + " is in the plan twice";
        switch (w.kind) {
            case FieldKind::Depth:
                if (w.slot != -1) return f + ": not a slot field";
                if (w.new_value < kDepthMin || w.new_value > kDepthMax) return "defensive depth " + std::to_string(w.new_value) + " is outside 1..100";
                break;
            case FieldKind::BuildUp:
                if (w.slot != -1) return f + ": not a slot field";
                if (w.new_value < kBuildUpMin || w.new_value > kBuildUpMax) return "build-up " + std::to_string(w.new_value) + " is outside 0..3";
                break;
            case FieldKind::Role:
            case FieldKind::RoleId:
            case FieldKind::Focus: {
                if (w.slot < 0 || w.slot >= kSlots) return f + ": the slot is outside 0..10";
                auto& t = slots[w.slot];  // value-initialised: three nullptrs
                t[w.kind == FieldKind::Role ? 0 : w.kind == FieldKind::RoleId ? 1 : 2] = &w;
                break;
            }
        }
    }
    for (const auto& kv : slots) {
        const int s = kv.first;
        const auto& t = kv.second;
        const std::string sl = "slot " + std::to_string(s);
        if (!t[0] || !t[1] || !t[2]) return sl + ": the role word, role id and focus are written together or not at all";
        const SlotView& v = now.slots[size_t(s)];
        const int32_t word = t[0]->new_value;
        const int rid = t[1]->new_value, foc = t[2]->new_value;
        if (rid < 0 || rid > kRoleIdMax) return sl + ": role id " + std::to_string(rid) + " is outside 0..63";
        if (foc < 0 || foc > kFocusMax) return sl + ": focus " + std::to_string(foc) + " is outside 0..7";
        if (!role_word_plain(word) || word > kRoleWordMax) return sl + ": role word " + std::to_string(word) + " is not a plain word up to 60000";
        if (role_group(word) != v.group())
            return sl + ": the position group would change (" + std::to_string(v.group()) + " -> " + std::to_string(role_group(word)) + ")";
        if (word != role_word(v.group(), rid, foc))
            return sl + ": role word " + std::to_string(word) + " is not group " + std::to_string(v.group()) + " / role " + std::to_string(rid) + " / focus " +
                   std::to_string(foc);
        if (!plan.undo) {
            if (!chk.allow || !chk.allow->ok) return sl + ": role and focus are not written without the role allow-list";
            if (!chk.allow->has(word)) return sl + ": role word " + std::to_string(word) + " was never seen in the game's own sheets (role allow-list)";
        }
    }
    if (plan.undo) {
        if (!chk.history) return "an undo writes only values Turbo read from the game before its write (none known)";
        const std::string w = chk.history->check_undo(plan);
        if (!w.empty()) return w;
    }
    return "";
}

std::string describe_writes(int user, int record_index, const std::vector<FieldWrite>& ws) {
    std::string s = "team " + std::to_string(user) + " record " + std::to_string(record_index);
    for (const FieldWrite& w : ws) {
        switch (w.kind) {
            case FieldKind::Depth: s += " depth " + std::to_string(w.old_value) + "->" + std::to_string(w.new_value); break;
            case FieldKind::BuildUp: s += " build-up " + std::to_string(w.old_value) + "->" + std::to_string(w.new_value); break;
            case FieldKind::Role:
                s += " slot " + std::to_string(w.slot) + " role " + std::to_string(w.old_value) + "->" + std::to_string(w.new_value);
                break;
            case FieldKind::RoleId:
            case FieldKind::Focus: break;  // the word says it
        }
    }
    return s;
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
        // 1. L1-L4 again (pointers can move between frames)
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
        // the plan on its own (ranges, the slot's three values together, the group, the allow-list; an undo's values in the history)
        why = check_plan(now, plan, chk);
        if (!why.empty()) return fail("G6", why + ": nothing written");
        // 2. snapshot (the old values, as they are in memory now)
        struct Snap {
            uint64_t addr;
            int32_t value;
        };
        std::vector<Snap> snap;
        for (const FieldWrite& w : plan.writes) {
            const uint64_t ad = field_address(now.at, w);
            int32_t cur = 0;
            if (!ad || !mem.rd(ad, cur)) return fail("snapshot", std::string(field_name(w.kind)) + " is not readable: nothing written");
            if (cur != w.old_value)
                return fail("changed", std::string(field_name(w.kind)) + (w.slot >= 0 ? " of slot " + std::to_string(w.slot) : std::string()) + " is " +
                                           std::to_string(cur) + ", not " + std::to_string(w.old_value) + " as when Apply was pressed: nothing written");
            snap.push_back({ad, cur});
        }
        const uint64_t dirty_addr = now.at.sheet + kSheetDirty;
        if (!mem.rd(dirty_addr, r.dirty_before)) return fail("snapshot", "the dirty flag is not readable: nothing written");
        if (chk.cancel && chk.cancel->load()) return fail("cancelled", "the job was given up: nothing written");
        // 3. write; then the dirty flag as UpdateSheet does
        bool write_failed = false;
        for (const FieldWrite& w : plan.writes)
            if (!mem.wr(field_address(now.at, w), w.new_value)) write_failed = true;
        const uint8_t one = 1;
        if (!write_failed && !mem.wr(dirty_addr, one)) write_failed = true;
        // 4. read back every written field
        std::string bad;
        for (const FieldWrite& w : plan.writes) {
            int32_t back = 0;
            if (!mem.rd(field_address(now.at, w), back) || back != w.new_value) {
                bad = std::string(field_name(w.kind)) + (w.slot >= 0 ? " of slot " + std::to_string(w.slot) : std::string()) + " reads " +
                      std::to_string(back) + ", not " + std::to_string(w.new_value);
                break;
            }
        }
        uint8_t dirty_back = 0;
        if (bad.empty() && (!mem.rd(dirty_addr, dirty_back) || dirty_back != 1)) bad = "the dirty flag reads " + std::to_string(dirty_back);
        if (write_failed && bad.empty()) bad = "a write was refused by the memory layer";
        if (!bad.empty()) {
            // full undo from the snapshot (fields and the dirty flag): every field that does not hold its old value is written back, then
            // everything is read back (a field the write never reached already holds it)
            for (const Snap& s : snap) {
                int32_t cur = 0;
                if (!mem.rd(s.addr, cur) || cur != s.value) mem.wr(s.addr, s.value);
            }
            uint8_t d = 0;
            if (!mem.rd(dirty_addr, d) || d != r.dirty_before) mem.wr(dirty_addr, r.dirty_before);
            bool restored = true;
            for (const Snap& s : snap) {
                int32_t back = 0;
                if (!mem.rd(s.addr, back) || back != s.value) restored = false;
            }
            if (!mem.rd(dirty_addr, d) || d != r.dirty_before) restored = false;
            r.undone = restored;
            r.restore_failed = !restored;
            r.session_off = true;
            r.writes = plan.writes;
            r.ok = false;
            r.stage = "readback";
            r.message = "read-back mismatch (" + bad + "): " + (restored ? "every field and the dirty flag were put back" : "PUTTING BACK FAILED") +
                        "; channel E is off for this session";
            r.log_line = "channel E: " + describe_writes(plan.user, plan.record_index, plan.writes) + " READ-BACK MISMATCH (" + bad + "), " +
                         (restored ? "undone from the snapshot" : "undo FAILED");
            r.reads = mem.reads;
            return r;
        }
        r.dirty_after = dirty_back;
        r.ok = true;
        r.written = true;
        r.writes = plan.writes;
        r.stage = "done";
        PageCache after(mem);
        std::string why_after = read_live(after, svc, plan.user, a, r.after);
        r.message = std::string(plan.undo ? "put back" : "written") + " and read back" + (why_after.empty() ? "" : " (read-out after: " + why_after + ")");
        r.log_line = "channel E: " + std::string(plan.undo ? "undo " : "") + describe_writes(plan.user, plan.record_index, plan.writes) + " (read back ok)";
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
        ws.push_back(x);
    }
    j["fields"] = ws;
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

static int32_t current_of(const LiveTactic& now, const FieldWrite& w) {
    switch (w.kind) {
        case FieldKind::Depth: return now.depth;
        case FieldKind::BuildUp: return now.build_up;
        case FieldKind::Role: return w.slot >= 0 && w.slot < kSlots ? now.slots[size_t(w.slot)].role : INT32_MIN;
        case FieldKind::RoleId: return w.slot >= 0 && w.slot < kSlots ? now.slots[size_t(w.slot)].role_id : INT32_MIN;
        case FieldKind::Focus: return w.slot >= 0 && w.slot < kSlots ? now.slots[size_t(w.slot)].focus : INT32_MIN;
    }
    return INT32_MIN;
}

std::string same_career(const UndoRecord& u, const std::string& session, long long load_gen) {
    if (u.session == session && u.load_gen == load_gen) return "";
    return "the undo file is from another career load (written " + (u.when.empty() ? std::string("?") : u.when) + ", load " + std::to_string(u.load_gen) +
           (u.session != session ? " of another Lua session" : "") + "; this is load " + std::to_string(load_gen) +
           "): the reload threw Turbo's in-memory change away (only an in-game save would have kept it); nothing put back";
}

std::string make_undo_plan(const LiveTactic& now, const UndoRecord& u, const std::string& session, long long load_gen, Plan& out,
                           std::vector<std::string>& skipped) {
    out = Plan();
    skipped.clear();
    if (u.writes.empty()) return "the undo file holds no field Turbo wrote";
    const std::string sc = same_career(u, session, load_gen);
    if (!sc.empty()) return sc;
    if (u.team != now.at.user) return "the undo file is for club " + std::to_string(u.team) + ", the live tactic is club " + std::to_string(now.at.user) + "'s";
    if (u.record_index != now.at.record_index || u.formation != now.formation || u.source_formation != now.source_formation || u.sheet_id != now.sheet_id)
        return "the active tactic is not the one Turbo wrote (record " + std::to_string(now.at.record_index) + ", formation " + std::to_string(now.formation) +
               "): nothing put back";
    for (int s = 0; s < kSlots; ++s)
        if (now.slots[size_t(s)].pid != u.pids[size_t(s)]) return "slot " + std::to_string(s) + " holds another player now: nothing put back";
    fill_identity(now, out);
    out.undo = true;
    out.load_gen = load_gen;
    // per slot: all three role values hold Turbo's values, or none is put back
    std::set<int> slot_ok, slot_bad;
    for (const FieldWrite& w : u.writes)
        if (w.slot >= 0) (current_of(now, w) == w.new_value ? slot_ok : slot_bad).insert(w.slot);
    for (const FieldWrite& w : u.writes) {
        const int32_t cur = current_of(now, w);
        const bool slot_field = w.slot >= 0;
        const bool holds = slot_field ? (slot_bad.count(w.slot) == 0) : cur == w.new_value;
        if (!holds) {
            if (!slot_field || w.kind == FieldKind::Role)
                skipped.push_back(std::string(field_name(w.kind)) + (slot_field ? " of slot " + std::to_string(w.slot) : std::string()) + " is " +
                                  std::to_string(slot_field && w.kind == FieldKind::Role ? now.slots[size_t(w.slot)].role : cur) +
                                  ", not Turbo's " + std::to_string(w.new_value) + ": left as it is");
            continue;
        }
        if (!slot_field && cur == w.old_value) continue;  // a slot's three role values go back together
        out.writes.push_back({w.kind, w.slot, cur, w.old_value});
    }
    if (out.writes.empty()) return skipped.empty() ? "every field already holds its value from before Turbo's write" : "no field still holds Turbo's value";
    return "";
}

bool same_record(const UndoRecord& u, const LiveTactic& now) {
    if (u.team != now.at.user || u.record_index != now.at.record_index || u.formation != now.formation || u.source_formation != now.source_formation ||
        u.sheet_id != now.sheet_id)
        return false;
    for (int s = 0; s < kSlots; ++s)
        if (u.pids[size_t(s)] != now.slots[size_t(s)].pid) return false;
    return true;
}

UndoRecord merge_undo(const UndoRecord& prev, const UndoRecord& next) {
    if (prev.session != next.session || prev.load_gen != next.load_gen) return next;  // another career load: its change is gone anyway
    if (prev.team != next.team || prev.record_index != next.record_index || prev.formation != next.formation ||
        prev.source_formation != next.source_formation || prev.sheet_id != next.sheet_id || prev.pids != next.pids)
        return next;
    UndoRecord m = next;
    m.dirty_before = prev.dirty_before;
    auto find = [](const std::vector<FieldWrite>& ws, FieldKind k, int slot) -> const FieldWrite* {
        for (const FieldWrite& w : ws)
            if (w.kind == k && w.slot == slot) return &w;
        return nullptr;
    };
    // a slot is one unit: when the earlier write's role word is this plan's old word, all three take the earlier old values
    for (FieldWrite& w : m.writes) {
        const FieldWrite* p = find(prev.writes, w.kind, w.slot);
        if (!p) continue;
        if (w.slot >= 0) {
            const FieldWrite* pr = find(prev.writes, FieldKind::Role, w.slot);
            const FieldWrite* nr = find(next.writes, FieldKind::Role, w.slot);
            if (pr && nr && pr->new_value == nr->old_value) w.old_value = p->old_value;
        } else if (p->new_value == w.old_value) {
            w.old_value = p->old_value;
        }
    }
    for (const FieldWrite& p : prev.writes)
        if (!find(next.writes, p.kind, p.slot)) m.writes.push_back(p);
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
    for (const FieldWrite& w : u.writes) {
        if (w.kind == FieldKind::RoleId || w.kind == FieldKind::Focus) continue;  // the word says it
        const int32_t cur = current_of(now, w);
        if (cur != w.new_value)
            out.push_back(std::string(w.kind == FieldKind::Role ? "slot " + std::to_string(w.slot) + " role" : field_name(w.kind)) + " " +
                          std::to_string(w.new_value) + " -> " + std::to_string(cur));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ the cm_mentalities mirror's checks
std::string mirror_field_name(const FieldWrite& w) {
    switch (w.kind) {
        case FieldKind::BuildUp: return "buildupplay";
        case FieldKind::Depth: return "defensivedepth";
        case FieldKind::Role: return "pos" + std::to_string(w.slot) + "role";
        case FieldKind::RoleId:
        case FieldKind::Focus: return "";
    }
    return "";
}

std::string mirror_row_check(const std::function<bool(const std::string&, int64_t&)>& get, const LiveTactic& now, const std::vector<FieldWrite>& writes) {
    for (int s = 0; s < kSlots; ++s) {
        const std::string pf = "playerid" + std::to_string(s), qf = "position" + std::to_string(s);
        int64_t pid = 0, pos = 0;
        if (!get(pf, pid) || !get(qf, pos)) return "this save's cm_mentalities has no " + pf + " / " + qf + ": the row cannot be matched to the live slots";
        if (pid != now.slots[size_t(s)].pid)
            return pf + " is " + std::to_string(pid) + ", the live slot " + std::to_string(s) + " holds player " + std::to_string(now.slots[size_t(s)].pid);
        if (pos != now.slots[size_t(s)].position)
            return qf + " is " + std::to_string(pos) + ", the live slot " + std::to_string(s) + " is position " + std::to_string(now.slots[size_t(s)].position);
    }
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

std::string undo_mirror_check(const Switches& sw, const std::function<bool(const std::string&, int64_t&)>& get, const UndoRecord& u) {
    const std::string block = sw.mirror_block();
    if (!block.empty()) return block;
    int64_t team = -1, active = -1, src = -1;
    const bool ok = get("teamid", team) && get("activetactic", active) && get("sourceformationid", src);
    if (!ok || team != u.team || active != 1 || src != u.source_formation)
        return "row " + std::to_string(u.mirror_row_key) + " is not club " + std::to_string(u.team) + "'s active tactic with sourceformationid " +
               std::to_string(u.source_formation) + " (teamid " + std::to_string(team) + ", activetactic " + std::to_string(active) + ", sourceformationid " +
               std::to_string(src) + ")";
    return "";
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

bool is_match_copy(Memory& mem, uint64_t hit, int user, const std::array<int32_t, kSlots>& pids) {
    if (hit < kCopyRoles) return false;
    const uint64_t out = hit - kCopyRoles;
    int32_t team = 0;
    if (!mem.rd(out + kCopyTeam, team) || team != user) return false;
    for (int s = 0; s < kSlots; ++s) {
        int32_t pid = 0;
        if (!mem.rd(out + kCopyPids + 4 * uint64_t(s), pid) || pid != pids[size_t(s)]) return false;
    }
    return true;
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
    std::lock_guard<std::mutex> lock(mu_);
    if (ctx.user > 0) user_ = ctx.user;
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
                       std::to_string(l.slots) + " slots; your club " + std::to_string(user) + " left out, " + std::to_string(l.skipped) + " club(s) skipped)"
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
                history_.note(plan);
            }
        }
        r.micros = h_.clock_us ? h_.clock_us() - t0 : 0.0;
        std::lock_guard<std::mutex> lock(mu_);
        if (r.session_off) session_off_ = r.message;
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

void CoreService::kickoff_check(int user, const std::array<int32_t, kSlots>& roles, const std::array<int32_t, kSlots>& pids) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (kickoff_running_) return;
        kickoff_running_ = true;
        kickoff_ = "kick-off check running ...";
    }
    auto job = [this, user, roles, pids]() {
        std::string line;
        try {
            const std::vector<uint8_t> pat = role_pattern(roles);
            const std::vector<Region> regs = h_.regions ? h_.regions() : std::vector<Region>();
            const size_t chunk = 1u << 20;
            std::vector<uint8_t> buf;
            std::vector<uint64_t> hits;
            const double t0 = h_.clock_us ? h_.clock_us() : 0.0;
            bool timed_out = false;
            for (const Region& g : regs) {
                for (uint64_t at = g.start; at < g.end && hits.size() < 64;) {
                    const size_t n = size_t(std::min<uint64_t>(chunk + pat.size(), g.end - at));
                    if (n >= pat.size() && h_.mem->read_block(at, n, buf))
                        for (uint64_t x : find_bytes(buf.data(), n, at, pat, 64, 4))
                            if (std::find(hits.begin(), hits.end(), x) == hits.end()) hits.push_back(x);
                    at += chunk;
                    if (h_.clock_us && h_.clock_us() - t0 > h_.scan_budget_s * 1e6) {
                        timed_out = true;
                        break;
                    }
                }
                if (timed_out) break;
            }
            int copies = 0;
            std::string where;
            for (uint64_t x : hits)
                if (is_match_copy(*h_.mem, x, user, pids)) {
                    ++copies;
                    if (where.size() < 80) where += (where.empty() ? "" : ", ") + hex(x - kCopyRoles);
                }
            line = "kick-off check: " + std::to_string(hits.size()) + " cop" + (hits.size() == 1 ? "y" : "ies") + " of the written role words, " +
                   std::to_string(copies) + " in the match-setup layout" + (copies ? " (" + where + "): the match uses the written roles" : "") +
                   (timed_out ? " (scan ran out of time)" : "") + "; depth and build-up are not checked by this";
        } catch (const std::exception& e) {
            line = std::string("kick-off check failed: ") + e.what();
        } catch (...) {
            line = "kick-off check failed";
        }
        log("channel E: " + line);
        std::lock_guard<std::mutex> lock(mu_);
        kickoff_ = line;
        kickoff_running_ = false;
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
