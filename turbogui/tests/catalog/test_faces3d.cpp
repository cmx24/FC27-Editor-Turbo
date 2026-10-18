// faces3d (core/faces3d.h): players.json model, verified ids, validation, the player_kit.lua generator against a SYNTHETIC stock and
// template (anchors computed here), every refusal, the header, apply/remove on a temp folder; and, when the research files are on
// this machine, a byte-compare of the generator against the F2 override that ran in game (stock read at run time from the
// evidence folder, never copied into the repository; SKIP otherwise).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << b;
}

// ---------------------------------------------------------------- synthetic stock + template
static const std::vector<std::string> kStockLines = {
    "local brts = ...",                          // 1
    "local function colours(initData)",          // 2
    "\tlocal isGeneric = initData.HeadClass ~= 0",  // 3
    "\treturn isGeneric",                        // 4
    "end",                                       // 5
    "local function hair(initData)",             // 6
    "\tlocal id = initData.Hair",                // 7
    "\treturn id",                               // 8
    "end",                                       // 9
    "return { colours = colours, hair = hair }",  // 10
    "",                                          // 11 (trailing newline)
};
static std::string stock_text(const std::string& nl) {
    std::string s;
    for (size_t i = 0; i < kStockLines.size(); ++i) s += (i ? nl : "") + kStockLines[i];
    return s;
}
static std::string lmd5(int n) { return md5s(kStockLines[n - 1]); }

struct TOpts {
    std::string stock_md5;
    std::string gate_h;           // default lmd5(1)
    std::string ins3_h;           // default lmd5(3)
    long long ins3_col = 42;      // after "...HeadClass ~= 0"
    std::string ins3_text = " or GATE(initData)$";
    bool entries_marker = true;
    bool second_marker = false;
};
static std::string make_template(const TOpts& o) {
    std::string t = "--[==[synthetic test template\n\nSPLICE SPEC\n";
    t += "@stock_md5\t" + o.stock_md5 + "\n";
    t += "@gate_after\t1\t" + (o.gate_h.empty() ? lmd5(1) : o.gate_h) + "\n";
    t += "@ins\t3\t" + (o.ins3_h.empty() ? lmd5(3) : o.ins3_h) + "\t" + std::to_string(o.ins3_col) + "\t" + o.ins3_text + "\n";
    t += "@ins\t7\t" + lmd5(7) + "\t12\t(H(initData) or $\n";
    t += "@ins\t7\t" + lmd5(7) + "\t25\t)$\n";
    t += "@add\t6\t" + lmd5(6) + "\t\tlocal tg = 1  $\n";
    t += "END SPEC\n]==]\n--@@GATE-BEGIN\n-- gate\nlocal G = { entries = {\n";
    if (o.entries_marker) t += "--@@ENTRIES@@\n";
    if (o.second_marker) t += "--@@ENTRIES@@\n";
    t += "} }\n--@@GATE-END\n";
    return t;
}

static void test_synthetic_generator() {
    for (const std::string& nl : {std::string("\r\n"), std::string("\n")}) {
        const std::string stock = stock_text(nl);
        TOpts o;
        o.stock_md5 = md5s(stock);
        const std::string tmpl = make_template(o);
        Table t;
        t[200] = Entry{0, 46, true, false, "B\xC3\xA9rnard \"x\""};
        t[100] = Entry{155, 0, false, true, ""};
        t[300] = Entry{0, 0, true, true, "nothing"};  // dropped (neither)
        const Built b = generate_body(tmpl, stock, t);
        check(b.ok, "synthetic body builds (" + b.error + ")");
        std::string want;
        auto add = [&](const std::string& l) { want += (want.empty() ? "" : nl) + l; };
        add(kStockLines[0]);
        add("-- gate");
        add("local G = { entries = {");
        add("\t\t[100] = { hair = 155, hc = false, bc = true },");
        add("\t\t[200] = { beard = 46, hc = true, bc = false },  -- Brnard x");
        add("} }");
        add(kStockLines[1]);
        add("\tlocal isGeneric = initData.HeadClass ~= 0 or GATE(initData)");
        add(kStockLines[3]);
        add(kStockLines[4]);
        add(kStockLines[5] + nl + "\tlocal tg = 1  ");
        add("\tlocal id = (H(initData) or initData.Hair)");
        for (int i = 8; i <= 11; ++i) add(kStockLines[i - 1]);
        check(b.bytes == want, "synthetic body equals the expected splice (nl bytes " + std::to_string(nl.size()) + ")");
        if (b.bytes != want) std::printf("--- got:\n%s\n--- want:\n%s\n", b.bytes.c_str(), want.c_str());
        check(b.md5 == md5s(b.bytes), "body md5 field");

        // empty table: the marker line goes away; a table with only dropped entries leaves an empty line (Python behaviour)
        const Built e = generate_body(tmpl, stock, Table{});
        check(e.ok && e.bytes.find("entries = {" + nl + "} }") != std::string::npos, "empty table removes the marker line");
        Table only_dropped;
        only_dropped[5] = Entry{0, 0, true, true, ""};
        const Built d = generate_body(tmpl, stock, only_dropped);
        check(d.ok && d.bytes.find("entries = {" + nl + nl + "} }") != std::string::npos, "only-dropped table leaves an empty line");

        // header
        const std::string js = players_json(t);
        const Built h = generate_override(tmpl, stock, t, "6AC07E31-2145C000", js);
        const std::string hl = "-- TURBO build 6AC07E31-2145C000 stock " + md5s(stock) + " table " + md5s(js).substr(0, 12) + "\r\n";
        check(h.ok && h.bytes == hl + b.bytes, "override = CRLF header + body");
        const auto ph = lualoader::parse_override_header(h.bytes);
        check(ph.ok && ph.build == "6AC07E31-2145C000" && ph.stock == md5s(stock) && ph.table == md5s(js).substr(0, 12),
              "hook parses the header back");
        check(table_hash(js).size() == 12, "table hash is 12 hex digits");
        check(!generate_override(tmpl, stock, t, "", js).ok, "empty build key refused");
        check(!generate_override(tmpl, stock, t, "a b", js).ok, "build key with a space refused");
    }
}

