// Native tests: gameplay tuning 2.4 (WP-A1, docs/TACTICS_OVERHAUL_DECISION.md 3.1) on a SIMULATED IMAGE: the signature attrib_db_getter,
// the walk from the static manager slot G (mgr -> the current gameplay database -> family -> group -> instance; the prematch handle; tag
// bit 2), the enumeration of the 15 gameplay databases from the heap (a byte copy of one is one database), the identity per instance
// (a wrong TypeInfo refuses that group only; an undeclared copy refuses it), shared instances written once each, the cross-check with the
// name search on the first run per build (tuning_chain_check.json), the profile file, the automatic moments (boot / career_load / hub,
// the prematch guard window, the owner rule across moments), Restore FC 27 defaults and the knob states (proven / located / unlocated).
// Nothing here is a real game address apart from the documented build constants G 0x14C26B128 / get_current_gameplay_attribdb 0x141077070.
// Included by test_tuning.h (namespace tuning_test); chain_cases() runs from tuning_test::run().
#pragma once

static const char* const kChainDbNames[15] = {
    "Fifa/Attribulator/Gameplay/gameplayattribdb",
    "Fifa/Attribulator/Gameplay/databases/simulation/simulation",
    "Fifa/Attribulator/Gameplay/databases/smallsided/smallsided",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/1v1/1v1",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/2v2/2v2",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/3v3/3v3",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/penta/penta",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/kickabouts/kickabouts",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/kickabouts/databases/attackingscenarios/attackingscenarios",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/kickabouts/databases/bocce/bocce",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/kickabouts/databases/bucketball/bucketball",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/kickabouts/databases/closesttothepin/closesttothepin",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/kickabouts/databases/drawtheline/drawtheline",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/kickabouts/databases/foottennis/foottennis",
    "Fifa/Attribulator/Gameplay/databases/smallsided/databases/kickabouts/databases/teamkeepaway/teamkeepaway",
};

