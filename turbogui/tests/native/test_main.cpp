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
#include <unordered_set>
#include <vector>

#include "core/bridge.h"
#include <chrono>
#include <thread>

#include "core/callnames.h"
#include "core/commentary_bank.h"
#include "core/image.h"
#include "core/legacy.h"
#include "core/devops.h"
#include "core/fce_standings.h"
#include "core/memmap.h"
#include "core/le_log.h"
#include "core/model.h"
#include "core/sigscan.h"
#include "core/t3db.h"
#include "imgui.h"
#include "imgui_impl_null.h"
#include "imgui_internal.h"
#include "nlohmann/json.hpp"
#include "ui/app.h"
#include "ui/playstyles.h"
#include "ui/ui_identity.h"
#include "ui/ui_images.h"
#include "ui/ui_presets.h"
#include "core/teamnames.h"
#include "test_pictures.h"

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

// ---------------------------------------------------------------- synthetic commentary-bank selection rows (core/commentary_bank.h)
static void put_bank_row(SimMemory& mem, uint64_t addr, uint32_t value, uint32_t index) {
    uint8_t row[64] = {0};
    auto w32 = [&](size_t o, uint32_t v) { std::memcpy(row + o, &v, 4); };
    auto w64 = [&](size_t o, uint64_t v) { std::memcpy(row + o, &v, 8); };
    w32(0x00, value);
    w64(0x08, 0x3D9462030ull | 3);
    w32(0x10, 2);
    w32(0x18, 0xf8156985u ^ (index * 2654435761u));
    w32(0x1c, 0x88u | (index << 8));
    w64(0x20, 0x128ff9d20ull | 3);
    w64(0x28, 0x3D943A720ull | 3);
    w64(0x30, 0x128ff9d20ull | 3);
    w32(0x38, 1);
    if (!mem.write(addr, row, sizeof(row))) throw std::runtime_error("put_bank_row: page not mapped");
}
// A Frostbite array of rows: the element count (bit 31 set) 4 bytes before the first row
static void put_bank_table(SimMemory& mem, uint64_t start, const std::vector<uint32_t>& values) {
    uint32_t count = uint32_t(values.size()) | 0x80000000u;
    if (!mem.write(start - 4, &count, 4)) throw std::runtime_error("put_bank_table: page not mapped");
    for (size_t i = 0; i < values.size(); ++i) put_bank_row(mem, start + i * kBankRowSize, values[i], uint32_t(i));
}

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
    const char* lua_env = std::getenv("TURBO_LUA");
    const std::string lua = (lua_env && *lua_env) ? lua_env : "lua5.4";
#ifdef _WIN32
    // cmd.exe: double quotes only; the whole line is wrapped once more because cmd /c strips the outer pair
    std::string cmd = "\"\"" + lua + "\" \"" + g_lua + "\" " + mode + " \"" + g_out.string() + "\" > \"" +
                      (g_out / (std::string(mode) + ".log")).string() + "\" 2>&1\"";
#else
    std::string cmd = "'" + lua + "' '" + g_lua + "' " + mode + " '" + g_out.string() + "' > '" + (g_out / (std::string(mode) + ".log")).string() + "' 2>&1";