static void test_refusals() {
    const std::string stock = stock_text("\r\n");
    Table t;
    t[1] = Entry{155, 46, true, true, ""};
    TOpts o;
    o.stock_md5 = md5s(stock);
    auto refused = [&](const TOpts& x, const std::string& stock_in, const std::string& needle, const std::string& what) {
        const Built b = generate_body(make_template(x), stock_in, t);
        check(!b.ok && b.bytes.empty() && b.error.find(needle) != std::string::npos, what + " (" + b.error + ")");
    };
    // whole-file md5: a changed stock is refused before any anchor is looked at
    std::string changed = stock;
    changed[5] = 'X';
    refused(o, changed, "stock md5", "changed stock refused by the file md5");
    // per-line anchor: right file md5, wrong line md5
    TOpts a = o;
    a.ins3_h = lmd5(4);
    refused(a, stock, "anchor md5 mismatch at stock line 3", "anchor mismatch refused");
    TOpts g = o;
    g.gate_h = std::string(32, '0');
    refused(g, stock, "anchor md5 mismatch at stock line 1", "gate anchor mismatch refused");
    TOpts c = o;
    c.ins3_col = 999;
    refused(c, stock, "column 999 out of range", "column out of range refused");
    TOpts m = o;
    m.ins3_text = " or GATE(initData)";  // no '$'
    refused(m, stock, "must end with '$'", "TEXT without $ refused");
    TOpts n = o;
    n.entries_marker = false;
    refused(n, stock, "exactly one --@@ENTRIES@@", "missing marker refused");
    TOpts n2 = o;
    n2.second_marker = true;
    refused(n2, stock, "exactly one --@@ENTRIES@@", "two markers refused");
    check(!generate_body("no spec here", stock, t).ok, "template without SPLICE SPEC refused");
    check(!generate_body(make_template(o) + "", stock + "\r\nextra", t).ok, "appended stock refused");
    // anchor line beyond the file
    std::string tm = make_template(o);
    tm.replace(tm.find("@ins\t3\t"), 7, "@ins\t99\t");
    const Built b = generate_body(tm, stock, t);
    check(!b.ok && b.error.find("line 99") != std::string::npos, "anchor beyond the file refused");
    // unknown directive
    std::string tu = make_template(o);
    tu.replace(tu.find("END SPEC"), 0, "@bogus\t1\n");
    check(!generate_body(tu, stock, t).ok, "unknown directive refused");
    Table neg;
    neg[1] = Entry{-3, 0, true, true, ""};
    check(!generate_body(make_template(o), stock, neg).ok, "negative model id refused");
}

static void test_players_json() {
    Table t;
    std::string err;
    check(parse_players_json("{\"229984\": {\"name\": \"Chilwell\", \"beard\": 46, \"hair\": 155},\n \"78183\": {\"beard\": 46, \"colours\": false, "
                             "\"hair_colour\": true}}",
                             t, &err),
          "research format parses (" + err + ")");
    check(t.size() == 2 && t[229984].hair == 155 && t[229984].beard == 46 && t[229984].hair_colour && t[229984].beard_colour &&
              t[229984].name == "Chilwell",
          "fields + colour defaults");
    check(t[78183].hair == 0 && t[78183].hair_colour && !t[78183].beard_colour, "colours=false is the default of the unset flag only");
    Table back;
    check(parse_players_json(players_json(t), back, &err) && back.size() == 2 && back[229984].hair == 155 && back[78183].beard == 46 &&
              !back[78183].beard_colour && back[229984].name == "Chilwell",
          "players_json round trip");
    for (const char* bad : {"[]", "{\"0123\": {}}", "{\"abc\": {}}", "{\"0\": {}}", "{\"5\": 3}", "{\"5\": {\"hair\": 1.5}}",
                            "{\"5\": {\"hair\": true}}", "{\"5\": {\"hair\": 0}}", "{\"5\": {\"beard\": \"46\"}}",
                            "{\"5\": {\"hair_colour\": 3}}", "{\"5\": {\"colour\": true}}", "not json"}) {
        Table x;
        check(!parse_players_json(bad, x, &err), std::string("refused: ") + bad);
    }
}

static void test_ids_and_validate() {
    check(ok_hair_ids().size() == 1371 && ok_beard_ids().size() == 105, "verified id counts 1371 / 105");
    check(hair_id_ok(155) && beard_id_ok(46), "known-good ids 155 / 46");
    check(!beard_id_ok(259) && !beard_id_ok(0) && !hair_id_ok(0) && !hair_id_ok(-1) && !hair_id_ok(999999), "unverified ids refused");
    check(std::string(template_md5()) == "5f3ddbce3d5b5eae50fd3f3224f7903f" && md5s(template_text()) == template_md5(),
          "embedded template is TEMPLATE v2 5f3ddbce (LF)");
    check(template_stock_md5(template_text()) == "9399e39a1e46a15d151ada423759abe9", "embedded template anchors stock 9399e39a");
    // the embedded template must hold no stock line: every anchor is an md5, every TEXT is Turbo's (short check: no 'function ' line
    // from the stock appears outside the gate block)
    const std::string tt = template_text();
    check(tt.find("@@GATE-BEGIN") != std::string::npos && tt.size() < 8000, "template is small (anchors + gate only)");

    auto hc = [](int64_t id) -> int64_t { return id == 1 ? 0 : id == 2 ? 1 : -1; };
    Table t;
    t[1] = Entry{155, 46, true, true, "ok"};
    check(validate(t, hc).empty(), "valid entry passes");
    t[2] = Entry{155, 0, true, true, ""};
    t[3] = Entry{0, 0, true, true, ""};
    t[1].beard = 259;
    const auto v = validate(t, hc);
    auto has = [&](const std::string& s) {
        for (const auto& x : v)
            if (x.find(s) != std::string::npos) return true;
        return false;
    };
    check(has("beard 259 is not a verified") && has("not a real face") && has("player 3: choose a hair") && has("not in the database"),
          "validate reports ids, head class, empty entry, unknown player");
    Table big;
    for (int i = 1; i <= int(kMaxEntries) + 1; ++i) big[i] = Entry{155, 0, true, true, ""};
    check(!validate(big, nullptr).empty(), "entry cap");
}

static void test_apply_remove() {
    const fs::path out = fs::temp_directory_path() / ("turbo_faces3d_test_" + std::to_string(std::rand()));
    fs::remove_all(out);
    fs::create_directories(out);
    const std::string o8 = out.u8string();
    Table t;
    t[1] = Entry{155, 0, true, true, ""};
    auto hc0 = [](int64_t) -> int64_t { return 0; };
    ApplyResult r = apply(o8, t, "6AC07E31-2145C000", hc0);
    check(!r.ok && r.message.find("not been captured") != std::string::npos && !fs::exists(out / "faces3d_on.txt") &&
              !fs::exists(out / "lua_override" / "player_kit.lua"),
          "apply without the stock cache refuses and writes nothing");
    write_file(out / "faces3d" / "stock_player_kit.cache", "-- not the stock\n");
    r = apply(o8, t, "6AC07E31-2145C000", hc0);
    check(!r.ok && r.message.find("is not the one this Turbo knows") != std::string::npos && !fs::exists(out / "faces3d_on.txt"),
          "apply with a foreign stock refuses");
    r = apply(o8, t, "", hc0);
    check(!r.ok && !fs::exists(out / "faces3d_on.txt"), "apply without a build key refuses");
    Table bad = t;
    bad[1].hair = 3999999;
    r = apply(o8, bad, "6AC07E31-2145C000", hc0);
    check(!r.ok && r.message.find("not a verified") != std::string::npos, "apply validates");
    // remove / empty apply
    write_file(out / "lua_override" / "player_kit.lua", "x");
    write_file(out / "faces3d_on.txt", "x");
    std::string err;
    check(remove(o8, &err) && !fs::exists(out / "lua_override" / "player_kit.lua") && !fs::exists(out / "faces3d_on.txt"), "remove");
    check(remove(o8, &err), "remove twice is fine");
    write_file(out / "faces3d_on.txt", "x");
    r = apply(o8, Table{}, "6AC07E31-2145C000", hc0);
    check(!r.ok && fs::exists(out / "faces3d_on.txt") && !fs::exists(out / "lua_override" / "player_kit.lua"),
          "empty apply no longer removes (2.16): it builds like any list, and a foreign stock refuses with the opt-in untouched");
    Table lp;
    check(load_players(o8, lp, &err) && lp.empty(), "load_players after empty apply");
    fs::remove_all(out);
    Table none;
    check(load_players((out / "nope").u8string(), none, &err) && none.empty(), "load_players: missing file = empty");
}

