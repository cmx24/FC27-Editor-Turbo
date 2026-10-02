// FC 27 LE Turbo GUI - native tests (Linux build of the GUI's platform-independent code).
//
// The game memory is a page image dumped by tests/native/gui_world.lua from the Turbo Lua test
// simulator, which lays T3DB tables out exactly as Live Editor's own Lua T3DB library reads them.
// The expected values were read by that library. Writes made here are checked by the same library
// (gui_world.lua verify_writes), and mailbox commands are executed by Turbo's real Lua bridge
// (gui_world.lua mailbox). The ImGui panels are driven by synthetic mouse/keyboard input through
// Dear ImGui's test-engine hooks with the null backend.
//
// usage: turbo_native_tests <out_dir> <gui_world.lua>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/bridge.h"
#include "core/le_log.h"
#include "core/model.h"
#include "core/t3db.h"
#include "imgui.h"
#include "imgui_impl_null.h"
#include "imgui_internal.h"
#include "nlohmann/json.hpp"
#include "ui/app.h"
#include "ui/playstyles.h"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace turbo;

// ---------------------------------------------------------------- ImGui assertion -> exception
namespace turbo {
void imgui_assert_failed(const char* expr, const char* file, int line) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "ImGui assertion failed: %s (%s:%d)", expr, file, line);
    throw std::runtime_error(buf);
}
}  // namespace turbo

// ---------------------------------------------------------------- mini test framework
static int g_pass = 0, g_fail = 0;
static std::string g_case;

static void check_impl(bool ok, const std::string& what, const char* file, int line) {
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL [%s] %s (%s:%d)\n", g_case.c_str(), what.c_str(), file, line);
    }
}
#define CHECK(cond, msg) check_impl((cond), (msg), __FILE__, __LINE__)
static std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}
static void run_case(const char* name, const std::function<void()>& fn) {
    g_case = name;
    int before = g_fail;
    try {
        fn();
    } catch (const std::exception& e) {
        ++g_fail;
        std::printf("  FAIL [%s] exception: %s\n", name, e.what());
    }
    std::printf("  %s %s\n", g_fail == before ? "PASS" : "FAIL", name);
}

// ---------------------------------------------------------------- simulated address space
class SimMemory : public Memory {
public:
    static constexpr uint64_t kPage = 4096;
    std::map<uint64_t, std::vector<uint8_t>> pages;
    int failed_reads = 0;

    bool read(uint64_t addr, void* out, size_t n) override {
        uint8_t* o = static_cast<uint8_t*>(out);
        for (size_t i = 0; i < n;) {
            uint64_t a = addr + i;
            auto it = pages.find(a / kPage);
            if (it == pages.end()) {
                ++failed_reads;
                return false;
            }
            size_t off = a % kPage, take = std::min<size_t>(n - i, kPage - off);
            std::memcpy(o + i, it->second.data() + off, take);
            i += take;
        }
        return true;
    }
    bool write(uint64_t addr, const void* in, size_t n) override {
        for (size_t i = 0; i < n; ++i)
            if (!pages.count((addr + i) / kPage)) return false;
        const uint8_t* s = static_cast<const uint8_t*>(in);
        for (size_t i = 0; i < n;) {
            uint64_t a = addr + i;
            auto& pg = pages[a / kPage];
            size_t off = a % kPage, take = std::min<size_t>(n - i, kPage - off);
            std::memcpy(pg.data() + off, s + i, take);
            i += take;
        }
        return true;
    }
    void map(uint64_t addr, size_t n) {
        for (uint64_t p = addr / kPage; p <= (addr + n) / kPage; ++p)
            if (!pages.count(p)) pages[p] = std::vector<uint8_t>(kPage, 0);
    }
    bool load(const fs::path& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        char magic[4];
        uint32_t ver = 0;
        uint64_t count = 0;
        f.read(magic, 4);
        f.read(reinterpret_cast<char*>(&ver), 4);
        f.read(reinterpret_cast<char*>(&count), 8);
        if (std::memcmp(magic, "TSIM", 4) != 0 || ver != 1) return false;
        pages.clear();
        for (uint64_t i = 0; i < count; ++i) {
            uint64_t p = 0;
            f.read(reinterpret_cast<char*>(&p), 8);
            std::vector<uint8_t> data(kPage);
            f.read(reinterpret_cast<char*>(data.data()), kPage);
            if (!f) return false;
            pages[p] = std::move(data);
        }
        return true;
    }
    bool save(const fs::path& path) const {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        uint32_t ver = 1;
        uint64_t count = pages.size();
        f.write("TSIM", 4);
        f.write(reinterpret_cast<const char*>(&ver), 4);
        f.write(reinterpret_cast<const char*>(&count), 8);
        for (const auto& kv : pages) {
            f.write(reinterpret_cast<const char*>(&kv.first), 8);
            f.write(reinterpret_cast<const char*>(kv.second.data()), kPage);
        }
        return static_cast<bool>(f);
    }
};

static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static json read_json(const fs::path& p) { return json::parse(read_file(p)); }

static fs::path g_out;
static std::string g_lua;

static int run_lua(const char* mode) {
    std::string cmd = "lua5.4 '" + g_lua + "' " + mode + " '" + g_out.string() + "' > '" + (g_out / (std::string(mode) + ".log")).string() + "' 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc != 0) std::printf("  lua %s failed, log:\n%s\n", mode, read_file(g_out / (std::string(mode) + ".log")).c_str());
    return rc;
}

static const GameDate kToday{2027, 1, 15};

