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
     "type": "i32", "unit": "%", "min": 0, "max": 100, "locator": "test_coll", "offset": 4, "applies": "next match setup", "verified": true},
    {"id": "marking.tightness", "label": "Marking tightness", "group": "Marking", "collection": "gp_test_mark", "field": "tight",
     "type": "u8", "min": 0, "max": 100, "locator": "test_coll", "offset": 8, "verified": true},
    {"id": "runs.in_behind", "label": "Runs in behind the defence", "group": "Attacking runs", "collection": "gp_test_runs", "field": "behind",
     "type": "bool", "locator": "test_coll", "offset": 9, "applies": "next match setup", "verified": true},
    {"id": "passing.error_ground", "label": "Ground pass error", "group": "Passing", "collection": "gp_test_pass", "field": "err_ground",
     "type": "f32", "min": 0, "max": 1, "locator": "test_coll", "offset": "0xC", "applies": "live", "when": ["hub", "prematch", "match"], "verified": true},
    {"id": "passing.error_lofted", "label": "Lofted pass error", "group": "Passing", "collection": "gp_test_pass", "field": "err_lofted",
     "type": "f32", "min": 0, "max": 1, "locator": "test_coll", "offset": "0x10", "applies": "next match setup", "verified": true},
    {"id": "shooting.error", "label": "Shot error", "group": "Shooting", "collection": "gp_test_shot", "field": "err",
     "type": "f32", "min": 0, "max": 2, "locator": "inline_coll", "offset": "0x14", "applies": "next match setup", "verified": true},
    {"id": "cpuai.pass_risk", "label": "CPU passing risk", "group": "CPU AI", "collection": "gp_test_cpu", "field": "risk",
     "type": "f32", "min": 0, "max": 1, "locator": "native_coll", "offset": "0x18", "applies": "next match setup", "verified": true},
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

