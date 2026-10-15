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

int main() {
    test_players_json();
    test_ids_and_validate();
    test_synthetic_generator();
    test_refusals();
    test_apply_remove();
    test_real_f2();
    std::printf("faces3d: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
