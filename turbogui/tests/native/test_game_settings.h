// Native tests: the game's own settings file (core/game_settings.h) and the Game settings tab (ui/ui_game_settings.cpp), on a SYNTHETIC
// Settings<timestamp> file built from the structure of a read-only copy (FBCHUNKS header, sizes, tag, CRC'd length, the constant words, a
// u32 array at the map's base): no byte of a real save is checked in. Every folder is under the run's output folder; the tests never
// open the real %LOCALAPPDATA% settings folder (test_main points LOCALAPPDATA at the run's tmp folder before the UI cases).
// Included by test_main.cpp; gsettings_test::run() runs the core cases, gsettings_test::ui_cases() the tab.
#pragma once
#include "core/game_settings.h"
#include "ui/ui_game_settings.h"

namespace gsettings_test {

using namespace turbo;
using namespace turbo::gsettings;

static const char* const kName = "Settings20261014213137933";

static void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int k = 0; k < 4; ++k) b[at + size_t(k)] = uint8_t(v >> (8 * k));
}
static uint32_t get32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}

// The synthetic file: the layout's structure, filler bytes, slider values 0..100 in the three candidate regions (base 0x1BC4)
static std::vector<uint8_t> fixture(uint32_t seed = 7) {
    std::vector<uint8_t> b(kFileSize, 0);
    std::memcpy(b.data(), "FBCHUNKS\x01\x00", 10);
    put32(b, 10, uint32_t(kMetaSize));
    put32(b, 14, uint32_t(kPayloadSize));
    std::memcpy(b.data() + kHeaderSize, "Personal Settings 1", 19);
    for (size_t i = 0x60; i < kMetaSize; ++i) b[kHeaderSize + i] = uint8_t((i * 31 + seed) & 0xFF);  // the meta's changing bytes
    const size_t P = kPayloadOff;
    std::memcpy(b.data() + P, "SaveType_Settin", 15);
    put32(b, P + 0x14, 0x31D);
    put32(b, P + 0x18, 1);
    put32(b, P + kCrcLenAt, uint32_t(kPayloadSize - kCrcFrom));
    put32(b, P + 0x34, 7);
    put32(b, P + 0x70, 9);
    for (int i = 0; i < 4; ++i) put32(b, P + kConstAt + 4u * size_t(i), kConstWords[i]);
    for (size_t i = 0x3000; i < kPayloadSize; i += 7) b[P + i] = uint8_t((i ^ seed) & 0xFF);  // filler outside the array
    const size_t base = 0x1BC4;
    for (int i = 0; i < 0xC6; ++i) put32(b, P + base + 4u * size_t(i), uint32_t(i % 5));  // small enum-like values
    for (int i = 0xC6; i < 0xEA; ++i) put32(b, P + base + 4u * size_t(i), 50);
    for (int k = 0; k < 44; ++k) {
        put32(b, P + base + 4u * size_t(0xEA + k), uint32_t(30 + (k * 7 + seed) % 60));
        put32(b, P + base + 4u * size_t(0x116 + k), uint32_t(30 + (k * 7 + seed) % 60));
    }
    put32(b, P + base + 4u * 0x142, 50);
    put32(b, P + base + 4u * 0x143, 1700);  // a field that is not a slider (above 100)
    for (int i = 0; i < 5; ++i) b[P + 0xB8976 - 5 + size_t(i)] = kCustomPrefix[i];  // the Custom block (GameplayTypeCustomData): 125 x 50
    for (int i = 0; i < 125; ++i) b[P + 0xB8976 + size_t(i)] = 50;
    fix_crc(b);
    return b;
}

static void write_file(const fs::path& p, const std::vector<uint8_t>& b) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}
static std::string md5_of(const fs::path& p) {
    std::vector<uint8_t> b;
    return read_bytes(p, b) ? md5_hex(b) : std::string("missing");
}
// every file of a folder -> md5
static std::map<std::string, std::string> md5s(const fs::path& dir) {
    std::map<std::string, std::string> m;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) m[e.path().filename().u8string()] = md5_of(e.path());
    return m;
}

// A test map: the three regions; two VERIFIED entries (CPU sprint speed = block_a[0], User sprint speed = block_b[0]), one unverified with
// an index, one not mapped
static std::string test_map(bool shipped_layout = true) {
    return std::string(R"({"turbo_settings_map": 1, "build": "6AC07E31", "layout": ")") + (shipped_layout ? "fbchunks-454-bc650" : "other") +
           R"(", "array_base": "0x1BC4",
 "regions": [{"id": "run36", "first": "0xC6", "count": 36}, {"id": "block_a", "first": "0xEA", "count": 44}, {"id": "block_b", "first": "0x116", "count": 44}],
 "entries": [
  {"id": "cpu.sprint_speed", "label": "Sprint speed", "group": "cpu", "index": "0xEA", "verified": true, "proof": "EV/test#cpu"},
  {"id": "user.sprint_speed", "label": "Sprint speed", "group": "user", "index": "0x116", "verified": true, "proof": "EV/test#user", "min": 10, "max": 90},
  {"id": "cpu.acceleration", "label": "Acceleration", "group": "cpu", "index": "0xEB", "verified": false},
  {"id": "match.half_length", "label": "Half length", "group": "match", "index": null, "verified": false}]})";
}

struct Rig {
    fs::path root, settings, out;
    SliderMap map;
    bool running = false;
    Env env;
    explicit Rig(const std::string& name, const std::vector<uint8_t>& file = fixture()) {
        root = g_out / "gsettings" / name;
        std::error_code ec;
        fs::remove_all(root, ec);
        settings = root / "settings";
        out = root / "turbo_output";
        fs::create_directories(out, ec);
        write_file(settings / kName, file);
        // the career and squad saves next to it: never touched
        write_file(settings / "CmMgrC20261014211520606", std::vector<uint8_t>(4096, 0xC1));
        write_file(settings / "Squads20261013234531645", std::vector<uint8_t>(2048, 0x5A));
        write_file(settings / "ProfileOptions", std::vector<uint8_t>(1024, 0x0F));
        std::string err;
        CHECK(SliderMap::parse(test_map(), map, &err), "test map: " + err);
        env.settings_dir = settings;
        env.out_dir = out;
        env.game_running = [this] { return running; };
        env.stamp = "20261014-220000-000";
    }
    std::vector<uint8_t> bytes() const {
        std::vector<uint8_t> b;
        read_bytes(settings / kName, b);
        return b;
    }
    uint32_t at(int index) const {
        const std::vector<uint8_t> b = bytes();
        return b.size() == kFileSize ? get32(b, kPayloadOff + map.offset_of(index)) : 0xFFFFFFFFu;
    }
    std::string others() const {  // the md5s of the files that are not the settings file
        std::string s;
        for (const auto& kv : md5s(settings))
            if (kv.first.rfind("Settings", 0) != 0) s += kv.first + "=" + kv.second + ";";
        return s;
    }
};

