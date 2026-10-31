// Native tests: gameplay tuning (core/tuning.h, ui/ui_tuning.cpp) on a SIMULATED tuning table: a static pointer slot (the anchor), a
// manager object with a vtable, a collection with a header (count, magic bytes) and eight knobs of every type behind it; a second copy
// of the collection the manager can be pointed at (the job must re-chase); an inline-pattern locator and a native locator onto the same
// table. Nothing here is a real game address: the shipped knobs file holds placeholders only (checked first). Included by test_main.cpp
// after test_team_sheet.h; tuning_test::run() runs the core cases, tuning_test::ui_cases() the Competitions > Gameplay tuning view.
#pragma once
#include "core/tuning.h"
#include "ui/ui_tuning.h"

namespace tuning_test {

using namespace turbo;
using namespace turbo::tuning;

static const char* const kBuildKey = "6AC07E31-2145C000";

struct TuneWorld {
    SimMemory mem;
    static constexpr uint64_t kImage = 0x140000000ULL, kImageSize = 0x2145C000ULL;
    static constexpr uint64_t kSlot = 0x14C100000ULL, kMgrVt = 0x14B200000ULL, kOtherVt = 0x14B300000ULL;
    static constexpr uint64_t kMgr = 0x21000000ULL, kColl = 0x22000000ULL, kColl2 = 0x23000000ULL;
    static constexpr uint64_t kBase = kColl + 0x20;  // the table's base (add 0x20)
    TuneWorld() {
        mem.map(kSlot, 8);
        mem.wr(kSlot, kMgr);
        mem.map(kMgrVt, 8);
        mem.map(kMgr, 0x80);
        mem.wr(kMgr, kMgrVt);
        mem.wr(kMgr + 0x40, kColl);
        put_coll(kColl);
        put_coll(kColl2);
    }
    void put_coll(uint64_t c) {
        mem.map(c, 0x100);
        mem.wr(c + 0x08, uint32_t(12));
        mem.write(c + 0x10, "TUNE", 4);
        const uint64_t b = c + 0x20;
        mem.wr(b + 0x00, 0.5f);         // pressing.intensity f32
        mem.wr(b + 0x04, int32_t(45));  // shape.line_height i32
        mem.wr(b + 0x08, uint8_t(60));  // marking.tightness u8
        mem.wr(b + 0x09, uint8_t(1));   // runs.in_behind bool
        mem.wr(b + 0x0C, 0.2f);         // passing.error_ground f32 (live, any phase)
        mem.wr(b + 0x10, 0.3f);         // passing.error_lofted f32
        mem.wr(b + 0x14, 0.7f);         // shooting.error f32 (inline-pattern locator)
        mem.wr(b + 0x18, 0.4f);         // cpuai.pass_risk f32 (native locator)
    }
    float f32(uint64_t a) {
        float v = 0;
        mem.rd(a, v);
        return v;
    }
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
};

// The knobs file of the simulated table (verified knobs of every type, a placeholder, four locators)
static const char* const kTestKnobs = R"({
  "_doc": ["a test file: the simulated table of test_tuning.h"],
  "schema": "turbo-tuning-knobs/1", "build": "6AC07E31-2145C000", "version": 7, "note": "simulated",
  "locators": [
    {"name": "test_coll", "kind": "chain", "signature": "tuning_test_slot", "deref": true, "chain": ["0x40"], "add": "0x20", "verified": true,
     "checks": [{"object": 0, "offset": 0, "kind": "vtable", "signature": "tuning_test_vt", "note": "the manager"},
                {"object": 1, "offset": 8, "kind": "u32_range", "min": 1, "max": 64},
                {"object": 1, "offset": "0x10", "kind": "bytes", "bytes": "54 55 4E 45"}]},
    {"name": "inline_coll", "kind": "chain", "pattern": "48 8B 05 ?? ?? ?? ??", "resolve": "rip", "deref": true, "chain": [64], "add": 32,
     "verified": true, "checks": [{"object": 0, "kind": "vtable", "signature": "tuning_test_vt"}]},
    {"name": "native_coll", "kind": "native", "verified": true},
    {"name": "todo_coll", "kind": "chain", "verified": false, "note": "a placeholder"}
  ],
  "knobs": [
    {"id": "pressing.intensity", "label": "Pressing intensity", "group": "Pressing", "collection": "gp_test_press", "field": "intensity",
     "type": "f32", "min": 0, "max": 1, "default": 0.5, "step": 0.01, "locator": "test_coll", "offset": "0x0", "applies": "next match setup", "verified": true},
    {"id": "shape.line_height", "label": "Defensive line height", "group": "Shape", "collection": "gp_test_shape", "field": "line",
     "type": "i32", "unit": "%", "min": 0, "max": 100, "default": 45, "locator": "test_coll", "offset": 4, "applies": "next match setup", "verified": true},
    {"id": "marking.tightness", "label": "Marking tightness", "group": "Marking", "collection": "gp_test_mark", "field": "tight",
     "type": "u8", "min": 0, "max": 100, "default": 60, "locator": "test_coll", "offset": 8, "verified": true},
    {"id": "runs.in_behind", "label": "Runs in behind the defence", "group": "Attacking runs", "collection": "gp_test_runs", "field": "behind",
     "type": "bool", "default": 1, "locator": "test_coll", "offset": 9, "applies": "next match setup", "verified": true},
    {"id": "passing.error_ground", "label": "Ground pass error", "group": "Passing", "collection": "gp_test_pass", "field": "err_ground",
     "type": "f32", "min": 0, "max": 1, "default": 0.2, "locator": "test_coll", "offset": "0xC", "applies": "live", "when": ["hub", "prematch", "match"], "verified": true},
    {"id": "passing.error_lofted", "label": "Lofted pass error", "group": "Passing", "collection": "gp_test_pass", "field": "err_lofted",
     "type": "f32", "min": 0, "max": 1, "default": 0.3, "locator": "test_coll", "offset": "0x10", "applies": "next match setup", "verified": true},
    {"id": "shooting.error", "label": "Shot error", "group": "Shooting", "collection": "gp_test_shot", "field": "err",
     "type": "f32", "min": 0, "max": 2, "default": 0.7, "locator": "inline_coll", "offset": "0x14", "applies": "next match setup", "verified": true},
    {"id": "cpuai.pass_risk", "label": "CPU passing risk", "group": "CPU AI", "collection": "gp_test_cpu", "field": "risk",
     "type": "f32", "min": 0, "max": 1, "default": 0.4, "locator": "native_coll", "offset": "0x18", "applies": "next match setup", "verified": true},
    {"id": "gk.reaction", "label": "Goalkeeper reaction time", "group": "Goalkeeper", "type": "f32", "locator": "todo_coll", "verified": false}
  ]
})";

static Lookup lookup_of(TuneWorld& w) {
    Lookup lk;
    lk.signature = [](const std::string& n) -> uint64_t {
        if (n == "tuning_test_slot") return TuneWorld::kSlot;
        if (n == "tuning_test_vt") return TuneWorld::kMgrVt;
        if (n == "tuning_other_vt") return TuneWorld::kOtherVt;
        return 0;
    };
    lk.scan = [](const Signature& s) {
        SigResult r;
        r.name = s.name;
        if (s.pattern == "48 8B 05 ?? ?? ?? ??" && s.resolve == "rip") {
            r.state = SigState::Found;
            r.match = 0x141000000ULL;
            r.address = TuneWorld::kSlot;
        } else {
            r.state = SigState::Missing;
            r.error = "no match";
        }
        return r;
    };
    lk.natives["native_coll"] = [](Memory& m, uint64_t& base) -> std::string {
        const uint64_t c = m.ptr(TuneWorld::kMgr + 0x40);
        if (!c) return "the manager holds no collection";
        base = c + 0x20;
        return "";
    };
    lk.image_base = TuneWorld::kImage;
    lk.image_size = TuneWorld::kImageSize;
    (void)w;
    return lk;
}

static Registry test_registry() {
    Registry r;
    std::string err;
    parse_registry(kTestKnobs, r, err);
    return r;
}

static AnchorMap anchors_of(Memory& mem, const Registry& reg, const Lookup& lk) {
    AnchorMap am;
    for (const Locator& l : reg.locators) am[l.name] = resolve_anchor(mem, l, lk);
    return am;
}

static std::map<std::string, KnobLive> live_of(Memory& mem, const Registry& reg, const AnchorMap& am, const Lookup& lk) {
    std::map<std::string, KnobLive> out;
    for (const Knob& k : reg.knobs) {
        KnobLive l;
        l.id = k.id;
        KnobAt at;
        l.why = locate_knob(mem, reg, k, am, lk, at);
        l.located = l.why.empty();
        l.addr = at.addr;
        l.value = at.value;
        l.copies = at.copies;
        l.writable = l.located && k.has_range();
        out[k.id] = l;
    }
    return out;
}

// A memory that drops (or refuses) the writes to one address: the read-back then sees the old value
struct DropMemory : Memory {
    SimMemory& m;
    uint64_t drop = 0;
    bool refuse = false;
    int writes = 0;
    explicit DropMemory(SimMemory& mm) : m(mm) {}
    bool read(uint64_t a, void* out, size_t n) override { return m.read(a, out, n); }
    bool write(uint64_t a, const void* in, size_t n) override {
        ++writes;
        if (drop && a == drop) return !refuse;
        return m.write(a, in, n);
    }
};

// The game-thread dispatcher of a test: jobs wait until pump() (or run at once)
struct TJobs {
    std::vector<std::function<void()>> q;
    bool inline_run = false, report_none = false;
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

struct TuneRig {
    TuneWorld& w;
    TJobs jobs;
    double clock = 1000.0;
    std::vector<std::string> logs;
    fs::path knobs, out;
    std::shared_ptr<CoreService> svc;
    TuneRig(TuneWorld& world, const fs::path& dir, const std::string& knobs_text = kTestKnobs, Memory* mem = nullptr) : w(world) {
        knobs = dir / "tuning";
        out = dir / "turbo_output";
        std::error_code ec;
        fs::create_directories(knobs, ec);
        fs::create_directories(out, ec);
        std::ofstream(knobs / "knobs_6AC07E31.json", std::ios::binary | std::ios::trunc) << knobs_text;
        Host h;
        h.mem = mem ? mem : &w.mem;
        h.build = kBuildKey;
        h.knobs_dir = knobs;
        h.out_dir = out;
        h.lookup = lookup_of(w);
        h.run_on_game_thread = [this](std::function<void()> f) { return jobs.run(std::move(f)); };
        h.run_background = [](std::function<void()> f) { f(); };
        h.log = [this](const std::string& s) { logs.push_back(s); };
        h.clock_us = [this]() { return clock; };
        svc = std::make_shared<CoreService>(std::move(h));
    }
    Context ctx(long long gen = 3, const std::string& phase = "hub") const {
        Context c;
        c.session = "s1";
        c.load_gen = gen;
        c.phase = phase;
        c.in_cm = true;
        return c;
    }
    std::map<std::string, KnobLive> live() {
        std::map<std::string, KnobLive> m;
        for (const KnobLive& l : svc->read()) m[l.id] = l;
        return m;
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

// The attribulator layout of TACTICS_MOD_CATALOGUE section 7 and the live run (turbo_output\ATTRIBULATOR_LIVE.json) on a simulated
// memory: a reflected type (TypeInfoData with its name, size, field count, signature and three FieldInfoData whose types are Float32 /
// Int32 / Boolean) and its TypeInfo in the image, and two instances of it in the heap (base and simulation, each with its vtable at +0,
// its TypeInfo at +8 and its asset name at +0x18), plus a decoy that carries the base name but no image pointer. L3: two database
// runtime objects (type AttribDBRuntime_test: +8 its TypeInfo, +0x18 its Name, +0x40 the group object with tag bit 2 set) and their
// group objects (type AttribGroupRuntime_test, +0x28 the instance), plus a decoy database object of another type with the base
// database's name. `twin`: a second instance with the base instance's name, vtable and TypeInfo that no database points at (the live
// run found one for the attacking line group). Lookup::refs searches the simulated pages with the same helpers the host uses.
struct AttribWorld {
    SimMemory mem;
    static constexpr uint64_t kImage = 0x140000000ULL, kImageSize = 0x2145C000ULL;
    static constexpr uint64_t kTd = 0x14D000000ULL, kFields = 0x14D001000ULL, kTiF = 0x14D002000ULL, kTiI = 0x14D002010ULL, kTiB = 0x14D002020ULL;
    static constexpr uint64_t kTiG = 0x14D002030ULL;  // the group type's TypeInfo (its +0 = kTd)
    static constexpr uint64_t kTdF = 0x14D003000ULL, kTdI = 0x14D003100ULL, kTdB = 0x14D003200ULL, kTypePtr = 0x14D005000ULL, kStrings = 0x14E000000ULL;
    static constexpr uint64_t kDbTd = 0x14D004000ULL, kDbTi = 0x14D004100ULL, kGrpTd = 0x14D004200ULL, kGrpTi = 0x14D004300ULL;
    static constexpr uint64_t kDbVt = 0x14D006000ULL, kGrpVt = 0x14D006100ULL;
    static constexpr uint64_t kBase = 0x24000000ULL, kSim = 0x24001000ULL, kDecoy = 0x24002000ULL, kNames = 0x24100000ULL, kNames2 = 0x24110000ULL;
    static constexpr uint64_t kDbBase = 0x24200000ULL, kDbSim = 0x24201000ULL, kGrpBase = 0x24202000ULL, kGrpSim = 0x24203000ULL;
    static constexpr uint64_t kDbDecoy = 0x24204000ULL, kTwin = 0x24205000ULL;
    static constexpr const char* kBaseName = "Fifa/Attribulator/Gameplay/groups/test/test_group_runtime";
    static constexpr const char* kSimName = "Fifa/Attribulator/Gameplay/databases/simulation/groups/test/test_group_runtime";
    static constexpr const char* kDbBaseName = "Fifa/Attribulator/Gameplay/gameplayattribdb";
    static constexpr const char* kDbSimName = "Fifa/Attribulator/Gameplay/databases/simulation/simulation";
    uint64_t str_at = kStrings;
    uint64_t put_str(const std::string& s) {
        const uint64_t a = str_at;
        mem.map(a, s.size() + 1);
        mem.write(a, s.c_str(), s.size() + 1);
        str_at += (s.size() + 16) & ~uint64_t(7);
        return a;
    }
    void put_type(uint64_t td, uint64_t ti, const char* name, uint16_t size, uint32_t sig) {
        mem.map(td, 0x48);
        mem.wr(td, put_str(name));
        mem.wr(td + 0x0E, size);
        mem.wr(td + 0x34, sig);
        mem.map(ti, 8);
        mem.wr(ti, td);
    }
    explicit AttribWorld(uint32_t sig = 0x12345678, bool twin = false) {
        // the field types' TypeInfoData, their TypeInfo objects
        const std::pair<uint64_t, const char*> types[] = {{kTdF, "Float32"}, {kTdI, "Int32"}, {kTdB, "Boolean"}};
        const uint64_t tis[] = {kTiF, kTiI, kTiB};
        for (int i = 0; i < 3; ++i) {
            mem.map(types[i].first, 0x48);
            mem.wr(types[i].first, put_str(types[i].second));
            mem.map(tis[i], 8);
            mem.wr(tis[i], types[i].first);
        }
        // the group's type and its TypeInfo
        mem.map(kTd, 0x48);
        mem.wr(kTd, put_str("AttribSchema_test_group"));
        mem.wr(kTd + 0x0E, uint16_t(0x40));
        mem.wr(kTd + 0x32, uint16_t(3));
        mem.wr(kTd + 0x34, sig);
        mem.wr(kTd + 0x40, kFields);
        mem.map(kTiG, 8);
        mem.wr(kTiG, kTd);
        const char* names[] = {"speedModifier", "count", "enabled"};
        const uint32_t offs[] = {0x28, 0x2c, 0x30};
        mem.map(kFields, 3 * 0x20);
        for (int i = 0; i < 3; ++i) {
            mem.wr(kFields + 0x20 * uint64_t(i), put_str(names[i]));
            mem.wr(kFields + 0x20 * uint64_t(i) + 0x10, offs[i]);
            mem.wr(kFields + 0x20 * uint64_t(i) + 0x18, tis[i]);
        }
        mem.map(kTypePtr, 8);
        // the database and group runtime types (no fields: only their name, size and signature are read)
        put_type(kDbTd, kDbTi, "AttribDBRuntime_test", 0x170, 0xA5106425);
        put_type(kGrpTd, kGrpTi, "AttribGroupRuntime_test", 0x40, 0x7ADE3BD5);
        // the instances (heap)
        mem.map(kNames, 0x400);
        mem.write(kNames, kBaseName, std::strlen(kBaseName) + 1);
        mem.write(kNames + 0x100, kSimName, std::strlen(kSimName) + 1);
        mem.write(kNames + 0x200, kBaseName, std::strlen(kBaseName) + 1);  // the decoy's own copy of the name
        mem.write(kNames + 0x300, kDbBaseName, std::strlen(kDbBaseName) + 1);
        mem.write(kNames + 0x340, kDbSimName, std::strlen(kDbSimName) + 1);
        mem.write(kNames + 0x3A0, kDbBaseName, std::strlen(kDbBaseName) + 1);  // the decoy database's own copy
        put_inst(kBase, kNames, 0.75f);
        put_inst(kSim, kNames + 0x100, 0.8f);
        mem.map(kDecoy, 0x40);
        mem.wr(kDecoy, uint64_t(0x30000000));  // not an image pointer
        mem.wr(kDecoy + 0x18, kNames + 0x200);
        // L3: database runtime objects -> group objects (tag bit 2 on the slots) -> the instances
        put_obj(kDbBase, kDbVt, kDbTi, kNames + 0x300);
        put_obj(kDbSim, kDbVt, kDbTi, kNames + 0x340);
        mem.wr(kDbBase + 0x40, kGrpBase | 4);
        mem.wr(kDbSim + 0x40, kGrpSim);
        put_obj(kGrpBase, kGrpVt, kGrpTi, 0);
        put_obj(kGrpSim, kGrpVt, kGrpTi, 0);
        mem.wr(kGrpBase + 0x28, kBase);
        mem.wr(kGrpSim + 0x28, kSim | 4);
        put_obj(kDbDecoy, kDbVt, kTiF, kNames + 0x3A0);  // the base database's name, another type
        if (twin) {
            mem.map(kNames2, 0x100);
            mem.write(kNames2, kBaseName, std::strlen(kBaseName) + 1);
            put_inst(kTwin, kNames2, 0.75f);
        }
    }
    void put_obj(uint64_t o, uint64_t vt, uint64_t ti, uint64_t name) {
        mem.map(o, 0x80);
        mem.wr(o, vt);
        mem.wr(o + 8, ti);
        if (name) mem.wr(o + 0x18, name);
    }
    void put_inst(uint64_t i, uint64_t name, float speed) {
        mem.map(i, 0x40);
        mem.wr(i, kTypePtr);
        mem.wr(i + 8, kTiG);
        mem.wr(i + 0x18, name);
        mem.wr(i + 0x28, speed);
        mem.wr(i + 0x2c, int32_t(5));
        mem.wr(i + 0x30, uint8_t(1));
    }
    float f32(uint64_t a) {
        float v = 0;
        mem.rd(a, v);
        return v;
    }
    Lookup lookup() {
        Lookup lk;
        lk.image_base = kImage;
        lk.image_size = kImageSize;
        lk.refs = [this](const std::vector<std::string>& names, bool in_image) {
            std::map<std::string, std::vector<uint64_t>> out;
            std::map<uint64_t, size_t> strings;
            for (const auto& pg : mem.pages) {
                const uint64_t base = pg.first * SimMemory::kPage;
                const bool img = base >= kImage && base < kImage + kImageSize;
                if (img != in_image) continue;
                for (const auto& h : find_cstrings(pg.second.data(), pg.second.size(), base, names, 64)) strings[h.second] = h.first;
            }
            std::set<uint64_t> values;
            for (const auto& kv : strings) values.insert(kv.first);
            for (const auto& pg : mem.pages) {
                const uint64_t base = pg.first * SimMemory::kPage;
                const bool img = base >= kImage && base < kImage + kImageSize;
                if (img != in_image) continue;
                for (const auto& h : find_qwords(pg.second.data(), pg.second.size(), base, values, 64)) out[names[strings[h.first]]].push_back(h.second);
            }
            return out;
        };
        return lk;
    }
};

static const char* const kAttribKnobs = R"({
  "schema": "turbo-tuning-knobs/1", "build": "6AC07E31-2145C000",
  "locators": [
    {"name": "attrib.test", "kind": "attrib", "type": "AttribSchema_test_group", "type_signature": "0x12345678", "type_size": "0x40", "type_fields": 3,
     "variants": [{"name": "base", "instance": "fifa/attribulator/gameplay/groups/test/test_group_runtime"},
                  {"name": "simulation", "instance": "Fifa/Attribulator/Gameplay/databases/simulation/groups/test/test_group_runtime"}], "verified": true}
  ],
  "knobs": [
    {"id": "shape.speed", "label": "Speed", "group": "Shape", "collection": "test_group", "field": "speedModifier", "type": "f32", "min": 0, "max": 2,
     "default": 0.75, "copy_defaults": {"simulation": 0.8}, "locator": "attrib.test", "offset": "0x28", "verified": true},
    {"id": "shape.count", "label": "Count", "group": "Shape", "collection": "test_group", "field": "count", "type": "i32", "min": 0, "max": 10,
     "default": 5, "locator": "attrib.test", "offset": "0x2c", "verified": true},
    {"id": "shape.enabled", "label": "Enabled", "group": "Shape", "collection": "test_group", "field": "enabled", "type": "bool",
     "default": 1, "locator": "attrib.test", "offset": "0x30", "verified": true},
    {"id": "shape.wrong_offset", "label": "Wrong offset", "group": "Shape", "collection": "test_group", "field": "speedModifier", "type": "f32",
     "min": 0, "max": 2, "default": 0.75, "locator": "attrib.test", "offset": "0x2c", "verified": true},
    {"id": "shape.missing", "label": "Missing", "group": "Shape", "collection": "test_group", "field": "nope", "type": "f32",
     "min": 0, "max": 2, "default": 1, "locator": "attrib.test", "offset": "0x34", "verified": true},
    {"id": "shape.wrong_type", "label": "Wrong type", "group": "Shape", "collection": "test_group", "field": "speedModifier", "type": "i32",
     "min": 0, "max": 2, "default": 1, "locator": "attrib.test", "offset": "0x28", "verified": true}
  ]
})";

// The same group through the database chain (L3), as the live run proved it: database runtime object by its Name and TypeInfo,
// +family_slot -> group object (its type checked), +group_slot -> the instance; vtable RVA and TypeInfo at +8 checked
static const char* const kAttribL3Knobs = R"({
  "schema": "turbo-tuning-knobs/1", "build": "6AC07E31-2145C000",
  "locators": [
    {"name": "attrib.l3", "kind": "attrib", "type": "AttribSchema_test_group", "type_signature": "0x12345678", "type_size": "0x40", "type_fields": 3,
     "db_type": "AttribDBRuntime_test", "db_type_signature": "0xA5106425", "group_type": "AttribGroupRuntime_test", "group_type_signature": "0x7ADE3BD5",
     "family_slot": "0x40", "group_slot": "0x28", "head": "typeinfo", "head_rva": "0xD005000",
     "variants": [{"name": "base", "instance": "Fifa/Attribulator/Gameplay/groups/test/test_group_runtime", "db": "Fifa/Attribulator/Gameplay/gameplayattribdb"},
                  {"name": "simulation", "instance": "Fifa/Attribulator/Gameplay/databases/simulation/groups/test/test_group_runtime",
                   "db": "Fifa/Attribulator/Gameplay/databases/simulation/simulation"}], "verified": true}
  ],
  "knobs": [
    {"id": "shape.speed", "label": "Speed", "group": "Shape", "collection": "test_group", "field": "speedModifier", "type": "f32", "min": 0, "max": 2,
     "default": 0.75, "copy_defaults": {"simulation": 0.8}, "locator": "attrib.l3", "offset": "0x28", "verified": true}
  ]
})";