// The attribulator as the 10-13 live run and T5's type data describe it, on a simulated image + heap
struct ChainWorld {
    SimMemory mem;
    static constexpr uint64_t kImage = 0x140000000ULL, kImageSize = 0x2145C000ULL;
    static constexpr uint64_t kGetter = 0x141077070ULL, kG = 0x14C26B128ULL;
    static constexpr uint64_t kTypes = 0x14D100000ULL, kTypeStr = 0x14E100000ULL;  // TypeInfoData / TypeInfo / field tables, their names
    static constexpr uint64_t kVtDb = 0x14A900000ULL, kVtDbP = 0x14A900100ULL, kVtGrpF = 0x14A900200ULL, kVtGrpP = 0x14A900300ULL,
                              kVtGrpT = 0x14A900400ULL, kVtLlw = 0x14A9BA448ULL, kVtZonal = 0x14A900600ULL, kVtTts = 0x14A9BF170ULL;
    static constexpr uint64_t kMgr = 0x30000000ULL, kHandleA = 0x30001000ULL, kHandleP = 0x30002000ULL;
    static constexpr uint64_t kDbs = 0x31000000ULL, kGrpF = 0x32000000ULL, kGrpP = 0x33000000ULL, kPmDb = 0x34000000ULL, kPmGrp = 0x34001000ULL;
    static constexpr uint64_t kLlw = 0x35000000ULL, kZonal = 0x35001000ULL, kZonalSim = 0x35002000ULL, kTts = 0x35003000ULL;
    static constexpr uint64_t kNames = 0x36000000ULL, kCopy = 0x37000000ULL, kExtra = 0x37100000ULL;
    static constexpr const char* kLlwName = "Fifa/Attribulator/Gameplay/groups/gp_positioningformation/gp_positioningformation_attackinglinelengthwidth_runtime";
    static constexpr const char* kZonalName = "Fifa/Attribulator/Gameplay/groups/gp_positioning/gp_positioning_zonal_defense_runtime";
    static constexpr const char* kZonalSimName = "Fifa/Attribulator/Gameplay/databases/simulation/groups/gp_positioning/gp_positioning_zonal_defense_runtime";
    static constexpr const char* kTtsName = "Fifa/Attribulator/PreMatch/groups/pm_teamtacticssettings/teamtacticssettings_runtime";
    static constexpr const char* kPmDbName = "Fifa/Attribulator/PreMatch/prematchattribdb";
    uint64_t str_at = kNames, tstr_at = kTypeStr, type_at = kTypes;
    std::map<std::string, uint64_t> ti, td;  // type name -> TypeInfo / TypeInfoData
    uint64_t name_ptr[15] = {};
    uint64_t heap_str(const std::string& s) {
        const uint64_t a = str_at;
        mem.map(a, s.size() + 1);
        mem.write(a, s.c_str(), s.size() + 1);
        str_at += (s.size() + 16) & ~uint64_t(7);
        return a;
    }
    uint64_t image_str(const std::string& s) {
        const uint64_t a = tstr_at;
        mem.map(a, s.size() + 1);
        mem.write(a, s.c_str(), s.size() + 1);
        tstr_at += (s.size() + 16) & ~uint64_t(7);
        return a;
    }
    // a reflected type: TypeInfoData (name, size, field count, signature, field table) and its TypeInfo, in the image
    struct Field {
        std::string name;
        uint32_t offset;
        std::string type;
    };
    void put_type(const std::string& name, uint16_t size, uint32_t sig, const std::vector<Field>& fields = {}) {
        const uint64_t d = type_at, i = type_at + 0x80, f = type_at + 0x100;
        type_at += 0x1000;
        mem.map(d, 0x1000);
        mem.wr(d, image_str(name));
        mem.wr(d + 0x0E, size);
        mem.wr(d + 0x32, uint16_t(fields.size()));
        mem.wr(d + 0x34, sig);
        mem.wr(d + 0x40, f);
        mem.wr(i, d);
        for (size_t k = 0; k < fields.size(); ++k) {
            mem.wr(f + 0x20 * k, image_str(fields[k].name));
            mem.wr(f + 0x20 * k + 0x10, fields[k].offset);
            mem.wr(f + 0x20 * k + 0x18, ti[fields[k].type]);
        }
        td[name] = d;
        ti[name] = i;
    }
    void put_obj(uint64_t o, size_t size, uint64_t vt, const std::string& type, uint64_t name) {
        mem.map(o, size);
        mem.wr(o, vt);
        mem.wr(o + 8, ti[type]);
        if (name) mem.wr(o + 0x18, name);
    }
    void put_db(uint64_t o, int k) {
        put_obj(o, 0x170, kVtDb, "AttribDBRuntime_gameplayattribdb", name_ptr[k]);
        mem.wr(o + 0x128, (kGrpF + 0x1000 * uint64_t(k)) | (k == 4 ? 4u : 0u));  // one tagged family slot (masked & ~4)
        mem.wr(o + 0x38, kGrpP + 0x1000 * uint64_t(k));
    }
    uint64_t db(int k) const { return kDbs + 0x1000 * uint64_t(k); }
    ChainWorld() {
        // the getter's bytes in the image: mov rcx,[rip+G] ... (G = 0x14C26B128)
        mem.map(kGetter, 0x40);
        const int32_t disp = int32_t(kG - (kGetter + 7));
        uint8_t code[27] = {0x48, 0x8B, 0x0D, 0, 0, 0, 0, 0x33, 0xC0, 0x48, 0x85, 0xC9, 0x74, 0x0C, 0x48, 0x8B, 0x11, 0x48, 0x85, 0xD2, 0x74, 0x04, 0x48, 0x8B, 0x42, 0x08, 0xC3};
        std::memcpy(code + 3, &disp, 4);
        mem.write(kGetter, code, sizeof(code));
        for (uint64_t v : {kVtDb, kVtDbP, kVtGrpF, kVtGrpP, kVtGrpT, kVtLlw, kVtZonal, kVtTts}) mem.map(v, 8);
        // the field types, then the types
        put_type("Float32", 4, 0x1);
        put_type("Int32", 4, 0x2);
        put_type("AttribDBRuntime_gameplayattribdb", 0x170, 0xA5106425);
        put_type("AttribDBRuntime_prematchattribdb", 0x70, 0xB8B9B8BB);
        put_type("AttribGroupRuntime_gp_positioningformation", 0x40, 0x7ADE3BD5);
        put_type("AttribGroupRuntime_gp_positioning", 0x1C0, 0xF3A936E7);
        put_type("AttribGroupRuntime_pm_teamtacticssettings", 0x20, 0xF1168603);
        put_type("AttribSchema_gp_positioningformation_linelengthwidth", 0x58, 0x1F261D29,
                 {{"lineWidthModifier", 0x28, "Float32"}, {"LineLengthModifierForBalanced", 0x30, "Float32"}});
        put_type("AttribSchema_gp_positioning_zonal_defense", 0x6C8, 0x3EDBAF9A,
                 {{"TacticDepthThresholdForZonePressing", 0x644, "Float32"}, {"dmfLineOffsetWhenTouchIsOnPar", 0x664, "Float32"}});
        put_type("AttribSchema_pm_teamtacticssettings", 0x50, 0xE7E46EEE,
                 {{"maxDepthThresholdForDeepDefensiveStyle", 0x3C, "Int32"}, {"minDepthThresholdForBalancedDefensiveStyle", 0x40, "Int32"}});
        // G -> the manager -> the handles -> the databases (the gameplay one current: index 0; the prematch handle at +8)
        mem.map(kG, 8);
        mem.wr(kG, kMgr);
        mem.map(kMgr, 0x240);
        mem.wr(kMgr + 0x0, kHandleA);
        mem.wr(kMgr + 0x8, kHandleP);
        mem.wr(kMgr + 0x18, int32_t(0));
        mem.map(kHandleA, 0x10);
        mem.wr(kHandleA + 8, db(0));
        mem.map(kHandleP, 0x10);
        mem.wr(kHandleP + 8, kPmDb);
        for (int k = 0; k < 15; ++k) name_ptr[k] = heap_str(kChainDbNames[k]);
        for (int k = 0; k < 15; ++k) {
            put_db(db(k), k);
            // each database its own group objects; the attacking line instance shared by all 15, the zonal one by 14 (the
            // simulation database has its own copy)
            put_obj(kGrpF + 0x1000 * uint64_t(k), 0x40, kVtGrpF, "AttribGroupRuntime_gp_positioningformation", 0);
            mem.wr(kGrpF + 0x1000 * uint64_t(k) + 0x28, kLlw);
            put_obj(kGrpP + 0x1000 * uint64_t(k), 0x1C0, kVtGrpP, "AttribGroupRuntime_gp_positioning", 0);
            mem.wr(kGrpP + 0x1000 * uint64_t(k) + 0x148, k == 1 ? (kZonalSim | 4) : kZonal);
        }
        put_obj(kPmDb, 0x70, kVtDbP, "AttribDBRuntime_prematchattribdb", heap_str(kPmDbName));
        mem.wr(kPmDb + 0x40, kPmGrp);
        put_obj(kPmGrp, 0x20, kVtGrpT, "AttribGroupRuntime_pm_teamtacticssettings", 0);
        mem.wr(kPmGrp + 0x18, kTts);
        put_obj(kLlw, 0x60, kVtLlw, "AttribSchema_gp_positioningformation_linelengthwidth", heap_str(kLlwName));
        mem.wr(kLlw + 0x28, 1.0f);
        mem.wr(kLlw + 0x30, 0.84f);
        put_obj(kZonal, 0x6D0, kVtZonal, "AttribSchema_gp_positioning_zonal_defense", heap_str(kZonalName));
        mem.wr(kZonal + 0x644, 0.31f);
        mem.wr(kZonal + 0x664, 8.0f);
        put_obj(kZonalSim, 0x6D0, kVtZonal, "AttribSchema_gp_positioning_zonal_defense", heap_str(kZonalSimName));
        mem.wr(kZonalSim + 0x644, 0.31f);
        mem.wr(kZonalSim + 0x664, 9.0f);  // the simulation copy's own FC 27 value (copy_defaults)
        put_obj(kTts, 0x50, kVtTts, "AttribSchema_pm_teamtacticssettings", heap_str(kTtsName));
        mem.wr(kTts + 0x3C, int32_t(30));
        mem.wr(kTts + 0x40, int32_t(31));
    }
    // a byte copy of a database object (a heap buffer that holds the same bytes: the same Name pointer, the same groups)
    void byte_copy_of(int k, uint64_t at) {
        std::vector<uint8_t> b;
        mem.read_block(db(k), 0x170, b);
        mem.map(at, 0x170);
        mem.write(at, b.data(), b.size());
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
    bool in_image(uint64_t base) const { return base >= kImage && base < kImage + kImageSize; }
    Lookup lookup(bool heap = true, bool refs = true) {
        Lookup lk;
        lk.image_base = kImage;
        lk.image_size = kImageSize;
        lk.signature = [](const std::string& n) -> uint64_t { return n == "attrib_db_getter" ? kG : 0; };
        if (heap)
            lk.heap_qwords = [this](uint64_t value, size_t max_hits) {
                std::vector<uint64_t> out;
                for (const auto& pg : mem.pages) {
                    const uint64_t base = pg.first * SimMemory::kPage;
                    if (in_image(base)) continue;
                    for (const auto& h : find_qwords(pg.second.data(), pg.second.size(), base, {value}, max_hits)) out.push_back(h.second);
                }
                return out;
            };
        if (refs)
            lk.refs = [this](const std::vector<std::string>& names, bool in_img) {
                std::map<std::string, std::vector<uint64_t>> out;
                std::map<uint64_t, size_t> strings;
                for (const auto& pg : mem.pages) {
                    const uint64_t base = pg.first * SimMemory::kPage;
                    if (in_image(base) != in_img) continue;
                    for (const auto& h : find_cstrings(pg.second.data(), pg.second.size(), base, names, 64)) strings[h.second] = h.first;
                }
                std::set<uint64_t> values;
                for (const auto& kv : strings) values.insert(kv.first);
                for (const auto& pg : mem.pages) {
                    const uint64_t base = pg.first * SimMemory::kPage;
                    if (in_image(base) != in_img) continue;
                    for (const auto& h : find_qwords(pg.second.data(), pg.second.size(), base, values, 64)) out[names[strings[h.first]]].push_back(h.second);
                }
                return out;
            };
        return lk;
    }
};

// The knobs file of the chain world: the attribute manager, three groups (the attacking line shared by all databases, zonal_defense with
// its simulation copy, the prematch depth bands) and one unverified group; knobs written at the three moments
static std::string chain_knobs(const std::string& zonal_extra = "", bool zonal_verified = true) {
    std::string dbs;
    for (int k = 0; k < 15; ++k) dbs += std::string(k ? ", " : "") + "\"" + kChainDbNames[k] + "\"";
    return std::string(R"({
  "schema": "turbo-tuning-knobs/1", "build": "6AC07E31-2145C000", "version": 4, "note": "chain world",
  "attrib_manager": {"signature": "attrib_db_getter", "current": ["0x0", "0x8"], "prematch_slot": "0x8", "handle_db": "0x8", "mgr_size": "0x240",
    "gameplay_db_type": "AttribDBRuntime_gameplayattribdb", "gameplay_db_signature": "0xa5106425",
    "prematch_db_type": "AttribDBRuntime_prematchattribdb", "prematch_db_signature": "0xb8b9b8bb",
    "gameplay_dbs": [)") + dbs + R"(], "prematch_dbs": ["Fifa/Attribulator/PreMatch/prematchattribdb"]},
  "locators": [
    {"name": "attrib.llw", "kind": "attrib", "type": "AttribSchema_gp_positioningformation_linelengthwidth", "type_signature": "0x1f261d29", "type_size": "0x58",
     "type_fields": 2, "db_type": "AttribDBRuntime_gameplayattribdb", "db_type_signature": "0xa5106425",
     "group_type": "AttribGroupRuntime_gp_positioningformation", "group_type_signature": "0x7ade3bd5", "family_slot": "0x128", "group_slot": "0x28",
     "head": "typeinfo", "head_rva": "0xa9ba448",
     "variants": [{"name": "base", "instance": "Fifa/Attribulator/Gameplay/groups/gp_positioningformation/gp_positioningformation_attackinglinelengthwidth_runtime",
                   "db": "Fifa/Attribulator/Gameplay/gameplayattribdb"}], "verified": true},
    {"name": "attrib.zonal", "kind": "attrib", "type": "AttribSchema_gp_positioning_zonal_defense", "type_signature": "0x3edbaf9a", "type_size": "0x6c8",
     "type_fields": 2, "db_type": "AttribDBRuntime_gameplayattribdb", "group_type": "AttribGroupRuntime_gp_positioning",
     "group_type_signature": "0xf3a936e7", "family_slot": "0x38", "group_slot": "0x148",
     "variants": [{"name": "base", "instance": "Fifa/Attribulator/Gameplay/groups/gp_positioning/gp_positioning_zonal_defense_runtime",
                   "db": "Fifa/Attribulator/Gameplay/gameplayattribdb"},
                  {"name": "simulation", "instance": "Fifa/Attribulator/Gameplay/databases/simulation/groups/gp_positioning/gp_positioning_zonal_defense_runtime",
                   "db": "Fifa/Attribulator/Gameplay/databases/simulation/simulation"})" + zonal_extra + R"(], "verified": )" +
           (zonal_verified ? "true" : "false") + R"(},
    {"name": "attrib.bands", "kind": "attrib", "type": "AttribSchema_pm_teamtacticssettings", "type_signature": "0xe7e46eee", "type_size": "0x50",
     "type_fields": 2, "db_type": "AttribDBRuntime_prematchattribdb", "group_type": "AttribGroupRuntime_pm_teamtacticssettings",
     "family_slot": "0x40", "group_slot": "0x18", "head_rva": "0xa9bf170",
     "variants": [{"name": "base", "instance": "Fifa/Attribulator/PreMatch/groups/pm_teamtacticssettings/teamtacticssettings_runtime",
                   "db": "Fifa/Attribulator/PreMatch/prematchattribdb"}], "verified": true},
    {"name": "attrib.unverified", "kind": "attrib", "type": "AttribSchema_gp_positioning_zonal_defense", "type_signature": "0x3edbaf9a",
     "type_size": "0x6c8", "type_fields": 2, "db_type": "AttribDBRuntime_gameplayattribdb", "family_slot": "0x38", "group_slot": "0x148",
     "variants": [{"name": "base", "instance": "Fifa/Attribulator/Gameplay/groups/gp_positioning/gp_positioning_zonal_defense_runtime"},
                  {"name": "simulation", "instance": "Fifa/Attribulator/Gameplay/databases/simulation/groups/gp_positioning/gp_positioning_zonal_defense_runtime"}],
     "verified": false}
  ],
  "knobs": [
    {"id": "shape.width_in", "label": "Team width with the ball", "group": "Shape with the ball", "collection": "llw", "field": "lineWidthModifier",
     "type": "f32", "min": 0.6, "max": 1.7, "default": 1.0, "step": 0.01, "locator": "attrib.llw", "offset": "0x28",
     "when": ["boot", "career_load", "hub"], "verified": true,
     "proof": {"session": "P1", "evidence": "EV/2026-10-14/TEST_25.md#width_in"}, "slider": true},
    {"id": "shape.length_in", "label": "Team length with the ball", "group": "Shape with the ball", "collection": "llw", "field": "LineLengthModifierForBalanced",
     "type": "f32", "min": 0.5, "max": 1.5, "default": 0.84, "locator": "attrib.llw", "offset": "0x30", "when": ["boot", "career_load", "hub"], "verified": true},
    {"id": "shape.gap_dmf", "label": "Gap to the holding midfielders", "group": "Shape without the ball", "collection": "zonal", "field": "dmfLineOffsetWhenTouchIsOnPar",
     "type": "f32", "unit": "m", "min": 4, "max": 24, "default": 8, "copy_defaults": {"simulation": 9}, "locator": "attrib.zonal", "offset": "0x664",
     "when": ["boot", "career_load", "hub"], "verified": )" + (zonal_verified ? "true" : "false") + R"(},
    {"id": "pressing.trigger_depth", "label": "Pressing starts at depth", "group": "Pressing", "collection": "zonal", "field": "TacticDepthThresholdForZonePressing",
     "type": "f32", "min": 0, "max": 1, "default": 0.31, "locator": "attrib.zonal", "offset": "0x644", "when": ["hub"], "verified": )" +
           (zonal_verified ? "true" : "false") + R"(},
    {"id": "lineband.deep_max", "label": "Deep: up to depth", "group": "Defensive line depth bands", "collection": "bands", "field": "maxDepthThresholdForDeepDefensiveStyle",
     "type": "i32", "min": 0, "max": 100, "default": 30, "locator": "attrib.bands", "offset": "0x3c", "below": ["lineband.balanced_min"],
     "when": ["boot", "career_load", "hub"], "verified": true},
    {"id": "lineband.balanced_min", "label": "Balanced: from depth", "group": "Defensive line depth bands", "collection": "bands",
     "field": "minDepthThresholdForBalancedDefensiveStyle", "type": "i32", "min": 0, "max": 100, "default": 31, "locator": "attrib.bands", "offset": "0x40",
     "when": ["boot", "career_load", "hub"], "verified": true},
    {"id": "marking.unverified", "label": "Unverified knob", "group": "Marking", "collection": "zonal", "field": "dmfLineOffsetWhenTouchIsOnPar",
     "type": "f32", "min": 4, "max": 24, "default": 8, "copy_defaults": {"simulation": 9}, "locator": "attrib.unverified", "offset": "0x664", "verified": false}
  ]
})";
}

