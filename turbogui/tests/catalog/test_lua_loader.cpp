// Lua loader hook (core/lua_loader.h): md5, the signature set and the install decision on a synthetic code buffer, rows,
// override checks, log formatting, and the detour logic driven by a fake lua_load / fake readers (blob reader, getS, another
// reader). Nothing here touches the game; the Windows host (src/win/lua_loader_win.cpp) only adds MinHook and the file switches.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/devops.h"
#include "core/lua_loader.h"

using namespace turbo;
using namespace turbo::lualoader;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    if (ok) ++g_pass;
    else {
        ++g_fail;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

// ---------------------------------------------------------------- md5
static std::string md5s(const std::string& s) { return md5_hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

static void test_md5() {
    check(md5s("") == "d41d8cd98f00b204e9800998ecf8427e", "md5 empty");
    check(md5s("abc") == "900150983cd24fb0d6963f7d28e17f72", "md5 abc");
    check(md5s("message digest") == "f96b697d7cb7938d525a2f31aaf161d0", "md5 message digest");
    check(md5s("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6", "md5 fox");
    std::string d;
    for (int i = 0; i < 8; ++i) d += "1234567890";
    check(md5s(d) == "57edf4a22be3c955ac49da2e2107b67a", "md5 80 bytes (two blocks)");
    check(md5s(std::string(1000000, 'a')) == "7707d6ae4e027c70eea2a935c2296f21", "md5 one million a");
    // 55 / 56 / 64 bytes: the padding edges
    check(md5s(std::string(55, 'x')).size() == 32 && md5s(std::string(56, 'x')) != md5s(std::string(55, 'x')), "md5 padding edges");
}

// ---------------------------------------------------------------- synthetic code
static const uint64_t kBase = 0x140001000ull;
static std::vector<uint8_t> g_code;

static void put_pattern(size_t off, const std::string& pattern) {
    std::vector<uint8_t> b;
    std::vector<bool> m;
    parse_pattern(pattern, b, m);
    for (size_t i = 0; i < b.size(); ++i) g_code[off + i] = m[i] ? b[i] : 0x00;
}
static void put_rel32(size_t at_disp, size_t next_insn, size_t target) {
    const int32_t d = int32_t(int64_t(target) - int64_t(next_insn));
    std::memcpy(&g_code[at_disp], &d, 4);
}

struct Offs {
    size_t lua_load = 0x100, gets = 0x400, loadstring = 0x600, blob = 0x800;
};

// lua_load, getS, luaL_loadstring (lea getS / call lua_load) and the blob reader at fixed offsets; xcall_to = where the call goes
static void build_code(size_t xcall_to, bool good_gets = true, bool with_blob = true, bool twice = false) {
    Offs o;
    g_code.assign(0x2000, 0xCC);
    const auto sigs = signatures();
    put_pattern(o.lua_load, sigs[0].pattern);
    if (twice) put_pattern(0x1000, sigs[0].pattern);
    put_pattern(o.gets, std::string(good_gets ? kGetSExpect : "83 7A 08 00 75 03 33 C0 C3 8B 42 08 49 89 00 48 8B 03") + " C7 42 08 00 00 00 00 C3");
    put_pattern(o.loadstring, sigs[1].pattern);
    put_rel32(o.loadstring + 29 + 3, o.loadstring + 29 + 7, o.gets);       // lea rdx,[rip+getS]
    put_rel32(o.loadstring + 50 + 1, o.loadstring + 50 + 5, xcall_to);     // call lua_load
    if (with_blob) put_pattern(o.blob, sigs[3].pattern);
}

static bool code_read(uint64_t a, void* out, size_t n) {
    if (a < kBase || a + n > kBase + g_code.size()) return false;
    std::memcpy(out, &g_code[size_t(a - kBase)], n);
    return true;
}

static std::vector<SigResult> scan_code() {
    std::vector<SigResult> rs;
    for (const auto& s : signatures()) rs.push_back(resolve_signature(s, g_code.data(), g_code.size(), kBase));
    return rs;
}

static void test_signatures() {
    Offs o;
    const auto sigs = signatures();
    check(sigs.size() == 4 && sigs[0].name == kSigLuaLoad && sigs[1].name == kSigLuaLoadXcheck && sigs[2].name == kSigLoadStringReader &&
              sigs[3].name == kSigBlobReader,
          "signature set");
    for (const auto& s : sigs) check(signature_offset_checked(s), "offset checked: " + s.name);

    build_code(o.lua_load);
    auto rs = scan_code();
    for (const auto& r : rs) check(r.state == SigState::Found, "found on synthetic code: " + r.name);
    check(rs[0].address == kBase + o.lua_load, "lua_load address");
    check(rs[1].address == kBase + o.lua_load, "xcheck resolves to lua_load");
    check(rs[2].address == kBase + o.gets, "getS resolved through the lea");
    check(rs[3].address == kBase + o.blob, "blob reader address");
    Located L = decide_install(rs, code_read);
    check(L.ok && L.lua_load == kBase + o.lua_load && L.gets == kBase + o.gets && L.blob_reader == kBase + o.blob && L.why.empty(),
          "decide_install: all agree -> install (" + L.why + ")");

    // the call in luaL_loadstring goes somewhere else: refused
    build_code(0x1800);
    L = decide_install(scan_code(), code_read);
    check(!L.ok && L.why.find("cross-check disagrees") != std::string::npos, "cross-check mismatch refused (" + L.why + ")");

    // lua_load twice: ambiguous, refused
    build_code(o.lua_load, true, true, true);
    rs = scan_code();
    check(rs[0].state == SigState::Ambiguous, "two lua_load matches = ambiguous");
    L = decide_install(rs, code_read);
    check(!L.ok, "ambiguous lua_load refused");

    // lua_load missing: refused with the state in the reason
    build_code(o.lua_load);
    g_code[o.lua_load + 3] = 0x90;
    rs = scan_code();
    L = decide_install(rs, code_read);
    check(!L.ok && L.why.find("lua_load") == 0, "missing lua_load refused (" + L.why + ")");

    // the scan saw the function, but the live bytes now start with a jump (another hook landed): refused
    build_code(o.lua_load);
    rs = scan_code();
    g_code[o.lua_load] = 0xE9;
    L = decide_install(rs, code_read);
    check(!L.ok && L.why.find("jump") != std::string::npos, "prologue jump refused (" + L.why + ")");
    // changed but not a jump: refused too
    build_code(o.lua_load);
    rs = scan_code();
    g_code[o.lua_load + 5] = 0x81;
    L = decide_install(rs, code_read);
    check(!L.ok && L.why.find("changed") != std::string::npos, "prologue changed refused");

    // getS bytes differ: installs, getS unknown
    build_code(o.lua_load, false);
    L = decide_install(scan_code(), code_read);
    check(L.ok && L.gets == 0 && L.why.find("getS bytes differ") != std::string::npos, "getS mismatch -> installed without getS");
    // blob reader missing: installs, blob 0
    build_code(o.lua_load, true, false);
    L = decide_install(scan_code(), code_read);
    check(L.ok && L.blob_reader == 0 && L.why.find("blob reader") != std::string::npos, "blob missing -> installed without blob");
    // nothing scanned
    L = decide_install({}, code_read);
    check(!L.ok, "no results refused");

    // prologue checks
    const uint8_t e9[] = {0xE9, 0, 0, 0, 0}, ff25[] = {0xFF, 0x25, 0, 0, 0, 0}, cc[] = {0xCC}, eb[] = {0xEB, 0x10},
                  movabs[] = {0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8, 0xFF, 0xE0}, pushret[] = {0x68, 1, 2, 3, 4, 0xC3};
    check(prologue_is_jump(e9, sizeof(e9)) && prologue_is_jump(ff25, sizeof(ff25)) && prologue_is_jump(cc, 1) && prologue_is_jump(eb, 2) &&
              prologue_is_jump(movabs, sizeof(movabs)) && prologue_is_jump(pushret, sizeof(pushret)),
          "prologue_is_jump: hooks seen");
    check(!prologue_is_jump(kLuaLoadPrologue, sizeof(kLuaLoadPrologue)), "prologue_is_jump: lua_load's own bytes are not a jump");
    check(prologue_is_jump(nullptr, 0), "prologue_is_jump: no bytes = refuse");
}

// ---------------------------------------------------------------- rows, overrides, formatting
static void test_rows_and_format() {
    const auto rows = default_rows();
    check(rows.size() == 1 && rows[0].name == "player_kit" && rows[0].size == 0x41140 && rows[0].md5 == "9399e39a1e46a15d151ada423759abe9" &&
              rows[0].override_rel == "lua_override\\player_kit.lua",
          "default row player_kit");
    check(match_row(rows, 0x41140, "9399e39a1e46a15d151ada423759abe9") == 0, "row match");
    check(match_row(rows, 0x41141, "9399e39a1e46a15d151ada423759abe9") == -1, "row size mismatch");
    check(match_row(rows, 0x41140, "9399e39a1e46a15d151ada423759abe8") == -1, "row md5 mismatch");
    check(match_row({}, 1, "x") == -1, "no rows");

    check(override_refusal("").find("empty") != std::string::npos, "override empty refused");
    check(override_refusal(std::string("\x1bLua\x54", 5)).find("binary") != std::string::npos, "override binary refused");
    check(override_refusal("\xEF\xBB\xBFlocal a = 1").find("BOM") != std::string::npos, "override BOM refused");
    check(override_refusal(std::string("local a\0 = 1", 12)).find("NUL") != std::string::npos, "override NUL refused");
    check(override_refusal("local a = 1\r\nreturn a\r\n").empty(), "override text accepted");

    check(mode_allows_text(nullptr) && mode_allows_text("t") && mode_allows_text("bt") && !mode_allows_text("b"), "mode_allows_text");
    check(quote_chunkname("@a\"b\\c", 6, 50) == "\"@a\\\"b\\\\c\"", "quote escapes");
    check(quote_chunkname("x\ny\x01", 4, 50) == "\"x\\ny\\x01\"", "quote control bytes");
    check(quote_chunkname("abcdef", 6, 3) == "\"abc\"...", "quote cut");

    CallRecord r;
    r.seq = 7;
    r.time = "12:12:08.931";
    r.thread = 4321;
    r.caller = "FC27.exe+0x89E1D7C";
    r.reader = "FC27.exe+0x89E182C";
    r.kind = ReaderKind::Blob;
    r.chunk = "\"@data/player_kit.lua\"";
    r.mode = "(null)";
    r.have_source = true;
    r.size = 0x41140;
    r.md5 = "9399e39a1e46a15d151ada423759abe9";
    r.rc = 0;
    check(format_line(r) ==
              "lua_loader: #7 t=12:12:08.931 tid=4321 caller=FC27.exe+0x89E1D7C reader=FC27.exe+0x89E182C(blob) chunk=\"@data/player_kit.lua\" "
              "mode=(null) size=0x41140(266560) md5=9399e39a1e46a15d151ada423759abe9 rc=0",
          "format_line log-only: " + format_line(r));
    r.substituted = true;
    r.row = "player_kit";
    r.new_size = 0x4198D;
    r.new_md5 = "48637b3e6ba3a69838cdc2984e5a59c0";
    r.rc = 3;
    check(format_line(r).find(" SUBSTITUTED row=player_kit new_size=0x4198D(268685) new_md5=48637b3e6ba3a69838cdc2984e5a59c0 rc=3") != std::string::npos,
          "format_line substituted: " + format_line(r));
    CallRecord o;
    o.kind = ReaderKind::Other;
    o.chunk = "(null)";
    o.mode = "\"b\"";
    o.note = "x";
    o.rc = -1;
    const std::string ol = format_line(o);
    check(ol.find("(other) chunk=(null) mode=\"b\" size=- note=\"x\" rc=-1") != std::string::npos, "format_line other: " + ol);

    LineQueue q(2);
    q.push("a");
    q.push("b");
    q.push("c");
    auto t = q.take();
    check(t.size() == 2 && t[0] == "a" && t[1] == "b" && q.dropped() == 1 && q.take().empty(), "line queue cap + drop count");
}

// ---------------------------------------------------------------- detour logic with fakes
// The fake lua_load pulls the whole chunk through the reader like luaZ_fill (until NULL or size 0) into the std::string that L
// points at; rc 0 for text, 3 for a source that starts with "ERR", -7 for a binary chunk in mode "t"
static int g_fake_calls = 0;
static int fake_lua_load(void* L, Reader reader, void* data, const char* chunkname, const char* mode) {
    ++g_fake_calls;
    auto* out = static_cast<std::string*>(L);
    out->clear();
    for (int i = 0; i < 100; ++i) {
        size_t n = 0;
        const char* p = reader(L, data, &n);
        if (!p || n == 0) break;
        out->append(p, n);
    }
    if (!out->empty() && (*out)[0] == 0x1B && mode && !std::strchr(mode, 'b')) return -7;
    return out->compare(0, 3, "ERR") == 0 ? 3 : 0;
}

// The game's blob reader, as decoded: data {Blob*, bool done}; first call: done = 1, size = vf[1](), return vf[0](); then NULL
struct FakeBlob {
    std::string bytes;
    int vf_calls = 0;
};
struct BlobData {
    FakeBlob* blob;
    bool done;
};
static const char* game_blob_reader(void*, void* data, size_t* size) {
    auto* d = static_cast<BlobData*>(data);
    if (d->done) return nullptr;
    d->done = true;
    *size = uint32_t(d->blob->bytes.size());
    ++d->blob->vf_calls;
    ++d->blob->vf_calls;
    return d->blob->bytes.data();
}
// LoadS {const char* s; uint32 size} and getS as decoded from 6AB9813C
struct LoadS {
    const char* s;
    uint32_t size;
};
static const char* game_getS(void*, void* data, size_t* size) {
    auto* ls = static_cast<LoadS*>(data);
    if (ls->size == 0) return nullptr;
    *size = ls->size;
    ls->size = 0;
    return ls->s;
}
static const char* other_reader(void* L, void* data, size_t* size) { return game_getS(L, data, size); }

static Engine* g_eng = nullptr;
// What MinHook does: the address lua_load receives (the blob reader's own address) now runs the detour
static const char* hooked_blob_reader(void* L, void* data, size_t* size) { return g_eng->on_blob_read(&game_blob_reader, L, data, size); }
// the lua_load detour, with a fixed caller address
static int hooked_lua_load(void* L, Reader r, void* d, const char* c, const char* m) { return g_eng->on_lua_load(&fake_lua_load, L, r, d, c, m, 0x14089E1D7Cull); }

// A reader that compiles another chunk while it is being called (nested lua_load on the same thread)
static std::string g_nested_out;
static const char* nesting_reader(void* L, void* data, size_t* size) {
    if (static_cast<LoadS*>(data)->size != 0) {  // only on the first pull (the second returns NULL)
        LoadS inner{"return 2", 8};
        hooked_lua_load(&g_nested_out, &other_reader, &inner, "=inner", nullptr);
    }
    return game_getS(L, data, size);
}

static std::vector<std::string> g_lines;

static Engine* make_engine(bool substitute, const std::string& stock, const std::string& over, bool over_ok = true) {
    Env env;
    env.read = [](uint64_t a, void* out, size_t n) {
        if (a < 0x10000) return false;  // the test's "unmapped" addresses
        std::memcpy(out, reinterpret_cast<const void*>(static_cast<uintptr_t>(a)), n);
        return true;
    };
    env.readable = [](const void* p, size_t) { return reinterpret_cast<uint64_t>(p) >= 0x10000; };
    env.where = [](uint64_t a) {
        if (a == reinterpret_cast<uint64_t>(&hooked_blob_reader)) return std::string("BLOB");
        if (a == reinterpret_cast<uint64_t>(&game_getS)) return std::string("GETS");
        char b[32];
        std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(a));
        return std::string(b);
    };
    env.thread_id = []() { return uint32_t(1); };
    env.now = []() { return std::string("00:00:00.000"); };
    env.emit = [](const std::string& s) { g_lines.push_back(s); };
    auto* e = new Engine(env);
    std::vector<Row> rows = {{"player_kit", stock.size(), md5s(stock), "lua_override\\player_kit.lua"}};
    std::vector<Override> ovs(1);
    if (over_ok) {
        auto* keep = new std::string(over);  // leaked on purpose, like the host
        ovs[0].data = keep->data();
        ovs[0].size = keep->size();
        ovs[0].md5 = md5s(over);
    } else {
        ovs[0].error = "file missing";
    }
    e->configure(reinterpret_cast<uint64_t>(&game_getS), reinterpret_cast<uint64_t>(&hooked_blob_reader), substitute, rows, ovs);
    return e;
}

static bool has(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

static void test_engine() {
    const std::string stock = "-- stock player_kit\r\nlocal a = 1\r\nreturn a\r\n";
    const std::string over = "-- override\r\nlocal a = 2\r\nreturn a\r\n";
    std::string out;

    // ---- log-only, blob reader: the parser gets exactly the original bytes, the reader ran once (vf calls 2), one line
    g_eng = make_engine(false, stock, over);
    g_lines.clear();
    FakeBlob fb{stock, 0};
    BlobData bd{&fb, false};
    int rc = hooked_lua_load(&out, &hooked_blob_reader, &bd, "@data/fifarna/lua/assets/player_kit.lua", nullptr);
    check(rc == 0 && out == stock && fb.vf_calls == 2 && bd.done, "log-only blob: original bytes, reader once, done set");
    check(g_lines.size() == 1 && has(g_lines[0], "reader=BLOB(blob)") && has(g_lines[0], "chunk=\"@data/fifarna/lua/assets/player_kit.lua\"") &&
              has(g_lines[0], "md5=" + md5s(stock)) && has(g_lines[0], "caller=0x14089E1D7C") && has(g_lines[0], "tid=1") && has(g_lines[0], " rc=0") &&
              !has(g_lines[0], "SUBSTITUTED"),
          "log-only blob line: " + (g_lines.empty() ? std::string("none") : g_lines[0]));
    char sz[64];
    std::snprintf(sz, sizeof(sz), "size=0x%zX(%zu)", stock.size(), stock.size());
    check(!g_lines.empty() && has(g_lines[0], sz), "log-only blob size");

    // ---- log-only: a matching row is NOT substituted
    g_lines.clear();
    FakeBlob fb2{stock, 0};
    BlobData bd2{&fb2, false};
    rc = hooked_lua_load(&out, &hooked_blob_reader, &bd2, "@x", nullptr);
    check(out == stock && g_eng->stats().substituted == 0, "log-only never substitutes");

    // ---- log-only, getS: read from the struct, not called early; the parser gets the original
    g_lines.clear();
    std::string src = "return 1";
    LoadS ls{src.c_str(), uint32_t(src.size())};
    rc = hooked_lua_load(&out, &game_getS, &ls, src.c_str(), nullptr);
    check(rc == 0 && out == src && ls.size == 0, "log-only getS: original bytes");
    check(g_lines.size() == 1 && has(g_lines[0], "reader=GETS(loadstring)") && has(g_lines[0], "md5=" + md5s(src)) && has(g_lines[0], "chunk=\"return 1\""),
          "log-only getS line: " + (g_lines.empty() ? std::string("none") : g_lines[0]));

    // ---- other reader: untouched, size not measured
    g_lines.clear();
    LoadS lo{src.c_str(), uint32_t(src.size())};
    rc = hooked_lua_load(&out, &other_reader, &lo, "=(load)", "bt");
    check(out == src && g_lines.size() == 1 && has(g_lines[0], "(other)") && has(g_lines[0], "size=-") && has(g_lines[0], "mode=\"bt\""),
          "other reader pass-through: " + (g_lines.empty() ? std::string("none") : g_lines[0]));

    // ---- chunkname / mode unreadable or null
    g_lines.clear();
    LoadS l2{src.c_str(), uint32_t(src.size())};
    hooked_lua_load(&out, &other_reader, &l2, reinterpret_cast<const char*>(0x100), reinterpret_cast<const char*>(0x200));
    check(g_lines.size() == 1 && has(g_lines[0], "chunk=(unreadable)") && has(g_lines[0], "mode=(unreadable)"), "unreadable chunkname/mode");
    g_lines.clear();
    LoadS l3{src.c_str(), uint32_t(src.size())};
    hooked_lua_load(&out, &other_reader, &l3, nullptr, nullptr);
    check(g_lines.size() == 1 && has(g_lines[0], "chunk=(null)"), "null chunkname");

    // ---- orphan blob read (outside a hooked lua_load): passes unchanged
    FakeBlob fo{stock, 0};
    BlobData bo{&fo, false};
    size_t n = 0;
    const char* p = hooked_blob_reader(nullptr, &bo, &n);
    check(p == fo.bytes.data() && n == stock.size() && g_eng->stats().blob_orphans == 1, "orphan blob read passes");

    // ---- nested lua_load: the inner call passes through without a line, the outer is logged
    g_lines.clear();
    LoadS ln{src.c_str(), uint32_t(src.size())};
    const long long nested0 = g_eng->stats().nested;
    hooked_lua_load(&out, &nesting_reader, &ln, "=outer", nullptr);
    check(out == src && g_nested_out == "return 2" && g_eng->stats().nested == nested0 + 1 && g_lines.size() == 1 && has(g_lines[0], "=outer"),
          "nested lua_load passes through");

    // ---- kill switch: nothing logged, nothing measured
    g_lines.clear();
    g_eng->set_killed(true);
    FakeBlob fk{stock, 0};
    BlobData bk{&fk, false};
    rc = hooked_lua_load(&out, &hooked_blob_reader, &bk, "@k", nullptr);
    check(out == stock && g_lines.empty() && fk.vf_calls == 2, "killed: pass-through, no line");
    g_eng->set_killed(false);

    // ---- SUBSTITUTE, blob, matching row: the parser gets the override; the game reader still ran exactly once
    g_eng = make_engine(true, stock, over);
    g_lines.clear();
    FakeBlob fs1{stock, 0};
    BlobData bs1{&fs1, false};
    rc = hooked_lua_load(&out, &hooked_blob_reader, &bs1, "@player_kit", nullptr);
    check(rc == 0 && out == over && fs1.vf_calls == 2 && bs1.done && g_eng->stats().substituted == 1, "substitute blob: override parsed");
    check(g_lines.size() == 1 && has(g_lines[0], "SUBSTITUTED row=player_kit") && has(g_lines[0], "new_md5=" + md5s(over)) && has(g_lines[0], "md5=" + md5s(stock)),
          "substitute blob line: " + (g_lines.empty() ? std::string("none") : g_lines[0]));

    // ---- SUBSTITUTE, non-matching source: original
    g_lines.clear();
    FakeBlob fs2{stock + "-- 1", 0};
    BlobData bs2{&fs2, false};
    hooked_lua_load(&out, &hooked_blob_reader, &bs2, "@other", nullptr);
    check(out == stock + "-- 1" && g_lines.size() == 1 && !has(g_lines[0], "SUBSTITUTED"), "substitute: other source unchanged");

    // ---- SUBSTITUTE, mode "b": text not allowed -> not substituted
    g_lines.clear();
    FakeBlob fs3{stock, 0};
    BlobData bs3{&fs3, false};
    hooked_lua_load(&out, &hooked_blob_reader, &bs3, "@b", "b");
    check(out == stock && g_lines.size() == 1 && !has(g_lines[0], "SUBSTITUTED"), "substitute: mode b refused");

    // ---- SUBSTITUTE, getS matching: Turbo's reader serves the override
    g_lines.clear();
    LoadS lg{stock.c_str(), uint32_t(stock.size())};
    rc = hooked_lua_load(&out, &game_getS, &lg, stock.c_str(), "t");
    check(rc == 0 && out == over && g_lines.size() == 1 && has(g_lines[0], "SUBSTITUTED"), "substitute getS");

    // ---- binary chunk with matching row: never substituted
    const std::string bin = std::string("\x1bLua") + "TTTT";
    Engine* eb = make_engine(true, bin, over);
    g_eng = eb;
    g_lines.clear();
    FakeBlob fb3{bin, 0};
    BlobData bb3{&fb3, false};
    hooked_lua_load(&out, &hooked_blob_reader, &bb3, "@bin", nullptr);
    check(out == bin && g_lines.size() == 1 && has(g_lines[0], "binary chunk") && !has(g_lines[0], "SUBSTITUTED"), "binary chunk never substituted");

    // ---- row matched but its override is not loaded
    g_eng = make_engine(true, stock, over, false);
    g_lines.clear();
    FakeBlob fb4{stock, 0};
    BlobData bb4{&fb4, false};
    hooked_lua_load(&out, &hooked_blob_reader, &bb4, "@nf", nullptr);
    check(out == stock && g_lines.size() == 1 && has(g_lines[0], "override is not loaded"), "row matched, override missing");

    // ---- rc is the original's
    g_eng = make_engine(false, stock, over);
    g_lines.clear();
    LoadS le{"ERR x", 5};
    rc = hooked_lua_load(&out, &game_getS, &le, "ERR x", nullptr);
    check(rc == 3 && g_lines.size() == 1 && has(g_lines[0], " rc=3"), "rc passed through and logged");
    check(g_fake_calls > 0 && g_eng->stats().errors == 0, "no engine errors");
}

int main() {
    test_md5();
    test_signatures();
    test_rows_and_format();
    test_engine();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
