// faces3d template v5 (5.0c E13, hair / beard replace): the clean-scan splices over the REAL stock player_kit.lua (read at run time
// from the evidence folder, never copied into the repository; SKIP when it is not on this machine), against the bytes the offline
// lupa trace ran (turbo_dev/evidence/2026-10-31/E13: gen_body.py -> v5_empty.lua md5 39474a1c, trace_clean_face.py PASS):
//   - the embedded v5 text is the reviewed template (md5 1c57d7f9) and anchors the same stock as v4 (9399e39a);
//   - v5 over the stock, empty list == v5_empty.lua byte for byte; a changed anchor line or a changed stock is refused;
//   - main-chunk locals stay stock + 1 (TURBO_GATE only: every new function is a TURBO_GATE field);
//   - build_live_override: hair_replace = true -> v5; false (the default, and what hair_replace_off.txt selects) -> v4 EXACTLY
//     (afea57af, the 5.0b live bytes); a v5 refusal falls back to v4;
//   - hair_replace_on reads turbo_output\faces3d\hair_replace_off.txt;
//   - with a Lua 5.4 (LUA=, else lua5.4): the v5 body compiles and TURBO_GATE.cleanMasks returns the stock region masks.
// Fails to build on the old tree (no template_v5_text / hair_replace_on / 6-argument build_live_override).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/faces3d.h"
#include "core/lua_loader.h"