// A service on the chain world (jobs on a test dispatcher, a wall clock of the test's choosing)
struct ChainRig {
    ChainWorld& w;
    TJobs jobs;
    double clock = 1000.0;
    long long wall = 1760000000;
    std::vector<std::string> logs;
    std::string throw_on;  // the test host's log throws on a line starting with it (an exception inside a background locate)
    fs::path knobs, out;
    std::shared_ptr<CoreService> svc;
    ChainRig(ChainWorld& world, const fs::path& dir, const std::string& text, Lookup lk, Memory* mem = nullptr, bool fresh = true) : w(world) {
        knobs = dir / "tuning";
        out = dir / "turbo_output";
        std::error_code ec;
        if (fresh) fs::remove_all(dir, ec);
        fs::create_directories(knobs, ec);
        fs::create_directories(out, ec);
        std::ofstream(knobs / "knobs_6AC07E31.json", std::ios::binary | std::ios::trunc) << text;
        Host h;
        h.mem = mem ? mem : &w.mem;
        h.build = kBuildKey;
        h.knobs_dir = knobs;
        h.out_dir = out;
        h.lookup = lk;
        h.run_on_game_thread = [this](std::function<void()> f) { return jobs.run(std::move(f)); };
        h.run_background = [](std::function<void()> f) { f(); };
        h.log = [this](const std::string& s) {
            logs.push_back(s);
            if (!throw_on.empty() && s.find(throw_on) == 0) throw std::runtime_error("test: the log failed");
        };
        h.clock_us = [this]() { return clock; };
        h.wall_s = [this]() { return wall; };
        svc = std::make_shared<CoreService>(std::move(h));
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
    int count(const std::string& part) const {
        int n = 0;
        for (const auto& l : logs) n += l.find(part) != std::string::npos ? 1 : 0;
        return n;
    }
    std::string line(const std::string& part) const {
        for (const auto& l : logs)
            if (l.find(part) != std::string::npos) return l;
        return "";
    }
    void profile(bool on, const std::map<std::string, double>& v, const std::string& preset = "custom") {
        tuning::Profile p;
        p.on = on;
        p.preset = preset;
        p.values = v;
        std::string err;
        save_profile(out / profile_file(), p, &err);
    }
    void touch(const char* name) { std::ofstream(out / name) << "x"; }
    void remove(const char* name) {
        std::error_code ec;
        fs::remove(out / name, ec);
    }
};

// Counts the writes per address (shared instances are written once each per job)
struct CountingWrites : Memory {
    SimMemory& m;
    std::map<uint64_t, int> writes;
    explicit CountingWrites(SimMemory& mm) : m(mm) {}
    bool read(uint64_t a, void* out, size_t n) override { return m.read(a, out, n); }
    bool write(uint64_t a, const void* in, size_t n) override {
        ++writes[a];
        return m.write(a, in, n);
    }
};

static Context chain_ctx(const std::string& phase, bool in_cm, long long gen = 3, long long prematch_t = 0) {
    Context c;
    c.session = "s1";
    c.load_gen = gen;
    c.phase = phase;
    c.in_cm = in_cm;
    c.prematch_t = prematch_t;
    c.bridge = true;
    return c;
}
// The main menu as Lua reports it (M1): Lua's bridge running, no career, its state written `age` s ago
static Context menu_ctx(long long wall, long long age = 1, long long gen = 1) {
    Context c = chain_ctx("unknown", false, gen);
    c.front_end_t = wall - age;
    return c;
}

static Plan profile_plan(ChainRig& r, const std::map<std::string, double>& values, const std::string& moment, std::vector<std::string>* skipped = nullptr) {
    tuning::Profile p;
    p.on = true;
    p.values = values;
    Plan plan;
    std::vector<std::string> sk;
    make_profile_plan(*r.svc->registry(), r.live(), p, moment, plan, sk);
    if (skipped) *skipped = sk;
    return plan;
}

static void chain_cases() {
    run_case("tuning 2.4: the signature attrib_db_getter - both built-in tables, rip at 0 resolves the manager slot G, an offset checked by its bytes", [] {
        for (const std::string& b : builtin_builds()) {
            const SignatureTable* t = builtin_signature_table(b);
            const Signature* s = t ? t->find("attrib_db_getter") : nullptr;
            CHECK(s && s->resolve == "rip" && s->offset == 0 && s->expect == "48 8B 0D" && !s->exact_build && s->adapt_optional && signature_offset_checked(*s),
                  "build " + b + ": attrib_db_getter is a checked rip signature, kept by auto-adapt, optional (review M4)");
        }
        // a title update where only attrib_db_getter misses: every other hook is adopted (the chain is off there, the name search remains)
        {
            const SignatureTable ad = adapt_signature_table(*newest_builtin_table(), "6B000000-21500000");
            std::vector<SigResult> rs;
            for (const auto& s : ad.sigs)
                if (!s.pattern.empty()) {
                    SigResult x;
                    x.name = s.name;
                    x.state = s.name == "attrib_db_getter" ? SigState::Ambiguous : SigState::Found;
                    rs.push_back(x);
                }
            const AdaptDecision d = decide_adapt(ad, rs);
            CHECK(d.adopt && d.optional_missing == 1 && d.reason.find("attrib_db_getter") != std::string::npos, "a missing getter never blocks the adapted table: " + d.reason);
        }
        ChainWorld w;
        std::vector<uint8_t> code;
        w.mem.read_block(ChainWorld::kGetter, 0x40, code);
        const SignatureTable* t = builtin_signature_table(kBuildKey);
        const SigResult r = resolve_signature(*t->find("attrib_db_getter"), code.data(), code.size(), ChainWorld::kGetter);
        CHECK(r.state == SigState::Found && r.match == ChainWorld::kGetter && r.address == ChainWorld::kG, fmt("resolved to G 0x%llX: %s", (unsigned long long)r.address, r.error.c_str()));
        code[0x10] = 0x90;  // another function's bytes: no match
        CHECK(resolve_signature(*t->find("attrib_db_getter"), code.data(), code.size(), ChainWorld::kGetter).state == SigState::Missing, "a changed body is not found");
    });

    run_case("tuning 2.4: the knobs file v4 fields - attrib_manager, when moments, live, proof, slider; refusals; states; registry_json round trip", [] {
        Registry r;
        std::string err;
        CHECK(parse_registry(chain_knobs(), r, err), "the chain world's knobs file parses: " + err);
        CHECK(r.manager.present && r.manager.signature == "attrib_db_getter" && r.manager.current == std::vector<int64_t>({0, 8}) &&
                  r.manager.prematch_slot == 8 && r.manager.gameplay_dbs.size() == 15 && r.manager.prematch_dbs.size() == 1 &&
                  r.manager.gameplay_db_signature == 0xA5106425 && r.manager.prematch_db_signature == 0xB8B9B8BB,
              "attrib_manager read");
        const Knob* w = r.knob("shape.width_in");
        const Knob* l = r.knob("shape.length_in");
        const Knob* t = r.knob("pressing.trigger_depth");
        CHECK(w && w->proven() && w->slider && w->proof.session == "P1" && w->when == std::vector<std::string>({"boot", "career_load", "hub"}), "a proven knob");
        CHECK(l && !l->proven() && !l->slider && l->allows_moment("boot") && l->allows("hub") && !l->allows("prematch") && !l->allows("match"),
              "a located knob: the three moments; hub writes, no prematch / match");
        CHECK(t && !t->allows_moment("boot") && t->allows_moment("hub") && !t->allows_moment("career_load"), "when [hub] only");
        Knob live;
        live.live = true;
        live.when = {"hub"};
        CHECK(live.allows("prematch") && live.allows("match") && live.allows("hub") && !live.allows_moment("boot"), "live: prematch and match too");
        CHECK(r.locator("attrib.llw")->chain_capable() && r.locator("attrib.bands")->chain_capable() && r.manager.prematch(*r.locator("attrib.bands")) &&
                  !r.manager.prematch(*r.locator("attrib.llw")),
              "chain-capable locators, the prematch one by its database type");
        Registry back;
        CHECK(parse_registry(registry_json(r), back, err) && back.manager.gameplay_dbs == r.manager.gameplay_dbs && back.knob("shape.width_in")->proven() &&
                  back.knob("shape.width_in")->slider && back.knob("shape.length_in")->when == l->when && back.manager.prematch_slot == 8,
              "registry_json round trip: " + err);
        auto refused = [](const std::string& text, const std::string& part) {
            Registry x;
            std::string e;
            return !parse_registry(text, x, e) && e.find(part) != std::string::npos;
        };
        auto subst = [](std::string s, const std::string& from, const std::string& to) {
            const size_t at = s.find(from);
            return at == std::string::npos ? s : s.replace(at, from.size(), to);
        };
        const std::string bad = chain_knobs();
        CHECK(refused(subst(bad, "\"when\": [\"boot\"", "\"when\": [\"kickoff\""), "unknown moment or phase"), "an unknown moment");
        CHECK(refused(subst(bad, "\"evidence\": \"EV/2026-10-14/TEST_25.md#width_in\"", "\"evidence\": \"\""), "proof must be"), "an empty evidence");
        CHECK(refused(subst(bad, "\"signature\": \"attrib_db_getter\"", "\"signature\": \"\""), "attrib_manager: signature"), "a manager without its signature");
        CHECK(refused(subst(bad, "\"prematch_slot\": \"0x8\"", "\"prematch_slot\": \"0x9\""), "prematch_slot"), "a misaligned slot");
        // the view's states
        KnobLive located;
        located.located = located.writable = true;
        KnobLive gone;
        CHECK(knob_state(*w, &located) == KnobState::Proven && knob_state(*l, &located) == KnobState::Located && knob_state(*w, &gone) == KnobState::Unlocated &&
                  knob_state(*w, nullptr) == KnobState::Unlocated && std::string(knob_state_name(KnobState::Located)) == "located",
              "proven / located / unlocated");
        Knob slider_only = *l;
        slider_only.slider = true;
        CHECK(knob_state(slider_only, &located) == KnobState::Located, "a slider without a proof stays read-only");
    });

    run_case("tuning 2.4: the walk from G - the manager, the current database, the prematch handle (and a moved one), tag bit 2, transient refusals", [] {
        ChainWorld w;
        Registry reg;
        std::string err;
        parse_registry(chain_knobs(), reg, err);
        const Lookup lk = w.lookup();
        AttribDbs dbs;
        bool transient = true;
        std::string why = walk_manager(w.mem, reg.manager, lk, dbs, transient);
        CHECK(why.empty() && dbs.g == ChainWorld::kG && dbs.mgr == ChainWorld::kMgr && dbs.current == w.db(0) && dbs.index == 0 &&
                  dbs.current_name == kChainDbNames[0] && dbs.prematch == ChainWorld::kPmDb && dbs.prematch_slot == 8 && dbs.dbs.size() == 2,
              "G -> mgr -> [[mgr+0]+8] = gameplayattribdb, [[mgr+8]+8] = prematchattribdb: " + why);
        // the prematch handle at another slot: every manager slot is tried, the one found is reported
        w.mem.wr(ChainWorld::kMgr + 0x8, uint64_t(0));
        w.mem.wr(ChainWorld::kMgr + 0x30, ChainWorld::kHandleP);
        why = walk_manager(w.mem, reg.manager, lk, dbs, transient);
        CHECK(why.empty() && dbs.prematch == ChainWorld::kPmDb && dbs.prematch_slot == 0x30 && dbs.summary().find("prematch database at mgr+0x30") != std::string::npos,
              "the prematch handle found at mgr+0x30: " + dbs.summary());
        // the current database's handle slot tagged (bit 2): masked
        w.mem.wr(ChainWorld::kMgr, ChainWorld::kHandleA | 4);
        why = walk_manager(w.mem, reg.manager, lk, dbs, transient);
        CHECK(why.empty() && dbs.current == w.db(0), "a tagged handle slot is masked");
        // the game is starting: no manager yet -> transient
        w.mem.wr(ChainWorld::kG, uint64_t(0));
        why = walk_manager(w.mem, reg.manager, lk, dbs, transient);
        CHECK(!why.empty() && transient && why.find("does not exist yet") != std::string::npos, "no manager yet: transient: " + why);
        w.mem.wr(ChainWorld::kG, ChainWorld::kMgr);
        w.mem.wr(ChainWorld::kHandleA + 8, uint64_t(0));
        why = walk_manager(w.mem, reg.manager, lk, dbs, transient);
        CHECK(!why.empty() && transient && why.find("no current gameplay database yet") != std::string::npos, "no current database yet: transient: " + why);
        // no current database, but another manager slot's handle holds a gameplay database (a list at +0x110): it seeds the enumeration
        w.mem.map(ChainWorld::kHandleA + 0x100, 0x10);
        w.mem.wr(ChainWorld::kHandleA + 0x108, w.db(3));
        w.mem.wr(ChainWorld::kMgr + 0x110, ChainWorld::kHandleA + 0x100);
        why = walk_manager(w.mem, reg.manager, lk, dbs, transient);
        CHECK(why.empty() && dbs.seed_slot == 0x110 && dbs.current == w.db(3) && dbs.summary().find("no current database yet (seeded from mgr+0x110: ") != std::string::npos,
              "seeded from mgr+0x110: " + why + " " + dbs.summary());
        enumerate_dbs(w.mem, reg.manager, lk, {0x128, 0x38}, dbs);
        CHECK(dbs.enumerated && dbs.dbs.front().addr == w.db(3), "the 15 databases found from the seed: " + dbs.enum_why);
        w.mem.wr(ChainWorld::kMgr + 0x110, uint64_t(0));
        // the current database is something else: not transient (the chain is refused for the session, the name search takes over)
        w.mem.wr(ChainWorld::kHandleA + 8, ChainWorld::kLlw);
        why = walk_manager(w.mem, reg.manager, lk, dbs, transient);
        CHECK(!why.empty() && !transient && why.find("is a AttribSchema_gp_positioningformation_linelengthwidth, not a AttribDBRuntime_gameplayattribdb") != std::string::npos,
              "a wrong object behind the handle: refused: " + why);
        Lookup none = lk;
        none.signature = [](const std::string&) -> uint64_t { return 0; };
        why = walk_manager(w.mem, reg.manager, none, dbs, transient);
        CHECK(why == "signature attrib_db_getter was not found on this game build" && !transient, why);
    });

    run_case("tuning 2.4: the 16 databases and the copies - 15 gameplay + 1 prematch, a byte copy is one database, shared instances once, the simulation copy", [] {
        ChainWorld w;
        w.byte_copy_of(2, ChainWorld::kCopy);  // a heap buffer with the smallsided database's bytes
        Registry reg;
        std::string err;
        parse_registry(chain_knobs(), reg, err);
        const Lookup lk = w.lookup();
        AttribDbs dbs;
        bool transient = false;
        CHECK(walk_manager(w.mem, reg.manager, lk, dbs, transient).empty(), "walked");
        enumerate_dbs(w.mem, reg.manager, lk, {0x128, 0x38}, dbs);
        int gp = 0, pm = 0;
        for (const AttribDb& d : dbs.dbs) (d.prematch ? pm : gp)++;
        CHECK(dbs.enumerated && gp == 15 && pm == 1 && dbs.dbs.front().addr == w.db(0) && dbs.enum_why == "15 of 15 gameplay databases",
              fmt("15 gameplay + 1 prematch (%d + %d): ", gp, pm) + dbs.enum_why);
        const Anchor llw = resolve_attrib_chain(w.mem, reg.manager, *reg.locator("attrib.llw"), dbs, lk);
        CHECK(llw.ok && llw.path == "chain" && llw.copies.size() == 1 && llw.copies[0].base == ChainWorld::kLlw && llw.copies[0].db == w.db(0) &&
                  llw.copies[0].db_name == kChainDbNames[0] && llw.layout.field.count("lineWidthModifier") && llw.type_ptr == ChainWorld::kVtLlw,
              "the attacking line: ONE instance shared by the 15 databases (one tagged slot masked): " + llw.why);
        const Anchor zonal = resolve_attrib_chain(w.mem, reg.manager, *reg.locator("attrib.zonal"), dbs, lk);
        CHECK(zonal.ok && zonal.copies.size() == 2 && zonal.copies[0].variant == "base" && zonal.copies[0].base == ChainWorld::kZonal &&
                  zonal.copies[1].variant == "simulation" && zonal.copies[1].base == ChainWorld::kZonalSim && zonal.copies[1].db == w.db(1),
              "zonal_defense: the base copy (14 databases) and the simulation copy (1): " + zonal.why);
        const Anchor bands = resolve_attrib_chain(w.mem, reg.manager, *reg.locator("attrib.bands"), dbs, lk);
        CHECK(bands.ok && bands.copies.size() == 1 && bands.copies[0].base == ChainWorld::kTts && bands.copies[0].db == ChainWorld::kPmDb, "the prematch group: " + bands.why);
        // locate_knob re-checks every copy through its database (chase): the values
        AnchorMap am = {{"attrib.llw", llw}, {"attrib.zonal", zonal}, {"attrib.bands", bands}};
        KnobAt at;
        CHECK(locate_knob(w.mem, reg, *reg.knob("shape.gap_dmf"), am, lk, at).empty() && at.copies.size() == 2 && at.copies[0].value == 8.0 &&
                  at.copies[1].value == 9.0,
              "gap_dmf: base 8, simulation 9");
        CHECK(locate_knob(w.mem, reg, *reg.knob("lineband.deep_max"), am, lk, at).empty() && at.value == 30, "deep_max 30");
        // without the enumeration (no heap search): the shared groups are located, the simulation copy is not reachable (not refused)
        AttribDbs only;
        walk_manager(w.mem, reg.manager, lk, only, transient);
        enumerate_dbs(w.mem, reg.manager, w.lookup(false), {0x128, 0x38}, only);
        CHECK(!only.enumerated && only.enum_why.find("current gameplay database only") != std::string::npos, only.enum_why);
        const Anchor z2 = resolve_attrib_chain(w.mem, reg.manager, *reg.locator("attrib.zonal"), only, lk);
        CHECK(!z2.ok && !z2.refused && z2.why.find("variant simulation is not reachable from the current database") == 0, z2.why);
        CHECK(resolve_attrib_chain(w.mem, reg.manager, *reg.locator("attrib.llw"), only, lk).ok, "the shared group still located");
        // a database missing from the heap: partial
        w.mem.wr(w.db(9) + 8, uint64_t(0x1234));
        AttribDbs part;
        walk_manager(w.mem, reg.manager, lk, part, transient);
        enumerate_dbs(w.mem, reg.manager, lk, {0x128, 0x38}, part);
        CHECK(!part.enumerated && part.enum_why.find("14 of 15 gameplay databases; not found: " + std::string(kChainDbNames[9])) == 0, part.enum_why);
    });

    run_case("tuning 2.4: identity per instance - a wrong TypeInfo, vtable, Name or an undeclared copy refuses THAT group (for the session), the others stay", [] {
        Registry reg;
        std::string err;
        parse_registry(chain_knobs(), reg, err);
        auto run = [&](ChainWorld& w, const std::string& loc) {
            const Lookup lk = w.lookup();
            AttribDbs dbs;
            bool tr = false;
            walk_manager(w.mem, reg.manager, lk, dbs, tr);
            enumerate_dbs(w.mem, reg.manager, lk, {0x128, 0x38}, dbs);
            return resolve_attrib_chain(w.mem, reg.manager, *reg.locator(loc), dbs, lk);
        };
        {
            ChainWorld w;  // the simulation zonal instance carries the attacking line's TypeInfo
            w.mem.wr(ChainWorld::kZonalSim + 8, w.ti["AttribSchema_gp_positioningformation_linelengthwidth"]);
            const Anchor z = run(w, "attrib.zonal");
            CHECK(!z.ok && z.refused && z.why.find("is a AttribSchema_gp_positioningformation_linelengthwidth, not a AttribSchema_gp_positioning_zonal_defense") !=
                                               std::string::npos,
                  "wrong TypeInfo: zonal refused: " + z.why);
            CHECK(run(w, "attrib.llw").ok && run(w, "attrib.bands").ok, "the other groups stay");
        }
        {
            ChainWorld w;  // the attacking line's vtable is not FC27.exe + head_rva
            w.mem.wr(ChainWorld::kLlw, ChainWorld::kVtZonal);
            const Anchor a = run(w, "attrib.llw");
            CHECK(!a.ok && a.refused && a.why.find("its vtable is") != std::string::npos, a.why);
        }
        {
            ChainWorld w;  // the kickabouts database holds a third, undeclared zonal copy
            w.put_obj(ChainWorld::kExtra, 0x6D0, ChainWorld::kVtZonal, "AttribSchema_gp_positioning_zonal_defense",
                      w.heap_str("Fifa/Attribulator/Gameplay/databases/smallsided/databases/kickabouts/groups/gp_positioning/gp_positioning_zonal_defense_runtime"));
            w.mem.wr(ChainWorld::kGrpP + 0x1000 * 7 + 0x148, ChainWorld::kExtra);
            const Anchor z = run(w, "attrib.zonal");
            CHECK(!z.ok && z.refused && z.why.find("an undeclared copy") == 0, "an undeclared copy: " + z.why);
        }
        {
            ChainWorld w;  // the group object is of another group type
            w.mem.wr(ChainWorld::kGrpF + 0x1000 * 3 + 8, w.ti["AttribGroupRuntime_gp_positioning"]);
            const Anchor a = run(w, "attrib.llw");
            CHECK(!a.ok && a.refused && a.why.find("is a AttribGroupRuntime_gp_positioning, not a AttribGroupRuntime_gp_positioningformation") != std::string::npos, a.why);
        }
        {
            ChainWorld w;  // the type's signature differs (another build's layout)
            w.mem.wr(w.td["AttribSchema_pm_teamtacticssettings"] + 0x34, uint32_t(0xDEADBEEF));
            const Anchor b = run(w, "attrib.bands");
            CHECK(!b.ok && b.refused && b.why.find("has signature 0xDEADBEEF, not 0xE7E46EEE") != std::string::npos, b.why);
        }
        {
            ChainWorld w;  // after the locate the instance is freed and reused: the job's chase says "locate again", nothing written
            const Lookup lk = w.lookup();
            AttribDbs dbs;
            bool tr = false;
            walk_manager(w.mem, reg.manager, lk, dbs, tr);
            enumerate_dbs(w.mem, reg.manager, lk, {0x128, 0x38}, dbs);
            AnchorMap am = {{"attrib.llw", resolve_attrib_chain(w.mem, reg.manager, *reg.locator("attrib.llw"), dbs, lk)}};
            w.mem.wr(ChainWorld::kLlw + 8, w.ti["AttribSchema_gp_positioning_zonal_defense"]);
            KnobAt at;
            const std::string why = locate_knob(w.mem, reg, *reg.knob("shape.width_in"), am, lk, at);
            CHECK(why.find("no longer holds its TypeInfo: locate again") != std::string::npos, why);
            w.mem.wr(ChainWorld::kLlw + 8, w.ti["AttribSchema_gp_positioningformation_linelengthwidth"]);
            w.mem.wr(ChainWorld::kHandleA + 8, w.db(1));  // the current database switched: the copy's own database path still holds it
            CHECK(locate_knob(w.mem, reg, *reg.knob("shape.width_in"), am, lk, at).empty(), "re-checked through its own database");
        }
    });

    run_case("tuning 2.4: the service - one chain locate (log line), the cross-check with the name search on the first run per build, the fallback", [] {
        ChainWorld w;
        ChainRig r(w, g_out / "tuning_chain_svc", chain_knobs(), w.lookup());
        LocateStatus ls = r.svc->locate_status();
        CHECK(ls.ready && ls.path == "chain" && ls.groups == 3 && ls.knobs == 6, fmt("ready through the chain: %d groups, %d knobs (%s)", ls.groups, ls.knobs, ls.why.c_str()));
        const std::string ok = r.line("tuning: chain locate ok ");
        CHECK(ok.find("tuning: chain locate ok 3 groups 6 knobs ") == 0 && ok.substr(ok.size() - 3) == " ms", "the log line: " + ok);
        CHECK(r.logged("tuning: attribute manager: G 0x14C26B128, mgr 0x30000000, current database Fifa/Attribulator/Gameplay/gameplayattribdb (index 0), "
                       "prematch database at mgr+0x8, 15 of 15 gameplay databases"),
              "the manager's summary");
        CHECK(r.logged("tuning: chain attrib.unverified (not verified in the knobs file: not located, never written): base 0x35001000, simulation 0x35002000 (identity ok)"),
              "an unverified group is checked and logged only");
        CHECK(r.logged("tuning: chain cross-check with the name search (first run on this build): 3 agree, 0 refused"), "the cross-check ran (first run)");
        ChainCheck cc;
        CHECK(load_chain_check(r.out / chain_check_file(), cc, nullptr) && cc.build == kBuildKey && cc.agree.size() == 3 && cc.agree.count("attrib.bands"),
              "turbo_output\\tuning_chain_check.json holds the three");
        auto live = r.live();
        CHECK(live["shape.width_in"].located && live["shape.gap_dmf"].copies.size() == 2 && live["lineband.deep_max"].value == 30 && !live["marking.unverified"].located &&
                  live["marking.unverified"].why.find("unverified") != std::string::npos,
              "the knobs located through the chain; the unverified one not");
        CHECK(r.svc->anchors_status().find("3 of 3 locators resolved (3 through the attribute manager)") == 0, r.svc->anchors_status());
        // a second game session on the same build: no name search again (the marker), the same instances
        ChainRig r2(w, g_out / "tuning_chain_svc", chain_knobs(), w.lookup(), nullptr, false);
        ls = r2.svc->locate_status();
        CHECK(ls.ready && ls.groups == 3 && !r2.logged("cross-check") && r2.logged("tuning: chain locate ok 3 groups 6 knobs"), "the second run: chain only");
        // the name search finds a twin of the base database (two objects named gameplayattribdb with its TypeInfo): no agreement -> refused
        ChainWorld w3;
        w3.mem.map(ChainWorld::kCopy, 0x170);
        w3.put_obj(ChainWorld::kCopy, 0x170, ChainWorld::kVtDb, "AttribDBRuntime_gameplayattribdb", w3.name_ptr[0]);
        ChainRig r3(w3, g_out / "tuning_chain_svc3", chain_knobs(), w3.lookup());
        ls = r3.svc->locate_status();
        CHECK(ls.ready && ls.groups == 1 && r3.logged("tuning: chain attrib.llw: REFUSED for this session: the chain and the name search do not agree") &&
                  r3.logged("tuning: chain locate ok 1 groups 2 knobs"),
              "the two gameplay groups refused, the prematch one agrees: " + r3.line("REFUSED"));
        CHECK(!r3.live()["shape.width_in"].located && r3.live()["lineband.deep_max"].located, "refused = not located");
        // the signature is not in the table: the name search (fallback), logged
        Lookup nosig = w.lookup();
        nosig.signature = [](const std::string&) -> uint64_t { return 0; };
        ChainRig r4(w, g_out / "tuning_chain_svc4", chain_knobs(), nosig);
        ls = r4.svc->locate_status();
        CHECK(ls.path == "search" && r4.logged("tuning: signature attrib_db_getter was not found on this game build: the attribulator is located by the name search (fallback)"),
              "fallback: " + ls.path);
        CHECK(ls.ready && r4.live()["shape.width_in"].located && r4.logged("found by the name search"), "located by the name search");
        // the game is starting (no manager): not ready, tried again after a back-off; then located
        ChainWorld w5;
        w5.mem.wr(ChainWorld::kG, uint64_t(0));
        ChainRig r5(w5, g_out / "tuning_chain_svc5", chain_knobs(), w5.lookup());
        ls = r5.svc->locate_status();
        CHECK(!ls.ready && ls.why.find("waiting for the attribute manager") != std::string::npos && r5.logged("tuning: chain locate: the attribute manager does not exist yet"),
              "not ready yet: " + ls.why);
        w5.mem.wr(ChainWorld::kG, ChainWorld::kMgr);
        r5.svc->locate_status();
        CHECK(!r5.logged("tuning: chain locate ok"), "not before the back-off");
        r5.clock += 6e6;
        ls = r5.svc->locate_status();
        CHECK(ls.ready && ls.groups == 3 && r5.logged("tuning: chain locate ok 3 groups"), "located after the back-off");
        // a wrong object behind the manager's handle: the chain is refused for the session, the name search takes over
        ChainWorld w6;
        w6.mem.wr(ChainWorld::kHandleA + 8, ChainWorld::kLlw);
        ChainRig r6(w6, g_out / "tuning_chain_svc6", chain_knobs(), w6.lookup());
        r6.svc->locate_status();
        ls = r6.svc->locate_status();
        CHECK(ls.path == "search" && r6.logged("tuning: chain locate REFUSED for this session") && ls.ready, "refused chain -> name search: " + ls.path);
    });

    run_case("tuning 2.4: B1 - a malformed tuning_chain_check.json or an exception in the locate never escapes; tuning off for the session", [] {
        ChainWorld w;
        const fs::path dir = g_out / "tuning_chain_b1";
        const char* bad[] = {R"({"turbo_tuning_chain_check": 1, "build": null, "agree": ["attrib.llw"]})",
                             R"({"turbo_tuning_chain_check": 1, "build": 5, "when": {"x": 1}, "agree": [1, null, "attrib.bands"]})",
                             R"([1, 2, 3])", R"({"turbo_tuning_chain_check": 1, "bu)", ""};
        int k = 0;
        for (const char* text : bad) {
            ChainRig r(w, dir, chain_knobs(), w.lookup());
            std::ofstream(r.out / chain_check_file(), std::ios::binary | std::ios::trunc) << text;
            LocateStatus ls;
            bool threw = false;
            try {
                ls = r.svc->locate_status();
            } catch (...) {
                threw = true;
            }
            CHECK(!threw && ls.ready && ls.groups == 3 && r.logged("tuning: chain cross-check with the name search (first run on this build): 3 agree"),
                  fmt("bad file %d: no throw, the cross-check runs again, 3 groups", k));
            ChainCheck cc;
            CHECK(load_chain_check(r.out / chain_check_file(), cc, nullptr) && cc.build == kBuildKey && cc.agree.size() == 3, fmt("bad file %d: rewritten", k));
            ++k;
        }
        {
            ChainRig r(w, dir, chain_knobs(), w.lookup());  // a huge file: not read (too big), the cross-check runs again
            {
                std::ofstream f(r.out / chain_check_file(), std::ios::binary | std::ios::trunc);
                f << R"({"turbo_tuning_chain_check": 1, "build": "6AC07E31-2145C000", "agree": ["attrib.llw", "attrib.zonal", "attrib.bands"], "pad": ")";
                f << std::string(200 * 1024, 'x') << "\"}";
            }
            std::string err;
            ChainCheck cc;
            CHECK(!load_chain_check(r.out / chain_check_file(), cc, &err) && err.find("too big") == 0, "a 200 KB file is not read: " + err);
            CHECK(r.svc->locate_status().ready && r.logged("cross-check with the name search"), "the cross-check runs again");
        }
        {
            ChainRig r(w, dir, chain_knobs(), w.lookup());  // an exception inside the background job (the host's log throws)
            r.throw_on = "tuning: attribute manager:";
            bool threw = false;
            LocateStatus ls;
            try {
                ls = r.svc->locate_status();
                ls = r.svc->locate_status();
            } catch (...) {
                threw = true;
            }
            CHECK(!threw && r.logged("tuning: locate FAILED (test: the log failed): gameplay tuning is off for this game session"), "caught and logged");
            CHECK(r.svc->session_off().find("the attribulator locate failed (test: the log failed)") == 0 && ls.ready && ls.groups == 0 &&
                      !r.live()["shape.width_in"].located,
                  "tuning off for the session, nothing located: " + r.svc->session_off());
            r.throw_on.clear();
            r.clock += 120e6;
            r.svc->locate_status();
            CHECK(r.count("tuning: chain locate ok") == 0 && r.count("tuning: attribute manager:") == 1, "no new locate in this session");
            Plan p;
            std::string why;
            p.moment = "boot";
            p.writes.push_back({"shape.width_in", 1.0, 0.6, "base"});
            CHECK(!r.svc->submit(chain_ctx("", false, -1), p, why) && why.find("off for this session") != std::string::npos, "no write: " + why);
        }
        Preset pr;
        std::string err;
        CHECK(!Preset::parse(R"({"turbo_tuning_preset": 1, "name": 5, "changes": []})", pr, &err), "a preset with a number for its name: refused, no throw");
        tuning::Profile prof;
        CHECK(!load_profile(dir / "nope.json", prof, &err) && err.empty(), "absent profile: no error");
    });

