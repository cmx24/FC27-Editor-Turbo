// Lua loader hook (core/lua_loader.h): md5, the signature set and the install decision on a synthetic code buffer, rows,
// override checks, log formatting, and the detour logic driven by a fake lua_load / fake readers (blob reader, getS, another
// reader). Nothing here touches the game; the Windows host (src/win/lua_loader_win.cpp) only adds MinHook and the file switches.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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
static void build_code(size_t xcall_to, bool good_gets = true, bool with_blob = true, bool twice = false, bool good_top = true) {
    Offs o;
    g_code.assign(0x2000, 0xCC);
    const auto sigs = signatures();
    put_pattern(o.lua_load, sigs[0].pattern);
    if (good_top) put_pattern(o.lua_load + kLuaLoadTopCheckOffset, "44 8B D8 85 C0 75 4C 48 8B 4B 10 4C 8B 51 F0");
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
    check(L.ok && L.lua_load == kBase + o.lua_load && L.gets == kBase + o.gets && L.blob_reader == kBase + o.blob && L.top_layout_ok && L.why.empty(),
          "decide_install: all agree -> install (" + L.why + ")");
    // the bytes after luaD_protectedparser differ: installs, but no chunkname substitution (no safe pop)
    build_code(o.lua_load, true, true, false, false);
    L = decide_install(scan_code(), code_read);
    check(L.ok && !L.top_layout_ok && L.why.find("stack layout not confirmed") != std::string::npos, "top layout mismatch -> installed, no chunkname (" + L.why + ")");
    build_code(o.lua_load);
    g_code[o.lua_load + kLuaLoadTopCheckOffset + 10] = 0x18;  // mov rcx,[rbx+18h]: another field
    L = decide_install(scan_code(), code_read);
    check(L.ok && !L.top_layout_ok, "top offset changed -> layout not confirmed");

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
              rows[0].override_rel == "lua_override\\player_kit.lua" && rows[0].chunkname == "data/fifarna/lua/assets/player_kit.lua" &&
              rows[0].build == "6AC07E31-2145C000" && std::string(kChunknameBuild) == "6AC07E31-2145C000",
          "default row player_kit (content + chunkname, build gate 6AC07E31)");
    const std::string pk = "data/fifarna/lua/assets/player_kit.lua";
    check(chunkname_equal(pk, pk.c_str(), pk.size()), "chunkname exact");
    check(chunkname_equal(pk, ("@" + pk).c_str(), pk.size() + 1), "chunkname leading @ tolerated");
    check(chunkname_equal("@" + pk, pk.c_str(), pk.size()), "row with @ matches name without");
    check(!chunkname_equal(pk, "data/fifarna/lua/assets/player_kit.lu", pk.size() - 1), "chunkname prefix refused");
    check(!chunkname_equal(pk, (pk + "x").c_str(), pk.size() + 1), "chunkname longer refused");
    check(!chunkname_equal(pk, ("@@" + pk).c_str(), pk.size() + 2), "only one @ stripped");
    check(!chunkname_equal(pk, "DATA/fifarna/lua/assets/player_kit.lua", pk.size()), "chunkname case-sensitive");
    check(!chunkname_equal("", "", 0) && !chunkname_equal("@", "@", 1), "empty chunkname never matches");
    check(match_chunkname_row(rows, pk.c_str(), pk.size()) == 0 && match_chunkname_row(rows, "x", 1) == -1, "match_chunkname_row");
    std::vector<Row> nocontent = {{"n", 0, "", "x", pk, "B"}};
    check(match_row(nocontent, 0, "") == -1, "row without size/md5 never matches by content");
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
    CallRecord c;
    c.kind = ReaderKind::Other;
    c.chunk = "\"data/fifarna/lua/assets/player_kit.lua\"";
    c.mode = "(null)";
    c.substituted = true;
    c.by_chunkname = true;
    c.row = "player_kit";
    c.new_size = 266580;
    c.new_md5 = "6165225a23f50000e6763759119800dc";
    const std::string cl = format_line(c);
    check(cl.find("size=- SUBSTITUTED row=player_kit override bytes 266580 md5 6165225a23f50000e6763759119800dc (stock unknown: chunkname match) rc=0") !=
              std::string::npos,
          "format_line chunkname substitution: " + cl);
    CallRecord f = c;
    f.substituted = false;
    f.fallback = true;
    f.override_rc = 3;
    f.override_msg = "\"[string] x\"";
    const std::string fl = format_line(f);
    check(fl.find(" fallback to original (override failed: rc=3 \"[string] x\") rc=0") != std::string::npos && fl.find("SUBSTITUTED") == std::string::npos,
          "format_line fallback: " + fl);

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
static std::mutex g_lines_m;  // the concurrency test emits from several threads

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
    env.emit = [](const std::string& s) {
        std::lock_guard<std::mutex> lk(g_lines_m);
        g_lines.push_back(s);
    };
    auto* e = new Engine(env);
    std::vector<Row> rows = {{"player_kit", stock.size(), md5s(stock), "lua_override\\player_kit.lua", "", ""}};
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

