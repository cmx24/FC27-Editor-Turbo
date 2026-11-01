// FC 27 LE Turbo GUI - Lua loader hook, platform-independent part (see lua_loader.h)
#include "core/lua_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/devops.h"

namespace turbo {
namespace lualoader {

// ---------------------------------------------------------------- md5 (RFC 1321)
namespace {

struct Md5 {
    uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
    uint64_t len = 0;
    uint8_t buf[64];
    size_t used = 0;

    static uint32_t rol(uint32_t x, int s) { return (x << s) | (x >> (32 - s)); }
    void block(const uint8_t* p) {
        static const uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
            0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
            0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
            0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
            0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
            0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
        uint32_t m[16];
        for (int i = 0; i < 16; ++i)
            m[i] = uint32_t(p[i * 4]) | (uint32_t(p[i * 4 + 1]) << 8) | (uint32_t(p[i * 4 + 2]) << 16) | (uint32_t(p[i * 4 + 3]) << 24);
        uint32_t A = a, B = b, C = c, D = d;
        for (int i = 0; i < 64; ++i) {
            uint32_t F;
            int g;
            if (i < 16) {
                F = (B & C) | (~B & D);
                g = i;
            } else if (i < 32) {
                F = (D & B) | (~D & C);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                F = B ^ C ^ D;
                g = (3 * i + 5) % 16;
            } else {
                F = C ^ (B | ~D);
                g = (7 * i) % 16;
            }
            const uint32_t t = D;
            D = C;
            C = B;
            B = B + rol(A + F + K[i] + m[g], S[i]);
            A = t;
        }
        a += A;
        b += B;
        c += C;
        d += D;
    }
    void update(const uint8_t* p, size_t n) {
        len += n;
        while (n > 0) {
            if (used == 0 && n >= 64) {
                block(p);
                p += 64;
                n -= 64;
                continue;
            }
            size_t k = 64 - used < n ? 64 - used : n;
            std::memcpy(buf + used, p, k);
            used += k;
            p += k;
            n -= k;
            if (used == 64) {
                block(buf);
                used = 0;
            }
        }
    }
    std::string hex() {
        const uint64_t bits = len * 8;
        const uint8_t pad = 0x80;
        update(&pad, 1);
        const uint8_t zero = 0;
        while (used != 56) update(&zero, 1);
        uint8_t l[8];
        for (int i = 0; i < 8; ++i) l[i] = uint8_t(bits >> (8 * i));
        update(l, 8);
        char out[33];
        const uint32_t v[4] = {a, b, c, d};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) std::snprintf(out + i * 8 + j * 2, 3, "%02x", unsigned((v[i] >> (8 * j)) & 0xFF));
        return std::string(out, 32);
    }
};

}  // namespace

std::string md5_hex(const uint8_t* p, size_t n) {
    Md5 m;
    if (n) m.update(p, n);
    return m.hex();
}

// ---------------------------------------------------------------- signatures
const char* const kSigLuaLoad = "lua_load";
const char* const kSigLuaLoadXcheck = "lua_load_xcheck";
const char* const kSigLoadStringReader = "lua_getS";
const char* const kSigBlobReader = "lua_game_blob_reader";
const char* const kGetSExpect = "83 7A 08 00 75 03 33 C0 C3 8B 42 08 49 89 00 48 8B 02";
const uint8_t kLuaLoadPrologue[8] = {0x48, 0x8B, 0xC4, 0x53, 0x48, 0x83, 0xEC, 0x50};
const char* const kLuaLoadTopCheck = "44 8B D8 85 C0 75 ?? 48 8B 4B 10 4C 8B 51 F0";
const size_t kLuaLoadTopCheckOffset = 0x45;
const char* const kChunknameBuild = "6AC07E31-2145C000";

namespace {
const char* const kLuaLoadPattern =
    "48 8B C4 53 48 83 EC 50 4C 89 40 E0 4D 85 C9 48 89 50 D8 4C 8D 05 ?? ?? ?? ?? 4D 0F 45 C1 48 89 48 E8 4C 8B 8C 24 80 00 00 00 "
    "48 8D 50 C8 48 8B D9 C7 40 C8 00 00 00 00 48 C7 40 D0 00 00 00 00 E8";
// luaL_loadstring: strlen loop, then luaL_loadbufferx inlined: lea rdx,[getS] at +29, call lua_load at +50
const char* const kLoadStringPattern =
    "48 83 EC 48 48 89 54 24 30 48 83 C8 FF 48 FF C0 80 3C 02 00 75 F7 4C 8B CA 89 44 24 38 48 8D 15 ?? ?? ?? ?? 48 C7 44 24 20 00 00 "
    "00 00 4C 8D 44 24 30 E8 ?? ?? ?? ??";
const char* const kBlobReaderPattern =
    "48 89 5C 24 08 57 48 83 EC 20 80 7A 08 00 49 8B F8 48 8B DA 75 1C 48 8B 0A C6 42 08 01 48 8B 01 FF 50 08 8B C0 48 89 07 48 8B 0B "
    "48 8B 01 FF 10";
}  // namespace

std::vector<Signature> signatures() {
    return {
        {kSigLuaLoad, kLuaLoadPattern, "none", 0, "Lua 5.4 lua_load (6AB9813C: 0x14081F5C0)"},
        {kSigLuaLoadXcheck, kLoadStringPattern, "rip", 50, "luaL_loadstring's call of lua_load (cross-check)", "E8"},
        {kSigLoadStringReader, kLoadStringPattern, "rip", 29, "luaL_loadstring's reader getS (6AB9813C: 0x1446A982C)", "48 8D 15"},
        {kSigBlobReader, kBlobReaderPattern, "none", 0, "the game's loadfile blob reader (6AB9813C: 0x1489E182C)"},
    };
}

bool prologue_is_jump(const uint8_t* p, size_t n) {
    if (!p || n < 1) return true;
    if (p[0] == 0xE9 || p[0] == 0xEB || p[0] == 0xCC) return true;
    if (n >= 2 && p[0] == 0xFF && p[1] == 0x25) return true;
    if (n >= 2 && (p[0] == 0x48 || p[0] == 0x49) && p[1] == 0xB8) return true;  // movabs rax/r8, imm64 (then jmp/push+ret)
    if (n >= 6 && p[0] == 0x68 && p[5] == 0xC3) return true;                      // push imm32 / ret
    return false;
}

Located decide_install(const std::vector<SigResult>& results, const std::function<bool(uint64_t, void*, size_t)>& read) {
    Located L;
    auto get = [&](const char* name) -> const SigResult* {
        for (const auto& r : results)
            if (r.name == name) return &r;
        return nullptr;
    };
    const SigResult* ll = get(kSigLuaLoad);
    const SigResult* xc = get(kSigLuaLoadXcheck);
    if (!ll || ll->state != SigState::Found) {
        L.why = std::string("lua_load ") + (ll ? sig_state_name(ll->state) : "not scanned");
        return L;
    }
    if (!xc || xc->state != SigState::Found) {
        L.why = std::string("cross-check lua_load_xcheck ") + (xc ? sig_state_name(xc->state) : "not scanned");
        return L;
    }
    if (xc->address != ll->address) {
        char b[160];
        std::snprintf(b, sizeof(b), "cross-check disagrees: lua_load 0x%llX, luaL_loadstring calls 0x%llX",
                      static_cast<unsigned long long>(ll->address), static_cast<unsigned long long>(xc->address));
        L.why = b;
        return L;
    }
    uint8_t pro[16] = {0};
    if (!read || !read(ll->address, pro, sizeof(pro))) {
        L.why = "lua_load's first bytes are not readable";
        return L;
    }
    if (prologue_is_jump(pro, sizeof(pro))) {
        L.why = "lua_load already starts with a jump (another module's hook): refused";
        return L;
    }
    if (std::memcmp(pro, kLuaLoadPrologue, sizeof(kLuaLoadPrologue)) != 0) {
        L.why = "lua_load's first bytes changed since the scan: refused";
        return L;
    }
    L.ok = true;
    L.lua_load = ll->address;
    std::string notes;
    {
        std::vector<uint8_t> tb;
        std::vector<bool> tm;
        uint8_t code[32] = {0};
        if (parse_pattern(kLuaLoadTopCheck, tb, tm) && tb.size() <= sizeof(code) && read(ll->address + kLuaLoadTopCheckOffset, code, tb.size())) {
            bool same = true;
            for (size_t i = 0; i < tb.size(); ++i)
                if (tm[i] && code[i] != tb[i]) same = false;
            L.top_layout_ok = same;
        }
        if (!L.top_layout_ok) notes += "lua_load stack layout not confirmed (no chunkname substitution); ";
    }
    if (const SigResult* gs = get(kSigLoadStringReader)) {
        std::vector<uint8_t> eb;
        std::vector<bool> em;
        uint8_t code[32] = {0};
        if (gs->state == SigState::Found && parse_pattern(kGetSExpect, eb, em) && eb.size() <= sizeof(code) && read(gs->address, code, eb.size())) {
            bool same = true;
            for (size_t i = 0; i < eb.size(); ++i)
                if (em[i] && code[i] != eb[i]) same = false;
            if (same) L.gets = gs->address;
            else notes += "getS bytes differ (loadstring sources not measured); ";
        } else {
            notes += "getS not found (loadstring sources not measured); ";
        }
    }
    if (const SigResult* br = get(kSigBlobReader)) {
        uint8_t b[16] = {0};
        if (br->state == SigState::Found && read(br->address, b, sizeof(b)) && !prologue_is_jump(b, sizeof(b))) L.blob_reader = br->address;
        else notes += "blob reader not found yet or already hooked (only file sources by size/md5 are affected; player_kit.lua comes "
                      "through the verified one-shot reader by chunkname and is unaffected); ";
    } else {
        notes += "blob reader not scanned; ";
    }
    if (!notes.empty()) notes.resize(notes.size() - 2);
    L.why = notes;
    return L;
}

// ---------------------------------------------------------------- rows
std::vector<Row> default_rows() {
    // fifarna player_kit.lua as the game ships it (stock extract, turbo_dev\evidence\2026-10-15\bisect\stock_copy.lua)
    // Launch L1 (6AC07E31, 2026-10-15): the game compiles it through reader FC27.exe+0x46AE738 ("other") with this chunkname,
    // so the row also matches by chunkname on that build.
    return {{"player_kit", 0x41140, "9399e39a1e46a15d151ada423759abe9", "lua_override\\player_kit.lua", "data/fifarna/lua/assets/player_kit.lua",
             kChunknameBuild}};
}

int match_row(const std::vector<Row>& rows, uint64_t size, const std::string& md5) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].size != 0 && !rows[i].md5.empty() && rows[i].size == size && rows[i].md5 == md5) return static_cast<int>(i);
    return -1;
}