    run_case("tuning 2.4: M3 - no locate (no heap pass, no search) while tuning_off / call_attrib_off / tactics_off exists", [] {
        ChainWorld w;
        int passes = 0;
        Lookup lk = w.lookup();
        auto inner = lk.heap_qwords;
        lk.heap_qwords = [&passes, inner](uint64_t v, size_t m) {
            ++passes;
            return inner(v, m);
        };
        for (const char* f : {kill_switch_file(), attrib_switch_file(), master_switch_file()}) {
            passes = 0;
            ChainRig r(w, g_out / "tuning_chain_m3", chain_knobs(), lk);
            r.touch(f);
            LocateStatus ls = r.svc->locate_status();
            auto live = r.live();
            CHECK(passes == 0 && !ls.ready && !live["shape.width_in"].located &&
                      r.logged(std::string("tuning: kill switch turbo_output\\") + f + " is present: the tuning tables are not located (no search)") &&
                      !r.logged("tuning: chain locate ok"),
                  std::string(f) + ": no locate");
            r.remove(f);
            ls = r.svc->locate_status();
            CHECK(passes == 1 && ls.ready && ls.groups == 3, std::string(f) + " removed: located (one heap pass)");
            // a kill switch after the locate: no new search, the anchors stay (Undo / Restore still find the tables)
            r.touch(f);
            CHECK(r.live()["shape.width_in"].located && passes == 1, std::string(f) + " after the locate: the tables stay found");
        }
    });

