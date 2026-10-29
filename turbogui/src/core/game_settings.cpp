// FC 27 LE Turbo GUI - the game's own settings file, offline (see game_settings.h)
#include "game_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

#include "nlohmann/json.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace turbo {
namespace gsettings {

namespace fs = std::filesystem;
using nlohmann::json;
using ojson = nlohmann::ordered_json;

// ------------------------------------------------------------------------------------------------ crc32, md5
uint32_t crc32(const uint8_t* p, size_t n) {
    static const auto table = [] {
        std::vector<uint32_t> t(256);
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

namespace {

// RFC 1321
struct Md5 {
    uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
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
        for (int i = 0; i < 16; ++i) m[i] = uint32_t(p[i * 4]) | uint32_t(p[i * 4 + 1]) << 8 | uint32_t(p[i * 4 + 2]) << 16 | uint32_t(p[i * 4 + 3]) << 24;
        uint32_t A = a, B = b, C = c, D = d;
        for (int i = 0; i < 64; ++i) {
            uint32_t f;
            int g;
            if (i < 16) { f = (B & C) | (~B & D); g = i; }
            else if (i < 32) { f = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = B ^ C ^ D; g = (3 * i + 5) % 16; }
            else { f = C ^ (B | ~D); g = (7 * i) % 16; }
            const uint32_t t = D;
            D = C;
            C = B;
            B = B + rol(A + f + K[i] + m[g], S[i]);
            A = t;
        }
        a += A; b += B; c += C; d += D;
    }
};

}  // namespace

std::string md5_hex(const uint8_t* p, size_t n) {
    Md5 h;
    size_t i = 0;
    for (; i + 64 <= n; i += 64) h.block(p + i);
    uint8_t tail[128] = {0};
    const size_t r = n - i;
    if (r) std::memcpy(tail, p + i, r);
    tail[r] = 0x80;
    const size_t tl = r < 56 ? 64 : 128;
    const uint64_t bits = uint64_t(n) * 8;
    for (int k = 0; k < 8; ++k) tail[tl - 8 + k] = uint8_t(bits >> (8 * k));
    h.block(tail);
    if (tl == 128) h.block(tail + 64);
    std::string s;
    static const char* hx = "0123456789abcdef";
    for (uint32_t w : {h.a, h.b, h.c, h.d})
        for (int k = 0; k < 4; ++k) {
            const uint8_t byte = uint8_t(w >> (8 * k));
            s += hx[byte >> 4];
            s += hx[byte & 15];
        }
    return s;
}

// ------------------------------------------------------------------------------------------------ the file
namespace {

uint32_t rd32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}
void wr32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int k = 0; k < 4; ++k) b[at + size_t(k)] = uint8_t(v >> (8 * k));
}
std::string hexs(uint64_t v) {
    char t[32];
    std::snprintf(t, sizeof(t), "0x%llX", static_cast<unsigned long long>(v));
    return t;
}
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
bool starts_ci(const std::string& s, const char* prefix) { return lower(s).rfind(lower(prefix), 0) == 0; }

}  // namespace

Check validate(const std::vector<uint8_t>& b) {
    Check c;
    if (b.size() != kFileSize) {
        c.why = "size " + std::to_string(b.size()) + " bytes, expected " + std::to_string(kFileSize) + " (another layout)";
        return c;
    }
    if (std::memcmp(b.data(), "FBCHUNKS\x01\x00", 10) != 0) {
        c.why = "not an FBCHUNKS file";
        return c;
    }
    if (rd32(b, 10) != kMetaSize || rd32(b, 14) != kPayloadSize) {
        c.why = "header sizes " + hexs(rd32(b, 10)) + "/" + hexs(rd32(b, 14)) + ", expected 0x454/0xBC650";
        return c;
    }
    if (std::memcmp(b.data() + kPayloadOff, "SaveType_Settin", 15) != 0) {
        c.why = "the payload is not SaveType_Settings";
        return c;
    }
    if (rd32(b, kPayloadOff + kCrcLenAt) != kPayloadSize - kCrcFrom) {
        c.why = "the payload's length word is " + hexs(rd32(b, kPayloadOff + kCrcLenAt)) + ", expected 0xBC620 (section mismatch)";
        return c;
    }
    for (int i = 0; i < 4; ++i)
        if (rd32(b, kPayloadOff + kConstAt + 4u * size_t(i)) != kConstWords[i]) {
            c.why = "section mismatch at payload+" + hexs(kConstAt + 4u * size_t(i)) + ": " + hexs(rd32(b, kPayloadOff + kConstAt + 4u * size_t(i))) +
                    ", expected " + hexs(kConstWords[i]);
            return c;
        }
    c.layout = layout_name();
    const char* name = reinterpret_cast<const char*>(b.data() + kHeaderSize);
    c.profile_name.assign(name, strnlen(name, 0x40));
    c.crc_stored = rd32(b, kPayloadOff + kCrcAt);
    c.crc_calc = crc32(b.data() + kPayloadOff + kCrcFrom, kPayloadSize - kCrcFrom);
    if (c.crc_stored != c.crc_calc) {
        c.why = "CRC check failed (stored " + hexs(c.crc_stored) + ", computed " + hexs(c.crc_calc) + ")";
        return c;
    }
    c.ok = true;
    return c;
}

void fix_crc(std::vector<uint8_t>& b) {
    if (b.size() != kFileSize) return;
    wr32(b, kPayloadOff + kCrcAt, crc32(b.data() + kPayloadOff + kCrcFrom, kPayloadSize - kCrcFrom));
}

bool get_u32(const std::vector<uint8_t>& b, size_t off, uint32_t& v) {
    if (b.size() != kFileSize || off < kCrcFrom || off + 4 > kPayloadSize) return false;
    v = rd32(b, kPayloadOff + off);
    return true;
}
bool set_u32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    if (b.size() != kFileSize || off < kCrcFrom || off + 4 > kPayloadSize) return false;
    wr32(b, kPayloadOff + off, v);
    return true;
}

// ------------------------------------------------------------------------------------------------ the folder
bool is_settings_name(const std::string& n) {
    if (n.size() != 8 + 17 || n.compare(0, 8, "Settings") != 0) return false;
    for (size_t i = 8; i < n.size(); ++i)
        if (n[i] < '0' || n[i] > '9') return false;
    return true;
}

bool is_protected_name(const std::string& n) {
    const std::string l = lower(n);
    if (l.rfind("cmmgrc", 0) == 0 || l.rfind("squads", 0) == 0 || l.rfind("profileoptions", 0) == 0 || l.rfind("assets", 0) == 0) return true;
    // _1_Squads..., _2_Squads...
    if (l.size() > 3 && l[0] == '_' && l.find("squads") != std::string::npos) return true;
    return false;
}

Folder scan(const fs::path& dir) {
    Folder f;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) {
        f.why = "the settings folder is not there: " + dir.u8string();
        return f;
    }
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string n = e.path().filename().u8string();
        if (starts_ci(n, "Settings")) f.settings.push_back(n);
    }
    std::sort(f.settings.begin(), f.settings.end());
    if (f.settings.empty()) {
        f.why = "no Settings file in " + dir.u8string() + " (start the game once and open Settings)";
        return f;
    }
    if (f.settings.size() > 1) {
        std::string all;
        for (const auto& s : f.settings) all += (all.empty() ? "" : ", ") + s;
        f.why = std::to_string(f.settings.size()) + " Settings files (" + all + "): exactly one is expected (which one the game loads is not known)";
        return f;
    }
    if (!is_settings_name(f.settings[0])) {
        f.why = f.settings[0] + " is not named Settings<17 digits>";
        return f;
    }
    f.file = dir / fs::u8path(f.settings[0]);
    f.ok = true;
    return f;
}