// The attribulator layout of TACTICS_MOD_CATALOGUE section 7 on a simulated memory: a reflected type (TypeInfoData with its name, size,
// field count, signature and three FieldInfoData whose types are Float32 / Int32 / Boolean) in the image, and two instances of it in
// the heap (base and simulation, each with the type pointer at +0 and its asset name at +0x18), plus a decoy that carries the base name
// but no image pointer. Lookup::refs searches the simulated pages with the same helpers the host uses.
struct AttribWorld {
    SimMemory mem;
    static constexpr uint64_t kImage = 0x140000000ULL, kImageSize = 0x2145C000ULL;
    static constexpr uint64_t kTd = 0x14D000000ULL, kFields = 0x14D001000ULL, kTiF = 0x14D002000ULL, kTiI = 0x14D002010ULL, kTiB = 0x14D002020ULL;
    static constexpr uint64_t kTdF = 0x14D003000ULL, kTdI = 0x14D003100ULL, kTdB = 0x14D003200ULL, kTypePtr = 0x14D005000ULL, kStrings = 0x14E000000ULL;
    static constexpr uint64_t kBase = 0x24000000ULL, kSim = 0x24001000ULL, kDecoy = 0x24002000ULL, kNames = 0x24100000ULL;
    static constexpr const char* kBaseName = "Fifa/Attribulator/Gameplay/groups/test/test_group_runtime";
    static constexpr const char* kSimName = "Fifa/Attribulator/Gameplay/databases/simulation/groups/test/test_group_runtime";
    uint64_t str_at = kStrings;
    uint64_t put_str(const std::string& s) {
        const uint64_t a = str_at;
        mem.map(a, s.size() + 1);
        mem.write(a, s.c_str(), s.size() + 1);
        str_at += (s.size() + 16) & ~uint64_t(7);
        return a;
    }
    explicit AttribWorld(uint32_t sig = 0x12345678) {
        // the field types' TypeInfoData, their TypeInfo objects
        const std::pair<uint64_t, const char*> types[] = {{kTdF, "Float32"}, {kTdI, "Int32"}, {kTdB, "Boolean"}};
        const uint64_t tis[] = {kTiF, kTiI, kTiB};
        for (int i = 0; i < 3; ++i) {
            mem.map(types[i].first, 0x48);
            mem.wr(types[i].first, put_str(types[i].second));
            mem.map(tis[i], 8);
            mem.wr(tis[i], types[i].first);
        }
        // the group's type
        mem.map(kTd, 0x48);
        mem.wr(kTd, put_str("AttribSchema_test_group"));
        mem.wr(kTd + 0x0E, uint16_t(0x40));
        mem.wr(kTd + 0x32, uint16_t(3));
        mem.wr(kTd + 0x34, sig);
        mem.wr(kTd + 0x40, kFields);
        const char* names[] = {"speedModifier", "count", "enabled"};
        const uint32_t offs[] = {0x28, 0x2c, 0x30};
        mem.map(kFields, 3 * 0x20);
        for (int i = 0; i < 3; ++i) {
            mem.wr(kFields + 0x20 * uint64_t(i), put_str(names[i]));
            mem.wr(kFields + 0x20 * uint64_t(i) + 0x10, offs[i]);
            mem.wr(kFields + 0x20 * uint64_t(i) + 0x18, tis[i]);
        }
        mem.map(kTypePtr, 8);
        // the instances (heap)
        mem.map(kNames, 0x400);
        mem.write(kNames, kBaseName, std::strlen(kBaseName) + 1);
        mem.write(kNames + 0x100, kSimName, std::strlen(kSimName) + 1);
        mem.write(kNames + 0x200, kBaseName, std::strlen(kBaseName) + 1);  // the decoy's own copy of the name
        put_inst(kBase, kNames, 0.75f);
        put_inst(kSim, kNames + 0x100, 0.8f);
        mem.map(kDecoy, 0x40);
        mem.wr(kDecoy, uint64_t(0x30000000));  // not an image pointer
        mem.wr(kDecoy + 0x18, kNames + 0x200);
    }
    void put_inst(uint64_t i, uint64_t name, float speed) {
        mem.map(i, 0x40);
        mem.wr(i, kTypePtr);
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
     "default": 0.75, "locator": "attrib.test", "offset": "0x28", "verified": true},
    {"id": "shape.count", "label": "Count", "group": "Shape", "collection": "test_group", "field": "count", "type": "i32", "min": 0, "max": 10,
     "default": 5, "locator": "attrib.test", "offset": "0x2c", "verified": true},
    {"id": "shape.enabled", "label": "Enabled", "group": "Shape", "collection": "test_group", "field": "enabled", "type": "bool",
     "default": 1, "locator": "attrib.test", "offset": "0x30", "verified": true},
    {"id": "shape.wrong_offset", "label": "Wrong offset", "group": "Shape", "collection": "test_group", "field": "speedModifier", "type": "f32",
     "min": 0, "max": 2, "locator": "attrib.test", "offset": "0x2c", "verified": true},
    {"id": "shape.missing", "label": "Missing", "group": "Shape", "collection": "test_group", "field": "nope", "type": "f32",
     "min": 0, "max": 2, "locator": "attrib.test", "offset": "0x34", "verified": true},
    {"id": "shape.wrong_type", "label": "Wrong type", "group": "Shape", "collection": "test_group", "field": "speedModifier", "type": "i32",
     "min": 0, "max": 2, "locator": "attrib.test", "offset": "0x28", "verified": true}
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

// --------------------------------------------------------------------------------------------------------------------------- core
static void run() {
    std::printf("native gameplay tuning (engine tuning tables)\n");

    run_case("tuning: the shipped knobs file - catalogue entries, all unverified, never located, every group, Turbo's presets name its knobs", [] {
        const fs::path p = fs::path("..") / "turbo" / "package" / "turbo" / "tuning" / "knobs_6AC07E31.json";
        std::ifstream f(p, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(!text.empty(), "the package's knobs file is there: " + p.string());
        Registry reg;
        std::string err;
        CHECK(parse_registry(text, reg, err), "it parses: " + err);
        CHECK(reg.build == kBuildKey && reg.knobs.size() >= 20, fmt("build %s, %zu knobs", reg.build.c_str(), reg.knobs.size()));
        CHECK(reg.placeholders() == int(reg.knobs.size()), "every knob is unverified or a placeholder: nothing is written until T1 confirms it");
        bool verified = false;
        for (const Knob& k : reg.knobs) verified = verified || k.verified;
        for (const Locator& l : reg.locators) verified = verified || l.verified;
        CHECK(!verified, "no knob and no locator is marked verified");
        // the catalogue knobs: group / field / type / offset from FC 27's type data, FC 27's own value inside Turbo's range, an attrib locator
        int catalogue = 0;
        for (const Knob& k : reg.knobs) {
            if (k.field.empty()) continue;
            ++catalogue;
            const Locator* l = reg.locator(k.locator);
            CHECK(l && l->kind == "attrib" && !l->type.empty() && l->type_signature && l->type_size && l->type_fields > 0 && !l->variants.empty(),
                  k.id + ": an attrib locator with its type check");
            CHECK(!k.collection.empty() && k.offset >= 0 && l && uint64_t(k.offset) + type_size(k.type) <= l->type_size, k.id + ": group, offset inside the type");
            CHECK(k.has_range() && k.has_default && check_value(k, k.def).empty(), k.id + ": FC 27's value inside Turbo's range");
            CHECK(k.label.find('_') == std::string::npos && k.label.find(k.field) == std::string::npos, k.id + ": a plain label, not the field name");
        }
        CHECK(catalogue >= 20, fmt("%d catalogue knobs", catalogue));
        const Knob* gap = reg.knob("shape.gap_dmf");
        CHECK(gap && gap->field == "dmfLineOffsetWhenTouchIsOnPar" && gap->offset == 0x664 && gap->def == 8 && gap->unit == "m", "the defence - midfield gap");
        const Locator* zd = gap ? reg.locator(gap->locator) : nullptr;
        CHECK(zd && zd->type == "AttribSchema_gp_positioning_zonal_defense" && zd->type_signature == 0x3edbaf9a && zd->type_size == 0x6c8 &&
                  zd->type_fields == 237 && zd->variants.size() == 2 && zd->variants[1].name == "simulation" && zd->variants[1].instance.empty(),
              "zonal_defense: base and simulation copies (the simulation instance name: T1)");
        const Knob* band = reg.knob("lineband.deep_max");
        CHECK(band && band->type == ValueType::I32 && band->def == 30 && band->below == std::vector<std::string>({"lineband.balanced_min"}) &&
                  band->applies == Applies::NextMatchSetup,
              "the line height bands: i32, FC 27's 30, kept below the next band, read at match setup");
        for (const std::string& g : standard_groups()) {
            bool has = false;
            for (const Knob& k : reg.knobs) has = has || k.group == g;
            CHECK(has, "group " + g + " has knobs");
        }
        for (const Preset& pr : builtin_presets()) {
            CHECK(pr.turbo() && !pr.changes.empty(), pr.name + " is Turbo's own");
            for (const PresetChange& c : pr.changes) {
                CHECK(reg.knob(c.knob) != nullptr, pr.name + " names knob " + c.knob);
                CHECK(c.relative, pr.name + ": " + c.knob + " is a relative change (no real value is known)");
            }
        }
        TuneWorld w;
        const Lookup lk = lookup_of(w);
        const AnchorMap am = anchors_of(w.mem, reg, lk);
        for (const Knob& k : reg.knobs) {
            KnobAt at;
            const std::string why = locate_knob(w.mem, reg, k, am, lk, at);
            CHECK(why.find(k.field.empty() ? "not located on this build (placeholder" : "not located on this build (unverified: ") == 0, k.id + ": " + why);
        }
        // the round trip through registry_json parses back to the same knobs
        Registry again;
        CHECK(parse_registry(registry_json(reg), again, err) && again.knobs.size() == reg.knobs.size(), "registry_json round trip: " + err);
        CHECK(knobs_file_names(kBuildKey) == std::vector<std::string>({"knobs_6AC07E31-2145C000.json", "knobs_6AC07E31.json"}), "file names");
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
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "when": ["kickoff"]})"), "unknown phase");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "applies": "sometimes"})"), "applies must be");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "locator": "nope"})"), "not in the file's locators");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "locator": "c", "min": 0, "max": 1, "collection": "x", "field": "y", "verified": true})"),
                "marked verified without");
        refused(knob(R"({"id": "a", "label": "A", "group": "Shape", "type": "f32", "offset": "0xZZ"})"), "offset must be");
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
            w.mem.wr(TuneWorld::kBase, 7.5f);  // outside 0..1: the layout is wrong
            const std::string why = why_of(w, lookup_of(w));
            CHECK(why.find("reads 7.5, outside the knob's range 0..1: wrong layout?") != std::string::npos, why);
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
                  p.writes.size() == 1 && p.writes[0].copy == "simulation" && has_text(skipped, "shape.gone@base is not in the knobs file"),
              "restore: only the copy that differs");
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
        last["shape.count@simulation"] = 5;
        r = apply_plan(w.mem, reg, am, lk, p, jo);
        CHECK(r.ok, "Turbo's last write is Turbo's: " + r.message);
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
        CHECK(has_text(logs, "locator attrib.test found: type AttribSchema_test_group, base 0x24000000, simulation 0x24001000"), "logged");
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
             "locator": "c", "offset": 0, "below": ["band.bal_min"], "verified": true},
            {"id": "band.bal_min", "label": "Balanced from", "group": "Shape", "collection": "g", "field": "b", "type": "i32", "min": 0, "max": 100,
             "locator": "c", "offset": 4, "verified": true}]})";
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
        Registry bad;
        CHECK(!parse_registry(R"({"schema": "turbo-tuning-knobs/1", "build": "b", "knobs": [{"id": "a", "label": "A", "group": "Shape", "type": "i32",
              "below": ["zz"]}]})", bad, err) && err.find("below names knob zz") != std::string::npos, err);
    });

    run_case("tuning: the gates G1..G5 in order - opt-in, kill switches, the knob's phases, a save, the knobs file, Undo / Restore", [] {
        const Registry reg = test_registry();
        GateInput in = hub_input();
        CHECK(evaluate(in).pass(), "all pass");
        in.sw.opt_in = false;
        CHECK(evaluate(in).first() == "G1 switches: off until turbo_output\\tuning_on.txt exists (opt-in)", evaluate(in).first());
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
        CHECK(rig.logged("gameplay tuning: knobs file") && rig.logged("9 knobs (1 placeholders"), "logged");
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
        CHECK(rig.svc->poll(r) && !r.ok && r.stage == "G1" && r.message.find("no tuning_on.txt") != std::string::npos && w.f32(TuneWorld::kBase) == 0.5f,
              "no opt-in: " + r.message);
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

    run_case("tuning: presets - Turbo's own relative presets, files in turbo_output\\tuning_presets, import format, values from the game defaults", [] {
        const Registry reg = test_registry();
        const fs::path dir = g_out / "tuning_test" / "presets";
        std::error_code ec;
        fs::remove_all(dir, ec);
        std::string err;
        CHECK(ensure_builtin_presets(dir, &err) == 3, "Turbo's three presets written: " + err);
        CHECK(fs::exists(dir / "turbo_high_line_press.json") && fs::exists(dir / "turbo_deep_block.json") && fs::exists(dir / "turbo_passing_errors.json"), "files");
        CHECK(ensure_builtin_presets(dir, &err) == 0, "an unchanged file is not written again");
        // a stale Turbo file is replaced; a user's file of the same name is never touched
        std::ofstream(dir / "turbo_deep_block.json", std::ios::binary | std::ios::trunc)
            << R"({"turbo_tuning_preset": 1, "id": "turbo_deep_block", "name": "Deep compact block", "author": "Turbo", "changes": []})";
        std::ofstream(dir / "turbo_passing_errors.json", std::ios::binary | std::ios::trunc)
            << R"({"turbo_tuning_preset": 1, "name": "My own", "author": "me", "changes": [{"knob": "pressing.intensity", "value": 0.7}]})";
        CHECK(ensure_builtin_presets(dir, &err) == 1, "only the stale Turbo file is rewritten");
        std::ofstream(dir / "broken.json", std::ios::binary | std::ios::trunc) << "{";
        std::vector<std::string> errors;
        const std::vector<Preset> list = load_presets(dir, &errors);
        CHECK(list.size() == 3 && list[0].turbo() && list[1].turbo() && list[2].name == "My own", fmt("listed %zu, Turbo's first", list.size()));
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
        CHECK(pv.values.size() == 4 && pv.values.at("pressing.intensity") == double(0.6f) && pv.values.at("shape.line_height") == 41 &&
                  pv.values.at("marking.tightness") == 100 && pv.values.at("passing.error_ground") == double(0.35f),
              "relative +20 % of 0.5, -10 % of 45 rounded, +100 % of 60 clamped to 100, an absolute value");
        CHECK(has_text(pv.skipped, "is a switch") && has_text(pv.skipped, "Goalkeeper reaction time: no value range") && has_text(pv.skipped, "nope: not in the knobs file") &&
                  has_text(pv.skipped, "Lofted pass error: 7 is outside 0..1"),
              "skipped with reasons");
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
        CHECK(preset_file_name(q) == "My_press.json" && preset_file_name(builtin_presets()[0]) == "turbo_high_line_press.json", "file names");
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

    run_case("tuning: kept values - the keep file, the re-apply plan of a career load skips what is not located or out of range", [] {
        TuneWorld w;
        const Registry reg = test_registry();
        const Lookup lk = lookup_of(w);
        const AnchorMap am = anchors_of(w.mem, reg, lk);
        const auto live = live_of(w.mem, reg, am, lk);
        KeepStore k;
        k.on = true;
        k.build = kBuildKey;
        k.values = {{"pressing.intensity", 0.8}, {"shape.line_height", 45}, {"gk.reaction", 1}, {"marking.tightness", 300}, {"gone", 1}};
        KeepStore back;
        std::string err;
        CHECK(KeepStore::parse(k.to_json(), back, &err) && back.on && back.values.size() == 5, "JSON round trip: " + err);
        Plan p;
        std::vector<std::string> skipped;
        CHECK(make_keep_plan(reg, live, k, p, skipped).empty() && p.kind == PlanKind::Reapply && p.writes.size() == 1 && p.writes[0].id == "pressing.intensity",
              "only the located, changed, in-range value");
        CHECK(skipped.size() == 3 && has_text(skipped, "gone is not in the knobs file") && has_text(skipped, "300 is outside") && has_text(skipped, "Goalkeeper"),
              "skipped: " + std::to_string(skipped.size()));
        k.on = false;
        CHECK(make_keep_plan(reg, live, k, p, skipped) == "keeping is off", "off");
        k.on = true;
        k.build = "6AB9813C-211EF000";
        CHECK(make_keep_plan(reg, live, k, p, skipped).find("are for game build 6AB9813C") != std::string::npos, "another build's values");
        const fs::path f = g_out / "tuning_test" / "keep" / keep_file();
        CHECK(save_keep(f, k, &err) && load_keep(f, back, &err) && back.build == "6AB9813C-211EF000", "file: " + err);
        CHECK(!load_keep(g_out / "tuning_test" / "keep" / "none.json", back, &err), "absent");
    });
}

// --------------------------------------------------------------------------------------------------------------------------- UI
// Scrolls the Gameplay tuning child window so that the item `label` is drawn on screen (the view is taller than the test window)
static const ItemRec* bring_tuning(Ui& ui, const std::string& label) {
    for (int i = 0; i < 12; ++i) {
        if (const ItemRec* r = ui.find(label)) return r;
        for (ImGuiWindow* w : GImGui->Windows)
            if (std::string(w->Name).find("##tuning") != std::string::npos) ImGui::SetScrollY(w, w->Scroll.y + 250.0f);
        ui.frames(2);
    }
    return ui.find(label);
}
static void tuning_top(Ui& ui) {
    for (ImGuiWindow* w : GImGui->Windows)
        if (std::string(w->Name).find("##tuning") != std::string::npos) ImGui::SetScrollY(w, 0.0f);
    ui.frames(2);
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("UI: Competitions > Gameplay tuning - rows and badges, opt-in, Apply in one job + Undo, Restore game defaults, presets, kill switch, kept values", [&] {
        tuning_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {opt_in_file(), kill_switch_file(), master_switch_file(), undo_file(), keep_file()}) fs::remove(out / f, ec);
        fs::remove_all(out / presets_dir(), ec);
        // a preset of the user's for the simulated knobs (Turbo's own name the shipped knobs, which this simulated table does not have)
        fs::create_directories(out / presets_dir(), ec);
        std::ofstream(out / presets_dir() / "Test_press.json", std::ios::binary)
            << R"({"turbo_tuning_preset": 1, "name": "Test press", "author": "me", "changes": [{"knob": "pressing.intensity", "relative_pct": 20},
                 {"knob": "shape.line_height", "relative_pct": 15}, {"knob": "marking.tightness", "relative_pct": 10}, {"knob": "gk.reaction", "relative_pct": 5}]})";
        TuneWorld w;
        TuneRig rig(w, g_out / "tuning_ui");
        rig.out = out;  // the switch files the job re-reads are the window's
        // the service looks at the window's turbo_output (a new rig with that folder)
        {
            Host h;
            h.mem = &w.mem;
            h.build = kBuildKey;
            h.knobs_dir = rig.knobs;
            h.out_dir = out;
            h.lookup = lookup_of(w);
            h.run_on_game_thread = [&rig](std::function<void()> f) { return rig.jobs.run(std::move(f)); };
            h.run_background = [](std::function<void()> f) { f(); };
            h.log = [&rig](const std::string& s) { rig.logs.push_back(s); };
            h.clock_us = [&rig]() { return rig.clock; };
            rig.svc = std::make_shared<CoreService>(std::move(h));
        }
        rig.jobs.inline_run = true;
        app.tuning = rig.svc;
        // Lua's state: in the hub
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
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        const long long gen = app.bridge.state().load_gen;
        CHECK(app.bridge.state().match_phase == "hub" && gen >= 1, "the bridge reports the hub");
        app.request_tab = 3;
        ui.frames(3);
        CHECK(ui.click("Gameplay tuning"), "the Gameplay tuning view");
        ui.frames(3);
        const TuningViewState& s = tuning_view_state();
        CHECK(s.drawn && s.registry_line.find("knobs_6AC07E31.json: 9 knobs (1 placeholder) for game build 6AC07E31-2145C000") != std::string::npos, s.registry_line);
        CHECK(s.groups.size() == 8 && s.groups[0] == "Shape" && s.groups[1] == "Pressing", "the groups in the standard order");
        auto row = [&](const std::string& id) -> const TuningViewState::Row* {
            for (const auto& r : s.rows)
                if (r.id == id) return &r;
            return nullptr;
        };
        const TuningViewState::Row* rp = row("pressing.intensity");
        CHECK(rp && rp->located == "located" && rp->applies == "applies next match" && rp->game == "0.50" && rp->editable, "a located row");
        const TuningViewState::Row* rg = row("passing.error_ground");
        CHECK(rg && rg->applies == "applies live", "a live knob");
        const TuningViewState::Row* rk = row("gk.reaction");
        CHECK(rk && rk->located == "not located" && !rk->editable && rk->why.find("not located on this build (placeholder") == 0, "a placeholder row");
        CHECK(!s.measure_enabled && s.measure_label.find("coming") != std::string::npos, "Measure: coming (disabled)");
        CHECK(s.presets.size() == 4 && s.presets[0].find("(Turbo)") != std::string::npos && s.presets[3] == "Test press" &&
                  fs::exists(out / presets_dir() / "turbo_deep_block.json"),
              "Turbo's own presets are written and listed first, then the user's");
        CHECK(!s.can_apply && s.status.find("G1 switches: off until turbo_output\\tuning_on.txt exists") == 0, "off without the opt-in: " + s.status);
        CHECK(!s.can_undo && !s.can_restore, "nothing to undo or restore");
        // the opt-in
        CHECK(ui.click("Gameplay tuning on (turbo_output\\tuning_on.txt)##tvopt"), "opt-in clicked");
        ui.frames(2);
        CHECK(fs::exists(out / opt_in_file()) && s.opt_in, "opt-in file created");
        CHECK(s.status == "G5 values: nothing changed", "only the values hold it back: " + s.status);
        // two knobs typed in
        const ItemRec* box = bring_tuning(ui, "##tvbox_pressing.intensity");
        CHECK(box && ui.type_into(box, "0.65"), "pressing typed");
        const ItemRec* box2 = bring_tuning(ui, "##tvbox_shape.line_height");
        CHECK(box2 && ui.type_into(box2, "70"), "line height typed");
        tuning_top(ui);
        ui.frames(2);
        CHECK(s.can_apply && s.status == "ready", "ready: " + s.status);
        CHECK(has_text(s.plan, "Pressing intensity: 0.50 -> 0.65 (applies at the next match setup)") && has_text(s.plan, "Defensive line height: 45 % -> 70 %"),
              "Apply will write both");
        CHECK(w.f32(TuneWorld::kBase) == 0.5f, "nothing written yet");
        CHECK(ui.click("Apply##tvapply"), "Apply clicked");
        ui.frames(2);
        CHECK(s.confirm_open && has_text(s.confirm_lines, "0.50 -> 0.65"), "Apply asks first");
        CHECK(ui.click("Apply##tvyes", "Apply gameplay tuning?"), "confirmed");
        ui.frames(4);
        CHECK(w.f32(TuneWorld::kBase) == 0.65f && w.i32(TuneWorld::kBase + 4) == 70, "both written in one job");
        CHECK(rig.jobs.ran == 1, fmt("one job (%d)", rig.jobs.ran));
        UndoRecord u;
        std::string err;
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "written" && u.knobs.size() == 2 && u.load_gen == gen, "undo file: written " + err);
        CHECK(ui.toast_contains("Gameplay tuning written and read back: it reaches the next match set up from the hub"), "toast");
        CHECK(s.can_undo && s.can_restore, "Undo and Restore offered");
        // a pre-match screen: Apply refused for hub-only knobs
        st["match_phase"] = "prematch";
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        ui.frames(2);
        CHECK(!s.can_undo && s.undo_why.find("G2 career phase") == 0, "pre-match: no undo: " + s.undo_why);
        st["match_phase"] = "hub";
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        // Undo
        CHECK(ui.click("Undo last Apply##tvundo"), "Undo clicked");
        ui.frames(4);
        CHECK(w.f32(TuneWorld::kBase) == 0.5f && w.i32(TuneWorld::kBase + 4) == 45, "put back");
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "restored", "undo file: " + u.state);
        CHECK(!s.can_undo, "nothing left to undo");
        // a preset into the sliders, then Apply, then Restore game defaults (with the kill switch present)
        CHECK(ui.click("##tvpreset"), "preset combo");
        ui.frames(2);
        CHECK(ui.click("High line + aggressive press (Turbo)##tvp1"), "Turbo's high line preset picked (Turbo's own are listed by name)");
        ui.frames(2);
        CHECK(ui.click("Load into the sliders##tvload"), "loaded");
        ui.frames(2);
        CHECK(s.preset_line.find("Loaded \"High line + aggressive press\" into the sliders: 1 knob(s) set, 6 skipped (lineband.balanced_max: not in the knobs "
                                 "file") == 0,
              "Turbo's preset on this table: only marking is here: " + s.preset_line);
        CHECK(ui.click("Discard edits##tvdiscard"), "discarded");
        ui.frames(2);
        CHECK(s.plan.empty(), "nothing to write after Discard edits");
        CHECK(ui.click("##tvpreset"), "preset combo");
        ui.frames(2);
        CHECK(ui.click("Test press##tvp3"), "the user's preset picked");
        ui.frames(2);
        CHECK(ui.click("Load into the sliders##tvload"), "loaded");
        ui.frames(2);
        CHECK(s.preset_line.find("Loaded \"Test press\" into the sliders: 3 knob(s) set, 1 skipped (Goalkeeper reaction time: no value range yet)") == 0,
              s.preset_line);
        CHECK(has_text(s.plan, "Pressing intensity: 0.50 -> 0.60") && has_text(s.plan, "Defensive line height: 45 % -> 52 %") &&
                  has_text(s.plan, "Marking tightness: 60 -> 66"),
              "+20 % pressing, +15 % line (rounded), +10 % marking, from the game defaults");
        CHECK(ui.click("Apply##tvapply"), "Apply");
        ui.frames(2);
        CHECK(ui.click("Apply##tvyes", "Apply gameplay tuning?"), "confirmed");
        ui.frames(4);
        CHECK(w.f32(TuneWorld::kBase) == 0.6f && w.i32(TuneWorld::kBase + 4) == 52 && w.u8(TuneWorld::kBase + 8) == 66, "the preset's values written");
        // the kept values: on, then a new career load writes them again
        CHECK(ui.click("Write these values again at every career load (turbo_output\\tuning_keep.json)##tvkeep"), "keep on");
        ui.frames(2);
        KeepStore k;
        CHECK(load_keep(out / keep_file(), k, &err) && k.on && k.values.size() == 3 && k.values["shape.line_height"] == 52, "keep file: " + err);
        std::ofstream(out / kill_switch_file()) << "x";
        ui.frames(130);
        CHECK(!s.can_apply && s.status.find("tuning_off.txt") != std::string::npos && s.can_restore, "kill switch: Apply off, Restore on: " + s.status);
        CHECK(ui.click("Restore game defaults##tvrestore"), "Restore clicked");
        ui.frames(4);
        CHECK(w.f32(TuneWorld::kBase) == 0.5f && w.i32(TuneWorld::kBase + 4) == 45 && w.u8(TuneWorld::kBase + 8) == 60,
              "game defaults back (with the kill switch present)");
        CHECK(load_keep(out / keep_file(), k, &err) && k.on && k.values.empty(), "the restored knobs are no longer kept");
        fs::remove(out / kill_switch_file(), ec);
        // keep two values and load another career: its first hub writes them again (one job)
        k.values = {{"pressing.intensity", 0.7}, {"marking.tightness", 80}};
        save_keep(out / keep_file(), k, &err);
        const int ran = rig.jobs.ran;
        st["load_gen"] = gen + 1;
        st["match_phase"] = "unknown";
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        tuning_reset();  // the keep file is read again
        ui.frames(130);
        CHECK(w.f32(TuneWorld::kBase) == 0.5f && rig.jobs.ran == ran, "not before the hub of the new career load");
        st["match_phase"] = "hub";
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        ui.frames(130);
        CHECK(w.f32(TuneWorld::kBase) == 0.7f && w.u8(TuneWorld::kBase + 8) == 80 && rig.jobs.ran == ran + 1, "the kept values written again in one job");
        bool queued_logged = false;
        for (const auto& l : app.log_lines) queued_logged = queued_logged || l.find("gameplay tuning: kept values queued for this career load") != std::string::npos;
        CHECK(queued_logged, "logged");
        CHECK(load_undo(out / undo_file(), u, &err) && u.state == "written" && u.load_gen == gen + 1, "the re-apply can be undone in its career load");
        ui.frames(130);
        CHECK(rig.jobs.ran == ran + 1, "once per career load");
        // Undo of another career load's write: refused
        st["load_gen"] = gen + 2;
        st["seq"] = st.value("seq", 0LL) + 1;
        k.on = false;
        save_keep(out / keep_file(), k, &err);
        tuning_reset();
        write_state();
        ui.frames(3);
        CHECK(!s.can_undo && s.undo_why.find("another career load") != std::string::npos, "undo across career loads refused: " + s.undo_why);
        // clean up (Competitions back on its first view for the cases after this one)
        tuning_top(ui);
        app.tuning = nullptr;
        for (const char* f : {opt_in_file(), kill_switch_file(), undo_file(), keep_file()}) fs::remove(out / f, ec);
        fs::remove_all(out / presets_dir(), ec);
        st = saved;
        st["seq"] = saved.value("seq", 0LL) + 50;
        write_state();
        tuning_reset();
        ui.click("Live standings (game)");
        ui.frames(2);
    });
}

}  // namespace tuning_test