    run_case("tuning 2.4: the profile's moments in the job - boot / career_load / hub, every copy once, read back, the log line, the prematch guard", [] {
        ChainWorld w;
        CountingWrites cw(w.mem);
        ChainRig r(w, g_out / "tuning_chain_moments", chain_knobs(), w.lookup(), &cw);
        r.jobs.inline_run = true;
        r.svc->locate_status();
        r.profile(true, {{"shape.width_in", 0.6}, {"shape.gap_dmf", 6.0}, {"lineband.deep_max", 25}});
        // boot: the main menu (no career)
        Plan p = profile_plan(r, {{"shape.width_in", 0.6}, {"shape.gap_dmf", 6.0}, {"lineband.deep_max", 25}}, "boot");
        CHECK(p.kind == PlanKind::Reapply && p.moment == "boot" && p.writes.size() == 4, fmt("4 writes: width 1 copy, gap 2 copies, deep 1 (%zu)", p.writes.size()));
        std::string why;
        cw.writes.clear();
        CHECK(r.svc->submit(menu_ctx(r.wall), p, why), "boot queued: " + why);
        ApplyResult res;
        CHECK(r.svc->poll(res) && res.ok && res.written && res.moment == "boot", "boot written: " + res.message);
        CHECK(r.logged("tuning: profile applied (boot) 3 knobs (read back ok)"), "the exact boot line");
        CHECK(w.f32(ChainWorld::kLlw + 0x28) == 0.6f && w.f32(ChainWorld::kZonal + 0x664) == 6.0f && w.f32(ChainWorld::kZonalSim + 0x664) == 6.0f &&
                  w.i32(ChainWorld::kTts + 0x3C) == 25,
              "every copy written");
        CHECK(cw.writes[ChainWorld::kLlw + 0x28] == 1 && cw.writes[ChainWorld::kZonal + 0x664] == 1 && cw.writes[ChainWorld::kZonalSim + 0x664] == 1 &&
                  cw.writes.size() == 4,
              "the instance shared by 15 databases written ONCE, each copy once");
        // career_load: the hub of a loaded career; the values in place are written again and read back (the whole profile)
        p = profile_plan(r, {{"shape.width_in", 0.6}, {"shape.gap_dmf", 6.0}, {"lineband.deep_max", 25}}, "career_load");
        CHECK(r.svc->submit(chain_ctx("hub", true), p, why) && r.svc->poll(res) && res.ok, "career_load: " + res.message);
        CHECK(r.logged("tuning: profile applied (career_load) 3 knobs (read back ok)"), "the career_load line");
        // hub: a prematch 2 s ago (Lua's stamp): refused in the job; 6 s ago: written
        p = profile_plan(r, {{"shape.width_in", 0.7}}, "hub");
        CHECK(r.svc->submit(chain_ctx("hub", true, 3, r.wall - 2), p, why) && r.svc->poll(res) && !res.ok && res.stage == "G2" &&
                  res.message.find("a match was being set up 2 s ago") == 0,
              "the prematch guard window: " + res.message);
        CHECK(r.logged("tuning: profile not applied (hub): G2: a match was being set up 2 s ago") && w.f32(ChainWorld::kLlw + 0x28) == 0.6f, "logged, nothing written");
        CHECK(r.svc->submit(chain_ctx("hub", true, 3, r.wall - 6), p, why) && r.svc->poll(res) && res.ok && w.f32(ChainWorld::kLlw + 0x28) == 0.7f,
              "6 s after the prematch: written");
        CHECK(r.logged("tuning: profile applied (hub) 1 knobs (read back ok)"), "the hub line");
        // the job's own gates: boot with a career loaded, without Lua's main-menu report, with a stale one, in a Lua match phase; hub in a
        // match, a knob whose `when` lacks the moment
        p = profile_plan(r, {{"shape.width_in", 0.8}}, "boot");
        CHECK(r.svc->submit(chain_ctx("hub", true), p, why) && r.svc->poll(res) && !res.ok && res.message.find("a career is loaded") == 0, res.message);
        Context no_lua = chain_ctx("", false, -1);
        no_lua.bridge = false;
        CHECK(r.svc->submit(no_lua, p, why) && r.svc->poll(res) && !res.ok && res.message.find("Lua has not reported the main menu") == 0,
              "no Lua report: no boot write: " + res.message);
        CHECK(r.svc->submit(menu_ctx(r.wall, 61), p, why) && r.svc->poll(res) && !res.ok && res.message.find("is 61 s old") != std::string::npos,
              "a stale report: " + res.message);
        Context kickoff = menu_ctx(r.wall);
        kickoff.phase = "match";
        CHECK(r.svc->submit(kickoff, p, why) && r.svc->poll(res) && !res.ok && res.message.find("a match is set up or played") == 0, res.message);
        Context tm = chain_ctx("hub", true);
        tm.team_mgmt = true;
        p = profile_plan(r, {{"shape.width_in", 0.8}}, "hub");
        CHECK(r.svc->submit(tm, p, why) && r.svc->poll(res) && !res.ok && res.message.find("Team Management is open") == 0, "m8: " + res.message);
        CHECK(w.f32(ChainWorld::kLlw + 0x28) == 0.7f, "none of these wrote");
        p = profile_plan(r, {{"shape.width_in", 0.8}}, "boot");
        p = profile_plan(r, {{"shape.width_in", 0.8}}, "hub");
        CHECK(r.svc->submit(chain_ctx("match", true), p, why) && r.svc->poll(res) && !res.ok && res.message.find("not in the career hub (Lua: match)") == 0, res.message);
        std::vector<std::string> sk;
        p = profile_plan(r, {{"pressing.trigger_depth", 0.2}, {"marking.unverified", 10}, {"nope", 1}, {"shape.width_in", 9.0}}, "boot", &sk);
        CHECK(p.writes.empty() && sk.size() == 4 && has_text(sk, "Pressing starts at depth: not written at boot") && has_text(sk, "Unverified knob: not written at boot") &&
                  has_text(sk, "nope is not in the knobs file") && has_text(sk, "outside 0.6..1.7"),
              "the profile plan skips: " + std::to_string(sk.size()));
        // the switches: tuning_profile_off.txt stops the moments (not a click), tuning_off.txt every write, the profile off too
        p = profile_plan(r, {{"shape.width_in", 0.8}}, "hub");
        r.touch(profile_off_file());
        CHECK(r.svc->submit(chain_ctx("hub", true), p, why) && r.svc->poll(res) && !res.ok && res.stage == "G1" &&
                  res.message.find("turbo_output\\tuning_profile_off.txt") == 0,
              res.message);
        r.remove(profile_off_file());
        r.profile(false, {{"shape.width_in", 0.8}});
        CHECK(r.svc->submit(chain_ctx("hub", true), p, why) && r.svc->poll(res) && !res.ok && res.message.find("tuning_profile.json is off") != std::string::npos,
              res.message);
        r.profile(true, {{"shape.width_in", 0.8}});
        r.touch(kill_switch_file());
        CHECK(r.svc->submit(chain_ctx("hub", true), p, why) && r.svc->poll(res) && !res.ok && res.message.find("tuning_off.txt") != std::string::npos, res.message);
        r.remove(kill_switch_file());
        CHECK(w.f32(ChainWorld::kLlw + 0x28) == 0.7f, "nothing written by the refused jobs");
    });