// ---------------------------------------------------------------- substitution by chunkname (a reader Turbo cannot measure)
// The fake Lua state: what lua_load compiled, and the stack depth in bytes. The fake lua_load pushes exactly one slot (the
// function, or on rc != 0 the error message), like Lua 5.4's lua_load; extra_push simulates a stack that is not as expected.
struct FakeL {
    std::string out;
    long long depth = 32;
    int pops = 0;
    int loads = 0;
    bool depth_unreadable = false;
    bool extra_push = false;
    std::string err;
};
static std::atomic<int> g_fakeL_calls{0};
static int fake_lua_load_L(void* Lv, Reader reader, void* data, const char*, const char* mode) {
    ++g_fakeL_calls;
    auto* L = static_cast<FakeL*>(Lv);
    ++L->loads;
    L->out.clear();
    for (int i = 0; i < 100; ++i) {
        size_t n = 0;
        const char* p = reader(Lv, data, &n);
        if (!p || n == 0) break;
        L->out.append(p, n);
    }
    L->depth += 16;
    if (L->extra_push) L->depth += 16;
    if (L->out.compare(0, 3, "ERR") == 0 || L->out.find("\nERR") != std::string::npos) {  // a line starting with ERR does not compile
        L->err = "\"[string \\\"player_kit\\\"]:1: syntax error\"";
        return 3;
    }
    if (!L->out.empty() && L->out[0] == 0x1B && mode && !std::strchr(mode, 'b')) return 3;
    return 0;
}
// The game's unsupported reader +0x46AE738 (6AB9813C counterpart 0x1446B08C8): data {Obj*, bool done}; one shot
struct OtherObj {
    std::string bytes;
    std::atomic<int> calls{0};
};
struct OtherData {
    OtherObj* o;
    bool done;
};
static const char* game_other_reader(void*, void* data, size_t* size) {
    auto* d = static_cast<OtherData*>(data);
    if (d->done) {
        *size = 0;
        return nullptr;
    }
    d->done = true;
    ++d->o->calls;
    *size = d->o->bytes.size();
    return d->o->bytes.data();
}
static int hooked_lua_load_L(void* L, Reader r, void* d, const char* c, const char* m) { return g_eng->on_lua_load(&fake_lua_load_L, L, r, d, c, m, 0x142277C9Cull); }

static std::atomic<uint32_t> g_next_tid{100};
static thread_local uint32_t t_tid = 0;

// ---- STOCK CAPTURE (faces3d WP-B): the verified one-shot reader is drained by Turbo, the parser always gets Turbo's replay
// A multi-chunk reader (the generic lua_Reader protocol): pieces in order, then NULL (or a non-NULL pointer with size 0)
struct MultiData {
    std::vector<std::string> parts;
    size_t i = 0;
    int calls = 0;
    bool zero_size_end = false;
};
static const char* multi_reader(void*, void* data, size_t* size) {
    auto* m = static_cast<MultiData*>(data);
    ++m->calls;
    if (m->i >= m->parts.size()) {
        *size = 0;
        return m->zero_size_end ? "" : nullptr;
    }
    const std::string& s = m->parts[m->i++];
    *size = s.size();
    return s.data();
}

static std::atomic<bool> g_verify{true};       // reader_capturable answer for the fake readers
static const void* g_unreadable = nullptr;      // env.readable says no for this pointer
static std::atomic<bool> g_save_throws{false};
struct Saved {
    std::mutex m;
    int count = 0;
    std::string row, md5, bytes;
};
static Saved g_saved;