bool chunkname_equal(const std::string& want, const char* got, size_t n) {
    if (want.empty() || !got) return false;
    const char* w = want.data();
    size_t wn = want.size();
    if (wn && w[0] == '@') ++w, --wn;
    if (n && got[0] == '@') ++got, --n;
    return wn != 0 && wn == n && std::memcmp(w, got, n) == 0;
}

int match_chunkname_row(const std::vector<Row>& rows, const char* got, size_t n) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (chunkname_equal(rows[i].chunkname, got, n)) return static_cast<int>(i);
    return -1;
}

std::string override_refusal(const std::string& bytes) {
    if (bytes.empty()) return "empty file";
    if (static_cast<unsigned char>(bytes[0]) == 0x1B) return "binary (precompiled) chunk: only source text is served";
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF)
        return "starts with a UTF-8 BOM (lua_load does not skip it): save without BOM";
    const size_t n = bytes.size() < 4096 ? bytes.size() : 4096;
    if (std::memchr(bytes.data(), 0, n)) return "contains a NUL byte (not a text file)";
    return "";
}

// ---------------------------------------------------------------- faces3d
const char* const kOtherReaderExpect =
    "80 7A 08 00 74 0A 49 C7 00 00 00 00 00 33 C0 C3 48 8B 02 C6 42 08 01 48 8B 48 20 49 89 08 48 8B 40 18 C3";
