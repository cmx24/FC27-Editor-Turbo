// Native tests: tactics channel E (core/team_sheet.h, ui/ui_live_tactic.cpp, turbo_output\TACTICS_CHANNEL_E_REPORT.txt section 4): your
// club's live tactic record on a SIMULATED sheet layout laid out exactly as the report's live probe read it on build 6AC07E31-2145C000
// (the service, the hash map with a second key in the same bucket, the 0x290-byte sheet, five 0xF8-byte records, eleven 0x48-byte slots,
// a stale copy of the service's vtable pair, the PlayerStatusManager chain) plus what the 2026-10-12 decode added: the tactic code at
// record +0x68, the Defensive Approach at +0xB4, and Edit Tactic's four inline working copies in the service (svc+0xF80 and +0x1170 hold
// the tactic, +0x1078 / +0x1268 are empty templates; 1012_svc_B00_1400.bin). Player ids are synthetic. Included by test_main.cpp after the
// Ui driver; team_sheet_test::run() runs the core cases, team_sheet_test::ui_cases() the Tactics tab card (from test_ui).
#pragma once
#include "core/team_sheet.h"
#include "ui/ui_live_tactic.h"

namespace team_sheet_test {

using namespace turbo;
using namespace turbo::tsheet;

struct SheetWorld {
    SimMemory mem;
    static constexpr uint64_t kVt1 = 0x14AD7F6C8ULL, kVt2 = 0x14AD7E8B8ULL, kSet = 0x145E43A60ULL, kUpd = 0x145E43BA4ULL, kAct = 0x145E301D4ULL;
    static constexpr uint64_t kRegistry = 0x14BE2A1F8ULL, kRegVt = 0x14982C1A0ULL, kFind = 0x141AF1B70ULL;
    static constexpr uint64_t kSvc = 0xF643F580ULL, kBuckets = 0xF207E360ULL, kNodeA = 0xB802B070ULL, kNodeB = 0xB802A0E0ULL;
    static constexpr uint64_t kSheet = 0x3BCC362A0ULL, kRecs = 0xBA0BE300ULL, kSlots0 = 0x3BCD1F380ULL, kStale = 0x3AC254510ULL;
    static constexpr uint64_t kManagers = 0x6A804330ULL, kHolder = 0x67187F90ULL, kPsm = 0x67B14EC0ULL, kPsmVt = 0x14AFFD898ULL;
    // the other club in the same bucket (node A): one sheet, one record, the role words the role allow-list learns from
    static constexpr uint64_t kSheetB = 0x3BCC40000ULL, kRecsB = 0xBA0C0000ULL, kSlotsB = 0x3BCD30000ULL;
    static constexpr std::array<int32_t, 11> kRolesB = {4227, 8581, 12737, 12867, 8581, 18306, 17155, 29571, 38211, 38214, 38273};
    static constexpr uint32_t kBucketCount = 887;
    // Edit Tactic's working copies (inline in the service) and their slot vectors; the codes of the 2026-10-12 evidence
    static constexpr uint64_t kCopySlots = 0x3BCD50000ULL;
    static constexpr const char* kCode = "?ZaE&CYzKoT7";
    static constexpr const char* kOtherCode = "YZ&e%cyNkC1o";
    // the report's 4-2-1-3 (2.9): positions and role words in slot order
    static constexpr std::array<int32_t, 11> kPositions = {0, 3, 4, 6, 7, 9, 11, 18, 23, 25, 27};
    static constexpr std::array<int32_t, 11> kRoles = {4227, 8581, 12737, 12867, 8581, 18306, 17155, 29571, 33797, 38213, 33797};
    int team;
    int32_t source_formation;
    std::array<int32_t, 11> pids{};

    explicit SheetWorld(int team_ = 95, int32_t sf = 36) : team(team_), source_formation(sf) {
        for (int s = 0; s < 11; ++s) pids[size_t(s)] = 100001 + s;
        // vtables, the registry
        mem.map(kVt1, 229 * 8);
        mem.wr(kVt1 + 8 * 23, kAct);
        mem.wr(kVt1 + 8 * 24, kSet);
        mem.wr(kVt1 + 8 * 182, kUpd);
        mem.map(kVt2, 16);
        mem.map(kRegistry, 16);
        mem.wr(kRegistry, kRegVt);
        mem.map(kRegVt, 0x40);
        mem.wr(kRegVt + 0x38, kFind);
        // the service
        mem.map(kSvc, kSvcSize);
        mem.wr(kSvc, kVt1);
        mem.wr(kSvc + 8, kVt2);
        mem.wr(kSvc + kSvcLoaded, uint8_t(1));
        mem.wr(kSvc + kSvcLastTeam, int32_t(1939));
        mem.wr(kSvc + kSvcActiveSheet, int32_t(-1));
        mem.wr(kSvc + kSvcBuckets, kBuckets);
        mem.wr(kSvc + kSvcBucketCount, kBucketCount);
        mem.wr(kSvc + kSvcNodeCount, uint32_t(841));
        // the map: another club first in the same bucket, then yours
        mem.map(kBuckets, (kBucketCount + 1) * 8);
        mem.wr(kBuckets + 8 * (uint64_t(team) % kBucketCount), kNodeA);
        mem.map(kNodeA, 0x30);
        mem.wr(kNodeA + kNodeKey, int32_t(team + int(kBucketCount)));
        mem.wr(kNodeA + kNodeNext, kNodeB);
        mem.wr(kNodeA + kNodeBegin, kSheetB);
        mem.wr(kNodeA + kNodeEnd, kSheetB + kSheetStride);
        mem.map(kSheetB, kSheetStride);
        mem.wr(kSheetB + kSheetId, int32_t(-1));
        mem.wr(kSheetB + kSheetTeam, int32_t(team + int(kBucketCount)));
        mem.wr(kSheetB + kSheetCurrent, uint8_t(1));
        mem.wr(kSheetB + kSheetRecBegin, kRecsB);
        mem.wr(kSheetB + kSheetRecEnd, kRecsB + kRecStride);
        mem.wr(kSheetB + kSheetSlotCount, int32_t(11));
        mem.map(kRecsB, kRecStride);
        mem.wr(kRecsB + kRecActive, uint8_t(1));
        mem.wr(kRecsB + kRecSourceFormation, int32_t(36));
        mem.wr(kRecsB + kRecSourceFormation2, int32_t(36));
        mem.wr(kRecsB + kRecSlotBegin, kSlotsB);
        mem.wr(kRecsB + kRecSlotEnd, kSlotsB + 11 * kSlotStride);
        mem.map(kSlotsB, 11 * kSlotStride);
        for (int s = 0; s < 11; ++s) {
            const uint64_t sl = kSlotsB + kSlotStride * uint64_t(s);
            mem.wr(sl + kSlotPid, int32_t(200001 + s));
            mem.wr(sl + kSlotRole, kRolesB[size_t(s)]);
            mem.wr(sl + kSlotRoleId, int32_t(role_id_of(kRolesB[size_t(s)])));
            mem.wr(sl + kSlotFocus, int32_t(focus_of(kRolesB[size_t(s)])));
        }
        mem.map(kNodeB, 0x30);
        mem.wr(kNodeB + kNodeKey, int32_t(team));
        mem.wr(kNodeB + kNodeBegin, kSheet);
        mem.wr(kNodeB + kNodeEnd, kSheet + kSheetStride);
        // the sheet
        mem.map(kSheet, kSheetStride);
        put_sheet(kSheet, -1, true, -1, uint8_t(1));
        // the records
        mem.map(kRecs, 5 * kRecStride);
        mem.map(kSlots0, 5 * 0x1000);
        put_records(kRecs, kSlots0);
        // Edit Tactic's working copies: +0xF80 and +0x1170 hold this tactic (its code, its values), +0x1078 / +0x1268 empty templates
        mem.map(kCopySlots, 4 * 0x400);
        put_copy(0xF80, kCode, 25, 0, 0);
        put_template(0x1078);
        put_copy(0x1170, kCode, 25, 0, 0);
        put_template(0x1268);
        // PlayerStatusManager: manager slot 87 of the career's table (report 2.4)
        mem.map(kManagers + 0x20 * 87, 0x20);
        mem.wr(kManagers + 0x20 * 87 + 0x10, int32_t(1));
        mem.wr(kManagers + 0x20 * 87 + 0x18, kHolder);
        mem.map(kHolder, 8);
        mem.wr(kHolder, kPsm);
        mem.map(kPsm, 0x40);
        mem.wr(kPsm, kPsmVt);
        mem.wr(kPsm + 0x08, kManagers);  // its manager table (report 2.4: PSM +8 = the hub)
        mem.wr(kPsm + 0x10, int32_t(team));
        // a freed copy that kept the vtable pair (report 2.5): the rest is 0xCC
        mem.map(kStale, kSvcSize);
        std::vector<uint8_t> cc(kSvcSize, 0xCC);
        mem.write(kStale, cc.data(), cc.size());
        mem.wr(kStale, kVt1);
        mem.wr(kStale + 8, kVt2);
    }
    void put_sheet(uint64_t e, int32_t tsid, bool current, int32_t mode, uint8_t dirty) {
        mem.wr(e + kSheetId, tsid);
        mem.wr(e + kSheetTeam, int32_t(team));
        mem.wr(e + 8, int32_t(-1));
        mem.wr(e + kSheetCurrent, uint8_t(current ? 1 : 0));
        for (int k = 0; k < kSheetPidCount; ++k) mem.wr(e + kSheetPids + 4 * uint64_t(k), int32_t(k < 31 ? 100001 + k : -1));
        mem.wr(e + kSheetRecBegin, kRecs);
        mem.wr(e + kSheetRecEnd, kRecs + 5 * kRecStride);
        mem.wr(e + kSheetSlotCount, int32_t(11));
        mem.wr(e + kSheetDirty, dirty);
        mem.wr(e + kSheetMode, mode);
    }
    void put_records(uint64_t rb, uint64_t sb0) {
        for (int i = 0; i < 5; ++i) {
            const uint64_t r = rb + kRecStride * uint64_t(i);
            const uint64_t sb = sb0 + 0x1000 * uint64_t(i);
            if (i == 0) {
                mem.wr(r + kRecName, r + 0x10);
                mem.wr(r + kRecNameSize, uint32_t(3));
                mem.wr(r + 0x0C, uint32_t(0x80000050));
                mem.write(r + 0x10, "433", 4);
                mem.write(r + kRecCode, kCode, 12);
                mem.wr(r + 0x77, uint8_t(3));
                mem.wr(r + kRecActive, uint8_t(1));
                mem.wr(r + kRecPreset, int32_t(1));
                mem.wr(r + kRecLineHeight, int32_t(25));
                mem.wr(r + 0xA8, int32_t(2));
                mem.wr(r + 0xAC, int32_t(2));
                mem.wr(r + kRecBuildUp, int32_t(0));
                mem.wr(r + kRecApproach, int32_t(0));
                mem.wr(r + kRecFormation, int32_t(27));
                mem.wr(r + kRecSourceFormation, source_formation);
                mem.wr(r + kRecAudio, int32_t(18));
                mem.wr(r + 0xCC, int32_t(99));
                mem.wr(r + kRecSourceFormation2, source_formation);
            } else {
                mem.wr(r + kRecFormation, int32_t(-1));
                mem.wr(r + kRecSourceFormation, int32_t(0));
                mem.wr(r + 0xB4, int32_t(1));
            }
            mem.wr(r + kRecSlotBegin, sb);
            mem.wr(r + kRecSlotEnd, sb + 11 * kSlotStride);
            for (int s = 0; s < 11; ++s) {
                const uint64_t sl = sb + kSlotStride * uint64_t(s);
                if (i != 0) {
                    mem.wr(sl + kSlotPid, int32_t(-1));
                    continue;
                }
                const int32_t w = kRoles[size_t(s)];
                mem.wr(sl + kSlotPid, pids[size_t(s)]);
                mem.wr(sl + kSlotPosition, kPositions[size_t(s)]);
                const float x = 0.1f + 0.08f * float(s), y = 0.05f * float(s);
                mem.wr(sl + kSlotX, x);
                mem.wr(sl + kSlotY, y);
                mem.wr(sl + 0x10, x);
                mem.wr(sl + 0x14, x);
                mem.wr(sl + kSlotRole, w);
                mem.wr(sl + kSlotRoleId, int32_t(role_id_of(w)));
                mem.wr(sl + kSlotFocus, int32_t(focus_of(w)));
            }
        }
    }
    // One of Edit Tactic's working copies: a full tactic record inline in the service with its own 11 slots (the map record's players and words)
    uint64_t copy_slots(uint64_t off) const {
        for (size_t i = 0; i < kWorkCopyOffsets.size(); ++i)
            if (kWorkCopyOffsets[i] == off) return kCopySlots + 0x400 * uint64_t(i);
        return 0;
    }
    void put_copy(uint64_t off, const char* code, int32_t line, int32_t build_up, int32_t approach) {
        const uint64_t r = kSvc + off, sb = copy_slots(off);
        std::vector<uint8_t> zero(kRecStride, 0);
        mem.write(r, zero.data(), zero.size());
        mem.wr(r + kRecName, r + 0x10);
        mem.wr(r + kRecNameSize, uint32_t(3));
        mem.wr(r + 0x0C, uint32_t(0x80000050));
        mem.write(r + 0x10, "433", 4);
        mem.write(r + kRecCode, code, 12);
        mem.wr(r + 0x77, uint8_t(3));
        mem.wr(r + kRecActive, uint8_t(1));
        mem.wr(r + kRecPreset, int32_t(1));
        mem.wr(r + kRecLineHeight, line);
        mem.wr(r + 0xA8, int32_t(2));
        mem.wr(r + 0xAC, int32_t(2));
        mem.wr(r + kRecBuildUp, build_up);
        mem.wr(r + kRecApproach, approach);
        mem.wr(r + kRecFormation, int32_t(27));
        mem.wr(r + kRecSourceFormation, source_formation);
        mem.wr(r + kRecAudio, int32_t(18));
        mem.wr(r + 0xCC, int32_t(99));
        mem.wr(r + kRecSourceFormation2, source_formation);
        mem.wr(r + kRecSlotBegin, sb);
        mem.wr(r + kRecSlotEnd, sb + 11 * kSlotStride);
        for (int s = 0; s < 11; ++s) {
            const uint64_t sl = sb + kSlotStride * uint64_t(s);
            const int32_t w = kRoles[size_t(s)];
            mem.wr(sl + kSlotPid, pids[size_t(s)]);
            mem.wr(sl + kSlotPosition, kPositions[size_t(s)]);
            mem.wr(sl + kSlotRole, w);
            mem.wr(sl + kSlotRoleId, int32_t(role_id_of(w)));
            mem.wr(sl + kSlotFocus, int32_t(focus_of(w)));
        }
    }
    // an empty template (svc+0x1078 / +0x1268 in the dump): no name, no code, +0xB4 = 1, formation -1
    void put_template(uint64_t off) {
        const uint64_t r = kSvc + off, sb = copy_slots(off);
        std::vector<uint8_t> zero(kRecStride, 0);
        mem.write(r, zero.data(), zero.size());
        mem.wr(r + kRecName, r + 0x10);
        mem.wr(r + 0x0C, uint32_t(0x80000050));
        mem.wr(r + 0x77, uint8_t(0x0F));
        mem.wr(r + 0xA8, int32_t(2));
        mem.wr(r + 0xAC, int32_t(2));
        mem.wr(r + kRecApproach, int32_t(1));
        for (uint64_t o = 0xB8; o < 0xD4; o += 4) mem.wr(r + o, int32_t(-1));
        mem.wr(r + kRecSourceFormation, int32_t(0));
        mem.wr(r + kRecSlotBegin, sb);
        mem.wr(r + kRecSlotEnd, sb + 11 * kSlotStride);
        for (int s = 0; s < 11; ++s) mem.wr(sb + kSlotStride * uint64_t(s) + kSlotPid, int32_t(-1));
    }
    uint64_t copy_rec(uint64_t off) const { return kSvc + off; }
    uint64_t copy_slot(uint64_t off, int s) const { return copy_slots(off) + kSlotStride * uint64_t(s); }
    Anchors anchors() const {
        Anchors a;
        a.build = kBuild;
        a.vt1 = kVt1;
        a.vt2 = kVt2;
        a.set = a.set_body = kSet;
        a.update = a.update_body = kUpd;
        a.active = a.active_body = kAct;
        a.registry = a.registry_this = kRegistry;
        a.image_base = 0x140000000ULL;
        a.image_size = 0x2145C000ULL;
        return a;
    }
    uint64_t record() const { return kRecs; }
    uint64_t slot(int s) const { return kSlots0 + kSlotStride * uint64_t(s); }
    int32_t i32(uint64_t a) {
        int32_t v = 0;
        mem.rd(a, v);
        return v;
    }
    uint8_t u8(uint64_t a) {
        uint8_t v = 0;
        mem.rd(a, v);
        return v;
    }
    std::vector<Region> regions() const {
        std::vector<Region> rs;
        for (const auto& kv : mem.pages) rs.push_back({kv.first * SimMemory::kPage, (kv.first + 1) * SimMemory::kPage});
        return merge_regions(rs);
    }
    std::vector<uint8_t> bytes(uint64_t a, size_t n) {
        std::vector<uint8_t> b;
        mem.read_block(a, n, b);
        return b;
    }
};

// A memory that drops (or refuses) the writes to one address: the read-back then sees the old value
struct FlakyMemory : Memory {
    SimMemory& m;
    uint64_t drop = 0;
    bool refuse = false;
    int writes = 0;
    explicit FlakyMemory(SimMemory& mm) : m(mm) {}
    bool read(uint64_t a, void* out, size_t n) override { return m.read(a, out, n); }
    bool write(uint64_t a, const void* in, size_t n) override {
        ++writes;
        if (drop && a == drop) return !refuse;
        return m.write(a, in, n);
    }
};

// A memory where, while `on`, the writes to `drop` never land and a write to `once` lands the first time only (putting it back then fails)
struct StickyMemory : Memory {
    SimMemory& m;
    bool on = false;
    uint64_t drop = 0, once = 0;
    int once_writes = 0;
    explicit StickyMemory(SimMemory& mm) : m(mm) {}
    bool read(uint64_t a, void* out, size_t n) override { return m.read(a, out, n); }
    bool write(uint64_t a, const void* in, size_t n) override {
        if (on && a == drop) return true;
        if (on && a == once && once_writes++ > 0) return true;
        return m.write(a, in, n);
    }
};

// A simulated cm_mentalities table in the T3DB layout core/t3db.h walks (header: +0x30 first record, +0x40 shortname, +0x44 record size,
// +0x78 / +0x7A capacity, +0x7C rows written; records bit-packed, last byte bit 0x80 = deleted): fields at odd bit offsets, one with a minimum
// (teamsheetid -1), the 11 slot players / positions / role words of the live tactic
struct DbWorld {
    static constexpr uint64_t kHdr = 0x5A000000ULL, kRecs = 0x5A010000ULL;
    static constexpr uint32_t kRecSize = 0x60, kRows = 4;
    SimMemory& mem;
    Table t;
    explicit DbWorld(SimMemory& m) : mem(m) {
        t.name = "cm_mentalities";
        t.shortname = "cmme";
        t.header = kHdr;
        t.first_record = kRecs;
        t.record_size = kRecSize;
        t.written = kRows;
        t.capacity = kRows;
        uint32_t bit = 0;
        auto add = [&](const std::string& n, int depth, int64_t min = 0) {
            Field f;
            f.name = n;
            f.shortname = n.substr(0, 4);
            f.type = FieldType::Int;
            f.raw_type = 0;
            f.bit_offset = bit;
            f.depth = depth;
            f.min = min;
            t.by_name[n] = int(t.fields.size());
            t.fields.push_back(f);
            bit += uint32_t(depth);
        };
        add("artificialkey", 16);
        add("teamid", 16);
        add("activetactic", 1);
        add("sourceformationid", 11);
        add("teamsheetid", 32, -1);
        add("defensivedepth", 7);
        add("buildupplay", 2);
        for (int s = 0; s < 11; ++s) add("playerid" + std::to_string(s), 32);
        for (int s = 0; s < 11; ++s) add("position" + std::to_string(s), 5);
        for (int s = 0; s < 11; ++s) add("pos" + std::to_string(s) + "role", 16);
        mem.map(kHdr, 0x100);
        mem.write(kHdr + 0x40, "cmme", 4);
        mem.wr(kHdr + 0x30, kRecs);
        mem.wr(kHdr + 0x44, int32_t(kRecSize));
        mem.wr(kHdr + 0x78, uint16_t(kRows));
        mem.wr(kHdr + 0x7A, uint16_t(kRows));
        mem.wr(kHdr + 0x7C, uint16_t(kRows));
        mem.map(kRecs, kRecSize * kRows);
    }
    uint64_t rec(int i) const { return kRecs + uint64_t(kRecSize) * uint64_t(i); }
    const Field* field(const std::string& n) const { return t.field(n); }
    void set(uint64_t r, const std::string& n, int64_t v) {
        Database d(mem);
        d.set(t, r, *t.field(n), Value::of_int(v));
    }
    void mem_set(int i, const std::string& n, int64_t v) { set(rec(i), n, v); }
    bool get_ok(uint64_t r, const std::string& n, int64_t& v) {
        Database d(mem);
        Value x;
        const Field* f = t.field(n);
        if (!f || !d.get(t, r, *f, x) || x.type != FieldType::Int) return false;
        v = x.i;
        return true;
    }
    int64_t get(uint64_t r, const std::string& n) {
        int64_t v = INT64_MIN;
        get_ok(r, n, v);
        return v;
    }
    // row i: the live tactic of the world's club (its 11 players, positions and role words), this key, line height and build-up
    uint64_t row(int i, const SheetWorld& w, int64_t key, int64_t depth, int64_t buildup) {
        const uint64_t r = rec(i);
        set(r, "artificialkey", key);
        set(r, "teamid", w.team);
        set(r, "activetactic", 1);
        set(r, "sourceformationid", w.source_formation);
        set(r, "teamsheetid", -1);
        set(r, "defensivedepth", depth);
        set(r, "buildupplay", buildup);
        for (int s = 0; s < 11; ++s) {
            set(r, "playerid" + std::to_string(s), w.pids[size_t(s)]);
            set(r, "position" + std::to_string(s), SheetWorld::kPositions[size_t(s)]);
            set(r, "pos" + std::to_string(s) + "role", SheetWorld::kRoles[size_t(s)]);
        }
        return r;
    }
    // what ui_live_tactic.cpp builds (attach_db_row / attach_undo_db_row): the table, the row, its identity; the Apply's fields
    DbRowWrite bare(uint64_t r) {
        DbRowWrite x;
        x.table = t;
        x.rec = r;
        x.key = get(r, "artificialkey");
        for (const char* n : {"artificialkey", "teamid", "activetactic", "sourceformationid", "teamsheetid"}) x.checks.push_back({n, Value::of_int(get(r, n))});
        return x;
    }
    DbRowWrite target(uint64_t r, const Plan& p, const LiveTactic&) {
        DbRowWrite x = bare(r);
        for (int s = 0; s < 11; ++s)
            for (const std::string& n : {"playerid" + std::to_string(s), "position" + std::to_string(s)}) x.checks.push_back({n, Value::of_int(get(r, n))});
        x.fields = apply_row_fields(p, [&](const std::string& n, int64_t& v) { return get_ok(r, n, v); });
        return x;
    }
};

// The game's one address space, for the UI tests: the simulated sheet's pages from the SheetWorld, everything else (the career database the
// card reads and the job writes) from the app's memory (the GUI world image)
struct SplitMemory : Memory {
    SimMemory& sheet;
    Memory& rest;
    SplitMemory(SimMemory& s, Memory& r) : sheet(s), rest(r) {}
    bool in_sheet(uint64_t a, size_t n) const {
        for (uint64_t p = a / SimMemory::kPage; p <= (a + (n ? n - 1 : 0)) / SimMemory::kPage; ++p)
            if (!sheet.pages.count(p)) return false;
        return true;
    }
    bool read(uint64_t a, void* o, size_t n) override { return in_sheet(a, n) ? sheet.read(a, o, n) : rest.read(a, o, n); }
    bool write(uint64_t a, const void* i, size_t n) override { return in_sheet(a, n) ? sheet.write(a, i, n) : rest.write(a, i, n); }
};

static LiveTactic live(SheetWorld& w) {
    LiveTactic lt;
    read_live(w.mem, SheetWorld::kSvc, w.team, w.anchors(), lt);
    return lt;
}

// The role allow-list of the world (the other club's words; yours left out)
static RoleAllowList allow_of(SheetWorld& w) { return build_role_allow_list(w.mem, SheetWorld::kSvc, w.anchors(), w.team); }
static JobChecks checks(const RoleAllowList* al, const WriteHistory* h = nullptr) {
    JobChecks c;
    c.allow = al;
    c.history = h;
    return c;
}

// The game-thread dispatcher of a test: jobs wait until pump()
struct Jobs {
    std::vector<std::function<void()>> q;
    bool inline_run = false, report_none = false;  // report_none: keeps the job but says it is not queued (game_hooks.cpp's "" case)
    int ran = 0;
    std::string run(std::function<void()> f) {
        if (inline_run && !report_none) {
            ++ran;
            f();
        } else {
            q.push_back(std::move(f));
        }
        return report_none ? "" : "hook";
    }
    void pump() {
        auto jobs = std::move(q);
        q.clear();
        for (auto& f : jobs) {
            ++ran;
            f();
        }
    }
};

struct ServiceRig {
    SheetWorld& w;
    Jobs jobs;
    double clock = 1000.0;
    uint64_t find_returns = 0;
    int find_calls = 0;
    std::vector<std::string> logs;
    fs::path out;
    std::shared_ptr<CoreService> svc;
    ServiceRig(SheetWorld& world, const fs::path& out_dir, Memory* mem = nullptr, std::function<void(Host&)> tweak = nullptr) : w(world), out(out_dir) {
        std::error_code ec;
        fs::create_directories(out, ec);
        Host h;
        h.mem = mem ? mem : &w.mem;
        h.anchors = w.anchors();
        h.psm_vtable = SheetWorld::kPsmVt;
        h.out_dir = out;
        h.run_on_game_thread = [this](std::function<void()> f) { return jobs.run(std::move(f)); };
        h.run_background = [](std::function<void()> f) { f(); };
        h.regions = [this]() { return w.regions(); };
        h.call_find = [this](uint64_t fn, uint64_t reg, uint32_t id) -> uint64_t {
            ++find_calls;
            return (fn == SheetWorld::kFind && reg == SheetWorld::kRegistry && id == kServiceId) ? find_returns : 0;
        };
        h.log = [this](const std::string& s) { logs.push_back(s); };
        h.clock_us = [this]() { return clock; };
        h.wall_us = [this]() { return clock; };  // one clock in the tests (the host gives the system clock for the wall waits)
        if (tweak) tweak(h);
        svc = std::make_shared<CoreService>(std::move(h));
    }
    Context ctx(long long gen = 3, bool allow_find = true) const {
        Context c;
        c.load_gen = gen;
        c.user = w.team;
        c.managers = SheetWorld::kManagers;
        c.in_cm = true;
        c.allow_find = allow_find;
        c.hub = true;
        c.bench_hub = true;
        return c;
    }
    bool logged(const std::string& part) const {
        for (const auto& l : logs)
            if (l.find(part) != std::string::npos) return true;
        return false;
    }
    void touch(const char* name) { std::ofstream(out / name) << "x"; }
    void remove(const char* name) {
        std::error_code ec;
        fs::remove(out / name, ec);
    }
};

static bool has_part(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v)
        if (x.find(s) != std::string::npos) return true;
    return false;
}

// 2.15 bench keep: the world's sheet holds 31 players (100001..100031) at #0..#30, the XI = the record's 11 slot players, -1 from #31
using Ids52 = std::array<int32_t, kSheetPidCount>;
static Ids52 ids_of(SheetWorld& w) {
    Ids52 ids{};
    for (int k = 0; k < kSheetPidCount; ++k) ids[size_t(k)] = w.i32(SheetWorld::kSheet + kSheetPids + 4 * uint64_t(k));
    return ids;
}
// Team Management's working copy at svc+0x550 (P214 layout, BENCH_FINDINGS: teamsheetid -1, your team, current 1, slots 11, dirty 255,
// mode -1, its own record vector) holding `ids`
static void put_tm_copy(SheetWorld& w, const Ids52& ids, uint64_t build = 0x3BCE0000ULL, uint8_t dirty = 255, int team = 0) {
    const uint64_t e = SheetWorld::kSvc + kTmCopyTarget;
    w.mem.wr(e + kSheetId, int32_t(-1));
    w.mem.wr(e + kSheetTeam, int32_t(team ? team : w.team));
    w.mem.wr(e + 8, int32_t(-1));
    w.mem.wr(e + kSheetCurrent, uint8_t(1));
    for (int k = 0; k < kSheetPidCount; ++k) w.mem.wr(e + kSheetPids + 4 * uint64_t(k), ids[size_t(k)]);
    w.mem.wr(e + kSheetRecBegin, build);
    w.mem.wr(e + kSheetRecEnd, build + 5 * kRecStride);
    w.mem.wr(e + kSheetSlotCount, int32_t(11));
    w.mem.wr(e + kSheetDirty, dirty);
    w.mem.wr(e + kSheetMode, int32_t(-1));
}
static Ids52 copy_ids(SheetWorld& w) {
    Ids52 ids{};
    for (int k = 0; k < kSheetPidCount; ++k) ids[size_t(k)] = w.i32(SheetWorld::kSvc + kTmCopyTarget + kSheetPids + 4 * uint64_t(k));
    return ids;
}
static uint64_t copy_id_at(int k) { return SheetWorld::kSvc + kTmCopyTarget + kSheetPids + 4 * uint64_t(k); }
// Team Management opened: its copy holds the game's order (4 places of the bench / reserves moved), the map keeps the world's order (your
// saved one); the plan writes the map's order into the copy
static void tm_open_world(SheetWorld& w, BenchPlan& p) {
    const LiveTactic lt = live(w);
    p.user = w.team;
    p.sheet_id = lt.sheet_id;
    p.load_gen = 3;
    p.build = 0x3BCE0000ULL;
    p.saved = lt.sheet_ids;
    Ids52 s = lt.sheet_ids;
    std::swap(s[11], s[15]);
    std::swap(s[20], s[29]);
    put_tm_copy(w, s, p.build);
    p.before = s;
}