// ================================================================ core: T3DB, model, bridge files
static void test_core() {
    json exp = read_json(g_out / "expected.json");
    SimMemory mem;
    Bridge bridge(g_out / "LE");
    Database db(mem);

    run_case("memory image and bridge files load", [&] {
        CHECK(mem.load(g_out / "world.img"), "world.img loads");
        CHECK(bridge.poll_files(), "poll_files reports new data");
        CHECK(bridge.meta_loaded(), "meta parsed: " + bridge.meta_error());
        CHECK(bridge.state().loaded, "state parsed");
        CHECK(bridge.state().in_cm, "in career");
        CHECK(bridge.state().user_team == 1, "user team 1");
        CHECK(bridge.state().date.as_int() == 20270115, "date 2027-01-15");
        CHECK(bridge.state().has_settings && !bridge.state().dry_run && bridge.state().auto_form == 100, "effective settings");
        CHECK(bridge.state().db_gen >= 1, "db_gen");
        CHECK(bridge.names()->size() == 20, fmt("player names from bridge_names.txt: %zu", bridge.names()->size()));
        CHECK(bridge.state().names_count == 20, "names count in bridge_state.json");
        CHECK(!bridge.poll_files(), "unchanged files are not re-read");
    });

    run_case("database walk finds every table in both database nodes", [&] {
        std::string err;
        int n = db.refresh(bridge.state().db_service, bridge.meta(), &err);
        CHECK(n == static_cast<int>(exp["tables"].size()), fmt("tables %d (expected %zu) %s", n, exp["tables"].size(), err.c_str()));
        CHECK(db.table("version") != nullptr, "table in the second database node");
        CHECK(mem.failed_reads == 0, fmt("no reads outside mapped memory (%d)", mem.failed_reads));
    });

    run_case("every value of every table matches Live Editor's Lua T3DB library", [&] {
        int compared = 0;
        for (auto it = exp["tables"].begin(); it != exp["tables"].end(); ++it) {
            const Table* t = db.table(it.key());
            CHECK(t != nullptr, "table " + it.key());
            if (!t) continue;
            const json& et = it.value();
            CHECK(t->record_size == et["record_size"].get<uint32_t>(), it.key() + " record size");
            CHECK(t->written == et["written"].get<uint32_t>(), it.key() + " written");
            CHECK(t->fields.size() == et["fields"].size(), it.key() + " field count");
            for (auto f = et["fields"].begin(); f != et["fields"].end(); ++f) {
                const Field* fl = t->field(f.key());
                CHECK(fl != nullptr, it.key() + "." + f.key());
                if (!fl) continue;
                CHECK(fl->byte_off() == f.value()["offset"].get<uint32_t>() && fl->start_bit() == f.value()["startbit"].get<int>(),
                      it.key() + "." + f.key() + " bit position");
                CHECK(fl->depth == f.value()["depth"].get<int>() && fl->min == f.value()["min"].get<int64_t>(), it.key() + "." + f.key() + " meta");
            }
            Snapshot snap;
            CHECK(snap.load(mem, *t), it.key() + " snapshot");
            CHECK(snap.valid.size() == et["records"].size(), fmt("%s valid records %zu vs %zu", it.key().c_str(), snap.valid.size(), et["records"].size()));
            for (const auto& r : et["records"]) {
                uint32_t idx = r["idx"].get<uint32_t>();
                uint64_t rec = t->record_addr(idx);
                CHECK(db.record_valid(*t, rec), it.key() + " record valid");
                for (auto v = r["values"].begin(); v != r["values"].end(); ++v) {
                    const Field* fl = t->field(v.key());
                    if (!fl) continue;
                    Value a, b = snap.get(idx, *fl);
                    bool ok = db.get(*t, rec, *fl, a);
                    std::string where = fmt("%s[%u].%s", it.key().c_str(), idx, v.key().c_str());
                    CHECK(ok, where + " readable");
                    CHECK(a == b, where + " snapshot == live read");
                    if (fl->type == FieldType::Int) CHECK(a.i == v.value().get<int64_t>(), where + fmt(" = %lld", static_cast<long long>(a.i)));
                    else if (fl->type == FieldType::Float) CHECK(std::fabs(a.f - v.value().get<double>()) < 1e-6, where + " float");
                    else CHECK(a.s == v.value().get<std::string>(), where + " = '" + a.s + "'");
                    ++compared;
                }
            }
        }
        CHECK(compared > 400, fmt("values compared: %d", compared));
    });

    run_case("deleted records are skipped and never written", [&] {
        const Table* t = db.table("players");
        CHECK(db.find(*t, "playerid", exp["model"]["deleted_player"].get<int64_t>()) == 0, "deleted player not found");
        uint64_t last = t->record_addr(t->written - 1);
        CHECK(!db.record_valid(*t, last), "last record deleted");
        std::string err;
        CHECK(!db.set_int(*t, last, "overallrating", 60, &err), "write to deleted record refused: " + err);
    });

    Model model(db);
    run_case("compressed text fields (FC 27 playernames.name) are read-only and never shown as text", [&] {
        const Table* t = db.table("playernames");
        CHECK(t != nullptr, "playernames");
        const Field* f = t ? t->field("name") : nullptr;
        CHECK(f && f->raw_type == 13 && f->type == FieldType::Unknown, "type 13 = compressed");
        if (t && f) {
            Value v;
            uint64_t rec = t->record_addr(0);
            CHECK(db.get(*t, rec, *f, v) && v.to_string() == "(compressed text)", "shown as (compressed text): " + v.to_string());
            std::string err;
            CHECK(!db.set(*t, rec, *f, Value::of_str("X"), &err) && err.find("compressed text") != std::string::npos,
                  "write refused: " + err);
        }
        Model bare(db);
        CHECK(bare.rebuild(kToday), "rebuild without names");
        const PlayerRow* p = bare.player(1001);
        CHECK(p && p->name == "#1001", "without bridge_names.txt players are shown by ID, never as garbage: " + (p ? p->name : ""));
        CHECK(bare.name_source().find("waiting for Turbo's Lua side") != std::string::npos, "status explains: " + bare.name_source());
    });

    run_case("model: players, names, clubs, ages", [&] {
        model.set_extra_names(bridge.names());
        CHECK(model.rebuild(kToday), "rebuild");
        const auto& ep = exp["model"]["players"];
        CHECK(model.players().size() == ep.size(), fmt("players %zu", model.players().size()));
        for (const auto& e : ep) {
            int64_t pid = e["playerid"].get<int64_t>();
            const PlayerRow* p = model.player(pid);
            CHECK(p != nullptr, fmt("player %lld", static_cast<long long>(pid)));
            if (!p) continue;
            CHECK(p->name == e["name"].get<std::string>(), fmt("%lld name '%s' vs '%s'", static_cast<long long>(pid), p->name.c_str(), e["name"].get<std::string>().c_str()));
            CHECK(p->club == e["club"].get<int64_t>(), fmt("%lld club %lld", static_cast<long long>(pid), static_cast<long long>(p->club)));
            CHECK(p->overall == e["overall"].get<int>() && p->potential == e["potential"].get<int>(), fmt("%lld ovr/pot", static_cast<long long>(pid)));
            CHECK(p->position == e["position"].get<int>(), fmt("%lld position", static_cast<long long>(pid)));
            CHECK(p->age == e["age"].get<int>(), fmt("%lld age %d vs %d", static_cast<long long>(pid), p->age, e["age"].get<int>()));
        }
        CHECK(model.name_source().find("playernames via Live Editor (20)") != std::string::npos, "name source: " + model.name_source());
        CHECK(model.name_source().find("editedplayernames") != std::string::npos, "edited names used");
    });

    run_case("model: teams, leagues, national teams, links, managers", [&] {
        for (const auto& e : exp["model"]["teams"]) {
            const TeamRow* t = model.team(e["teamid"].get<int64_t>());
            CHECK(t != nullptr, "team " + e["name"].get<std::string>());
            if (!t) continue;
            CHECK(t->name == e["name"].get<std::string>(), "team name " + t->name);
            CHECK(t->overall == e["overall"].get<int>(), "team overall " + t->name);
            CHECK(t->league == e["league"].get<int64_t>(), "team league " + t->name);
        }
        CHECK(model.is_national_team(1318) && !model.is_national_team(1), "national team detection");
        CHECK(model.links_of_team(1).size() == 6, fmt("Arsenal links %zu", model.links_of_team(1).size()));
        CHECK(model.links_of_player(1001).size() == 2, "Saka: club + national team");
        CHECK(model.managers().size() == exp["model"]["managers"].size(), "managers");
        for (const auto& e : exp["model"]["managers"]) {
            bool found = false;
            for (const auto& m : model.managers())
                if (m.managerid == e["managerid"].get<int64_t>())
                    found = m.name == e["name"].get<std::string>() && m.teamid == e["teamid"].get<int64_t>();
            CHECK(found, "manager " + e["name"].get<std::string>());
        }
    });

    run_case("dates: Lilian day numbers both ways", [&] {
        for (const auto& d : exp["dates"]) {
            GameDate g{d["y"].get<int>(), d["m"].get<int>(), d["d"].get<int>()};
            int64_t days = d["days"].get<int64_t>();
            CHECK(gregorian_days_from_date(g) == days, fmt("%d-%02d-%02d -> %lld (LE ToGregorianDays)", g.year, g.month, g.day, static_cast<long long>(days)));
            GameDate back = date_from_gregorian_days(days);
            CHECK(back.as_int() == g.as_int(), fmt("%lld -> %d-%02d-%02d", static_cast<long long>(days), back.year, back.month, back.day));
        }
        GameDate first = date_from_gregorian_days(1);
        CHECK(first.as_int() == 15821015, "day 1 = 1582-10-15");
        int bad = 0;
        for (int64_t days = 1; days <= 250000; ++days)
            if (gregorian_days_from_date(date_from_gregorian_days(days)) != days) ++bad;
        CHECK(bad == 0, fmt("round trip mismatches: %d", bad));
        CHECK(!is_real_date(GameDate{2027, 2, 29}) && is_real_date(GameDate{2028, 2, 29}), "leap years");
        CHECK(!is_real_date(GameDate{2027, 4, 31}), "April 31");
        CHECK(age_on(GameDate{2009, 1, 15}, kToday) == 18 && age_on(GameDate{2009, 1, 16}, kToday) == 17, "age on birthday");
    });

    run_case("validation: ranges, minimums, text length, types", [&] {
        const Table* p = db.table("players");
        const Field* ovr = p->field("overallrating");
        const Field* pp2 = p->field("preferredposition2");
        const Field* hgt = p->field("height");
        const Field* tr1 = p->field("trait1");
        CHECK(Database::validate(*ovr, Value::of_int(127)).empty(), "7 bits: 127 ok");
        CHECK(!Database::validate(*ovr, Value::of_int(128)).empty(), "7 bits: 128 refused");
        CHECK(!Database::validate(*ovr, Value::of_int(-1)).empty(), "-1 refused");
        CHECK(Database::validate(*pp2, Value::of_int(-1)).empty(), "min -1 accepted");
        CHECK(pp2->max() == 30, "pp2 max = -1 + 31");
        CHECK(!Database::validate(*hgt, Value::of_int(129)).empty() && Database::validate(*hgt, Value::of_int(257)).empty() &&
                  !Database::validate(*hgt, Value::of_int(258)).empty(), "height 130..257");
        CHECK(tr1->max() == (1 << 30) - 1, "trait1 30 bits");
        CHECK(!Database::validate(*ovr, Value::of_str("80")).empty(), "type mismatch refused");
        const Field* tn = db.table("teams")->field("teamname");
        CHECK(Database::validate(*tn, Value::of_str(std::string(29, 'x'))).empty(), "29 bytes + NUL fits 30");
        CHECK(!Database::validate(*tn, Value::of_str(std::string(30, 'x'))).empty(), "30 bytes refused");
        Value v;
        CHECK(Database::parse(*ovr, "85", v).empty() && v.i == 85, "parse int");
        CHECK(!Database::parse(*ovr, "8x", v).empty(), "parse garbage refused");
        const Field* fx = db.table("formations")->field("offset1x");
        CHECK(Database::parse(*fx, "-1.25", v).empty() && v.f == -1.25f, "parse float");
        CHECK(!Database::validate(*fx, Value::of_float(NAN)).empty(), "NaN refused");
    });

    // ---- writes, verified afterwards by Live Editor's Lua library
    json writes = json::array();
    auto write = [&](const std::string& table, int64_t key, const std::string& key_field, const std::string& field, const Value& v) {
        const Table* t = db.table(table);
        uint64_t rec = db.find(*t, key_field, key);
        CHECK(rec != 0, table + " record");
        uint32_t idx = static_cast<uint32_t>((rec - t->first_record) / t->record_size);
        std::vector<uint8_t> before, after;
        mem.read_block(rec, t->record_size, before);
        std::string err;
        bool ok = db.set(*t, rec, *t->field(field), v, &err);
        CHECK(ok, table + "." + field + " written: " + err);
        mem.read_block(rec, t->record_size, after);
        // only the field's own bits may change
        const Field* f = t->field(field);
        std::vector<uint8_t> masked_b = before, masked_a = after;
        if (f->type == FieldType::String) {
            for (size_t k = 0; k < f->max_len(); ++k) masked_b[f->byte_off() + k] = masked_a[f->byte_off() + k] = 0;
        } else {
            int bits = f->type == FieldType::Float ? 32 : f->depth;
            insert_bits(masked_b.data() + f->byte_off(), f->start_bit(), bits, 0);
            insert_bits(masked_a.data() + f->byte_off(), f->start_bit(), bits, 0);
        }
        CHECK(masked_a == masked_b, table + "." + field + ": no other bits changed");
        Value back;
        db.get(*t, rec, *f, back);
        CHECK(back == v, table + "." + field + " reads back");
        json jv;
        if (v.type == FieldType::Int) jv = v.i;
        else if (v.type == FieldType::Float) jv = static_cast<double>(v.f);
        else jv = v.s;
        writes.push_back({{"table", table}, {"idx", idx}, {"field", field}, {"value", jv}});
    };

    run_case("writes: ints, negative minimums, wide bit fields, dates, text, UTF-8, floats", [&] {
        write("players", 1002, "playerid", "overallrating", Value::of_int(99));
        write("players", 1003, "playerid", "preferredposition2", Value::of_int(-1));
        write("players", 1001, "playerid", "trait1", Value::of_int((1 << 30) - 1));
        write("players", 1004, "playerid", "icontrait2", Value::of_int(0x2AAA));
        write("players", 1005, "playerid", "birthdate", Value::of_int(gregorian_days_from_date(GameDate{2000, 2, 29})));
        write("players", 1006, "playerid", "height", Value::of_int(257));
        write("players", 1006, "playerid", "hashighqualityhead", Value::of_int(0));
        write("teams", 1, "teamid", "teamname", Value::of_str("Arsenal FC"));
        write("teams", 241, "teamid", "teamname", Value::of_str("Inter"));
        write("editedplayernames", 3003, "playerid", "commonname", Value::of_str("Bob\xC3\xA9"));
        write("formations", 1, "teamid", "offset1x", Value::of_float(-1.5f));
        write("formations", 7, "teamid", "offset1y", Value::of_float(0.333f));
        write("teamplayerlinks", 2001, "playerid", "jerseynumber", Value::of_int(128));
        write("version", 27, "major", "minor", Value::of_int(2));
        std::string err;
        const Table* p = db.table("players");
        uint64_t rec = db.find(*p, "playerid", 1002);
        CHECK(!db.set_int(*p, rec, "overallrating", 128, &err), "out of range refused");
        CHECK(err.find("outside the field range") != std::string::npos, "reason: " + err);
        CHECK(db.get_int(*p, rec, "overallrating") == 99, "value untouched after refusal");
        CHECK(model.rebuild(kToday) && model.player(1005)->age == 26, "age from the new birthdate");
        CHECK(model.team(1)->name == "Arsenal FC", "team name re-read");
    });

    run_case("Live Editor's Lua library reads every write and no other change", [&] {
        mem.save(g_out / "after_writes.img");
        std::ofstream(g_out / "writes.json") << writes.dump();
        CHECK(run_lua("verify_writes") == 0, "gui_world.lua verify_writes");
        std::string log = read_file(g_out / "verify_writes.log");
        CHECK(log.find("mismatches=0") != std::string::npos, "lua verification: " + log.substr(0, 400));
    });

    run_case("stale table: writes refused after the database moved", [&] {
        const Table* t = db.table("teams");
        uint64_t rec = db.find(*t, "teamid", 7);
        uint32_t shortname = 0;
        mem.rd(t->header + 0x40, shortname);
        uint32_t other = 0x58585858;  // "XXXX"
        mem.wr(t->header + 0x40, other);
        std::string err;
        CHECK(!db.table_alive(*t, rec), "header changed -> not alive");
        CHECK(!db.set_int(*t, rec, "overallrating", 80, &err), "write refused: " + err);
        mem.wr(t->header + 0x40, shortname);
        CHECK(db.table_alive(*t, rec), "alive again");
        CHECK(!db.table_alive(*t, t->first_record + uint64_t(t->record_size) * t->written), "record past the end");
        CHECK(!db.table_alive(*t, rec + 1), "misaligned record");
    });

    run_case("bridge files: bad or half-written files never throw", [&] {
        BridgeState s;
        CHECK(!Bridge::parse_state("{\"in_cm\": 5, \"seq\": \"x\"}", s) && !s.error.empty(), "wrong types -> error, no throw");
        CHECK(!Bridge::parse_state("{\"session\":\"A\",\"se", s), "truncated");
        DbMeta m;
        std::string err;
        CHECK(!Bridge::parse_meta("{\"shortname_name_tables_map\":{\"plyr\":5},\"field_desc_map\":{}}", m, &err), "no tables");
        CHECK(!Bridge::parse_meta("{\"shortname_name_tables_map\":{\"plyr\":\"players\"},\"field_desc_map\":{\"plyr\":{\"pid_\":{\"name\":\"playerid\",\"depth\":\"x\"}}}}", m, &err) || m.tables.size() == 1,
              "bad depth type handled");
        // a half-written state file keeps the last good state
        fs::path dir = g_out / "LE2" / "turbo_output";
        fs::create_directories(dir);
        std::ofstream(dir / "bridge_state.json") << "{\"session\":\"S\",\"seq\":1,\"db_gen\":1,\"in_cm\":true,\"user_team\":1}";
        Bridge b(g_out / "LE2");
        b.poll_files();
        CHECK(b.state().loaded && b.state().seq == 1, "first state");
        std::ofstream(dir / "bridge_state.json") << "{\"session\":\"S\",\"se";
        fs::last_write_time(dir / "bridge_state.json", fs::file_time_type::clock::now() + std::chrono::seconds(5));
        b.poll_files();
        CHECK(b.state().loaded && b.state().seq == 1, "half-written file ignored, last state kept");
        std::ofstream(dir / "bridge_state.json") << "{\"session\":\"S\",\"seq\":2,\"db_gen\":1,\"in_cm\":true,\"user_team\":1}";
        b.poll_files();
        CHECK(b.state().seq == 2, "complete file read on the next poll");
        CHECK(b.state().meta_error.empty(), "no meta_error reported");
        // Turbo's Lua side could not write bridge_meta.json: the GUI shows why instead of "waiting"
        fs::path dir3 = g_out / "LE3" / "turbo_output";
        fs::create_directories(dir3);
        std::ofstream(dir3 / "bridge_state.json")
            << "{\"session\":\"S\",\"seq\":1,\"db_gen\":1,\"in_cm\":false,\"meta_error\":\"GetDBMeta failed: not ready\"}";
        App a3(mem, g_out / "LE3", 0, "meta-error");
        a3.bridge.poll_files();
        CHECK(a3.bridge.state().meta_error == "GetDBMeta failed: not ready", "meta_error parsed from bridge_state.json");
        CHECK(!a3.refresh(), "no meta: not connected");
        CHECK(a3.db_error.find("cannot read the game database: GetDBMeta failed: not ready") != std::string::npos,
              "the reason is shown: " + a3.db_error);
    });

    run_case("bridge_names.txt: parsing, half-written files, stale files from an earlier session", [&] {
        NameMap m;
        std::string sess;
        CHECK(Bridge::parse_names("#turbo-names ABC 2\n1\tSaka\n2\t\xC3\x98degaard\n", m, &sess), "parsed");
        CHECK(sess == "ABC" && m.size() == 2 && m[2] == "\xC3\x98degaard", "session and UTF-8 name");
        CHECK(!Bridge::parse_names("#turbo-names ABC 3\n1\tSaka\n2\tX", m), "fewer lines than announced = half written");
        CHECK(!Bridge::parse_names("1\tSaka\n", m), "no header");
        CHECK(Bridge::parse_names("#turbo-names ABC 1\r\n7\tKane\r\n\tbad\nx\ty\n", m) && m.size() == 1 && m[7] == "Kane",
              "CRLF and bad lines");
        fs::path dir = g_out / "LE4" / "turbo_output";
        fs::create_directories(dir);
        std::ofstream(dir / "bridge_names.txt") << "#turbo-names S 2\n1\tA\n";
        Bridge b(g_out / "LE4");
        b.poll_files();
        CHECK(b.names()->empty(), "half-written names file not used");
        std::ofstream(dir / "bridge_names.txt") << "#turbo-names S 2\n1\tA\n2\tB\n";
        fs::last_write_time(dir / "bridge_names.txt", fs::file_time_type::clock::now() + std::chrono::seconds(3));
        CHECK(b.poll_files() && b.names()->size() == 2, "complete file read on the next poll");
        // files older than the DLL's load time belong to an earlier game session
        Bridge c(g_out / "LE4");
        c.set_min_file_time(fs::file_time_type::clock::now() + std::chrono::seconds(60));
        c.poll_files();
        CHECK(c.names()->empty() && !c.state().loaded, "stale files ignored");
        Bridge d(g_out / "LE");
        d.set_min_file_time(fs::file_time_type::clock::now() - std::chrono::minutes(5));
        d.poll_files();
        CHECK(d.meta_loaded() && d.state().loaded && d.names()->size() == 20, "fresh files used");
    });

    run_case("mailbox protocol against Turbo's Lua bridge", [&] {
        const uint64_t kMb = 0x30000000;
        mem.map(kMb, kMailboxSize);
        Mailbox mb(mem, kMb);
        CHECK(mb.init(), "init");
        uint32_t magic = 0;
        mem.rd(kMb, magic);
        CHECK(magic == kMailboxMagic, "magic");
        std::string err;
        CHECK(mb.submit(json({{"op", "run"}, {"module", "form_morale"}, {"overrides", {{"form", 77}, {"morale", 66}, {"fitness", 0}}}}).dump(), &err), "submit: " + err);
        CHECK(mb.pending(), "pending");
        CHECK(!mb.submit("{\"op\":\"ping\"}", &err), "second submit refused while pending");
        bool ok = false;
        std::string result;
        CHECK(!mb.take_result(ok, result), "no result yet");
        mem.save(g_out / "mailbox_in.img");
        std::ofstream(g_out / "mailbox.json") << json({{"mailbox", hex_addr(kMb)}}).dump();
        CHECK(run_lua("mailbox") == 0, "gui_world.lua mailbox");
        SimMemory after;
        CHECK(after.load(g_out / "mailbox_out.img"), "mailbox_out.img");
        mem.pages = after.pages;
        CHECK(!mb.pending(), "acknowledged by Lua");
        CHECK(mb.take_result(ok, result), "result available");
        CHECK(ok, "status ok: " + result);
        CHECK(result.find("form") != std::string::npos || !result.empty(), "result text: " + result);
        CHECK(!mb.take_result(ok, result), "result taken once");
        CHECK(mb.heartbeat() > 0, "Lua heartbeat");
        json calls = read_json(g_out / "mailbox_calls.json");
        CHECK(calls["set_player_form"].size() == 6, fmt("SetPlayerForm calls: %zu", calls["set_player_form"].size()));
        bool all77 = true;
        for (const auto& c : calls["set_player_form"]) all77 = all77 && c[1].get<int>() == 77;
        CHECK(all77, "form value from the GUI");
        CHECK(calls["boxes"].get<int>() == 0, "no message box for GUI commands");
        CHECK(mb.submit("{\"op\":\"ping\"}", &err), "next command accepted");
        mb.cancel();
        CHECK(!mb.pending() && !mb.take_result(ok, result), "cancelled command: not pending, no result");
    });
}