struct ChunkOpts {
    bool substitute = true;
    std::string over;                 // override bytes ("" = not loaded)
    std::string running = "6AC07E31-2145C000";
    bool with_fallback = true;
    CaptureConfig cap{true, false, false};
    std::string row_md5;              // the row's expected stock md5 ("" = the default row's)
};

static Engine* make_chunk_engine(const ChunkOpts& o) {
    Env env;
    env.read = [](uint64_t a, void* out, size_t n) {
        if (a < 0x10000) return false;
        std::memcpy(out, reinterpret_cast<const void*>(static_cast<uintptr_t>(a)), n);
        return true;
    };
    env.readable = [](const void* p, size_t) { return reinterpret_cast<uint64_t>(p) >= 0x10000 && p != g_unreadable; };
    env.where = [](uint64_t a) {
        char b[32];
        std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(a));
        return std::string(b);
    };
    env.thread_id = []() {
        if (!t_tid) t_tid = ++g_next_tid;
        return t_tid;
    };
    env.now = []() { return std::string("15:08:54.651"); };
    env.emit = [](const std::string& s) {
        std::lock_guard<std::mutex> lk(g_lines_m);
        g_lines.push_back(s);
    };
    if (o.with_fallback) {
        env.stack_depth = [](void* L) { return static_cast<FakeL*>(L)->depth_unreadable ? -1LL : static_cast<FakeL*>(L)->depth; };
        env.top_message = [](void* L) { return static_cast<FakeL*>(L)->err; };
        env.pop1 = [](void* L) {
            static_cast<FakeL*>(L)->depth -= 16;
            ++static_cast<FakeL*>(L)->pops;
            return true;
        };
    }
    env.reader_capturable = [](uint64_t r) {
        return g_verify.load() && (r == reinterpret_cast<uint64_t>(&game_other_reader) || r == reinterpret_cast<uint64_t>(&multi_reader));
    };
    env.save_capture = [](const std::string& row, std::shared_ptr<const std::string> bytes, const std::string& md5) {
        if (g_save_throws.load()) throw std::runtime_error("save failed");
        std::lock_guard<std::mutex> lk(g_saved.m);
        ++g_saved.count;
        g_saved.row = row;
        g_saved.md5 = md5;
        g_saved.bytes = *bytes;
    };
    {
        std::lock_guard<std::mutex> lk(g_saved.m);
        g_saved.count = 0;
        g_saved.bytes.clear();
        g_saved.md5.clear();
    }
    auto* e = new Engine(env);
    std::vector<Row> rows = default_rows();
    if (!o.row_md5.empty()) rows[0].md5 = o.row_md5;
    std::vector<Override> ovs(1);
    if (!o.over.empty()) {
        auto* keep = new std::string(o.over);
        ovs[0].data = keep->data();
        ovs[0].size = keep->size();
        ovs[0].md5 = md5s(o.over);
    } else {
        ovs[0].error = "file missing";
    }
    e->configure(reinterpret_cast<uint64_t>(&game_getS), reinterpret_cast<uint64_t>(&hooked_blob_reader), o.substitute, rows, ovs, o.running, o.cap);
    return e;
}

static std::string line0() {
    std::lock_guard<std::mutex> lk(g_lines_m);
    return g_lines.empty() ? std::string("none") : g_lines[0];
}

struct OneCall {
    FakeL L;
    OtherObj ob;
    OtherData od{nullptr, false};
    int rc = -99;
};
static void run_other(OneCall& c, const std::string& stock, const std::string& chunk, const char* mode = nullptr) {
    c.ob.bytes = stock;
    c.od = OtherData{&c.ob, false};
    c.rc = hooked_lua_load_L(&c.L, &game_other_reader, &c.od, chunk.c_str(), mode);
}