    run_case("tuning 2.4: the owner rule across moments, Undo and Restore FC 27 defaults (the knobs file's values, the kill switch present)", [] {
        ChainWorld w;
        ChainRig r(w, g_out / "tuning_chain_owner", chain_knobs(), w.lookup());
        r.jobs.inline_run = true;
        r.svc->locate_status();
        r.profile(true, {{"shape.width_in", 0.6}});
        std::string why;
        ApplyResult res;
        Plan p = profile_plan(r, {{"shape.width_in", 0.6}}, "boot");
        CHECK(r.svc->submit(menu_ctx(r.wall), p, why) && r.svc->poll(res) && res.ok, "boot wrote 0.6");
        // another tool changes the value between the moments: the career_load moment refuses it (neither FC 27's nor Turbo's last write)
        w.mem.wr(ChainWorld::kLlw + 0x28, 0.75f);
        p = profile_plan(r, {{"shape.width_in", 0.6}}, "career_load");
        CHECK(r.svc->submit(chain_ctx("hub", true), p, why) && r.svc->poll(res) && !res.ok && res.stage == "owner" &&
                  res.message.find("neither FC 27's 1.00 nor Turbo's last write") != std::string::npos,
              "the owner rule across moments: " + res.message);
        CHECK(w.f32(ChainWorld::kLlw + 0x28) == 0.75f && r.logged("tuning: profile not applied (career_load): owner:"), "left alone");
        // the career load put FC 27's value back (a rebuilt table): written again
        w.mem.wr(ChainWorld::kLlw + 0x28, 1.0f);
        p = profile_plan(r, {{"shape.width_in", 0.6}, {"shape.gap_dmf", 5.0}}, "career_load");
        CHECK(r.svc->submit(chain_ctx("hub", true), p, why) && r.svc->poll(res) && res.ok && w.f32(ChainWorld::kLlw + 0x28) == 0.6f, "written over FC 27's value");
        // Restore FC 27 defaults: the knobs file's values into every copy (base 8, simulation 9), with the kill switch present
        Plan v;
        std::vector<std::string> sk;
        CHECK(make_vanilla_plan(*r.svc->registry(), r.live(), v, sk).empty() && v.kind == PlanKind::Restore && v.writes.size() == 3,
              fmt("width + both gap copies (%zu)", v.writes.size()));
        r.touch(kill_switch_file());
        CHECK(r.svc->submit(chain_ctx("hub", true), v, why) && r.svc->poll(res) && res.ok, "restored with tuning_off.txt present: " + res.message);
        r.remove(kill_switch_file());
        CHECK(w.f32(ChainWorld::kLlw + 0x28) == 1.0f && w.f32(ChainWorld::kZonal + 0x664) == 8.0f && w.f32(ChainWorld::kZonalSim + 0x664) == 9.0f,
              "FC 27's own values: base 8, simulation 9 (copy_defaults)");
        CHECK(make_vanilla_plan(*r.svc->registry(), r.live(), v, sk).empty() && v.writes.empty(), "nothing left to restore");
    });