bool read_bytes(const fs::path& p, std::vector<uint8_t>& out, size_t max_bytes) {
    std::error_code ec;
    const uintmax_t sz = fs::file_size(p, ec);
    if (ec || sz > max_bytes) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(size_t(sz), 0);
    if (sz) f.read(reinterpret_cast<char*>(out.data()), std::streamsize(sz));
    return bool(f) || f.gcount() == std::streamsize(sz);
}

namespace {

bool write_bytes(const fs::path& p, const std::vector<uint8_t>& b, std::string* err) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (err) *err = "cannot write " + p.u8string();
        return false;
    }
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    f.flush();
    if (!f) {
        if (err) *err = "write failed: " + p.u8string();
        return false;
    }
    return true;
}

bool default_replace(const fs::path& from, const fs::path& to, std::string* err) {
    std::error_code ec;
    fs::remove(to, ec);
    fs::rename(from, to, ec);
    if (ec) {
        if (err) *err = ec.message();
        return false;
    }
    return true;
}

bool valid_id(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

std::string str_of(const json& o, const char* key) {
    if (!o.contains(key) || !o[key].is_string()) return "";
    return o[key].get<std::string>();
}

// an integer as a JSON number or a "0x.." / decimal string
bool int_of(const json& v, long long& out) {
    if (v.is_number_integer()) {
        out = v.get<long long>();
        return true;
    }
    if (!v.is_string()) return false;
    const std::string s = v.get<std::string>();
    if (s.empty()) return false;
    char* end = nullptr;
    const long long x = std::strtoll(s.c_str(), &end, 0);
    if (!end || *end) return false;
    out = x;
    return true;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ the slider map
const std::vector<std::string>& groups() {
    static const std::vector<std::string> g = {"match", "player", "user", "cpu", "meta"};
    return g;
}
const char* group_label(const std::string& group) {
    if (group == "match") return "Match";
    if (group == "player") return "Player";
    if (group == "user") return "Sliders: User";
    if (group == "cpu") return "Sliders: CPU";
    if (group == "meta") return "Sliders: groups";
    return "Other";
}

const Entry* SliderMap::entry(const std::string& id) const {
    for (const Entry& e : entries)
        if (e.id == id) return &e;
    return nullptr;
}
const Region* SliderMap::region_of(int index) const {
    for (const Region& r : regions)
        if (index >= r.first && index < r.first + r.count) return &r;
    return nullptr;
}

bool SliderMap::parse(const std::string& text, SliderMap& out, std::string* err) {
    auto fail = [&](const std::string& w) {
        if (err) *err = w;
        return false;
    };
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("turbo_settings_map")) return fail("not a Turbo settings slider map");
    SliderMap m;
    m.build = str_of(j, "build");
    m.layout = str_of(j, "layout");
    m.note = str_of(j, "note");
    if (m.layout != layout_name()) return fail("layout \"" + m.layout + "\", Turbo reads " + layout_name());
    long long base = 0;
    if (!j.contains("array_base") || !int_of(j["array_base"], base) || base < static_cast<long long>(kCrcFrom) || base >= static_cast<long long>(kPayloadSize))
        return fail("array_base missing or outside the payload");
    m.base = size_t(base);
    if (!j.contains("regions") || !j["regions"].is_array()) return fail("regions is not a list");
    std::set<std::string> seen;
    for (const auto& r : j["regions"]) {
        Region g;
        long long first = 0, count = 0;
        if (!r.is_object() || !r.contains("first") || !r.contains("count") || !int_of(r["first"], first) || !int_of(r["count"], count))
            return fail("a region needs first and count");
        g.id = str_of(r, "id");
        g.label = str_of(r, "label");
        g.candidate = str_of(r, "candidate");
        if (!valid_id(g.id) || !seen.insert(g.id).second) return fail("region id \"" + g.id + "\" is not valid or twice");
        if (first < 0 || count <= 0 || count > 256) return fail(g.id + ": first/count out of range");
        g.first = int(first);
        g.count = int(count);
        if (m.offset_of(g.first + g.count - 1) + 4 > kPayloadSize) return fail(g.id + " ends outside the payload");
        for (const Region& o : m.regions)
            if (g.first < o.first + o.count && o.first < g.first + g.count) return fail(g.id + " overlaps " + o.id);
        m.regions.push_back(g);
    }
    if (j.contains("custom_block")) {
        const auto& cb = j["custom_block"];
        long long cbase = 0, ccount = 0;
        if (!cb.is_object() || !cb.contains("base") || !cb.contains("count") || !int_of(cb["base"], cbase) || !int_of(cb["count"], ccount))
            return fail("custom_block needs base and count");
        if (cbase < static_cast<long long>(kCrcFrom + 5) || ccount <= 0 || ccount > 256 || cbase + ccount > static_cast<long long>(kPayloadSize))
            return fail("custom_block is outside the payload");
        m.custom_base = size_t(cbase);
        m.custom_count = int(ccount);
    }
    if (!j.contains("entries") || !j["entries"].is_array()) return fail("entries is not a list");
    std::set<std::string> ids, used_index, used_custom;
    for (const auto& x : j["entries"]) {
        if (!x.is_object()) return fail("an entry is not an object");
        Entry e;
        e.id = str_of(x, "id");
        e.label = str_of(x, "label");
        e.group = str_of(x, "group");
        e.note = str_of(x, "note");
        if (!valid_id(e.id) || !ids.insert(e.id).second) return fail("entry id \"" + e.id + "\" is not valid or twice");
        if (std::find(groups().begin(), groups().end(), e.group) == groups().end()) return fail(e.id + ": group \"" + e.group + "\" is not known");
        if (e.label.empty()) e.label = e.id;
        if (x.contains("index") && !x["index"].is_null()) {
            long long ix = -1;
            if (!int_of(x["index"], ix) || ix < 0 || ix > 0xFFFF) return fail(e.id + ": index is not a number");
            e.index = int(ix);
            if (!m.region_of(e.index)) return fail(e.id + ": index " + hexs(uint64_t(e.index)) + " is outside every region");
            if (!used_index.insert(std::to_string(e.index)).second) return fail(e.id + ": index " + hexs(uint64_t(e.index)) + " is used twice");
        }
        if (x.contains("custom_index") && !x["custom_index"].is_null()) {
            long long ci = -1;
            if (!int_of(x["custom_index"], ci) || ci < 0 || ci >= m.custom_count) return fail(e.id + ": custom_index is not a number inside the custom_block");
            e.custom_index = int(ci);
            if (!used_custom.insert(std::to_string(e.custom_index)).second) return fail(e.id + ": custom_index " + hexs(uint64_t(e.custom_index)) + " is used twice");
        }
        long long mn = 0, mx = 100;
        if (x.contains("min") && !int_of(x["min"], mn)) return fail(e.id + ": min is not a number");
        if (x.contains("max") && !int_of(x["max"], mx)) return fail(e.id + ": max is not a number");
        if (mn < 0 || mx > 100 || mn > mx) return fail(e.id + ": min/max must be inside 0..100");
        e.min = int(mn);
        e.max = int(mx);
        e.verified = x.contains("verified") && x["verified"].is_boolean() && x["verified"].get<bool>();
        e.proof = str_of(x, "proof");
        e.evidence = str_of(x, "evidence");
        e.game_name = str_of(x, "game_name");
        if (!e.evidence.empty() && e.evidence != "screen" && e.evidence != "table") return fail(e.id + ": evidence must be \"screen\" or \"table\"");
        if (e.verified && e.evidence == "table") return fail(e.id + ": verified needs screen evidence, not the table");
        if (e.verified && e.index < 0 && e.custom_index < 0) return fail(e.id + ": verified without an index");
        if (e.verified && e.proof.empty()) return fail(e.id + ": verified needs its proof (where the in-game check is)");
        m.entries.push_back(e);
    }
    out = std::move(m);
    return true;
}

bool load_map(const fs::path& file, SliderMap& out, std::string* err) {
    std::vector<uint8_t> b;
    if (!read_bytes(file, b, 1u << 20)) {
        if (err) *err = "cannot read " + file.u8string();
        return false;
    }
    std::string why;
    if (!SliderMap::parse(std::string(b.begin(), b.end()), out, &why)) {
        if (err) *err = file.filename().u8string() + ": " + why;
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ the current values
Snapshot read_current(const fs::path& settings_dir, const SliderMap& m) {
    Snapshot s;
    const Folder f = scan(settings_dir);
    if (!f.ok) {
        s.why = f.why;
        return s;
    }
    s.file = f.file;
    s.name = f.file.filename().u8string();
    std::vector<uint8_t> b;
    if (!read_bytes(f.file, b)) {
        s.why = "cannot read " + s.name;
        return s;
    }
    s.md5 = md5_hex(b);
    const Check c = validate(b);
    s.layout = c.layout;
    s.profile = c.profile_name;
    if (!c.ok) {
        s.why = s.name + ": " + c.why;
        return s;
    }
    for (const Region& r : m.regions)
        for (int i = r.first; i < r.first + r.count; ++i) {
            uint32_t v = 0;
            if (get_u32(b, m.offset_of(i), v)) s.slots.push_back({i, v});
        }
    if (m.custom_count > 0) {
        for (int i = 0; i < m.custom_count; ++i) s.custom_slots.push_back({i, b[kPayloadOff + m.custom_offset_of(i)]});
        const size_t pre = kPayloadOff + m.custom_base - sizeof(kCustomPrefix);
        s.custom_prefix.assign(b.begin() + std::ptrdiff_t(pre), b.begin() + std::ptrdiff_t(pre + sizeof(kCustomPrefix)));
    }
    for (const Entry& e : m.entries) {
        uint32_t v = 0;
        if (e.custom_index >= 0 && m.custom_count > 0) {
            s.values[e.id] = b[kPayloadOff + m.custom_offset_of(e.custom_index)];  // what the Custom screen shows wins over the u32 view
            continue;
        }
        if (e.index >= 0 && get_u32(b, m.offset_of(e.index), v)) s.values[e.id] = int(std::min<uint32_t>(v, 0x7FFFFFFF));
    }
    s.ok = true;
    return s;
}

static std::string hex_bytes(const uint8_t* p, size_t n) {
    std::string out;
    char t[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(t, sizeof(t), "%02X", p[i]);
        if (i) out += ' ';
        out += t;
    }
    return out;
}

std::vector<std::string> status_lines(const Snapshot& s, const SliderMap& m) {
    std::vector<std::string> v;
    for (const Region& r : m.regions) {
        std::string l = r.id;
        while (l.size() < 8) l += ' ';
        for (const auto& x : s.slots)
            if (x.first >= r.first && x.first < r.first + r.count) l += " " + std::to_string(x.second);
        v.push_back(l);
    }
    if (m.custom_count <= 0 || s.custom_prefix.size() != sizeof(kCustomPrefix)) {
        v.push_back("custom block: not in the slider map");
        return v;
    }
    const bool pre_ok = std::memcmp(s.custom_prefix.data(), kCustomPrefix, sizeof(kCustomPrefix)) == 0;
    v.push_back("custom block (" + std::to_string(m.custom_count) + " bytes at payload " + hexs(m.custom_base) + "), the 5 bytes before it: " +
                hex_bytes(s.custom_prefix.data(), s.custom_prefix.size()) +
                (pre_ok ? std::string(" (--apply accepts this file)")
                        : " (--apply REFUSES this file: it needs " + hex_bytes(kCustomPrefix, sizeof(kCustomPrefix)) + "; nothing will be written)"));
    for (const Entry& e : m.entries) {
        if (e.custom_index < 0) continue;
        const auto it = s.values.find(e.id);
        v.push_back("custom " + e.id + " (" + hexs(uint64_t(e.custom_index)) + "): " + (it == s.values.end() ? std::string("?") : std::to_string(it->second)) +
                    (e.writable() ? std::string() : " (not verified in game)"));
    }
    std::string all = "custom bytes:";
    for (const auto& x : s.custom_slots) all += " " + std::to_string(x.second);
    v.push_back(all);
    v.push_back(std::string("these values are ") + kSlidersSource);
    return v;
}

// ------------------------------------------------------------------------------------------------ presets
std::string safe_name(const std::string& s) {
    std::string out;
    bool us = false;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        if (ok) {
            out += c;
            us = false;
        } else if (!us && !out.empty()) {
            out += '_';
            us = true;
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 60) out.resize(60);
    return out.empty() ? "preset" : out;
}

bool same_name(const std::string& a, const std::string& b) { return lower(a) == lower(b); }

std::string Preset::to_json() const {
    ojson j = ojson::object();
    j["turbo_settings_preset"] = 1;
    j["name"] = name;
    j["note"] = note;
    j["created"] = created;
    ojson src = ojson::object();
    src["file"] = src_file;
    src["md5"] = src_md5;
    src["layout"] = src_layout;
    j["source"] = src;
    ojson v = ojson::object();
    for (const auto& kv : values) v[kv.first] = kv.second;
    j["values"] = v;
    if (include_unverified) j["include_unverified"] = true;
    return j.dump(1, ' ', false, ojson::error_handler_t::replace) + "\n";
}

bool Preset::parse(const std::string& text, Preset& out, std::string* err) {
    auto fail = [&](const std::string& w) {
        if (err) *err = w;
        return false;
    };
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("turbo_settings_preset")) return fail("not a Turbo game settings preset");
    Preset p;
    p.name = str_of(j, "name");
    p.note = str_of(j, "note");
    p.created = str_of(j, "created");
    if (p.name.empty()) return fail("the preset has no name");
    if (j.contains("source") && j["source"].is_object()) {
        p.src_file = str_of(j["source"], "file");
        p.src_md5 = str_of(j["source"], "md5");
        p.src_layout = str_of(j["source"], "layout");
    }
    if (!j.contains("values") || !j["values"].is_object()) return fail("values is not an object");
    p.include_unverified = j.contains("include_unverified") && j["include_unverified"].is_boolean() && j["include_unverified"].get<bool>();
    for (auto it = j["values"].begin(); it != j["values"].end(); ++it) {
        if (!valid_id(it.key())) return fail("\"" + it.key() + "\" is not a slider id");
        if (!it.value().is_number_integer()) return fail(it.key() + ": not a whole number");
        const long long v = it.value().get<long long>();
        if (v < 0 || v > 100) return fail(it.key() + ": " + std::to_string(v) + " is outside 0..100");
        p.values[it.key()] = int(v);
    }
    out = std::move(p);
    return true;
}

std::string preset_file_name(const Preset& p) { return safe_name(p.name) + ".json"; }

bool write_text(const fs::path& p, const std::string& text, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    if (!write_bytes(tmp, std::vector<uint8_t>(text.begin(), text.end()), err)) return false;
    return default_replace(tmp, p, err);
}

std::vector<Preset> load_presets(const fs::path& dir, std::vector<std::string>* errors) {
    std::vector<Preset> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (lower(e.path().extension().u8string()) == ".json") files.push_back(e.path());
        if (files.size() >= 500) break;
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
        std::vector<uint8_t> b;
        Preset p;
        std::string err;
        if (!read_bytes(f, b, 1u << 20) || !Preset::parse(std::string(b.begin(), b.end()), p, &err)) {
            if (errors) errors->push_back(f.filename().u8string() + ": " + (err.empty() ? "cannot read" : err));
            continue;
        }
        p.file = f;
        out.push_back(std::move(p));
    }
    std::stable_sort(out.begin(), out.end(), [](const Preset& a, const Preset& b) { return lower(a.name) < lower(b.name); });
    return out;
}

std::string unique_preset_name(const std::vector<Preset>& have, const std::string& wanted) {
    auto taken = [&](const std::string& n) {
        for (const Preset& q : have)
            if (same_name(q.name, n) || safe_name(q.name) == safe_name(n)) return true;
        return false;
    };
    std::string base = wanted;
    while (!base.empty() && base.back() == ' ') base.pop_back();
    if (base.empty()) base = "Preset";
    if (!taken(base)) return base;
    for (int i = 2; i < 1000; ++i)
        if (!taken(base + " " + std::to_string(i))) return base + " " + std::to_string(i);
    return base + " copy";
}

bool write_preset_file(const fs::path& dir, Preset& p, std::string* err) {
    const fs::path f = dir / fs::u8path(preset_file_name(p));
    if (!write_text(f, p.to_json(), err)) return false;
    p.file = f;
    return true;
}

bool delete_preset_file(const Preset& p, std::string* err) {
    std::error_code ec;
    if (p.file.empty() || !fs::remove(p.file, ec) || ec) {
        if (err) *err = "cannot delete " + p.file.u8string();
        return false;
    }
    return true;
}

Preset preset_from_snapshot(const std::string& name, const Snapshot& s, const SliderMap& m, const std::string& created) {
    Preset p;
    p.name = name;
    p.created = created;
    p.note = "Saved from " + s.name;
    p.src_file = s.name;
    p.src_md5 = s.md5;
    p.src_layout = s.layout;
    for (const Entry& e : m.entries) {
        if (!e.writable()) continue;
        auto it = s.values.find(e.id);
        if (it == s.values.end() || it->second < 0 || it->second > 100) continue;
        p.values[e.id] = it->second;
    }
    return p;
}

Plan plan_preset(const Preset& p, const SliderMap& m) {
    Plan out;
    for (const auto& kv : p.values) {
        const Entry* e = m.entry(kv.first);
        if (!e) {
            out.skipped.push_back(kv.first + ": not in the slider map");
            continue;
        }
        if (e->index < 0 && e->custom_index < 0) {
            out.skipped.push_back(e->label + ": not mapped yet");
            continue;
        }
        const bool table_only = !e->verified && e->inferred() && p.include_unverified;
        if (!e->verified && !table_only) {
            out.skipped.push_back(e->label + (e->inferred() ? ": mapped from the game's table but not seen on the EA screen yet (the preset does not include unverified sliders)"
                                                            : ": not verified in game yet (read-only)"));
            continue;
        }
        int v = kv.second;
        if (v < e->min || v > e->max) {
            const int to = std::min(std::max(v, e->min), e->max);
            out.clamped.push_back(e->label + ": " + std::to_string(v) + " -> " + std::to_string(to));
            v = to;
        }
        out.values[e->id] = v;
        if (table_only) out.unverified.push_back(e->id);
    }
    return out;
}

std::string Pending::to_json() const {
    ojson j = ojson::object();
    j["turbo_settings_pending"] = 1;
    j["preset"] = preset;
    j["staged"] = staged;
    j["source_md5"] = src_md5;
    ojson v = ojson::object();
    for (const auto& kv : values) v[kv.first] = kv.second;
    j["values"] = v;
    if (!unverified.empty()) {
        ojson u = ojson::array();
        for (const std::string& id : unverified) u.push_back(id);
        j["unverified"] = u;
    }
    return j.dump(1, ' ', false, ojson::error_handler_t::replace) + "\n";
}

bool Pending::parse(const std::string& text, Pending& out, std::string* err) {
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("turbo_settings_pending") || !j.contains("values") || !j["values"].is_object()) {
        if (err) *err = "not a Turbo staged settings file";
        return false;
    }
    Pending p;
    p.preset = str_of(j, "preset");
    p.staged = str_of(j, "staged");
    p.src_md5 = str_of(j, "source_md5");
    for (auto it = j["values"].begin(); it != j["values"].end(); ++it) {
        if (!valid_id(it.key()) || !it.value().is_number_integer()) {
            if (err) *err = "\"" + it.key() + "\": not a slider id with a whole number";
            return false;
        }
        p.values[it.key()] = int(std::max<long long>(-1, std::min<long long>(1000, it.value().get<long long>())));
    }
    if (j.contains("unverified")) {
        if (!j["unverified"].is_array()) {
            if (err) *err = "unverified is not a list";
            return false;
        }
        for (const auto& x : j["unverified"]) {
            if (!x.is_string() || !valid_id(x.get<std::string>()) || p.values.find(x.get<std::string>()) == p.values.end()) {
                if (err) *err = "unverified names an id that is not a staged slider";
                return false;
            }
            p.unverified.push_back(x.get<std::string>());
        }
    }
    out = std::move(p);
    return true;
}

