// Native tests: tactics channel E (core/team_sheet.h, ui/ui_live_tactic.cpp, turbo_output\TACTICS_CHANNEL_E_REPORT.txt section 4): your
// club's live tactic record on a SIMULATED sheet layout laid out exactly as the report's live probe read it on build 6AC07E31-2145C000
// (the service, the hash map with a second key in the same bucket, the 0x290-byte sheet, five 0xF8-byte records, eleven 0x48-byte slots,
// a stale copy of the service's vtable pair, the PlayerStatusManager chain). Player ids are synthetic. Included by test_main.cpp after the
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
                mem.wr(r + kRecActive, uint8_t(1));
                mem.wr(r + kRecPreset, int32_t(1));
                mem.wr(r + kRecDepth, int32_t(25));
                mem.wr(r + 0xA8, int32_t(2));
                mem.wr(r + 0xAC, int32_t(2));
                mem.wr(r + kRecBuildUp, int32_t(0));
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
        CHECK(lt.name == "433" && lt.depth == 25 && lt.build_up == 0 && lt.preset == 1 && lt.formation == 27 && lt.source_formation == 36 && lt.audio == 18,
              "\"433\", depth 25, build-up 0, preset 1, formation 27 / 36 / 18");
        CHECK(lt.sheet_id == -1 && lt.mode == -1 && lt.dirty == 1 && lt.squad.size() == 31, "sheet -1, mode -1, dirty 1, 31 players");
        const SlotView& st = lt.slots[9];
        CHECK(st.role == 38213 && st.group() == 9 && st.role_id == 21 && st.focus == 5 && st.position == 25, "ST: 38213 = group 9 / role 21 / focus 5");
        CHECK(lt.slots[0].role == 4227 && lt.slots[0].group() == 1 && lt.slots[0].role_id == 2 && lt.slots[0].focus == 3, "GK: 4227 = 1 / 2 / 3");
        CHECK(role_word(9, 21, 5) == 38213 && role_word(4, 30, 2) == 18306 && role_word(8, 16, 5) == 33797, "the encoding of report 2.9");
        CHECK(depth_band(25) == 0 && depth_band(31) == 1 && depth_band(75) == 2 && depth_band(91) == 3 && depth_band_text(75) == "band 2 (61-90)", "depth bands");
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

    run_case("channel E: G5 - SetSheet's propagation (vf 0xF8) would run for an active sheet with a mode: refused", [] {
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
        CHECK(lt.at.sheet == SheetWorld::kSheet + kSheetStride && active_sheet_id(w.mem, lt.at, 0, id) && id == 2 && check_g5(w.mem, lt).empty(),
              "the sheet is not the active one of its mode: pass");
    });

    run_case("channel E: G6 - value ranges, the group stays, preset and formation refused in 2.2, the plan", [] {
        SheetWorld w;
        const LiveTactic lt = live(w);
        const RoleAllowList al = allow_of(w);
        Plan p;
        auto plan = [&](Request r) { return make_plan(lt, r, p, &al); };
        Request r;
        r.build_up = 4;
        CHECK(plan(r).find("outside 0..3") != std::string::npos, "build-up 4");
        r = Request();
        r.depth = 0;
        CHECK(plan(r).find("outside 1..100") != std::string::npos, "depth 0");
        r.depth = 101;
        CHECK(plan(r).find("outside 1..100") != std::string::npos, "depth 101");
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
        CHECK(plan(r).find("preset") != std::string::npos && plan(r).find("2.2") != std::string::npos, "preset refused");
        r = Request();
        r.formation = true;
        CHECK(plan(r).find("formation") != std::string::npos, "formation refused");
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
        r.depth = 60;
        CHECK(make_plan(lt, r, p, nullptr).empty() && p.writes.size() == 1, "depth stays editable without the list");
        // nothing changes
        r = Request();
        r.build_up = 0;
        r.depth = 25;
        r.slots = {{9, 21, 5}};
        CHECK(plan(r).empty() && p.writes.empty(), "the same values: an empty plan");
        // the acceptance test's change (report section 5)
        r = Request();
        r.build_up = 2;
        r.depth = 75;
        r.slots = {{9, -1, 6}};
        CHECK(plan(r).empty() && p.writes.size() == 5, fmt("build-up, depth, the three role values (%zu writes)", p.writes.size()));
        CHECK(p.writes[0].kind == FieldKind::BuildUp && p.writes[0].old_value == 0 && p.writes[0].new_value == 2 && p.writes[1].kind == FieldKind::Depth &&
                  p.writes[1].new_value == 75 && p.writes[2].kind == FieldKind::Role && p.writes[2].slot == 9 && p.writes[2].old_value == 38213 &&
                  p.writes[2].new_value == 38214 && p.writes[3].kind == FieldKind::RoleId && p.writes[3].new_value == 21 && p.writes[4].kind == FieldKind::Focus &&
                  p.writes[4].new_value == 6,
              "build-up 0->2, depth 25->75, slot 9 38213->38214 (21 / 6)");
        CHECK(p.user == 95 && p.record_index == 0 && p.formation == 27 && p.source_formation == 36 && p.sheet_id == -1 && p.pids[9] == 100010, "the plan names its record");
    });

    run_case("channel E: the write - in place, dirty = 1, read back, nothing else touched (the report's log line)", [] {
        SheetWorld w;
        const LiveTactic lt = live(w);
        Request r;
        r.build_up = 2;
        r.depth = 75;
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
        CHECK(res.log_line == "channel E: team 95 record 0 build-up 0->2 depth 25->75 slot 9 role 38213->38214 (read back ok)", "log: " + res.log_line);
        // the walks read whole pages: a few dozen reads of the game instead of several hundred (the report's < 50 us job)
        CHECK(res.reads == counting.reads && res.reads > 0 && res.reads <= 40 && counting.writes == 6 && w.mem.failed_reads == reads_before,
              fmt("%d reads (%d counted), %d writes: 5 fields + the dirty flag", res.reads, counting.reads, counting.writes));
        CHECK(w.i32(SheetWorld::kRecs + kRecBuildUp) == 2 && w.i32(SheetWorld::kRecs + kRecDepth) == 75, "build-up 2, depth 75 in the record");
        CHECK(w.i32(w.slot(9) + kSlotRole) == 38214 && w.i32(w.slot(9) + kSlotRoleId) == 21 && w.i32(w.slot(9) + kSlotFocus) == 6, "slot 9: 38214 / 21 / 6");
        CHECK(w.u8(SheetWorld::kSheet + kSheetDirty) == 1 && res.dirty_before == 0 && res.dirty_after == 1, "dirty 0 -> 1");
        CHECK(res.after.depth == 75 && res.after.slots[9].focus == 6, "the read-out after the write");
        // byte for byte: only the written fields and the dirty flag changed
        auto diff = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
            std::vector<size_t> d;
            for (size_t i = 0; i < a.size() && i < b.size(); ++i)
                if (a[i] != b[i]) d.push_back(i);
            return d;
        };
        CHECK(diff(svc_before, w.bytes(SheetWorld::kSvc, kSvcSize)).empty(), "the service (and its cache +0x13A0 / +0x13A4) untouched");
        CHECK(diff(sheet_before, w.bytes(SheetWorld::kSheet, kSheetStride)) == std::vector<size_t>({kSheetDirty}), "the sheet: only the dirty flag");
        std::vector<size_t> rd = diff(rec_before, w.bytes(SheetWorld::kRecs, 5 * kRecStride));
        CHECK(rd == std::vector<size_t>({kRecDepth, kRecBuildUp}), fmt("the records: only depth and build-up (%zu bytes)", rd.size()));
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
        r.depth = 60;
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
        w.mem.rd(new_recs + kRecDepth, depth);
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
        r.build_up = 3;
        r.depth = 80;
        r.slots = {{9, -1, 6}};
        Plan p;
        const RoleAllowList al = allow_of(w);
        CHECK(make_plan(live(w), r, p, &al).empty(), "plan");
        const ApplyResult res = apply_plan(fm, SheetWorld::kSvc, w.anchors(), p, checks(&al));
        CHECK(!res.ok && res.undone && res.session_off && !res.restore_failed && res.stage == "readback", "undone: " + res.message);
        CHECK(res.message.find("focus of slot 9 reads 5, not 6") != std::string::npos, "the mismatch is named: " + res.message);
        CHECK(res.log_line.find("READ-BACK MISMATCH") != std::string::npos && res.log_line.find("undone from the snapshot") != std::string::npos, res.log_line);
        CHECK(w.i32(SheetWorld::kRecs + kRecBuildUp) == 0 && w.i32(SheetWorld::kRecs + kRecDepth) == 25 && w.i32(w.slot(9) + kSlotRole) == 38213 &&
                  w.i32(w.slot(9) + kSlotRoleId) == 21 && w.i32(w.slot(9) + kSlotFocus) == 5,
              "every field holds its value from before");
        CHECK(w.u8(SheetWorld::kSheet + kSheetDirty) == 0, "the dirty flag is back to 0");
        // a write the memory layer refuses: the same full undo
        SheetWorld w2;
        FlakyMemory fm2(w2.mem);
        fm2.drop = SheetWorld::kRecs + kRecDepth;
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
        CHECK(rig.svc->read(rig.ctx(), lt, why) && lt.depth == 25 && lt.g5.empty(), "read: " + why);
        // the bridge's club must be the game's
        Context other = rig.ctx();
        other.user = 96;
        CHECK(!rig.svc->read(other, lt, why) && why.find("not the bridge's 96") != std::string::npos, "another club: " + why);
        Context nomgr = rig.ctx();
        nomgr.managers = 0;
        CHECK(!rig.svc->read(nomgr, lt, why) && why.find("manager table") != std::string::npos, "no manager table: " + why);
        // a write: no opt-in -> the job refuses; then on
        Request r;
        r.depth = 70;
        Plan p;
        rig.svc->read(rig.ctx(), lt, why);
        make_plan(lt, r, p, nullptr);
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued: " + why);
        CHECK(rig.svc->busy(), "busy while queued");
        std::string why2;
        CHECK(!rig.svc->submit(rig.ctx(), p, why2) && why2.find("still queued") != std::string::npos, "one job at a time");
        rig.jobs.pump();
        ApplyResult res;
        CHECK(rig.svc->poll(res) && !res.ok && res.stage == "G1" && res.message.find("call_team_sheet_on.txt") != std::string::npos, "no opt-in: " + res.message);
        CHECK(w.i32(SheetWorld::kRecs + kRecDepth) == 25 && !rig.svc->busy(), "nothing written");
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
        CHECK(w.i32(SheetWorld::kRecs + kRecDepth) == 25, "still nothing written");
        // a job the game thread runs late
        rig.svc->submit(rig.ctx(), p, why);
        rig.clock += 2.5e6;
        rig.jobs.pump();
        CHECK(rig.svc->poll(res) && !res.ok && res.stage == "late" && w.i32(SheetWorld::kRecs + kRecDepth) == 25, "late: " + res.message);
        // on time
        rig.svc->submit(rig.ctx(), p, why);
        rig.jobs.pump();
        CHECK(rig.svc->poll(res) && res.ok && res.written && w.i32(SheetWorld::kRecs + kRecDepth) == 70, "written: " + res.message);
        CHECK(rig.logged("channel E: team 95 record 0 depth 25->70 (read back ok)"), "logged with the job time");
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
        fm.drop = SheetWorld::kRecs + kRecDepth;
        ServiceRig rig2(w, out, &fm);
        rig2.find_returns = SheetWorld::kSvc;
        rig2.jobs.inline_run = true;
        CHECK(rig2.svc->read(rig2.ctx(), lt, why), "rig 2 read: " + why);
        Plan p2;
        Request r2;
        r2.depth = 33;
        make_plan(lt, r2, p2, nullptr);
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
        r.depth = 44;
        Plan p;
        make_plan(lt, r, p, nullptr);
        // job 1 is queued and never run (the dispatcher dropped it): after 10 s the service stops waiting
        CHECK(rig.svc->submit(rig.ctx(), p, why) && rig.svc->busy(), "job 1 queued");
        rig.clock += 5e6;
        CHECK(rig.svc->busy() && !rig.svc->submit(rig.ctx(), p, why), "5 s: still waited for");
        rig.clock += 6e6;
        CHECK(!rig.svc->busy(), "11 s: not busy any more");
        CHECK(rig.svc->submit(rig.ctx(), p, why) && rig.svc->busy() && rig.logged("did not come back"), "job 2 queued, the lost one logged: " + why);
        rig.jobs.pump();  // job 1 runs at last (given up: cancelled, nothing written), then job 2
        ApplyResult a, b;
        CHECK(rig.svc->poll(a) && a.stage == "cancelled" && rig.svc->poll(b) && b.ok && b.written && w.i32(SheetWorld::kRecs + kRecDepth) == 44,
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
        p.writes = {{FieldKind::Depth, 3, 25, 50}};
        CHECK(check_plan(lt, p, checks(&al)).find("not a slot field") != std::string::npos, "depth with a slot");
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
            r.depth = 70;
            Plan p;
            make_plan(live(w), r, p, nullptr);
            std::atomic<bool> cancel{true};
            JobChecks ck;
            ck.cancel = &cancel;
            const ApplyResult res = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p, ck);
            CHECK(!res.ok && res.stage == "cancelled" && w.i32(SheetWorld::kRecs + kRecDepth) == 25, "cancelled: " + res.message);
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
            CHECK(rig.svc->poll(a) && a.stage == "cancelled" && w.i32(SheetWorld::kRecs + kRecDepth) == 25, "it ran: cancelled, nothing written: " + a.message);
            // in the job: another club named by the game, the hub left, a reload: refused
            rig.jobs.report_none = false;
            Context c = rig.ctx();
            CHECK(rig.svc->submit(c, p, why), "queued");
            Context away = c;
            away.hub = false;
            rig.svc->note_context(away);  // the UI saw the hub left before the game thread ran the job
            rig.jobs.pump();
            CHECK(rig.svc->poll(a) && a.stage == "G2" && w.i32(SheetWorld::kRecs + kRecDepth) == 25, "hub left: " + a.message);
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
            CHECK(rig.svc->poll(a) && a.stage == "user" && w.i32(SheetWorld::kRecs + kRecDepth) == 25, "the game names another club: " + a.message);
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

    run_case("channel E: the cm_mentalities mirror's checks - the row must be the live tactic; its undo obeys the switches and the row", [] {
        SheetWorld w;
        const LiveTactic lt = live(w);
        Request r;
        r.build_up = 2;
        r.depth = 75;
        Plan p;
        make_plan(lt, r, p, nullptr);
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
        UndoRecord u = UndoRecord::from_plan(p);
        u.mirror_row_key = 2;
        Switches sw;
        CHECK(undo_mirror_check(sw, get, u).find("off until") != std::string::npos, "the mirror is opt-in: its undo too");
        sw.mirror_on = true;
        CHECK(undo_mirror_check(sw, get, u).empty(), "on, the tactic's row: put back");
        sw.off = true;
        CHECK(undo_mirror_check(sw, get, u).find("call_team_sheet_off.txt") != std::string::npos, "the kill switch stops it");
        sw.off = false;
        sw.mirror_off = true;
        CHECK(undo_mirror_check(sw, get, u).find("channel_e_db_mirror_off.txt") != std::string::npos, "the mirror's own switch");
        sw.mirror_off = false;
        row["teamid"] = 7;
        CHECK(undo_mirror_check(sw, get, u).find("is not club 95's active tactic") != std::string::npos, "another club's artificialkey: refused");
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
        r.build_up = 2;
        r.depth = 75;
        r.slots = {{9, -1, 6}};
        Plan p;
        const RoleAllowList al = allow_of(w);
        make_plan(live(w), r, p, &al);
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
        CHECK(UndoRecord::parse(u.to_json(), back, &err) && back.writes.size() == 5 && back.team == 95 && back.pids == u.pids && back.state == "written" &&
                  back.mirror.size() == 1 && back.mirror[0].old_value == 55 && back.mirror_row_key == 2 && back.dirty_before == 1,
              "round trip: " + err);
        CHECK(!UndoRecord::parse("{\"x\":1}", back, &err), "not an undo file: " + err);
        const fs::path f = test_tmp_dir("channel_e_undo") / "channel_e_undo.json";
        CHECK(save_undo(f, u, &err) && load_undo(f, back, &err) && back.writes.size() == 5, "saved and loaded: " + err);
        // apply, then undo
        CHECK(apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), p, checks(&al)).ok, "written");
        WriteHistory hist;
        hist.note(p);  // what the service records after the job
        const JobChecks uck = checks(nullptr, &hist);
        CHECK(reverted(live(w), u).empty(), "nothing overwritten");
        Plan up;
        std::vector<std::string> skipped;
        CHECK(make_undo_plan(live(w), u, "", 3, up, skipped).empty() && up.undo && up.writes.size() == 5 && skipped.empty() && up.load_gen == 3,
              "the undo plan: all five");
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
                if (fw.kind == FieldKind::Depth) fw.old_value = 0;
            Plan dp;
            CHECK(make_undo_plan(live(w), d, "", 3, dp, skipped).empty(), "the depth-0 undo plan is made (the file is trusted only that far)");
            const ApplyResult dr = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), dp, uck);
            CHECK(!dr.ok && dr.stage == "G6" && dr.message.find("outside 1..100") != std::string::npos && w.i32(SheetWorld::kRecs + kRecDepth) == 75,
                  "depth 0: refused in the job: " + dr.message);
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
                if (fw.kind == FieldKind::Depth) fw.old_value = 60;
            Plan op;
            make_undo_plan(live(w), ov, "", 3, op, skipped);
            const ApplyResult orr = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), op, uck);
            CHECK(!orr.ok && orr.message.find("before Turbo's first write") != std::string::npos, "an undo value not in the history: " + orr.message);
            Plan fresh;
            make_undo_plan(live(w), u, "", 3, fresh, skipped);
            CHECK(fresh.writes.size() == 5 && !apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), fresh, checks(nullptr)).ok, "an undo without the history: refused");
        }
        // Edit Tactic changed the depth since: that field is left alone, the rest goes back
        w.mem.wr(SheetWorld::kRecs + kRecDepth, int32_t(40));
        CHECK(has_part(reverted(live(w), u), "defensivedepth 75 -> 40"), "the overwrite is seen");
        CHECK(make_undo_plan(live(w), u, "", 3, up, skipped).empty() && up.writes.size() == 4 && has_part(skipped, "defensivedepth is 40"), "depth left as it is");
        const ApplyResult ur = apply_plan(w.mem, SheetWorld::kSvc, w.anchors(), up, uck);
        CHECK(ur.ok && ur.undo && w.i32(SheetWorld::kRecs + kRecBuildUp) == 0 && w.i32(w.slot(9) + kSlotRole) == 38213 && w.i32(SheetWorld::kRecs + kRecDepth) == 40,
              "build-up and the slot back, depth kept: " + ur.message);
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
        a.depth = 60;
        a.slots = {{9, -1, 6}};
        make_plan(live(w2), a, p1, &al);
        apply_plan(w2.mem, SheetWorld::kSvc, w2.anchors(), p1, checks(&al));
        WriteHistory hist2;
        hist2.note(p1);
        UndoRecord u1 = UndoRecord::from_plan(p1);
        u1.state = "written";
        u1.dirty_before = 0;
        Request b;
        b.depth = 70;
        b.build_up = 1;
        make_plan(live(w2), b, p2, nullptr);
        UndoRecord u2 = merge_undo(u1, UndoRecord::from_plan(p2));
        auto find = [](const UndoRecord& x, FieldKind k, int slot) -> const FieldWrite* {
            for (const auto& fw : x.writes)
                if (fw.kind == k && fw.slot == slot) return &fw;
            return nullptr;
        };
        CHECK(find(u2, FieldKind::Depth, -1) && find(u2, FieldKind::Depth, -1)->old_value == 25 && find(u2, FieldKind::Depth, -1)->new_value == 70,
              "depth: 25 (before the first write) -> 70");
        CHECK(find(u2, FieldKind::BuildUp, -1) && find(u2, FieldKind::BuildUp, -1)->old_value == 0, "build-up: 0 -> 1");
        CHECK(find(u2, FieldKind::Role, 9) && find(u2, FieldKind::Role, 9)->old_value == 38213 && find(u2, FieldKind::Role, 9)->new_value == 38214 &&
                  u2.writes.size() == 5 && u2.dirty_before == 0,
              "the slot of the first write stays listed with its original value");
        apply_plan(w2.mem, SheetWorld::kSvc, w2.anchors(), p2);
        hist2.note(p2);
        CHECK(make_undo_plan(live(w2), u2, "", -1, up, skipped).empty() && apply_plan(w2.mem, SheetWorld::kSvc, w2.anchors(), up, checks(nullptr, &hist2)).ok,
              "the merged undo runs");
        CHECK(w2.i32(SheetWorld::kRecs + kRecDepth) == 25 && w2.i32(SheetWorld::kRecs + kRecBuildUp) == 0 && w2.i32(w2.slot(9) + kSlotRole) == 38213,
              "back to the values before Turbo's first write");
        UndoRecord another = UndoRecord::from_plan(p2);
        another.team = 96;
        CHECK(merge_undo(u1, another).writes.size() == another.writes.size(), "another club is not merged");
        UndoRecord later = UndoRecord::from_plan(p2);
        later.load_gen = 9;
        CHECK(merge_undo(u1, later).writes.size() == later.writes.size(), "another career load is not merged");
    });

    run_case("channel E: the kick-off check finds the match-setup copy of the written role words (report 2.12), read-only", [] {
        SheetWorld w;
        std::array<int32_t, 11> roles{}, pids{};
        for (int s = 0; s < 11; ++s) {
            roles[size_t(s)] = SheetWorld::kRoles[size_t(s)];
            pids[size_t(s)] = w.pids[size_t(s)];
        }
        roles[9] = 38214;
        const std::vector<uint8_t> pat = role_pattern(roles);
        CHECK(pat.size() == 44, "44 bytes");
        // the match copy: team at +0x28, roles at +0x118, pids at +0x168; a second hit without the pids (a UI copy)
        const uint64_t out = 0x429F9EB08ULL, ui = 0xFA6ECCF0ULL;
        w.mem.map(out, 0x200);
        w.mem.wr(out + kCopyTeam, int32_t(95));
        w.mem.write(out + kCopyRoles, pat.data(), pat.size());
        for (int s = 0; s < 11; ++s) w.mem.wr(out + kCopyPids + 4 * uint64_t(s), pids[size_t(s)]);
        w.mem.map(ui, 0x100);
        w.mem.write(ui, pat.data(), pat.size());
        CHECK(is_match_copy(w.mem, out + kCopyRoles, 95, pids) && !is_match_copy(w.mem, ui, 95, pids) && !is_match_copy(w.mem, out + kCopyRoles, 96, pids),
              "the layout tells them apart");
        std::vector<uint8_t> buf(0x100, 0);
        std::memcpy(buf.data() + 0x41, pat.data(), pat.size());
        std::memcpy(buf.data() + 0x80, pat.data(), pat.size());
        CHECK(find_bytes(buf.data(), buf.size(), 0x1000, pat, 10, 4) == std::vector<uint64_t>({0x1080}), "4-aligned hits only");
        const fs::path o = test_tmp_dir("channel_e_kickoff");
        ServiceRig rig(w, o);
        const std::vector<uint8_t> before = w.bytes(out, 0x200);
        rig.svc->kickoff_check(95, roles, pids);
        const std::string ks = rig.svc->kickoff_status();
        CHECK(ks.find("2 copies of the written role words, 1 in the match-setup layout") != std::string::npos && ks.find("0x429F9EB08") != std::string::npos,
              "status: " + ks);
        CHECK(rig.logged("channel E: kick-off check"), "logged");
        CHECK(w.bytes(out, 0x200) == before, "nothing written");
    });
}