// The real thing, only where the research files exist (EA source never enters the repository)
static void test_real_f2() {
    const char* env = std::getenv("TURBO_RESEARCH_DIR");
    const fs::path research = env ? fs::u8path(env) : fs::current_path() / ".." / ".." / ".." / "research";
    const fs::path stock_p = research / ".." / "evidence" / "2026-10-15" / "bisect" / "stock_copy.lua";
    const fs::path players_p = research / "c2_lua_loader" / "players.json";
    const fs::path f2_p = research / "c2_lua_loader" / "generated" / "f2_player_kit.lua";
    std::string stock, pj, f2;
    if (!read_file(stock_p, stock) || !read_file(players_p, pj)) {
        std::printf("  SKIP real-stock byte compare (research files not here: %s)\n", research.u8string().c_str());
        return;
    }
    Table t;
    std::string err;
    check(parse_players_json(pj, t, &err), "research players.json parses (" + err + ")");
    const Built body = generate_body(template_text(), stock, t);
    check(body.ok && body.md5 == "490036315266d068a57166c16852c5cd", "real stock: body md5 == f2_body 490036315266... (" + body.md5 + body.error + ")");
    const Built ov = generate_override(template_text(), stock, t, "6AC07E31-2145C000", pj);
    check(ov.ok && ov.md5 == "442d5115e917b9b1f8e277f8583ba0d8", "real stock: override md5 == F2 442d5115... (" + ov.md5 + ")");
    check(ov.bytes.compare(0, 92, "-- TURBO build 6AC07E31-2145C000 stock 9399e39a1e46a15d151ada423759abe9 table 222cbba80890\r\n") == 0,
          "real stock: header line (table 222cbba80890)");
    if (read_file(f2_p, f2)) check(ov.bytes == f2, "real stock: byte-identical to generated\\f2_player_kit.lua");
    // apply end to end on a temp turbo_output with the real stock as the cache
    const fs::path out = fs::temp_directory_path() / ("turbo_faces3d_real_" + std::to_string(std::rand()));
    fs::remove_all(out);
    write_file(out / "faces3d" / "stock_player_kit.cache", stock);
    const ApplyResult r = apply(out.u8string(), t, "6AC07E31-2145C000", nullptr);
    std::string written, js;
    const Built body4 = generate_body(template_v4_text(), stock, t);
    check(r.ok && read_file(out / "lua_override" / "player_kit.lua", written) && read_file(out / "faces3d" / "players.json", js) &&
              fs::exists(out / "faces3d_on.txt") && md5s(written) == r.override_md5 && r.table == md5s(js).substr(0, 12) && body4.ok &&
              written.substr(written.find("\r\n") + 2) == body4.bytes && override_has_live_looks(out.u8string()),
          "apply with the real stock writes players.json + the v4 override (header table = its md5) + faces3d_on.txt (" + r.message + ")");
    std::string err2;
    check(remove(out.u8string(), &err2) && !fs::exists(out / "faces3d_on.txt") && fs::exists(out / "faces3d" / "players.json"),
          "remove keeps players.json");
    fs::remove_all(out);
    std::printf("  real-stock byte compare done: override %s, %zu bytes\n", ov.md5.c_str(), ov.bytes.size());
}

// ---------------------------------------------------------------- v3 ("follow")
static void test_players_json_v3() {
    Table t;
    std::string err;
    check(parse_players_json("{\"229984\": {\"name\": \"Chilwell follow (no fallback)\", \"mode\": \"follow\"},\n"
                             " \"230107\": {\"name\": \"C\", \"mode\": \"fixed\", \"hair\": 155, \"beard\": 46},\n"
                             " \"5\": {\"mode\": \"follow\", \"parts\": [\"beard\"], \"beard_fallback\": 47, \"colours\": false}}",
                             t, &err),
          "v3 format parses (" + err + ")");
    check(t[229984].follow && t[229984].follow_hair && t[229984].follow_beard && t[229984].hair == 0 && !t[230107].follow &&
              t[230107].hair == 155 && t[5].follow && !t[5].follow_hair && t[5].follow_beard && t[5].beard_fallback == 47 && !t[5].hair_colour,
          "follow / fixed / parts / fallback fields");
    Table back;
    t[229984].hair = 155;  // a follow entry's picker choice is memory only
    const std::string js = players_json(t);
    check(parse_players_json(js, back, &err) && back.size() == 3 && back[229984].follow && back[229984].hair == 0 && !back[5].follow_hair &&
              back[5].beard_fallback == 47 && back[230107].hair == 155 && !back[230107].follow,
          "v3 round trip (" + err + "): " + js);
    check(js.find("\"hair\": 155, \"beard\"") == std::string::npos || js.find("\"mode\"") != std::string::npos, "follow hair not written");
    // a fixed-only table is written as 2.8 wrote it (no mode field)
    Table fx;
    fx[1] = Entry{155, 46, true, false, ""};
    check(players_json(fx) == "{\n  \"1\": {\"hair\": 155, \"beard\": 46, \"hair_colour\": true, \"beard_colour\": false}\n}\n", "fixed-only file unchanged");
    for (const char* bad : {"{\"5\": {\"mode\": \"x\"}}", "{\"5\": {\"mode\": \"follow\", \"hair\": 155}}",
                            "{\"5\": {\"mode\": \"fixed\", \"parts\": [\"hair\"]}}", "{\"5\": {\"mode\": \"follow\", \"parts\": []}}",
                            "{\"5\": {\"mode\": \"follow\", \"parts\": [\"hair\", \"hair\"]}}", "{\"5\": {\"mode\": \"follow\", \"parts\": \"hair\"}}",
                            "{\"5\": {\"mode\": \"follow\", \"parts\": [\"hair\"], \"beard_fallback\": 47}}",
                            "{\"5\": {\"mode\": \"follow\", \"beard_fallback\": 259}}", "{\"5\": {\"mode\": \"follow\", \"hair_fallback\": 99999}}",
                            "{\"5\": {\"mode\": 1}}", "{\"5\": {\"hair_fallback\": 250}}"}) {
        Table x;
        check(!parse_players_json(bad, x, &err), std::string("v3 refused: ") + bad);
    }
    // validate: follow needs no ids
    Table v;
    v[1].follow = true;
    check(validate(v, nullptr).empty(), "follow entry without ids is valid");
    v[1].follow_hair = v[1].follow_beard = false;
    check(!validate(v, nullptr).empty(), "follow without parts refused");
    check(std::string(template_v3_md5()) == "e007e38dc60cac3b75e02bcae7e34b1a" && md5s(template_v3_text()) == template_v3_md5(),
          "embedded template v3 is e007e38d (LF)");
    check(template_stock_md5(template_v3_text()) == "9399e39a1e46a15d151ada423759abe9", "v3 anchors the same stock");
    Table f1;
    f1[1].follow = true;
    check(template_for(fx) == template_v4_text() && template_for(f1) == template_v4_text(), "template_for: v4 for every list (2.16 live looks)");
}

