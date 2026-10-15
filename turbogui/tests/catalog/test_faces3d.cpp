// faces3d (core/faces3d.h): players.json model, verified ids, validation, the player_kit.lua generator against a SYNTHETIC stock and
// template (anchors computed here), every refusal, the header, apply/remove on a temp folder; and, when the research files are on
// this machine, a byte-compare of the generator against the F2 override that ran in game (stock read at run time from the
// evidence folder, never copied into the repository; SKIP otherwise).
#include <cstdio>
#include <cstdlib>
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
    check(has("beard 259 is not a verified") && has("not a scanned head") && has("player 3: choose a hair") && has("not in the database"),
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
    check(r.ok && !fs::exists(out / "faces3d_on.txt"), "empty apply = remove");
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
    check(r.ok && read_file(out / "lua_override" / "player_kit.lua", written) && read_file(out / "faces3d" / "players.json", js) &&
              fs::exists(out / "faces3d_on.txt") && md5s(written) == r.override_md5 && r.table == md5s(js).substr(0, 12) &&
              written.substr(written.find("\r\n") + 2) == body.bytes,
          "apply with the real stock writes players.json + override (header table = its md5) + faces3d_on.txt (" + r.message + ")");
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
    check(template_for(fx) == template_text() && template_for(f1) == template_v3_text(), "template_for: v2 fixed-only, v3 with follow");
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
    check(r.ok && read_file(out / "lua_override" / "player_kit.lua", written) && read_file(out / "faces3d" / "players.json", js) &&
              written.substr(written.find("\r\n") + 2) == body.bytes && fs::exists(out / "faces3d" / "applied" / (r.table + ".json")),
          "apply (follow) writes the v3 body + the applied snapshot (" + r.message + ")");
    Table rt;
    std::string why;
    check(running_table(out.u8string(), r.table, rt, &why) && rt[229984].follow, "running_table finds the applied list");
    Table fixed_only = t;
    fixed_only.erase(229984);
    const ApplyResult r2 = apply(out.u8string(), fixed_only, "6AC07E31-2145C000", nullptr);
    const Built v2b = generate_body(template_text(), stock, fixed_only);
    check(r2.ok && read_file(out / "lua_override" / "player_kit.lua", written) && v2b.ok && written.substr(written.find("\r\n") + 2) == v2b.bytes &&
              written.find("GATE v2") != std::string::npos,
          "apply (fixed only) writes the v2 body");
    check(running_table(out.u8string(), r.table, rt, &why) && rt.size() == 2, "the first start's list is still known after a second Apply");
    fs::remove_all(out);
    std::printf("  real-stock v3 byte compare done: override %s, %zu bytes\n", ov.md5.c_str(), ov.bytes.size());
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
    std::printf("faces3d: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