    run_case("tuning 2.4: the profile file - JSON round trip, bad files refused, the moments' plan, the switches read with it", [] {
        tuning::Profile p;
        p.on = true;
        p.preset = "Turbo compact";
        p.values = {{"shape.width_in", 0.9}, {"lineband.deep_max", 28}};
        tuning::Profile back;
        std::string err;
        CHECK(tuning::Profile::parse(p.to_json(), back, &err) && back.on && back.preset == "Turbo compact" && back.values == p.values, "round trip: " + err);
        CHECK(tuning::Profile::parse(R"({"on": false, "preset": "custom", "values": {"_comment": "x", "a": 1}})", back, &err) && !back.on && back.values.size() == 1,
              "comments skipped");
        CHECK(tuning::Profile::parse(R"({"on": true, "values": {}})", back, &err) && back.preset == "custom", "the preset defaults to custom");
        CHECK(!tuning::Profile::parse(R"({"values": {}})", back, &err) && err.find("\"on\"") == 0, "on is required: " + err);
        CHECK(!tuning::Profile::parse(R"({"on": "yes"})", back, &err), "on is a bool");
        CHECK(!tuning::Profile::parse(R"({"on": true, "values": {"a": "x"}})", back, &err) && err.find("is not a number") != std::string::npos, err);
        CHECK(!tuning::Profile::parse(R"({"on": true, "values": [1]})", back, &err), "values is an object");
        const fs::path dir = g_out / "tuning_profile_file";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        CHECK(!load_profile(dir / profile_file(), back, &err) && err.empty(), "absent: false, no error");
        CHECK(save_profile(dir / profile_file(), p, &err) && load_profile(dir / profile_file(), back, &err) && back.values == p.values, "the file: " + err);
        Switches sw = tuning::read_switches(dir);
        CHECK(sw.profile_on && !sw.profile_off && sw.opted_in() && sw.blocking_moments().empty(), "the profile on is an opt-in");
        std::ofstream(dir / profile_off_file()) << "x";
        sw = tuning::read_switches(dir);
        CHECK(sw.profile_off && sw.blocking().empty() && sw.blocking_moments() == profile_off_file(), "tuning_profile_off.txt stops the moments only");
        std::ofstream(dir / master_switch_file()) << "x";
        CHECK(tuning::read_switches(dir).blocking_moments() == master_switch_file(), "tactics_off.txt first");
        Switches none;
        CHECK(!none.opted_in() && (none.session = true, none.opted_in()), "the session tick is an opt-in");
        CHECK(std::string(moment_name(Moment::CareerLoad)) == "career_load" && moments().size() == 3, "names");
    });