static void test_running_and_gate() {
    // parse_gate_entries over a synthetic gate block (v2 and v3 lines)
    const std::string ov =
        "-- TURBO build 6AC07E31-2145C000 stock 9399e39a1e46a15d151ada423759abe9 table 30ed1d5d0b6b\r\nlocal x\r\n"
        "-- ===== TURBO PER-PLAYER FACE GATE v3 (Turbo-owned) =====\r\n"
        "\t\t[229984] = { beard = -1, hair = -1, hc = true, bc = true },  -- follow Chilwell follow (no fallback)\r\n"
        "\t\t[230107] = { beard = 46, hair = 155, hc = false, bc = true },  -- fixed Coulibaly\r\n"
        "\t\t[5] = { beard = -1, bf = 47, hc = true, bc = false },  -- follow\r\n"
        "\t\t1, 3, 5, 9,\r\n"
        "-- ===== END TURBO PER-PLAYER FACE GATE v3 =====\r\n";
    Table t;
    check(parse_gate_entries(ov, t) && t.size() == 3, "gate entries parsed");
    check(t[229984].follow && t[229984].follow_hair && t[229984].follow_beard && t[229984].name == "Chilwell follow (no fallback)", "follow entry");
    check(!t[230107].follow && t[230107].hair == 155 && t[230107].beard == 46 && !t[230107].hair_colour && t[230107].name == "Coulibaly", "fixed entry");
    check(t[5].follow && !t[5].follow_hair && t[5].follow_beard && t[5].beard_fallback == 47 && !t[5].beard_colour, "follow beard only");
    Table none;
    check(!parse_gate_entries("no gate", none), "no gate block");

    const fs::path out = fs::temp_directory_path() / ("turbo_faces3d_run_" + std::to_string(std::rand()));
    fs::remove_all(out);
    std::string why;
    Table r;
    check(!running_table(out.u8string(), "30ed1d5d0b6b", r, &why) && why.find("restart") != std::string::npos, "nothing on disk: unknown");
    write_file(out / "lua_override" / "player_kit.lua", ov);
    check(running_table(out.u8string(), "30ed1d5d0b6b", r, &why) && r.size() == 3 && r[229984].follow, "override on disk with that table");
    check(!running_table(out.u8string(), "aaaaaaaaaaaa", r, &why), "override of another table: unknown");
    const std::string js = "{\n  \"77\": {\"mode\": \"follow\", \"hair_colour\": true, \"beard_colour\": true}\n}\n";
    write_file(out / "faces3d" / "applied" / (table_hash(js) + ".json"), js);
    check(running_table(out.u8string(), table_hash(js), r, &why) && r.size() == 1 && r[77].follow, "snapshot of the applied list");
    write_file(out / "faces3d" / "applied" / "bbbbbbbbbbbb.json", js);
    check(!running_table(out.u8string(), "bbbbbbbbbbbb", r, &why), "snapshot whose md5 is not its name is ignored");

    // live_gate
    lualoader::Faces3dStatus st;
    check(!live_gate(nullptr, &t, 229984).active, "no hooks");
    check(live_gate(&st, &t, 229984).why.find("off at this game start") != std::string::npos, "not opted in");
    st.opted_in = st.installed = st.substitute = st.header_ok = st.build_key_ok = st.stock_matches_header = true;
    st.substituted = 18;
    LiveGate g = live_gate(&st, &t, 229984);
    check(g.active && g.member && g.follow_hair && g.follow_beard, "active for a follow member: " + g.why);
    g = live_gate(&st, &t, 230107);
    check(!g.active && g.member && g.why.find("fixed look") != std::string::npos, "fixed member: " + g.why);
    g = live_gate(&st, &t, 999);
    check(!g.active && !g.member && g.why.find("restart") != std::string::npos, "not a member: " + g.why);
    check(!live_gate(&st, nullptr, 229984).active, "running list unknown");
    lualoader::Faces3dStatus k = st;
    k.killed = true;
    check(live_gate(&k, &t, 229984).why.find("kill switch") != std::string::npos, "kill switch");
    k = st;
    k.legacy_refused = true;
    check(!live_gate(&k, &t, 229984).active, "legacy refused");
    k = st;
    k.substituted = 0;
    k.fallbacks = 18;
    check(live_gate(&k, &t, 229984).why.find("failed to compile") != std::string::npos, "fallbacks only");
    k = st;
    k.build_key_ok = false;
    check(!live_gate(&k, &t, 229984).active, "other build");
    k = st;
    k.stock_matches_header = false;
    check(live_gate(&k, &t, 229984).why.find("stale") != std::string::npos, "stale stock");

    // backup + writes
    PreviewBackup b, b2;
    std::string err;
    check(load_preview_backup(out.u8string(), b, &err) && b.empty(), "no backup file = empty");
    b[229984].name = "Ben Chilwell";
    b[229984].original = {{"hairtypecode", 696}, {"facialhairtypecode", 259}, {"hairstylecode", 0}};
    b[229984].written = {{"hairtypecode", 155}, {"facialhairtypecode", 46}};
    check(save_preview_backup(out.u8string(), b, &err) && fs::exists(out / "faces3d" / "preview_backup.json"), "backup saved");
    check(load_preview_backup(out.u8string(), b2, &err) && b2.size() == 1 && b2[229984].original.at("hairtypecode") == 696 &&
              b2[229984].written.at("facialhairtypecode") == 46 && b2[229984].name == "Ben Chilwell",
          "backup read back");
    check(save_preview_backup(out.u8string(), PreviewBackup{}, &err) && !fs::exists(out / "faces3d" / "preview_backup.json"), "empty backup deletes the file");
    // 2.8 in-game bug: a record without originals ({"name": "", "original": {}, "written": {}}) must never reach the file
    PreviewBackup hollow;
    hollow[229984];
    check(save_preview_backup(out.u8string(), b, &err) && save_preview_backup(out.u8string(), hollow, &err) &&
              !fs::exists(out / "faces3d" / "preview_backup.json"),
          "a backup holding only a record without originals deletes the file");
    hollow[230107] = b[229984];
    check(save_preview_backup(out.u8string(), hollow, &err) && load_preview_backup(out.u8string(), b2, &err) && b2.size() == 1 && b2.count(230107) &&
              b2[230107].original.at("hairtypecode") == 696,
          "records without originals are left out of the file");
    write_file(out / "faces3d" / "preview_backup.json", "{\"players\": {\"229984\": {\"name\": \"\", \"original\": {}, \"written\": {}}}}");
    check(load_preview_backup(out.u8string(), b2, &err) && b2.empty(), "the in-game hollow file reads as no record");
    fs::remove(out / "faces3d" / "preview_backup.json");
    const std::map<std::string, int64_t> cur{{"hairtypecode", 696}, {"facialhairtypecode", 259}, {"hairstylecode", 3}};
    auto w = preview_writes(155, 46, cur);
    check(w.size() == 3 && w["hairtypecode"] == 155 && w["facialhairtypecode"] == 46 && w["hairstylecode"] == 0, "writes hair, beard, style 0");
    w = preview_writes(155, 0, {{"hairtypecode", 155}, {"facialhairtypecode", 259}, {"hairstylecode", 0}});
    check(w.empty(), "nothing that already holds");
    w = preview_writes(0, 0, cur);
    check(w.empty(), "no part chosen: nothing written");
    fs::remove_all(out);
}