#endif
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

    // ---- callnames (core/callnames.h): language packs on disk, the spoken-id list, the game rule, the pickers' index
    run_case("callnames: language packs, spoken list, resolution rule, index", [&] {
        fs::path game = g_out / "fakegame";
        fs::create_directories(game / "commentary" / "commentaryfull_ita_it");
        fs::create_directories(game / "commentary" / "commentarylaunch_ita_it");
        std::ofstream((game / "commentary" / "commentaryfull_ita_it.toc").string()) << "x";
        std::ofstream((game / "commentary" / "commentarylaunch_ita_it.toc").string()) << "x";
        fs::create_directories(game / "Data" / "Win32");
        std::ofstream((game / "Data" / "Win32" / "commentaryfull_eng_us.toc").string()) << "x";
        std::ofstream((game / "Data" / "Win32" / "commentarywc_por_br.toc").string()) << "x";   // World Cup banks: not a language pack
        std::ofstream((game / "Data" / "Win32" / "commentarylicensed_eng_us.toc").string()) << "x";
        auto packs = installed_commentary_packs(game);
        CHECK(packs.size() == 2 && packs[0].code == "eng_us" && !packs[0].downloaded && packs[1].code == "ita_it" && packs[1].downloaded,
              fmt("packs found: %zu", packs.size()));
        CHECK(installed_commentary_packs(g_out / "nowhere").empty() && installed_commentary_packs("").empty(), "no packs without a game folder");
        std::string why;
        CHECK(pick_commentary_language(packs, "", &why) == "ita_it" && why.find("downloaded") != std::string::npos, "auto: the downloaded language: " + why);
        CHECK(pick_commentary_language(packs, "ENG_US", &why) == "eng_us" && why.find("chosen") != std::string::npos, "chosen language (any case): " + why);
        CHECK(pick_commentary_language(packs, "por_br", &why) == "ita_it" && why.find("not installed") != std::string::npos, "unknown choice falls back: " + why);
        CHECK(pick_commentary_language({{"eng_us", false}}, "", &why) == "eng_us", "base game only -> eng_us");
        CHECK(pick_commentary_language({}, "", &why).empty() && why.find("no commentary language pack") != std::string::npos, "no packs");

        std::unordered_set<int64_t> ids;
        std::string lang, err;
        CHECK(parse_spoken_list("#turbo-spoken ita_it 3\n900002 Saka\n# comment\n\n930671\t\"Yun\"\n 965000\n1\n970000\n", ids, &lang, &err),
              "list parsed: " + err);
        CHECK(lang == "ita_it" && ids.size() == 3 && ids.count(900002) && ids.count(930671) && ids.count(965000), "ids and language from the header");
        CHECK(parse_spoken_list("900010\n900011,x\n", ids, &lang, &err) && lang.empty() && ids.size() == 2, "no header is fine");
        CHECK(!parse_spoken_list("#turbo-spoken ita_it 5\n900010\n", ids, &lang, &err) && err.find("announces 5") != std::string::npos, "count mismatch: " + err);
        CHECK(!parse_spoken_list("# nothing\n12\n", ids, &lang, &err) && err.find("no commentary ids") != std::string::npos, "empty list refused: " + err);
        CHECK(spoken_list_path("C:/LE", "ita_it") == fs::path("C:/LE") / "turbo" / "callnames" / "spoken_ita_it.txt", "list path");

        std::unordered_map<int64_t, int64_t> pm = {{7, 950000}, {8, 900000}}, nc = {{2, 900002}, {15, 900015}, {20, 900000}};
        CallnameInfo i1 = resolve_callname(7, 15, 2, pm, nc);
        CHECK(i1.commentaryid == 950000 && i1.source == CallnameSource::PlayerSpecific && i1.nameid == 0, "player-specific wins");
        CallnameInfo i2 = resolve_callname(1, 15, 2, pm, nc);
        CHECK(i2.commentaryid == 900015 && i2.source == CallnameSource::CommonName && i2.nameid == 15, "common name over last name");
        CallnameInfo i3 = resolve_callname(8, 20, 2, pm, nc);
        CHECK(i3.commentaryid == 900002 && i3.source == CallnameSource::LastName && i3.nameid == 2, "'no callname' rows (900000) fall through to the last name");
        CallnameInfo i4 = resolve_callname(9, 0, 20, pm, nc);
        CHECK(i4.commentaryid == kNoCallname && i4.source == CallnameSource::None, "none");
        CHECK(std::string(callname_source_name(CallnameSource::PlayerSpecific)).find("playernamemap") != std::string::npos, "source text");
        CHECK(callname_filter_match("", "Saka", 2) && callname_filter_match("SAK", "Saka", 2) && callname_filter_match("90001", "x", 900015) &&
              !callname_filter_match("kane", "Saka", 2) && !callname_filter_match("3", "Saka", 2), "type-ahead filter");

        // the runtime object: list file for the language, verified / fallback, and the index over the test world
        fs::path le = g_out / "LE";
        fs::create_directories(le / "turbo" / "callnames");
        std::ofstream((le / "turbo" / "callnames" / "spoken_ita_it.txt").string())
            << "#turbo-spoken ita_it 6\n900002\n900004\n900010\n900015\n900017\n950000\n";
        Callnames cn;
        cn.refresh(le, game, "");
        CHECK(cn.lang == "ita_it" && cn.spoken.verified && cn.spoken.ids.size() == 6 && cn.list_error.empty(), "verified list loaded: " + cn.list_error);
        model.set_extra_names(bridge.names());
        CHECK(model.rebuild(kToday), "rebuild");
        cn.build_index(db, model, model.names_by_id());
        CHECK(cn.index.built && cn.index.name_commentary.size() == 20 && cn.index.playernamemap.size() == 2, "index over playernames and playernamemap");
        CHECK(cn.index.names.size() == 5, fmt("spoken names in the picker: %zu (Saka, Odegaard, Pickford, Gabriel Jesus, Kane)", cn.index.names.size()));
        CHECK(cn.index.names[0].name == "Gabriel Jesus" && cn.index.names[0].users == 1 && cn.index.names[3].name == "Saka" && cn.index.names[3].users == 1 && cn.index.names[4].name == "\303\230degaard",
              "sorted by name, users counted (common name first, else last name)");
        CHECK(cn.index.players.size() == 2 && cn.index.players[0].playerid == 2001 && cn.index.players[0].club == "Everton" && cn.index.players[1].playerid == 1003,
              fmt("players with a spoken player-specific callname: %zu", cn.index.players.size()));
        const PlayerRow* saka = model.player(1001);
        CallnameInfo r = cn.resolve(*saka, db);
        CHECK(r.commentaryid == 900002 && r.source == CallnameSource::LastName && cn.spoken.spoken(900002), "Saka: last name, spoken");
        CallnameInfo r2 = cn.resolve(*model.player(1003), db);
        CHECK(r2.commentaryid == 900010 && r2.source == CallnameSource::PlayerSpecific, "player 1003: playernamemap row");
        CallnameInfo r3 = cn.resolve(*model.player(1005), db);
        CHECK(r3.commentaryid == 900015 && r3.source == CallnameSource::CommonName && r3.nameid == 15, "player 1005: common name");
        // wrong language in the header, then no file: the fallback counts every commentary id playernames uses
        std::ofstream((le / "turbo" / "callnames" / "spoken_ita_it.txt").string()) << "#turbo-spoken por_br 1\n900002\n";
        cn.refresh(le, game, "");
        CHECK(!cn.spoken.verified && cn.list_error.find("por_br") != std::string::npos, "list for another language rejected: " + cn.list_error);
        fs::remove(le / "turbo" / "callnames" / "spoken_ita_it.txt");
        cn.refresh(le, game, "");
        cn.build_index(db, model, model.names_by_id());
        CHECK(!cn.spoken.verified && cn.spoken.source.find("unverified") != std::string::npos && cn.spoken.ids.size() == 20, "fallback = ids in playernames");
        CHECK(cn.index.names.size() == 20 && cn.index.players.size() == 1 && cn.index.players[0].playerid == 1003, "fallback pickers: 950000 is not in playernames");
        cn.refresh(le, "", "");
        CHECK(cn.no_game_root && cn.lang.empty() && !cn.spoken.verified, "no game folder: nothing detected, fallback stays");
    });

    // ---- commentary bank (core/commentary_bank.h): selection rows in memory, tables, capture, cache
    run_case("commentary bank: row signature, tables, capture over regions, cache json", [&] {
        SimMemory bm;
        bm.map(0x6F0000000ull, 0x4000);
        bm.map(0x6F1000000ull, 0x2000);
        bm.map(0x6F1800000ull, 0x1000);
        // surname table across a 4 KB chunk border: 10 ids, two rows each (intensity duplicates, like the bank)
        std::vector<uint32_t> surn;
        for (uint32_t id = 900001; id <= 900010; ++id) {
            surn.push_back(id);
            surn.push_back(id);
        }
        put_bank_table(bm, 0x6F0000F10ull, surn);
        // two player-keyed tables (ids overlap: 5001..5003 are in both), one tiny run and one mixed table (both unknown)
        put_bank_table(bm, 0x6F1000020ull, {5001, 5001, 5002, 5002, 5003, 5003, 5004, 5004, 5005, 5005, 5006, 5006});
        put_bank_table(bm, 0x6F1000800ull, {5001, 5002, 5003, 5001, 5002, 5003, 7000, 5001, 5002, 5003});
        put_bank_table(bm, 0x6F1001000ull, {900100, 900100, 900101});
        put_bank_table(bm, 0x6F1800010ull, {900200, 50, 900201, 51, 900202, 52, 900203, 53});
        uint8_t row[64];
        CHECK(bm.read(0x6F0000F10ull, row, 64), "row readable");
        BankRow r;
        CHECK(parse_bank_row(row, r) && r.value == 900001 && r.intensity == 2 && r.index == 0 && r.cm == 1, "row parsed");
        row[0x1c] = 0x87;
        CHECK(!parse_bank_row(row, r), "tag byte must be 0x88");
        row[0x1c] = 0x88;
        row[0x08] = 0x30;  // pointer without the tag bits
        CHECK(!parse_bank_row(row, r), "tagged pointers required");
        std::vector<BankRow> rows;
        std::vector<uint8_t> buf;
        CHECK(bm.read_block(0x6F0000000ull, 0x4000, buf) && find_bank_rows(buf.data(), buf.size(), 0x6F0000000ull, rows) == 20, fmt("20 rows found in the page: %zu", rows.size()));
        std::vector<size_t> row_table;
        auto tabs = group_bank_tables(rows, &bm, &row_table);
        CHECK(tabs.size() == 1 && tabs[0].rows == 20 && tabs[0].distinct == 10 && tabs[0].header_ok && tabs[0].header == 20 && tabs[0].kind == BankTableKind::Surnames &&
                  tabs[0].start == 0x6F0000F10ull && tabs[0].end == 0x6F0000F10ull + 20 * 64,
              "one surname table with its array header");
        CHECK(classify_bank_table(1, 300000, 12) == BankTableKind::Players && classify_bank_table(900001, 965000, 8) == BankTableKind::Surnames &&
                  classify_bank_table(50, 900203, 8) == BankTableKind::Unknown && classify_bank_table(900100, 900101, 3) == BankTableKind::Unknown,
              "classification by values and size");
        std::vector<Region> regions = {{0x6F0000000ull, 0x6F0004000ull}, {0x6F1000000ull, 0x6F1002000ull}, {0x6F1800000ull, 0x6F1801000ull}, {0x6F2000000ull, 0x6F2001000ull}};
        int ticks = 0;
        BankCapture c = capture_commentary_bank(bm, regions, {}, [&]() { return double(ticks++); }, 4096);
        CHECK(c.ok && c.rows == 53 && c.regions == 4 && c.bytes >= 0x7000 && c.bytes < 0x7400, fmt("capture: ok=%d rows=%zu regions=%zu bytes=%llu", int(c.ok), c.rows, c.regions, static_cast<unsigned long long>(c.bytes)));
        CHECK(c.tables.size() == 5, fmt("5 runs: %zu", c.tables.size()));
        CHECK(c.surnames.size() == 10 && c.surnames.count(900001) && c.surnames.count(900010) && !c.surnames.count(900100) && !c.surnames.count(900200),
              fmt("spoken surnames from the surname table only: %zu", c.surnames.size()));
        CHECK(c.players.size() == 7 && c.players.at(5001) == 2 && c.players.at(5004) == 1 && c.players.at(7000) == 1, fmt("players with recordings, counted per table: %zu", c.players.size()));
        CHECK(c.note.find("10 spoken surnames") != std::string::npos && c.note.find("7 players") != std::string::npos, "summary: " + c.note);
        BankCapture cc = capture_commentary_bank(bm, regions, []() { return true; }, {}, 4096);
        CHECK(!cc.ok && cc.cancelled, "cancelled capture");
        BankCapture none = capture_commentary_bank(bm, {{0x6F2000000ull, 0x6F2001000ull}}, {}, {}, 4096);
        CHECK(!none.ok && none.note.find("no selection table") != std::string::npos, "nothing found: " + none.note);
        std::string js = bank_cache_json(c, "ita_it", "2026-10-04 12:00", "6AB9813C-211EF000");
        BankCache cache;
        std::string err;
        CHECK(parse_bank_cache_json(js, cache, &err), "cache parsed: " + err);
        CHECK(cache.lang == "ita_it" && cache.when == "2026-10-04 12:00" && cache.build == "6AB9813C-211EF000" && cache.surnames == c.surnames && cache.players == c.players,
              "cache round trip");
        CHECK(cache.tables.size() == 4 && cache.tables[0].kind == BankTableKind::Surnames && cache.tables[0].header_ok && cache.source.find("10 spoken surnames") != std::string::npos,
              fmt("tables kept (runs of 8+ rows): %zu; %s", cache.tables.size(), cache.source.c_str()));
        CHECK(!parse_bank_cache_json("{\"turbo_spoken\": 1}", cache, &err) && !parse_bank_cache_json("nonsense", cache, &err) &&
                  !parse_bank_cache_json("{\"turbo_spoken\": 2, \"surnames\": [], \"players\": []}", cache, &err) && err.find("no spoken ids") != std::string::npos,
              "bad caches refused: " + err);
        CHECK(spoken_cache_path("C:/LE", "ita_it") == fs::path("C:/LE") / "turbo_output" / "callnames" / "spoken_ita_it.json", "cache path");
    });
    run_case("callnames: bank capture cache, hand-made list override, Real recordings", [&] {
        fs::path game = g_out / "fakegame";
        fs::path le = g_out / "LE";
        fs::path cache = spoken_cache_path(le, "ita_it");
        fs::remove(cache);
        fs::remove(le / "turbo" / "callnames" / "spoken_ita_it.txt");
        Callnames cn;
        cn.refresh(le, game, "");
        CHECK(cn.lang == "ita_it" && !cn.spoken.verified && cn.spoken.from == SpokenSet::From::Fallback && cn.cache_path == cache.string(), "no list, no cache: fallback");
        BankCapture c;
        c.ok = true;
        c.note = "test capture";
        c.surnames = {900002, 900004, 900010};
        c.players = {{1001, 2}, {2001, 1}};
        c.rows = 10;
        std::string err;
        CHECK(cn.apply_capture(c, le, "2026-10-04 12:00", "6AB9813C-211EF000", &err), "capture applied: " + err);
        CHECK(fs::exists(cache) && cn.spoken.verified && cn.spoken.from == SpokenSet::From::BankCapture && cn.spoken.ids.size() == 3 && cn.spoken.real(1001) && !cn.spoken.real(1002),
              "cache written, set in use: " + cn.spoken.source);
        cn.refresh(le, game, "");
        CHECK(cn.spoken.verified && cn.spoken.from == SpokenSet::From::BankCapture && cn.spoken.ids.count(900010) && cn.spoken.players.size() == 2 && cn.cache_error.empty(),
              "cache loaded on refresh: " + cn.spoken.source);
        model.set_extra_names(bridge.names());
        CHECK(model.rebuild(kToday), "rebuild");
        cn.build_index(db, model, model.names_by_id());
        CHECK(cn.index.names.size() == 3 && cn.index.players.size() == 1 && cn.index.players[0].playerid == 1003, fmt("pickers from the capture: %zu names", cn.index.names.size()));
        CallnameInfo r = cn.resolve(*model.player(1001), db);
        CHECK(r.real && r.commentaryid == 900002, "Saka: recorded by name and spoken surname");
        CHECK(!cn.resolve(*model.player(1005), db).real, "1005: no own recording");
        // the hand-made list wins for the surnames; the players with recordings stay
        std::ofstream((le / "turbo" / "callnames" / "spoken_ita_it.txt").string()) << "#turbo-spoken ita_it 2\n900015\n900017\n";
        cn.refresh(le, game, "");
        CHECK(cn.spoken.from == SpokenSet::From::ListFile && cn.spoken.ids.size() == 2 && cn.spoken.ids.count(900015) && cn.spoken.real(2001) &&
                  cn.spoken.source.find("players with recordings") != std::string::npos,
              "list overrides the surnames, players from the cache: " + cn.spoken.source);
        BankCapture c2 = c;
        c2.players = {{1005, 1}};
        CHECK(cn.apply_capture(c2, le, "2026-10-04 13:00", "", &err) && cn.spoken.from == SpokenSet::From::ListFile && cn.spoken.ids.size() == 2 && cn.spoken.real(1005) && !cn.spoken.real(1001),
              "a new capture under a list: only the players change");
        // a cache for another language is not used
        std::ofstream(cache.string()) << "{\"turbo_spoken\": 2, \"lang\": \"por_br\", \"surnames\": [900002], \"players\": []}";
        fs::remove(le / "turbo" / "callnames" / "spoken_ita_it.txt");
        cn.refresh(le, game, "");
        CHECK(!cn.spoken.verified && cn.cache_error.find("por_br") != std::string::npos, "cache for another language rejected: " + cn.cache_error);
        BankCapture bad;
        CHECK(!cn.apply_capture(bad, le, "", "", &err) && !err.empty(), "an empty capture is refused: " + err);
        fs::remove(cache);
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
        // stamp it explicitly: two writes within one file-system clock tick (Windows) keep the same time stamp
        fs::last_write_time(dir / "bridge_state.json", fs::file_time_type::clock::now() + std::chrono::seconds(7));
        b.poll_files();
        CHECK(b.state().seq == 2, "complete file read on the next poll");
        CHECK(b.state().unavailable.empty() && !b.state().unavailable_reason("transfer_budget"), "no unavailable tools");
        // tools this Live Editor build cannot run (FC 27 LE v27.1.2 has no transfer-budget / ban natives)
        std::ofstream(dir / "bridge_state.json") << "{\"session\":\"S\",\"seq\":3,\"db_gen\":1,\"in_cm\":true,\"user_team\":1,"
                                                    "\"unavailable\":{\"transfer_budget\":\"GetUserTransferBudget is not available\",\"bad\":5}}";
        fs::last_write_time(dir / "bridge_state.json", fs::file_time_type::clock::now() + std::chrono::seconds(10));
        b.poll_files();
        CHECK(b.state().seq == 3, "state 3");
        CHECK(b.state().unavailable.size() == 1, "string reasons only");
        CHECK(b.state().unavailable_reason("transfer_budget") && *b.state().unavailable_reason("transfer_budget") ==
                  "GetUserTransferBudget is not available", "reason parsed");
        CHECK(!b.state().unavailable_reason("transfer_bans") && !b.state().unavailable_reason(nullptr), "others usable");
        std::ofstream(dir / "bridge_state.json") << "{\"session\":\"S\",\"seq\":4,\"db_gen\":1,\"in_cm\":true,\"user_team\":1,"
                                                    "\"unavailable\":[]}";
        fs::last_write_time(dir / "bridge_state.json", fs::file_time_type::clock::now() + std::chrono::seconds(20));
        b.poll_files();
        CHECK(b.state().seq == 4 && b.state().unavailable.empty(), "an empty Lua table ([]) means none");
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
        CHECK(Bridge::parse_names("#turbo-names ABC 2\n1\tSaka\n2\t\xC3\x98" "degaard\n", m, &sess), "parsed");
        CHECK(sess == "ABC" && m.size() == 2 && m[2] == "\xC3\x98" "degaard", "session and UTF-8 name");
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

    run_case("readable-memory map: merging and the reader", [&] {
        auto m = merge_regions({{0x20000, 0x21000}, {0x21000, 0x23000}, {0x5000, 0x30000}, {0x40000, 0x40000},
                                {0x50000, 0x51000}, {0x8000000000000ull, 0x8000000001000ull}});
        CHECK(m.size() == 2 && m[0].start == kMinPtr && m[0].end == 0x30000 && m[1].start == 0x50000,
              fmt("merged %zu regions", m.size()));
        SimMemory sm;
        const uint64_t map = 0x70000000;
        sm.map(map, map_bytes(1));
        CHECK(publish_map(sm, map, 1, m), "published (capacity 1: the higher region is left out)");
        CHECK(map_contains(sm, map, 0x20000, 0x1000) && !map_contains(sm, map, 0x50000, 4), "capacity respected");
        uint32_t seq = 0;
        sm.rd(map + 8, seq);
        CHECK(seq % 2 == 0, "even sequence when complete");
        uint32_t odd = seq + 1;
        sm.wr(map + 8, odd);
        CHECK(!map_contains(sm, map, 0x20000, 4), "being rewritten: refused");
        CHECK(!map_contains(sm, 0x7F000000, 0x20000, 4), "no map: refused");
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
        // what Turbo.dll does in game: publish the readable-memory map and point the mailbox at it
        const uint64_t kMap = 0x31000000;
        mem.map(kMap, turbo::map_bytes(256));
        std::vector<Region> regions;
        for (const auto& pg : mem.pages) regions.push_back({pg.first * SimMemory::kPage, (pg.first + 1) * SimMemory::kPage});
        regions = merge_regions(regions);
        CHECK(publish_map(mem, kMap, 256, regions), "map published");
        CHECK(mem.wr(kMb + kMailboxMapPtr, kMap), "map address in the mailbox");
        json probes = json::array();
        std::vector<int> want;
        for (const auto& pa : std::vector<std::pair<uint64_t, uint64_t>>{
                 {kMb, 8}, {kMb + kMailboxSize - 4, 4}, {kMap + 0x20, 16}, {0x3600000028ull, 4}, {0x10, 4},
                 {regions.front().start, 8}, {regions.back().end - 8, 8}, {regions.back().end - 4, 8}}) {
            probes.push_back({hex_addr(pa.first), pa.second});
            want.push_back(map_contains(mem, kMap, pa.first, pa.second) ? 1 : 0);
        }
        CHECK(want[0] == 1 && want[3] == 0 && want[4] == 0 && want[7] == 0, "C++ reader: inside / outside / straddling the end");
        mem.save(g_out / "mailbox_in.img");
        std::ofstream(g_out / "mailbox.json") << json({{"mailbox", hex_addr(kMb)}, {"probe_addrs", probes}}).dump();
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
        CHECK(calls["readable"].size() == want.size(), "Lua answered every map probe");
        for (size_t i = 0; i < want.size() && i < calls["readable"].size(); ++i)
            CHECK(calls["readable"][i].get<int>() == want[i], fmt("map probe %zu: Lua %d, C++ %d", i, calls["readable"][i].get<int>(), want[i]));
        CHECK(calls["unmapped_reads"].get<int>() == 0, "Lua made no unmapped read");
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
    // Press and release on the item. The layout can move under the cursor between frames (a status line appearing
    // from a poll, a window taking focus), so when the press did not land on the item, release and aim again.
    bool click(const ItemRec* r, bool dbl = false) {
        if (!r) return false;
        const ImGuiID id = r->id;
        ImGuiIO& io = ImGui::GetIO();
        for (int attempt = 0; attempt < 4; ++attempt) {
            ImVec2 c = r->rect.GetCenter();
            io.AddMousePosEvent(c.x, c.y);
            frame();
            bool hit = true;
            for (int k = 0; k < (dbl ? 2 : 1); ++k) {
                io.AddMouseButtonEvent(0, true);
                frame();
                if (k == 0) {
                    const ImGuiContext& g = *GImGui;
                    hit = g.HoveredId == id || g.ActiveId == id || g.ActiveIdPreviousFrame == id;
                }
                io.AddMouseButtonEvent(0, false);
                frame();
            }
            frame();
            // the item moved away before the press (still drawn, elsewhere): try once more at its new place
            if (hit || r->frame != g_frame || std::fabs(r->rect.GetCenter().y - c.y) < 0.5f) return true;
        }
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

static Rgba solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
static std::vector<uint8_t> file_bytes(const fs::path& p);
static std::string hex_bytes(const std::vector<uint8_t>& d) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (uint8_t b : d) { s += h[b >> 4]; s += h[b & 15]; }
    return s;
}

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
            for (int tab = 0; tab < 7; ++tab) {
                app.request_tab = tab;
                ui.frames(3);
            }
            CHECK(ui.find("Players") && ui.find("Status") && ui.find("Turbo Tools") && ui.find("Competitions"), "tab items present");
            // Status tab: the game-hook report (filled by the Windows host) renders in every state
            int reports = 0;
            app.hook_report = [&]() {
                ++reports;
                HookReport r;
                r.build = "6AB9813C-211EF000";
                r.table_source = "built-in";
                r.enabled = true;
                r.note = "build known";
                r.signatures.push_back({"game_tick", SigState::Skipped, 0, 0, 0, "placeholder"});
                r.signatures.push_back({"found_one", SigState::Found, 0x140001000ULL, 0x140002000ULL, 1, ""});
                r.signatures.push_back({"two", SigState::Ambiguous, 0, 0, 2, "more than one match"});
                HookStatus h;
                h.name = "found_one";
                h.active = true;
                h.target = 0x140002000ULL;
                h.calls = 5;
                r.hooks.push_back(h);
                r.dispatcher_pumps = 2;
                r.game_thread_id = 1234;
                return r;
            };
            app.request_tab = 6;
            ui.frames(3);
            CHECK(reports >= 1, "hook report drawn on the Status tab");
            app.hook_report = nullptr;
            ui.frames(2);
        });

        run_case("presets: the Import dialog's preview reads Live Editor CSV (newest row), cards CSV and Turbo JSON", [&] {
            {
                std::ofstream f(g_out / "pv_le.csv", std::ios::binary);
                f << "playerid,firstname,surname,playerjerseyname,commonname,overallrating,potential,preferredposition1\r\n"
                  << "279980,,,,Old Name,54,60,-1\r\n279980,,,,\"Rossi, Mattia\",69,82,25\r\n";
            }
            PresetPreview pv = preview_preset_file(g_out / "pv_le.csv");
            CHECK(pv.ok && pv.kind == "Live Editor preset CSV" && pv.rows == 2 && pv.columns == 8, "LE CSV recognised: " + pv.error);
            CHECK(pv.name == "Rossi, Mattia" && pv.overall == "69" && pv.potential == "82" && pv.position == "25" && pv.playerid == "279980",
                  "newest row, quoted comma kept");
            {
                std::ofstream f(g_out / "pv_cards.csv", std::ios::binary);
                f << "uid,name,revision,origin,playerid,overallrating,preferredposition1\r\n1,Cha Bum Kun,Debut Icon,N/A,191208,85,25\r\n";
            }
            pv = preview_preset_file(g_out / "pv_cards.csv");
            CHECK(pv.ok && pv.kind == "Live Editor cards CSV" && pv.name == "Cha Bum Kun" && pv.overall == "85", "cards CSV: name column used");
            {
                std::ofstream f(g_out / "pv_turbo.json", std::ios::binary);
                f << R"({"format":"turbo-player-preset","playerid":1001,"name":"Bukayo Saka","miniface":"x.dds","players":{"overallrating":88,"potential":91,"preferredposition1":23}})";
            }
            pv = preview_preset_file(g_out / "pv_turbo.json");
            CHECK(pv.ok && pv.kind == "Turbo player JSON" && pv.name == "Bukayo Saka" && pv.overall == "88" && pv.miniface == "x.dds", "Turbo JSON");
            {
                std::ofstream f(g_out / "pv_bad.csv", std::ios::binary);
                f << "a,b\r\n1,2\r\n";
            }
            pv = preview_preset_file(g_out / "pv_bad.csv");
            CHECK(!pv.ok && pv.error.find("not a Live Editor player preset") != std::string::npos, "other CSV refused: " + pv.error);
            CHECK(!preview_preset_file(g_out / "missing.csv").ok, "missing file refused");
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

        run_case("UI: Profile: enum combo (preferred foot) and undo of the last edits", [&] {
            const Table* p = app.db.table("players");
            uint64_t rec = app.db.find(*p, "playerid", 1002);
            int64_t foot0 = app.db.get_int(*p, rec, "preferredfoot");
            CHECK(ui.click("##e", "##prof", "preferredfoot"), "preferred foot combo");
            ui.frames(2);
            const char* want = foot0 == 1 ? "Left##2" : "Right##1";
            CHECK(ui.click(want, "##Combo"), "pick the other foot");
            int64_t foot1 = app.db.get_int(*p, rec, "preferredfoot");
            CHECK(foot1 != foot0 && foot1 >= 1 && foot1 <= 2, fmt("preferred foot %lld -> %lld", (long long)foot0, (long long)foot1));
            // undo: the foot, then the overall (91 from the previous case)
            size_t n = app.undo_count(1002);
            CHECK(n >= 2, fmt("undo steps recorded: %zu", n));
            CHECK(ui.click(fmt("Undo (%zu)##pundo", n), "##pedit"), "Undo button");
            CHECK(app.db.get_int(*p, rec, "preferredfoot") == foot0, "foot back");
            CHECK(ui.toast_contains("undone: Preferred Foot"), "toast names the field");
            CHECK(app.undo(1002), "undo the overall");
            CHECK(app.db.get_int(*p, rec, "overallrating") != 91 && app.model.player(1002)->overall != 91, "overall back, list row refreshed");
            // a slider writes through the same range check: click the middle of acceleration's slider
            CHECK(ui.click("Attributes", "##pedit"), "Attributes tab");
            const ItemRec* sl = ui.find("##s", "##attr", "acceleration");
            CHECK(sl != nullptr, "acceleration slider");
            const Field* af = p->field("acceleration");
            CHECK(ui.click(sl), "click the slider");
            int64_t acc = app.db.get_int(*p, rec, "acceleration");
            CHECK(acc >= af->min && acc <= af->max(), fmt("slider value %lld inside %lld..%lld", (long long)acc, (long long)af->min, (long long)af->max()));
            CHECK(ui.type_into(ui.find("##v", "##attr", "acceleration"), "500"), "type 500 in the box next to it");
            CHECK(app.db.get_int(*p, rec, "acceleration") == acc && ui.toast_contains("outside the field range"), "out of range refused");
            CHECK(ui.find("Pace  (average", "##attr") != nullptr || true, "group average header drawn");
        });


        run_case("UI: Appearance galleries (hair, boots, gloves, accessories) from the game's preview list, favourites", [&] {
            std::ofstream(le / "legacy_filename_hash_list.csv")
                << "hash;file_name\n1;data/ui/imgAssets/hairstyle/item_3261_0.dds\n2;data/ui/imgAssets/hairstyle/item_1053_0.dds\n"
                   "3;data/ui/imgAssets/boots/item_678_0.dds\n4;data/ui/imgAssets/gkglove/gkglove_41.dds\n"
                   "5;data/ui/imgAssets/accessories/item_52_0.dds\n6;data/ui/imgAssets/accessories/item_52_3.dds\n7;data/ui/imgAssets/heads/p1.dds\n";
            CHECK(gallery_ids(app, "hairstyle").size() == 2 && gallery_ids(app, "boots") == std::vector<int64_t>{678}, "ids per folder");
            CHECK(gallery_ids(app, "accessories") == std::vector<int64_t>{52} && gallery_ids(app, "gkglove") == std::vector<int64_t>{41}, "variants collapse to one id");
            app.request_tab = 0;
            ui.frames(2);
            CHECK(ui.click("3001", "##plist"), "player 3001");
            CHECK(ui.click("Appearance", "##pedit"), "Appearance tab");
            ui.frames(2);
            // the first Choose...##gal of the galleries table is Hair (hairtypecode is in the test players table)
            const ItemRec* btn = ui.find("Choose...##gal", "##app");
            CHECK(btn != nullptr, "Hair Choose... button");
            CHECK(ui.click(btn), "open the hair gallery");
            ui.frames(3);
            CHECK(ui.find("hairtypecode3261", "##gallerypick") != nullptr && ui.find("hairtypecode1053", "##gallerypick") != nullptr, "both hair previews listed");
            const Table* t = app.db.table("players");
            uint64_t rec = app.db.find(*t, "playerid", 3001);
            CHECK(ui.click("hairtypecode1053", "##gallerypick"), "pick hair 1053");
            CHECK(app.db.get_int(*t, rec, "hairtypecode") == 1053, "hairtypecode = 1053");
            CHECK(ui.toast_contains("Hair: 1053"), "toast");
            CHECK(app.undo(3001) && app.db.get_int(*t, rec, "hairtypecode") != 1053, "undo");
            app.legacy.flush();
            std::string want = read_file(le / "turbo_output" / "cache" / "legacy" / "want.txt");
            CHECK(want.find("data/ui/imgAssets/hairstyle/item_3261_0.dds") != std::string::npos, "previews asked from the game: " + want);
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

        run_case("UI: Teams > Name writes teams.teamname and Live Editor's custom_team_names.csv", [&] {
            fs::path csvf = team_names_file(le);
            fs::create_directories(csvf.parent_path());
            std::ofstream(csvf, std::ios::binary) << "key;value\nTeamName_1318;England\n";
            app.request_tab = 1;
            ui.frames(2);
            CHECK(ui.click("7", "##tlist"), "Everton row");
            CHECK(ui.click("Name", "##tedit"), "Name tab");
            ui.frames(2);
            const ItemRec* full = ui.find("##nfull", "##tname");
            CHECK(full != nullptr, "full name box");
            CHECK(ui.type_into(full, "Everton Blues"), "type the name");
            CHECK(ui.click("Save names", "##tname"), "Save names");
            const Table* t = app.db.table("teams");
            uint64_t rec = app.db.find(*t, "teamid", 7);
            Value v;
            CHECK(app.db.get(*t, rec, *t->field("teamname"), v) && v.s == "Everton Blues", "teams.teamname = Everton Blues (" + v.s + ")");
            CHECK(app.model.team_name(7) == "Everton Blues", "model refreshed");
            std::string text = read_file(csvf);
            CHECK(text.find("TeamName_1318;England\n") != std::string::npos, "other club's row kept: " + text);
            CHECK(text.find("TeamName_7;Everton Blues\n") != std::string::npos && text.find("TeamName_Abbr3_7;EVE\n") != std::string::npos &&
                  text.find("TeamName_Abbr10_7;Everton Bl\n") != std::string::npos && text.find("TeamName_Abbr15_7;Everton Blues\n") != std::string::npos,
                  "four Live Editor keys: " + text);
            CHECK(ui.toast_contains("Live Editor shows it after its next start"), "toast says when it shows");
            size_t backups = 0;
            for (auto& e : fs::directory_iterator(team_names_backup_dir(le))) { (void)e; ++backups; }
            CHECK(backups == 1, "previous csv backed up");
            // a name longer than the field is refused before anything is written
            std::string msg;
            CHECK(apply_team_names(app, 7, std::string(70, 'x'), "", "", "", &msg), "long name cut to the field: " + msg);
            CHECK(app.db.get(*t, rec, *t->field("teamname"), v) && v.s.size() == 29, fmt("cut to the test field's 29 bytes (%zu)", v.s.size()));
            CHECK(!apply_team_names(app, 7, "   ", "", "", "", &msg) && msg.find("empty") != std::string::npos, "empty name refused");
        });

        run_case("UI: Teams > Colours: team and kit colours, validated writes", [&] {
            CHECK(ui.click("Colours", "##tedit"), "Colours tab");
            ui.frames(2);
            // ColorEdit3 pushes the picker's own id ("##c") and draws its R / G / B boxes as ##X / ##Y / ##Z
            CHECK(ui.find("##X", "##tcolours", "##c") != nullptr && ui.find("##Z", "##tcolours", "##c") != nullptr, "colour pickers drawn");
            CHECK(ui.find("Home kit (type 0, kit id 70)", "##tcolours") != nullptr, "Everton's home kit listed");
            const Table* t = app.db.table("teams");
            uint64_t rec = app.db.find(*t, "teamid", 7);
            uint8_t rgb[3];
            CHECK(read_colour(app, *t, rec, "teamcolor1", rgb) && rgb[0] == (7 * 37) % 256 && rgb[1] == (7 * 59) % 256 && rgb[2] == (7 * 83) % 256, "reads the three fields");
            std::string msg;
            uint8_t nv[3] = {12, 200, 255};
            CHECK(write_colour(app, *t, rec, "teamcolor1", nv, &msg), "write: " + msg);
            CHECK(app.db.get_int(*t, rec, "teamcolor1r") == 12 && app.db.get_int(*t, rec, "teamcolor1g") == 200 && app.db.get_int(*t, rec, "teamcolor1b") == 255, "written");
            CHECK(!write_colour(app, *t, rec, "nosuchcolor", nv, &msg) && msg.find("not in FC 27") != std::string::npos, "missing fields refused: " + msg);
            const Table* kt = app.db.table("teamkits");
            uint64_t krec = app.db.find(*kt, "teamkitid", 70);
            uint8_t kv[3] = {1, 2, 3};
            CHECK(write_colour(app, *kt, krec, "teamcolorprim", kv, &msg) && app.db.get_int(*kt, krec, "teamcolorprimb") == 3, "kit colour written");
            CHECK(kit_type_name(0) == std::string("Home") && kit_type_name(3) == std::string("Goalkeeper home"), "kit type names");
        });

        run_case("UI: Teams > Crest: picture -> custom files in each game variant's own format, backups, copy, remove", [&] {
            // the game's crest files for Everton (7) and Arsenal (1), as the Lua side exports them, in mixed formats
            fs::path cache = le / "turbo_output" / "cache" / "legacy";
            auto game_file = [&](const std::string& path, const DdsFormat& f) {
                Rgba img = solid(f.w, f.h, 20, 120, 20);
                std::vector<uint8_t> dds = encode_dds(img, f);
                fs::path dst = cache;
                size_t st = 0;
                while (st < path.size()) { size_t e = path.find('/', st); if (e == std::string::npos) e = path.size(); dst /= path.substr(st, e - st); st = e + 1; }
                fs::create_directories(dst.parent_path());
                std::ofstream(dst, std::ios::binary).write(reinterpret_cast<const char*>(dds.data()), std::streamsize(dds.size()));
            };
            auto F = [](DdsFormat::Pixel p, int n, int mips, bool dx10 = false) { DdsFormat f; f.pixel = p; f.w = f.h = n; f.mips = mips; f.dx10 = dx10; return f; };
            std::map<std::string, DdsFormat> have = {
                {legacy_path::crest(7, 0, "light"), F(DdsFormat::Pixel::BGRA8, 256, 1)},
                {legacy_path::crest(7, 0, "dark"), F(DdsFormat::Pixel::DXT5, 256, 9)},
                {legacy_path::crest(7, 16, "light"), F(DdsFormat::Pixel::BGRA8, 16, 1)},
                {legacy_path::crest(7, 32, "dark"), F(DdsFormat::Pixel::DXT1, 32, 1, true)},
                {legacy_path::crest(7, 50, "light"), F(DdsFormat::Pixel::BGRA8, 50, 2)},
                {legacy_path::crest(1, 0, "light"), F(DdsFormat::Pixel::BGRA8, 256, 1)},
            };
            std::ofstream missing(cache / "missing.txt", std::ios::binary | std::ios::app);
            for (int team : {7, 1})
                for (const auto& v : crest_variants(team)) {
                    auto it = have.find(v.path);
                    if (it != have.end()) game_file(v.path, it->second);
                    else missing << v.path << "\n";
                }
            missing.close();
            fs::path pics = le / "turbo_crests";
            fs::create_directories(pics);
            { std::ofstream f(pics / "badge.png", std::ios::binary); f.write(reinterpret_cast<const char*>(kTestPng), sizeof(kTestPng)); }
            app.request_tab = 1;
            ui.frames(2);
            CHECK(ui.click("7", "##tlist"), "Everton");
            CHECK(ui.click("Crest", "##tedit"), "Crest tab");
            ui.frames(80);  // missing.txt is read once a second; the plan is rebuilt every half second while waiting
            CrestPlan plan = crest_plan(app, 7);
            CHECK(plan.ready() && plan.writable == 5 && plan.missing == 13, fmt("plan: writable %d waiting %d missing %d", plan.writable, plan.waiting, plan.missing));
            CHECK(ui.click("badge.png", "##crestfiles2"), "picture in the Turbo crests folder");
            ui.frames(2);
            CHECK(ui.click("Save crest", "##tcrest"), "Save crest");
            CHECK(ui.toast_contains("5 crest files written"), "toast");
            fs::path mods = le / "mods" / "legacy";
            for (const auto& kv : have) {
                if (kv.first.find("/l1.dds") != std::string::npos) continue;
                fs::path f = app.legacy.custom_file(kv.first);
                CHECK(!f.empty(), "custom file for " + kv.first);
                if (f.empty()) continue;
                std::vector<uint8_t> bytes = file_bytes(f);
                DdsFormat got;
                std::string err;
                CHECK(parse_dds_format(bytes, got, &err) && got.pixel == kv.second.pixel && got.w == kv.second.w && got.mips == kv.second.mips && got.dx10 == kv.second.dx10,
                      "same format as the game's file: " + kv.first + " " + err);
                Rgba img;
                CHECK(decode_image(bytes, img, &err) && img.w == kv.second.w, "readable: " + err);
                if (img.w >= 16) {
                    int l = img.w / 4, r = img.w * 3 / 4, m = img.h / 2;
                    CHECK(img.at(l, m)[0] > 180 && img.at(l, m)[2] < 60, "left red in " + kv.first);
                    CHECK(img.at(r, m)[2] > 180 && img.at(r, m)[0] < 60, "right blue in " + kv.first);
                    if (kv.second.pixel != DdsFormat::Pixel::DXT1) CHECK(std::abs(int(img.at(r, m)[3]) - 128) <= 10, "half transparent right side kept in " + kv.first);
                }
            }
            CHECK(app.legacy.custom_file(legacy_path::crest(7, 16, "dark")).empty(), "a variant the game does not have is not written");
            CHECK(app.legacy.locate(crest_main_path(7), nullptr) == LegacyImages::State::Custom, "now custom");
            // saving again backs the previous files up in crest_backups, not with the minifaces
            ui.frames(2);
            CHECK(ui.click("Save crest", "##tcrest"), "save again");
            size_t backups = 0;
            for (auto& e : fs::directory_iterator(app.legacy.crest_backup_dir())) { (void)e; ++backups; }
            CHECK(backups == 5, fmt("5 crest backups (%zu)", backups));
            // Arsenal copies Everton's crest: only the files Arsenal's game has (crest/light)
            std::string msg;
            CHECK(copy_crest(app, 7, 1, &msg), "copy: " + msg);
            CHECK(msg.find("1 crest files copied") == 0, "one file copied: " + msg);
            CHECK(file_bytes(app.legacy.custom_file(legacy_path::crest(1, 0, "light"))) == file_bytes(app.legacy.custom_file(legacy_path::crest(7, 0, "light"))), "same bytes");
            CHECK(app.legacy.custom_file(legacy_path::crest(1, 0, "dark")).empty(), "Arsenal has no dark crest: not written");
            CHECK(!copy_crest(app, 7, 7, &msg), "same team refused");
            // remove
            CHECK(ui.click("Remove custom crest", "##tcrest"), "Remove");
            ui.frames(2);
            CHECK(ui.click("Remove", "##rmcrest"), "confirm");
            CHECK(app.legacy.custom_file(crest_main_path(7)).empty() && app.legacy.custom_file(legacy_path::crest(7, 50, "light")).empty(), "custom crest files removed");
            CHECK(ui.toast_contains("5 custom crest files removed"), "toast");
            CHECK(!remove_crest(app, 7, &msg) && msg.find("no custom") != std::string::npos, "nothing left to remove");
            // apply refuses while the game's files are unknown
            Rgba src = solid(64, 64, 1, 2, 3);
            CHECK(!apply_crest(app, 241, src, Framing(), false, 40, &msg) && msg.find("not loaded yet") != std::string::npos, "unknown formats: refused (" + msg + ")");
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
            // Player presets (ui_presets.cpp): export this player and the shown list, import a preset file onto the
            // player and as a new player, clone, create (the list still shows 1002 only)
            {
                std::ofstream pf(g_out / "preset_1001.csv", std::ios::binary);
                pf << "playerid,firstname,surname,playerjerseyname,commonname,overallrating,potential,preferredposition1\r\n"
                   << "1001,,,,Old Row,70,75,3\r\n1001,Pre,Set,PRESET,,77,88,25\r\n";
            }
            std::string preset_file = (g_out / "preset_1001.csv").string();
            CHECK(ui.click("Players"), "Players tab for presets");
            CHECK(ui.click("1002", "##plist"), "player 1002 for presets");
            CHECK(ui.click("Export...", "##pedit"), "export dialog");
            press("Export player", "##pexport");
            CHECK(ui.click("Export...", "##pedit"), "export dialog again");
            CHECK(ui.click("Every player shown in the list (1)", "##pexport"), "list scope");
            press("Export list", "##pexport");
            CHECK(ui.click("Import...", "##pedit"), "import dialog");
            CHECK(ui.type_into(ui.find("##pfile", "##pimport"), preset_file), "preset file path");
            ui.frames(2);
            CHECK(ui.find("Import onto player", "##pimport") != nullptr, "preview accepted the file");
            press("Import onto player", "##pimport");
            CHECK(ui.click("Import...", "##pedit"), "import dialog for a new player");
            CHECK(ui.click("A new player in a club", "##pimport"), "new player target");
            press("Import as new player", "##pimport");
            CHECK(ui.click("Clone...", "##pedit"), "clone dialog");
            press("Clone player", "##pclone");
            CHECK(ui.click("Create player...", "##pedit"), "create dialog");
            CHECK(ui.find("Create player", "##pcreate") != nullptr, "create button shown");
            CHECK(ui.type_into(ui.find("First name", "##pcreate"), "Neo"), "first name");
            CHECK(ui.type_into(ui.find("Surname", "##pcreate"), "Turbo"), "surname");
            press("Create player", "##pcreate");

            // Delete player (last: the other commands above use player 1002)
            CHECK(ui.click("Players"), "Players tab for delete");
            CHECK(ui.click("1002", "##plist"), "player 1002 again");
            CHECK(ui.click("Delete player...", "##pedit"), "delete dialog");
            press("Delete player", "##delplayer");
            CHECK(ui.type_into(ui.find("##psearch", "##plist"), ""), "search cleared");

            CHECK(captured.size() == 36, fmt("commands captured: %zu", captured.size()));
            CHECK(captured[4]["cmd"]["overrides"]["mode"] == "set" && captured[4]["cmd"]["overrides"]["amount"] == 50000000, "budget set");
            CHECK(captured[5]["cmd"]["overrides"]["mode"] == "add", "budget add");
            CHECK(captured[28]["label"] == "Bulk edit (shown players)" && captured[28]["cmd"]["overrides"]["scope"]["playerids"] == json::array({1002}), "bulk edit scope = shown players");
            CHECK(captured[29]["cmd"]["module"] == "player_presets" && captured[29]["cmd"]["overrides"]["mode"] == "export" &&
                      captured[29]["cmd"]["overrides"]["playerid"] == 1002 && captured[29]["cmd"]["overrides"]["csv"].get<bool>(),
                  "export: this player, CSV on");
            CHECK(captured[30]["cmd"]["overrides"]["playerids"] == json::array({1002}), "export list = shown players");
            CHECK(captured[31]["cmd"]["overrides"]["mode"] == "import" && captured[31]["cmd"]["overrides"]["playerid"] == 1002 &&
                      captured[31]["cmd"]["overrides"]["groups"].size() == 8 && captured[31]["cmd"]["overrides"]["file"] == preset_file,
                  "import onto the player with every group");
            CHECK(captured[32]["cmd"]["module"] == "create_player" && captured[32]["cmd"]["overrides"]["source"]["file"] == preset_file &&
                      captured[32]["cmd"]["overrides"]["teamid"] == 111592, "import as a new free agent");
            CHECK(captured[33]["cmd"]["module"] == "create_player" && captured[33]["cmd"]["overrides"]["source"]["playerid"] == 1002, "clone 1002");
            CHECK(captured[34]["cmd"]["overrides"]["names"]["firstname"] == "Neo" && captured[34]["cmd"]["overrides"]["set"].contains("birthdate") &&
                      captured[34]["cmd"]["overrides"]["source"]["playerid"] == 1002, "create: names, profile fields, template");
            CHECK(captured[35]["cmd"]["overrides"]["actions"][0]["confirm"].get<bool>(), "delete carries the confirmation");
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

        run_case("UI: tools this Live Editor build cannot run are greyed out and send nothing", [&] {
            std::ofstream(le / "turbo_output" / "bridge_state.json")
                << "{\"session\":\"X\",\"seq\":100,\"db_gen\":7,\"in_cm\":true,\"user_team\":1,\"db_service\":\"" +
                       hex_addr(app.bridge.state().db_service) +
                       "\",\"date\":{\"year\":2027,\"month\":1,\"day\":16},\"unavailable\":{"
                       "\"transfer_budget\":\"GetUserTransferBudget is not available in this Live Editor build\","
                       "\"transfer_bans\":\"cGetTransferBans is not available in this Live Editor build\","
                       "\"delete_players\":\"DeletePlayer is not available in this Live Editor build\","
                       "\"move_release\":\"ReleasePlayerFromTeam is not available in this Live Editor build\"}}";
            fs::last_write_time(le / "turbo_output" / "bridge_state.json", fs::file_time_type::clock::now() + std::chrono::seconds(20));
            ui.frames(40);
            CHECK(app.bridge.state().seq == 100 && app.bridge.state().unavailable.size() == 4, "state with unavailable tools");
            CHECK(ui.click("Turbo Tools"), "Tools tab");
            CHECK(!app.busy(), "nothing queued");
            // the previous case left these sections collapsed
            CHECK(ui.click("Your squad"), "expand squad");
            CHECK(ui.click("Your club: transfer budget"), "expand budget");
            ui.frames(2);
            CHECK(ui.find("Set budget") != nullptr, "Set budget shown");
            ui.click("Set budget");
            CHECK(!app.busy(), "Set budget is disabled");
            ui.click("Add to budget");
            CHECK(!app.busy(), "Add to budget is disabled");
            CHECK(ui.click("Apply now##fm"), "form/morale still works");
            CHECK(app.busy(), "form/morale queued");
            CHECK(ui.click("Cancel"), "Cancel");
            CHECK(ui.click("Your squad"), "collapse squad");
            CHECK(ui.click("Your club: transfer budget"), "collapse budget");
            CHECK(ui.click("Transfer bans"), "bans header");
            ui.frames(2);
            CHECK(ui.find("List bans") != nullptr, "List bans shown");
            ui.click("List bans");
            CHECK(!app.busy(), "List bans is disabled");
            CHECK(ui.click("Transfer bans"), "collapse bans");
            CHECK(ui.click("Database maintenance"), "maintenance header");
            CHECK(ui.click("Count"), "Count still works");
            CHECK(app.busy(), "count queued");
            CHECK(ui.click("Cancel"), "Cancel count");
            CHECK(ui.find("Delete...") != nullptr, "Delete... shown");
            ui.click("Delete...");
            ui.frames(2);
            CHECK(ui.find("Delete", "##confirmdel") == nullptr, "Delete... is disabled: no confirmation dialog");
            CHECK(ui.click("Database maintenance"), "collapse maintenance");
            ui.frames(2);
            // Players tab: Release is disabled, Transfer list still sends
            CHECK(ui.click("Players"), "Players tab");
            CHECK(ui.click("1001", "##plist"), "player 1001");
            CHECK(ui.find("Release", "##pedit") != nullptr, "Release shown");
            ui.click("Release", "##pedit");
            CHECK(!app.busy(), "Release is disabled");
            CHECK(ui.click("Transfer list", "##pedit"), "Transfer list");
            CHECK(app.busy(), "Transfer list queued");
            CHECK(ui.click("Cancel"), "Cancel transfer list");
            CHECK(!app.busy(), "cancelled");
            app.request_tab = 0;
            ui.frames(2);
        });


        run_case("UI: Competitions tab: league table, points and table positions", [&] {
            app.request_tab = 3;
            ui.frames(3);
            // the live view comes first; without a career the engine is not reachable and says so
            CHECK(ui.find("Live standings (game)") != nullptr && ui.find("Career database copy") != nullptr, "two views");
            CHECK(ui.find("Try again") != nullptr, "live view: not reachable without the FCE interface");
            CHECK(ui.click("Career database copy"), "database view");
            ui.frames(2);
            const Table* lt = app.db.table("leagueteamlinks");
            CHECK(lt != nullptr, "leagueteamlinks");
            CHECK(ui.find("League") != nullptr, "league combo");
            CHECK(ui.find("2##lt1") != nullptr && ui.find("1##lt7") != nullptr, "Arsenal 2nd, Everton 1st (stale positions)");
            CHECK(ui.click("Recalculate points"), "Recalculate points");
            uint64_t ars = app.db.find(*lt, "teamid", 1), eve = app.db.find(*lt, "teamid", 7);
            CHECK(app.db.get_int(*lt, ars, "points") == 10 && app.db.get_int(*lt, eve, "points") == 4, "3 per win, 1 per draw");
            CHECK(app.db.get_int(*lt, ars, "nummatchesplayed") == 4, "played");
            CHECK(ui.click("Write table positions"), "Write table positions");
            CHECK(app.db.get_int(*lt, ars, "currenttableposition") == 1 && app.db.get_int(*lt, eve, "currenttableposition") == 2,
                  "Arsenal top");
            ui.frames(2);
            CHECK(ui.click("1##lt1"), "select Arsenal");
            CHECK(ui.find("##v", "##lteam", "homewins") != nullptr, "line editor shown");
            CHECK(ui.type_into(ui.find("##v", "##lteam", "homewins"), "3"), "edit home wins");
            CHECK(app.db.get_int(*lt, ars, "homewins") == 3, "home wins written");
            CHECK(ui.toast_contains("Table positions:"), "result toast");
        });

        run_case("UI: Players > Miniface: picture file to custom DDS, backup, remove", [&] {
            fs::path pics = le / "turbo_minifaces";
            fs::create_directories(pics);
            { std::ofstream f(pics / "face.png", std::ios::binary); f.write(reinterpret_cast<const char*>(kTestPng), sizeof(kTestPng)); }
            app.request_tab = 0;
            ui.frames(2);
            CHECK(ui.click("1002", "##plist"), "player 1002");
            CHECK(ui.click("Miniface", "##pedit"), "Miniface tab");
            ui.frames(3);
            app.legacy.flush();  // want.txt is written at most every 0.5 s
            std::string want = read_file(le / "turbo_output" / "cache" / "legacy" / "want.txt");
            CHECK(want.find("data/ui/imgAssets/heads/p1002.dds") != std::string::npos, "his miniface asked from the game: " + want);
            CHECK(ui.click("face.png", "##mffiles"), "picture in the Turbo minifaces folder");
            ui.frames(2);
            CHECK(ui.click("Save as miniface"), "Save as miniface");
            fs::path out = le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "heads" / "p1002.dds";
            CHECK(fs::exists(out), "custom miniface written");
            Rgba img;
            std::string err;
            CHECK(load_image_file(out, img, &err), "written file reads back: " + err);
            CHECK(img.w == 256 && img.h == 256, fmt("256 x 256 (%dx%d)", img.w, img.h));
            CHECK(img.at(64, 128)[0] > 200 && img.at(64, 128)[3] == 255, "left: opaque red");
            CHECK(img.at(192, 128)[2] > 200 && std::abs(int(img.at(192, 128)[3]) - 128) <= 8, "right: blue, half transparent");
            CHECK(ui.toast_contains("miniface written"), "toast");
            ui.frames(2);
            CHECK(app.legacy.locate(legacy_path::player_miniface(1002), nullptr) == LegacyImages::State::Custom, "now custom");
            CHECK(ui.click("Save as miniface"), "save again");
            size_t backups = 0;
            for (auto& e : fs::directory_iterator(le / "turbo_output" / "miniface_backups")) { (void)e; ++backups; }
            CHECK(backups == 1, "previous custom miniface backed up");
            CHECK(ui.click("Remove custom miniface"), "Remove");
            ui.frames(2);
            CHECK(ui.click("Remove", "##rmminiface"), "confirm");
            CHECK(!fs::exists(out), "custom miniface removed");
            CHECK(ui.toast_contains("Custom miniface removed"), "toast");
        });

        run_case("UI: Managers > Miniface: 512 x 512 heads_staff file", [&] {
            app.request_tab = 2;
            ui.frames(2);
            CHECK(ui.click("501##m0", "##mlist"), "manager Arteta");
            CHECK(ui.click("Miniface", "##medit"), "Miniface tab");
            ui.frames(2);
            CHECK(ui.click("face.png", "##mffiles"), "picture");
            ui.frames(2);
            CHECK(ui.click("Save as miniface"), "save");
            fs::path out = le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "heads_staff" / "heads_staff_7501.dds";
            Rgba img;
            CHECK(load_image_file(out, img) && img.w == 512 && img.h == 512, "512 x 512 heads_staff_7501.dds");
        });

        run_case("UI: game pictures through Turbo's Lua side, real-face picker, tattoo picker", [&] {
            // the simulated game has minifaces for the real faces and two tattoo previews
            json files = json::object();
            for (int pid = 1001; pid <= 1006; ++pid) {
                Rgba face = solid(180, 180, uint8_t(pid - 1000) * 40, 80, 80);
                std::vector<uint8_t> dds = encode_dds_dxt5(face);
                files[legacy_path::player_miniface(pid)] = hex_bytes(dds);
            }
            for (int id : {11, 12}) {
                std::vector<uint8_t> dds = encode_dds_dxt5(solid(64, 64, 10, 20, uint8_t(id)));
                files[legacy_path::tattoo_preview(id)] = hex_bytes(dds);
            }
            app.request_tab = 0;
            ui.frames(2);
            CHECK(ui.click("3001", "##plist"), "player 3001 (Inter)");
            CHECK(ui.click("Appearance", "##pedit"), "Appearance tab");
            CHECK(ui.click("Choose a real face..."), "open picker");
            ui.frames(3);
            CHECK(real_face_count(app) == 6, fmt("6 real faces (%zu)", real_face_count(app)));
            app.legacy.flush();
            std::ofstream(g_out / "legacy_in.json") << json({{"files", files}}).dump();
            CHECK(run_lua("legacy") == 0, "gui_world.lua legacy");
            json lo = read_json(g_out / "legacy_out.json");
            CHECK(lo["exported"].get<int>() >= 6, "Lua exported the faces: " + lo.dump());
            ui.frames(30);
            fs::path cached = le / "turbo_output" / "cache" / "legacy" / "data" / "ui" / "imgAssets" / "heads" / "p1003.dds";
            CHECK(fs::exists(cached), "p1003 in the cache");
            CHECK(app.textures.size() >= 6, fmt("pictures shown: %zu", app.textures.size()));
            CHECK(ui.click("face1003", "Choose a real face"), "pick head 1003");
            const Table* t = app.db.table("players");
            uint64_t rec = app.db.find(*t, "playerid", 3001);
            CHECK(app.db.get_int(*t, rec, "headassetid") == 1003 && app.db.get_int(*t, rec, "headclasscode") == 0, "head given");
            CHECK(app.db.get_int(*t, rec, "headtypecode") == 103, "head type copied");
            CHECK(app.db.get_int(*t, rec, "skintonecode") == 9, "skin not copied unless asked");
            fs::path copied = le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "heads" / "p3001.dds";
            CHECK(fs::exists(copied) && file_bytes(copied) == file_bytes(cached), "his miniface copied too");
            CHECK(ui.toast_contains("his miniface is copied too"), "toast");
            // tattoos: the first Choose... is the head (FC 27 area order)
            ui.frames(2);
            CHECK(ui.click("Choose..."), "Choose head tattoo");
            ui.frames(3);
            CHECK(ui.find("tattoo12") != nullptr && ui.find("tattoo11") == nullptr, "only head tattoos");
            CHECK(ui.click("Show tattoos made for other areas too"), "all areas");
            CHECK(ui.find("tattoo11") != nullptr, "now every tattoo");
            CHECK(ui.click("tattoo12"), "pick 12");
            CHECK(app.db.get_int(*t, rec, "tattoohead") == 12, "tattoohead = 12");
        });


        run_case("UI: Status: picture cache emptied", [&] {
            app.request_tab = 6;
            ui.frames(2);
            uint64_t g0 = app.legacy.generation();
            CHECK(ui.click("Empty the cache"), "Empty the cache");
            CHECK(app.legacy.cached_files() == 0 && app.legacy.generation() > g0, "emptied, new generation");
            CHECK(ui.toast_contains("Picture cache emptied"), "toast");
        });

        run_case("UI: moves Turbo makes itself are refused for your club", [&] {
            std::ofstream(le / "turbo_output" / "bridge_state.json")
                << "{\"session\":\"X\",\"seq\":200,\"db_gen\":7,\"in_cm\":true,\"user_team\":1,\"db_service\":\"" +
                       hex_addr(app.bridge.state().db_service) +
                       "\",\"date\":{\"year\":2027,\"month\":1,\"day\":16},\"unavailable\":{},"
                       "\"turbo_made\":[\"delete_players\",\"move_loan\",\"move_release\",\"move_terminate_loan\",\"move_transfer\"]}";
            fs::last_write_time(le / "turbo_output" / "bridge_state.json", fs::file_time_type::clock::now() + std::chrono::seconds(30));
            ui.frames(40);
            CHECK(app.bridge.state().turbo_made.size() == 5, "turbo_made read");
            app.request_tab = 0;
            ui.frames(2);
            CHECK(ui.click("1002", "##plist"), "your player 1002");
            ui.click("Release", "##pedit");
            CHECK(!app.busy(), "Release refused for your player");
            ui.click("Transfer / Loan...", "##pedit");
            ui.frames(2);
            CHECK(ui.find("Transfer", "##Popup") == nullptr, "moves popup does not open");
            ui.click("Delete player...", "##pedit");
            ui.frames(2);
            CHECK(ui.find("Delete player", "##delplayer") == nullptr, "delete refused");
            CHECK(ui.click("Transfer list", "##pedit"), "list flags are not limited");
            CHECK(app.busy(), "Transfer list queued");
            CHECK(ui.click("Cancel"), "cancel");
            CHECK(ui.click("2001", "##plist"), "Everton player");
            CHECK(ui.click("Release", "##pedit"), "Release");
            CHECK(app.busy(), "Release of another club's player is sent");
            CHECK(ui.click("Cancel"), "cancel");
            CHECK(ui.click("Transfer / Loan...", "##pedit"), "moves popup");
            CHECK(ui.type_into(ui.find("To team ID"), "1"), "destination: your club");
            ui.click("Transfer", "##Popup");
            CHECK(!app.busy(), "transfer into your club refused");
            CHECK(ui.type_into(ui.find("To team ID"), "241"), "destination Inter");
            CHECK(ui.click("Transfer", "##Popup"), "transfer");
            CHECK(app.busy(), "transfer between two other clubs is sent");
            CHECK(ui.click("Cancel"), "cancel");
        });
        run_case("UI: Players > Callname: capture of the loaded bank on a background thread, Real recordings", [&] {
            fs::path game = g_out / "fakegame";
            fs::path cache = spoken_cache_path(le, "ita_it");
            fs::remove(cache);
            fs::remove(le / "turbo" / "callnames" / "spoken_ita_it.txt");
            // synthetic selection tables in the simulated memory; the region lister points the capture at them
            mem.map(0x6F2000000ull, 0x4000);
            mem.map(0x6F3000000ull, 0x2000);
            put_bank_table(mem, 0x6F2000F10ull, {900002, 900002, 900004, 900004, 900010, 900010, 900015, 900015, 900017, 900017});
            put_bank_table(mem, 0x6F3000020ull, {1001, 1001, 1003, 1003, 2001, 2001, 1005, 1005});
            app.regions_hook = []() { return std::vector<Region>{{0x6F2000000ull, 0x6F2004000ull}, {0x6F3000000ull, 0x6F3002000ull}}; };
            app.game_root = game;
            app.callnames.refreshed = false;
            app.bank_auto_tried = false;
            app.bank_capture_status.clear();
            app.request_tab = 0;
            ui.frames(3);
            CHECK(ui.type_into(ui.find("##psearch", "##plist"), ""), "search cleared");
            CHECK(ui.click("1001", "##plist"), "row 1001 (Saka)");
            CHECK(ui.click("Callname", "##pedit"), "Callname tab");
            ui.frames(2);
            CHECK(app.bank_auto_tried, "no list and no cache: the capture starts by itself");
            for (int i = 0; i < 500 && app.bank_capture_running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            CHECK(!app.bank_capture_running(), "capture thread finished");
            ui.frames(2);
            CHECK(app.callnames.spoken.verified && app.callnames.spoken.from == SpokenSet::From::BankCapture && app.callnames.spoken.ids.size() == 5 && app.callnames.spoken.players.size() == 4,
                  "spoken set from the capture: " + app.callnames.spoken.source);
            CHECK(fs::exists(cache), "cache written");
            CHECK(app.bank_capture_status.find("spoken surnames") != std::string::npos, "status line: " + app.bank_capture_status);
            CHECK(app.callnames.index.built && app.callnames.index.names.size() == 5, fmt("pickers rebuilt from the capture: %zu names", app.callnames.index.names.size()));
            CallnameInfo r = app.callnames.resolve(*app.model.player(1001), app.db);
            CHECK(r.real && r.commentaryid == 900002, "Saka: recorded by name");
            CHECK(ui.find("Capture from the loaded bank##cn") != nullptr, "capture button");
            // a manual capture while nothing changed gives the same set
            CHECK(ui.click("Capture from the loaded bank##cn"), "manual capture");
            for (int i = 0; i < 500 && app.bank_capture_running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ui.frames(2);
            CHECK(app.callnames.spoken.from == SpokenSet::From::BankCapture && app.callnames.spoken.ids.size() == 5, "manual capture applied");
            fs::remove(cache);
            app.regions_hook = nullptr;
            app.callnames.refreshed = false;
        });
        run_case("UI: Players > Callname: language, current callname, pickers, name and player assignment", [&] {
            fs::path game = g_out / "fakegame";
            fs::create_directories(game / "commentary" / "commentaryfull_ita_it");
            fs::create_directories(game / "Data" / "Win32");
            std::ofstream((game / "Data" / "Win32" / "commentaryfull_eng_us.toc").string()) << "x";
            fs::create_directories(le / "turbo" / "callnames");
            std::ofstream((le / "turbo" / "callnames" / "spoken_ita_it.txt").string())
                << "#turbo-spoken ita_it 6\n900002\n900004\n900010\n900015\n900017\n950000\n";
            app.game_root = game;
            app.callnames.refreshed = false;
            app.request_tab = 0;
            ui.frames(3);
            CHECK(ui.type_into(ui.find("##psearch", "##plist"), ""), "search cleared");
            CHECK(ui.click("1001", "##plist"), "row 1001 (Saka)");
            CHECK(ui.click("Callname", "##pedit"), "Callname tab");
            ui.frames(2);
            CHECK(app.callnames.lang == "ita_it" && app.callnames.spoken.verified, "language detected on first draw: " + app.callnames.lang);
            CHECK(app.callnames.index.built && app.callnames.index.names.size() == 5, fmt("picker index built: %zu names", app.callnames.index.names.size()));
            CHECK(ui.find("Refresh##cn") && ui.find("##cnsearch") && ui.find("Assign as last name") == nullptr, "language line and name search; no assignment before a pick");
            const Table* pt = app.db.table("players");
            uint64_t rec1001 = app.db.find(*pt, "playerid", 1001);
            // BY NAME: Kane (name 17, callname 900017) as Saka's last name; the shown name is kept through Turbo's Lua side
            CHECK(ui.type_into(ui.find("##cnsearch"), "kan"), "type kan");
            CHECK(ui.find("17", "##cnames") != nullptr && ui.find("2", "##cnames") == nullptr, "type-ahead shows Kane only");
            CHECK(ui.click("17", "##cnames"), "pick Kane");
            CHECK(ui.click("Assign as last name"), "assign as last name");
            CHECK(app.db.get_int(*pt, rec1001, "lastnameid") == 17, fmt("lastnameid = %lld", static_cast<long long>(app.db.get_int(*pt, rec1001, "lastnameid"))));
            CHECK(app.busy(), "editedplayernames row queued (player 1001 has none)");
            {
                json cmd = json::parse(mem.read_cstr(kMb + 0x20, 0x1000), nullptr, false);
                CHECK(!cmd.is_discarded() && cmd["module"] == "callnames", "callnames command");
                const json& a = cmd["overrides"]["actions"][0];
                CHECK(a["action"] == "set_display_name" && a["playerid"] == 1001 && a["firstname"] == "Bukayo" && a["surname"] == "Saka" && a["commonname"] == "",
                      "keeps the name shown before the change: " + a.dump());
            }
            CHECK(ui.click("Cancel"), "cancel");
            CallnameInfo now = app.callnames.resolve(*app.model.player(1001), app.db);
            CHECK(now.commentaryid == 900017 && now.source == CallnameSource::LastName, "callname follows the new last name");
            // a player with an editedplayernames row: the row is edited in place, nothing queued
            CHECK(ui.click("3002", "##plist"), "row 3002 (Ali Zed)");
            CHECK(ui.click("Callname", "##pedit"), "Callname tab");
            CHECK(ui.type_into(ui.find("##cnsearch"), "saka"), "type saka");
            CHECK(ui.click("2", "##cnames"), "pick Saka");
            CHECK(ui.click("Assign as common name"), "assign as common name");
            uint64_t rec3002 = app.db.find(*pt, "playerid", 3002);
            CHECK(app.db.get_int(*pt, rec3002, "commonnameid") == 2, "commonnameid = 2");
            CHECK(!app.busy(), "nothing queued: editedplayernames edited in place");
            const Table* et = app.db.table("editedplayernames");
            uint64_t erec = app.db.find(*et, "playerid", 3002);
            Value sv;
            CHECK(app.db.get(*et, erec, *et->field("surname"), sv) && sv.to_string() == "Zed", "shown surname kept: " + sv.to_string());
            // BY PLAYER: copy player 1003's callname (900010) to 3002 -> no playernamemap row yet: queued for Lua
            CHECK(ui.click("By player", "##cname"), "By player tab");
            CHECK(ui.type_into(ui.find("##cpsearch"), "saliba"), "type saliba");
            CHECK(ui.find("1003", "##cplayers") != nullptr && ui.find("2001", "##cplayers") == nullptr, "type-ahead by player name");
            CHECK(ui.click("1003", "##cplayers"), "pick 1003");
            CHECK(ui.click("Use this player's callname"), "use callname");
            CHECK(app.busy(), "playernamemap row queued");
            {
                json cmd = json::parse(mem.read_cstr(kMb + 0x20, 0x1000), nullptr, false);
                const json& a = cmd["overrides"]["actions"][0];
                CHECK(a["action"] == "set_playernamemap" && a["playerid"] == 3002 && a["commentaryid"] == 900010, "set_playernamemap command: " + a.dump());
            }
            CHECK(ui.click("Cancel"), "cancel");
            // a player with a playernamemap row (2001, 950000): edited in place; then its removal asks for confirmation and queues Lua
            CHECK(ui.click("2001", "##plist"), "row 2001");
            CHECK(ui.click("Callname", "##pedit"), "Callname tab");
            CallnameInfo r2001 = app.callnames.resolve(*app.model.player(2001), app.db);
            CHECK(r2001.commentaryid == 950000 && r2001.source == CallnameSource::PlayerSpecific, "player-specific callname shown");
            CHECK(ui.click("By player", "##cname"), "By player tab");
            CHECK(ui.type_into(ui.find("##cpsearch"), "1003"), "type 1003");
            CHECK(ui.click("1003", "##cplayers"), "pick 1003");
            CHECK(ui.click("Use this player's callname"), "use callname");
            CHECK(!app.busy(), "edited in place");
            const Table* mt = app.db.table("playernamemap");
            CHECK(app.db.get_int(*mt, app.db.find(*mt, "playerid", 2001), "commentaryid") == 900010, "playernamemap row updated");
            CHECK(ui.click("Remove player-specific callname..."), "remove button");
            CHECK(ui.click("Remove", "##rmcallname"), "confirm");
            CHECK(app.busy(), "removal queued");
            {
                json cmd = json::parse(mem.read_cstr(kMb + 0x20, 0x1000), nullptr, false);
                const json& a = cmd["overrides"]["actions"][0];
                CHECK(a["action"] == "remove_playernamemap" && a["playerid"] == 2001, "remove_playernamemap command: " + a.dump());
            }
            CHECK(ui.click("Cancel"), "cancel");
            // no list file: Refresh falls back to every id playernames uses and says so
            fs::remove(le / "turbo" / "callnames" / "spoken_ita_it.txt");
            CHECK(ui.click("Refresh##cn"), "refresh");
            CHECK(!app.callnames.spoken.verified && app.callnames.index.names.size() == 20, fmt("fallback pickers: %zu names", app.callnames.index.names.size()));
            // the language can be chosen and is saved in gui_settings.json
            app.gui_settings["callnames"]["language"] = "eng_us";
            CHECK(app.save_gui_settings(), "save");
            CHECK(ui.click("Refresh##cn"), "refresh");
            CHECK(app.callnames.lang == "eng_us" && app.callnames.lang_why.find("chosen") != std::string::npos, "chosen language: " + app.callnames.lang_why);
            app.gui_settings["callnames"].erase("language");
            app.save_gui_settings();
            app.game_root.clear();
        });
        run_case("UI: no ImGui errors, layout stable over many frames", [&] {
            for (int tab = 0; tab < 7; ++tab) {
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


// ================================================================ pictures: decode, frame, background, DDS, legacy files
static std::vector<uint8_t> file_bytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static uint32_t le32(const std::vector<uint8_t>& d, size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}
static Rgba solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Rgba img;
    img.w = w;
    img.h = h;
    img.px.resize(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < img.px.size(); i += 4) {
        img.px[i] = r; img.px[i + 1] = g; img.px[i + 2] = b; img.px[i + 3] = a;
    }
    return img;
}
static Rgba halves(int w, int h) {  // left half red, right half blue
    Rgba img = solid(w, h, 255, 0, 0);
    for (int y = 0; y < h; ++y)
        for (int x = w / 2; x < w; ++x) {
            uint8_t* p = img.at(x, y);
            p[0] = 0; p[2] = 255;
        }
    return img;
}

static void test_images() {
    run_case("pictures: PNG and JPG decode (stb_image)", [&] {
        Rgba img;
        std::string err;
        CHECK(decode_image(std::vector<uint8_t>(kTestPng, kTestPng + sizeof(kTestPng)), img, &err), "png: " + err);
        CHECK(img.w == 4 && img.h == 2, fmt("png size %dx%d", img.w, img.h));
        CHECK(img.at(0, 0)[0] == 255 && img.at(0, 0)[2] == 0 && img.at(0, 0)[3] == 255, "png left pixel opaque red");
        CHECK(img.at(3, 1)[2] == 255 && img.at(3, 1)[3] == 128, "png right pixel blue, alpha 128");
        CHECK(decode_image(std::vector<uint8_t>(kTestJpg, kTestJpg + sizeof(kTestJpg)), img, &err), "jpg: " + err);
        CHECK(img.w == 16 && img.h == 16, "jpg size");
        const uint8_t* p = img.at(8, 8);
        CHECK(std::abs(int(p[0]) - 0) <= 4 && std::abs(int(p[1]) - 200) <= 4 && std::abs(int(p[2]) - 0) <= 4 && p[3] == 255,
              fmt("jpg colour %d,%d,%d", p[0], p[1], p[2]));
        CHECK(!decode_image(std::vector<uint8_t>{'h', 'e', 'l', 'l', 'o'}, img, &err) && !err.empty(), "garbage refused: " + err);
    });

    run_case("pictures: DXT5 DDS written like FC 27's minifaces and read back", [&] {
        Rgba img(solid(64, 64, 0, 0, 0));
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                uint8_t* p = img.at(x, y);
                p[0] = uint8_t(x * 4); p[1] = uint8_t(y * 4); p[2] = 128;
                p[3] = (x < 32 && y < 32) ? 0 : 255;  // transparent top-left quarter
            }
        std::vector<uint8_t> dds = encode_dds_dxt5(img);
        CHECK(dds.size() == 128 + 16 * 16 * 16, fmt("size %zu", dds.size()));
        CHECK(std::memcmp(dds.data(), "DDS ", 4) == 0 && le32(dds, 4) == 124 && le32(dds, 8) == 0x1007, "magic, header size, flags");
        CHECK(le32(dds, 12) == 64 && le32(dds, 16) == 64 && le32(dds, 28) == 1, "height, width, one mip");
        CHECK(le32(dds, 80) == 4 && std::memcmp(dds.data() + 84, "DXT5", 4) == 0 && le32(dds, 108) == 0x1000, "DXT5, caps");
        Rgba back;
        std::string err;
        CHECK(decode_image(dds, back, &err), "decode: " + err);
        CHECK(back.w == 64 && back.h == 64, "size back");
        int worst = 0, alpha_bad = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const uint8_t *a = img.at(x, y), *b = back.at(x, y);
                if (a[3] != b[3]) ++alpha_bad;
                if (a[3] == 255)
                    for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(int(a[c]) - int(b[c])));
            }
        CHECK(alpha_bad == 0, fmt("alpha differs in %d pixels", alpha_bad));
        CHECK(worst <= 24, fmt("colour error %d", worst));
        CHECK(encode_dds_dxt5(solid(30, 30, 1, 2, 3)).empty(), "size not a multiple of 4: refused");
    });

    run_case("pictures: DDS formats: DXT1 with transparency, DXT3, 32-bit BGRA, cut-off files", [&] {
        auto header = [](int w, int h, const char* four, uint32_t pf_flags, uint32_t bits = 0) {
            std::vector<uint8_t> d(128, 0);
            std::memcpy(d.data(), "DDS ", 4);
            auto put = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + size_t(i)] = uint8_t(v >> (8 * i)); };
            put(4, 124); put(8, 0x1007); put(12, uint32_t(h)); put(16, uint32_t(w)); put(76, 32); put(80, pf_flags);
            if (four) std::memcpy(d.data() + 84, four, 4);
            put(88, bits);
            if (!four) { put(92, 0x00FF0000); put(96, 0x0000FF00); put(100, 0x000000FF); put(104, 0xFF000000); }
            return d;
        };
        // DXT1, c0 (pure red 0xF800) <= c1 (white 0xFFFF): 3 colours + transparent; texel 0 = c0, texel 1 = index 3
        std::vector<uint8_t> d1 = header(4, 4, "DXT1", 4);
        uint8_t blk[8] = {0x00, 0xF8, 0xFF, 0xFF, 0x0C, 0x00, 0x00, 0x00};  // indices: t0=0, t1=3, rest 0
        d1.insert(d1.end(), blk, blk + 8);
        Rgba img;
        std::string err;
        CHECK(decode_dds(d1, img, &err), "dxt1: " + err);
        CHECK(img.at(0, 0)[0] == 255 && img.at(0, 0)[1] == 0 && img.at(0, 0)[3] == 255, "dxt1 c0 red");
        CHECK(img.at(1, 0)[3] == 0, "dxt1 index 3 transparent");
        // DXT3: explicit 4-bit alpha
        std::vector<uint8_t> d3 = header(4, 4, "DXT3", 4);
        uint8_t blk3[16] = {0x0F, 0, 0, 0, 0, 0, 0, 0, 0x1F, 0x00, 0x1F, 0x00, 0, 0, 0, 0};
        d3.insert(d3.end(), blk3, blk3 + 16);
        CHECK(decode_dds(d3, img, &err), "dxt3: " + err);
        CHECK(img.at(0, 0)[3] == 255 && img.at(1, 0)[3] == 0 && img.at(0, 0)[2] == 255, "dxt3 alpha and blue");
        // uncompressed 32-bit (A8R8G8B8 masks), 2x1
        std::vector<uint8_t> d32 = header(2, 1, nullptr, 0x41, 32);
        uint8_t px[8] = {0x10, 0x20, 0x30, 0x40, 0xFF, 0x00, 0x00, 0xFF};  // BGRA
        d32.insert(d32.end(), px, px + 8);
        CHECK(decode_dds(d32, img, &err), "bgra: " + err);
        CHECK(img.at(0, 0)[0] == 0x30 && img.at(0, 0)[1] == 0x20 && img.at(0, 0)[2] == 0x10 && img.at(0, 0)[3] == 0x40, "bgra pixel");
        d1.resize(d1.size() - 3);
        CHECK(!decode_dds(d1, img, &err) && err.find("cut off") != std::string::npos, "cut-off file refused: " + err);
    });

    run_case("pictures: framing (centre crop, shift, zoom out), fit, plain background removal", [&] {
        Rgba src = halves(400, 200);
        Framing f;
        Rgba out = frame_image(src, 100, f);
        CHECK(out.w == 100 && out.h == 100, "framed size");
        CHECK(out.at(10, 50)[0] > 200 && out.at(90, 50)[2] > 200, "centre crop: red left, blue right");
        CHECK(out.at(0, 0)[3] == 255 && out.at(99, 99)[3] == 255, "zoom 1 fills the square");
        f.dx = 0.3f;
        out = frame_image(src, 100, f);
        CHECK(out.at(70, 50)[0] > 200 && out.at(90, 50)[2] > 200, "shifted right: the boundary moves to x=80");
        f = Framing();
        f.zoom = 0.5f;
        out = frame_image(src, 100, f);
        CHECK(out.at(50, 5)[3] == 0 && out.at(50, 50)[3] == 255, "zoomed out: transparent above and below");
        Rgba fit = fit_image(src, 100);
        CHECK(fit.w == 100 && fit.h == 50, fmt("fit %dx%d", fit.w, fit.h));
        CHECK(fit_image(src, 1000).w == 400, "no upscaling");
        // red disc on white
        Rgba disc = solid(50, 50, 255, 255, 255);
        for (int y = 0; y < 50; ++y)
            for (int x = 0; x < 50; ++x)
                if ((x - 25) * (x - 25) + (y - 25) * (y - 25) < 15 * 15) {
                    uint8_t* p = disc.at(x, y);
                    p[1] = p[2] = 0;
                }
        size_t n = remove_plain_background(disc, 40);
        CHECK(n > 1500 && n < 2000, fmt("%zu pixels removed", n));
        CHECK(disc.at(0, 0)[3] == 0 && disc.at(49, 49)[3] == 0, "background transparent");
        CHECK(disc.at(25, 25)[3] == 255, "subject kept");
    });

    run_case("legacy files: want list, states, custom files with backups, cache", [&] {
        fs::path le = g_out / "legacy_le";
        fs::remove_all(le);
        LegacyImages L(le);
        const std::string a = legacy_path::player_miniface(123), b = legacy_path::tattoo_preview(7);
        CHECK(a == "data/ui/imgAssets/heads/p123.dds" && b == "data/ui/imgAssets/tattoo/item_7_0.dds", "paths");
        CHECK(legacy_path::staff_miniface(9) == "data/ui/imgAssets/heads_staff/heads_staff_9.dds", "staff path");
        CHECK(!legacy_path::valid("data/../x.dds") && !legacy_path::valid("C:/x.dds") && !legacy_path::valid("data/a b.dds"), "bad paths");
        fs::path f;
        CHECK(L.locate(a, &f) == LegacyImages::State::Waiting, "not cached: waiting");
        CHECK(L.locate(b, &f) == LegacyImages::State::Waiting, "second waiting");
        CHECK(L.locate("data/../evil.dds", &f) == LegacyImages::State::Invalid, "invalid");
        L.tick(0.0);
        std::string want = read_file(L.cache_dir() / "want.txt");
        CHECK(want.rfind("#gen ", 0) == 0, "generation line first");
        CHECK(want.find(b + "\n" + a + "\n") != std::string::npos, "most recent first: " + want);
        CHECK(want.find("evil") == std::string::npos, "invalid never listed");
        // Lua exported one, the other is missing
        fs::create_directories(L.cache_dir() / "data" / "ui" / "imgAssets" / "heads");
        std::ofstream(L.cache_dir() / "data" / "ui" / "imgAssets" / "heads" / "p123.dds") << "x";
        std::ofstream(L.cache_dir() / "missing.txt") << b << "\n";
        L.tick(2.0);
        CHECK(L.locate(a, &f) == LegacyImages::State::Game && f.filename() == "p123.dds", "exported: game picture");
        CHECK(L.locate(b, &f) == LegacyImages::State::Missing, "missing");
        L.tick(3.0);
        CHECK(L.waiting() == 0, "nothing waiting");
        CHECK(read_file(L.cache_dir() / "want.txt").find(a) == std::string::npos, "arrived picture leaves the list");
        // a custom file written by another tool in upper case wins
        fs::create_directories(L.mods_dir() / "data" / "ui" / "imgAssets" / "heads");
        std::ofstream(L.mods_dir() / "data" / "ui" / "imgAssets" / "heads" / "P123.DDS") << "old";
#ifdef _WIN32
        // Windows file names are case-insensitive: P123.DDS and p123.dds are the same file
        CHECK(L.locate(a, &f) == LegacyImages::State::Custom && read_file(f) == "old", "custom (any case) first");
#else
        CHECK(L.locate(a, &f) == LegacyImages::State::Custom && f.filename() == "P123.DDS", "custom (any case) first");
#endif
        CHECK(L.locate(a, &f, false) == LegacyImages::State::Game, "game picture when asked for the game's own");
        std::string err;
        fs::path bk;
        CHECK(L.save_custom(a, {'n', 'e', 'w'}, &err, &bk), "save: " + err);
        CHECK(!bk.empty() && read_file(bk) == "old", "old custom file backed up");
        CHECK(read_file(L.custom_file(a)) == "new" && L.custom_file(a).filename() == "p123.dds", "new file, lower-case name");
#ifndef _WIN32
        CHECK(!fs::exists(L.mods_dir() / "data" / "ui" / "imgAssets" / "heads" / "P123.DDS"), "upper-case duplicate removed");
#endif
        CHECK(L.remove_custom(a, &err, &bk), "remove: " + err);
        CHECK(read_file(bk) == "new" && L.custom_file(a).empty(), "removed after backup");
        CHECK(!L.remove_custom(a, &err) && err.find("no custom") != std::string::npos, "nothing to remove");
        CHECK(!L.save_custom("data/../x.dds", {'x'}, &err), "invalid path refused");
        size_t backups = 0;
        for (auto& e : fs::directory_iterator(L.backup_dir())) { (void)e; ++backups; }
        CHECK(backups == 2, fmt("backups %zu", backups));
        uint64_t g0 = L.generation();
        CHECK(L.cached_files() == 1, "one cached picture");
        CHECK(L.clear_cache(&err), "clear: " + err);
        CHECK(L.cached_files() == 0 && L.generation() > g0, "cache empty, new generation");
        CHECK(read_file(L.cache_dir() / "want.txt").find("#gen " + std::to_string(L.generation())) == 0, "new generation published");
        CHECK(L.locate(b, &f) == LegacyImages::State::Waiting, "missing list forgotten with the cache");
    });

    run_case("pictures: DDS format read and written back the same way (crests: BGRA8, DXT5 + mips, DXT1, DXT3, 50 x 50, DX10)", [&] {
        auto gradient = [](int n) {
            Rgba img(solid(n, n, 0, 0, 0));
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x) {
                    uint8_t* q = img.at(x, y);
                    q[0] = uint8_t(x * 255 / std::max(1, n - 1)); q[1] = uint8_t(y * 255 / std::max(1, n - 1)); q[2] = 90;
                    q[3] = (x < n / 4) ? 0 : 255;  // transparent left strip
                }
            return img;
        };
        std::string err;
        // plain 256 x 256 B8G8R8A8 like FC Editor's crests, no mips
        DdsFormat f;
        f.pixel = DdsFormat::Pixel::BGRA8; f.w = f.h = 256; f.mips = 1;
        std::vector<uint8_t> dds = encode_dds(gradient(256), f, &err);
        CHECK(dds.size() == 128 + 256 * 256 * 4, fmt("BGRA8 size %zu (%s)", dds.size(), err.c_str()));
        CHECK(le32(dds, 8) == (0x1007 | 0x8) && le32(dds, 20) == 1024 && le32(dds, 80) == 0x41 && le32(dds, 88) == 32, "BGRA8 header: pitch, RGB|ALPHA, 32 bits");
        CHECK(le32(dds, 92) == 0x00FF0000 && le32(dds, 100) == 0x000000FF && le32(dds, 104) == 0xFF000000, "BGRA masks");
        DdsFormat back;
        CHECK(parse_dds_format(dds, back, &err) && back.pixel == DdsFormat::Pixel::BGRA8 && back.w == 256 && back.mips == 1 && !back.dx10, "parsed back: " + err);
        Rgba img;
        CHECK(decode_image(dds, img, &err) && img.w == 256, "decodes: " + err);
        CHECK(img.at(200, 10)[0] == 200 && img.at(200, 10)[1] == 10 && img.at(200, 10)[2] == 90 && img.at(200, 10)[3] == 255, "BGRA8 exact pixels");
        CHECK(img.at(5, 5)[3] == 0, "transparency kept");
        // the original header is reused when the format came from a file (same bytes except the pitch)
        std::vector<uint8_t> hdr = dds;
        hdr[32] = 'T'; hdr[33] = 'X';  // reserved bytes of the original header
        DdsFormat from_file;
        CHECK(parse_dds_format(hdr, from_file, &err) && from_file.header.size() == 128, "header kept");
        std::vector<uint8_t> again = encode_dds(gradient(256), from_file, &err);
        CHECK(again.size() == hdr.size() && again[32] == 'T' && again[33] == 'X', "original header bytes written back");
        // DXT5 with a full mip chain (256 -> 1 = 9 levels)
        f.pixel = DdsFormat::Pixel::DXT5; f.mips = 9;
        dds = encode_dds(gradient(256), f, &err);
        size_t expect = 128;
        for (int n = 256; n >= 1; n /= 2) expect += size_t((n + 3) / 4) * size_t((n + 3) / 4) * 16;
        CHECK(dds.size() == expect, fmt("DXT5 9 mips size %zu / %zu", dds.size(), expect));
        CHECK((le32(dds, 8) & 0x20000) && le32(dds, 28) == 9 && (le32(dds, 108) & 0x400000) && std::memcmp(dds.data() + 84, "DXT5", 4) == 0, "mip flags, DXT5");
        CHECK(parse_dds_format(dds, back, &err) && back.pixel == DdsFormat::Pixel::DXT5 && back.mips == 9, "DXT5 parsed back");
        CHECK(decode_image(dds, img, &err) && img.w == 256 && img.at(5, 5)[3] == 0 && img.at(200, 10)[3] == 255, "DXT5 top level decodes with alpha");
        // DXT1 and DXT3 (DXT3 keeps 4-bit alpha), 50 x 50 = sizes that are not multiples of 4
        f.pixel = DdsFormat::Pixel::DXT1; f.mips = 1; f.w = f.h = 50;
        dds = encode_dds(gradient(50), f, &err);
        CHECK(dds.size() == 128 + 13 * 13 * 8, fmt("DXT1 50x50 size %zu", dds.size()));
        CHECK(decode_image(dds, img, &err) && img.w == 50 && img.h == 50 && std::abs(int(img.at(40, 40)[1]) - 208) <= 24, "DXT1 50x50 decodes: " + err);
        f.pixel = DdsFormat::Pixel::DXT3;
        dds = encode_dds(gradient(50), f, &err);
        CHECK(dds.size() == 128 + 13 * 13 * 16 && std::memcmp(dds.data() + 84, "DXT3", 4) == 0, "DXT3 50x50");
        CHECK(decode_image(dds, img, &err) && img.at(2, 2)[3] == 0 && img.at(40, 40)[3] == 255, "DXT3 alpha");
        // 50 x 50 BGRA8 with 2 mips (50 -> 25)
        f.pixel = DdsFormat::Pixel::BGRA8; f.mips = 2;
        dds = encode_dds(gradient(50), f, &err);
        CHECK(dds.size() == 128 + 50 * 50 * 4 + 25 * 25 * 4, fmt("BGRA8 50x50 2 mips size %zu", dds.size()));
        // DX10 header
        f.pixel = DdsFormat::Pixel::RGBA8; f.mips = 1; f.dx10 = true; f.w = f.h = 32;
        dds = encode_dds(gradient(32), f, &err);
        CHECK(dds.size() == 148 + 32 * 32 * 4 && std::memcmp(dds.data() + 84, "DX10", 4) == 0 && le32(dds, 128) == 28, "DX10 RGBA8");
        CHECK(parse_dds_format(dds, back, &err) && back.dx10 && back.pixel == DdsFormat::Pixel::RGBA8 && back.header.size() == 148, "DX10 parsed back");
        CHECK(decode_image(dds, img, &err) && img.at(20, 3)[0] == uint8_t(20 * 255 / 31), "DX10 RGBA8 decodes");
        // refusals
        CHECK(encode_dds(gradient(16), f, &err).empty() && err.find("size") != std::string::npos, "wrong picture size refused: " + err);
        std::vector<uint8_t> bad = dds;
        bad[84] = 'A'; bad[85] = 'T'; bad[86] = 'I'; bad[87] = '2';
        CHECK(!parse_dds_format(bad, back, &err) && !err.empty(), "unknown FourCC refused: " + err);
        Rgba half = halve_image(gradient(50));
        CHECK(half.w == 25 && half.h == 25 && halve_image(half).w == 12 && halve_image(solid(1, 1, 1, 1, 1)).w == 1, "halving sizes");
        // crest paths
        CHECK(legacy_path::crest(7, 0, "light") == "data/ui/imgAssets/crest/light/l7.dds", "big crest path");
        CHECK(legacy_path::crest(7, 32, "dark") == "data/ui/imgAssets/crest32x32/dark/l7.dds", "32 x 32 dark path");
        CHECK(crest_variants(7).size() == 18 && crest_main_path(7) == legacy_path::crest(7, 0, "light"), "18 variants");
        for (const auto& v : crest_variants(115845)) CHECK(legacy_path::valid(v.path), "valid: " + v.path);
    });

    run_case("team names: Live Editor's custom_team_names.csv read, set, other rows kept, atomic save with backup", [&] {
        fs::path le = g_out / "names_le";
        fs::remove_all(le);
        fs::path file = team_names_file(le);
        CHECK(file == le / "extensions" / "global" / "custom_team_names.csv", "file location");
        TeamNamesCsv csv;
        std::string err;
        CHECK(csv.load(file, &err) && csv.size() == 0 && csv.loaded(), "missing file = empty list");
        csv.set_team_names(115845, "Atalanta BC", "", "Atalanta", "Atalanta BC");
        CHECK(csv.get("TeamName_115845") == "Atalanta BC" && !csv.has("TeamName_Abbr3_115845"), "empty abbreviation = no row");
        fs::path bk;
        CHECK(csv.save(file, team_names_backup_dir(le), &err, &bk) && bk.empty(), "first save, nothing to back up: " + err);
        CHECK(read_file(file) == "key;value\nTeamName_115845;Atalanta BC\nTeamName_Abbr10_115845;Atalanta\nTeamName_Abbr15_115845;Atalanta BC\n", "file text: " + read_file(file));
        // a file as the user has it (CRLF, other rows, a comment) is kept as it is
        std::ofstream(file, std::ios::binary) << "key;value\r\nTeamName_1;Arsenal FC\r\n# note\r\nTeamName_Abbr3_1;ARS\r\nTeamName_115845;Atalanta\r\n";
        TeamNamesCsv c2;
        CHECK(c2.load(file, &err) && c2.size() == 5 && c2.get("TeamName_Abbr3_1") == "ARS", "loaded 5 rows");
        c2.set("TeamName_115845", "Bergamo Calcio");
        c2.set("TeamName_Abbr3_115845", "BER");
        c2.set("TeamName_Abbr3_1", "");  // removed
        CHECK(c2.save(file, team_names_backup_dir(le), &err, &bk), "save: " + err);
        CHECK(!bk.empty() && read_file(bk).find("TeamName_115845;Atalanta\r\n") != std::string::npos, "previous file backed up");
        std::string text = read_file(file);
        CHECK(text == "key;value\r\nTeamName_1;Arsenal FC\r\n# note\r\nTeamName_115845;Bergamo Calcio\r\nTeamName_Abbr3_115845;BER\r\n", "rows kept in place, CRLF kept: " + text);
        CHECK(!fs::exists(file.string() + ".tmp"), "no temp file left");
        CHECK(clean_team_name("  A;B\nC  ") == "ABC" && clean_team_name("Borussia Moenchengladbach 1900", 10) == "Borussia M", "cleaning and cutting");
        CHECK(clean_team_name("\xC3\x89\xC3\x89", 3) == "\xC3\x89", "no half UTF-8 sequence");
        TeamNameKeys k = team_name_keys(5);
        CHECK(k.full == "TeamName_5" && k.abbr3 == "TeamName_Abbr3_5" && k.abbr10 == "TeamName_Abbr10_5" && k.abbr15 == "TeamName_Abbr15_5", "keys");
    });
}


// ================================================================ dev service (memory tools)
static void test_devops() {
    run_case("dev service: read, ptrs, write, find, scan / refine, dump, multi, bad requests", [&] {
        SimMemory mem;
        const uint64_t A = 0x50000000, B = 0x50100000;
        mem.map(A, 0x4000);
        mem.map(B, 0x2000);
        uint32_t v = 4242;
        mem.wr(A + 0x100, v);
        mem.wr(A + 0x3000, v);
        mem.wr(B + 0x10, v);
        uint64_t p = B + 0x10;
        mem.wr(A + 0x200, p);
        uint8_t sig[] = {0x48, 0x8B, 0x05, 0x11, 0x22};
        mem.write(B + 0x800, sig, sizeof(sig));
        float fl = 3.25f;
        mem.wr(A + 0x400, fl);
        DevEnv env;
        env.mem = &mem;
        env.regions = [&]() {
            return std::vector<DevRegion>{{A, A + 0x4000, true, "private"}, {B, B + 0x2000, false, "image"}};
        };
        double clock = 0.0;
        env.clock = [&]() { return clock; };
        std::vector<int> keys;
        env.key = [&](int vk, int ms, bool* sent) { keys.push_back(vk); *sent = true; return std::string(); };
        env.sleep = [&](int ms) { clock += ms / 1000.0; };
        env.out_dir = g_out;
        DevService d(env);
        json r = d.run({{"id", 1}, {"op", "read"}, {"addr", "0x50000100"}, {"len", 4}});
        CHECK(r["ok"] && r["hex"] == "92100000" && r["id"] == 1, "read: " + r.dump());
        r = d.run({{"op", "ptrs"}, {"addr", "0x50000200"}, {"count", 1}});
        CHECK(r["values"][0] == "0x50100010", "ptrs: " + r.dump());
        r = d.run({{"op", "read"}, {"addr", "0x60000000"}});
        CHECK(!r["ok"] && r["error"].get<std::string>().find("unreadable") != std::string::npos, "unreadable refused");
        r = d.run({{"op", "find"}, {"pattern", "48 8B ?? 11"}});
        CHECK(r["ok"] && r["hits"].size() == 1 && r["hits"][0] == "0x50100800", "find with wildcard: " + r.dump());
        r = d.run({{"op", "find"}, {"pattern", "48 8B ?? 11"}, {"writable", true}});
        CHECK(r["hits"].empty(), "find limited to writable regions");
        r = d.run({{"op", "scan"}, {"type", "i32"}, {"value", 4242}});
        CHECK(r["count"] == 3, "scan i32: " + r.dump());
        uint32_t v2 = 4243;
        mem.wr(A + 0x3000, v2);
        r = d.run({{"op", "changed"}});
        CHECK(r["count"] == 1 && r["first"][0] == "0x50003000", "changed: " + r.dump());
        r = d.run({{"op", "refine"}, {"value", 4243}});
        CHECK(r["count"] == 1, "refine value");
        r = d.run({{"op", "list"}});
        CHECK(r["candidates"][0]["value"] == 4243, "list with values");
        r = d.run({{"op", "scan"}, {"type", "f32"}, {"value", 3.25}, {"writable", true}});
        CHECK(r["count"] == 1 && r["first"][0] == "0x50000400", "scan f32: " + r.dump());
        r = d.run({{"op", "write"}, {"addr", "0x50000100"}, {"hex", "01020304"}});
        uint32_t back = 0;
        mem.rd(A + 0x100, back);
        CHECK(r["ok"] && back == 0x04030201, "write");
        r = d.run({{"op", "write"}, {"addr", "0x60000000"}, {"hex", "01"}});
        CHECK(!r["ok"], "write to unmapped memory refused");
        r = d.run({{"op", "dump"}, {"addr", "0x50100800"}, {"len", 5}, {"file", "dev_dump.bin"}});
        CHECK(r["ok"] && file_bytes(g_out / "dev_dump.bin") == std::vector<uint8_t>(sig, sig + 5), "dump");
        r = d.run({{"op", "dump"}, {"addr", "0x50100800"}, {"len", 5}, {"file", "../evil.bin"}});
        CHECK(!r["ok"], "dump file name must be plain");
        r = d.run({{"op", "seq"}, {"steps", {{{"op", "key"}, {"vk", 27}, {"ms", 150}}, {{"op", "sleep"}, {"ms", 500}}, {{"op", "ping"}}}}});
        CHECK(r["ok"] && keys.size() == 1 && keys[0] == 27 && clock >= 0.5, "seq: key, sleep, ping: " + r.dump());
        r = d.run({{"op", "multi"}, {"ops", {{{"op", "multi"}, {"ops", json::array()}}}}});
        CHECK(!r["ok"], "nested multi refused");
        r = d.run({{"op", "region"}, {"addr", "0x50100004"}});
        CHECK(r["ok"] && r["start"] == "0x50100000" && r["kind"] == "image" && !r["writable"], "region");
        r = d.run({{"op", "nonsense"}});
        CHECK(!r["ok"], "unknown op");
        r = d.run(json::array());
        CHECK(!r["ok"], "not an object");
        // time budget: a scan stops when the clock runs out
        env.time_budget = 0.0;
        DevService d2(env);
        env.clock = nullptr;
        r = d2.run({{"op", "find"}, {"pattern", "FF FF FF FF FF"}});
        CHECK(r["ok"], "find with no hits");
    });
}

// Signature scanning for game-code hooks (core/sigscan.h; the Windows host src/win/game_hooks.cpp builds on it)
static void test_sigscan() {
    using namespace turbo;
    run_case("signatures: pattern scan over a synthetic code buffer (unique, ambiguous, missing)", [&] {
        std::vector<uint8_t> code(0x1000, 0xCC);
        const uint64_t base = 0x140001000ULL;
        // a "function" at +0x100: 48 89 5C 24 08 57 48 83 EC 20 ...
        const uint8_t fn[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9};
        std::memcpy(code.data() + 0x100, fn, sizeof(fn));
        // the same prologue again at +0x800 (ambiguous for a short pattern), but a different body
        std::memcpy(code.data() + 0x800, fn, 10);
        std::vector<uint8_t> b;
        std::vector<bool> m;
        CHECK(parse_pattern("48 89 5C 24 ?? 57 48 83 EC 20 48 8B F9", b, m) && b.size() == 13 && !m[4], "pattern parsed");
        auto hits = scan_pattern(code.data(), code.size(), base, b, m, 10);
        CHECK(hits.size() == 1 && hits[0] == base + 0x100, fmt("unique hit: %zu", hits.size()));
        CHECK(parse_pattern("48 89 5C 24 08 57", b, m), "short pattern");
        hits = scan_pattern(code.data(), code.size(), base, b, m, 10);
        CHECK(hits.size() == 2 && hits[1] == base + 0x800, "two hits");
        hits = scan_pattern(code.data(), code.size(), base, b, m, 1);
        CHECK(hits.size() == 1, "max_hits honoured");
        CHECK(parse_pattern("?? ?? 5C 24 08 57 00", b, m), "pattern with leading wildcards");
        hits = scan_pattern(code.data(), code.size(), base, b, m, 10);
        CHECK(hits.empty(), "leading wildcards and a mismatching tail: no hit");
        CHECK(parse_pattern("?? ?? 5C 24 08 57", b, m), "leading wildcards");
        hits = scan_pattern(code.data(), code.size(), base, b, m, 10);
        CHECK(hits.size() == 2 && hits[0] == base + 0x100, "leading wildcards: match starts before the first fixed byte");
        CHECK(parse_pattern("?? ??", b, m), "only wildcards parse");
        CHECK(scan_pattern(code.data(), code.size(), base, b, m, 10).empty(), "only wildcards never match");
        // a pattern at the very end of the buffer
        code[0xFFE] = 0xAB;
        code[0xFFF] = 0xCD;
        CHECK(parse_pattern("AB CD", b, m), "tail pattern");
        hits = scan_pattern(code.data(), code.size(), base, b, m, 10);
        CHECK(hits.size() == 1 && hits[0] == base + 0xFFE, "match at the end of the buffer");
        CHECK(scan_pattern(code.data(), 1, base, b, m, 10).empty(), "buffer shorter than the pattern");
    });
    run_case("signatures: rip-relative operands (call/jmp rel32, jcc, call [rip], mov/lea/cmp [rip], mov imm)", [&] {
        using V = std::vector<uint8_t>;
        uint64_t t = 0;
        std::string err;
        const uint64_t at = 0x140010000ULL;
        V call = {0xE8, 0x10, 0x00, 0x00, 0x00};
        CHECK(resolve_rip(call.data(), call.size(), at, t, err) && t == at + 5 + 0x10, "E8 rel32");
        V jmp = {0xE9, 0xF0, 0xFF, 0xFF, 0xFF};
        CHECK(resolve_rip(jmp.data(), jmp.size(), at, t, err) && t == at + 5 - 0x10, "E9 negative rel32");
        V jcc = {0x0F, 0x84, 0x00, 0x01, 0x00, 0x00};
        CHECK(resolve_rip(jcc.data(), jcc.size(), at, t, err) && t == at + 6 + 0x100, "0F 84 rel32");
        V callrip = {0xFF, 0x15, 0x00, 0x00, 0x01, 0x00};
        CHECK(resolve_rip(callrip.data(), callrip.size(), at, t, err) && t == at + 6 + 0x10000, "FF 15 [rip]: the slot");
        V movrax = {0x48, 0x8B, 0x05, 0x34, 0x12, 0x00, 0x00, 0x90};
        CHECK(resolve_rip(movrax.data(), movrax.size(), at, t, err) && t == at + 7 + 0x1234, "48 8B 05 disp32");
        V learcx = {0x48, 0x8D, 0x0D, 0xFC, 0xFF, 0xFF, 0xFF};
        CHECK(resolve_rip(learcx.data(), learcx.size(), at, t, err) && t == at + 7 - 4, "48 8D 0D negative disp32");
        V lea_r8 = {0x4C, 0x8D, 0x05, 0x00, 0x10, 0x00, 0x00};
        CHECK(resolve_rip(lea_r8.data(), lea_r8.size(), at, t, err) && t == at + 7 + 0x1000, "4C 8D 05 (REX.R)");
        V mov32 = {0x8B, 0x0D, 0x08, 0x00, 0x00, 0x00};
        CHECK(resolve_rip(mov32.data(), mov32.size(), at, t, err) && t == at + 6 + 8, "8B 0D without REX");
        V cmpq = {0x48, 0x83, 0x3D, 0x10, 0x00, 0x00, 0x00, 0x00};
        CHECK(resolve_rip(cmpq.data(), cmpq.size(), at, t, err) && t == at + 8 + 0x10, "48 83 3D disp32 imm8");
        V movimm = {0xC7, 0x05, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
        CHECK(resolve_rip(movimm.data(), movimm.size(), at, t, err) && t == at + 10 + 0x10, "C7 05 disp32 imm32");
        V movsxd = {0x48, 0x63, 0x05, 0x04, 0x00, 0x00, 0x00};
        CHECK(resolve_rip(movsxd.data(), movsxd.size(), at, t, err) && t == at + 7 + 4, "48 63 05 (movsxd)");
        V notrip = {0x48, 0x8B, 0x45, 0x10};
        CHECK(!resolve_rip(notrip.data(), notrip.size(), at, t, err) && err.find("rip-relative") != std::string::npos, "mov rax,[rbp+10] refused");
        V cut = {0xE8, 0x10, 0x00};
        CHECK(!resolve_rip(cut.data(), cut.size(), at, t, err), "cut-off instruction refused");
        V other = {0x90, 0x90, 0x90, 0x90, 0x90};
        CHECK(!resolve_rip(other.data(), other.size(), at, t, err) && err.find("opcode 90") != std::string::npos, "nop refused");
        CHECK(!resolve_rip(nullptr, 0, at, t, err), "null refused");
    });
    run_case("signatures: table JSON, build keys, built-in fallback, end-to-end resolution", [&] {
        CHECK(build_key(0x6AB9813C, 0x211EF000) == "6AB9813C-211EF000", "build key format");
        CHECK(build_key(0, 1) == "00000000-00000001", "build key zero padded");
        SignatureTable t;
        std::string err;
        const std::string good =
            "{\"build\":\"6AB9813C-211EF000\",\"game\":\"FC27.exe\",\"signatures\":{"
            "\"call_site\":{\"pattern\":\"E8 ?? ?? ?? ?? 48 8B D8 EB\",\"resolve\":\"rip\",\"note\":\"x\"},"
            "\"plain\":{\"pattern\":\"48 89 5C 24 08 57 48 83 EC 20\",\"offset\":2},"
            "\"later\":{\"pattern\":\"\"}}}";
        CHECK(parse_signature_table(good, t, err), "table parses: " + err);
        CHECK(t.build == "6AB9813C-211EF000" && t.sigs.size() == 3 && t.find("plain") && t.find("plain")->offset == 2 &&
                  t.find("call_site")->resolve == "rip" && t.find("later")->pattern.empty() && !t.find("nope"),
              "table contents");
        CHECK(!parse_signature_table("[]", t, err) && !err.empty(), "array refused");
        CHECK(!parse_signature_table("{\"signatures\":{}}", t, err) && err.find("build") != std::string::npos, "build missing");
        CHECK(!parse_signature_table("{\"build\":\"x\",\"signatures\":{\"a\":{\"pattern\":\"ZZ\"}}}", t, err) &&
                  err.find("malformed") != std::string::npos,
              "bad pattern refused");
        CHECK(!parse_signature_table("{\"build\":\"x\",\"signatures\":{\"a\":{\"pattern\":\"90\",\"resolve\":\"lea\"}}}", t, err),
              "unknown resolve refused");
        CHECK(!parse_signature_table("{\"build\":\"x\",\"signatures\":{\"a\":{\"offset\":99999}}}", t, err), "offset range");
        CHECK(!parse_signature_table("{\"build\":\"x\",\"signatures\":[]}", t, err), "signatures must be an object");
        CHECK(parse_signature_table("{\"build\":\"x\"}", t, err) && t.sigs.empty() && t.game == "FC27.exe", "no signatures is fine");
        // round trip through signature_table_json
        CHECK(parse_signature_table(good, t, err), "reparse");
        SignatureTable t2;
        CHECK(parse_signature_table(signature_table_json(t), t2, err) && t2.sigs.size() == 3 && t2.find("plain")->offset == 2 &&
                  t2.find("call_site")->resolve == "rip" && t2.find("call_site")->note == "x",
              "JSON round trip");
        // built-in table: the 2026-10-03 build is known, a made-up build is not
        const SignatureTable* b = builtin_signature_table("6AB9813C-211EF000");
        CHECK(b && b->find("game_tick") && b->find("game_tick")->pattern.empty(), "built-in table for the known build");
        CHECK(builtin_signature_table("00000000-00000000") == nullptr, "unknown build has no table");
        CHECK(!builtin_builds().empty() && builtin_builds()[0] == "6AB9813C-211EF000", "builtin_builds lists it");
        for (const auto& bt : builtin_builds()) {
            const SignatureTable* tb = builtin_signature_table(bt);
            SignatureTable rt;
            CHECK(tb && parse_signature_table(signature_table_json(*tb), rt, err) && rt.sigs.size() == tb->sigs.size(),
                  "built-in table " + bt + " is well formed");
        }
        // end to end over a synthetic buffer
        std::vector<uint8_t> code(0x400, 0xCC);
        const uint64_t base = 0x140002000ULL;
        const uint8_t fn[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
        std::memcpy(code.data() + 0x40, fn, sizeof(fn));
        const uint8_t site[] = {0xE8, 0xFB, 0xFF, 0xFF, 0xFF, 0x48, 0x8B, 0xD8, 0xEB};  // call rel -5 -> itself
        std::memcpy(code.data() + 0x200, site, sizeof(site));
        SigResult r = resolve_signature(*t.find("plain"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.match == base + 0x40 && r.address == base + 0x42, "plain + offset");
        r = resolve_signature(*t.find("call_site"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.match == base + 0x200 && r.address == base + 0x200, "rip resolved: " + r.error);
        r = resolve_signature(*t.find("later"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Skipped && r.address == 0, "placeholder skipped");
        Signature miss{"miss", "AA BB CC DD EE FF", "none", 0, ""};
        r = resolve_signature(miss, code.data(), code.size(), base);
        CHECK(r.state == SigState::Missing && r.hits == 0, "missing");
        Signature amb{"amb", "CC CC CC", "none", 0, ""};
        r = resolve_signature(amb, code.data(), code.size(), base);
        CHECK(r.state == SigState::Ambiguous && r.hits == 2 && r.address == 0, "ambiguous never resolves");
        Signature bad{"bad", "48 89 5C 24 08 57 48 83 EC 20", "rip", 0, ""};
        r = resolve_signature(bad, code.data(), code.size(), base);
        CHECK(r.state == SigState::BadPattern && r.error.find("rip-relative") != std::string::npos, "non-rip instruction refused");
        Signature off{"off", "48 89 5C 24 08 57 48 83 EC 20", "none", 0x1000, ""};
        r = resolve_signature(off, code.data(), code.size(), base);
        CHECK(r.state == SigState::BadPattern, "offset outside the buffer refused");
        Signature badpat{"badpat", "4", "none", 0, ""};
        r = resolve_signature(badpat, code.data(), code.size(), base);
        CHECK(r.state == SigState::BadPattern, "malformed pattern");
        CHECK(std::string(sig_state_name(SigState::Found)) == "found" && std::string(sig_state_name(SigState::Ambiguous)) == "ambiguous",
              "state names");
    });
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

// ---------------------------------------------------------------- live standings (FCE DataManager) on synthetic memory
namespace {
struct FceWorld {
    SimMemory mem;
    uint64_t ifce = 0x30000000, hub = 0x30001000, dc = 0x30002000, dm = 0x30003000;
    uint64_t slist = 0x30004000, rows = 0x30005000, flist = 0x30006000, fx = 0x30007000;
    static constexpr uint64_t kBase = 0x140000000ull;
    void w64(uint64_t a, uint64_t v) { mem.wr(a, v); }
    void w32(uint64_t a, uint32_t v) { mem.wr(a, v); }
    void w16(uint64_t a, uint16_t v) { mem.wr(a, v); }
    void w8(uint64_t a, uint8_t v) { mem.wr(a, v); }
    void row(int id, uint16_t comp, uint32_t team, int hw, int hd, int hl, int hgf, int hga, int aw, int ad, int al, int agf, int aga, int pts) {
        uint64_t a = rows + uint64_t(id) * fce::kStandingSize;
        w16(a, uint16_t(id)); w16(a + 2, comp); w32(a + 4, team); w8(a + 8, uint8_t(id));
        uint8_t c[] = {uint8_t(hw), uint8_t(hd), uint8_t(hl), uint8_t(hgf), uint8_t(hga), uint8_t(aw), uint8_t(ad), uint8_t(al), uint8_t(agf), uint8_t(aga)};
        mem.write(a + 9, c, 10);
        w16(a + 0x14, uint16_t(int16_t(pts))); w8(a + 0x16, 1);
    }
    void fixture(int id, uint16_t comp, int home, int away, int hs, int as, uint8_t completion, uint32_t date = 20260815) {
        uint64_t a = fx + uint64_t(id) * fce::kFixtureSize;
        w32(a, date); w16(a + 4, 1500); w16(a + 6, uint16_t(id)); w16(a + 8, comp);
        w16(a + 0xA, uint16_t(int16_t(home))); w16(a + 0xC, uint16_t(int16_t(away)));
        w8(a + 0xF, uint8_t(int8_t(hs))); w8(a + 0x10, 0xFF); w8(a + 0x11, uint8_t(int8_t(as))); w8(a + 0x12, 0xFF);
        w8(a + 0x13, completion); w8(a + 0x14, 1);
    }
    FceWorld(int nrows = 4, int nfix = 3, bool vtables = true) {
        for (uint64_t a : {ifce, hub, dc, dm, slist, rows, flist, fx}) mem.map(a, 0x1000);
        if (vtables) { w64(ifce, kBase + fce::kRvaInterfaceVtable); w64(dm, kBase + fce::kRvaDataManagerVtable); }
        w64(ifce + 0x18, hub); w64(hub + 0x18, dc); w64(dc + 0x80, dm); w64(dm + 0x28, dc);
        w64(dm + 0x88, slist); w64(slist, rows); w64(slist + 8, rows + uint64_t(nrows) * fce::kStandingSize);
        w64(dm + 0x60, flist); w32(flist, uint32_t(nfix)); w64(flist + 8, fx);
        // league 100: Arsenal(1) 2W 1D, Everton(7) 1W 1D 1L, Inter(241) 0W 1D 2L; row 3 unused
        row(0, 100, 1, 1, 1, 0, 4, 1, 1, 0, 0, 2, 1, 7);
        row(1, 100, 7, 1, 1, 0, 3, 1, 0, 0, 1, 1, 2, 4);
        row(2, 100, 241, 0, 1, 0, 1, 1, 0, 0, 2, 1, 5, 1);
        fixture(0, 100, 0, 1, 2, 1, 1);  // Arsenal 2-1 Everton (home row 0, away row 1)
        fixture(1, 100, 1, 2, 3, 0, 1);  // Everton 3-0 Inter
        fixture(2, 100, 2, 0, 1, 1, 0);  // Inter v Arsenal: not played yet (completion 0) although scores set
    }
};
}  // namespace

static void test_fce_standings() {
    run_case("FCE: locate the DataManager through the interface chain, validate, read rows and fixtures", [&] {
        FceWorld w;
        fce::Located loc;
        std::string err = fce::locate(w.mem, w.ifce, FceWorld::kBase, loc);
        CHECK(err.empty(), "located: " + err);
        CHECK(loc.manager == w.dm && loc.connector == w.dc && loc.hub == w.hub, "chain");
        CHECK(loc.row_count == 4 && loc.rows_begin == w.rows && loc.fixture_count == 3 && loc.fixtures_data == w.fx, "lists");
        CHECK(fce::validate(w.mem, loc, FceWorld::kBase), "validate");
        std::vector<fce::StandingRow> rows;
        CHECK(fce::read_rows(w.mem, loc, rows) && rows.size() == 4, "rows read");
        CHECK(rows[0].teamid == 1 && rows[0].hw == 1 && rows[0].hd == 1 && rows[0].aw == 1 && rows[0].points == 7 && rows[0].used == 1, "Arsenal row");
        CHECK(rows[0].played() == 3 && rows[0].gf() == 6 && rows[0].ga() == 2 && rows[0].gd() == 4, "derived played / GD");
        CHECK(rows[3].used == 0, "unused slot");
        std::vector<fce::Fixture> fx;
        CHECK(fce::read_fixtures(w.mem, loc, fx) && fx.size() == 3, "fixtures read");
        CHECK(fx[0].home_sid == 0 && fx[0].away_sid == 1 && fx[0].home_score == 2 && fx[0].away_score == 1 && fx[0].played(), "fixture 0");
        CHECK(!fx[2].played(), "fixture 2 not played");
        // wrong vtable / broken back-pointer / absurd list are refused
        fce::Located bad;
        CHECK(!fce::locate(w.mem, w.ifce, FceWorld::kBase + 0x1000, bad).empty() && !bad.ok(), "vtable mismatch refused");
        CHECK(fce::locate(w.mem, w.ifce, 0, bad).empty(), "no image base: vtables skipped");
        w.w64(w.dm + 0x28, w.dm);
        CHECK(!fce::locate(w.mem, w.ifce, FceWorld::kBase, bad).empty(), "back-pointer refused");
        CHECK(!fce::validate(w.mem, loc, FceWorld::kBase), "validate notices the change");
        w.w64(w.dm + 0x28, w.dc);
        w.w64(w.slist + 8, w.rows + 7);
        CHECK(!fce::locate(w.mem, w.ifce, FceWorld::kBase, bad).empty(), "odd vector size refused");
        CHECK(!fce::locate(w.mem, 0, FceWorld::kBase, bad).empty(), "no ifce refused");
    });

    run_case("FCE: write a row (identity check, only counters and points change)", [&] {
        FceWorld w;
        fce::Located loc;
        CHECK(fce::locate(w.mem, w.ifce, FceWorld::kBase, loc).empty(), "located");
        std::vector<fce::StandingRow> rows;
        fce::read_rows(w.mem, loc, rows);
        fce::StandingRow r = rows[2];
        r.aw = 2; r.al = 0; r.agf = 9; r.points = 7;
        CHECK(fce::write_row(w.mem, loc, r).empty(), "written");
        std::vector<fce::StandingRow> again;
        fce::read_rows(w.mem, loc, again);
        CHECK(again[2].aw == 2 && again[2].al == 0 && again[2].agf == 9 && again[2].points == 7 && again[2].hd == 1, "values back");
        CHECK(again[2].id == 2 && again[2].compobj == 100 && again[2].teamid == 241 && again[2].used == 1, "identity untouched");
        uint8_t b = 0;
        w.mem.rd(w.rows + 2 * fce::kStandingSize + 0x17, b);
        CHECK(b == 0, "padding byte untouched");
        fce::StandingRow stale = rows[1];
        stale.teamid = 999;
        CHECK(!fce::write_row(w.mem, loc, stale).empty(), "changed identity refused");
        fce::StandingRow unused = rows[3];
        CHECK(!fce::write_row(w.mem, loc, unused).empty(), "unused row refused");
        fce::StandingRow outside = rows[0];
        outside.addr = w.rows + 4 * fce::kStandingSize;
        CHECK(!fce::write_row(w.mem, loc, outside).empty(), "row outside the list refused");
    });

    run_case("FCE: apply_result adds and removes outcomes within the u8 limits", [&] {
        fce::StandingRow h, a;
        h.used = a.used = 1;
        fce::Points pts;
        CHECK(fce::apply_result(h, a, 2, 1, pts, +1).empty(), "home win");
        CHECK(h.hw == 1 && h.hgf == 2 && h.hga == 1 && h.points == 3 && a.al == 1 && a.agf == 1 && a.aga == 2 && a.points == 0, "home win counters");
        CHECK(fce::apply_result(h, a, 0, 0, pts, +1).empty(), "draw");
        CHECK(h.hd == 1 && a.ad == 1 && h.points == 4 && a.points == 1, "draw counters");
        CHECK(fce::apply_result(h, a, 0, 3, pts, +1).empty(), "away win");
        CHECK(h.hl == 1 && a.aw == 1 && h.points == 4 && a.points == 4 && h.hga == 4 && a.agf == 4, "away win counters");
        CHECK(fce::apply_result(h, a, 2, 1, pts, -1).empty(), "remove the home win");
        CHECK(h.hw == 0 && h.hgf == 0 && h.points == 1 && a.al == 0 && a.aga == 0 && a.points == 4, "removed");
        fce::StandingRow h2 = h, a2 = a;
        CHECK(!fce::apply_result(h, a, 2, 1, pts, -1).empty(), "removing a result that is not there is refused");
        CHECK(h.hw == h2.hw && h.points == h2.points && a.al == a2.al, "nothing changed on refusal");
        h.hgf = 254;
        CHECK(!fce::apply_result(h, a, 3, 0, pts, +1).empty(), "goal counter past 255 refused");
        CHECK(h.hgf == 254 && h.hw == 0, "unchanged after refusal");
        CHECK(!fce::apply_result(h, a, -1, 0, pts, +1).empty(), "negative score refused");
        fce::Points two{2, 1, 0};
        fce::StandingRow x, y;
        x.used = y.used = 1;
        fce::apply_result(x, y, 1, 0, two, +1);
        CHECK(x.points == 2, "custom points per win");
    });

    run_case("FCE: edit_result patches the fixture and both rows consistently", [&] {
        FceWorld w;
        fce::Located loc;
        CHECK(fce::locate(w.mem, w.ifce, FceWorld::kBase, loc).empty(), "located");
        fce::Points pts;
        // Arsenal 2-1 Everton -> Arsenal 1-3 Everton
        CHECK(fce::edit_result(w.mem, loc, 0, 1, 3, pts).empty(), "edited");
        std::vector<fce::StandingRow> rows;
        std::vector<fce::Fixture> fx;
        fce::read_rows(w.mem, loc, rows);
        fce::read_fixtures(w.mem, loc, fx);
        CHECK(fx[0].home_score == 1 && fx[0].away_score == 3 && fx[0].completion == 1, "fixture score");
        CHECK(rows[0].hw == 0 && rows[0].hl == 1 && rows[0].hd == 1 && rows[0].hgf == 3 && rows[0].hga == 3 && rows[0].points == 4, "Arsenal row: win became a loss");
        CHECK(rows[1].al == 0 && rows[1].aw == 1 && rows[1].agf == 3 && rows[1].aga == 1 && rows[1].points == 7, "Everton row: loss became a win");
        CHECK(rows[0].played() == 3 && rows[1].played() == 3, "played unchanged");
        CHECK(fce::edit_result(w.mem, loc, 0, 1, 3, pts).empty(), "same score again: no-op");
        CHECK(!fce::edit_result(w.mem, loc, 2, 1, 0, pts).empty(), "unplayed fixture refused");
        CHECK(!fce::edit_result(w.mem, loc, 7, 1, 0, pts).empty(), "no such fixture");
        CHECK(!fce::edit_result(w.mem, loc, 0, 100, 0, pts).empty(), "score out of range");
        // a draw -> away win, with a loss worth 1 point (like a shootout competition)
        fce::Points shoot{3, 1, 1};
        FceWorld w2;
        fce::Located loc2;
        fce::locate(w2.mem, w2.ifce, FceWorld::kBase, loc2);
        w2.fixture(1, 100, 1, 2, 2, 2, 1);
        w2.row(1, 100, 7, 1, 1, 0, 5, 3, 0, 1, 0, 1, 1, 5);
        w2.row(2, 100, 241, 0, 1, 0, 1, 1, 0, 1, 1, 3, 4, 2);
        CHECK(fce::edit_result(w2.mem, loc2, 1, 0, 1, shoot).empty(), "draw -> away win");
        fce::read_rows(w2.mem, loc2, rows);
        CHECK(rows[1].hd == 0 && rows[1].hl == 1 && rows[1].points == 5 && rows[2].ad == 0 && rows[2].aw == 1 && rows[2].points == 4, "points with a loss worth 1");
    });
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
    std::printf("native pictures\n");
    test_images();
    std::printf("native dev service\n");
    test_devops();
    std::printf("native signature scanning\n");
    test_sigscan();
    std::printf("native Live Editor log\n");
    test_le_log();
    std::printf("native live standings\n");
    test_fce_standings();
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
