// Native tests (2.20 K2 crest-lua, brief 2.20-K2-crest-lua): the shirt-crest list turbo_output\kits\crest2d.json (core/faces3d.h
// CrestList), the crest splices of the ONE generated player_kit.lua, and the "Use my crest on the shirt" checkbox's work
// (ui/ui_crest_shirt.h crest_shirt::*). Acceptance tests of the brief:
//   (i)   an empty list builds byte-identical to today's output: golden md5s afea57af (no saved list) and 83cceb6f (the launch set),
//         the same values test_faces3d.cpp test_real_v4 pins;
//   (ii)  with [95] the diff against (i) is exactly the TURBO_CREST2D block, the _loadTeamCrest fragment and the crowd line (ranges diffed);
//   (iii) the generated file compiles in the suite's Lua 5.4 and _loadTeamCrest, run on stub loaders with a strict initData, calls the
//         2D crest loader for a listed club and the stock bundle crest for the others;
//   (iv)  the crowd's kitData (no TeamId, no KitColorJerseyPri) never raises;
//   (v)   the faces band pins stay in test_faces3d.cpp (CATALOG run_faces3d), unchanged.
// The real-stock cases need the stock player_kit.lua (9399e39a) from turbo_dev\evidence (EA source never enters the repository):
// found from TURBO_RESEARCH_DIR (as test_faces3d.cpp) or by walking up from the run folder; "SKIP" is printed when it is not there.
// Included by test_main.cpp; crest2d_test::run() is called from main next to crest_serve_test::run().
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/faces3d.h"
#include "core/lua_loader.h"
#include "ui/ui_crest_shirt.h"

namespace crest2d_test {

namespace f3 = turbo::faces3d;

inline std::string md5s(const std::string& s) { return turbo::lualoader::md5_hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

inline bool read_file_ok(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

inline std::vector<std::string> split_lines(const std::string& s, const std::string& nl) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find(nl, a);
        if (b == std::string::npos) {
            out.push_back(s.substr(a));
            return out;
        }
        out.push_back(s.substr(a, b - a));
        a = b + nl.size();
    }
}

// turbo_dev\evidence\2026-10-15\bisect\stock_copy.lua (md5 9399e39a), and the research folder of test_faces3d.cpp
inline fs::path find_turbo_dev() {
    if (const char* env = std::getenv("TURBO_RESEARCH_DIR"))
        if (*env) return fs::u8path(env) / "..";
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::exists(p / "evidence" / "2026-10-15" / "bisect" / "stock_copy.lua", ec)) return p;
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return fs::path();
}

inline const char* lua_bin() {
    const char* e = std::getenv("TURBO_LUA");
    if (e && *e) return e;
    e = std::getenv("LUA");  // the variable test_faces3d's Lua gate reads
    return (e && *e) ? e : "lua5.4";
}

// Runs `lua driver args...` and returns its output; rc = the exit code
inline std::string run_lua_file(const fs::path& driver, const std::vector<fs::path>& args, const fs::path& out, int& rc) {
#ifdef _WIN32
    std::string cmd = "\"\"" + std::string(lua_bin()) + "\" \"" + driver.string() + "\"";
    for (const auto& a : args) cmd += " \"" + a.string() + "\"";
    cmd += " > \"" + out.string() + "\" 2>&1\"";
#else
    std::string cmd = "'" + std::string(lua_bin()) + "' '" + driver.string() + "'";
    for (const auto& a : args) cmd += " '" + a.string() + "'";
    cmd += " > '" + out.string() + "' 2>&1";
#endif
    rc = std::system(cmd.c_str());
    return read_file(out);
}