// v3 against build_gated_v3.py: the f3 launch file (F5 showed it in game) and fixtures built by the Python builder
static void test_real_v3() {
    const char* env = std::getenv("TURBO_RESEARCH_DIR");
    const fs::path research = env ? fs::u8path(env) : fs::current_path() / ".." / ".." / ".." / "research";
    const fs::path stock_p = research / ".." / "evidence" / "2026-10-15" / "bisect" / "stock_copy.lua";
    const fs::path pj_p = research / "c2_lua_loader" / "players_v3_test.json";
    std::string stock, pj, f3;
    if (!read_file(stock_p, stock) || !read_file(pj_p, pj)) {
        std::printf("  SKIP real-stock v3 byte compare (research files not here)\n");
        return;
    }
    check(md5s(pj) == "30ed1d5d0b6b39ff319d0b222c2eeb06", "players_v3_test.json is the launch set");
    Table t;
    std::string err;
    check(parse_players_json(pj, t, &err) && t.size() == 2 && t[229984].follow && !t[230107].follow, "players_v3_test.json parses (" + err + ")");
    const Built body = generate_body(template_v3_text(), stock, t);
    check(body.ok && body.md5 == "53e7dc9783629ff82bc88b9a4cca2c30", "real stock v3: body md5 == f3_body 53e7dc97... (" + body.md5 + body.error + ")");
    const Built ov = generate_override(template_v3_text(), stock, t, "6AC07E31-2145C000", pj);
    check(ov.ok && ov.md5 == "4cfdf40bb7af09422a90d94c95eca99f" && ov.bytes.size() == 273169,
          "real stock v3: override md5 == f3 4CFDF40B... (" + ov.md5 + ")");
    check(ov.bytes.compare(0, 92, "-- TURBO build 6AC07E31-2145C000 stock 9399e39a1e46a15d151ada423759abe9 table 30ed1d5d0b6b\r\n") == 0,
          "real stock v3: header line (table 30ed1d5d0b6b)");
    if (read_file(research / "c2_lua_loader" / "generated" / "f3_player_kit.lua", f3)) check(ov.bytes == f3, "byte-identical to generated\\f3_player_kit.lua");
    // the generated file tells which players this start runs
    Table run;
    check(parse_gate_entries(ov.bytes, run) && run.size() == 2 && run[229984].follow && run[230107].hair == 155, "f3 gate entries read back");
    // fixtures: md5 of build_gated_v3.py --players <json> (bodies), the JSON text is here
    struct Fx {
        const char* name;
        const char* json;  // "" = research players.json (the F2 set)
        const char* md5;
    };
    const Fx fx[] = {
        {"A F2 set as v3", "", "cc4007322527144e46d7a95c8f2f4516"},
        {"B fallbacks + colours false",
         "{\"229984\": {\"mode\": \"follow\", \"hair_fallback\": 250, \"beard_fallback\": 47, \"name\": \"Chilwell\"}, \"230107\": {\"hair\": 155, "
         "\"beard\": 46, \"colours\": false}}",
         "3496b779a01a056105eae270ec086791"},
        {"C parts hair / beard",
         "{\"229984\": {\"mode\": \"follow\", \"parts\": [\"hair\"], \"hair_colour\": false}, \"230107\": {\"mode\": \"follow\", \"parts\": [\"beard\"], "
         "\"beard_fallback\": 47, \"name\": \"Couli baly\"}}",
         "7c92a61c875095882a0270076638ddb2"},
        {"D empty", "{}", "5a9bb89ba69ffd9d040a332a59aa1669"},
        {"E only dropped", "{\"5\": {\"mode\": \"fixed\", \"name\": \"nothing\"}}", "5a9bb89ba69ffd9d040a332a59aa1669"},
    };
    for (const Fx& f : fx) {
        std::string js = f.json;
        if (js.empty() && !read_file(research / "c2_lua_loader" / "players.json", js)) continue;
        Table x;
        check(parse_players_json(js, x, &err), std::string("fixture ") + f.name + " parses (" + err + ")");
        const Built b = generate_body(template_v3_text(), stock, x);
        check(b.ok && b.md5 == f.md5, std::string("fixture ") + f.name + ": " + b.md5 + " == " + f.md5 + " " + b.error);
    }
    // apply with a follow entry writes v3 + the snapshot; a fixed-only table still writes the v2 (F2) bytes
    const fs::path out = fs::temp_directory_path() / ("turbo_faces3d_v3_" + std::to_string(std::rand()));
    fs::remove_all(out);
    write_file(out / "faces3d" / "stock_player_kit.cache", stock);
    const ApplyResult r = apply(out.u8string(), t, "6AC07E31-2145C000", nullptr);
    std::string written, js;
    const Built body4 = generate_body(template_v4_text(), stock, t);
    check(r.ok && read_file(out / "lua_override" / "player_kit.lua", written) && read_file(out / "faces3d" / "players.json", js) && body4.ok &&
              written.substr(written.find("\r\n") + 2) == body4.bytes && fs::exists(out / "faces3d" / "applied" / (r.table + ".json")),
          "apply (follow) writes the v4 body + the applied snapshot (" + r.message + ")");
    Table rt;
    std::string why;
    check(running_table(out.u8string(), r.table, rt, &why) && rt[229984].follow, "running_table finds the applied list");
    Table fixed_only = t;
    fixed_only.erase(229984);
    const ApplyResult r2 = apply(out.u8string(), fixed_only, "6AC07E31-2145C000", nullptr);
    const Built v4b = generate_body(template_v4_text(), stock, fixed_only);
    check(r2.ok && read_file(out / "lua_override" / "player_kit.lua", written) && v4b.ok && written.substr(written.find("\r\n") + 2) == v4b.bytes &&
              written.find("GATE v4") != std::string::npos,
          "apply (fixed only) writes the v4 body too");
    check(running_table(out.u8string(), r.table, rt, &why) && rt.size() == 2, "the first start's list is still known after a second Apply");
    fs::remove_all(out);
    std::printf("  real-stock v3 byte compare done: override %s, %zu bytes\n", ov.md5.c_str(), ov.bytes.size());
}

// ---------------------------------------------------------------- v4: live looks (2.16)
static std::string joined(const std::vector<int>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]);
    return s;
}
static std::string spec_of(const std::string& t) {
    const size_t a = t.find("\nSPLICE SPEC\n"), b = t.find("\nEND SPEC\n");
    return (a == std::string::npos || b == std::string::npos) ? std::string() : t.substr(a, b - a);
}