    run_case("tuning 2.4: the moment tracker - boot once at the main menu, career_load once per load, hub on every entry, the prematch window, profile off / changes", [] {
        MomentTracker t;
        MomentInput in;
        in.now = 100;
        in.wall = 1760000000;
        in.profile_on = true;
        in.ready = false;
        CHECK(t.next(in) == Moment::None, "not before the locate is ready");
        in.ready = true;
        in.busy = true;
        CHECK(t.next(in) == Moment::None, "not while a job is busy");
        in.busy = false;
        CHECK(t.next(in) == Moment::None, "M1: no Lua report (the bridge not started): no boot");
        in.bridge = true;
        CHECK(t.next(in) == Moment::None, "Lua running but no main-menu report: no boot");
        in.front_end_t = in.wall - 61;
        CHECK(t.next(in) == Moment::None, "a 61 s old report: no boot (a Kick-Off may have started since)");
        in.front_end_t = in.wall - 2;
        in.phase = "match";
        CHECK(t.next(in) == Moment::None, "a Lua match phase: no boot");
        in.phase = "unknown";
        CHECK(t.next(in) == Moment::Boot, "boot: ready, no career, Lua reported the main menu 2 s ago");
        t.fired(Moment::Boot, in);
        CHECK(t.next(in) == Moment::None && t.boot_done(), "boot once");
        // a career loads: its hub -> career_load, then hub
        in.bridge = in.in_cm = true;
        in.session = "s1";
        in.load_gen = 1;
        in.phase = "hub";
        in.now = 110;
        CHECK(t.next(in) == Moment::CareerLoad, "career_load in the hub of the new load");
        t.fired(Moment::CareerLoad, in);
        CHECK(t.next(in) == Moment::Hub, "then the hub moment (the hub line follows)");
        t.fired(Moment::Hub, in);
        CHECK(t.next(in) == Moment::None, "nothing more while in the hub");
        // kick-off: hub -> prematch -> hub (0.5 s) -> match: nothing in the window, also when the poll misses the prematch (Lua's stamp)
        in.phase = "prematch";
        in.now = 120;
        CHECK(t.next(in) == Moment::None, "prematch: nothing");
        in.phase = "hub";
        in.now = 120.5;
        CHECK(t.next(in) == Moment::None && t.hub_pending(), "the 0.5 s hub before kick-off: refused (the tracker saw the prematch)");
        in.phase = "match";
        in.now = 121;
        CHECK(t.next(in) == Moment::None && !t.hub_pending(), "the match: the pending entry is dropped");
        MomentTracker t2 = t;  // the same, but the GUI's 0.5 s poll never saw the prematch: Lua's stamp 1 s ago
        in.phase = "hub";
        in.now = 200;
        in.prematch_t = in.wall - 1;
        CHECK(t2.next(in) == Moment::None, "Lua's prematch stamp 1 s ago: nothing");
        in.prematch_t = in.wall - 6;
        CHECK(t2.next(in) == Moment::Hub, "6 s after the stamp: the hub entry fires");
        // after the match: back in the hub
        in.phase = "match";
        in.now = 300;
        t.next(in);
        in.phase = "hub";
        in.now = 900;
        CHECK(t.next(in) == Moment::Hub, "re-entering the hub after the match");
        t.fired(Moment::Hub, in);
        // the profile changes in the hub (another preset, a hand-edited file): applied there
        in.profile_rev = 2;
        CHECK(t.next(in) == Moment::Hub, "a changed profile in the hub");
        t.fired(Moment::Hub, in);
        // the profile off: nothing; switched on again in the hub: applied
        in.profile_on = false;
        CHECK(t.next(in) == Moment::None, "off: nothing");
        in.profile_on = true;
        CHECK(t.next(in) == Moment::Hub, "switched on in the hub: applied");
        t.fired(Moment::Hub, in);
        // a new career load (another load_gen): career_load again
        in.load_gen = 2;
        CHECK(t.next(in) == Moment::CareerLoad, "career_load once per load");
        t.fired(Moment::CareerLoad, in);
        CHECK(t.load_done() == "s1#2", t.load_done());
        // Team Management open: no career_load / hub moment until it closes (m8)
        in.load_gen = 3;
        in.team_mgmt = true;
        CHECK(t.next(in) == Moment::None, "Team Management open: nothing");
        in.team_mgmt = false;
        CHECK(t.next(in) == Moment::CareerLoad, "closed: the career_load moment");
        t.fired(Moment::CareerLoad, in);
        // a career seen before the locate is ready: no boot moment (career_load covers it)
        MomentTracker t3;
        MomentInput c = in;
        c.ready = false;
        CHECK(t3.next(c) == Moment::None && t3.boot_done(), "a career first: boot skipped");
        c.ready = true;
        CHECK(t3.next(c) == Moment::CareerLoad, "its career_load");
        // the gate of the job
        Knob hub_only;
        hub_only.label = "H";
        hub_only.when = {"hub"};
        auto mc = [](bool in_cm, const std::string& phase, long long prematch = -1, long long save = -1, long long front = -1, bool tmg = false) {
            MomentContext c;
            c.in_cm = in_cm;
            c.phase = phase;
            c.prematch_age = prematch;
            c.save_age = save;
            c.front_end_age = front;
            c.team_mgmt = tmg;
            return c;
        };
        CHECK(moment_gate("boot", mc(false, "", -1, -1, 3), {}).empty() && moment_gate("boot", mc(true, "hub", -1, -1, 3), {}).find("a career is loaded") == 0,
              "boot");
        CHECK(moment_gate("boot", mc(false, ""), {}).find("Lua has not reported the main menu") == 0 && moment_gate("boot", mc(false, "", -1, -1, 61), {}).find("61 s old") != std::string::npos &&
                  moment_gate("boot", mc(false, "match", -1, -1, 3), {}).find("a match is set up or played") == 0,
              "boot needs Lua's fresh main-menu report, no match phase");
        CHECK(moment_gate("hub", mc(true, "hub", 3), {}).find("3 s ago") != std::string::npos && moment_gate("hub", mc(true, "hub", 5), {}).empty(), "the 5 s window");
        CHECK(moment_gate("career_load", mc(true, "hub", -1, 10), {}).find("a save started 10 s ago") == 0, "a save");
        CHECK(moment_gate("career_load", mc(false, "hub"), {}) == "no career is loaded", "career_load needs a career");
        CHECK(moment_gate("hub", mc(true, "hub", -1, -1, -1, true), {}) == "Team Management is open", "Team Management (m8)");
        CHECK(moment_gate("career_load", mc(true, "hub"), {&hub_only}) == "H is not written at career_load", "when");
        CHECK(moment_gate("kickoff", mc(true, "hub"), {}).find("is not a moment") != std::string::npos, "unknown");
        CHECK(prematch_guard(1000, 996).find("4 s ago") != std::string::npos && prematch_guard(1000, 995).empty() && prematch_guard(1000, 0).empty(), "prematch_guard");
    });

    run_case("tuning 2.4: the gates - G1 opt-in by the session tick or the profile, G2 waits 5 s after a prematch for a hub write of a non-live knob", [] {
        Registry reg;
        std::string err;
        parse_registry(chain_knobs(), reg, err);
        GateInput in;
        in.bridge = in.in_cm = true;
        in.match_phase = "hub";
        in.load_gen = 3;
        in.knobs = {reg.knob("shape.width_in")};
        Gates g = evaluate(in);
        CHECK(!g.pass() && g.first().find("G1 switches: off: tick \"write in this game session\", switch the tuning profile on, or create turbo_output\\tuning_on.txt") == 0,
              g.first());
        in.sw.session = true;
        CHECK(evaluate(in).pass(), "the session tick");
        in.sw.session = false;
        in.sw.profile_on = true;
        CHECK(evaluate(in).pass(), "the profile on");
        in.prematch_age = 2;
        g = evaluate(in);
        CHECK(!g.pass() && g.first().find("G2 career phase: a match was being set up 2 s ago") == 0, g.first());
        Knob live = *reg.knob("shape.width_in");
        live.live = true;
        in.knobs = {&live};
        CHECK(evaluate(in).pass(), "a live knob is not held back");
        in.knobs = {reg.knob("shape.width_in")};
        in.prematch_age = 7;
        CHECK(evaluate(in).pass(), "7 s later");
    });

    run_case("tuning 2.4: Turbo's own presets - FC 27 default, Turbo compact, Turbo expansive; only usable (proven) knobs are set, the others listed", [] {
        const auto& b = builtin_presets();
        CHECK(b.size() == 3 && b[0].name == "FC 27 default" && b[0].fc27 && b[1].name == "Turbo compact" && b[2].name == "Turbo expansive", "three presets");
        for (const Preset& p : b) {
            CHECK(p.turbo() && p.note.find("Turbo's own") == 0, p.name + " is Turbo's own");
            for (const PresetChange& c : p.changes) CHECK(c.relative && std::fabs(c.pct) <= 50, p.name + ": " + c.knob + " a relative change, a generic size");
        }
        Preset back;
        std::string err;
        CHECK(Preset::parse(b[0].to_json(), back, &err) && back.fc27 && back.changes.empty(), "fc27 round trip: " + err);
        Registry reg;
        parse_registry(chain_knobs(), reg, err);
        auto base = [&](const std::string& id, double& v) {
            const Knob* k = reg.knob(id);
            if (!k) return false;
            v = k->def;
            return true;
        };
        auto proven_only = [](const Knob& k) { return k.proven() ? std::string() : std::string("not proven yet"); };
        PresetValues pv = preset_values(b[0], reg, base, proven_only);
        CHECK(pv.values.size() == 1 && pv.values["shape.width_in"] == 1.0 && pv.skipped.empty(), "FC 27 default: the proven knob at FC 27's value");
        pv = preset_values(b[1], reg, base, proven_only);
        CHECK(pv.values.size() == 1 && std::fabs(pv.values["shape.width_in"] - 0.9) < 1e-6 && has_text(pv.skipped, "Team length with the ball: not proven yet"),
              "Turbo compact: width -10 %, the unproven ones listed");
        pv = preset_values(b[2], reg, base);
        CHECK(pv.values.count("shape.length_in") && std::fabs(pv.values["shape.length_in"] - 0.924) < 1e-4, "without the filter every located knob");
        // Turbo 2.3's own presets are removed from the folder (a user's file of that name is kept)
        const fs::path dir = g_out / "tuning_presets_24";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        Preset old;
        old.id = "turbo_deep_block";
        old.name = "Deep compact block";
        old.author = "Turbo";
        old.changes = {{"shape.gap_dmf", true, -15, 0}};
        std::ofstream(dir / "turbo_deep_block.json") << old.to_json();
        old.author = "me";
        std::ofstream(dir / "turbo_high_line_press.json") << old.to_json();
        CHECK(ensure_builtin_presets(dir, &err) == 3 && !fs::exists(dir / "turbo_deep_block.json") && fs::exists(dir / "turbo_high_line_press.json") &&
                  fs::exists(dir / "turbo_compact.json") && fs::exists(dir / "turbo_fc27_default.json"),
              "Turbo's own written, 2.3's own removed, the user's kept");
    });
}