// The driver: the whole generated file must compile; the slice (_luminance, _loadCrestForWardrobe, the TURBO_CREST2D block,
// _loadTeamCrest) runs against recording stubs
inline const char* kDriver = R"LUA(
local whole, slice = arg[1], arg[2]
local n, bad = 0, 0
local function ok(c, what) n = n + 1; if not c then bad = bad + 1; print("LUAFAIL " .. what) end end
local function readall(p) local f = assert(io.open(p, "rb")); local s = f:read("a"); f:close(); return s end
local f, e = load(readall(whole), "=player_kit.lua")
ok(f ~= nil, "the whole generated player_kit.lua compiles in Lua 5.4: " .. tostring(e))
local calls = {}
brts = { kit = "KIT" }
SETTING = { DecalsUseExtraMaps = true }
function loadAsync(brt, name, miss, lane) calls[#calls + 1] = "async " .. name; return "A:" .. name end
function loadRuntimeDDS(name) calls[#calls + 1] = "dds " .. name; return "D:" .. name end
local loadTeamCrest = assert(load(readall(slice), "=slice"))()
local function strict(t) return setmetatable(t, { __index = function(_, k) error("error: invalid read, no key in the table: " .. k) end }) end
local function P(team, crest, colour)
  return strict({ TeamId = team, Kit = team, KitType = 0, KitYear = 0, IsGeneric = 0, CrestAssetId = crest or -1,
                  KitColorJerseyPri = colour or { 0.9, 0.9, 0.9, 1 }, laneId = 3 })
end
local function run(initData, prefix) calls = {}; local okc, c = pcall(loadTeamCrest, initData, prefix or "crest"); return okc, c end
ok(type(TURBO_CREST2D) == "table" and TURBO_CREST2D[95] == true and TURBO_CREST2D[113974] == true and TURBO_CREST2D[96] == nil, "TURBO_CREST2D lists 95 and 113974")
-- (iii) a listed club: the 2D crest through the stock generic branch, folder by the jersey colour
local okc, c = run(P(95))
ok(okc and c == "D:data/ui/imgAssets/crest/light/l95.dds;data/ui/imgAssets/crest/dark/notfound.dds", "listed 95, light jersey: crest/light/l95.dds (" .. tostring(c) .. ")")
ok(calls[2] == "async crest_fallback_coeff" and calls[3] == "async crest_fallback_normal" and #calls == 3, "listed 95: the fallback coeff / normal of the generic branch")
okc, c = run(P(95, -1, { 0.05, 0.05, 0.05, 1 }), "crest_shorts")
ok(okc and c == "D:data/ui/imgAssets/crest/dark/l95.dds;data/ui/imgAssets/crest/dark/notfound.dds", "listed 95, shorts, dark jersey: crest/dark/l95.dds (" .. tostring(c) .. ")")
okc, c = run(P(113974))
ok(okc and c == "D:data/ui/imgAssets/crest/light/l113974.dds;data/ui/imgAssets/crest/dark/notfound.dds", "listed 113974")
-- not listed: the stock bundle crest
okc, c = run(P(96))
ok(okc and c == "A:crest_96_0_0_color;crest_96_0_0_color;crest_0_0_0_color" and calls[2] == "async crest_96_0_0_coeff;crest_fallback_coeff", "unlisted 96: stock bundle crest (" .. tostring(c) .. ")")
okc, c = run(P(96), "crest_shorts")
ok(okc and c == "A:crest_shorts_96_0_0_color;crest_96_0_0_color;crest_0_0_0_color", "unlisted 96, shorts: stock bundle crest")
-- a listed club with a custom crest id keeps the stock custom-crest path
okc, c = run(P(95, 7))
ok(okc and c == "D:data/ui/imgAssets/crest/custom/l7.dds;data/ui/imgAssets/crest/ssf/l7.dds;data/ui/imgAssets/crest/ssf/notfound.dds", "listed 95 with CrestAssetId 7: stock custom crest")
-- a strict initData without TeamId never raises (rawget)
okc, c = run(strict({ Kit = 95, KitType = 0, KitYear = 0, IsGeneric = 0, CrestAssetId = -1, laneId = 1 }))
ok(okc and c == "A:crest_95_0_0_color;crest_95_0_0_color;crest_0_0_0_color", "strict initData without TeamId: no error, stock")
-- (iv) the crowd's kitData (player_kit.lua loadLowResCrowd): no TeamId, no KitColorJerseyPri, no IsGeneric
local kd = { laneId = 1, CrestAssetId = -1, Kit = 95, KitType = 0, KitYear = 0, isCustomKit = 0 }
okc, c = run(kd)
--@@CROWD@@
ok(rawget(kd, "TeamId") == nil and rawget(kd, "KitColorJerseyPri") == nil and kd.Kit == 95, "the crowd's kitData is not written")
okc, c = run({ laneId = 1, CrestAssetId = -1, Kit = 96, KitType = 0, KitYear = 0, isCustomKit = 0 })
ok(okc and c == "A:crest_96_0_0_color;crest_96_0_0_color;crest_0_0_0_color", "crowd kitData of unlisted 96: stock bundle crest")
-- the generic flag set by EA keeps working; the list off (global gone) = stock for everyone
okc, c = run(strict({ TeamId = 96, Kit = 96, KitType = 0, KitYear = 0, IsGeneric = 1, CrestAssetId = -1, KitColorJerseyPri = { 0.9, 0.9, 0.9, 1 }, laneId = 3 }))
ok(okc and c == "D:data/ui/imgAssets/crest/light/l96.dds;data/ui/imgAssets/crest/dark/notfound.dds", "EA generic kit 96: stock generic branch")
TURBO_CREST2D = nil
okc, c = run(P(95))
ok(okc and c == "A:crest_95_0_0_color;crest_95_0_0_color;crest_0_0_0_color", "TURBO_CREST2D gone: stock for a listed club, no error")
print("LUARESULT " .. n .. " " .. bad)
)LUA";

// Crowd expectation (commit B): a listed club in the crowd gets its 2D crest, dark folder unless TeamColorPri says light
inline const char* kCrowdExpect = R"LUA(ok(okc and c == "D:data/ui/imgAssets/crest/dark/l95.dds;data/ui/imgAssets/crest/dark/notfound.dds", "(iv) crowd kitData of listed 95: no error, the 2D crest, dark folder (" .. tostring(c) .. ")")
ok(calls[2] == "async crest_fallback_coeff" and calls[3] == "async crest_fallback_normal" and #calls == 3, "(iv) crowd 95: the generic branch's fallback coeff / normal")
local kd2 = { laneId = 2, CrestAssetId = -1, Kit = 95, KitType = 0, KitYear = 0, isCustomKit = 0, TeamColorPri = { 0.9, 0.9, 0.9, 1 } }
okc, c = run(kd2)
ok(okc and c == "D:data/ui/imgAssets/crest/light/l95.dds;data/ui/imgAssets/crest/dark/notfound.dds", "(iv) crowd 95 with a light TeamColorPri: light folder (" .. tostring(c) .. ")")
okc, c = run({ laneId = 1, CrestAssetId = -1, Kit = "95", KitType = 0, KitYear = 0 })
ok(not okc or string.sub(c, 1, 2) == "A:", "(iv) a crowd Kit that is not a number never takes the branch (" .. tostring(c) .. ")")
okc, c = run({ laneId = 1, CrestAssetId = 7, Kit = 95, KitType = 0, KitYear = 0, isCustomKit = 0 })
ok(okc and c == "D:data/ui/imgAssets/crest/custom/l7.dds;data/ui/imgAssets/crest/ssf/l7.dds;data/ui/imgAssets/crest/ssf/notfound.dds", "(iv) crowd 95 with a custom crest id: stock custom path")
)LUA";
// The crowd line of commit B, as generate_body writes it (faces3d.cpp kCrestCrowdAdd)
inline const char* kCrowdLine =
    "\tif not is_generic_kit and TURBO_CREST2D ~= nil and getmetatable(initData) == nil and rawget(initData, \"TeamId\") == nil and "
    "type(rawget(initData, \"Kit\")) == \"number\" and TURBO_CREST2D[initData.Kit] == true and type(rawget(initData, \"CrestAssetId\")) == "
    "\"number\" and initData.CrestAssetId < 1 then initData = { TeamId = initData.Kit, CrestAssetId = initData.CrestAssetId, "
    "KitColorJerseyPri = rawget(initData, \"TeamColorPri\") or { 0.05, 0.05, 0.05, 1 }, laneId = rawget(initData, \"laneId\") }; "
    "is_generic_kit = true end  -- TURBO shirt crest, crowd";

inline std::string slice(const std::string& s, const char* from, const char* to) {
    const size_t a = s.find(from);
    const size_t b = a == std::string::npos ? std::string::npos : s.find(to, a);
    return (a == std::string::npos || b == std::string::npos) ? std::string() : s.substr(a, b - a);
}

inline void run() {
    run_case("crest2d: the list file (parse, write, load, save)", [] {
        f3::CrestList c;
        std::string err;
        const f3::CrestList want{95, 113974};
        CHECK(f3::parse_crest2d_json("{\"teams\": [95, 113974, 95]}", c, &err) && c == want, "parse: 95, 113974 (dupes folded)");
        CHECK(f3::crest2d_json(c) == "{\"teams\": [95, 113974]}\n", "write: sorted, one line");
        CHECK(f3::parse_crest2d_json(f3::crest2d_json(c), c, &err) && c.size() == 2, "round trip");
        CHECK(f3::parse_crest2d_json("{\"teams\": []}", c, &err) && c.empty(), "an empty list parses");
        for (const char* bad : {"[]", "{}", "{\"teams\": 95}", "{\"teams\": [0]}", "{\"teams\": [-3]}", "{\"teams\": [\"95\"]}", "{\"teams\": [1.5]}",
                                "{\"teams\": [2147483648]}", "not json"}) {
            f3::CrestList keep{7};
            err.clear();
            CHECK(!f3::parse_crest2d_json(bad, keep, &err) && keep == f3::CrestList{7} && !err.empty(), std::string("refused, list untouched: ") + bad);
        }
        const fs::path root = test_tmp_dir("crest2d_files");
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
        CHECK(f3::load_crest2d(root.u8string(), c, &err) && c.empty(), "no file = an empty list");
        CHECK(f3::save_crest2d(root.u8string(), f3::CrestList{95}, &err) && read_file(root / "kits" / "crest2d.json") == "{\"teams\": [95]}\n",
              "save writes kits\\crest2d.json");
        CHECK(f3::load_crest2d(root.u8string(), c, &err) && c == f3::CrestList{95}, "load reads it back");
        CHECK(f3::save_crest2d(root.u8string(), f3::CrestList{}, &err) && !fs::exists(root / "kits" / "crest2d.json"), "an empty list deletes the file");
        {
            std::ofstream o(root / "kits" / "crest2d.json", std::ios::binary);
            o << "{broken";
        }
        CHECK(!f3::load_crest2d(root.u8string(), c, &err) && !err.empty(), "a damaged file is refused: " + err);
        fs::remove_all(root, ec);
    });

    run_case("crest2d: the checkbox's work (on, off, one undo)", [] {
        namespace cs = turbo::crest_shirt;
        const fs::path root = test_tmp_dir("crest2d_toggle");
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
        const std::string r = root.u8string();
        cs::reset_for_tests();
        std::string msg;
        CHECK(!cs::is_on(r, 95) && !cs::can_undo(), "starts off, nothing to undo");
        CHECK(cs::set(r, 95, true, msg) && cs::is_on(r, 95) && cs::can_undo() && msg.find("next game start") != std::string::npos, "on: " + msg);
        CHECK(cs::set(r, 113974, true, msg) && cs::is_on(r, 113974) && cs::is_on(r, 95), "a second club, the first stays");
        CHECK(cs::undo(r, msg) && !cs::is_on(r, 113974) && cs::is_on(r, 95) && !cs::can_undo(), "undo puts the list back as it was: " + msg);
        CHECK(!cs::undo(r, msg), "one step only");
        CHECK(cs::set(r, 95, false, msg) && !cs::is_on(r, 95) && !fs::exists(root / "kits" / "crest2d.json"), "off: the last club off removes the file");
        CHECK(cs::undo(r, msg) && cs::is_on(r, 95), "undo of off");
        CHECK(!cs::set(r, 0, true, msg), "no club chosen: nothing written");
        {
            std::ofstream o(root / "kits" / "crest2d.json", std::ios::binary);
            o << "{broken";
        }
        CHECK(!cs::set(r, 96, true, msg) && read_file(root / "kits" / "crest2d.json") == "{broken", "a damaged list is never overwritten: " + msg);
        for (const std::string& m : {msg})
            for (const char* w : {"json", "lua", "Lua", "hook", ".txt"}) CHECK(m.find(w) == std::string::npos, std::string("plain words: no '") + w + "'");
        cs::reset_for_tests();
        fs::remove_all(root, ec);
    });

    run_case("crest2d: synthetic stock refuses a list, the faces never pay for it", [] {
        // a non-empty list on a stock the crest change does not know: generate_body refuses, build_live_override drops the list
        f3::CrestList one{95};
        const f3::Built b = f3::generate_body(f3::template_v4_text(), "not the stock", f3::Table{}, one);
        CHECK(!b.ok && b.error.find("stock md5") != std::string::npos, "a changed stock is refused by the template first: " + b.error);
    });

    const fs::path dev = find_turbo_dev();
    std::string stock, pj;
    if (dev.empty() || !read_file_ok(dev / "evidence" / "2026-10-15" / "bisect" / "stock_copy.lua", stock) ||
        !read_file_ok(dev / "research" / "c2_lua_loader" / "players_v3_test.json", pj)) {
        std::printf("  SKIP crest2d real-stock cases (turbo_dev\\evidence stock_copy.lua or research players_v3_test.json not found)\n");
        return;
    }

    run_case("crest2d (i): an empty list = today's output, byte for byte", [&] {
        CHECK(md5s(stock) == "9399e39a1e46a15d151ada423759abe9", "the real stock is 9399e39a");
        f3::Table t;
        std::string err;
        CHECK(f3::parse_players_json(pj, t, &err), "players_v3_test.json parses");
        const f3::Built e = f3::generate_body(f3::template_v4_text(), stock, f3::Table{}, f3::CrestList{});
        CHECK(e.ok && e.md5 == "afea57af5c80424c8d03e6a189639940" && e.bytes.size() == 282530, "golden: no saved list, no crest list = afea57af (" + e.md5 + ")");
        const f3::Built l = f3::generate_body(f3::template_v4_text(), stock, t, f3::CrestList{});
        CHECK(l.ok && l.md5 == "83cceb6fa1d9b5f83561d95e21b677c8", "golden: the launch set, no crest list = 83cceb6f (" + l.md5 + ")");
        CHECK(f3::generate_body(f3::template_v4_text(), stock, t).md5 == l.md5, "the default argument = an empty list");
        std::string note, why;
        const auto mem = f3::build_live_override(stock, t, note, why);
        CHECK(mem && md5s(*mem) == l.md5 && note == "2 players on the saved list", "build_live_override without a list: today's bytes and note");
        const auto mem0 = f3::build_live_override(stock, f3::Table{}, note, why, f3::CrestList{});
        CHECK(mem0 && md5s(*mem0) == e.md5 && note == "0 players on the saved list", "build_live_override with an empty list: today's bytes and note");
        CHECK(e.bytes.find("TURBO_CREST2D") == std::string::npos, "no crest word in today's output");
    });

    run_case("crest2d (ii): with [95] only the crest block, the _loadTeamCrest fragment and the crowd line differ", [&] {
        f3::Table t;
        std::string err;
        f3::parse_players_json(pj, t, &err);
        for (const f3::Table& tab : {f3::Table{}, t}) {
            const f3::Built a = f3::generate_body(f3::template_v4_text(), stock, tab);
            const f3::Built b = f3::generate_body(f3::template_v4_text(), stock, tab, f3::CrestList{95});
            CHECK(a.ok && b.ok, "both build (" + b.error + ")");
            if (!a.ok || !b.ok) continue;
            const std::vector<std::string> A = split_lines(a.bytes, "\r\n"), B = split_lines(b.bytes, "\r\n");
            size_t p = 0;
            while (p < A.size() && p < B.size() && A[p] == B[p]) ++p;
            size_t q = 0;
            while (q < A.size() - p && q < B.size() - p && A[A.size() - 1 - q] == B[B.size() - 1 - q]) ++q;
            const size_t ea = A.size() - q, eb = B.size() - q;
            // A[p, ea) = stock 2603..2610 (_loadTeamCrest's head down to is_generic_kit); B[p, eb) = the block, the same lines, the fragment
            CHECK(ea > p && A[p] == "local function _loadTeamCrest(initData, prefix)" && A[ea - 1] == "\tlocal is_generic_kit = initData.IsGeneric == 1" &&
                      ea - p == 8,
                  "the changed range of today's output is _loadTeamCrest's head .. is_generic_kit (" + std::to_string(ea - p) + " lines)");
            // commit B: B[p, eb) = the 6-line block, the same 8 lines (the last with the fragment), the crowd line
            CHECK((eb - p) - (ea - p) == 7, "7 lines added in the changed range (6 block + 1 crowd): " + std::to_string((eb - p) - (ea - p)));
            const size_t block = 6;
            bool block_ok = (eb - p) - (ea - p) == 7;
            for (size_t i = 0; i < block && block_ok; ++i) {
                const std::string& s = B[p + i];
                block_ok = i < 3 ? s.compare(0, 3, "-- ") == 0 : (i == 3 ? s == "TURBO_CREST2D = {" : (i == 4 ? s == "\t[95] = true," : s == "}"));
            }
            CHECK(block_ok, "the added block is the TURBO_CREST2D global table, nothing else");
            bool same = block_ok;
            for (size_t i = 0; same && i + 1 < ea - p; ++i) same = B[p + block + i] == A[p + i];
            CHECK(same, "_loadTeamCrest's lines down to is_generic_kit are untouched");
            const std::string frag = " or (TURBO_CREST2D ~= nil and TURBO_CREST2D[rawget(initData, \"TeamId\")] == true and initData.CrestAssetId < 1)";
            CHECK(eb >= 2 && B[eb - 2] == A[ea - 1] + frag, "is_generic_kit gets the one fragment: " + (eb >= 2 ? B[eb - 2] : std::string()));
            CHECK(eb >= 1 && B[eb - 1] == kCrowdLine, "the crowd line follows is_generic_kit: " + (eb >= 1 ? B[eb - 1] : std::string()));
            CHECK(A.size() + 7 == B.size(), "nothing else differs (" + std::to_string(B.size() - A.size()) + " lines added)");
            // and the other way round: take the block, the fragment and the crowd line out again, the bytes are (i)'s
            std::string back = b.bytes;
            const size_t bp = back.find("-- TURBO shirt crest");
            const size_t be = back.find("}\r\n", bp);
            if (bp != std::string::npos && be != std::string::npos) back.erase(bp, be + 3 - bp);
            const size_t fp = back.find(frag);
            if (fp != std::string::npos) back.erase(fp, frag.size());
            const size_t cp = back.find("\r\n" + std::string(kCrowdLine));
            if (cp != std::string::npos) back.erase(cp, 2 + std::strlen(kCrowdLine));
            CHECK(back == a.bytes, "block, fragment and crowd line taken out = (i) byte for byte");
            // the faces gate is in the common prefix: not one byte of the band changed
            CHECK(a.bytes.find("END TURBO PER-PLAYER FACE GATE") < a.bytes.find("local function _loadTeamCrest") &&
                      b.bytes.compare(0, b.bytes.find("-- TURBO shirt crest"), a.bytes, 0, b.bytes.find("-- TURBO shirt crest")) == 0,
                  "the faces gate and everything before the crest block are byte-identical");
        }
        // a two-club list adds one line per club, sorted
        const f3::Built two = f3::generate_body(f3::template_v4_text(), stock, f3::Table{}, f3::CrestList{113974, 95});
        CHECK(two.ok && two.bytes.find("TURBO_CREST2D = {\r\n\t[95] = true,\r\n\t[113974] = true,\r\n}\r\n") != std::string::npos, "two clubs, sorted, CRLF");
        CHECK(!f3::generate_body(f3::template_v4_text(), stock, f3::Table{}, f3::CrestList{0}).ok, "club id 0 is refused");
        // a list the build refuses never costs the faces (build_live_override drops it)
        std::string note, why;
        const auto mem = f3::build_live_override(stock, t, note, why, f3::CrestList{0});
        const f3::Built l = f3::generate_body(f3::template_v4_text(), stock, t);
        CHECK(mem && md5s(*mem) == l.md5 && note.find("shirt-crest list refused") != std::string::npos, "refused list: the faces build as today (" + note + ")");
        const auto mem95 = f3::build_live_override(stock, t, note, why, f3::CrestList{95});
        CHECK(mem95 && *mem95 == f3::generate_body(f3::template_v4_text(), stock, t, f3::CrestList{95}).bytes &&
                  note == "2 players on the saved list; 1 club on the shirt-crest list",
              "build_live_override with [95]: the crest build, note says so (" + note + ")");
        f3::Table junk;
        junk[5] = f3::Entry{-5, 0, true, true, ""};
        const auto mem3 = f3::build_live_override(stock, junk, note, why, f3::CrestList{95});
        CHECK(mem3 && *mem3 == f3::generate_body(f3::template_v4_text(), stock, f3::Table{}, f3::CrestList{95}).bytes &&
                  note.find("saved list refused") != std::string::npos && note.find("1 club on the shirt-crest list") != std::string::npos,
              "a bad saved list falls back to the bands and keeps the crest list (" + note + ")");
    });

    run_case("crest2d (iii)+(iv): the generated file in Lua 5.4", [&] {
        const fs::path dir = test_tmp_dir("crest2d_lua");
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        int rc = 0;
#ifdef _WIN32
        // cmd.exe strips the first and the last quote of the line: the whole line is quoted once more (as in run_lua_file)
        const std::string vcmd = "\"\"" + std::string(lua_bin()) + "\" -v > \"" + (dir / "v.txt").string() + "\" 2>&1\"";
#else
        const std::string vcmd = "'" + std::string(lua_bin()) + "' -v > '" + (dir / "v.txt").string() + "' 2>&1";
#endif
        // run_native.sh needs lua5.4 (its header): a missing interpreter fails here, never a silent pass of (iii)/(iv)
        if (std::system(vcmd.c_str()) != 0) {
            CHECK(false, std::string("the Lua interpreter runs: ") + lua_bin() + " -v failed (set LUA or TURBO_LUA)");
            return;
        }
        f3::Table t;
        std::string err;
        f3::parse_players_json(pj, t, &err);
        const f3::Built b = f3::generate_body(f3::template_v4_text(), stock, t, f3::CrestList{95, 113974});
        CHECK(b.ok, "the crest build (" + b.error + ")");
        const std::string s = slice(b.bytes, "local function _luminance(sRGB4)", "local function _color4To3") +
                              slice(b.bytes, "local function _loadCrestForWardrobe", "local function _loadBrandMaps") +
                              "\r\nreturn _loadTeamCrest\r\n";
        CHECK(s.find("TURBO_CREST2D = {") != std::string::npos && s.find("local function _loadTeamCrest") != std::string::npos, "slice found");
        std::string drv = kDriver;
        const size_t m = drv.find("--@@CROWD@@\n");
        drv.replace(m, std::strlen("--@@CROWD@@\n"), kCrowdExpect);
        const fs::path whole = dir / "player_kit.lua", sl = dir / "slice.lua", dv = dir / "driver.lua", out = dir / "out.txt";
        {
            std::ofstream(whole, std::ios::binary) << b.bytes;
            std::ofstream(sl, std::ios::binary) << s;
            std::ofstream(dv, std::ios::binary) << drv;
        }
        const std::string o = run_lua_file(dv, {whole, sl}, out, rc);
        const size_t r = o.find("LUARESULT ");
        int n = 0, bad = -1;
        if (r != std::string::npos) std::sscanf(o.c_str() + r, "LUARESULT %d %d", &n, &bad);
        CHECK(rc == 0 && r != std::string::npos && n >= 15 && bad == 0, "Lua 5.4: " + std::to_string(n) + " checks, " + std::to_string(bad) + " failed");
        if (bad != 0) std::printf("%s\n", o.c_str());
        // today's output (no list) compiles too and has no crest global: the driver's slice needs TURBO_CREST2D, so only compile it
        const f3::Built e = f3::generate_body(f3::template_v4_text(), stock, t);
        {
            std::ofstream(whole, std::ios::binary) << e.bytes;
            std::ofstream(dv, std::ios::binary) << "local f = io.open(arg[1], 'rb'); local s = f:read('a'); f:close()\n"
                                                   "local c, e = load(s, '=player_kit.lua'); print(c and 'COMPILED' or ('NOCOMPILE ' .. tostring(e)))\n";
        }
        CHECK(run_lua_file(dv, {whole}, out, rc).find("COMPILED") != std::string::npos && rc == 0, "today's output compiles (control)");
        fs::remove_all(dir, ec);
    });

    // Pit wall 12:47, conditions 1 and 2 of the lua_loader_win.cpp CHANGE: the boot read next to players.json
    run_case("crest2d: the boot read (lua_loader_win.cpp) - no list = today's bytes, a damaged list = one line, faces untouched", [&] {
        const fs::path root = test_tmp_dir("crest2d_boot");
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "faces3d", ec);
        fs::create_directories(root / "kits", ec);
        std::ofstream(root / "faces3d" / "players.json", std::ios::binary) << pj;
        const std::string r = root.u8string();
        f3::Table saved;
        std::string err, line, note, why;
        CHECK(f3::load_players(r, saved, &err) && saved.size() == 2, "the saved faces list loads (2 players)");
        const f3::Table saved0 = saved;
        const std::string today = f3::generate_body(f3::template_v4_text(), stock, saved).bytes;
        CHECK(md5s(today) == "83cceb6fa1d9b5f83561d95e21b677c8", "today's bytes for this saved list = 83cceb6f");
        // condition 1: missing file and an empty list = no line, byte-identical to today's build
        f3::CrestList c = f3::load_crest2d_for_boot(r, line);
        CHECK(c.empty() && line.empty(), "missing kits\\crest2d.json: no list, no log line");
        auto m = f3::build_live_override(stock, saved, note, why, c);
        CHECK(m && *m == today && note == "2 players on the saved list", "missing file: build_live_override = today's bytes and note (" + note + ")");
        std::ofstream(root / "kits" / "crest2d.json", std::ios::binary) << "{\"teams\": []}\n";
        c = f3::load_crest2d_for_boot(r, line);
        CHECK(c.empty() && line.empty(), "an empty list file: no list, no log line");
        m = f3::build_live_override(stock, saved, note, why, c);
        CHECK(m && *m == today && note == "2 players on the saved list", "empty list: build_live_override = today's bytes and note");
        // condition 2: a damaged file = no list, exactly one line, the saved faces list untouched and built as today
        std::ofstream(root / "kits" / "crest2d.json", std::ios::binary) << "{\"teams\": [95,";
        c = f3::load_crest2d_for_boot(r, line);
        CHECK(c.empty() && !line.empty() && line.find('\n') == std::string::npos && line.find("unreadable") != std::string::npos &&
                  line.rfind("lua_loader: ", 0) == 0,
              "a damaged list: no list and one log line: " + line);
        f3::Table again;
        CHECK(f3::load_players(r, again, &err) && f3::players_json(again) == f3::players_json(saved0), "the saved faces list reads the same after it");
        m = f3::build_live_override(stock, saved, note, why, c);
        CHECK(m && *m == today && note == "2 players on the saved list", "a damaged list: the faces build is today's, byte for byte");
        // and a good list after it is carried (the boot path end to end, minus the hook)
        std::ofstream(root / "kits" / "crest2d.json", std::ios::binary) << "{\"teams\": [95]}\n";
        c = f3::load_crest2d_for_boot(r, line);
        m = f3::build_live_override(stock, saved, note, why, c);
        CHECK(line.empty() && m && *m == f3::generate_body(f3::template_v4_text(), stock, saved, f3::CrestList{95}).bytes &&
                  note == "2 players on the saved list; 1 club on the shirt-crest list",
              "list [95]: the boot build carries it (" + note + ")");
        fs::remove_all(root, ec);
    });

    run_case("crest2d: the research Apply carries the list (and never fails the faces for it)", [&] {
        const fs::path root = test_tmp_dir("crest2d_apply");
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "faces3d", ec);
        CHECK(std::string(turbo::lualoader::kStockCacheRel) == "faces3d\\stock_player_kit.cache", "the stock cache name");
        std::ofstream(root / "faces3d" / "stock_player_kit.cache", std::ios::binary) << stock;
        const std::string r = root.u8string();
        f3::ApplyResult a = f3::apply(r, f3::Table{}, "6AC07E31-2145C000", nullptr);
        CHECK(a.ok && read_file(root / "lua_override" / "player_kit.lua").find("TURBO_CREST2D") == std::string::npos, "no list: no crest block (" + a.message + ")");
        std::string err;
        f3::save_crest2d(r, f3::CrestList{95}, &err);
        a = f3::apply(r, f3::Table{}, "6AC07E31-2145C000", nullptr);
        CHECK(a.ok && read_file(root / "lua_override" / "player_kit.lua").find("TURBO_CREST2D = {\r\n\t[95] = true,") != std::string::npos &&
                  a.message.find("1 club on the shirt-crest list") != std::string::npos,
              "list [95]: the override carries it (" + a.message + ")");
        {
            std::ofstream(root / "kits" / "crest2d.json", std::ios::binary) << "{broken";
        }
        a = f3::apply(r, f3::Table{}, "6AC07E31-2145C000", nullptr);
        CHECK(a.ok && read_file(root / "lua_override" / "player_kit.lua").find("TURBO_CREST2D") == std::string::npos &&
                  a.message.find("Shirt-crest list left out") != std::string::npos,
              "a damaged list is left out, the faces are written (" + a.message + ")");
        fs::remove_all(root, ec);
    });
}

}  // namespace crest2d_test