static void test_bands() {
    check(std::string(template_v4_md5()) == "53d77dcc4db1092d928968e081808a47" && md5s(template_v4_text()) == template_v4_md5(),
          "embedded template v4 md5 53d77dcc (research\\c2_lua_loader\\player_kit_template_v4.lua)");
    check(template_stock_md5(template_v4_text()) == "9399e39a1e46a15d151ada423759abe9", "v4 anchors the same stock");
    check(!spec_of(template_v4_text()).empty() && spec_of(template_v4_text()) == spec_of(template_v3_text()), "v4 SPLICE SPEC == v3 (byte for byte)");
    const std::string g4 = template_v4_text();
    check(g4.find(lualoader::kLiveLooksMarker) != std::string::npos && std::string(template_v3_text()).find(lualoader::kLiveLooksMarker) == std::string::npos,
          "the live-looks marker is in v4 only");
    // frozen lists: the index is what a save holds, so size and content are pinned
    check(band_hair_ids().size() == 1371 && md5s(joined(band_hair_ids())) == "fc5c2b27a7434afdc2d6d078f5b7e6a7", "band hair list frozen (1371, md5 fc5c2b27)");
    check(band_beard_ids().size() == 105 && md5s(joined(band_beard_ids())) == "ed00efe7730de53635e5743d24e78956", "band beard list frozen (105, md5 ed00efe7)");
    check(hair_band_code(155) == 8193 && hair_band_code(250) == 8281 && beard_band_code(46) == 355 && beard_band_code(47) == 356,
          "band codes 155 -> 8193, 250 -> 8281, beard 46 -> 355, 47 -> 356 (the harness values)");
    check(hair_band_code(band_hair_ids().front()) == 8040 && hair_band_code(band_hair_ids().back()) == 9410 &&
              beard_band_code(band_beard_ids().front()) == 336 && beard_band_code(band_beard_ids().back()) == 440,
          "bands: hairtypecode 8040..9410, facialhairtypecode 336..440");
    check(9410 <= 10000 && 440 <= 512, "bands inside the schema ranges (hairtypecode max 10000, facialhairtypecode max 512)");
    check(!hair_from_code(3022) && !hair_from_code(2013) && !beard_from_code(304) && !beard_from_code(301),
          "EA's largest codes (base tables: hair 3022 / 2013, beard 304 / 301) are not band codes");
    bool rt = true;
    for (int id : band_hair_ids()) rt = rt && hair_from_code(hair_band_code(id)) == id;
    for (int id : band_beard_ids()) rt = rt && beard_from_code(beard_band_code(id)) == id;
    check(rt, "every listed id round-trips through its band code");
    check(!hair_from_code(8039) && !hair_from_code(9411) && !hair_from_code(0) && !hair_from_code(-1) && !beard_from_code(335) &&
              !beard_from_code(441) && !beard_from_code(0),
          "codes just outside the bands select nothing");
    check(!hair_band_code(0) && !hair_band_code(999999) && !beard_band_code(259) && !beard_band_code(0), "unlisted ids have no band code");
    check(kHairOwnCode == 9999 && kBeardOwnCode == 511 && !hair_id_ok(kHairOwnCode) && !beard_id_ok(kBeardOwnCode) && !hair_from_code(kHairOwnCode) &&
              !beard_from_code(kBeardOwnCode) && is_turbo_hair_code(kHairOwnCode) && is_turbo_beard_code(kBeardOwnCode) && is_turbo_hair_code(8193) &&
              !is_turbo_hair_code(8039) && !is_turbo_beard_code(335) && !is_turbo_hair_code(696),
          "own codes 9999 / 511: no model, no band index, but Turbo codes; 8039 / 335 are plain ids");
}

static void test_look_writes() {
    using M = std::map<std::string, int64_t>;
    const M cur{{"hairtypecode", 696}, {"facialhairtypecode", 259}};
    const M orig{{"hairtypecode", 696}, {"facialhairtypecode", 259}};
    check(look_writes(155, 46, cur, orig) == M{{"hairtypecode", 8193}, {"facialhairtypecode", 355}}, "hair + beard -> both band codes");
    check(look_writes(155, 0, cur, orig) == M{{"hairtypecode", 8193}}, "hair only: the beard code is left alone");
    check(look_writes(0, 46, cur, orig) == M{{"facialhairtypecode", 355}}, "beard only: the hair code is left alone");
    check(look_writes(0, 0, cur, orig).empty(), "his own look and no band code: nothing written");
    const M banded{{"hairtypecode", 8193}, {"facialhairtypecode", 355}};
    check(look_writes(0, 0, banded, orig) == M{{"hairtypecode", 696}, {"facialhairtypecode", 259}}, "back to his own: his recorded originals");
    check(look_writes(0, 0, banded, M{}) == M{{"hairtypecode", 0}, {"facialhairtypecode", 0}}, "back to his own without originals: 0 (outside the band)");
    check(look_writes(0, 0, banded, banded) == M{{"hairtypecode", 0}, {"facialhairtypecode", 0}}, "an original that is itself a band code is never written back");
    const M owned{{"hairtypecode", kHairOwnCode}, {"facialhairtypecode", kBeardOwnCode}};
    check(look_writes(0, 0, owned, orig) == M{{"hairtypecode", 696}, {"facialhairtypecode", 259}}, "own codes count as Turbo codes: his originals come back");
    check(look_writes(0, 0, owned, owned) == M{{"hairtypecode", 0}, {"facialhairtypecode", 0}}, "an own code is never written back as an original");
    check(look_writes(250, 47, banded, orig) == M{{"hairtypecode", 8281}, {"facialhairtypecode", 356}}, "change of choice: the new band codes");
    check(look_writes(155, 46, banded, orig).empty(), "same choice again: nothing written");
    check(look_writes(999999, 259, cur, orig).empty(), "unlisted ids: nothing written");
    check(look_writes(155, 46, M{{"hairtypecode", 696}}, orig) == M{{"hairtypecode", 8193}}, "a table without facialhairtypecode: only hair");
    bool never_style = true;
    for (const auto& kv : look_writes(155, 46, M{{"hairtypecode", 1}, {"facialhairtypecode", 1}, {"hairstylecode", 3}}, M{}))
        never_style = never_style && kv.first != "hairstylecode";
    check(never_style, "hairstylecode is never written (stock uses it in a real face's own asset names)");
}