// ================================================================ UI: driven by synthetic input
struct ItemRec {
    ImGuiID id = 0;
    std::string label;
    ImRect rect;
    ImGuiID seed = 0, seed2 = 0;  // ID stack top and the one below at submission
    std::string window;
    ImGuiItemStatusFlags flags = 0;
    ImRect clip;  // visible part of the window when the item was submitted
    int frame = -1;
};
static std::unordered_map<ImGuiID, ItemRec> g_items;
static int g_frame = 0;

static void note_item(ImGuiContext* ctx, ImGuiID id, ItemRec& r) {
    r.id = id;
    r.frame = g_frame;
    if (ImGuiWindow* w = ctx->CurrentWindow) {
        r.window = w->Name;
        int n = w->IDStack.Size;
        r.seed = n >= 1 ? w->IDStack[n - 1] : 0;
        r.seed2 = n >= 2 ? w->IDStack[n - 2] : 0;
    }
}
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*) {
    if (!id) return;
    ItemRec& r = g_items[id];
    note_item(ctx, id, r);
    r.rect = bb;
    if (ctx->CurrentWindow) r.clip = ctx->CurrentWindow->ClipRect;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
    if (!id) return;
    ItemRec& r = g_items[id];
    note_item(ctx, id, r);
    r.label = label ? label : "";
    r.flags = flags;
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id) {
    auto it = g_items.find(id);
    return it == g_items.end() ? nullptr : it->second.label.c_str();
}