static void run() {
    std::printf("native game settings file (2.8)\n");

    run_case("game settings: crc32 and md5 against known values", [] {
        const std::string c = "123456789";
        CHECK(crc32(reinterpret_cast<const uint8_t*>(c.data()), c.size()) == 0xCBF43926u, "crc32 check value");
        auto md5s_ = [](const std::string& s) { return md5_hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()); };
        CHECK(md5s_("") == "d41d8cd98f00b204e9800998ecf8427e", "md5 empty");
        CHECK(md5s_("abc") == "900150983cd24fb0d6963f7d28e17f72", "md5 abc");
        CHECK(md5s_("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6", "md5 fox");
        CHECK(md5s_("12345678901234567890123456789012345678901234567890123456789012345678901234567890") == "57edf4a22be3c955ac49da2e2107b67a",
              "md5 80 bytes (two blocks)");
        CHECK(md5s_(std::string(56, 'a')) == "3b0c8ac703f828b04c6c197006d17218", "md5 56 bytes (the padding needs a second block)");
    });

    run_case("game settings: the synthetic file validates; every refusal of validate", [] {
        std::vector<uint8_t> b = fixture();
        Check c = validate(b);
        CHECK(c.ok && c.layout == "fbchunks-454-bc650" && c.profile_name == "Personal Settings 1" && c.crc_stored == c.crc_calc, "ok: " + c.why);
        auto refused = [&](std::vector<uint8_t> x, const std::string& want, const char* what) {
            const Check r = validate(x);
            CHECK(!r.ok && r.why.find(want) != std::string::npos, std::string(what) + ": " + r.why);
        };
        { auto x = b; x.push_back(0); refused(x, "size", "one byte more"); }
        { auto x = b; x.resize(kFileSize - 4); refused(x, "size", "shorter"); }
        { auto x = b; x[0] = 'X'; refused(x, "FBCHUNKS", "magic"); }
        { auto x = b; put32(x, 10, 0x455); refused(x, "header sizes", "meta size"); }
        { auto x = b; put32(x, 14, 0xBC651); refused(x, "header sizes", "payload size"); }
        { auto x = b; x[kPayloadOff + 9] = 'X'; fix_crc(x); refused(x, "SaveType", "tag"); }
        { auto x = b; put32(x, kPayloadOff + kCrcLenAt, 0xBC624); refused(x, "section mismatch", "length word"); }
        { auto x = b; put32(x, kPayloadOff + kConstAt + 8, 0x2EE78); refused(x, "section mismatch", "constant word"); }
        { auto x = b; x[kPayloadOff + 0x5000] ^= 1; refused(x, "CRC", "one payload bit"); }
        { auto x = b; x[kPayloadOff + kCrcAt] ^= 1; refused(x, "CRC", "the stored CRC"); }
        // the meta and the bytes before payload+48 are outside the CRC (the game changes the meta at every save)
        { auto x = b; x[kHeaderSize + 0x100] ^= 0xFF; CHECK(validate(x).ok, "a meta byte changed: still valid"); }
        // get/set stay inside the payload's CRC'd part
        uint32_t v = 0;
        CHECK(!get_u32(b, 4, v) && !get_u32(b, kPayloadSize - 2, v) && get_u32(b, 0x1BC4 + 4 * 0xEA, v) && v <= 100, "get_u32 bounds");
    });

    run_case("game settings: names and the folder scan (exactly one Settings<17 digits>, never CmMgrC / Squads / ProfileOptions)", [] {
        CHECK(is_settings_name(kName) && !is_settings_name("Settings2026101421313793") && !is_settings_name("Settings2026101421313793x") &&
                  !is_settings_name("settings20261014213137933") && !is_settings_name("CmMgrC20261014211520606"),
              "is_settings_name");
        CHECK(is_protected_name("CmMgrC20261014211520606") && is_protected_name("Squads20261013234531645") && is_protected_name("_1_Squads20261004234127468") &&
                  is_protected_name("ProfileOptions") && is_protected_name("ProfileOptions_profile") && !is_protected_name(kName),
              "is_protected_name");
        const fs::path d = g_out / "gsettings" / "scan";
        std::error_code ec;
        fs::remove_all(d, ec);
        fs::create_directories(d, ec);
        Folder f = scan(d / "missing");
        CHECK(!f.ok && f.why.find("not there") != std::string::npos, f.why);
        write_file(d / "CmMgrC20261014211520606", std::vector<uint8_t>(16, 1));
        f = scan(d);
        CHECK(!f.ok && f.why.find("no Settings file") != std::string::npos, "only a career save: " + f.why);
        write_file(d / kName, fixture());
        f = scan(d);
        CHECK(f.ok && f.file.filename() == kName, "one file: " + f.why);
        write_file(d / "Settings20261014213137999", fixture());
        f = scan(d);
        CHECK(!f.ok && f.why.find("2 Settings files") != std::string::npos, "two: " + f.why);
        fs::remove(d / "Settings20261014213137999", ec);
        write_file(d / "Settings_old", fixture());
        f = scan(d);
        CHECK(!f.ok && f.settings.size() == 2, "a Settings_old next to it is counted too: " + f.why);
        fs::remove(d / kName, ec);
        f = scan(d);
        CHECK(!f.ok && f.why.find("not named") != std::string::npos, "only Settings_old: " + f.why);
    });

    run_case("game settings: the shipped slider map loads, every entry verified:false; bad maps refused", [] {
        const fs::path p = fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file();
        SliderMap m;
        std::string err;
        CHECK(load_map(p, m, &err), "shipped map: " + err);
        int verified = 0, user = 0, cpu = 0, match = 0, player = 0;
        for (const Entry& e : m.entries) {
            verified += e.verified ? 1 : 0;
            user += e.group == "user";
            cpu += e.group == "cpu";
            match += e.group == "match";
            player += e.group == "player";
        }
        CHECK(verified == 3, fmt("only the three Custom bytes observed in G-S1 are verified (%d)", verified));
        CHECK(m.base == 0x1BC4 && m.regions.size() == 3 && user >= 30 && cpu >= 30 && match >= 3 && player >= 3,
              fmt("base 0x%zX, %zu regions, %d user, %d cpu, %d match, %d player", m.base, m.regions.size(), user, cpu, match, player));
        CHECK(m.region_of(0xEA) && m.region_of(0xEA)->id == "block_a" && m.region_of(0x141) && m.region_of(0x141)->id == "block_b" &&
                  !m.region_of(0x142) && !m.region_of(0xC5),
              "regions");
        auto bad = [&](std::string text, const std::string& want, const char* what) {
            SliderMap x;
            std::string e;
            CHECK(!SliderMap::parse(text, x, &e) && e.find(want) != std::string::npos, std::string(what) + ": " + e);
        };
        auto with = [&](const std::string& from, const std::string& to) {
            std::string t = test_map();
            const size_t at = t.find(from);
            CHECK(at != std::string::npos, "test map holds " + from);
            if (at != std::string::npos) t.replace(at, from.size(), to);
            return t;
        };
        bad(test_map(false), "layout", "another layout");
        bad("[1]", "not a Turbo", "not a map");
        bad(with("\"0x1BC4\"", "\"0xBC650\""), "array_base", "base outside the payload");
        bad(with("\"index\": \"0xEB\"", "\"index\": \"0x142\""), "outside every region", "an index outside the regions");
        bad(with("\"index\": \"0xEB\"", "\"index\": \"0xEA\""), "used twice", "an index twice");
        bad(with("\"proof\": \"EV/test#cpu\"", "\"proof\": \"\""), "proof", "verified without a proof");
        bad(with("\"index\": null, \"verified\": false", "\"index\": null, \"verified\": true, \"proof\": \"x\""), "without an index", "verified without an index");
        bad(with("\"group\": \"match\"", "\"group\": \"tactics\""), "group", "an unknown group");
        bad(with("\"first\": \"0x116\"", "\"first\": \"0x100\""), "overlaps", "overlapping regions");
        bad(with("\"min\": 10", "\"min\": -1"), "min/max", "min below 0");
        bad(with("\"id\": \"cpu.acceleration\"", "\"id\": \"cpu.sprint_speed\""), "twice", "an id twice");
    });

    run_case("game settings: read_current lists the mapped values and every candidate slot (read-only)", [] {
        Rig r("read");
        const std::string before = md5_of(r.settings / kName);
        const gsettings::Snapshot s = read_current(r.settings, r.map);
        CHECK(s.ok && s.name == kName && s.layout == "fbchunks-454-bc650" && s.md5 == before && s.profile == "Personal Settings 1", "ok: " + s.why);
        CHECK(s.slots.size() == 36 + 44 + 44 && s.values.size() == 3 && s.values.at("cpu.sprint_speed") == int(r.at(0xEA)) &&
                  s.values.at("cpu.acceleration") == int(r.at(0xEB)) && !s.values.count("match.half_length"),
              fmt("%zu slots, %zu values", s.slots.size(), s.values.size()));
        CHECK(md5_of(r.settings / kName) == before, "reading changes nothing");
        // a CRC failure: not read
        std::vector<uint8_t> b = fixture();
        b[kPayloadOff + 0x9000] ^= 1;
        write_file(r.settings / kName, b);
        const gsettings::Snapshot s2 = read_current(r.settings, r.map);
        CHECK(!s2.ok && s2.why.find("CRC") != std::string::npos, s2.why);
    });

    run_case("game settings: patch changes only the target u32s and the CRC; backup with md5s; one file, same name; CmMgrC/Squads untouched", [] {
        Rig r("patch");
        const std::vector<uint8_t> before = r.bytes();
        const std::string others = r.others();
        const uint32_t a0 = r.at(0xEA);
        const uint32_t want = a0 == 77 ? 78 : 77;
        const Outcome o = patch(r.env, r.map, {{0xEA, want, "cpu.sprint_speed"}}, "test");
        CHECK(o.ok && o.message.find("Written: 1 value") != std::string::npos, o.message);
        const std::vector<uint8_t> after = r.bytes();
        std::vector<size_t> diff;
        for (size_t i = 0; i < kFileSize && i < after.size(); ++i)
            if (before[i] != after[i]) diff.push_back(i);
        const size_t at = kPayloadOff + r.map.offset_of(0xEA), crc = kPayloadOff + kCrcAt;
        bool only = !diff.empty();
        for (size_t d : diff) only = only && ((d >= at && d < at + 4) || (d >= crc && d < crc + 4));
        CHECK(after.size() == kFileSize && only && validate(after).ok && r.at(0xEA) == want, fmt("%zu bytes differ, all in the u32 or the CRC: %d", diff.size(), only));
        CHECK(o.md5_before == md5_hex(before) && o.md5_after == md5_hex(after), "md5s reported");
        const Folder f = scan(r.settings);
        CHECK(f.ok && f.file.filename() == kName, "one Settings file, the same name: " + f.why);
        CHECK(r.others() == others, "the career, squads and ProfileOptions files are untouched");
        std::error_code ec;
        CHECK(!fs::exists(r.settings / "~turbo_settings_write.tmp", ec), "no temp file left");
        // the backup
        CHECK(o.backup == r.out / backup_dir() / "20261014-220000-000" && md5_of(o.backup / kName) == md5_hex(before), "backup: " + o.backup.u8string());
        const json rec = read_json(o.backup / "backup.json");
        CHECK(rec.value("file", "") == kName && rec.value("md5_before", "") == md5_hex(before) && rec.value("md5_after", "") == md5_hex(after) &&
                  rec["writes"].size() == 1 && rec["writes"][0].value("to", 0) == int(want),
              "backup.json");
        // the same values again: nothing to change, no new backup
        Rig* rp = &r;
        rp->env.stamp = "20261014-220001-000";
        const Outcome o2 = patch(r.env, r.map, {{0xEA, want, "cpu.sprint_speed"}}, "test");
        CHECK(o2.ok && o2.message.find("Nothing to change") != std::string::npos && !fs::exists(r.out / backup_dir() / "20261014-220001-000", ec), o2.message);
        // a value above 100 is brought to 100
        const Outcome o3 = patch(r.env, r.map, {{0x116, 250, "user.sprint_speed"}}, "test");
        CHECK(o3.ok && r.at(0x116) == 100, o3.message);
        // dry run: nothing written
        const std::string md5_now = md5_of(r.settings / kName);
        Env dry = r.env;
        dry.dry_run = true;
        dry.stamp = "20261014-220009-000";
        const Outcome o4 = patch(dry, r.map, {{0xEB, 3, "cpu.acceleration"}}, "test");
        CHECK(o4.ok && o4.message.find("Dry run") != std::string::npos && md5_of(r.settings / kName) == md5_now &&
                  !fs::exists(r.out / backup_dir() / "20261014-220009-000", ec),
              o4.message);
    });

    run_case("game settings: every refusal of patch writes nothing (game running, two files, CRC, outside the regions, layout moved, unknown game state)", [] {
        auto unchanged = [](Rig& r, const Outcome& o, const std::string& want, const char* what, const std::string& md5) {
            std::error_code ec;
            CHECK(!o.ok && o.message.find(want) != std::string::npos, std::string(what) + ": " + o.message);
            CHECK(md5_of(r.settings / kName) == md5 && !fs::exists(r.out / backup_dir(), ec), std::string(what) + ": nothing written, no backup");
        };
        {
            Rig r("refuse_running");
            const std::string m = md5_of(r.settings / kName);
            r.running = true;
            unchanged(r, patch(r.env, r.map, {{0xEA, 5, "x"}}, "t"), "FC 27 is running", "game running", m);
            Env e = r.env;
            e.game_running = nullptr;
            r.running = false;
            unchanged(r, patch(e, r.map, {{0xEA, 5, "x"}}, "t"), "cannot tell", "no running check", m);
        }
        {
            Rig r("refuse_two");
            const std::string m = md5_of(r.settings / kName);
            write_file(r.settings / "Settings20261014220000000", fixture(9));
            unchanged(r, patch(r.env, r.map, {{0xEA, 5, "x"}}, "t"), "2 Settings files", "two Settings files", m);
        }
        {
            std::vector<uint8_t> b = fixture();
            b[kPayloadOff + 0x8000] ^= 0x10;
            Rig r("refuse_crc", b);
            unchanged(r, patch(r.env, r.map, {{0xEA, 5, "x"}}, "t"), "CRC", "CRC fails", md5_of(r.settings / kName));
        }
        {
            std::vector<uint8_t> b = fixture();
            b.resize(kFileSize + 16);
            Rig r("refuse_size", b);
            unchanged(r, patch(r.env, r.map, {{0xEA, 5, "x"}}, "t"), "size", "size mismatch", md5_of(r.settings / kName));
        }
        {
            std::vector<uint8_t> b = fixture();
            put32(b, kPayloadOff + kConstAt, 0x38944);
            fix_crc(b);
            Rig r("refuse_section", b);
            unchanged(r, patch(r.env, r.map, {{0xEA, 5, "x"}}, "t"), "section mismatch", "section mismatch", md5_of(r.settings / kName));
        }
        {
            Rig r("refuse_region");
            const std::string m = md5_of(r.settings / kName);
            unchanged(r, patch(r.env, r.map, {{0x143, 5, "x"}}, "t"), "outside the slider map", "outside the regions", m);
            unchanged(r, patch(r.env, r.map, {{0xEA, 5, "a"}, {0x10, 1, "b"}}, "t"), "outside the slider map", "one write outside: none written", m);
        }
        {
            std::vector<uint8_t> b = fixture();
            put32(b, kPayloadOff + 0x1BC4 + 4 * 0xEC, 384);  // a slot that is not a slider value now
            fix_crc(b);
            Rig r("refuse_moved", b);
            unchanged(r, patch(r.env, r.map, {{0xEC, 5, "x"}}, "t"), "layout moved", "layout moved", md5_of(r.settings / kName));
        }
        {
            // the folder holds only careers / squads: nothing to write, they stay as they are
            Rig r("refuse_cmmgr");
            std::error_code ec;
            fs::remove(r.settings / kName, ec);
            const std::string others = r.others();
            const Outcome o = patch(r.env, r.map, {{0xEA, 5, "x"}}, "t");
            CHECK(!o.ok && o.message.find("no Settings file") != std::string::npos && r.others() == others, "only CmMgrC / Squads: " + o.message);
        }
        {
            Rig r("refuse_empty");
            CHECK(!patch(r.env, r.map, {}, "t").ok, "no write: refused");
        }
    });

    run_case("game settings: presets - JSON round trip, refusals, the folder, unique names, Save current takes verified sliders only", [] {
        Rig r("presets");
        const gsettings::Snapshot s = read_current(r.settings, r.map);
        Preset p = preset_from_snapshot("Sim realistic", s, r.map, "2026-10-14T22:00");
        CHECK(p.values.size() == 2 && p.values.count("cpu.sprint_speed") && p.values.count("user.sprint_speed") && !p.values.count("cpu.acceleration") &&
                  p.src_md5 == s.md5 && p.src_file == kName && p.src_layout == "fbchunks-454-bc650",
              fmt("verified only: %zu", p.values.size()));
        Preset back;
        std::string err;
        CHECK(Preset::parse(p.to_json(), back, &err) && back.name == p.name && back.values == p.values && back.src_md5 == p.src_md5 &&
                  back.created == p.created,
              "round trip: " + err);
        auto bad = [&](const std::string& t, const std::string& want) {
            Preset x;
            std::string e;
            CHECK(!Preset::parse(t, x, &e) && e.find(want) != std::string::npos, want + ": " + e);
        };
        bad(R"({"name": "x", "values": {}})", "not a Turbo");
        bad(R"({"turbo_settings_preset": 1, "values": {}})", "no name");
        bad(R"({"turbo_settings_preset": 1, "name": "x", "values": {"cpu.sprint_speed": 101}})", "outside 0..100");
        bad(R"({"turbo_settings_preset": 1, "name": "x", "values": {"cpu.sprint_speed": 5.5}})", "whole number");
        bad(R"({"turbo_settings_preset": 1, "name": "x", "values": {"0x1F6C": 5}})", "not a slider id");
        // the folder
        const fs::path dir = r.out / presets_dir();
        CHECK(write_preset_file(dir, p, &err) && p.file == dir / "Sim_realistic.json", "written: " + err);
        std::ofstream(dir / "broken.json") << "{";
        Preset q = p;
        q.name = "alpha";
        write_preset_file(dir, q, &err);
        std::vector<std::string> errors;
        std::vector<Preset> all = load_presets(dir, &errors);
        CHECK(all.size() == 2 && all[0].name == "alpha" && all[1].name == "Sim realistic" && errors.size() == 1 && errors[0].find("broken.json") == 0,
              fmt("%zu presets, %zu errors", all.size(), errors.size()));
        CHECK(unique_preset_name(all, "Sim realistic") == "Sim realistic 2" && unique_preset_name(all, "SIM REALISTIC") == "SIM REALISTIC 2" &&
                  unique_preset_name(all, "beta") == "beta",
              "unique names");
        CHECK(delete_preset_file(all[0], &err) && load_presets(dir, nullptr).size() == 1, "deleted");
        // the plan: unverified / not mapped / unknown skipped; clamped to the entry's range
        Preset x;
        x.name = "mixed";
        x.values = {{"cpu.sprint_speed", 40}, {"user.sprint_speed", 95}, {"cpu.acceleration", 60}, {"match.half_length", 3}, {"nope.id", 1}};
        const Plan pl = plan_preset(x, r.map);
        CHECK(pl.values.size() == 2 && pl.values.at("cpu.sprint_speed") == 40 && pl.values.at("user.sprint_speed") == 90 && pl.skipped.size() == 3 &&
                  pl.clamped.size() == 1,
              fmt("%zu values, %zu skipped, %zu clamped", pl.values.size(), pl.skipped.size(), pl.clamped.size()));
        // the shipped map (nothing verified): Save current saves nothing
        SliderMap shipped;
        load_map(fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file(), shipped, &err);
        {
            const Preset sp = preset_from_snapshot("x", read_current(r.settings, shipped), shipped, "");
            CHECK(sp.values.size() == 3 && sp.values.count("user.sprint_speed") && sp.values.count("user.acceleration") && sp.values.count("cpu.sprint_speed"),
                  "shipped map: only the three verified Custom bytes are saved");
        }
    });

    run_case("game settings: Load stages settings_pending.json (verified only); --apply writes it, renames it; nothing staged twice; never with the shipped map", [] {
        Rig r("stage");
        Preset p;
        p.name = "Press";
        p.values = {{"cpu.sprint_speed", 41}, {"user.sprint_speed", 59}, {"cpu.acceleration", 12}};
        std::string msg;
        // the shipped map: nothing verified, nothing staged
        SliderMap shipped;
        load_map(fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file(), shipped, &msg);
        for (Entry& e : shipped.entries) e.verified = false;  // 2.10: the shipped map now has 3 verified bytes; this case needs "none"
        Preset named;
        named.name = "Named";
        named.values = {{"cpu.sprint_speed", 41}};
        std::error_code ec;
        CHECK(!stage_pending(r.out, named, shipped, "t", &msg) && msg.find("Nothing staged") != std::string::npos && !fs::exists(r.out / pending_file(), ec), msg);
        CHECK(stage_pending(r.out, p, r.map, "2026-10-14T22:10:00", &msg) && msg.find("2 slider(s)") != std::string::npos, msg);
        const json pj = read_json(r.out / pending_file());
        CHECK(pj["values"].size() == 2 && !pj["values"].contains("cpu.acceleration") && pj.value("preset", "") == "Press", "pending holds the verified two");
        CHECK(load_presets(r.out / presets_dir(), nullptr).empty(), "the staged file is not a preset (it is outside the presets folder)");
        // the game running: refused, the pending file stays
        r.running = true;
        Outcome o = apply_pending(r.env, r.map);
        CHECK(!o.ok && fs::exists(r.out / pending_file(), ec), "running: " + o.message);
        r.running = false;
        // with the shipped map (no verified entry) nothing is applied even from a staged file
        o = apply_pending(r.env, shipped);
        CHECK(!o.ok && o.message.find("verified") != std::string::npos && fs::exists(r.out / pending_file(), ec), "shipped map: " + o.message);
        const uint32_t acc = r.at(0xEB);
        o = apply_pending(r.env, r.map);
        CHECK(o.ok && r.at(0xEA) == 41 && r.at(0x116) == 59 && r.at(0xEB) == acc, "applied: " + o.message);
        CHECK(!fs::exists(r.out / pending_file(), ec) && fs::exists(r.out / "settings_pending.applied_20261014-220000-000.json", ec), "renamed after the apply");
        o = apply_pending(r.env, r.map);
        CHECK(!o.ok && o.message.find("Nothing staged") != std::string::npos, "second apply: " + o.message);
        // a hand-written pending with an unverified id only: refused
        std::ofstream(r.out / pending_file()) << R"({"turbo_settings_pending": 1, "values": {"cpu.acceleration": 3}})";
        o = apply_pending(r.env, r.map);
        CHECK(!o.ok && r.at(0xEB) == acc, "unverified only: " + o.message);
    });

    run_case("game settings: Undo puts the backup back (md5), only while the file is Turbo's unless --force; one Settings file after", [] {
        Rig r("undo");
        const std::string orig = md5_of(r.settings / kName);
        const std::string others = r.others();
        Outcome o = undo_last(r.env, false);
        CHECK(!o.ok && o.message.find("Nothing to undo") != std::string::npos, o.message);
        o = patch(r.env, r.map, {{0xEA, 3, "cpu.sprint_speed"}}, "t");
        CHECK(o.ok, o.message);
        r.running = true;
        o = undo_last(r.env, false);
        CHECK(!o.ok && o.message.find("running") != std::string::npos, "running: " + o.message);
        r.running = false;
        o = undo_last(r.env, false);
        CHECK(o.ok && md5_of(r.settings / kName) == orig && scan(r.settings).ok && r.others() == others, "undone: " + o.message);
        o = undo_last(r.env, false);
        CHECK(!o.ok && o.message.find("put back already") != std::string::npos, "twice: " + o.message);
        // the game saved a new file since the write: refused without --force; with --force the backup is back and the game's file gone
        r.env.stamp = "20261014-230000-000";
        o = patch(r.env, r.map, {{0xEA, 4, "cpu.sprint_speed"}}, "t");
        CHECK(o.ok, o.message);
        std::error_code ec;
        fs::remove(r.settings / kName, ec);
        write_file(r.settings / "Settings20261014231500000", fixture(11));
        const std::string game_md5 = md5_of(r.settings / "Settings20261014231500000");
        o = undo_last(r.env, false);
        CHECK(!o.ok && o.message.find("no longer the one Turbo wrote") != std::string::npos && md5_of(r.settings / "Settings20261014231500000") == game_md5,
              "game saved since: " + o.message);
        o = undo_last(r.env, true);
        const Folder f = scan(r.settings);
        CHECK(o.ok && f.ok && f.file.filename() == kName && md5_of(r.settings / kName) == orig && r.others() == others, "--force: " + o.message);
        // a backup whose file does not match its md5: refused
        r.env.stamp = "20261014-235900-000";
        o = patch(r.env, r.map, {{0xEA, 6, "cpu.sprint_speed"}}, "t");
        write_file(o.backup / kName, fixture(13));
        const std::string now = md5_of(r.settings / kName);
        o = undo_last(r.env, false);
        CHECK(!o.ok && o.message.find("md5 differs") != std::string::npos && md5_of(r.settings / kName) == now, "tampered backup: " + o.message);
    });

    run_case("game settings: marker test - a COPY with disjoint markers (A 1..44, B 56..99, never 50) + legend; the settings folder only read; "
             "install checks the source, then Undo",
             [] {
        Rig r("markers");
        SliderMap shipped;
        std::string err;
        CHECK(load_map(fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file(), shipped, &err), err);
        const auto folder_before = md5s(r.settings);
        const fs::path mk = r.root / "markers";
        Outcome o = write_markers(r.settings, mk, shipped, "blocks");
        CHECK(o.ok, o.message);
        CHECK(md5s(r.settings) == folder_before, "the settings folder is unchanged (read only)");
        std::vector<uint8_t> copy;
        CHECK(read_bytes(mk / kName, copy) && validate(copy).ok && md5_hex(copy) == o.md5_after, "the copy has the same name and validates");
        std::set<uint32_t> seen;
        bool ok_a = true, ok_b = true;
        for (int k = 0; k < 44; ++k) {
            const uint32_t a = get32(copy, kPayloadOff + shipped.offset_of(0xEA + k)), b = get32(copy, kPayloadOff + shipped.offset_of(0x116 + k));
            ok_a = ok_a && a == uint32_t(1 + k);
            ok_b = ok_b && b == uint32_t(56 + k);
            seen.insert(a);
            seen.insert(b);
        }
        CHECK(ok_a && ok_b && seen.size() == 88 && !seen.count(50), "A = 1..44, B = 56..99, all distinct, no 50");
        const std::vector<uint8_t> src = r.bytes();
        bool rest_same = true;
        for (size_t i = 0; i < kFileSize; ++i) {
            const size_t po = i >= kPayloadOff ? i - kPayloadOff : 0;
            const bool in_blocks = i >= kPayloadOff && po >= shipped.offset_of(0xEA) && po < shipped.offset_of(0x142);
            const bool in_crc = i >= kPayloadOff + kCrcAt && i < kPayloadOff + kCrcAt + 4;
            if (!in_blocks && !in_crc && src[i] != copy[i]) rest_same = false;
        }
        CHECK(rest_same, "only the blocks and the CRC differ from the source");
        const json legend = read_json(mk / "markers.json");
        CHECK(legend.value("source_file", "") == kName && legend.value("source_md5", "") == o.md5_before && legend["markers"].size() == 88 &&
                  legend["markers"][0].value("region", "") == "block_a" && legend["markers"][44].value("value", 0) == 56,
              "markers.json");
        std::vector<uint8_t> txt;
        CHECK(read_bytes(mk / "markers.txt", txt) && std::string(txt.begin(), txt.end()).find(" 56  = block_b  slot  0") != std::string::npos, "markers.txt legend");
        // the run set: the 36-value run only
        Outcome orun = write_markers(r.settings, r.root / "markers_run", shipped, "run");
        std::vector<uint8_t> rc;
        read_bytes(r.root / "markers_run" / kName, rc);
        CHECK(orun.ok && rc.size() == kFileSize && get32(rc, kPayloadOff + shipped.offset_of(0xC6)) == 1 && get32(rc, kPayloadOff + shipped.offset_of(0xE9)) == 36 &&
                  get32(rc, kPayloadOff + shipped.offset_of(0xEA)) == get32(src, kPayloadOff + shipped.offset_of(0xEA)),
              "run set: " + orun.message);
        CHECK(!write_markers(r.settings, r.root / "m3", shipped, "all").ok, "an unknown set is refused");
        CHECK(!write_markers(r.settings, r.settings, shipped, "blocks").ok && md5s(r.settings) == folder_before, "never into the settings folder itself");
        // install: refused when the file changed since the copy; then ok (= the copy's bytes), Undo restores
        r.running = true;
        o = install_markers(r.env, shipped, mk);
        CHECK(!o.ok && md5s(r.settings) == folder_before, "running: " + o.message);
        r.running = false;
        const std::string orig = md5_of(r.settings / kName);
        o = install_markers(r.env, shipped, mk);
        CHECK(o.ok && md5_of(r.settings / kName) == legend.value("marker_md5", "") && o.lines.size() == 88, "installed: " + o.message);
        o = undo_last(r.env, false);
        CHECK(o.ok && md5_of(r.settings / kName) == orig && md5s(r.settings) == folder_before, "undone: " + o.message);
        write_file(r.settings / kName, fixture(21));
        o = install_markers(r.env, shipped, mk);
        CHECK(!o.ok && o.message.find("no longer the one") != std::string::npos, "source changed: " + o.message);
    });
}