static void test_live_looks_state() {
    check(!live_looks(nullptr).active && !live_looks(nullptr).restart_needed, "no hooks: not active, no restart advice");
    {
        // 2.16 product mode: built in memory at the game's start; nothing ever asks for a restart
        lualoader::Faces3dStatus p;
        check(!live_looks(&p).active && !live_looks(&p).restart_needed, "product: hook not started: off, no restart advice");
        p.opted_in = p.installed = p.substitute = p.in_memory = true;
        check(!live_looks(&p).active && live_looks(&p).restart_needed && live_looks(&p).why.find("restart the game once") != std::string::npos,
              "product: the hook never saw the boot compile: the one honest restart");
        p.capture_refused = 18;
        check(!live_looks(&p).active && !live_looks(&p).restart_needed && live_looks(&p).why.find("this game version") != std::string::npos,
              "product: the reader is not the verified one (another game build): off, no restart advice");
        p.capture_refused = 0;
        p.captures = 18;
        p.built_error = "the game's player_kit.lua (md5 x) is not the one Turbo knows";
        check(!live_looks(&p).active && !live_looks(&p).restart_needed && live_looks(&p).why.find("not the one Turbo knows") != std::string::npos,
              "product: another player_kit.lua: off, says why, no restart advice");
        p.built_error.clear();
        p.built = true;
        p.substituted = 18;
        check(live_looks(&p).active && live_looks(&p).why == "Changes show straight away.", "product: built and served: active");
        p.substituted = 0;
        p.fallbacks = 18;
        check(!live_looks(&p).active && !live_looks(&p).restart_needed, "product: failed compile: off, no restart advice");
        p.fallbacks = 0;
        p.substituted = 18;
        p.killed = true;
        check(!live_looks(&p).active && !live_looks(&p).restart_needed, "product: kill switch: off");
        p.killed = false;
        p.legacy_refused = true;
        check(!live_looks(&p).active && !live_looks(&p).restart_needed, "product: legacy file: off");
    }
    // the research file mode
    lualoader::Faces3dStatus st;
    st.opted_in = true;
    check(!live_looks(&st).active && !live_looks(&st).restart_needed, "not installed: no restart advice");
    st.installed = true;
    st.substitute = true;
    st.header_ok = true;
    st.build_key_ok = true;
    st.stock_matches_header = true;
    st.captured_md5 = "9399e39a1e46a15d151ada423759abe9";
    st.substituted = 18;
    check(!live_looks(&st).active && live_looks(&st).restart_needed, "an older override (no live rule) at this start: one restart");
    st.live_looks = true;
    check(live_looks(&st).active, "v4 served: active");
    st.killed = true;
    check(!live_looks(&st).active && !live_looks(&st).restart_needed, "kill switch: off, no restart advice");
    st.killed = false;
    st.legacy_refused = true;
    check(!live_looks(&st).active, "legacy player_kit.lua: off");
    st.legacy_refused = false;
    st.build_key_ok = false;
    check(!live_looks(&st).active && live_looks(&st).restart_needed, "another build: restart");
    st.build_key_ok = true;
    st.substituted = 0;
    st.fallbacks = 18;
    check(!live_looks(&st).active && !live_looks(&st).restart_needed, "override failed to compile: off");
    // the hook reports live_looks from the override it holds
    for (const bool v4 : {true, false}) {
        lualoader::Env env;
        lualoader::Engine eng(env);
        const std::string body = std::string("-- TURBO build 6AC07E31-2145C000 stock 9399e39a1e46a15d151ada423759abe9 table 000000000000\r\n") +
                                 (v4 ? "local TURBO_GATE = {}\r\nTURBO_GATE.band = function(initData)\r\nend\r\n" : "local TURBO_GATE = {}\r\n");
        static std::vector<std::string> keep;  // the engine keeps the pointer
        keep.push_back(body);
        lualoader::Override o;
        o.data = keep.back().data();
        o.size = keep.back().size();
        o.md5 = md5s(keep.back());
        eng.configure(0, 0, true, lualoader::default_rows(), {o}, "6AC07E31-2145C000");
        lualoader::Faces3dStatus fs3;
        eng.fill_status(fs3);
        check(fs3.live_looks == v4, std::string("hook status live_looks = ") + (v4 ? "true for a v4 override" : "false for an older one"));
    }
}

// The v4 gate itself, filled by the generator and run in a real Lua 5.4 (lua5.4 from MSYS2; SKIP when it is not on PATH)
static void test_gate_v4_in_lua() {
    const std::string t4 = template_v4_text();
    const size_t g0 = t4.find("--@@GATE-BEGIN\n"), g1 = t4.find("--@@GATE-END");
    check(g0 != std::string::npos && g1 != std::string::npos, "v4 gate block found in the template");
    if (g0 == std::string::npos || g1 == std::string::npos) return;
    const std::string stock = stock_text("\n");
    TOpts o;
    o.stock_md5 = md5s(stock);
    std::string tmpl = make_template(o);
    const size_t s0 = tmpl.find("--@@GATE-BEGIN\n");
    tmpl = tmpl.substr(0, s0) + t4.substr(g0, g1 + std::strlen("--@@GATE-END\n") - g0);
    Table t;
    t[230107] = Entry{155, 46, true, true, "baked"};
    const Built b = generate_body(tmpl, stock, t);
    check(b.ok, "synthetic v4 body builds (" + b.error + ")");
    if (!b.ok) return;
    const size_t a = b.bytes.find("local TURBO_GATE = {"), e = b.bytes.find("-- ===== END TURBO PER-PLAYER FACE GATE v4");
    check(a != std::string::npos && e != std::string::npos, "generated v4 gate found");
    if (a == std::string::npos || e == std::string::npos) return;
    const char* lua = std::getenv("LUA");
    const std::string luabin = lua && *lua ? lua : "lua5.4";
    if (std::system((luabin + " -v > " + (fs::temp_directory_path() / "turbo_lua_v.txt").string() + " 2>&1").c_str()) != 0) {
        std::printf("  SKIP v4 gate in Lua (%s not found)\n", luabin.c_str());
        return;
    }
    const std::string H155 = std::to_string(hair_band_code(155)), B46 = std::to_string(beard_band_code(46)),
                      H250 = std::to_string(hair_band_code(250)), B47 = std::to_string(beard_band_code(47));
    std::string drv = b.bytes.substr(a, e - a) + R"LUA(
local n, bad = 0, 0
local function ok(c, what) n = n + 1; if not c then bad = bad + 1; print("LUAFAIL " .. what) end end
local function P(t) t.PlayerId = t.PlayerId or 1; t.HeadClass = t.HeadClass or 0; t.HairStyle = t.HairStyle or 0
  t.Hair = t.Hair or 696; t.FacialHairType = t.FacialHairType or 259; return t end
local G = TURBO_GATE
ok(#G.bandHair == 1371 and #G.bandBeard == 105, "band lists 1371 / 105")
ok(G.bandHair[1] == )LUA" + std::to_string(band_hair_ids().front()) + R"LUA( and G.bandHair[1371] == )LUA" + std::to_string(band_hair_ids().back()) + R"LUA(, "band hair ends")
ok(G.hairId(P{Hair = )LUA" + H155 + R"LUA(}) == 155 and G.beardId(P{Hair = )LUA" + H155 + R"LUA(}) == nil, "band hair 155 only")
ok(G.beardId(P{FacialHairType = )LUA" + B46 + R"LUA(}) == 46 and G.hairId(P{FacialHairType = )LUA" + B46 + R"LUA(}) == nil, "band beard 46 only")
ok(G.strandBeardId(P{FacialHairType = )LUA" + B46 + R"LUA(}) == 46, "band beard on a real face replaces the strand beard")
ok(G.hairColours(P{Hair = )LUA" + H155 + R"LUA(}) == true and G.facialHairColours(P{FacialHairType = )LUA" + B46 + R"LUA(}) == true, "band colours from the DB")
ok(G.get(P{}) == nil and G.hairId(P{}) == nil and G.beardId(P{}) == nil, "EA codes 696/259: nothing (stock)")
ok(G.get(P{HeadClass = 1, Hair = )LUA" + H155 + R"LUA(, FacialHairType = )LUA" + B46 + R"LUA(}) == nil, "head class 1: no band")
ok(G.get(P{HairStyle = 1, Hair = )LUA" + H155 + R"LUA(}) == nil, "hair style 1: no band")
ok(G.hairId(P{PlayerId = 230107}) == 155 and G.beardId(P{PlayerId = 230107}) == 46, "baked entry without a band code")
ok(G.hairId(P{PlayerId = 230107, Hair = )LUA" + std::to_string(kHairOwnCode) + R"LUA(, FacialHairType = )LUA" + std::to_string(kBeardOwnCode) + R"LUA(}) == nil and
   G.beardId(P{PlayerId = 230107, Hair = )LUA" + std::to_string(kHairOwnCode) + R"LUA(, FacialHairType = )LUA" + std::to_string(kBeardOwnCode) + R"LUA(}) == nil and
   G.hairColours(P{PlayerId = 230107, Hair = )LUA" + std::to_string(kHairOwnCode) + R"LUA(}) == nil, "own codes 9999/511 skip the baked entry (stock)")
ok(G.beardId(P{PlayerId = 230107, Hair = )LUA" + std::to_string(kHairOwnCode) + R"LUA(, FacialHairType = )LUA" + B47 + R"LUA(}) == 47 and
   G.hairId(P{PlayerId = 230107, Hair = )LUA" + std::to_string(kHairOwnCode) + R"LUA(, FacialHairType = )LUA" + B47 + R"LUA(}) == nil, "own hair + band beard")
ok(G.get(P{Hair = )LUA" + std::to_string(kHairOwnCode) + R"LUA(}) ~= nil and G.hairId(P{Hair = )LUA" + std::to_string(kHairOwnCode) + R"LUA(}) == nil, "own code on a player not listed: stock")
ok(G.hairId(P{PlayerId = 230107, Hair = 8039, FacialHairType = 335}) == 155, "8039 / 335 are plain ids: the baked entry stands")
ok(G.hairId(P{PlayerId = 230107, Hair = )LUA" + H250 + R"LUA(, FacialHairType = )LUA" + B47 + R"LUA(}) == 250 and
   G.beardId(P{PlayerId = 230107, Hair = )LUA" + H250 + R"LUA(, FacialHairType = )LUA" + B47 + R"LUA(}) == 47, "band beats the baked entry")
ok(G.hairId(P{Hair = )LUA" + H155 + R"LUA(.0}) == 155, "float band code")
ok(G.get(P{Hair = )LUA" + H155 + R"LUA( + 0.5}) == nil, "fractional code")
for _, c in ipairs({8039, 9411, 10000, -3, 0}) do ok(G.get(P{Hair = c}) == nil, "hair code " .. c .. " outside the band") end
for _, c in ipairs({335, 441, 512}) do ok(G.get(P{FacialHairType = c}) == nil, "beard code " .. c .. " outside the band") end
local strict = setmetatable({PlayerId = 1, HeadClass = 0, HairStyle = 0}, {__index = function(t, k) error("no key " .. k) end})
ok(pcall(G.get, strict) and G.get(strict) == nil, "strict initData without Hair / FacialHairType: no error, nothing")
ok(pcall(G.get, "x") and G.get("x") == nil, "non-table initData: no error, nothing")
ok(G.get(P{Hair = )LUA" + H155 + R"LUA(}) == G.get(P{PlayerId = 9, Hair = )LUA" + H155 + R"LUA(}), "one memo entry per look")
G.enabled = false
ok(G.get(P{Hair = )LUA" + H155 + R"LUA(}) == nil, "enabled = false: nothing")
G.enabled = true
TURBO_FACES_GATE_OFF = true
ok(G.get(P{Hair = )LUA" + H155 + R"LUA(}) == nil, "TURBO_FACES_GATE_OFF: nothing")
print("LUARESULT " .. n .. " " .. bad)
)LUA";
    const fs::path f = fs::temp_directory_path() / ("turbo_gate_v4_" + std::to_string(std::rand()) + ".lua");
    const fs::path outp = f.string() + ".out";
    write_file(f, drv);
    const int rc = std::system((luabin + " \"" + f.string() + "\" > \"" + outp.string() + "\" 2>&1").c_str());
    std::string out;
    read_file(outp, out);
    const size_t r = out.find("LUARESULT ");
    int n = 0, bad = -1;
    if (r != std::string::npos) std::sscanf(out.c_str() + r, "LUARESULT %d %d", &n, &bad);
    check(rc == 0 && r != std::string::npos && n >= 25 && bad == 0, "v4 gate in Lua 5.4: " + std::to_string(n) + " checks, " + std::to_string(bad) + " failed");
    if (bad != 0) std::printf("%s\n", out.c_str());
    std::error_code ec;
    fs::remove(f, ec);
    fs::remove(outp, ec);
}