// --------------------------------------------------------------------------------------------------------------------------- UI
// Teams > Tactics > My team: the summary line, the card, Apply (asks first), the cm_mentalities mirror, the read-back, Undo, the gates
static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("UI: Teams > Tactics > My team: channel E - the live read-out, opt-in, Apply writes in place + mirror, overwrite seen, Undo, gates", [&] {
        live_tactic_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {opt_in_file(), kill_switch_file(), master_switch_file(), undo_file(), mirror_off_file(), mirror_on_file()}) fs::remove(out / f, ec);
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
        CHECK(s.summary_shown && s.summary.find("\"433\", depth 25, build-up 0") != std::string::npos, "the summary line: " + s.summary);
        CHECK(rig.logged("channel E: role allow-list: 10 triples from 1 teams"), "the role allow-list is logged");
        // the card
        if (!s.card_open) ui.click("Edit the live tactic (channel E)##ltcard", "##tacright");
        ui.frames(2);
        CHECK(s.card_open, "card open");
        CHECK(s.written == "live team sheet" && s.seen == "untested" && s.tag.find("live team sheet, untested") == 0, "badges: " + s.tag);
        CHECK(s.read_ok && s.readout.find("formation 27 (source 10), depth 25 band 0 (1-30), build-up 0, preset 1") != std::string::npos, s.readout);
        CHECK(s.slots.size() == 11 && s.slots[9].find("role 38213 = group 9 / role 21 / focus 5") != std::string::npos, "slot 9: " + (s.slots.size() > 9 ? s.slots[9] : ""));
        CHECK(s.not_written.find("preset 1") != std::string::npos && s.not_written.find("formation 27") != std::string::npos, s.not_written);
        CHECK(!s.can_apply && s.status.find("G1") == 0, "off without the opt-in: " + s.status);
        // the opt-in checkbox writes the file
        CHECK(ui.click("Channel E on (turbo_output\\call_team_sheet_on.txt)##ltopt", "##tacright"), "opt-in clicked");
        ui.frames(2);
        CHECK(fs::exists(out / opt_in_file()) && s.opt_in, "opt-in file created");
        CHECK(s.status.find("G6 values: nothing changed") == 0, "now only the values hold it back: " + s.status);
        // the acceptance test's change: build-up 0 -> 2, depth 25 -> 75, the striker's focus 5 -> 6
        CHECK(ui.type_into(ui.find("##ltbu", "##tacright"), "2"), "build-up typed");
        CHECK(ui.type_into(ui.find("##ltdd", "##tacright"), "75"), "depth typed");
        CHECK(ui.type_into(ui.find("##ltslot", "##tacright"), "9"), "slot 9");
        ui.frames(2);
        // only what the game itself uses is offered: role ids 21 / 22 of group 9, focuses 5 (the live one), 3 and 6 with role 21 - not 4
        auto has_int = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
        CHECK(has_int(s.role_choices, 21) && has_int(s.role_choices, 22) && s.role_choices.size() == 2, "role ids offered");
        CHECK(has_int(s.focus_choices, 5) && has_int(s.focus_choices, 3) && has_int(s.focus_choices, 6) && !has_int(s.focus_choices, 4) &&
                  s.focus_choices.size() == 3,
              "focuses offered");
        CHECK(ui.click("6##ltfoc6", "##tacright"), "focus 6 picked");
        // the mirror is opt-in: on, it still checks the row (this save's cm_mentalities has no playerid0..10: skipped)
        std::ofstream(out / mirror_on_file()) << "x";
        ui.frames(130);
        CHECK(s.can_apply && s.status == "ready", "ready: " + s.status);
        CHECK(has_part(s.plan, "build-up (buildupplay) 0 -> 2") && has_part(s.plan, "defensive depth 25 -> 75 (band 2 (61-90))") &&
                  has_part(s.plan, "slot 9 (ST) role word 38213 -> 38214 = group 9 / role 21 / focus 6"),
              "Apply will write the three changes");
        CHECK(w.i32(SheetWorld::kRecs + kRecDepth) == 25, "nothing written yet");
        CHECK(ui.click("Apply to the live tactic##ltapply", "##tacright"), "Apply clicked");
        ui.frames(2);
        CHECK(s.confirm_open && has_part(s.confirm_lines, "38213 -> 38214"), "Apply asks first");
        CHECK(ui.click("Apply##ltyes", "Apply to the live tactic?"), "confirmed");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecBuildUp) == 2 && w.i32(SheetWorld::kRecs + kRecDepth) == 75 && w.i32(w.slot(9) + kSlotRole) == 38214 &&
                  w.i32(w.slot(9) + kSlotFocus) == 6 && w.u8(SheetWorld::kSheet + kSheetDirty) == 1,
              "written into the live record");
        CHECK(s.last.find("team 1 record 0 build-up 0->2 depth 25->75 slot 9 role 38213->38214 (read back ok)") != std::string::npos, "last: " + s.last);
        UndoRecord u;
        std::string err;
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "written" && u.team == 1 && u.writes.size() == 5, "undo file: written " + err);
        // the mirror: cm_mentalities row artificialkey 2 (55 / 2) cannot be matched to the live slots (no playerid0..10 / position0..10 in this save)
        const Table* cm = app.db.table("cm_mentalities");
        const uint64_t row = cm ? app.db.find(*cm, "artificialkey", 2) : 0;
        CHECK(row && app.db.get_int(*cm, row, "defensivedepth") == 55 && app.db.get_int(*cm, row, "buildupplay") == 2, "the row is not touched");
        CHECK(u.mirror_state == "skipped" && u.mirror.empty() && s.mirror.find("no playerid0 / position0") != std::string::npos, "mirror skipped: " + s.mirror);
        // Edit Tactic overwrites the depth: the 5-second re-read and the card say so
        w.mem.wr(SheetWorld::kRecs + kRecDepth, int32_t(25));
        ui.frames(70);
        CHECK(has_part(s.overwritten, "defensivedepth 75 -> 25"), "overwrite seen");
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
            CHECK(has_part(s.overwritten, "Edit Tactic (or the game) overwrote the change: defensivedepth 75 -> 25"), "this load's write again");
        }
        // a match being set up: no write, no undo
        st["match_phase"] = "prematch";
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        ui.frames(2);
        CHECK(!s.can_apply && !s.can_undo && s.status.find("G2 career hub") == 0, "pre-match: refused: " + s.status);
        st["match_phase"] = "hub";
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        // the mirror's undo record points at another club's row (artificialkey 3, club 7): it must never be put back there
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
        // Undo: the fields that still hold Turbo's values go back; the overwritten depth is left as it is; the mirror goes back
        CHECK(ui.click("Undo Turbo's write##ltundo", "##tacright"), "Undo clicked");
        ui.frames(4);
        CHECK(w.i32(SheetWorld::kRecs + kRecBuildUp) == 0 && w.i32(w.slot(9) + kSlotRole) == 38213 && w.i32(w.slot(9) + kSlotFocus) == 5 &&
                  w.i32(SheetWorld::kRecs + kRecDepth) == 25,
              "put back");
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "restored" && u.note.find("defensivedepth is 25") != std::string::npos, "undo file: " + u.state + " " + u.note);
        // the mirror's undo: refused by the kill switch (call_team_sheet_off.txt stops the mirror both ways); no row touched
        const uint64_t row3 = cm ? app.db.find(*cm, "artificialkey", 3) : 0;
        CHECK(app.db.get_int(*cm, row, "defensivedepth") == 55 && row3 && app.db.get_int(*cm, row3, "defensivedepth") == 45 && u.mirror_state == "written" &&
                  s.mirror.find("not put back: kill switch turbo_output\\call_team_sheet_off.txt") != std::string::npos,
              "the mirror's undo refused: " + s.mirror);
        CHECK(!s.can_undo, "nothing left to undo");
        // clean up
        app.team_sheet = nullptr;
        for (const char* f : {opt_in_file(), kill_switch_file(), undo_file(), mirror_on_file()}) fs::remove(out / f, ec);
        st = saved;
        st["seq"] = saved.value("seq", 0LL) + 50;
        write_state();
        live_tactic_reset();
    });
}

}  // namespace team_sheet_test