// --------------------------------------------------------------------------------------------------------------------------- core
static void run() {
    std::printf("native tactics channel E (live team sheet)\n");

    run_case("channel E: signatures - build 6AC07E31-2145C000 only, exact_build, left out by auto-adapt, guards on their function", [] {
        const SignatureTable* nb = builtin_signature_table("6AC07E31-2145C000");
        const SignatureTable* ob = builtin_signature_table("6AB9813C-211EF000");
        CHECK(nb && ob, "both tables");
        if (!nb || !ob) return;
        int exact = 0;
        for (const auto& s : nb->sigs) exact += s.exact_build ? 1 : 0;
        CHECK(exact == 11, fmt("11 exact-build entries on 6AC07E31 (%d)", exact));
        for (const auto& s : ob->sigs) CHECK(!s.exact_build && s.name.rfind("teamsheet_", 0) != 0, s.name + ": nothing of channel E on 6AB9813C");
        for (const char* n : anchor_signatures()) CHECK(nb->find(n) && nb->find(n)->exact_build, std::string(n) + " is built in for 6AC07E31");
        CHECK(nb->find("teamsheet_ctor") != nullptr, "the ctor too");
        const SignatureTable a = adapt_signature_table(*nb, "6B000000-21500000");
        CHECK(a.sigs.size() == nb->sigs.size() - 11 && !a.find("teamsheet_set") && !a.find("service_registry"), "auto-adapt leaves them out");
        for (const auto& s : nb->sigs) {
            if (!s.exact_build) continue;
            std::vector<uint8_t> b;
            std::vector<bool> m;
            CHECK(parse_pattern(s.pattern, b, m), s.name + ": pattern parses");
            CHECK(signature_offset_checked(s), s.name + ": offset rests on checked bytes");
        }
        // the report's offsets: VT1 / VT2 at +0x10 / +0x1A of the ctor, the registry at +0x13 / +0x25 of the match copy
        CHECK(nb->find("teamsheet_vt1")->offset == 0x10 && nb->find("teamsheet_vt2")->offset == 0x1A && nb->find("teamsheet_vt1")->expect == "48 8D 05",
              "VT1 / VT2: lea rax at +0x10 / +0x1A");
        CHECK(nb->find("service_registry")->offset == 0x13 && nb->find("service_registry")->expect == "48 8B 05" &&
                  nb->find("service_registry_this")->offset == 0x25 && nb->find("service_registry_this")->expect == "48 8D 0D",
              "the registry: mov rax at +0x13, lea rcx at +0x25");
        // JSON keeps the flag
        SignatureTable rt;
        std::string err;
        CHECK(parse_signature_table(signature_table_json(*nb), rt, err) && rt.find("teamsheet_set") && rt.find("teamsheet_set")->exact_build &&
                  !rt.find("game_tick")->exact_build,
              "exact_build survives the JSON round trip: " + err);
        CHECK(!parse_signature_table(R"({"build":"B","signatures":{"x":{"pattern":"90","exact_build":1}}})", rt, err) && err.find("exact_build") != std::string::npos,
              "a non-bool exact_build is refused: " + err);
        // each layout guard materialised (wildcards filled) resolves where its function's pattern does, once; the UPD guard's bytes
        // carry the report's 'expect' offsets +0x19 / +0x2F / +0x51 / +0x68
        struct Pair {
            const char* fn;
            const char* body;
        };
        for (const Pair& p : {Pair{"teamsheet_set", "teamsheet_set_body"}, Pair{"teamsheet_update", "teamsheet_update_body"},
                              Pair{"teamsheet_active", "teamsheet_active_body"}}) {
            std::vector<uint8_t> b;
            std::vector<bool> m;
            parse_pattern(nb->find(p.body)->pattern, b, m);
            for (size_t i = 0; i < b.size(); ++i)
                if (!m[i]) b[i] = 0x11;
            std::vector<uint8_t> code(0x1000, 0xCC);
            std::memcpy(code.data() + 0x400, b.data(), b.size());
            const uint64_t base = 0x145E40000ULL;
            const SigResult rf = resolve_signature(*nb->find(p.fn), code.data(), code.size(), base);
            const SigResult rb = resolve_signature(*nb->find(p.body), code.data(), code.size(), base);
            CHECK(rf.state == SigState::Found && rb.state == SigState::Found && rf.address == base + 0x400 && rb.address == rf.address,
                  std::string(p.body) + " sits on " + p.fn);
            if (std::string(p.body) == "teamsheet_update_body") {
                auto at = [&](size_t off, const std::vector<uint8_t>& want) { return std::equal(want.begin(), want.end(), b.begin() + long(off)); };
                CHECK(at(0x19, {0x80, 0xB9, 0x04, 0x14, 0x00, 0x00, 0x00}) && at(0x2F, {0x48, 0x8D, 0x8F, 0xB0, 0x13, 0x00, 0x00}) &&
                          at(0x51, {0x48, 0x81, 0xC3, 0x90, 0x02, 0x00, 0x00}) && at(0x68, {0xC6, 0x83, 0x88, 0x02, 0x00, 0x00, 0x01}),
                      "UpdateSheet: loaded +0x1404 at +0x19, map +0x13B0 at +0x2F, stride 0x290 at +0x51, dirty = 1 at +0x68");
            }
            // a different byte where the guard checks (another layout) leaves the guard unmatched while the function still matches
            code[0x400 + b.size() - 1] ^= 0xFF;
            CHECK(resolve_signature(*nb->find(p.body), code.data(), code.size(), base).state == SigState::Missing, std::string(p.body) + ": another layout misses");
        }
    });

    run_case("channel E: anchors (G3) - the verified build, every signature, guards and the vtable slots agree", [] {
        SheetWorld w;
        const Anchors a = w.anchors();
        CHECK(check_anchors(w.mem, a).empty(), "the simulated build passes: " + check_anchors(w.mem, a));
        Anchors b = a;
        b.build = "6B000000-21500000";
        CHECK(check_anchors(w.mem, b).find("is not 6AC07E31-2145C000") != std::string::npos, "another build is refused: " + check_anchors(w.mem, b));
        b = a;
        b.vt2 = 0;
        CHECK(check_anchors(w.mem, b).find("teamsheet_vt2") != std::string::npos, "a missing signature is named");
        b = a;
        b.update_body = a.update + 0x40;
        CHECK(check_anchors(w.mem, b).find("teamsheet_update_body") != std::string::npos, "a guard elsewhere is refused");
        b = a;
        b.registry_this = a.registry + 8;
        CHECK(check_anchors(w.mem, b).find("registry") != std::string::npos, "the registry operands must agree");
        w.mem.wr(SheetWorld::kVt1 + 8 * 24, uint64_t(0x145E43A61ULL));
        CHECK(check_anchors(w.mem, a).find("VT1 slot 24") != std::string::npos, "SetSheet must be VT1[24]: " + check_anchors(w.mem, a));
        w.mem.wr(SheetWorld::kVt1 + 8 * 24, SheetWorld::kSet);
        w.mem.wr(SheetWorld::kVt1 + 8 * 182, uint64_t(0));
        CHECK(check_anchors(w.mem, a).find("VT1 slot 182") != std::string::npos, "UpdateSheet must be VT1[182]");
        std::string why;
        CHECK(registry_find_fn(w.mem, a, why) == SheetWorld::kFind, "the registry's Find: vtable +0x38 " + why);
        w.mem.wr(SheetWorld::kRegVt + 0x38, uint64_t(0x7FF600001000ULL));
        CHECK(registry_find_fn(w.mem, a, why) == 0 && why.find("not inside FC27.exe") != std::string::npos, "a Find outside the exe is not called");
    });

    run_case("channel E: L1-L4 and the read-out of the simulated sheet (report 2.5-2.9)", [] {
        SheetWorld w;
        LiveTactic lt;
        const std::string why = read_live(w.mem, SheetWorld::kSvc, 95, w.anchors(), lt);
        CHECK(why.empty(), "read: " + why);
        CHECK(lt.at.node == SheetWorld::kNodeB && lt.at.hops == 2 && lt.at.sheet == SheetWorld::kSheet && lt.at.sheets == 1, "the node after another key, one sheet");
        CHECK(lt.at.records == 5 && lt.at.record_index == 0 && lt.at.record == SheetWorld::kRecs && lt.at.slots == SheetWorld::kSlots0, "five records, record 0 active");
        CHECK(lt.at.header.bucket_count == 887 && lt.at.header.node_count == 841 && lt.at.header.last_team == 1939 && lt.at.header.active_sheet == -1, "the header");
        CHECK(lt.name == "433" && lt.line_height == 25 && lt.build_up == 0 && lt.approach == 0 && lt.preset == 1 && lt.formation == 27 &&
                  lt.source_formation == 36 && lt.audio == 18 && lt.code == SheetWorld::kCode,
              "\"433\", line height 25, build-up 0, approach 0, preset 1, formation 27 / 36 / 18, code ?ZaE&CYzKoT7");
        CHECK(lt.copies.size() == 4 && lt.matching_copies() == 2 && lt.copy(0xF80) && lt.copy(0xF80)->matches && lt.copy(0x1170)->matches &&
                  !lt.copy(0x1078)->matches && lt.copy(0x1078)->why.find("empty template") != std::string::npos && !lt.copy(0x1268)->matches,
              "Edit Tactic's working copies: +0xF80 and +0x1170 hold this tactic, the templates do not");
        CHECK(lt.sheet_id == -1 && lt.mode == -1 && lt.dirty == 1 && lt.squad.size() == 31, "sheet -1, mode -1, dirty 1, 31 players");
        CHECK(lt.sheet_ids[0] == 100001 && lt.sheet_ids[30] == 100031 && lt.sheet_ids[31] == -1 && lt.sheet_ids[51] == -1,
              "the sheet's 52 ids in the game's order, -1 kept (the bench probe)");
        const SlotView& st = lt.slots[9];
        CHECK(st.role == 38213 && st.group() == 9 && st.role_id == 21 && st.focus == 5 && st.position == 25, "ST: 38213 = group 9 / role 21 / focus 5");
        CHECK(lt.slots[0].role == 4227 && lt.slots[0].group() == 1 && lt.slots[0].role_id == 2 && lt.slots[0].focus == 3, "GK: 4227 = 1 / 2 / 3");
        CHECK(role_word(9, 21, 5) == 38213 && role_word(4, 30, 2) == 18306 && role_word(8, 16, 5) == 33797, "the encoding of report 2.9");
        CHECK(line_band(25) == 0 && line_band(31) == 1 && line_band(75) == 2 && line_band(91) == 3 && line_band_text(75) == "band 2 (61-90)", "line height bands");
        CHECK(check_g5(w.mem, lt).empty(), "G5: mode -1 passes");
    });

    run_case("channel E: a vtable-pair hit alone is not enough - the stale copy, two live services, the registry Find's result", [] {
        SheetWorld w;
        const Anchors a = w.anchors();
        CHECK(check_service(w.mem, SheetWorld::kStale, a).find("loaded flag") != std::string::npos, "the freed copy fails the header checks");
        std::vector<uint8_t> buf(0x2000, 0);
        uint64_t p1 = SheetWorld::kVt1, p2 = SheetWorld::kVt2;
        std::memcpy(buf.data() + 0x100, &p1, 8);
        std::memcpy(buf.data() + 0x108, &p2, 8);
        std::memcpy(buf.data() + 0x804, &p1, 8);  // unaligned: not an object start
        std::memcpy(buf.data() + 0x80C, &p2, 8);
        std::memcpy(buf.data() + 0x1000, &p1, 8);
        CHECK(find_vtable_pairs(buf.data(), buf.size(), 0x10000000ULL, p1, p2, 10) == std::vector<uint64_t>({0x10000100ULL}), "8-aligned pairs only, both qwords");
        uint64_t svc = 0;
        CHECK(pick_service(w.mem, {SheetWorld::kStale, SheetWorld::kSvc}, a, svc).empty() && svc == SheetWorld::kSvc, "the live one is picked");
        CHECK(!pick_service(w.mem, {SheetWorld::kStale}, a, svc).empty() && svc == 0, "only the stale copy: none");
        w.mem.wr(SheetWorld::kStale + kSvcLoaded, uint8_t(1));
        w.mem.wr(SheetWorld::kStale + kSvcBucketCount, uint32_t(887));
        w.mem.wr(SheetWorld::kStale + kSvcNodeCount, uint32_t(800));
        w.mem.wr(SheetWorld::kStale + kSvcBuckets, SheetWorld::kBuckets);
        const std::string two = pick_service(w.mem, {SheetWorld::kStale, SheetWorld::kSvc}, a, svc);
        CHECK(two.find("2 objects") != std::string::npos && svc == 0, "two live-looking services: refused, not guessed: " + two);
        CHECK(accept_find_result(w.mem, SheetWorld::kSvc, a, svc).empty() && svc == SheetWorld::kSvc, "Find returned the service");
        CHECK(accept_find_result(w.mem, SheetWorld::kSvc + 8, a, svc).empty() && svc == SheetWorld::kSvc, "Find returned its second base");
        CHECK(!accept_find_result(w.mem, SheetWorld::kNodeB, a, svc).empty() && svc == 0, "something else: refused");
        CHECK(!accept_find_result(w.mem, 0, a, svc).empty(), "null: refused");
        int user = 0;
        CHECK(user_from_psm(w.mem, SheetWorld::kManagers, SheetWorld::kPsmVt, user).empty() && user == 95, "PSM +0x10 = 95");
        CHECK(!user_from_psm(w.mem, SheetWorld::kManagers, 0x14AFFD8A0ULL, user).empty() && user == 0, "another PSM vtable: refused");
        CHECK(!user_from_psm(w.mem, 0, SheetWorld::kPsmVt, user).empty(), "no manager table: refused");
        w.mem.wr(SheetWorld::kPsm + 0x08, uint64_t(0x6A900000ULL));
        CHECK(user_from_psm(w.mem, SheetWorld::kManagers, SheetWorld::kPsmVt, user).find("is not the career's") != std::string::npos && user == 0,
              "a PSM of another manager table: refused");
        w.mem.wr(SheetWorld::kPsm + 0x08, SheetWorld::kManagers);
        w.mem.wr(SheetWorld::kManagers + 0x20 * 87 + 0x10, int32_t(2));
        CHECK(user_from_psm(w.mem, SheetWorld::kManagers, SheetWorld::kPsmVt, user).find("2 instances") != std::string::npos, "two instances: refused");
    });

    run_case("channel E: every locate check fails closed with 'layout mismatch (...)'", [] {
        struct Case {
            const char* name;
            std::function<void(SheetWorld&)> mutate;
            const char* expect;
        };
        const std::vector<Case> cases = {
            {"loaded flag 0", [](SheetWorld& w) { w.mem.wr(SheetWorld::kSvc + kSvcLoaded, uint8_t(0)); }, "loaded flag"},
            {"bucket count 0", [](SheetWorld& w) { w.mem.wr(SheetWorld::kSvc + kSvcBucketCount, uint32_t(0)); }, "bucket count"},
            {"node count too high", [](SheetWorld& w) { w.mem.wr(SheetWorld::kSvc + kSvcNodeCount, uint32_t(887 * 4 + 1)); }, "node count"},
            {"a node chain that loops", [](SheetWorld& w) { w.mem.wr(SheetWorld::kNodeA + kNodeNext, SheetWorld::kNodeA); }, "more than 64 nodes"},
            {"the club has no node", [](SheetWorld& w) { w.mem.wr(SheetWorld::kNodeB + kNodeKey, int32_t(5)); }, "has no sheet"},
            {"a vector not of 0x290 sheets", [](SheetWorld& w) { w.mem.wr(SheetWorld::kNodeB + kNodeEnd, SheetWorld::kSheet + 0x200); }, "sheet vector"},
            {"no current sheet", [](SheetWorld& w) { w.mem.wr(SheetWorld::kSheet + kSheetCurrent, uint8_t(0)); }, "0 current sheets"},
            {"10 slots", [](SheetWorld& w) { w.mem.wr(SheetWorld::kSheet + kSheetSlotCount, int32_t(10)); }, "0 current sheets"},
            {"a dirty flag of 2", [](SheetWorld& w) { w.mem.wr(SheetWorld::kSheet + kSheetDirty, uint8_t(2)); }, "0 current sheets"},
            {"a record vector not of 0xF8 records", [](SheetWorld& w) { w.mem.wr(SheetWorld::kSheet + kSheetRecEnd, SheetWorld::kRecs + 0x100); }, "record vector"},
            {"no active record", [](SheetWorld& w) { w.mem.wr(SheetWorld::kRecs + kRecActive, uint8_t(0)); }, "0 active tactic records"},
            {"two active records", [](SheetWorld& w) {
                 w.mem.wr(SheetWorld::kRecs + kRecStride + kRecActive, uint8_t(1));
                 w.mem.wr(SheetWorld::kRecs + kRecStride + kRecSourceFormation, int32_t(36));
             }, "2 active tactic records"},
            {"source formation +0xC4 != +0xD0", [](SheetWorld& w) { w.mem.wr(SheetWorld::kRecs + kRecSourceFormation2, int32_t(37)); }, "sourceformationid"},
            {"10 slots in the record", [](SheetWorld& w) { w.mem.wr(SheetWorld::kRecs + kRecSlotEnd, SheetWorld::kSlots0 + 10 * kSlotStride); }, "slot vector"},
            {"a slot player not in the sheet's list", [](SheetWorld& w) { w.mem.wr(w.slot(4) + kSlotPid, int32_t(999999)); }, "not in the sheet's player list"},
            {"a role id mirror that disagrees", [](SheetWorld& w) { w.mem.wr(w.slot(9) + kSlotRoleId, int32_t(20)); }, "slot 9 role word"},
            {"a focus mirror that disagrees", [](SheetWorld& w) { w.mem.wr(w.slot(2) + kSlotFocus, int32_t(2)); }, "slot 2 role word"},
            {"the service's vtables", [](SheetWorld& w) { w.mem.wr(SheetWorld::kSvc + 8, uint64_t(0)); }, "vtables"},
        };
        for (const Case& c : cases) {
            SheetWorld w;
            c.mutate(w);
            Located at;
            const std::string why = locate(w.mem, SheetWorld::kSvc, 95, w.anchors(), at);
            CHECK(why.rfind("layout mismatch (", 0) == 0 && why.find(c.expect) != std::string::npos, std::string(c.name) + ": " + why);
        }
        SheetWorld two;  // two current sheets of the club
        two.mem.map(SheetWorld::kSheet + kSheetStride, kSheetStride);
        two.put_sheet(SheetWorld::kSheet + kSheetStride, 3, true, -1, 0);
        two.mem.wr(SheetWorld::kNodeB + kNodeEnd, SheetWorld::kSheet + 2 * kSheetStride);
        Located at;
        CHECK(locate(two.mem, SheetWorld::kSvc, 95, two.anchors(), at).find("2 current sheets") != std::string::npos, "two current sheets: refused");
        two.put_sheet(SheetWorld::kSheet + kSheetStride, 3, false, -1, 0);
        CHECK(locate(two.mem, SheetWorld::kSvc, 95, two.anchors(), at).empty() && at.sheet == SheetWorld::kSheet && at.sheets == 2, "a second, not current sheet is fine");
    });

    run_case("channel E: G5 - only mode -1 is written (decision WP-B1); the active sheet of a mode names vf 0xF8", [] {
        SheetWorld w;
        CHECK(check_g5(w.mem, live(w)).empty(), "mode -1: pass");
        w.mem.wr(SheetWorld::kSheet + kSheetMode, int32_t(0));
        w.mem.wr(SheetWorld::kSheet + kSheetId, int32_t(4));
        LiveTactic lt = live(w);
        int32_t id = 0;
        CHECK(active_sheet_id(w.mem, lt.at, 0, id) && id == 4, "GetActiveSheetId's loop: sheet 4");
        CHECK(check_g5(w.mem, lt).find("vf 0xF8") != std::string::npos, "mode 0, the active sheet: refused");
        CHECK(active_sheet_id(w.mem, lt.at, 1, id) && id == -1, "another mode: none");
        // a sheet that cannot be read: refused, not "none"
        LiveTactic bad = lt;
        bad.at.vec_begin = 0x7FF000000000ULL;
        CHECK(!active_sheet_id(w.mem, bad.at, 0, id) && check_g5(w.mem, bad).find("could not read") != std::string::npos, "unreadable: refused");
        // another current sheet of mode 0 comes first: GetActiveSheetId names it, so SetSheet would not propagate this one
        w.mem.map(SheetWorld::kSheet + kSheetStride, kSheetStride);
        std::vector<uint8_t> e = w.bytes(SheetWorld::kSheet, kSheetStride);
        w.mem.write(SheetWorld::kSheet + kSheetStride, e.data(), e.size());
        w.mem.wr(SheetWorld::kSheet + kSheetTeam, int32_t(96));  // the first element: not the club's (L3 still finds exactly one)
        w.mem.wr(SheetWorld::kSheet + kSheetId, int32_t(2));
        w.mem.wr(SheetWorld::kNodeB + kNodeEnd, SheetWorld::kSheet + 2 * kSheetStride);
        lt = live(w);
        CHECK(lt.at.sheet == SheetWorld::kSheet + kSheetStride && active_sheet_id(w.mem, lt.at, 0, id) && id == 2 &&
                  check_g5(w.mem, lt).find("sheet mode 0: only mode -1 (your club's own sheet) is written (decision WP-B1: mode > -1 refused)") == 0,
              "2.4: not the active one of its mode, still refused (mode > -1): " + check_g5(w.mem, lt));
        // in the job: mode 0, not the active sheet -> stage G5, nothing written
        Request r;
        r.approach = 2;
        r.line_height = 70;
        Plan p;
        CHECK(make_plan(lt, r, p, nullptr).empty(), "plan");
        const std::vector<uint8_t> recs_before = w.bytes(SheetWorld::kRecs, 5 * kRecStride);
        const ApplyResult res = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p);
        CHECK(!res.ok && !res.written && res.stage == "G5" && res.message.find("mode > -1 refused") != std::string::npos &&
                  w.bytes(SheetWorld::kRecs, 5 * kRecStride) == recs_before && w.u8(SheetWorld::kSheet + kSheetStride + kSheetDirty) == 1,
              "the job refuses a mode-0 sheet: " + res.stage + " " + res.message);
        // mode 1 too (any mode > -1)
        w.mem.wr(SheetWorld::kSheet + kSheetStride + kSheetMode, int32_t(1));
        CHECK(!check_g5(w.mem, live(w)).empty(), "mode 1: refused");
        w.mem.wr(SheetWorld::kSheet + kSheetStride + kSheetMode, int32_t(-1));
        CHECK(check_g5(w.mem, live(w)).empty(), "back to mode -1: pass");
    });

    run_case("channel E: G6 - value ranges, known enum values, the group stays, preset and formation refused, the plan", [] {
        SheetWorld w;
        const LiveTactic lt = live(w);
        const RoleAllowList al = allow_of(w);
        Plan p;
        auto plan = [&](Request r) { return make_plan(lt, r, p, &al); };
        Request r;
        r.build_up = 4;
        CHECK(plan(r).find("outside 0..3") != std::string::npos, "build-up 4");
        r.build_up = 2;
        CHECK(plan(r).find("no known label") != std::string::npos && plan(r).find("Short Passing") != std::string::npos, "build-up 2: unknown, refused: " + plan(r));
        r.build_up = 3;
        CHECK(plan(r).find("no known label") != std::string::npos, "build-up 3: unknown, refused");
        r = Request();
        r.approach = 4;
        CHECK(plan(r).find("outside 0..3") != std::string::npos, "approach 4: refused");
        r.approach = -2;
        CHECK(plan(r).find("outside 0..3") != std::string::npos, "approach -2: refused");
        r = Request();
        r.line_height = 0;
        CHECK(plan(r).find("outside 1..100") != std::string::npos, "line height 0");
        r.line_height = 101;
        CHECK(plan(r).find("outside 1..100") != std::string::npos, "line height 101");
        r = Request();
        r.slots = {{9, -1, 8}};
        CHECK(plan(r).find("focus 8") != std::string::npos, "focus 8");
        r.slots = {{9, 64, -1}};
        CHECK(plan(r).find("role id 64") != std::string::npos, "role id 64");
        r.slots = {{11, 1, 1}};
        CHECK(plan(r).find("outside 0..10") != std::string::npos, "slot 11");
        r.slots = {{9, -1, 6}, {9, -1, 4}};
        CHECK(plan(r).find("named twice") != std::string::npos, "a slot twice");
        r = Request();
        r.preset = true;
        CHECK(plan(r).find("preset") != std::string::npos && plan(r).find("not written") != std::string::npos, "preset refused");
        r = Request();
        r.formation = true;
        CHECK(plan(r).find("formation") != std::string::npos, "formation refused");
        // the labels (findings 1 + the game's text): known values only
        CHECK(build_up_styles().size() == 2 && build_up_text(0) == "Balanced (0)" && build_up_text(1) == "Counter (1)" && build_up_text(2) == "unknown value 2",
              "Build-Up Style: Balanced 0, Counter 1; 2 unknown");
        CHECK(defensive_approaches().size() == 4 && approach_text(0) == "Deep (0)" && approach_text(1) == "Balanced (1)" && approach_text(2) == "High (2)" &&
                  approach_text(3) == "Aggressive (3)" && approach_text(4) == "unknown value 4",
              "Defensive Approach: Deep, Balanced, High, Aggressive");
        CHECK(defensive_approach(0)->seen && defensive_approach(1)->seen && !defensive_approach(3)->seen && defensive_approach(1)->default_line == 50 &&
                  defensive_approach(1)->default_line_seen && defensive_approach(3)->default_line == 95 && !defensive_approach(3)->default_line_seen,
              "seen / defaults (BALANCED showed 50)");
        // 2.4: Build-Up Style 2 / 3 and Aggressive (3) stay refused; Turbo writes the approach band-consistent, so up to Line Height 90
        CHECK(build_up_writable(0) && build_up_writable(1) && !build_up_writable(2) && !build_up_writable(3) && approach_writable(0) &&
                  approach_writable(1) && approach_writable(2) && !approach_writable(3) && !approach_writable(4) && max_writable_line() == 90,
              "writable: build-up 0 / 1, approach 0..2, line height up to 90");
        r = Request();
        r.approach = 3;
        r.line_height = 95;
        CHECK(plan(r).find("Line Height 95 lies in Aggressive's band") != std::string::npos && plan(r).find("up to 90") != std::string::npos,
              "Aggressive's band refused: " + plan(r));
        r.line_height = 90;
        CHECK(plan(r).find("Defensive Approach Aggressive (3): its stored value is not decoded yet") == 0, "Aggressive refused: " + plan(r));
        r = Request();
        r.approach = 2;
        CHECK(plan(r).find("is not Line Height 25's band") != std::string::npos && plan(r).find("band-consistent") != std::string::npos,
              "High with line 25: refused (band): " + plan(r));
        r.line_height = 61;
        CHECK(plan(r).empty() && p.writes.size() == 3 && p.writes[0].new_value == 2 && p.writes[1].new_value == 61, "High with 61: consistent");
        r = Request();
        r.line_height = 30;
        CHECK(plan(r).empty() && p.writes.size() == 3 && p.writes[0].new_value == 0, "Deep stays with 30");
        r.line_height = 31;
        CHECK(plan(r).find("band-consistent") != std::string::npos, "31 is Balanced's band: the approach must follow");
        // the game's own Aggressive / 95 (or 2.3's write) is written back unchanged when only the build-up changes
        LiveTactic ag = lt;
        ag.approach = 3;
        ag.line_height = 95;
        r = Request();
        r.build_up = 1;
        CHECK(make_plan(ag, r, p, &al).empty() && p.writes.size() == 3 && p.writes[0].new_value == 3 && p.writes[1].new_value == 95 && p.writes[2].new_value == 1,
              "the game's 95 / Aggressive kept: " + make_plan(ag, r, p, &al));
        // a live record whose approach is not its line's band: a build-up change alone is refused (Turbo would write the pair as it is)
        LiveTactic odd_band = lt;
        odd_band.approach = 1;
        CHECK(make_plan(odd_band, r, p, &al).find("Defensive Approach Balanced (1) is not Line Height 25's band") == 0, "inconsistent live pair: refused");
        // a live value without a label is written back unchanged when only another field changes (it is the game's own value)
        LiveTactic sp = lt;
        sp.build_up = 3;
        r = Request();
        r.approach = 1;
        r.line_height = 50;
        CHECK(make_plan(sp, r, p, &al).empty() && p.writes.size() == 3 && p.writes[2].kind == FieldKind::BuildUp && p.writes[2].old_value == 3 &&
                  p.writes[2].new_value == 3,
              "the game's build-up 3 kept: " + make_plan(sp, r, p, &al));
        // a word above the save's range (group 14: role ids above 41)
        LiveTactic hi = lt;
        hi.slots[3].role = role_word(14, 41, 7);
        hi.slots[3].role_id = 41;
        hi.slots[3].focus = 7;
        r = Request();
        r.slots = {{3, 41, 7}};
        CHECK(make_plan(hi, r, p, &al).empty() && p.writes.empty(), "group 14 / role 41 / focus 7 = 59975: allowed, unchanged");
        r.slots = {{3, 42, 0}};
        CHECK(make_plan(hi, r, p, &al).find("above 60000") != std::string::npos, "group 14 / role 42: the save could not store it");
        LiveTactic odd = lt;
        odd.slots[5].role |= 0x08;
        r.slots = {{5, -1, 1}};
        CHECK(make_plan(odd, r, p, &al).find("bits 3..5") != std::string::npos, "an encoding never seen is not written");
        // the role allow-list: a triple no other club uses is refused; without the list no role / focus edit at all
        r = Request();
        r.slots = {{9, -1, 4}};
        CHECK(plan(r).find("never seen in the game's own sheets") != std::string::npos, "group 9 / role 21 / focus 4: refused: " + plan(r));
        r.slots = {{9, -1, 6}};
        CHECK(make_plan(lt, r, p, nullptr).find("allow-list is not built") != std::string::npos, "no list: refused");
        RoleAllowList none;
        none.why = "the walk ran out of time";
        CHECK(make_plan(lt, r, p, &none).find("ran out of time") != std::string::npos, "a failed list: refused with its reason");
        r = Request();
        r.line_height = 60;
        r.approach = 1;
        CHECK(make_plan(lt, r, p, nullptr).empty() && p.writes.size() == 3, "line height stays editable without the list (with the other two)");
        // nothing changes
        r = Request();
        r.build_up = 0;
        r.line_height = 25;
        r.approach = 0;
        r.slots = {{9, 21, 5}};
        CHECK(plan(r).empty() && p.writes.empty(), "the same values: an empty plan");
        // the 2.4 acceptance change: Deep -> High, line height 90 (High's band), Counter, the striker's focus 5 -> 6
        r = Request();
        r.approach = 2;
        r.line_height = 90;
        r.build_up = 1;
        r.slots = {{9, -1, 6}};
        CHECK(plan(r).empty() && p.writes.size() == 6, fmt("approach, line height, build-up, the three role values (%zu writes)", p.writes.size()));
        CHECK(p.writes[0].kind == FieldKind::Approach && p.writes[0].old_value == 0 && p.writes[0].new_value == 2 && p.writes[1].kind == FieldKind::LineHeight &&
                  p.writes[1].old_value == 25 && p.writes[1].new_value == 90 && p.writes[2].kind == FieldKind::BuildUp && p.writes[2].new_value == 1 &&
                  p.writes[3].kind == FieldKind::Role && p.writes[3].slot == 9 && p.writes[3].old_value == 38213 && p.writes[3].new_value == 38214 &&
                  p.writes[4].kind == FieldKind::RoleId && p.writes[4].new_value == 21 && p.writes[5].kind == FieldKind::Focus && p.writes[5].new_value == 6,
              "approach 0->2, line height 25->90, build-up 0->1, slot 9 38213->38214 (21 / 6)");
        CHECK(p.user == 95 && p.record_index == 0 && p.formation == 27 && p.source_formation == 36 && p.sheet_id == -1 && p.pids[9] == 100010 &&
                  p.code == SheetWorld::kCode,
              "the plan names its record and code");
        // only the build-up changes: all three are in the plan (written together), the other two with their live values
        r = Request();
        r.build_up = 1;
        CHECK(plan(r).empty() && p.writes.size() == 3 && p.writes[0].new_value == 0 && p.writes[1].old_value == 25 && p.writes[1].new_value == 25 &&
                  p.writes[2].new_value == 1,
              "the three record fields together");
        // the job checks the same rules on a plan it is handed (a forged or stale plan): together, band-consistent, writable
        Plan forged = p;
        forged.writes.pop_back();
        CHECK(check_plan(lt, forged, checks(&al)).find("written together or not at all") != std::string::npos, "two of three: refused in the job");
        forged = p;
        forged.writes[1].new_value = 80;
        CHECK(check_plan(lt, forged, checks(&al)).find("is not Line Height 80's band") != std::string::npos, "an inconsistent pair: refused in the job");
        forged.writes[0].new_value = 3;
        forged.writes[1].new_value = 95;
        CHECK(check_plan(lt, forged, checks(&al)).find("Defensive Approach Aggressive (3): its stored value is not decoded yet") == 0, "Aggressive in the job");
        forged.writes[0].new_value = 2;
        CHECK(check_plan(lt, forged, checks(&al)).find("Line Height 95 lies in Aggressive's band") == 0, "Aggressive's band in the job");
    });

    run_case("channel E: the write - approach, line height and build-up together, the map record only (no Edit Tactic copy), read back, nothing else", [] {
        SheetWorld w;
        const LiveTactic lt = live(w);
        Request r;
        r.approach = 2;
        r.line_height = 90;
        r.build_up = 1;
        r.slots = {{9, -1, 6}};
        Plan p;
        const RoleAllowList al = allow_of(w);
        const JobChecks ck = checks(&al);
        CHECK(make_plan(lt, r, p, &al).empty(), "plan");
        w.mem.wr(SheetWorld::kSheet + kSheetDirty, uint8_t(0));  // as after a save
        const std::vector<uint8_t> svc_before = w.bytes(SheetWorld::kSvc, kSvcSize), sheet_before = w.bytes(SheetWorld::kSheet, kSheetStride),
                                   rec_before = w.bytes(SheetWorld::kRecs, 5 * kRecStride), slots_before = w.bytes(SheetWorld::kSlots0, 5 * 0x1000);
        const int reads_before = w.mem.failed_reads;
        struct CountingSim : Memory {
            SimMemory& m;
            int reads = 0, writes = 0;
            explicit CountingSim(SimMemory& mm) : m(mm) {}
            bool read(uint64_t a, void* o, size_t n) override {
                ++reads;
                return m.read(a, o, n);
            }
            bool write(uint64_t a, const void* i, size_t n) override {
                ++writes;
                return m.write(a, i, n);
            }
        } counting(w.mem);
        const ApplyResult res = apply_plan(counting, SheetWorld::kSvc, w.anchors(), p, ck);
        CHECK(res.ok && res.written && !res.undone && res.stage == "done", "written: " + res.message);
        CHECK(res.log_line ==
                  "channel E: team 95 record 0 approach 0->2 line height 25->90 build-up 0->1 slot 9 role 38213->38214 (read back ok) [record now LH 90 BU 1 DA 2]",
              "log: " + res.log_line);
        CHECK(res.copies_written == 0 && res.copies.empty(), "no Edit Tactic copy (2.4): " + res.copies);
        // the walks read whole pages (the working copies share the service's page); the snapshot and the read-back read each field itself
        CHECK(res.reads == counting.reads && res.reads > 0 && res.reads <= 80 && counting.writes == 7 && w.mem.failed_reads == reads_before,
              fmt("%d reads (%d counted), %d writes: 6 fields + the dirty flag", res.reads, counting.reads, counting.writes));
        CHECK(w.i32(SheetWorld::kRecs + kRecApproach) == 2 && w.i32(SheetWorld::kRecs + kRecLineHeight) == 90 && w.i32(SheetWorld::kRecs + kRecBuildUp) == 1,
              "the map record: High, 90, Counter");
        for (uint64_t c : {uint64_t(0xF80), uint64_t(0x1170)})
            CHECK(w.i32(w.copy_rec(c) + kRecApproach) == 0 && w.i32(w.copy_rec(c) + kRecLineHeight) == 25 && w.i32(w.copy_rec(c) + kRecBuildUp) == 0 &&
                      w.i32(w.copy_slot(c, 9) + kSlotRole) == 38213 && w.i32(w.copy_slot(c, 9) + kSlotFocus) == 5,
                  fmt("Edit Tactic's working copy svc+0x%llX is not written (2.4)", (unsigned long long)c));
        CHECK(w.i32(w.slot(9) + kSlotRole) == 38214 && w.i32(w.slot(9) + kSlotRoleId) == 21 && w.i32(w.slot(9) + kSlotFocus) == 6, "slot 9: 38214 / 21 / 6");
        CHECK(w.u8(SheetWorld::kSheet + kSheetDirty) == 1 && res.dirty_before == 0 && res.dirty_after == 1, "dirty 0 -> 1");
        CHECK(res.after.line_height == 90 && res.after.approach == 2 && res.after.slots[9].focus == 6 && res.after.copy(0xF80)->approach == 0,
              "the read-out after the write");
        // byte for byte: only the written fields and the dirty flag changed
        auto diff = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
            std::vector<size_t> d;
            for (size_t i = 0; i < a.size() && i < b.size(); ++i)
                if (a[i] != b[i]) d.push_back(i);
            return d;
        };
        // the service: untouched (its cache +0x13A0 / +0x13A4, the working copies, the templates)
        CHECK(diff(svc_before, w.bytes(SheetWorld::kSvc, kSvcSize)).empty(), "the service: not one byte changed");
        CHECK(diff(sheet_before, w.bytes(SheetWorld::kSheet, kSheetStride)) == std::vector<size_t>({kSheetDirty}), "the sheet: only the dirty flag");
        std::vector<size_t> rd = diff(rec_before, w.bytes(SheetWorld::kRecs, 5 * kRecStride));
        CHECK(rd == std::vector<size_t>({kRecLineHeight, kRecBuildUp, kRecApproach}), fmt("the records: only the three fields (%zu bytes)", rd.size()));
        std::vector<size_t> sd = diff(slots_before, w.bytes(SheetWorld::kSlots0, 5 * 0x1000));
        bool only_slot9 = !sd.empty();
        for (size_t i : sd) only_slot9 = only_slot9 && i >= 9 * kSlotStride + kSlotRole && i < 9 * kSlotStride + kSlotFocus + 4;
        CHECK(only_slot9, "the slots: only slot 9's role word / role id / focus");
        // the same plan again: its old values are gone, nothing is written
        const ApplyResult again = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p, ck);
        CHECK(!again.ok && !again.written && again.stage == "changed" && again.message.find("nothing written") != std::string::npos, "a stale plan: " + again.message);
        // a plan with nothing to change
        Plan empty;
        make_plan(live(w), Request(), empty, nullptr);
        const ApplyResult none = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), empty);
        CHECK(none.ok && !none.written && none.message == "nothing to change", "nothing to change");
    });

    run_case("channel E: a moved vector - the job re-chases every pointer (records and slots re-allocated after Apply was pressed)", [] {
        SheetWorld w;
        Request r;
        r.line_height = 60;
        r.approach = 1;
        r.slots = {{9, 22, 1}};
        Plan p;
        const RoleAllowList al = allow_of(w);
        CHECK(al.ok && al.has(role_word(9, 22, 1)), "the list has 9 / 22 / 1: " + al.why);
        CHECK(make_plan(live(w), r, p, &al).empty(), "plan made on the old layout");
        // Sheet::operator= re-allocated both vectors (as the game's UpdateSheet may): copy, re-point, poison the old memory
        const uint64_t new_recs = 0x1C0000000ULL, new_slots = 0x1C0100000ULL;
        w.mem.map(new_recs, 5 * kRecStride);
        w.mem.map(new_slots, 5 * 0x1000);
        std::vector<uint8_t> recs = w.bytes(SheetWorld::kRecs, 5 * kRecStride), slots = w.bytes(SheetWorld::kSlots0, 5 * 0x1000);
        w.mem.write(new_recs, recs.data(), recs.size());
        w.mem.write(new_slots, slots.data(), slots.size());
        for (int i = 0; i < 5; ++i) {
            w.mem.wr(new_recs + kRecStride * uint64_t(i) + kRecSlotBegin, new_slots + 0x1000 * uint64_t(i));
            w.mem.wr(new_recs + kRecStride * uint64_t(i) + kRecSlotEnd, new_slots + 0x1000 * uint64_t(i) + 11 * kSlotStride);
        }
        w.mem.wr(new_recs + kRecName, new_recs + 0x10);
        w.mem.wr(SheetWorld::kSheet + kSheetRecBegin, new_recs);
        w.mem.wr(SheetWorld::kSheet + kSheetRecEnd, new_recs + 5 * kRecStride);
        std::vector<uint8_t> poison(5 * 0x1000, 0xCC);
        w.mem.write(SheetWorld::kSlots0, poison.data(), poison.size());
        w.mem.write(SheetWorld::kRecs, poison.data(), 5 * kRecStride);
        const ApplyResult res = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p, checks(&al));
        CHECK(res.ok && res.written, "written at the new place: " + res.message);
        int32_t depth = 0, word = 0;
        w.mem.rd(new_recs + kRecLineHeight, depth);
        w.mem.rd(new_slots + 9 * kSlotStride + kSlotRole, word);
        CHECK(depth == 60 && word == role_word(9, 22, 1), "the new vectors hold the values");
        std::vector<uint8_t> old = w.bytes(SheetWorld::kSlots0, 5 * 0x1000);
        CHECK(std::all_of(old.begin(), old.end(), [](uint8_t b) { return b == 0xCC; }), "the freed slots were not written");
        old = w.bytes(SheetWorld::kRecs, 5 * kRecStride);
        CHECK(std::all_of(old.begin(), old.end(), [](uint8_t b) { return b == 0xCC; }), "the freed records were not written");
    });

    run_case("channel E: read-back mismatch -> every field and the dirty flag put back, channel E off for the session", [] {
        SheetWorld w;
        w.mem.wr(SheetWorld::kSheet + kSheetDirty, uint8_t(0));
        FlakyMemory fm(w.mem);
        fm.drop = w.slot(9) + kSlotFocus;  // this write silently does not land
        Request r;
        r.build_up = 1;
        r.approach = 2;
        r.line_height = 80;
        r.slots = {{9, -1, 6}};
        Plan p;
        const RoleAllowList al = allow_of(w);
        CHECK(make_plan(live(w), r, p, &al).empty(), "plan");
        const ApplyResult res = apply_plan(fm, SheetWorld::kSvc, w.anchors(), p, checks(&al));
        CHECK(!res.ok && res.undone && res.session_off && !res.restore_failed && res.stage == "readback", "undone: " + res.message);
        CHECK(res.message.find("focus of slot 9 reads 5, not 6") != std::string::npos, "the mismatch is named: " + res.message);
        CHECK(res.log_line.find("READ-BACK MISMATCH") != std::string::npos && res.log_line.find("undone from the snapshot") != std::string::npos, res.log_line);
        CHECK(w.i32(SheetWorld::kRecs + kRecBuildUp) == 0 && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && w.i32(w.slot(9) + kSlotRole) == 38213 &&
                  w.i32(w.slot(9) + kSlotRoleId) == 21 && w.i32(w.slot(9) + kSlotFocus) == 5 && w.i32(SheetWorld::kRecs + kRecApproach) == 0,
              "every field holds its value from before");
        for (uint64_t c : {uint64_t(0xF80), uint64_t(0x1170)})
            CHECK(w.i32(w.copy_rec(c) + kRecApproach) == 0 && w.i32(w.copy_rec(c) + kRecLineHeight) == 25 && w.i32(w.copy_rec(c) + kRecBuildUp) == 0 &&
                      w.i32(w.copy_slot(c, 9) + kSlotRole) == 38213,
                  "the working copies untouched");
        CHECK(w.u8(SheetWorld::kSheet + kSheetDirty) == 0, "the dirty flag is back to 0");
        // a write the memory layer refuses: the same full undo
        SheetWorld w2;
        FlakyMemory fm2(w2.mem);
        fm2.drop = SheetWorld::kRecs + kRecLineHeight;
        fm2.refuse = true;
        const ApplyResult res2 = apply_plan(fm2, SheetWorld::kSvc, w2.anchors(), p, checks(&al));
        CHECK(!res2.ok && res2.undone && w2.i32(SheetWorld::kRecs + kRecBuildUp) == 0 && w2.i32(w2.slot(9) + kSlotRole) == 38213, "refused write: undone");
    });

    run_case("channel E: the service - registry Find on the game thread, the scan fallback, kill switches in the job, session off, late jobs", [] {
        const fs::path out = test_tmp_dir("channel_e_service");
        std::error_code ec;
        fs::remove_all(out, ec);
        SheetWorld w;
        ServiceRig rig(w, out);
        rig.find_returns = SheetWorld::kSvc + 8;  // the registry hands out the second base
        CHECK(rig.svc->anchors_status().empty(), "anchors ok");
        LiveTactic lt;
        std::string why;
        CHECK(!rig.svc->read(rig.ctx(), lt, why) && why.find("locating") != std::string::npos, "first read: locating (" + why + ")");
        CHECK(rig.jobs.q.size() == 1 && rig.find_calls == 0, "the Find waits for the game thread");
        rig.jobs.pump();
        CHECK(rig.find_calls == 1 && rig.svc->located() == SheetWorld::kSvc && rig.svc->located_load_gen() == 3, "located by the registry Find");
        CHECK(rig.svc->locate_status().find("registry Find") != std::string::npos, rig.svc->locate_status());
        CHECK(rig.svc->read(rig.ctx(), lt, why) && lt.line_height == 25 && lt.g5.empty(), "read: " + why);
        // the bridge's club must be the game's
        Context other = rig.ctx();
        other.user = 96;
        CHECK(!rig.svc->read(other, lt, why) && why.find("not the bridge's 96") != std::string::npos, "another club: " + why);
        Context nomgr = rig.ctx();
        nomgr.managers = 0;
        CHECK(!rig.svc->read(nomgr, lt, why) && why.find("manager table") != std::string::npos, "no manager table: " + why);
        // a write: no opt-in -> the job refuses; then on
        Request r;
        r.line_height = 70;
        r.approach = 2;
        Plan p;
        rig.svc->read(rig.ctx(), lt, why);
        CHECK(make_plan(lt, r, p, nullptr).empty(), "plan");
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued: " + why);
        CHECK(rig.svc->busy(), "busy while queued");
        std::string why2;
        CHECK(!rig.svc->submit(rig.ctx(), p, why2) && why2.find("still queued") != std::string::npos, "one job at a time");
        rig.jobs.pump();
        ApplyResult res;
        CHECK(rig.svc->poll(res) && !res.ok && res.stage == "G1" && res.message.find("call_team_sheet_on.txt") != std::string::npos, "no opt-in: " + res.message);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && !rig.svc->busy(), "nothing written");
        rig.touch(opt_in_file());
        rig.touch(kill_switch_file());
        rig.svc->submit(rig.ctx(), p, why);
        rig.jobs.pump();
        CHECK(rig.svc->poll(res) && !res.ok && res.message.find("call_team_sheet_off.txt") != std::string::npos, "kill switch: " + res.message);
        rig.remove(kill_switch_file());
        rig.touch(master_switch_file());
        rig.svc->submit(rig.ctx(), p, why);
        rig.jobs.pump();
        CHECK(rig.svc->poll(res) && !res.ok && res.message.find("tactics_off.txt") != std::string::npos, "master switch: " + res.message);
        rig.remove(master_switch_file());
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 25, "still nothing written");
        // a job the game thread runs late
        rig.svc->submit(rig.ctx(), p, why);
        rig.clock += 2.5e6;
        rig.jobs.pump();
        CHECK(rig.svc->poll(res) && !res.ok && res.stage == "late" && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25, "late: " + res.message);
        // on time
        rig.svc->submit(rig.ctx(), p, why);
        rig.jobs.pump();
        CHECK(rig.svc->poll(res) && res.ok && res.written && w.i32(SheetWorld::kRecs + kRecLineHeight) == 70, "written: " + res.message);
        CHECK(rig.logged("channel E: team 95 record 0 approach 0->2 line height 25->70 (read back ok) [record now LH 70 BU 0 DA 2] [job"), "logged with the job time");
        CHECK(w.i32(w.copy_rec(0xF80) + kRecLineHeight) == 25 && w.i32(w.copy_rec(0x1170) + kRecLineHeight) == 25, "the service's job writes no Edit Tactic copy");
        // another career load: located again (the cached service belongs to load 3)
        CHECK(!rig.svc->read(rig.ctx(4), lt, why) && why.find("locating") != std::string::npos, "load 4: locating again");
        CHECK(!rig.svc->submit(rig.ctx(4), p, why) && why.find("not located") != std::string::npos, "no write before it is located: " + why);
        rig.jobs.pump();
        // Find calls: the first locate, the written job's re-check (M1), this locate
        CHECK(rig.svc->read(rig.ctx(4), lt, why) && rig.find_calls == 3, "located for load 4: " + why);
        // the Find returns something else: the scan finds the live service among the vtable-pair hits (the stale copy is refused)
        rig.find_returns = SheetWorld::kNodeB;
        rig.svc->relocate();
        CHECK(!rig.svc->read(rig.ctx(4), lt, why), "relocating");
        rig.jobs.pump();
        // the scan's pick is used only after it passes its header checks again 5 s later
        CHECK(rig.svc->located() == 0 && rig.svc->locate_status().find("again") != std::string::npos, "picked, not yet located: " + rig.svc->locate_status());
        CHECK(!rig.svc->read(rig.ctx(4), lt, why) && why.find("again") != std::string::npos, "before 5 s: " + why);
        rig.clock += 5e6;
        CHECK(rig.svc->read(rig.ctx(4), lt, why), "5 s later: " + why);
        CHECK(rig.svc->located() == SheetWorld::kSvc && rig.svc->scans() == 1 && rig.svc->locate_status().find("vtable-pair scan") != std::string::npos,
              "the scan located it: " + rig.svc->locate_status());
        CHECK(rig.logged("registry Find not used"), "the Find's refusal is logged");
        // without the opt-in (allow_find false) the Find is never called: read-only scan
        const int finds = rig.find_calls;
        rig.svc->relocate();
        CHECK(!rig.svc->read(rig.ctx(4, false), lt, why) && rig.find_calls == finds && rig.svc->scans() == 2, "scan only: " + why);
        rig.clock += 5e6;
        CHECK(rig.svc->read(rig.ctx(4, false), lt, why), "scan only, 5 s later: " + why);
        // the service freed under Turbo: the next read finds it again
        w.mem.wr(SheetWorld::kSvc + kSvcLoaded, uint8_t(0));
        CHECK(!rig.svc->read(rig.ctx(4, false), lt, why) && rig.svc->located() == 0, "a failed header forgets the service: " + why);
        w.mem.wr(SheetWorld::kSvc + kSvcLoaded, uint8_t(1));
        CHECK(!rig.svc->read(rig.ctx(4, false), lt, why), "scanned again");
        rig.clock += 5e6;
        CHECK(rig.svc->read(rig.ctx(4, false), lt, why), "found again: " + why);
        // a read-back mismatch switches channel E off for the session
        FlakyMemory fm(w.mem);
        fm.drop = SheetWorld::kRecs + kRecLineHeight;
        ServiceRig rig2(w, out, &fm);
        rig2.find_returns = SheetWorld::kSvc;
        rig2.jobs.inline_run = true;
        CHECK(rig2.svc->read(rig2.ctx(), lt, why), "rig 2 read: " + why);
        Plan p2;
        Request r2;
        r2.line_height = 33;
        r2.approach = 1;
        CHECK(make_plan(lt, r2, p2, nullptr).empty(), "plan 2");
        CHECK(rig2.svc->submit(rig2.ctx(), p2, why), "queued");
        CHECK(rig2.svc->poll(res) && res.undone && !rig2.svc->session_off().empty(), "mismatch: " + res.message);
        CHECK(!rig2.svc->submit(rig2.ctx(), p2, why) && why.find("off for this session") != std::string::npos, "no further write: " + why);
        // no game-thread dispatcher: refused, nothing queued
        Host h;
        h.mem = &w.mem;
        h.anchors = w.anchors();
        CoreService none(h);
        CHECK(!none.submit(rig.ctx(), p, why) && why.find("dispatcher") != std::string::npos, "no dispatcher: " + why);
    });

    run_case("channel E: the page cache - whole pages read once, unreadable pages and long blocks read directly, a write drops the pages", [] {
        SimMemory m;
        m.map(0x20000000ULL, 0x2000);
        for (uint32_t i = 0; i < 0x800; ++i) m.wr(0x20000000ULL + 4 * uint64_t(i), i);
        struct Count : Memory {
            SimMemory& m;
            int reads = 0;
            explicit Count(SimMemory& mm) : m(mm) {}
            bool read(uint64_t a, void* o, size_t n) override {
                ++reads;
                return m.read(a, o, n);
            }
            bool write(uint64_t a, const void* i, size_t n) override { return m.write(a, i, n); }
        } c(m);
        PageCache pc(c);
        uint32_t v = 0;
        CHECK(pc.rd(0x20000010ULL, v) && v == 4 && pc.rd(0x20000FFCULL, v) && v == 0x3FF && c.reads == 1, "one page, one read");
        uint64_t q = 0;
        CHECK(pc.rd(0x20000FFCULL, q) && q == (uint64_t(0x400) << 32 | 0x3FF) && c.reads == 2, "a value across two pages");
        CHECK(!pc.rd(0x20004000ULL, v) && c.reads == 4, "an unmapped page: the page read and the direct read both fail");
        std::vector<uint8_t> big;
        CHECK(pc.read_block(0x20000000ULL, 0x2000, big) && c.reads == 4, "a block inside cached pages: no read");
        CHECK(pc.wr(0x20000010ULL, uint32_t(77)) && pc.rd(0x20000010ULL, v) && v == 77 && c.reads == 5, "a write drops the pages: read again");
        CHECK(pc.reads() == 5, fmt("its own count (%d)", pc.reads()));
    });

    run_case("channel E: lost jobs and failed locates - not waited for forever, not scanned every second", [] {
        const fs::path out = test_tmp_dir("channel_e_lost");
        std::error_code ec;
        fs::remove_all(out, ec);
        SheetWorld w;
        ServiceRig rig(w, out);
        rig.find_returns = SheetWorld::kSvc;
        rig.touch(opt_in_file());
        LiveTactic lt;
        std::string why;
        rig.svc->read(rig.ctx(), lt, why);
        rig.jobs.pump();
        CHECK(rig.svc->read(rig.ctx(), lt, why), "located: " + why);
        Request r;
        r.line_height = 44;
        r.approach = 1;
        Plan p;
        CHECK(make_plan(lt, r, p, nullptr).empty(), "plan");
        // job 1 is queued and never run (the dispatcher dropped it): after 10 s the service stops waiting
        CHECK(rig.svc->submit(rig.ctx(), p, why) && rig.svc->busy(), "job 1 queued");
        rig.clock += 5e6;
        CHECK(rig.svc->busy() && !rig.svc->submit(rig.ctx(), p, why), "5 s: still waited for");
        rig.clock += 6e6;
        CHECK(!rig.svc->busy(), "11 s: not busy any more");
        CHECK(rig.svc->submit(rig.ctx(), p, why) && rig.svc->busy() && rig.logged("did not come back"), "job 2 queued, the lost one logged: " + why);
        rig.jobs.pump();  // job 1 runs at last (given up: cancelled, nothing written), then job 2
        ApplyResult a, b;
        CHECK(rig.svc->poll(a) && a.stage == "cancelled" && rig.svc->poll(b) && b.ok && b.written && w.i32(SheetWorld::kRecs + kRecLineHeight) == 44,
              "job 1 cancelled, job 2 written: " + a.message + " / " + b.message);
        CHECK(!rig.svc->busy(), "idle");
        // a career load whose scan finds nothing: one scan, then none until a minute passed or Locate again
        SheetWorld none;
        none.mem.wr(SheetWorld::kSvc + kSvcLoaded, uint8_t(0));
        none.mem.wr(SheetWorld::kStale, uint64_t(0));
        ServiceRig r2(none, out);
        CHECK(!r2.svc->read(r2.ctx(5, false), lt, why) && r2.svc->scans() == 1, "scan 1: " + why);
        CHECK(!r2.svc->read(r2.ctx(5, false), lt, why) && r2.svc->scans() == 1 && why.find("none a live service") != std::string::npos,
              "the next read does not scan again: " + why);
        r2.clock += 30e6;
        r2.svc->read(r2.ctx(5, false), lt, why);
        CHECK(r2.svc->scans() == 1, "30 s later: still not");
        r2.clock += 31e6;
        r2.svc->read(r2.ctx(5, false), lt, why);
        CHECK(r2.svc->scans() == 2, "a minute later: once more");
        r2.svc->relocate();
        r2.svc->read(r2.ctx(5, false), lt, why);
        CHECK(r2.svc->scans() == 3, "Locate again: at once");
        r2.svc->read(r2.ctx(6, false), lt, why);
        CHECK(r2.svc->scans() == 4, "a new career load: at once");
        // a registry Find job the game thread never runs: after 90 s the locate starts again
        ServiceRig r3(w, out);
        r3.find_returns = SheetWorld::kSvc;
        r3.svc->read(r3.ctx(7), lt, why);
        CHECK(r3.jobs.q.size() == 1, "the Find is queued");
        r3.jobs.q.clear();  // dropped
        r3.svc->read(r3.ctx(7), lt, why);
        CHECK(r3.jobs.q.empty() && why.find("locating") != std::string::npos, "still locating: " + why);
        r3.clock += 91e6;
        r3.svc->read(r3.ctx(7), lt, why);
        CHECK(r3.jobs.q.size() == 1 && r3.logged("did not finish"), "queued again after 90 s");
        r3.jobs.pump();
        CHECK(r3.svc->read(r3.ctx(7), lt, why), "located: " + why);
    });

    run_case("channel E: the role allow-list - every other club's triples, yours left out, bounded, fails closed", [] {
        SheetWorld w;
        const RoleAllowList al = allow_of(w);
        CHECK(al.ok && al.teams == 1 && al.words.size() == 10 && al.summary() == "10 triples from 1 teams", "built: " + al.summary() + " " + al.why);
        CHECK(al.has(38214) && al.has(38211) && al.has(role_word(9, 22, 1)) && !al.has(38213) && !al.has(38212), "the other club's words only");
        CHECK(al.role_ids(9) == std::vector<int>({21, 22}) && al.focuses(9, 21) == std::vector<int>({3, 6}), "per group / role");
        const RoleAllowList both = build_role_allow_list(w.mem, SheetWorld::kSvc, w.anchors(), -1);
        CHECK(both.ok && both.teams == 2 && both.has(38213), "nobody left out: both clubs");
        SheetWorld loop;
        loop.mem.wr(SheetWorld::kNodeA + kNodeNext, SheetWorld::kNodeA);
        const RoleAllowList bad = allow_of(loop);
        CHECK(!bad.ok && bad.why.find("bad node chain") != std::string::npos, "a looping chain: not built: " + bad.why);
        SheetWorld off;
        off.mem.wr(SheetWorld::kSvc + kSvcLoaded, uint8_t(0));
        CHECK(!allow_of(off).ok, "a service that fails its header: not built");
        double t = 0.0;
        const RoleAllowList slow = build_role_allow_list(w.mem, SheetWorld::kSvc, w.anchors(), w.team, [&t]() { return t += 6e6; });
        CHECK(!slow.ok && slow.why.find("ran out of time") != std::string::npos, "the time budget: " + slow.why);
        // a club whose sheet does not look right is skipped, the rest is used
        SheetWorld odd;
        odd.mem.wr(SheetWorld::kSheetB + kSheetSlotCount, int32_t(10));
        const RoleAllowList partial = build_role_allow_list(odd.mem, SheetWorld::kSvc, odd.anchors(), -1);
        CHECK(partial.ok && partial.skipped == 1 && !partial.has(38214) && partial.has(38213), "the odd club skipped: " + partial.summary());
        // the plan check refuses a word outside the list inside the job too
        LiveTactic lt = live(w);
        Plan p;
        p.user = 95;
        p.writes = {{FieldKind::Role, 9, 38213, 38212}, {FieldKind::RoleId, 9, 21, 21}, {FieldKind::Focus, 9, 5, 4}};
        CHECK(check_plan(lt, p, checks(&al)).find("never seen") != std::string::npos, "38212 refused in the job");
        p.writes.pop_back();
        CHECK(check_plan(lt, p, checks(&al)).find("together") != std::string::npos, "the three slot fields together");
        p.writes = {{FieldKind::Role, 9, 38213, 38214}, {FieldKind::RoleId, 9, 21, 22}, {FieldKind::Focus, 9, 5, 6}};
        CHECK(check_plan(lt, p, checks(&al)).find("is not group 9 / role 22 / focus 6") != std::string::npos, "an inconsistent word");
        p.writes = {{FieldKind::LineHeight, 3, 25, 50}};
        CHECK(check_plan(lt, p, checks(&al)).find("not a slot field") != std::string::npos, "line height with a slot");
        // a forged plan: an unknown Build-Up Style, a working copy named by an Apply
        p.writes = {{FieldKind::BuildUp, -1, 0, 2}};
        CHECK(check_plan(lt, p, checks(&al)).find("no known label") != std::string::npos, "build-up 2 refused in the job too");
        p.writes = {{FieldKind::Approach, -1, 0, 5}};
        CHECK(check_plan(lt, p, checks(&al)).find("outside 0..3") != std::string::npos, "approach 5 refused in the job");
        p.writes = {{FieldKind::LineHeight, -1, 25, 50, 0xF80}};
        CHECK(check_plan(lt, p, checks(&al)).find("only an undo names a working copy") != std::string::npos, "a copy write in an Apply: refused");
        // the other clubs' values (evidence for the next decode, never a gate)
        CHECK(al.approach_count.count(0) == 1 && al.build_up_count.count(0) == 1 && al.values_summary().find("approach 0 x1 (line 0-0)") == 0,
              "values: " + al.values_summary());
    });

    run_case("channel E: the scan fails closed - a stale copy re-checked 5 s later, a timeout, the 16-hit cap; the cancel flag", [] {
        const fs::path out = test_tmp_dir("channel_e_scan");
        std::error_code ec;
        fs::remove_all(out, ec);
        LiveTactic lt;
        std::string why;
        {
            // the live service is gone; a freed copy with an intact header is the only pair: picked, then freed before the re-check
            SheetWorld w;
            std::vector<uint8_t> hdr = w.bytes(SheetWorld::kSvc, kSvcSize);
            w.mem.write(SheetWorld::kStale, hdr.data(), hdr.size());
            w.mem.wr(SheetWorld::kSvc, uint64_t(0));
            ServiceRig rig(w, out);
            CHECK(!rig.svc->read(rig.ctx(3, false), lt, why) && rig.svc->scans() == 1 && why.find("again") != std::string::npos, "picked: " + why);
            w.mem.wr(SheetWorld::kStale + kSvcLoaded, uint8_t(0xCC));
            rig.clock += 5e6;
            CHECK(!rig.svc->read(rig.ctx(3, false), lt, why) && rig.svc->located() == 0 && why.find("failed its header checks again") != std::string::npos,
                  "the re-check refuses it: " + why);
            CHECK(rig.logged("failed its header checks"), "logged");
        }
        {
            // a scan that runs out of time locates nothing
            SheetWorld w;
            double tick = 0.0;
            ServiceRig rig(w, out, nullptr, [&tick](Host& h) {
                h.clock_us = [&tick]() { return tick += 1e6; };
                h.scan_budget_s = 0.5;
            });
            CHECK(!rig.svc->read(rig.ctx(3, false), lt, why) && rig.svc->located() == 0 && why.find("ran out of time") != std::string::npos, "timeout: " + why);
        }
        {
            // 16 or more vtable-pair hits (one of them the live service): refused, not guessed
            SheetWorld w;
            for (int i = 0; i < 16; ++i) {
                const uint64_t a = 0x500000000ULL + 0x2000ULL * uint64_t(i);
                w.mem.map(a, 16);
                w.mem.wr(a, SheetWorld::kVt1);
                w.mem.wr(a + 8, SheetWorld::kVt2);
            }
            ServiceRig rig(w, out);
            CHECK(!rig.svc->read(rig.ctx(3, false), lt, why) && rig.svc->located() == 0 && why.find("the scan's cap") != std::string::npos, "the cap: " + why);
        }
        {
            // the cancel flag: set, nothing is written; a job the dispatcher reports as not queued is cancelled and writes nothing if it runs
            SheetWorld w;
            Request r;
            r.line_height = 70;
            r.approach = 2;
            Plan p;
            CHECK(make_plan(live(w), r, p, nullptr).empty(), "plan");
            std::atomic<bool> cancel{true};
            JobChecks ck;
            ck.cancel = &cancel;
            const ApplyResult res = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p, ck);
            CHECK(!res.ok && res.stage == "cancelled" && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25, "cancelled: " + res.message);
            ServiceRig rig(w, out);
            rig.find_returns = SheetWorld::kSvc;
            rig.touch(opt_in_file());
            rig.svc->read(rig.ctx(), lt, why);
            rig.jobs.pump();
            CHECK(rig.svc->read(rig.ctx(), lt, why), "located: " + why);
            rig.jobs.report_none = true;
            CHECK(!rig.svc->submit(rig.ctx(), p, why) && why.find("dispatcher") != std::string::npos && rig.jobs.q.size() == 1, "not queued, yet held: " + why);
            rig.jobs.pump();
            ApplyResult a;
            CHECK(rig.svc->poll(a) && a.stage == "cancelled" && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25, "it ran: cancelled, nothing written: " + a.message);
            // in the job: another club named by the game, the hub left, a reload: refused
            rig.jobs.report_none = false;
            Context c = rig.ctx();
            CHECK(rig.svc->submit(c, p, why), "queued");
            Context away = c;
            away.hub = false;
            rig.svc->note_context(away);  // the UI saw the hub left before the game thread ran the job
            rig.jobs.pump();
            CHECK(rig.svc->poll(a) && a.stage == "G2" && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25, "hub left: " + a.message);
            CHECK(rig.svc->submit(c, p, why), "queued again");
            Context reload = c;
            reload.load_gen = 4;
            rig.svc->note_context(reload);
            rig.jobs.pump();
            CHECK(rig.svc->poll(a) && a.stage == "G2" && a.message.find("reloaded") != std::string::npos, "reloaded: " + a.message);
            rig.svc->note_context(c);
            CHECK(rig.svc->submit(c, p, why), "queued once more");
            w.mem.wr(SheetWorld::kPsm + 0x10, int32_t(96));
            rig.jobs.pump();
            CHECK(rig.svc->poll(a) && a.stage == "user" && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25, "the game names another club: " + a.message);
            w.mem.wr(SheetWorld::kPsm + 0x10, int32_t(95));
            CHECK(rig.svc->submit(c, p, why), "and again");
            rig.find_returns = SheetWorld::kNodeB;
            rig.jobs.pump();
            CHECK(rig.svc->poll(a) && a.stage == "locate" && a.message.find("registry Find") != std::string::npos, "the Find hands out another object: " + a.message);
            Plan other = p;
            other.load_gen = 2;
            CHECK(!rig.svc->submit(c, other, why) && why.find("another") == std::string::npos && why.find("career load 2") != std::string::npos,
                  "a plan of another career load: " + why);
        }
    });

    run_case("channel E: the cm_mentalities row's checks - the row must be the live tactic; its fields; its undo obeys the database switches and the row", [] {
        SheetWorld w;
        const LiveTactic lt = live(w);
        Request r;
        r.build_up = 1;
        r.line_height = 75;
        r.approach = 2;
        Plan p;
        CHECK(make_plan(lt, r, p, nullptr).empty() && p.writes.size() == 3, "plan");
        std::map<std::string, int64_t> row;
        for (int s = 0; s < 11; ++s) {
            row["playerid" + std::to_string(s)] = lt.slots[size_t(s)].pid;
            row["position" + std::to_string(s)] = lt.slots[size_t(s)].position;
        }
        row["defensivedepth"] = 25;
        row["buildupplay"] = 0;
        row["teamid"] = 95;
        row["activetactic"] = 1;
        row["sourceformationid"] = 36;
        auto get = [&row](const std::string& n, int64_t& v) {
            auto it = row.find(n);
            if (it == row.end()) return false;
            v = it->second;
            return true;
        };
        CHECK(mirror_row_check(get, lt, p.writes).empty(), "the live tactic's row: " + mirror_row_check(get, lt, p.writes));
        row["playerid3"] = 1;
        CHECK(mirror_row_check(get, lt, p.writes).find("playerid3 is 1") != std::string::npos, "another player in slot 3: skipped");
        row["playerid3"] = lt.slots[3].pid;
        row["defensivedepth"] = 55;
        CHECK(mirror_row_check(get, lt, p.writes).find("not the live tactic") != std::string::npos, "another depth before the write: skipped");
        row["defensivedepth"] = 25;
        row.erase("position0");
        CHECK(mirror_row_check(get, lt, p.writes).find("no playerid0 / position0") != std::string::npos, "no slot fields: skipped");
        CHECK(mirror_field_name({FieldKind::Role, 9, 0, 0}) == "pos9role" && mirror_field_name({FieldKind::Focus, 9, 0, 0}).empty(), "field names");
        row["position0"] = lt.slots[0].position;
        // the Apply's row fields: the mirrored fields that change (the approach has no column; line height and build-up do)
        const std::vector<DbRowField> af = apply_row_fields(p, get);
        CHECK(af.size() == 2 && af[0].name == "defensivedepth" && af[0].old_value == 25 && af[0].new_value == 75 && af[1].name == "buildupplay" &&
                  af[1].old_value == 0 && af[1].new_value == 1,
              fmt("defensivedepth 25 -> 75, buildupplay 0 -> 1 (%zu)", af.size()));
        row["buildupplay"] = 1;
        CHECK(apply_row_fields(p, get).size() == 1, "a field that already holds the value is left out");
        row["buildupplay"] = 0;
        int64_t lo = 0, hi = 0;
        CHECK(db_row_field_range("defensivedepth", lo, hi) && lo == 1 && hi == 100 && db_row_field_range("buildupplay", lo, hi) && hi == 3 &&
                  db_row_field_range("pos10role", lo, hi) && hi == 60000 && !db_row_field_range("pos11role", lo, hi) && !db_row_field_range("teamid", lo, hi) &&
                  !db_row_field_range("posxrole", lo, hi),
              "the ranges Turbo writes in");
        // the undo's row: the database switches and the row's identity
        UndoRecord u = UndoRecord::from_plan(p);
        u.mirror_row_key = 2;
        row["artificialkey"] = 2;
        Switches sw;
        CHECK(undo_row_check(sw, get, u).empty(), "the tactic's row: put back");
        sw.off = true;
        sw.master = true;
        CHECK(undo_row_check(sw, get, u).empty(), "the channel's kill switches do not stop an undo (it puts the game's values back)");
        sw.off = sw.master = false;
        sw.db_row_off = true;
        CHECK(undo_row_check(sw, get, u).find("channel_e_db_off.txt") != std::string::npos, "the row's own switch");
        sw.db_row_off = false;
        sw.db_row_off_legacy = true;
        CHECK(undo_row_check(sw, get, u).find("channel_e_db_mirror_off.txt") != std::string::npos, "2.3's name of it");
        sw.db_row_off_legacy = false;
        sw.db_off = true;
        CHECK(undo_row_check(sw, get, u).find("tactics_db_off.txt") != std::string::npos, "the database rows channel");
        CHECK(sw.blocking().empty() && !sw.db_row_block().empty(), "a database switch does not stop the live write");
        sw.db_off = false;
        row["artificialkey"] = 3;
        CHECK(undo_row_check(sw, get, u).find("is not club 95's active tactic") != std::string::npos, "another artificialkey: refused");
        row["artificialkey"] = 2;
        row["teamid"] = 7;
        CHECK(undo_row_check(sw, get, u).find("is not club 95's active tactic") != std::string::npos, "another club's row: refused");
        row["teamid"] = 95;
        // the undo's fields: those whose live field the undo puts back and that still hold Turbo's value
        u.mirror = {{"defensivedepth", 25, 75}, {"buildupplay", 0, 1}};
        u.mirror_state = "written";
        row["defensivedepth"] = 75;
        row["buildupplay"] = 1;
        Plan up;
        up.undo = true;
        up.writes = {{FieldKind::Approach, -1, 2, 0}, {FieldKind::LineHeight, -1, 75, 25}, {FieldKind::BuildUp, -1, 1, 0}};
        std::vector<std::string> left;
        std::vector<DbRowField> uf = undo_row_fields(u, up, get, left);
        CHECK(uf.size() == 2 && uf[0].old_value == 75 && uf[0].new_value == 25 && uf[1].new_value == 0 && left.empty(), "both back");
        row["buildupplay"] = 3;  // the game's save wrote another value since
        up.writes.pop_back();    // and the live build-up is not put back
        uf = undo_row_fields(u, up, get, left);
        CHECK(uf.size() == 1 && uf[0].name == "defensivedepth" && left.size() == 1 && left[0].find("buildupplay") == 0, "only the line height: " + left[0]);
        // add_job_rows: the first value before Turbo's write stays; another row starts over; an undo marks it restored
        UndoRecord v;
        DbRowWrite rw;
        rw.table.name = "cm_mentalities";
        rw.key = 2;
        rw.fields = {{"defensivedepth", 25, 75}};
        add_job_rows(v, {rw}, false);
        rw.fields = {{"defensivedepth", 75, 60}, {"buildupplay", 0, 1}};
        add_job_rows(v, {rw}, false);
        CHECK(v.mirror_state == "written" && v.mirror_row_key == 2 && v.mirror.size() == 2 && v.mirror[0].old_value == 25 && v.mirror[0].new_value == 60,
              "merged: defensivedepth 25 -> 60");
        rw.fields = {{"defensivedepth", 50, 70}};  // the game's save put 50 there since
        add_job_rows(v, {rw}, false);
        CHECK(v.mirror[0].old_value == 50 && v.mirror[0].new_value == 70, "a value the game wrote in between is what was replaced");
        rw.key = 9;
        add_job_rows(v, {rw}, false);
        CHECK(v.mirror_row_key == 9 && v.mirror.size() == 1, "another row starts over");
        add_job_rows(v, {rw}, true);
        CHECK(v.mirror_state == "restored", "an undo's rows: restored");
    });

    run_case("channel E 2.4: one job - the live record and its cm_mentalities row written, read back, rolled back and put back together", [] {
        SheetWorld w;
        DbWorld db(w.mem);
        const uint64_t rec = db.row(0, w, 0, 25, 0);
        db.row(1, w, 1, 50, 1);  // another row of the club (not the active tactic): never touched
        db.mem_set(1, "teamid", 95);
        db.mem_set(1, "activetactic", 0);
        const std::vector<uint8_t> other_before = w.bytes(DbWorld::kRecs + DbWorld::kRecSize, DbWorld::kRecSize);
        Request r;
        r.approach = 2;
        r.line_height = 70;
        r.build_up = 1;
        Plan p;
        CHECK(make_plan(live(w), r, p, nullptr).empty(), "plan");
        p.load_gen = 3;
        p.rows.push_back(db.target(rec, p, live(w)));
        CHECK(p.rows[0].fields.size() == 2 && p.rows[0].checks.size() >= 26, fmt("the row: 2 fields, %zu checks", p.rows[0].checks.size()));
        const ApplyResult res = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p);
        CHECK(res.ok && res.written, "written: " + res.message);
        CHECK(res.log_line == "channel E: team 95 record 0 approach 0->2 line height 25->70 build-up 0->1; cm_mentalities artificialkey 0 defensivedepth 25->70 "
                             "buildupplay 0->1 (read back ok) [record now LH 70 BU 1 DA 2]",
              "log: " + res.log_line);
        CHECK(db.get(rec, "defensivedepth") == 70 && db.get(rec, "buildupplay") == 1 && db.get(rec, "teamid") == 95 && db.get(rec, "pos9role") == 38213,
              "the row holds the new values, the rest as it was");
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 70 && w.i32(SheetWorld::kRecs + kRecApproach) == 2, "the live record too");
        CHECK(w.bytes(DbWorld::kRecs + DbWorld::kRecSize, DbWorld::kRecSize) == other_before, "the other row untouched");
        CHECK(res.rows.size() == 1 && res.rows[0].fields.size() == 2 && res.rows[0].fields[0].old_value == 25 && res.rows[0].fields[0].new_value == 70,
              "the result names the row's snapshot");
        // the undo: one job again, live record and row back
        WriteHistory hist;
        hist.note(p, res.writes, res.rows);
        UndoRecord u = UndoRecord::from_plan(p);
        u.load_gen = 3;
        u.state = "written";
        add_job_rows(u, res.rows, false);
        CHECK(u.mirror_state == "written" && u.mirror_table == "cm_mentalities" && u.mirror_row_key == 0 && u.mirror.size() == 2, "the undo record names the row");
        UndoRecord back;
        std::string err;
        CHECK(UndoRecord::parse(u.to_json(), back, &err) && back.mirror.size() == 2 && back.mirror[0].old_value == 25 && back.mirror_row_key == 0,
              "the row survives the JSON (db_mirror): " + err);
        Plan up;
        std::vector<std::string> skipped;
        CHECK(make_undo_plan(live(w), u, "", 3, up, skipped).empty() && up.writes.size() == 3, "undo plan");
        auto get = [&](const std::string& n, int64_t& v) { return db.get_ok(rec, n, v); };
        CHECK(undo_row_check(Switches(), get, u).empty(), "the row is the tactic's");
        std::vector<std::string> left;
        DbRowWrite ur = db.bare(rec);
        ur.fields = undo_row_fields(u, up, get, left);
        up.rows.push_back(ur);
        // an undo row value that is not the row's original: refused in the job
        Plan forged = up;
        forged.rows[0].fields[0].new_value = 40;
        JobChecks hck;
        hck.history = &hist;
        const ApplyResult fr = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), forged, hck);
        CHECK(!fr.ok && fr.stage == "G6" && fr.message.find("the row held 25 before Turbo's first write") != std::string::npos && db.get(rec, "defensivedepth") == 70,
              "forged row undo: " + fr.message);
        const ApplyResult ures = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), up, hck);
        CHECK(ures.ok && ures.undo && db.get(rec, "defensivedepth") == 25 && db.get(rec, "buildupplay") == 0 && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25 &&
                  w.i32(SheetWorld::kRecs + kRecApproach) == 0,
              "undone: live record and row back: " + ures.message);
        CHECK(ures.log_line.find("channel E: undo team 95 record 0 approach 2->0 line height 70->25 build-up 1->0; cm_mentalities artificialkey 0 defensivedepth "
                                 "70->25 buildupplay 1->0 (read back ok)") == 0,
              "log: " + ures.log_line);
    });

    run_case("channel E 2.4: one job - the row re-checked in the job (identity, owner rule, table, ranges, mirror), a row write that does not land rolls back all", [] {
        Request r;
        r.approach = 2;
        r.line_height = 70;
        r.build_up = 1;
        struct Case {
            const char* name;
            std::function<void(SheetWorld&, DbWorld&, uint64_t, Plan&)> mutate;
            const char* stage;
            const char* expect;
        };
        const std::vector<Case> cases = {
            {"another club in the row", [](SheetWorld&, DbWorld& d, uint64_t, Plan&) { d.mem_set(0, "teamid", 96); }, "db row", "teamid is 96, not 95"},
            {"no longer the active tactic", [](SheetWorld&, DbWorld& d, uint64_t, Plan&) { d.mem_set(0, "activetactic", 0); }, "db row", "activetactic is 0"},
            {"another player in slot 4", [](SheetWorld&, DbWorld& d, uint64_t, Plan&) { d.mem_set(0, "playerid4", 1); }, "db row", "playerid4 is 1"},
            {"the depth written by someone else", [](SheetWorld&, DbWorld& d, uint64_t, Plan&) { d.mem_set(0, "defensivedepth", 33); }, "db row",
             "defensivedepth is 33, not 25"},
            {"the table moved (another save loaded)", [](SheetWorld& w, DbWorld&, uint64_t, Plan&) { w.mem.wr(DbWorld::kHdr + 0x30, uint64_t(0x5B000000ULL)); }, "db row",
             "the database changed"},
            {"the row deleted", [](SheetWorld& w, DbWorld&, uint64_t rec, Plan&) { w.mem.wr(rec + DbWorld::kRecSize - 1, uint8_t(0x80)); }, "db row", "deleted"},
            {"a row value that is not the live record's", [](SheetWorld&, DbWorld&, uint64_t, Plan& p) { p.rows[0].fields[0].new_value = 71; }, "G6",
             "is not what the live record gets"},
            {"a depth outside 1..100", [](SheetWorld&, DbWorld&, uint64_t, Plan& p) { p.rows[0].fields[0].new_value = 0; }, "G6", "outside 1..100"},
            {"a field Turbo never writes", [](SheetWorld&, DbWorld&, uint64_t, Plan& p) { p.rows[0].fields[0].name = "teamid"; }, "G6", "not a field Turbo writes"},
        };
        for (const Case& c : cases) {
            SheetWorld w;
            DbWorld db(w.mem);
            const uint64_t rec = db.row(0, w, 0, 25, 0);
            Plan p;
            make_plan(live(w), r, p, nullptr);
            p.rows.push_back(db.target(rec, p, live(w)));
            c.mutate(w, db, rec, p);
            const std::vector<uint8_t> recs_before = w.bytes(SheetWorld::kRecs, kRecStride);
            const ApplyResult res = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p);
            CHECK(!res.ok && !res.written && res.stage == c.stage && res.message.find(c.expect) != std::string::npos && res.message.find("nothing written") != std::string::npos &&
                      w.bytes(SheetWorld::kRecs, kRecStride) == recs_before,
                  std::string(c.name) + ": " + res.stage + " " + res.message);
        }
        // the row's write does not land: the live record, the dirty flag and the row are put back, channel E off for the session
        SheetWorld w;
        w.mem.wr(SheetWorld::kSheet + kSheetDirty, uint8_t(0));
        DbWorld db(w.mem);
        const uint64_t rec = db.row(0, w, 0, 25, 0);
        Plan p;
        make_plan(live(w), r, p, nullptr);
        p.rows.push_back(db.target(rec, p, live(w)));
        FlakyMemory fm(w.mem);
        fm.drop = rec + db.field("defensivedepth")->byte_off();
        const ApplyResult res = apply_plan(fm, SheetWorld::kSvc, w.anchors(), p);
        CHECK(!res.ok && res.undone && res.session_off && res.message.find("cm_mentalities artificialkey 0 defensivedepth reads 25, not 70") != std::string::npos,
              "mismatch named: " + res.message);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && w.i32(SheetWorld::kRecs + kRecApproach) == 0 && w.i32(SheetWorld::kRecs + kRecBuildUp) == 0 &&
                  w.u8(SheetWorld::kSheet + kSheetDirty) == 0 && db.get(rec, "defensivedepth") == 25 && db.get(rec, "buildupplay") == 0,
              "the live record, the dirty flag and the row are back");
        CHECK(res.log_line.find("READ-BACK MISMATCH") != std::string::npos && res.log_line.find("undone from the snapshot") != std::string::npos, res.log_line);
    });

    run_case("channel E 2.4: the service strips the row while channel_e_db_off.txt / tactics_db_off.txt exist (the live record is still written)", [] {
        const fs::path out = test_tmp_dir("channel_e_dbrow");
        std::error_code ec;
        fs::remove_all(out, ec);
        SheetWorld w;
        DbWorld db(w.mem);
        const uint64_t rec = db.row(0, w, 0, 25, 0);
        ServiceRig rig(w, out);
        rig.find_returns = SheetWorld::kSvc;
        rig.touch(opt_in_file());
        rig.jobs.inline_run = true;
        LiveTactic lt;
        std::string why;
        CHECK(rig.svc->read(rig.ctx(), lt, why), "read: " + why);
        for (const char* k : {db_row_off_file(), db_row_off_legacy_file(), db_off_file()}) {
            Request r;
            r.approach = lt.approach == 2 ? 1 : 2;
            r.line_height = r.approach == 2 ? 70 : 50;
            Plan p;
            CHECK(make_plan(lt, r, p, nullptr).empty(), "plan");
            p.load_gen = 3;
            p.rows.push_back(db.target(rec, p, lt));
            rig.touch(k);
            ApplyResult res;
            CHECK(rig.svc->submit(rig.ctx(), p, why) && rig.svc->poll(res) && res.ok && res.written && res.rows.empty(), std::string(k) + ": " + res.message);
            CHECK(res.log_line.find(std::string("[cm_mentalities not written: kill switch turbo_output\\") + k + " is present]") != std::string::npos,
                  "said so: " + res.log_line);
            CHECK(db.get(rec, "defensivedepth") == 25 && w.i32(SheetWorld::kRecs + kRecLineHeight) == r.line_height, "the row untouched, the live record written");
            rig.remove(k);
            rig.svc->read(rig.ctx(), lt, why);
        }
        // no switch: the service's job writes the row with the live record
        Request r;
        r.approach = 0;
        r.line_height = 25;
        Plan p;
        CHECK(make_plan(lt, r, p, nullptr).empty(), "plan");
        p.load_gen = 3;
        db.mem_set(0, "defensivedepth", lt.line_height);
        p.rows.push_back(db.target(rec, p, lt));
        ApplyResult res;
        CHECK(rig.svc->submit(rig.ctx(), p, why) && rig.svc->poll(res) && res.ok && res.rows.size() == 1 && db.get(rec, "defensivedepth") == 25 &&
                  rig.logged("; cm_mentalities artificialkey 0 defensivedepth " + std::to_string(lt.line_height) + "->25"),
              "the row written in the service's job: " + res.log_line);
    });

    run_case("channel E 2.4 review: switches re-read in the job; a record write that fails with a row in the plan; putting back fails with a row", [] {
        Request r;
        r.approach = 2;
        r.line_height = 70;
        r.build_up = 1;
        {
            // m1: a kill switch dropped (or the opt-in removed, or the row's switch dropped) after the click: seen in the job itself
            const fs::path out = test_tmp_dir("channel_e_jobswitch");
            std::error_code ec;
            fs::remove_all(out, ec);
            SheetWorld w;
            DbWorld db(w.mem);
            const uint64_t rec = db.row(0, w, 0, 25, 0);
            ServiceRig rig(w, out);
            rig.find_returns = SheetWorld::kSvc;
            rig.touch(opt_in_file());
            LiveTactic lt;
            std::string why;
            rig.svc->read(rig.ctx(), lt, why);
            rig.jobs.pump();
            CHECK(rig.svc->read(rig.ctx(), lt, why), "read: " + why);
            auto plan_now = [&]() {
                Plan p;
                make_plan(lt, r, p, nullptr);
                p.load_gen = 3;
                p.rows.push_back(db.target(rec, p, lt));
                return p;
            };
            ApplyResult res;
            for (const char* k : {kill_switch_file(), master_switch_file()}) {
                CHECK(rig.svc->submit(rig.ctx(), plan_now(), why), "queued: " + why);
                rig.touch(k);
                rig.jobs.pump();
                CHECK(rig.svc->poll(res) && !res.ok && res.stage == "G1" && res.message.find(std::string(k) + " (seen in the job)") != std::string::npos &&
                          w.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && db.get(rec, "defensivedepth") == 25,
                      std::string(k) + " after the click: " + res.message);
                rig.remove(k);
            }
            CHECK(rig.svc->submit(rig.ctx(), plan_now(), why), "queued");
            rig.remove(opt_in_file());
            rig.jobs.pump();
            CHECK(rig.svc->poll(res) && !res.ok && res.stage == "G1" && res.message.find("no call_team_sheet_on.txt (seen in the job)") != std::string::npos,
                  "the opt-in removed after the click: " + res.message);
            rig.touch(opt_in_file());
            CHECK(rig.svc->submit(rig.ctx(), plan_now(), why), "queued");
            rig.touch(db_row_off_file());
            rig.jobs.pump();
            CHECK(rig.svc->poll(res) && res.ok && res.written && res.rows.empty() && db.get(rec, "defensivedepth") == 25 &&
                      w.i32(SheetWorld::kRecs + kRecLineHeight) == 70 &&
                      res.log_line.find("[cm_mentalities not written: kill switch turbo_output\\channel_e_db_off.txt is present]") != std::string::npos,
                  "channel_e_db_off.txt after the click: the row left out in the job: " + res.log_line);
            rig.remove(db_row_off_file());
        }
        {
            // M4.1: the record write is silently dropped with a row in the plan: the row WAS written and is put back with everything else
            SheetWorld w;
            DbWorld db(w.mem);
            const uint64_t rec = db.row(0, w, 0, 25, 0);
            Plan p;
            CHECK(make_plan(live(w), r, p, nullptr).empty(), "plan");
            p.rows.push_back(db.target(rec, p, live(w)));
            FlakyMemory fm(w.mem);
            fm.drop = SheetWorld::kRecs + kRecLineHeight;
            const ApplyResult res = apply_plan(fm, SheetWorld::kSvc, w.anchors(), p);
            CHECK(!res.ok && res.undone && !res.restore_failed && res.message.find("Line Height reads 25, not 70") != std::string::npos, "mismatch: " + res.message);
            CHECK(db.get(rec, "defensivedepth") == 25 && db.get(rec, "buildupplay") == 0 && w.i32(SheetWorld::kRecs + kRecApproach) == 0 &&
                      w.i32(SheetWorld::kRecs + kRecBuildUp) == 0,
                  "the row and the record are back");
            CHECK(res.rows.size() == 1 && res.rows[0].fields.size() == 2 && res.log_line.find("; cm_mentalities artificialkey 0 defensivedepth 25->70") != std::string::npos,
                  "the row is named (it was written): " + res.log_line);
            // m9: the record write REFUSED by the memory layer: the row is never tried, and neither the result nor the log claims it
            SheetWorld w2;
            DbWorld db2(w2.mem);
            const uint64_t rec2 = db2.row(0, w2, 0, 25, 0);
            Plan p2;
            make_plan(live(w2), r, p2, nullptr);
            p2.rows.push_back(db2.target(rec2, p2, live(w2)));
            FlakyMemory fm2(w2.mem);
            fm2.drop = SheetWorld::kRecs + kRecLineHeight;
            fm2.refuse = true;
            const ApplyResult res2 = apply_plan(fm2, SheetWorld::kSvc, w2.anchors(), p2);
            CHECK(!res2.ok && res2.undone && res2.rows.empty() && res2.rows_note == "not written: the live record's write failed first" &&
                      res2.log_line.find("cm_mentalities artificialkey") == std::string::npos && db2.get(rec2, "defensivedepth") == 25,
                  "no row claimed: " + res2.log_line);
        }
        {
            // M4.2: putting back fails with a row: the approach and the row's depth stay at Turbo's values; the undo file names them; the undo
            // puts both back (the row although its live field, the line height, already holds the game's value)
            struct OnceMemory : Memory {
                SimMemory& m;
                bool on = true;
                uint64_t drop = 0;
                std::map<uint64_t, int> once;  // address -> writes seen (the first lands, the rest are dropped)
                explicit OnceMemory(SimMemory& mm) : m(mm) {}
                bool read(uint64_t a, void* o, size_t n) override { return m.read(a, o, n); }
                bool write(uint64_t a, const void* i, size_t n) override {
                    if (on && a == drop) return true;
                    auto it = once.find(a);
                    if (on && it != once.end() && it->second++ > 0) return true;
                    return m.write(a, i, n);
                }
            };
            SheetWorld w;
            DbWorld db(w.mem);
            const uint64_t rec = db.row(0, w, 0, 25, 0);
            Plan p;
            CHECK(make_plan(live(w), r, p, nullptr).empty(), "plan");
            p.load_gen = 3;
            p.rows.push_back(db.target(rec, p, live(w)));
            OnceMemory om(w.mem);
            om.drop = SheetWorld::kRecs + kRecLineHeight;
            om.once[SheetWorld::kRecs + kRecApproach] = 0;
            om.once[rec + db.field("defensivedepth")->byte_off()] = 0;
            const ApplyResult res = apply_plan(om, SheetWorld::kSvc, w.anchors(), p);
            om.on = false;
            CHECK(!res.ok && res.restore_failed && !res.undone && res.session_off, "putting back failed: " + res.message);
            CHECK(w.i32(SheetWorld::kRecs + kRecApproach) == 2 && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && w.i32(SheetWorld::kRecs + kRecBuildUp) == 0 &&
                      db.get(rec, "defensivedepth") == 70 && db.get(rec, "buildupplay") == 0,
                  "stuck at Turbo's: the approach and the row's depth");
            WriteHistory hist;
            hist.note(p, res.writes, res.rows);  // what the service records after such a job
            UndoRecord u = UndoRecord::from_plan(p);
            u.load_gen = 3;
            u.state = "failed";
            add_job_writes(u, res.writes);
            add_job_rows(u, res.rows, false);
            CHECK(u.mirror_state == "written" && u.mirror.size() == 2, "the undo file names the row's fields");
            Plan up;
            std::vector<std::string> skipped;
            CHECK(make_undo_plan(live(w), u, "", 3, up, skipped).empty() && up.writes.size() == 1 && up.writes[0].kind == FieldKind::Approach,
                  fmt("the undo plan: the approach only (%zu)", up.writes.size()));
            auto get = [&](const std::string& n, int64_t& v) { return db.get_ok(rec, n, v); };
            std::vector<std::string> left;
            const LiveTactic now = live(w);
            DbRowWrite ur = db.bare(rec);
            ur.fields = undo_row_fields(u, up, get, left, &now);
            CHECK(ur.fields.size() == 1 && ur.fields[0].name == "defensivedepth" && ur.fields[0].new_value == 25 && left.size() == 1,
                  "the row's depth goes back although the line height is not in the plan: " + (left.empty() ? std::string() : left[0]));
            std::vector<std::string> left0;
            CHECK(undo_row_fields(u, up, get, left0).empty(), "without the live read: the old rule (only fields this undo puts back)");
            up.rows.push_back(ur);
            JobChecks hck;
            hck.history = &hist;
            const ApplyResult ures = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), up, hck);
            CHECK(ures.ok && ures.undo && w.i32(SheetWorld::kRecs + kRecApproach) == 0 && db.get(rec, "defensivedepth") == 25,
                  "undone: the approach and the row: " + ures.log_line);
        }
    });

    run_case("channel E: the gates G1..G6 in order, with the first reason", [] {
        GateInput in;
        in.sw.opt_in = true;
        in.bridge = true;
        in.in_cm = true;
        in.match_phase = "hub";
        in.load_gen = 3;
        in.located_load_gen = 3;
        CHECK(evaluate(in).pass() && evaluate(in).first().empty() && evaluate(in).list.size() == 7, "everything passes");
        GateInput x = in;
        x.sw.opt_in = false;
        CHECK(evaluate(x).first().find("G1") == 0 && evaluate(x).first().find("call_team_sheet_on.txt") != std::string::npos, evaluate(x).first());
        x.undo = true;
        CHECK(evaluate(x).pass(), "an undo needs no opt-in");
        x = in;
        x.sw.off = true;
        CHECK(evaluate(x).first().find("call_team_sheet_off.txt") != std::string::npos, evaluate(x).first());
        x.sw.master = true;
        CHECK(evaluate(x).first().find("tactics_off.txt") != std::string::npos, "the master is named first");
        x.undo = true;
        CHECK(evaluate(x).pass(), "an undo puts the game's values back even with a kill switch");
        struct G2 {
            std::function<void(GateInput&)> f;
            const char* expect;
        };
        for (const G2& c : std::vector<G2>{
                 {[](GateInput& g) { g.bridge = false; }, "has not reported"},
                 {[](GateInput& g) { g.in_cm = false; }, "not in a career"},
                 {[](GateInput& g) { g.match_phase = ""; }, "does not report the match phase"},
                 {[](GateInput& g) { g.match_phase = "unknown"; }, "not known yet"},
                 {[](GateInput& g) { g.match_phase = "prematch"; }, "Lua: prematch"},
                 {[](GateInput& g) { g.match_phase = "match"; }, "Lua: match"},
                 {[](GateInput& g) { g.save_age = 5; }, "a save started 5 s ago"},
                 {[](GateInput& g) { g.load_gen = -1; g.located_load_gen = -1; }, "career loads"},
                 {[](GateInput& g) { g.located_load_gen = 2; }, "(re)loaded"},
             }) {
            GateInput g = in;
            c.f(g);
            const std::string f = evaluate(g).first();
            CHECK(f.find("G2 career hub") == 0 && f.find(c.expect) != std::string::npos, f);
        }
        x = in;
        x.save_age = kSaveQuietSeconds;
        CHECK(evaluate(x).pass(), "a save 30 s ago is over");
        // only "hub" passes (any other phase string is refused)
        x = in;
        x.match_phase = "left_prematch";
        CHECK(evaluate(x).first().find("G2 career hub") == 0, "only the hub: " + evaluate(x).first());
        x.undo = true;
        CHECK(evaluate(x).first().find("G2 career hub") == 0, "an undo too");
        // an undo is not stopped by the session switch (it puts Turbo's values back)
        x = in;
        x.session_off = "read-back mismatch";
        x.undo = true;
        CHECK(evaluate(x).pass(), "undo after a mismatch: allowed");
        x = in;
        x.anchors = "game build X";
        CHECK(evaluate(x).first() == "G3 build and anchors: game build X", evaluate(x).first());
        x = in;
        x.locate = "layout mismatch (y)";
        CHECK(evaluate(x).first() == "G4 locate: layout mismatch (y)", "G4");
        x = in;
        x.g5 = "vf 0xF8";
        CHECK(evaluate(x).first().find("G5") == 0, "G5");
        x = in;
        x.plan = "nothing changed";
        CHECK(evaluate(x).first() == "G6 values: nothing changed", "G6");
        x = in;
        x.session_off = "read-back mismatch";
        CHECK(evaluate(x).first().find("session") == 0, "session off");
        Switches s;
        CHECK(s.blocking().empty(), "no file: nothing blocks");
        s.off = true;
        CHECK(s.blocking() == "call_team_sheet_off.txt", "the channel switch");
        s.master = true;
        CHECK(s.blocking() == "tactics_off.txt", "the master first");
    });

    run_case("channel E: the undo record - JSON, undo plans that put back only Turbo's own values, merges, overwrite detection", [] {
        SheetWorld w;
        Request r;
        r.build_up = 1;
        r.line_height = 75;
        r.approach = 2;
        r.slots = {{9, -1, 6}};
        Plan p;
        const RoleAllowList al = allow_of(w);
        CHECK(make_plan(live(w), r, p, &al).empty() && p.writes.size() == 6, "plan");
        p.load_gen = 3;
        UndoRecord u = UndoRecord::from_plan(p);
        u.state = "written";
        u.when = "2026-10-12 01:00:00";
        u.load_gen = 3;
        u.dirty_before = 1;
        u.mirror = {{"defensivedepth", 55, 75}};
        u.mirror_state = "written";
        u.mirror_row_key = 2;
        UndoRecord back;
        std::string err;
        CHECK(UndoRecord::parse(u.to_json(), back, &err) && back.writes.size() == 6 && back.code == SheetWorld::kCode && back.team == 95 && back.pids == u.pids && back.state == "written" &&
                  back.mirror.size() == 1 && back.mirror[0].old_value == 55 && back.mirror_row_key == 2 && back.dirty_before == 1,
              "round trip: " + err);
        CHECK(!UndoRecord::parse("{\"x\":1}", back, &err), "not an undo file: " + err);
        const fs::path f = test_tmp_dir("channel_e_undo") / "channel_e_undo.json";
        CHECK(save_undo(f, u, &err) && load_undo(f, back, &err) && back.writes.size() == 6, "saved and loaded: " + err);
        // apply, then undo
        CHECK(apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p, checks(&al)).ok, "written");
        WriteHistory hist;
        hist.note(p);  // what the service records after the job
        const JobChecks uck = checks(nullptr, &hist);
        CHECK(reverted(live(w), u).empty(), "nothing overwritten");
        Plan up;
        std::vector<std::string> skipped;
        CHECK(make_undo_plan(live(w), u, "", 3, up, skipped).empty() && up.undo && up.writes.size() == 6 && skipped.empty() && up.load_gen == 3,
              "the undo plan: approach, line height, build-up and the slot");
        // another career load (or Lua session): refused, the reload threw the change away
        Plan tmp;
        CHECK(make_undo_plan(live(w), u, "", 4, tmp, skipped).find("another career load") != std::string::npos, "undo across load_gen: refused");
        CHECK(make_undo_plan(live(w), u, "S2", 3, tmp, skipped).find("another Lua session") != std::string::npos, "undo across sessions: refused");
        // a corrupted undo file: slot 11 is refused when parsed; depth 0 and a group change are refused inside the job
        {
            std::string j = u.to_json();
            const size_t at = j.find("\"slot\": 9");
            CHECK(at != std::string::npos, "the JSON names slot 9");
            std::string bad = j;
            bad.replace(at, 9, "\"slot\": 11");
            UndoRecord x;
            CHECK(!UndoRecord::parse(bad, x, &err) && err.find("slot 11") != std::string::npos, "slot 11: " + err);
            UndoRecord d = u;
            for (auto& fw : d.writes)
                if (fw.kind == FieldKind::LineHeight) fw.old_value = 0;
            Plan dp;
            CHECK(make_undo_plan(live(w), d, "", 3, dp, skipped).empty(), "the depth-0 undo plan is made (the file is trusted only that far)");
            const ApplyResult dr = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), dp, uck);
            CHECK(!dr.ok && dr.stage == "G6" && dr.message.find("outside 1..100") != std::string::npos && w.i32(SheetWorld::kRecs + kRecLineHeight) == 75,
                  "line height 0: refused in the job: " + dr.message);
            UndoRecord gcg = u;
            for (auto& fw : gcg.writes)
                if (fw.kind == FieldKind::Role) fw.old_value = role_word(3, 21, 5);
            Plan gp;
            make_undo_plan(live(w), gcg, "", 3, gp, skipped);
            const ApplyResult gr = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), gp, uck);
            CHECK(!gr.ok && gr.message.find("position group would change") != std::string::npos && w.i32(w.slot(9) + kSlotRole) == 38214,
                  "a group change: refused in the job: " + gr.message);
            // a value the game never held before Turbo's write (the history says 25)
            UndoRecord ov = u;
            for (auto& fw : ov.writes)
                if (fw.kind == FieldKind::LineHeight) fw.old_value = 60;
            Plan op;
            make_undo_plan(live(w), ov, "", 3, op, skipped);
            const ApplyResult orr = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), op, uck);
            CHECK(!orr.ok && orr.message.find("before Turbo's first write") != std::string::npos, "an undo value not in the history: " + orr.message);
            Plan fresh;
            make_undo_plan(live(w), u, "", 3, fresh, skipped);
            CHECK(fresh.writes.size() == 6 && !apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), fresh, checks(nullptr)).ok, "an undo without the history: refused");
        }
        // Edit Tactic changed the depth since: that field is left alone, the rest goes back
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(40));
        CHECK(has_part(reverted(live(w), u), "Line Height 75 -> 40"), "the overwrite is seen");
        CHECK(make_undo_plan(live(w), u, "", 3, up, skipped).empty() && up.writes.size() == 5 && has_part(skipped, "Line Height is 40"), "line height left as it is");
        const ApplyResult ur = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), up, uck);
        CHECK(ur.ok && ur.undo && w.i32(SheetWorld::kRecs + kRecBuildUp) == 0 && w.i32(w.slot(9) + kSlotRole) == 38213 && w.i32(SheetWorld::kRecs + kRecLineHeight) == 40 &&
                  w.i32(SheetWorld::kRecs + kRecApproach) == 0,
              "approach, build-up and the slot back, line height kept: " + ur.message);
        CHECK(ur.log_line.find("channel E: undo team 95") == 0, ur.log_line);
        CHECK(!make_undo_plan(live(w), u, "", 3, up, skipped).empty(), "nothing holds Turbo's values any more");
        // another record: refused
        UndoRecord other = u;
        other.formation = 28;
        CHECK(make_undo_plan(live(w), other, "", 3, up, skipped).find("not the one Turbo wrote") != std::string::npos, "another formation: nothing put back");
        // a second Apply keeps the values from before the first
        SheetWorld w2;
        Plan p1, p2;
        Request a;
        a.line_height = 60;
        a.approach = 1;
        a.slots = {{9, -1, 6}};
        CHECK(make_plan(live(w2), a, p1, &al).empty(), "plan 1");
        apply_plan(w2.mem, SheetWorld::kSvc, w2.anchors(), p1, checks(&al));
        WriteHistory hist2;
        hist2.note(p1);
        UndoRecord u1 = UndoRecord::from_plan(p1);
        u1.state = "written";
        u1.dirty_before = 0;
        Request b;
        b.line_height = 70;
        b.approach = 2;
        b.build_up = 1;
        CHECK(make_plan(live(w2), b, p2, nullptr).empty(), "plan 2");
        UndoRecord u2 = merge_undo(u1, UndoRecord::from_plan(p2));
        auto find = [](const UndoRecord& x, FieldKind k, int slot) -> const FieldWrite* {
            for (const auto& fw : x.writes)
                if (fw.kind == k && fw.slot == slot) return &fw;
            return nullptr;
        };
        CHECK(find(u2, FieldKind::LineHeight, -1) && find(u2, FieldKind::LineHeight, -1)->old_value == 25 && find(u2, FieldKind::LineHeight, -1)->new_value == 70,
              "line height: 25 (before the first write) -> 70");
        CHECK(find(u2, FieldKind::BuildUp, -1) && find(u2, FieldKind::BuildUp, -1)->old_value == 0, "build-up: 0 -> 1");
        CHECK(find(u2, FieldKind::Role, 9) && find(u2, FieldKind::Role, 9)->old_value == 38213 && find(u2, FieldKind::Role, 9)->new_value == 38214 &&
                  u2.writes.size() == 6 && u2.dirty_before == 0,
              "the slot of the first write stays listed with its original value");
        apply_plan(w2.mem, SheetWorld::kSvc, w2.anchors(), p2);
        hist2.note(p2);
        CHECK(make_undo_plan(live(w2), u2, "", -1, up, skipped).empty() && apply_plan(w2.mem, SheetWorld::kSvc, w2.anchors(), up, checks(nullptr, &hist2)).ok,
              "the merged undo runs");
        CHECK(w2.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && w2.i32(SheetWorld::kRecs + kRecBuildUp) == 0 && w2.i32(w2.slot(9) + kSlotRole) == 38213,
              "back to the values before Turbo's first write");
        UndoRecord another = UndoRecord::from_plan(p2);
        another.team = 96;
        CHECK(merge_undo(u1, another).writes.size() == another.writes.size(), "another club is not merged");
        UndoRecord later = UndoRecord::from_plan(p2);
        later.load_gen = 9;
        CHECK(merge_undo(u1, later).writes.size() == later.writes.size(), "another career load is not merged");
    });

    run_case("channel E 2.4: the kick-off self-proof - 20 s after Lua's match phase the match copies are found (pids / role words), decoded, ONE line names the values", [] {
        SheetWorld w;
        const fs::path o = test_tmp_dir("channel_e_kickoff");
        std::error_code ec;
        fs::remove_all(o, ec);
        ServiceRig rig(w, o);
        rig.find_returns = SheetWorld::kSvc;
        rig.touch(opt_in_file());
        // the decode table: one place, T2's offsets, u8
        const std::vector<CopyField>& tbl = match_copy_fields();
        CHECK(tbl.size() == 3 && std::string(tbl[0].name) == "LH" && tbl[0].offset == 0x158 && std::string(tbl[1].name) == "BU" && tbl[1].offset == 0x15B &&
                  std::string(tbl[2].name) == "DA" && tbl[2].offset == 0x15C && tbl[0].width == 1 && tbl[1].width == 1 && tbl[2].width == 1 && tbl[0].known &&
                  tbl[1].known && tbl[2].known,
              "LH +0x158, BU +0x15B, DA +0x15C, u8");
        CHECK(kCopyStyleWindow == 0x150 && kCopyStyleWindowLen == 0x18 && kKickoffHead == 0x1A0 && kKickoffTail == 0x200, "the window and the dump");
        std::array<int32_t, 11> roles{}, pids{};
        for (int s = 0; s < 11; ++s) {
            roles[size_t(s)] = SheetWorld::kRoles[size_t(s)];
            pids[size_t(s)] = w.pids[size_t(s)];
        }
        roles[9] = 38214;
        const std::vector<uint8_t> pat = role_pattern(roles);
        CHECK(pat.size() == 44, "44 bytes");
        std::vector<uint8_t> buf(0x100, 0);
        std::memcpy(buf.data() + 0x41, pat.data(), pat.size());
        std::memcpy(buf.data() + 0x80, pat.data(), pat.size());
        CHECK(find_bytes(buf.data(), buf.size(), 0x1000, pat, 10, 4) == std::vector<uint64_t>({0x1080}), "4-aligned hits only");
        // a match with no tactic read in the hub before: not checked, said so, nothing dumped
        Context c = rig.ctx();
        c.phase = "hub";
        rig.svc->note_context(c);
        c.hub = false;
        c.phase = "match";
        rig.svc->note_context(c);
        CHECK(rig.logged("Lua phase hub -> match: kick-off check in 20 s (no live tactic was read in the hub before)"), "the phase change is logged");
        rig.svc->kickoff_tick();
        CHECK(rig.svc->kickoff_status().empty(), "not before 20 s");
        rig.clock += 20e6;
        rig.svc->kickoff_tick();
        CHECK(rig.logged("channel E kick-off: not checked (the live tactic was not read in the career hub before this match"), "no arm: logged");
        CHECK(!fs::exists(o / kickoff_dump_file()), "nothing dumped");
        // in the hub: Turbo writes High / 70 / Counter and the striker's focus 6 (the service arms the check with the values it read back)
        c.phase = "hub";
        c.hub = true;
        LiveTactic lt;
        std::string why;
        CHECK(!rig.svc->read(c, lt, why), "locating");
        rig.jobs.pump();
        CHECK(rig.svc->read(c, lt, why), "read in the hub: " + why);
        Request r;
        r.approach = 2;
        r.line_height = 70;
        r.build_up = 1;
        r.slots = {{9, -1, 6}};
        Plan p;
        const RoleAllowList al = allow_of(w);
        CHECK(make_plan(lt, r, p, &al).empty(), "plan");
        p.load_gen = 3;
        CHECK(rig.svc->submit(c, p, why), "queued: " + why);
        rig.jobs.pump();
        ApplyResult res;
        CHECK(rig.svc->poll(res) && res.ok && res.written, "written: " + res.message);
        // the two match-setup copies the game made on 2026-10-12 (report 2.12 + T2's style block): team, formation ids, positions, role words,
        // pids, Line Height / Build-Up Style / Defensive Approach as bytes at +0x158 / +0x15B / +0x15C; and a third hit of the role words
        // without the layout (a UI copy)
        auto put_copy = [&](uint64_t out, int32_t team, int lh, int bu, int da, const std::array<int32_t, 11>& rw) {
            w.mem.map(out, kKickoffHead + kKickoffTail);
            w.mem.wr(out + 0x1C, int32_t(27));
            w.mem.wr(out + 0x20, int32_t(36));
            w.mem.wr(out + 0x24, int32_t(36));
            w.mem.wr(out + kCopyTeam, team);
            w.mem.wr(out + 0x2C, int32_t(18));
            for (int s = 0; s < 11; ++s) {
                w.mem.wr(out + kCopyPositions + 4 * uint64_t(s), SheetWorld::kPositions[size_t(s)]);
                w.mem.wr(out + kCopyRoles + 4 * uint64_t(s), rw[size_t(s)]);
                w.mem.wr(out + kCopyPids + 4 * uint64_t(s), pids[size_t(s)]);
            }
            w.mem.wr(out + 0x158, uint8_t(lh));
            w.mem.wr(out + 0x159, uint8_t(2));
            w.mem.wr(out + 0x15A, uint8_t(2));
            w.mem.wr(out + 0x15B, uint8_t(bu));
            w.mem.wr(out + 0x15C, uint8_t(da));
        };
        const uint64_t out1 = 0x429F9EB08ULL, out2 = 0x429FA0000ULL, ui = 0xFA6ECCF0ULL;
        put_copy(out1, 95, 70, 1, 2, roles);
        put_copy(out2, 95, 70, 1, 2, roles);
        w.mem.map(ui, 0x100);
        w.mem.write(ui, pat.data(), pat.size());
        CHECK(is_match_copy_at(w.mem, out1, 95, pids) && is_match_copy(w.mem, out1 + kCopyRoles, 95, pids) && !is_match_copy(w.mem, ui, 95, pids) &&
                  !is_match_copy_at(w.mem, out1, 96, pids),
              "the layout tells them apart");
        w.mem.wr(out2 + kCopyPositions, int32_t(3));
        CHECK(!is_match_copy_at(w.mem, out2, 95, pids), "positions: the goalkeeper first");
        w.mem.wr(out2 + kCopyPositions, int32_t(0));
        w.mem.wr(out2 + kCopyPositions + 8, int32_t(28));
        CHECK(!is_match_copy_at(w.mem, out2, 95, pids), "positions 0..27");
        w.mem.wr(out2 + kCopyPositions + 8, SheetWorld::kPositions[2]);
        const std::vector<uint8_t> before1 = w.bytes(out1, kKickoffHead + kKickoffTail), before2 = w.bytes(out2, kKickoffHead + kKickoffTail);
        auto match = [&](Context& cx) {
            cx.phase = "prematch";
            cx.hub = false;
            rig.svc->note_context(cx);
            cx.phase = "match";
            rig.svc->note_context(cx);
            rig.clock += 20e6;
            rig.svc->kickoff_tick();
            return rig.svc->kickoff_status();
        };
        // the pre-match screen, then the match: 20 s later the game tick runs the check (the card is never drawn here)
        c.phase = "prematch";
        c.hub = false;
        rig.svc->note_context(c);
        c.phase = "match";
        rig.svc->note_context(c);
        CHECK(rig.logged("Lua phase prematch -> match: kick-off check in 20 s (the tactic after Turbo's write: Line Height 70, Build-Up Style Counter (1), "
                         "Defensive Approach High (2))"),
              "armed with Turbo's values");
        rig.clock += 19e6;
        rig.svc->kickoff_tick();
        CHECK(!fs::exists(o / kickoff_dump_file()), "19 s: not yet");
        rig.clock += 1e6;
        const size_t logs0 = rig.logs.size();
        rig.svc->kickoff_tick();
        const std::string ks = rig.svc->kickoff_status();
        CHECK(ks.find("channel E kick-off: match copy team 95 LH 70 BU 1 DA 2 roles 11/11 = Turbo (ok) | 2 copies (0x429F9EB08, 0x429FA0000; ") == 0,
              "THE line: " + ks);
        CHECK(ks.find("decode LH +0x158 BU +0x15B DA +0x15C u8 (T2 static decode") != std::string::npos &&
                  ks.find("; raw LH +0x158=46 BU +0x15B=01 DA +0x15C=02; u8 70 at +0x158; ") != std::string::npos &&
                  ks.find("window +0x150: 00 00 00 00 00 00 00 00 46 02 02 01 02 00") != std::string::npos &&
                  ks.find("dumped to turbo_output\\channel_e_kickoff_dump.bin (2 x 0x3A0 bytes)") != std::string::npos &&
                  ks.find("looked for the tactic after Turbo's write (LH 70 BU 1 DA 2, Turbo's values)") != std::string::npos,
              "the details: " + ks);
        CHECK(ks.size() < 1000, fmt("one log line fits Turbo's log buffer (%zu chars)", ks.size()));
        size_t lines = 0;
        for (size_t i = logs0; i < rig.logs.size(); ++i) lines += rig.logs[i].find("channel E kick-off:") == 0 ? 1 : 0;
        CHECK(lines == 1 && rig.logs.back() == ks, fmt("exactly one kick-off line logged (%zu)", lines));
        std::vector<uint8_t> dump;
        {
            std::ifstream f(o / kickoff_dump_file(), std::ios::binary);
            dump.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        std::vector<uint8_t> both = before1;
        both.insert(both.end(), before2.begin(), before2.end());
        CHECK(dump == both, fmt("the dump is both copies byte for byte (%zu bytes)", dump.size()));
        std::string idx;
        {
            std::ifstream f(o / kickoff_index_file(), std::ios::binary);
            idx.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        CHECK(idx.find("block 0: out 0x429F9EB08") != std::string::npos && idx.find("+0x28 team 95") != std::string::npos &&
                  idx.find("decode (match_copy_fields): LH +0x158 = 70 BU +0x15B = 1 DA +0x15C = 2, roles 11/11") != std::string::npos &&
                  idx.find("u8 == Line Height 70 outside the known blocks at: +0x158") != std::string::npos &&
                  idx.find("Defensive Approach High (2) (Turbo's values)") != std::string::npos,
              "the index: " + idx.substr(0, 600));
        CHECK(w.bytes(out1, kKickoffHead + kKickoffTail) == before1 && w.bytes(out2, kKickoffHead + kKickoffTail) == before2, "nothing written to the game");
        // once per match: the next ticks do nothing
        const size_t logs = rig.logs.size();
        rig.clock += 60e6;
        rig.svc->kickoff_tick();
        CHECK(rig.logs.size() == logs, "once per match");
        // the match was set up from other values: the line says what differs
        w.mem.wr(out1 + 0x158, uint8_t(25));
        std::string k = match(c);
        CHECK(k.find("channel E kick-off: match copy team 95 LH 25 BU 1 DA 2 roles 11/11 = game (differs: copy 1: LH 25 not 70) | ") == 0, "LH differs: " + k);
        w.mem.wr(out1 + 0x158, uint8_t(70));
        // a copy found by its player ids whose role words are not Turbo's: roles 10/11
        w.mem.wr(out2 + kCopyRoles + 4 * 9, int32_t(38213));
        k = match(c);
        CHECK(k.find("roles 11/11 = game (differs: copy 2: roles 10/11) | 2 copies") != std::string::npos, "roles differ (found by its pids): " + k);
        w.mem.wr(out2 + kCopyRoles + 4 * 9, int32_t(38214));
        // a match whose copies are made late: a look every 15 s of wall time, found on the third
        w.mem.wr(out1 + kCopyTeam, int32_t(7));
        w.mem.wr(out2 + kCopyTeam, int32_t(7));
        k = match(c);
        CHECK(k.find("channel E kick-off: no match copy found (") == 0 && k.find("(look 1 of 5)") != std::string::npos && rig.logged("looking again in 15 s"),
              "none yet: looks again: " + k);
        rig.clock += 14e6;
        rig.svc->kickoff_tick();
        CHECK(rig.svc->kickoff_status() == k, "not before 15 s");
        rig.clock += 1e6;
        rig.svc->kickoff_tick();
        CHECK(rig.svc->kickoff_status().find("(look 2 of 5)") != std::string::npos, "look 2: " + rig.svc->kickoff_status());
        w.mem.wr(out1 + kCopyTeam, int32_t(95));
        w.mem.wr(out2 + kCopyTeam, int32_t(95));
        rig.clock += 15e6;
        rig.svc->kickoff_tick();
        k = rig.svc->kickoff_status();
        CHECK(k.find("= Turbo (ok) | 2 copies") != std::string::npos && k.find("(look 3 of 5)") != std::string::npos, "found on the third look: " + k);
        // never found: five looks, the last one says so and nothing is scheduled after it
        w.mem.wr(out1 + kCopyTeam, int32_t(7));
        w.mem.wr(out2 + kCopyTeam, int32_t(7));
        k = match(c);
        for (int i = 0; i < 6; ++i) {
            rig.clock += 15e6;
            rig.svc->kickoff_tick();
        }
        k = rig.svc->kickoff_status();
        CHECK(k.find("channel E kick-off: no match copy found (") == 0 && k.find("(look 5 of 5)") != std::string::npos, "the last look: " + k);
        CHECK(!rig.logged("(look 6 of 5)"), "no sixth look");
        w.mem.wr(out1 + kCopyTeam, int32_t(95));
        w.mem.wr(out2 + kCopyTeam, int32_t(95));
        // review m7: the background thread cannot be started: the check says so and is not stuck "running" for the session
        {
            SheetWorld w7;
            const fs::path o7 = test_tmp_dir("channel_e_kickoff7");
            fs::remove_all(o7, ec);
            bool fail_start = true;
            ServiceRig r7(w7, o7, nullptr, [&fail_start](Host& h) {
                h.run_background = [&fail_start](std::function<void()> f) {
                    if (fail_start) throw std::runtime_error("thread");
                    f();
                };
            });
            Context c7 = r7.ctx();
            c7.phase = "match";
            c7.hub = false;
            r7.svc->note_context(c7);
            r7.clock += 20e6;
            r7.svc->kickoff_tick();
            CHECK(r7.svc->kickoff_status() == "channel E kick-off: check failed (the background thread could not be started)" &&
                      r7.logged("channel E kick-off: check failed (the background thread could not be started)"),
                  "said: " + r7.svc->kickoff_status());
            fail_start = false;
            c7.phase = "hub";
            r7.svc->note_context(c7);
            c7.phase = "match";
            r7.svc->note_context(c7);
            r7.clock += 20e6;
            r7.svc->kickoff_tick();
            CHECK(r7.svc->kickoff_status().find("channel E kick-off: not checked (") == 0, "the next match is checked again: " + r7.svc->kickoff_status());
        }
        // its own off switch, channel E's kill switches and the opt-in (it reads all of memory during a match: testers only)
        rig.touch(kickoff_off_file());
        k = match(c);
        CHECK(k == "channel E kick-off: not checked (off: turbo_output\\channel_e_kickoff_off.txt)", k);
        rig.remove(kickoff_off_file());
        fs::remove(o / kickoff_dump_file(), ec);
        for (const char* ks2 : {kill_switch_file(), master_switch_file()}) {
            rig.touch(ks2);
            k = match(c);
            CHECK(k == std::string("channel E kick-off: not checked (kill switch turbo_output\\") + ks2 + " is present (channel E is off))", k);
            rig.remove(ks2);
        }
        CHECK(!fs::exists(o / kickoff_dump_file()), "nothing dumped while a kill switch is present");
        rig.remove(opt_in_file());
        k = match(c);
        CHECK(k.find("channel E kick-off: not checked (channel E is not switched on") == 0, k);
        rig.touch(opt_in_file());
        // "Turbo's values" only while the tactic read is exactly what Turbo's write left: the game changed the line height (same career load)
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(50));
        c.phase = "hub";
        c.hub = true;
        CHECK(rig.svc->read(c, lt, why) && lt.line_height == 50, "read in the hub: " + why);
        k = match(c);
        CHECK(k.find("LH 70 BU 1 DA 2 roles 11/11 = game (differs from the hub's read: copy 1: LH 70 not 50; copy 2: LH 70 not 50) | ") != std::string::npos &&
                  k.find("the game's values)") != std::string::npos,
              "the hub read the game's 50: " + k);
        w.mem.wr(out1 + 0x158, uint8_t(50));
        w.mem.wr(out2 + 0x158, uint8_t(50));
        k = match(c);
        CHECK(k.find("LH 50 BU 1 DA 2 roles 11/11 = game (the hub's values; no Turbo write in place) | ") != std::string::npos, "the game's values: " + k);
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(70));
        w.mem.wr(out1 + 0x158, uint8_t(70));
        w.mem.wr(out2 + 0x158, uint8_t(70));
        c.phase = "hub";
        c.hub = true;
        CHECK(rig.svc->read(c, lt, why) && lt.line_height == 70, "read in the hub: " + why);
        k = match(c);
        CHECK(k.find("= Turbo (ok) | ") != std::string::npos, "Turbo's values again: " + k);
        // the career was reloaded (load 4) since the tactic was read: not checked; after a read in load 4's hub the values are the game's
        Context c4 = rig.ctx(4);
        c4.phase = "hub";
        rig.svc->note_context(c4);
        k = match(c4);
        CHECK(k == "channel E kick-off: not checked (the live tactic was last read in career load 3, this match is in load 4 (open Teams > Tactics > My team "
                   "once in the hub))",
              "an earlier load's tactic: " + k);
        c4.phase = "hub";
        c4.hub = true;
        CHECK(!rig.svc->read(c4, lt, why), "load 4: the service is located again first");
        rig.jobs.pump();
        CHECK(rig.svc->read(c4, lt, why) && lt.line_height == 70, "read in load 4's hub: " + why);
        k = match(c4);
        CHECK(k.find("= game (the hub's values; no Turbo write in place) | ") != std::string::npos, "after the reload the values are not called Turbo's: " + k);
        c.phase = "hub";
        rig.svc->note_context(c);
        // a field the table does not know: "?" and "= unknown (...)"; the helpers
        KickoffCopy kc;
        kc.out = out1;
        kc.bytes = before1;
        KickoffArm arm;
        arm.armed = true;
        arm.roles = roles;
        arm.pids = pids;
        arm.line_height = 70;
        arm.build_up = 1;
        arm.approach = 2;
        arm.turbo_values = true;
        std::vector<CopyField> no_da = {{"LH", 0x158, 1, true}, {"BU", 0x15B, 1, true}, {"DA", 0x15C, 1, false}};
        const KickoffRead kr = decode_match_copy(kc, arm, no_da);
        bool ok = true;
        CHECK(kr.lh == 70 && kr.bu == 1 && kr.da == -1 && !kr.decoded() && kickoff_verdict(arm, {kr}, ok) == "= unknown (DA not decoded: see the raw window)" && !ok,
              "an unknown field is never guessed");
        CHECK(kr.raw == "LH +0x158=46 BU +0x15B=01 DA +0x15C=02 (not decoded)" && kr.lh_bytes == std::vector<size_t>({0x158}),
              "its raw byte is still carried (X2's readout): " + kr.raw);
        // a table entry X2 finds elsewhere (say +0x160, two bytes): raw bytes read there, decoded when known
        const KickoffRead k2 = decode_match_copy(kc, arm, {{"LH", 0x160, 2, false}});
        CHECK(k2.raw == "LH +0x160=0000 (not decoded)" && k2.lh == -1, "another offset, raw: " + k2.raw);
        CHECK(kickoff_line(arm, {kr}, "") == "channel E kick-off: match copy team 95 LH 70 BU 1 DA ? roles 11/11 = unknown (DA not decoded: see the raw window)",
              kickoff_line(arm, {kr}, ""));
        const KickoffRead full = decode_match_copy(kc, arm);
        CHECK(kickoff_verdict(arm, {full}, ok) == "= Turbo (ok)" && ok, "decoded: Turbo (ok)");
        CHECK(window_hex({0x46, 0x02, 0xff}) == "46 02 ff" && kickoff_value_offsets(before1, 27) == std::vector<size_t>({0x1C}) &&
                  kickoff_byte_offsets(before1, 70) == std::vector<size_t>({0x158}),
              "the offset helpers");
    });

    run_case("channel E 2.3: Edit Tactic's working copies - header checks, matched by the map record's code, never guessed", [] {
        SheetWorld w;
        LiveTactic lt = live(w);
        CHECK(lt.code == "?ZaE&CYzKoT7" && lt.matching_copies() == 2, "two copies hold the tactic");
        const WorkCopy* f80 = lt.copy(0xF80);
        CHECK(f80 && f80->addr == SheetWorld::kSvc + 0xF80 && f80->code == "?ZaE&CYzKoT7" && f80->line_height == 25 && f80->build_up == 0 &&
                  f80->approach == 0 && f80->slots == w.copy_slots(0xF80) && f80->pids[9] == w.pids[9] && f80->roles[9] == 38213 && f80->label() == "svc+0xF80",
              "svc+0xF80 read");
        CHECK(lt.copy(0x1268)->why.find("empty template") != std::string::npos && lt.copy(0x1234) == nullptr, "a template; no other offset");
        struct Case {
            const char* name;
            std::function<void(SheetWorld&)> mutate;
            const char* expect;
        };
        const std::vector<Case> cases = {
            {"another code (an Edit Tactic save not committed)", [](SheetWorld& x) { x.mem.write(x.copy_rec(0xF80) + kRecCode, SheetWorld::kOtherCode, 12); }, ""},
            {"a code that is not printable", [](SheetWorld& x) { x.mem.wr(x.copy_rec(0xF80) + kRecCode + 3, uint8_t(0x07)); }, "not 12 printable"},
            {"a 13-character code", [](SheetWorld& x) { x.mem.wr(x.copy_rec(0xF80) + kRecCode + 12, uint8_t('x')); }, "longer than 12"},
            {"not active", [](SheetWorld& x) { x.mem.wr(x.copy_rec(0xF80) + kRecActive, uint8_t(0)); }, "not marked active"},
            {"another formation", [](SheetWorld& x) { x.mem.wr(x.copy_rec(0xF80) + kRecFormation, int32_t(28)); }, "another formation"},
            {"another source formation", [](SheetWorld& x) { x.mem.wr(x.copy_rec(0xF80) + kRecSourceFormation2, int32_t(37)); }, "another formation"},
            {"10 slots", [](SheetWorld& x) { x.mem.wr(x.copy_rec(0xF80) + kRecSlotEnd, x.copy_slots(0xF80) + 10 * kSlotStride); }, "is not 11 slots"},
            {"a slot without a player", [](SheetWorld& x) { x.mem.wr(x.copy_slot(0xF80, 4) + kSlotPid, int32_t(-1)); }, "holds no player"},
            {"a role mirror that disagrees", [](SheetWorld& x) { x.mem.wr(x.copy_slot(0xF80, 9) + kSlotFocus, int32_t(1)); }, "role word"},
            {"a name pointer that is not one", [](SheetWorld& x) { x.mem.wr(x.copy_rec(0xF80) + kRecName, uint64_t(0x10)); }, "no tactic record header"},
        };
        for (const Case& c : cases) {
            SheetWorld x;
            c.mutate(x);
            const LiveTactic l = live(x);
            const WorkCopy* cp = l.copy(0xF80);
            CHECK(cp && !cp->matches && l.matching_copies() == 1 && (std::string(c.expect).empty() ? cp->why.empty() : cp->why.find(c.expect) != std::string::npos),
                  std::string(c.name) + ": " + (cp ? cp->why : std::string("?")));
        }
        // the map record's code not readable: no copy can match
        SheetWorld nc;
        nc.mem.wr(SheetWorld::kRecs + kRecCode + 2, uint8_t(0x01));
        CHECK(live(nc).code.empty() && live(nc).matching_copies() == 0, "a map record without a readable code: nothing matches");
    });

    run_case("channel E copy machinery (X3 branches a / b): only a copy target is written, none by default; a copy of another tactic or line-up is left out", [] {
        // stand-in targets: the inline working copies (2.4 never names them: the service's jobs pass no copy target)
        const std::set<uint64_t> e2(kWorkCopyOffsets.begin(), kWorkCopyOffsets.end());
        auto tck = [&e2](const RoleAllowList* al) {
            JobChecks c;
            c.allow = al;
            c.copy_targets = &e2;
            return c;
        };
        Request r;
        r.approach = 2;
        r.line_height = 90;
        r.build_up = 1;
        // the default: no target, copies holding the tactic stay as they are, nothing is said
        {
            SheetWorld d;
            Plan pd;
            CHECK(make_plan(live(d), r, pd, nullptr).empty(), "plan");
            std::string dn;
            CHECK(copy_writes(live(d), pd, dn, nullptr).empty() && dn.empty(), "no target: no copy write, no note");
            const std::vector<uint8_t> svc_before = d.bytes(SheetWorld::kSvc, kSvcSize);
            const ApplyResult rd = apply_plan(d.mem, SheetWorld::kSvc, d.anchors(), pd);
            CHECK(rd.ok && rd.written && rd.copies_written == 0 && rd.copies.empty() && d.bytes(SheetWorld::kSvc, kSvcSize) == svc_before &&
                      d.i32(SheetWorld::kRecs + kRecApproach) == 2,
                  "the default writes the map record only: " + rd.log_line);
            // one target: only that copy
            const std::set<uint64_t> one = {0x1170};
            JobChecks c1;
            c1.copy_targets = &one;
            SheetWorld o1;
            Plan po1;
            CHECK(make_plan(live(o1), r, po1, nullptr).empty(), "plan");
            const ApplyResult r1 = apply_plan(o1.mem, SheetWorld::kSvc, o1.anchors(), po1, c1);
            CHECK(r1.ok && r1.copies_written == 1 && o1.i32(o1.copy_rec(0x1170) + kRecApproach) == 2 && o1.i32(o1.copy_rec(0xF80) + kRecApproach) == 0,
                  "only the target is written: " + r1.copies);
        }
        SheetWorld w;
        w.mem.write(w.copy_rec(0xF80) + kRecCode, SheetWorld::kOtherCode, 12);  // Edit Tactic saved another tactic (not committed)
        w.mem.write(w.copy_rec(0x1170) + kRecCode, SheetWorld::kOtherCode, 12);
        const std::vector<uint8_t> svc_before = w.bytes(SheetWorld::kSvc, kSvcSize);
        Plan p;
        CHECK(make_plan(live(w), r, p, nullptr).empty(), "plan");
        const ApplyResult res = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p, tck(nullptr));
        CHECK(res.ok && res.written && res.copies_written == 0, "the map record written: " + res.message);
        CHECK(res.copies.find("no Edit Tactic working copy holds this tactic (code ?ZaE&CYzKoT7)") != std::string::npos &&
                  res.copies.find("Edit Tactic shows its own copy until the next career load") != std::string::npos,
              "said so: " + res.copies);
        CHECK(res.log_line.find("(read back ok) [no Edit Tactic working copy holds this tactic") != std::string::npos, "in the log line: " + res.log_line);
        CHECK(w.i32(SheetWorld::kRecs + kRecApproach) == 2 && w.bytes(SheetWorld::kSvc, kSvcSize) == svc_before, "map written, the service untouched");
        // one copy holds the tactic but another player stands in the written slot: that copy is left out (map and the other copy written)
        SheetWorld x;
        x.mem.wr(x.copy_slot(0x1170, 9) + kSlotPid, int32_t(100003));
        Request rr;
        rr.slots = {{9, -1, 6}};
        Plan pp;
        const RoleAllowList al = allow_of(x);
        CHECK(make_plan(live(x), rr, pp, &al).empty(), "plan");
        const ApplyResult r2 = apply_plan(x.mem, SheetWorld::kSvc, x.anchors(), pp, tck(&al));
        CHECK(r2.ok && r2.copies_written == 1 && x.i32(x.copy_slot(0xF80, 9) + kSlotFocus) == 6 && x.i32(x.copy_slot(0x1170, 9) + kSlotFocus) == 5,
              "svc+0xF80 written, svc+0x1170 left: " + r2.copies);
        CHECK(r2.copies.find("svc+0x1170 (its slot 9 holds player 100003) NOT written") != std::string::npos && r2.log_line.find("NOT written") != std::string::npos,
              "said so: " + r2.log_line);
        // the same code and formation but other players (another team's or another line-up's tactic) in a slot the plan does not write: an
        // Apply of the approach / line height / build-up only leaves that copy out too (players are compared in all 11 slots)
        SheetWorld o;
        o.mem.wr(o.copy_slot(0x1170, 4) + kSlotPid, int32_t(200005));
        const LiveTactic lo = live(o);
        CHECK(lo.copy(0x1170) && lo.copy(0x1170)->matches, "the code still names it this tactic");
        Plan po;
        CHECK(make_plan(lo, r, po, nullptr).empty(), "plan");
        std::string onote;
        const std::vector<FieldWrite> ocw = copy_writes(lo, po, onote, &e2);
        CHECK(ocw.size() == 3 && ocw[0].copy == 0xF80 && onote.find("svc+0x1170 (its slot 4 holds player 200005) NOT written") != std::string::npos,
              "the preview leaves it out: " + onote);
        const ApplyResult ro = apply_plan(o.mem, SheetWorld::kSvc, o.anchors(), po, tck(nullptr));
        CHECK(ro.ok && ro.copies_written == 1 && o.i32(o.copy_rec(0xF80) + kRecApproach) == 2 && o.i32(o.copy_rec(0x1170) + kRecApproach) == 0 &&
                  o.i32(o.copy_rec(0x1170) + kRecLineHeight) == 25 && o.i32(o.copy_rec(0x1170) + kRecBuildUp) == 0 && o.i32(SheetWorld::kRecs + kRecApproach) == 2,
              "map and svc+0xF80 written, svc+0x1170 untouched: " + ro.copies);
        CHECK(ro.log_line.find("svc+0x1170 (its slot 4 holds player 200005) NOT written") != std::string::npos, "logged: " + ro.log_line);
        // the map record holds a Build-Up Style without a known label (the game's 2) and only the line height changes: the map record keeps its
        // 2 (written back unchanged), a copy holding 0 never gets the unknown 2 (left out), a copy holding 2 already is written
        SheetWorld u;
        u.mem.wr(SheetWorld::kRecs + kRecBuildUp, int32_t(2));
        u.put_copy(0xF80, SheetWorld::kCode, 25, 2, 0);
        Request rl;
        rl.line_height = 60;
        rl.approach = 1;
        Plan pu;
        CHECK(make_plan(live(u), rl, pu, nullptr).empty() && pu.writes.size() == 3 && pu.writes[2].kind == FieldKind::BuildUp && pu.writes[2].new_value == 2,
              "plan: the game's build-up 2 kept");
        const ApplyResult ru = apply_plan(u.mem, SheetWorld::kSvc, u.anchors(), pu, tck(nullptr));
        CHECK(ru.ok && ru.copies_written == 1 && u.i32(SheetWorld::kRecs + kRecLineHeight) == 60 && u.i32(SheetWorld::kRecs + kRecBuildUp) == 2 &&
                  u.i32(u.copy_rec(0xF80) + kRecLineHeight) == 60 && u.i32(u.copy_rec(0x1170) + kRecBuildUp) == 0 &&
                  u.i32(u.copy_rec(0x1170) + kRecLineHeight) == 25,
              "map and svc+0xF80 (already 2) written, svc+0x1170 left: " + ru.copies);
        CHECK(ru.copies.find("svc+0x1170 (its Build-Up Style is 0; the live record's 2 has no known label and is not copied) NOT written") != std::string::npos,
              "said so: " + ru.copies);
        bool unknown_copied = false;
        for (const FieldWrite& fw : ru.writes) unknown_copied = unknown_copied || (fw.copy && fw.kind == FieldKind::BuildUp);
        CHECK(!unknown_copied, "no Build-Up Style write into a copy");
        // a copy that already holds the values is not written again
        SheetWorld y;
        y.put_copy(0x1170, SheetWorld::kCode, 90, 1, 2);
        Plan py;
        CHECK(make_plan(live(y), r, py, nullptr).empty(), "plan");
        const ApplyResult r3 = apply_plan(y.mem, SheetWorld::kSvc, y.anchors(), py, tck(nullptr));
        CHECK(r3.ok && r3.copies_written == 1 && r3.copies.find("svc+0x1170 already holds these values") != std::string::npos, "already there: " + r3.copies);
    });

    run_case("channel E copy machinery: copies in the snapshot, the read-back and the undo; the service's job and undo leave every copy alone", [] {
        const std::set<uint64_t> e2(kWorkCopyOffsets.begin(), kWorkCopyOffsets.end());
        Request r;
        r.approach = 2;
        r.line_height = 90;
        r.build_up = 1;
        {
            // the service (no copy target): map record only, and an undo file naming a copy (2.3's E2) leaves that copy as it is
            SheetWorld w;
            const fs::path out = test_tmp_dir("channel_e_copies");
            std::error_code ec;
            fs::remove_all(out, ec);
            ServiceRig rig(w, out);
            rig.find_returns = SheetWorld::kSvc;
            rig.touch(opt_in_file());
            rig.jobs.inline_run = true;
            LiveTactic lt;
            std::string why;
            CHECK(rig.svc->read(rig.ctx(), lt, why), "located and read: " + why);
            Plan p;
            CHECK(make_plan(lt, r, p, nullptr).empty(), "plan");
            p.load_gen = 3;
            CHECK(rig.svc->submit(rig.ctx(), p, why), "queued: " + why);
            ApplyResult res;
            CHECK(rig.svc->poll(res) && res.ok && res.written && res.writes.size() == 3 && w.i32(w.copy_rec(0xF80) + kRecApproach) == 0 &&
                      w.i32(w.copy_rec(0x1170) + kRecApproach) == 0,
                  fmt("map 3, no copy (%zu)", res.writes.size()));
            UndoRecord u = UndoRecord::from_plan(p);
            u.load_gen = 3;
            u.state = "written";
            u.writes.push_back({FieldKind::Approach, -1, 0, 2, 0xF80});  // as a 2.3 undo file would name it
            w.mem.wr(w.copy_rec(0xF80) + kRecApproach, int32_t(2));
            rig.svc->read(rig.ctx(), lt, why);
            Plan up;
            std::vector<std::string> skipped;
            CHECK(make_undo_plan(lt, u, "", 3, up, skipped).empty() && up.writes.size() == 3 && has_part(skipped, "Turbo 2.4 writes no Edit Tactic working copy"),
                  "the copy is left alone: " + (skipped.empty() ? std::string() : skipped[0]));
            CHECK(rig.svc->submit(rig.ctx(), up, why) && rig.svc->poll(res) && res.ok && res.undo && w.i32(SheetWorld::kRecs + kRecApproach) == 0 &&
                      w.i32(w.copy_rec(0xF80) + kRecApproach) == 2,
                  "undone: the map record back, the copy as it is: " + res.message);
            Plan forged = up;
            forged.writes = {{FieldKind::Approach, -1, 2, 0, 0xF80}};
            CHECK(check_plan(live(w), forged, checks(nullptr)).find("Turbo writes no Edit Tactic working copy (2.4)") != std::string::npos,
                  "a copy undo without the target: refused in the job");
        }
        // the machinery with stand-in targets
        SheetWorld w;
        JobChecks ck;
        ck.copy_targets = &e2;
        Plan p;
        CHECK(make_plan(live(w), r, p, nullptr).empty(), "plan");
        p.load_gen = 3;
        const ApplyResult res = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p, ck);
        CHECK(res.ok && res.written && res.writes.size() == 3 + 6, fmt("map 3 + copies 2 x 3 (%zu)", res.writes.size()));
        WriteHistory hist;
        hist.note(p, res.writes);
        UndoRecord u = UndoRecord::from_plan(p);
        u.session = "";
        u.load_gen = 3;
        u.state = "written";
        add_job_writes(u, res.writes);
        CHECK(u.writes.size() == 9, "the undo record lists the copies too");
        UndoRecord back;
        std::string err;
        CHECK(UndoRecord::parse(u.to_json(), back, &err) && back.writes.size() == 9 && back.writes[3].copy == 0xF80 && back.code == SheetWorld::kCode,
              "the copies survive the JSON: " + err);
        std::string bad = u.to_json();
        const size_t at = bad.find("\"0xF80\"");
        CHECK(at != std::string::npos, "the copy is named in the JSON");
        bad.replace(at, 7, "\"0xF88\"");
        CHECK(!UndoRecord::parse(bad, back, &err) && err.find("not one of Edit Tactic's") != std::string::npos, "an unknown copy offset: " + err);
        // Edit Tactic saved svc+0xF80 since (another code): the undo puts back the map record and svc+0x1170, leaves svc+0xF80 alone
        CHECK(reverted(live(w), u).empty(), "nothing overwritten yet");
        w.mem.write(w.copy_rec(0xF80) + kRecCode, SheetWorld::kOtherCode, 12);
        w.mem.wr(w.copy_rec(0xF80) + kRecLineHeight, int32_t(50));
        LiveTactic lt = live(w);
        CHECK(has_part(reverted(lt, u), "Edit Tactic copy svc+0xF80 holds another tactic now"), "the copy's change is seen");
        Plan up;
        std::vector<std::string> skipped;
        CHECK(make_undo_plan(lt, u, "", 3, up, skipped, &e2).empty() && up.writes.size() == 3 + 3 && has_part(skipped, "svc+0xF80 does not hold this tactic any more"),
              fmt("map (approach, line height, build-up) + svc+0x1170 (%zu): ", up.writes.size()) + (skipped.empty() ? "" : skipped[0]));
        JobChecks uck;
        uck.history = &hist;
        uck.copy_targets = &e2;
        const ApplyResult ur = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), up, uck);
        CHECK(ur.ok && ur.written && ur.undo, "undone: " + ur.message);
        CHECK(w.i32(SheetWorld::kRecs + kRecApproach) == 0 && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && w.i32(SheetWorld::kRecs + kRecBuildUp) == 0,
              "map back to Deep / 25 / Balanced");
        CHECK(w.i32(w.copy_rec(0x1170) + kRecApproach) == 0 && w.i32(w.copy_rec(0x1170) + kRecLineHeight) == 25 && w.i32(w.copy_rec(0x1170) + kRecBuildUp) == 0,
              "svc+0x1170 back");
        CHECK(w.i32(w.copy_rec(0xF80) + kRecLineHeight) == 50 && w.i32(w.copy_rec(0xF80) + kRecApproach) == 2, "svc+0xF80 (Edit Tactic's) left as it is");
        CHECK(ur.log_line.find("channel E: undo team 95 record 0 approach 2->0 line height 90->25 build-up 1->0; Edit Tactic copy svc+0x1170 too (read back ok)") == 0,
              "log: " + ur.log_line);
        // an undo value that is not the copy's original (history) is refused in the job
        WriteHistory h;
        h.note(p, {{FieldKind::LineHeight, -1, 25, 90, 0x1170}});  // the copy held 25 before Turbo's first write
        Plan forged;
        forged.user = 95;
        forged.record_index = 0;
        forged.load_gen = 3;
        forged.undo = true;
        forged.writes = {{FieldKind::LineHeight, -1, 25, 60, 0x1170}};
        LiveTactic now = live(w);
        JobChecks fck;
        fck.history = &h;
        fck.copy_targets = &e2;
        CHECK(check_plan(now, forged, fck).find("Line Height in Edit Tactic copy svc+0x1170: the undo would write 60, but the game held 25") == 0,
              "a forged copy undo: " + check_plan(now, forged, fck));
        forged.writes[0].copy = 0xF80;  // Edit Tactic's own tactic now
        CHECK(check_plan(now, forged, fck).find("does not hold this tactic any more") != std::string::npos, "a copy of another tactic: refused");
        // a copy write that does not land: everything (map and copies) is put back, channel E off for the session
        SheetWorld z;
        FlakyMemory fm(z.mem);
        fm.drop = z.copy_rec(0x1170) + kRecApproach;
        Plan pz;
        CHECK(make_plan(live(z), r, pz, nullptr).empty(), "plan");
        const ApplyResult rz = apply_plan(fm, SheetWorld::kSvc, z.anchors(), pz, ck);
        CHECK(!rz.ok && rz.undone && rz.session_off && rz.message.find("Defensive Approach in Edit Tactic copy svc+0x1170 reads 0, not 2") != std::string::npos,
              "mismatch: " + rz.message);
        CHECK(z.i32(SheetWorld::kRecs + kRecApproach) == 0 && z.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && z.i32(z.copy_rec(0xF80) + kRecLineHeight) == 25 &&
                  z.i32(z.copy_rec(0xF80) + kRecApproach) == 0,
              "map and the other copy put back");
    });

    run_case("channel E 2.4: overwrite detection and Re-apply - the prompt names what the game put there; one plan puts Turbo's values back; Undo still exact", [] {
        SheetWorld w;
        const RoleAllowList al = allow_of(w);
        Request r;
        r.approach = 2;
        r.line_height = 70;
        r.build_up = 1;
        r.slots = {{9, -1, 6}};
        Plan p;
        CHECK(make_plan(live(w), r, p, &al).empty(), "plan");
        p.load_gen = 3;
        const ApplyResult res = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p, checks(&al));
        CHECK(res.ok && res.written, "written: " + res.message);
        WriteHistory hist;
        hist.note(p, res.writes);
        UndoRecord u = UndoRecord::from_plan(p);
        u.state = "written";
        u.load_gen = 3;
        CHECK(replaced_prompt(live(w), u).empty(), "Turbo's values in place: no prompt");
        UndoRecord pend = u;
        pend.state = "pending";
        // the game's Yes on leaving Team Management copies Edit Tactic's screen into the record: Balanced / 50 / Balanced, the striker's old focus
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(50));
        w.mem.wr(SheetWorld::kRecs + kRecApproach, int32_t(1));
        w.mem.wr(SheetWorld::kRecs + kRecBuildUp, int32_t(0));
        w.mem.wr(w.slot(9) + kSlotRole, int32_t(38213));
        w.mem.wr(w.slot(9) + kSlotFocus, int32_t(5));
        const std::string pr = replaced_prompt(live(w), u);
        CHECK(pr == "the game replaced your tactic (LH 50 / Balanced / Balanced). Re-apply?", "the prompt: " + pr);
        CHECK(replaced_prompt(live(w), pend).empty(), "only a write in place is watched");
        // only the striker's focus changed back: still replaced
        UndoRecord only_slot = u;
        only_slot.writes = {u.writes[3], u.writes[4], u.writes[5]};
        CHECK(!replaced_prompt(live(w), only_slot).empty(), "a role word the game put back: replaced");
        // Re-apply: Turbo's values as one request, G6 again
        std::vector<std::string> skipped;
        const Request rr = reapply_request(live(w), u, skipped);
        CHECK(rr.line_height == 70 && rr.build_up == 1 && rr.approach == 2 && rr.slots.size() == 1 && rr.slots[0].slot == 9 && rr.slots[0].role_id == 21 &&
                  rr.slots[0].focus == 6 && skipped.empty(),
              "Turbo's values as a request");
        Plan p2;
        CHECK(make_plan(live(w), rr, p2, &al).empty() && p2.writes.size() == 6, fmt("plan: the record and the slot (%zu)", p2.writes.size()));
        p2.load_gen = 3;
        const ApplyResult r2 = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p2, checks(&al));
        CHECK(r2.ok && r2.written && w.i32(SheetWorld::kRecs + kRecLineHeight) == 70 && w.i32(SheetWorld::kRecs + kRecApproach) == 2 && w.i32(w.slot(9) + kSlotRole) == 38214,
              "re-applied: " + r2.message);
        hist.note(p2, r2.writes);
        CHECK(replaced_prompt(live(w), u).empty(), "in place again: no prompt");
        // the undo after a re-apply puts back what the game had put there (Edit Tactic's values), and the job's history agrees
        UndoRecord n2 = UndoRecord::from_plan(p2);
        n2.load_gen = 3;
        const UndoRecord u2 = merge_undo(u, n2);
        Plan up;
        CHECK(make_undo_plan(live(w), u2, "", 3, up, skipped).empty() && up.writes.size() == 6, fmt("undo plan (%zu)", up.writes.size()));
        const ApplyResult ur = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), up, checks(nullptr, &hist));
        CHECK(ur.ok && ur.undo && w.i32(SheetWorld::kRecs + kRecLineHeight) == 50 && w.i32(SheetWorld::kRecs + kRecApproach) == 1 &&
                  w.i32(SheetWorld::kRecs + kRecBuildUp) == 0 && w.i32(w.slot(9) + kSlotRole) == 38213 && w.i32(w.slot(9) + kSlotFocus) == 5,
              "the undo puts back the game's values from before the re-apply: " + ur.message);
        // a slot that holds another player now is left out of the re-apply, and named
        w.mem.wr(w.slot(9) + kSlotPid, int32_t(100012));
        const Request rs = reapply_request(live(w), u, skipped);
        CHECK(rs.slots.empty() && skipped.size() == 1 && skipped[0] == "slot 9 holds player 100012 now, not 100010", "another player: " + (skipped.empty() ? "" : skipped[0]));
        CHECK(!replaced_prompt(live(w), u).empty(), "the record fields are the game's: replaced");
        // review m4: only a written slot with another player while LH / BU / DA hold Turbo's values -> nothing Re-apply could put back: no prompt
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(70));
        w.mem.wr(SheetWorld::kRecs + kRecApproach, int32_t(2));
        w.mem.wr(SheetWorld::kRecs + kRecBuildUp, int32_t(1));
        CHECK(replaced_prompt(live(w), u).empty(), "a line-up change alone: no prompt");
        w.mem.wr(w.slot(9) + kSlotPid, int32_t(100010));
        // Edit Tactic's save gives the record a new code: still the same tactic record, still prompted
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(50));
        w.mem.wr(SheetWorld::kRecs + kRecApproach, int32_t(1));
        w.mem.write(SheetWorld::kRecs + kRecCode, SheetWorld::kOtherCode, 12);
        CHECK(same_tactic_record(u, live(w)) && !replaced_prompt(live(w), u).empty() && other_tactic_note(live(w), u).empty(), "a new code: the same record");
        CHECK(reapply_check(live(w), u, "", 3).empty(), "re-apply allowed: " + reapply_check(live(w), u, "", 3));
        // review M2: another career load or Lua session: refused
        CHECK(reapply_check(live(w), u, "", 4).find("another career load") != std::string::npos, "another load: refused");
        CHECK(reapply_check(live(w), u, "S2", 3).find("another career load") != std::string::npos, "another session: refused");
        UndoRecord other_club = u;
        other_club.team = 96;
        CHECK(replaced_prompt(live(w), other_club).empty() && reapply_check(live(w), other_club, "", 3).find("club 96") != std::string::npos, "another club");
        // review m4: the user made another record the active tactic: no prompt, a note, no Re-apply
        UndoRecord other_rec = u;
        other_rec.record_index = 1;
        other_rec.formation = 28;
        CHECK(replaced_prompt(live(w), other_rec).empty() && other_tactic_note(live(w), other_rec).find("the active tactic is another one now") == 0 &&
                  reapply_check(live(w), other_rec, "", 3).find("the active tactic is another one now") == 0,
              "another record: " + other_tactic_note(live(w), other_rec));
        // Turbo's values back in place: nothing to re-apply
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(70));
        w.mem.wr(SheetWorld::kRecs + kRecApproach, int32_t(2));
        w.mem.wr(SheetWorld::kRecs + kRecBuildUp, int32_t(1));
        w.mem.wr(w.slot(9) + kSlotRole, int32_t(38214));
        w.mem.wr(w.slot(9) + kSlotFocus, int32_t(6));
        CHECK(reapply_check(live(w), u, "", 3).find("in place again") != std::string::npos, "in place: " + reapply_check(live(w), u, "", 3));
    });

    run_case("channel E 2.3: G2 refuses while Lua says Team Management is open", [] {
        GateInput in;
        in.sw.opt_in = true;
        in.bridge = true;
        in.in_cm = true;
        in.match_phase = "hub";
        in.load_gen = 3;
        in.located_load_gen = 3;
        CHECK(evaluate(in).pass(), "closed: pass");
        in.team_mgmt = true;
        const std::string f = evaluate(in).first();
        CHECK(f.find("G2 career hub: Team Management is open") == 0 && f.find("Yes autosaves the career") != std::string::npos, f);
        in.undo = true;
        CHECK(!evaluate(in).pass(), "an undo too");
    });

    // ------------------------------------------------------------------------------------------------ 2.15 bench keep (Team Management's copy)
    run_case("bench keep 2.15: the working copies - read-only, live only for your team with dirty 255, slots 11, mode -1, current 1", [&] {
        SheetWorld w;
        std::vector<TmCopy> cs = read_tm_copies(w.mem, SheetWorld::kSvc);
        CHECK(cs.size() == 2 && cs[0].offset == 0x550 && cs[1].offset == 0xA60 && cs[0].readable && !cs[0].live(w.team) && !cs[1].live(w.team),
              "two copies read, none built while Team Management is closed (zeros)");
        BenchPlan p;
        tm_open_world(w, p);
        cs = read_tm_copies(w.mem, SheetWorld::kSvc);
        CHECK(cs[0].live(w.team) && cs[0].ids == p.before && cs[0].rec_begin == p.build && cs[0].label() == "svc+0x550" && !cs[0].live(96), "built for team 95");
        for (auto mutate : std::vector<std::function<void()>>{
                 [&] { w.mem.wr(SheetWorld::kSvc + 0x550 + kSheetDirty, uint8_t(1)); }, [&] { w.mem.wr(SheetWorld::kSvc + 0x550 + kSheetMode, int32_t(0)); },
                 [&] { w.mem.wr(SheetWorld::kSvc + 0x550 + kSheetSlotCount, int32_t(10)); },
                 [&] { w.mem.wr(SheetWorld::kSvc + 0x550 + kSheetCurrent, uint8_t(0)); }}) {
            put_tm_copy(w, p.before, p.build);
            mutate();
            CHECK(!read_tm_copies(w.mem, SheetWorld::kSvc)[0].live(w.team), "one field off: not Team Management's copy");
        }
        // the service's read carries them (read_live alone does not)
        const fs::path out = test_tmp_dir("bench_keep_copies");
        std::error_code ec;
        fs::remove_all(out, ec);
        put_tm_copy(w, p.before, p.build);
        ServiceRig rig(w, out);
        rig.find_returns = SheetWorld::kSvc;
        rig.jobs.inline_run = true;
        LiveTactic lt;
        std::string why;
        CHECK(rig.svc->read(rig.ctx(), lt, why) && lt.tm_copies.size() == 2 && lt.tm_copies[0].live(w.team) && lt.tm_copies[0].ids == p.before,
              "CoreService::read fills tm_copies: " + why);
        CHECK(live(w).tm_copies.empty(), "read_live leaves them out (the tactic job reads nothing more)");
    });

    run_case("bench keep 2.15: apply_bench - your saved order (the map) into Team Management's copy, ids 11..51 only, the XI untouched, read back", [&] {
        CHECK(bench::kKeepSaveQuietS == kSaveQuietSeconds, "bench keep waits as long after a save as G2 does");
        SheetWorld w;
        BenchPlan p;
        tm_open_world(w, p);
        const std::vector<uint8_t> sheet0 = w.bytes(SheetWorld::kSheet, kSheetStride), recs0 = w.bytes(SheetWorld::kRecs, 5 * kRecStride),
                                   svc0 = w.bytes(SheetWorld::kSvc, kSvcSize);
        const BenchResult r = apply_bench(w.mem, SheetWorld::kSvc, w.anchors(), p);
        CHECK(r.ok && r.written && r.moved == 4 && r.stage == "done", "written: " + r.message);
        CHECK(copy_ids(w) == p.saved && r.after == p.saved, "the copy holds your saved order");
        CHECK(w.bytes(SheetWorld::kSheet, kSheetStride) == sheet0 && w.bytes(SheetWorld::kRecs, 5 * kRecStride) == recs0, "the map and its records untouched");
        const std::vector<uint8_t> svc1 = w.bytes(SheetWorld::kSvc, kSvcSize);
        bool rest_same = true;
        const size_t ids0 = kTmCopyTarget + kSheetPids + 4 * bench::kStarters, ids1 = kTmCopyTarget + kSheetPids + 4 * kSheetPidCount;
        for (size_t i = 0; i < svc0.size(); ++i)
            if ((i < ids0 || i >= ids1) && svc0[i] != svc1[i]) rest_same = false;
        CHECK(rest_same && w.u8(SheetWorld::kSvc + kTmCopyTarget + kSheetDirty) == 255,
              "nothing else in the service: the copy's ids 0..10, its dirty 255, the other copy, the header byte-identical");
        CHECK(r.log_line == "bench keep: team 95 sheet -1: your saved order written into Team Management's working copy svc+0x550, places 11..51 (4 places; the "
                            "starting XI untouched) (read back ok)",
              r.log_line);
        const BenchResult again = apply_bench(w.mem, SheetWorld::kSvc, w.anchors(), p);
        CHECK(!again.ok && !again.written && again.stage == "changed" && copy_ids(w) == p.saved, "the same plan again: refused: " + again.message);
    });

    run_case("bench keep 2.15: apply_bench refuses - the copy or the map changed, a starter, other players, empty places, the XI, the copy's identity, G5", [&] {
        struct Case {
            const char* name;
            std::function<void(SheetWorld&, BenchPlan&)> tweak;
            const char* stage;
            const char* why;
        };
        auto both = [](SheetWorld& w, BenchPlan& p, const std::function<void(Ids52&)>& f) {  // the copy in memory and the plan's "before" alike
            f(p.before);
            put_tm_copy(w, p.before, p.build);
        };
        const std::vector<Case> cases = {
            {"the copy's order changed since the read (a move on the screen)", [](SheetWorld& w, BenchPlan&) {
                 w.mem.wr(copy_id_at(12), int32_t(100014));
                 w.mem.wr(copy_id_at(13), int32_t(100013)); }, "changed", "the working copy's order changed since the read (#12"},
            {"your saved order (the map) changed since the read", [](SheetWorld& w, BenchPlan&) {
                 w.mem.wr(SheetWorld::kSheet + kSheetPids + 4 * 12, int32_t(100014));
                 w.mem.wr(SheetWorld::kSheet + kSheetPids + 4 * 13, int32_t(100013)); }, "changed", "your saved order (the map) changed since the read (#12"},
            {"a starter moved in the copy", [&](SheetWorld& w, BenchPlan& p) { both(w, p, [](Ids52& x) { std::swap(x[3], x[15]); }); }, "changed",
             "(the starting XI changed (#3"},
            {"another player in the copy", [&](SheetWorld& w, BenchPlan& p) { both(w, p, [](Ids52& x) { x[25] = 999999; }); }, "changed",
             "(other players on the sheet"},
            {"an empty place moved in the copy", [&](SheetWorld& w, BenchPlan& p) { both(w, p, [](Ids52& x) { std::swap(x[30], x[31]); }); }, "changed",
             "(the empty places moved (#30)"},
            {"the copy already holds your order", [&](SheetWorld& w, BenchPlan& p) { both(w, p, [&](Ids52& x) { x = p.saved; }); }, "changed", "(same order)"},
            // slot 9 holds a bench player (still on the sheet: L4 passes)
            {"the map's ids 0..10 are not the slots' players", [](SheetWorld& w, BenchPlan&) { w.mem.wr(w.slot(9) + kSlotPid, int32_t(100020)); }, "changed",
             "ids 0..10 are not the 11 players of the active tactic's slots"},
            {"the copy is not built (dirty 1)", [](SheetWorld& w, BenchPlan& p) { put_tm_copy(w, p.before, p.build, 1); }, "changed",
             "is not Team Management's copy of your sheet now (team 95, dirty 1"},
            {"the copy is another club's", [](SheetWorld& w, BenchPlan& p) { put_tm_copy(w, p.before, p.build, 255, 96); }, "changed", "(team 96, dirty 255"},
            {"the copy was built again", [](SheetWorld&, BenchPlan& p) { p.build = 0x3BCF0000ULL; }, "changed", "the working copy was built again since the read"},
            {"the copy is another sheet", [](SheetWorld& w, BenchPlan&) { w.mem.wr(SheetWorld::kSvc + kTmCopyTarget + kSheetId, int32_t(5)); }, "changed",
             "the working copy is sheet 5, yours is -1"},
            {"another sheet id in the plan", [](SheetWorld&, BenchPlan& p) { p.sheet_id = 7; }, "changed", "your sheet is -1, not 7"},
            {"another club", [](SheetWorld&, BenchPlan& p) { p.user = 96; }, "locate", "layout mismatch"},
            {"G5: a sheet of mode 0", [](SheetWorld& w, BenchPlan&) { w.mem.wr(SheetWorld::kSheet + kSheetMode, int32_t(0)); }, "G5", ""},
        };
        for (const Case& c : cases) {
            SheetWorld w;
            BenchPlan p;
            tm_open_world(w, p);
            c.tweak(w, p);
            const Ids52 map0 = ids_of(w), copy0 = copy_ids(w);
            const BenchResult r = apply_bench(w.mem, SheetWorld::kSvc, w.anchors(), p);
            CHECK(!r.ok && !r.written && r.stage == c.stage && r.message.find(c.why) != std::string::npos && r.message.find("nothing written") != std::string::npos,
                  std::string(c.name) + ": " + r.stage + ": " + r.message);
            CHECK(ids_of(w) == map0 && copy_ids(w) == copy0, std::string(c.name) + ": nothing written");
            CHECK(r.log_line.find("bench keep: refused (") == 0, r.log_line);
        }
        SheetWorld w;
        BenchPlan p;
        tm_open_world(w, p);
        const std::atomic<bool> cancel{true};
        const BenchResult r = apply_bench(w.mem, SheetWorld::kSvc, w.anchors(), p, &cancel);
        CHECK(!r.ok && r.stage == "cancelled" && copy_ids(w) == p.before, "a cancelled job writes nothing");
    });

    run_case("bench keep 2.15: a read-back mismatch puts every id of the copy back; putting back that fails is said", [&] {
        SheetWorld w;
        BenchPlan p;
        tm_open_world(w, p);
        FlakyMemory fm(w.mem);
        fm.drop = copy_id_at(29);  // this write silently does not land
        const BenchResult r = apply_bench(fm, SheetWorld::kSvc, w.anchors(), p);
        CHECK(!r.ok && !r.written && r.undone && !r.restore_failed && r.session_off && r.stage == "readback", "undone: " + r.message);
        CHECK(r.message.find("id #29 reads 100021, not 100030") != std::string::npos && r.message.find("bench keep is off for this session") != std::string::npos,
              r.message);
        CHECK(copy_ids(w) == p.before && ids_of(w) == p.saved, "the copy as before the job, the map untouched");
        CHECK(r.log_line.find("working copy svc+0x550: READ-BACK MISMATCH") != std::string::npos && r.log_line.find("undone from the snapshot") != std::string::npos,
              r.log_line);
        SheetWorld w2;
        BenchPlan p2;
        tm_open_world(w2, p2);
        FlakyMemory fm2(w2.mem);
        fm2.drop = copy_id_at(15);
        fm2.refuse = true;
        const BenchResult r2 = apply_bench(fm2, SheetWorld::kSvc, w2.anchors(), p2);
        CHECK(!r2.ok && r2.undone && copy_ids(w2) == p2.before, "a refused write: undone: " + r2.message);
        SheetWorld w4;
        BenchPlan p4;
        tm_open_world(w4, p4);
        StickyMemory sm(w4.mem);
        sm.on = true;
        sm.drop = copy_id_at(29);
        sm.once = copy_id_at(11);
        const BenchResult r4 = apply_bench(sm, SheetWorld::kSvc, w4.anchors(), p4);
        CHECK(!r4.ok && !r4.undone && r4.restore_failed && r4.session_off && r4.message.find("PUTTING BACK FAILED") != std::string::npos &&
                  r4.log_line.find("undo FAILED") != std::string::npos,
              "restore failed: " + r4.message);
    });

    run_case("bench keep 2.15: the service - switches before queuing and in the job, G2 without Team Management, one channel E job at a time, its own results", [&] {
        const fs::path out = test_tmp_dir("bench_keep_service");
        std::error_code ec;
        fs::remove_all(out, ec);
        SheetWorld w;
        ServiceRig rig(w, out);
        rig.find_returns = SheetWorld::kSvc;
        LiveTactic lt;
        std::string why, why2;
        rig.svc->read(rig.ctx(), lt, why);
        rig.jobs.pump();
        CHECK(rig.svc->read(rig.ctx(), lt, why), "located and read: " + why);
        BenchPlan p;
        tm_open_world(w, p);
        // refused before queuing: no opt-in, each kill switch, G2 for bench keep, another career load
        CHECK(!rig.svc->submit_bench(rig.ctx(), p, why) && why == "off until turbo_output\\bench_keep_on.txt exists (opt-in)" && rig.jobs.q.empty(), "no opt-in: " + why);
        rig.touch(bench::keep_on_file());
        for (const char* f : {bench::keep_off_file(), master_switch_file(), kill_switch_file()}) {
            rig.touch(f);
            CHECK(!rig.svc->submit_bench(rig.ctx(), p, why) && why.find(std::string("kill switch turbo_output\\") + f) == 0 && rig.jobs.q.empty(),
                  std::string(f) + ": " + why);
            rig.remove(f);
        }
        Context nohub = rig.ctx();
        nohub.hub = false;
        nohub.bench_hub = false;
        CHECK(!rig.svc->submit_bench(nohub, p, why) && why.find("not in the career hub (G2 for bench keep") == 0, "not in the hub: " + why);
        rig.svc->note_context(rig.ctx());
        BenchPlan p4 = p;
        p4.load_gen = 4;
        CHECK(!rig.svc->submit_bench(rig.ctx(), p4, why) && why.find("career load 4") != std::string::npos, "another career load: " + why);
        CHECK(copy_ids(w) == p.before, "nothing written so far");
        // queued; a tactic Apply waits for it; bench_keep_off.txt dropped after queuing: refused in the job
        CHECK(rig.svc->submit_bench(rig.ctx(), p, why) && rig.jobs.q.size() == 1 && rig.svc->busy(), "queued: " + why);
        CHECK(!rig.svc->submit_bench(rig.ctx(), p, why2) && why2 == "a channel E job is still queued or running", "one job at a time: " + why2);
        Plan tp;
        Request rq;
        rq.line_height = 70;
        rq.approach = 2;
        CHECK(make_plan(lt, rq, tp, nullptr).empty(), "a tactic plan");
        CHECK(!rig.svc->submit(rig.ctx(), tp, why2) && why2.find("still queued") != std::string::npos, "the tactic Apply waits: " + why2);
        rig.touch(bench::keep_off_file());
        rig.jobs.pump();
        BenchResult br;
        ApplyResult ar;
        CHECK(!rig.svc->poll(ar), "the tactic card's queue never sees a bench result");
        CHECK(rig.svc->poll_bench(br) && !br.ok && br.stage == "G1" && br.message.find("bench_keep_off.txt is present (seen in the job): nothing written") != std::string::npos,
              "kill switch in the job: " + br.message);
        CHECK(!rig.svc->busy() && copy_ids(w) == p.before, "nothing written, not busy");
        rig.remove(bench::keep_off_file());
        // G2 for bench keep in the job: a save started (or the hub was left) between queuing and the game thread
        CHECK(rig.svc->submit_bench(rig.ctx(), p, why), "queued: " + why);
        rig.svc->note_context(nohub);
        rig.jobs.pump();
        CHECK(rig.svc->poll_bench(br) && !br.ok && br.stage == "G2" && br.message.find("not in the career hub any more") == 0 && copy_ids(w) == p.before,
              "G2 in the job: " + br.message);
        CHECK(rig.svc->submit_bench(rig.ctx(), p, why), "queued: " + why);
        rig.svc->note_context(rig.ctx(4));
        rig.jobs.pump();
        CHECK(rig.svc->poll_bench(br) && !br.ok && br.stage == "G2" && br.message.find("reloaded") != std::string::npos && copy_ids(w) == p.before,
              "a reload in between: " + br.message);
        rig.svc->note_context(rig.ctx());
        CHECK(rig.svc->submit_bench(rig.ctx(), p, why), "queued: " + why);
        rig.clock += 2.5e6;
        rig.jobs.pump();
        CHECK(rig.svc->poll_bench(br) && !br.ok && br.stage == "late" && copy_ids(w) == p.before, "late: " + br.message);
        CHECK(rig.svc->submit_bench(rig.ctx(), p, why), "queued: " + why);
        w.mem.wr(SheetWorld::kPsm + 0x10, int32_t(96));
        rig.jobs.pump();
        CHECK(rig.svc->poll_bench(br) && !br.ok && br.stage == "user" && copy_ids(w) == p.before, "the game's club: " + br.message);
        w.mem.wr(SheetWorld::kPsm + 0x10, int32_t(w.team));
        // Team Management open (channel E's G2 fails: hub false), G2 for bench keep holds: written (the copy exists only while it is open)
        Context tm_open = rig.ctx();
        tm_open.hub = false;
        const int finds = rig.find_calls;
        CHECK(rig.svc->submit_bench(tm_open, p, why), "queued while Team Management is open: " + why);
        rig.jobs.pump();
        CHECK(rig.svc->poll_bench(br) && br.ok && br.written && br.moved == 4 && copy_ids(w) == p.saved && ids_of(w) == p.saved, "written: " + br.message);
        CHECK(rig.find_calls == finds + 1, "the registry Find re-checked in the job (opt-in context)");
        CHECK(rig.logged("bench keep: team 95 sheet -1: your saved order written into Team Management's working copy svc+0x550, places 11..51 (4 places; the "
                         "starting XI untouched) (read back ok) [job"),
              "logged with the job's time");
        CHECK(!rig.svc->poll(ar) && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && rig.svc->bench_off().empty() && rig.svc->session_off().empty(),
              "the tactic side untouched");
        // a bench read-back mismatch: bench keep off for the session; channel E's tactic writes stay on
        SheetWorld w2;
        BenchPlan q;
        tm_open_world(w2, q);
        FlakyMemory fm(w2.mem);
        fm.drop = copy_id_at(29);
        const fs::path out2 = test_tmp_dir("bench_keep_service2");
        fs::remove_all(out2, ec);
        ServiceRig rig2(w2, out2, &fm);
        rig2.find_returns = SheetWorld::kSvc;
        rig2.jobs.inline_run = true;
        rig2.touch(bench::keep_on_file());
        rig2.touch(opt_in_file());
        CHECK(rig2.svc->read(rig2.ctx(), lt, why), "rig 2: " + why);
        CHECK(rig2.svc->submit_bench(rig2.ctx(), q, why), "queued: " + why);
        CHECK(rig2.svc->poll_bench(br) && br.undone && br.session_off && copy_ids(w2) == q.before, "mismatch: " + br.message);
        CHECK(!rig2.svc->bench_off().empty() && !rig2.svc->submit_bench(rig2.ctx(), q, why) && why.find("bench keep is off for this session") == 0,
              "no further bench write: " + why);
        CHECK(rig2.svc->session_off().empty(), "channel E's tactic writes are not switched off by bench keep");
        // channel E off for the session (a tactic read-back mismatch) stops bench keep too
        SheetWorld w3;
        BenchPlan q3;
        tm_open_world(w3, q3);
        FlakyMemory fm3(w3.mem);
        fm3.drop = SheetWorld::kRecs + kRecLineHeight;
        const fs::path out3 = test_tmp_dir("bench_keep_service3");
        fs::remove_all(out3, ec);
        ServiceRig rig3(w3, out3, &fm3);
        rig3.find_returns = SheetWorld::kSvc;
        rig3.jobs.inline_run = true;
        rig3.touch(bench::keep_on_file());
        rig3.touch(opt_in_file());
        CHECK(rig3.svc->read(rig3.ctx(), lt, why), "rig 3: " + why);
        Plan tp3;
        CHECK(make_plan(lt, rq, tp3, nullptr).empty() && rig3.svc->submit(rig3.ctx(), tp3, why) && rig3.svc->poll(ar) && ar.undone, "a tactic mismatch");
        CHECK(!rig3.svc->submit_bench(rig3.ctx(), q3, why) && why.find("channel E is off for this session") == 0 && copy_ids(w3) == q3.before,
              "bench keep refused while channel E is off: " + why);
        Host h;
        h.mem = &w.mem;
        h.anchors = w.anchors();
        CoreService none(h);
        CHECK(!none.submit_bench(rig.ctx(), p, why) && why.find("dispatcher") != std::string::npos, "no dispatcher: " + why);
    });
}
// --------------------------------------------------------------------------------------------------------------------------- UI
// The Tactics tab's right column is short in the test window: scroll it so the item is at its top (a widget out of view has no label for
// the driver), or back to the top
static void lt_scroll_to(Ui& ui, const std::string& label) {
    const ItemRec* r = ui.find(label, "##tacright");
    if (!r) return;
    for (ImGuiWindow* w : GImGui->Windows)
        if (r->window == w->Name) ImGui::SetScrollY(w, w->Scroll.y + (r->rect.Min.y - w->InnerClipRect.Min.y) - 2.0f);
    ui.frames(2);
}
static void lt_scroll_top(Ui& ui) {
    for (ImGuiWindow* w : GImGui->Windows) {
        const std::string n = w->Name;
        if (n.size() >= 9 && n.find("##tacright") != std::string::npos && n.find("##tacsliders") == std::string::npos) ImGui::SetScrollY(w, 0.0f);
    }
    ui.frames(2);
}