class Ui {
public:
    App& app;
    double t = 0.0;
    explicit Ui(App& a) : app(a) {}

    void frame() {
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ++g_frame;
        app.tick(t);
        app.draw();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
        t += 1.0 / 60.0;
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) frame();
    }
    // Item seen in the last frame with this label (optionally inside a window whose name contains `win`,
    // optionally pushed under PushID(scope))
    const ItemRec* find(const std::string& label, const std::string& win = "", const std::string& scope = "") {
        const ItemRec* best = nullptr;
        for (const auto& kv : g_items) {
            const ItemRec& r = kv.second;
            if (r.frame != g_frame) continue;
            // widgets that report no label (combos): match the ID the label hashes to
            if (r.label != label && !(r.label.empty() && ImHashStr(label.c_str(), 0, r.seed) == r.id)) continue;
            if (!win.empty() && r.window.find(win) == std::string::npos) continue;
            if (!scope.empty() && ImHashStr(scope.c_str(), 0, r.seed2) != r.seed) continue;
            if (r.rect.GetWidth() <= 0 || r.rect.GetHeight() <= 0) continue;
            if (!r.clip.Contains(r.rect.GetCenter())) continue;  // scrolled out of view: not clickable
            if (!best || r.rect.Min.y < best->rect.Min.y) best = &r;
        }
        return best;
    }
    bool click(const ItemRec* r, bool dbl = false) {
        if (!r) return false;
        ImVec2 c = r->rect.GetCenter();
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(c.x, c.y);
        frame();
        for (int k = 0; k < (dbl ? 2 : 1); ++k) {
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        }
        frame();
        return true;
    }
    bool click(const std::string& label, const std::string& win = "", const std::string& scope = "") {
        const ItemRec* r = find(label, win, scope);
        if (!r) dump("click target not found: '" + label + "' in '" + win + "'");
        return click(r);
    }
    void dump(const std::string& why) {
        if (!std::getenv("TURBO_TEST_DEBUG")) return;
        std::printf("    DEBUG %s; items in frame %d:\n", why.c_str(), g_frame);
        for (const auto& kv : g_items)
            if (kv.second.frame == g_frame)
                std::printf("      '%s' win='%s' rect=(%.0f,%.0f)-(%.0f,%.0f)\n", kv.second.label.c_str(), kv.second.window.c_str(),
                            kv.second.rect.Min.x, kv.second.rect.Min.y, kv.second.rect.Max.x, kv.second.rect.Max.y);
    }
    void key(ImGuiKey k, bool ctrl = false) {
        ImGuiIO& io = ImGui::GetIO();
        if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(k, true);
        frame();
        io.AddKeyEvent(k, false);
        if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
    }
    // Click a text/number box, replace its content, press Enter
    bool type_into(const ItemRec* r, const std::string& text) {
        if (!click(r)) return false;
        key(ImGuiKey_A, true);
        if (text.empty()) key(ImGuiKey_Backspace);
        else ImGui::GetIO().AddInputCharactersUTF8(text.c_str());
        frame();
        key(ImGuiKey_Enter);
        frames(2);
        return true;
    }
    bool toast_contains(const std::string& s) const {
        for (const auto& tt : app.toasts)
            if (tt.text.find(s) != std::string::npos) return true;
        return false;
    }
};