const char* const kStockCacheRel = "faces3d\\stock_player_kit.cache";
const char* const kLegacyKitRel = "mods\\legacy\\data\\fifarna\\lua\\assets\\player_kit.lua";
const char* const kLiveLooksMarker = "TURBO_GATE.band = function";
const char* const kResearchFileSwitch = "lua_loader_research_file.txt";
const char* const kLogOnlySwitch = "lua_loader_log_only.txt";

bool has_live_looks(const char* data, size_t size) {
    if (!data || !size) return false;
    const size_t m = std::strlen(kLiveLooksMarker);
    return size >= m && std::search(data, data + size, kLiveLooksMarker, kLiveLooksMarker + m) != data + size;
}

namespace {
bool is_md5_hex(const std::string& s) {
    if (s.size() != 32) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
}  // namespace

OverrideHeader parse_override_header(const std::string& bytes) {
    OverrideHeader h;
    size_t e = bytes.find('\n');
    std::string line = bytes.substr(0, e == std::string::npos ? std::min<size_t>(bytes.size(), 512) : std::min<size_t>(e, 512));
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> t;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        size_t j = i;
        while (j < line.size() && line[j] != ' ' && line[j] != '\t') ++j;
        if (j > i) t.push_back(line.substr(i, j - i));
        i = j;
    }
    if (t.size() < 2 || t[0] != "--" || t[1] != "TURBO") {
        h.error = "no TURBO header on the first line (want `-- TURBO build <key> stock <md5> table <hash>`)";
        return h;
    }
    if (t.size() != 8 || t[2] != "build" || t[4] != "stock" || t[6] != "table") {
        h.error = "malformed TURBO header: " + line.substr(0, 160);
        return h;
    }
    h.build = t[3];
    h.stock = lower(t[5]);
    h.table = t[7];
    if (!is_md5_hex(h.stock)) {
        h.error = "TURBO header stock is not an md5: " + t[5];
        return h;
    }
    h.ok = true;
    return h;
}

std::string make_override_header(const std::string& build, const std::string& stock_md5, const std::string& table) {
    return "-- TURBO build " + build + " stock " + lower(stock_md5) + " table " + table;
}

StartDecision decide_start(const StartSwitches& s) {
    StartDecision d;
    d.opted_in = true;  // 2.16: on whenever Turbo is installed (faces3d_on.txt no longer matters)
    if (s.game_hooks_off) d.why = "turbo_output\\game_hooks_off.txt present";
    else if (s.hook_off) d.why = "kill switch turbo_output\\hook_lua_load_off.txt present";
    else if (s.faces_off) d.why = "kill switch turbo_output\\faces3d_off.txt present";
    if (!d.why.empty()) {
        d.refused = true;
        return d;
    }
    const bool research = s.research_file && !s.log_only;  // the override file (test lane), as 2.8-2.15
    const bool want_sub = !s.log_only;
    if (want_sub && s.legacy_present) {
        d.legacy_refused = true;
        d.why = std::string(kLegacyKitRel) +
                " is present: substitution refused (LE would serve that file; a longer player_kit.lua crashes match load). Remove it; the hook "
                "runs LOG-ONLY and caches nothing.";
    }
    d.substitute = want_sub && !s.legacy_present;
    d.in_memory = d.substitute && !research;
    d.capture_mode = !d.substitute && !s.legacy_present;
    return d;
}

bool write_file_atomic(const std::string& path_utf8, const std::string& bytes, std::string* err) {
    namespace fs = std::filesystem;
    const fs::path p = fs::u8path(path_utf8);
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.u8string();
            return false;
        }
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        f.flush();
        if (!f) {
            if (err) *err = "writing " + tmp.u8string() + " failed";
            f.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, p, ec);  // libstdc++ on Windows: MoveFileExW(MOVEFILE_REPLACE_EXISTING)
    if (ec) {
        std::error_code ec2;
        fs::remove(p, ec2);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec2);
            if (err) *err = "rename to " + p.u8string() + " failed: " + ec.message();
            return false;
        }
    }
    return true;
}