// Teams > Tactics > My team: the summary line, the card, Apply (asks first), the cm_mentalities row in the job, the read-back, Undo, the gates
static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    live_tactic_set_details_open(true);  // 2.5: the card's "More" node (role / focus per slot, the live record) is collapsed in the app
    run_case("UI: Teams > Tactics > My team: channel E - the live read-out, opt-in, Apply writes in place + mirror, overwrite seen, Undo, gates", [&] {
        live_tactic_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {opt_in_file(), kill_switch_file(), master_switch_file(), undo_file(), db_row_off_file(), db_row_off_legacy_file()}) fs::remove(out / f, ec);
        // the simulated game: your club (Arsenal, 1) with the tactic of cm_mentalities row artificialkey 2 (active, sourceformationid 10)
        SheetWorld w(1, 10);
        ServiceRig rig(w, out);
        rig.find_returns = SheetWorld::kSvc;
        rig.jobs.inline_run = true;
        app.team_sheet = rig.svc;
        // Lua's state: in the hub, the career's manager table
        const fs::path state_file = out / "bridge_state.json";
        json st = read_json(state_file);
        const json saved = st;
        int bumps = 500;
        auto write_state = [&]() {
            {
                std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                f << st.dump();
            }
            fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
            app.next_poll = 0.0;
            ui.frames(3);
        };
        st["match_phase"] = "hub";
        st["managers"] = hex_addr(SheetWorld::kManagers);
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        CHECK(app.bridge.state().match_phase == "hub" && app.bridge.state().managers == SheetWorld::kManagers, "the bridge reports the hub");
        tactics_tab_test::select_team(app, ui, 7);  // any club: the card is about yours
        tactics_tab_test::go_scope(ui, "My team");
        ui.frames(3);
        const LiveTacticState& s = live_tactic_state();
        // no opt-in yet: the read-only scan, whose pick is used 5 s later (after its header checks pass again)
        CHECK(s.summary.find("again") != std::string::npos, "the scan's pick is checked again first: " + s.summary);
        rig.clock += 6e6;
        ui.frames(70);
        CHECK(s.summary_shown && s.summary.find("\"433\", Defensive Approach Deep (0), Line Height 25, Build-Up Style Balanced (0)") != std::string::npos,
              "the summary line: " + s.summary);
        CHECK(rig.logged("channel E: role allow-list: 10 triples from 1 teams"), "the role allow-list is logged");
        // the card
        if (!s.card_open) ui.click("Edit the live tactic (channel E)##ltcard", "##tacright");
        ui.frames(2);
        CHECK(s.card_open, "card open");
        CHECK(s.written == "live team sheet" && s.seen == "untested" && s.tag.find("live team sheet, untested") == 0, "badges: " + s.tag);
        CHECK(s.read_ok &&
                  s.readout.find("code ?ZaE&CYzKoT7: formation 27 (source 10), Defensive Approach Deep (0), Line Height 25 band 0 (1-30), Build-Up Style "
                                 "Balanced (0), preset 1") != std::string::npos,
              s.readout);
        CHECK(s.safety == "If you open Edit Tactic and answer Yes to 'save your changes to your team sheet', the game commits its screen values and AUTOSAVES "
                          "your career.",
              "the safety line: " + s.safety);
        CHECK(s.slots.size() == 11 && s.slots[9].find("role 38213 = group 9 / role 21 / focus 5") != std::string::npos, "slot 9: " + (s.slots.size() > 9 ? s.slots[9] : ""));
        CHECK(s.not_written.find("preset 1") != std::string::npos && s.not_written.find("formation 27") != std::string::npos, s.not_written);
        CHECK(!s.can_apply && s.status.find("G1") == 0, "off without the opt-in: " + s.status);
        // the opt-in checkbox writes the file
        CHECK(ui.click("Channel E on (turbo_output\\call_team_sheet_on.txt)##ltopt", "##tacright"), "opt-in clicked");
        ui.frames(2);
        CHECK(fs::exists(out / opt_in_file()) && s.opt_in, "opt-in file created");
        CHECK(s.status.find("G6 values: nothing changed") == 0, "now only the values hold it back: " + s.status);
        // the 2.4 acceptance change: Deep -> High (its default 70 comes in), Line Height 90 (High's band), Counter, the striker's focus 5 -> 6
        CHECK(s.approach_choices == std::vector<int>({0, 1, 2}) && s.build_up_choices == std::vector<int>({0, 1}),
              "only the writable values are offered (no Aggressive)");
        CHECK(!ui.find("Aggressive##ltap3", "##tacright"), "no Aggressive button");
        lt_scroll_to(ui, "Deep##ltap0");  // the editor at the top of the column
        CHECK(ui.click("High##ltap2", "##tacright"), "High picked");
        ui.frames(2);
        CHECK(s.line_hint.find("band 2 (61-90): High's band") == 0, "its default Line Height 70 came in: " + s.line_hint);
        // a line in Aggressive's band: the approach follows the line and Apply refuses it (Aggressive is not written)
        CHECK(ui.type_into(ui.find("##ltlhbox", "##tacright"), "95"), "line height 95 typed");
        ui.frames(2);
        CHECK(s.plan_error.find("Line Height 95 lies in Aggressive's band") == 0 && !s.can_apply, "95 refused: " + s.plan_error);
        CHECK(ui.type_into(ui.find("##ltlhbox", "##tacright"), "90"), "line height typed");
        ui.frames(2);
        CHECK(s.line_hint.find("band 2 (61-90): High's band") == 0, "90 is High's band, the approach followed: " + s.line_hint);
        CHECK(ui.click("Counter##ltbu1", "##tacright"), "Counter picked");
        CHECK(ui.type_into(ui.find("##ltslot", "##tacright"), "9"), "slot 9");
        ui.frames(2);
        // only what the game itself uses is offered: role ids 21 / 22 of group 9, focuses 5 (the live one), 3 and 6 with role 21 - not 4
        auto has_int = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
        CHECK(has_int(s.role_choices, 21) && has_int(s.role_choices, 22) && s.role_choices.size() == 2, "role ids offered");
        CHECK(has_int(s.focus_choices, 5) && has_int(s.focus_choices, 3) && has_int(s.focus_choices, 6) && !has_int(s.focus_choices, 4) &&
                  s.focus_choices.size() == 3,
              "focuses offered");
        CHECK(ui.click("6##ltfoc6", "##tacright"), "focus 6 picked");
        // the cm_mentalities row is part of the write (2.4: no opt-in); here the row's depth is not the live one: the row is left out
        ui.frames(130);
        CHECK(s.can_apply && s.status == "ready", "ready: " + s.status);
        CHECK(has_part(s.plan, "Defensive Approach Deep (0) -> High (2)") && has_part(s.plan, "Line Height 25 -> 90 (band 2 (61-90))") &&
                  has_part(s.plan, "Build-Up Style Balanced (0) -> Counter (1)") &&
                  has_part(s.plan, "slot 9 (ST) role word 38213 -> 38214 = group 9 / role 21 / focus 6") && !has_part(s.plan, "working cop"),
              "Apply will write the four changes into the map record (no Edit Tactic copy)");
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 25, "nothing written yet");
        lt_scroll_top(ui);
        CHECK(ui.click("Apply to the live tactic##ltapply", "##tacright"), "Apply clicked");
        ui.frames(2);
        CHECK(s.confirm_open && has_part(s.confirm_lines, "38213 -> 38214") && has_part(s.confirm_lines, "High (2)"), "Apply asks first");
        CHECK(ui.click("Apply##ltyes", "Apply to the live tactic?"), "confirmed");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecBuildUp) == 1 && w.i32(SheetWorld::kRecs + kRecLineHeight) == 90 && w.i32(SheetWorld::kRecs + kRecApproach) == 2 &&
                  w.i32(w.slot(9) + kSlotRole) == 38214 && w.i32(w.slot(9) + kSlotFocus) == 6 && w.u8(SheetWorld::kSheet + kSheetDirty) == 1,
              "written into the live record");
        for (uint64_t c : {uint64_t(0xF80), uint64_t(0x1170)})
            CHECK(w.i32(w.copy_rec(c) + kRecApproach) == 0 && w.i32(w.copy_rec(c) + kRecLineHeight) == 25 && w.i32(w.copy_rec(c) + kRecBuildUp) == 0 &&
                      w.i32(w.copy_slot(c, 9) + kSlotRole) == 38213,
                  "Edit Tactic's working copies are not written (2.4)");
        CHECK(s.last.find("team 1 record 0 approach 0->2 line height 25->90 build-up 0->1 slot 9 role 38213->38214 (read back ok)") != std::string::npos,
              "last: " + s.last);
        UndoRecord u;
        std::string err;
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "written" && u.team == 1 && u.writes.size() == 6 && u.code == SheetWorld::kCode && u.copies.empty(),
              fmt("undo file: written, the map record's 6 fields (%zu) ", u.writes.size()) + err);
        // the row: cm_mentalities artificialkey 2 holds the live slots but depth 55 / build-up 2, not the live 25 / 0 (it is not the live tactic):
        // the live record is written alone, the log line and the card say why
        const Table* cm = app.db.table("cm_mentalities");
        const uint64_t row = cm ? app.db.find(*cm, "artificialkey", 2) : 0;
        CHECK(row && app.db.get_int(*cm, row, "defensivedepth") == 55 && app.db.get_int(*cm, row, "buildupplay") == 2, "the row is not touched");
        CHECK(u.mirror_state == "skipped" && u.mirror.empty() &&
                  s.mirror.find("cm_mentalities not written: artificialkey 2: defensivedepth is 55 in the database, the live value before the write was 25: "
                                "the row is not the live tactic") == 0,
              "row left out: " + s.mirror);
        CHECK(rig.logged("(read back ok) [cm_mentalities not written: artificialkey 2: defensivedepth is 55 in the database"), "the log line says so");
        // the game overwrites the line height: the 5-second re-read and the card say so
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(25));
        ui.frames(70);
        CHECK(has_part(s.overwritten, "Line Height 90 -> 25"), "overwrite seen");
        CHECK(rig.logged("channel E") , "the service logged");
        // the same record seen after a career reload: an earlier load's write (no in-game save after it?), not an Edit Tactic overwrite
        {
            UndoRecord now_u;
            CHECK(load_undo(out / undo_file(), now_u, &err), "undo file: " + err);
            UndoRecord older = now_u;
            older.load_gen = now_u.load_gen - 1;
            save_undo(out / undo_file(), older, &err);
            live_tactic_reset();
            ui.frames(3);
            CHECK(has_part(s.overwritten, "earlier career load") && !has_part(s.overwritten, "Edit Tactic"), "an earlier load's write");
            save_undo(out / undo_file(), now_u, &err);
            live_tactic_reset();
            ui.frames(3);
            CHECK(has_part(s.overwritten, "Edit Tactic (or the game) overwrote the change: Line Height 90 -> 25"), "this load's write again");
        }
        // a match being set up: no write, no undo
        st["match_phase"] = "prematch";
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        ui.frames(2);
        CHECK(!s.can_apply && !s.can_undo && s.status.find("G2 career hub") == 0, "pre-match: refused: " + s.status);
        // Team Management open (Lua: ENTERING_TEAM_MANAGEMENT): refused too
        st["match_phase"] = "hub";
        st["team_mgmt"] = true;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        ui.frames(2);
        CHECK(app.bridge.state().team_mgmt && !s.can_apply && !s.can_undo && s.status.find("G2 career hub: Team Management is open") == 0,
              "Team Management open: refused: " + s.status);
        st["team_mgmt"] = false;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        // the undo record's row is another club's (artificialkey 3, club 7): it must never be put back there
        {
            UndoRecord fake;
            CHECK(load_undo(out / undo_file(), fake, &err), "undo file: " + err);
            fake.mirror_state = "written";
            fake.mirror_table = "cm_mentalities";
            fake.mirror_row_key = 3;
            fake.mirror = {{"defensivedepth", 45, 75}, {"buildupplay", 0, 2}};
            save_undo(out / undo_file(), fake, &err);
            live_tactic_reset();
            ui.frames(3);
        }
        // the kill switch: Apply off, Undo still possible (it puts the game's values back)
        std::ofstream(out / kill_switch_file()) << "x";
        ui.frames(130);
        CHECK(!s.can_apply && s.status.find("call_team_sheet_off.txt") != std::string::npos && s.can_undo, "kill switch: " + s.status);
        // Undo: the fields that still hold Turbo's values go back; the overwritten depth is left as it is
        CHECK(ui.click("Undo Turbo's write##ltundo", "##tacright"), "Undo clicked");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecBuildUp) == 0 && w.i32(w.slot(9) + kSlotRole) == 38213 && w.i32(w.slot(9) + kSlotFocus) == 5 &&
                  w.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && w.i32(SheetWorld::kRecs + kRecApproach) == 0,
              "put back");
        for (uint64_t c : {uint64_t(0xF80), uint64_t(0x1170)})
            CHECK(w.i32(w.copy_rec(c) + kRecApproach) == 0 && w.i32(w.copy_rec(c) + kRecLineHeight) == 25 && w.i32(w.copy_rec(c) + kRecBuildUp) == 0 &&
                      w.i32(w.copy_slot(c, 9) + kSlotRole) == 38213 && w.i32(w.copy_slot(c, 9) + kSlotFocus) == 5,
                  "Edit Tactic's working copies untouched");
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "restored" && u.note.find("Line Height is 25") != std::string::npos, "undo file: " + u.state + " " + u.note);
        // the row's undo: refused (artificialkey 3 is club 7's row), in the same job; no row touched
        const uint64_t row3 = cm ? app.db.find(*cm, "artificialkey", 3) : 0;
        CHECK(app.db.get_int(*cm, row, "defensivedepth") == 55 && row3 && app.db.get_int(*cm, row3, "defensivedepth") == 45 && u.mirror_state == "written" &&
                  s.mirror.find("cm_mentalities not put back: row 3 is not club 1's active tactic") == 0,
              "the row's undo refused: " + s.mirror);
        CHECK(!s.can_undo, "nothing left to undo");
        // clean up
        app.team_sheet = nullptr;
        for (const char* f : {opt_in_file(), kill_switch_file(), undo_file()}) fs::remove(out / f, ec);
        st = saved;
        st["seq"] = saved.value("seq", 0LL) + 50;
        write_state();
        live_tactic_reset();
    });

    // 2.4: after Team Management the game replaced Turbo's tactic: the card asks once, one click re-applies, nothing happens by itself
    run_case("UI: channel E - after Team Management the game replaced the tactic: the prompt, one-click Re-apply, never automatic", [&] {
        live_tactic_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {opt_in_file(), kill_switch_file(), master_switch_file(), undo_file(), db_row_off_file()}) fs::remove(out / f, ec);
        SheetWorld w(1, 10);
        ServiceRig rig(w, out);
        rig.find_returns = SheetWorld::kSvc;
        rig.jobs.inline_run = true;
        app.team_sheet = rig.svc;
        double wall = 1.0e9;  // the watch runs on wall time (not Turbo's QPC clock, ~3x in game): the test drives it
        live_tactic_test_wall_clock([&wall]() { return wall; });
        std::vector<std::string> hooked;  // the card's own log lines (log_line: App::log + log_hook)
        app.log_hook = [&hooked](const std::string& x) { hooked.push_back(x); };
        auto app_logged = [&hooked](const std::string& part) { return has_part(hooked, part); };
        const fs::path state_file = out / "bridge_state.json";
        json st = read_json(state_file);
        const json saved = st;
        int bumps = 700;
        auto write_state = [&]() {
            {
                std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                f << st.dump();
            }
            fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
            app.next_poll = 0.0;
            ui.frames(3);
        };
        st["match_phase"] = "hub";
        st["team_mgmt"] = false;
        st["managers"] = hex_addr(SheetWorld::kManagers);
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        tactics_tab_test::select_team(app, ui, 7);
        tactics_tab_test::go_scope(ui, "My team");
        ui.frames(3);
        const LiveTacticState& s = live_tactic_state();
        rig.clock += 6e6;
        ui.frames(70);
        CHECK(s.read_ok, "read: " + s.summary);
        if (!s.card_open) ui.click("Edit the live tactic (channel E)##ltcard", "##tacright");
        ui.frames(2);
        std::ofstream(out / opt_in_file()) << "x";
        lt_scroll_to(ui, "Deep##ltap0");
        CHECK(ui.click("High##ltap2", "##tacright"), "High picked (its default Line Height 70)");
        CHECK(ui.click("Counter##ltbu1", "##tacright"), "Counter picked");
        ui.frames(130);
        CHECK(s.can_apply && s.status == "ready", "ready: " + s.status);
        lt_scroll_top(ui);
        CHECK(ui.click("Apply to the live tactic##ltapply", "##tacright"), "Apply clicked");
        ui.frames(2);
        CHECK(ui.click("Apply##ltyes", "Apply to the live tactic?"), "confirmed");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 70 && w.i32(SheetWorld::kRecs + kRecApproach) == 2 && w.i32(SheetWorld::kRecs + kRecBuildUp) == 1,
              "written: " + s.last);
        CHECK(s.replaced.empty() && !s.can_reapply, "no prompt after Turbo's own write");
        // Team Management open; on leaving the user answers Yes: the game copies Edit Tactic's screen into the record; closed; the re-read
        auto tm_yes = [&](int32_t lh, int32_t ap, int32_t bu) {
            st["team_mgmt"] = true;
            st["seq"] = st.value("seq", 0LL) + 1;
            write_state();
            w.mem.wr(SheetWorld::kRecs + kRecLineHeight, lh);
            w.mem.wr(SheetWorld::kRecs + kRecApproach, ap);
            w.mem.wr(SheetWorld::kRecs + kRecBuildUp, bu);
            st["team_mgmt"] = false;
            st["seq"] = st.value("seq", 0LL) + 1;
            write_state();
            ui.frames(3);
        };
        // Team Management opens; the user answers Yes on leaving: the game copies Edit Tactic's screen (Balanced / 50 / Balanced) into the record
        st["team_mgmt"] = true;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(50));
        w.mem.wr(SheetWorld::kRecs + kRecApproach, int32_t(1));
        w.mem.wr(SheetWorld::kRecs + kRecBuildUp, int32_t(0));
        st["team_mgmt"] = false;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        CHECK(app_logged("channel E: Team Management closed: watching your club's live record in the hub"), "the close is seen");
        ui.frames(3);
        CHECK(s.replaced == "the game replaced your tactic (LH 50 / Balanced / Balanced). Re-apply?" && s.can_reapply, "the prompt: " + s.replaced);
        CHECK(app_logged("channel E: after Team Management: the game replaced your tactic (LH 50 / Balanced / Balanced). Re-apply? (waiting for the user's click)"),
              "logged once");
        // never automatic: time passes, the record keeps the game's values
        ui.t += 30.0;
        ui.frames(130);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 50 && w.i32(SheetWorld::kRecs + kRecApproach) == 1 && !s.replaced.empty(), "nothing re-applied by itself");
        // one click
        lt_scroll_top(ui);
        CHECK(ui.click("Re-apply##ltreapply", "##tacright"), "Re-apply clicked");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 70 && w.i32(SheetWorld::kRecs + kRecApproach) == 2 && w.i32(SheetWorld::kRecs + kRecBuildUp) == 1,
              "re-applied: " + s.last);
        CHECK(s.replaced.empty() && app_logged("channel E: re-apply of Turbo's tactic queued (the user's click after the game replaced it)") &&
                  rig.logged("channel E: team 1 record 0 approach 1->2 line height 50->70 build-up 0->1"),
              "the prompt is gone, the write logged: " + s.last);
        UndoRecord u;
        std::string err;
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "written" && u.note == "re-apply after the game replaced the tactic",
              "undo file: written, the re-apply named: " + u.state + " " + u.note + " " + err);
        // Team Management again, No this time (nothing changes): no prompt
        st["team_mgmt"] = true;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        st["team_mgmt"] = false;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        ui.frames(3);
        wall += 5.0;
        ui.frames(3);
        CHECK(s.replaced.empty() && !app_logged("still in place"), "No: nothing to ask, nothing claimed");
        // Undo puts back what the game had put there before the re-apply (Balanced / 50 / Balanced)
        ui.frames(130);
        lt_scroll_top(ui);
        CHECK(s.can_undo, "Undo offered: " + s.status);
        CHECK(ui.click("Undo Turbo's write##ltundo", "##tacright"), "Undo clicked");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 50 && w.i32(SheetWorld::kRecs + kRecApproach) == 1 && w.i32(SheetWorld::kRecs + kRecBuildUp) == 0,
              "undone to the game's values: " + s.last);
        // review M2: a prompt of one career load never survives a reload: Apply again, the game replaces it, the career is reloaded -> gone
        lt_scroll_to(ui, "Deep##ltap0");
        CHECK(ui.click("High##ltap2", "##tacright"), "High picked again");
        CHECK(ui.click("Counter##ltbu1", "##tacright"), "Counter picked again");
        ui.frames(130);
        lt_scroll_top(ui);
        CHECK(ui.click("Apply to the live tactic##ltapply", "##tacright"), "Apply clicked");
        ui.frames(2);
        CHECK(ui.click("Apply##ltyes", "Apply to the live tactic?"), "confirmed");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 70, "written again: " + s.last);
        // review M3: the game's copy lands late (8 s of wall time after Lua's flag cleared, long after any timed re-read): still prompted
        tm_yes(70, 2, 1);  // closed with Turbo's values still there
        CHECK(s.replaced.empty(), "nothing replaced yet");
        wall += 8.0;
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(50));
        w.mem.wr(SheetWorld::kRecs + kRecApproach, int32_t(1));
        w.mem.wr(SheetWorld::kRecs + kRecBuildUp, int32_t(0));
        ui.frames(3);
        CHECK(!s.replaced.empty() && s.can_reapply, "the late copy is seen: " + s.replaced);
        st["load_gen"] = st.value("load_gen", 0LL) + 1;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        ui.frames(3);
        CHECK(s.replaced.empty() && !s.can_reapply && !ui.find("Re-apply##ltreapply", "##tacright") &&
                  app_logged("channel E: the Re-apply prompt was dropped: the career was (re)loaded"),
              "the reload dropped the prompt: " + s.replaced);
        ui.t += 30.0;
        ui.frames(130);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 50 && w.i32(SheetWorld::kRecs + kRecApproach) == 1, "nothing written into the reloaded career");
        // clean up
        live_tactic_test_wall_clock(nullptr);
        app.log_hook = nullptr;
        app.team_sheet = nullptr;
        for (const char* f : {opt_in_file(), undo_file()}) fs::remove(out / f, ec);
        st = saved;
        st["seq"] = saved.value("seq", 0LL) + 70;
        write_state();
        live_tactic_reset();
    });

    // review M4.3 / M4.5: the real row code of the card (mirror_row, the teamsheetid filter, add_row_checks with the tactic code) on its success
    // path, the db_mirror of the undo file, Undo putting the row back; and Team Management / a save starting between the click and the job
    run_case("UI: channel E - the cm_mentalities row written and put back by the card's own code; Team Management or a save between click and job", [&] {
        live_tactic_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {opt_in_file(), kill_switch_file(), master_switch_file(), undo_file(), db_row_off_file(), db_row_off_legacy_file()}) fs::remove(out / f, ec);
        SheetWorld w(1, 10);
        // the live record holds what Arsenal's active cm_mentalities row (artificialkey 2) holds: Balanced / 55 / build-up 2 (the game's value)
        w.mem.wr(SheetWorld::kRecs + kRecLineHeight, int32_t(55));
        w.mem.wr(SheetWorld::kRecs + kRecApproach, int32_t(1));
        w.mem.wr(SheetWorld::kRecs + kRecBuildUp, int32_t(2));
        SplitMemory split(w.mem, app.db.memory());  // the job writes the card's database row through the game's one address space
        ServiceRig rig(w, out, &split);
        rig.find_returns = SheetWorld::kSvc;
        rig.jobs.inline_run = true;
        app.team_sheet = rig.svc;
        const fs::path state_file = out / "bridge_state.json";
        json st = read_json(state_file);
        const json saved = st;
        int bumps = 800;
        auto write_state = [&]() {
            {
                std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                f << st.dump();
            }
            fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
            app.next_poll = 0.0;
            ui.frames(3);
        };
        st["match_phase"] = "hub";
        st["team_mgmt"] = false;
        st["managers"] = hex_addr(SheetWorld::kManagers);
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        tactics_tab_test::select_team(app, ui, 7);
        tactics_tab_test::go_scope(ui, "My team");
        ui.frames(3);
        const LiveTacticState& s = live_tactic_state();
        rig.clock += 6e6;
        ui.frames(70);
        CHECK(s.read_ok && s.summary.find("Defensive Approach Balanced (1), Line Height 55, Build-Up Style unknown value 2") != std::string::npos, "read: " + s.summary);
        const Table* cm = app.db.table("cm_mentalities");
        const uint64_t row = cm ? app.db.find(*cm, "artificialkey", 2) : 0;
        CHECK(row && app.db.get_int(*cm, row, "defensivedepth") == 55 && app.db.get_int(*cm, row, "playerid9") == 100010 && app.db.get_int(*cm, row, "pos9role") == 38213,
              "the fixture's row is the live tactic's");
        if (!s.card_open) ui.click("Edit the live tactic (channel E)##ltcard", "##tacright");
        ui.frames(2);
        std::ofstream(out / opt_in_file()) << "x";
        lt_scroll_to(ui, "Deep##ltap0");
        CHECK(ui.click("High##ltap2", "##tacright"), "High picked (its default Line Height 70; the game's build-up 2 kept)");
        ui.frames(130);
        CHECK(s.can_apply && s.status == "ready", "ready: " + s.status);
        lt_scroll_top(ui);
        CHECK(ui.click("Apply to the live tactic##ltapply", "##tacright"), "Apply clicked");
        ui.frames(2);
        CHECK(ui.click("Apply##ltyes", "Apply to the live tactic?"), "confirmed");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 70 && w.i32(SheetWorld::kRecs + kRecApproach) == 2 && w.i32(SheetWorld::kRecs + kRecBuildUp) == 2,
              "the live record written: " + s.last);
        CHECK(app.db.get_int(*cm, row, "defensivedepth") == 70 && app.db.get_int(*cm, row, "buildupplay") == 2 && app.db.get_int(*cm, row, "teamid") == 1,
              "the row written in the same job (the card's own mirror_row / add_row_checks)");
        CHECK(s.last.find("approach 1->2 line height 55->70; cm_mentalities artificialkey 2 defensivedepth 55->70 (read back ok) [record now LH 70 BU 2 DA 2]") !=
                  std::string::npos,
              "last: " + s.last);
        CHECK(s.mirror == "cm_mentalities artificialkey 2: defensivedepth 55 -> 70 (in the same job, read back)", "the card: " + s.mirror);
        UndoRecord u;
        std::string err;
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "written" && u.mirror_state == "written" && u.mirror_table == "cm_mentalities" &&
                  u.mirror_row_key == 2 && u.mirror.size() == 1 && u.mirror[0].field == "defensivedepth" && u.mirror[0].old_value == 55 && u.mirror[0].new_value == 70,
              "the undo file's db_mirror: " + u.mirror_state + " " + err);
        // Undo: the live record and the row back, in one job
        ui.frames(130);
        lt_scroll_top(ui);
        CHECK(s.can_undo, "Undo offered: " + s.status);
        CHECK(ui.click("Undo Turbo's write##ltundo", "##tacright"), "Undo clicked");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 55 && w.i32(SheetWorld::kRecs + kRecApproach) == 1 && app.db.get_int(*cm, row, "defensivedepth") == 55,
              "undone, row too: " + s.last);
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "restored" && u.mirror_state == "restored", "undo file: " + u.state + " / " + u.mirror_state);
        CHECK(s.mirror == "cm_mentalities artificialkey 2: defensivedepth 70 -> 55 (in the same job, read back)", "the card: " + s.mirror);
        // M4.5: Team Management opens between the click and the game thread's job: refused in the job (the UI's hub view), nothing written
        rig.jobs.inline_run = false;
        ui.frames(130);
        lt_scroll_to(ui, "Deep##ltap0");
        CHECK(ui.click("High##ltap2", "##tacright"), "High again");
        ui.frames(130);
        lt_scroll_top(ui);
        CHECK(ui.click("Apply to the live tactic##ltapply", "##tacright"), "Apply clicked");
        ui.frames(2);
        CHECK(ui.click("Apply##ltyes", "Apply to the live tactic?"), "confirmed");
        ui.frames(2);
        CHECK(rig.jobs.q.size() == 1, "the job waits for the game thread");
        st["team_mgmt"] = true;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        rig.jobs.pump();
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 55 && app.db.get_int(*cm, row, "defensivedepth") == 55 && s.last.find("refused (G2)") != std::string::npos,
              "Team Management opened: refused in the job: " + s.last);
        st["team_mgmt"] = false;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        // a save starts between the click and the job: refused too
        ui.frames(130);
        lt_scroll_top(ui);
        CHECK(ui.click("Apply to the live tactic##ltapply", "##tacright"), "Apply clicked");
        ui.frames(2);
        CHECK(ui.click("Apply##ltyes", "Apply to the live tactic?"), "confirmed");
        ui.frames(2);
        CHECK(rig.jobs.q.size() == 1, "queued");
        st["save_t"] = (long long)(std::time(nullptr));
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        rig.jobs.pump();
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecLineHeight) == 55 && app.db.get_int(*cm, row, "defensivedepth") == 55 && s.last.find("refused (G2)") != std::string::npos,
              "a save started: refused in the job: " + s.last);
        // clean up (the fixture's row is back at 55)
        app.team_sheet = nullptr;
        for (const char* f : {opt_in_file(), undo_file()}) fs::remove(out / f, ec);
        st = saved;
        st["seq"] = saved.value("seq", 0LL) + 80;
        write_state();
        live_tactic_reset();
    });

    // review: a job whose putting back failed may leave values in Edit Tactic's working copies; the undo file must name them (it named the
    // map record's fields only), so Undo can put them back
    run_case("UI: channel E - putting back failed: the undo file names every field the job wrote; Undo puts back what still holds Turbo's value", [&] {
        live_tactic_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {opt_in_file(), kill_switch_file(), master_switch_file(), undo_file(), db_row_off_file(), db_row_off_legacy_file()}) fs::remove(out / f, ec);
        SheetWorld w(1, 10);
        StickyMemory sm(w.mem);
        ServiceRig rig(w, out, &sm);
        rig.find_returns = SheetWorld::kSvc;
        rig.jobs.inline_run = true;
        app.team_sheet = rig.svc;
        const fs::path state_file = out / "bridge_state.json";
        json st = read_json(state_file);
        const json saved = st;
        int bumps = 600;
        auto write_state = [&]() {
            {
                std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                f << st.dump();
            }
            fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
            app.next_poll = 0.0;
            ui.frames(3);
        };
        st["match_phase"] = "hub";
        st["team_mgmt"] = false;
        st["managers"] = hex_addr(SheetWorld::kManagers);
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        tactics_tab_test::select_team(app, ui, 7);
        tactics_tab_test::go_scope(ui, "My team");
        ui.frames(3);
        const LiveTacticState& s = live_tactic_state();
        rig.clock += 6e6;
        ui.frames(70);
        CHECK(s.read_ok && s.summary.find("Defensive Approach Deep (0), Line Height 25") != std::string::npos, "read: " + s.summary);
        if (!s.card_open) ui.click("Edit the live tactic (channel E)##ltcard", "##tacright");
        ui.frames(2);
        std::ofstream(out / opt_in_file()) << "x";
        lt_scroll_to(ui, "Deep##ltap0");
        CHECK(ui.click("High##ltap2", "##tacright"), "High picked");
        ui.frames(2);
        CHECK(ui.type_into(ui.find("##ltlhbox", "##tacright"), "90"), "line height typed");
        ui.frames(2);
        CHECK(ui.click("Counter##ltbu1", "##tacright"), "Counter picked");
        ui.frames(130);
        CHECK(s.can_apply && s.status == "ready", "ready: " + s.status);
        // the map record's Line Height write never lands (read-back mismatch); its approach takes the write, then refuses to go back
        sm.drop = SheetWorld::kRecs + kRecLineHeight;
        sm.once = SheetWorld::kRecs + kRecApproach;
        sm.on = true;
        lt_scroll_top(ui);
        CHECK(ui.click("Apply to the live tactic##ltapply", "##tacright"), "Apply clicked");
        ui.frames(2);
        CHECK(ui.click("Apply##ltyes", "Apply to the live tactic?"), "confirmed");
        ui.frames(4);
        sm.on = false;
        CHECK(s.last.find("READ-BACK MISMATCH") != std::string::npos && s.last.find("undo FAILED") != std::string::npos, "last: " + s.last);
        CHECK(w.i32(SheetWorld::kRecs + kRecApproach) == 2 && w.i32(SheetWorld::kRecs + kRecLineHeight) == 25 && w.i32(SheetWorld::kRecs + kRecBuildUp) == 0 &&
                  w.i32(w.copy_rec(0xF80) + kRecApproach) == 0,
              "everything put back but the Defensive Approach");
        UndoRecord u;
        std::string err;
        bool names_ap = false;
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "failed", "undo file: failed: " + u.state + " " + err);
        for (const FieldWrite& x : u.writes) names_ap = names_ap || (!x.copy && x.kind == FieldKind::Approach && x.old_value == 0 && x.new_value == 2);
        CHECK(names_ap && u.writes.size() == 3, fmt("the undo file names every field the job wrote (%zu)", u.writes.size()));
        // Undo (allowed after "failed", while channel E is off for the session): the approach goes back; the rest already holds its value
        ui.frames(130);
        CHECK(s.can_undo, "Undo offered: " + s.status);
        CHECK(ui.click("Undo Turbo's write##ltundo", "##tacright"), "Undo clicked");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecApproach) == 0, "the Defensive Approach put back");
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "restored", "undo file: restored: " + u.state + " " + u.note);
        CHECK(rig.logged("channel E: undo team 1 record 0 approach 2->0 (read back ok)"), "the undo is logged");
        // clean up
        app.team_sheet = nullptr;
        for (const char* f : {opt_in_file(), undo_file()}) fs::remove(out / f, ec);
        st = saved;
        st["seq"] = saved.value("seq", 0LL) + 60;
        write_state();
        live_tactic_reset();
    });

    // 2.15 bench keep through the card's own code: the toggle writes the opt-in; Team Management opens = its working copy at svc+0x550 is built
    // in the game's order (Lua's team_mgmt stays false, as on LE 27.1.5) -> one guarded job writes your saved order (the map's) into it;
    // the probe logs the copy; one write per build; channel E's undo file untouched; the toggle off
    run_case("UI: bench keep - the toggle, Team Management's copy built in the game's order -> your saved order written into it once per build, logged", [&] {
        live_tactic_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {opt_in_file(), kill_switch_file(), master_switch_file(), undo_file(), bench::keep_on_file(), bench::keep_off_file()})
            fs::remove(out / f, ec);
        SheetWorld w(1, 10);
        ServiceRig rig(w, out);
        rig.find_returns = SheetWorld::kSvc;
        rig.jobs.inline_run = true;
        app.team_sheet = rig.svc;
        double wall = 2.0e9;
        live_tactic_test_wall_clock([&wall]() { return wall; });
        std::vector<std::string> hooked;
        app.log_hook = [&hooked](const std::string& x) { hooked.push_back(x); };
        auto app_logged = [&hooked](const std::string& part) { return has_part(hooked, part); };
        const fs::path state_file = out / "bridge_state.json";
        json st = read_json(state_file);
        const json saved = st;
        int bumps = 900;
        auto write_state = [&]() {
            {
                std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                f << st.dump();
            }
            fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
            app.next_poll = 0.0;
            ui.frames(3);
        };
        st["match_phase"] = "hub";
        st["team_mgmt"] = false;
        st["save_t"] = (long long)(std::time(nullptr)) - 3600;
        st["managers"] = hex_addr(SheetWorld::kManagers);
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        tactics_tab_test::select_team(app, ui, 7);
        tactics_tab_test::go_scope(ui, "My team");
        ui.frames(3);
        const LiveTacticState& s = live_tactic_state();
        rig.clock += 6e6;
        ui.frames(70);
        CHECK(s.read_ok, "read: " + s.summary);
        lt_scroll_top(ui);
        CHECK(!s.bench_keep_on && s.bench_keep == "Bench order: off (no turbo_output\\bench_keep_on.txt)", "off by default: " + s.bench_keep);
        CHECK(ui.click("Keep my bench order after Team Management##ltbench", "##tacright"), "the toggle clicked");
        auto step = [&](int n) {
            for (int i = 0; i < n; ++i) {
                wall += 0.6;
                ui.frames(2);
            }
        };
        step(3);
        CHECK(fs::exists(out / bench::keep_on_file()) && s.bench_keep_on &&
                  s.bench_keep == "Bench order: on: waiting for Team Management to build its copy of your sheet",
              "on: the opt-in file written: " + s.bench_keep);
        CHECK(app_logged("bench keep: switched on in the Turbo window (turbo_output\\bench_keep_on.txt)") &&
                  app_logged("bench keep: watching Team Management's working copy svc+0x550 (not built: Team Management closed)"),
              "the toggle and the watch are logged");
        // Team Management opens: its copy is built in the game's order (4 places of the bench / reserves); Lua's team_mgmt stays false
        const Ids52 orig = ids_of(w);
        Ids52 game = orig;
        std::swap(game[11], game[15]);
        std::swap(game[20], game[29]);
        put_tm_copy(w, game, 0x3BCE0000ULL);
        step(2);
        CHECK(copy_ids(w) == orig && ids_of(w) == orig, "your saved order is in Team Management's copy, the map untouched: " + s.bench_keep);
        CHECK(app_logged("bench keep: Team Management built its working copy svc+0x550 in another order than your saved one (4 places of the bench / reserves"),
              "the build is logged");
        CHECK(app_logged("bench keep: your saved order is queued for the game thread (4 places of the bench / reserves, Team Management's working copy svc+0x550)"),
              "queued line");
        CHECK(rig.logged("bench keep: team 1 sheet -1: your saved order written into Team Management's working copy svc+0x550, places 11..51 (4 places; the "
                         "starting XI untouched) (read back ok) [job"),
              "the job's line");
        CHECK(app_logged("bench probe: Team Management's working copy svc+0x550 built [records 0x3BCE0000]: against your map sheet (your saved order): 4 of 31 places "
                         "changed"),
              "the probe logs the copy's build");
        CHECK(app_logged("bench probe: Team Management opened"), "the probe sees Team Management open by the copy (Lua's flag stays false)");
        step(2);
        CHECK(s.bench_keep.find("Bench order: on: put back: your saved order is in Team Management's working copy, places 11..51 (4 places written") == 0,
              s.bench_keep);
        CHECK(!fs::exists(out / undo_file()) && s.last.empty(), "channel E's tactic card saw nothing of it (no undo file, no 'Last' line)");
        // the same build in the game's order again: one write per build
        put_tm_copy(w, game, 0x3BCE0000ULL);
        step(10);
        CHECK(copy_ids(w) == game, "ONE write per build: " + s.bench_keep);
        // Team Management closes (the copy is no longer your sheet's) and opens again: the new build is written
        put_tm_copy(w, game, 0x3BCE0000ULL, 1);
        step(2);
        CHECK(app_logged("bench probe: Team Management's working copy svc+0x550 is no longer your sheet's (Team Management closed?)"), "the copy gone is logged");
        put_tm_copy(w, game, 0x3BD00000ULL);
        step(2);
        CHECK(copy_ids(w) == orig, "the next opening is written again");
        // the toggle off: the file removed
        lt_scroll_top(ui);
        CHECK(ui.click("Keep my bench order after Team Management##ltbench", "##tacright"), "the toggle clicked again");
        step(2);
        CHECK(!fs::exists(out / bench::keep_on_file()) && !s.bench_keep_on && s.bench_keep == "Bench order: off (no turbo_output\\bench_keep_on.txt)",
              "off: " + s.bench_keep);
        put_tm_copy(w, game, 0x3BD10000ULL);
        step(4);
        CHECK(copy_ids(w) == game, "off: nothing written");
        // clean up
        app.log_hook = nullptr;
        live_tactic_test_wall_clock(nullptr);
        app.team_sheet = nullptr;
        for (const char* f : {opt_in_file(), undo_file(), bench::keep_on_file()}) fs::remove(out / f, ec);
        st = saved;
        st["seq"] = saved.value("seq", 0LL) + 90;
        write_state();
        live_tactic_reset();
    });
    live_tactic_set_details_open(false);
}

}  // namespace team_sheet_test