using namespace turbo;
using namespace turbo::faces3d;
namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    if (ok) ++g_pass;
    else {
        ++g_fail;
        std::printf("  FAIL %s\n", what.c_str());
    }
}
static std::string md5s(const std::string& s) { return lualoader::md5_hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
static bool read_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
static void write_file(const fs::path& p, const std::string& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(b.data(), static_cast<std::streamsize>(b.size()));
}
// lines that start with "local " at column 0 (the main chunk's locals: stock functions are written at column 0, bodies are tabbed)
static int col0_locals(const std::string& s) {
    int n = 0;
    size_t a = 0;
    while (a < s.size()) {
        size_t e = s.find('\n', a);
        if (e == std::string::npos) e = s.size();
        if (s.compare(a, 6, "local ") == 0) ++n;
        a = e + 1;
    }
    return n;
}

static const char* const kV5Md5 = "1c57d7f9d5e18f8a73104b3651bc07ab";        // evidence/2026-10-31/E13/player_kit_template_v5.lua
static const char* const kV5EmptyMd5 = "39474a1c24c5fe8ec3897fc2f5b94f68";   // evidence/2026-10-31/E13/v5_empty.lua (gen_body.py)
static const size_t kV5EmptySize = 289562;
static const char* const kV4EmptyMd5 = "afea57af5c80424c8d03e6a189639940";   // test_faces3d.cpp test_real_v4 (5.0b live, empty list)

static void test_embedded() {
    const std::string t5 = template_v5_text();
    check(md5s(t5) == kV5Md5 && std::string(template_v5_md5()) == kV5Md5, "embedded v5 == the reviewed template 1c57d7f9 (" + md5s(t5) + ")");
    check(template_stock_md5(t5) == "9399e39a1e46a15d151ada423759abe9", "v5 anchors the same stock as v4");
    check(t5.find("FACE GATE v5") != std::string::npos && t5.find("TURBO_GATE.band = function") != std::string::npos,
          "v5 keeps the live-looks marker and names itself in the gate");
    check(t5.find("\r") == std::string::npos, "v5 text is LF");
    // the v4 part is unchanged: every v4 SPLICE row is still in v5
    const std::string t4 = template_v4_text();
    const size_t a = t4.find("\nSPLICE SPEC\n"), e = t4.find("\nEND SPEC\n");
    bool all = a != std::string::npos && e != std::string::npos;
    if (all) {
        size_t p = a + 13;
        while (p < e) {
            size_t q = t4.find('\n', p);
            const std::string row = t4.substr(p, q - p);
            if (!row.empty() && t5.find("\n" + row + "\n") == std::string::npos) all = false;
            p = q + 1;
        }
    }
    check(all, "every v4 splice row is kept in v5");
}

static void test_switch_file() {
    const fs::path d = fs::temp_directory_path() / ("turbo_e13_" + std::to_string(std::rand()));
    std::error_code ec;
    fs::remove_all(d, ec);  // std::rand is not seeded: a crashed earlier run may have left the same folder
    fs::create_directories(d / "faces3d", ec);
    check(hair_replace_on(d.u8string()), "no hair_replace_off.txt: hair replace on");
    write_file(d / "faces3d" / "hair_replace_off.txt", "");
    check(!hair_replace_on(d.u8string()), "faces3d\\hair_replace_off.txt present: hair replace off");
    fs::remove_all(d, ec);
}

static void test_real_v5() {
    const char* env = std::getenv("TURBO_RESEARCH_DIR");
    const fs::path research = env ? fs::u8path(env) : fs::current_path() / ".." / ".." / ".." / "research";
    const fs::path stock_p = research / ".." / "evidence" / "2026-10-15" / "bisect" / "stock_copy.lua";
    const fs::path pj_p = research / "c2_lua_loader" / "players_v3_test.json";
    std::string stock, pj;
    if (!read_file(stock_p, stock) || !read_file(pj_p, pj)) {
        std::printf("  SKIP real-stock v5 (research files not here)\n");
        return;
    }
    const Built e5 = generate_body(template_v5_text(), stock, Table{});
    check(e5.ok && e5.md5 == kV5EmptyMd5 && e5.bytes.size() == kV5EmptySize,
          "real stock v5, empty list == v5_empty.lua 39474a1c (the lupa-traced bytes): " + e5.md5 + " " + std::to_string(e5.bytes.size()) + e5.error);
    if (!e5.ok) return;
    check(lualoader::has_live_looks(e5.bytes.data(), e5.bytes.size()), "v5 body carries the live-looks marker");
    check(col0_locals(e5.bytes) == col0_locals(stock) + 1, "v5 main-chunk locals == stock + 1 (" + std::to_string(col0_locals(e5.bytes)) + " vs " +
                                                             std::to_string(col0_locals(stock)) + ")");
    // the splices landed where the trace saw them
    for (const char* s : {"\tif isGenericPlayer or faceGenMap ~= nil then ", "\tif normalMap ~= nil and normalGenMap == nil then",
                          "\tif specMaskMap ~= nil and specMaskGenMap == nil then", "local function _loadStarHead(items, initData, args, commonMaps, generatedTextures)",
                          "starhead_found = _loadStarHead(items, initData, args, commonMaps, generatedTextures)",
                          "\tlocal tgFaceGen, tgNormalGen, tgSpecGen = TURBO_GATE.cleanLoad(", "colorMap, tgFaceGen or nil, normalMap, tgNormalGen or nil,",
                          "specMaskMap, tgSpecGen or nil,", "TURBO_GATE.cleanLoad = function(", "\tcleanScan = true,"})
        check(e5.bytes.find(s) != std::string::npos, std::string("v5 body has: ") + s);
    // anchor refusal: one v5-only anchor md5 changed (the @ins row on stock line 1603)
    {
        std::string t = template_v5_text();
        const size_t r = t.find("\n@ins\t1603\t");
        check(r != std::string::npos, "v5 has the 1603 row");
        if (r != std::string::npos) {
            const size_t h = r + std::strlen("\n@ins\t1603\t");
            t[h] = t[h] == '0' ? '1' : '0';
            const Built x = generate_body(t, stock, Table{});
            check(!x.ok && x.bytes.empty() && x.error.find("anchor md5 mismatch at stock line 1603") != std::string::npos,
                  "v5 refuses a changed anchor (" + x.error + ")");
        }
    }
    std::string bad = stock;
    bad[300] = bad[300] == 'q' ? 'r' : 'q';
    const Built r = generate_body(template_v5_text(), bad, Table{});
    check(!r.ok && r.bytes.empty() && r.error.find("stock md5") != std::string::npos, "v5 refuses a changed stock");
    // the live serve
    Table t;
    std::string err;
    check(parse_players_json(pj, t, &err), "players_v3_test.json parses");
    std::string note, why;
    const auto on0 = build_live_override(stock, Table{}, note, why, CrestList{}, true);
    check(on0 && md5s(*on0) == kV5EmptyMd5 && note == "0 players on the saved list" && why.empty(), "build_live_override(hair_replace) == v5 empty body");
    const auto off0 = build_live_override(stock, Table{}, note, why, CrestList{}, false);
    check(off0 && md5s(*off0) == kV4EmptyMd5, "build_live_override(hair_replace off) == v4 afea57af exactly (switch file)");
    const auto def0 = build_live_override(stock, Table{}, note, why);
    check(def0 && md5s(*def0) == kV4EmptyMd5, "build_live_override default == v4 (callers that pass nothing keep 5.0b)");
    const auto on = build_live_override(stock, t, note, why, CrestList{}, true);
    const Built b5 = generate_body(template_v5_text(), stock, t);
    check(on && b5.ok && md5s(*on) == b5.md5 && note == "2 players on the saved list", "build_live_override(hair_replace), launch set == v5 launch set");
    const auto off = build_live_override(stock, t, note, why, CrestList{}, false);
    check(off && md5s(*off) == "83cceb6fa1d9b5f83561d95e21b677c8", "build_live_override(off), launch set == v4 83cceb6f exactly");
    std::string why2;
    check(!build_live_override(bad, t, note, why2, CrestList{}, true) && why2.find("stock md5") != std::string::npos,
          "build_live_override(hair_replace) refuses a changed stock: " + why2);

    // Lua 5.4: the body compiles, and the gate's region-mask lists
    const char* lua = std::getenv("LUA");
    const std::string luabin = lua && *lua ? lua : "lua5.4";
    if (std::system((luabin + " -v > " + (fs::temp_directory_path() / "turbo_lua_v5v.txt").string() + " 2>&1").c_str()) != 0) {
        std::printf("  SKIP v5 in Lua (%s not found)\n", luabin.c_str());
        return;
    }
    const fs::path body = fs::temp_directory_path() / ("turbo_v5_body_" + std::to_string(std::rand()) + ".lua");
    write_file(body, e5.bytes);
    const size_t ga = e5.bytes.find("local TURBO_GATE = {"), ge = e5.bytes.find("-- ===== END TURBO PER-PLAYER FACE GATE v5");
    check(ga != std::string::npos && ge != std::string::npos, "v5 gate block found in the body");
    if (ga == std::string::npos || ge == std::string::npos) return;
    std::string gate = e5.bytes.substr(ga, ge - ga);
    for (size_t p; (p = gate.find('\r')) != std::string::npos;) gate.erase(p, 1);
    std::string bp = body.string();
    for (auto& ch : bp)
        if (ch == '\\') ch = '/';
    const std::string drv = "local f, e = loadfile(\"" + bp + "\")\nprint(f and 'LUACOMPILE ok' or ('LUACOMPILE ' .. tostring(e)))\n" + gate + R"LUA(
local n, bad = 0, 0
local function ok(c, what) n = n + 1; if not c then bad = bad + 1; print("LUAFAIL " .. what) end end
local function s(t) return table.concat(t, ",") end
local G = TURBO_GATE
ok(G.cleanScan == true, "cleanScan default true")
ok(s(G.cleanMasks(25, nil)) == "0,3,0,3", "hair only: masks 0,3,0,3")
ok(s(G.cleanMasks(nil, 27)) == "1,2,5", "beard only: masks 1,2,5")
ok(s(G.cleanMasks(25, 27)) == "0,3,0,3,1,2,5", "hair + beard")
ok(#G.cleanMasks(nil, nil) == 0, "no part: no mask")
ok(s(G.keepMasks) == "4,6,7" and #G.maskNames == 8 and G.maskNames[1] == "mask_0" and G.maskNames[8] == "mask_7", "keep masks + names")
print("LUARESULT " .. n .. " " .. bad)
)LUA";
    const fs::path f = fs::temp_directory_path() / ("turbo_gate_v5_" + std::to_string(std::rand()) + ".lua");
    const fs::path outp = f.string() + ".out";
    write_file(f, drv);
    const int rc = std::system((luabin + " \"" + f.string() + "\" > \"" + outp.string() + "\" 2>&1").c_str());
    std::string out;
    read_file(outp, out);
    check(out.find("LUACOMPILE ok") != std::string::npos, "v5 body compiles in Lua 5.4");
    const size_t rr = out.find("LUARESULT ");
    int n = 0, nb = -1;
    if (rr != std::string::npos) std::sscanf(out.c_str() + rr, "LUARESULT %d %d", &n, &nb);
    check(rc == 0 && rr != std::string::npos && n == 6 && nb == 0, "v5 gate in Lua 5.4: " + std::to_string(n) + " checks, " + std::to_string(nb) + " failed");
    if (nb != 0) std::printf("%s\n", out.c_str());
    std::error_code ec;
    fs::remove(f, ec);
    fs::remove(outp, ec);
    fs::remove(body, ec);
}

int main() {
    test_embedded();
    test_switch_file();
    test_real_v5();
    std::printf("faces3d v5: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
