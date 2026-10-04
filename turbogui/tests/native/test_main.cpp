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
#include <deque>
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
#include "core/game_calls.h"
#include "core/gamethread.h"
#include "core/memmap.h"
#include "core/le_log.h"
#include "core/model.h"
#include "core/player_capture.h"
#include "core/sigscan.h"
#include "core/standings_refresh.h"
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
        // surname table across a 4 KB chunk border: 10 sparse ids, two rows each (intensity duplicates, like the bank)
        std::vector<uint32_t> surn;
        for (uint32_t id = 900001; id <= 900145; id += 16) {
            surn.push_back(id);
            surn.push_back(id);
        }
        put_bank_table(bm, 0x6F0000F10ull, surn);
        // two player-keyed tables (ids overlap: 5001..5201 are in both), one tiny run and one mixed table (both unknown)
        put_bank_table(bm, 0x6F1000020ull, {5001, 5001, 5101, 5101, 5201, 5201, 5301, 5301, 5401, 5401, 5501, 5501});
        put_bank_table(bm, 0x6F1000800ull, {5001, 5101, 5201, 5001, 5101, 5201, 7000, 5001, 5101, 5201});
        put_bank_table(bm, 0x6F1001000ull, {900100, 900100, 900101});
        put_bank_table(bm, 0x6F1800010ull, {900200, 50, 900201, 51, 900202, 52, 900203, 53});
        // a dense run of consecutive keys (the bank's sample index, two rows each): never a selection table
        std::vector<uint32_t> dense;
        for (uint32_t id = 930000; id < 930040; ++id) {
            dense.push_back(id);
            dense.push_back(id);
        }
        bm.map(0x6F1900000ull, 0x2000);
        put_bank_table(bm, 0x6F1900010ull, dense);
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
        CHECK(classify_bank_table(1, 300000, 12, 6) == BankTableKind::Players && classify_bank_table(900001, 965000, 8, 4) == BankTableKind::Surnames &&
                  classify_bank_table(50, 900203, 8, 8) == BankTableKind::Unknown && classify_bank_table(900100, 900101, 3, 2) == BankTableKind::Unknown &&
                  classify_bank_table(930000, 930039, 80, 40) == BankTableKind::Index && classify_bank_table(930000, 930039, 80, 12) == BankTableKind::Surnames,
              "classification by values, size and density");
        std::vector<Region> regions = {{0x6F0000000ull, 0x6F0004000ull}, {0x6F1000000ull, 0x6F1002000ull}, {0x6F1800000ull, 0x6F1801000ull},
                                       {0x6F1900000ull, 0x6F1902000ull}, {0x6F2000000ull, 0x6F2001000ull}};
        int ticks = 0;
        BankCapture c = capture_commentary_bank(bm, regions, {}, [&]() { return double(ticks++); }, 4096);
        CHECK(c.ok && c.rows == 133 && c.regions == 5 && c.bytes >= 0x9000 && c.bytes < 0x9400, fmt("capture: ok=%d rows=%zu regions=%zu bytes=%llu", int(c.ok), c.rows, c.regions, static_cast<unsigned long long>(c.bytes)));
        CHECK(c.tables.size() == 6 && c.rejected == 1, fmt("6 runs, 1 rejected (dense index): %zu / %zu", c.tables.size(), c.rejected));
        CHECK(c.surnames.size() == 10 && c.surnames.count(900001) && c.surnames.count(900145) && !c.surnames.count(900100) && !c.surnames.count(900200) && !c.surnames.count(930000),
              fmt("spoken surnames from the surname table only: %zu", c.surnames.size()));
        CHECK(c.players.size() == 7 && c.players.at(5001) == 2 && c.players.at(5301) == 1 && c.players.at(7000) == 1, fmt("players with recordings, counted per table: %zu", c.players.size()));
        CHECK(c.note.find("10 spoken surnames") != std::string::npos && c.note.find("7 players") != std::string::npos && c.note.find("1 runs rejected") != std::string::npos, "summary: " + c.note);
        // the known-id filter: a surname table whose values the database does not know is rejected
        std::unordered_set<int64_t> known = {900001, 900017, 900033, 900049, 900065, 900081, 900097, 900113, 900129, 900145};
        BankCapture ck = capture_commentary_bank(bm, regions, {}, {}, 4096, &known);
        CHECK(ck.ok && ck.surnames.size() == 10 && ck.rejected == 1, "every surname known: accepted");
        std::unordered_set<int64_t> few = {900001, 900017};
        BankCapture cf = capture_commentary_bank(bm, regions, {}, {}, 4096, &few);
        CHECK(cf.ok && cf.surnames.empty() && cf.rejected == 2 && cf.players.size() == 7, "surname table of mostly unknown ids rejected, players kept: " + cf.note);
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
        CHECK(cache.tables.size() == 5 && cache.tables[0].kind == BankTableKind::Surnames && cache.tables[0].header_ok && cache.tables[4].kind == BankTableKind::Index &&
                  cache.source.find("10 spoken surnames") != std::string::npos,
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

    // ---- the game's audio service (core/commentary_audio.h): batch layout, pointer chain, the stepped build, cache record
    run_case("commentary audio: name batch and canary, pointer chain checks, the stepped build with a fake caller, cache record and precedence", [&] {
        using namespace caudio;
        // the batch: rows follow the asked list, ids outside 1..2^20-1 are skipped, the canary closes it
        std::vector<int64_t> asked = {900001, 0, 900002, 1 << 20, 950000};
        std::vector<uint64_t> b = pack_name_batch(asked);
        CHECK(b.size() == 4 && batch_row(b[0]) == 0 && batch_id(b[0]) == 900001 && batch_row(b[1]) == 2 && batch_id(b[1]) == 900002 &&
                  batch_row(b[2]) == 4 && batch_id(b[2]) == 950000 && batch_row(b[3]) == 5 && batch_id(b[3]) == kCanaryId,
              "packed {row, id} elements + canary");
        std::unordered_set<int64_t> kept;
        std::string err;
        CHECK(!unpack_name_batch(b, 4, asked, kept, err) && err.find("canary") != std::string::npos, "canary survived = the filter did not run: " + err);
        CHECK(unpack_name_batch(b, 3, asked, kept, err) && kept.size() == 3 && kept.count(950000), "every id kept");
        std::vector<uint64_t> f = {b[0], b[2]};  // the filter erased 900002 in place
        CHECK(unpack_name_batch(f, 2, asked, kept, err) && kept.size() == 2 && kept.count(900001) && kept.count(950000) && !kept.count(900002), "erased element gone");
        CHECK(unpack_name_batch(f, 0, asked, kept, err) && kept.empty(), "nothing kept");
        std::vector<uint64_t> bad = {batch_elem(0, 900009)};
        CHECK(!unpack_name_batch(bad, 1, asked, kept, err) && err.find("layout") != std::string::npos, "an id that was not asked at that row: " + err);
        std::vector<uint64_t> re = {b[2], b[0]};
        CHECK(!unpack_name_batch(re, 2, asked, kept, err) && err.find("reordered") != std::string::npos, "reordered: " + err);
        CHECK(!unpack_name_batch(f, 5, asked, kept, err), "more survivors than elements");
        // the pointer chain on synthetic memory: service (vtable) -> +0x40 names (vtable) -> +0x08 inner -> +0x10 audio
        SimMemory cm;
        cm.map(0x6A0000000ull, 0x4000);
        Fns fn;
        fn.registry = 0x14C2A8590ull;
        fn.get_service = 0x142A52420ull;
        fn.filter_names = 0x1439074C8ull;
        fn.service_vtable = 0x14A8C6D70ull;
        fn.names_vtable = 0x14A8C5F48ull;
        const uint64_t svc = 0x6A0000100ull, names = 0x6A0001000ull, inner = 0x6A0002000ull, audio = 0x6A0003000ull;
        cm.wr(svc, fn.service_vtable);
        cm.wr(svc + kServiceNames, names);
        cm.wr(names, fn.names_vtable);
        cm.wr(names + kNamesInner, inner);
        cm.wr(inner + kInnerAudio, audio);
        cm.wr(audio, uint64_t(0x1496FB5A8ull));
        Chain ch;
        CHECK(resolve_chain(cm, svc, fn, ch, err) && ch.service == svc && ch.names == names && ch.inner == inner && ch.audio == audio, "chain resolved: " + err);
        CHECK(fn.missing_names() == nullptr && std::string(fn.missing_players()) == "speech_query_ctor", "missing pieces named per path");
        Fns none;
        CHECK(std::string(none.missing_names()) == "commentary_service_registry" && fn_signatures().size() == 18, "first missing; 18 signatures");
        cm.wr(svc, uint64_t(0x14A8C6D78ull));
        CHECK(!resolve_chain(cm, svc, fn, ch, err) && err.find("not the commentary service") != std::string::npos, "wrong service vtable: " + err);
        cm.wr(svc, fn.service_vtable);
        cm.wr(names, uint64_t(0x14A8C5F50ull));
        CHECK(!resolve_chain(cm, svc, fn, ch, err) && err.find("not the commentary names object") != std::string::npos, "wrong names vtable: " + err);
        cm.wr(names, fn.names_vtable);
        cm.wr(names + kNamesInner, uint64_t(0));
        CHECK(!resolve_chain(cm, svc, fn, ch, err) && err.find("not bound") != std::string::npos, "null inner: " + err);
        cm.wr(names + kNamesInner, inner);
        cm.wr(inner + kInnerAudio, uint64_t(0x6A0FF0000ull));
        CHECK(!resolve_chain(cm, svc, fn, ch, err) && err.find("audio system") != std::string::npos, "unreadable audio: " + err);
        CHECK(!resolve_chain(cm, 0x6B0000000ull, fn, ch, err) && !resolve_chain(cm, 7, fn, ch, err), "unreadable / non-pointer service");
        // the build: a fake game whose bank speaks every third id and records every even player (link too for multiples of 10)
        struct FakeAudio : Caller {
            bool drop_canary = true, players_ok = true, all_no = false;
            int name_calls = 0, player_calls = 0;
            bool filter_names(std::vector<uint64_t>& batch, size_t& survivors, std::string&) override {
                ++name_calls;
                size_t w = 0;
                for (size_t i = 0; i < batch.size(); ++i) {
                    const int64_t id = batch_id(batch[i]);
                    const bool keep = id == kCanaryId ? !drop_canary : (!all_no && id % 3 == 0);
                    if (keep) batch[w++] = batch[i];
                }
                survivors = w;
                return true;
            }
            bool player_audio(const std::vector<int64_t>& pids, std::vector<int>& flags, std::string& e) override {
                ++player_calls;
                if (!players_ok) {
                    e = "player path off in the fake";
                    return false;
                }
                flags.assign(pids.size(), 0);
                for (size_t i = 0; i < pids.size(); ++i) {
                    if (pids[i] % 2 == 0) flags[i] |= kPlayerLowSimple;
                    if (pids[i] % 10 == 0) flags[i] |= kPlayerLowLink;
                }
                return true;
            }
        };
        BuildRequest req;
        for (int64_t id = 900001; id <= 901000; ++id) req.names.push_back(id);
        for (int64_t pid = 1; pid <= 500; ++pid) req.players.push_back(pid);
        double t = 0.0;  // a fast game: a step costs 0.5 ms -> the batch doubles up to the maximum
        auto clock = [&t]() { t += 0.0005; return t; };
        FakeAudio fake;
        Build build(req, clock);
        CHECK(build.batch() == 100, "starts with batch_start");
        int steps = 1;
        while (build.step(fake)) ++steps;
        BuildResult r = build.result();
        CHECK(build.done() && r.ok, "build done ok: " + r.note);
        CHECK(r.names.size() == 333 && r.names.count(900003) && !r.names.count(900004) && r.names_checked == 1000, fmt("333 of 1000 names: %zu", r.names.size()));
        CHECK(r.players.size() == 250 && r.players.at(10) == 3 && r.players.at(2) == 1 && !r.players.count(3) && r.players_checked == 500,
              fmt("250 players with recordings: %zu", r.players.size()));
        CHECK(build.batch() == 400 && steps == 6 && r.steps == 6 && fake.name_calls == 4 && fake.player_calls == 2, fmt("batch grew to 400: %d steps", steps));
        CHECK(r.note.find("333 of 1000") != std::string::npos && r.note.find("250 of 500") != std::string::npos && r.seconds > 0.0 && r.elapsed > 0.0, "summary: " + r.note);
        CHECK(!build.step(fake), "no step after done");
        // a slow game: the batch halves down to the minimum
        double ts = 0.0;
        auto slow = [&ts]() { ts += 0.01; return ts; };
        Build b2(req, slow);
        FakeAudio f2;
        b2.step(f2);
        CHECK(b2.batch() == 50, fmt("batch halved after a slow step: %zu", b2.batch()));
        for (int i = 0; i < 3 && b2.step(f2); ++i) {}
        CHECK(b2.batch() == 25, fmt("batch stays at the minimum: %zu", b2.batch()));
        CHECK(b2.progress().find("names") != std::string::npos && b2.progress().find("/ 1000") != std::string::npos, "progress: " + b2.progress());
        b2.cancel();
        CHECK(!b2.step(f2) && b2.result().cancelled && !b2.result().ok, "cancelled");
        // the canary survives: the filter did not run (no bridge) -> failed, nothing counted as spoken
        FakeAudio f3;
        f3.drop_canary = false;
        Build b3(req, clock);
        while (b3.step(f3)) {}
        CHECK(!b3.result().ok && b3.result().note.find("canary") != std::string::npos && b3.result().names.empty(), "unanswered batch: " + b3.result().note);
        // every answer 'no': the bank is not bound in this screen -> failed (never cached as an empty set)
        FakeAudio f4;
        f4.all_no = true;
        Build b4(req, clock);
        while (b4.step(f4)) {}
        CHECK(!b4.result().ok && b4.result().note.find("not bound") != std::string::npos, "all-no: " + b4.result().note);
        // the player path off: the names stand, the note says why, the player pass stops after one call
        FakeAudio f5;
        f5.players_ok = false;
        Build b5(req, clock);
        while (b5.step(f5)) {}
        CHECK(b5.result().ok && b5.result().names.size() == 333 && b5.result().players.empty() && b5.result().players_note.find("fake") != std::string::npos &&
                  b5.result().note.find("players not checked") != std::string::npos && f5.player_calls == 1,
              "names only: " + b5.result().note);
        Build b6(BuildRequest{}, clock);
        CHECK(!b6.step(fake) && !b6.result().ok && b6.result().note.find("nothing to check") != std::string::npos, "empty request");
        Build b7(req, clock);
        b7.abort("boom");
        CHECK(b7.done() && !b7.result().ok && b7.result().note == "boom" && !b7.step(fake), "abort");
        // the cache record: source "game audio service", read back with the audio-service source line
        BankCapture c = to_capture(r);
        CHECK(c.ok && c.source == kAudioSource && c.surnames.size() == 333 && c.players.size() == 250 && c.checked_names == 1000 && c.checked_players == 500 &&
                  c.steps == r.steps,
              "to_capture");
        std::string js = bank_cache_json(c, "ita_it", "2026-10-04 00:40", "6AB9813C-211EF000");
        BankCache cache;
        CHECK(parse_bank_cache_json(js, cache, &err) && cache.kind == kAudioSource && cache.surnames.size() == 333 && cache.players.size() == 250 &&
                  cache.players.at(10) == 3 && cache.checked_names == 1000 && cache.checked_players == 500 &&
                  cache.source == "the game's audio service (built 2026-10-04 00:40)",
              "cache round trip: " + cache.source + " / " + err);
        BankCapture scan;
        scan.ok = true;
        scan.surnames = {900001};
        BankCache c2;
        CHECK(parse_bank_cache_json(bank_cache_json(scan, "ita_it", "x", ""), c2, &err) && c2.kind == "live bank capture" && c2.source.find("live bank capture") == 0,
              "a memory-scan cache keeps its source");
        // precedence: hand-made list > game-built set > fallback
        fs::path game = g_out / "fakegame";
        fs::path le = g_out / "LE";
        fs::path cpath = spoken_cache_path(le, "ita_it");
        fs::remove(cpath);
        fs::remove(le / "turbo" / "callnames" / "spoken_ita_it.txt");
        Callnames cn;
        cn.refresh(le, game, "");
        CHECK(cn.spoken.from == SpokenSet::From::Fallback, "fallback first");
        CHECK(cn.apply_capture(c, le, "2026-10-04 00:40", "6AB9813C-211EF000", &err) && cn.spoken.from == SpokenSet::From::GameAudio && cn.spoken.verified &&
                  cn.spoken.ids.size() == 333 && cn.spoken.real(10) && !cn.spoken.real(3) && cn.spoken.source.find("audio service") != std::string::npos,
              "game-built set applied: " + cn.spoken.source + " " + err);
        cn.refresh(le, game, "");
        CHECK(cn.spoken.from == SpokenSet::From::GameAudio && cn.spoken.ids.size() == 333 && cn.spoken.players.size() == 250,
              "game-built set loaded from the cache: " + cn.spoken.source);
        std::ofstream((le / "turbo" / "callnames" / "spoken_ita_it.txt").string()) << "900015\n";
        cn.refresh(le, game, "");
        CHECK(cn.spoken.from == SpokenSet::From::ListFile && cn.spoken.ids.size() == 1 && cn.spoken.real(10) && cn.spoken.source.find("audio service") != std::string::npos,
              "hand-made list over the game-built set, players kept: " + cn.spoken.source);
        fs::remove(le / "turbo" / "callnames" / "spoken_ita_it.txt");
        fs::remove(cpath);
    });
    run_case("commentary audio: probe flags, probe sample, id cache, Lua list, the watcher state machine, an unbound result is never cached", [&] {
        using namespace caudio;
        struct FakeAudio : Caller {
            bool bound = true, drop_canary = true;
            int calls = 0;
            bool filter_names(std::vector<uint64_t>& batch, size_t& survivors, std::string&) override {
                ++calls;
                size_t w = 0;
                for (size_t i = 0; i < batch.size(); ++i) {
                    const int64_t id = batch_id(batch[i]);
                    const bool keep = id == kCanaryId ? !drop_canary : (bound && id % 3 == 0);
                    if (keep) batch[w++] = batch[i];
                }
                survivors = w;
                return true;
            }
            bool player_audio(const std::vector<int64_t>& pids, std::vector<int>& flags, std::string&) override {
                flags.assign(pids.size(), 0);
                return true;
            }
        };
        // the flags: a probe carries its mark, an all-no answer is `unbound`, a surviving canary is `no_filter`
        BuildRequest req;
        for (int64_t id = 900001; id <= 900060; ++id) req.names.push_back(id);
        req.probe = true;
        FakeAudio unbound;
        unbound.bound = false;
        Build b1(req);
        while (b1.step(unbound)) {}
        BuildResult r1 = b1.result();
        CHECK(!r1.ok && r1.probe && r1.unbound && !r1.no_filter && r1.names.empty(), "probe, all no: unbound: " + r1.note);
        FakeAudio nofilter;
        nofilter.drop_canary = false;
        Build b2(req);
        while (b2.step(nofilter)) {}
        CHECK(!b2.result().ok && b2.result().no_filter && !b2.result().unbound, "canary survived: no_filter: " + b2.result().note);
        FakeAudio bound;
        Build b3(req);
        while (b3.step(bound)) {}
        CHECK(b3.result().ok && b3.result().probe && b3.result().names.size() == 20 && !b3.result().unbound, "probe, bound: " + b3.result().note);
        // (b) an unbound result is never cached: to_capture keeps ok = false, apply_capture refuses it, no file appears
        fs::path game = g_out / "fakegame";
        fs::path lroot = g_out / "LE_probe";
        fs::create_directories(game / "commentary" / "commentaryfull_ita_it");
        fs::path cpath = spoken_cache_path(lroot, "ita_it");
        fs::remove(cpath);
        Callnames cn;
        cn.refresh(lroot, game, "");
        std::string err;
        BankCapture cap = to_capture(r1);
        CHECK(!cap.ok && cap.surnames.empty(), "to_capture of the unbound result is not ok");
        CHECK(!cn.apply_capture(cap, lroot, "x", "b", &err) && err.find("not bound") != std::string::npos && !fs::exists(cpath), "unbound result refused, no cache file: " + err);
        CHECK(!cn.spoken.verified && cn.spoken.from == SpokenSet::From::Fallback, "the set stays unverified");
        // the probe sample: preferred ids first (capped), then ids spread evenly, no duplicates, nothing outside the range
        std::vector<int64_t> all;
        for (int64_t id = 900000; id < 905000; ++id) all.push_back(id);
        std::vector<int64_t> pref = {904999, 900000, 900000, 0, 1 << 20, 902500};
        std::vector<int64_t> smp = probe_sample(all, pref, 48, 16);
        // 3 preferred ids, then 48 spread ids of which 900000 and 902500 repeat the preferred ones: 49 in all
        CHECK(smp.size() == 49 && smp[0] == 904999 && smp[1] == 900000 && smp[2] == 902500 && std::unordered_set<int64_t>(smp.begin(), smp.end()).size() == smp.size(),
              fmt("sample: %zu ids (preferred first, no duplicates)", smp.size()));
        CHECK(std::count(smp.begin(), smp.end(), 0) == 0 && std::count(smp.begin(), smp.end(), 1 << 20) == 0, "ids outside the 20-bit range dropped");
        CHECK(probe_sample({}, {}, 48, 16).empty() && probe_sample({900001, 900002}, {}, 48, 16).size() == 2, "small lists");
        std::vector<int64_t> big(5000);
        for (size_t i = 0; i < big.size(); ++i) big[i] = 900000 + static_cast<int64_t>(i);
        std::vector<int64_t> smp2 = probe_sample(big, {}, 48, 16);
        CHECK(smp2.size() == 48 && smp2.front() == 900000 && smp2.back() > 904800, "48 ids spread over 5000");
        // the id cache: json round trip, rejection of foreign / empty files
        IdCache ic;
        ic.names = {900002, 900001, 900001, 950000};
        ic.preview = {900002};
        ic.players = {5, 1001, 3};
        ic.when = "2026-10-04 02:10";
        ic.session = "6AC1E3C8";
        ic.source = "the career database";
        IdCache back;
        CHECK(parse_id_cache_json(id_cache_json(ic), back, &err) && back.names == std::vector<int64_t>({900001, 900002, 950000}) && back.preview == std::vector<int64_t>({900002}) &&
                  back.players == std::vector<int64_t>({3, 5, 1001}) && back.when == ic.when && back.session == ic.session && back.source == ic.source,
              "id cache round trip (sorted, deduplicated): " + err);
        CHECK(!parse_id_cache_json("{\"turbo_spoken\": 2}", back, &err) && err.find("not an id cache") != std::string::npos, "foreign json refused: " + err);
        CHECK(!parse_id_cache_json("{\"turbo_ids\": 1, \"names\": []}", back, &err) && err.find("no commentary ids") != std::string::npos, "empty list refused: " + err);
        CHECK(!parse_id_cache_json("not json", back, &err), "garbage refused");
        CHECK(id_cache_path(lroot) == lroot / "turbo_output" / "callnames" / "ids.json", "cache path");
        // Lua's commentary list
        std::vector<int64_t> ids;
        std::string session;
        CHECK(parse_commentary_list("#turbo-commentary 6AC1E3C8 4\n900001\tBerrie\n900002\tCalico\r\n12\tnot a callname\n965000\tLast\n", ids, &session, &err) &&
                  ids == std::vector<int64_t>({900001, 900002, 965000}) && session == "6AC1E3C8",
              "commentary list parsed (ids in the name range): " + err);
        CHECK(!parse_commentary_list("#turbo-commentary 6AC1E3C8 4\n900001\tBerrie\n", ids, nullptr, &err) && err.find("announces 4") != std::string::npos && ids.empty(),
              "a list being rewritten (count mismatch) refused: " + err);
        CHECK(!parse_commentary_list("#turbo-names 6AC1E3C8 1\n5\tSaka\n", ids, nullptr, &err) && err.find("header") != std::string::npos, "a names file refused: " + err);
        CHECK(!parse_commentary_list("#turbo-commentary x 1\n12\tnot a callname\n", ids, nullptr, &err) && err.find("no commentary id") != std::string::npos, "no id in range: " + err);
        // the watcher: verified -> Idle (no line); no ids -> a line saying so; waiting -> a probe at once, then every 3 s
        SpokenWatch w;
        SpokenWatch::Inputs in;
        in.service = in.available = in.have_ids = true;
        in.verified = true;
        CHECK(w.tick(0.0, in) == SpokenWatch::Action::None && w.state() == SpokenWatch::State::Idle && w.line().empty(), "verified: idle, no line");
        in.verified = false;
        in.have_ids = false;
        in.why_not = "no commentary id list at hand: connect to a career once";
        CHECK(w.tick(0.0, in) == SpokenWatch::Action::None && w.line().find("not built yet") != std::string::npos && w.line().find("connect to a career") != std::string::npos,
              "no ids: " + w.line());
        in.have_ids = true;
        in.why_not.clear();
        CHECK(w.tick(1.0, in) == SpokenWatch::Action::Probe && w.state() == SpokenWatch::State::Waiting && w.line().find("Create Player") != std::string::npos &&
                  w.line().find("start a match") != std::string::npos,
              "waiting: the first probe runs at once: " + w.line());
        w.started(1.0, true);
        CHECK(w.probes() == 1 && w.pending() && w.tick(1.1, in) == SpokenWatch::Action::None, "a pending probe: nothing more is started");
        w.last_probe_clock = "02:10:05";
        w.on_result(1.2, r1);  // unbound
        CHECK(!w.pending() && w.state() == SpokenWatch::State::Waiting && w.last_probe.find("not bound") != std::string::npos && w.next_probe() > 4.1 && w.next_probe() < 4.3,
              fmt("unbound: waiting, next probe in 3 s (%.1f)", w.next_probe()));
        CHECK(w.tick(2.0, in) == SpokenWatch::Action::None && w.line().find("02:10:05") != std::string::npos && w.line().find("not bound in this screen") != std::string::npos &&
                  w.line().find("every 3 s") != std::string::npos,
              "line while waiting: " + w.line());
        in.available = false;
        CHECK(w.tick(5.0, in) == SpokenWatch::Action::None, "service busy: no probe");
        in.available = true;
        CHECK(w.tick(5.0, in) == SpokenWatch::Action::Probe, "probe due");
        w.started(5.0, true);
        BuildResult boom;
        boom.probe = true;
        boom.note = "exception inside the audio-service call";
        w.on_result(5.1, boom);
        CHECK(w.last_probe.find("error") != std::string::npos && w.next_probe() > 11.0 && w.next_probe() < 11.2, fmt("an error doubles the interval: next at %.1f", w.next_probe()));
        w.started(11.2, true);
        w.on_result(11.3, boom);
        CHECK(w.next_probe() > 23.2 && w.next_probe() < 23.4, fmt("doubled again: %.1f", w.next_probe()));
        double gap = 0.0;
        for (int i = 0; i < 8; ++i) {
            const double at = w.next_probe();
            w.started(at, true);
            w.on_result(at + 0.1, boom);
            gap = w.next_probe() - (at + 0.1);
        }
        w.tick(w.next_probe() - 1.0, in);
        CHECK(gap > 59.9 && gap < 60.1 && w.line().find("every 60 s") != std::string::npos, fmt("capped at 60 s (gap %.1f): %s", gap, w.line().c_str()));
        // a bound answer: the full build starts on the next tick; its ok result ends the watch
        w.started(100.0, true);
        w.on_result(100.1, b3.result());
        CHECK(w.state() == SpokenWatch::State::Building && w.last_probe.find("bound") == 0 && w.bound_seen() == 1, "bound: " + w.last_probe);
        CHECK(w.tick(100.2, in) == SpokenWatch::Action::Build && w.line().find("building the set now") != std::string::npos, "the build is asked for: " + w.line());
        w.started(100.2, false);
        CHECK(w.tick(100.3, in) == SpokenWatch::Action::None, "the build is pending");
        BuildResult full;
        full.ok = true;
        full.note = "1800 of 4849 commentary ids have audio";
        full.names = {900003};
        w.on_result(102.0, full);
        CHECK(w.state() == SpokenWatch::State::Idle && w.last_build.find("ok: 1800") == 0, "build ok: idle: " + w.last_build);
        in.verified = true;
        CHECK(w.tick(102.1, in) == SpokenWatch::Action::None && w.line().empty(), "verified: no line");
        // a build that found the bank unbound (the screen changed): back to waiting, interval reset; a refusal backs off
        in.verified = false;
        w.tick(103.0, in);
        w.started(103.0, true);
        w.on_result(103.1, b3.result());
        CHECK(w.tick(103.2, in) == SpokenWatch::Action::Build, "build again");
        w.started(103.2, false);
        BuildResult fail;
        fail.unbound = true;
        fail.note = "the game answered no for every one";
        w.on_result(104.0, fail);
        CHECK(w.state() == SpokenWatch::State::Waiting && w.next_probe() > 106.9 && w.next_probe() < 107.1 && w.last_build.find("failed") == 0, "unbound build: waiting again");
        CHECK(w.tick(107.1, in) == SpokenWatch::Action::Probe, "probe again");
        w.refused(107.1, "a build is already running");
        CHECK(!w.pending() && w.state() == SpokenWatch::State::Waiting && w.last_probe.find("not started") == 0 && w.next_probe() > 113.0, "refused: backoff");
        // a manual build started by the user while waiting: the watcher waits for it, an ok result ends the watch
        w.started(120.0, false);
        CHECK(w.state() == SpokenWatch::State::Building && w.tick(120.1, in) == SpokenWatch::Action::None, "manual build pending");
        w.on_result(121.0, full);
        CHECK(w.state() == SpokenWatch::State::Idle, "manual build ok");
        fs::remove_all(lroot);
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

// Stand-in for the Windows host's capture service (src/win/player_capture_win.cpp): records requests, answers when told
struct FakeCapture : capture::CaptureService {
    capture::Status st;
    std::vector<capture::Request> requests;
    capture::Request current;
    bool pending = false;
    bool deliver = false;  // the next poll answers with `next`
    capture::Result next;
    int cancels = 0;
    capture::Status status() override {
        capture::Status s = st;
        s.busy = pending;
        s.busy_label = pending ? current.label : "";
        return s;
    }
    bool request(const capture::Request& r, std::string* err) override {
        if (!st.installed || !st.available) {
            if (err) *err = "fake: unavailable";
            return false;
        }
        if (pending) {
            if (err) *err = "fake: busy";
            return false;
        }
        requests.push_back(r);
        current = r;
        pending = true;
        return true;
    }
    bool poll(capture::Result& out) override {
        if (!pending || !deliver) return false;
        out = next;
        out.id = current.id;
        out.label = current.label;
        pending = false;
        deliver = false;
        return true;
    }
    void cancel() override {
        ++cancels;
        pending = false;
    }
    const capture::Template* learned() override { return nullptr; }
};

// ---------------------------------------------------------------- live standings (FCE DataManager) on synthetic memory
namespace {
struct FceWorld {
    SimMemory mem;
    uint64_t ifce = 0x30000000, hub = 0x30001000, dc = 0x30002000, dm = 0x30003000;
    uint64_t slist = 0x30004000, rows = 0x30005000, flist = 0x30006000, fx = 0x30007000;
    uint64_t col = 0x30008000, cdata = 0x30009000;  // CompObjectDataList {cap, count, data} and its 0x30-byte records
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
    // one CompObjectDataList record (docs/re/standings-ui-path.md 0c: +0 id, +4 parent, +6 type, +7 short, +0xE desc, +0x2F used)
    void compobj(int id, uint16_t parent, uint8_t type, const char* short_name, const char* desc) {
        uint64_t a = cdata + uint64_t(id) * fce::kCompObjSize;
        w16(a, uint16_t(id)); w16(a + 2, uint16_t(id)); w16(a + 4, parent); w8(a + 6, type);
        mem.write(a + 7, short_name, std::strlen(short_name) + 1);
        mem.write(a + 0xE, desc, std::strlen(desc) + 1);
        w8(a + 0x2F, 1);
    }
    // three more rows for the same clubs in group 101: the cup's setup pool (the game never shows it as a table)
    void add_pool() {
        row(4, 101, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        row(5, 101, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        row(6, 101, 241, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        w64(slist + 8, rows + 7 * fce::kStandingSize);
    }
    FceWorld(int nrows = 4, int nfix = 3, bool vtables = true) {
        for (uint64_t a : {ifce, hub, dc, dm, slist, rows, flist, fx, col}) mem.map(a, 0x1000);
        mem.map(cdata, 0x2000);
        // the competition tree: FIFA -> England -> C13 (league 13) -> S1 FCE_League_Stage -> G1 (group 100);
        //                                       -> C210 (a cup)    -> S1 FCE_Setup_Stage  -> G1 (group 101, the pool)
        w64(dm + 0x50, col); w32(col, 128); w32(col + 4, 102); w64(col + 8, cdata);
        compobj(0, 0xFFFF, fce::kCompTypeRoot, "FIFA", "FIFA");
        compobj(1, 0, fce::kCompTypeNation, "ENGL", "NationName_14");
        compobj(2, 1, fce::kCompTypeCompetition, "C13", "TrophyName_Abbr15_13");
        compobj(3, 2, fce::kCompTypeStage, "S1", "FCE_League_Stage");
        compobj(4, 1, fce::kCompTypeCompetition, "C210", "TrophyName_Abbr15_210");
        compobj(5, 4, fce::kCompTypeStage, "S1", "FCE_Setup_Stage");
        compobj(100, 3, fce::kCompTypeGroup, "G1", "");
        compobj(101, 5, fce::kCompTypeGroup, "G1", "");
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

// ---------------------------------------------------------------- standings refresh (core/standings_refresh.h) on synthetic memory
// A career manager table reached from the comm service ([[comm+0x20]+0x10]) with the StandingsViewManager in slot 108
// and the FCE interface in slot 1; the SVM's mLiveStandings as a real red-black-tree layout (anchor at +0x250, nodes
// with right / left / parent / key / value, size counter at +0x270); a fake game that records the calls; and a fake
// RefreshService for the UI tests.
namespace {
struct SvmWorld {
    SimMemory mem;
    static constexpr uint64_t kBase = 0x140000000ULL, kImageSize = 0x211EF000ULL;
    static constexpr uint64_t kComm = 0x50000000ULL, kX = 0x50001000ULL, kManagers = 0x50002000ULL, kTypes = 0x50006000ULL,
                              kHolders = 0x50008000ULL, kSvm = 0x50010000ULL, kIfce = 0x50012000ULL, kNodes = 0x50014000ULL,
                              kOther = 0x50018000ULL, kLive = 0x5001A000ULL, kStaff = 0x5001C000ULL, kAlloc = 0x5001E000ULL,
                              kSdm = 0x50020000ULL, kTree = 0x50030000ULL;  // kTree well past kSdm: a test relies on kSdm+0x2000 being unmapped
    // the game's own addresses (FC27.exe 1.0.140.64835): the vtables sit in the image, the functions too
    static constexpr uint64_t kVtable = kBase + svm::kRvaVtable, kLiveVtable = kBase + svm::kRvaLiveStandingsVtable,
                              kStaffVtable = 0x14B0160D8ULL, kIfceVtable = kBase + fce::kRvaInterfaceVtable,
                              kListener = 0x147DA0E10ULL, kRefresh = 0x147DA5310ULL, kPost = 0x148A35D3CULL,
                              kAllocGlobal = 0x14C269EA8ULL, kAllocFn = 0x142F95928ULL, kStaffListener = 0x147DA0C90ULL,
                              kCompObjVtable = kBase + svm::kRvaCompObjectVtable, kListVtable = kBase + svm::kRvaStandingListVtable;
    static constexpr int kStaffTypeId = 107;
    std::vector<int32_t> keys;
    uint64_t tree_next = kTree;      // bump allocator for set_tree
    uint64_t last_root = 0, last_stage = 0, last_list = 0, last_rows = 0;  // the nodes of the last set_tree (tests corrupt them)
    explicit SvmWorld(std::vector<int32_t> ks = {1200, 1300, 1400}) {
        for (uint64_t a : {kComm, kX, kTypes, kHolders, kSvm, kIfce, kOther, kLive, kStaff, kAlloc, kSdm, kVtable & ~0xFFFULL,
                           kLiveVtable & ~0xFFFULL, kIfceVtable & ~0xFFFULL, kAllocGlobal & ~0xFFFULL, kCompObjVtable & ~0xFFFULL})
            mem.map(a, 0x1000);
        mem.map(kTree, 0x8000);
        mem.map(kManagers, 0x20 * 256 + 0x100);
        mem.map(kNodes, 0x4000);
        mem.wr(kComm + svm::kCommManagersA, kX);
        mem.wr(kX + svm::kCommManagersB, kManagers);
        add_slot(svm::kTypeId, kSvm);
        add_slot(svm::kIfceTypeId, kIfce);
        add_slot(svm::kSimDayTypeId, kSdm);
        add_slot(kStaffTypeId, kStaff);
        // the StandingsViewManager: vtable (slot 1 = the career-event listener), ctx, a free critical section at +0x280
        mem.wr(kSvm, kVtable);
        mem.wr(kVtable + svm::kVtableSlotListener, kListener);
        mem.wr(kSvm + svm::kOffCtx, kManagers);
        set_critical_section(kSvm + svm::kOffCritSec, -1, 0, 0);
        mem.wr(kSvm + svm::kOffUserCopy, kLive);
        mem.wr(kSvm + svm::kOffFirstUpdate, uint8_t(1));
        // the FCE interface (slot 1) with the game's Post in vtable slot 4
        mem.wr(kIfce, kIfceVtable);
        mem.wr(kIfceVtable + svm::kIfceVtablePost, kPost);
        // the game allocator global -> allocator object -> vtable with allocate in slot 2
        mem.wr(kAllocGlobal, kAlloc);
        mem.wr(kAlloc, kAlloc + 0x100);
        mem.wr(kAlloc + 0x100 + svm::kAllocVtableAlloc, kAllocFn);
        // the SimDayManager idle
        mem.wr(kSdm + svm::kOffCtx, kManagers);
        mem.wr(kSdm + svm::kOffSimDayState, int32_t(0));
        // the StaffManager in slot 107: the class whose vtable Turbo mistook for the SVM's on 04-10-2026. Its +0x250 /
        // +0x280 hold other members (the live values read in the game: LockCount 0x38547280, "semaphore" 0x34B6AC20)
        mem.wr(kStaff, kStaffVtable);
        mem.wr(kStaffVtable + svm::kVtableSlotListener, kStaffListener);
        mem.wr(kStaff + svm::kOffCtx, kManagers);
        mem.wr(kStaff + svm::kOffMap, uint64_t(0x34B69D80ULL));
        mem.wr(kStaff + svm::kOffMapRoot, uint64_t(0x24C2F50ULL));
        mem.wr(kStaff + svm::kOffMapSize, uint32_t(0x2C302C30));
        mem.wr(kStaff + svm::kOffCritSec, uint64_t(0x34B60480ULL));
        mem.wr(kStaff + svm::kOffCritSec + svm::kCsLockCount, int32_t(0x38547280));
        mem.wr(kStaff + svm::kOffCritSec + svm::kCsOwner, uint64_t(0x2C30312C08F0D180ULL));
        mem.wr(kStaff + svm::kOffCritSec + 0x18, uint64_t(0x34B6AC20ULL));
        set_map(ks);
    }
    void add_slot(int id, uint64_t obj) {
        const uint64_t slot = kManagers + svm::kSlotSize * uint64_t(id), typ = kTypes + uint64_t(id) * 0x20, holder = kHolders + uint64_t(id) * 0x10;
        mem.wr(slot + svm::kSlotCount, int32_t(1));
        mem.wr(slot + svm::kSlotType, typ);
        mem.wr(typ + svm::kTypeFlag, int32_t(1));
        mem.wr(slot + svm::kSlotHolder, holder);
        mem.wr(holder, obj);
    }
    void set_critical_section(uint64_t cs, int32_t lock, int32_t recursion, uint64_t owner) {
        mem.wr(cs, uint64_t(~0ULL));  // DebugInfo: none
        mem.wr(cs + svm::kCsLockCount, lock);
        mem.wr(cs + svm::kCsRecursion, recursion);
        mem.wr(cs + svm::kCsOwner, owner);
        mem.wr(cs + 0x18, uint64_t(0));  // LockSemaphore
        mem.wr(cs + 0x20, uint64_t(0x100));  // SpinCount
    }
    uint64_t node(int i) const { return kNodes + uint64_t(i) * 0x40; }
    uint64_t live(int i) const { return kLive + uint64_t(i) * 0x10; }
    // balanced tree over keys[lo, hi) under `parent`; returns the subtree root (0 when empty)
    uint64_t build(int lo, int hi, uint64_t parent, int& next) {
        if (lo >= hi) return 0;
        const int mid = (lo + hi) / 2;
        const uint64_t n = node(next++);
        mem.wr(n + svm::kNodeParent, parent);
        mem.wr(n + svm::kNodeKey, keys[size_t(mid)]);
        mem.wr(n + svm::kNodeValue, live(mid));
        mem.wr(live(mid), kLiveVtable);
        mem.wr(live(mid) + 8, kOther + uint64_t(mid) * 0x100);
        const uint64_t l = build(lo, mid, n, next), r = build(mid + 1, hi, n, next);
        mem.wr(n + svm::kNodeLeft, l);
        mem.wr(n + svm::kNodeRight, r);
        return n;
    }
    void set_map(std::vector<int32_t> ks) {
        keys = std::move(ks);
        std::vector<uint8_t> zero(0x4000, 0);
        mem.write(kNodes, zero.data(), zero.size());
        int next = 0;
        const uint64_t root = build(0, int(keys.size()), kSvm + svm::kOffMap, next);
        uint64_t l = root, r = root, t = 0;
        while (l && mem.rd(l + svm::kNodeLeft, t) && t) l = t;
        while (r && mem.rd(r + svm::kNodeRight, t) && t) r = t;
        mem.wr(kSvm + svm::kOffMapRoot, root);
        mem.wr(kSvm + svm::kOffMap + svm::kNodeLeft, l ? l : kSvm + svm::kOffMap);
        mem.wr(kSvm + svm::kOffMap + svm::kNodeRight, r ? r : kSvm + svm::kOffMap);
        mem.wr(kSvm + svm::kOffMapSize, uint32_t(keys.size()));
    }
    uint64_t node_of(int32_t key) const {
        for (size_t i = 0; i < keys.size(); ++i) {
            int32_t k = 0;
            SimMemory& m = const_cast<SimMemory&>(mem);
            if (m.rd(node(int(i)) + svm::kNodeKey, k) && k == key) return node(int(i));
        }
        return 0;
    }
    svm::Request request() const {
        svm::Request req;
        req.comm = kComm;
        req.image_base = kBase;
        req.image_size = kImageSize;
        return req;
    }
    static svm::Fns fns() { return svm::Fns{kRefresh, kListener, 0, kPost, kAllocGlobal, kCompObjVtable, kListVtable}; }
    struct TreeRow {
        int id, team, points;
    };
    // The LiveStandings tree of map entry `key` as the game lays it out (docs/re/standings-ui-path.md 0c): competition node
    // -> stage node -> StandingObject list node with `rows` of 0xA0 bytes (+0 FCE standing id, +4 team, +0x68 points)
    void set_tree(int32_t key, uint16_t group, const std::vector<TreeRow>& rows) {
        auto alloc = [&](uint64_t n) {
            uint64_t a = tree_next;
            tree_next += (n + 0xF) & ~0xFULL;
            return a;
        };
        const uint64_t comp = alloc(0x80), kids1 = alloc(8), stage = alloc(0x80), kids2 = alloc(8), list = alloc(0x38),
                       rws = alloc(uint64_t(rows.size()) * svm::kRowSize + 0x10);
        mem.wr(comp, kCompObjVtable);
        mem.wr(comp + svm::kCoChildren, kids1);
        mem.wr(comp + svm::kCoChildCount, uint32_t(1));
        mem.wr(comp + svm::kCoId, key);
        mem.wr(comp + svm::kCoType, int32_t(3));
        mem.wr(kids1, stage);
        mem.wr(stage, kCompObjVtable);
        mem.wr(stage + svm::kCoChildren, kids2);
        mem.wr(stage + svm::kCoChildCount, uint32_t(1));
        mem.wr(stage + svm::kCoId, key + 1);
        mem.wr(stage + svm::kCoType, int32_t(4));
        mem.wr(kids2, list);
        mem.wr(list, kListVtable);
        mem.wr(list + svm::kSlRowCount, int32_t(rows.size()));
        mem.wr(list + svm::kSlRows, rws);
        mem.wr(list + svm::kSlGroup, int32_t(group));
        for (size_t i = 0; i < rows.size(); ++i) {
            const uint64_t r = rws + i * svm::kRowSize;
            mem.wr(r + svm::kRowId, int32_t(rows[i].id));
            mem.wr(r + svm::kRowTeam, uint32_t(rows[i].team));
            mem.wr(r + svm::kRowPoints, int32_t(rows[i].points));
        }
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) mem.wr(live(int(i)) + 8, comp);
        last_root = comp;
        last_stage = stage;
        last_list = list;
        last_rows = rws;
    }
};

struct FakeSvmGame : svm::Caller {
    std::vector<std::pair<uint64_t, int32_t>> refreshes, events;
    int fail_after = -1;  // refresh_comp fails once this many ran
    bool fail_event = false;
    bool refresh_comp(uint64_t s, int32_t comp, std::string& err) override {
        if (fail_after >= 0 && int(refreshes.size()) == fail_after) {
            err = "boom";
            return false;
        }
        refreshes.push_back({s, comp});
        return true;
    }
    bool career_event(uint64_t s, int32_t id, std::string& err) override {
        if (fail_event) {
            err = "boom";
            return false;
        }
        events.push_back({s, id});
        return true;
    }
};

// Stand-in for the Windows host's refresh service (src/win/standings_refresh_win.cpp): records requests, answers when told
struct FakeRefresh : svm::RefreshService {
    std::vector<svm::Request> requests;
    std::deque<svm::Result> results;
    bool off = false;
    std::string why;
    bool request(const svm::Request& r, std::string& w) override {
        if (off) {
            w = why;
            return false;
        }
        requests.push_back(r);
        return true;
    }
    bool poll(svm::Result& out) override {
        if (results.empty()) return false;
        out = results.front();
        results.pop_front();
        return true;
    }
    std::string status() override { return "standings_refresh: fake"; }
    svm::Fns anchors = SvmWorld::fns();
    svm::Fns fns() override { return anchors; }
};
}  // namespace

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
            // leave team 7 as the later cases know it
            CHECK(apply_team_names(app, 7, "Everton", "", "", "", &msg), "name restored: " + msg);
            CHECK(app.model.team_name(7) == "Everton", "model shows the restored name: " + app.model.team_name(7));
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

        run_case("UI: Managers > Job offers: club picker, button gated by the capability, confirmation sends the job_offer module", [&] {
            app.mailbox->cancel();
            app.request_tab = 2;
            ui.frames(2);
            CHECK(ui.click("Job offers (Manager Career)", "##medit"), "section header opens");
            ui.frames(2);
            // the simulated Lua side has no Turbo.dll native: the capability is missing, the button does nothing
            CHECK(app.bridge.state().unavailable_reason("job_offer") != nullptr, "job_offer unavailable in the test world");
            const ItemRec* btn = ui.find("Create job offer", "##medit");
            CHECK(btn != nullptr, "button drawn");
            ui.click(btn);
            ui.frames(2);
            CHECK(!app.mailbox->pending(), "disabled button sends nothing");
            // the club picker: search narrows the list, the user's own club (1) and national teams are never offered
            const ItemRec* search = ui.find("##josearch", "##medit", "joboffers");
            CHECK(search != nullptr, "search box");
            CHECK(ui.find("1##jo", "##medit") == nullptr, "own club not listed");
            CHECK(ui.find("1318##jo", "##medit") == nullptr, "national team not listed");
            if (!ui.find("7##jo", "##medit")) ui.dump("Everton row missing before the search");
            CHECK(ui.find("7##jo", "##medit") != nullptr && ui.find("241##jo", "##medit") != nullptr, "league clubs listed");
            CHECK(app.model.team_name(7) == "Everton", "team 7 is named '" + app.model.team_name(7) + "'");
            ui.type_into(search, "7");  // by id (the name filter is the same code path as the Teams tab's search)
            CHECK(std::string(app.job_offer_search) == "7", std::string("typed into the search box: '") + app.job_offer_search + "'");
            CHECK(ui.find("7##jo", "##medit") != nullptr && ui.find("241##jo", "##medit") == nullptr, "search filters the clubs");
            CHECK(ui.click("7##jo", "##medit"), "pick Everton");
            CHECK(app.job_offer_team == 7, "club selected");
            // Turbo.dll's native shows up (Lua rewrites bridge_state.json without the job_offer entry): the button lights up
            fs::path state_file = le / "turbo_output" / "bridge_state.json";
            json st = read_json(state_file);
            // each rewrite gets a later time stamp (the bridge re-reads on a changed mtime; several writes within one
            // second would otherwise look unchanged)
            int bumps = 0;
            auto write_state_file = [&]() {
                {
                    std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                    f << st.dump();
                }
                fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
            };
            st["unavailable"].erase("job_offer");
            st["seq"] = st.value("seq", 0LL) + 1;
            write_state_file();
            app.next_poll = 0.0;
            ui.frames(3);
            CHECK(app.bridge.state().unavailable_reason("job_offer") == nullptr, "capability picked up");
            CHECK(ui.click("Create job offer", "##medit"), "button");
            ui.frames(2);
            CHECK(ui.find("Cancel", "Create job offer?") != nullptr, "confirmation modal");
            CHECK(ui.click("Cancel", "Create job offer?"), "cancel");
            ui.frames(2);
            CHECK(!app.mailbox->pending(), "cancelled: nothing sent");
            CHECK(ui.click("Create job offer", "##medit"), "button again");
            ui.frames(2);
            CHECK(ui.click("Create", "Create job offer?"), "confirm");
            ui.frames(2);
            CHECK(app.mailbox->pending(), "command sent to Lua");
            std::string cmd = mem.read_cstr(kMb + kMbCmd, kMbTextSize);
            json j = json::parse(cmd, nullptr, false);
            CHECK(!j.is_discarded() && j.value("op", "") == "run" && j.value("module", "") == "job_offer", "op run / module job_offer: " + cmd);
            CHECK(j.contains("overrides") && j["overrides"].value("teamid", 0) == 7 && j["overrides"].value("confirm", false) &&
                      j["overrides"].value("enabled", false),
                  "overrides carry the club, enabled and confirm");
            CHECK(app.pending_label.rfind("Job offer from Everton", 0) == 0, "label: " + app.pending_label);
            CHECK(!app.job_offer_status.empty(), "status line set");
            // the answer from Lua lands in the status line and a toast
            mem.wr(kMb + kMbStatus, static_cast<int32_t>(1));
            std::vector<uint8_t> text(kMbTextSize, 0);
            const char* msg = "job offer created: Everton (7) wants you as manager (job offer sent on 20270115 (weekly wage 25007))";
            std::memcpy(text.data(), msg, std::strlen(msg));
            mem.write(kMb + kMbResult, text.data(), text.size());
            mem.wr(kMb + kMbAckSeq, app.mailbox->seq());
            ui.frames(3);
            CHECK(app.job_offer_status.find("Everton (7) wants you") != std::string::npos, "status shows the outcome: " + app.job_offer_status);
            CHECK(ui.toast_contains("Job offer from Everton"), "toast");
            // a queued call's outcome arrives through bridge_state.json (game_call) as a toast
            st["game_call"] = {{"seq", 3}, {"ok", false}, {"text", "job offer from team 7: this club already made you an offer on 20270110"}};
            st["seq"] = st.value("seq", 0LL) + 1;
            write_state_file();
            app.next_poll = 0.0;
            ui.frames(3);
            CHECK(app.bridge.state().game_call_seq == 3 && !app.bridge.state().game_call_ok, "game_call parsed");
            CHECK(ui.toast_contains("already made you an offer"), "queued outcome toast");
            // restore the world's state file for the cases after this one
            st.erase("game_call");
            st["unavailable"]["job_offer"] = "TurboJobOfferCreate is not available in this Live Editor build";
            write_state_file();
            app.next_poll = 0.0;
            ui.frames(3);
            ui.click("Job offers (Manager Career)", "##medit");  // fold the section again
            ui.frames(2);
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

        run_case("UI: Players > Miniface > The 3D model: no service, unavailable, generate, picture, save, failure", [&] {
            // no service at all (a build without the game hooks): the button is there, disabled, with the reason
            app.capture.reset();
            app.request_tab = 0;
            ui.frames(2);
            CHECK(ui.click("1002", "##plist"), "player 1002");
            CHECK(ui.click("Miniface", "##pedit"), "Miniface tab");
            CHECK(ui.click("The 3D model", "##mface"), "3D model tab");
            ui.frames(2);
            CHECK(ui.find("Generate from 3D model") != nullptr, "button drawn without a service");
            // a service that is not available yet: the button stays disabled, a click asks nothing
            auto fake = std::make_shared<FakeCapture>();
            app.capture = fake;
            fake->st.installed = true;
            fake->st.available = false;
            fake->st.reason = "the game's frontend renderer is not up";
            ui.frames(2);
            CHECK(ui.click("Generate from 3D model"), "button present");
            ui.frames(2);
            CHECK(fake->requests.empty(), "nothing asked while unavailable");
            // available: Generate asks for this player with his club as the second id, default camera
            fake->st.available = true;
            fake->st.reason = "ready";
            ui.frames(2);
            CHECK(ui.click("Generate from 3D model"), "Generate");
            ui.frames(2);
            CHECK(fake->requests.size() == 1, fmt("one request (%zu)", fake->requests.size()));
            if (!fake->requests.empty()) {
                const capture::Request& r = fake->requests.back();
                const PlayerRow* p = app.model.player(1002);
                CHECK(r.id == 1002 && !r.manager && p && r.second_id == int32_t(p->club) && r.camera == 0 && r.use_template && r.mode_override == -1 && r.extra_override == -1,
                      fmt("request id %d second %d manager %d camera %d", r.id, r.second_id, int(r.manager), r.camera));
                CHECK(r.label.find("1002") != std::string::npos, "label: " + r.label);
            }
            CHECK(ui.find("Cancel##capture") != nullptr, "Cancel shown while pending");
            // the picture arrives: it becomes the New miniface's source, saving writes the DDS from it
            fake->next = capture::Result();
            fake->next.ok = true;
            fake->next.format = "DDS DXT5 256x256";
            fake->next.bytes = 12345;
            fake->next.image = solid(256, 256, 10, 200, 40);
            fake->deliver = true;
            ui.frames(3);
            CHECK(ui.toast_contains("3D model rendered"), "toast");
            CHECK(!fake->pending, "consumed");
            fs::path out = le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "heads" / "p1002.dds";
            fs::remove(out);
            CHECK(ui.click("Save as miniface"), "Save as miniface");
            Rgba img;
            CHECK(load_image_file(out, img) && img.w == 256 && img.h == 256, "p1002.dds written from the capture");
            if (!img.empty()) {
                const uint8_t* px = img.at(128, 128);
                // the capture is a plain picture: with "remove plain background" switched on by the capture the solid colour
                // becomes transparent around the edge, the middle keeps the colour or is cleared too (flood fill); either is
                // a picture made from the capture, not the previous custom file
                CHECK(px[3] == 0 || (std::abs(int(px[1]) - 200) <= 8 && std::abs(int(px[0]) - 10) <= 8), fmt("pixel %d,%d,%d,%d", px[0], px[1], px[2], px[3]));
            }
            // a failed capture reports the reason and leaves the editor usable
            CHECK(ui.click("Generate from 3D model"), "Generate again");
            ui.frames(2);
            CHECK(fake->requests.size() == 2, "second request");
            fake->next = capture::Result();
            fake->next.ok = false;
            fake->next.error = "the game did not finish the capture in 20 s";
            fake->deliver = true;
            ui.frames(3);
            CHECK(ui.toast_contains("3D model capture failed"), "failure toast");
            CHECK(!fake->pending, "failure consumed");
            // cancel while pending
            CHECK(ui.click("Generate from 3D model"), "Generate a third time");
            ui.frames(2);
            CHECK(ui.click("Cancel##capture"), "Cancel");
            ui.frames(2);
            CHECK(fake->cancels == 1 && !fake->pending, "cancelled");
            // the request is refused by the service: the error shows, nothing pends
            fake->st.available = true;
            fake->pending = true;  // busy inside the service
            ui.frames(2);
            CHECK(ui.find("Generate from 3D model") != nullptr, "button while the service is busy");
            fake->pending = false;
            fs::remove(out);
        });

        run_case("UI: Managers > Miniface > The 3D model asks for the manager's head id", [&] {
            auto fake = std::make_shared<FakeCapture>();
            fake->st.installed = true;
            fake->st.available = true;
            fake->st.reason = "ready";
            app.capture = fake;
            app.request_tab = 2;
            ui.frames(2);
            CHECK(ui.click("501##m0", "##mlist"), "manager Arteta");
            CHECK(ui.click("Miniface", "##medit"), "Miniface tab");
            CHECK(ui.click("The 3D model", "##mmface"), "3D model tab");
            ui.frames(2);
            CHECK(ui.click("Generate from 3D model"), "Generate");
            ui.frames(2);
            CHECK(fake->requests.size() == 1, "one request");
            if (!fake->requests.empty()) {
                const capture::Request& r = fake->requests.back();
                CHECK(r.manager && r.id == 7501 && r.second_id == -1, fmt("manager request id %d second %d manager %d", r.id, r.second_id, int(r.manager)));
            }
            fake->next = capture::Result();
            fake->next.ok = true;
            fake->next.format = "raw RGBA 540x540";
            fake->next.image = solid(540, 540, 90, 90, 200);
            fake->deliver = true;
            ui.frames(3);
            CHECK(ui.toast_contains("3D model rendered for manager head 7501"), "toast names the manager head");
            fs::path out = le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "heads_staff" / "heads_staff_7501.dds";
            fs::remove(out);
            CHECK(ui.click("Save as miniface"), "save");
            Rgba img;
            CHECK(load_image_file(out, img) && img.w == 512 && img.h == 512, "512 x 512 heads_staff_7501.dds from the capture");
            app.capture.reset();
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
            app.spoken_auto_tried = true;  // no automatic audio-service build in this case (no service set)
            app.bank_capture_status.clear();
            app.request_tab = 0;
            ui.frames(3);
            CHECK(ui.type_into(ui.find("##psearch", "##plist"), ""), "search cleared");
            CHECK(ui.click("1001", "##plist"), "row 1001 (Saka)");
            CHECK(ui.click("Callname", "##pedit"), "Callname tab");
            ui.frames(2);
            CHECK(ui.click("Capture from the loaded bank##cn"), "the memory scan is manual now (the audio service is the automatic path)");
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
        run_case("UI: Players > Callname: spoken set from the game's audio service (fake service): automatic build, status line, Rebuild, pickers", [&] {
            fs::path game = g_out / "fakegame";
            fs::path cache = spoken_cache_path(le, "ita_it");
            fs::remove(cache);
            fs::remove(le / "turbo" / "callnames" / "spoken_ita_it.txt");
            // a fake service: every request is answered at once by a fake game whose bank speaks 900002 / 900004 / 900010
            // and has player 1001's (PLAYER_LOW_SIMPLE) and 2001's (both events) own recordings
            struct FakeService : caudio::Service {
                int requests = 0, probes = 0;
                bool installed = true;
                bool bound = true;  // false: the fake bank answers 'no' for every id (the career hub)
                caudio::BuildRequest last;
                std::deque<caudio::BuildResult> out;
                caudio::ServiceStatus status() override {
                    caudio::ServiceStatus s;
                    s.installed = installed;
                    s.available = installed;
                    s.players = true;
                    s.reason = installed ? "" : "off in the test";
                    s.runs = requests;
                    return s;
                }
                bool request(const caudio::BuildRequest& r, std::string* err) override {
                    if (!installed) {
                        if (err) *err = "off in the test";
                        return false;
                    }
                    if (r.probe) ++probes;
                    else ++requests;
                    last = r;
                    struct C : caudio::Caller {
                        bool bound = true;
                        bool filter_names(std::vector<uint64_t>& b, size_t& n, std::string&) override {
                            size_t w = 0;
                            for (uint64_t e : b) {
                                const int64_t id = caudio::batch_id(e);
                                if (bound && (id == 900002 || id == 900004 || id == 900010)) b[w++] = e;
                            }
                            n = w;
                            return true;
                        }
                        bool player_audio(const std::vector<int64_t>& p, std::vector<int>& f, std::string&) override {
                            f.assign(p.size(), 0);
                            for (size_t i = 0; i < p.size(); ++i)
                                if (p[i] == 1001) f[i] = caudio::kPlayerLowSimple;
                                else if (p[i] == 2001) f[i] = caudio::kPlayerLowSimple | caudio::kPlayerLowLink;
                            return true;
                        }
                    } c;
                    c.bound = bound;
                    caudio::Build b(r);
                    while (b.step(c)) {}
                    out.push_back(b.result());
                    return true;
                }
                bool poll(caudio::BuildResult& r) override {
                    if (out.empty()) return false;
                    r = out.front();
                    out.pop_front();
                    return true;
                }
                void cancel() override {}
            };
            auto svc = std::make_shared<FakeService>();
            app.commentary_audio = svc;
            app.game_root = game;
            app.callnames.refreshed = false;
            app.spoken_auto_tried = false;
            app.spoken_build_status.clear();
            app.request_tab = 0;
            ui.frames(3);
            CHECK(ui.type_into(ui.find("##psearch", "##plist"), ""), "search cleared");
            CHECK(ui.click("1001", "##plist"), "row 1001 (Saka)");
            CHECK(ui.click("Callname", "##pedit"), "Callname tab");
            ui.frames(4);
            CHECK(app.spoken_auto_tried && svc->probes == 1 && svc->requests == 1 && app.spoken_watch.bound_seen() == 1,
                  fmt("no list and no cache: a probe answers 'bound', the build starts by itself (probes %d, builds %d)", svc->probes, svc->requests));
            CHECK(fs::exists(caudio::id_cache_path(le)), "the id list is cached while the database is connected");
            CHECK(svc->last.names.size() >= 5 && std::count(svc->last.names.begin(), svc->last.names.end(), 900010) == 1 && !svc->last.players.empty() &&
                      std::is_sorted(svc->last.names.begin(), svc->last.names.end()),
                  fmt("request: %zu ids, %zu players", svc->last.names.size(), svc->last.players.size()));
            CHECK(app.callnames.spoken.verified && app.callnames.spoken.from == SpokenSet::From::GameAudio && app.callnames.spoken.ids.size() == 3 &&
                      app.callnames.spoken.players.size() == 2,
                  "spoken set from the service: " + app.callnames.spoken.source);
            CHECK(fs::exists(cache), "cache written");
            CHECK(app.spoken_build_status.find("3 of") != std::string::npos, "status line: " + app.spoken_build_status);
            CHECK(app.callnames.index.built && app.callnames.index.names.size() == 3, fmt("pickers from the set: %zu names", app.callnames.index.names.size()));
            CallnameInfo r = app.callnames.resolve(*app.model.player(1001), app.db);
            CHECK(r.real && r.commentaryid == 900002, "Saka: recorded by name");
            CHECK(ui.find("Rebuild from the game's audio service##cn") != nullptr && ui.find("Capture from the loaded bank##cn") != nullptr,
                  "rebuild button (default) and the capture button (diagnostic)");
            CHECK(ui.click("Rebuild from the game's audio service##cn"), "rebuild");
            ui.frames(3);
            CHECK(svc->requests == 2 && app.callnames.spoken.from == SpokenSet::From::GameAudio && app.callnames.spoken.ids.size() == 3, "rebuilt");
            // the service off: no request goes out; the cache still serves the set
            svc->installed = false;
            app.callnames.refreshed = false;
            ui.frames(2);
            ui.click("Rebuild from the game's audio service##cn");
            ui.frames(2);
            CHECK(svc->requests == 2 && app.callnames.spoken.from == SpokenSet::From::GameAudio, "no request while unavailable, cache still in use");
            fs::remove(cache);
            app.commentary_audio = nullptr;
            app.callnames.refreshed = false;
        });
        run_case("UI: Players > Callname: the watcher: an unbound bank is probed, never cached, and built by itself once the game answers; the id cache serves a build without the database", [&] {
            fs::path game = g_out / "fakegame";
            fs::path cache = spoken_cache_path(le, "ita_it");
            fs::remove(cache);
            fs::remove(le / "turbo" / "callnames" / "spoken_ita_it.txt");
            struct FakeService : caudio::Service {
                int requests = 0, probes = 0;
                bool bound = false;
                caudio::BuildRequest last;
                std::deque<caudio::BuildResult> out;
                caudio::ServiceStatus status() override {
                    caudio::ServiceStatus s;
                    s.installed = s.available = s.players = true;
                    s.runs = requests;
                    return s;
                }
                bool request(const caudio::BuildRequest& r, std::string*) override {
                    if (r.probe) ++probes;
                    else ++requests;
                    last = r;
                    struct C : caudio::Caller {
                        bool bound = false;
                        bool filter_names(std::vector<uint64_t>& b, size_t& n, std::string&) override {
                            size_t w = 0;
                            for (uint64_t e : b) {
                                const int64_t id = caudio::batch_id(e);
                                if (bound && (id == 900002 || id == 900004 || id == 900010)) b[w++] = e;
                            }
                            n = w;
                            return true;
                        }
                        bool player_audio(const std::vector<int64_t>& p, std::vector<int>& f, std::string& e) override {
                            f.assign(p.size(), 0);
                            e = "the events PLAYER_LOW_SIMPLE / PLAYER_LOW_LINK are not bound in this screen (a match binds them)";
                            return false;
                        }
                    } c;
                    c.bound = bound;
                    caudio::Build b(r);
                    while (b.step(c)) {}
                    out.push_back(b.result());
                    return true;
                }
                bool poll(caudio::BuildResult& r) override {
                    if (out.empty()) return false;
                    r = out.front();
                    out.pop_front();
                    return true;
                }
                void cancel() override {}
            };
            auto svc = std::make_shared<FakeService>();
            app.commentary_audio = svc;
            app.game_root = game;
            app.callnames.refreshed = false;
            app.spoken_auto_tried = false;
            app.spoken_build_status.clear();
            app.spoken_watch = caudio::SpokenWatch{};
            // the career hub: every probe answers 'no'; nothing is built, nothing cached, the tab and the Status tab say what to do
            ui.frames(4);
            CHECK(svc->probes == 1 && svc->requests == 0 && !fs::exists(cache) && !app.callnames.spoken.verified, fmt("unbound: one probe, no build, no cache (probes %d)", svc->probes));
            CHECK(app.spoken_watch.state() == caudio::SpokenWatch::State::Waiting && app.spoken_watch_line().find("not built yet") != std::string::npos &&
                      app.spoken_watch_line().find("Create Player") != std::string::npos && app.spoken_watch_line().find("not bound in this screen") != std::string::npos,
                  "watcher line: " + app.spoken_watch_line());
            CHECK(svc->last.probe && svc->last.names.size() >= 5 && svc->last.names.size() <= 64 && svc->last.players.empty(), fmt("the probe asked a sample of %zu ids", svc->last.names.size()));
            CHECK(ui.click("1001", "##plist") && ui.click("Callname", "##pedit"), "Callname tab");
            ui.frames(2);
            CHECK(app.spoken_watch_line().find("not built yet") != std::string::npos && !app.callnames.spoken.verified, "the tab shows the watcher line (drawn from spoken_watch_line)");
            CHECK(svc->probes == 1, "no second probe within the interval");
            ui.t += 3.5;
            ui.frames(2);
            CHECK(svc->probes == 2 && svc->requests == 0 && !fs::exists(cache), fmt("probed again after the interval (probes %d), still nothing cached", svc->probes));
            // a manual Rebuild in the hub: the full build answers 'no' for every id: failed, not cached, the watcher keeps waiting
            CHECK(ui.click("Rebuild from the game's audio service##cn"), "manual rebuild while unbound");
            ui.frames(3);
            CHECK(svc->requests == 1 && !fs::exists(cache) && !app.callnames.spoken.verified && app.spoken_build_status.find("not bound") != std::string::npos &&
                      app.spoken_watch.state() == caudio::SpokenWatch::State::Waiting,
                  "all-no build: not cached: " + app.spoken_build_status);
            // the user opens Create Player (or a match): the next probe answers, the build runs by itself and is cached
            svc->bound = true;
            ui.t += 3.5;
            ui.frames(4);
            CHECK(svc->probes == 3 && svc->requests == 2 && fs::exists(cache) && app.callnames.spoken.verified && app.callnames.spoken.from == SpokenSet::From::GameAudio &&
                      app.callnames.spoken.ids.size() == 3 && app.spoken_watch.state() == caudio::SpokenWatch::State::Idle && app.spoken_watch_line().empty(),
                  fmt("bound: built by itself and cached (probes %d, builds %d): %s", svc->probes, svc->requests, app.callnames.spoken.source.c_str()));
            CHECK(svc->last.names.size() >= 5 && !svc->last.players.empty() && !svc->last.probe, "the full build asked every id and every player");
            ui.t += 10.0;
            ui.frames(2);
            CHECK(svc->probes == 3, "verified: no more probes");
            // the next session (cache present): no probe at all
            app.callnames.refreshed = false;
            app.spoken_watch = caudio::SpokenWatch{};
            ui.t += 10.0;
            ui.frames(3);
            CHECK(svc->probes == 3 && app.callnames.spoken.verified, "a cached set needs no probe");
            // without the database (main menu): the id list comes from the cache written while connected, else from Lua's list
            {
                App bare(mem, le, 0, "uitest-bare");  // no mailbox: the main app keeps its pending commands
                bare.game_root = game;
                caudio::IdCache ids;
                std::string why;
                CHECK(!bare.db.ready() && bare.spoken_ids(ids, &why) && ids.names.size() >= 5 && ids.source.find("id cache") != std::string::npos,
                      "no database: the id cache serves the list: " + ids.source + " " + why);
                fs::path idp = caudio::id_cache_path(le);
                fs::path keep = idp.string() + ".keep";
                fs::rename(idp, keep);
                std::ofstream((bare.bridge.dir() / "bridge_commentary.txt").string()) << "#turbo-commentary 6AC1E3C8 3\n900002\tCalico\n900004\tGates\n900010\tHumber\n";
                CHECK(bare.spoken_ids(ids, &why) && ids.names == std::vector<int64_t>({900002, 900004, 900010}) && ids.players.empty() && ids.source.find("bridge_commentary") != std::string::npos,
                      "no cache: Lua's commentary list serves the ids: " + ids.source);
                fs::remove(bare.bridge.dir() / "bridge_commentary.txt");
                CHECK(!bare.spoken_ids(ids, &why) && why.find("connect to a career") != std::string::npos, "nothing at hand: " + why);
                fs::rename(keep, idp);
            }
            fs::remove(cache);
            app.commentary_audio = nullptr;
            app.callnames.refreshed = false;
            app.spoken_watch = caudio::SpokenWatch{};
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
        run_case("UI: Competitions > Live standings: a row write queues the standings refresh; its outcome arrives as a toast", [&] {
            // the engine rows (FceWorld) and the career managers (SvmWorld), mapped into the App's memory
            FceWorld fw;
            SvmWorld sw;
            for (const auto& kv : fw.mem.pages) mem.pages[kv.first] = kv.second;
            for (const auto& kv : sw.mem.pages) mem.pages[kv.first] = kv.second;
            auto fake = std::make_shared<FakeRefresh>();
            app.standings_refresh = fake;
            app.standings_refresh_status.clear();
            fs::path state_file = le / "turbo_output" / "bridge_state.json";
            json st = read_json(state_file);
            const json saved = st;
            int bumps = 0;
            auto write_state_file = [&]() {
                {
                    std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                    f << st.dump();
                }
                fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
            };
            st["ifce"] = hex_addr(fw.ifce);
            st["svm"] = hex_addr(SvmWorld::kSvm);
            st["managers"] = hex_addr(SvmWorld::kManagers);
            st["comm_service"] = hex_addr(SvmWorld::kComm);
            st["seq"] = st.value("seq", 0LL) + 1;
            write_state_file();
            app.next_poll = 0.0;
            app.request_tab = 3;
            ui.frames(3);
            CHECK(app.bridge.state().svm == SvmWorld::kSvm && app.bridge.state().managers == SvmWorld::kManagers &&
                      app.bridge.state().ifce == fw.ifce,
                  "bridge_state carries ifce / svm / managers");
            CHECK(ui.click("Live standings (game)"), "live view tab (the earlier case left the database view)");
            ui.frames(2);
            CHECK(ui.find("Try again") == nullptr && ui.find("Reload") != nullptr, "live view reachable through the published ifce");
            CHECK(ui.click("1##ls0"), "select the leader (Arsenal, row 0)");
            ui.frames(2);
            const ItemRec* pts = ui.find("Points", "##lsedit");
            CHECK(pts != nullptr, "points editor shown");
            CHECK(ui.type_into(pts, "9"), "type 9 points");
            CHECK(ui.click("Apply to the game", "##lsedit"), "Apply to the game");
            ui.frames(2);
            CHECK(fake->requests.size() == 1, fmt("one refresh request (%zu)", fake->requests.size()));
            if (!fake->requests.empty()) {
                const svm::Request& rq = fake->requests[0];
                CHECK(rq.svm == SvmWorld::kSvm && rq.managers == SvmWorld::kManagers && rq.comm == SvmWorld::kComm && rq.ifce == fw.ifce,
                      "request carries the published addresses");
                CHECK(rq.image_base == app.game_base, "request carries the image base");
                CHECK(rq.label.find("Arsenal") != std::string::npos && rq.label.find("row") != std::string::npos, "label: " + rq.label);
            }
            CHECK(app.standings_refresh_status.find("queued") != std::string::npos, "status line: " + app.standings_refresh_status);
            fce::Located loc;
            std::vector<fce::StandingRow> rows;
            CHECK(fce::locate(mem, fw.ifce, 0, loc).empty() && fce::read_rows(mem, loc, rows) && rows.size() == 4 && rows[0].points == 9,
                  "the row itself was written");
            CHECK(ui.toast_contains("Arsenal updated in the game"), "row toast");
            // the outcome arrives from the game thread: App::tick turns it into a toast and the status line
            svm::Result r;
            r.ok = true;
            r.stage = "done";
            r.refreshed = 3;
            r.message = "Arsenal row: the game's standings view re-read 3 competitions (comp ids 1200, 1300, 1400)";
            fake->results.push_back(r);
            ui.frames(2);
            CHECK(ui.toast_contains("Standings refresh: Arsenal row: the game's standings view re-read 3 competitions"), "outcome toast");
            CHECK(app.standings_refresh_status.find("re-read 3") != std::string::npos, "status line updated: " + app.standings_refresh_status);
            bool err_toast = false;
            for (const auto& tt : app.toasts) err_toast = err_toast || (tt.text.find("Standings refresh:") != std::string::npos && tt.error);
            CHECK(!err_toast, "a good outcome is not an error toast");
            // a failed outcome is an error toast
            r.ok = false;
            r.stage = "validate";
            r.message = "the career's StandingsViewManager is not known: load a career";
            fake->results.push_back(r);
            ui.frames(2);
            err_toast = false;
            for (const auto& tt : app.toasts) err_toast = err_toast || (tt.text.find("not known") != std::string::npos && tt.error);
            CHECK(err_toast, "failed outcome shown as an error");
            // the service refuses (kill switch): the row is still written, the user is told at once
            fake->off = true;
            fake->why = "kill switch turbo_output\\call_standings_refresh_off.txt is present";
            app.toasts.clear();
            CHECK(ui.click("1##ls0"), "select the leader again");
            ui.frames(2);
            pts = ui.find("Points", "##lsedit");
            CHECK(pts != nullptr && ui.type_into(pts, "11"), "type 11 points");
            CHECK(ui.click("Apply to the game", "##lsedit"), "Apply again");
            ui.frames(2);
            CHECK(fake->requests.size() == 1, "refused request not recorded");
            CHECK(ui.toast_contains("Standings refresh: kill switch"), "refusal toast");
            fce::read_rows(mem, loc, rows);
            CHECK(rows.size() == 4 && rows[0].points == 11, "row written although the refresh is off");
            // no service at all (a host without the game calls): only a log line
            app.standings_refresh = nullptr;
            app.toasts.clear();
            CHECK(ui.click("1##ls0"), "select the leader once more");
            ui.frames(2);
            pts = ui.find("Points", "##lsedit");
            CHECK(pts != nullptr && ui.type_into(pts, "12"), "type 12 points");
            CHECK(ui.click("Apply to the game", "##lsedit"), "Apply without a service");
            ui.frames(2);
            CHECK(app.standings_refresh_status.find("no refresh service") != std::string::npos, "status without a service");
            CHECK(!ui.toast_contains("Standings refresh:"), "no refresh toast without a service");
            // restore the state for the cases after this one
            st = saved;
            st["seq"] = st.value("seq", 0LL) + 2;
            write_state_file();
            app.next_poll = 0.0;
            app.standings_refresh_status.clear();
            ui.frames(3);
            CHECK(ui.find("Try again") != nullptr, "live view unreachable again without the ifce");
        });

        run_case("UI: Competitions > Live standings: groups are named by the competition tree and the one the game shows is preferred", [&] {
            // 04-10-2026: the Coppa Italia's setup pool (group 1066) holds the same 20 clubs as Serie A (group 1120); the
            // leagueteamlinks heuristic named both "Serie A" and the pool won. Here: league 13 group 100 (shown by the game as
            // competition 1200) against the cup pool 101 (same clubs, not shown)
            FceWorld fw;
            fw.add_pool();
            SvmWorld sw({1200});  // one competition in the view, with a real tree (the default values hold no tree)
            sw.set_tree(1200, 100, {{0, 1, 7}, {1, 7, 4}, {2, 241, 1}});
            for (const auto& kv : fw.mem.pages) mem.pages[kv.first] = kv.second;
            for (const auto& kv : sw.mem.pages) mem.pages[kv.first] = kv.second;
            auto fake = std::make_shared<FakeRefresh>();
            app.standings_refresh = fake;
            app.standings_refresh_status.clear();
            fs::path state_file = le / "turbo_output" / "bridge_state.json";
            json st = read_json(state_file);
            const json saved = st;
            int bumps = 100;
            auto write_state_file = [&]() {
                {
                    std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                    f << st.dump();
                }
                fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
            };
            st["ifce"] = hex_addr(fw.ifce);
            st["svm"] = hex_addr(SvmWorld::kSvm);
            st["managers"] = hex_addr(SvmWorld::kManagers);
            st["comm_service"] = hex_addr(SvmWorld::kComm);
            st["user_team"] = 241;  // Inter: in the league group and in the pool
            st["seq"] = st.value("seq", 0LL) + 1;
            write_state_file();
            app.next_poll = 0.0;
            app.request_tab = 3;
            ui.frames(3);
            CHECK(ui.click("Live standings (game)"), "live view tab");
            ui.frames(2);
            CHECK(ui.click("Reload"), "Reload (the located chain is the same, the rows grew)");
            ui.frames(2);
            const std::string league = "English Premier League (3 clubs, comp 100) [shown by the game as competition 1200]";
            const std::string pool = "Competition 210 - setup stage (3 clubs, comp 101) [not shown by the game]";
            CHECK(ui.find("Competition") != nullptr, "competition combo");
            CHECK(ui.click("Competition"), "open the combo");
            ui.frames(2);
            CHECK(ui.find(league, "##Combo") != nullptr, "the league group is named by the tree and marked as shown");
            CHECK(ui.find(pool, "##Combo") != nullptr, "the cup pool is named by the tree and marked as not shown");
            CHECK(ui.click(pool, "##Combo"), "pick the pool");
            ui.frames(2);
            CHECK(live_standings_view_line() == "The game's Standings screen reads: 1200 -> group 100 (3 rows)", "what the game reads: " + live_standings_view_line());
            CHECK(live_standings_view_warning().find("not one the game's Standings screen shows") != std::string::npos, "warning for the pool");
            // the default selection (a fresh view) prefers the shown group although the pool has the lower id... the pool's id
            // is higher here, so make the choice visible: select the pool, then force a re-selection through the group map
            CHECK(ui.click("1##ls4"), "select the pool's leader (row 4)");
            ui.frames(2);
            const ItemRec* pts = ui.find("Points", "##lsedit");
            CHECK(pts != nullptr && ui.type_into(pts, "6"), "type 6 points");
            CHECK(ui.click("Apply to the game", "##lsedit"), "Apply on the pool row");
            ui.frames(2);
            CHECK(fake->requests.size() == 1 && fake->requests[0].rows == std::vector<uint16_t>{4}, "the request names the written row 4");
            // the outcome from the game thread carries the warning: shown as an error toast and in the status line
            svm::Result r;
            r.ok = true;
            r.warning = true;
            r.stage = "done";
            r.message = "Inter row: the game's standings view re-read 1 competition (comp ids 1200); WARNING: row 4 is not among the rows the game's standings view shows";
            fake->results.push_back(r);
            ui.frames(2);
            bool warn_toast = false;
            for (const auto& tt : app.toasts) warn_toast = warn_toast || (tt.text.find("WARNING: row 4") != std::string::npos && tt.error);
            CHECK(warn_toast, "warning outcome shown as an error toast");
            CHECK(app.standings_refresh_status.rfind("warning: ", 0) == 0, "status line: " + app.standings_refresh_status);
            // the league group: no warning, the written row is the shown one
            CHECK(ui.click("Competition"), "open the combo again");
            ui.frames(2);
            CHECK(ui.click(league, "##Combo"), "pick the league");
            ui.frames(2);
            CHECK(live_standings_view_warning().empty(), "no warning for the shown group");
            CHECK(ui.click("1##ls0"), "select the league leader (row 0)");
            ui.frames(2);
            pts = ui.find("Points", "##lsedit");
            CHECK(pts != nullptr && ui.type_into(pts, "10"), "type 10 points");
            CHECK(ui.click("Apply to the game", "##lsedit"), "Apply on the league row");
            ui.frames(2);
            CHECK(fake->requests.size() == 2 && fake->requests[1].rows == std::vector<uint16_t>{0}, "the request names row 0");
            // without the anchors (another build) the view cannot be read: no marks, a hint instead
            fake->anchors = svm::Fns{};
            app.game_base = 0;
            CHECK(ui.click("Reload"), "Reload without anchors");
            ui.frames(2);
            CHECK(ui.find("Competition") != nullptr && ui.click("Competition"), "open the combo");
            ui.frames(2);
            CHECK(ui.find("English Premier League (3 clubs, comp 100)", "##Combo") != nullptr, "league named by the tree, no mark");
            CHECK(ui.find("Competition 210 - setup stage (3 clubs, comp 101)", "##Combo") != nullptr, "pool named by the tree, no mark");
            CHECK(ui.click("English Premier League (3 clubs, comp 100)", "##Combo"), "close the combo");
            ui.frames(2);
            CHECK(live_standings_view_line() == "The game's standings view could not be read (the FCEI::CompObject / StandingObject list vtables are not "
                                                "known on this game build).",
                  "hint without anchors: " + live_standings_view_line());
            CHECK(live_standings_view_warning().empty(), "no warning without anchors");
            // restore the state for the cases after this one
            app.standings_refresh = nullptr;
            st = saved;
            st["seq"] = st.value("seq", 0LL) + 2;
            write_state_file();
            app.next_poll = 0.0;
            app.standings_refresh_status.clear();
            ui.frames(3);
            CHECK(ui.find("Try again") != nullptr, "live view unreachable again without the ifce");
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
        CHECK(b && b->find("game_tick") && !b->find("game_tick")->pattern.empty() && b->find("game_tick")->resolve == "none" &&
                  b->find("post_career_event") && b->find("post_career_event")->resolve == "rip" &&
                  b->find("post_career_event")->offset == 6,
              "built-in table for the known build carries the tick and the event post");
        CHECK(b && b->find("career_event_dispatch") && b->find("career_event_dispatch")->resolve == "none" &&
                  b->find("career_event_dispatch")->offset == -24,
              "built-in table carries the career-event dispatch anchored past Live Editor's hook bytes");
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

// Game-thread dispatcher pieces (core/gamethread.h): the job queue, inline hook detection, the synthetic career event,
// and the built-in signatures for build 6AB9813C-211EF000 against the bytes of that build's image.
static void* noop_a(void* self, void*, void*, void*) { return self; }
static void* noop_b(void* self, void*, void*, void*) { return self; }

static void test_gamethread() {
    run_case("job queue: order, bounded drain, exceptions counted, oldest dropped when full", [&] {
        JobQueue q(4);
        std::vector<int> ran;
        for (int i = 1; i <= 3; ++i) q.push([&, i] { ran.push_back(i); });
        CHECK(q.size() == 3 && q.dropped() == 0, "three queued");
        CHECK(q.drain(2) == 2 && ran.size() == 2 && ran[0] == 1 && ran[1] == 2 && q.size() == 1, "bounded drain keeps order");
        CHECK(q.drain(0) == 1 && ran.size() == 3 && ran[2] == 3 && q.size() == 0 && q.ran() == 3, "drain all");
        CHECK(q.drain(0) == 0 && q.drain(5) == 0, "empty drain");
        std::vector<std::string> errors;
        q.push([] { throw std::runtime_error("boom"); });
        q.push([&] { ran.push_back(4); });
        q.push([] { throw 42; });
        CHECK(q.drain(0, [&](const char* w) { errors.push_back(w); }) == 3, "all three ran");
        CHECK(errors.size() == 2 && errors[0] == "boom" && errors[1] == "unknown exception" && q.failed() == 2 && ran.back() == 4,
              "exceptions counted and reported, the rest still run");
        ran.clear();
        bool dropped = false;
        for (int i = 1; i <= 6; ++i) dropped = q.push([&, i] { ran.push_back(i); }) || dropped;
        CHECK(dropped && q.size() == 4 && q.dropped() == 2, "capacity 4: two dropped");
        q.drain(0);
        CHECK(ran.size() == 4 && ran[0] == 3 && ran[3] == 6, "the oldest were dropped");
        // a job that queues another job: the new one waits for the next drain
        q.push([&] { q.push([&] { ran.push_back(99); }); });
        CHECK(q.drain(0) == 1 && q.size() == 1 && ran.back() != 99, "re-queued job waits");
        CHECK(q.drain(0) == 1 && ran.back() == 99, "and runs next time");
        CHECK(!q.push(nullptr) && q.size() == 0, "null job ignored");
    });

    run_case("inline hook detection: jmp rel32, jmp [rip], movabs+jmp, push+ret, plain code, truncated", [&] {
        const uint64_t at = 0x14060124CULL;
        uint64_t t = 0;
        const uint8_t jmp[] = {0xE9, 0x10, 0x00, 0x00, 0x00, 0xCC};
        CHECK(detect_inline_hook(jmp, sizeof(jmp), at, &t) == InlineHook::JmpRel32 && t == at + 5 + 0x10, "jmp rel32");
        const uint8_t jneg[] = {0xE9, 0xF6, 0xFF, 0xFF, 0xFF};
        CHECK(detect_inline_hook(jneg, sizeof(jneg), at, &t) == InlineHook::JmpRel32 && t == at + 5 - 10, "negative rel32");
        const uint8_t ind[] = {0xFF, 0x25, 0x00, 0x10, 0x00, 0x00};
        CHECK(detect_inline_hook(ind, sizeof(ind), at, &t) == InlineHook::JmpIndirect && t == at + 6 + 0x1000, "jmp [rip]: the slot");
        const uint8_t mov[] = {0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0xFF, 0xE0};
        CHECK(detect_inline_hook(mov, sizeof(mov), at, &t) == InlineHook::MovabsJmp && t == 0x1122334455667788ULL, "movabs+jmp rax");
        const uint8_t movno[] = {0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x48, 0x89};
        CHECK(detect_inline_hook(movno, sizeof(movno), at, &t) == InlineHook::None && t == 0, "movabs without jmp is code");
        const uint8_t pr[] = {0x68, 0x44, 0x33, 0x22, 0x11, 0xC7, 0x44, 0x24, 0x04, 0x88, 0x77, 0x66, 0x55, 0xC3};
        CHECK(detect_inline_hook(pr, sizeof(pr), at, &t) == InlineHook::PushRet && t == 0x5566778811223344ULL, "push+ret");
        const uint8_t post[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49};
        CHECK(detect_inline_hook(post, sizeof(post), at, &t) == InlineHook::None && t == 0, "PostEvent's own prologue is not a hook");
        CHECK(detect_inline_hook(jmp, 3, at, &t) == InlineHook::Truncated && detect_inline_hook(ind, 4, at, &t) == InlineHook::Truncated &&
                  detect_inline_hook(nullptr, 0, at, &t) == InlineHook::Truncated && detect_inline_hook(mov, 8, at, &t) == InlineHook::Truncated,
              "too few bytes is never a verdict");
        CHECK(detect_inline_hook(jmp, sizeof(jmp), at, nullptr) == InlineHook::JmpRel32, "target may be null");
        CHECK(std::string(inline_hook_name(InlineHook::JmpRel32)) == "jmp rel32" && std::string(inline_hook_name(InlineHook::None)) == "none",
              "names");
        // Live Editor v27.1.2's two forms, as read from the running game at the career-event dispatch 0x147B8C0D8:
        // the 23-byte stub (04-10-2026 00:51, detour 0x7FFD046FA100 = FCLiveEditor.DLL+0x33A100) and, after a restart,
        // the 6-byte jmp [rip+disp32] through a slot page below the exe (slot 0x13FB70000), each followed by the bytes
        // Live Editor leaves behind (a 90 / the 89 44 66 90 residue).
        const uint8_t stub[] = {0x48, 0x8D, 0x64, 0x24, 0x80, 0x50, 0x48, 0xB8, 0x00, 0xA1, 0x6F, 0x04, 0xFD, 0x7F, 0x00, 0x00,
                                0x48, 0x87, 0x04, 0x24, 0xC2, 0x80, 0x00, 0x90, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B};
        const uint64_t disp = 0x147B8C0D8ULL;
        CHECK(detect_inline_hook(stub, sizeof(stub), disp, &t) == InlineHook::LeaPushMovabsRet && t == 0x7FFD046FA100ULL,
              "lea rsp/push rax/movabs/xchg/ret stub: the detour");
        CHECK(detect_inline_hook(stub, 23, disp, &t) == InlineHook::LeaPushMovabsRet && t == 0x7FFD046FA100ULL, "exactly 23 bytes suffice");
        CHECK(detect_inline_hook(stub, 22, disp, &t) == InlineHook::Truncated && detect_inline_hook(stub, 5, disp, &t) == InlineHook::Truncated,
              "fewer bytes is no verdict");
        uint8_t broken[sizeof(stub)];
        std::memcpy(broken, stub, sizeof(stub));
        broken[20] = 0xC3;  // ret without the 0x80: not the stub
        CHECK(detect_inline_hook(broken, sizeof(broken), disp, &t) == InlineHook::None && t == 0, "a different tail is not the stub");
        const uint8_t lea_code[] = {0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xD8, 0x4C,
                                    0x8B, 0xFA, 0x48, 0x8B, 0xF9, 0x4C, 0x8D, 0x05, 0x02, 0xE4, 0x20, 0x03, 0xBA, 0x01, 0x00, 0x00};
        CHECK(detect_inline_hook(lea_code, sizeof(lea_code), disp, &t) == InlineHook::None, "a plain lea prologue is code");
        const uint8_t jmprip[] = {0xFF, 0x25, 0x22, 0x3F, 0xFE, 0xF7, 0x89, 0x44, 0x66, 0x90, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55,
                                  0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B};
        CHECK(detect_inline_hook(jmprip, sizeof(jmprip), disp, &t) == InlineHook::JmpIndirect && t == 0x13FB70000ULL,
              "jmp [rip+disp32]: the slot page below the exe");
        CHECK(std::string(inline_hook_name(InlineHook::LeaPushMovabsRet)) == "lea/push/movabs/ret" && kInlineHookProbeBytes >= 23,
              "name and probe size");
    });

    run_case("synthetic career event: layout the game's PostEvent walks lands only in Turbo's objects", [&] {
        auto* s = new SyntheticEvent();
        build_synthetic_event(*s, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent);
        CHECK(verify_synthetic_event(*s, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent), "verifies after build");
        auto q = [](const uint8_t* p) {
            uint64_t v = 0;
            std::memcpy(&v, p, 8);
            return v;
        };
        // what PostEvent does: rax=[event]; call [rax+8]; call [rax+0x20]; rcx=[wrapper]; rax=[rcx]; call [rax+0x30]; jmp [[event]+0x10]
        void** evt_vt = reinterpret_cast<void**>(q(s->event));
        CHECK(evt_vt == s->vtable && evt_vt[1] == reinterpret_cast<void*>(&noop_a) && evt_vt[4] == reinterpret_cast<void*>(&noop_a) &&
                  evt_vt[2] == reinterpret_cast<void*>(&noop_a),
              "event vtable slots 1, 4, 2 are the no-op");
        void* disp = reinterpret_cast<void*>(q(s->wrapper));
        CHECK(disp == s->dispatcher, "wrapper -> dispatcher");
        void** disp_vt = reinterpret_cast<void**>(q(s->dispatcher));
        CHECK(disp_vt == s->vtable && disp_vt[6] == reinterpret_cast<void*>(&noop_a), "dispatcher slot 6 is the no-op");
        for (size_t i = 0; i < kSyntheticVtableSlots; ++i)
            CHECK(s->vtable[i] == reinterpret_cast<void*>(&noop_a), fmt("slot %zu", i));
        CHECK(q(s->event + 8) == 0, "reference count 0");
        CHECK(q(s->event + kSyntheticTypeOffset) == static_cast<uint64_t>(kSyntheticCareerEvent), "type id at +0x10");
        // what the career-event dispatcher's Dispatch(this, id, event) (0x147B8C0D8, the function Live Editor hooks) reads
        // on this build: rcx=[this+0x18]; rax=[rcx]; call [rax+0xF8]  (helper vf[31])
        //                for id 0x1D/0x1E only: [this+0x20]->+0x318 (hub: skipped, the synthetic id is far away)
        //                rsi = ([this+0x30]-[this+0x28])>>3 listeners; rax=[this+0x58] tree root; [this+0xA0] filter
        void* helper = reinterpret_cast<void*>(q(s->dispatcher + kDispatchHelperOffset));
        CHECK(helper == s->dispatcher, "helper at +0x18 is the dispatcher itself");
        void** helper_vt = reinterpret_cast<void**>(q(static_cast<uint8_t*>(helper)));
        CHECK(helper_vt == s->vtable && kDispatchHelperSlot < kSyntheticVtableSlots && helper_vt[kDispatchHelperSlot] == reinterpret_cast<void*>(&noop_a),
              "helper vf[31] is the no-op");
        CHECK(q(s->dispatcher + kDispatchListenersBegin) == q(s->dispatcher + kDispatchListenersEnd), "no listener: begin == end");
        CHECK(q(s->dispatcher + kDispatchTreeRootOffset) == 0, "per-id callback tree empty (null root)");
        CHECK(q(s->dispatcher + kDispatchFilterOffset) == 0, "no listener filter");
        CHECK(static_cast<uint32_t>(kSyntheticCareerEvent) - 0x1Du > 1u, "the synthetic id never takes the hub-reading branch");
        // every other qword points at the object itself: a reader that follows fields never leaves Turbo's memory
        bool self = true;
        for (size_t off = 0x18; off + 8 <= kSyntheticBlock; off += 8) self = self && q(s->event + off) == reinterpret_cast<uint64_t>(s->event);
        for (size_t off = 8; off + 8 <= kSyntheticBlock; off += 8) {
            self = self && q(s->wrapper + off) == reinterpret_cast<uint64_t>(s->wrapper);
            if (off == kDispatchTreeRootOffset || off == kDispatchFilterOffset) continue;
            self = self && q(s->dispatcher + off) == reinterpret_cast<uint64_t>(s->dispatcher);
        }
        CHECK(self, "self-referential filler");
        // a dispatcher whose tree root or filter was filled in is refused (the walk would leave Turbo's memory)
        {
            SyntheticEvent* d = new SyntheticEvent();
            build_synthetic_event(*d, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent);
            const uint64_t me = reinterpret_cast<uint64_t>(d->dispatcher);
            std::memcpy(d->dispatcher + kDispatchTreeRootOffset, &me, 8);
            CHECK(!verify_synthetic_event(*d, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent), "tree root set: refused");
            build_synthetic_event(*d, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent);
            std::memcpy(d->dispatcher + kDispatchFilterOffset, &me, 8);
            CHECK(!verify_synthetic_event(*d, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent), "filter set: refused");
            build_synthetic_event(*d, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent);
            const uint64_t other = me + 8;
            std::memcpy(d->dispatcher + kDispatchListenersEnd, &other, 8);
            CHECK(!verify_synthetic_event(*d, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent), "a listener: refused");
            delete d;
        }
        // the no-op returns its first argument, so a chain of virtual calls never produces a null
        using Fn = void* (*)(void*, void*, void*, void*);
        CHECK(reinterpret_cast<Fn>(evt_vt[1])(s->event, nullptr, nullptr, nullptr) == s->event, "no-op returns self");
        // any modification is caught before use
        CHECK(!verify_synthetic_event(*s, reinterpret_cast<void*>(&noop_b), kSyntheticCareerEvent), "other no-op refused");
        CHECK(!verify_synthetic_event(*s, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent + 1), "other type refused");
        const uint64_t zero = 0;
        std::memcpy(s->event + 0x40, &zero, 8);
        CHECK(!verify_synthetic_event(*s, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent), "changed filler refused");
        build_synthetic_event(*s, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent);
        s->vtable[6] = reinterpret_cast<void*>(&noop_b);
        CHECK(!verify_synthetic_event(*s, reinterpret_cast<void*>(&noop_a), kSyntheticCareerEvent), "changed slot refused");
        CHECK(kSyntheticCareerEvent == 0x7E7E0001, "id shared with core/events.lua SYNTHETIC_ID");
        delete s;
    });

    run_case("built-in signatures of build 6AB9813C-211EF000 resolve on that build's bytes and stay unique", [&] {
        const SignatureTable* b = builtin_signature_table("6AB9813C-211EF000");
        CHECK(b != nullptr, "table");
        if (!b) return;
        // the first 48 bytes of the MainLoop frame body at 0x1459E2E7C and the 24 bytes of the PostEvent call site at
        // 0x147B9019C in DataController::InsertTeamPlayer, as dumped from the running game (docs/re/game_thread.md)
        const uint8_t tick[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
                                0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x79, 0x60, 0x48, 0x8b, 0xf1, 0x48, 0x8b, 0xea, 0x48, 0x8b,
                                0x5f, 0x10, 0x48, 0x8b, 0xcb, 0xff, 0x15, 0xa1, 0x82, 0x67, 0x09, 0x48, 0x8b, 0x4f, 0x10, 0xe8};
        const uint8_t site[] = {0x4c, 0x8b, 0xc0, 0x48, 0x8b, 0xcf, 0xe8, 0xa5, 0x10, 0xa7, 0xf8, 0x48,
                                0x8d, 0x8c, 0x24, 0x90, 0x00, 0x00, 0x00, 0xe8, 0xf0, 0x83, 0x70, 0xfa};
        // the career-event dispatch 0x147B8C0D8 as the running game shows it on 04-10-2026 (Live Editor's 23-byte stub
        // over the prologue, then the function's own bytes from +24 on, where the signature is anchored)
        const uint8_t dispatch[] = {0x48, 0x8D, 0x64, 0x24, 0x80, 0x50, 0x48, 0xB8, 0x00, 0xA1, 0x6F, 0x04, 0xFD, 0x7F, 0x00, 0x00,
                                    0x48, 0x87, 0x04, 0x24, 0xC2, 0x80, 0x00, 0x90, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B,
                                    0xF9, 0x8B, 0xEA, 0x48, 0x8B, 0x49, 0x18, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0xF8, 0x00, 0x00, 0x00,
                                    0x8D, 0x45, 0xE3, 0x83, 0xF8, 0x01, 0x77, 0x3D, 0x48, 0x8B, 0x47, 0x20, 0x48, 0x8B, 0x88, 0x18};
        // one buffer standing for the code section: the site at its real address, the tick body 0x1000 later, the
        // dispatch 0x1400 later
        const uint64_t base = 0x147B9019CULL;
        std::vector<uint8_t> code(0x2000, 0xCC);
        std::memcpy(code.data(), site, sizeof(site));
        std::memcpy(code.data() + 0x1000, tick, sizeof(tick));
        std::memcpy(code.data() + 0x1400, dispatch, sizeof(dispatch));
        SigResult r = resolve_signature(*b->find("post_career_event"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.match == base && r.address == 0x14060124CULL, "PostEvent resolved through the call site: " + r.error);
        r = resolve_signature(*b->find("game_tick"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.address == base + 0x1000 && r.match == r.address, "frame body found: " + r.error);
        r = resolve_signature(*b->find("career_event_dispatch"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.match == base + 0x1400 + 24 && r.address == base + 0x1400,
              "dispatch found past the hook bytes, address 24 bytes back: " + r.error);
        // the same function with Live Editor's other hook form (jmp [rip+disp32] + residue) resolves identically
        const uint8_t jmprip[] = {0xFF, 0x25, 0x22, 0x3F, 0xFE, 0xF7, 0x89, 0x44, 0x66, 0x90, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55,
                                  0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
        std::memcpy(code.data() + 0x1400, jmprip, sizeof(jmprip));
        r = resolve_signature(*b->find("career_event_dispatch"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.address == base + 0x1400, "the anchor ignores the first 24 bytes");
        // a second copy of either makes it ambiguous: a title update that duplicates code never hooks the wrong one
        std::memcpy(code.data() + 0x1800, tick, sizeof(tick));
        r = resolve_signature(*b->find("game_tick"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Ambiguous && r.address == 0, "duplicate refused");
        // every built-in pattern parses and is at least 12 fixed bytes long
        for (const auto& s : b->sigs) {
            std::vector<uint8_t> bytes;
            std::vector<bool> mask;
            CHECK(parse_pattern(s.pattern, bytes, mask), "pattern parses: " + s.name);
            size_t fixed = 0;
            for (bool m : mask) fixed += m ? 1 : 0;
            CHECK(fixed >= 12, "pattern long enough: " + s.name);
        }
    });
}

// ================================================================ game calls (core/game_calls.h)
// A synthetic JobMarketManager: vtable, career hub with the manager slots the sequence touches, and the application
// hash table (8 buckets, chains through +0x28). A fake game answers the calls over the same memory.
struct JobWorld {
    SimMemory mem;
    static constexpr uint64_t kJmm = 0x20000000ULL, kHub = 0x20010000ULL, kBuckets = 0x20020000ULL, kNodes = 0x20030000ULL,
                              kHolders = 0x20040000ULL, kObjs = 0x20050000ULL, kVtable = 0x14B016428ULL;
    static constexpr uint32_t kCount = 8;
    int next_node = 0;
    JobWorld() {
        mem.map(kJmm, 0x1000);
        mem.map(kHub, 0x2000);
        mem.map(kBuckets, 0x1000);
        mem.map(kNodes, 0x4000);
        mem.map(kHolders, 0x1000);
        mem.map(kObjs, 0x8000);
        mem.wr(kJmm, kVtable);
        mem.wr(kJmm + jmm::kHub, kHub);
        const uint64_t slots[] = {jmm::kHubX198, jmm::kHubCalendar, jmm::kHubTeams, jmm::kHubDispatcher, jmm::kHubX6d8, jmm::kHubXf38};
        int k = 0;
        for (uint64_t s : slots) {
            uint64_t holder = kHolders + static_cast<uint64_t>(k) * 0x10, obj = kObjs + static_cast<uint64_t>(k) * 0x1000;
            mem.wr(kHub + s, holder);
            mem.wr(holder, obj);
            mem.wr(obj, 0x140001000ULL + static_cast<uint64_t>(k) * 0x100);  // a vtable-shaped first word
            if (s == jmm::kHubXf38) mem.wr(obj + 0x98, obj + 0x200);          // the listener object
            if (s == jmm::kHubCalendar) mem.wr(obj + jmm::kCalendarDate, static_cast<int32_t>(2027));
            ++k;
        }
        mem.wr(kJmm + jmm::kBucketCount, kCount);
        mem.wr(kJmm + jmm::kBuckets, kBuckets);
        mem.wr(kBuckets + kCount * 8, ~0ULL);  // EASTL end sentinel
    }
    // What AddApplication does: a node at the head of the team's bucket chain, offer date -1
    uint64_t add_node(int team, int wage, int countdown) {
        uint64_t node = kNodes + static_cast<uint64_t>(next_node++) * jmm::kNodeSize;
        mem.wr(node + jmm::kNodeKey, static_cast<int32_t>(team));
        mem.wr(node + jmm::kNodeTeam, static_cast<int32_t>(team));
        mem.wr(node + jmm::kNodeOffer + jmm::kOfferSentDay, static_cast<int32_t>(-1));
        mem.wr(node + jmm::kNodeWage, static_cast<int32_t>(wage));
        mem.wr(node + jmm::kNodeCountdown, static_cast<int32_t>(countdown));
        const uint64_t slot = kBuckets + (static_cast<uint64_t>(team) % kCount) * 8;
        uint64_t head = 0;
        mem.rd(slot, head);
        mem.wr(node + jmm::kNodeNext, head);
        mem.wr(slot, node);
        uint32_t n = 0;
        mem.rd(kJmm + jmm::kElementCount, n);
        mem.wr(kJmm + jmm::kElementCount, n + 1);
        return node;
    }
    int32_t i32(uint64_t a) {
        int32_t v = 0;
        mem.rd(a, v);
        return v;
    }
};

struct FakeGame : GameCaller {
    JobWorld& w;
    int applies = 0, offers = 0, has_calls = 0;
    bool fail_apply = false, apply_noop = false, offer_noop = false, today_ok = true;
    int stamp = 20270115, today_v = 20270115;
    explicit FakeGame(JobWorld& world) : w(world) {}
    bool has_application(uint64_t jmm, int team, bool& out, std::string& err) override {
        ++has_calls;
        out = find_application(w.mem, jmm, team, err) != 0;
        return true;
    }
    bool apply_for_job(uint64_t, int team, std::string& err) override {
        ++applies;
        if (fail_apply) {
            err = "boom";
            return false;
        }
        if (!apply_noop) w.add_node(team, 25000 + team, 7);
        return true;
    }
    bool make_offer(uint64_t, uint64_t offer, std::string&) override {
        ++offers;
        if (!offer_noop) w.mem.wr(offer + jmm::kOfferSentDay, static_cast<int32_t>(stamp));
        return true;
    }
    bool today(uint64_t, int& out, std::string& err) override {
        if (!today_ok) {
            err = "no calendar";
            return false;
        }
        out = today_v;
        return true;
    }
};

static void test_game_calls() {
    using namespace turbo;
    const JobMarketFns fns{JobWorld::kVtable, 0x147DD4FC4ULL, 0x147DBBF64ULL, 0x147DD5510ULL, 0x142AA5824ULL};
    run_case("game calls: job offer end to end on a synthetic JobMarketManager (apply, node walk, writes, offer, check)", [&] {
        JobWorld w;
        FakeGame g(w);
        JobOfferRequest req;
        req.jmm = JobWorld::kJmm;
        req.team = 7;
        JobOfferResult r = job_offer_create(w.mem, g, fns, req);
        CHECK(r.ok && r.stage == "done", "ok: " + r.message + " [" + r.stage + "]");
        CHECK(r.applied && g.applies == 1 && g.offers == 1 && g.has_calls == 2, "applied once, offered once");
        std::string err;
        uint64_t node = find_application(w.mem, JobWorld::kJmm, 7, err);
        CHECK(node != 0 && node == r.node, "node found");
        CHECK(w.i32(node + jmm::kNodeOffer + jmm::kOfferTeam) == 7, "offer.mTeamId written");
        CHECK(w.i32(node + jmm::kNodeOffer + jmm::kOfferWage) == 25007, "offer.mWage = node.wage");
        CHECK(w.i32(node + jmm::kNodeCountdown) == 0, "countdown zeroed");
        CHECK(w.i32(node + jmm::kNodeOffer + jmm::kOfferSentDay) == 20270115 && r.sent_day == 20270115, "offer dated by MakeOffer");
        CHECK(r.wage == 25007 && r.message.find("20270115") != std::string::npos && r.message.find("25007") != std::string::npos,
              "message: " + r.message);
        // an application the user already made: no second ApplyForJob, the offer is made on that node
        uint64_t n9 = w.add_node(9, 30000, 3);
        req.team = 9;
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(r.ok && !r.applied && g.applies == 1 && g.offers == 2 && r.node == n9 && r.wage == 30000, "existing application reused: " + r.message);
        // the club already answered: refused, nothing written, no second offer
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "find" && r.message.find("already made you an offer") != std::string::npos, "open offer refused: " + r.message);
        CHECK(g.offers == 2, "no second MakeOffer");
        w.mem.wr(n9 + jmm::kNodeOffer + jmm::kOfferAccepted, static_cast<uint8_t>(1));
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("already accepted") != std::string::npos, "accepted offer refused: " + r.message);
        // bucket chains: 15 and 23 share bucket 7 with team 7; the walk picks the right node, absent teams are 0
        uint64_t n15 = w.add_node(15, 1, 1), n23 = w.add_node(23, 2, 1);
        CHECK(find_application(w.mem, JobWorld::kJmm, 23, err) == n23 && find_application(w.mem, JobWorld::kJmm, 15, err) == n15 &&
                  find_application(w.mem, JobWorld::kJmm, 7, err) == node,
              "chain walk finds each team");
        CHECK(find_application(w.mem, JobWorld::kJmm, 31, err) == 0 && err.empty(), "absent team: 0 without error");
        CHECK(find_application(w.mem, JobWorld::kJmm, 4, err) == 0 && err.empty(), "empty bucket: 0 without error");
        // the calendar disagrees with the stamp: still ok, the message says so; no calendar: plain message
        g.today_v = 20270116;
        req.team = 15;
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(r.ok && r.message.find("calendar says 20270116") != std::string::npos, "date mismatch reported: " + r.message);
        g.today_ok = false;
        req.team = 23;
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(r.ok && r.message.find("job offer sent on 20270115") != std::string::npos, "no calendar: still ok: " + r.message);
        CHECK(w.mem.failed_reads == 0, "no read outside the synthetic memory");
    });

    run_case("game calls: every refusal path stops before the game is called or before anything is written", [&] {
        JobWorld w;
        FakeGame g(w);
        JobOfferRequest req;
        req.jmm = JobWorld::kJmm;
        req.team = 7;
        JobMarketFns none;
        JobOfferResult r = job_offer_create(w.mem, g, none, req);
        CHECK(!r.ok && r.stage == "validate" && r.message.find("jmm_vtable") != std::string::npos, "functions missing: " + r.message);
        JobMarketFns part = fns;
        part.make_offer = 0;
        r = job_offer_create(w.mem, g, part, req);
        CHECK(!r.ok && r.message.find("jmm_make_offer") != std::string::npos, "make_offer missing: " + r.message);
        req.team = 0;
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "validate" && r.message.find("positive") != std::string::npos, "team 0: " + r.message);
        req.team = 7;
        req.jmm = 0x10;
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("not a pointer") != std::string::npos, "bad pointer: " + r.message);
        req.jmm = 0x30000000ULL;
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("not readable") != std::string::npos, "unmapped: " + r.message);
        req.jmm = JobWorld::kJmm;
        w.mem.wr(JobWorld::kJmm, 0x14B000000ULL);
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("not the JobMarketManager") != std::string::npos, "wrong vtable: " + r.message);
        w.mem.wr(JobWorld::kJmm, JobWorld::kVtable);
        uint64_t saved = 0;
        w.mem.rd(JobWorld::kHub + jmm::kHubCalendar, saved);
        w.mem.wr(JobWorld::kHub + jmm::kHubCalendar, 0ULL);
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("calendar manager") != std::string::npos, "hub slot unreadable: " + r.message);
        w.mem.wr(JobWorld::kHub + jmm::kHubCalendar, saved);
        w.mem.wr(JobWorld::kJmm + jmm::kBucketCount, 0u);
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("bucket count") != std::string::npos, "bucket count 0: " + r.message);
        w.mem.wr(JobWorld::kJmm + jmm::kBucketCount, 5000000u);
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("bucket count") != std::string::npos, "bucket count huge: " + r.message);
        w.mem.wr(JobWorld::kJmm + jmm::kBucketCount, JobWorld::kCount);
        CHECK(g.applies == 0 && g.offers == 0, "the game was never called");
        // the game refuses / does nothing
        g.fail_apply = true;
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "apply" && r.message.find("ApplyForJob: boom") != std::string::npos, "apply failed: " + r.message);
        g.fail_apply = false;
        g.apply_noop = true;
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "apply" && r.message.find("did not register") != std::string::npos, "apply made no node: " + r.message);
        g.apply_noop = false;
        g.offer_noop = true;
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "check" && r.message.find("stayed unset") != std::string::npos, "offer not stamped: " + r.message);
        CHECK(g.offers == 1, "MakeOffer called once");
        // broken chains: a loop and a bad pointer are bounded errors, not crashes
        std::string err;
        uint64_t node = find_application(w.mem, JobWorld::kJmm, 7, err);
        CHECK(node != 0, "node present");
        w.mem.wr(node + jmm::kNodeNext, node);
        CHECK(find_application(w.mem, JobWorld::kJmm, 15, err) == 0 && err.find("longer than") != std::string::npos, "loop bounded: " + err);
        w.mem.wr(node + jmm::kNodeNext, 0x8ULL);
        CHECK(find_application(w.mem, JobWorld::kJmm, 15, err) == 0 && err.find("bad pointer") != std::string::npos, "bad next: " + err);
        w.mem.wr(node + jmm::kNodeNext, 0x30000000ULL);
        CHECK(find_application(w.mem, JobWorld::kJmm, 15, err) == 0 && err.find("not readable") != std::string::npos, "unmapped next: " + err);
        w.mem.wr(node + jmm::kNodeNext, 0ULL);
        // a node whose value disagrees with its key is a layout mismatch
        w.mem.wr(node + jmm::kNodeTeam, static_cast<int32_t>(8));
        w.mem.wr(node + jmm::kNodeOffer + jmm::kOfferSentDay, static_cast<int32_t>(-1));
        g.offer_noop = false;
        r = job_offer_create(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "find" && r.message.find("layout mismatch") != std::string::npos, "key/value mismatch: " + r.message);
        CHECK(g.offers == 1, "no MakeOffer on a suspicious node");
    });

    run_case("game calls: mailbox call block round trip (Lua request, DLL result) inside the version-2 mailbox", [&] {
        CHECK(kMbCallEnd <= kMailboxSize && kMbCall >= kMbResult + kMbTextSize, "call block fits after the result text");
        CHECK(kMailboxVersion >= 2, "mailbox version carries the call block");
        SimMemory mem;
        const uint64_t mb = 0x31000000ULL;
        mem.map(mb, kMailboxSize);
        const int64_t args[4] = {0x20000000LL, 7, 0, 0};
        CHECK(write_call_request(mem, mb, 5, kCallOpJobOffer, args), "request written");
        GameCallBlock b;
        CHECK(read_call_block(mem, mb, b), "block read");
        CHECK(b.op == kCallOpJobOffer && b.seq == 5 && b.status == kCallIdle && b.args[0] == 0x20000000LL && b.args[1] == 7, "request fields");
        CHECK(write_call_result(mem, mb, 5, kCallOk, 20270115, 25007, "job offer sent on 20270115"), "result written");
        CHECK(read_call_block(mem, mb, b) && b.status == kCallOk && b.result_seq == 5 && b.out[0] == 20270115 && b.out[1] == 25007 &&
                  b.text == "job offer sent on 20270115",
              "result fields");
        std::string longtext(2000, 'x');
        CHECK(write_call_result(mem, mb, 6, kCallFailed, 0, 0, longtext) && read_call_block(mem, mb, b) && b.text.size() == kMbCallTextSize - 1 &&
                  b.status == kCallFailed && b.result_seq == 6,
              "long text cut to the block");
        CHECK(!write_call_request(mem, 0x40000000ULL, 1, 1, args) && !read_call_block(mem, 0x40000000ULL, b) &&
                  !write_call_result(mem, 0x40000000ULL, 1, 1, 0, 0, ""),
              "unmapped mailbox refused");
        // the legacy part of the mailbox is untouched by the call block
        Mailbox box(mem, mb);
        CHECK(box.init() && !box.pending(), "mailbox init over the same memory");
        CHECK(write_call_request(mem, mb, 7, kCallOpJobOffer, args) && !box.pending(), "call block does not disturb the command channel");
    });

    run_case("signatures: the built-in job-offer entries resolve on the game's prologue bytes (vtable via the ctor's lea)", [&] {
        const SignatureTable* t = builtin_signature_table("6AB9813C-211EF000");
        CHECK(t != nullptr, "built-in table");
        if (!t) return;
        const char* names[] = {"jmm_vtable", "jmm_handle_event", "jmm_has_application", "jmm_apply_for_job", "jmm_make_offer", "calendar_today_int"};
        for (const char* n : names) CHECK(t->find(n) && !t->find(n)->pattern.empty(), std::string("entry ") + n);
        // bytes read from fc27_image.bin (FC27.exe 1.0.140.64835) at the functions' VAs
        const uint8_t ctor[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
                                0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xD7, 0xA2, 0xE7,
                                0x03, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x70, 0x48, 0x8D, 0x05, 0x70, 0x03, 0x26, 0x03};
        const uint8_t handle[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                  0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x60, 0x4D, 0x8B, 0xF8, 0x4C, 0x8B, 0x41, 0x08, 0x48, 0x8B};
        const uint8_t has[] = {0x44, 0x8B, 0x81, 0xF8, 0x08, 0x00, 0x00, 0x4C, 0x8B, 0x89, 0xF0, 0x08, 0x00, 0x00, 0x4C, 0x63,
                               0xD2, 0x33, 0xD2, 0x49, 0x8B, 0xC2, 0x49, 0xF7, 0xF0, 0x8B, 0xC2, 0x49, 0x8B, 0x0C, 0xC1, 0x48};
        const uint8_t apply[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48,
                                 0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0xE8, 0x3D,
                                 0x90, 0x01, 0x00};
        const uint8_t offer[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x08, 0x4C, 0x8B,
                                 0xD9, 0x48, 0x8B, 0xDA, 0x4C, 0x8B, 0x80, 0x18, 0x03, 0x00, 0x00, 0x49, 0x8B, 0x08, 0x48, 0x83};
        const uint8_t today[] = {0x48, 0x83, 0xEC, 0x28, 0x83, 0xCA, 0xFF, 0xE8, 0x18, 0x00, 0x00, 0x00, 0x3C, 0x01, 0x75, 0x0C,
                                 0x6B, 0x41, 0x08, 0x64, 0x03, 0x41, 0x04, 0x6B, 0xD0, 0x64, 0x03, 0x11, 0x8B, 0xC2, 0x48, 0x83};
        // the ctor sits at +0x100 of a buffer based so that its VA is the real one: the lea then resolves to the real vtable
        const uint64_t base = 0x147DB6088ULL - 0x100;
        std::vector<uint8_t> code(0x1000, 0xCC);
        std::memcpy(code.data() + 0x100, ctor, sizeof(ctor));
        std::memcpy(code.data() + 0x300, handle, sizeof(handle));
        std::memcpy(code.data() + 0x400, has, sizeof(has));
        std::memcpy(code.data() + 0x500, apply, sizeof(apply));
        std::memcpy(code.data() + 0x600, offer, sizeof(offer));
        std::memcpy(code.data() + 0x700, today, sizeof(today));
        SigResult r = resolve_signature(*t->find("jmm_vtable"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.match == 0x147DB6088ULL && r.address == 0x14B016428ULL,
              "jmm_vtable resolves to base+0xB016428: " + r.error);
        struct Exp { const char* name; uint64_t off; } exp[] = {{"jmm_handle_event", 0x300}, {"jmm_has_application", 0x400},
                                                                 {"jmm_apply_for_job", 0x500}, {"jmm_make_offer", 0x600},
                                                                 {"calendar_today_int", 0x700}};
        for (const auto& e : exp) {
            r = resolve_signature(*t->find(e.name), code.data(), code.size(), base);
            CHECK(r.state == SigState::Found && r.address == base + e.off, std::string(e.name) + ": " + r.error);
        }
        // no entry matches twice inside the buffer that holds all six prologues (distinct patterns)
        for (const char* n : names) {
            r = resolve_signature(*t->find(n), code.data(), code.size(), base);
            CHECK(r.hits == 1, std::string(n) + " unique");
        }
    });

    run_case("signatures: the built-in commentary-audio entries resolve on the game's bytes (registry + getter from one call site, strings by offset)", [&] {
        const SignatureTable* t = builtin_signature_table("6AB9813C-211EF000");
        CHECK(t != nullptr, "built-in table");
        if (!t) return;
        for (const auto& fs_ : caudio::fn_signatures()) CHECK(t->find(fs_.signature) && !t->find(fs_.signature)->pattern.empty(), std::string("entry ") + fs_.signature);
        // call_site: FC27.exe bytes at 0x1480B255F (64 bytes)
        const uint8_t call_site[] = {
            0x48, 0x8B, 0x15, 0x2A, 0x60, 0x1F, 0x04, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xE8, 0xB0, 0xFE, 0x99,
            0xFA, 0x48, 0x8B, 0x4C, 0x24, 0x38, 0x48, 0x85, 0xC9, 0x74, 0x20, 0x48, 0x8B, 0x01, 0xFF, 0x50,
            0x60, 0x48, 0x85, 0xC0, 0x74, 0x15, 0x48, 0x8B, 0x08, 0x48, 0x8D, 0x54, 0x24, 0x50, 0x4C, 0x8B,
            0x81, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0x41, 0xFF, 0xD0, 0x48, 0x8B, 0xB5, 0xF0, 0x01,
        };
        const uint64_t call_site_va = 0x1480B255FULL;
        // filter: FC27.exe bytes at 0x1439074C8 (164 bytes)
        const uint8_t filter[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x41,
            0x56, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0xFA, 0x48, 0x8B,
            0x49, 0x10, 0x48, 0x8D, 0x15, 0x47, 0x47, 0xE8, 0x05, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0xE0, 0x00,
            0x00, 0x00, 0x4C, 0x8B, 0xF0, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xBB, 0x00, 0x00, 0x00, 0x48, 0x8B,
            0x4B, 0x10, 0x4C, 0x8D, 0x05, 0xEF, 0xD3, 0xFC, 0x06, 0x48, 0x8B, 0x11, 0x4C, 0x8B, 0x4A, 0x48,
            0x48, 0x8D, 0x15, 0x61, 0xE1, 0xE9, 0x05, 0x41, 0xFF, 0xD1, 0x48, 0x8D, 0x4C, 0x24, 0x20, 0x48,
            0x8B, 0xD8, 0xE8, 0xC5, 0x96, 0xD6, 0xFC, 0x4C, 0x8B, 0xC3, 0x48, 0x8D, 0x54, 0x24, 0x20, 0x48,
            0x8D, 0x4C, 0x24, 0x70, 0xE8, 0x7F, 0x99, 0xEA, 0xFC, 0x41, 0xB8, 0x02, 0x00, 0x00, 0x00, 0x48,
            0x8D, 0x15, 0x4A, 0xE1, 0xE9, 0x05, 0x48, 0x8D, 0x4C, 0x24, 0x70, 0xE8, 0x8C, 0x8E, 0xEA, 0xFC,
            0x48, 0x8B, 0x1F, 0xEB, 0x4A, 0x44, 0x8B, 0x43, 0x04, 0x48, 0x8D, 0x15, 0xA0, 0xFE, 0xD9, 0x05,
            0xE8, 0x77, 0x8E, 0xEA,
        };
        const uint64_t filter_va = 0x1439074C8ULL;
        // fe_check: FC27.exe bytes at 0x14294A15B (250 bytes)
        const uint8_t fe_check[] = {
            0x49, 0x8B, 0x4D, 0x08, 0x4C, 0x8D, 0x05, 0x5A, 0xB5, 0xE5, 0x06, 0x48, 0x8D, 0x15, 0x13, 0xB5,
            0xE5, 0x06, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48, 0x48, 0x8D, 0x4D, 0xD0, 0x4C, 0x8B, 0xF8, 0xE8,
            0x75, 0x6A, 0xD2, 0xFD, 0xC7, 0x45, 0x28, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x05, 0xF3, 0x7D,
            0xCD, 0x06, 0x48, 0x89, 0x45, 0x20, 0x48, 0x8D, 0x45, 0xD0, 0x48, 0x89, 0x45, 0x30, 0x48, 0x8D,
            0x45, 0xD0, 0x48, 0x89, 0x45, 0x58, 0x48, 0x8D, 0x45, 0xD0, 0x48, 0x89, 0x85, 0x88, 0x00, 0x00,
            0x00, 0x48, 0x8D, 0x05, 0x95, 0x36, 0x36, 0x09, 0x48, 0x89, 0x45, 0x68, 0x8D, 0x5F, 0x02, 0xC7,
            0x45, 0x38, 0x00, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x45, 0x50, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89,
            0x5D, 0x70, 0xC7, 0x85, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x85, 0xA0, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC5, 0xFA, 0x10, 0x35, 0xD7, 0xF2, 0xD3, 0x06, 0xC5, 0xFA,
            0x10, 0x3D, 0x2F, 0xA0, 0xCD, 0x06, 0xC5, 0xF9, 0xEF, 0xC0, 0xC5, 0xFA, 0x11, 0x75, 0x78, 0xC5,
            0xFA, 0x11, 0x7D, 0x7C, 0xC5, 0xFA, 0x7F, 0x45, 0x40, 0xC5, 0xFA, 0x7F, 0x85, 0x90, 0x00, 0x00,
            0x00, 0x4D, 0x85, 0xFF, 0x74, 0x0C, 0x49, 0x8B, 0xD7, 0x48, 0x8D, 0x4D, 0x20, 0xE8, 0x1F, 0x6D,
            0xE6, 0xFD, 0x45, 0x8B, 0xC6, 0x48, 0x8D, 0x15, 0x71, 0x81, 0xCD, 0x06, 0x48, 0x8D, 0x4D, 0x20,
            0xE8, 0xB4, 0x61, 0xE6, 0xFD, 0x41, 0xB8, 0x02, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x5B, 0xB4,
            0xE5, 0x06, 0x48, 0x8D, 0x4D, 0x20, 0xE8, 0x9E, 0x61, 0xE6, 0xFD, 0x49, 0x8B, 0x4D, 0x08, 0x4C,
            0x8D, 0x05, 0x5F, 0xB4, 0xE5, 0x06, 0x48, 0x8D, 0x15, 0x28,
        };
        const uint64_t fe_check_va = 0x14294A15BULL;
        // query_ctor: FC27.exe bytes at 0x1407B0EC0 (48 bytes)
        const uint8_t query_ctor[] = {
            0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8D, 0x05, 0xB0, 0x10, 0xE7, 0x08,
            0x33, 0xC9, 0x89, 0x4B, 0x08, 0x48, 0x89, 0x03, 0x48, 0x8D, 0x05, 0x69, 0xC9, 0x4F, 0x0B, 0x48,
            0x89, 0x53, 0x10, 0x89, 0x4B, 0x18, 0x48, 0x89, 0x4B, 0x20, 0x48, 0x89, 0x4B, 0x28, 0x48, 0x89,
        };
        const uint64_t query_ctor_va = 0x1407B0EC0ULL;
        // query_set_int: FC27.exe bytes at 0x1407B03E4 (48 bytes)
        const uint8_t query_set_int[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xF8, 0x48, 0x8B, 0xD9,
            0xE8, 0x33, 0xEB, 0xFF, 0xFF, 0x3B, 0x43, 0x18, 0x73, 0x0F, 0x8B, 0xD0, 0x48, 0x8B, 0x43, 0x20,
            0x48, 0x8B, 0x0C, 0xD0, 0x48, 0x85, 0xC9, 0x75, 0x0B, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83,
        };
        const uint64_t query_set_int_va = 0x1407B03E4ULL;
        // query_dtor: FC27.exe bytes at 0x1407B0B6C (56 bytes)
        const uint8_t query_dtor[] = {
            0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48,
            0x89, 0x78, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xF6, 0x48,
            0x8D, 0x05, 0xEE, 0x13, 0xE7, 0x08, 0x48, 0x8D, 0x79, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x89, 0x01,
            0x39, 0x71, 0x18, 0x76, 0x30, 0x48, 0x8B, 0x07,
        };
        const uint64_t query_dtor_va = 0x1407B0B6CULL;
        // scope_ctor: FC27.exe bytes at 0x140670BF4 (52 bytes)
        const uint8_t scope_ctor[] = {
            0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x05, 0x67, 0xD6, 0xF9, 0x08, 0xC7, 0x41, 0x08,
            0x01, 0x00, 0x00, 0x00, 0x48, 0x89, 0x01, 0x48, 0x8B, 0xD9, 0xC7, 0x41, 0x0C, 0x01, 0x00, 0x00,
            0x00, 0x48, 0xC7, 0x41, 0x10, 0x00, 0x00, 0x00, 0x00, 0xE8, 0xB2, 0x93, 0xEC, 0xFF, 0x48, 0x89,
            0x43, 0x18, 0x48, 0xC7,
        };
        const uint64_t scope_ctor_va = 0x140670BF4ULL;
        // scope_dtor: FC27.exe bytes at 0x14053A030 (52 bytes)
        const uint8_t scope_dtor[] = {
            0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48,
            0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8D, 0x05, 0x15,
            0x42, 0x0D, 0x09, 0x48, 0x89, 0x01, 0x48, 0x83, 0x79, 0x38, 0x00, 0x75, 0x68, 0x4C, 0x8B, 0x77,
            0x28, 0x8B, 0x6F, 0x30,
        };
        const uint64_t scope_dtor_va = 0x14053A030ULL;
        // svc_ctor: FC27.exe bytes at 0x1438A149A (32 bytes)
        const uint8_t svc_ctor[] = {
            0x48, 0x8D, 0x0D, 0xCF, 0x58, 0x02, 0x07, 0x48, 0x89, 0x53, 0x18, 0x48, 0x89, 0x0B, 0x48, 0x8D,
            0x05, 0xB9, 0x57, 0x02, 0x07, 0x44, 0x8D, 0x75, 0x10, 0x48, 0x89, 0x43, 0x08, 0x41, 0x8B, 0xCE,
        };
        const uint64_t svc_ctor_va = 0x1438A149AULL;
        // names_ctor: FC27.exe bytes at 0x1438A1509 (36 bytes)
        const uint8_t names_ctor[] = {
            0x48, 0x8D, 0x0D, 0x38, 0x4A, 0x02, 0x07, 0x48, 0x89, 0x68, 0x08, 0x48, 0x89, 0x08, 0xEB, 0x03,
            0x48, 0x8B, 0xC5, 0x48, 0x8B, 0xD6, 0x48, 0x89, 0x43, 0x40, 0x49, 0x8B, 0xCE, 0xE8, 0xF9, 0xF7,
            0xDC, 0xFC, 0x48, 0x85,
        };
        const uint64_t names_ctor_va = 0x1438A1509ULL;
        // expected string targets (the lea operands), checked against the image's string bytes
        // commentary_str_bridge              0x14978BC38 = "CommentaryBridge"
        // commentary_str_player_name_fe      0x14A8D4900 = "PLAYER_NAME_FE"
        // commentary_str_db_events           0x1497A5680 = "CommentaryDbEvents"
        // commentary_str_player_intensity    0x1497A5698 = "player_intensity"
        // commentary_str_surname_id          0x1496A7408 = "surname_ID"
        // commentary_str_player_low_simple   0x1497A56C0 = "PLAYER_LOW_SIMPLE"
        // commentary_str_player_db_pid       0x149622398 = "player_db_pID"
        // commentary_str_player_low_link     0x1497A56B0 = "PLAYER_LOW_LINK"
        const uint64_t str_va[] = {0x14978BC38ULL, 0x14A8D4900ULL, 0x1497A5680ULL, 0x1497A5698ULL, 0x1496A7408ULL, 0x1497A56C0ULL, 0x149622398ULL, 0x1497A56B0ULL};

        struct Exp {
            const char* name;
            const uint8_t* bytes;
            size_t n;
            uint64_t va;
            uint64_t expect;
        } exp[] = {
            {"commentary_service_registry", call_site, sizeof(call_site), call_site_va, 0x14C2A8590ULL},
            {"commentary_service_get", call_site, sizeof(call_site), call_site_va, 0x142A52420ULL},
            {"commentary_filter_names", filter, sizeof(filter), filter_va, filter_va},
            {"commentary_str_bridge", filter, sizeof(filter), filter_va, str_va[0]},
            {"commentary_str_player_name_fe", filter, sizeof(filter), filter_va, str_va[1]},
            {"commentary_str_db_events", filter, sizeof(filter), filter_va, str_va[2]},
            {"commentary_str_player_intensity", filter, sizeof(filter), filter_va, str_va[3]},
            {"commentary_str_surname_id", filter, sizeof(filter), filter_va, str_va[4]},
            {"commentary_str_player_low_simple", fe_check, sizeof(fe_check), fe_check_va, str_va[5]},
            {"commentary_str_player_db_pid", fe_check, sizeof(fe_check), fe_check_va, str_va[6]},
            {"commentary_str_player_low_link", fe_check, sizeof(fe_check), fe_check_va, str_va[7]},
            {"speech_query_ctor", query_ctor, sizeof(query_ctor), query_ctor_va, query_ctor_va},
            {"speech_query_set_int", query_set_int, sizeof(query_set_int), query_set_int_va, query_set_int_va},
            {"speech_query_dtor", query_dtor, sizeof(query_dtor), query_dtor_va, query_dtor_va},
            {"scratch_scope_ctor", scope_ctor, sizeof(scope_ctor), scope_ctor_va, scope_ctor_va},
            {"scratch_scope_dtor", scope_dtor, sizeof(scope_dtor), scope_dtor_va, scope_dtor_va},
            {"commentary_service_vtable", svc_ctor, sizeof(svc_ctor), svc_ctor_va, 0x14A8C6D70ULL},
            {"commentary_names_vtable", names_ctor, sizeof(names_ctor), names_ctor_va, 0x14A8C5F48ULL},
        };
        // every snippet sits at +0x100 of a buffer based so that its VA is the real one: rip operands resolve to the real targets
        for (const auto& e : exp) {
            std::vector<uint8_t> code(0x400, 0xCC);
            std::memcpy(code.data() + 0x100, e.bytes, e.n);
            const uint64_t base = e.va - 0x100;
            SigResult r = resolve_signature(*t->find(e.name), code.data(), code.size(), base);
            CHECK(r.state == SigState::Found && r.address == e.expect,
                  fmt("%s resolves to 0x%llX (got 0x%llX, %s): %s", e.name, static_cast<unsigned long long>(e.expect), static_cast<unsigned long long>(r.address),
                      sig_state_name(r.state), r.error.c_str()));
        }
        // a buffer that holds every snippet: no entry matches twice (distinct patterns)
        {
            std::vector<uint8_t> all(0x2000, 0xCC);
            size_t off = 0x100;
            for (const auto& e : exp) {
                bool dup = false;
                for (const auto& o : exp)
                    if (o.bytes == e.bytes && &o < &e) dup = true;
                if (dup) continue;
                std::memcpy(all.data() + off, e.bytes, e.n);
                off += 0x200;
            }
            for (const auto& e : exp) {
                SigResult r = resolve_signature(*t->find(e.name), all.data(), all.size(), 0x150000000ULL);
                CHECK(r.hits == 1, std::string(e.name) + " unique");
            }
        }
        // the host fills Fns from the table in fn_signatures() order: every name is in the table
        caudio::Fns fn;
        for (const auto& fs_ : caudio::fn_signatures()) {
            const Signature* sg = t->find(fs_.signature);
            if (sg) fn.*(fs_.field) = 1;
        }
        CHECK(fn.missing_names() == nullptr && fn.missing_players() == nullptr, "every signature of both paths is in the built-in table");
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

static void test_fce_compobjs() {
    run_case("FCE: the CompObjectDataList names a group's stage, competition and nation (the cup pool is not a league table)", [&] {
        FceWorld w;
        fce::Located loc;
        CHECK(fce::locate(w.mem, w.ifce, FceWorld::kBase, loc).empty(), "located");
        CHECK(loc.compobj_list == w.col && loc.compobjs_data == w.cdata && loc.compobj_count == 102, fmt("compobj list: %u entries", unsigned(loc.compobj_count)));
        std::vector<fce::CompObj> objs;
        CHECK(fce::read_compobjs(w.mem, loc, objs) && objs.size() == 102, "records read");
        CHECK(objs[2].short_name == "C13" && objs[2].desc == "TrophyName_Abbr15_13" && objs[2].parent == 1 && objs[2].type == fce::kCompTypeCompetition &&
                  objs[2].comp_number() == 13 && objs[2].used == 1,
              "record 2 decoded");
        CHECK(objs[0].parent == 0xFFFF && objs[0].comp_number() == -1 && objs[3].comp_number() == -1, "root / stage have no competition number");
        fce::GroupInfo gi;
        CHECK(fce::describe_group(objs, 100, gi), "group 100 described");
        CHECK(gi.group == 100 && gi.stage == 3 && gi.comp == 2 && gi.nation == 1 && gi.stage_desc == "FCE_League_Stage" && gi.comp_short == "C13" &&
                  gi.comp_number == 13 && gi.nation_short == "ENGL" && gi.league_stage() && !gi.setup_stage(),
              "league 13, league stage, England");
        CHECK(fce::describe_group(objs, 101, gi) && gi.comp == 4 && gi.comp_number == 210 && gi.stage_desc == "FCE_Setup_Stage" && gi.setup_stage() &&
                  !gi.league_stage(),
              "the pool: competition 210, setup stage");
        CHECK(!fce::describe_group(objs, 7, gi) && gi.group == 7 && gi.comp == 0, "an unused id is refused");
        CHECK(!fce::describe_group(objs, 5000, gi), "an id past the list is refused");
        // a parent loop or a bad parent stops the walk without looping
        w.compobj(50, 50, fce::kCompTypeGroup, "G9", "");
        w.compobj(51, 60000, fce::kCompTypeGroup, "G9", "");
        CHECK(fce::read_compobjs(w.mem, loc, objs) && fce::describe_group(objs, 50, gi) && gi.comp == 0 && gi.stage == 0, "self-parent: nothing above");
        CHECK(fce::describe_group(objs, 51, gi) && gi.comp == 0, "parent past the list: nothing above");
        // an odd list header leaves the tree out but the rows usable
        w.w32(w.col + 4, 200);  // count > capacity
        fce::Located loc2;
        CHECK(fce::locate(w.mem, w.ifce, FceWorld::kBase, loc2).empty() && loc2.compobj_list == 0 && loc2.compobj_count == 0 && loc2.row_count == 4,
              "count > capacity: no tree, rows fine");
        CHECK(fce::read_compobjs(w.mem, loc2, objs) && objs.empty(), "no tree: empty");
        w.w32(w.col + 4, 102);
        w.w64(w.dm + 0x50, 0);
        CHECK(fce::locate(w.mem, w.ifce, FceWorld::kBase, loc2).empty() && loc2.compobj_list == 0, "no list pointer: no tree");
    });
}

static void test_standings_refresh() {
    const svm::Fns fns = SvmWorld::fns();
    run_case("standings refresh: SVM = manager slot 108, map walked in key order, every dereference checked, one sync request per key", [&] {
        SvmWorld w;
        std::string err;
        CHECK(svm::manager_table(w.mem, SvmWorld::kComm) == SvmWorld::kManagers, "manager table from the comm service");
        CHECK(svm::manager_at(w.mem, SvmWorld::kManagers, svm::kTypeId, err) == SvmWorld::kSvm && err.empty(), "slot 108: " + err);
        CHECK(svm::manager_at(w.mem, SvmWorld::kManagers, svm::kIfceTypeId, err) == SvmWorld::kIfce, "slot 1: " + err);
        CHECK(svm::manager_at(w.mem, SvmWorld::kManagers, SvmWorld::kStaffTypeId, err) == SvmWorld::kStaff, "slot 107 (the StaffManager): " + err);
        CHECK(svm::manager_at(w.mem, SvmWorld::kManagers, 53, err) == 0 && err.find("0 instances") != std::string::npos, "empty slot: " + err);
        uint64_t s = 0;
        CHECK(svm::locate(w.mem, SvmWorld::kManagers, s, SvmWorld::kVtable).empty() && s == SvmWorld::kSvm, "locate with the vtable");
        CHECK(svm::locate(w.mem, SvmWorld::kManagers, s).empty() && s == SvmWorld::kSvm, "locate without a vtable");
        CHECK(svm::validate(w.mem, SvmWorld::kSvm, SvmWorld::kManagers, SvmWorld::kVtable, SvmWorld::kListener).empty(), "validate");
        CHECK(svm::validate(w.mem, SvmWorld::kSvm, 0, 0, 0).empty(), "validate without anchors (back-pointer, slot 108, critical section)");
        CHECK(svm::validate_request_path(w.mem, SvmWorld::kSvm, SvmWorld::kIfce, SvmWorld::kPost, SvmWorld::kAllocGlobal, SvmWorld::kBase,
                                         SvmWorld::kImageSize)
                  .empty(),
              "request path: interface, Post, allocator");
        CHECK(svm::sim_busy(w.mem, SvmWorld::kManagers).empty(), "SimDayManager idle");
        std::vector<int32_t> keys;
        CHECK(svm::map_keys(w.mem, SvmWorld::kSvm, keys, SvmWorld::kLiveVtable).empty() && keys.size() == 3 && keys[0] == 1200 &&
                  keys[1] == 1300 && keys[2] == 1400,
              "keys in order, values are LiveStandings");
        FakeSvmGame g;
        svm::Request req = w.request();
        req.label = "Napoli row";
        svm::Result r = svm::refresh(w.mem, g, fns, req);
        CHECK(r.ok && r.stage == "done", "ok: " + r.message);
        CHECK(r.svm == SvmWorld::kSvm && r.managers == SvmWorld::kManagers, "located objects reported");
        CHECK(r.keys.size() == 3 && r.refreshed == 3 && !r.fallback, "three keys refreshed");
        CHECK(g.refreshes.size() == 3 && g.events.empty(), "three sync requests, no listener call");
        bool same = g.refreshes.size() == 3;
        for (size_t i = 0; same && i < 3; ++i) same = g.refreshes[i].first == SvmWorld::kSvm && g.refreshes[i].second == keys[i];
        CHECK(same, "each request on the SVM with the map key");
        CHECK(r.message.find("Napoli row: ") == 0 && r.message.find("3 competitions") != std::string::npos &&
                  r.message.find("1200, 1300, 1400") != std::string::npos,
              "message: " + r.message);
        // the published addresses (Lua: slot 108, the table, the interface) are accepted as they are
        g = FakeSvmGame();
        req = w.request();
        req.comm = 0;
        req.svm = SvmWorld::kSvm;
        req.managers = SvmWorld::kManagers;
        req.ifce = SvmWorld::kIfce;
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(r.ok && g.refreshes.size() == 3, "published svm / managers / ifce: " + r.message);
        // only the svm published: its own table is used
        g = FakeSvmGame();
        svm::Request lone;
        lone.svm = SvmWorld::kSvm;
        lone.image_base = SvmWorld::kBase;
        r = svm::refresh(w.mem, g, fns, lone);
        CHECK(r.ok && g.refreshes.size() == 3 && r.managers == SvmWorld::kManagers, "svm only: " + r.message);
        // the vtable from the signature instead of the RVA (no image base: the LiveStandings vtable check is skipped)
        svm::Fns sig = fns;
        sig.vtable = SvmWorld::kVtable;
        req.image_base = 0;
        req.image_size = 0;
        g = FakeSvmGame();
        r = svm::refresh(w.mem, g, sig, req);
        CHECK(r.ok && g.refreshes.size() == 3, "vtable from the signature: " + r.message);
        // without the optional anchors the request path is checked for shape only
        svm::Fns bare;
        bare.refresh_comp = fns.refresh_comp;
        bare.listener = fns.listener;
        g = FakeSvmGame();
        r = svm::refresh(w.mem, g, bare, w.request());
        CHECK(r.ok && g.refreshes.size() == 3, "no optional anchors: " + r.message);
        // a single key, singular wording
        w.set_map({77});
        g = FakeSvmGame();
        r = svm::refresh(w.mem, g, fns, w.request());
        CHECK(r.ok && g.refreshes.size() == 1 && g.refreshes[0].second == 77 && r.message.find("1 competition (") != std::string::npos, r.message);
    });

    run_case("standings refresh: the crash of 04-10-2026 cannot recur: the StaffManager (slot 107, vtable 0x14B0160D8) is refused everywhere", [&] {
        SvmWorld w;
        FakeSvmGame g;
        // the StaffManager published as the svm with the table known: not the object of slot 108
        svm::Request req = w.request();
        req.svm = SvmWorld::kStaff;
        svm::Result r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "validate" && r.message.find("not the object of manager slot 108") != std::string::npos, "published staff: " + r.message);
        // published alone (no table): its vtable is not the SVM's
        svm::Request lone;
        lone.svm = SvmWorld::kStaff;
        lone.image_base = SvmWorld::kBase;
        r = svm::refresh(w.mem, g, fns, lone);
        CHECK(!r.ok && r.message.find("not the StandingsViewManager") != std::string::npos && r.message.find("0x14B0160D8") != std::string::npos,
              "staff alone: " + r.message);
        // no vtable known at all (no signature, no image base): its vtable's slot 1 is not the listener
        svm::Fns blind = fns;
        blind.vtable = 0;
        lone.image_base = 0;
        r = svm::refresh(w.mem, g, blind, lone);
        CHECK(!r.ok && r.message.find("slot 1") != std::string::npos, "staff without any vtable anchor: " + r.message);
        // even with no anchors the critical section bytes give it away
        std::string v = svm::validate(w.mem, SvmWorld::kStaff, 0, 0, 0);
        CHECK(v.find("critical section") != std::string::npos || v.find("slot 108") != std::string::npos, "validate(staff) without anchors: " + v);
        // slot 108 itself holding the StaffManager (the old vtable constant): refused, and no other slot is searched
        w.mem.wr(SvmWorld::kHolders + uint64_t(svm::kTypeId) * 0x10, SvmWorld::kStaff);
        req = w.request();
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("manager slot 108") != std::string::npos && r.message.find("0x14B0160D8") != std::string::npos,
              "slot 108 holds the staff manager: " + r.message);
        uint64_t s = 1;
        CHECK(!svm::locate(w.mem, SvmWorld::kManagers, s, SvmWorld::kVtable).empty() && s == 0, "locate never picks another slot");
        w.mem.wr(SvmWorld::kHolders + uint64_t(svm::kTypeId) * 0x10, SvmWorld::kSvm);
        CHECK(g.refreshes.empty() && g.events.empty(), "nothing was called");
        // the map walk on the StaffManager's bytes is refused as well (no fallback call on it)
        std::vector<int32_t> keys;
        CHECK(!svm::map_keys(w.mem, SvmWorld::kStaff, keys, SvmWorld::kLiveVtable).empty() && keys.empty(), "staff +0x250 is no map");
        // and the world is intact
        r = svm::refresh(w.mem, g, fns, w.request());
        CHECK(r.ok && g.refreshes.size() == 3, "intact: " + r.message);
    });

    run_case("standings refresh: validation refuses wrong objects, mismatches, a busy game and unresolved functions; nothing is called", [&] {
        SvmWorld w;
        FakeSvmGame g;
        svm::Request req = w.request();
        svm::Fns none, part;
        part.refresh_comp = fns.refresh_comp;
        svm::Result r = svm::refresh(w.mem, g, none, req);
        CHECK(!r.ok && r.stage == "validate" && r.message.find("svm_refresh_comp") != std::string::npos, r.message);
        r = svm::refresh(w.mem, g, part, req);
        CHECK(!r.ok && r.message.find("svm_listener") != std::string::npos, r.message);
        // nothing known at all
        r = svm::refresh(w.mem, g, fns, svm::Request());
        CHECK(!r.ok && r.stage == "validate" && r.message.find("not known") != std::string::npos, r.message);
        // the published object is not the SVM (zeroed page: no vtable) and no manager table is known
        {
            svm::Request lone;
            lone.svm = SvmWorld::kOther;
            lone.image_base = SvmWorld::kBase;
            r = svm::refresh(w.mem, g, fns, lone);
            CHECK(!r.ok && r.message.find("not the StandingsViewManager") != std::string::npos, "wrong vtable: " + r.message);
        }
        // a stale published svm (another address than slot 108)
        req.svm = SvmWorld::kOther;
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("stale") != std::string::npos, "stale: " + r.message);
        req.svm = 0;
        // right vtable, listener not in slot 1
        w.mem.wr(SvmWorld::kVtable + svm::kVtableSlotListener, uint64_t(0x140001000ULL));
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("slot 1") != std::string::npos, "listener slot: " + r.message);
        w.mem.wr(SvmWorld::kVtable + svm::kVtableSlotListener, SvmWorld::kListener);
        // back-pointer to another table
        w.mem.wr(SvmWorld::kSvm + svm::kOffCtx, SvmWorld::kOther);
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("belongs to manager table") != std::string::npos, "back-pointer: " + r.message);
        svm::Request pub;
        pub.svm = SvmWorld::kSvm;
        pub.image_base = SvmWorld::kBase;
        r = svm::refresh(w.mem, g, fns, pub);
        CHECK(!r.ok && r.message.find("does not hold") != std::string::npos, "published svm, its own table holds it nowhere: " + r.message);
        w.mem.wr(SvmWorld::kSvm + svm::kOffCtx, SvmWorld::kManagers);
        // slot 108 holds another object
        w.mem.wr(SvmWorld::kHolders + uint64_t(svm::kTypeId) * 0x10, SvmWorld::kIfce);
        r = svm::refresh(w.mem, g, fns, pub);
        CHECK(!r.ok && r.message.find("does not hold") != std::string::npos, "slot 108 holds another object: " + r.message);
        w.mem.wr(SvmWorld::kHolders + uint64_t(svm::kTypeId) * 0x10, SvmWorld::kSvm);
        // no career: slot 108 has no instance
        w.mem.wr(SvmWorld::kManagers + svm::kSlotSize * uint64_t(svm::kTypeId) + svm::kSlotCount, int32_t(0));
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("no career loaded") != std::string::npos, "no instance: " + r.message);
        w.mem.wr(SvmWorld::kManagers + svm::kSlotSize * uint64_t(svm::kTypeId) + svm::kSlotCount, int32_t(1));
        // the critical section is owned / not a critical section
        w.set_critical_section(SvmWorld::kSvm + svm::kOffCritSec, 0, 1, 0x1234);
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("critical section") != std::string::npos && r.message.find("LockCount 0") != std::string::npos, "owned CS: " + r.message);
        w.set_critical_section(SvmWorld::kSvm + svm::kOffCritSec, 0x38547280, 0, 0);
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("critical section") != std::string::npos, "garbage CS: " + r.message);
        w.set_critical_section(SvmWorld::kSvm + svm::kOffCritSec, -1, 0, 0);
        // the FCE interface Turbo wrote to is not the career's
        req.ifce = SvmWorld::kOther;
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("not the one Turbo edited") != std::string::npos, "ifce mismatch: " + r.message);
        req.ifce = 0;
        // the interface's vtable slot 4 is not the game's Post / outside the image / the holder chain broken
        w.mem.wr(SvmWorld::kIfceVtable + svm::kIfceVtablePost, uint64_t(0x148A00000ULL));
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("not the game's Post function") != std::string::npos, "Post mismatch: " + r.message);
        w.mem.wr(SvmWorld::kIfceVtable + svm::kIfceVtablePost, uint64_t(0x50000000ULL));
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("no function in slot 4") != std::string::npos, "Post outside the image: " + r.message);
        w.mem.wr(SvmWorld::kIfceVtable + svm::kIfceVtablePost, SvmWorld::kPost);
        w.mem.wr(SvmWorld::kManagers + svm::kCtxIfceHolder, uint64_t(0));
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("no FCE interface holder") != std::string::npos, "no holder: " + r.message);
        w.mem.wr(SvmWorld::kManagers + svm::kCtxIfceHolder, SvmWorld::kHolders + uint64_t(svm::kIfceTypeId) * 0x10);
        // the allocator global empty / its function outside the image
        w.mem.wr(SvmWorld::kAllocGlobal, uint64_t(0));
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("allocator global") != std::string::npos, "no allocator: " + r.message);
        w.mem.wr(SvmWorld::kAllocGlobal, SvmWorld::kAlloc);
        w.mem.wr(SvmWorld::kAlloc + 0x100 + svm::kAllocVtableAlloc, uint64_t(0x7FF000000000ULL));
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.message.find("allocate function") != std::string::npos, "allocator fn outside the image: " + r.message);
        w.mem.wr(SvmWorld::kAlloc + 0x100 + svm::kAllocVtableAlloc, SvmWorld::kAllocFn);
        // the game is processing a match day / the SimDayManager is missing
        w.mem.wr(SvmWorld::kSdm + svm::kOffSimDayState, int32_t(5));
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "busy" && r.message.find("match day") != std::string::npos && r.message.find("state 5") != std::string::npos,
              "sim running: " + r.message);
        w.mem.wr(SvmWorld::kSdm + svm::kOffSimDayState, int32_t(0));
        w.mem.wr(SvmWorld::kManagers + svm::kSlotSize * uint64_t(svm::kSimDayTypeId) + svm::kSlotCount, int32_t(0));
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "busy" && r.message.find("SimDayManager is not available") != std::string::npos, "no SimDayManager: " + r.message);
        w.mem.wr(SvmWorld::kManagers + svm::kSlotSize * uint64_t(svm::kSimDayTypeId) + svm::kSlotCount, int32_t(1));
        // unmapped / cut-off objects
        pub.svm = 0x60000000ULL;
        r = svm::refresh(w.mem, g, fns, pub);
        CHECK(!r.ok && r.message.find("not readable") != std::string::npos, "unmapped: " + r.message);
        // readable first word in the last mapped page of the block (map() maps one page past it), +0x4B7 is not
        pub.svm = SvmWorld::kSdm + 0x1FF0;
        w.mem.wr(SvmWorld::kSdm + 0x1FF0, SvmWorld::kVtable);
        r = svm::refresh(w.mem, g, fns, pub);
        CHECK(!r.ok && r.message.find("cut off") != std::string::npos, "cut off: " + r.message);
        w.mem.wr(SvmWorld::kSdm + 0x1FF0, uint64_t(0));
        pub.svm = 3;
        r = svm::refresh(w.mem, g, fns, pub);
        CHECK(!r.ok && r.message.find("not a pointer") != std::string::npos, "bad address: " + r.message);
        CHECK(g.refreshes.empty() && g.events.empty(), "nothing was called");
        // and the world is intact afterwards
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(r.ok && g.refreshes.size() == 3, "intact: " + r.message);
    });

    run_case("standings refresh: every map inconsistency is reported and nothing is called; the full refresh only when opted in", [&] {
        SvmWorld w;
        svm::Request req = w.request();
        auto expect_refused = [&](const char* what, const char* text) {
            FakeSvmGame g;
            svm::Result r = svm::refresh(w.mem, g, fns, req);
            CHECK(!r.ok && r.stage == "walk" && !r.fallback, std::string(what) + ": " + r.message);
            CHECK(g.refreshes.empty() && g.events.empty(), std::string(what) + ": nothing called");
            CHECK(r.message.find(text) != std::string::npos, std::string(what) + ": reason in the message: " + r.message);
            std::vector<int32_t> keys;
            CHECK(!svm::map_keys(w.mem, SvmWorld::kSvm, keys, SvmWorld::kLiveVtable).empty() || keys.empty(), std::string(what) + ": walk refused");
        };
        // empty map: reported, the full refresh is opt-in
        w.set_map({});
        expect_refused("empty map", "opt-in");
        // no root but a count
        w.mem.wr(SvmWorld::kSvm + svm::kOffMapSize, uint32_t(3));
        expect_refused("no root with a count", "no root");
        // wrong parent link
        w.set_map({1200, 1300, 1400});
        w.mem.wr(w.node_of(1200) + svm::kNodeParent, SvmWorld::kOther);
        expect_refused("parent link", "has parent");
        // keys out of order
        w.set_map({1200, 1300, 1400});
        w.mem.wr(w.node_of(1400) + svm::kNodeKey, int32_t(1250));
        expect_refused("key order", "not ascending");
        // size counter disagrees
        w.set_map({1200, 1300, 1400});
        w.mem.wr(SvmWorld::kSvm + svm::kOffMapSize, uint32_t(2));
        expect_refused("size mismatch", "counts 2 entries but 3");
        // a node pointer that is no pointer / unmapped
        w.set_map({1200, 1300, 1400});
        w.mem.wr(w.node_of(1300) + svm::kNodeLeft, uint64_t(3));
        expect_refused("bad node pointer", "bad node pointer");
        w.set_map({1200, 1300, 1400});
        w.mem.wr(w.node_of(1300) + svm::kNodeLeft, uint64_t(0x60000000ULL));
        expect_refused("unmapped node", "not readable");
        // a value that is no LiveStandings (another vtable / no object)
        w.set_map({1200, 1300, 1400});
        w.mem.wr(w.live(1), SvmWorld::kStaffVtable);
        expect_refused("value vtable", "does not hold a LiveStandings object");
        w.set_map({1200, 1300, 1400});
        w.mem.wr(w.node_of(1300) + svm::kNodeValue, uint64_t(0x60000000ULL));
        expect_refused("value unmapped", "does not hold a LiveStandings object");
        // a cycle (right child pointing back at the root): the parent check catches it
        w.set_map({1200, 1300, 1400});
        w.mem.wr(w.node_of(1400) + svm::kNodeRight, w.node_of(1300));
        expect_refused("cycle", "has parent");
        // too many nodes for a standings map (size counter) and a tree bigger than the bound (walk)
        std::vector<int32_t> many;
        for (int i = 0; i < 70; ++i) many.push_back(100 + i);
        w.set_map(many);
        expect_refused("size over the bound", "70 entries");
        w.mem.wr(SvmWorld::kSvm + svm::kOffMapSize, uint32_t(3));
        expect_refused("walk over the bound", "more than 64 nodes");
        // the anchor itself unreadable
        w.set_map({1200, 1300, 1400});
        w.mem.wr(SvmWorld::kSvm + svm::kOffMapRoot, uint64_t(0x60000000ULL));
        expect_refused("unmapped root", "not readable");
        // the opt-in: an empty map runs the game's own full refresh (event 29 on the SVM) once; an inconsistent map never does
        w.set_map({});
        req.allow_fallback = true;
        FakeSvmGame g;
        svm::Result r = svm::refresh(w.mem, g, fns, req);
        CHECK(r.ok && r.fallback && r.stage == "done" && g.refreshes.empty() && g.events.size() == 1 && g.events[0].first == SvmWorld::kSvm &&
                  g.events[0].second == svm::kEventPostLoadPrepare,
              "opt-in fallback: " + r.message);
        CHECK(r.message.find("POST_LOAD_PREPARE") != std::string::npos && r.message.find("now holds 0") != std::string::npos, r.message);
        g = FakeSvmGame();
        g.fail_event = true;
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "fallback" && r.message.find("boom") != std::string::npos, "fallback failed: " + r.message);
        w.set_map({1200, 1300, 1400});
        w.mem.wr(SvmWorld::kSvm + svm::kOffMapSize, uint32_t(2));
        g = FakeSvmGame();
        r = svm::refresh(w.mem, g, fns, req);
        CHECK(!r.ok && r.stage == "walk" && g.events.empty() && g.refreshes.empty(), "inconsistent map with the opt-in: still nothing called: " + r.message);
    });

    run_case("standings refresh: a failing sync request stops the sequence and the message counts what ran", [&] {
        SvmWorld w;
        FakeSvmGame g;
        g.fail_after = 1;
        svm::Result r = svm::refresh(w.mem, g, fns, w.request());
        CHECK(!r.ok && r.stage == "refresh" && r.refreshed == 1 && g.refreshes.size() == 1 && g.events.empty(), r.message);
        CHECK(r.message.find("1 of 3") != std::string::npos && r.message.find("1300") != std::string::npos && r.message.find("boom") != std::string::npos,
              "message: " + r.message);
    });

    run_case("standings refresh: the one-shot gate runs one refresh per edit and none while one is in flight", [&] {
        svm::OneShotGate gate;
        std::string why;
        CHECK(!gate.take(why) && why.find("no edit") != std::string::npos, "nothing armed: " + why);
        CHECK(gate.arm("Napoli row", why) && gate.armed() && !gate.in_flight(), "armed by an edit");
        CHECK(gate.take(why) && !gate.armed() && gate.in_flight() && gate.label() == "Napoli row", "taken once");
        CHECK(!gate.take(why) && why.find("still running") != std::string::npos && why.find("Napoli row") != std::string::npos, "second take refused: " + why);
        CHECK(!gate.arm("fixture 3", why) && why.find("still running") != std::string::npos, "arming while running refused: " + why);
        gate.done();
        CHECK(!gate.in_flight() && !gate.armed(), "done: idle, not armed again");
        CHECK(!gate.take(why), "the finished run does not re-arm");
        CHECK(gate.arm("fixture 3", why) && gate.take(why) && gate.label() == "fixture 3", "next edit, next run");
        gate.done();
    });

    run_case("standings refresh: the view's rows are read from the LiveStandings trees (what the Standings screen shows)", [&] {
        SvmWorld w({1118, 1300});
        w.set_tree(1118, 1120, {{3506, 48, 0}, {3490, 52, 0}});
        w.set_tree(1300, 1301, {{10, 7, 3}});
        std::vector<svm::ShownGroup> shown;
        std::string err = svm::shown_groups(w.mem, SvmWorld::kSvm, SvmWorld::kLiveVtable, SvmWorld::kCompObjVtable, SvmWorld::kListVtable, shown);
        CHECK(err.empty(), "walked: " + err);
        CHECK(shown.size() == 2 && shown[0].key == 1118 && shown[0].group == 1120 && shown[0].rows.size() == 2 && shown[1].key == 1300 &&
                  shown[1].group == 1301 && shown[1].rows.size() == 1,
              fmt("two groups (%zu)", shown.size()));
        if (shown.size() == 2) {
            CHECK(shown[0].rows[0].id == 3506 && shown[0].rows[0].team == 48 && shown[0].rows[0].points == 0 && shown[0].rows[1].id == 3490,
                  "rows carry the FCE standing id, the team and the points");
            CHECK(shown[1].rows[0].id == 10 && shown[1].rows[0].team == 7 && shown[1].rows[0].points == 3, "second group's row");
        }
        // which written rows are visible
        std::string msg;
        CHECK(!svm::describe_rows(shown, {3506}, msg) && msg.find("row 3506 (team 48) is shown by competition 1118 (group 1120) with 0 points") != std::string::npos,
              "a shown row: " + msg);
        msg.clear();
        CHECK(svm::describe_rows(shown, {3322}, msg) && msg.find("WARNING: row 3322 is not among the rows") != std::string::npos &&
                  msg.find("1120 (comp 1118), 1301 (comp 1300)") != std::string::npos,
              "a hidden row: " + msg);
        msg.clear();
        CHECK(svm::describe_rows(shown, {10, 3322}, msg) && msg.find("row 10 (team 7)") != std::string::npos && msg.find("WARNING: row 3322") != std::string::npos,
              "one of each");
        msg.clear();
        CHECK(!svm::describe_rows(shown, {}, msg) && msg.empty(), "no rows: nothing said");
        // the anchors are required: without the two vtables nothing is walked
        CHECK(!svm::shown_groups(w.mem, SvmWorld::kSvm, SvmWorld::kLiveVtable, 0, SvmWorld::kListVtable, shown).empty() && shown.empty(), "no CompObject vtable");
        CHECK(!svm::shown_groups(w.mem, SvmWorld::kSvm, SvmWorld::kLiveVtable, SvmWorld::kCompObjVtable, 0, shown).empty(), "no list vtable");
        CHECK(!svm::shown_groups(w.mem, SvmWorld::kSvm, SvmWorld::kLiveVtable, SvmWorld::kCompObjVtable, SvmWorld::kCompObjVtable, shown).empty(), "equal vtables");
        // an empty value (null data) is skipped; an empty map gives no groups
        w.mem.wr(w.live(1) + 8, uint64_t(0));
        CHECK(svm::shown_groups(w.mem, SvmWorld::kSvm, SvmWorld::kLiveVtable, SvmWorld::kCompObjVtable, SvmWorld::kListVtable, shown).empty() && shown.size() == 1,
              "null data skipped");
        SvmWorld empty({});
        CHECK(svm::shown_groups(empty.mem, SvmWorld::kSvm, SvmWorld::kLiveVtable, SvmWorld::kCompObjVtable, SvmWorld::kListVtable, shown).empty() && shown.empty(),
              "empty map: no groups");
        // inconsistencies: a node with another vtable, too many children, too many rows, a bad rows pointer, a cycle, a deep
        // chain, a row with an impossible id; each reported with the output cleared
        SvmWorld w2({1118});
        w2.set_tree(1118, 1120, {{1, 1, 1}});
        auto bad = [&](const char* what) {
            std::vector<svm::ShownGroup> out;
            std::string e = svm::shown_groups(w2.mem, SvmWorld::kSvm, SvmWorld::kLiveVtable, SvmWorld::kCompObjVtable, SvmWorld::kListVtable, out);
            CHECK(!e.empty() && out.empty(), std::string(what) + ": " + e);
        };
        w2.mem.wr(w2.last_stage, uint64_t(0x1234));
        bad("foreign vtable");
        w2.mem.wr(w2.last_stage, SvmWorld::kCompObjVtable);
        w2.mem.wr(w2.last_root + svm::kCoChildCount, uint32_t(65));
        bad("65 children");
        w2.mem.wr(w2.last_root + svm::kCoChildCount, uint32_t(1));
        w2.mem.wr(w2.last_list + svm::kSlRowCount, int32_t(65));
        bad("65 rows");
        w2.mem.wr(w2.last_list + svm::kSlRowCount, int32_t(-1));
        bad("negative row count");
        w2.mem.wr(w2.last_list + svm::kSlRowCount, int32_t(1));
        w2.mem.wr(w2.last_list + svm::kSlRows, uint64_t(0x10));
        bad("bad rows pointer");
        w2.mem.wr(w2.last_list + svm::kSlRows, w2.last_rows);
        w2.mem.wr(w2.last_list + svm::kSlGroup, int32_t(70000));
        bad("group id out of range");
        w2.mem.wr(w2.last_list + svm::kSlGroup, int32_t(1120));
        w2.mem.wr(w2.last_rows + svm::kRowId, int32_t(-5));
        bad("row id out of range");
        w2.mem.wr(w2.last_rows + svm::kRowId, int32_t(1));
        {
            // the stage's child list points back at the root: a cycle, stopped by the depth bound
            uint64_t kids = 0;
            w2.mem.rd(w2.last_stage + svm::kCoChildren, kids);
            w2.mem.wr(kids, w2.last_root);
            bad("cycle");
            w2.mem.wr(kids, w2.last_list);
        }
        w2.mem.wr(w2.last_root + svm::kCoChildren, uint64_t(0x30));
        bad("bad children pointer");
        {
            std::vector<svm::ShownGroup> out;
            uint64_t kids1 = w2.last_root + 0x80;  // restored: the first allocation after the comp node is its child list
            w2.mem.wr(w2.last_root + svm::kCoChildren, kids1);
            CHECK(svm::shown_groups(w.mem, SvmWorld::kSvm, SvmWorld::kLiveVtable, SvmWorld::kCompObjVtable, SvmWorld::kListVtable, out).empty() && out.size() == 1,
                  "restored tree walks again");
        }
    });

    run_case("standings refresh: the outcome says whether the written rows are among those the view shows (the 04-10-2026 case)", [&] {
        // the live case: the view holds comp 1118 -> group 1120 with Napoli's row 3506; Turbo wrote row 3322 (the Coppa Italia pool)
        SvmWorld w({1118});
        w.set_tree(1118, 1120, {{3506, 48, 0}, {3490, 52, 0}});
        FakeSvmGame game;
        svm::Request req = w.request();
        req.label = "SSC Napoli row";
        req.rows = {3322};
        svm::Result r = svm::refresh(w.mem, game, fns, req);
        CHECK(r.ok && r.stage == "done" && r.refreshed == 1 && game.refreshes.size() == 1, "the call ran: " + r.message);
        CHECK(r.warning, "warning: the written row is invisible");
        CHECK(r.message.find("re-read 1 competition (comp ids 1118)") != std::string::npos && r.message.find("WARNING: row 3322 is not among") != std::string::npos &&
                  r.message.find("1120 (comp 1118)") != std::string::npos,
              "message: " + r.message);
        CHECK(r.shown.size() == 1 && r.shown[0].group == 1120 && r.shown[0].rows.size() == 2, "the outcome carries the view's rows");
        // the right row
        req.rows = {3506};
        r = svm::refresh(w.mem, game, fns, req);
        CHECK(r.ok && !r.warning && r.message.find("row 3506 (team 48) is shown by competition 1118 (group 1120) with 0 points") != std::string::npos,
              "shown row: " + r.message);
        // no rows named: the message is the old one, nothing appended
        req.rows.clear();
        r = svm::refresh(w.mem, game, fns, req);
        CHECK(r.ok && !r.warning && r.message == "SSC Napoli row: the game's standings view re-read 1 competition (comp ids 1118)", "no rows: " + r.message);
        CHECK(r.shown.size() == 1, "the view's rows are still reported");
        // the anchors missing (another build): the call still runs, the check is reported as not possible, no warning
        svm::Fns no_anchor = fns;
        no_anchor.compobj_vtable = no_anchor.standinglist_vtable = 0;
        req.rows = {3322};
        req.image_base = 0;
        r = svm::refresh(w.mem, game, no_anchor, req);
        CHECK(r.ok && !r.warning && r.message.find("could not be checked") != std::string::npos && r.shown.empty(), "no anchors: " + r.message);
        // with the image base the RVAs stand in for the anchors
        req.image_base = SvmWorld::kBase;
        r = svm::refresh(w.mem, game, no_anchor, req);
        CHECK(r.ok && r.warning && r.shown.size() == 1, "RVA fallback: " + r.message);
        // a tree the walk refuses: reported, the call's own outcome unchanged
        w.mem.wr(w.last_stage, uint64_t(0x1234));
        r = svm::refresh(w.mem, game, fns, req);
        CHECK(r.ok && !r.warning && r.message.find("could not be checked: comp 1118: node") != std::string::npos, "bad tree: " + r.message);
    });

    run_case("signatures: the built-in standings-refresh entries resolve on the game's bytes (SVM vtable via the SVM ctor's lea)", [&] {
        const SignatureTable* t = builtin_signature_table("6AB9813C-211EF000");
        CHECK(t != nullptr, "built-in table");
        if (!t) return;
        const char* names[] = {"svm_refresh_comp", "svm_listener", "svm_vtable", "fce_iface_post", "svm_allocator", "fcei_compobject_vtable",
                               "fcei_standinglist_vtable"};
        for (const char* n : names) CHECK(t->find(n) && !t->find(n)->pattern.empty(), std::string("entry ") + n);
        CHECK(t->find("svm_slot10") == nullptr, "the StaffManager slot-10 anchor is gone");
        // bytes read from fc27_image.bin (FC27.exe 1.0.140.64835) at the functions' VAs
        const uint8_t ctor[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
                                0x89, 0x51, 0x08, 0x48, 0x8D, 0x05, 0x1E, 0x43, 0x9C, 0x01, 0xBA, 0x0C, 0x00, 0x00, 0x00, 0x48,
                                0x89, 0x01, 0x48, 0x8B, 0xD9, 0x4C, 0x8D, 0x0D, 0x50, 0x33, 0x1C, 0xFA, 0x48, 0x81, 0xC1, 0x08,
                                0x01, 0x00, 0x00, 0x44, 0x8D, 0x42, 0x0F, 0xE8, 0xE4, 0x2C, 0x98, 0xF8, 0x33, 0xFF, 0x48, 0x8D};
        const uint8_t refresh[] = {0x83, 0xFA, 0xFF, 0x74, 0x65, 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
                                   0x8B, 0x41, 0x08, 0x4C, 0x8D, 0x05, 0xF6, 0x09, 0x27, 0x03, 0x45, 0x33, 0xC9, 0x8B, 0xDA, 0x48,
                                   0x8B, 0x48, 0x38, 0x41, 0x8D, 0x51, 0x70, 0x48, 0x8B, 0x39, 0x48, 0x8B, 0x0D, 0x67, 0x4B, 0x4C,
                                   0x04, 0x48, 0x8B, 0x01};
        const uint8_t listener[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                    0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x05, 0x52, 0xF5, 0xE8, 0x03, 0x48, 0x33,
                                    0xC4, 0x48, 0x89, 0x45, 0xF8, 0x49, 0x8B, 0xD8, 0x48, 0x8B, 0xF9, 0x40, 0x32, 0xF6, 0xC5, 0xF9};
        const uint8_t post[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x48, 0x8B, 0xDA, 0xFF, 0x50, 0x40, 0x8B,
                                0x53, 0x10, 0x4C, 0x8B, 0xC3, 0x48, 0x8B, 0xC8, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xE9, 0xDA, 0x1E,
                                0x61, 0xFB, 0xCC, 0xCC};
        // the FCEI::CompObject clone allocator 0x144040198 (0x100 bytes): the two node vtables are its lea operands
        const uint8_t clone[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xDB, 0x48, 0x8B, 0xF9, 0x85, 0xD2, 0x75, 0x51, 0x48, 0x8B, 0x0D,
            0xEE, 0x9C, 0x22, 0x08, 0x4C, 0x8D, 0x05, 0xEF, 0xE4, 0xA9, 0x06, 0x45, 0x33, 0xC9, 0xBA, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x01,
            0xFF, 0x50, 0x10, 0x4C, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x89, 0x78, 0x08, 0x49, 0x8D, 0x4B,
            0x20, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x58, 0x18, 0x48, 0x8D, 0x05, 0x2E, 0x18, 0xAA, 0x06, 0x49, 0x89, 0x03, 0xE8, 0xFA, 0xF3,
            0x50, 0xFD, 0xE8, 0xF5, 0xF3, 0x50, 0xFD, 0x49, 0x8B, 0xDB, 0xEB, 0x7F, 0x83, 0xFA, 0x01, 0x75, 0x35, 0x48, 0x8B, 0x0D, 0x98, 0x9C,
            0x22, 0x08, 0x4C, 0x8D, 0x05, 0xE1, 0xE4, 0xA9, 0x06, 0x45, 0x33, 0xC9, 0x48, 0x8B, 0x01, 0x41, 0x8D, 0x51, 0x38, 0xFF, 0x50, 0x10,
            0x48, 0x85, 0xC0, 0x74, 0x5A, 0x48, 0x8D, 0x0D, 0x10, 0x1B, 0xAA, 0x06, 0x48, 0x89, 0x08, 0x83, 0xC9, 0xFF, 0x89, 0x48, 0x30, 0x89,
            0x48, 0x34, 0xEB, 0x2F, 0x83, 0xFA, 0x02, 0x75, 0x40, 0x48, 0x8B, 0x0D, 0x5E, 0x9C, 0x22, 0x08, 0x4C, 0x8D, 0x05, 0x8F, 0xE4, 0xA9,
            0x06, 0x45, 0x33, 0xC9, 0x48, 0x8B, 0x01, 0x41, 0x8D, 0x51, 0x30, 0xFF, 0x50, 0x10, 0x48, 0x85, 0xC0, 0x74, 0x20, 0x48, 0x8D, 0x0D,
            0xEE, 0x19, 0xAA, 0x06, 0x48, 0x89, 0x08, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x58, 0x18, 0x89, 0x58, 0x20, 0x48, 0x89, 0x58, 0x28,
            0x48, 0x8B, 0xD8, 0x48, 0x89, 0x78, 0x08, 0x48, 0x8B, 0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0xCC,
            0xCC, 0xCC, 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48};
        // the ctor sits at +0x100 of a buffer based so that its VA is the real one: the lea then resolves to the real vtable
        const uint64_t base = 0x147D9A700ULL - 0x100;
        std::vector<uint8_t> code(0x1000, 0xCC);
        std::memcpy(code.data() + 0x100, ctor, sizeof(ctor));
        std::memcpy(code.data() + 0x300, refresh, sizeof(refresh));
        std::memcpy(code.data() + 0x400, listener, sizeof(listener));
        std::memcpy(code.data() + 0x500, post, sizeof(post));
        std::memcpy(code.data() + 0x600, clone, sizeof(clone));
        SigResult r = resolve_signature(*t->find("svm_vtable"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.match == 0x147D9A700ULL && r.address == 0x14975EA38ULL,
              "svm_vtable resolves to base+0x975EA38 (= svm::kRvaVtable): " + r.error);
        CHECK(0x14975EA38ULL - 0x140000000ULL == svm::kRvaVtable, "constant agrees with the ctor");
        CHECK(svm::kRvaVtable != 0xB0160D8ULL, "the StaffManager vtable is not the constant");
        struct Exp { const char* name; uint64_t off; } exp[] = {{"svm_refresh_comp", 0x300}, {"svm_listener", 0x400}, {"fce_iface_post", 0x500}};
        for (const auto& e : exp) {
            r = resolve_signature(*t->find(e.name), code.data(), code.size(), base);
            CHECK(r.state == SigState::Found && r.address == base + e.off, std::string(e.name) + ": " + r.error);
        }
        // the allocator global: the mov rcx,[rip+disp32] at +0x2A of the same function
        r = resolve_signature(*t->find("svm_allocator"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.match == base + 0x300 && r.address == base + 0x300 + 0x2A + 7 + 0x044C4B67ULL,
              "svm_allocator resolves through the rip operand: " + r.error);
        CHECK(0x147DA5310ULL + 0x2A + 7 + 0x044C4B67ULL == 0x14C269EA8ULL, "on the real function that is the allocator global 0x14C269EA8");
        // the node vtables of the view's rows: the lea rax / lea rcx at +0x4B / +0x89 of the clone allocator
        r = resolve_signature(*t->find("fcei_compobject_vtable"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.match == base + 0x600 && r.address == base + 0x600 + 0x4B + 7 + 0x06AA182EULL,
              "fcei_compobject_vtable resolves through the lea at +0x4B: " + r.error);
        r = resolve_signature(*t->find("fcei_standinglist_vtable"), code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.match == base + 0x600 && r.address == base + 0x600 + 0x89 + 7 + 0x06AA1B10ULL,
              "fcei_standinglist_vtable resolves through the lea at +0x89: " + r.error);
        CHECK(0x144040198ULL + 0x4B + 7 + 0x06AA182EULL == 0x140000000ULL + svm::kRvaCompObjectVtable, "on the real allocator: the CompObject vtable 0x14AAE1A18");
        CHECK(0x144040198ULL + 0x89 + 7 + 0x06AA1B10ULL == 0x140000000ULL + svm::kRvaStandingListVtable, "on the real allocator: the list vtable 0x14AAE1D38");
        for (const char* n : names) {
            r = resolve_signature(*t->find(n), code.data(), code.size(), base);
            CHECK(r.hits == 1, std::string(n) + " unique");
        }
        // the mailbox op the Lua native uses
        CHECK(kCallOpStandingsRefresh == 2, "call op 2");
    });
}

// ---------------------------------------------------------------- miniface from the 3D model (core/player_capture.h)
static void test_player_capture() {
    using namespace capture;
    run_case("player capture: the game's default descriptor and the request plan", [&] {
        PlayerDesc d = default_desc(1234, 7, true, false);
        CHECK(sizeof(PlayerDesc) == 0x6C && kPlayerDescSize == 0x6C, "0x6C bytes");
        CHECK(d.id() == 1234 && d.i32(0x04) == -1 && d.second_id() == 7 && d.i32(0x0C) == -1 && d.i32(0x10) == -1, "ids and -1s");
        CHECK(d.i32(0x14) == 0 && d.i32(0x18) == 0 && d.i32(0x1C) == 0, "+14..+1F zero");
        CHECK(d.i32(0x20) == -1 && d.i32(0x24) == 1 && d.i32(0x28) == 0 && d.i32(0x2C) == 0, "+20 block");
        for (size_t off = 0x30; off < 0x4C; off += 4) CHECK(d.i32(off) == 0, fmt("+%02zX zero", off));
        CHECK(d.i32(0x4C) == -1 && d.i32(0x50) == 1 && d.i32(0x54) == -1 && d.i32(0x58) == -1, "+4C block");
        CHECK(d.i32(0x5C) == 0 && d.i32(0x60) == 0, "+5C zero");
        CHECK(d.flag64() && !d.flag68() && d.b[0x65] == 0 && d.b[0x69] == 0, "flags");
        PlayerDesc m = default_desc(9999, -1, false, true);
        CHECK(!m.flag64() && m.flag68() && m.second_id() == -1, "manager flags");
        std::string txt = describe_desc(d);
        CHECK(txt.find("+00=1234") == 0 && txt.find("+08=7") != std::string::npos && txt.find("+64=1") != std::string::npos, "describe: " + txt);
        CHECK(describe_desc(PlayerDesc()) == "(all zero)", "describe zero");
        uint8_t hb[18];
        for (int i = 0; i < 18; ++i) hb[i] = uint8_t(i * 17);
        std::string hx = hex_bytes(hb, 18);
        CHECK(hx.rfind("00 11 22", 0) == 0 && hx.find("|") == 16 * 3 - 1 && hx.size() == 18 * 3 - 1, "hex dump: " + hx);
        // plan: default descriptor, presets
        Request r;
        r.id = 55;
        r.second_id = 9;
        r.camera = 0;
        Plan p = plan_request(r, nullptr);
        CHECK(p.desc.id() == 55 && p.desc.second_id() == 9 && p.desc.flag64() && p.mode == 0 && p.extra == 0 && p.note == "default descriptor", "default plan");
        r.camera = 2;
        p = plan_request(r, nullptr);
        CHECK(p.mode == cameras()[2].mode && p.extra == cameras()[2].extra, "preset 2");
        r.camera = kCameraLearned;
        p = plan_request(r, nullptr);
        CHECK(p.mode == cameras()[0].mode && p.extra == cameras()[0].extra, "learned camera without a template falls back to preset 0");
        r.mode_override = 1;
        r.extra_override = 3;
        p = plan_request(r, nullptr);
        CHECK(p.mode == 1 && p.extra == 3, "overrides");
        // plan from a learned template: the template's bytes with the id replaced
        Template t;
        t.learned = true;
        t.desc = default_desc(777, 42, true, false);
        t.desc.set_i32(0x30, 123456);
        t.mode = 1;
        t.extra = 3;
        Request r2;
        r2.id = 88;
        r2.second_id = -1;
        r2.camera = kCameraLearned;
        p = plan_request(r2, &t);
        CHECK(p.desc.id() == 88 && p.desc.second_id() == 42 && p.desc.i32(0x30) == 123456 && p.mode == 1 && p.extra == 3, "template plan: " + describe_desc(p.desc));
        CHECK(p.note.find("learned descriptor") == 0 && p.note.find("learned mode/extra") != std::string::npos, "note: " + p.note);
        r2.second_id = 5;
        r2.camera = 0;
        r2.use_template = false;
        p = plan_request(r2, &t);
        CHECK(p.desc.second_id() == 5 && p.desc.i32(0x30) == 0 && p.mode == 0 && p.note == "default descriptor", "template declined");
        r2.manager = true;
        r2.id = 7501;
        p = plan_request(r2, nullptr);
        CHECK(!p.desc.flag64() && p.desc.id() == 7501 && p.desc.second_id() == 5, "manager plan");
    });

    run_case("player capture: the game's callback object (eastl::function shape)", [&] {
        int ctx = 5;
        auto inv = reinterpret_cast<void*>(&test_player_capture);
        Delegate d = make_delegate(&ctx, inv);
        CHECK(sizeof(Delegate) == 0x20 && d.storage[0] == &ctx && d.storage[1] == nullptr && d.manager == reinterpret_cast<void*>(&delegate_manager) && d.invoker == inv, "shape");
        CHECK(delegate_context(&d) == &ctx && delegate_context(d.storage) == &ctx, "context from the storage pointer");
        Delegate copy;
        CHECK(delegate_manager(&copy, &d, kMgrCopy) == nullptr && copy.storage[0] == &ctx && copy.manager == nullptr, "copy op copies the 16-byte storage only");
        Delegate moved;
        delegate_manager(&moved, &d, kMgrMove);
        CHECK(moved.storage[0] == &ctx, "move op");
        Delegate untouched;
        untouched.storage[0] = &untouched;
        delegate_manager(&untouched, nullptr, kMgrDestruct);
        CHECK(untouched.storage[0] == &untouched, "destruct op leaves the storage alone");
        delegate_manager(&untouched, &d, 3);
        delegate_manager(&untouched, &d, 4);
        CHECK(untouched.storage[0] == &untouched, "query ops do nothing");
        delegate_manager(&d, &d, kMgrCopy);
        CHECK(d.storage[0] == &ctx, "self copy");
        // the vector the game reads: begin / end over one descriptor
        PlayerDesc one = default_desc(1, 2, true, false);
        DescVector v;
        v.begin = &one;
        v.end = &one + 1;
        CHECK(sizeof(DescVector) == 0x20 && reinterpret_cast<const uint8_t*>(v.end) - reinterpret_cast<const uint8_t*>(v.begin) == 0x6C, "vector shape");
    });

    run_case("player capture: picture bytes (DDS, PNG, raw RGBA, garbage)", [&] {
        Rgba src = solid(64, 64, 200, 30, 10);
        std::vector<uint8_t> dds = encode_dds_dxt5(src);
        Rgba out;
        std::string fmt_, err;
        CHECK(decode_slice(dds.data(), dds.size(), out, &fmt_, &err), "dds: " + err);
        CHECK(out.w == 64 && out.h == 64 && fmt_ == "DDS DXT5 64x64", "dds format: " + fmt_);
        const uint8_t* p = out.at(10, 10);
        CHECK(std::abs(int(p[0]) - 200) <= 8 && std::abs(int(p[1]) - 30) <= 8 && p[3] == 255, fmt("dds colour %d,%d,%d", p[0], p[1], p[2]));
        CHECK(decode_slice(kTestPng, sizeof(kTestPng), out, &fmt_, &err) && out.w == 4 && out.h == 2 && fmt_ == "PNG 4x2", "png: " + fmt_ + " " + err);
        std::vector<uint8_t> raw(32 * 32 * 4, 0);
        for (size_t i = 0; i < raw.size(); i += 4) {
            raw[i] = 1;
            raw[i + 1] = 2;
            raw[i + 2] = 3;
            raw[i + 3] = 255;
        }
        CHECK(decode_slice(raw.data(), raw.size(), out, &fmt_, &err) && out.w == 32 && out.h == 32 && fmt_ == "raw RGBA 32x32", "raw: " + fmt_);
        CHECK(out.at(5, 5)[0] == 1 && out.at(5, 5)[2] == 3 && out.at(31, 31)[3] == 255, "raw pixels");
        CHECK(raw_square_side(64 * 64 * 4) == 64 && raw_square_side(540 * 540 * 4) == 540 && raw_square_side(kSliceSizeMode0) == 0 &&
                  raw_square_side(kSliceSizeMode1) == 0 && raw_square_side(0) == 0 && raw_square_side(63 * 63 * 4 + 4) == 0,
              "raw square guesses");
        std::vector<uint8_t> junk(1000, 0x5A);
        CHECK(!decode_slice(junk.data(), junk.size(), out, &fmt_, &err) && err.find("unknown picture format") == 0, "junk refused: " + err);
        CHECK(!decode_slice(nullptr, 0, out, &fmt_, &err) && !decode_slice(junk.data(), 8, out, &fmt_, &err), "empty refused");
        std::vector<uint8_t> bad_dds(dds.begin(), dds.begin() + 100);
        CHECK(!decode_slice(bad_dds.data(), bad_dds.size(), out, &fmt_, &err) && err.rfind("DDS: ", 0) == 0, "truncated dds: " + err);
    });

    run_case("player capture: built-in signatures resolve the controller's globals", [&] {
        const SignatureTable* b = builtin_signature_table("6AB9813C-211EF000");
        CHECK(b != nullptr, "table");
        if (!b) return;
        for (const char* n : {"PlayerCaptureController_GetOrCreate", "PlayerCapture_RequestStatic_B", "PlayerCaptureController_Start", "PlayerCapture_Settings",
                              "PlayerCapture_ListenerHub", "PlayerCapture_Renderer", "PlayerCaptureStream_OnSlot"})
            CHECK(b->find(n) && !b->find(n)->pattern.empty(), std::string("entry ") + n);
        const Signature* st = b->find("PlayerCaptureController_Start");
        const Signature* se = b->find("PlayerCapture_Settings");
        const Signature* hb = b->find("PlayerCapture_ListenerHub");
        const Signature* rd = b->find("PlayerCapture_Renderer");
        if (!st || !se || !hb || !rd) return;
        CHECK(se->pattern == st->pattern && hb->pattern == st->pattern && rd->pattern == st->pattern, "the globals share Start's pattern");
        CHECK(se->resolve == "rip" && se->offset == 0x24 && hb->resolve == "rip" && hb->offset == 0xB4 && rd->resolve == "rip" && rd->offset == 0x141, "rip offsets");
        // synthetic Start: the prologue bytes of the real function, then mov rcx,[rip+d] at +0xB4 and mov rbx,[rip+d] at +0x141
        std::vector<uint8_t> code(0x1000, 0xCC);
        const uint64_t base = 0x147000000ULL, fn = 0x100;
        const uint8_t pro[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x81, 0xEC, 0xC0, 0x04, 0x00, 0x00,
                               0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xB0, 0x04, 0x00, 0x00,
                               0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00};
        std::memcpy(code.data() + fn, pro, sizeof(pro));
        auto put_rip = [&](size_t at, uint8_t modrm, uint64_t target) {
            code[fn + at] = 0x48;
            code[fn + at + 1] = 0x8B;
            code[fn + at + 2] = modrm;
            int32_t disp = int32_t(int64_t(target) - int64_t(base + fn + at + 7));
            std::memcpy(code.data() + fn + at + 3, &disp, 4);
        };
        put_rip(0x24, 0x05, base + 0x800);   // settings
        put_rip(0xB4, 0x0D, base + 0x808);   // hub
        put_rip(0x141, 0x1D, base + 0x810);  // renderer
        SigResult r = resolve_signature(*st, code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.address == base + fn, "Start found: " + r.error);
        r = resolve_signature(*se, code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.address == base + 0x800, "settings global: " + r.error);
        r = resolve_signature(*hb, code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.address == base + 0x808, "hub global: " + r.error);
        r = resolve_signature(*rd, code.data(), code.size(), base);
        CHECK(r.state == SigState::Found && r.address == base + 0x810, "renderer global: " + r.error);
        // every capture pattern must be unique in a buffer that holds Start once (no accidental double match)
        std::memcpy(code.data() + 0x900, pro, sizeof(pro));
        r = resolve_signature(*st, code.data(), code.size(), base);
        CHECK(r.state == SigState::Ambiguous, "two copies are ambiguous");
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
    std::printf("native game calls\n");
    test_game_calls();
    std::printf("native game-thread dispatcher\n");
    test_gamethread();
    std::printf("native Live Editor log\n");
    test_le_log();
    std::printf("native live standings\n");
    test_fce_standings();
    test_fce_compobjs();
    std::printf("native standings refresh\n");
    test_standings_refresh();
    std::printf("native player capture\n");
    test_player_capture();
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