// --------------------------------------------------------------------------------------------------------------------------- UI
static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("UI: Game settings tab - read-only values, the closed-game note, Save current / Load (stage) / Duplicate / Rename / Delete / Discard; "
             "the settings folder is never written",
             [&] {
        const fs::path root = g_out / "gsettings" / "ui";
        std::error_code ec;
        fs::remove_all(root, ec);
        const fs::path settings = root / "settings";
        write_file(settings / kName, fixture());
        write_file(settings / "CmMgrC20261014211520606", std::vector<uint8_t>(4096, 0xC1));
        const auto folder_before = md5s(settings);
        const fs::path map = root / "slider_map.json";
        std::ofstream(map, std::ios::binary) << test_map();
        const fs::path out = le / "turbo_output";
        fs::remove_all(out / presets_dir(), ec);
        fs::remove(out / pending_file(), ec);
        game_settings_set_paths(settings, map);
        game_settings_reset();
        app.request_tab = 7;
        ui.frames(3);
        const GameSettingsViewState& s = game_settings_view_state();
        CHECK(s.drawn && s.closed_note && s.file_ok && s.file_line.find(kName) != std::string::npos && s.file_line.find("CRC ok") != std::string::npos,
              "file: " + s.file_line);
        CHECK(s.map_line.find("4 sliders, 2 verified") != std::string::npos, s.map_line);
        auto row = [&](const std::string& id) -> const GameSettingsViewState::Row* {
            for (const auto& r : s.rows)
                if (r.id == id) return &r;
            return nullptr;
        };
        const auto* rc = row("cpu.sprint_speed");
        const auto* ra = row("cpu.acceleration");
        const auto* rh = row("match.half_length");
        CHECK(rc && rc->state == "verified" && ra && ra->state == "not verified: read-only" && rh && rh->value == "not mapped yet" && s.slots == 124,
              fmt("rows %zu, slots %d", s.rows.size(), s.slots));
        const int reads = s.file_reads;
        ui.frames(20);
        CHECK(s.file_reads == reads, fmt("the file is not read every frame (%d -> %d)", reads, s.file_reads));
        // Save current
        const ItemRec* box = ui.find("##gsname");
        CHECK(box && ui.type_into(box, "My sliders"), "name typed");
        CHECK(ui.click("Save current##gssave"), "Save current");
        ui.frames(2);
        CHECK(fs::exists(out / presets_dir() / "My_sliders.json", ec) && s.presets == std::vector<std::string>({"My sliders"}), "saved: " + s.preset_line);
        const json pj = read_json(out / presets_dir() / "My_sliders.json");
        CHECK(pj["values"].size() == 2 && pj["source"].value("file", "") == kName, "the preset holds the two verified sliders");
        // Load = stage
        CHECK(ui.click("Load: apply when the game is closed (stage)##gsload"), "Load");
        ui.frames(2);
        CHECK(fs::exists(out / pending_file(), ec) && s.pending_line.find("\"My sliders\", 2 slider(s)") != std::string::npos, "staged: " + s.pending_line);
        CHECK(md5s(settings) == folder_before, "Load wrote nothing into the settings folder");
        // Duplicate, Rename, Delete
        CHECK(ui.click("Duplicate##gsdup"), "Duplicate");
        ui.frames(2);
        CHECK(s.presets == std::vector<std::string>({"My sliders", "My sliders copy"}), "duplicated: " + s.preset_line);
        CHECK(ui.click("Rename...##gsren"), "Rename");
        ui.frames(2);
        const ItemRec* rb = ui.find("##gsrenamebox");
        CHECK(rb && ui.type_into(rb, "Second"), "new name typed");
        CHECK(ui.click("Rename##gsrenameok", "Rename preset"), "rename confirmed");
        ui.frames(2);
        CHECK(s.presets == std::vector<std::string>({"My sliders", "Second"}) && !fs::exists(out / presets_dir() / "My_sliders_copy.json", ec), "renamed: " + s.preset_line);
        CHECK(ui.click("Delete...##gsdel"), "Delete");
        ui.frames(2);
        CHECK(ui.click("Delete##gsdeleteok", "Delete preset?"), "delete confirmed");
        ui.frames(2);
        CHECK(s.presets == std::vector<std::string>({"My sliders"}), "deleted: " + s.preset_line);
        // Discard staged
        CHECK(ui.click("Discard staged##gsdiscard"), "Discard staged");
        ui.frames(2);
        CHECK(!fs::exists(out / pending_file(), ec) && s.pending_line.empty(), "discarded");
        // Refresh reads the file again; nothing in the settings folder changed through all of it
        CHECK(ui.click("Refresh##gsrefresh"), "Refresh");
        ui.frames(2);
        CHECK(s.file_reads > reads && md5s(settings) == folder_before, "the settings folder is unchanged after every flow");
        // the shipped map (nothing verified): Save current saves nothing and says why
        game_settings_set_paths(settings, fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file());
        game_settings_reset();
        ui.frames(3);
        CHECK(s.map_line.find("3 verified") != std::string::npos, s.map_line);  // 2.10: the three observed Custom bytes
        const ItemRec* box2 = ui.find("##gsname");
        CHECK(box2 && ui.type_into(box2, "Nothing"), "name typed");
        CHECK(ui.click("Save current##gssave"), "Save current");
        ui.frames(2);
        {
            // the three verified Custom bytes are saved (50/50/50 in the fixture); no unverified slider is in the preset
            Preset sp;
            std::string perr;
            std::vector<uint8_t> pb;
            CHECK(fs::exists(out / presets_dir() / "Nothing.json", ec) && read_bytes(out / presets_dir() / "Nothing.json", pb) &&
                      Preset::parse(std::string(pb.begin(), pb.end()), sp, &perr) && sp.values.size() == 3 && sp.values.count("user.sprint_speed") == 1,
                  "3 verified values saved: " + s.preset_line);
            fs::remove(out / presets_dir() / "Nothing.json", ec);
        }
        // a missing settings folder: a plain line, no throw, nothing created
        game_settings_set_paths(root / "nope", map);
        game_settings_reset();
        ui.frames(3);
        CHECK(!s.file_ok && s.file_line.find("not there") != std::string::npos && !fs::exists(root / "nope", ec), s.file_line);
        fs::remove_all(out / presets_dir(), ec);
        game_settings_set_paths({}, {});
        game_settings_reset();
        app.request_tab = 0;
        ui.frames(2);
    });
}