// The C++ generator against the harness-validated Python build (validate_gate_v4.py prints the same md5s)
static void test_real_v4() {
    const char* env = std::getenv("TURBO_RESEARCH_DIR");
    const fs::path research = env ? fs::u8path(env) : fs::current_path() / ".." / ".." / ".." / "research";
    const fs::path stock_p = research / ".." / "evidence" / "2026-10-15" / "bisect" / "stock_copy.lua";
    const fs::path pj_p = research / "c2_lua_loader" / "players_v3_test.json";
    std::string stock, pj;
    if (!read_file(stock_p, stock) || !read_file(pj_p, pj)) {
        std::printf("  SKIP real-stock v4 compare (research files not here)\n");
        return;
    }
    Table t;
    std::string err;
    check(parse_players_json(pj, t, &err), "players_v3_test.json parses");
    const Built e = generate_body(template_v4_text(), stock, Table{});
    check(e.ok && e.md5 == "afea57af5c80424c8d03e6a189639940" && e.bytes.size() == 282530,
          "real stock v4, empty list: body md5 == validate_gate_v4.py afea57af... (" + e.md5 + e.error + ")");
    const Built b = generate_body(template_v4_text(), stock, t);
    check(b.ok && b.md5 == "83cceb6fa1d9b5f83561d95e21b677c8", "real stock v4, launch set: body md5 == validate_gate_v4.py 83cceb6f... (" + b.md5 + ")");
    check(b.ok && lualoader::has_live_looks(b.bytes.data(), b.bytes.size()), "real stock v4 carries the live-looks marker");
    // the exact function the hook calls at the boot capture (win/lua_loader_win.cpp build_override)
    {
        std::string note, why;
        const auto mem = build_live_override(stock, t, note, why);
        check(mem && md5s(*mem) == b.md5 && note == "2 players on the saved list" && why.empty(), "build_live_override == the launch-set body (" + why + ")");
        const auto mem0 = build_live_override(stock, Table{}, note, why);
        check(mem0 && md5s(*mem0) == e.md5 && note == "0 players on the saved list", "build_live_override, empty list == the empty body");
        std::string bad2 = stock;
        bad2[200] = bad2[200] == 'q' ? 'r' : 'q';
        std::string why2;
        check(!build_live_override(bad2, t, note, why2) && why2.find("stock md5") != std::string::npos, "build_live_override refuses a changed stock: " + why2);
        Table junk;
        junk[5] = Entry{-5, 0, true, true, ""};  // the generator refuses a negative model id
        std::string why3;
        const auto mem3 = build_live_override(stock, junk, note, why3);
        check(mem3 && md5s(*mem3) == e.md5 && note.find("saved list refused") != std::string::npos && why3.empty(),
              "build_live_override: a bad saved list falls back to the bands alone (" + note + ")");
    }
    std::string bad_stock = stock;
    bad_stock[100] = bad_stock[100] == 'x' ? 'y' : 'x';
    const Built r = generate_body(template_v4_text(), bad_stock, t);
    check(!r.ok && r.bytes.empty() && r.error.find("stock md5") != std::string::npos, "v4 refuses a changed stock (as today)");
    std::printf("  real-stock v4 compare done: empty %s, launch set %s\n", e.md5.c_str(), b.md5.c_str());
}

int main() {
    test_players_json();
    test_players_json_v3();
    test_ids_and_validate();
    test_synthetic_generator();
    test_refusals();
    test_apply_remove();
    test_running_and_gate();
    test_real_f2();
    test_real_v3();
    test_bands();
    test_look_writes();
    test_live_looks_state();
    test_gate_v4_in_lua();
    test_real_v4();
    std::printf("faces3d: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