bool stage_pending(const fs::path& out_dir, const Preset& p, const SliderMap& m, const std::string& stamp, std::string* msg) {
    const Plan plan = plan_preset(p, m);
    if (plan.values.empty()) {
        if (msg)
            *msg = "Nothing staged: none of \"" + p.name + "\"'s " + std::to_string(p.values.size()) +
                   " value(s) is a slider verified in game yet (every slider is read-only until the marker launch proves the map)";
        return false;
    }
    Pending pd;
    pd.preset = p.name;
    pd.staged = stamp;
    pd.src_md5 = p.src_md5;
    pd.values = plan.values;
    pd.unverified = plan.unverified;
    std::string err;
    if (!write_text(out_dir / pending_file(), pd.to_json(), &err)) {
        if (msg) *msg = "Not staged: " + err;
        return false;
    }
    if (msg) {
        *msg = "Staged \"" + p.name + "\": " + std::to_string(plan.values.size()) + " slider(s)" +
               (plan.unverified.empty() ? std::string() : " (" + std::to_string(plan.unverified.size()) + " of them mapped from the game's table, not yet seen on the EA screen)") +
               " in turbo_output\\" + pending_file() + ". Close FC 27, run turbo\\turbo_settings_apply.exe --apply, then start the game." +
               " Not proven in game yet: on 10-24 the game showed 50 after an apply, and --apply refuses a Settings file whose bytes before the" +
               " Custom block are not 01 00 00 00 01.";
        if (!plan.skipped.empty()) *msg += " Not staged: " + std::to_string(plan.skipped.size()) + " (not verified / not mapped).";
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ the writes
namespace {

fs::path unique_dir(const fs::path& parent, const std::string& stamp) {
    std::error_code ec;
    fs::path d = parent / fs::u8path(stamp.empty() ? std::string("backup") : stamp);
    for (int i = 2; fs::exists(d, ec) && i < 100; ++i) d = parent / fs::u8path(stamp + "-" + std::to_string(i));
    return d;
}

// The common refusals before any write: game running, folder, file, validation. Fills bytes and the file
bool preflight(const Env& env, Outcome& o, fs::path& file, std::vector<uint8_t>& bytes) {
    if (!env.game_running) {
        o.message = "Refused: cannot tell whether FC 27 is running";
        return false;
    }
    if (env.game_running()) {
        o.message = "Refused: FC 27 is running. Close the game first (it keeps the settings in memory and saves them to a new file).";
        return false;
    }
    const Folder f = scan(env.settings_dir);
    if (!f.ok) {
        o.message = "Refused: " + f.why;
        return false;
    }
    const std::string name = f.file.filename().u8string();
    if (is_protected_name(name) || !is_settings_name(name)) {
        o.message = "Refused: " + name + " is not the settings file";
        return false;
    }
    if (!read_bytes(f.file, bytes)) {
        o.message = "Refused: cannot read " + name;
        return false;
    }
    const Check c = validate(bytes);
    if (!c.ok) {
        o.message = "Refused: " + name + ": " + c.why;
        return false;
    }
    file = f.file;
    o.md5_before = md5_hex(bytes);
    return true;
}

// temp -> read back -> backup -> replace -> read back. `record` goes into backup.json
bool commit(const Env& env, Outcome& o, const fs::path& file, const std::vector<uint8_t>& before, const std::vector<uint8_t>& after, ojson record) {
    const std::string name = file.filename().u8string();
    std::string err;
    std::error_code ec;
    const std::string md5_after = md5_hex(after);
    // 1. the temp file next to the original (never named Settings*), read back
    const fs::path tmp = env.settings_dir / "~turbo_settings_write.tmp";
    if (!write_bytes(tmp, after, &err)) {
        o.message = "Not written: " + err;
        fs::remove(tmp, ec);
        return false;
    }
    std::vector<uint8_t> back;
    if (!read_bytes(tmp, back) || md5_hex(back) != md5_after || !validate(back).ok) {
        o.message = "Not written: the temp file did not read back the same (nothing changed)";
        fs::remove(tmp, ec);
        return false;
    }
    // 2. the backup with md5s
    const fs::path bdir = unique_dir(env.out_dir / backup_dir(), env.stamp);
    fs::create_directories(bdir, ec);
    if (!write_bytes(bdir / fs::u8path(name), before, &err)) {
        o.message = "Not written: the backup failed: " + err;
        fs::remove(tmp, ec);
        return false;
    }
    std::vector<uint8_t> bk;
    if (!read_bytes(bdir / fs::u8path(name), bk) || md5_hex(bk) != o.md5_before) {
        o.message = "Not written: the backup did not read back the same";
        fs::remove(tmp, ec);
        return false;
    }
    record["turbo_settings_backup"] = 1;
    record["file"] = name;
    record["md5_before"] = o.md5_before;
    record["md5_after"] = md5_after;
    record["stamp"] = env.stamp;
    if (!write_text(bdir / "backup.json", record.dump(1, ' ', false, ojson::error_handler_t::replace) + "\n", &err)) {
        o.message = "Not written: backup.json failed: " + err;
        fs::remove(tmp, ec);
        return false;
    }
    o.backup = bdir;
    // 3. the temp replaces the original (same name)
    const auto replace = env.replace ? env.replace : default_replace;
    if (!replace(tmp, file, &err)) {
        o.message = "Not written: the file could not be replaced (" + err + "); the original is unchanged, the backup is in " + bdir.u8string();
        fs::remove(tmp, ec);
        return false;
    }
    // 4. read back, one Settings file
    std::vector<uint8_t> fin;
    const Folder f = scan(env.settings_dir);
    if (!read_bytes(file, fin) || md5_hex(fin) != md5_after || !validate(fin).ok || !f.ok) {
        o.message = "WRITTEN BUT THE READ-BACK FAILED: run --undo (backup " + bdir.u8string() + ")";
        return false;
    }
    o.md5_after = md5_after;
    o.ok = true;
    return true;
}

}  // namespace

Outcome patch(const Env& env, const SliderMap& m, const std::vector<Write>& writes, const std::string& label) {
    Outcome o;
    if (writes.empty()) {
        o.message = "Nothing to write";
        return o;
    }
    fs::path file;
    std::vector<uint8_t> before;
    if (!preflight(env, o, file, before)) return o;
    std::vector<uint8_t> after = before;
    ojson list = ojson::array();
    int changed = 0;
    for (const Write& w : writes) {
        if (w.custom) {
            if (m.custom_count <= 0 || w.index < 0 || w.index >= m.custom_count) {
                o.message = "Refused: " + w.what + ": custom index " + hexs(uint64_t(w.index)) + " is outside the slider map's custom block";
                return o;
            }
            const size_t pre = kPayloadOff + m.custom_base - sizeof(kCustomPrefix);
            if (std::memcmp(after.data() + pre, kCustomPrefix, sizeof(kCustomPrefix)) != 0) {
                o.message = "Refused: " + w.what + ": the bytes before the custom block are " + hex_bytes(after.data() + pre, sizeof(kCustomPrefix)) +
                            ", not 01 00 00 00 01 (the layout moved, or the game keeps this file in a state Turbo does not know yet):" +
                            " the sliders were NOT applied, nothing was written";
                return o;
            }
            const size_t at = kPayloadOff + m.custom_offset_of(w.index);
            const uint32_t cur = after[at];
            if (cur > 100) {
                o.message = "Refused: " + w.what + " holds " + std::to_string(cur) + " now (a slider is 0..100): the layout moved";
                return o;
            }
            const uint32_t v = std::min<uint32_t>(w.value, 100);
            if (v != cur) ++changed;
            after[at] = uint8_t(v);
            o.lines.push_back(w.what + " (custom block byte " + hexs(uint64_t(w.index)) + "): " + std::to_string(cur) + " -> " + std::to_string(v));
            ojson x = ojson::object();
            x["what"] = w.what;
            x["custom_index"] = w.index;
            x["from"] = cur;
            x["to"] = v;
            list.push_back(x);
            continue;
        }
        const Region* r = m.region_of(w.index);
        if (!r) {
            o.message = "Refused: " + w.what + ": index " + hexs(uint64_t(w.index)) + " is outside the slider map's regions";
            return o;
        }
        uint32_t cur = 0;
        if (!get_u32(after, m.offset_of(w.index), cur)) {
            o.message = "Refused: " + w.what + ": outside the payload";
            return o;
        }
        if (cur > 100) {
            o.message = "Refused: " + w.what + " holds " + std::to_string(cur) + " now (a slider is 0..100): the layout moved";
            return o;
        }
        const uint32_t v = std::min<uint32_t>(w.value, 100);
        if (v != cur) ++changed;
        set_u32(after, m.offset_of(w.index), v);
        o.lines.push_back(w.what + " (" + r->id + " index " + hexs(uint64_t(w.index)) + "): " + std::to_string(cur) + " -> " + std::to_string(v));
        ojson x = ojson::object();
        x["what"] = w.what;
        x["index"] = w.index;
        x["from"] = cur;
        x["to"] = v;
        list.push_back(x);
    }
    if (changed == 0) {
        o.ok = true;
        o.message = "Nothing to change: " + file.filename().u8string() + " already holds these values";
        return o;
    }
    fix_crc(after);
    if (!validate(after).ok) {
        o.message = "Refused: the patched file does not validate";
        return o;
    }
    if (env.dry_run) {
        o.ok = true;
        o.message = "Dry run: " + std::to_string(changed) + " value(s) would change in " + file.filename().u8string() + "; nothing written";
        return o;
    }
    ojson rec = ojson::object();
    rec["label"] = label;
    rec["writes"] = list;
    if (!commit(env, o, file, before, after, rec)) return o;
    o.message = "Written: " + std::to_string(changed) + " value(s) in " + file.filename().u8string() + " (CRC checked; backup " + o.backup.u8string() +
                "). The values are in the Settings file; the in-game settings screen (only 3 sliders seen so far) and the on-pitch effect are NOT yet confirmed. Undo: turbo_settings_apply.exe --undo";
    return o;
}

Outcome apply_pending(const Env& env, const SliderMap& m) {
    Outcome o;
    const fs::path pf = env.out_dir / pending_file();
    std::vector<uint8_t> b;
    Pending pd;
    std::string err;
    if (!read_bytes(pf, b, 1u << 20)) {
        o.message = "Nothing staged: turbo_output\\" + std::string(pending_file()) + " is not there (Load a preset in Turbo's Game settings tab)";
        return o;
    }
    if (!Pending::parse(std::string(b.begin(), b.end()), pd, &err)) {
        o.message = "The sliders were NOT applied (nothing written): Refused: " + std::string(pending_file()) + ": " + err;
        return o;
    }
    std::vector<Write> writes;
    std::vector<std::string> skipped;
    for (const auto& kv : pd.values) {
        const Entry* e = m.entry(kv.first);
        const bool listed = std::find(pd.unverified.begin(), pd.unverified.end(), kv.first) != pd.unverified.end();
        if (!e || !(e->writable() || (listed && e->inferred()))) {
            skipped.push_back(kv.first);
            continue;
        }
        Write w;
        w.custom = e->custom_index >= 0;
        w.index = w.custom ? e->custom_index : e->index;
        w.value = uint32_t(std::min(std::max(kv.second, e->min), e->max));
        w.what = e->id;
        writes.push_back(w);
    }
    if (writes.empty()) {
        o.message = "The sliders were NOT applied (nothing written; turbo_output\\" + std::string(pending_file()) +
                    " stays): Refused: none of the staged values is a slider verified in game";
        return o;
    }
    o = patch(env, m, writes, "preset " + pd.preset);
    if (!o.ok)  // 3.0.1 (GUI30J): the user-facing text says plainly that nothing was applied, and why
        o.message = "The sliders were NOT applied (nothing written; turbo_output\\" + std::string(pending_file()) + " stays): " + o.message;
    for (const std::string& s : skipped) o.lines.push_back(s + ": not written (not verified in game)");
    if (o.ok && !env.dry_run) {
        std::error_code ec;
        fs::rename(pf, env.out_dir / fs::u8path("settings_pending.applied_" + (env.stamp.empty() ? std::string("x") : env.stamp) + ".json"), ec);
        if (ec) o.lines.push_back("the staged file was not renamed: " + ec.message() + " (delete it so it is not applied again)");
    }
    return o;
}

Outcome undo_last(const Env& env, bool force) {
    Outcome o;
    // the newest backup with a backup.json
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (const auto& e : fs::directory_iterator(env.out_dir / backup_dir(), ec))
        if (e.is_directory(ec) && fs::exists(e.path() / "backup.json", ec)) dirs.push_back(e.path());
    if (dirs.empty()) {
        o.message = "Nothing to undo: no backup in turbo_output\\" + std::string(backup_dir());
        return o;
    }
    std::sort(dirs.begin(), dirs.end());
    const fs::path bdir = dirs.back();
    std::vector<uint8_t> rb;
    json rec;
    if (read_bytes(bdir / "backup.json", rb, 1u << 20)) rec = json::parse(std::string(rb.begin(), rb.end()), nullptr, false);
    if (rec.is_discarded() || !rec.is_object() || !rec.contains("turbo_settings_backup")) {
        o.message = "Refused: " + (bdir / "backup.json").u8string() + " does not parse";
        return o;
    }
    if (rec.contains("undone") && rec["undone"].is_boolean() && rec["undone"].get<bool>()) {
        o.message = "Nothing to undo: the newest backup (" + bdir.filename().u8string() + ") was put back already";
        return o;
    }
    const std::string name = str_of(rec, "file"), md5_before = str_of(rec, "md5_before"), md5_after = str_of(rec, "md5_after");
    if (!is_settings_name(name) || is_protected_name(name)) {
        o.message = "Refused: the backup names \"" + name + "\", not a settings file";
        return o;
    }
    std::vector<uint8_t> original;
    if (!read_bytes(bdir / fs::u8path(name), original) || md5_hex(original) != md5_before || !validate(original).ok) {
        o.message = "Refused: the backup file is missing or its md5 differs from backup.json";
        return o;
    }
    if (!env.game_running) {
        o.message = "Refused: cannot tell whether FC 27 is running";
        return o;
    }
    if (env.game_running()) {
        o.message = "Refused: FC 27 is running. Close the game first.";
        return o;
    }
    const Folder f = scan(env.settings_dir);
    if (!f.ok) {
        o.message = "Refused: " + f.why;
        return o;
    }
    std::vector<uint8_t> cur;
    read_bytes(f.file, cur);
    o.md5_before = md5_hex(cur);
    const std::string cur_name = f.file.filename().u8string();
    if (cur_name != name || o.md5_before != md5_after) {
        if (!force) {
            o.message = "Refused: the settings file is no longer the one Turbo wrote (now " + cur_name + " " + o.md5_before + ", Turbo wrote " + name +
                        " " + md5_after + "): the game saved since. Run --undo --force to put the backup back anyway.";
            return o;
        }
        o.lines.push_back("--force: the current file " + cur_name + " (" + o.md5_before + ") is replaced by the backup");
    }
    if (env.dry_run) {
        o.ok = true;
        o.message = "Dry run: " + name + " would be put back from " + bdir.u8string();
        return o;
    }
    // the backup -> temp -> the folder holds only it
    std::string err;
    const fs::path tmp = env.settings_dir / "~turbo_settings_write.tmp";
    std::vector<uint8_t> back;
    if (!write_bytes(tmp, original, &err) || !read_bytes(tmp, back) || md5_hex(back) != md5_before) {
        fs::remove(tmp, ec);
        o.message = "Not undone: the temp file failed " + err;
        return o;
    }
    const fs::path target = env.settings_dir / fs::u8path(name);
    const auto replace = env.replace ? env.replace : default_replace;
    if (!replace(tmp, target, &err)) {
        fs::remove(tmp, ec);
        o.message = "Not undone: " + err;
        return o;
    }
    if (cur_name != name) {
        // a Settings<digits> file the game wrote since (scan: it was the only Settings file, is_settings_name): the backup is in place first
        fs::remove(f.file, ec);
        if (ec) {
            o.message = "UNDO INCOMPLETE: " + name + " is back but " + cur_name + " could not be removed (two Settings files): delete " + cur_name + " by hand";
            return o;
        }
    }
    std::vector<uint8_t> fin;
    const Folder after = scan(env.settings_dir);
    if (!after.ok || after.file.filename().u8string() != name || !read_bytes(target, fin) || md5_hex(fin) != md5_before) {
        o.message = "UNDO READ-BACK FAILED: copy " + (bdir / fs::u8path(name)).u8string() + " into the settings folder by hand";
        return o;
    }
    ojson r2 = ojson::parse(std::string(rb.begin(), rb.end()), nullptr, false);
    if (!r2.is_discarded()) {
        r2["undone"] = true;
        write_text(bdir / "backup.json", r2.dump(1, ' ', false, ojson::error_handler_t::replace) + "\n", nullptr);
    }
    o.md5_after = md5_before;
    o.ok = true;
    o.backup = bdir;
    o.message = "Undone: " + name + " is back (md5 " + md5_before + ", the only Settings file)";
    return o;
}

// ------------------------------------------------------------------------------------------------ the marker test
std::vector<Write> marker_writes(const SliderMap& m, const std::string& set, std::string* err) {
    std::vector<Write> w;
    auto region = [&](const char* id) -> const Region* {
        for (const Region& r : m.regions)
            if (r.id == id) return &r;
        return nullptr;
    };
    auto fill = [&](const Region* r, uint32_t first_value, int max_count) {
        for (int k = 0; k < r->count && k < max_count; ++k) {
            Write x;
            x.index = r->first + k;
            x.value = first_value + uint32_t(k);
            x.what = r->id + "[" + std::to_string(k) + "]";
            w.push_back(x);
        }
    };
    if (set == "blocks") {
        const Region *a = region("block_a"), *b = region("block_b");
        if (!a || !b || a->count > 44 || b->count > 44) {
            if (err) *err = "the map needs regions block_a and block_b of at most 44 values";
            return {};
        }
        fill(a, 1, 44);   // 1..44
        fill(b, 56, 44);  // 56..99
    } else if (set == "run") {
        const Region* r = region("run36");
        if (!r || r->count > 49) {
            if (err) *err = "the map needs the region run36 (at most 49 values)";
            return {};
        }
        fill(r, 1, 49);  // 1..36
    } else {
        if (err) *err = "marker set \"" + set + "\": use blocks or run";
        return {};
    }
    return w;
}

Outcome write_markers(const fs::path& settings_dir, const fs::path& out_dir, const SliderMap& m, const std::string& set) {
    Outcome o;
    std::string err;
    const std::vector<Write> writes = marker_writes(m, set, &err);
    if (writes.empty()) {
        o.message = "Refused: " + err;
        return o;
    }
    const Folder f = scan(settings_dir);
    if (!f.ok) {
        o.message = "Refused: " + f.why;
        return o;
    }
    std::error_code ec;
    if (fs::equivalent(out_dir, settings_dir, ec)) {
        o.message = "Refused: the copy must go to another folder than the game's settings folder";
        return o;
    }
    std::vector<uint8_t> b;
    if (!read_bytes(f.file, b)) {
        o.message = "Refused: cannot read " + f.file.u8string();
        return o;
    }
    const Check c = validate(b);
    if (!c.ok) {
        o.message = "Refused: " + c.why;
        return o;
    }
    o.md5_before = md5_hex(b);
    std::vector<uint8_t> mk = b;
    ojson legend = ojson::array();
    std::string txt;
    for (const Write& w : writes) {
        uint32_t cur = 0;
        get_u32(mk, m.offset_of(w.index), cur);
        if (cur > 100) {
            o.message = "Refused: " + w.what + " holds " + std::to_string(cur) + " (a slider is 0..100): the layout moved";
            return o;
        }
        set_u32(mk, m.offset_of(w.index), w.value);
        const Region* r = m.region_of(w.index);
        ojson x = ojson::object();
        x["value"] = w.value;
        x["region"] = r ? r->id : "";
        x["slot"] = w.index - (r ? r->first : 0);
        x["index"] = w.index;
        x["was"] = cur;
        legend.push_back(x);
        char line[160];
        std::snprintf(line, sizeof(line), "%3u  = %-8s slot %2d  (index 0x%X, was %u)\n", w.value, r ? r->id.c_str() : "?", w.index - (r ? r->first : 0), w.index, cur);
        txt += line;
    }
    fix_crc(mk);
    if (!validate(mk).ok) {
        o.message = "Refused: the marker copy does not validate";
        return o;
    }
    const std::string name = f.file.filename().u8string();
    fs::create_directories(out_dir, ec);
    if (!write_bytes(out_dir / fs::u8path(name), mk, &err)) {
        o.message = "Not written: " + err;
        return o;
    }
    o.md5_after = md5_hex(mk);
    ojson j = ojson::object();
    j["turbo_settings_markers"] = 1;
    j["set"] = set;
    j["source_file"] = name;
    j["source_md5"] = o.md5_before;
    j["marker_md5"] = o.md5_after;
    j["layout"] = layout_name();
    j["array_base"] = hexs(m.base);
    j["markers"] = legend;
    if (!write_text(out_dir / "markers.json", j.dump(1, ' ', false, ojson::error_handler_t::replace) + "\n", &err)) {
        o.message = "Not written: " + err;
        return o;
    }
    const std::string head =
        "Turbo settings marker test (set " + set + ") - copy of " + name + " (md5 " + o.md5_before + ")\r\n"
        "marker copy md5 " + o.md5_after + "\r\n\r\n"
        "1. Close FC 27. Back up the whole settings folder with md5s of every file (CmMgrC* included).\r\n"
        "2. turbo\\turbo_settings_apply.exe --install-markers \"<this folder>\" (it checks the file is still " + name + ", backs it up, patches it).\r\n"
        "3. Start the game the usual way (desktop shortcut / launch_fc27.ps1); never send Escape to the game. Settings > Game Settings:\r\n"
        "   note the Gameplay Type, switch the view to Custom first, screenshot the User and the CPU sliders. Each number on screen gives\r\n"
        "   its region and slot below (set blocks marks only the slider blocks, not the Match / Player pages).\r\n"
        "4. Load the throwaway career LEICESTER only (never save it) and screenshot its sliders: the markers there = the career uses\r\n"
        "   these profile sliders; other values = the career keeps its own copy (CmMgrC*).\r\n"
        "5. Kill the game. turbo\\turbo_settings_apply.exe --undo (or --undo --force when the game saved a new file), then compare the\r\n"
        "   md5s of every file with step 1 (CmMgrC* unchanged, one Settings file with the pre-test md5).\r\n\r\n"
        "value = region slot (index, the value before)\r\n";
    std::string body = head;
    for (char ch : txt) {
        if (ch == '\n') body += "\r\n";
        else body += ch;
    }
    if (!write_text(out_dir / "markers.txt", body, &err)) {
        o.message = "Not written: " + err;
        return o;
    }
    o.ok = true;
    o.message = "Marker copy written to " + (out_dir / fs::u8path(name)).u8string() + " (" + std::to_string(writes.size()) +
                " slots, legend markers.txt). The settings folder was only read.";
    return o;
}

Outcome install_markers(const Env& env, const SliderMap& m, const fs::path& marker_dir) {
    Outcome o;
    std::vector<uint8_t> b;
    if (!read_bytes(marker_dir / "markers.json", b, 1u << 20)) {
        o.message = "Refused: no markers.json in " + marker_dir.u8string() + " (run --write-markers first)";
        return o;
    }
    const json j = json::parse(std::string(b.begin(), b.end()), nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("turbo_settings_markers") || !j.contains("markers") || !j["markers"].is_array()) {
        o.message = "Refused: markers.json does not parse";
        return o;
    }
    const Folder f = scan(env.settings_dir);
    if (!f.ok) {
        o.message = "Refused: " + f.why;
        return o;
    }
    std::vector<uint8_t> cur;
    read_bytes(f.file, cur);
    const std::string name = f.file.filename().u8string();
    if (name != str_of(j, "source_file") || md5_hex(cur) != str_of(j, "source_md5")) {
        o.message = "Refused: the settings file is no longer the one the marker copy was made from (" + name + " vs " + str_of(j, "source_file") +
                    "): run --write-markers again";
        return o;
    }
    std::vector<Write> writes;
    for (const auto& x : j["markers"]) {
        long long ix = -1, v = -1;
        if (!x.is_object() || !x.contains("index") || !x.contains("value") || !int_of(x["index"], ix) || !int_of(x["value"], v) || v < 0 || v > 100) {
            o.message = "Refused: a marker in markers.json is not valid";
            return o;
        }
        Write w;
        w.index = int(ix);
        w.value = uint32_t(v);
        w.what = "marker " + std::to_string(v);
        writes.push_back(w);
    }
    o = patch(env, m, writes, "marker test " + str_of(j, "set"));
    if (o.ok && !env.dry_run && o.md5_after != str_of(j, "marker_md5"))
        o.lines.push_back("note: the written file's md5 " + o.md5_after + " differs from the marker copy's " + str_of(j, "marker_md5"));
    return o;
}

// ------------------------------------------------------------------------------------------------ 4.0 sliders-rw (a): the map per build
std::string map_file_for(const std::string& build) {
    if (build.empty()) return map_file();
    std::string b = build;
    for (char& c : b) c = char(std::toupper(static_cast<unsigned char>(c)));
    // 6AD922D1: the 6AC07E31 Settings layout unchanged ([O] 8E82B111, evidence\2026-10-28\SLIDERS_WRITE\PLAN.md section 0)
    if (b == "6AC07E31" || b == "6AD922D1") return map_file();
    return std::string();
}

// ------------------------------------------------------------------------------------------------ 4.0 sliders-rw (c): the live write
namespace live {

Found find_block(Memory& mem, const std::vector<Span>& spans, const uint8_t* expect, size_t n) {
    Found f;
    if (!expect || n == 0) {
        f.why = "no Custom bytes to look for (the Settings file has no Custom block)";
        return f;
    }
    std::vector<uint8_t> buf;
    for (const Span& s : spans) {
        if (s.end <= s.start || s.end - s.start < kCountAt + n) continue;
        // chunks overlap by kCountAt + n so a block across a chunk edge is still seen whole
        uint64_t p = (s.start + kCountAt + 7) & ~uint64_t(7);  // the next block start to examine (each once, across chunks)
        for (uint64_t at = s.start; at + kCountAt + n <= s.end;) {
            const uint64_t len = std::min<uint64_t>(kChunk, s.end - at);
            buf.resize(size_t(len));
            if (p < at + kCountAt) p = (at + kCountAt + 7) & ~uint64_t(7);
            if (!mem.read(at, buf.data(), size_t(len))) {
                p = (at + len + 7) & ~uint64_t(7);  // an unreadable chunk: skip it
            } else {
                for (; p + n <= at + len; p += 8) {
                    const uint8_t* blk = buf.data() + size_t(p - at);
                    if (blk[0] != expect[0]) continue;
                    uint32_t count = 0;
                    std::memcpy(&count, blk - kCountAt, sizeof(count));
                    if (count != uint32_t(n) || std::memcmp(blk, expect, n) != 0) continue;
                    if (++f.hits == 1) f.addr = p;
                }
            }
            if (at + len >= s.end) break;
            at += len - (kCountAt + n + 8);  // overlap: the next chunk still holds the count of a block that starts at p
        }
    }
    if (f.hits == 1) {
        f.ok = true;
        return f;
    }
    if (f.hits == 0)
        f.why = "the game's Custom sliders were not found in memory (no block with the newest Settings file's values and the count " +
                std::to_string(n) + "); open Game Type Settings once, Back, then Refresh and Load again";
    else
        f.why = std::to_string(f.hits) + " blocks look like the game's Custom sliders: refused (never a guess)";
    f.addr = 0;
    return f;
}

bool page_writable(uint64_t addr, size_t n) {
    if (addr == 0 || n == 0) return false;
#ifdef _WIN32
    for (uint64_t p = addr; p < addr + n;) {
        MEMORY_BASIC_INFORMATION mb{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(p)), &mb, sizeof(mb)) != sizeof(mb)) return false;
        if (mb.State != MEM_COMMIT || (mb.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mb.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
            return false;
        const uint64_t end = reinterpret_cast<uint64_t>(mb.BaseAddress) + mb.RegionSize;
        if (end <= p) return false;
        p = end;
    }
    return true;
#else
    return false;
#endif
}

Result write_block(Memory& mem, uint64_t addr, const SliderMap& m, const std::map<std::string, int>& values, const WritableFn& writable) {
    Result r;
    if (addr == 0 || m.custom_count <= 0) {
        r.message = "nothing written: no block address or the map has no Custom block";
        return r;
    }
    const size_t n = size_t(m.custom_count);
    if (writable && !writable(addr, n)) {
        r.message = "nothing written: the block found is not on a writable page (refused, never a guess)";
        return r;
    }
    std::vector<uint8_t> blk;
    if (!mem.read_block(addr, n, blk)) {
        r.message = "nothing written: the block can no longer be read";
        return r;
    }
    for (const auto& [id, value] : values) {
        const Entry* e = m.entry(id);
        if (!e || e->custom_index < 0 || e->custom_index >= m.custom_count) {
            r.skipped.push_back(id);
            continue;
        }
        const uint8_t v = uint8_t(std::clamp(value, e->min, e->max));
        const int old = blk[size_t(e->custom_index)];
        const uint64_t at = addr + uint64_t(e->custom_index);
        uint8_t back = 0;
        if (!mem.write(at, &v, 1) || !mem.read(at, &back, 1) || back != v) {
            const uint8_t o = uint8_t(old);
            mem.write(at, &o, 1);
            r.message = "read-back differs at " + id + ": nothing more written";
            return r;
        }
        char hx[16];
        std::snprintf(hx, sizeof(hx), "0x%02X", e->custom_index);
        r.lines.push_back(id + " (" + hx + "): " + std::to_string(old) + " -> " + std::to_string(int(v)));
        r.changed[e->custom_index] = {old, int(v)};
    }
    r.ok = !r.changed.empty();
    r.message = r.ok ? std::to_string(r.changed.size()) + " slider(s) written in the running game" : std::string("no slider of the preset has a Custom index");
    return r;
}

std::string journal_json(const Result& r, const std::string& preset, const std::string& stamp) {
    json j;
    j["turbo_sliders_live"] = 1;
    j["preset"] = preset;
    j["written"] = stamp;
    json c = json::object();
    for (const auto& [ci, ov] : r.changed) c[std::to_string(ci)] = {{"old", ov.first}, {"new", ov.second}};
    j["custom"] = c;
    j["skipped"] = r.skipped;
    return j.dump(1);
}

}  // namespace live

}  // namespace gsettings
}  // namespace turbo