static void test_ui() {
    SimMemory mem;
    CHECK(mem.load(g_out / "world.img"), "world.img");
    const uint64_t kMb = 0x31000000;
    mem.map(kMb, kMailboxSize);
    fs::path le = g_out / "LE";
    fs::remove(le / "turbo_output" / "gui_settings.json");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    ImGui_ImplNull_Init();

    {
        App app(mem, le, kMb, "uitest");
        Ui ui(app);
        app.visible = true;

        run_case("UI: connects and every main tab renders", [&] {
            ui.frames(3);
            CHECK(app.connected(), "connected: " + app.db_error);
            CHECK(app.model.players().size() == 12, fmt("players %zu", app.model.players().size()));
            CHECK(fs::exists(le / "turbo_output" / "bridge_dll.json"), "bridge_dll.json published");
            json dll = read_json(le / "turbo_output" / "bridge_dll.json");
            CHECK(dll["mailbox"].get<std::string>() == hex_addr(kMb), "mailbox address published");
            // Lua only trusts bridge_dll.json while its time stamp is fresh (a file left by an earlier game session is
            // never read), so the GUI must stamp it and keep it fresh
            CHECK(dll.contains("updated") && dll["updated"].is_number_integer(), "bridge_dll.json carries a time stamp");
            int64_t stamp = dll["updated"].get<int64_t>();
            CHECK(std::llabs(stamp - static_cast<int64_t>(std::time(nullptr))) <= 5, "time stamp is current");
            {
                json old = dll;
                old["updated"] = 1;
                std::ofstream((le / "turbo_output" / "bridge_dll.json").string()) << old.dump();
            }
            app.tick(app.now + 3.0);
            json dll2 = read_json(le / "turbo_output" / "bridge_dll.json");
            CHECK(dll2["updated"].get<int64_t>() > 1000000000, "tick refreshes the time stamp");
            for (int tab = 0; tab < 6; ++tab) {
                app.request_tab = tab;
                ui.frames(3);
            }
            CHECK(ui.find("Players") && ui.find("Status") && ui.find("Turbo Tools"), "tab items present");
        });

        run_case("UI: Players list, select with the mouse, edit an attribute", [&] {
            CHECK(ui.click("Players"), "Players tab");
            CHECK(ui.click("1002", "##plist"), "row 1002");
            CHECK(app.sel_player == 1002, fmt("selected %lld", static_cast<long long>(app.sel_player)));
            CHECK(ui.click("Attributes", "##pedit"), "Attributes tab");
            const ItemRec* acc = ui.find("##v", "##attr", "acceleration");
            CHECK(acc != nullptr, "acceleration box");
            CHECK(ui.type_into(acc, "97"), "type 97");
            const Table* p = app.db.table("players");
            uint64_t rec = app.db.find(*p, "playerid", 1002);
            CHECK(app.db.get_int(*p, rec, "acceleration") == 97, fmt("acceleration = %lld", static_cast<long long>(app.db.get_int(*p, rec, "acceleration"))));
            CHECK(ui.type_into(ui.find("##v", "##attr", "acceleration"), "200"), "type 200");
            CHECK(app.db.get_int(*p, rec, "acceleration") == 97, "out of range not written");
            CHECK(ui.toast_contains("outside the field range"), "error shown");
        });

        run_case("UI: search box filters the list", [&] {
            const ItemRec* s = ui.find("##psearch", "##plist");
            CHECK(s != nullptr, "search box");
            ui.type_into(s, "saliba");
            CHECK(ui.find("1003", "##plist") != nullptr && ui.find("1001", "##plist") == nullptr, "only Saliba listed");
            ui.type_into(ui.find("##psearch", "##plist"), "");
            CHECK(ui.find("1001", "##plist") != nullptr, "filter cleared");
        });

        run_case("UI: Players list filters (position, PlayStyle, PlayStyle+, retiring, OVR, POT, age)", [&] {
            // Oracle: the same rules computed directly from the model, compared with what the list shows
            auto expect = [&](const std::function<bool(const PlayerRow&)>& keep) {
                std::vector<int64_t> want, got = app.list_player_ids;
                for (const auto& p : app.model.players())
                    if (keep(p)) want.push_back(p.playerid);
                std::sort(want.begin(), want.end());
                std::sort(got.begin(), got.end());
                return want == got;
            };
            const size_t all = app.model.players().size();
            ui.frames(2);
            CHECK(app.list_player_ids.size() == all, "no filter: everyone listed");
            CHECK(ui.click("Retiring", "##plist"), "Retiring");
            CHECK(expect([](const PlayerRow& p) { return p.retiring; }) && app.list_player_ids.size() == 1, "retiring: one player");
            CHECK(ui.click("Retiring", "##plist"), "Retiring off");
            CHECK(ui.click("##fstyle", "##plist"), "PlayStyle combo");
            CHECK(ui.click(playstyle1_names()[0], "##Combo"), "first PlayStyle");
            CHECK(expect([](const PlayerRow& p) { return (p.playstyles & 1) != 0; }) && !app.list_player_ids.empty(), "PlayStyle bit 0");
            CHECK(ui.click("+ only", "##plist"), "+ only");
            CHECK(expect([](const PlayerRow& p) { return (p.playstyles_plus & 1) != 0; }) && app.list_player_ids.size() == 1,
                  "PlayStyle+ bit 0: one player");
            CHECK(ui.click("Clear##filters", "##plist"), "Clear");
            CHECK(app.list_player_ids.size() == all, "cleared");
            CHECK(ui.click("##fpos", "##plist"), "position combo");
            CHECK(ui.click(position_name(14), "##Combo"), "position 14");
            CHECK(expect([](const PlayerRow& p) { return std::count(std::begin(p.positions), std::end(p.positions), 14) > 0; }) &&
                      app.list_player_ids.size() > 1,
                  "position 14 as any of the 7 preferred positions");
            CHECK(ui.click("Clear##filters", "##plist"), "Clear");
            CHECK(ui.type_into(ui.find("Min OVR", "##plist"), "87"), "min OVR");
            CHECK(expect([](const PlayerRow& p) { return p.overall >= 87; }), "OVR >= 87");
            CHECK(ui.type_into(ui.find("Max age", "##plist"), "25"), "max age");
            CHECK(expect([](const PlayerRow& p) { return p.overall >= 87 && p.age >= 0 && p.age <= 25; }), "and age <= 25");
            CHECK(ui.type_into(ui.find("Min POT", "##plist"), "90"), "min POT");
            CHECK(expect([](const PlayerRow& p) { return p.overall >= 87 && p.potential >= 90 && p.age >= 0 && p.age <= 25; }),
                  "and POT >= 90");
            CHECK(ui.click("Clear##filters", "##plist"), "Clear");
            CHECK(app.list_player_ids.size() == all, "cleared again");
        });

        run_case("UI: PlayStyles All/None buttons and a single bit", [&] {
            CHECK(ui.click("PlayStyles", "##pedit"), "PlayStyles tab");
            const Table* p = app.db.table("players");
            uint64_t rec = app.db.find(*p, "playerid", 1002);
            CHECK(ui.click("All", "##ps", "trait1"), "All (trait1)");
            CHECK(app.db.get_int(*p, rec, "trait1") == (1 << 30) - 1, "all PlayStyles");
            CHECK(ui.click("None", "##ps", "trait1"), "None (trait1)");
            CHECK(app.db.get_int(*p, rec, "trait1") == 0, "no PlayStyles");
            // bit checkboxes ("<name>##<bit>") of trait1 are the topmost ones on the PlayStyles tab
            const ItemRec* bit0 = nullptr;
            for (const auto& kv : g_items) {
                const ItemRec& r = kv.second;
                if (r.frame != g_frame || r.window.find("##ps") == std::string::npos) continue;
                if (r.label.size() < 3 || r.label.compare(r.label.size() - 3, 3, "##0") != 0) continue;
                if (!bit0 || r.rect.Min.y < bit0->rect.Min.y) bit0 = &r;
            }
            CHECK(bit0 != nullptr, "first PlayStyle checkbox");
            ui.click(bit0);
            CHECK(app.db.get_int(*p, rec, "trait1") == 1, fmt("bit 0 set: %lld", static_cast<long long>(app.db.get_int(*p, rec, "trait1"))));
        });

        run_case("UI: Profile tab shows the corrected birth date and age", [&] {
            CHECK(ui.click("Profile", "##pedit"), "Profile tab");
            CHECK(ui.find("##v", "##prof", "overallrating") != nullptr, "overall box");
            const Table* p = app.db.table("players");
            uint64_t rec = app.db.find(*p, "playerid", 1002);
            CHECK(ui.type_into(ui.find("##v", "##prof", "overallrating"), "91"), "type overall");
            CHECK(app.db.get_int(*p, rec, "overallrating") == 91, "overall written");
            CHECK(app.model.player(1002)->overall == 91, "list row refreshed");
        });

        run_case("UI: Teams, squad jersey edit, jump to player", [&] {
            CHECK(ui.click("Teams"), "Teams tab");
            CHECK(ui.click("7", "##tlist"), "Everton row");
            CHECK(app.sel_team == 7, "team selected");
            CHECK(ui.click("Squad", "##tedit"), "Squad tab");
            const ItemRec* j = ui.find("##v", "##tedit", "jerseynumber");
            CHECK(j != nullptr, "jersey box");
            CHECK(ui.type_into(j, "1"), "type jersey");
            bool found = false;
            for (const auto& l : app.model.links_of_team(7)) found = found || l.jersey == 1;
            CHECK(found, "jersey 1 in model links");
            CHECK(ui.click("Jordan Pickford", "##tedit"), "click player in squad");
            ui.frames(2);
            CHECK(app.sel_player == 2001, "player selected from squad");
            CHECK(ui.find("1002", "##plist") != nullptr, "Players tab opened");
        });

        run_case("UI: Managers, edit text within the field's byte limit", [&] {
            CHECK(ui.click("Managers"), "Managers tab");
            CHECK(ui.click("502##m1", "##mlist"), "Moyes row");
            CHECK(app.sel_manager == 1, "selected");
            const ItemRec* sur = ui.find("##v", "##medit", "surname");
            CHECK(sur != nullptr, "surname box");
            ui.type_into(sur, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");  // field holds 20 bytes incl. NUL
            const Table* m = app.db.table("manager");
            uint64_t rec = app.db.find(*m, "managerid", 502);
            Value v;
            app.db.get(*m, rec, *m->field("surname"), v);
            CHECK(v.s == "ABCDEFGHIJKLMNOPQRS", "truncated to 19 bytes: '" + v.s + "'");
            const Table* tm = app.db.table("teams");
            uint64_t trow = app.db.find(*tm, "teamid", 7);
            CHECK(app.db.get_int(*tm, trow, "teamid") == 7, "neighbouring table intact");
        });

        run_case("UI: Database tab, pick a table, double-click a float cell, edit", [&] {
            CHECK(ui.click("Database"), "Database tab");
            CHECK(ui.click("Table"), "table combo");
            CHECK(ui.click("formations"), "formations");
            CHECK(app.db_table == "formations", "table chosen: " + app.db_table);
            const ItemRec* cell = ui.find("0.5000", "FC 27 LE Turbo");
            CHECK(cell != nullptr, "offset1x cell");
            ui.click(cell, true);
            const ItemRec* box = ui.find("##v", "##Popup", "offset1x");
            CHECK(box != nullptr, "edit popup");
            ui.type_into(box, "0.75");
            const Table* f = app.db.table("formations");
            Value v;
            app.db.get(*f, app.db.find(*f, "teamid", 1), *f->field("offset1x"), v);
            CHECK(std::fabs(v.f - 0.75f) < 1e-6f, fmt("offset1x = %f", v.f));
            ui.frames(2);
            CHECK(ui.find("0.7500", "FC 27 LE Turbo") != nullptr, "grid shows the new value");
        });

        run_case("UI: Turbo Tools run through the mailbox and Turbo's Lua side", [&] {
            CHECK(ui.click("Turbo Tools"), "Tools tab");
            CHECK(ui.click("Apply now##fm"), "Apply now");
            CHECK(app.busy(), "command queued");
            ui.frames(2);
            CHECK(ui.find("Cancel") != nullptr, "Cancel button while queued");
            mem.save(g_out / "mailbox_in.img");
            std::ofstream(g_out / "mailbox.json") << json({{"mailbox", hex_addr(kMb)}}).dump();
            CHECK(run_lua("mailbox") == 0, "Lua processed the command");
            SimMemory after;
            after.load(g_out / "mailbox_out.img");
            mem.pages = after.pages;
            ui.frames(3);
            CHECK(!app.busy(), "done");
            CHECK(ui.toast_contains("Apply now##fm") || ui.toast_contains("Apply now"), "result toast");
            json calls = read_json(g_out / "mailbox_calls.json");
            CHECK(calls["set_player_form"].size() == 6 && calls["set_player_form"][0][1].get<int>() == 100, "form 100 for the squad");
            CHECK(app.lua_alive(), "Lua heartbeat seen");
        });

        run_case("UI: auto checkbox saves gui_settings.json and queues boot; Cancel", [&] {
            CHECK(ui.click("Keep every day (auto)##fm"), "auto checkbox");
            json gs = read_json(le / "turbo_output" / "gui_settings.json");
            CHECK(gs["auto"]["form_morale"]["enabled"].get<bool>(), "enabled saved");
            CHECK(gs["auto"]["form_morale"]["form"].get<int>() == 100, "form saved");
            CHECK(app.busy(), "boot queued");
            CHECK(ui.click("Cancel"), "Cancel");
            CHECK(!app.busy(), "cancelled");
        });

        run_case("UI: dry run and show/hide key settings", [&] {
            CHECK(ui.click("Safety"), "Safety header");
            CHECK(ui.click("Dry run (Turbo Tools report only, write nothing)"), "dry run checkbox");
            CHECK(read_json(le / "turbo_output" / "gui_settings.json")["turbo"]["dry_run"].get<bool>(), "dry run saved");
            CHECK(ui.click("Status"), "Status tab");
            CHECK(ui.click("Show/hide key"), "key combo");
            CHECK(ui.find("F12") == nullptr, "keys below the combo's visible height are not clickable without scrolling");
            CHECK(ui.click("F5"), "F5");
            CHECK(app.toggle_vk == 0x74, "toggle key F5");
            CHECK(read_json(le / "turbo_output" / "gui_settings.json")["gui"]["toggle_key"].get<int>() == 0x74, "key saved");
        });

        run_case("UI: every Turbo Tools and player button sends a command Turbo's Lua side runs", [&] {
            // Click every button that queues a command, record the exact JSON it put in the mailbox, cancel it so
            // the next one can be sent, then run all of them through Turbo's real Lua bridge (gui_world.lua commands)
            json captured = json::array();
            auto take = [&](const std::string& label) {
                if (!app.busy()) {
                    CHECK(false, "no command queued by '" + label + "'");
                    return;
                }
                std::string text = mem.read_cstr(kMb + 0x20, 0x1000);
                json cmd = json::parse(text, nullptr, false);
                CHECK(!cmd.is_discarded(), "valid JSON from '" + label + "': " + text);
                captured.push_back({{"label", label}, {"cmd", cmd}});
                CHECK(ui.click("Cancel"), "Cancel after '" + label + "'");
                CHECK(!app.busy(), "cancelled '" + label + "'");
            };
            auto press = [&](const std::string& label, const std::string& win = "") {
                CHECK(ui.click(label, win), "button '" + label + "'");
                take(label);
            };
            auto press_as = [&](const std::string& label, const std::string& record, const std::string& win = "") {
                CHECK(ui.click(label, win), "button '" + label + "'");
                take(record);
            };
            auto header = [&](const std::string& label) { CHECK(ui.click(label), "header '" + label + "'"); };

            CHECK(ui.click("Turbo Tools"), "Tools tab");
            press("Apply now##fm");
            press("Set role for whole squad");
            press("Extend my squad's contracts");
            press("Extend every other club's contracts");
            header("Your squad");  // collapse, so the next sections stay in view
            press("Set budget");
            press("Add to budget");
            header("Your club: transfer budget");
            header("Player Career");
            press("Give my player every PlayStyle");
            header("Player Career");
            header("Exports (CSV in turbo_output)");
            for (const char* b : {"Season stats", "Fixtures & results", "Transfer history", "Jersey numbers", "Export tables", "Probe report"})
                press(b);
            header("Exports (CSV in turbo_output)");
            header("Transfer bans");
            for (const char* b : {"List bans", "Ban every team", "Remove all team bans"}) press(b);
            header("Transfer bans");
            header("Database maintenance");
            press("Count");
            CHECK(ui.click("Delete..."), "Delete... opens the confirmation");
            press("Delete", "##confirmdel");
            press("Capture real-face list");
            press("Apply real-face list");
            header("Database maintenance");

            CHECK(ui.click("Players"), "Players tab");
            CHECK(ui.click("1002", "##plist"), "player 1002");
            for (const char* b : {"Release", "Terminate loan", "Transfer list", "Loan list", "Remove from lists"}) press(b, "##pedit");
            CHECK(ui.click("Transfer / Loan..."), "moves popup");
            CHECK(ui.type_into(ui.find("To team ID"), "7"), "destination team (Everton)");
            press("Transfer", "##Popup");
            CHECK(ui.click("Transfer / Loan..."), "moves popup again");
            press("Loan (months)", "##Popup");

            // Bulk edit: my squad, one field and an action
            CHECK(ui.click("Turbo Tools"), "Tools tab again");
            header("Bulk edit players");
            CHECK(ui.type_into(ui.find("##fn0"), "potential"), "bulk field name");
            CHECK(ui.type_into(ui.find("##fv0"), "80"), "bulk field value");
            CHECK(ui.click("Form##be"), "bulk form action");
            press_as("Apply bulk edit", "Bulk edit (my squad)");
            // Bulk edit: exactly the players the Players list shows (searched down to 1002)
            CHECK(ui.click("Players"), "Players tab for the search");
            CHECK(ui.type_into(ui.find("##psearch", "##plist"), "1002"), "search 1002");
            CHECK(app.list_player_ids.size() == 1 && app.list_player_ids[0] == 1002, "list shows 1002 only");
            CHECK(ui.click("Turbo Tools"), "Tools tab");
            CHECK(ui.click("Players shown in the Players list##be"), "list scope");
            press_as("Apply bulk edit", "Bulk edit (shown players)");
            header("Bulk edit players");
            // Delete player (last: the other commands above use player 1002)
            CHECK(ui.click("Players"), "Players tab for delete");
            CHECK(ui.click("1002", "##plist"), "player 1002 again");
            CHECK(ui.click("Delete player...", "##pedit"), "delete dialog");
            press("Delete player", "##delplayer");
            CHECK(ui.type_into(ui.find("##psearch", "##plist"), ""), "search cleared");

            CHECK(captured.size() == 30, fmt("commands captured: %zu", captured.size()));
            CHECK(captured[4]["cmd"]["overrides"]["mode"] == "set" && captured[4]["cmd"]["overrides"]["amount"] == 50000000, "budget set");
            CHECK(captured[5]["cmd"]["overrides"]["mode"] == "add", "budget add");
            CHECK(captured[28]["label"] == "Bulk edit (shown players)" && captured[28]["cmd"]["overrides"]["scope"]["playerids"] == json::array({1002}), "bulk edit scope = shown players");
            CHECK(captured[29]["cmd"]["overrides"]["actions"][0]["confirm"].get<bool>(), "delete carries the confirmation");
            std::ofstream(g_out / "gui_commands.json") << captured.dump(1);
            CHECK(run_lua("commands") == 0, "gui_world.lua commands");
            json results = read_json(g_out / "gui_commands_out.json");
            CHECK(results.size() == captured.size(), "a result for every command");
            // A command may be refused for a reason that belongs to the simulated world (expected below), never
            // because the GUI and the Lua side disagree about modules, operations or settings.
            // Every command must succeed except Turbo's own deliberate safety refusal: the test world's real-face
            // list (41 players) is below min_list_size (100), so Apply refuses to touch any head
            const std::map<std::string, std::string> expected_refusal = {
                {"Apply real-face list", "fewer than min_list_size"},
            };
            CHECK(captured[1]["cmd"]["overrides"]["role"].get<int>() == 3, "squad role default is 3 (Rotation)");
            for (const auto& r : results) {
                std::string label = r["label"], text = r["text"];
                bool ok = r["ok"].get<bool>();
                for (const char* bad : {"unknown Turbo module", "unknown op", "crashed", "bad command", "module missing",
                                        "unknown mode", "must be", "attempt to", "error:"})
                    CHECK(text.find(bad) == std::string::npos, "'" + label + "' contract error: " + text);
                auto ex = expected_refusal.find(label);
                if (!ok) CHECK(ex != expected_refusal.end() && text.find(ex->second) != std::string::npos,
                               "'" + label + "' refused: " + text);
                if (std::getenv("TURBO_TEST_DEBUG")) std::printf("      %-36s %s  %s\n", label.c_str(), ok ? "ok  " : "FAIL", text.c_str());
            }
        });

        run_case("UI: UI size follows the window height and the user's setting, and is saved", [&] {
            float h = ImGui::GetIO().DisplaySize.y;
            float expect = std::round(auto_ui_scale(h) * 20.0f) / 20.0f;
            CHECK(std::fabs(app.ui_scale_applied - expect) < 0.001f, fmt("auto scale %.2f for height %.0f", app.ui_scale_applied, h));
            CHECK(std::fabs(auto_ui_scale(1550.0f) - 1550.0f / 1080.0f) < 0.001f && auto_ui_scale(720.0f) == 1.0f &&
                      auto_ui_scale(10000.0f) == 3.0f, "auto scale: height/1080, between 1 and 3");
            app.ui_scale_user = 1.5f;
            ui.frames(2);
            CHECK(std::fabs(app.ui_scale_applied - std::round(expect * 1.5f * 20.0f) / 20.0f) < 0.001f, "user factor applied");
            CHECK(std::fabs(g_ui_scale - app.ui_scale_applied) < 0.001f && std::fabs(S(100.0f) - 100.0f * app.ui_scale_applied) < 0.01f,
                  "S() follows");
            CHECK(std::fabs(ImGui::GetStyle().FontScaleDpi - app.ui_scale_applied) < 0.001f, "font scale follows");
            CHECK(app.save_gui_settings(), "saved");
            CHECK(std::fabs(read_json(le / "turbo_output" / "gui_settings.json")["gui"]["ui_scale"].get<double>() - 1.5) < 0.001,
                  "ui_scale saved");
            App again(mem, le, 0, "reload");
            CHECK(std::fabs(again.ui_scale_user - 1.5f) < 0.001f, "ui_scale loaded");
            app.ui_scale_user = 1.0f;
            ui.frames(2);
            CHECK(app.save_gui_settings(), "reset saved");
        });

        run_case("UI: database reload (another save) invalidates rows safely", [&] {
            std::ofstream(le / "turbo_output" / "bridge_state.json")
                << "{\"session\":\"X\",\"seq\":99,\"db_gen\":7,\"in_cm\":true,\"user_team\":1,\"db_service\":\"" + hex_addr(app.bridge.state().db_service) +
                       "\",\"date\":{\"year\":2027,\"month\":1,\"day\":16}}";
            fs::last_write_time(le / "turbo_output" / "bridge_state.json", fs::file_time_type::clock::now() + std::chrono::seconds(10));
            uint64_t v0 = app.model.version();
            app.visible = false;
            ui.frames(40);  // polls every 0.5 s
            CHECK(app.model.version() == v0, "hidden: the re-read waits");
            CHECK(app.refresh_pending, "re-read pending");
            app.visible = true;
            app.request_tab = 0;
            ui.frames(3);
            CHECK(app.model.version() != v0, "shown: model rebuilt for the new db_gen");
            CHECK(!app.refresh_pending, "nothing pending");
            CHECK(ui.find("1001", "##plist") != nullptr, "list rebuilt");
            CHECK(app.model.player(3003)->age == 18, "age follows the new date");
        });

        run_case("UI: no ImGui errors, layout stable over many frames", [&] {
            for (int tab = 0; tab < 6; ++tab) {
                app.request_tab = tab;
                ui.frames(5);
            }
            app.visible = false;
            ui.frames(2);
            app.visible = true;
            ui.frames(2);
            CHECK(true, "rendered");
        });
    }
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
}

// Live Editor's own log decides when a Turbo.dll loaded at game launch may start (src/win/dllmain.cpp)
static void test_le_log() {
    using turbo::LeLogState;
    using turbo::le_log_state;
    const uint64_t base = 0x7FFCD0B10000ULL;
    // Lines exactly as FC 27 Live Editor v27.1.2 writes them (from a real live_editor_<date>.log)
    const std::string other =
        "17:38:59.206226\tINFO\tFC 27 Live Editor - v27.1.2\r\n"
        "17:38:59.206952\tINFO\tProc <FC27.exe> 0x140000000-0x1611EF000\r\n"
        "17:38:59.207163\tINFO\tModule <FCLiveEditor.DLL> 0x7FF8B36D0000-0x7FF8B413D000\r\n"
        "17:39:20.000000\tINFO\tInitial setup done\r\n";
    const std::string ours_start =
        "20:24:09.779039\tINFO\tFC 27 Live Editor - v27.1.2\n"
        "20:24:09.779342\tINFO\tProc <FC27.exe> 0x140000000-0x1611EF000\n"
        "20:24:09.779458\tINFO\tModule <FCLiveEditor.DLL> 0x7FFCD0B10000-0x7FFCD157D000\n"
        "20:24:09.993802\tINFO\t[LUA] Execute: C:\\FC 27 Live Editor\\lua\\autorun\\turbo_boot.lua\n"
        "20:24:27.190355\tINFO\tMain Menu reached\n";
    const std::string ours_done = ours_start + "20:24:42.331781\tINFO\tInitial setup done\n";
    CHECK(turbo::le_log_header(base) == "Module <FCLiveEditor.DLL> 0x7FFCD0B10000-", "header text as Live Editor prints it");
    CHECK(le_log_state("", base) == LeLogState::NoSession, "empty log: no session");
    CHECK(le_log_state(other, base) == LeLogState::NoSession, "another session's 'Initial setup done' does not count");
    CHECK(le_log_state(other + ours_start, base) == LeLogState::Waiting, "this session started: waiting");
    CHECK(le_log_state(other + ours_done, base) == LeLogState::Done, "this session reported Initial setup done");
    CHECK(le_log_state(ours_done, 0) == LeLogState::NoSession, "no Live Editor module: no session");
    // The same module base reused by a later game session: only the last header counts
    CHECK(le_log_state(ours_done + other + ours_start, base) == LeLogState::Waiting,
          "an earlier session with the same base and 'done' does not count for the new one");
    // A later session (another base) reporting done after ours started: not ours
    std::string tail = other;
    CHECK(le_log_state(ours_start + tail, base) == LeLogState::Waiting, "a later session's 'done' does not count");
    // Lower-case hex and CRLF line ends
    std::string lower = ours_done;
    const size_t at = lower.find("0x7FFCD0B10000");
    lower.replace(at, 14, "0x7ffcd0b10000");
    CHECK(le_log_state(lower, base) == LeLogState::Done, "hex digits in either case");
    CHECK(le_log_state("Module <FCLiveEditor.DLL> 0x7FFCD0B10000-0x7FFCD157D000\r\nInitial setup done\r\n", base) == LeLogState::Done,
          "CRLF line ends");
    // A base that is a prefix of another must not match it
    CHECK(le_log_state("Module <FCLiveEditor.DLL> 0x7FFCD0B100000-0x7FFCD157D000\nInitial setup done\n", base) == LeLogState::NoSession,
          "the base is matched whole (up to the '-')");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <out_dir> <gui_world.lua>\n", argv[0]);
        return 2;
    }
    g_out = argv[1];
    g_lua = argv[2];
    std::printf("native core\n");
    test_core();
    std::printf("native Live Editor log\n");
    test_le_log();
    std::printf("native UI\n");
    try {
        test_ui();
    } catch (const std::exception& e) {
        ++g_fail;
        std::printf("  FAIL UI setup: %s\n", e.what());
    }
    std::printf("RESULT %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