// ---------------------------------------------------------------------------------------------- 2.10: the Custom u8 block (TEST_31 G-S1)
static std::string custom_map(bool verified) {
    const std::string v = verified ? R"("verified": true, "proof": "TEST_31 section 7")" : R"("verified": false)";
    return std::string(R"({"turbo_settings_map": 1, "build": "6AC07E31", "layout": "fbchunks-454-bc650", "array_base": "0x1BC4",
 "regions": [{"id": "run36", "first": "0xC6", "count": 36}, {"id": "block_a", "first": "0xEA", "count": 44}, {"id": "block_b", "first": "0x116", "count": 44}],
 "custom_block": {"base": "0xB8976", "count": 125},
 "entries": [
  {"id": "user.sprint_speed", "label": "Sprint speed", "group": "user", "custom_index": "0x50", )") + v + R"(},
  {"id": "user.acceleration", "label": "Acceleration", "group": "user", "custom_index": "0x51", )" + v + R"(},
  {"id": "cpu.sprint_speed", "label": "Sprint speed", "group": "cpu", "custom_index": "0x24", )" + v + R"(},
  {"id": "cpu.acceleration", "label": "Acceleration", "group": "cpu", "custom_index": "0x25", "verified": false}]})";
}

// The real G-S1 files (read-only evidence). TURBO_G_S1_DIR overrides; a missing folder is a loud SKIP, never a pass.
static fs::path gs1_dir() {
    if (const char* e = std::getenv("TURBO_G_S1_DIR")) return fs::path(e);
    return fs::path("C:/FC 27 Live Editor/turbo_dev/evidence/2026-10-15/g_s1");
}
static bool gs1_load(std::vector<uint8_t>& before, std::vector<uint8_t>& after) {
    const fs::path d = gs1_dir();
    return read_bytes(d / "before" / "Settings20261015210509608", before, 1u << 20) && read_bytes(d / "after" / "Settings20261015210853207", after, 1u << 20);
}
static std::vector<Write> gs1_writes(const SliderMap& m) {
    std::vector<Write> w;
    for (auto [id, v] : {std::pair<const char*, int>{"user.sprint_speed", 37}, {"user.acceleration", 41}, {"cpu.sprint_speed", 63}}) {
        Write x;
        x.custom = true;
        x.index = m.entry(id)->custom_index;
        x.value = uint32_t(v);
        x.what = id;
        w.push_back(x);
    }
    return w;
}
static std::string hx(size_t v) {
    char b[24];
    std::snprintf(b, sizeof(b), "0x%zX", v);
    return b;
}