bool set_faces3d_opt_in(const std::string& turbo_output_utf8, bool on, std::string* err) {
    namespace fs = std::filesystem;
    const fs::path p = fs::u8path(turbo_output_utf8) / "faces3d_on.txt";
    if (on) return write_file_atomic(p.u8string(), "faces3d opt-in (Turbo GUI): the Lua loader hook serves turbo_output\\lua_override\\player_kit.lua\r\n", err);
    std::error_code ec;
    fs::remove(p, ec);
    if (ec && fs::exists(p)) {
        if (err) *err = "cannot remove " + p.u8string() + ": " + ec.message();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- engine
const char* reader_kind_name(ReaderKind k) {
    switch (k) {
        case ReaderKind::Blob: return "blob";
        case ReaderKind::LoadString: return "loadstring";
        default: return "other";
    }
}

bool mode_allows_text(const char* mode) { return mode == nullptr || std::strchr(mode, 't') != nullptr; }

std::string quote_chunkname(const char* s, size_t n, size_t max_len) {
    std::string out = "\"";
    size_t i = 0;
    for (; i < n && i < max_len; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c >= 0x20 && c < 0x7F) {
            out += char(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            char b[8];
            std::snprintf(b, sizeof(b), "\\x%02X", c);
            out += b;
        }
    }
    out += '"';
    if (i < n) out += "...";
    return out;
}

std::string format_line(const CallRecord& r) {
    char head[256];
    std::snprintf(head, sizeof(head), "lua_loader: #%llu t=%s tid=%lu caller=%s reader=%s(%s) chunk=", static_cast<unsigned long long>(r.seq),
                  r.time.c_str(), static_cast<unsigned long>(r.thread), r.caller.c_str(), r.reader.c_str(), reader_kind_name(r.kind));
    std::string s = head;
    s += r.chunk;
    s += " mode=" + r.mode;
    if (r.have_source) {
        char b[96];
        std::snprintf(b, sizeof(b), " size=0x%llX(%llu) md5=", static_cast<unsigned long long>(r.size), static_cast<unsigned long long>(r.size));
        s += b;
        s += r.md5;
    } else {
        s += " size=-";
    }
    if (r.captured) {
        char b[128];
        std::snprintf(b, sizeof(b), " captured stock %s: %llu bytes md5 ", r.row.c_str(), static_cast<unsigned long long>(r.size));
        s += b;
        s += r.md5;
    }
    if (!r.note.empty()) s += " note=\"" + r.note + "\"";
    if (r.served_stock) s += " served captured stock";
    if (r.substituted && r.by_chunkname) {
        char b[128];
        std::snprintf(b, sizeof(b), " SUBSTITUTED row=%s override bytes %llu md5 ", r.row.c_str(), static_cast<unsigned long long>(r.new_size));
        s += b;
        s += r.new_md5 + (r.captured ? " (chunkname match, header matches the captured stock)" : " (stock unknown: chunkname match)");
    } else if (r.substituted) {
        char b[96];
        std::snprintf(b, sizeof(b), " SUBSTITUTED row=%s new_size=0x%llX(%llu) new_md5=", r.row.c_str(), static_cast<unsigned long long>(r.new_size),
                      static_cast<unsigned long long>(r.new_size));
        s += b;
        s += r.new_md5;
    }
    if (r.fallback)
        s += std::string(r.captured ? " fallback to captured stock" : " fallback to original") + " (override failed: rc=" + std::to_string(r.override_rc) +
             " " + r.override_msg + ")";
    s += " rc=" + std::to_string(r.rc);
    return s;
}

namespace {
// Turbo's own reader for a substituted loadstring source (one shot, like getS)
struct SubSource {
    const char* p;
    size_t n;
    bool done;
};
const char* sub_reader(void*, void* data, size_t* size) {
    auto* s = static_cast<SubSource*>(data);
    if (s->done) return nullptr;
    s->done = true;
    *size = s->n;
    return s->p;
}

// The captured stock source, replayed to lua_load after Turbo drained the game's reader (which is then consumed). Fixed-size piece
// table on the detour's stack (no allocation before the drain is complete); `copy` (when set) is Turbo's own contiguous copy, else
// the pieces are replayed exactly as the game's reader returned them; `tail` is the game's reader itself when more than kMax
// pieces came (the rest is read through it: no md5, no substitution, no rewind).
struct Replay {
    static const int kMax = 64;
    const char* p[kMax];
    size_t n[kMax];
    int count = 0;
    int idx = 0;
    Reader tail = nullptr;
    void* tail_data = nullptr;
    const char* copy = nullptr;
    size_t copy_n = 0;
    bool copy_done = false;
    void rewind() {
        idx = 0;
        copy_done = false;
    }
};
const char* replay_reader(void* L, void* data, size_t* size) {
    auto* r = static_cast<Replay*>(data);
    if (r->copy) {
        if (r->copy_done) {
            *size = 0;
            return nullptr;
        }
        r->copy_done = true;
        *size = r->copy_n;
        return r->copy;
    }
    if (r->idx < r->count) {
        *size = r->n[r->idx];
        return r->p[r->idx++];
    }
    if (r->tail) return r->tail(L, r->tail_data, size);
    *size = 0;
    return nullptr;
}
}  // namespace

struct Engine::Ctx {
    void* data = nullptr;
    CallRecord* rec = nullptr;
    bool text_ok = false;
    bool seen = false;
};

namespace {
thread_local int t_depth = 0;
thread_local void* t_ctx = nullptr;  // Engine::Ctx* of the lua_load call running on this thread
struct DepthGuard {
    DepthGuard() { ++t_depth; }
    ~DepthGuard() { --t_depth; }
};
}  // namespace

void Engine::configure(uint64_t gets, uint64_t blob_reader, bool substitute, std::vector<Row> rows, std::vector<Override> overrides,
                       std::string running_build, CaptureConfig capture) {
    gets_ = gets;
    blob_ = blob_reader;
    capture_ = capture;
    substitute_ = substitute && !capture.legacy_present;
    rows_ = std::move(rows);
    overrides_ = std::move(overrides);
    overrides_.resize(rows_.size());
    build_ = std::move(running_build);
    failed_.reset(new std::atomic<bool>[rows_.size() + 1]);
    published_.reset(new std::atomic<bool>[rows_.size() + 1]);
    for (size_t i = 0; i <= rows_.size(); ++i) {
        failed_[i].store(false);
        published_[i].store(false);
    }
    headers_.assign(rows_.size(), OverrideHeader());
    live_.assign(rows_.size(), false);
    built_.reset(new BuiltRow[rows_.size() + 1]);
    for (size_t i = 0; i < rows_.size(); ++i) {
        const Override& o = overrides_[i];
        if (o.data && o.size && o.error.empty()) {
            headers_[i] = parse_override_header(std::string(o.data, std::min<size_t>(o.size, 600)));
            live_[i] = has_live_looks(o.data, o.size);
        } else {
            headers_[i].error = o.error.empty() ? "override not loaded" : o.error;
        }
    }
}

void Engine::set_error(const std::string& e) {
    std::lock_guard<std::mutex> lk(st_m_);
    last_error_ = e;
}

void Engine::note_capture(uint64_t size, const std::string& md5) {
    std::lock_guard<std::mutex> lk(st_m_);
    if (captured_md5_.empty()) {
        captured_md5_ = md5;
        captured_size_ = size;
    } else if (captured_md5_ != md5 || captured_size_ != size) {
        ++capture_mismatch_;
        last_error_ = "two captures of the same row differ: " + captured_md5_ + " vs " + md5;
    }
}

void Engine::fill_status(Faces3dStatus& st) const {
    const Stats s = stats();
    st.captures = s.captures;
    st.served_stock = s.served_stock;
    st.substituted = s.substituted;
    st.fallbacks = s.fallbacks;
    st.capture_refused = s.capture_refused;
    st.capture_mismatch = s.capture_mismatch;
    st.running_build = build_;
    st.substitute = substitute_;
    st.legacy_refused = capture_.legacy_present;
    st.killed = killed();
    if (!headers_.empty()) {
        st.header_ok = headers_[0].ok;
        st.build_key_ok = headers_[0].ok && !build_.empty() && headers_[0].build == build_;
        st.header_stock = headers_[0].stock;
        st.header_table = headers_[0].table;
        st.live_looks = headers_[0].ok && !live_.empty() && live_[0];
    }
    st.in_memory = capture_.build_in_memory;
    if (st.in_memory && !rows_.empty()) {
        // product mode: the in-memory build stands for the file header (same gates: running build, captured stock)
        BuiltRow& b = built_[0];
        std::lock_guard<std::mutex> lk(b.m);
        st.built = b.bytes != nullptr;
        st.built_error = b.error;
        st.header_ok = st.built;
        st.build_key_ok = st.built && !build_.empty() && rows_[0].build == build_;
        st.header_stock = st.built ? rows_[0].md5 : std::string();
        st.header_table.clear();
        st.live_looks = st.built && has_live_looks(b.o.data, b.o.size);
    }
    std::lock_guard<std::mutex> lk(st_m_);
    st.captured_md5 = captured_md5_;
    st.captured_size = captured_size_;
    st.stock_matches_header = st.header_ok && !captured_md5_.empty() && captured_md5_ == st.header_stock;
    if (!last_error_.empty()) st.last_error = last_error_;
}

const Override* Engine::built_for(size_t row, const std::string& stock, const std::string& md5, std::string& why) {
    BuiltRow& b = built_[row];
    std::lock_guard<std::mutex> lk(b.m);
    if (!b.tried) {
        b.tried = true;
        std::string note;
        const Row& rw = rows_[row];
        if (md5 != rw.md5) {
            b.error = "the game's player_kit.lua (md5 " + md5 + ") is not the one Turbo knows (" + rw.md5 + "): game update?";
        } else if (build_.empty()) {
            // Turbo X: the build key only names where the chunkname was first seen (6AC07E31). The stock file itself is checked
            // above by md5 and every anchor line again by the generator, so a later build with the SAME player_kit.lua (6AD922D1:
            // md5 9399e39a, unchanged by the 10-27 update) gets the in-memory build; a changed file still serves stock.
            b.error = "game build unknown";
        } else if (!env_.build_override) {
            b.error = "no builder";
        } else {
            try {
                b.bytes = env_.build_override(rw.name, stock, note, b.error);
            } catch (...) {
                b.bytes.reset();
                b.error = "the build threw";
            }
            if (b.bytes && b.error.empty()) b.error = override_refusal(*b.bytes);
            if (!b.error.empty() || !b.bytes || b.bytes->empty()) {
                if (b.error.empty()) b.error = "the build returned nothing";
                b.bytes.reset();
            }
        }
        if (b.bytes) {
            b.o.data = b.bytes->data();
            b.o.size = b.bytes->size();
            b.o.md5 = md5_hex(reinterpret_cast<const uint8_t*>(b.o.data), b.o.size);
        }
        if (env_.emit) {
            char sz[32];
            std::snprintf(sz, sizeof(sz), "%llu", static_cast<unsigned long long>(b.o.size));
            // 5.0c E13: the label names the template the bytes carry (v5 = hair replace, its gate marker), so O1 reads it from the log
            const bool v5 = b.bytes && b.bytes->find("FACE GATE v5") != std::string::npos;
            env_.emit(b.bytes ? "lua_loader: " + rw.name + " served: " + (v5 ? "v5" : "v4") + " built in memory (stock md5 " + md5 + ", " + note + ", " + sz + " B, md5 " + b.o.md5 + ")"
                              : "lua_loader: " + rw.name + " served: stock (" + b.error + ")");
        }
        if (!b.bytes) set_error(b.error);
    }
    why = b.error;
    return b.bytes ? &b.o : nullptr;
}

Engine::Stats Engine::stats() const {
    Stats s;
    s.calls = calls_.load();
    s.logged = logged_.load();
    s.nested = nested_.load();
    s.passthrough_killed = killed_calls_.load();
    s.blob_reads = blob_reads_.load();
    s.blob_orphans = blob_orphans_.load();
    s.substituted = substituted_.load();
    s.errors = errors_.load();
    s.fallbacks = fallbacks_.load();
    s.captures = captures_.load();
    s.served_stock = served_stock_.load();
    s.capture_refused = capture_refused_.load();
    s.capture_mismatch = capture_mismatch_.load();
    s.published = published_count_.load();
    return s;
}

bool Engine::text_allowed(const char* mode) {
    if (!mode) return true;
    char m[8] = {0};
    if (!env_.read(reinterpret_cast<uint64_t>(mode), m, 4)) return false;  // unreadable mode: never substitute
    return mode_allows_text(m);
}

const Override* Engine::override_for(uint64_t size, const std::string& md5, int* row_out) const {
    const int i = match_row(rows_, size, md5);
    if (row_out) *row_out = i;
    if (i < 0 || size_t(i) >= overrides_.size()) return nullptr;
    const Override& o = overrides_[size_t(i)];
    return (o.data && o.size && o.error.empty()) ? &o : nullptr;
}

void Engine::fill_common(CallRecord& r, std::string& raw, bool& raw_ok, Reader reader, const char* chunkname, const char* mode, uint64_t caller) {
    r.seq = ++seq_;
    r.time = env_.now ? env_.now() : "";
    r.thread = env_.thread_id ? env_.thread_id() : 0;
    r.caller = env_.where ? env_.where(caller) : "";
    const uint64_t ra = reinterpret_cast<uint64_t>(reader);
    r.reader = env_.where ? env_.where(ra) : "";
    r.kind = (blob_ && ra == blob_) ? ReaderKind::Blob : (gets_ && ra == gets_) ? ReaderKind::LoadString : ReaderKind::Other;
    if (!chunkname) {
        r.chunk = "(null)";
    } else {
        // up to 128 bytes, page by page (a name near the end of a readable page must not fail as a whole)
        char buf[129] = {0};
        size_t got = 0;
        uint64_t a = reinterpret_cast<uint64_t>(chunkname);
        while (got < 128) {
            size_t step = size_t(0x1000 - (a & 0xFFF));
            if (step > 128 - got) step = 128 - got;
            if (!env_.read(a, buf + got, step)) break;
            got += step;
            a += step;
            if (std::memchr(buf, 0, got)) break;
        }
        if (got == 0) {
            r.chunk = "(unreadable)";
        } else {
            size_t len = strnlen(buf, got);
            if (len < got) {  // the whole name, NUL inside the window: usable for an exact chunkname match
                raw.assign(buf, len);
                raw_ok = true;
            }
            // loadstring's chunkname is the source text itself: keep the line short
            r.chunk = quote_chunkname(buf, len < got ? len : got, r.kind == ReaderKind::LoadString ? 60 : 120);
            if (len == got && got == 128) r.chunk += "(cut)";
        }
    }
    if (!mode) {
        r.mode = "(null)";
    } else {
        char m[8] = {0};
        r.mode = env_.read(reinterpret_cast<uint64_t>(mode), m, 4) ? quote_chunkname(m, strnlen(m, 4), 4) : "(unreadable)";
    }
}

int Engine::on_lua_load(LuaLoadFn orig, void* L, Reader reader, void* data, const char* chunkname, const char* mode, uint64_t caller) {
    ++calls_;
    if (killed()) {
        ++killed_calls_;
        return orig(L, reader, data, chunkname, mode);
    }
    if (t_depth > 0) {
        ++nested_;
        return orig(L, reader, data, chunkname, mode);
    }
    DepthGuard depth;
    CallRecord rec;
    Ctx ctx;
    SubSource sub{nullptr, 0, false};  // per call, on this thread's stack: outlives the lua_load call that reads it
    Reader use_reader = reader;
    void* use_data = data;
    void* const prev_ctx = t_ctx;
    std::string raw;
    bool raw_ok = false;
    int chunk_row = -1;           // >= 0: substituted by chunkname (the fallback applies)
    long long depth_before = -1;  // stack depth before the substituted call
    Replay replay;                // the drained stock source (STOCK CAPTURE); fixed size, on this thread's stack
    bool drained = false;         // the game's reader was consumed: every path from here serves `replay` or the override
    std::string copy;             // Turbo's copy of the drained stock (outlives every lua_load call below)
    try {
        fill_common(rec, raw, raw_ok, reader, chunkname, mode, caller);
        ctx.data = data;
        ctx.rec = &rec;
        ctx.text_ok = text_allowed(mode);
        if (rec.kind == ReaderKind::Blob) {
            t_ctx = &ctx;  // the blob reader detour fills size/md5 (and substitutes) when the parser calls it
        } else if (rec.kind == ReaderKind::LoadString) {
            // LoadS {const char* s; uint32 size}: read, never called
            uint8_t ls[12] = {0};
            if (data && env_.read(reinterpret_cast<uint64_t>(data), ls, sizeof(ls))) {
                uint64_t p = 0;
                uint32_t n = 0;
                std::memcpy(&p, ls, 8);
                std::memcpy(&n, ls + 8, 4);
                const char* src = reinterpret_cast<const char*>(static_cast<uintptr_t>(p));
                if (n > 0 && src && env_.readable(src, n)) {
                    rec.have_source = true;
                    rec.size = n;
                    rec.md5 = md5_hex(reinterpret_cast<const uint8_t*>(src), n);
                    if (static_cast<unsigned char>(src[0]) == 0x1B) {
                        rec.note = "binary chunk";
                    } else if (substitute_ && ctx.text_ok) {
                        int row = -1;
                        if (const Override* o = override_for(n, rec.md5, &row)) {
                            sub = SubSource{o->data, o->size, false};
                            use_reader = &sub_reader;
                            use_data = &sub;
                            rec.substituted = true;
                            rec.row = rows_[size_t(row)].name;
                            rec.new_size = o->size;
                            rec.new_md5 = o->md5;
                        } else if (row >= 0) {
                            rec.note = "row " + rows_[size_t(row)].name + " matched but its override is not loaded";
                        }
                    }
                } else if (n > 0) {
                    rec.note = "source unreadable";
                } else {
                    rec.note = "empty source";
                }
            } else {
                rec.note = "reader data unreadable";
            }
        } else if (raw_ok && capture_.enabled) {
            // STOCK CAPTURE: a chunkname row on a reader whose live bytes are the known one-shot leaf reader
            const int row = match_chunkname_row(rows_, raw.data(), raw.size());
            if (row >= 0) {
                const Row& rw = rows_[size_t(row)];
                rec.row = rw.name;
                const uint64_t ra = reinterpret_cast<uint64_t>(reader);
                if (!env_.reader_capturable || !env_.reader_capturable(ra)) {
                    ++capture_refused_;
                    rec.note = "row " + rw.name + " chunkname matched but the reader's code is not the verified one-shot reader: not captured, original";
                    set_error(rec.note);
                } else {
                    // 1. drain (no allocation, nothing that throws): the lua_Reader contract, until NULL or size 0
                    bool complete = false;
                    while (replay.count < Replay::kMax) {
                        size_t sz = 0;
                        const char* p = reader(L, data, &sz);
                        if (!p || sz == 0) {
                            complete = true;
                            break;
                        }
                        replay.p[replay.count] = p;
                        replay.n[replay.count] = sz;
                        ++replay.count;
                    }
                    if (!complete) {
                        replay.tail = reader;  // more pieces than kMax: the rest comes from the game's reader itself
                        replay.tail_data = data;
                    }
                    drained = true;
                    use_reader = &replay_reader;
                    use_data = &replay;
                    // 2. copy + md5 (may throw: the catch below keeps serving `replay` as drained)
                    uint64_t total = 0;
                    bool readable = complete;
                    for (int i = 0; i < replay.count && readable; ++i) {
                        total += replay.n[i];
                        if (!env_.readable(replay.p[i], replay.n[i])) readable = false;
                    }
                    if (!complete) {
                        rec.note = "row " + rw.name + ": the reader returned more than " + std::to_string(Replay::kMax) + " pieces: not measured, original";
                        set_error(rec.note);
                    } else if (!readable) {
                        rec.note = "row " + rw.name + ": drained source unreadable: replayed as returned, not measured";
                        set_error(rec.note);
                    } else {
                        copy.reserve(size_t(total));
                        for (int i = 0; i < replay.count; ++i) copy.append(replay.p[i], replay.n[i]);
                        rec.have_source = true;
                        rec.size = total;
                        rec.md5 = md5_hex(reinterpret_cast<const uint8_t*>(copy.data()), copy.size());
                        rec.captured = true;
                        replay.copy = copy.data();
                        replay.copy_n = copy.size();
                        ++captures_;
                        note_capture(total, rec.md5);
                        // 3. publish the first capture once (the host writes the cache on its own thread)
                        if (!capture_.legacy_present && (capture_.capture_mode || rec.md5 == rw.md5) && env_.save_capture) {
                            bool expect = false;
                            if (published_[size_t(row)].compare_exchange_strong(expect, true)) {
                                ++published_count_;
                                env_.save_capture(rw.name, std::make_shared<const std::string>(copy), rec.md5);
                            }
                        }
                    }
                    // 4. what lua_load gets: the override only behind every gate, else the captured stock (replay)
                    if (substitute_) {
                        const Override* op = &overrides_[size_t(row)];
                        const OverrideHeader* hp = &headers_[size_t(row)];
                        OverrideHeader mem_h;  // product mode: the in-memory build passes the header gates by construction
                        std::string why;
                        if (capture_.build_in_memory) {
                            if (!rec.captured) {
                                why = "stock not measured";
                            } else if (const Override* b = built_for(size_t(row), copy, rec.md5, why)) {
                                op = b;
                                mem_h.ok = true;
                                mem_h.build = build_;
                                mem_h.stock = rec.md5;
                                hp = &mem_h;
                            }
                        }
                        const Override& o = *op;
                        const OverrideHeader& h = *hp;
                        if (!why.empty()) {
                        } else if (!rec.captured) why = "stock not measured";
                        else if (!(o.data && o.size && o.error.empty())) why = "override not loaded (" + (o.error.empty() ? std::string("missing") : o.error) + ")";
                        else if (!h.ok) why = "override refused: " + h.error;
                        else if (build_.empty() || h.build != build_)
                            why = "stale override: header build " + h.build + ", running " + (build_.empty() ? std::string("unknown") : build_);
                        else if (h.stock != rec.md5) why = "stale override: made for stock " + h.stock + ", the game compiled " + rec.md5;
                        else if (!ctx.text_ok) why = "mode refuses text";
                        else if (!env_.stack_depth || !env_.top_message || !env_.pop1) why = "the lua_load stack layout is not confirmed (no safe fallback)";
                        else if (failed_[size_t(row)].load(std::memory_order_acquire)) why = "override failed to compile earlier";
                        else if ((depth_before = env_.stack_depth(L)) < 0) why = "the Lua stack is unreadable";
                        if (why.empty()) {
                            sub = SubSource{o.data, o.size, false};
                            use_reader = &sub_reader;
                            use_data = &sub;
                            chunk_row = row;
                            rec.substituted = true;
                            rec.by_chunkname = true;
                            rec.new_size = o.size;
                            rec.new_md5 = o.md5;
                        } else {
                            rec.note = "row " + rw.name + ": " + why + ": stock served";
                            set_error(rec.note);
                        }
                    }
                    if (!rec.substituted && rec.captured) rec.served_stock = true;
                }
            }
        }
    } catch (...) {
        ++errors_;
        if (drained) {  // the game's reader is consumed: what was drained is the only correct source
            replay.rewind();
            use_reader = &replay_reader;
            use_data = &replay;
        } else {
            use_reader = reader;
            use_data = data;
        }
        rec.substituted = false;
        rec.by_chunkname = false;
        rec.served_stock = drained;
        chunk_row = -1;
    }
    if (rec.served_stock) ++served_stock_;
    int rc = orig(L, use_reader, use_data, chunkname, mode);
    if (chunk_row >= 0 && rc != 0) {
        // The override did not compile. lua_load pushed exactly one error object; drop it and load the original (its reader and
        // data were never called or changed). Every check below must hold, else the failed rc stands (the lua_load contract:
        // exactly one value pushed).
        failed_[size_t(chunk_row)].store(true, std::memory_order_release);
        std::string msg;
        long long after = -1;
        bool popped = false;
        try {
            msg = env_.top_message(L);
            after = env_.stack_depth(L);
            if (after == depth_before + 16) popped = env_.pop1(L);
        } catch (...) {
            ++errors_;
        }
        rec.override_rc = rc;
        if (popped) {
            rec.fallback = true;
            rec.substituted = false;
            ++fallbacks_;
            // the game's reader was drained: replay the captured stock (never the consumed original reader)
            replay.rewind();
            rc = drained ? orig(L, &replay_reader, &replay, chunkname, mode) : orig(L, reader, data, chunkname, mode);
        }
        try {
            rec.override_msg = msg;
            set_error("override failed to compile (rc=" + std::to_string(rec.override_rc) + " " + msg + ")" +
                      (popped ? std::string(": stock served") : std::string(": stack not as expected, no retry")));
            if (!popped)
                rec.note = "override failed (rc=" + std::to_string(rec.override_rc) + " " + msg + ") and the stack is not as expected (depth " +
                           std::to_string(depth_before) + " -> " + std::to_string(after) + "): no retry";
        } catch (...) {
            ++errors_;
        }
    }
    t_ctx = prev_ctx;
    try {
        if (rec.kind == ReaderKind::Blob && !ctx.seen && rec.note.empty()) rec.note = "blob reader not called";
        rec.rc = rc;
        if (rec.substituted) ++substituted_;
        if (env_.emit) env_.emit(format_line(rec));
        ++logged_;
    } catch (...) {
        ++errors_;
    }
    return rc;
}

const char* Engine::on_blob_read(Reader orig, void* L, void* data, size_t* size) {
    ++blob_reads_;
    const char* p = orig(L, data, size);
    if (killed()) return p;
    auto* c = static_cast<Ctx*>(t_ctx);
    if (!c || c->data != data) {
        ++blob_orphans_;
        return p;
    }
    if (c->seen || !p || !size) return p;  // later calls (NULL = end of chunk) pass unchanged
    try {
        c->seen = true;
        CallRecord& r = *c->rec;
        const size_t n = *size;
        if (n == 0) {
            r.note = "empty source";
            return p;
        }
        if (!env_.readable(p, n)) {
            r.note = "source unreadable";
            return p;
        }
        r.have_source = true;
        r.size = n;
        r.md5 = md5_hex(reinterpret_cast<const uint8_t*>(p), n);
        if (static_cast<unsigned char>(p[0]) == 0x1B) {
            r.note = "binary chunk";
            return p;
        }
        if (!substitute_ || !c->text_ok) return p;
        int row = -1;
        const Override* o = override_for(n, r.md5, &row);
        if (!o) {
            if (row >= 0) r.note = "row " + rows_[size_t(row)].name + " matched but its override is not loaded";
            return p;
        }
        r.substituted = true;
        r.row = rows_[size_t(row)].name;
        r.new_size = o->size;
        r.new_md5 = o->md5;
        *size = o->size;  // last: nothing below can throw
        return o->data;
    } catch (...) {
        ++errors_;
        c->rec->substituted = false;  // the original buffer is what the parser gets
    }
    return p;
}

// ---------------------------------------------------------------- line queue
void LineQueue::push(std::string s) {
    std::lock_guard<std::mutex> lock(m_);
    if (q_.size() >= cap_) {
        ++dropped_;
        return;
    }
    q_.push_back(std::move(s));
}

std::vector<std::string> LineQueue::take() {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lock(m_);
    out.swap(q_);
    return out;
}

}  // namespace lualoader
}  // namespace turbo