static bool has_text(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v)
        if (x.find(s) != std::string::npos) return true;
    return false;
}

static GateInput hub_input() {
    GateInput in;
    in.sw.opt_in = true;
    in.bridge = in.in_cm = true;
    in.match_phase = "hub";
    in.load_gen = 3;
    return in;
}

// 2.7: the simulated table with a promoted catalogue: pressing.intensity proven AND live (applies "live"), shape.line_height proven and latched
// ("next match setup"), marking.tightness proven with "applies" unknown; shooting.error is `"live": true` WITHOUT a proof (it is not live_ok).
// promoted = false: the same file with no proof at all (the 2.6.0 state)
static std::string live_knobs_text(bool promoted = true) {
    std::string t = kTestKnobs;
    auto begin_of = [&](const std::string& id) { return t.find("{\"id\": \"" + id + "\""); };
    auto end_of = [&](const std::string& id) { return t.find("\"verified\": true}", begin_of(id)); };
    auto add = [&](const std::string& id, const std::string& extra) { t.insert(end_of(id), extra); };
    if (promoted) {
        const std::string a = "pressing.intensity";
        const size_t b0 = begin_of(a), b1 = end_of(a);
        std::string o = t.substr(b0, b1 - b0);
        o.replace(o.find("next match setup"), 16, "live");
        t = t.substr(0, b0) + o + t.substr(b1);
        for (const char* id : {"pressing.intensity", "shape.line_height", "marking.tightness"})
            add(id, std::string("\"proof\": {\"session\": \"T29\", \"evidence\": \"EV/2026-10-20/battery_010101/result.json#") + id + "\"}, \"slider\": true, " +
                        (std::string(id) == "pressing.intensity" ? "\"live\": true, " : ""));
    }
    add("shooting.error", "\"live\": true, ");
    return t;
}

#include "test_tuning_chain.h"  // 2.4: the chain from the attribute manager, the profile, the moments (chain_cases)