static void run_custom() {
    std::printf("native game settings: the Custom u8 block (2.10)\n");

    run_case("game settings custom: map parse (custom_block, custom_index, verified rules)", [] {
        SliderMap m;
        std::string err;
        CHECK(SliderMap::parse(custom_map(true), m, &err), err);
        CHECK(m.custom_base == 0xB8976 && m.custom_count == 125 && m.custom_offset_of(0x50) == 0xB8976 + 0x50, "block base/count");
        const Entry* e = m.entry("user.sprint_speed");
        CHECK(e && e->custom_index == 0x50 && e->index == -1 && e->verified && e->writable(), "entry with a custom index only is writable when verified");
        CHECK(!m.entry("cpu.acceleration")->writable(), "unverified stays read-only");
        auto bad = [&](const std::string& from, const std::string& to, const char* want) {
            std::string t = custom_map(true), er;
            const size_t at = t.find(from);
            CHECK(at != std::string::npos, "test text");
            t.replace(at, from.size(), to);
            SliderMap x;
            CHECK(!SliderMap::parse(t, x, &er) && er.find(want) != std::string::npos, std::string(want) + ": " + er);
        };
        bad("\"custom_index\": \"0x50\"", "\"custom_index\": \"0x7D\"", "custom_index");  // one past the block
        bad("\"custom_index\": \"0x51\"", "\"custom_index\": \"0x50\"", "used twice");
        bad("\"count\": 125", "\"count\": 0", "custom_block");
        bad("\"base\": \"0xB8976\"", "\"base\": \"0xBC650\"", "custom_block");
        // verified with neither index is refused
        std::string t = custom_map(true), er;
        const std::string from = "\"custom_index\": \"0x25\", \"verified\": false";
        t.replace(t.find(from), from.size(), "\"verified\": true, \"proof\": \"x\"");
        SliderMap x;
        CHECK(!SliderMap::parse(t, x, &er) && er.find("verified without an index") != std::string::npos, er);
    });

    run_case("game settings custom: patch writes the u8 bytes only, CRC valid, guards refuse", [] {
        Rig r("custom_patch");
        SliderMap m;
        std::string err;
        CHECK(SliderMap::parse(custom_map(true), m, &err), err);
        r.map = m;
        const std::vector<uint8_t> before = r.bytes();
        const std::vector<Write> w = gs1_writes(m);
        const std::string others = r.others();
        Outcome o = patch(r.env, m, w, "custom test");
        CHECK(o.ok, o.message);
        const std::vector<uint8_t> after = r.bytes();
        CHECK(validate(after).ok, "the CRC was fixed");
        for (size_t i = 0; i < before.size(); ++i)
            if (before[i] != after[i]) {
                const size_t pi = i - kPayloadOff;
                CHECK((pi >= kCrcAt && pi < kCrcAt + 4) || pi == 0xB8976 + 0x50 || pi == 0xB8976 + 0x51 || pi == 0xB8976 + 0x24, "unexpected byte " + hx(i));
            }
        CHECK(after[kPayloadOff + 0xB8976 + 0x50] == 37 && after[kPayloadOff + 0xB8976 + 0x51] == 41 && after[kPayloadOff + 0xB8976 + 0x24] == 63, "values");
        CHECK(r.others() == others, "the other save files are untouched");
        const gsettings::Snapshot s = read_current(r.settings, m);
        CHECK(s.ok && s.values.at("user.sprint_speed") == 37 && s.values.at("cpu.sprint_speed") == 63 && s.values.at("cpu.acceleration") == 50 &&
                  s.custom_slots.size() == 125 && s.custom_slots[0x50].second == 37,
              "snapshot reads the custom bytes");
        CHECK(undo_last(r.env, false).ok && r.bytes() == before, "undo restores the file");
        auto refuse = [&](std::vector<uint8_t> file, int idx, const char* want, const char* what) {
            Rig q("custom_guard", file);
            Write x;
            x.custom = true;
            x.index = idx;
            x.value = 10;
            x.what = "g";
            const Outcome g = patch(q.env, m, {x}, "g");
            CHECK(!g.ok && g.message.find(want) != std::string::npos, std::string(what) + ": " + g.message);
            CHECK(q.bytes() == file, std::string(what) + ": file unchanged");
        };
        refuse(before, 125, "outside", "index past the block");
        refuse(before, -1, "outside", "negative index");
        {
            auto f = before;
            f[kPayloadOff + 0xB8976 - 1] = 0;
            fix_crc(f);
            refuse(f, 0x50, "layout moved", "prefix changed");
        }
        {
            auto f = before;
            f[kPayloadOff + 0xB8976 + 0x50] = 101;
            fix_crc(f);
            refuse(f, 0x50, "0..100", "a byte above 100");
        }
        SliderMap none;
        CHECK(SliderMap::parse(test_map(), none, &err), err);
        Write x;
        x.custom = true;
        x.index = 0;
        x.value = 5;
        x.what = "n";
        Rig q("custom_nomap");
        CHECK(!patch(q.env, none, {x}, "n").ok, "a map with no custom block refuses a custom write");
    });

    run_case("game settings custom: apply_pending routes a verified custom entry to the byte", [] {
        Rig r("custom_pending");
        SliderMap m;
        std::string err;
        CHECK(SliderMap::parse(custom_map(true), m, &err), err);
        Preset p;
        p.name = "t";
        p.values = {{"user.sprint_speed", 12}, {"cpu.acceleration", 99}};
        std::string msg;
        CHECK(stage_pending(r.out, p, m, "20261015-000000-000", &msg), msg);
        const Outcome o = apply_pending(r.env, m);
        CHECK(o.ok, o.message);
        const auto b = r.bytes();
        CHECK(b[kPayloadOff + 0xB8976 + 0x50] == 12 && b[kPayloadOff + 0xB8976 + 0x25] == 50, "verified written, unverified (cpu.acceleration) not");
    });

    run_case("game settings custom: G-S1 real files (game CRC reproduced; byte-identical to the game's file)", [] {
        std::vector<uint8_t> before, after;
        if (!gs1_load(before, after)) {
            std::printf("  SKIP (NOT A PASS): the G-S1 evidence files are missing under %s (set TURBO_G_S1_DIR)\n", gs1_dir().u8string().c_str());
            return;
        }
        CHECK(before.size() == kFileSize && after.size() == kFileSize, "sizes");
        CHECK(md5_hex(before) == "d88f8b79906a80937c2a8d49564a58aa" && md5_hex(after) == "304c68b3bf5a82d5ba0fac9a84863be8", "the evidence files are the recorded ones");
        // (a) the game's own CRC is what our code computes, on both real files
        for (const auto* f : {&before, &after}) {
            const Check c = validate(*f);
            CHECK(c.ok && c.crc_stored == c.crc_calc, "real file validates: " + c.why);
        }
        CHECK(validate(after).crc_stored == 0xCE704494u, "the game's CRC of the after file");
        // (b) only our writes on the real before file: exactly the 3 bytes + the 4 CRC bytes differ
        SliderMap m;
        std::string err;
        CHECK(SliderMap::parse(custom_map(true), m, &err), err);
        const std::vector<Write> w = gs1_writes(m);
        Rig r("custom_real_b", before);
        Outcome o = patch(r.env, m, w, "g_s1 isolated");
        CHECK(o.ok, o.message);
        const std::vector<uint8_t> mine = r.bytes();
        std::vector<size_t> d;
        for (size_t i = 0; i < before.size(); ++i)
            if (before[i] != mine[i]) d.push_back(i);
        CHECK(d == std::vector<size_t>({0x476, 0x477, 0x478, 0x479, 0xB8E00, 0xB8E2C, 0xB8E2D}), "isolated diff = CRC word + the 3 slider bytes");
        // (c) before + what the game ALSO changed (meta block, 8 other payload bytes) + our 3 writes + fix_crc == the game's file, byte for byte
        std::vector<uint8_t> t = before;
        for (size_t i = kHeaderSize; i < kPayloadOff; ++i) t[i] = after[i];
        for (size_t i : {size_t(0x2142), size_t(0x2146), size_t(0x2147), size_t(0x216A), size_t(0x2536), size_t(0x2537), size_t(0x77855), size_t(0x77856)})
            t[i] = after[i];
        fix_crc(t);  // a valid file to patch (the pre-patch CRC check); the patch must then end on the game's CRC
        Rig r2("custom_real_c", t);
        o = patch(r2.env, m, w, "g_s1 byte-identical");
        CHECK(o.ok, o.message);
        const std::vector<uint8_t> built = r2.bytes();
        CHECK(built == after && md5_hex(built) == "304c68b3bf5a82d5ba0fac9a84863be8", "byte-identical to the game-written file (md5 " + md5_hex(built) + ")");
        uint32_t v = 0;
        CHECK(get_u32(after, 0x1BC4 + 4 * 0x116, v) && v == 35 && get_u32(after, 0x1BC4 + 4 * 0x117, v) && v == 48 && get_u32(after, 0x1BC4 + 4 * 0xEA, v) && v == 35,
              "the u32 slots of the old map did not move");
    });

    run_case("game settings custom: the SHIPPED map (3 observed bytes verified, nothing else)", [] {
        SliderMap m;
        std::string err;
        CHECK(load_map(fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file(), m, &err), err);
        CHECK(m.custom_base == 0xB8976 && m.custom_count == 125, "custom block in the shipped map");
        std::set<std::string> ver;
        for (const Entry& e : m.entries)
            if (e.verified) {
                ver.insert(e.id);
                CHECK(e.custom_index >= 0 && e.index < 0 && !e.proof.empty(), e.id + ": verified only through the custom block, with a proof");
            }
        CHECK(ver == std::set<std::string>({"user.sprint_speed", "user.acceleration", "cpu.sprint_speed"}), "exactly the three observed bytes are verified");
        CHECK(m.entry("user.sprint_speed")->custom_index == 0x50 && m.entry("user.acceleration")->custom_index == 0x51 && m.entry("cpu.sprint_speed")->custom_index == 0x24,
              "observed indices");
        CHECK(m.entry("cpu.acceleration")->custom_index == 0x25 && !m.entry("cpu.acceleration")->writable(), "cpu.acceleration assessed, read-only");
        CHECK(m.entry("user.injury_frequency")->custom_index == 0x5C && !m.entry("user.injury_frequency")->writable(), "injury frequency (id 0xE8) assessed, read-only");
        for (const Entry& e : m.entries)
            if (e.group == "user" || e.group == "cpu") CHECK(e.index < 0, e.id + ": no u32 index");
    });

    // ---------------------------------------------------------------- 2.12.1: all 125 Custom bytes, named from the game's own slider table
    run_case("game settings 2.12.1: the shipped map names all 125 Custom bytes from the game's table; only 3 are screen-proven", [] {
        SliderMap m;
        std::string err;
        CHECK(load_map(fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file(), m, &err), err);
        std::set<int> used;
        std::set<std::string> names;
        int screen = 0, table = 0;
        for (const Entry& e : m.entries) {
            if (e.custom_index < 0) continue;
            CHECK(used.insert(e.custom_index).second, e.id + ": custom index used twice");
            CHECK(!e.game_name.empty() && names.insert(e.game_name).second, e.id + ": the game's name is there and unique: " + e.game_name);
            if (e.verified) {
                ++screen;
                CHECK(e.evidence == "screen" && !e.inferred() && e.writable(), e.id + ": screen-proven");
            } else {
                ++table;
                CHECK(e.evidence == "table" && e.inferred() && !e.writable(), e.id + ": table-mapped, not writable without include_unverified");
            }
        }
        CHECK(used.size() == 125 && *used.begin() == 0 && *used.rbegin() == 124, fmt("%zu distinct bytes 0..124", used.size()));
        CHECK(screen == 3 && table == 122, fmt("%d screen-proven, %d table-mapped", screen, table));
        auto at = [&](const char* id) {
            const Entry* e = m.entry(id);
            return e ? e->custom_index : -99;
        };
        CHECK(at("cpu.defending_aggression") == 0 && at("cpu.long_shot_frequency") == 15 && at("user.defending_aggression") == 16 && at("user.long_shot_frequency") == 31,
              "CPU-AI: opponent 0..15, teammates 16..31");
        CHECK(at("meta.all_pass_speed") == 32 && at("meta.all_shot_error") == 35, "the four group sliders");
        CHECK(at("cpu.sprint_speed") == 36 && at("cpu.acceleration") == 37 && at("cpu.cross_error") == 36 + 20 && at("cpu.cross_speed") == 36 + 28 &&
                  at("cpu.physicality_impact") == 36 + 36 && at("cpu.pos_marking") == 73 && at("cpu.gk_ability") == 79,
              "CPU sliders 36..72, positioning 73..79");
        CHECK(at("user.sprint_speed") == 80 && at("user.acceleration") == 81 && at("user.trap_error") == 86 && at("user.pass_speed") == 91 && at("user.injury_frequency") == 92 &&
                  at("user.cross_error") == 100 && at("user.cross_speed") == 108 && at("user.physicality_impact") == 116 && at("user.pos_marking") == 117 &&
                  at("user.gk_ability") == 123 && at("user.power_bar_speed") == 124,
              "user sliders 80..116, positioning 117..123, power bar 124 (injury frequency 0xE8 = byte 0x5C is the code-verified anchor)");
        CHECK(at("user.dribble_error") == -1 && at("cpu.pass_angle_error") == -1 && !m.entry("user.dribble_error")->inferred(), "dribble error / pass angle error have no slot");
    });

    run_case("game settings 2.12.1: include_unverified stages the table-mapped sliders; the flag is saved; apply writes only the listed bytes", [] {
        Rig r("full_map");
        std::string err;
        const fs::path shipped = fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file();
        CHECK(load_map(shipped, r.map, &err), err);
        Preset p;
        p.name = "all sliders";
        for (const Entry& e : r.map.entries)
            if (e.custom_index >= 0) p.values[e.id] = 10 + e.custom_index % 80;
        CHECK(p.values.size() == 125 && !p.include_unverified, "125 mapped values, the flag is off by default");
        const Plan off = plan_preset(p, r.map);
        CHECK(off.values.size() == 3 && off.unverified.empty() && off.skipped.size() == 122, fmt("flag off: %zu staged, %zu skipped", off.values.size(), off.skipped.size()));
        CHECK(off.skipped.front().find("not seen on the EA screen") != std::string::npos, "the skip reason names the table mapping: " + off.skipped.front());
        p.include_unverified = true;
        const Plan on = plan_preset(p, r.map);
        CHECK(on.values.size() == 125 && on.unverified.size() == 122 && on.skipped.empty(), fmt("flag on: %zu staged, %zu unverified", on.values.size(), on.unverified.size()));
        Preset back;
        CHECK(Preset::parse(p.to_json(), back, &err) && back.include_unverified && back.values == p.values, "the flag round-trips: " + err);
        Preset noflag = p;
        noflag.include_unverified = false;
        CHECK(noflag.to_json().find("include_unverified") == std::string::npos && Preset::parse(noflag.to_json(), back, &err) && !back.include_unverified, "no key = false");
        // Load stages, the tool applies
        std::string msg;
        CHECK(stage_pending(r.out, p, r.map, "20261016-000000-000", &msg) && msg.find("122 of them") != std::string::npos, msg);
        std::vector<uint8_t> pb;
        Pending pd;
        CHECK(read_bytes(r.out / pending_file(), pb) && Pending::parse(std::string(pb.begin(), pb.end()), pd, &err) && pd.values.size() == 125 && pd.unverified.size() == 122,
              "staged file: " + err);
        const Outcome o = apply_pending(r.env, r.map);
        CHECK(o.ok, o.message);
        const auto b = r.bytes();
        int wrong = 0;
        for (int k = 0; k < 125; ++k)
            if (b[kPayloadOff + 0xB8976 + size_t(k)] != uint8_t(10 + k % 80)) ++wrong;
        CHECK(wrong == 0 && validate(b).ok, fmt("%d of 125 Custom bytes differ; CRC valid", wrong));
        // a staged file without the unverified list writes the three proven bytes only (the tool does not trust the values alone)
        Rig q("full_map_nolist");
        CHECK(load_map(shipped, q.map, &err), err);
        Pending nl = pd;
        nl.unverified.clear();
        CHECK(write_text(q.out / pending_file(), nl.to_json(), &err), err);
        const Outcome o2 = apply_pending(q.env, q.map);
        CHECK(o2.ok, o2.message);
        const auto b2 = q.bytes();
        int changed = 0;
        for (int k = 0; k < 125; ++k)
            if (b2[kPayloadOff + 0xB8976 + size_t(k)] != 50) ++changed;
        CHECK(changed == 3, fmt("%d bytes changed without the list (only the 3 screen-proven may)", changed));
        // a staged file naming a non-table id as unverified: that id is skipped, the verified byte is written
        Rig z("full_map_badlist");
        CHECK(load_map(shipped, z.map, &err), err);
        const std::string bad =
            "{\"turbo_settings_pending\": 1, \"preset\": \"x\", \"values\": {\"match.half_length\": 7, \"user.sprint_speed\": 44}, \"unverified\": [\"match.half_length\"]}";
        CHECK(write_text(z.out / pending_file(), bad, &err), err);
        const Outcome o3 = apply_pending(z.env, z.map);
        const auto b3 = z.bytes();
        CHECK(o3.ok && b3[kPayloadOff + 0xB8976 + 80] == 44, "the verified byte is written: " + o3.message);
        bool skipped = false;
        for (const std::string& l : o3.lines) skipped = skipped || l.find("match.half_length") != std::string::npos;
        CHECK(skipped, "match.half_length (not a table-mapped Custom byte) is skipped");
        const std::string badlist = "{\"turbo_settings_pending\": 1, \"preset\": \"x\", \"values\": {\"user.sprint_speed\": 44}, \"unverified\": [\"not.staged\"]}";
        Pending px;
        CHECK(!Pending::parse(badlist, px, &err) && err.find("unverified") != std::string::npos, "an unverified id that is not staged is refused: " + err);
    });
}

}  // namespace gsettings_test
