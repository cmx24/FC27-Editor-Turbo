// FC 27 LE Turbo GUI - Lua loader hook, platform-independent part (see lua_loader.h)
#include "core/lua_loader.h"

#include <cstdio>
#include <cstring>

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
        else notes += "blob reader not found or already hooked (file sources not measured, no substitution); ";
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
    return {{"player_kit", 0x41140, "9399e39a1e46a15d151ada423759abe9", "lua_override\\player_kit.lua"}};
}

int match_row(const std::vector<Row>& rows, uint64_t size, const std::string& md5) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].size == size && rows[i].md5 == md5) return static_cast<int>(i);
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
    if (!r.note.empty()) s += " note=\"" + r.note + "\"";
    if (r.substituted) {
        char b[96];
        std::snprintf(b, sizeof(b), " SUBSTITUTED row=%s new_size=0x%llX(%llu) new_md5=", r.row.c_str(), static_cast<unsigned long long>(r.new_size),
                      static_cast<unsigned long long>(r.new_size));
        s += b;
        s += r.new_md5;
    }
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

void Engine::configure(uint64_t gets, uint64_t blob_reader, bool substitute, std::vector<Row> rows, std::vector<Override> overrides) {
    gets_ = gets;
    blob_ = blob_reader;
    substitute_ = substitute;
    rows_ = std::move(rows);
    overrides_ = std::move(overrides);
    overrides_.resize(rows_.size());
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

void Engine::fill_common(CallRecord& r, Reader reader, const char* chunkname, const char* mode, uint64_t caller) {
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
    SubSource sub{nullptr, 0, false};
    Reader use_reader = reader;
    void* use_data = data;
    void* const prev_ctx = t_ctx;
    try {
        fill_common(rec, reader, chunkname, mode, caller);
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
        }
    } catch (...) {
        ++errors_;
        use_reader = reader;
        use_data = data;
        rec.substituted = false;
    }
    const int rc = orig(L, use_reader, use_data, chunkname, mode);
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