// --------------------------------------------------------------------------------------------------------------------------- core
static void run() {
    std::printf("native gameplay tuning (engine tuning tables)\n");
    chain_cases();

    run_case("tuning: the shipped knobs file v5 - the attribute manager, chain slots per group, two groups verified, the moments, no slider without a proof, the inventory candidates", [] {
        const fs::path p = fs::path("..") / "turbo" / "package" / "turbo" / "tuning" / "knobs_6AC07E31.json";
        std::ifstream f(p, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(!text.empty(), "the package's knobs file is there: " + p.string());
        Registry reg;
        std::string err;
        CHECK(parse_registry(text, reg, err), "it parses: " + err);
        CHECK(reg.build == kBuildKey && reg.version == 5 && reg.knobs.size() == 124 && reg.locators.size() == 36,
              fmt("build %s, v%d, %zu knobs, %zu locators", reg.build.c_str(), reg.version, reg.knobs.size(), reg.locators.size()));
        // the attribute manager: the signature, the current database chain, the prematch handle [A], the 15 + 1 databases' names
        const AttribManager& m = reg.manager;
        CHECK(m.present && m.signature == "attrib_db_getter" && m.current == std::vector<int64_t>({0, 8}) && m.prematch_slot == 8 && m.handle_db == 8 &&
                  m.mgr_size == 0x240 && m.gameplay_db_type == "AttribDBRuntime_gameplayattribdb" && m.gameplay_db_signature == 0xA5106425 &&
                  m.prematch_db_type == "AttribDBRuntime_prematchattribdb" && m.prematch_db_signature == 0xB8B9B8BB && m.gameplay_dbs.size() == 15 &&
                  m.prematch_dbs == std::vector<std::string>({"Fifa/Attribulator/PreMatch/prematchattribdb"}) &&
                  m.gameplay_dbs[0] == "Fifa/Attribulator/Gameplay/gameplayattribdb",
              "attrib_manager");
        const SignatureTable* sig = builtin_signature_table(kBuildKey);
        CHECK(sig && sig->find(m.signature), "its signature is in the built-in table");
        // every locator through the databases: FC 27's type data (T5 layout_T5.json) for its family and group slots (the 8 locators of v4; the
        // 28 new ones belong to the inventory candidates and are checked below)
        struct Slots {
            const char* locator;
            uint64_t family, group;
            const char* group_type;
            uint32_t group_sig;
            bool prematch;
        };
        const Slots slots[] = {
            {"attrib.attackinglinelengthwidth", 0x128, 0x28, "AttribGroupRuntime_gp_positioningformation", 0x7ADE3BD5, false},
            {"attrib.defensivelinelengthwidth", 0x128, 0x30, "AttribGroupRuntime_gp_positioningformation", 0x7ADE3BD5, false},
            {"attrib.zonal_defense", 0x38, 0x148, "AttribGroupRuntime_gp_positioning", 0xF3A936E7, false},
            {"attrib.defense_general", 0x38, 0xA8, "AttribGroupRuntime_gp_positioning", 0xF3A936E7, false},
            {"attrib.markingtightness", 0x38, 0x68, "AttribGroupRuntime_gp_positioning", 0xF3A936E7, false},
            {"attrib.attackingaggressiveness", 0x38, 0x30, "AttribGroupRuntime_gp_positioning", 0xF3A936E7, false},
            {"attrib.teamtacticssettings", 0x40, 0x18, "AttribGroupRuntime_pm_teamtacticssettings", 0xF1168603, true},
            {"attrib.partialteampress", 0x38, 0x128, "AttribGroupRuntime_gp_positioning", 0xF3A936E7, false},
        };
        for (const Slots& s : slots) {
            const Locator* l = reg.locator(s.locator);
            CHECK(l && l->chain_capable() && l->family_slot == s.family && l->group_slot == s.group && l->group_type == s.group_type &&
                      l->group_type_signature == s.group_sig && m.prematch(*l) == s.prematch && l->l3(),
                  std::string(s.locator) + ": its chain slots, group type, database (and each variant's database for the name search)");
        }
        // VERIFIED only what the live run proved (turbo_output\ATTRIBULATOR_LIVE.json): the attacking line group and the depth bands
        const std::set<std::string> proven = {"attrib.attackinglinelengthwidth", "attrib.teamtacticssettings"};
        for (const Locator& l : reg.locators) {
            CHECK(l.verified == (proven.count(l.name) == 1), l.name + ": verified only when the live run proved it");
            if (l.verified)
                CHECK(l.head == "typeinfo" && l.head_rva && l.variants.size() == 1 && l.variants[0].guid.size() == 16, l.name + ": TypeInfo, vtable and asset GUID");
        }
        for (const Knob& k : reg.knobs)
            CHECK(k.verified == (proven.count(k.locator) == 1 && !k.candidate), k.id + ": verified exactly with its locator (a v5 candidate never: its effect and field are not confirmed)");
        CHECK(reg.placeholders() == 13 + 101, fmt("%d knobs not confirmed (the 13 of v4 and the 101 inventory candidates)", reg.placeholders()));
        // the moments, no live write, the honest UI: a slider only with a proof (a pure data change adds one: "proof" + "slider")
        for (const Knob& k : reg.knobs) {
            CHECK(k.when == std::vector<std::string>({"boot", "career_load", "hub"}) && !k.live, k.id + ": the three moments, not live (gate X1 decides)");
            CHECK(!k.slider || k.proven(), k.id + ": no slider without a proof");
            CHECK(!k.proven() || (k.verified && k.proof.evidence.rfind("EV/", 0) == 0), k.id + ": a proof names its evidence and needs a verified knob");
        }
        // the catalogue knobs: group / field / type / offset from FC 27's type data, FC 27's own value inside Turbo's range
        for (const Knob& k : reg.knobs) {
            const Locator* l = reg.locator(k.locator);
            CHECK(l && l->kind == "attrib" && !l->type.empty() && l->type_signature && l->type_size && l->type_fields > 0 && !l->variants.empty(),
                  k.id + ": an attrib locator with its type check");
            CHECK(!k.collection.empty() && !k.field.empty() && k.offset >= 0 && l && uint64_t(k.offset) + type_size(k.type) <= l->type_size,
                  k.id + ": group, field, offset inside the type");
            CHECK(k.has_range() && k.has_default && check_value(k, k.def).empty(), k.id + ": FC 27's value inside Turbo's range");
            CHECK(k.label.find('_') == std::string::npos && k.label.find(k.field) == std::string::npos, k.id + ": a plain label, not the field name");
            CHECK(std::find(standard_groups().begin(), standard_groups().end(), k.group) != standard_groups().end() &&
                      (k.verified || k.notes.find("Not yet confirmed in the running game") != std::string::npos),
                  k.id + ": a standard group, an unconfirmed knob says so");
        }
        // FC 27's own values and type data only (TACTICS_MOD_CATALOGUE sections 4 and 6; T5 for the partial press): nothing else
        struct Fc27 {
            const char* id;
            const char* collection;
            const char* field;
            ValueType type;
            int64_t offset;
            double value;
        };
        const Fc27 fc27[] = {
            {"shape.width_in", "gp_positioningformation_attackinglinelengthwidth", "lineWidthModifier", ValueType::F32, 0x28, 1.0},
            {"shape.width_in_cb", "gp_positioningformation_attackinglinelengthwidth", "lineWidthModifierForCB", ValueType::F32, 0x3c, 1.0},
            {"shape.length_in", "gp_positioningformation_attackinglinelengthwidth", "LineLengthModifierForBalanced", ValueType::F32, 0x30, 0.84},
            {"shape.length_in_possession", "gp_positioningformation_attackinglinelengthwidth", "LineLengthModifierForPossession", ValueType::F32, 0x50, 0.8},
            {"shape.width_out", "gp_positioningformation_defensivelinelengthwidth", "lineWidthModifier", ValueType::F32, 0x28, 1.0},
            {"shape.length_out", "gp_positioningformation_defensivelinelengthwidth", "LineLengthModifierForBalanced", ValueType::F32, 0x30, 1.0},
            {"shape.gap_dmf", "gp_positioning_zonal_defense", "dmfLineOffsetWhenTouchIsOnPar", ValueType::F32, 0x664, 8.0},
            {"shape.gap_dmf_deep", "gp_positioning_zonal_defense", "dmfLineOffsetWhenTouchIsDeep", ValueType::F32, 0x658, 9.0},
            {"shape.gap_mf_deep", "gp_positioning_zonal_defense", "mfLineOffsetWithDmfLineWhenTouchIsDeep", ValueType::F32, 0x67c, 14.0},
            {"shape.holding_line_depth", "gp_positioning_defense_general", "depthThresholdForHoldingLine", ValueType::F32, 0x9c, 0.3},
            {"lineband.deep_max", "pm_teamtacticssettings", "maxDepthThresholdForDeepDefensiveStyle", ValueType::I32, 0x3c, 30},
            {"lineband.balanced_min", "pm_teamtacticssettings", "minDepthThresholdForBalancedDefensiveStyle", ValueType::I32, 0x40, 31},
            {"lineband.balanced_max", "pm_teamtacticssettings", "maxDepthThresholdForBalancedDefensiveStyle", ValueType::I32, 0x2c, 60},
            {"lineband.high_min", "pm_teamtacticssettings", "minDepthThresholdForHighDefensiveStyle", ValueType::I32, 0x38, 61},
            {"lineband.high_max", "pm_teamtacticssettings", "maxDepthThresholdForHighDefensiveStyle", ValueType::I32, 0x44, 90},
            {"lineband.aggressive_min", "pm_teamtacticssettings", "minDepthThresholdForAggressiveDefensiveStyle", ValueType::I32, 0x34, 91},
            {"pressing.trigger_depth", "gp_positioning_zonal_defense", "TacticDepthThresholdForZonePressing", ValueType::F32, 0x644, 0.31},
            {"pressing.zone_marking_depth", "gp_positioning_zonal_defense", "TacticDepthThresholdForZoneMarking", ValueType::F32, 0x684, 1.01},
            {"pressing.no_double_marking_own_half", "gp_positioning_defense_general", "teamPressDisableDoubleManMarkingInDefenderHalf", ValueType::Bool, 0xb8, 1},
            {"marking.tightness", "gp_positioning_markingtightness", "markingTightnessUniversalModifier", ValueType::F32, 0x28, 0.5},
            {"runs.frequency", "gp_positioning_attackingaggressiveness", "runFrequencyUniversialModifier", ValueType::F32, 0x28, 0.5},
            {"pressing.partial_highworkratetime", "gp_positioning_partialteampress", "partialTeamPressHighWorkRateTime", ValueType::F32, 0x84, 6.0},
            {"pressing.partial_cooldownmultiplier", "gp_positioning_partialteampress", "partialTeamPressCooldownMultiplier", ValueType::F32, 0x74, 1.0},
        };
        // v5: the 23 knobs of v4 first and unchanged (saved presets name them), then the 101 inventory candidates
        CHECK(reg.knobs.size() == sizeof(fc27) / sizeof(fc27[0]) + 101, fmt("%zu knobs: the %zu of the FC 27 table of this test and 101 candidates", reg.knobs.size(), sizeof(fc27) / sizeof(fc27[0])));
        for (size_t i = 0; i < sizeof(fc27) / sizeof(fc27[0]); ++i) CHECK(reg.knobs[i].id == fc27[i].id && !reg.knobs[i].candidate, std::string(fc27[i].id) + ": still in its v4 place, not a candidate");
        for (const Fc27& x : fc27) {
            const Knob* k = reg.knob(x.id);
            CHECK(k && k->collection == x.collection && k->field == x.field && k->type == x.type && k->offset == x.offset && k->def == x.value,
                  std::string(x.id) + ": FC 27's group, field, type, offset and own value");
        }
        // v5 candidates: offline inventory only. Never verified, no proof, no slider, not live; ranked 1..N with the 23 old knobs; FC 27's own values
        {
            int cands = 0;
            std::set<int> ranks;
            for (const Knob& k : reg.knobs) {
                CHECK(k.rank >= 1 && k.rank <= int(reg.knobs.size()) && ranks.insert(k.rank).second, k.id + ": a unique rank 1..N");
                CHECK(k.impact == "high" || k.impact == "medium" || k.impact == "low", k.id + ": an impact word");
                CHECK(!k.category.empty(), k.id + ": a category");
                if (!k.candidate) continue;
                ++cands;
                CHECK(!k.verified && !k.proven() && !k.slider && !k.live && k.placeholder().find("unverified") == 0, k.id + ": a candidate is unverified, no proof, no slider, not live");
                CHECK(k.scale == "mult" || k.scale == "add" || k.scale == "int" || k.scale == "switch", k.id + ": a scale class");
                const Locator* cl = reg.locator(k.locator);
                bool v4_loc = false;  // a locator of v4 (verified or not, as before); the new ones are unverified and carry the asset GUID
                for (const Slots& s : slots) v4_loc = v4_loc || k.locator == s.locator;
                CHECK(cl && cl->kind == "attrib" && cl->variants[0].name == "base" && cl->variants[0].instance.rfind("Fifa/Attribulator/", 0) == 0 && cl->family_slot &&
                          cl->group_slot && cl->l3() && (v4_loc || (!cl->verified && cl->variants[0].guid.size() == 16)),
                      k.id + ": a chain locator with its slots and instance name (a new one is unverified and has the asset GUID)");
            }
            CHECK(cands == 101, fmt("%d candidates", cands));
            struct Cand {
                const char* id;
                const char* collection;
                const char* field;
                ValueType type;
                int64_t offset;
                double value;
                double sim;  // the Authentic copy's value (-1 = the same)
            };
            const Cand cand[] = {
                {"pressing.cut_lane_reach", "gp_positioning_zonal_defense_cutpassinglanes", "MaxDistToIntercept_WithoutInstruction", ValueType::F32, 0x28, 3.5, 6.0},
                {"marking.tight_in_box", "gp_positioning_zonal_defense", "TightMarkingInBoxEnabled", ValueType::Bool, 0x6bf, 0, -1},
                {"marking.after_kickoff_period", "gp_positioning_zonal_defense", "AfterKickoffZonalMarkingPeriod", ValueType::I32, 0x634, 20, -1},
                {"goalkeeper.rush_radius", "gp_goalkeeper_goalkeeper", "GK_RushToContain_Max_Radius", ValueType::F32, 0x90, 60, -1},
                {"recovery.defenders_sprint_back", "gp_positioning_backwardspeed", "defenderSprintBackSpeedRate", ValueType::F32, 0x2c, 1.0, -1},
                {"shooting.base_speed_max", "gp_kickshot_shooting", "SHOT_BaseSpeedMax", ValueType::F32, 0x2d0, 35.75, -1},
                {"ball.linear_drag", "gp_physics_physics", "BallLinearDrag", ValueType::F32, 0x50, 0.003, -1},
                {"defending.jockey_speed", "gp_defense_jockey", "Jockey_SpeedJog", ValueType::F32, 0x78, 0.28, 0.18},
            };
            for (const Cand& x : cand) {
                const Knob* k = reg.knob(x.id);
                double s = -1;
                CHECK(k && k->candidate && k->collection == x.collection && k->field == x.field && k->type == x.type && k->offset == x.offset && k->def == x.value &&
                          (x.sim < 0 ? k->copy_defaults.empty() : (k->game_value("simulation", s) && s == x.sim)),
                      std::string(x.id) + ": FC 27's group, field, type, offset, value (and the Authentic copy's)");
            }
            const Locator* cl = reg.locator("attrib.positioning_zonal_defense_cutpassinglanes");
            CHECK(cl && cl->variants.size() == 2 && cl->variants[1].name == "simulation" && cl->variants[1].db == "Fifa/Attribulator/Gameplay/databases/simulation/simulation",
                  "a candidate's group that the Authentic database copies has both variants");
        }
        // the one copy whose FC 27 value differs: the Authentic (simulation) database has the double-marking switch off
        const Knob* dm = reg.knob("pressing.no_double_marking_own_half");
        double sim = -1, base = -1;
        CHECK(dm && dm->game_value("simulation", sim) && sim == 0 && dm->game_value("base", base) && base == 1 && dm->copy_defaults.size() == 1,
              "copy_defaults: base on, simulation off");
        const Locator* zd = reg.locator("attrib.zonal_defense");
        CHECK(zd && zd->variants.size() == 2 && zd->variants[1].name == "simulation" && zd->variants[1].db == "Fifa/Attribulator/Gameplay/databases/simulation/simulation" &&
                  zd->variants[1].instance.find("/databases/simulation/") != std::string::npos,
              "zonal_defense: base and simulation (Authentic) copies, each with its database");
        const Knob* band = reg.knob("lineband.deep_max");
        CHECK(band && band->below == std::vector<std::string>({"lineband.balanced_min"}) && band->applies == Applies::NextMatchSetup &&
                  band->group == "Defensive line depth bands",
              "the line height bands: kept below the next band, read at match setup");
        for (const char* g : {"Shape with the ball", "Shape without the ball", "Defensive line depth bands", "Pressing", "Marking", "Attacking runs"}) {
            bool has = false;
            for (const Knob& k : reg.knobs) has = has || k.group == g;
            CHECK(has, std::string("group ") + g + " has knobs");
        }
        for (const Preset& pr : builtin_presets())
            for (const PresetChange& c : pr.changes) CHECK(reg.knob(c.knob) != nullptr && c.relative, pr.name + " names knob " + c.knob + " (a relative change)");
        std::vector<uint8_t> gb;
        CHECK(guid_bytes("1852d61d-8c0d-edf7-37a7-258314497a5c", gb) &&
                  gb == std::vector<uint8_t>({0x1d, 0xd6, 0x52, 0x18, 0x0d, 0x8c, 0xf7, 0xed, 0x37, 0xa7, 0x25, 0x83, 0x14, 0x49, 0x7a, 0x5c}) &&
                  guid_text(gb) == "1852d61d-8c0d-edf7-37a7-258314497a5c" && !guid_bytes("1852d61d-8c0d", gb),
              "the asset GUID as the bytes before the instance (the live run's attacking line group)");
        TuneWorld w;
        const Lookup lk = lookup_of(w);
        const AnchorMap am = anchors_of(w.mem, reg, lk);
        for (const Knob& k : reg.knobs) {
            KnobAt at;
            const std::string why = locate_knob(w.mem, reg, k, am, lk, at);
            CHECK(why.find(k.verified ? "not located on this build (" : "not located on this build (unverified: ") == 0, k.id + ": " + why);
        }
        // the round trip through registry_json parses back to the same knobs and manager
        Registry again;
        CHECK(parse_registry(registry_json(reg), again, err) && again.knobs.size() == reg.knobs.size() && again.manager.gameplay_dbs == m.gameplay_dbs,
              "registry_json round trip: " + err);
        CHECK(knobs_file_names(kBuildKey) == std::vector<std::string>({"knobs_6AC07E31-2145C000.json", "knobs_6AC07E31.json"}), "file names");
    });

    run_case("tuning: knobs file v5 fields - candidate, rank, impact, category, scale; a candidate is never a working knob", [] {
        auto file = [](const std::string& body) {
            return std::string(R"({"schema": "turbo-tuning-knobs/1", "build": "6AC07E31-2145C000", "locators": [], "knobs": [)") + body + "]}";
        };
        const std::string base = R"("id": "a.one", "label": "One", "group": "Shape", "type": "f32", "min": 0, "max": 2, "default": 1)";
        Registry r;
        std::string err;
        CHECK(parse_registry(file("{" + base + R"(, "candidate": true, "rank": 7, "impact": "high", "category": "Pressing", "scale": "mult"})"), r, err) && r.knobs.size() == 1 &&
                  r.knobs[0].candidate && r.knobs[0].rank == 7 && r.knobs[0].impact == "high" && r.knobs[0].category == "Pressing" && r.knobs[0].scale == "mult",
              "a candidate with its ranking parses: " + err);
        CHECK(!r.knobs[0].proven() && !r.knobs[0].slider && !r.knobs[0].verified && !r.knobs[0].placeholder().empty(), "and is a placeholder");
        Registry again;
        CHECK(parse_registry(registry_json(r), again, err) && again.knobs.size() == 1 && again.knobs[0].candidate && again.knobs[0].rank == 7 &&
                  again.knobs[0].impact == "high" && again.knobs[0].category == "Pressing" && again.knobs[0].scale == "mult",
              "the ranking survives registry_json: " + err);
        Registry plain;
        const bool plain_ok = parse_registry(file("{" + base + "}"), plain, err);
        CHECK(plain_ok && !plain.knobs[0].candidate && plain.knobs[0].rank == 0 && plain.knobs[0].impact.empty() &&
                  registry_json(plain).find("\"candidate\"") == std::string::npos && registry_json(plain).find("\"rank\"") == std::string::npos,
              "a v4 knob has none of it and registry_json writes none: " + err);
        auto refused = [&](const std::string& extra, const std::string& part) {
            Registry x;
            std::string e;
            const bool ok = parse_registry(file("{" + base + extra + "}"), x, e);
            CHECK(!ok && e.find(part) != std::string::npos, "refused (" + part + "): " + e);
        };
        refused(R"(, "candidate": "yes")", "candidate must be");
        refused(R"(, "rank": 0)", "rank must be");
        refused(R"(, "rank": "first")", "rank must be");
        refused(R"(, "impact": "huge")", "impact must be");
        refused(R"(, "candidate": true, "slider": true)", "a candidate must be unverified");
        refused(R"(, "candidate": true, "live": true)", "a candidate must be unverified");
        refused(R"(, "candidate": true, "proof": {"session": "P1", "evidence": "EV/2026-10-14/TEST.md"})", "a candidate must be unverified");
    });

    run_case("tuning: a knobs file with an error is refused as a whole", [] {
        Registry r;
        std::string err;
        CHECK(parse_registry(kTestKnobs, r, err) && r.knobs.size() == 9 && r.locators.size() == 4, "the test file parses: " + err);
        auto refused = [&](const std::string& text, const std::string& part) {
            Registry x;
            std::string e;
            const bool ok = parse_registry(text, x, e);
            CHECK(!ok && e.find(part) != std::string::npos, "refused (" + part + "): " + e);
        };
        auto knob = [](const std::string& body) {
            return std::string(R"({"schema": "turbo-tuning-knobs/1", "build": "6AC07E31-2145C000", "locators": [{"name": "c", "kind": "chain", "signature": "s", "verified": true, "checks": [{"kind": "ptr"}]}], "knobs": [)") +
                   body + "]}";
        };
        refused("[]", "not a JSON object");
        refused(R"({"schema": "x", "build": "b", "knobs": []})", "schema");
        refused(R"({"schema": "turbo-tuning-knobs/1", "knobs": []})", "no build");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f64"})"), "type must be");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32"}, {"id": "a", "label": "B", "group": "Shape", "type": "f32"})"), "knob a twice");
        refused(knob(R"({"id": "A b", "label": "A", "group": "Shape", "type": "f32"})"), "knob id");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "min": 2, "max": 1})"), "min > max");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "min": 0})"), "both min and max");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "u8", "min": 0, "max": 300})"), "u8 range");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "min": 0, "max": 1, "default": 2})"), "default outside");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "when": ["kickoff"]})"), "unknown moment or phase");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "live": "yes"})"), "live must be");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "proof": {"session": "P1"}})"), "proof must be");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "applies": "sometimes"})"), "applies must be");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "locator": "nope"})"), "not in the file's locators");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "locator": "c", "min": 0, "max": 1, "collection": "x", "field": "y", "verified": true})"),
                "marked verified without");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "offset": "0xZZ"})"), "offset must be");
        // the owner rule needs FC 27's own value: a verified knob without it is refused, and so is a copy value of another locator's copy
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "locator": "c", "min": 0, "max": 1, "collection": "x", "field": "y", "offset": 0, "verified": true})"),
                "marked verified without FC 27's own value (default)");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "locator": "c", "min": 0, "max": 1, "default": 0.5, "copy_defaults": {"simulation": 0.4}})"),
                "copy_defaults names simulation, which is not a variant of its locator");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "min": 0, "max": 1, "copy_defaults": {"simulation": 4}})"),
                "copy_defaults: simulation is outside min..max");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "i32", "min": 0, "max": 10, "default": 2.5})"), "default of an integer");
        refused(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "a", "kind": "attrib", "type": "T", "head": "vtable"}], "knobs": []})",
                "head must be");
        refused(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "c", "signature": "s", "verified": true}], "knobs": []})",
                "without a header check");
        refused(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "c", "verified": true}], "knobs": []})", "without a signature");
        refused(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "c", "signature": "s", "pattern": "48", "checks": []}], "knobs": []})",
                "not both");
        refused(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "c", "signature": "s", "checks": [{"kind": "magic"}]}], "knobs": []})",
                "unknown kind");
        refused(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "c", "signature": "s", "checks": [{"kind": "ptr", "object": 3}]}], "knobs": []})",
                "object must be");
        refused(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "c", "signature": "s", "chain": [1,2,3,4,5,6,7,8,9]}], "knobs": []})",
                "at most 8");
        // load_registry: the file of this build only
        const fs::path dir = g_out / "tuning_test" / "load";
        std::error_code ec;
        fs::create_directories(dir, ec);
        Registry x;
        CHECK(!load_registry(dir, kBuildKey, x, err) && err.find("no knobs file for game build 6AC07E31-2145C000") == 0, err);
        std::string other = kTestKnobs;
        const std::string build_key = "\"build\": \"6AC07E31-2145C000\"";
        other.replace(other.find(build_key), build_key.size(), "\"build\": \"6AB9813C-211EF000\"");
        std::ofstream(dir / "knobs_6AC07E31.json", std::ios::binary | std::ios::trunc) << other;
        CHECK(!load_registry(dir, kBuildKey, x, err) && err.find("is for game build 6AB9813C-211EF000") != std::string::npos, err);
        std::ofstream(dir / "knobs_6AC07E31-2145C000.json", std::ios::binary | std::ios::trunc) << kTestKnobs;
        CHECK(load_registry(dir, kBuildKey, x, err) && x.version == 7 && x.source.find("knobs_6AC07E31-2145C000.json") != std::string::npos,
              "the full build key's file comes first: " + err);
    });

    run_case("tuning: locate - anchor, deref, chain, header checks, the inline pattern and the native locator", [] {
        TuneWorld w;
        const Registry reg = test_registry();
        const Lookup lk = lookup_of(w);
        const AnchorMap am = anchors_of(w.mem, reg, lk);
        CHECK(am.at("test_coll").ok && am.at("test_coll").addr == TuneWorld::kSlot, "signature anchor");
        CHECK(am.at("inline_coll").ok && am.at("inline_coll").addr == TuneWorld::kSlot, "inline pattern anchor (the scan)");
        CHECK(am.at("native_coll").ok, "native locator present");
        CHECK(!am.at("todo_coll").ok && am.at("todo_coll").why.find("placeholder") == 0, "placeholder locator: " + am.at("todo_coll").why);
        std::vector<Copy> bases;
        std::vector<uint64_t> objs;
        CHECK(chase(w.mem, *reg.locator("test_coll"), am.at("test_coll"), lk, bases, &objs).empty() && bases.size() == 1 && bases[0].base == TuneWorld::kBase &&
                  bases[0].variant.empty() && objs.size() == 2 &&
                  objs[0] == TuneWorld::kMgr && objs[1] == TuneWorld::kColl,
              "the chain: slot -> manager -> collection, base = collection + 0x20");
        auto live = live_of(w.mem, reg, am, lk);
        CHECK(live["pressing.intensity"].located && live["pressing.intensity"].value == double(0.5f) && live["pressing.intensity"].addr == TuneWorld::kBase,
              "f32 knob: " + live["pressing.intensity"].why);
        CHECK(live["shape.line_height"].located && live["shape.line_height"].value == 45, "i32 knob");
        CHECK(live["marking.tightness"].located && live["marking.tightness"].value == 60, "u8 knob");
        CHECK(live["runs.in_behind"].located && live["runs.in_behind"].value == 1 && live["runs.in_behind"].writable, "bool knob (range 0..1 implied)");
        CHECK(live["shooting.error"].located && live["shooting.error"].value == double(0.7f), "inline-pattern locator: " + live["shooting.error"].why);
        CHECK(live["cpuai.pass_risk"].located && live["cpuai.pass_risk"].value == double(0.4f), "native locator: " + live["cpuai.pass_risk"].why);
        CHECK(!live["gk.reaction"].located && live["gk.reaction"].why.find("not located on this build (placeholder") == 0, "placeholder: " + live["gk.reaction"].why);
    });

    run_case("tuning: refuse unresolved - every missing or wrong piece says 'not located on this build (...)'", [] {
        const Registry reg = test_registry();
        const Knob& k = *reg.knob("pressing.intensity");
        auto why_of = [&](TuneWorld& w, Lookup lk) {
            const AnchorMap am = anchors_of(w.mem, reg, lk);
            KnobAt at;
            return locate_knob(w.mem, reg, k, am, lk, at);
        };
        {
            TuneWorld w;
            Lookup lk = lookup_of(w);
            lk.signature = [](const std::string& n) -> uint64_t { return n == "tuning_test_vt" ? TuneWorld::kMgrVt : 0; };
            const std::string why = why_of(w, lk);
            CHECK(why == "not located on this build (signature tuning_test_slot was not found on this game build)", why);
        }
        {
            TuneWorld w;
            w.mem.wr(TuneWorld::kMgr, TuneWorld::kOtherVt);  // another object in the slot
            const std::string why = why_of(w, lookup_of(w));
            CHECK(why.find("not located on this build (header check: [0x21000000 + 0x0] = 0x14B300000, not tuning_test_vt 0x14B200000 (the manager))") == 0, why);
        }
        {
            TuneWorld w;
            w.mem.wr(TuneWorld::kColl + 8, uint32_t(9999));
            const std::string why = why_of(w, lookup_of(w));
            CHECK(why.find("= 9999, outside 1..64") != std::string::npos, why);
        }
        {
            TuneWorld w;
            w.mem.write(TuneWorld::kColl + 0x10, "TUNX", 4);
            const std::string why = why_of(w, lookup_of(w));
            CHECK(why.find("the bytes at") != std::string::npos && why.find("differ (54554E58)") != std::string::npos, why);
        }
        {
            TuneWorld w;
            w.mem.wr(TuneWorld::kMgr + 0x40, uint64_t(0));
            const std::string why = why_of(w, lookup_of(w));
            CHECK(why == "not located on this build (chain step 1: [0x21000040] holds no pointer)", why);
        }
        {
            TuneWorld w;
            w.mem.wr(TuneWorld::kSlot, uint64_t(0));
            const std::string why = why_of(w, lookup_of(w));
            CHECK(why == "not located on this build (the pointer slot 0x14C100000 holds no object)", why);
        }
        {
            TuneWorld w;
            w.mem.wr(TuneWorld::kBase, 7.5f);  // outside 0..1: another tool, or the layout is wrong
            const std::string why = why_of(w, lookup_of(w));
            CHECK(why.find("reads 7.5, outside Turbo's range 0..1: another tool changed it, or the layout differs on this build") != std::string::npos, why);
        }
        {
            TuneWorld w;
            Lookup lk = lookup_of(w);
            lk.image_size = 0x1000;  // the vtable signature outside FC27.exe
            const std::string why = why_of(w, lk);
            CHECK(why.find("signature tuning_test_vt is outside FC27.exe") != std::string::npos, why);
        }
        {
            TuneWorld w;
            Lookup lk = lookup_of(w);
            lk.scan = nullptr;
            lk.natives.clear();
            const AnchorMap am = anchors_of(w.mem, reg, lk);
            CHECK(!am.at("inline_coll").ok && am.at("inline_coll").why.find("needs the game's code scan") != std::string::npos, am.at("inline_coll").why);
            CHECK(!am.at("native_coll").ok && am.at("native_coll").why == "native locator native_coll is not in this Turbo", am.at("native_coll").why);
            lk.scan = [](const Signature& s) {
                SigResult r;
                r.name = s.name;
                r.state = SigState::Ambiguous;
                r.error = "2 matches";
                return r;
            };
            const Anchor a = resolve_anchor(w.mem, *reg.locator("inline_coll"), lk);
            CHECK(!a.ok && a.why == "its pattern is ambiguous (2 matches) in FC27.exe", a.why);
        }
        {
            // a locator not resolved yet / still being scanned
            TuneWorld w;
            const Lookup lk = lookup_of(w);
            AnchorMap am;
            KnobAt at;
            CHECK(locate_knob(w.mem, reg, k, am, lk, at) == "not located on this build (locator test_coll is not resolved yet)", "not resolved yet");
            am["test_coll"].pending = true;
            CHECK(locate_knob(w.mem, reg, k, am, lk, at) == "not located on this build (locator test_coll: searching ...)", "searching");
        }
    });

    run_case("tuning: values and ranges - types, whole numbers, switches, f32 bits, the plan refuses what the game must not get", [] {
        const Registry reg = test_registry();
        const Knob& f = *reg.knob("pressing.intensity");
        const Knob& i = *reg.knob("shape.line_height");
        const Knob& u = *reg.knob("marking.tightness");
        const Knob& b = *reg.knob("runs.in_behind");
        const Knob& ph = *reg.knob("gk.reaction");
        CHECK(check_value(f, 0.75).empty() && check_value(f, 0).empty() && check_value(f, 1).empty(), "f32 inside");
        CHECK(check_value(f, 1.01) == "Pressing intensity: 1.01 is outside 0..1", check_value(f, 1.01));
        CHECK(check_value(f, std::nan("")) == "Pressing intensity: not a number", "NaN");
        CHECK(check_value(i, 50.5).find("not a whole number") != std::string::npos && check_value(i, -1).find("outside 0..100") != std::string::npos, "i32");
        CHECK(check_value(u, 101).find("outside") != std::string::npos && check_value(u, 100).empty(), "u8");
        CHECK(check_value(b, 2) == "Runs in behind the defence: a switch is 0 or 1" && check_value(b, 0).empty(), "bool");
        CHECK(check_value(ph, 0.5).find("no value range") != std::string::npos, "a knob without a range is read-only");
        CHECK(representable(f, 0.1) == double(0.1f) && representable(i, 49.6) == 50 && representable(b, 0.7) == 1, "representable");
        CHECK(same_value(ValueType::F32, 0.1, double(0.1f)) && !same_value(ValueType::F32, 0.1, 0.1000001) && same_value(ValueType::I32, 4, 4.0), "same_value");
        CHECK(format_value(f, 0.5) == "0.50" && format_value(i, 45) == "45 %" && format_value(b, 1) == "on" && format_value(b, 0) == "off",
              "format: " + format_value(f, 0.5) + " / " + format_value(i, 45));
        TuneWorld w;
        const Lookup lk = lookup_of(w);
        const AnchorMap am = anchors_of(w.mem, reg, lk);
        const auto live = live_of(w.mem, reg, am, lk);
        Plan p;
        CHECK(make_plan(reg, live, {{"pressing.intensity", 1.5}}, PlanKind::Apply, p) == "Pressing intensity: 1.5 is outside 0..1", "out of range");
        CHECK(make_plan(reg, live, {{"gk.reaction", 0.5}}, PlanKind::Apply, p).find("Goalkeeper reaction time: not located on this build") == 0, "placeholder");
        CHECK(make_plan(reg, live, {{"nope", 0.5}}, PlanKind::Apply, p) == "knob nope is not in the knobs file", "unknown knob");
        CHECK(make_plan(reg, live, {{"shape.line_height", 45}, {"pressing.intensity", 0.5}}, PlanKind::Apply, p).empty() && p.writes.empty(),
              "unchanged values: an empty plan");
        CHECK(make_plan(reg, live, {{"shape.line_height", 70}, {"pressing.intensity", 0.6}}, PlanKind::Apply, p).empty() && p.writes.size() == 2 &&
                  p.writes[0].id == "pressing.intensity" && p.writes[0].old_value == 0.5 && p.writes[0].new_value == double(0.6f) &&
                  p.writes[1].id == "shape.line_height" && p.writes[1].new_value == 70,
              "the plan, in the registry's order, with representable values");
    });

    run_case("tuning: the job - every knob together, read back, the snapshot; a moved table is re-chased; changed values refuse", [] {
        TuneWorld w;
        const Registry reg = test_registry();
        const Lookup lk = lookup_of(w);
        const AnchorMap am = anchors_of(w.mem, reg, lk);
        Plan p;
        auto live = live_of(w.mem, reg, am, lk);
        CHECK(make_plan(reg, live, {{"pressing.intensity", 0.8}, {"shape.line_height", 70}, {"marking.tightness", 80}, {"runs.in_behind", 0},
                                    {"shooting.error", 1.2}, {"cpuai.pass_risk", 0.1}},
                        PlanKind::Apply, p)
                      .empty() &&
                  p.writes.size() == 6,
              "six knobs, four types, three locators");
        ApplyResult r = apply_plan(w.mem, reg, am, lk, p);
        CHECK(r.ok && r.written && r.stage == "done", "written: " + r.message);
        CHECK(w.f32(TuneWorld::kBase) == 0.8f && w.i32(TuneWorld::kBase + 4) == 70 && w.u8(TuneWorld::kBase + 8) == 80 && w.u8(TuneWorld::kBase + 9) == 0 &&
                  w.f32(TuneWorld::kBase + 0x14) == 1.2f && w.f32(TuneWorld::kBase + 0x18) == 0.1f,
              "in memory");
        CHECK(w.f32(TuneWorld::kBase + 0x0C) == 0.2f && w.f32(TuneWorld::kBase + 0x10) == 0.3f && w.f32(TuneWorld::kColl2 + 0x20) == 0.5f, "nothing else touched");
        CHECK(r.snapshot.size() == 6 && r.snapshot["pressing.intensity"] == 0.5 && r.snapshot["shape.line_height"] == 45, "the snapshot");
        CHECK(r.log_line.find("gameplay tuning apply: Pressing intensity 0.50 -> 0.80, Defensive line height 45 % -> 70 %") == 0 &&
                  r.log_line.find("(read back ok)") != std::string::npos,
              r.log_line);
        CHECK(r.reads > 0, "reads counted");
        // the manager now points at the other copy (re-allocated after Apply was pressed): the job writes into the new place
        live = live_of(w.mem, reg, am, lk);
        CHECK(make_plan(reg, live, {{"passing.error_lofted", 0.9}}, PlanKind::Apply, p).empty() && p.writes.size() == 1, "plan on the first copy");
        w.mem.wr(TuneWorld::kMgr + 0x40, TuneWorld::kColl2);
        w.mem.wr(TuneWorld::kColl2 + 0x20 + 0x10, 0.3f);
        r = apply_plan(w.mem, reg, am, lk, p);
        CHECK(r.ok && w.f32(TuneWorld::kColl2 + 0x30) == 0.9f && w.f32(TuneWorld::kBase + 0x10) == 0.3f, "re-chased: the new copy written, the old left alone");
        // a value changed since Apply was pressed: nothing written
        live = live_of(w.mem, reg, am, lk);
        CHECK(make_plan(reg, live, {{"pressing.intensity", 0.9}, {"shape.line_height", 10}}, PlanKind::Apply, p).empty(), "plan");
        w.mem.wr(TuneWorld::kColl2 + 0x24, int32_t(44));
        r = apply_plan(w.mem, reg, am, lk, p);
        CHECK(!r.ok && r.stage == "changed" && r.message.find("Defensive line height is 44 %, not 45 %") == 0 && w.f32(TuneWorld::kColl2 + 0x20) == 0.5f,
              "changed: " + r.message);
        // G5 again in the job (a plan made by hand)
        Plan bad;
        bad.writes = {{"pressing.intensity", 0.5, 3.0}};
        r = apply_plan(w.mem, reg, am, lk, bad);
        CHECK(!r.ok && r.stage == "G5" && w.f32(TuneWorld::kColl2 + 0x20) == 0.5f, "range checked in the job: " + r.message);
        bad.writes = {{"gk.reaction", 0, 1}};
        r = apply_plan(w.mem, reg, am, lk, bad);
        CHECK(!r.ok && r.stage == "locate" && r.message.find("not located on this build (placeholder") != std::string::npos, "placeholder in a plan: " + r.message);
        bad.writes = {{"pressing.intensity", 0.5, 0.6}, {"pressing.intensity", 0.5, 0.7}};
        r = apply_plan(w.mem, reg, am, lk, bad);
        CHECK(!r.ok && r.stage == "plan", "a knob twice: " + r.message);
        std::atomic<bool> cancel{true};
        bad.writes = {{"pressing.intensity", 0.5, 0.6}};
        JobOptions jo;
        jo.cancel = &cancel;
        r = apply_plan(w.mem, reg, am, lk, bad, jo);
        CHECK(!r.ok && r.stage == "cancelled" && w.f32(TuneWorld::kColl2 + 0x20) == 0.5f, "cancelled: nothing written");
        Plan none;
        r = apply_plan(w.mem, reg, am, lk, none);
        CHECK(r.ok && !r.written && r.message == "nothing to change", "an empty plan");
    });

    run_case("tuning: partial-write rollback - a dropped or refused write puts EVERY knob back, tuning off for the session", [] {
        TuneWorld w;
        const Registry reg = test_registry();
        const Lookup lk = lookup_of(w);
        const AnchorMap am = anchors_of(w.mem, reg, lk);
        const auto live = live_of(w.mem, reg, am, lk);
        Plan p;
        CHECK(make_plan(reg, live, {{"pressing.intensity", 0.8}, {"shape.line_height", 70}, {"marking.tightness", 80}}, PlanKind::Apply, p).empty(), "plan");
        DropMemory dm(w.mem);
        dm.drop = TuneWorld::kBase + 4;  // the line height write is dropped (reported fine): its read-back shows the old value
        ApplyResult r = apply_plan(dm, reg, am, lk, p);
        CHECK(!r.ok && r.stage == "readback" && r.undone && !r.restore_failed && r.session_off, "read-back mismatch: " + r.message);
        CHECK(r.message.find("Defensive line height reads 45 %, not 70 %") != std::string::npos && r.message.find("every knob was put back") != std::string::npos,
              r.message);
        CHECK(w.f32(TuneWorld::kBase) == 0.5f && w.i32(TuneWorld::kBase + 4) == 45 && w.u8(TuneWorld::kBase + 8) == 60, "every knob back from the snapshot");
        CHECK(r.log_line.find("READ-BACK MISMATCH") != std::string::npos && r.log_line.find("undone from the snapshot") != std::string::npos, r.log_line);
        // a write the memory layer refuses
        dm.refuse = true;
        r = apply_plan(dm, reg, am, lk, p);
        CHECK(!r.ok && r.stage == "readback" && r.undone && w.f32(TuneWorld::kBase) == 0.5f && w.u8(TuneWorld::kBase + 8) == 60, "refused write: " + r.message);
        // putting back fails too (the dropped address keeps Turbo's value? no: it never held it; the first knob is dropped on the way back)
        struct BackFails : Memory {
            SimMemory& m;
            int writes = 0;
            explicit BackFails(SimMemory& mm) : m(mm) {}
            bool read(uint64_t a, void* o, size_t n) override {
                if (a == TuneWorld::kBase + 8 && writes >= 3) {  // after the three writes the u8 reads wrong
                    *static_cast<uint8_t*>(o) = 1;
                    return true;
                }
                return m.read(a, o, n);
            }
            bool write(uint64_t a, const void* i, size_t n) override {
                ++writes;
                return m.write(a, i, n);
            }
        } bf(w.mem);
        r = apply_plan(bf, reg, am, lk, p);
        CHECK(!r.ok && r.restore_failed && !r.undone && r.message.find("PUTTING BACK FAILED") != std::string::npos, "the restore read back wrong: " + r.message);
    });

    run_case("tuning: the memory search helpers - NUL-terminated names in any letter case, 8-aligned pointers", [] {
        const char text[] = "xxFIFA/A\0Fifa/A\0fifa/ab\0";
        const auto s = find_cstrings(reinterpret_cast<const uint8_t*>(text), sizeof(text), 0x1000, {"fifa/a"}, 10);
        CHECK(s.size() == 2 && s[0].second == 0x1002 && s[1].second == 0x1009, fmt("two exact names, letter case ignored (%zu)", s.size()));
        CHECK(find_cstrings(reinterpret_cast<const uint8_t*>(text), sizeof(text), 0x1000, {"fifa/a"}, 1).size() == 1, "max hits");
        uint64_t q[4] = {0x1002, 0x55, 0x1009, 0x1002};
        const auto p = find_qwords(reinterpret_cast<const uint8_t*>(q), sizeof(q), 0x2000, {0x1002, 0x1009}, 10);
        CHECK(p.size() == 3 && p[0].second == 0x2000 && p[1].second == 0x2010 && p[2].second == 0x2018, "the slots");
        CHECK(find_qwords(reinterpret_cast<const uint8_t*>(q) + 4, sizeof(q) - 4, 0x2004, {0x1002}, 10).empty() ||
                  find_qwords(reinterpret_cast<const uint8_t*>(q) + 4, sizeof(q) - 4, 0x2004, {0x1002}, 10)[0].second % 8 == 0,
              "only 8-aligned slots");
        CHECK(same_name("Fifa/X", "fifa/x") && !same_name("Fifa/X", "fifa/y") && !same_name("a", "ab"), "same_name");
    });

    run_case("tuning: the attrib locator - type and field names, every database copy written together, refusals, re-checks, the owner rule", [] {
        AttribWorld w;
        Registry reg;
        std::string err;
        CHECK(parse_registry(kAttribKnobs, reg, err), "parses: " + err);
        const Lookup lk = w.lookup();
        const AnchorMap am = anchors_of(w.mem, reg, lk);
        const Anchor& a = am.at("attrib.test");
        CHECK(a.ok && a.copies.size() == 2 && a.copies[0].variant == "base" && a.copies[0].base == AttribWorld::kBase && a.copies[1].base == AttribWorld::kSim &&
                  a.type_ptr == AttribWorld::kTypePtr,
              "the type by name, one instance per variant (the decoy without an image pointer left out): " + a.why);
        CHECK(a.layout.name == "AttribSchema_test_group" && a.layout.size == 0x40 && a.layout.fields == 3 && a.layout.field.at("count").offset == 0x2c &&
                  a.layout.field.at("count").type == "Int32",
              "the type data");
        auto live = live_of(w.mem, reg, am, lk);
        const KnobLive& sp = live["shape.speed"];
        CHECK(sp.located && sp.copies.size() == 2 && sp.copies[0].value == double(0.75f) && sp.copies[1].value == double(0.8f) && sp.value == double(0.75f),
              "two copies, each its own value: " + sp.why);
        CHECK(live["shape.count"].located && live["shape.count"].value == 5 && live["shape.enabled"].located, "i32 / bool fields");
        CHECK(live["shape.wrong_offset"].why == "not located on this build (the knobs file puts speedModifier at 0x2C, the game's type data at 0x28)",
              live["shape.wrong_offset"].why);
        CHECK(live["shape.missing"].why == "not located on this build (field nope is not in type AttribSchema_test_group on this build)", live["shape.missing"].why);
        CHECK(live["shape.wrong_type"].why == "not located on this build (field speedModifier is Float32 in the game, the knobs file says i32)",
              live["shape.wrong_type"].why);
        // one knob, both copies, one job
        Plan p;
        CHECK(make_plan(reg, live, {{"shape.speed", 1.0}}, PlanKind::Apply, p).empty() && p.writes.size() == 2 && p.writes[0].copy == "base" &&
                  p.writes[0].old_value == double(0.75f) && p.writes[1].copy == "simulation" && p.writes[1].old_value == double(0.8f),
              "the plan writes every copy");
        ApplyResult r = apply_plan(w.mem, reg, am, lk, p);
        CHECK(r.ok && w.f32(AttribWorld::kBase + 0x28) == 1.0f && w.f32(AttribWorld::kSim + 0x28) == 1.0f, "both copies written: " + r.message);
        CHECK(r.snapshot.size() == 2 && r.snapshot["shape.speed@base"] == double(0.75f) && r.snapshot["shape.speed@simulation"] == double(0.8f), "snapshot by copy");
        CHECK(r.log_line.find("Speed [base] 0.750 -> 1.000, Speed [simulation] 0.800 -> 1.000") != std::string::npos, r.log_line);
        // the undo puts each copy back to ITS old value
        UndoRecord u;
        u.session = "s";
        u.load_gen = 1;
        u.state = "written";
        u.knobs = r.writes;
        UndoRecord back;
        CHECK(UndoRecord::parse(u.to_json(), back, &err) && back.knobs.size() == 2 && back.knobs[1].copy == "simulation", "undo JSON keeps the copy");
        live = live_of(w.mem, reg, am, lk);
        std::vector<std::string> skipped;
        CHECK(make_undo_plan(reg, live, back, "s", 1, p, skipped).empty() && p.writes.size() == 2, "undo plan");
        r = apply_plan(w.mem, reg, am, lk, p);
        CHECK(r.ok && w.f32(AttribWorld::kBase + 0x28) == 0.75f && w.f32(AttribWorld::kSim + 0x28) == 0.8f, "each copy back to its own value");
        // Restore game defaults by copy
        w.mem.wr(AttribWorld::kSim + 0x28, 1.5f);
        live = live_of(w.mem, reg, am, lk);
        CHECK(make_restore_plan(reg, live, {{"shape.speed@base", 0.75}, {"shape.speed@simulation", 0.8}, {"shape.gone@base", 1}}, p, skipped).empty() &&
                  p.writes.size() == 2 && p.writes[0].copy == "base" && p.writes[0].new_value == 0.75 && p.writes[1].copy == "simulation" &&
                  p.writes[1].new_value == double(float(0.8)) && has_text(skipped, "shape.gone@base is not in the knobs file"),
              "restore: one copy differs, both copies go back together");
        CHECK(make_restore_plan(reg, live, {{"shape.speed@simulation", 0.8}}, p, skipped).empty() && p.writes.empty() &&
                  has_text(skipped, "Speed [base]: no game default recorded for this copy"),
              "restore: a knob without the default of every copy is left alone");
        CHECK(make_restore_plan(reg, live, {{"shape.speed@base", 0.75}, {"shape.speed@simulation", 0.8}, {"shape.count@base", 1}, {"shape.count@simulation", 1}},
                                p, skipped, "shape.speed")
                      .empty() &&
                  p.writes.size() == 2 && p.writes[0].id == "shape.speed",
              "restore of one knob (the row's Reset)");
        // a partial write across copies: the simulation copy's write is dropped -> both copies back
        live = live_of(w.mem, reg, am, lk);
        CHECK(make_plan(reg, live, {{"shape.speed", 1.25}}, PlanKind::Apply, p).empty() && p.writes.size() == 2, "plan");
        DropMemory dm(w.mem);
        dm.drop = AttribWorld::kSim + 0x28;
        r = apply_plan(dm, reg, am, lk, p);
        CHECK(!r.ok && r.undone && w.f32(AttribWorld::kBase + 0x28) == 0.75f && w.f32(AttribWorld::kSim + 0x28) == 1.5f, "rollback across copies: " + r.message);
        // the owner rule: a value that is neither FC 27's nor Turbo's last write is someone else's
        w.mem.wr(AttribWorld::kBase + 0x2c, int32_t(7));
        live = live_of(w.mem, reg, am, lk);
        CHECK(make_plan(reg, live, {{"shape.count", 8}}, PlanKind::Apply, p).empty(), "plan");
        JobOptions jo;
        jo.owner_check = true;
        std::map<std::string, double> last;
        jo.last_written = &last;
        r = apply_plan(w.mem, reg, am, lk, p, jo);
        CHECK(!r.ok && r.stage == "owner" && r.message.find("neither FC 27's 5 nor Turbo's last write") != std::string::npos,
              "owner: " + r.message);
        last["shape.count@base"] = 7;
        r = apply_plan(w.mem, reg, am, lk, p, jo);
        CHECK(r.ok, "Turbo's last write is Turbo's (base), FC 27's own value (simulation): " + r.message);
        // COPIES GO TOGETHER: a value for one copy only is refused, a value per copy is fine, a copy that already holds the value is
        // written with it; a hand-made plan that names one copy of two is refused by the job before anything is written
        live = live_of(w.mem, reg, am, lk);
        CHECK(make_plan(reg, live, {{"shape.speed@base", 1.0}}, PlanKind::Apply, p) ==
                  "Speed: no value for its simulation copy (every copy of a knob is written together)",
              "one copy only: refused");
        CHECK(make_plan(reg, live, {{"shape.speed@base", 1.0}, {"shape.speed@simulation", 1.1}}, PlanKind::Apply, p).empty() && p.writes.size() == 2 &&
                  p.writes[1].copy == "simulation" && p.writes[1].new_value == double(1.1f),
              "a value per copy");
        CHECK(make_plan(reg, live, {{"shape.speed", 1.0}, {"shape.speed@simulation", 1.1}}, PlanKind::Apply, p).find("not both") != std::string::npos, "both forms");
        CHECK(make_plan(reg, live, {{"shape.speed@smallsided", 1.0}}, PlanKind::Apply, p) == "Speed: there is no copy \"smallsided\" on this build", "no such copy");
        CHECK(make_plan(reg, live, {{"shape.speed", 0.75}}, PlanKind::Apply, p).empty() && p.writes.size() == 2 && p.writes[0].old_value == double(0.75f) &&
                  p.writes[0].new_value == double(0.75f) && p.writes[1].old_value == double(1.5f),
              "base already holds 0.75: written with it, together with the simulation copy");
        Plan one = p;
        one.writes.resize(1);
        {
            DropMemory counted(w.mem);
            r = apply_plan(counted, reg, am, lk, one);
            CHECK(!r.ok && r.stage == "plan" && r.message == "Speed: the plan writes 1 of its 2 copies; every copy of a knob is written together: nothing written" &&
                      counted.writes == 0,
                  "one copy of two: nothing written: " + r.message);
            // VALUES IN RANGE: a hand-made plan with a value outside the knob's range is refused in the job (G5), nothing written
            Plan big;
            big.writes = {{"shape.speed", double(0.75f), 5.0, "base"}, {"shape.speed", double(1.5f), 5.0, "simulation"}};
            r = apply_plan(counted, reg, am, lk, big);
            CHECK(!r.ok && r.stage == "G5" && r.message.find("Speed: 5 is outside 0..2") == 0 && counted.writes == 0, "out of range: " + r.message);
            // NOT VERIFIED: the same knob marked unverified is never located, so the job writes nothing
            Registry unverified = reg;
            for (Knob& k : unverified.knobs) k.verified = false;
            r = apply_plan(counted, unverified, am, lk, p);
            CHECK(!r.ok && r.stage == "locate" && r.message.find("not located on this build (unverified: ") != std::string::npos && counted.writes == 0,
                  "unverified: " + r.message);
            Registry unverified_loc = reg;
            unverified_loc.locators[0].verified = false;
            r = apply_plan(counted, unverified_loc, am, lk, p);
            CHECK(!r.ok && r.stage == "locate" && r.message.find("has not been confirmed in the running game yet") != std::string::npos && counted.writes == 0,
                  "unverified locator: " + r.message);
        }
        // FC 27's own value per copy (copy_defaults): the Authentic copy of a switch that is off in FC 27
        {
            Registry r2 = reg;
            for (Knob& k : r2.knobs)
                if (k.id == "shape.enabled") k.copy_defaults["simulation"] = 0;
            w.mem.wr(AttribWorld::kSim + 0x30, uint8_t(0));
            auto l2 = live_of(w.mem, r2, am, lk);
            CHECK(make_plan(r2, l2, {{"shape.enabled", 0}}, PlanKind::Apply, p).empty() && p.writes.size() == 2, "plan: switch off in both copies");
            std::map<std::string, double> last2;
            JobOptions j2;
            j2.owner_check = true;
            j2.last_written = &last2;
            r = apply_plan(w.mem, r2, am, lk, p, j2);
            CHECK(r.ok, "base holds FC 27's on, simulation FC 27's off: both FC 27's own: " + r.message);
            last2["shape.enabled@base"] = 0;
            last2["shape.enabled@simulation"] = 0;
            w.mem.wr(AttribWorld::kSim + 0x30, uint8_t(1));  // another tool switches the Authentic copy on
            l2 = live_of(w.mem, r2, am, lk);
            CHECK(make_plan(r2, l2, {{"shape.enabled", 1}}, PlanKind::Apply, p).empty(), "plan");
            r = apply_plan(w.mem, r2, am, lk, p, j2);
            CHECK(!r.ok && r.stage == "owner" && r.message == "Enabled [simulation] is on, neither FC 27's off nor Turbo's last write: another tool or mod changed it; "
                                                              "nothing written",
                  "owner by copy: " + r.message);
            uint8_t b0 = 9;
            w.mem.rd(AttribWorld::kBase + 0x30, b0);
            CHECK(b0 == 0, "the base copy was not written either");
            // a knob whose FC 27 value is not known: only Turbo's own last write may be written over
            Registry r3 = reg;
            for (Knob& k : r3.knobs)
                if (k.id == "shape.speed") k.has_default = false;
            auto l3 = live_of(w.mem, r3, am, lk);
            CHECK(make_plan(r3, l3, {{"shape.speed", 1.0}}, PlanKind::Apply, p).empty(), "plan");
            r = apply_plan(w.mem, r3, am, lk, p, j2);
            CHECK(!r.ok && r.stage == "owner" && r.message.find("FC 27's own value of it is not in the knobs file") != std::string::npos, "unknown: " + r.message);
            w.mem.wr(AttribWorld::kBase + 0x30, uint8_t(1));
            w.mem.wr(AttribWorld::kSim + 0x30, uint8_t(1));
        }
        // the instance head (the live run: +0 the vtable, +8 the TypeInfo): "typeinfo" and head_rva keep only instances of the type
        {
            AttribWorld wh;
            Locator l = *reg.locator("attrib.test");
            l.head = "typeinfo";
            const Anchor h1 = resolve_anchor(wh.mem, l, wh.lookup());
            CHECK(h1.ok && h1.td == AttribWorld::kTd && h1.copies.size() == 2, "head typeinfo: " + h1.why);
            l.head_rva = AttribWorld::kTypePtr - AttribWorld::kImage;
            CHECK(resolve_anchor(wh.mem, l, wh.lookup()).ok, "head_rva");
            l.head_rva += 8;
            const Anchor h2 = resolve_anchor(wh.mem, l, wh.lookup());
            CHECK(!h2.ok && h2.why == "variant base: 1 object(s) carry its name, none with the type's head (vtable FC27.exe+0xD005008)", h2.why);
            l.head_rva = 0;
            wh.mem.wr(AttribWorld::kTiG, uint64_t(AttribWorld::kTdF));  // the TypeInfo at +8 now describes another type
            const Anchor h3 = resolve_anchor(wh.mem, l, wh.lookup());
            CHECK(!h3.ok && h3.why == "variant base: 1 object(s) carry its name, none with the type's head (its TypeInfo at +8)", h3.why);
            // a TypeInfo changed after the search: every locate re-checks it
            wh.mem.wr(AttribWorld::kTiG, uint64_t(AttribWorld::kTd));
            AnchorMap hm;
            Registry rh = reg;
            for (Locator& x : rh.locators) x.head = "typeinfo";
            hm["attrib.test"] = resolve_anchor(wh.mem, rh.locators[0], wh.lookup());
            wh.mem.wr(AttribWorld::kSim + 8, uint64_t(AttribWorld::kTiF));
            KnobAt at;
            const std::string why = locate_knob(wh.mem, rh, *rh.knob("shape.speed"), hm, wh.lookup(), at);
            CHECK(why.find("variant simulation: the instance at 0x24001000 no longer holds its TypeInfo: locate again") != std::string::npos, why);
        }
        // re-checks inside every locate: the instance freed (another object type) or renamed
        w.mem.wr(AttribWorld::kSim, uint64_t(0x14D009000ULL));
        live = live_of(w.mem, reg, am, lk);
        CHECK(live["shape.speed"].why.find("variant simulation: the instance at 0x24001000 no longer holds its object type") != std::string::npos &&
                  live["shape.speed"].why.find("locate again") != std::string::npos,
              live["shape.speed"].why);
        w.mem.wr(AttribWorld::kSim, AttribWorld::kTypePtr);
        w.mem.write(AttribWorld::kNames + 0x100, "X", 1);
        live = live_of(w.mem, reg, am, lk);
        CHECK(live["shape.speed"].why.find("is no longer named") != std::string::npos, live["shape.speed"].why);
        w.mem.write(AttribWorld::kNames + 0x100, "F", 1);
        // refusals at the search: the type's signature differs (layout changed), a variant's instance missing, no search at all
        {
            AttribWorld w2(0x0BADF00D);
            const Lookup lk2 = w2.lookup();
            const Anchor b = resolve_anchor(w2.mem, *reg.locator("attrib.test"), lk2);
            CHECK(!b.ok && b.why == "type AttribSchema_test_group has signature 0xBADF00D, not 0x12345678: the layout may have changed on this build", b.why);
            AttribWorld w3;
            w3.mem.wr(AttribWorld::kSim + 0x18, uint64_t(0));
            const Anchor c = resolve_anchor(w3.mem, *reg.locator("attrib.test"), w3.lookup());
            CHECK(!c.ok && c.why == "variant simulation: its instance is not in memory (the attribulator not loaded yet?)", c.why);
            Lookup none = w3.lookup();
            none.refs = nullptr;
            CHECK(resolve_anchor(w3.mem, *reg.locator("attrib.test"), none).why.find("needs the game's memory search") != std::string::npos, "no search");
        }
        // the file: an attrib locator marked verified needs its type check and every instance name
        Registry x;
        CHECK(!parse_registry(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "a", "kind": "attrib", "type": "T", "verified": true,
              "variants": [{"name": "base", "instance": "n"}]}], "knobs": []})", x, err) && err.find("needs type, type_signature") != std::string::npos, err);
        CHECK(!parse_registry(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "a", "kind": "attrib", "type": "T", "type_signature": "0x1",
              "type_size": "0x10", "type_fields": 1, "verified": true, "variants": [{"name": "base", "instance": "n"}, {"name": "simulation", "instance": ""}]}],
              "knobs": []})", x, err) && err.find("variant simulation has no instance name") != std::string::npos, err);
        CHECK(parse_registry(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "a", "kind": "attrib", "type": "T",
              "variants": [{"name": "base", "instance": "n"}, {"name": "simulation", "instance": ""}]}], "knobs": []})", x, err) &&
                  x.locators[0].placeholder().find("placeholder") == 0,
              "unverified: fine, and a placeholder");
    });

    run_case("tuning: the attrib locator through the database chain (L3) - the instance the game uses, not a twin with its name; re-checked in every job", [] {
        AttribWorld w(0x12345678, true);  // with a twin: a second object with the base instance's name, vtable and TypeInfo
        Registry l2, l3;
        std::string err;
        CHECK(parse_registry(kAttribKnobs, l2, err) && parse_registry(kAttribL3Knobs, l3, err), "parse: " + err);
        const Lookup lk = w.lookup();
        // by name only (L2) the twin makes the base copy ambiguous: refused, never guessed
        const Anchor by_name = resolve_anchor(w.mem, *l2.locator("attrib.test"), lk);
        CHECK(!by_name.ok && by_name.why == "variant base: 2 instances carry its name", by_name.why);
        // through the database chain (L3): the database objects by name and TypeInfo (the decoy of another type left out), the group
        // objects (tag bit 2 masked), the instances the databases point at
        AnchorMap am;
        am["attrib.l3"] = resolve_anchor(w.mem, *l3.locator("attrib.l3"), lk);
        const Anchor& a = am["attrib.l3"];
        CHECK(a.ok && a.copies.size() == 2 && a.copies[0].base == AttribWorld::kBase && a.copies[0].db == AttribWorld::kDbBase &&
                  a.copies[1].base == AttribWorld::kSim && a.copies[1].db == AttribWorld::kDbSim && a.db_td == AttribWorld::kDbTd &&
                  a.group_td == AttribWorld::kGrpTd,
              "L3: " + a.why);
        auto live = live_of(w.mem, l3, am, lk);
        CHECK(live["shape.speed"].located && live["shape.speed"].copies.size() == 2, "located: " + live["shape.speed"].why);
        Plan p;
        CHECK(make_plan(l3, live, {{"shape.speed", 1.2}}, PlanKind::Apply, p).empty() && p.writes.size() == 2, "plan");
        std::map<std::string, double> last;
        JobOptions jo;
        jo.owner_check = true;
        jo.last_written = &last;
        ApplyResult r = apply_plan(w.mem, l3, am, lk, p, jo);
        CHECK(r.ok && w.f32(AttribWorld::kBase + 0x28) == 1.2f && w.f32(AttribWorld::kSim + 0x28) == 1.2f && w.f32(AttribWorld::kTwin + 0x28) == 0.75f,
              "both copies the databases use written, the twin untouched: " + r.message);
        // every job re-checks the chain: a database that points at another instance, a database object freed, a group of another type
        w.mem.wr(AttribWorld::kGrpBase + 0x28, AttribWorld::kTwin);
        KnobAt at;
        std::string why = locate_knob(w.mem, l3, *l3.knob("shape.speed"), am, lk, at);
        CHECK(why.find("variant base: its database now points at another instance (0x24205000, was 0x24000000): locate again") != std::string::npos, why);
        w.mem.wr(AttribWorld::kGrpBase + 0x28, AttribWorld::kBase);
        w.mem.wr(AttribWorld::kDbSim + 8, AttribWorld::kTiF);
        why = locate_knob(w.mem, l3, *l3.knob("shape.speed"), am, lk, at);
        CHECK(why.find("variant simulation: the database object at 0x24201000 is no longer Fifa/Attribulator/Gameplay/databases/simulation/simulation: "
                       "locate again") != std::string::npos,
              why);
        live = live_of(w.mem, l3, am, lk);
        CHECK(make_plan(l3, live, {{"shape.speed", 1.0}}, PlanKind::Apply, p).find("not located on this build") != std::string::npos, "nothing planned");
        w.mem.wr(AttribWorld::kDbSim + 8, AttribWorld::kDbTi);
        w.mem.wr(AttribWorld::kGrpSim + 8, AttribWorld::kTiF);
        why = locate_knob(w.mem, l3, *l3.knob("shape.speed"), am, lk, at);
        CHECK(why.find("the group object 0x24203000 is not a AttribGroupRuntime_test") != std::string::npos, why);
        const Anchor g = resolve_anchor(w.mem, *l3.locator("attrib.l3"), lk);
        CHECK(!g.ok && g.why.find("variant simulation: the group object 0x24203000 is not a AttribGroupRuntime_test") == 0, g.why);
        w.mem.wr(AttribWorld::kGrpSim + 8, AttribWorld::kGrpTi);
        // refusals at the search: the database type's signature, a database not loaded, an instance of another name
        {
            Locator l = *l3.locator("attrib.l3");
            l.db_type_signature = 0x1;
            CHECK(resolve_anchor(w.mem, l, lk).why == "type AttribDBRuntime_test has signature 0xA5106425, not 0x1: the layout may have changed on this build",
                  resolve_anchor(w.mem, l, lk).why);
            l = *l3.locator("attrib.l3");
            l.variants[1].db = "Fifa/Attribulator/Gameplay/databases/smallsided/smallsided";
            CHECK(resolve_anchor(w.mem, l, lk).why.find("variant simulation: its database Fifa/Attribulator/Gameplay/databases/smallsided/smallsided "
                                                        "(AttribDBRuntime_test) is not in memory") == 0,
                  resolve_anchor(w.mem, l, lk).why);
            l = *l3.locator("attrib.l3");
            l.variants[1].instance = "Fifa/Attribulator/Gameplay/groups/test/other_runtime";
            CHECK(resolve_anchor(w.mem, l, lk).why.find("variant simulation: the instance 0x24001000 its database points at is not named") == 0,
                  resolve_anchor(w.mem, l, lk).why);
        }
        // the asset GUID in the 16 bytes before the instance: checked at the search and in every job
        {
            const std::vector<uint8_t> g16 = {0x1d, 0xd6, 0x52, 0x18, 0x0d, 0x8c, 0xf7, 0xed, 0x37, 0xa7, 0x25, 0x83, 0x14, 0x49, 0x7a, 0x5c};
            w.mem.map(AttribWorld::kBase - 0x10, 0x10);
            w.mem.write(AttribWorld::kBase - 0x10, g16.data(), g16.size());
            Registry lg = l3;
            lg.locators[0].variants[0].guid = g16;
            AnchorMap gm;
            gm["attrib.l3"] = resolve_anchor(w.mem, lg.locators[0], lk);
            CHECK(gm["attrib.l3"].ok, "guid: " + gm["attrib.l3"].why);
            const uint8_t x = 0;
            w.mem.write(AttribWorld::kBase - 0x10, &x, 1);
            KnobAt gat;
            const std::string gw = locate_knob(w.mem, lg, *lg.knob("shape.speed"), gm, lk, gat);
            CHECK(gw.find("variant base: the instance at 0x24000000 no longer carries its asset GUID: locate again") != std::string::npos, gw);
            CHECK(resolve_anchor(w.mem, lg.locators[0], lk).why.find("does not carry the asset GUID 1852d61d-8c0d-edf7-37a7-258314497a5c") != std::string::npos,
                  "guid at the search");
        }
        // the file: L3 needs its slots; the round trip keeps the chain
        Registry x;
        CHECK(!parse_registry(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "a", "kind": "attrib", "type": "T",
              "variants": [{"name": "base", "instance": "n", "db": "d"}]}], "knobs": []})", x, err) &&
                  err.find("db_type, family_slot and group_slot are needed") != std::string::npos,
              err);
        CHECK(!parse_registry(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "locators": [{"name": "a", "kind": "attrib", "type": "T", "db_type": "D",
              "family_slot": "0x12A", "group_slot": "0x28", "variants": [{"name": "base", "instance": "n", "db": "d"}]}], "knobs": []})", x, err) &&
                  err.find("8-aligned") != std::string::npos,
              err);
        CHECK(parse_registry(registry_json(l3), x, err) && x.locators[0].l3() && x.locators[0].family_slot == 0x40 && x.locators[0].group_slot == 0x28 &&
                  x.locators[0].variants[1].db == AttribWorld::kDbSimName && x.locators[0].group_type == "AttribGroupRuntime_test" &&
                  x.locators[0].head_rva == 0xD005000 && x.knobs[0].copy_defaults.at("simulation") == 0.8,
              "round trip: " + err);
    });

    run_case("tuning: the service with an attrib locator - one search for every locator, every copy, defaults by copy, the owner rule, search again", [] {
        AttribWorld w;
        const fs::path dir = g_out / "tuning_test" / "svc_attrib";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "tuning", ec);
        fs::create_directories(dir / "turbo_output", ec);
        std::ofstream(dir / "tuning" / "knobs_6AC07E31.json", std::ios::binary) << kAttribKnobs;
        std::ofstream(dir / "turbo_output" / opt_in_file()) << "x";
        int searches = 0;
        std::vector<std::string> logs;
        Host h;
        h.mem = &w.mem;
        h.build = kBuildKey;
        h.knobs_dir = dir / "tuning";
        h.out_dir = dir / "turbo_output";
        h.lookup = w.lookup();
        const auto refs = h.lookup.refs;
        h.lookup.refs = [&searches, refs](const std::vector<std::string>& n, bool img) {
            ++searches;
            return refs(n, img);
        };
        h.run_on_game_thread = [](std::function<void()> f) {
            f();
            return std::string("hook");
        };
        h.run_background = [](std::function<void()> f) { f(); };
        h.log = [&logs](const std::string& s) { logs.push_back(s); };
        double clock = 1000;
        h.clock_us = [&clock]() { return clock; };
        CoreService svc(std::move(h));
        auto read = [&]() {
            std::map<std::string, KnobLive> m;
            for (const KnobLive& l : svc.read()) m[l.id] = l;
            return m;
        };
        auto live = read();
        CHECK(live["shape.speed"].located && live["shape.speed"].copies.size() == 2 && searches == 2, fmt("located, one image + one heap search (%d)", searches));
        CHECK(has_text(logs, "gameplay tuning: locator attrib.test found: type AttribSchema_test_group, base 0x24000000, simulation 0x24001000"), "logged (the 2.3 line)");
        Context c;
        c.session = "s";
        c.load_gen = 1;
        c.phase = "hub";
        c.in_cm = true;
        Plan p;
        std::string why;
        CHECK(make_plan(*svc.registry(), live, {{"shape.speed", 1.0}}, PlanKind::Apply, p).empty() && svc.submit(c, p, why), "submitted: " + why);
        ApplyResult r;
        CHECK(svc.poll(r) && r.ok && w.f32(AttribWorld::kBase + 0x28) == 1.0f && w.f32(AttribWorld::kSim + 0x28) == 1.0f, "both copies: " + r.message);
        auto defs = svc.defaults();
        CHECK(defs.size() == 2 && defs["shape.speed@base"] == double(0.75f) && defs["shape.speed@simulation"] == double(0.8f), "defaults by copy");
        CHECK(svc.last_written()["shape.speed@simulation"] == 1.0, "Turbo's last write by copy");
        // another tool changes the base copy: neither FC 27's value nor Turbo's -> refused, also for Restore game defaults
        w.mem.wr(AttribWorld::kBase + 0x28, 0.9f);
        live = read();
        CHECK(make_plan(*svc.registry(), live, {{"shape.speed", 1.1}}, PlanKind::Apply, p).empty() && svc.submit(c, p, why), "submitted");
        CHECK(svc.poll(r) && !r.ok && r.stage == "owner" && w.f32(AttribWorld::kSim + 0x28) == 1.0f, "owner: " + r.message);
        std::vector<std::string> skipped;
        CHECK(make_restore_plan(*svc.registry(), live, defs, p, skipped).empty() && p.writes.size() == 2 && svc.submit(c, p, why), "restore submitted");
        CHECK(svc.poll(r) && !r.ok && r.stage == "owner", "restore refused while another tool owns the base copy: " + r.message);
        w.mem.wr(AttribWorld::kBase + 0x28, 1.0f);
        live = read();
        CHECK(make_restore_plan(*svc.registry(), live, defs, p, skipped).empty() && svc.submit(c, p, why) && svc.poll(r) && r.ok &&
                  w.f32(AttribWorld::kBase + 0x28) == 0.75f && w.f32(AttribWorld::kSim + 0x28) == 0.8f,
              "restored: " + r.message);
        // the instance freed: the locator is searched again at the next read
        w.mem.wr(AttribWorld::kSim, uint64_t(0x14D009000ULL));
        live = read();
        CHECK(!live["shape.speed"].located && has_text(logs, "locator attrib.test lost its data"), "lost: " + live["shape.speed"].why);
        w.mem.wr(AttribWorld::kSim, AttribWorld::kTypePtr);
        live = read();
        CHECK(live["shape.speed"].located && searches == 4, fmt("searched again and found (%d searches)", searches));
    });

    run_case("tuning: the line height bands stay apart - a knob kept below another is checked with the values after the plan", [] {
        const std::string text = R"({"schema": "turbo-tuning-knobs/1", "build": "6AC07E31-2145C000",
          "locators": [{"name": "c", "kind": "native", "verified": true}],
          "knobs": [
            {"id": "band.deep_max", "label": "Deep up to", "group": "Shape", "collection": "g", "field": "a", "type": "i32", "min": 0, "max": 100,
             "default": 30, "locator": "c", "offset": 0, "below": ["band.bal_min"], "verified": true},
            {"id": "band.bal_min", "label": "Balanced from", "group": "Shape", "collection": "g", "field": "b", "type": "i32", "min": 0, "max": 100,
             "default": 31, "locator": "c", "offset": 4, "verified": true}]})";
        Registry reg;
        std::string err;
        CHECK(parse_registry(text, reg, err), err);
        SimMemory mem;
        mem.map(0x22000000, 0x100);
        mem.wr(uint64_t(0x22000000), int32_t(30));
        mem.wr(uint64_t(0x22000004), int32_t(31));
        Lookup lk;
        lk.natives["c"] = [](Memory&, uint64_t& b) -> std::string {
            b = 0x22000000;
            return "";
        };
        const AnchorMap am = anchors_of(mem, reg, lk);
        const auto live = live_of(mem, reg, am, lk);
        Plan p;
        CHECK(make_plan(reg, live, {{"band.deep_max", 31}}, PlanKind::Apply, p) == "Deep up to (31) must stay below Balanced from (31)", "overlap refused");
        CHECK(make_plan(reg, live, {{"band.deep_max", 40}, {"band.bal_min", 41}}, PlanKind::Apply, p).empty() && p.writes.size() == 2, "both moved together");
        CHECK(make_plan(reg, live, {{"band.bal_min", 20}}, PlanKind::Apply, p).find("must stay below") != std::string::npos, "the other side too");
        auto i32 = [&mem](uint64_t a) {
            int32_t v = 0;
            mem.rd(a, v);
            return v;
        };
        // the job checks it too: Apply 30 -> 40 and 31 -> 41, then a row Reset of Balanced from alone (41 -> 31) is refused, nothing written
        CHECK(make_plan(reg, live, {{"band.deep_max", 40}, {"band.bal_min", 41}}, PlanKind::Apply, p).empty(), "apply planned");
        CHECK(apply_plan(mem, reg, am, lk, p).ok && i32(0x22000000) == 40 && i32(0x22000004) == 41, "applied");
        const auto after = live_of(mem, reg, am, lk);
        const std::map<std::string, double> defs = {{copy_key("band.deep_max", ""), 30}, {copy_key("band.bal_min", ""), 31}};
        std::vector<std::string> skipped;
        CHECK(make_restore_plan(reg, after, defs, p, skipped, "band.bal_min").empty() && p.writes.size() == 1, "row Reset planned");
        ApplyResult r = apply_plan(mem, reg, am, lk, p);
        CHECK(!r.ok && r.stage == "plan" && r.message == "Deep up to (40) must stay below Balanced from (31): nothing written", r.message);
        CHECK(i32(0x22000000) == 40 && i32(0x22000004) == 41, "row Reset: no overlap written");
        // reset together (what the row's Reset does for knobs tied by a below rule): both back
        CHECK(make_restore_plan(reg, after, defs, p, skipped).empty() && p.writes.size() == 2, "both reset planned");
        CHECK(apply_plan(mem, reg, am, lk, p).ok && i32(0x22000000) == 30 && i32(0x22000004) == 31, "both reset");
        // Undo of a record that holds only Balanced from (41 -> 31) while Deep up to is 40: refused, nothing written
        mem.wr(uint64_t(0x22000000), int32_t(40));
        mem.wr(uint64_t(0x22000004), int32_t(41));
        UndoRecord u;
        u.state = "written";
        u.session = "s";
        u.load_gen = 1;
        u.knobs.push_back({"band.bal_min", 31, 41, ""});
        CHECK(make_undo_plan(reg, live_of(mem, reg, am, lk), u, "s", 1, p, skipped).empty() && p.writes.size() == 1, "undo planned");
        r = apply_plan(mem, reg, am, lk, p);
        CHECK(!r.ok && r.message.find("must stay below") != std::string::npos, "undo refused: " + r.message);
        CHECK(i32(0x22000000) == 40 && i32(0x22000004) == 41, "undo: no overlap written");
        Registry bad;
        CHECK(!parse_registry(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "knobs": [{"id": "a", "label": "A", "group": "Shape", "type": "i32",
              "below": ["zz"]}]})", bad, err) && err.find("below names knob zz") != std::string::npos, err);
    });

    run_case("tuning: the gates G1..G5 in order - opt-in, kill switches, the knob's phases, a save, the knobs file, Undo / Restore", [] {
        const Registry reg = test_registry();
        GateInput in = hub_input();
        CHECK(evaluate(in).pass(), "all pass");
        in.sw.opt_in = false;
        CHECK(evaluate(in).first() ==
                  "G1 switches: off: tick \"write in this game session\", switch the tuning profile on, or create turbo_output\\tuning_on.txt (opt-in)",
              evaluate(in).first());
        in.sw.opt_in = true;
        in.sw.off = true;
        CHECK(evaluate(in).first() == "G1 switches: kill switch turbo_output\\tuning_off.txt is present", evaluate(in).first());
        in.sw.master = true;
        CHECK(evaluate(in).first() == "G1 switches: kill switch turbo_output\\tactics_off.txt is present", evaluate(in).first());
        in.restore = true;
        CHECK(evaluate(in).pass(), "Undo / Restore game defaults are not blocked by the kill switches");
        in = hub_input();
        in.match_phase = "prematch";
        CHECK(evaluate(in).first().find("G2 career phase: a match is being set up or played (Lua: prematch)") == 0, evaluate(in).first());
        in.knobs = {reg.knob("passing.error_ground")};  // when: hub, prematch, match
        CHECK(evaluate(in).pass(), "a knob that may be written before a match");
        in.knobs.push_back(reg.knob("pressing.intensity"));
        CHECK(evaluate(in).first() == "G2 career phase: Pressing intensity may be written in the career hub only (Lua: prematch)", evaluate(in).first());
        in.restore = true;
        CHECK(!evaluate(in).pass(), "the phase holds for Restore too");
        in = hub_input();
        in.match_phase = "unknown";
        CHECK(evaluate(in).first().find("the match phase is not known yet") != std::string::npos, "unknown phase");
        in.match_phase = "";
        CHECK(evaluate(in).first().find("does not report the match phase") != std::string::npos, "old Lua");
        in = hub_input();
        in.save_age = 5;
        CHECK(evaluate(in).first() == "G2 career phase: a save started 5 s ago: wait a moment", evaluate(in).first());
        in = hub_input();
        in.in_cm = false;
        CHECK(evaluate(in).first() == "G2 career phase: not in a career", "not in a career");
        in = hub_input();
        in.registry = "no knobs file for game build X";
        CHECK(evaluate(in).first() == "G3 knobs file: no knobs file for game build X", "G3");
        in = hub_input();
        in.locate = "Pressing intensity: not located on this build (...)";
        CHECK(evaluate(in).first().find("G4 located:") == 0, "G4");
        in = hub_input();
        in.values = "nothing changed";
        CHECK(evaluate(in).first() == "G5 values: nothing changed", "G5");
        in = hub_input();
        in.session_off = "read-back mismatch";
        CHECK(evaluate(in).first() == "session: read-back mismatch", "session");
        in.restore = true;
        CHECK(evaluate(in).pass(), "Restore still goes after a session switch-off");
        Gates gs = evaluate(hub_input());
        CHECK(gs.list.size() == 6 && gs.list[0].name == "G1 switches" && gs.list[4].name == "G5 values", "six gates in order");
    });

    run_case("tuning: the service - the knobs file, locators, the job on the game thread, switches and phase in the job, defaults, session off", [] {
        TuneWorld w;
        const fs::path dir = g_out / "tuning_test" / "svc";
        std::error_code ec;
        fs::remove_all(dir, ec);
        TuneRig rig(w, dir);
        CHECK(rig.svc->registry_status().empty() && rig.svc->registry()->knobs.size() == 9, "loaded: " + rig.svc->registry_status());
        CHECK(rig.logged("gameplay tuning: knobs file") && rig.logged("9 knobs (8 verified in the running game, 1 not, never written; 0 proven in a played match)") &&
                  rig.logged("call_attrib_off.txt") && rig.logged("tuning_profile_off.txt"),
              "logged");
        auto live = rig.live();
        CHECK(live["pressing.intensity"].located && live["shooting.error"].located && live["cpuai.pass_risk"].located && !live["gk.reaction"].located,
              "read: located / not located");
        CHECK(rig.svc->anchors_status() == "3 of 3 locators resolved, 1 placeholder", rig.svc->anchors_status());
        CHECK(rig.logged("locator inline_coll anchored at 0x14C100000 (inline pattern)"), "the inline pattern was scanned in the background");
        Plan p;
        CHECK(make_plan(*rig.svc->registry(), live, {{"pressing.intensity", 0.8}, {"shape.line_height", 70}}, PlanKind::Apply, p).empty(), "plan");
        std::string why;
        // no opt-in: queued, the job refuses (the switch files are read right before queuing)
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued: " + why);
        rig.jobs.pump();
        ApplyResult r;
        CHECK(rig.svc->poll(r) && !r.ok && r.stage == "G1" && r.message.find("no opt-in (the session tick, the tuning profile or turbo_output\\tuning_on.txt)") == 0 &&
                  w.f32(TuneWorld::kBase) == 0.5f,
              "no opt-in: " + r.message);
        // the tab's session tick is an opt-in, the profile on is one too
        rig.svc->set_session_opt_in(true);
        CHECK(rig.svc->session_opt_in() && rig.svc->submit(rig.ctx(), p, why), "queued with the session tick");
        rig.svc->set_session_opt_in(false);
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.stage != "G1", "the session tick passes G1: " + r.stage + " " + r.message);
        rig.jobs.q.clear();
        // (that job wrote: FC 27's values back into the table for the cases below)
        w.mem.wr(TuneWorld::kBase, 0.5f);
        w.mem.wr(TuneWorld::kBase + 4, int32_t(45));
        rig.touch(opt_in_file());
        rig.touch(kill_switch_file());
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.stage == "G1" && r.message.find("tuning_off.txt") != std::string::npos && w.f32(TuneWorld::kBase) == 0.5f, "kill switch: " + r.message);
        rig.remove(kill_switch_file());
        rig.touch(master_switch_file());
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.stage == "G1" && r.message.find("tactics_off.txt") != std::string::npos, "master switch: " + r.message);
        rig.remove(master_switch_file());
        rig.touch(attrib_switch_file());
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.stage == "G1" && r.message.find("call_attrib_off.txt") != std::string::npos && w.f32(TuneWorld::kBase) == 0.5f,
              "the attribulator switch: " + r.message);
        rig.remove(attrib_switch_file());
        // the phase changes between Apply and the job
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued");
        rig.svc->note_context(rig.ctx(3, "prematch"));
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.stage == "G2" && r.message.find("Pressing intensity may be written in the career hub only (now: prematch)") == 0, r.message);
        // a career reload between Apply and the job
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued");
        rig.svc->note_context(rig.ctx(4));
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.stage == "G2" && r.message.find("the career was reloaded") == 0, r.message);
        // a late job
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued");
        rig.clock += 3e6;
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.stage == "late", r.message);
        // one job at a time; written; the defaults are the first snapshot
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued");
        CHECK(rig.svc->busy() && !rig.svc->submit(rig.ctx(), p, why) && why.find("still queued") != std::string::npos, "one job at a time");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.ok && r.written && w.f32(TuneWorld::kBase) == 0.8f && w.i32(TuneWorld::kBase + 4) == 70, "written: " + r.message);
        CHECK(rig.logged("gameplay tuning apply: Pressing intensity 0.50 -> 0.80") && rig.logged("[job "), "the result is logged with its time");
        auto defs = rig.svc->defaults();
        CHECK(defs.size() == 2 && defs["pressing.intensity"] == 0.5 && defs["shape.line_height"] == 45, "game defaults = the first snapshot");
        // a second write keeps the first defaults
        live = rig.live();
        CHECK(make_plan(*rig.svc->registry(), live, {{"pressing.intensity", 0.9}, {"marking.tightness", 90}}, PlanKind::Apply, p).empty(), "plan 2");
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued 2");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.ok, "written 2");
        defs = rig.svc->defaults();
        CHECK(defs.size() == 3 && defs["pressing.intensity"] == 0.5 && defs["marking.tightness"] == 60, "the defaults stay the values before Turbo's FIRST write");
        // Restore game defaults: every knob Turbo wrote, while the kill switch is present (it puts the game's values back)
        rig.touch(kill_switch_file());
        live = rig.live();
        std::vector<std::string> skipped;
        CHECK(make_restore_plan(*rig.svc->registry(), live, defs, p, skipped).empty() && p.writes.size() == 3 && p.kind == PlanKind::Restore, "restore plan");
        CHECK(rig.svc->submit(rig.ctx(), p, why), "restore queued");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.ok && w.f32(TuneWorld::kBase) == 0.5f && w.i32(TuneWorld::kBase + 4) == 45 && w.u8(TuneWorld::kBase + 8) == 60,
              "restored with the kill switch present: " + r.message);
        rig.remove(kill_switch_file());
        CHECK(rig.svc->defaults().size() == 3, "a restore does not change the defaults");
        // Undo with EVERY kill switch present and no opt-in: it puts the values from before Turbo's write back; an Apply is refused
        CHECK(make_plan(*rig.svc->registry(), rig.live(), {{"pressing.intensity", 0.55}}, PlanKind::Apply, p).empty() && rig.svc->submit(rig.ctx(), p, why),
              "queued");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.ok && w.f32(TuneWorld::kBase) == 0.55f, "written: " + r.message);
        {
            UndoRecord ur;
            ur.session = "s1";
            ur.load_gen = 3;
            ur.state = "written";
            ur.knobs = r.writes;
            for (const char* f : {kill_switch_file(), master_switch_file(), attrib_switch_file()}) rig.touch(f);
            rig.remove(opt_in_file());
            CHECK(make_undo_plan(*rig.svc->registry(), rig.live(), ur, "s1", 3, p, skipped).empty() && rig.svc->submit(rig.ctx(), p, why), "undo queued: " + why);
            rig.jobs.pump();
            CHECK(rig.svc->poll(r) && r.ok && w.f32(TuneWorld::kBase) == 0.5f, "undone with every kill switch present: " + r.message);
            rig.touch(opt_in_file());
            CHECK(make_plan(*rig.svc->registry(), rig.live(), {{"pressing.intensity", 0.6}}, PlanKind::Apply, p).empty() && rig.svc->submit(rig.ctx(), p, why),
                  "queued");
            rig.jobs.pump();
            CHECK(rig.svc->poll(r) && !r.ok && r.stage == "G1" && w.f32(TuneWorld::kBase) == 0.5f, "Apply refused under the kill switches: " + r.message);
            for (const char* f : {kill_switch_file(), master_switch_file(), attrib_switch_file()}) rig.remove(f);
        }
        // a read-back mismatch switches tuning off for the session; Restore still goes
        {
            TuneWorld w2;
            DropMemory dm(w2.mem);
            dm.drop = TuneWorld::kBase;
            TuneRig r2(w2, g_out / "tuning_test" / "svc2", kTestKnobs, &dm);
            r2.touch(opt_in_file());
            auto l2 = r2.live();
            Plan p2;
            CHECK(make_plan(*r2.svc->registry(), l2, {{"pressing.intensity", 0.8}}, PlanKind::Apply, p2).empty(), "plan");
            CHECK(r2.svc->submit(r2.ctx(), p2, why), "queued");
            r2.jobs.pump();
            ApplyResult x;
            CHECK(r2.svc->poll(x) && x.session_off && x.undone, "mismatch");
            CHECK(r2.svc->session_off().find("read-back mismatch") == 0, "off for the session");
            CHECK(!r2.svc->submit(r2.ctx(), p2, why) && why.find("off for this session") != std::string::npos, "Apply refused: " + why);
            Plan rp;
            rp.kind = PlanKind::Restore;
            CHECK(r2.svc->submit(r2.ctx(), rp, why), "Restore still queued: " + why);
            r2.jobs.pump();
            CHECK(r2.svc->poll(x) && x.ok, "an empty restore runs");
            CHECK(r2.svc->defaults().count("pressing.intensity") == 1, "the snapshot of a mismatched write still records the default");
        }
        // no dispatcher, a lost job, another career load's plan
        rig.jobs.report_none = true;
        live = rig.live();
        CHECK(make_plan(*rig.svc->registry(), live, {{"pressing.intensity", 0.7}}, PlanKind::Apply, p).empty(), "plan");
        CHECK(!rig.svc->submit(rig.ctx(), p, why) && why.find("no game-thread dispatcher") == 0 && !rig.svc->busy(), "not queued: " + why);
        rig.jobs.q.clear();
        rig.jobs.report_none = false;
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued (never run)");
        rig.jobs.q.clear();  // the dispatcher dropped it
        CHECK(rig.svc->busy(), "waiting");
        rig.clock += kJobLostUs + 1e6;
        CHECK(!rig.svc->busy() && rig.svc->submit(rig.ctx(), p, why), "given up after 10 s: " + why);
        CHECK(rig.logged("did not come back"), "logged");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.ok, "the next job runs");
        p.load_gen = 2;
        CHECK(!rig.svc->submit(rig.ctx(), p, why) && why.find("career load 2") != std::string::npos, "a plan of another career load: " + why);
        // the knobs file read again (refused while a job runs)
        p.load_gen = -1;
        CHECK(rig.svc->submit(rig.ctx(), p, why), "queued");
        CHECK(!rig.svc->reload(why) && why.find("still queued") != std::string::npos, "reload refused while busy");
        rig.jobs.pump();
        rig.svc->poll(r);
        CHECK(rig.svc->reload(why) && rig.svc->registry()->version == 7, "reloaded: " + why);
        // a broken file: the whole feature reads off with the reason
        std::ofstream(rig.knobs / "knobs_6AC07E31.json", std::ios::binary | std::ios::trunc) << "{\"schema\": \"turbo-tuning-knobs/1\"}";
        CHECK(!rig.svc->reload(why) && why.find("is not used: no build") != std::string::npos && rig.svc->registry()->knobs.empty(), why);
        CHECK(!rig.svc->submit(rig.ctx(), p, why) && why.find("is not used") != std::string::npos, "nothing queued without a knobs file");
    });

    run_case("tuning: undo bound to the career load - JSON, merges, only Turbo's values go back, another career load refused", [] {
        TuneWorld w;
        const Registry reg = test_registry();
        const Lookup lk = lookup_of(w);
        const AnchorMap am = anchors_of(w.mem, reg, lk);
        UndoRecord u;
        u.build = kBuildKey;
        u.session = "s1";
        u.load_gen = 3;
        u.state = "written";
        u.when = "2026-10-12 12:00:00";
        u.knobs = {{"pressing.intensity", 0.5, 0.8}, {"shape.line_height", 45, 70}};
        UndoRecord back;
        std::string err;
        CHECK(UndoRecord::parse(u.to_json(), back, &err) && back.knobs.size() == 2 && back.knobs[1].new_value == 70 && back.load_gen == 3 && back.state == "written",
              "JSON round trip: " + err);
        CHECK(!UndoRecord::parse("{\"x\": 1}", back, &err) && err == "not a gameplay tuning undo file", "not an undo file");
        // Turbo's values in place
        w.mem.wr(TuneWorld::kBase, 0.8f);
        w.mem.wr(TuneWorld::kBase + 4, int32_t(70));
        auto live = live_of(w.mem, reg, am, lk);
        Plan p;
        std::vector<std::string> skipped;
        CHECK(make_undo_plan(reg, live, u, "s1", 3, p, skipped).empty() && p.kind == PlanKind::Undo && p.writes.size() == 2 && p.writes[0].new_value == 0.5,
              "the undo plan");
        // ACROSS CAREER LOADS: refused (a reload may have rebuilt the tables)
        const std::string other = make_undo_plan(reg, live, u, "s1", 4, p, skipped);
        CHECK(other.find("the undo file is from another career load (written in load 3, this is load 4)") == 0 &&
                  other.find("use Restore game defaults") != std::string::npos,
              other);
        CHECK(make_undo_plan(reg, live, u, "s2", 3, p, skipped).find("of another game session") != std::string::npos, "another Lua session");
        // a knob changed since (by the game or another tool) is left alone
        w.mem.wr(TuneWorld::kBase + 4, int32_t(30));
        live = live_of(w.mem, reg, am, lk);
        CHECK(make_undo_plan(reg, live, u, "s1", 3, p, skipped).empty() && p.writes.size() == 1 && p.writes[0].id == "pressing.intensity" &&
                  has_text(skipped, "Defensive line height is 30 %, not Turbo's 70 %"),
              "only Turbo's value goes back");
        // nothing Turbo's left: refused with the reasons
        w.mem.wr(TuneWorld::kBase, 0.1f);
        live = live_of(w.mem, reg, am, lk);
        CHECK(make_undo_plan(reg, live, u, "s1", 3, p, skipped).find("nothing can be put back: Pressing intensity is 0.10") == 0, "nothing to put back");
        u.state = "restored";
        CHECK(make_undo_plan(reg, live, u, "s1", 3, p, skipped) == "nothing to put back (the undo file says \"restored\")", "already restored");
        // merges: the first old value stays, other knobs stay listed; another career load is not merged
        UndoRecord a = u;
        a.state = "written";
        UndoRecord b = a;
        b.knobs = {{"pressing.intensity", 0.8, 0.9}, {"marking.tightness", 60, 90}};
        const UndoRecord m = merge_undo(a, b);
        CHECK(m.knobs.size() == 3 && m.knobs[0].old_value == 0.5 && m.knobs[0].new_value == 0.9 && m.knobs[1].id == "marking.tightness" &&
                  m.knobs[2].id == "shape.line_height",
              "merged");
        b.load_gen = 4;
        CHECK(merge_undo(a, b).knobs.size() == 2, "another career load: not merged");
        CHECK(same_career(a, "s1", 3).empty() && !same_career(a, "s1", 4).empty(), "same_career");
        // the file
        const fs::path f = g_out / "tuning_test" / "undo" / undo_file();
        CHECK(save_undo(f, a, &err) && load_undo(f, back, &err) && back.knobs.size() == 2, "saved and loaded: " + err);
        // the restore plan from the defaults
        std::map<std::string, double> defs = {{"pressing.intensity", 0.5}, {"gk.reaction", 1}};
        CHECK(make_restore_plan(reg, live, defs, p, skipped).empty() && p.writes.size() == 1 && p.writes[0].new_value == 0.5 && has_text(skipped, "Goalkeeper"),
              "restore plan");
        CHECK(make_restore_plan(reg, live, {}, p, skipped) == "nothing to restore: Turbo has not written a knob in this game session", "no defaults");
    });

    run_case("tuning 2.7: live-proven knobs - Timing, the gates, tuning_live_off.txt, a live plan refuses a latched knob, a mid-match write is read back, Undo/Restore stay open", [] {
        Registry reg;
        std::string err;
        CHECK(parse_registry(live_knobs_text(), reg, err), "a promoted catalogue parses: " + err);
        const Knob* a = reg.knob("pressing.intensity");
        const Knob* b = reg.knob("shape.line_height");
        const Knob* c = reg.knob("marking.tightness");
        const Knob* d = reg.knob("shooting.error");
        CHECK(a && b && c && d, "knobs");
        CHECK(a->live_ok() && a->timing() == Knob::Timing::Now && std::string(a->timing_label()) == "now (live)", "proven + live: now");
        CHECK(!b->live_ok() && b->timing() == Knob::Timing::NextMatch && std::string(b->timing_label()) == "next match (latched at kick-off)", "proven latched");
        CHECK(!c->live_ok() && c->timing() == Knob::Timing::Unknown && std::string(c->timing_label()) == "not measured: assume the next match", "proven, applies unknown");
        CHECK(d->live && !d->live_ok() && !d->allows("match") && !d->allows("prematch"), "live: true without a proof is NOT live (no mid-match write)");
        CHECK(a->allows("match") && a->allows("prematch") && a->allows("hub") && !a->allows_moment("match"), "a live-proven knob is allowed mid-match without 'match' in when");
        CHECK(!b->allows("match") && !b->allows("prematch") && b->allows("hub") && !c->allows("match"), "a latched / unmeasured knob is hub-only");
        // the gates
        GateInput in = hub_input();
        in.match_phase = "match";
        in.knobs = {a};
        in.live_apply = true;
        CHECK(evaluate(in).pass(), "match phase, live plan of a live-proven knob: all gates pass: " + evaluate(in).first());
        in.knobs = {a, b};
        CHECK(evaluate(in).first() == "G2 career phase: Defensive line height is not proven live (it is read when the match starts): use Apply in the career hub, before kick-off",
              evaluate(in).first());
        in.live_apply = false;
        CHECK(evaluate(in).first().find("G2 career phase: Defensive line height may be written in the career hub only (Lua: match): it is latched when the match starts") == 0, evaluate(in).first());
        in.knobs = {d};
        in.live_apply = true;
        CHECK(evaluate(in).first().find("is not proven live") != std::string::npos, "live: true without a proof is refused by the live gate");
        in = hub_input();
        in.knobs = {a};
        in.live_apply = true;
        CHECK(evaluate(in).pass(), "Apply now in the hub passes too");
        in.sw.live_off = true;
        CHECK(evaluate(in).first() == "G1 switches: kill switch turbo_output\\tuning_live_off.txt is present (no gameplay tuning write during a match)", evaluate(in).first());
        in.live_apply = false;
        in.knobs = {b};
        CHECK(evaluate(in).pass(), "the live kill switch does not stop a normal hub Apply");
        in.match_phase = "match";
        in.knobs = {a};
        CHECK(!evaluate(in).pass(), "... but it stops any write during a match");
        in.restore = true;
        CHECK(evaluate(in).pass(), "... and Undo / Restore are not blocked");
        // the service: a mid-match write
        TuneWorld w;
        const fs::path dir = g_out / "tuning_test" / "live27";
        std::error_code ec;
        fs::remove_all(dir, ec);
        TuneRig rig(w, dir, live_knobs_text());
        rig.touch(opt_in_file());
        auto live = rig.live();
        CHECK(live["pressing.intensity"].located && live["shape.line_height"].located, "located");
        Plan p;
        CHECK(make_plan(*rig.svc->registry(), live, {{"pressing.intensity", 0.8}}, PlanKind::Apply, p).empty(), "live plan");
        p.live = true;
        std::string why;
        rig.svc->note_context(rig.ctx(3, "match"));
        CHECK(rig.svc->submit(rig.ctx(3, "match"), p, why), "queued mid-match: " + why);
        rig.jobs.pump();
        ApplyResult r;
        CHECK(rig.svc->poll(r) && r.ok && r.written && r.message == "written and read back" && w.f32(TuneWorld::kBase) == 0.8f, "written mid-match and read back: " + r.message);
        // a live plan that holds a latched knob is refused before queuing; a normal plan of it is refused in the job (match phase)
        Plan q;
        CHECK(make_plan(*rig.svc->registry(), live, {{"shape.line_height", 70}}, PlanKind::Apply, q).empty(), "latched plan");
        Plan ql = q;
        ql.live = true;
        CHECK(rig.svc->submit(rig.ctx(3, "match"), ql, why), "queued (the job refuses it)");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && !r.ok && r.stage == "G1" && r.message.find("not proven live") != std::string::npos && w.i32(TuneWorld::kBase + 4) == 45,
              "a live plan of a latched knob writes nothing: " + r.message);
        CHECK(rig.svc->submit(rig.ctx(3, "match"), q, why), "queued");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && !r.ok && r.stage == "G2" && r.message.find("Defensive line height may be written in the career hub only (now: match)") == 0 &&
                  w.i32(TuneWorld::kBase + 4) == 45,
              "a latched knob is not written mid-match: " + r.message);
        // the kill switch is read right before queuing and again in the job
        Plan p2;
        live = rig.live();
        CHECK(make_plan(*rig.svc->registry(), live, {{"pressing.intensity", 0.3}}, PlanKind::Apply, p2).empty(), "second live plan");
        p2.live = true;
        rig.touch(live_off_file());
        CHECK(rig.svc->submit(rig.ctx(3, "match"), p2, why), "queued");
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && !r.ok && r.stage == "G1" && r.message.find("tuning_live_off.txt") != std::string::npos && w.f32(TuneWorld::kBase) == 0.8f,
              "the live kill switch: " + r.message);
        rig.remove(live_off_file());
        CHECK(rig.svc->submit(rig.ctx(3, "match"), p2, why), "queued again");
        rig.touch(live_off_file());
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && !r.ok && r.stage == "G1" && w.f32(TuneWorld::kBase) == 0.8f, "the switch is read again inside the job: " + r.message);
        // Restore mid-match with the kill switch present: not blocked
        live = rig.live();
        Plan rs;
        std::vector<std::string> sk;
        CHECK(make_vanilla_plan(*rig.svc->registry(), live, rs, sk).empty() && rs.writes.size() == 1, "a vanilla plan for the live knob");
        CHECK(rig.svc->submit(rig.ctx(3, "match"), rs, why), "Restore queued mid-match: " + why);
        rig.jobs.pump();
        CHECK(rig.svc->poll(r) && r.ok && r.written && w.f32(TuneWorld::kBase) == 0.5f, "FC 27's value is back mid-match: " + r.message);
        // 0 proven = 2.6.0: no knob is proven, live_ok or newly allowed mid-match
        Registry none;
        CHECK(parse_registry(live_knobs_text(false), none, err), "the unpromoted file parses");
        bool any = false;
        for (const Knob& k : none.knobs) any = any || k.live_ok() || k.proven() || (k.allows("match") && k.id != "passing.error_ground");
        CHECK(!any, "0 proven: no knob is proven, live_ok or allowed mid-match (passing.error_ground names 'match' in when, as in 2.6.0)");
    });

    run_case("tuning 2.7: Turbo mixed styles - six in memory, only proven knobs, empty with 0 proven, clamped to the catalogue range, ids exist in the shipped catalogue", [] {
        const fs::path p = fs::path("..") / "turbo" / "package" / "turbo" / "tuning" / "knobs_6AC07E31.json";
        std::ifstream f(p, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Registry reg;
        std::string err;
        CHECK(parse_registry(text, reg, err), "shipped catalogue: " + err);
        const auto& rec = style_recipes();
        CHECK(rec.size() == 6 && rec[0].name == "Turbo style: High press" && rec[1].name == "Turbo style: Low block" && rec[2].name == "Turbo style: Wide" &&
                  rec[3].name == "Turbo style: Narrow" && rec[4].name == "Turbo style: Counter" && rec[5].name == "Turbo style: Possession",
              "the six styles");
        for (const StyleRecipe& r : rec) {
            CHECK(r.id.rfind("turbo_style_", 0) == 0 && !r.changes.empty(), r.id + ": id and changes");
            for (const auto& ch : r.changes) {
                const Knob* k = reg.knob(ch.first);
                CHECK(k && !k->candidate && k->has_range() && k->type == ValueType::F32 && std::fabs(ch.second) <= 100.0, r.id + ": " + ch.first + " is a real, non-candidate catalogue knob");
            }
        }
        // 0 proven (the shipped state): all six empty
        std::vector<Preset> st = style_presets(reg);
        bool all_empty = st.size() == 6;
        for (const Preset& s : st) all_empty = all_empty && s.changes.empty() && is_style_preset(s) && s.turbo() && s.note.find("empty until its knobs are proven") != std::string::npos;
        CHECK(all_empty, "0 proven knobs: every style is empty");
        const PresetValues none = preset_values(st[0], reg, [](const std::string&, double& v) { v = 1; return true; });
        CHECK(none.values.empty() && none.skipped.empty(), "an empty style gives nothing");
        // the files: Turbo's file presets are unchanged (the styles are never written)
        const fs::path dir = g_out / "tuning_test" / "styles27";
        std::error_code ec;
        fs::remove_all(dir, ec);
        CHECK(ensure_builtin_presets(dir, &err) == 5, "still five Turbo files");
        int files = 0;
        for (const auto& en : fs::directory_iterator(dir)) files += en.path().filename().string().find("style") != std::string::npos ? 1 : 0;
        CHECK(files == 0, "no style file on disk");
        // some proven: width_in and length_in promoted in the data
        auto promote = [&](const std::vector<std::string>& ids) {
            std::string t = text;
            for (const std::string& id : ids) {
                const size_t a = t.find("\"id\": \"" + id + "\"");
                const size_t v = t.find("\"verified\": true,", a);
                CHECK(a != std::string::npos && v != std::string::npos, id + " in the file");
                t.insert(v, "\"proof\": {\"session\": \"T29\", \"evidence\": \"EV/2026-10-20/b/result.json#" + id + "\"}, \"slider\": true, ");
            }
            Registry r2;
            std::string e2;
            CHECK(parse_registry(t, r2, e2), "promoted file parses: " + e2);
            return r2;
        };
        Registry r2 = promote({"shape.width_in", "shape.length_in"});
        st = style_presets(r2);
        std::map<std::string, size_t> n;
        for (const Preset& s : st) n[s.name] = s.changes.size();
        CHECK(n["Turbo style: Wide"] == 1 && n["Turbo style: Narrow"] == 1 && n["Turbo style: Possession"] == 2 && n["Turbo style: Counter"] == 1 &&
                  n["Turbo style: High press"] == 0 && n["Turbo style: Low block"] == 0,
              "styles hold only the two proven knobs: Wide 1, Narrow 1, Possession 2, Counter 1, High press 0, Low block 0");
        for (const Preset& s : st)
            for (const PresetChange& ch : s.changes) CHECK(r2.knob(ch.knob)->proven(), s.name + ": " + ch.knob + " is proven");
        CHECK(st[2].note.find("1 of 3 today") != std::string::npos, "the note counts: " + st[2].note);
        // values: relative to FC 27's value, clamped to the catalogue range, made representable
        const auto base = [&](const std::string& id, double& v) {
            const Knob* k = r2.knob(id);
            v = k->def;
            return true;
        };
        const PresetValues pw = preset_values(st[2], r2, base);  // Wide: width_in +20 %
        CHECK(pw.values.size() == 1 && std::fabs(pw.values.at("shape.width_in") - 1.2) < 1e-6, "Wide: width_in 1.0 -> 1.2");
        Preset big = st[2];
        big.changes = {{"shape.width_in", true, 500, 0}, {"shape.length_in", true, -500, 0}};
        const PresetValues pb = preset_values(big, r2, base);
        CHECK(pb.values.at("shape.width_in") == 1.7 && pb.values.at("shape.length_in") == 0.5, "relative changes are clamped to min..max");
        Preset abs = st[2];
        abs.changes = {{"shape.width_in", false, 0, 9.0}, {"shape.length_in", false, 0, -3.0}};
        const PresetValues pa = preset_values(abs, r2, base);
        CHECK(pa.values.at("shape.width_in") == 1.7 && pa.values.at("shape.length_in") == 0.5 && pa.clamped.size() == 2 &&
                  pa.clamped[0] == "Team width with the ball: 9 -> 1.7" && pa.skipped.empty(),
              "absolute values outside the range are clamped and said so: " + (pa.clamped.empty() ? std::string("none") : pa.clamped[0]));
        Preset nan = st[2];
        nan.changes = {{"shape.width_in", false, 0, std::numeric_limits<double>::quiet_NaN()}};
        CHECK(preset_values(nan, r2, base).values.empty(), "NaN is refused");
        CHECK(preset_status(st[5], r2).measured == 2, "the status of a style: 2 measured");
        Preset back;
        CHECK(Preset::parse(st[5].to_json(), back, &err) && back.changes.size() == 2 && back.turbo() && back.name == "Turbo style: Possession", "round trip: " + err);
    });

    run_case("tuning: presets - Turbo's own relative presets, files in turbo_output\\tuning_presets, import format, values from the game defaults", [] {
        const Registry reg = test_registry();
        const fs::path dir = g_out / "tuning_test" / "presets";
        std::error_code ec;
        fs::remove_all(dir, ec);
        std::string err;
        CHECK(ensure_builtin_presets(dir, &err) == 5, "Turbo's five presets written: " + err);
        CHECK(fs::exists(dir / "turbo_fc27_default.json") && fs::exists(dir / "turbo_compact.json") && fs::exists(dir / "turbo_expansive.json") &&
                  fs::exists(dir / "turbo_wide_short.json") && fs::exists(dir / "turbo_narrow_long.json"),
              "files");
        CHECK(ensure_builtin_presets(dir, &err) == 0, "an unchanged file is not written again");
        // a stale Turbo file is replaced; a user's file of the same name is never touched
        std::ofstream(dir / "turbo_compact.json", std::ios::binary | std::ios::trunc)
            << R"({"turbo_tuning_preset": 1, "id": "turbo_compact", "name": "Turbo compact", "author": "Turbo", "changes": []})";
        std::ofstream(dir / "turbo_expansive.json", std::ios::binary | std::ios::trunc)
            << R"({"turbo_tuning_preset": 1, "name": "My own", "author": "me", "changes": [{"knob": "pressing.intensity", "value": 0.7}]})";
        CHECK(ensure_builtin_presets(dir, &err) == 1, "only the stale Turbo file is rewritten");
        std::ofstream(dir / "broken.json", std::ios::binary | std::ios::trunc) << "{";
        std::vector<std::string> errors;
        const std::vector<Preset> list = load_presets(dir, &errors);
        CHECK(list.size() == 5 && list[0].turbo() && list[0].name == "FC 27 default" && list[1].name == "Turbo compact" && !list[1].changes.empty() &&
                  list[4].name == "My own" && !list[4].turbo(),
              fmt("listed %zu, Turbo's first; the user's file with a Turbo name kept", list.size()));
        CHECK(errors.size() == 1 && errors[0].find("broken.json") == 0, "a broken file is reported");
        // values: relative from each knob's base (the game default), clamped, representable; switches and placeholders skipped
        Preset p;
        p.name = "t";
        p.changes = {{"pressing.intensity", true, 20, 0}, {"shape.line_height", true, -10, 0}, {"marking.tightness", true, 100, 0}, {"runs.in_behind", true, 10, 0},
                     {"gk.reaction", true, 5, 0}, {"nope", true, 5, 0}, {"passing.error_ground", false, 0, 0.35}, {"passing.error_lofted", false, 0, 7}};
        std::map<std::string, double> base = {{"pressing.intensity", 0.5}, {"shape.line_height", 45}, {"marking.tightness", 60}, {"runs.in_behind", 1},
                                              {"passing.error_ground", 0.2}, {"passing.error_lofted", 0.3}};
        const PresetValues pv = preset_values(p, reg, [&](const std::string& id, double& v) {
            auto it = base.find(id);
            if (it == base.end()) return false;
            v = it->second;
            return true;
        });
        CHECK(pv.values.size() == 5 && pv.values.at("pressing.intensity") == double(0.6f) && pv.values.at("shape.line_height") == 41 &&
                  pv.values.at("marking.tightness") == 100 && pv.values.at("passing.error_ground") == double(0.35f) && pv.values.at("passing.error_lofted") == 1.0,
              "relative +20 % of 0.5, -10 % of 45 rounded, +100 % of 60 clamped to 100, an absolute value, an absolute 7 clamped to the range's 1 (2.7)");
        CHECK(has_text(pv.skipped, "is a switch") && has_text(pv.skipped, "Goalkeeper reaction time: no value range") && has_text(pv.skipped, "nope: not in the knobs file") &&
                  pv.skipped.size() == 3 && pv.clamped == std::vector<std::string>({"Lofted pass error: 7 -> 1"}),
              "skipped with reasons; the out-of-range absolute value is clamped and said so (2.7)");
        // a preset from the sliders: relative to the base, an absolute value when the base is 0 or a switch
        base["cpuai.pass_risk"] = 0;
        const Preset q = preset_from_values("My press", reg, {{"pressing.intensity", 0.6}, {"shape.line_height", 45}, {"runs.in_behind", 0}, {"cpuai.pass_risk", 0.2}},
                                            [&](const std::string& id, double& v) {
                                                auto it = base.find(id);
                                                if (it == base.end()) return false;
                                                v = it->second;
                                                return true;
                                            });
        CHECK(q.changes.size() == 3 && q.changes[0].knob == "pressing.intensity" && q.changes[0].relative && q.changes[0].pct == 20 && !q.changes[1].relative &&
                  q.changes[1].knob == "runs.in_behind" && !q.changes[2].relative && q.changes[2].value == 0.2,
              "from values");
        CHECK(preset_file_name(q) == "My_press.json" && preset_file_name(builtin_presets()[0]) == "turbo_fc27_default.json", "file names");
        Preset back;
        CHECK(Preset::parse(q.to_json(), back, &err) && back.changes.size() == 3 && back.name == "My press" && back.id == "user_My_press", "round trip: " + err);
        CHECK(!Preset::parse(R"({"turbo_tuning_preset": 1, "name": "x", "changes": [{"knob": "a", "relative_pct": 5, "value": 1}]})", back, &err) &&
                  err.find("one of them") != std::string::npos,
              "relative and value together: " + err);
        CHECK(!Preset::parse(R"({"turbo_tuning_preset": 1, "name": "x", "changes": [{"knob": "a", "relative_pct": -500}]})", back, &err) &&
                  err.find("-100..1000") != std::string::npos,
              "a -500 % change: " + err);
        CHECK(!Preset::parse(R"({"turbo_tuning_preset": 1, "changes": []})", back, &err) && err == "the preset has no name", err);
    });
}

// (2026-10-31, 5.0c menus step 6, second pass: the UI cases that drew ui_tuning.cpp's draw_tuning in a window of their own were deleted. No page draws
// that view any more (Advanced > Gameplay tuning went with the Lab), and a test of a view no page draws proves nothing; the knobs file, locators,
// job, gates, undo and presets cases above are the tuning tests.)

}  // namespace tuning_test