static void test_header_and_switches() {
    const std::string md5 = "9399e39a1e46a15d151ada423759abe9";
    OverrideHeader h = parse_override_header("-- TURBO build 6AC07E31-2145C000 stock " + md5 + " table 1a2b3c\r\nlocal x = 1\r\n");
    check(h.ok && h.build == "6AC07E31-2145C000" && h.stock == md5 && h.table == "1a2b3c", "header CRLF parsed");
    h = parse_override_header("--  TURBO   build B  stock 9399E39A1E46A15D151ADA423759ABE9 table t\nreturn 1\n");
    check(h.ok && h.build == "B" && h.stock == md5, "header LF, repeated spaces, upper-case md5 normalised");
    h = parse_override_header("-- TURBO build B stock " + md5 + " table t");
    check(h.ok, "header without newline");
    check(!parse_override_header("local x = 1\n").ok && has(parse_override_header("local x = 1\n").error, "no TURBO header"), "missing header refused");
    check(!parse_override_header("\n-- TURBO build B stock " + md5 + " table t\n").ok, "header must be the first line");
    check(!parse_override_header("-- TURBO build B stock " + md5 + "\n").ok, "header without table refused");
    check(!parse_override_header("-- TURBO build B stock nothex table t\n").ok, "header stock not md5 refused");
    check(!parse_override_header("-- TURBO stock " + md5 + " build B table t\n").ok, "header fields out of order refused");
    check(make_override_header("B", "9399E39A1E46A15D151ADA423759ABE9", "t") == "-- TURBO build B stock " + md5 + " table t", "make_override_header");
    check(parse_override_header(make_override_header("B", md5, "t") + "\n").ok, "make -> parse round trip");

    StartSwitches s;
    StartDecision d = decide_start(s);
    check(!d.opted_in && !d.substitute, "start: nothing opted in");
    s.lua_on = true;
    d = decide_start(s);
    check(d.opted_in && !d.refused && !d.substitute && d.capture_mode, "start: lua_loader_on = LOG-ONLY capture mode");
    s.lua_substitute = true;
    d = decide_start(s);
    check(d.substitute && !d.capture_mode, "start: dev substitute");
    s = StartSwitches();
    s.faces_on = true;
    d = decide_start(s);
    check(d.opted_in && d.substitute && !d.capture_mode, "start: faces3d_on = substitute");
    s.lua_substitute = true;  // without lua_on: no effect on its own
    s.faces_on = false;
    check(!decide_start(s).opted_in, "start: substitute file alone is not an opt-in");
    s = StartSwitches();
    s.faces_on = true;
    s.legacy_present = true;
    d = decide_start(s);
    check(d.opted_in && !d.refused && !d.substitute && !d.capture_mode && d.legacy_refused && has(d.why, "player_kit.lua is present"),
          "start: legacy player_kit.lua refuses substitution and caching: " + d.why);
    for (int k = 0; k < 3; ++k) {
        s = StartSwitches();
        s.faces_on = true;
        s.lua_on = true;
        (k == 0 ? s.faces_off : k == 1 ? s.hook_off : s.game_hooks_off) = true;
        d = decide_start(s);
        check(d.refused && !d.substitute && !d.why.empty(), "start: kill switch " + std::to_string(k) + " refuses: " + d.why);
    }

    // atomic writes (a temp folder of the test)
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / ("turbo_ll_test_" + std::to_string(::time(nullptr)));
    fs::remove_all(dir, ec);
    std::string err;
    check(set_faces3d_opt_in(dir.u8string(), true, &err) && fs::exists(dir / "faces3d_on.txt") && !fs::exists(dir / "faces3d_on.txt.tmp"),
          "faces3d_on.txt written atomically (" + err + ")");
    check(set_faces3d_opt_in(dir.u8string(), true, &err), "faces3d_on.txt rewritten over itself");
    check(set_faces3d_opt_in(dir.u8string(), false, &err) && !fs::exists(dir / "faces3d_on.txt"), "faces3d_on.txt removed");
    check(set_faces3d_opt_in(dir.u8string(), false, &err), "removing an absent faces3d_on.txt is ok");
    const fs::path cache = dir / "faces3d" / "stock_player_kit.cache";
    check(write_file_atomic(cache.u8string(), "AAA", &err) && write_file_atomic(cache.u8string(), "BBBB", &err), "cache written twice");
    std::ifstream f(cache, std::ios::binary);
    std::string got((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    check(got == "BBBB" && !fs::exists(fs::path(cache.u8string() + ".tmp")), "cache replaced, no tmp left");
    fs::remove_all(dir, ec);
    check(std::string(kStockCacheRel) == "faces3d\\stock_player_kit.cache" &&
              std::string(kLegacyKitRel) == "mods\\legacy\\data\\fifarna\\lua\\assets\\player_kit.lua",
          "cache / legacy paths");
    std::vector<uint8_t> eb;
    std::vector<bool> em;
    check(parse_pattern(kOtherReaderExpect, eb, em) && eb.size() == 35 && std::all_of(em.begin(), em.end(), [](bool b) { return b; }),
          "other reader pattern: 35 exact bytes");
}

static void test_capture() {
    const std::string pk = "data/fifarna/lua/assets/player_kit.lua";
    const std::string stock = "-- stock kit (captured by Turbo)\nreturn 1\n";
    const std::string sm = md5s(stock);
    const std::string build = "6AC07E31-2145C000";
    const std::string over = make_override_header(build, sm, "tbl1") + "\r\n-- marker override\nreturn 2\n";
    const std::string bad = make_override_header(build, sm, "tbl1") + "\nERR this does not compile";
    char cap_txt[160];
    std::snprintf(cap_txt, sizeof(cap_txt), "captured stock player_kit: %zu bytes md5 %s", stock.size(), sm.c_str());

    // ---- LOG-ONLY capture mode: drained once by Turbo, the parser gets the stock bytes, published once
    ChunkOpts lo;
    lo.substitute = false;
    lo.cap = CaptureConfig{true, true, false};
    g_eng = make_chunk_engine(lo);
    g_lines.clear();
    {
        OneCall c;
        run_other(c, stock, pk);
        const std::string l = line0();
        check(c.rc == 0 && c.L.out == stock && c.ob.calls == 1 && c.od.done && c.L.loads == 1 && c.L.depth == 48, "log-only capture: stock compiled, reader drained once");
        check(has(l, cap_txt) && has(l, "served captured stock") && !has(l, "SUBSTITUTED") && has(l, "md5=" + sm), "log-only capture line: " + l);
        check(g_saved.count == 1 && g_saved.bytes == stock && g_saved.md5 == sm && g_saved.row == "player_kit", "capture published once (capture mode)");
    }
    {
        OneCall c;
        run_other(c, stock, pk);
        check(c.L.out == stock && g_saved.count == 1, "second capture: not published again");
        Faces3dStatus st;
        g_eng->fill_status(st);
        check(st.captures == 2 && st.served_stock == 2 && st.substituted == 0 && st.captured_md5 == sm && st.captured_size == stock.size() &&
                  st.capture_mismatch == 0 && !st.substitute,
              "status after two log-only captures");
    }
    // another chunkname on the same reader: untouched by Turbo (the parser calls the game's reader)
    g_lines.clear();
    {
        OneCall c;
        run_other(c, stock, "data/fifarna/lua/assets/pitch.lua");
        check(c.L.out == stock && c.ob.calls == 1 && !has(line0(), "captured") && g_eng->stats().captures == 2, "other chunkname not captured");
    }
    // killed: pure pass-through
    g_eng->set_killed(true);
    {
        OneCall c;
        run_other(c, stock, pk);
        check(c.L.out == stock && g_eng->stats().captures == 2, "killed: no capture");
    }
    g_eng->set_killed(false);

    // ---- the reader's bytes are not verified: never drained, the original as before
    g_verify = false;
    g_eng = make_chunk_engine(lo);
    g_lines.clear();
    {
        OneCall c;
        run_other(c, stock, pk);
        check(c.L.out == stock && c.ob.calls == 1 && has(line0(), "not the verified one-shot reader") && g_eng->stats().capture_refused == 1 &&
                  g_eng->stats().captures == 0 && g_saved.count == 0,
              "unverified reader: original, no capture: " + line0());
    }
    g_verify = true;
    // capture disabled in the config: the old pass-through
    {
        ChunkOpts o = lo;
        o.cap.enabled = false;
        g_eng = make_chunk_engine(o);
        OneCall c;
        run_other(c, stock, pk);
        check(c.L.out == stock && g_eng->stats().captures == 0, "capture disabled: pass-through");
    }

    // ---- multi-chunk reader (generic protocol): pieces concatenated, md5 over all, the parser gets the same bytes
    for (int zero_end = 0; zero_end < 2; ++zero_end) {
        g_eng = make_chunk_engine(lo);
        g_lines.clear();
        FakeL L;
        MultiData m;
        m.parts = {"-- part one\n", "local a = 1\n", "return a\n"};
        m.zero_size_end = zero_end != 0;
        const int rc = hooked_lua_load_L(&L, &multi_reader, &m, pk.c_str(), nullptr);
        const std::string all = m.parts[0] + m.parts[1] + m.parts[2];
        check(rc == 0 && L.out == all && m.calls == 4 && has(line0(), "md5=" + md5s(all)) && g_saved.bytes == all,
              std::string("multi-chunk reader, end by ") + (zero_end ? "size 0" : "NULL") + ": " + line0());
    }
    // more pieces than the table holds: replayed, then the game's reader for the rest; nothing measured
    {
        g_eng = make_chunk_engine(lo);
        g_lines.clear();
        FakeL L;
        MultiData m;
        std::string all;
        for (int i = 0; i < 100; ++i) {
            m.parts.push_back("-- " + std::to_string(i) + "\n");
            all += m.parts.back();
        }
        const int rc = hooked_lua_load_L(&L, &multi_reader, &m, pk.c_str(), nullptr);
        check(rc == 0 && L.out == all && has(line0(), "more than 64 pieces") && g_eng->stats().captures == 0 && g_saved.count == 0,
              "overflow: every byte served, not measured: " + line0().substr(0, 200));
    }
    // a drained piece the host says is unreadable: replayed as returned, not measured
    {
        g_eng = make_chunk_engine(lo);
        g_lines.clear();
        OneCall c;
        c.ob.bytes = stock;
        g_unreadable = c.ob.bytes.data();
        c.od = OtherData{&c.ob, false};
        c.rc = hooked_lua_load_L(&c.L, &game_other_reader, &c.od, pk.c_str(), nullptr);
        g_unreadable = nullptr;
        check(c.rc == 0 && c.L.out == stock && c.ob.calls == 1 && has(line0(), "unreadable") && g_eng->stats().captures == 0, "unreadable drained source: replayed");
    }
    // an exception after the drain (here: the publish callback throws): the drained stock is still what the parser gets
    {
        g_save_throws = true;
        g_eng = make_chunk_engine(lo);
        g_lines.clear();
        OneCall c;
        run_other(c, stock, pk);
        g_save_throws = false;
        check(c.rc == 0 && c.L.out == stock && c.ob.calls == 1 && g_eng->stats().errors == 1 && g_eng->stats().served_stock == 1,
              "exception after the drain: stock replayed");
    }

    // ---- SUBSTITUTE, header names the running build and the captured md5: the override
    ChunkOpts so;
    so.over = over;
    g_eng = make_chunk_engine(so);
    g_lines.clear();
    {
        OneCall c;
        run_other(c, stock, pk);
        char want[200];
        std::snprintf(want, sizeof(want), "SUBSTITUTED row=player_kit override bytes %zu md5 %s (chunkname match, header matches the captured stock)", over.size(),
                      md5s(over).c_str());
        const std::string l = line0();
        check(c.rc == 0 && c.L.out == over && c.ob.calls == 1 && c.L.loads == 1 && c.L.depth == 48 && c.L.pops == 0, "substitute: override compiled");
        check(has(l, want) && has(l, cap_txt) && !has(l, "served captured stock"), "substitute line: " + l);
        check(g_saved.count == 0, "substitute mode: md5 differs from the row's expected stock: not published");
        Faces3dStatus st;
        g_eng->fill_status(st);
        check(st.substitute && st.header_ok && st.build_key_ok && st.stock_matches_header && st.substituted == 1 && st.captures == 1 && st.last_error.empty(),
              "status: substituted, header ok");
    }
    {
        OneCall c;
        run_other(c, stock, "@" + pk);
        check(c.L.out == over, "substitute: leading @ tolerated");
    }
    // substitute mode publishes when the md5 is the row's expected stock md5
    {
        ChunkOpts o = so;
        o.row_md5 = sm;
        g_eng = make_chunk_engine(o);
        OneCall c;
        run_other(c, stock, pk);
        check(c.L.out == over && g_saved.count == 1 && g_saved.md5 == sm, "substitute mode: expected md5 -> published");
    }

    // ---- every refusal after the drain serves the CAPTURED STOCK (the game's reader is consumed): out == stock, reader called once
    struct Case {
        std::string name;
        ChunkOpts o;
        std::string mode;
        bool depth_unreadable;
        std::string note;
    };
    std::vector<Case> cases;
    {
        Case k{"stale header build", so, "", false, "stale override: header build 6AB9813C-211EF000"};
        k.o.over = make_override_header("6AB9813C-211EF000", sm, "t") + "\nreturn 3\n";
        cases.push_back(k);
    }
    {
        Case k{"stale header stock", so, "", false, "stale override: made for stock 00000000000000000000000000000000"};
        k.o.over = make_override_header(build, "00000000000000000000000000000000", "t") + "\nreturn 3\n";
        cases.push_back(k);
    }
    {
        Case k{"missing header", so, "", false, "no TURBO header"};
        k.o.over = "-- marker override without a header\nreturn 2\n";
        cases.push_back(k);
    }
    {
        Case k{"running build unknown", so, "", false, "running unknown"};
        k.o.running = "";
        cases.push_back(k);
    }
    {
        Case k{"override not loaded", so, "", false, "override not loaded"};
        k.o.over = "";
        cases.push_back(k);
    }
    cases.push_back(Case{"mode b", so, "b", false, "mode refuses text"});
    {
        Case k{"no safe pop", so, "", false, "stack layout is not confirmed"};
        k.o.with_fallback = false;
        cases.push_back(k);
    }
    cases.push_back(Case{"stack unreadable", so, "", true, "Lua stack is unreadable"});
    {
        Case k{"legacy file present", so, "", false, ""};
        k.o.cap.legacy_present = true;
        k.o.row_md5 = sm;
        cases.push_back(k);
    }
    for (const auto& k : cases) {
        g_eng = make_chunk_engine(k.o);
        g_lines.clear();
        OneCall c;
        c.L.depth_unreadable = k.depth_unreadable;
        run_other(c, stock, pk, k.mode.empty() ? nullptr : k.mode.c_str());
        const std::string l = line0();
        check(c.rc == 0 && c.L.out == stock && c.ob.calls == 1 && c.L.loads == 1 && has(l, "served captured stock") && !has(l, "SUBSTITUTED") &&
                  has(l, k.note) && g_eng->stats().substituted == 0,
              "refusal after drain -> captured stock (" + k.name + "): " + l);
        Faces3dStatus st;
        g_eng->fill_status(st);
        if (k.name == "legacy file present")
            check(st.legacy_refused && !st.substitute && g_saved.count == 0, "legacy: never substituted, never published");
        else
            check(!st.last_error.empty() && has(st.last_error, k.note), "status last_error (" + k.name + "): " + st.last_error);
        if (k.name == "stale header build") check(!st.build_key_ok && st.header_ok, "status: build key not ok");
        if (k.name == "stale header stock") check(st.build_key_ok && !st.stock_matches_header, "status: stale stock");
    }

    // ---- the override fails to compile: one pop, the CAPTURED STOCK replayed (the game's reader is not called again)
    ChunkOpts sb = so;
    sb.over = bad;
    g_eng = make_chunk_engine(sb);
    g_lines.clear();
    {
        OneCall c;
        run_other(c, stock, pk);
        const std::string l = line0();
        check(c.rc == 0 && c.L.out == stock && c.ob.calls == 1 && c.L.loads == 2 && c.L.pops == 1 && c.L.depth == 48,
              "compile failure: captured stock replayed, game reader drained once, net one slot");
        check(has(l, "fallback to captured stock (override failed: rc=3") && has(l, cap_txt) && !has(l, "SUBSTITUTED"), "fallback line: " + l);
        Faces3dStatus st;
        g_eng->fill_status(st);
        check(st.fallbacks == 1 && st.substituted == 0 && has(st.last_error, "failed to compile"), "fallback in status: " + st.last_error);
    }
    g_lines.clear();
    {
        OneCall c;
        run_other(c, stock, pk);
        check(c.rc == 0 && c.L.out == stock && c.L.loads == 1 && c.L.pops == 0 && has(line0(), "failed to compile earlier"), "latch: captured stock directly");
    }
    // the stock itself fails too: its rc is what the game gets
    g_eng = make_chunk_engine(sb);
    {
        OneCall c;
        const std::string st = "ERR stock broken";
        ChunkOpts o = sb;
        o.over = make_override_header(build, md5s(st), "t") + "\nERR x";
        g_eng = make_chunk_engine(o);
        run_other(c, st, pk);
        check(c.rc == 3 && c.L.out == st && c.L.pops == 1 && c.L.depth == 48 && c.ob.calls == 1, "fallback: the stock's own rc returned");
    }
    // the stack grew by more than one slot: no pop, no retry
    g_eng = make_chunk_engine(sb);
    g_lines.clear();
    {
        OneCall c;
        c.L.extra_push = true;
        run_other(c, stock, pk);
        check(c.rc == 3 && c.L.pops == 0 && c.L.loads == 1 && has(line0(), "no retry"), "unexpected stack -> no retry: " + line0());
    }

    // ---- 18 threads x 50 (Launch L1: 18 compiles on 18 threads at boot): every thread captures on its own, one publish
    for (int mode = 0; mode < 2; ++mode) {
        ChunkOpts o = mode == 0 ? lo : so;
        g_eng = make_chunk_engine(o);
        g_lines.clear();
        const int N = 18, K = 50;
        std::vector<FakeL> Ls(N);
        std::vector<OtherObj> obs(N);
        for (int i = 0; i < N; ++i) obs[i].bytes = stock;
        std::atomic<int> go{0};
        std::vector<int> bad_calls(N, 0);
        std::vector<std::thread> th;
        for (int i = 0; i < N; ++i)
            th.emplace_back([&, i]() {
                while (!go.load()) std::this_thread::yield();
                for (int k = 0; k < K; ++k) {
                    OtherData od{&obs[i], false};
                    const int rc = hooked_lua_load_L(&Ls[i], &game_other_reader, &od, pk.c_str(), nullptr);
                    if (rc != 0 || Ls[i].out != (mode == 0 ? stock : over) || !od.done) ++bad_calls[i];
                }
            });
        go = 1;
        for (auto& t : th) t.join();
        bool all = true;
        for (int i = 0; i < N; ++i)
            if (bad_calls[i] || obs[i].calls != K || Ls[i].depth != 32 + K * 16) all = false;
        const auto s = g_eng->stats();
        Faces3dStatus st;
        g_eng->fill_status(st);
        const std::string tag = mode == 0 ? "log-only" : "substitute";
        check(all, "18 threads x 50 (" + tag + "): every call got the whole source, each reader drained once per call");
        check(s.captures == N * K && s.capture_mismatch == 0 && st.captured_md5 == sm && s.errors == 0 && g_lines.size() == size_t(N * K),
              "18 threads (" + tag + "): counts, one md5");
        check(g_saved.count == (mode == 0 ? 1 : 0) && s.published == (mode == 0 ? 1 : 0), "18 threads (" + tag + "): published exactly " + std::to_string(g_saved.count));
        check(mode == 0 ? (s.served_stock == N * K && s.substituted == 0) : (s.substituted == N * K && s.served_stock == 0), "18 threads (" + tag + "): served counts");
    }
    // 18 threads, the override fails: everyone ends with the captured stock
    {
        g_eng = make_chunk_engine(sb);
        g_lines.clear();
        const int N = 18;
        std::vector<OneCall> cs(N);
        std::atomic<int> go{0};
        std::vector<std::thread> th;
        for (int i = 0; i < N; ++i)
            th.emplace_back([&, i]() {
                while (!go.load()) std::this_thread::yield();
                run_other(cs[i], stock, pk);
            });
        go = 1;
        for (auto& t : th) t.join();
        bool all = true;
        for (int i = 0; i < N; ++i)
            if (cs[i].rc != 0 || cs[i].L.out != stock || cs[i].ob.calls != 1 || cs[i].L.depth != 48 || cs[i].L.pops > 1) all = false;
        const auto st = g_eng->stats();
        check(all && st.fallbacks >= 1 && st.fallbacks <= N && st.substituted == 0 && g_lines.size() == size_t(N),
              "18 threads, failing override: all captured stock, fallbacks " + std::to_string(st.fallbacks));
    }
}

int main() {
    test_md5();
    test_signatures();
    test_rows_and_format();
    test_engine();
    test_header_and_switches();
    test_capture();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
