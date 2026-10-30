// FC 27 LE Turbo GUI - 3D kits on the pitch: resolver, plan, block table, runtime (see kit_swap.h)
#include "kit_swap.h"

#include <algorithm>
#include <array>
#include <map>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <set>
#include <iterator>
#include <sstream>
#include <tuple>

#include "nlohmann/json.hpp"

namespace turbo {
namespace kitswap {

namespace fs = std::filesystem;
using nlohmann::json;

static bool fail(std::string* err, const std::string& why) {
    if (err) *err = why;
    return false;
}
static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]); }
static uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
static uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
static uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
static uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
static std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

uint64_t fnv1a64(const uint8_t* p, size_t n, uint64_t h) {
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ULL;
    return h;
}

// ---------------------------------------------------------------- DbObject (layout.toc)
namespace {
constexpr size_t kTocData = 0x22C;
constexpr uint32_t kManifestMagic = 0x9D798ED6;

bool varint(const uint8_t* d, size_t n, size_t& p, uint64_t& r) {
    r = 0;
    int s = 0;
    for (;;) {
        if (p >= n || s > 56) return false;
        const uint8_t b = d[p++];
        r |= uint64_t(b & 0x7F) << s;
        s += 7;
        if (!(b & 0x80)) return true;
    }
}

// false = malformed; end = the 0 terminator
bool read_value(const uint8_t* d, size_t n, size_t& p, std::string& name, json& v, bool& end, int depth) {
    end = false;
    if (depth > 24 || p >= n) return false;
    const uint8_t t = d[p++];
    if (t == 0) {
        end = true;
        return true;
    }
    name.clear();
    if (!(t & 0x80)) {
        size_t e = p;
        while (e < n && d[e]) ++e;
        if (e >= n) return false;
        name.assign(reinterpret_cast<const char*>(d + p), e - p);
        p = e + 1;
    }
    switch (t & 0x1F) {
        case 1:
        case 2: {
            uint64_t size;
            if (!varint(d, n, p, size) || size > n - p) return false;
            const size_t stop = p + size_t(size);
            json out = (t & 0x1F) == 1 ? json::array() : json::object();
            while (p < stop) {
                std::string nm;
                json x;
                bool e2 = false;
                if (!read_value(d, stop, p, nm, x, e2, depth + 1)) return false;
                if (e2) break;
                if ((t & 0x1F) == 1) out.push_back(std::move(x));
                else out[nm] = std::move(x);
            }
            p = stop;
            v = std::move(out);
            return true;
        }
        case 6:
            if (p >= n) return false;
            v = d[p++] != 0;
            return true;
        case 7: {
            uint64_t len;
            if (!varint(d, n, p, len) || len == 0 || len > n - p) return false;
            v = std::string(reinterpret_cast<const char*>(d + p), size_t(len) - 1);
            p += size_t(len);
            return true;
        }
        case 8:
            if (n - p < 4) return false;
            v = int32_t(le32(d + p));
            p += 4;
            return true;
        case 9:
            if (n - p < 8) return false;
            v = int64_t(uint64_t(le32(d + p)) | uint64_t(le32(d + p + 4)) << 32);
            p += 8;
            return true;
        case 0xB:
        case 0xC: {
            const size_t k = (t & 0x1F) == 0xB ? 4 : 8;
            if (n - p < k) return false;
            v = 0.0;
            p += k;
            return true;
        }
        case 0xF:
            if (n - p < 16) return false;
            v = hex(d + p, 16);
            p += 16;
            return true;
        case 0x10:
            if (n - p < 20) return false;
            v = hex(d + p, 20);
            p += 20;
            return true;
        case 0x13: {
            uint64_t len;
            if (!varint(d, n, p, len) || len > n - p) return false;
            v = std::string();  // blobs are not needed
            p += size_t(len);
            return true;
        }
        default:
            return false;
    }
}
}  // namespace

bool parse_layout(const std::vector<uint8_t>& d, std::map<std::string, std::string>& packages, std::string* err) {
    size_t p = (d.size() > 4 && d[0] == 0x00 && d[1] == 0xD1 && d[2] == 0xCE && d[3] == 0x01) ? kTocData : 0;
    std::string nm;
    json v;
    bool end = false;
    if (!read_value(d.data(), d.size(), p, nm, v, end, 0) || end || !v.is_object()) return fail(err, "layout.toc is not a DbObject");
    try {
        packages.clear();
        for (const json& c : v.at("installManifest").at("installChunks")) {
            const int64_t pi = c.at("persistentIndex").get<int64_t>();
            char b[16];
            std::snprintf(b, sizeof(b), "%08x", static_cast<unsigned>(pi & 0xFFFFFFFF));
            packages[b] = c.at("name").get<std::string>();
        }
    } catch (const std::exception& e) {
        return fail(err, std::string("layout.toc: ") + e.what());
    }
    return !packages.empty() || fail(err, "layout.toc lists no install chunks");
}

// ---------------------------------------------------------------- toc, blocks, manifest, texture header
bool parse_toc_bundles(const std::vector<uint8_t>& d, std::vector<std::vector<Loc>>& bundles, std::string* err) {
    bundles.clear();
    if (d.size() < kTocData + 60) return fail(err, "toc too short");
    uint32_t h[15];
    for (int i = 0; i < 15; ++i) h[i] = be32(&d[kTocData + size_t(i) * 4]);
    const uint64_t count = h[2];
    const uint64_t table = kTocData + uint64_t(h[1]);
    if (table + count * 16 > d.size()) return fail(err, "toc bundle table is cut off");
    bundles.resize(size_t(count));
    for (uint64_t i = 0; i < count; ++i) {
        const uint8_t* e = &d[size_t(table + i * 16)];
        const uint32_t size = be32(e + 4);
        const uint64_t off = be64(e + 8);
        if (!(size & 0x40000000u)) continue;
        const uint64_t o = kTocData + off, len = size & 0x3FFFFFFFu;
        if (o + len > d.size() || len < 36) continue;
        const uint8_t* bd = &d[size_t(o)];
        uint32_t flags_off = be32(bd + 8), cnt = be32(bd + 12);
        uint64_t p = be32(bd + 16);
        if (flags_off + uint64_t(cnt) > len || cnt > 100000) continue;
        std::vector<Loc> locs;
        locs.reserve(cnt);
        std::string cas;
        bool bad = false;
        for (uint32_t k = 0; k < cnt; ++k) {
            const uint8_t fl = bd[flags_off + k];
            if (fl & 0x80) {
                if (p + 8 > len) { bad = true; break; }
                cas = hex(bd + p, 8);
                p += 8;
            }
            if (p + 8 > len) { bad = true; break; }
            Loc l;
            l.cas = cas;
            l.off = be32(bd + p);
            l.size = be32(bd + p + 4);
            l.flag = fl;
            locs.push_back(l);
            p += 8;
        }
        if (!bad) bundles[size_t(i)] = std::move(locs);
    }
    return true;
}

bool block_headers(const std::vector<uint8_t>& d, std::vector<Block>& out) {
    out.clear();
    size_t p = 0;
    while (p < d.size()) {
        if (d.size() - p < 8) return false;
        Block b;
        b.dsize = be32(&d[p]) & 0xFFFFFFu;
        const uint16_t code = le16(&d[p + 4]);
        b.csize = uint32_t(be16(&d[p + 6])) + (uint32_t((code >> 8) & 0x0F) << 16);
        b.ctype = uint8_t(code & 0x7F);
        b.payload = p + 8;
        if (b.payload + b.csize > d.size()) return false;
        out.push_back(b);
        p = size_t(b.payload) + b.csize;
    }
    return true;
}

bool read_blocks(const std::vector<uint8_t>& d, const OodleFn& oodle, std::vector<uint8_t>& out, std::string* err) {
    std::vector<Block> bl;
    if (!block_headers(d, bl)) return fail(err, "block stream is cut off");
    out.clear();
    for (const Block& b : bl) {
        const size_t at = out.size();
        if (b.ctype == 0) {
            if (b.dsize > b.csize) return fail(err, "stored block shorter than its raw size");
            out.insert(out.end(), d.begin() + std::ptrdiff_t(b.payload), d.begin() + std::ptrdiff_t(b.payload + b.dsize));
        } else if (b.ctype == 0x11 || b.ctype == 0x15 || b.ctype == 0x19) {
            if (!oodle) return fail(err, "Oodle block without a decompressor");
            out.resize(at + b.dsize);
            if (!oodle(&d[size_t(b.payload)], b.csize, out.data() + at, b.dsize)) return fail(err, "Oodle decompress failed");
        } else {
            return fail(err, "block type " + std::to_string(b.ctype));
        }
    }
    return true;
}

bool parse_manifest(const std::vector<uint8_t>& b, Manifest& out, std::string* err) {
    out = Manifest();
    if (b.size() < 0x24 || le32(&b[4]) != kManifestMagic) return fail(err, "manifest magic");
    const uint32_t total = le32(&b[8]), ne = le32(&b[12]), nr = le32(&b[16]), nc = le32(&b[20]), so = le32(&b[24]);
    if (total > 1000000 || ne > 100000 || nr > 100000 || nc > 100000) return fail(err, "manifest counts");
    const uint64_t strings = 4 + uint64_t(so);
    uint64_t p = 0x24 + 20ull * total;
    const uint64_t need = p + 8ull * ne + 8ull * nr + 4ull * nr + 24ull * nr + 24ull * nc;
    if (need > b.size() || strings >= b.size()) return fail(err, "manifest is cut off");
    auto cstr = [&](uint32_t off, std::string& s) {
        uint64_t e = strings + off;
        if (e >= b.size()) return false;
        uint64_t q = e;
        while (q < b.size() && b[size_t(q)]) ++q;
        if (q >= b.size()) return false;
        s.assign(reinterpret_cast<const char*>(&b[size_t(e)]), size_t(q - e));
        return true;
    };
    for (uint32_t i = 0; i < ne; ++i, p += 8) {
        std::string s;
        if (!cstr(le32(&b[size_t(p)]), s)) return fail(err, "manifest string");
        out.ebx.push_back(std::move(s));
    }
    for (uint32_t i = 0; i < nr; ++i, p += 8) {
        ManifestRes r;
        if (!cstr(le32(&b[size_t(p)]), r.name)) return fail(err, "manifest string");
        r.size = le32(&b[size_t(p) + 4]);
        out.res.push_back(std::move(r));
    }
    for (uint32_t i = 0; i < nr; ++i, p += 4) out.res[i].type = le32(&b[size_t(p)]);
    p += 24ull * nr;  // res meta (16) + res rid (8)
    for (uint32_t i = 0; i < nc; ++i, p += 24) {
        ManifestChunk c;
        c.guid = hex(&b[size_t(p)], 16);
        c.logical = le32(&b[size_t(p) + 20]);
        out.chunks.push_back(std::move(c));
    }
    return true;
}

bool parse_texture_header(const std::vector<uint8_t>& h, TexHeader& out, std::string* err) {
    out = TexHeader();
    if (h.size() < 0x38 + 15 * 4) return fail(err, "texture header too short");
    out.fmt_id = le32(&h[0x0C]);
    out.w = le16(&h[0x16]);
    out.h = le16(&h[0x18]);
    out.mips = h[0x1E];
    out.guid = hex(&h[0x28], 16);
    if (out.mips < 1 || out.mips > 15 || out.w < 1 || out.h < 1) return fail(err, "texture header: implausible size");
    for (int i = 0; i < out.mips; ++i) out.mip_sizes.push_back(le32(&h[0x38 + size_t(i) * 4]));
    return true;
}

uint32_t dxgi_of_format(uint32_t f) {
    switch (f) {
        case 54: return 71;
        case 55: return 72;
        case 60: return 77;
        case 61: return 78;
        case 62: return 80;
        case 63: return 83;
        case 66: return 98;
        case 67: return 99;
        default: return 0;
    }
}

std::vector<uint32_t> bc_mip_sizes(uint32_t dxgi, int w, int h, int mips) {
    uint32_t bs = 0;
    switch (dxgi) {
        case 71: case 72: case 80: bs = 8; break;
        case 77: case 78: case 83: case 98: case 99: bs = 16; break;
        default: return {};
    }
    std::vector<uint32_t> v;
    for (int m = 0; m < mips; ++m) {
        const int mw = std::max(1, w >> m), mh = std::max(1, h >> m);
        v.push_back(uint32_t(((mw + 3) / 4) * ((mh + 3) / 4)) * bs);
    }
    return v;
}

// ---------------------------------------------------------------- game files
bool GameFiles::init(std::string* err) {
    std::vector<uint8_t> d;
    if (!read_file("Data/layout.toc", d)) return fail(err, "cannot read Data\\layout.toc");
    return parse_layout(d, packages_, err);
}

bool GameFiles::read_file(const std::string& rel, std::vector<uint8_t>& out) {
    std::ifstream f(root_ / fs::path(rel), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff n = f.tellg();
    if (n < 0 || n > (std::streamoff(1) << 30)) return false;
    out.resize(size_t(n));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return bool(f);
}

fs::path GameFiles::cas_path(const std::string& cas) const {
    if (cas.size() != 16) return {};
    auto it = packages_.find(cas.substr(4, 8));
    if (it == packages_.end()) return {};
    char name[32];
    std::snprintf(name, sizeof(name), "cas_%02d.cas", int(std::strtol(cas.substr(12, 4).c_str(), nullptr, 16)));
    fs::path p = root_ / (cas.substr(0, 4) != "0000" ? "Patch" : "Data") / "Win32";
    std::string pkg = it->second;
    for (char& c : pkg)
        if (c == '\\') c = '/';
    return p / fs::path(pkg) / name;
}

bool GameFiles::read(const std::string& cas, uint64_t off, uint64_t size, std::vector<uint8_t>& out) {
    const fs::path p = cas_path(cas);
    if (p.empty() || size > (uint64_t(1) << 28)) return false;
    const std::string key = p.string();
    auto it = files_.find(key);
    if (it == files_.end()) {
        auto f = std::make_unique<std::ifstream>(p, std::ios::binary);
        if (!*f) return false;
        it = files_.emplace(key, std::move(f)).first;
    }
    std::ifstream& f = *it->second;
    f.clear();
    f.seekg(std::streamoff(off));
    out.resize(size_t(size));
    f.read(reinterpret_cast<char*>(out.data()), std::streamsize(size));
    return bool(f) && uint64_t(f.gcount()) == size;
}

std::vector<std::string> GameFiles::list_tocs() const {
    std::vector<std::string> v;
    for (const char* layer : {"Data", "Patch"}) {
        std::error_code ec;
        std::vector<std::string> names;
        for (fs::directory_iterator it(root_ / layer / "Win32", ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().extension() == ".toc") names.push_back(it->path().filename().string());
        std::sort(names.begin(), names.end());
        for (const auto& n : names) v.push_back(std::string(layer) + "/Win32/" + n);
    }
    return v;
}

// ---------------------------------------------------------------- names
int kind_slot(const std::string& kind) {
    std::string k = kind;
    if (k.compare(0, 5, "loc2_") == 0) k = k.substr(5);
    const size_t a = k.rfind('_');
    if (a == std::string::npos || a == 0) return -1;
    const size_t b = k.rfind('_', a - 1);
    if (b == std::string::npos) return -1;
    const std::string type = k.substr(b + 1, a - b - 1), var = k.substr(a + 1);
    if (type.empty() || var.empty() || type.size() > 3 || var.size() > 2) return -1;
    for (char c : type + var)
        if (c < '0' || c > '9') return -1;
    int t = std::atoi(type.c_str());
    if (t >= 40) t -= 40;
    return t >= 0 && t <= 4 ? t : -1;
}

std::string role_of(const std::string& leaf, int team) {
    const std::string tok = "_" + std::to_string(team) + "_";
    size_t at = 0;
    while ((at = leaf.find(tok, at)) != std::string::npos) {
        size_t p = at + tok.size();
        size_t q = p;
        while (q < leaf.size() && leaf[q] >= '0' && leaf[q] <= '9') ++q;
        if (q > p && q < leaf.size() && leaf[q] == '_') {
            size_t r = q + 1, r2 = r;
            while (r2 < leaf.size() && leaf[r2] >= '0' && leaf[r2] <= '9') ++r2;
            if (r2 > r && r2 < leaf.size() && leaf[r2] == '_' && r2 + 1 < leaf.size()) return leaf.substr(0, at) + "_" + leaf.substr(r2 + 1);
        }
        ++at;
    }
    return "";
}

KitName parse_kit_name(const std::string& path) {
    KitName k;
    std::vector<std::string> p;
    size_t s = 0;
    for (;;) {
        const size_t e = path.find('/', s);
        p.push_back(path.substr(s, e == std::string::npos ? std::string::npos : e - s));
        if (e == std::string::npos) break;
        s = e + 1;
    }
    size_t c = 0;
    while (c < p.size() && p[c] != "character") ++c;
    if (c + 6 > p.size() || p[c + 1] != "kit" || p[c + 2].compare(0, 4, "kit_") != 0) return k;
    k.slug = p[c + 3];
    k.kind = p[c + 4];
    k.leaf = p.back();
    k.part = p.size() > c + 6 ? p[c + 5] : "";
    const size_t u = k.slug.rfind('_');
    if (u == std::string::npos || u + 1 >= k.slug.size()) return k;
    for (size_t i = u + 1; i < k.slug.size(); ++i)
        if (k.slug[i] < '0' || k.slug[i] > '9') return k;
    k.team = std::atoi(k.slug.c_str() + u + 1);
    k.ok = true;
    return k;
}

std::string phys_key(const Loc& l) { return l.cas + ":" + std::to_string(l.off) + ":" + std::to_string(l.size); }

static bool parse_phys_key(const std::string& k, Loc& l) {
    const size_t a = k.find(':'), b = k.find(':', a + 1);
    if (a == std::string::npos || b == std::string::npos) return false;
    l.cas = k.substr(0, a);
    l.off = std::strtoull(k.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
    l.size = uint32_t(std::strtoul(k.substr(b + 1).c_str(), nullptr, 10));
    return true;
}

// ---------------------------------------------------------------- index
bool scan_kits(GameFiles& files, const OodleFn& oodle, KitIndex& out, Progress* progress, std::string* err) {
    out = KitIndex();
    const double t0 = now_s();
    const auto tocs = files.list_tocs();
    for (const std::string& rel : tocs) {
        if (progress && progress->cancel.load()) return fail(err, "cancelled");
        std::vector<uint8_t> toc;
        if (!files.read_file(rel, toc)) continue;
        std::vector<std::vector<Loc>> bundles;
        if (!parse_toc_bundles(toc, bundles, nullptr)) continue;
        toc.clear();
        toc.shrink_to_fit();
        const int ti = int(out.tocs.size());
        out.tocs.push_back(rel);
        if (progress) progress->total += int(bundles.size());
        std::vector<uint8_t> raw, blob;
        for (size_t i = 0; i < bundles.size(); ++i) {
            if (progress) {
                ++progress->done;
                if ((i & 255) == 0 && progress->cancel.load()) return fail(err, "cancelled");
            }
            const auto& locs = bundles[i];
            if (locs.empty() || locs[0].size < 40) continue;
            ++out.bundles_seen;
            if (!files.read_loc(locs[0], raw)) { ++out.unreadable; continue; }
            Manifest m;
            if (raw.size() > 8 && le32(&raw[4]) == kManifestMagic) {
                if (!parse_manifest(raw, m, nullptr)) { ++out.unreadable; continue; }
            } else {
                if (!read_blocks(raw, oodle, blob, nullptr) || !parse_manifest(blob, m, nullptr)) { ++out.unreadable; continue; }
            }
            KitName first;
            std::set<std::string> kinds;
            for (const auto& n : m.ebx) {
                if (n.find("character/kit/kit_") == std::string::npos) continue;
                KitName k = parse_kit_name(n);
                if (!k.ok) continue;
                if (!first.ok) first = k;
                kinds.insert(k.kind);
            }
            if (!first.ok) continue;
            const size_t base = 1 + m.ebx.size() + m.res.size();
            if (locs.size() < base + m.chunks.size()) continue;
            KitBundle kb;
            kb.toc = ti;
            kb.bundle = uint32_t(i);
            kb.team = first.team;
            kb.kind = first.kind;
            for (const auto& kind : kinds) {
                const int ks = kind_slot(kind);
                if (ks >= 0) kb.slot_mask |= 1u << ks;
                out.kinds[first.team].insert(kind);
            }
            out.bundles.push_back(kb);
            for (size_t c = 0; c < m.chunks.size(); ++c) {
                auto& u = out.users[phys_key(locs[base + c])];
                for (const auto& kind : kinds) {
                    bool have = false;
                    for (const auto& x : u) have = have || (x.team == first.team && x.kind == kind);
                    if (!have) u.push_back({first.team, kind_slot(kind), kind});
                }
            }
        }
    }
    out.seconds = now_s() - t0;
    return true;
}

// ---------------------------------------------------------------- the kit index on disk
std::string index_signature(const GameFiles& files) {
    std::string sig = "v1";
    auto add = [&](const std::string& rel) {
        std::error_code ec;
        const fs::path p = files.root() / fs::path(rel);
        const auto sz = fs::file_size(p, ec);
        const auto tm = fs::last_write_time(p, ec);
        sig += "|" + rel + ":" + (ec ? std::string("?") : std::to_string(sz) + ":" + std::to_string(tm.time_since_epoch().count()));
    };
    add("Data/layout.toc");
    for (const auto& t : files.list_tocs()) add(t);
    return sig;
}

namespace {
struct Wr {
    std::vector<uint8_t> b;
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
    }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void u64(uint64_t v) {
        u32(uint32_t(v));
        u32(uint32_t(v >> 32));
    }
    void u8(uint8_t v) { b.push_back(v); }
    void str(const std::string& s) {
        u32(uint32_t(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    }
};
struct Rd {
    const std::vector<uint8_t>& b;
    size_t p = 0;
    bool ok = true;
    explicit Rd(const std::vector<uint8_t>& v) : b(v) {}
    uint32_t u32() {
        if (b.size() - p < 4) { ok = false; return 0; }
        const uint32_t v = le32(&b[p]);
        p += 4;
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
    uint64_t u64() {
        const uint64_t lo = u32();
        return lo | uint64_t(u32()) << 32;
    }
    uint8_t u8() {
        if (b.size() - p < 1) { ok = false; return 0; }
        return b[p++];
    }
    std::string str() {
        const uint32_t n = u32();
        if (!ok || b.size() - p < n) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(&b[p]), n);
        p += n;
        return s;
    }
};
constexpr uint32_t kCacheMagic = 0x58494B54;  // "TKIX"
constexpr uint32_t kCacheVersion = 2;
}  // namespace

bool save_index_cache(const GameFiles& files, const KitIndex& idx, const fs::path& file) {
    try {
        std::vector<std::string> kinds;
        std::unordered_map<std::string, uint32_t> kidx;
        auto kid = [&](const std::string& k) {
            auto it = kidx.find(k);
            if (it != kidx.end()) return it->second;
            kinds.push_back(k);
            return kidx[k] = uint32_t(kinds.size() - 1);
        };
        Wr w;
        w.u32(kCacheMagic);
        w.u32(kCacheVersion);
        w.str(index_signature(files));
        w.u32(uint32_t(idx.tocs.size()));
        for (const auto& t : idx.tocs) w.str(t);
        w.i32(idx.bundles_seen);
        w.i32(idx.unreadable);
        w.u32(uint32_t(idx.bundles.size()));
        for (const auto& b : idx.bundles) {
            w.i32(b.toc);
            w.u32(b.bundle);
            w.i32(b.team);
            w.u32(kid(b.kind));
            w.u32(b.slot_mask);
        }
        w.u32(uint32_t(idx.users.size()));
        for (const auto& kv : idx.users) {
            w.str(kv.first);
            w.u32(uint32_t(kv.second.size()));
            for (const auto& u : kv.second) {
                w.i32(u.team);
                w.i32(u.slot);
                w.u32(kid(u.kind));
            }
        }
        w.u32(uint32_t(idx.kinds.size()));
        for (const auto& kv : idx.kinds) {
            w.i32(kv.first);
            w.u32(uint32_t(kv.second.size()));
            for (const auto& k : kv.second) w.u32(kid(k));
        }
        Wr head;
        head.u32(uint32_t(kinds.size()));
        for (const auto& k : kinds) head.str(k);
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        const fs::path tmp = file.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(head.b.data()), std::streamsize(head.b.size()));
            f.write(reinterpret_cast<const char*>(w.b.data()), std::streamsize(w.b.size()));
            if (!f) return false;
        }
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
        return !ec;
    } catch (...) {
        return false;
    }
}

bool load_index_cache(const GameFiles& files, const fs::path& file, KitIndex& out) {
    try {
        std::ifstream f(file, std::ios::binary | std::ios::ate);
        if (!f) return false;
        const std::streamoff n = f.tellg();
        if (n < 16 || n > (std::streamoff(1) << 29)) return false;
        std::vector<uint8_t> d;
        d.resize(size_t(n));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(d.data()), n);
        if (!f) return false;
        Rd r(d);
        const uint32_t nk = r.u32();
        if (!r.ok || nk > 1000000) return false;
        std::vector<std::string> kinds;
        for (uint32_t i = 0; i < nk && r.ok; ++i) kinds.push_back(r.str());
        if (r.u32() != kCacheMagic || r.u32() != kCacheVersion || !r.ok) return false;
        if (r.str() != index_signature(files)) return false;  // a patch or another build: scan again
        KitIndex o;
        const uint32_t nt = r.u32();
        if (!r.ok || nt > 100000) return false;
        for (uint32_t i = 0; i < nt && r.ok; ++i) o.tocs.push_back(r.str());
        o.bundles_seen = r.i32();
        o.unreadable = r.i32();
        auto kind_of = [&](uint32_t i) -> const std::string& {
            static const std::string none;
            if (i >= kinds.size()) { r.ok = false; return none; }
            return kinds[i];
        };
        const uint32_t nb = r.u32();
        if (!r.ok || nb > 5000000) return false;
        for (uint32_t i = 0; i < nb && r.ok; ++i) {
            KitBundle b;
            b.toc = r.i32();
            b.bundle = r.u32();
            b.team = r.i32();
            b.kind = kind_of(r.u32());
            b.slot_mask = r.u32();
            if (b.toc < 0 || size_t(b.toc) >= o.tocs.size()) return false;
            o.bundles.push_back(std::move(b));
        }
        const uint32_t nu = r.u32();
        if (!r.ok || nu > 20000000) return false;
        o.users.reserve(nu);
        for (uint32_t i = 0; i < nu && r.ok; ++i) {
            std::string key = r.str();
            const uint32_t c = r.u32();
            if (!r.ok || c > 100000) return false;
            std::vector<KitUser> us;
            for (uint32_t k = 0; k < c && r.ok; ++k) {
                KitUser u;
                u.team = r.i32();
                u.slot = r.i32();
                u.kind = kind_of(r.u32());
                us.push_back(std::move(u));
            }
            o.users.emplace(std::move(key), std::move(us));
        }
        const uint32_t nteams = r.u32();
        if (!r.ok || nteams > 1000000) return false;
        for (uint32_t i = 0; i < nteams && r.ok; ++i) {
            const int32_t team = r.i32();
            const uint32_t c = r.u32();
            if (!r.ok || c > 100000) return false;
            for (uint32_t k = 0; k < c && r.ok; ++k) o.kinds[team].insert(kind_of(r.u32()));
        }
        if (!r.ok || r.p != d.size()) return false;
        o.from_cache = true;
        out = std::move(o);
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------- staged textures
bool load_staged(const fs::path& dir, StagedKit& out, std::string* err) {
    out = StagedKit();
    std::ifstream f(dir / "manifest.json", std::ios::binary);
    if (!f) return fail(err, "no staged textures (turbo_output\\kits\\...\\manifest.json missing)");
    json j = json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("textures")) return fail(err, "manifest.json is not a Turbo kit manifest");
    try {
        out.team = int(j.at("teamid").get<int64_t>());
        out.slot = int(j.at("teamkittypetechid").get<int64_t>());
        const std::string hs = j.value("hotspots", std::string());
        std::error_code ec0;
        if (!hs.empty() && hs.find_first_of("/\\") == std::string::npos && fs::is_regular_file(dir / hs, ec0)) out.hotspots = dir / hs;
        for (const json& t : j.at("textures")) {
            StagedTexture s;
            s.role = t.at("role").get<std::string>();
            s.file = dir / t.at("file").get<std::string>();
            s.dxgi = uint32_t(t.at("dxgi").get<int64_t>());
            s.w = int(t.at("width").get<int64_t>());
            s.h = int(t.at("height").get<int64_t>());
            s.mips = int(t.at("mips").get<int64_t>());
            s.bytes = uint64_t(t.at("bytes").get<int64_t>());
            std::error_code ec;
            if (!fs::is_regular_file(s.file, ec) || fs::file_size(s.file, ec) != s.bytes) return fail(err, s.role + ": staged file missing or its size changed");
            out.textures.push_back(std::move(s));
        }
    } catch (const std::exception& e) {
        return fail(err, std::string("manifest.json: ") + e.what());
    }
    return !out.textures.empty() || fail(err, "manifest.json lists no textures");
}

// ---------------------------------------------------------------- table
struct TableBuilder::Impl {
    struct Rec {
        Entry e;
        bool dead = false;
    };
    std::vector<Rec> recs;
    std::vector<std::shared_ptr<std::vector<uint8_t>>> storage;
    std::vector<uint8_t> swap;
    std::vector<std::string> src_file;  // 2.19: per item, the staged file of its bytes
    std::vector<uint64_t> src_off;
    std::set<uint32_t> excluded;  // 2.13: items left out of the table (a kit folder left whole after a clash)
};
TableBuilder::TableBuilder() : d_(new Impl) {}
TableBuilder::~TableBuilder() = default;
uint32_t TableBuilder::items() const { return uint32_t(d_->storage.size()); }
uint32_t TableBuilder::add_item(std::shared_ptr<std::vector<uint8_t>> data, bool swap, const std::string& src_file, uint64_t src_off) {
    d_->src_file.push_back(data ? src_file : std::string());
    d_->src_off.push_back(data ? src_off : 0);
    d_->storage.push_back(std::move(data));
    d_->swap.push_back(swap ? 1 : 0);
    return uint32_t(d_->storage.size() - 1);
}
bool TableBuilder::add_block(uint32_t item, uint32_t csize, uint32_t dsize, uint64_t head, uint64_t fnv, const uint8_t* data, std::string*) {
    if (item >= d_->storage.size() || !csize || !dsize) return false;
    if (!data && d_->storage[item]) return false;  // a real item needs its replacement bytes; a watch-only item has none
    Impl::Rec r;
    r.e.csize = csize;
    r.e.dsize = dsize;
    r.e.head = head;
    r.e.fnv = fnv;
    r.e.data = data;
    r.e.item = item;
    d_->recs.push_back(r);
    return true;
}
void TableBuilder::exclude_item(uint32_t item) { d_->excluded.insert(item); }
std::vector<PlanRecord> TableBuilder::records() const {
    std::vector<PlanRecord> out(d_->storage.size());
    for (size_t i = 0; i < out.size(); ++i) {
        out[i].swap = d_->swap[i] != 0;
        out[i].data = d_->storage[i];
        out[i].src_file = d_->src_file[i];
        out[i].src_off = d_->src_off[i];
    }
    for (const auto& r : d_->recs) {
        PlanBlock b;
        b.csize = r.e.csize;
        b.dsize = r.e.dsize;
        b.head = r.e.head;
        b.fnv = r.e.fnv;
        const auto& sp = d_->storage[r.e.item];
        if (r.e.data && sp) b.off = uint64_t(r.e.data - sp->data());
        out[r.e.item].blocks.push_back(b);
    }
    return out;
}
uint32_t TableBuilder::add_record(const PlanRecord& r) {
    const uint32_t id = add_item(r.data, r.swap, r.src_file, r.src_off);
    for (const PlanBlock& b : r.blocks) add_block(id, b.csize, b.dsize, b.head, b.fnv, r.data && b.off != ~0ULL ? r.data->data() + b.off : nullptr, nullptr);
    return id;
}

static uint64_t mix(uint32_t cs, uint32_t ds, uint64_t head) {
    uint64_t h = head * 0x9E3779B97F4A7C15ULL;
    h ^= (uint64_t(cs) << 32 | ds) * 0xC2B2AE3D27D4EB4FULL;
    h ^= h >> 29;
    return h;
}

std::shared_ptr<Table> TableBuilder::finish(std::vector<std::string>* dropped, std::set<uint32_t>* dropped_items, std::map<uint32_t, std::set<uint32_t>>* clash_with) {
    auto& recs = d_->recs;
    const auto& excluded = d_->excluded;
    // identical key: the same replacement bytes = one entry serving both items; different bytes = a conflict: every block of every item that
    // wants that block goes (a half-swapped texture is worse than none). finish() may run again after exclude_item(): nothing is kept between runs.
    for (auto& r : recs) r.dead = excluded.count(r.e.item) != 0;
    std::set<uint32_t> poisoned;
    if (clash_with) clash_with->clear();
    std::map<size_t, std::vector<uint32_t>> aliases;  // survivor record -> items of its byte-identical duplicates
    std::vector<size_t> order;
    for (size_t i = 0; i < recs.size(); ++i)
        if (!recs[i].dead) order.push_back(i);
    auto key = [&](size_t i) { return std::make_tuple(recs[i].e.csize, recs[i].e.dsize, recs[i].e.head, recs[i].e.fnv); };
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return key(a) < key(b) || (key(a) == key(b) && a < b); });
    for (size_t i = 0; i < order.size();) {
        size_t j = i + 1;
        while (j < order.size() && key(order[j]) == key(order[i])) ++j;
        // the survivor of a group of identical blocks: the first one that carries replacement bytes (a watch-only record never wins over a real one)
        size_t first = order[i];
        for (size_t k = i; k < j; ++k)
            if (recs[order[k]].e.data) { first = order[k]; break; }
        bool conflict = false;
        for (size_t k = i; k < j && !conflict; ++k) {
            const auto& other = recs[order[k]];
            conflict = other.e.data && recs[first].e.data && std::memcmp(recs[first].e.data, other.e.data, other.e.dsize) != 0;
        }
        if (conflict) {  // every real item of the group goes (also one whose bytes equal the survivor's: its block would be missing otherwise)
            std::set<uint32_t> group;
            for (size_t k = i; k < j; ++k)
                if (recs[order[k]].e.data) group.insert(recs[order[k]].e.item);
            for (uint32_t it : group) {
                poisoned.insert(it);
                if (clash_with)
                    for (uint32_t o : group)
                        if (o != it) (*clash_with)[it].insert(o);
            }
            size_t watch = order[i];  // the watch-only records of the group stay: one entry, every counter
            bool have = false;
            for (size_t k = i; k < j; ++k) {
                const size_t oi = order[k];
                if (recs[oi].e.data) continue;
                if (!have) { watch = oi; have = true; continue; }
                recs[oi].dead = true;
                if (recs[oi].e.item != recs[watch].e.item) aliases[watch].push_back(recs[oi].e.item);
            }
        } else {
            for (size_t k = i; k < j; ++k) {
                const size_t oi = order[k];
                if (oi == first) continue;
                recs[oi].dead = true;  // identical block, same replacement (or watch only): one entry, both counters
                if (recs[oi].e.item != recs[first].e.item) aliases[first].push_back(recs[oi].e.item);
            }
        }
        i = j;
    }
    if (dropped_items) *dropped_items = poisoned;
    if (dropped)
        for (uint32_t it : poisoned) dropped->push_back("item " + std::to_string(it) + ": the same compressed block is also wanted with other bytes");
    auto t = std::make_shared<Table>();
    t->n_items_ = uint32_t(d_->storage.size());
    t->alloc_counters();
    t->item_swap_ = d_->swap;
    t->storage_ = d_->storage;
    t->src_file_ = d_->src_file;
    t->src_off_ = d_->src_off;
    for (size_t ri = 0; ri < recs.size(); ++ri) {
        auto& r = recs[ri];
        if (r.dead || poisoned.count(r.e.item)) continue;
        Entry e = r.e;
        auto al = aliases.find(ri);
        if (al != aliases.end()) {
            e.alias_off = uint32_t(t->alias_pool_.size());
            for (uint32_t x : al->second)
                if (!poisoned.count(x)) t->alias_pool_.push_back(x);
            e.alias_n = uint32_t(t->alias_pool_.size()) - e.alias_off;
        }
        t->entries_.push_back(e);
        if (e.data) t->bytes_ += e.dsize;
        t->min_csize_ = std::min(t->min_csize_, e.csize);
        t->max_csize_ = std::max(t->max_csize_, e.csize);
    }
    t->index_slots();
    return t;
}

void Table::alloc_counters() {
    hits_.reset(new std::atomic<uint64_t>[n_items_ ? n_items_ : 1]);
    first_ms_.reset(new std::atomic<uint64_t>[n_items_ ? n_items_ : 1]);
    last_ms_.reset(new std::atomic<uint64_t>[n_items_ ? n_items_ : 1]);
    for (uint32_t i = 0; i < (n_items_ ? n_items_ : 1); ++i) {
        hits_[i].store(0);
        first_ms_[i].store(0);
        last_ms_[i].store(0);
    }
}

void Table::index_slots() {
    uint32_t cap = 16;
    while (cap < entries_.size() * 2 + 2) cap <<= 1;
    slots_.assign(cap, 0);
    mask_ = cap - 1;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        uint32_t s = uint32_t(mix(e.csize, e.dsize, e.head)) & mask_;
        while (slots_[s]) s = (s + 1) & mask_;
        slots_[s] = uint32_t(i + 1);
    }
}

bool Table::may_match(uint64_t srclen, uint64_t dstlen) const {
    return !entries_.empty() && srclen >= 8 && srclen >= min_csize_ && srclen <= max_csize_ && dstlen > 0 && dstlen <= 0x1000000;
}

const Entry* Table::match(const uint8_t* src, uint64_t srclen, uint64_t dstlen) const {
    if (!src || !may_match(srclen, dstlen)) return nullptr;
    uint64_t head;
    std::memcpy(&head, src, 8);
    uint32_t s = uint32_t(mix(uint32_t(srclen), uint32_t(dstlen), head)) & mask_;
    bool have_hash = false;
    uint64_t h = 0;
    for (uint32_t probes = 0; slots_[s] && probes <= mask_; s = (s + 1) & mask_, ++probes) {
        const Entry& e = entries_[slots_[s] - 1];
        if (e.csize != srclen || e.dsize != dstlen || e.head != head) continue;
        if (!have_hash) {
            h = fnv1a64(src, size_t(srclen));
            have_hash = true;
        }
        if (h == e.fnv) return &e;
    }
    return nullptr;
}

// ---------------------------------------------------------------- hotspots
namespace {
struct HsSlot {
    const char* key;
    int index;
    bool badge;  // team / brand: the zip's "not used" (all negative) is applied when decals follow the zip
    bool rot;    // the entry has a meaningful rotation (competition sleeve badges)
};
const HsSlot kHsSlots[] = {
    {"jersey/team", 0, true, false},          {"jersey/numberback", 1, false, false},   {"jersey/numberfront", 2, false, false},
    {"jersey/name", 3, false, false},         {"jersey/namearccenter", 4, false, false}, {"jersey/namearctangent", 5, false, false},
    {"jersey/brand", 7, true, false},         {"shorts/team", 11, true, false},          {"shorts/number", 12, false, false},
    {"shorts/brand", 13, true, false},        {"competition/leftsleevelong", 20, false, true}, {"competition/leftsleeveshort", 21, false, true},
    {"competition/rightsleevelong", 22, false, true}, {"competition/rightsleeveshort", 23, false, true}, {"competition/center", 24, false, true}};
constexpr size_t kHsData = 0x90, kHsStride = 0x30, kHsCount = 25;
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string f4(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.4f", double(v));
    return b;
}
}  // namespace

bool parse_hotspots(const std::vector<uint8_t>& bytes, HotspotMap& out, std::string* err) {
    out.clear();
    json j = json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("Hotspots") || !j["Hotspots"].is_array()) return fail(err, "hotspots.hotspot is not the expected JSON (Hotspots array missing)");
    for (const json& h : j["Hotspots"]) {
        try {
            if (!h.is_object() || !h.contains("Group") || !h.contains("Name") || !h.contains("Bounds")) continue;
            const json& b = h["Bounds"];
            HotspotRect r;
            r.x = b.at("X").get<float>();
            r.y = b.at("Y").get<float>();
            r.z = b.at("Z").get<float>();
            r.w = b.at("W").get<float>();
            r.rot = h.contains("Rotation") ? h["Rotation"].get<float>() : 0.f;
            out[lower(h["Group"].get<std::string>()) + "/" + lower(h["Name"].get<std::string>())] = r;
        } catch (const std::exception&) {
        }
    }
    return !out.empty() || fail(err, "hotspots.hotspot lists no usable hotspot");
}

bool patch_hotspot_ebx(std::vector<uint8_t>& ebx, const std::string& res_name, const HotspotMap& zip, bool decals, std::vector<std::string>* changes, std::string* why) {
    auto no = [&](const std::string& m) {
        if (why) *why = m;
        return false;
    };
    const size_t path_at = kHsData + kHsCount * kHsStride;
    if (ebx.size() < path_at + res_name.size() + 1) return no("the hotspot asset has another size than the known layout");
    if (std::memcmp(ebx.data(), "RIFF", 4) != 0 || std::memcmp(&ebx[8], "EBX\0EBXD", 8) != 0) return no("not an EBX container");
    if (le32(&ebx[0x8C]) != kHsCount) return no("the hotspot asset does not hold 25 entries");
    if (std::memcmp(&ebx[path_at], res_name.c_str(), res_name.size() + 1) != 0) return no("the asset's own path is not at the expected place");
    for (size_t i = 0; i < kHsCount; ++i)
        for (int k = 0; k < 4; ++k) {
            float f;
            std::memcpy(&f, &ebx[kHsData + i * kHsStride + size_t(k) * 4], 4);
            if (!std::isfinite(f)) return no("a game rectangle is not a finite number");
        }
    std::vector<uint8_t> w = ebx;
    std::vector<std::string> ch;
    for (const HsSlot& sl : kHsSlots) {
        auto it = zip.find(sl.key);
        if (it == zip.end()) continue;
        const HotspotRect& z = it->second;
        const float v[4] = {z.x, z.y, z.z, z.w};
        bool fin = true, neg = true;
        for (float f : v) {
            fin = fin && std::isfinite(f) && f > -2.f && f < 2.f;
            neg = neg && f < 0.f;
        }
        if (!fin || !std::isfinite(z.rot) || z.rot < -360.f || z.rot > 360.f) { ch.push_back(std::string(sl.key) + ": zip value out of range, game's kept"); continue; }
        uint8_t* e = &w[kHsData + size_t(sl.index) * kHsStride];
        float g[4];
        std::memcpy(g, e, 16);
        const bool goff = g[0] < 0.f && g[1] < 0.f && g[2] < 0.f && g[3] < 0.f;  // the game has this decal switched off for this kit
        if (goff && !neg) { ch.push_back(std::string(sl.key) + ": game has it off, kept"); continue; }
        if (neg) {
            if (!sl.badge) { ch.push_back(std::string(sl.key) + ": zip says 'not used', game's kept"); continue; }
            if (!decals) {
                ch.push_back(std::string(sl.key) + ": zip says 'not used' (its badge / sponsor is baked into the picture), game's kept: with the zip's decals off the game's own decal is drawn too"
                             " (a second badge / sponsor next to the baked one); tick 'Also use the zip's badge and brand pictures' for one");
                continue;
            }
        } else {
            const bool zpoint = std::fabs(z.x - z.z) < 1e-6f && std::fabs(z.y - z.w) < 1e-6f;
            const bool gpoint = std::fabs(g[0] - g[2]) < 1e-6f && std::fabs(g[1] - g[3]) < 1e-6f;
            if (zpoint && !gpoint) { ch.push_back(std::string(sl.key) + ": zip gives a point where the game has a rectangle, game's kept"); continue; }
        }
        float rot_old;
        std::memcpy(&rot_old, e + 0x20, 4);
        const bool same = std::memcmp(g, v, 16) == 0 && (!sl.rot || rot_old == z.rot);
        if (same) continue;
        std::memcpy(e, v, 16);
        if (sl.rot) std::memcpy(e + 0x20, &z.rot, 4);
        ch.push_back(std::string(sl.key) + " (" + f4(g[0]) + "," + f4(g[1]) + "," + f4(g[2]) + "," + f4(g[3]) + ") -> (" + f4(v[0]) + "," + f4(v[1]) + "," + f4(v[2]) + "," + f4(v[3]) + ")" +
                     (sl.rot ? " rot " + f4(rot_old) + " -> " + f4(z.rot) : std::string()));
    }
    ebx = std::move(w);
    if (changes) *changes = std::move(ch);
    return true;
}

// ---------------------------------------------------------------- plan
namespace {
struct Tex {
    std::string res, leaf, kind, part, role;
    TexHeader hdr;
    Loc loc;
    uint32_t logical = 0;
    bool in_slot = true;  // false: a texture of another kit folder of the club (only watched)
};
struct HsEbx {
    std::string res, kind;
    Loc loc;
};
bool is_hotspot_leaf(const std::string& leaf, int team) {
    const std::string pre = "hotspots_" + std::to_string(team) + "_";
    if (leaf.compare(0, pre.size(), pre) != 0) return false;
    int us = 0;
    for (size_t i = pre.size(); i < leaf.size(); ++i) {
        if (leaf[i] == '_') ++us;
        else if (leaf[i] < '0' || leaf[i] > '9') return false;
    }
    return us == 1 && leaf.back() != '_';
}
struct Cand {
    std::string note;  // swapped, with a caveat (shared with the club's other kits)
    uint32_t item;
    std::vector<Block> blocks;
    std::vector<uint64_t> fnv;
    std::vector<uint64_t> head;
    size_t plan_index;
    std::string own_key;
};
void add_user(std::vector<KitUser>& v, const KitUser& u) {
    for (const KitUser& x : v)
        if (x.team == u.team && x.kind == u.kind) return;
    v.push_back(u);
}
const char* kOtherClubsOption = "the option 'also change other clubs' kits that share this texture' serves it";
}  // namespace

bool kit_role(const std::string& role) { return role == "jersey_color" || role == "shorts_color" || role == "socks_color"; }
bool detail_role(const std::string& role) {
    for (const char* g : {"jersey_", "shorts_", "socks_"})
        for (const char* m : {"coeff", "normal", "taa"})
            if (role == std::string(g) + m) return true;
    return false;
}

std::string users_text(const std::vector<KitUser>& users) {
    std::string s;
    for (const KitUser& u : users) s += (s.empty() ? "" : ", ") + ("team " + std::to_string(u.team) + " " + u.kind);
    return s;
}

std::vector<uint32_t> leave_folder(Plan& plan, const std::string& kind, LeftBy why, const std::string& role, const std::string& reason,
                                   const std::vector<KitUser>& shared) {
    std::vector<uint32_t> ids;
    FolderVerdict* v = nullptr;
    for (auto& f : plan.folders)
        if (f.kind == kind) v = &f;
    if (!v) {
        plan.folders.push_back({});
        v = &plan.folders.back();
        v->team = plan.team;
        v->slot = plan.slot;
        v->kind = kind;
    }
    if (!v->swapped) return ids;  // already left: the first reason stays
    v->swapped = false;
    v->why = why;
    v->role = role;
    v->reason = reason;
    v->shared = shared;
    v->details_left = 0;
    const std::string with = "left with the rest of this kit (" + kind + "): its " + role + " stays the game's, never half a kit";
    for (PlanItem& it : plan.items) {
        if (it.probe || it.kind != kind || !it.swap) continue;
        if (it.table_item != 0xFFFFFFFFu) ids.push_back(it.table_item);
        it.swap = false;
        it.table_item = 0xFFFFFFFFu;
        it.left_by = why;
        it.reason = with;
    }
    plan.swapped = plan.skipped = 0;
    for (const PlanItem& it : plan.items)
        if (!it.probe) ++(it.swap ? plan.swapped : plan.skipped);
    return ids;
}

Plan build_plan(GameFiles& files, const OodleFn& oodle, const KitIndex& index, const StagedKit& staged, const Options& opt, bool swap, TableBuilder& tb,
                Progress* progress) {
    Plan plan;
    plan.team = staged.team;
    plan.slot = staged.slot;
    try {
        // 1. every texture of the club's kit bundles of that slot
        std::map<int, std::vector<std::vector<Loc>>> toc_cache;
        std::vector<Tex> texs;
        std::vector<HsEbx> hs_ebx;
        for (const KitBundle& kb : index.bundles) {
            const bool in_slot_bundle = staged.slot >= 0 && staged.slot < 32 && (kb.slot_mask & (1u << staged.slot));
            if (kb.team != staged.team || !(in_slot_bundle || (opt.watch_club && kb.slot_mask))) continue;
            if (progress && progress->cancel.load()) { plan.error = "cancelled"; return plan; }
            auto it = toc_cache.find(kb.toc);
            if (it == toc_cache.end()) {
                std::vector<uint8_t> toc;
                std::vector<std::vector<Loc>> b;
                if (!files.read_file(index.tocs[size_t(kb.toc)], toc) || !parse_toc_bundles(toc, b, nullptr)) {
                    plan.notes.push_back(index.tocs[size_t(kb.toc)] + ": cannot be read again");
                    continue;
                }
                it = toc_cache.emplace(kb.toc, std::move(b)).first;
            }
            if (kb.bundle >= it->second.size()) continue;
            const auto& locs = it->second[kb.bundle];
            std::vector<uint8_t> raw, blob;
            if (locs.empty() || !files.read_loc(locs[0], raw)) continue;
            Manifest m;
            if (raw.size() > 8 && le32(&raw[4]) == kManifestMagic) {
                if (!parse_manifest(raw, m, nullptr)) continue;
            } else if (!read_blocks(raw, oodle, blob, nullptr) || !parse_manifest(blob, m, nullptr)) {
                continue;
            }
            const size_t ne = m.ebx.size(), nr = m.res.size();
            if (locs.size() < 1 + ne + nr + m.chunks.size()) continue;
            for (size_t e = 0; e < ne; ++e) {
                const KitName kn = parse_kit_name(m.ebx[e]);
                if (!kn.ok || kn.team != staged.team || kind_slot(kn.kind) != staged.slot || !is_hotspot_leaf(kn.leaf, staged.team)) continue;
                hs_ebx.push_back({m.ebx[e], kn.kind, locs[1 + e]});
            }
            for (size_t r = 0; r < nr; ++r) {
                if (m.res[r].type != kTextureResType) continue;
                const KitName kn = parse_kit_name(m.res[r].name);
                if (!kn.ok || kn.team != staged.team) continue;
                const int rs = kind_slot(kn.kind);  // the slot is decided per texture (a bundle can name kit folders of several slots)
                const bool in_slot = rs == staged.slot;
                if (!in_slot && !(opt.watch_club && rs >= 0)) continue;
                Tex t;
                t.in_slot = in_slot;
                t.res = m.res[r].name;
                t.leaf = kn.leaf;
                t.kind = kn.kind;
                t.part = kn.part;
                t.role = role_of(kn.leaf, staged.team);
                std::vector<uint8_t> hraw, hdr;
                std::string e;
                if (!files.read_loc(locs[1 + ne + r], hraw) || !read_blocks(hraw, oodle, hdr, &e) || !parse_texture_header(hdr, t.hdr, &e)) {
                    plan.notes.push_back(t.res + ": header unreadable (" + e + ")");
                    continue;
                }
                size_t ci = m.chunks.size();
                for (size_t c = 0; c < m.chunks.size(); ++c)
                    if (m.chunks[c].guid == t.hdr.guid) ci = c;
                if (ci == m.chunks.size()) {
                    plan.notes.push_back(t.res + ": its chunk GUID is not in the bundle");
                    continue;
                }
                t.loc = locs[1 + ne + nr + ci];
                t.logical = m.chunks[ci].logical;
                texs.push_back(std::move(t));
            }
        }
        std::sort(texs.begin(), texs.end(), [](const Tex& a, const Tex& b) { return std::tie(a.kind, a.part, a.leaf) < std::tie(b.kind, b.part, b.leaf); });
        bool any_in_slot = false;
        for (const Tex& t : texs) any_in_slot = any_in_slot || t.in_slot;
        if (!any_in_slot) {
            plan.error = kNothingToSwap;
            return plan;
        }
        // 2. check each against its staged texture and the sharing rules
        std::map<std::string, const StagedTexture*> by_role;
        for (const auto& s : staged.textures) by_role[s.role] = &s;
        std::vector<Cand> cands;
        std::map<size_t, std::shared_ptr<std::vector<uint8_t>>> data_of;
        std::map<size_t, std::string> file_of;  // 2.19: the staged file of those bytes (they start at kDdsHeader in it)
        std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> role_data;  // one copy of a staged picture's data per role
        std::set<std::string> seen_tex;                                          // the same resource at the same place, listed by several bundles
        for (const Tex& t : texs) {
            if (!t.in_slot) continue;
            if (!seen_tex.insert(t.res + "@" + phys_key(t.loc)).second) continue;
            PlanItem it;
            it.res = t.res;
            it.kind = t.kind;
            it.role = t.role;
            auto skip = [&](const std::string& why) {
                it.swap = false;
                it.reason = why;
                if (it.left_by == LeftBy::None) it.left_by = LeftBy::Other;
                plan.items.push_back(it);
                ++plan.skipped;
            };
            if (t.role.empty()) { skip("not a texture the zip carries"); continue; }
            auto sr = by_role.find(t.role);
            if (sr == by_role.end()) { skip("not in the zip: the game's own texture stays"); continue; }
            const bool decal = t.role.compare(0, 5, "brand") == 0 || t.role.compare(0, 5, "crest") == 0;
            if (decal && !opt.include_decals) { skip("decals are left to the game"); continue; }
            if (t.role.compare(0, 5, "crest") == 0 && opt.keep_game_crest) { skip("the game's own club badge stays (the zip's crest picture is not served)"); continue; }
            const StagedTexture& st = *sr->second;
            const uint32_t dxgi = dxgi_of_format(t.hdr.fmt_id);
            if (!dxgi) { skip("game format id " + std::to_string(t.hdr.fmt_id) + " is not one Turbo converts"); continue; }
            if (st.dxgi != dxgi || st.w != t.hdr.w || st.h != t.hdr.h || st.mips != t.hdr.mips) {
                skip("the staged texture (dxgi " + std::to_string(st.dxgi) + ", " + std::to_string(st.w) + "x" + std::to_string(st.h) + ", " + std::to_string(st.mips) +
                     " mips) is not the game's (" + std::to_string(dxgi) + ", " + std::to_string(t.hdr.w) + "x" + std::to_string(t.hdr.h) + ", " + std::to_string(t.hdr.mips) + " mips)");
                continue;
            }
            const std::vector<uint32_t> expect = bc_mip_sizes(dxgi, t.hdr.w, t.hdr.h, t.hdr.mips);
            uint64_t sum = 0;
            for (uint32_t x : expect) sum += x;
            if (expect != t.hdr.mip_sizes || sum != t.logical || st.bytes != kDdsHeader + sum) { skip("the mip chain size differs from the game's chunk"); continue; }
            // sharing: the users of this physical chunk. This club's same slot: fine; this club's other slots: share_within_club;
            // other clubs: share_other_clubs (2.13); a kit folder of this club that is no slot: never
            const auto us = index.users.find(phys_key(t.loc));
            std::string unknown;
            std::set<std::string> club_kinds;  // this club's OTHER kit slots that use the same chunk
            std::vector<KitUser> others;       // other clubs' kits on the same chunk
            if (us != index.users.end())
                for (const KitUser& u : us->second) {
                    if (u.team != staged.team) add_user(others, u);
                    else if (u.slot < 0) { if (unknown.empty()) unknown = "team " + std::to_string(u.team) + " " + u.kind; }
                    else if (u.slot != staged.slot) { club_kinds.insert(u.kind); add_user(it.shared, u); }
                }
            for (const KitUser& u : others) add_user(it.shared, u);
            if (!unknown.empty()) { skip("the chunk is also used by " + unknown + ": left to the game"); continue; }
            if (!others.empty() && !opt.share_other_clubs) {
                it.left_by = LeftBy::OtherClubs;
                skip("the chunk is also used by " + users_text(others) + ": left to the game (" + kOtherClubsOption + ")");
                continue;
            }
            std::string club_note;
            if (!club_kinds.empty()) {
                std::string list;
                for (const auto& k : club_kinds) list += (list.empty() ? "" : ", ") + k;
                if (!opt.share_within_club) {
                    it.left_by = LeftBy::ThisClub;
                    skip("the chunk is also used by this club's " + list + ": left to the game (the option 'also change this club's other kits that share a texture' serves it)");
                    continue;
                }
                club_note = "shared with this club's " + list + ": those kits show it too";
            }
            if (!others.empty()) club_note += (club_note.empty() ? "" : "; ") + ("also changes " + users_text(others) + " (the same chunk)");
            // blocks
            std::vector<uint8_t> chunk;
            if (!files.read_loc(t.loc, chunk)) { skip("the chunk cannot be read"); continue; }
            Cand c;
            if (!block_headers(chunk, c.blocks) || c.blocks.empty()) { skip("the chunk's block stream is not readable"); continue; }
            uint64_t raw_sum = 0;
            bool ok = true;
            for (const Block& b : c.blocks) {
                raw_sum += b.dsize;
                if (b.ctype != 0x19 && b.ctype != 0x15 && b.ctype != 0x11) { ok = false; it.reason = "stored (uncompressed) block: the decompress hook never sees it"; }
                else if (b.csize < opt.min_csize && !(opt.census && b.csize >= opt.min_csize_unique)) {  // smaller ones only when the census proves them unique
                    ok = false;
                    it.reason = "compressed block of " + std::to_string(b.csize) + " bytes is uniform fill, too small to recognise safely";
                }
            }
            if (ok && raw_sum != t.logical) { ok = false; it.reason = "block sizes do not add up to the chunk"; }
            if (!ok) { skip(it.reason); continue; }
            for (const Block& b : c.blocks) {
                c.fnv.push_back(fnv1a64(&chunk[size_t(b.payload)], b.csize));
                uint64_t h8 = 0;
                std::memcpy(&h8, &chunk[size_t(b.payload)], 8);
                c.head.push_back(h8);
            }
            // the staged data (read and checked once per role)
            auto rd = role_data.find(t.role);
            if (rd == role_data.end()) {
                std::vector<uint8_t> file;
                {
                    std::ifstream f(st.file, std::ios::binary);
                    file.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
                }
                std::shared_ptr<std::vector<uint8_t>> data;
                if (file.size() == st.bytes && file.size() >= kDdsHeader && std::memcmp(file.data(), "DDS ", 4) == 0 && le32(&file[128]) == dxgi &&
                    int(le32(&file[12])) == t.hdr.h && int(le32(&file[16])) == t.hdr.w && int(le32(&file[28])) == t.hdr.mips)
                    data = std::make_shared<std::vector<uint8_t>>(file.begin() + std::ptrdiff_t(kDdsHeader), file.end());
                rd = role_data.emplace(t.role, data).first;
            }
            if (!rd->second) {
                skip("the staged DDS file does not match its manifest");
                continue;
            }
            it.swap = true;
            it.blocks = int(c.blocks.size());
            it.bytes = sum;
            c.plan_index = plan.items.size();
            c.own_key = phys_key(t.loc);
            c.note = club_note;
            it.reason = club_note;
            plan.items.push_back(it);
            data_of[c.plan_index] = rd->second;
            file_of[c.plan_index] = st.file.u8string();
            cands.push_back(std::move(c));
        }
        // 3. the block census: an OTHER kit chunk that holds a byte-identical compressed block would show the replacement too. Such a
        //    texture stays the game's, except (2.13, share_other_clubs) when every such chunk belongs to other clubs: then it is served and
        //    the note names every kit that changes with it ("also changes team 2 goalie_home_2_0 (byte-identical blocks: ...)").
        std::set<size_t> bad;  // census: candidates that stay the game's
        if (opt.census && !cands.empty()) {
            std::map<std::pair<uint32_t, uint32_t>, std::vector<std::pair<size_t, size_t>>> want;  // (csize,dsize) -> (cand, block)
            std::set<std::string> own;
            for (size_t ci = 0; ci < cands.size(); ++ci) {
                own.insert(cands[ci].own_key);
                for (size_t bi = 0; bi < cands[ci].blocks.size(); ++bi) want[{cands[ci].blocks[bi].csize, cands[ci].blocks[bi].dsize}].push_back({ci, bi});
            }
            uint32_t min_c = 0xFFFFFFFFu;
            for (const auto& w : want) min_c = std::min(min_c, w.first.first);
            // per candidate: the other kit chunks holding one of its blocks -> which of its blocks they hold
            std::map<size_t, std::map<std::string, std::set<size_t>>> hit;
            size_t scanned = 0;
            std::vector<uint8_t> hdr, pay;
            for (const auto& kv : index.users) {
                if (progress && progress->cancel.load()) { plan.error = "cancelled"; return plan; }
                if (own.count(kv.first)) continue;
                Loc l;
                if (!parse_phys_key(kv.first, l) || l.size < 8 + min_c) continue;
                ++scanned;
                uint64_t p = 0;
                while (p + 8 <= l.size) {
                    if (!files.read(l.cas, l.off + p, 8, hdr)) break;
                    const uint32_t ds = be32(hdr.data()) & 0xFFFFFFu;
                    const uint16_t code = le16(&hdr[4]);
                    const uint32_t cs = uint32_t(be16(&hdr[6])) + (uint32_t((code >> 8) & 0x0F) << 16);
                    if (p + 8 + cs > l.size) break;
                    auto w = want.find({cs, ds});
                    if (w != want.end() && files.read(l.cas, l.off + p + 8, cs, pay)) {
                        const uint64_t f = fnv1a64(pay.data(), pay.size());
                        for (const auto& cb : w->second)
                            if (cands[cb.first].fnv[cb.second] == f) hit[cb.first][kv.first].insert(cb.second);
                    }
                    p += 8 + cs;
                }
            }
            plan.notes.push_back("block census: " + std::to_string(scanned) + " other kit chunks checked");
            for (const auto& h : hit) {
                const size_t ci = h.first;
                PlanItem& it = plan.items[cands[ci].plan_index];
                std::vector<KitUser> others, club;
                bool unknown = false;
                std::string counts;
                for (const auto& ch : h.second) {
                    const auto us = index.users.find(ch.first);
                    if (us == index.users.end() || us->second.empty()) {
                        unknown = true;
                        continue;
                    }
                    for (const KitUser& u : us->second) {
                        if (u.team != staged.team) add_user(others, u);
                        else add_user(club, u);
                    }
                    counts += (counts.empty() ? "" : ", ") + ("team " + std::to_string(us->second[0].team) + " " + us->second[0].kind + " " + std::to_string(ch.second.size()) +
                                                              " of " + std::to_string(cands[ci].blocks.size()));
                }
                for (const KitUser& u : others) add_user(it.shared, u);
                for (const KitUser& u : club) add_user(it.shared, u);
                if (!club.empty() || unknown) {  // this club's own kit data: a patched fragment is never made (as before 2.13)
                    std::vector<KitUser> all = club;
                    for (const KitUser& u : others) add_user(all, u);
                    bad.insert(ci);
                    it.left_by = LeftBy::ThisClubData;
                    it.reason = "a byte-identical compressed block is also in another kit chunk (" + (all.empty() ? std::string("a chunk the index does not name") : users_text(all)) + ")";
                } else if (!opt.share_other_clubs) {
                    bad.insert(ci);
                    it.left_by = LeftBy::OtherClubs;
                    it.reason = "a byte-identical compressed block is also in another club's kit chunk (" + users_text(others) + "): left to the game (" + kOtherClubsOption + ")";
                } else {
                    cands[ci].note += (cands[ci].note.empty() ? "" : "; ") + ("also changes " + users_text(others) + " (byte-identical blocks: " + counts + ")");
                }
            }
            for (size_t ci : bad) plan.items[cands[ci].plan_index].swap = false;
        }
        for (size_t ci = 0; ci < cands.size(); ++ci)
            if (!bad.count(ci)) plan.items[cands[ci].plan_index].reason = cands[ci].note;
        // 3b. never half a kit (2.13): every kit folder of the slot is shown whole, the zip's or the game's. A folder whose jersey, shorts or
        //     socks picture is not served stays the game's: jersey, shorts, socks, brand and hotspots. The deciding picture is the one with
        //     the hardest reason (one no option fixes before one an option fixes), the first in jersey, shorts, socks order among equals.
        auto hardness = [](LeftBy w) { return w == LeftBy::OtherClubs ? 1 : w == LeftBy::ThisClub ? 2 : 3; };
        for (const PlanItem& it : plan.items) {
            if (it.probe || std::any_of(plan.folders.begin(), plan.folders.end(), [&](const FolderVerdict& f) { return f.kind == it.kind; })) continue;
            FolderVerdict v;
            v.team = plan.team;
            v.slot = plan.slot;
            v.kind = it.kind;
            plan.folders.push_back(v);
        }
        std::sort(plan.folders.begin(), plan.folders.end(), [](const FolderVerdict& a, const FolderVerdict& b) {
            const bool la = a.kind.compare(0, 5, "loc2_") == 0, lb = b.kind.compare(0, 5, "loc2_") == 0;
            return la != lb ? lb : a.kind < b.kind;
        });
        std::vector<std::string> kinds;
        for (const auto& f : plan.folders) kinds.push_back(f.kind);
        for (const std::string& kind : kinds) {
            const PlanItem* by = nullptr;
            for (const PlanItem& it : plan.items)
                if (!it.probe && it.kind == kind && kit_role(it.role) && !it.swap && (!by || hardness(it.left_by) > hardness(by->left_by))) by = &it;
            if (by) {
                const PlanItem d = *by;  // leave_folder rewrites the items
                leave_folder(plan, kind, d.left_by, d.role, d.reason, d.shared);
            }
        }
        // ... and a served texture whose blocks are the same as a picture of a folder left whole would change that folder through the hook
        // (it recognises blocks by content): it stays the game's too, and when it is a jersey / shorts / socks picture so does its folder
        for (bool again = true; again;) {
            again = false;
            std::map<std::tuple<uint32_t, uint32_t, uint64_t>, std::string> guard;  // blocks of the pictures of left folders -> "folder role"
            for (const Cand& c : cands) {
                const PlanItem& it = plan.items[c.plan_index];
                const bool left_folder = std::any_of(plan.folders.begin(), plan.folders.end(), [&](const FolderVerdict& f) { return f.kind == it.kind && !f.swapped; });
                if (!left_folder || !kit_role(it.role)) continue;
                for (size_t b = 0; b < c.blocks.size(); ++b) guard.emplace(std::make_tuple(c.blocks[b].csize, c.blocks[b].dsize, c.fnv[b]), it.kind + " " + it.role);
            }
            for (const Cand& c : cands) {
                PlanItem& it = plan.items[c.plan_index];
                if (!it.swap) continue;
                std::string same;
                for (size_t b = 0; b < c.blocks.size() && same.empty(); ++b) {
                    auto g = guard.find(std::make_tuple(c.blocks[b].csize, c.blocks[b].dsize, c.fnv[b]));
                    if (g != guard.end()) same = g->second;
                }
                if (same.empty()) continue;
                it.swap = false;
                it.left_by = LeftBy::ThisClubData;
                it.reason = "its data is the same as " + same + ", which stays the game's (the hook would change that kit too)";
                if (kit_role(it.role)) {
                    const PlanItem d = it;
                    leave_folder(plan, d.kind, d.left_by, d.role, d.reason, d.shared);
                }
                again = true;
            }
        }
        // 4. the table
        for (Cand& c : cands) {
            PlanItem& it = plan.items[c.plan_index];
            if (!it.swap) continue;
            const uint32_t id = tb.add_item(data_of[c.plan_index], swap, file_of[c.plan_index], kDdsHeader);
            uint64_t at = 0;
            const uint8_t* base = data_of[c.plan_index]->data();
            for (size_t b = 0; b < c.blocks.size(); ++b) {
                tb.add_block(id, c.blocks[b].csize, c.blocks[b].dsize, c.head[b], c.fnv[b], base + at, nullptr);
                at += c.blocks[b].dsize;
            }
            it.table_item = id;
        }
        // 5. the hotspot asset(s): the zip's name / number / sleeve-badge placement
        if (!swap && opt.apply_hotspots && !staged.hotspots.empty()) {
            plan.notes.push_back("hotspots: not applied (watch only): the game's name and number positions stay");
        } else if (opt.apply_hotspots && !staged.hotspots.empty()) {
            HotspotMap zip;
            std::string e;
            std::vector<uint8_t> jb;
            {
                std::ifstream f(staged.hotspots, std::ios::binary);
                jb.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            }
            if (!parse_hotspots(jb, zip, &e)) {
                plan.notes.push_back("hotspots: " + e + " (the game's name and number positions stay)");
            } else {
                std::set<std::string> seen_hs;
                for (const HsEbx& h : hs_ebx) {
                    if (!seen_hs.insert(h.res + "@" + phys_key(h.loc)).second) continue;
                    PlanItem it;
                    it.res = h.res;
                    it.kind = h.kind;
                    it.role = "hotspots";
                    auto skip = [&](const std::string& why) {
                        it.swap = false;
                        it.reason = why;
                        if (it.left_by == LeftBy::None) it.left_by = LeftBy::Other;
                        plan.items.push_back(it);
                        ++plan.skipped;
                    };
                    const auto fv = std::find_if(plan.folders.begin(), plan.folders.end(), [&](const FolderVerdict& f) { return f.kind == h.kind; });
                    if (fv != plan.folders.end() && !fv->swapped) {
                        it.left_by = fv->why;
                        skip("left with the rest of this kit (" + h.kind + "): its " + fv->role + " stays the game's, never half a kit");
                        continue;
                    }
                    std::vector<uint8_t> raw, dec;
                    std::vector<Block> bl;
                    if (!files.read_loc(h.loc, raw) || !block_headers(raw, bl)) { skip("the hotspot asset cannot be read"); continue; }
                    if (bl.size() != 1 || (bl[0].ctype != 0x19 && bl[0].ctype != 0x15 && bl[0].ctype != 0x11)) { skip("the hotspot asset is not one Oodle block: left to the game"); continue; }
                    if (bl[0].csize < kMinEbxCsize) { skip("the hotspot asset's block is too small to recognise safely"); continue; }
                    if (!read_blocks(raw, oodle, dec, &e) || dec.size() != bl[0].dsize) { skip("the hotspot asset does not decompress (" + e + ")"); continue; }
                    std::vector<std::string> ch;
                    std::string why;
                    if (!patch_hotspot_ebx(dec, h.res, zip, opt.include_decals, &ch, &why)) { skip(why + ": left to the game"); continue; }
                    int n = 0;
                    std::string txt;
                    for (const auto& c : ch) {
                        if (c.find(" -> ") != std::string::npos) ++n;
                        txt += (txt.empty() ? "" : "; ") + c;
                    }
                    if (n == 0) { skip("the zip's hotspots equal the game's: nothing to change" + (txt.empty() ? std::string() : " (" + txt + ")")); continue; }
                    auto data = std::make_shared<std::vector<uint8_t>>(std::move(dec));
                    uint64_t h8 = 0;
                    std::memcpy(&h8, &raw[size_t(bl[0].payload)], 8);
                    const uint32_t id = tb.add_item(data, swap);
                    tb.add_block(id, bl[0].csize, bl[0].dsize, h8, fnv1a64(&raw[size_t(bl[0].payload)], bl[0].csize), data->data(), nullptr);
                    it.swap = true;
                    it.blocks = 1;
                    it.bytes = bl[0].dsize;
                    it.table_item = id;
                    it.reason = std::to_string(n) + " hotspot rectangle(s) patched: " + txt;
                    plan.items.push_back(it);
                    ++plan.swapped;
                }
                if (hs_ebx.empty()) plan.notes.push_back("hotspots: the game has no hotspot asset for this kit's slot");
            }
        } else if (!staged.hotspots.empty() && !opt.apply_hotspots) {
            plan.notes.push_back("hotspots: not applied (option off): the game's name and number positions stay");
        }
        // 6. watch only: the club's other kit folders (and this slot's textures the zip does not carry), counted when the game loads them
        if (opt.watch_club) {
            static const char* kProbeRoles[] = {"jersey_color", "jersey_normal", "jersey_taa", "jersey_coeff", "shorts_color", "shorts_normal", "socks_color", "socks_normal"};
            std::set<std::string> done;
            for (const Cand& c : cands) done.insert(c.own_key);
            for (const Tex& t : texs) {
                if (progress && progress->cancel.load()) { plan.error = "cancelled"; return plan; }
                bool want = false;
                for (const char* r : kProbeRoles) want = want || t.role == r;
                const std::string key = phys_key(t.loc);
                if (!want || done.count(key)) continue;
                const auto us = index.users.find(key);
                bool exclusive = us != index.users.end();
                std::set<std::string> kinds;
                if (exclusive)
                    for (const KitUser& u : us->second) {
                        exclusive = exclusive && u.team == staged.team;
                        kinds.insert(u.kind);
                    }
                if (!exclusive) continue;  // shared with another club: a hit would say nothing about this club
                std::vector<uint8_t> chunk;
                std::vector<Block> blocks;
                if (!files.read_loc(t.loc, chunk) || !block_headers(chunk, blocks) || blocks.empty()) continue;
                done.insert(key);
                std::vector<const Block*> usable;
                for (const Block& b : blocks)
                    if ((b.ctype == 0x19 || b.ctype == 0x15 || b.ctype == 0x11) && b.csize >= opt.min_csize) usable.push_back(&b);
                if (usable.empty()) continue;
                const uint32_t id = tb.add_item(nullptr, false);
                for (const Block* b : usable) {
                    uint64_t h8 = 0;
                    std::memcpy(&h8, &chunk[size_t(b->payload)], 8);
                    tb.add_block(id, b->csize, b->dsize, h8, fnv1a64(&chunk[size_t(b->payload)], b->csize), nullptr, nullptr);
                }
                PlanItem pi;
                pi.res = t.res;
                std::string joined;
                for (const auto& k : kinds) joined += (joined.empty() ? "" : "+") + k;
                pi.kind = joined;
                pi.role = t.role;
                pi.probe = true;
                pi.swap = false;
                pi.blocks = int(usable.size());
                pi.table_item = id;
                pi.reason = "watch only: the game's own texture, counted when a match loads it";
                plan.items.push_back(pi);
                ++plan.probes;
            }
        }
        // the counts, and per served folder: the other kits that change with it, the detail maps that stay the game's
        plan.swapped = plan.skipped = 0;
        for (const PlanItem& it : plan.items)
            if (!it.probe) ++(it.swap ? plan.swapped : plan.skipped);
        for (FolderVerdict& f : plan.folders) {
            if (!f.swapped) continue;
            for (const PlanItem& it : plan.items) {
                if (it.probe || it.kind != f.kind) continue;
                if (it.swap)
                    for (const KitUser& u : it.shared)
                        if (u.team != plan.team || u.slot != plan.slot) add_user(f.shared, u);
                if (!it.swap && detail_role(it.role)) ++f.details_left;
            }
        }
        // item ids are the builder's: keep the plan's own numbering out of the table (hits are looked up by the Runtime)
        plan.ok = true;
    } catch (const std::exception& e) {
        plan.ok = false;
        plan.error = std::string("plan failed: ") + e.what();
    }
    return plan;
}

// ---------------------------------------------------------------- 2.19: the finished table on disk (kit_table.cache)
namespace {
constexpr uint32_t kTableMagic = 0x43544B54;  // "TKTC"
std::string hex64(uint64_t v) {
    char b[20];
    std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(v));
    return b;
}
// FNV-1a 64 of a whole file, "missing" when it cannot be read
std::string file_hash(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "missing";
    std::vector<char> buf(1 << 16);
    uint64_t h = 0xCBF29CE484222325ULL;
    while (f) {
        f.read(buf.data(), std::streamsize(buf.size()));
        const std::streamsize n = f.gcount();
        if (n > 0) h = fnv1a64(reinterpret_cast<const uint8_t*>(buf.data()), size_t(n), h);
    }
    return hex64(h);
}
void put_users(Wr& w, const std::vector<KitUser>& us) {
    w.u32(uint32_t(us.size()));
    for (const auto& u : us) {
        w.i32(u.team);
        w.i32(u.slot);
        w.str(u.kind);
    }
}
bool get_users(Rd& r, std::vector<KitUser>& us) {
    const uint32_t n = r.u32();
    if (!r.ok || n > 100000) return false;
    us.clear();
    for (uint32_t i = 0; i < n && r.ok; ++i) {
        KitUser u;
        u.team = r.i32();
        u.slot = r.i32();
        u.kind = r.str();
        us.push_back(std::move(u));
    }
    return r.ok;
}
}  // namespace

TableCacheKey make_table_cache_key(const std::string& turbo_version, const std::string& game_build, const std::string& index_sig, const std::vector<SlotState>& slots,
                                   const std::vector<fs::path>& staged_dirs, const Options& o) {
    TableCacheKey k;
    k.add("format", std::to_string(kTableCacheFormat));
    k.add("turbo version", turbo_version);
    k.add("game build", game_build);
    k.add("kit index (the game's tocs)", index_sig);
    std::ostringstream op;
    op << "decals " << o.include_decals << " crest " << o.keep_game_crest << " share_club " << o.share_within_club << " share_other " << o.share_other_clubs << " watch "
       << o.watch_club << " min_csize " << o.min_csize << " unique " << o.min_csize_unique << " census " << o.census << " hotspots " << o.apply_hotspots;
    k.add("options", op.str());
    std::ostringstream sl;
    for (const SlotState& s : slots)
        sl << s.team << ":" << s.slot << ":" << mode_name(s.mode) << ":" << s.decals << s.keep_crest << s.share_club << s.share_other << ";";
    k.add("kits (swap_state)", sl.str());
    for (size_t i = 0; i < slots.size(); ++i) {
        const fs::path dir = i < staged_dirs.size() ? staged_dirs[i] : fs::path();
        std::string v = "manifest " + file_hash(dir / "manifest.json") + " hotspots " + file_hash(dir / "hotspots.hotspot");
        std::vector<std::string> files;
        std::error_code ec;
        if (fs::is_directory(dir, ec)) {
            for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                if (!it->is_regular_file(e2)) continue;
                const auto sz = fs::file_size(it->path(), e2);
                const auto tm = fs::last_write_time(it->path(), e2);
                files.push_back(fs::relative(it->path(), dir, e2).generic_u8string() + ":" + (e2 ? std::string("?") : std::to_string(sz) + ":" + std::to_string(tm.time_since_epoch().count())));
            }
        } else {
            files.push_back("no folder");
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) v += "|" + f;
        k.add("staged team " + std::to_string(slots[i].team) + " slot " + std::to_string(slots[i].slot), v);
    }
    return k;
}

struct TableCacheIO {
    static bool save(const fs::path& file, const TableCacheKey& key, const Table& t, const std::string& message, const std::vector<ItemStatus>& items,
                     const std::vector<FolderVerdict>& folders) {
        Wr w;
        w.u32(kTableMagic);
        w.u32(kTableCacheFormat);
        w.u32(uint32_t(key.fields.size()));
        for (const auto& f : key.fields) {
            w.str(f.first);
            w.str(f.second);
        }
        // the replacement data: one blob per distinct buffer (the items of a plan share one per picture); a staged DDS is referenced (file,
        // offset, size, hash of the bytes served), a patched hotspot asset is stored inline
        std::map<const std::vector<uint8_t>*, uint32_t> blob_of;
        std::vector<uint32_t> item_blob(t.n_items_, 0xFFFFFFFFu);
        Wr blobs;
        uint32_t nblobs = 0;
        for (uint32_t i = 0; i < t.n_items_; ++i) {
            const std::shared_ptr<std::vector<uint8_t>> sp = i < t.storage_.size() ? t.storage_[i] : nullptr;
            if (!sp) continue;
            auto it = blob_of.find(sp.get());
            if (it == blob_of.end()) {
                const std::string src = i < t.src_file_.size() ? t.src_file_[i] : std::string();
                const uint64_t off = i < t.src_off_.size() ? t.src_off_[i] : 0;
                if (src.empty()) {
                    if (sp->size() > (1u << 20)) return false;  // only small in-memory data (hotspot assets) is stored inline
                    blobs.u8(1);
                    blobs.u32(uint32_t(sp->size()));
                    blobs.b.insert(blobs.b.end(), sp->begin(), sp->end());
                } else {
                    blobs.u8(2);
                    // a staged file under the cache's folder (turbo_output\kits) is named relative to it ("10/home/jersey_color.dds")
                    const fs::path rel = fs::u8path(src).lexically_relative(file.parent_path());
                    const std::string rs = rel.generic_u8string();
                    blobs.str(!rel.empty() && rel.is_relative() && rs.compare(0, 2, "..") != 0 ? "rel:" + rs : src);
                    blobs.u64(off);
                    blobs.u64(sp->size());
                    blobs.u64(fnv1a64(sp->data(), sp->size()));
                }
                it = blob_of.emplace(sp.get(), nblobs++).first;
            }
            item_blob[i] = it->second;
        }
        w.u32(nblobs);
        w.b.insert(w.b.end(), blobs.b.begin(), blobs.b.end());
        w.u32(t.n_items_);
        for (uint32_t i = 0; i < t.n_items_; ++i) {
            w.u32(item_blob[i]);
            w.u8(i < t.item_swap_.size() ? t.item_swap_[i] : 0);
        }
        w.u32(uint32_t(t.entries_.size()));
        for (const Entry& e : t.entries_) {
            w.u32(e.csize);
            w.u32(e.dsize);
            w.u64(e.head);
            w.u64(e.fnv);
            w.u32(e.item);
            w.u32(e.alias_off);
            w.u32(e.alias_n);
            uint64_t off = 0xFFFFFFFFFFFFFFFFULL;
            if (e.data) {
                const std::shared_ptr<std::vector<uint8_t>> sp = e.item < t.storage_.size() ? t.storage_[e.item] : nullptr;
                if (!sp || e.data < sp->data() || e.data + e.dsize > sp->data() + sp->size()) return false;  // never a pointer the file cannot name
                off = uint64_t(e.data - sp->data());
            }
            w.u64(off);
        }
        w.u32(uint32_t(t.alias_pool_.size()));
        for (uint32_t a : t.alias_pool_) w.u32(a);
        w.u64(t.bytes_);
        w.u32(t.min_csize_);
        w.u32(t.max_csize_);
        // what the UI and the log show
        w.str(message);
        w.u32(uint32_t(items.size()));
        for (const auto& it : items) {
            w.i32(it.team);
            w.i32(it.slot);
            w.str(it.res);
            w.str(it.kind);
            w.str(it.role);
            w.str(it.reason);
            w.u8(it.swap);
            w.u8(it.probe);
            w.i32(it.copies);
            w.i32(it.blocks);
            w.u32(it.table_item);
            w.u8(uint8_t(it.left_by));
            put_users(w, it.shared);
        }
        w.u32(uint32_t(folders.size()));
        for (const auto& f : folders) {
            w.i32(f.team);
            w.i32(f.slot);
            w.str(f.kind);
            w.u8(f.swapped);
            w.u8(uint8_t(f.why));
            w.str(f.role);
            w.str(f.reason);
            put_users(w, f.shared);
            w.i32(f.details_left);
        }
        w.u64(fnv1a64(w.b.data(), w.b.size()));  // the whole file's checksum
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        const fs::path tmp = file.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(w.b.data()), std::streamsize(w.b.size()));
            if (!f) return false;
        }
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
        return !ec;
    }

    static bool load(const fs::path& file, const TableCacheKey& key, CachedBuild& out, std::string* why, const std::atomic<bool>* cancel) {
        auto no = [&](const std::string& w) {
            if (why) *why = w;
            return false;
        };
        std::vector<uint8_t> d;
        {
            std::ifstream f(file, std::ios::binary | std::ios::ate);
            if (!f) return no("no cache file");
            const std::streamoff n = f.tellg();
            if (n < 24 || n > (std::streamoff(1) << 30)) return no("damaged (size)");
            d.resize(size_t(n));
            f.seekg(0);
            f.read(reinterpret_cast<char*>(d.data()), n);
            if (!f) return no("damaged (read)");
        }
        uint64_t sum = 0;
        std::memcpy(&sum, &d[d.size() - 8], 8);
        if (fnv1a64(d.data(), d.size() - 8) != sum) return no("damaged (checksum)");
        d.resize(d.size() - 8);
        Rd r(d);
        if (r.u32() != kTableMagic || !r.ok) return no("damaged (not a table cache)");
        const uint32_t fmt = r.u32();
        if (fmt != kTableCacheFormat) return no("key changed: format (" + std::to_string(fmt) + " -> " + std::to_string(kTableCacheFormat) + ")");
        const uint32_t nf = r.u32();
        if (!r.ok || nf > 100000) return no("damaged (key)");
        std::vector<std::pair<std::string, std::string>> fields;
        for (uint32_t i = 0; i < nf && r.ok; ++i) {
            std::string a = r.str();
            std::string b = r.str();
            fields.emplace_back(std::move(a), std::move(b));
        }
        if (!r.ok) return no("damaged (key)");
        for (size_t i = 0; i < std::max(fields.size(), key.fields.size()); ++i) {
            if (i >= fields.size()) return no("key changed: " + key.fields[i].first + " (new)");
            if (i >= key.fields.size()) return no("key changed: " + fields[i].first + " (gone)");
            if (fields[i].first != key.fields[i].first) return no("key changed: " + key.fields[i].first + " (was " + fields[i].first + ")");
            if (fields[i].second != key.fields[i].second) return no("key changed: " + key.fields[i].first);
        }
        // the replacement data, read again from the staged files and checked against the hash of the bytes the build served
        const uint32_t nb = r.u32();
        if (!r.ok || nb > 1000000) return no("damaged (blobs)");
        std::vector<std::shared_ptr<std::vector<uint8_t>>> blobs;
        std::vector<std::string> blob_src;
        std::vector<uint64_t> blob_off;
        for (uint32_t i = 0; i < nb; ++i) {
            if (cancel && cancel->load()) return no("cancelled");
            const uint8_t kind = r.u8();
            if (!r.ok) return no("damaged (blob)");
            if (kind == 1) {
                const uint32_t n = r.u32();
                if (!r.ok || d.size() - r.p < n) return no("damaged (inline blob)");
                blobs.push_back(std::make_shared<std::vector<uint8_t>>(d.begin() + std::ptrdiff_t(r.p), d.begin() + std::ptrdiff_t(r.p + n)));
                r.p += n;
                blob_src.emplace_back();
                blob_off.push_back(0);
            } else if (kind == 2) {
                const std::string named = r.str();
                const std::string src = named.compare(0, 4, "rel:") == 0 ? (file.parent_path() / fs::u8path(named.substr(4))).u8string() : named;
                const uint64_t off = r.u64();
                const uint64_t n = r.u64();
                const uint64_t h = r.u64();
                if (!r.ok || n > (uint64_t(1) << 31) || off > (uint64_t(1) << 31)) return no("damaged (file blob)");
                auto data = std::make_shared<std::vector<uint8_t>>();
                {
                    std::ifstream f(fs::u8path(src), std::ios::binary | std::ios::ate);
                    if (!f) return no("staged data changed: " + src + " cannot be read");
                    const std::streamoff fsz = f.tellg();
                    if (fsz < 0 || uint64_t(fsz) != off + n) return no("staged data changed: " + src + " has another size");
                    data->resize(size_t(n));
                    f.seekg(std::streamoff(off));
                    f.read(reinterpret_cast<char*>(data->data()), std::streamsize(n));
                    if (!f) return no("staged data changed: " + src + " cannot be read");
                }
                if (fnv1a64(data->data(), data->size()) != h) return no("staged data changed: " + src + " (other bytes)");
                blobs.push_back(std::move(data));
                blob_src.push_back(src);
                blob_off.push_back(off);
            } else {
                return no("damaged (blob kind)");
            }
        }
        auto t = std::make_shared<Table>();
        t->n_items_ = r.u32();
        if (!r.ok || t->n_items_ > 10000000) return no("damaged (items)");
        t->storage_.resize(t->n_items_);
        t->src_file_.resize(t->n_items_);
        t->src_off_.resize(t->n_items_, 0);
        t->item_swap_.resize(t->n_items_, 0);
        for (uint32_t i = 0; i < t->n_items_ && r.ok; ++i) {
            const uint32_t b = r.u32();
            t->item_swap_[i] = r.u8() ? 1 : 0;
            if (!r.ok) return no("damaged (item)");
            if (b == 0xFFFFFFFFu) continue;
            if (b >= blobs.size()) return no("damaged (item blob)");
            t->storage_[i] = blobs[b];
            t->src_file_[i] = blob_src[b];
            t->src_off_[i] = blob_off[b];
        }
        const uint32_t ne = r.u32();
        if (!r.ok || ne > 50000000) return no("damaged (entries)");
        t->entries_.reserve(ne);
        for (uint32_t i = 0; i < ne && r.ok; ++i) {
            Entry e;
            e.csize = r.u32();
            e.dsize = r.u32();
            e.head = r.u64();
            e.fnv = r.u64();
            e.item = r.u32();
            e.alias_off = r.u32();
            e.alias_n = r.u32();
            const uint64_t off = r.u64();
            if (!r.ok || e.item >= t->n_items_ || !e.csize || !e.dsize) return no("damaged (entry)");
            if (off != 0xFFFFFFFFFFFFFFFFULL) {
                const auto& sp = t->storage_[e.item];
                if (!sp || off > sp->size() || sp->size() - off < e.dsize) return no("damaged (entry data)");
                e.data = sp->data() + off;
            } else if (t->storage_[e.item]) {
                return no("damaged (entry without data)");
            }
            t->entries_.push_back(e);
        }
        const uint32_t na = r.u32();
        if (!r.ok || na > 50000000) return no("damaged (aliases)");
        for (uint32_t i = 0; i < na && r.ok; ++i) {
            const uint32_t a = r.u32();
            if (a >= t->n_items_) return no("damaged (alias)");
            t->alias_pool_.push_back(a);
        }
        for (const Entry& e : t->entries_)
            if (uint64_t(e.alias_off) + e.alias_n > t->alias_pool_.size()) return no("damaged (alias range)");
        t->bytes_ = r.u64();
        t->min_csize_ = r.u32();
        t->max_csize_ = r.u32();
        CachedBuild cb;
        cb.message = r.str();
        const uint32_t ni = r.u32();
        if (!r.ok || ni > 10000000) return no("damaged (item list)");
        for (uint32_t i = 0; i < ni && r.ok; ++i) {
            ItemStatus it;
            it.team = r.i32();
            it.slot = r.i32();
            it.res = r.str();
            it.kind = r.str();
            it.role = r.str();
            it.reason = r.str();
            it.swap = r.u8() != 0;
            it.probe = r.u8() != 0;
            it.copies = r.i32();
            it.blocks = r.i32();
            it.table_item = r.u32();
            const uint8_t lb = r.u8();
            if (lb > uint8_t(LeftBy::Other)) return no("damaged (item)");
            it.left_by = LeftBy(lb);
            if (!get_users(r, it.shared)) return no("damaged (item users)");
            if (it.table_item != 0xFFFFFFFFu && it.table_item >= t->n_items_) return no("damaged (item number)");
            cb.items.push_back(std::move(it));
        }
        const uint32_t nfo = r.u32();
        if (!r.ok || nfo > 1000000) return no("damaged (folders)");
        for (uint32_t i = 0; i < nfo && r.ok; ++i) {
            FolderVerdict f;
            f.team = r.i32();
            f.slot = r.i32();
            f.kind = r.str();
            f.swapped = r.u8() != 0;
            const uint8_t lb = r.u8();
            if (lb > uint8_t(LeftBy::Other)) return no("damaged (folder)");
            f.why = LeftBy(lb);
            f.role = r.str();
            f.reason = r.str();
            if (!get_users(r, f.shared)) return no("damaged (folder users)");
            f.details_left = r.i32();
            cb.folders.push_back(std::move(f));
        }
        if (!r.ok || r.p != d.size()) return no("damaged (length)");
        // the totals must be the entries' own (a table that disagrees with itself is never used)
        uint64_t bytes = 0;
        uint32_t mn = 0xFFFFFFFFu, mx = 0;
        for (const Entry& e : t->entries_) {
            if (e.data) bytes += e.dsize;
            mn = std::min(mn, e.csize);
            mx = std::max(mx, e.csize);
        }
        if (bytes != t->bytes_ || mn != t->min_csize_ || mx != t->max_csize_) return no("damaged (totals)");
        t->alloc_counters();
        t->index_slots();
        cb.table = t;
        out = std::move(cb);
        if (why) why->clear();
        return true;
    }
};

bool save_table_cache(const fs::path& file, const TableCacheKey& key, const Table& table, const std::string& message, const std::vector<ItemStatus>& items,
                      const std::vector<FolderVerdict>& folders) {
    try {
        return TableCacheIO::save(file, key, table, message, items, folders);
    } catch (...) {
        return false;
    }
}

bool load_table_cache(const fs::path& file, const TableCacheKey& key, CachedBuild& out, std::string* why, const std::atomic<bool>* cancel) {
    try {
        return TableCacheIO::load(file, key, out, why, cancel);
    } catch (...) {
        if (why) *why = "damaged (exception)";
        return false;
    }
}

// ---------------------------------------------------------------- 2.20: the swap plan of one staged kit (swap_plan.bin)
const char* const kPlanStaleReason = "Re-stage this kit (planned for another game version)";
const char* const kPlanNoneReason = "Re-stage this kit (it has no swap plan yet)";
const char* const kNothingToSwap = "the game has no 3D kit textures for this club's slot (nothing to swap)";
const char* const kPlanOptionsReason = "Re-stage this kit (planned with other kit options)";
const char* const kPlanStagedReason = "Re-stage this kit (its staged pictures changed since it was planned)";
const char* const kPlanDamagedReason = "Re-stage this kit (its swap plan file is damaged)";
static const char* const kPlanFormatReason = "Re-stage this kit (planned by an older Turbo)";

namespace {
constexpr uint32_t kPlanMagic = 0x4C504B54;  // "TKPL"
const char* const kKeyFormat = "plan format";
const char* const kKeyBuild = "game build";
const char* const kKeyIndex = "kit index (the game's tocs)";
}  // namespace

Options plan_options(const Options& base, const SlotState& s) {
    Options o = base;
    o.include_decals = s.decals;
    o.keep_game_crest = s.keep_crest;
    o.share_within_club = s.share_club;
    o.share_other_clubs = s.share_other;
    return o;
}

// a plan's row: "ready", "nothing" (planned: the game has no 3D kit textures for this slot, nothing to swap, not a fault), else "failed"
std::string plan_row_state(const SwapPlan& sp) {
    if (sp.plan.ok) return "ready";
    return sp.plan.error == kNothingToSwap ? "nothing" : "failed";
}

PlanKey make_plan_key(const std::string& game_build, const std::string& index_sig, const SlotState& s, const fs::path& dir, const Options& o) {
    PlanKey k;
    k.add(kKeyFormat, std::to_string(kPlanFormat));
    k.add(kKeyBuild, game_build);
    k.add(kKeyIndex, index_sig);
    k.add("kit", std::to_string(s.team) + ":" + std::to_string(s.slot));
    k.add("manifest", file_hash(dir / "manifest.json"));
    // the hotspots file the manifest names (load_staged's rule), "missing" when there is none
    std::string hot = "missing";
    try {
        std::ifstream mf(dir / "manifest.json", std::ios::binary);
        const json mj = mf ? json::parse(mf, nullptr, false) : json();
        const std::string hs = mj.is_object() && mj.contains("hotspots") && mj["hotspots"].is_string() ? mj["hotspots"].get<std::string>() : std::string();
        std::error_code ec0;
        if (!hs.empty() && hs.find_first_of("/\\") == std::string::npos && fs::is_regular_file(dir / hs, ec0)) hot = file_hash(dir / hs);
    } catch (...) {
    }
    k.add("hotspots", hot);
    k.add("mode", mode_name(s.mode));
    std::ostringstream ko;
    ko << "decals " << s.decals << " crest " << s.keep_crest << " share_club " << s.share_club << " share_other " << s.share_other;
    k.add("kit options", ko.str());
    std::ostringstream go;
    go << "watch " << o.watch_club << " min_csize " << o.min_csize << " unique " << o.min_csize_unique << " census " << o.census;
    // the names-and-numbers switch changes only a kit that has a hotspots file (it is not saved in swap_state.json, so keying every kit
    // to it would switch every kit off after a restart with the switch moved)
    if (hot != "missing") go << " hotspots " << o.apply_hotspots;
    k.add("global options", go.str());
    return k;
}

SwapPlan make_swap_plan(GameFiles& files, const OodleFn& oodle, const KitIndex& index, const StagedKit& staged, const SlotState& slot, const Options& base,
                        const PlanKey& key, Progress* progress) {
    SwapPlan sp;
    sp.key = key;
    TableBuilder tb;
    const auto t0 = std::chrono::steady_clock::now();
    sp.plan = build_plan(files, oodle, index, staged, plan_options(base, slot), slot.mode == Mode::Swap, tb, progress);
    sp.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    sp.records = tb.records();
    const auto k = index.kinds.find(staged.team);
    if (k != index.kinds.end()) sp.kinds = k->second;
    uint64_t h = 0xCBF29CE484222325ULL;
    for (const KitBundle& b : index.bundles) {
        if (b.team != staged.team) continue;
        std::ostringstream e;
        e << (b.toc >= 0 && size_t(b.toc) < index.tocs.size() ? index.tocs[size_t(b.toc)] : std::string("?")) << "#" << b.bundle << ":" << b.kind << ":" << b.slot_mask << ";";
        const std::string s = e.str();
        h = fnv1a64(reinterpret_cast<const uint8_t*>(s.data()), s.size(), h);
    }
    sp.donor = hex64(h);
    // a verdict a read error shaped is never kept (the next staging plans again)
    for (const auto& n : sp.plan.notes) sp.transient = sp.transient || n.find("cannot be read") != std::string::npos || n.find("unreadable") != std::string::npos;
    for (const auto& it : sp.plan.items) sp.transient = sp.transient || it.reason.find("cannot be read") != std::string::npos;
    return sp;
}

struct SwapPlanIO {
    static void put_key(Wr& w, const PlanKey& key) {
        w.u32(uint32_t(key.fields.size()));
        for (const auto& f : key.fields) {
            w.str(f.first);
            w.str(f.second);
        }
    }
    static bool save(const fs::path& file, const SwapPlan& sp) {
        Wr w;
        w.u32(kPlanMagic);
        w.u32(kPlanFormat);
        put_key(w, sp.key);
        const Plan& p = sp.plan;
        w.i32(p.team);
        w.i32(p.slot);
        w.u8(p.ok);
        w.str(p.error);
        w.i32(p.swapped);
        w.i32(p.skipped);
        w.i32(p.probes);
        w.u64(uint64_t(sp.seconds * 1000.0 + 0.5));
        w.str(sp.donor);
        w.u32(uint32_t(p.notes.size()));
        for (const auto& n : p.notes) w.str(n);
        w.u32(uint32_t(sp.kinds.size()));
        for (const auto& k : sp.kinds) w.str(k);
        w.u32(uint32_t(p.items.size()));
        for (const PlanItem& it : p.items) {
            w.str(it.res);
            w.str(it.kind);
            w.str(it.role);
            w.u8(it.swap);
            w.str(it.reason);
            w.u8(it.probe);
            w.i32(it.blocks);
            w.u64(it.bytes);
            w.u32(it.table_item);
            w.u8(uint8_t(it.left_by));
            put_users(w, it.shared);
        }
        w.u32(uint32_t(p.folders.size()));
        for (const FolderVerdict& f : p.folders) {
            w.i32(f.team);
            w.i32(f.slot);
            w.str(f.kind);
            w.u8(f.swapped);
            w.u8(uint8_t(f.why));
            w.str(f.role);
            w.str(f.reason);
            put_users(w, f.shared);
            w.i32(f.details_left);
        }
        w.u32(uint32_t(sp.records.size()));
        for (const PlanRecord& r : sp.records) {
            w.u8(r.swap);
            if (!r.data) {
                w.u8(0);
            } else if (r.src_file.empty()) {  // a patched hotspot asset: small, stored inline
                if (r.data->size() > (1u << 20)) return false;
                w.u8(1);
                w.u32(uint32_t(r.data->size()));
                w.b.insert(w.b.end(), r.data->begin(), r.data->end());
            } else {  // a staged picture: named relative to the plan's folder (the kit's staged folder), checked by size and hash at load
                w.u8(2);
                const fs::path rel = fs::u8path(r.src_file).lexically_relative(file.parent_path());
                const std::string rs = rel.generic_u8string();
                w.str(!rel.empty() && rel.is_relative() && rs.compare(0, 2, "..") != 0 ? "rel:" + rs : r.src_file);
                w.u64(r.src_off);
                w.u64(r.data->size());
                w.u64(fnv1a64(r.data->data(), r.data->size()));
            }
            w.u32(uint32_t(r.blocks.size()));
            for (const PlanBlock& b : r.blocks) {
                if (r.data && b.off != ~0ULL && (b.off > r.data->size() || r.data->size() - b.off < b.dsize)) return false;  // never an offset the data cannot serve
                w.u32(b.csize);
                w.u32(b.dsize);
                w.u64(b.head);
                w.u64(b.fnv);
                w.u64(b.off);
            }
        }
        w.u64(fnv1a64(w.b.data(), w.b.size()));
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        const fs::path tmp = file.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(w.b.data()), std::streamsize(w.b.size()));
            if (!f) return false;
        }
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
        return !ec;
    }

    static bool load(const fs::path& file, const PlanKey& key, SwapPlan& out, std::string* why) {
        auto no = [&](const std::string& w) {
            if (why) *why = w;
            return false;
        };
        std::vector<uint8_t> d;
        {
            std::error_code ec;
            if (!fs::exists(file, ec)) return no("no plan");
            std::ifstream f(file, std::ios::binary | std::ios::ate);
            if (!f) return no("no plan");
            const std::streamoff n = f.tellg();
            if (n < 24 || n > (std::streamoff(1) << 30)) return no("damaged (size)");
            d.resize(size_t(n));
            f.seekg(0);
            f.read(reinterpret_cast<char*>(d.data()), n);
            if (!f) return no("damaged (read)");
        }
        uint64_t sum = 0;
        std::memcpy(&sum, &d[d.size() - 8], 8);
        if (fnv1a64(d.data(), d.size() - 8) != sum) return no("damaged (checksum)");
        d.resize(d.size() - 8);
        Rd r(d);
        if (r.u32() != kPlanMagic || !r.ok) return no("damaged (not a swap plan)");
        const uint32_t fmt = r.u32();
        if (fmt != kPlanFormat) return no(std::string("stale: ") + kKeyFormat);
        const uint32_t nf = r.u32();
        if (!r.ok || nf > 1000) return no("damaged (key)");
        SwapPlan sp;
        for (uint32_t i = 0; i < nf && r.ok; ++i) {
            std::string a = r.str();
            std::string b = r.str();
            sp.key.fields.emplace_back(std::move(a), std::move(b));
        }
        if (!r.ok) return no("damaged (key)");
        for (size_t i = 0; i < std::max(sp.key.fields.size(), key.fields.size()); ++i) {
            if (i >= sp.key.fields.size()) return no("stale: " + key.fields[i].first);
            if (i >= key.fields.size() || sp.key.fields[i].first != key.fields[i].first) return no("stale: " + sp.key.fields[i].first);
            if (sp.key.fields[i].second != key.fields[i].second) return no("stale: " + key.fields[i].first);
        }
        Plan& p = sp.plan;
        p.team = r.i32();
        p.slot = r.i32();
        p.ok = r.u8() != 0;
        p.error = r.str();
        p.swapped = r.i32();
        p.skipped = r.i32();
        p.probes = r.i32();
        sp.seconds = double(r.u64()) / 1000.0;
        sp.donor = r.str();
        const uint32_t nn = r.u32();
        if (!r.ok || nn > 100000) return no("damaged (notes)");
        for (uint32_t i = 0; i < nn && r.ok; ++i) p.notes.push_back(r.str());
        const uint32_t nk = r.u32();
        if (!r.ok || nk > 100000) return no("damaged (kit folders)");
        for (uint32_t i = 0; i < nk && r.ok; ++i) sp.kinds.insert(r.str());
        const uint32_t ni = r.u32();
        if (!r.ok || ni > 1000000) return no("damaged (items)");
        for (uint32_t i = 0; i < ni && r.ok; ++i) {
            PlanItem it;
            it.res = r.str();
            it.kind = r.str();
            it.role = r.str();
            it.swap = r.u8() != 0;
            it.reason = r.str();
            it.probe = r.u8() != 0;
            it.blocks = r.i32();
            it.bytes = r.u64();
            it.table_item = r.u32();
            const uint8_t lb = r.u8();
            if (lb > uint8_t(LeftBy::Other)) return no("damaged (item)");
            it.left_by = LeftBy(lb);
            if (!get_users(r, it.shared)) return no("damaged (item users)");
            p.items.push_back(std::move(it));
        }
        const uint32_t nfo = r.u32();
        if (!r.ok || nfo > 100000) return no("damaged (folders)");
        for (uint32_t i = 0; i < nfo && r.ok; ++i) {
            FolderVerdict f;
            f.team = r.i32();
            f.slot = r.i32();
            f.kind = r.str();
            f.swapped = r.u8() != 0;
            const uint8_t lb = r.u8();
            if (lb > uint8_t(LeftBy::Other)) return no("damaged (folder)");
            f.why = LeftBy(lb);
            f.role = r.str();
            f.reason = r.str();
            if (!get_users(r, f.shared)) return no("damaged (folder users)");
            f.details_left = r.i32();
            p.folders.push_back(std::move(f));
        }
        const uint32_t nr = r.u32();
        if (!r.ok || nr > 1000000) return no("damaged (records)");
        // the staged bytes: one read per distinct (file, offset, size, hash), checked against what the plan served
        std::map<std::tuple<std::string, uint64_t, uint64_t, uint64_t>, std::shared_ptr<std::vector<uint8_t>>> files_read;
        for (uint32_t i = 0; i < nr && r.ok; ++i) {
            PlanRecord rec;
            rec.swap = r.u8() != 0;
            const uint8_t kind = r.u8();
            if (!r.ok) return no("damaged (record)");
            if (kind == 1) {
                const uint32_t n = r.u32();
                if (!r.ok || d.size() - r.p < n) return no("damaged (inline data)");
                rec.data = std::make_shared<std::vector<uint8_t>>(d.begin() + std::ptrdiff_t(r.p), d.begin() + std::ptrdiff_t(r.p + n));
                r.p += n;
            } else if (kind == 2) {
                const std::string named = r.str();
                const std::string src = named.compare(0, 4, "rel:") == 0 ? (file.parent_path() / fs::u8path(named.substr(4))).u8string() : named;
                const uint64_t off = r.u64();
                const uint64_t n = r.u64();
                const uint64_t h = r.u64();
                if (!r.ok || n > (uint64_t(1) << 31) || off > (uint64_t(1) << 31)) return no("damaged (file data)");
                auto& slot = files_read[std::make_tuple(src, off, n, h)];
                if (!slot) {
                    auto data = std::make_shared<std::vector<uint8_t>>();
                    std::ifstream f(fs::u8path(src), std::ios::binary | std::ios::ate);
                    if (!f) return no("staged data changed: " + src + " cannot be opened");
                    const std::streamoff fsz = f.tellg();
                    if (fsz < 0 || uint64_t(fsz) != off + n) return no("staged data changed: " + src + " has another size");
                    data->resize(size_t(n));
                    f.seekg(std::streamoff(off));
                    f.read(reinterpret_cast<char*>(data->data()), std::streamsize(n));
                    if (!f) return no("staged data changed: " + src + " cannot be read");
                    if (fnv1a64(data->data(), data->size()) != h) return no("staged data changed: " + src + " (other bytes)");
                    slot = std::move(data);
                }
                rec.data = slot;
                rec.src_file = src;
                rec.src_off = off;
            } else if (kind != 0) {
                return no("damaged (record kind)");
            }
            const uint32_t nb = r.u32();
            if (!r.ok || nb > 1000000) return no("damaged (blocks)");
            for (uint32_t k = 0; k < nb && r.ok; ++k) {
                PlanBlock b;
                b.csize = r.u32();
                b.dsize = r.u32();
                b.head = r.u64();
                b.fnv = r.u64();
                b.off = r.u64();
                if (!r.ok || !b.csize || !b.dsize) return no("damaged (block)");
                if (b.off != ~0ULL) {
                    if (!rec.data || b.off > rec.data->size() || rec.data->size() - b.off < b.dsize) return no("damaged (block data)");
                } else if (rec.data) {
                    return no("damaged (block without data)");
                }
                rec.blocks.push_back(b);
            }
            sp.records.push_back(std::move(rec));
        }
        if (!r.ok || r.p != d.size()) return no("damaged (length)");
        for (const PlanItem& it : p.items)
            if (it.table_item != 0xFFFFFFFFu && it.table_item >= sp.records.size()) return no("damaged (item number)");
        out = std::move(sp);
        if (why) why->clear();
        return true;
    }
};

bool save_swap_plan(const fs::path& file, const SwapPlan& p) {
    try {
        return SwapPlanIO::save(file, p);
    } catch (...) {
        return false;
    }
}

bool load_swap_plan(const fs::path& file, const PlanKey& key, SwapPlan& out, std::string* why, std::string* reason) {
    std::string w;
    bool ok = false;
    try {
        ok = SwapPlanIO::load(file, key, out, &w);
    } catch (...) {
        w = "damaged (exception)";
    }
    if (why) *why = w;
    if (reason) *reason = ok ? std::string() : plan_reason_words(w);
    return ok;
}

std::string plan_reason_words(const std::string& why) {
    if (why.empty()) return "";
    if (why == "no plan") return kPlanNoneReason;
    if (why.compare(0, 7, "damaged") == 0) return kPlanDamagedReason;
    if (why.compare(0, 19, "staged data changed") == 0) return kPlanStagedReason;
    if (why.compare(0, 7, "stale: ") == 0) {
        const std::string f = why.substr(7);
        if (f == kKeyFormat) return kPlanFormatReason;
        if (f == kKeyBuild || f == kKeyIndex) return kPlanStaleReason;
        if (f == "manifest" || f == "hotspots" || f == "kit") return kPlanStagedReason;
        return kPlanOptionsReason;
    }
    return kPlanDamagedReason;
}

void replay_plan(const SwapPlan& sp, TableBuilder& tb, Plan& plan) {
    plan = sp.plan;
    std::vector<uint32_t> ids;
    ids.reserve(sp.records.size());
    for (const PlanRecord& r : sp.records) ids.push_back(tb.add_record(r));
    for (PlanItem& it : plan.items)
        if (it.table_item != 0xFFFFFFFFu) it.table_item = it.table_item < ids.size() ? ids[it.table_item] : 0xFFFFFFFFu;
}

Assembled assemble_table(std::vector<Plan>& plans, TableBuilder& tb) {
    Assembled a;
    // The table. A clash (the same game block wanted with other bytes, by two of the zip's pictures or by two active kits) takes those
    // textures out; when one is a jersey / shorts / socks picture its whole kit folder goes too (2.13: never half a kit), and the table is
    // finished again without them.
    std::map<uint32_t, std::pair<size_t, size_t>> where;  // table item -> (plan, item)
    for (size_t pi = 0; pi < plans.size(); ++pi)
        for (size_t ii = 0; ii < plans[pi].items.size(); ++ii)
            if (plans[pi].items[ii].table_item != 0xFFFFFFFFu) where[plans[pi].items[ii].table_item] = {pi, ii};
    for (int pass = 0; pass < 8; ++pass) {
        std::set<uint32_t> dropped_items;
        std::map<uint32_t, std::set<uint32_t>> clash;
        a.table = tb.finish(nullptr, &dropped_items, &clash);
        if (dropped_items.empty()) break;
        struct Lost {
            size_t plan;
            PlanItem by;
        };
        std::vector<Lost> lost;
        for (uint32_t id : dropped_items) {
            tb.exclude_item(id);
            const auto w = where.find(id);
            if (w == where.end()) continue;
            Plan& pl = plans[w->second.first];
            PlanItem& it = pl.items[w->second.second];
            if (!it.swap) continue;
            std::vector<KitUser> with;  // the other kits that want the same game data
            bool other_kit = false;
            for (uint32_t o : clash[id]) {
                const auto wo = where.find(o);
                if (wo == where.end()) continue;
                const Plan& op = plans[wo->second.first];
                const PlanItem& oi = op.items[wo->second.second];
                other_kit = other_kit || wo->second.first != w->second.first;
                if (wo->second.first != w->second.first) add_user(with, KitUser{op.team, op.slot, oi.kind});
            }
            it.swap = false;
            it.table_item = 0xFFFFFFFFu;
            it.left_by = LeftBy::Clash;
            for (const KitUser& u : with) add_user(it.shared, u);
            it.reason = other_kit ? "the game uses the same data in " + users_text(with) + ", which another active kit wants with other bytes: left to the game"
                                  : "the game uses the same data for two of the zip's pictures (for example crest and crest_shorts): left to the game";
            if (kit_role(it.role)) lost.push_back({w->second.first, it});
        }
        for (const Lost& l : lost)
            for (uint32_t id : leave_folder(plans[l.plan], l.by.kind, l.by.left_by, l.by.role, l.by.reason, l.by.shared)) tb.exclude_item(id);
        for (Plan& pl : plans) {  // counts and verdicts after the clash
            pl.swapped = pl.skipped = 0;
            for (const PlanItem& it : pl.items)
                if (!it.probe) ++(it.swap ? pl.swapped : pl.skipped);
            for (FolderVerdict& f : pl.folders) {
                if (!f.swapped) continue;
                f.details_left = 0;
                for (const PlanItem& it : pl.items)
                    if (!it.probe && it.kind == f.kind && !it.swap && detail_role(it.role)) ++f.details_left;
            }
        }
    }
    if (!a.table) a.table = tb.finish(nullptr);
    for (const Plan& pl : plans) {
        a.swapped += pl.swapped;
        a.skipped += pl.skipped;
        for (const auto& f : pl.folders) a.folders.push_back(f);
        for (const auto& it : pl.items) {
            ItemStatus st;
            st.team = pl.team;
            st.slot = pl.slot;
            st.res = it.res;
            st.kind = it.kind;
            st.role = it.role;
            st.reason = it.reason;
            st.swap = it.swap;
            st.probe = it.probe;
            st.blocks = it.blocks;
            st.table_item = it.table_item;
            st.left_by = it.left_by;
            st.shared = it.shared;
            if (!it.swap && !it.probe) {  // the same resource left to the game at several places (big clubs): one line
                bool merged = false;
                for (auto& o : a.items)
                    if (!o.swap && !o.probe && o.team == st.team && o.slot == st.slot && o.res == st.res && o.reason == st.reason) { ++o.copies; merged = true; break; }
                if (merged) continue;
            }
            a.items.push_back(st);
        }
    }
    return a;
}

// ---------------------------------------------------------------- runtime
const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Observe: return "observe";
        case Mode::Swap: return "swap";
        default: return "off";
    }
}

static const char* slot_folder_name(int s) {
    static const char* n[] = {"home", "away", "goalkeeper", "third", "fourth"};
    return s >= 0 && s <= 4 ? n[s] : "unknown";
}

Runtime::Runtime(fs::path le_root, fs::path game_root, OodleFn oodle) : le_(std::move(le_root)), game_(std::move(game_root)), oodle_(std::move(oodle)), diag_(new Diag[kDiag]) {}

Runtime::~Runtime() {
    stop_ = true;
    progress_.cancel = true;
    generation_++;
    if (worker_.joinable()) worker_.join();
}

bool Runtime::opted_in() const {
    std::error_code ec;
    return fs::exists(le_ / "turbo_output" / "kits3d_on.txt", ec);
}

fs::path Runtime::state_path() const { return le_ / "turbo_output" / "kits" / "swap_state.json"; }
fs::path Runtime::log_path() const { return le_ / "turbo_output" / "kit_swap_log.txt"; }
fs::path Runtime::index_cache_path() const { return le_ / "turbo_output" / "kits" / "kit_index.cache"; }
fs::path Runtime::table_cache_path() const { return le_ / "turbo_output" / "kits" / "kit_table.cache"; }
fs::path Runtime::staged_dir(int team, int slot) const { return le_ / "turbo_output" / "kits" / std::to_string(team) / slot_folder_name(slot); }

void Runtime::set_hook_installed(bool on, const std::string& why) {
    std::lock_guard<std::mutex> lk(m_);
    hook_ = on;
    hook_why_ = why;
}

void Runtime::save_state_locked() {
    json j = {{"version", 2}, {"kits", json::array()}, {"watch_club", options.watch_club}};
    for (const auto& s : slots_)
        j["kits"].push_back({{"team", s.team}, {"slot", slot_folder_name(s.slot)}, {"slot_type", s.slot}, {"mode", mode_name(s.mode)}, {"decals", s.decals},
                             {"keep_game_crest", s.keep_crest}, {"share_within_club", s.share_club}, {"share_other_clubs", s.share_other}});
    std::error_code ec;
    fs::create_directories(state_path().parent_path(), ec);
    const fs::path tmp = state_path().string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(2) << "\n";
    }
    fs::remove(state_path(), ec);
    fs::rename(tmp, state_path(), ec);
}

bool Runtime::hook_ready(std::string* err) {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (hook_ || !ensure_hook) return true;
    }
    std::string why;
    const bool ok = ensure_hook(&why);  // outside the lock: it installs a hook
    set_hook_installed(ok, ok ? std::string() : "the Oodle hook is not installed: " + why);
    if (!ok) return fail(err, "the decompress hook could not be installed: " + why);
    return true;
}

void Runtime::start() {
    {
        std::lock_guard<std::mutex> lk(m_);
        started_s_ = now_s();
        std::ifstream f(state_path(), std::ios::binary);
        if (f) {
            json j = json::parse(f, nullptr, false);
            if (j.is_object() && j.contains("kits") && j["kits"].is_array()) {
                if (j.value("version", 1) >= 2) options.watch_club = j.value("watch_club", options.watch_club);
                for (const json& k : j["kits"]) {
                    SlotState s;
                    s.team = k.value("team", -1);
                    s.slot = k.value("slot_type", -1);
                    const std::string m = k.value("mode", "observe");
                    s.mode = m == "swap" ? Mode::Swap : Mode::Observe;
                    s.decals = k.value("decals", true);  // a version 1 file has none of these: the defaults
                    s.keep_crest = k.value("keep_game_crest", true);
                    s.share_club = k.value("share_within_club", false);
                    s.share_other = k.value("share_other_clubs", false);  // 2.13: a file written before has none: off
                    if (s.team >= 0 && s.slot >= 0 && s.slot <= 4) slots_.push_back(s);
                }
            }
        }
        if (slots_.empty()) return;
    }
    // A kit left on in the last session: nothing is read, scanned or hooked while the opt-in file is missing, the kill switch is there, or the
    // host refuses the hook (the state file stays as it is; switching a kit on later builds everything)
    std::string why;
    if (require_opt_in && !opted_in()) why = "turbo_output\\kits3d_on.txt is missing: nothing is read or hooked";
    else if (killed_.load()) why = "kill switch turbo_output\\kits3d_off.txt is present: nothing is read or hooked";
    else if (!hook_ready(&why)) {
    }
    if (!why.empty()) {
        std::lock_guard<std::mutex> lk(m_);
        state_ = "off";
        message_ = why;
        build_skipped_ = true;
        return;
    }
    rebuild_async();
}

bool Runtime::set_slot_options(int team, int slot, bool decals, bool keep_crest, bool share_club, bool share_other) {
    {
        std::lock_guard<std::mutex> lk(m_);
        bool found = false;
        for (auto& s : slots_)
            if (s.team == team && s.slot == slot) {
                s.decals = decals;
                s.keep_crest = keep_crest;
                s.share_club = share_club;
                s.share_other = share_other;
                found = true;
            }
        if (!found) return false;
        save_state_locked();
    }
    request_plan(team, slot);  // 2.20: the kit is planned again with its new options (stage time, not the next start)
    return true;
}

bool Runtime::slot_options(int team, int slot, bool* decals, bool* keep_crest, bool* share_club, bool* share_other) const {
    std::lock_guard<std::mutex> lk(m_);
    for (const auto& s : slots_)
        if (s.team == team && s.slot == slot) {
            if (decals) *decals = s.decals;
            if (keep_crest) *keep_crest = s.keep_crest;
            if (share_club) *share_club = s.share_club;
            if (share_other) *share_other = s.share_other;
            return true;
        }
    return false;
}

std::set<std::string> Runtime::kinds_of(int team) const {
    std::lock_guard<std::mutex> lk(m_);
    if (!index_) {  // 2.20: the start reads no index; the club's plans carry its kit folders
        auto pk = plan_kinds_.find(team);
        return pk == plan_kinds_.end() ? std::set<std::string>() : pk->second;
    }
    auto it = index_->kinds.find(team);
    return it == index_->kinds.end() ? std::set<std::string>() : it->second;
}

fs::path Runtime::plan_path(int team, int slot) const { return staged_dir(team, slot) / "swap_plan.bin"; }

void Runtime::request_plan(int team, int slot, bool force) {
    if (team < 0 || slot < 0 || slot > 4) return;
    {
        std::lock_guard<std::mutex> lk(m_);
        const auto k = std::make_pair(team, slot);
        bool found = false;
        for (auto& p : pending_plans_)
            if (p.first == k) {
                p.second = p.second || force;
                found = true;
            }
        if (!found) pending_plans_.push_back({k, force});
        Status::PlanRow& row = plan_rows_[k];
        row.team = team;
        row.slot = slot;
        row.state = "queued";
        row.reason.clear();
        if (planning_ == k) progress_.cancel = true;  // the plan being made is already out of date: made again
    }
    rebuild_async();
}

void Runtime::replan_all() {
    std::vector<SlotState> slots;
    {
        std::lock_guard<std::mutex> lk(m_);
        slots = slots_;
    }
    for (const SlotState& s : slots) request_plan(s.team, s.slot, true);
}

void Runtime::rebuild() {
    std::vector<SlotState> slots;
    {
        std::lock_guard<std::mutex> lk(m_);
        slots = slots_;
    }
    for (const SlotState& s : slots) request_plan(s.team, s.slot, false);
    if (slots.empty()) rebuild_async();
}

void Runtime::set_killed(bool k) {
    const bool was = killed_.exchange(k);
    if (was == k) return;
    if (k) {
        progress_.cancel = true;  // a running scan / build stops at once (the worker sees the switch and ends)
        return;
    }
    bool again = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        again = build_skipped_ && !slots_.empty();
    }
    bool hooked = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        hooked = hook_ || !ensure_hook;
        if (again && !hooked) message_ = "kill switch released: switch the kit on again to install the hook";  // never install a hook from the watcher thread
    }
    if (again && hooked) rebuild_async();  // the build the switch skipped
}

bool Runtime::activate(int team, int slot, Mode mode, std::string* err) {
    if (mode == Mode::Off) {
        deactivate(team, slot);
        return true;
    }
    if (require_opt_in && !opted_in()) return fail(err, "turbo_output\\kits3d_on.txt is missing: the 3D kit feature is off");
    StagedKit sk;
    if (!load_staged(staged_dir(team, slot), sk, err)) return false;
    if (sk.team != team || sk.slot != slot) return fail(err, "the staged textures belong to another club or slot");
    if (!hook_ready(err)) return false;
    {
        std::lock_guard<std::mutex> lk(m_);
        bool found = false;
        for (auto& s : slots_)
            if (s.team == team && s.slot == slot) { s.mode = mode; found = true; }
        if (!found) slots_.push_back({team, slot, mode});
        save_state_locked();
    }
    request_plan(team, slot);  // 2.20: kept when its plan matches (switching on is then instant), else planned now: the user asked for it
    return true;
}

void Runtime::deactivate(int team, int slot) {
    {
        std::lock_guard<std::mutex> lk(m_);
        slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [&](const SlotState& s) { return s.team == team && s.slot == slot; }), slots_.end());
        save_state_locked();
    }
    rebuild_async();
}

bool Runtime::is_active(int team, int slot, Mode* mode) const {
    std::lock_guard<std::mutex> lk(m_);
    for (const auto& s : slots_)
        if (s.team == team && s.slot == slot) {
            if (mode) *mode = s.mode;
            return true;
        }
    return false;
}

void Runtime::rebuild_async() {
    std::lock_guard<std::mutex> lk(m_);
    generation_++;
    if (state_ != "planning") {
        state_ = "building";
        message_ = "loading the kit plans";
    }
    // 2.20: a running worker sees the new generation and publishes again when it is done (a plan being made is not thrown away; the
    // kill switch and request_plan for the same kit cancel it themselves)
    if (running_) return;
    running_ = true;
    if (worker_.joinable()) worker_.join();
    worker_ = std::thread(&Runtime::worker, this);
}

void Runtime::wait_idle() const {
    for (int i = 0; i < 6000; ++i) {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (!running_) return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void Runtime::publish(std::shared_ptr<Table> t) {
    std::lock_guard<std::mutex> lk(m_);
    const double now = now_s();
    for (auto& k : keep_) retired_.push_back({k, now});
    keep_.clear();
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(), [&](const auto& r) { return now - r.second > 120.0; }), retired_.end());
    if (t) keep_.push_back(t);
    table_.store(t ? t.get() : nullptr);
}

// 2.20: the kits asked for by request_plan, one after the other (stage time: an import, an option, switching a kit on, Rebuild all kit
// plans). Each plan is written to the kit's staged folder; the worker then publishes the table from the plans as at start.
void Runtime::plan_pending(GameFiles& files, const std::string& sig, const Options& base, int) {
    for (;;) {
        std::pair<int, int> k;
        bool force = false;
        SlotState s;
        double estimate = 0;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (pending_plans_.empty() || stop_.load() || killed_.load()) {
                planning_ = {-1, -1};
                return;
            }
            k = pending_plans_.front().first;
            force = pending_plans_.front().second;
            pending_plans_.erase(pending_plans_.begin());
            planning_ = k;
            s = SlotState{k.first, k.second, Mode::Swap};  // a kit that is not switched on yet: planned the way switching it on uses it
            for (const SlotState& x : slots_)
                if (x.team == k.first && x.slot == k.second) s = x;
            estimate = last_plan_s_ > 0 ? last_plan_s_ : 20.0;
            state_ = "planning";
            char b[120];
            std::snprintf(b, sizeof(b), "Planning your kit... (about %.0f s)", std::max(1.0, estimate));
            message_ = b;
            Status::PlanRow& row = plan_rows_[k];
            row.team = k.first;
            row.slot = k.second;
            row.state = "planning";
            row.reason.clear();
            progress_.cancel = false;
        }
        const fs::path dir = staged_dir(k.first, k.second);
        const PlanKey key = make_plan_key(game_build, sig, s, dir, base);
        auto finish_row = [&](const std::string& state, const std::string& reason, double secs) {
            std::lock_guard<std::mutex> lk(m_);
            Status::PlanRow& row = plan_rows_[k];
            row.state = state;
            row.reason = reason;
            row.seconds = secs;
        };
        if (!force) {  // a plan that still matches is kept (switching a planned kit on is instant)
            auto sp = std::make_shared<SwapPlan>();
            if (load_swap_plan(plan_path(k.first, k.second), key, *sp)) {
                std::error_code ec;
                const auto mt = fs::last_write_time(plan_path(k.first, k.second), ec);
                loaded_[k] = {sp, mt};
                finish_row(plan_row_state(*sp), sp->plan.ok ? std::string() : sp->plan.error, sp->seconds);
                continue;
            }
        }
        // the kit index (which game bundles hold which club's kits): kit_index.cache, read in milliseconds; scanned from the game's tocs only
        // here, at stage time, when a game patch made the saved one stale (about 2 minutes, once)
        std::shared_ptr<KitIndex> index;
        {
            std::lock_guard<std::mutex> lk(m_);
            index = index_;
        }
        if (!index) {
            auto ni = std::make_shared<KitIndex>();
            std::string err;
            if (load_index_cache(files, index_cache_path(), *ni)) {
                index = ni;
                std::lock_guard<std::mutex> lk(m_);
                index_ = ni;
                index_cached_ = true;
            } else {
                {
                    std::lock_guard<std::mutex> lk(m_);
                    message_ = "Planning your kit... reading the game's kit list first (about 2 min, once after a game update)";
                    scanning_ = true;
                }
                const bool ok = scan_kits(files, oodle_, *ni, &progress_, &err);
                {
                    std::lock_guard<std::mutex> lk(m_);
                    scanning_ = false;
                }
                if (ok) {
                    index = ni;
                    save_index_cache(files, *ni, index_cache_path());
                    std::lock_guard<std::mutex> lk(m_);
                    index_ = ni;
                    index_cached_ = false;
                    index_seconds_ = ni->seconds;
                } else if (err == "cancelled") {
                    std::lock_guard<std::mutex> lk(m_);
                    pending_plans_.insert(pending_plans_.begin(), {k, force});  // asked again by the next pass (or dropped by the kill switch / stop)
                    continue;
                } else {
                    finish_row("failed", "The game's kit list could not be read: " + err, 0);
                    continue;
                }
            }
        }
        StagedKit sk;
        std::string e;
        if (!load_staged(dir, sk, &e)) {
            finish_row("failed", "Re-stage this kit (" + e + ")", 0);
            continue;
        }
        if (sk.team != k.first || sk.slot != k.second) {
            finish_row("failed", "Re-stage this kit (the staged pictures belong to another club or slot)", 0);
            continue;
        }
        auto sp = std::make_shared<SwapPlan>(make_swap_plan(files, oodle_, *index, sk, s, base, key, &progress_));
        if (sp->plan.error == "cancelled") {
            std::lock_guard<std::mutex> lk(m_);
            bool again = false;
            for (const auto& p : pending_plans_) again = again || p.first == k;
            if (!again && !stop_.load() && !killed_.load()) pending_plans_.insert(pending_plans_.begin(), {k, force});
            continue;
        }
        std::string line;
        if (sp->transient) {
            finish_row("failed", "A game file could not be read while planning this kit: plan it again", sp->seconds);
            line = "kit swap: plan for team " + std::to_string(k.first) + " " + slot_folder_name(k.second) + " not kept (a game file could not be read)";
        } else if (!save_swap_plan(plan_path(k.first, k.second), *sp)) {
            finish_row("failed", "The plan could not be written to " + plan_path(k.first, k.second).u8string(), sp->seconds);
            line = "kit swap: plan for team " + std::to_string(k.first) + " " + slot_folder_name(k.second) + " could not be written";
        } else {
            std::error_code ec;
            const auto mt = fs::last_write_time(plan_path(k.first, k.second), ec);
            loaded_[k] = {sp, mt};
            finish_row(plan_row_state(*sp), sp->plan.ok ? std::string() : sp->plan.error, sp->seconds);
            char b[200];
            std::snprintf(b, sizeof(b), "kit swap: planned team %d %s in %.1f s: %d textures served, %d left to the game%s", k.first, slot_folder_name(k.second), sp->seconds,
                          sp->plan.swapped, sp->plan.skipped, sp->plan.ok ? "" : " (no plan: ");
            line = b;
            if (!sp->plan.ok) line += sp->plan.error + ")";
            std::lock_guard<std::mutex> lk(m_);
            last_plan_s_ = std::max(1.0, sp->seconds);
            plan_kinds_[k.first].insert(sp->kinds.begin(), sp->kinds.end());
        }
        if (log_line) {
            try {
                log_line(line);
            } catch (...) {
            }
        }
    }
}

void Runtime::worker() {
    if (worker_init) {
        try {
            worker_init();
        } catch (...) {
        }
    }
    for (;;) {
        if (stop_.load()) {
            std::lock_guard<std::mutex> lk(m_);
            running_ = false;
            planning_ = {-1, -1};
            return;
        }
        if (killed_.load() || (require_opt_in && !opted_in())) {  // nothing read, planned or published while the kill switch / a missing opt-in says so
            std::lock_guard<std::mutex> lk(m_);
            state_ = "off";
            message_ = killed_.load() ? "kill switch turbo_output\\kits3d_off.txt is present: nothing is read or swapped" : "turbo_output\\kits3d_on.txt is missing: nothing is read or swapped";
            build_skipped_ = true;
            running_ = false;
            planning_ = {-1, -1};
            return;
        }
        const int gen = generation_.load();
        progress_.cancel = false;
        progress_.done = 0;
        progress_.total = 0;
        bool pending = false;
        {
            std::lock_guard<std::mutex> lk(m_);
            pending = !pending_plans_.empty();
        }
        std::string state, message;
        std::vector<ItemStatus> items;
        std::vector<FolderVerdict> folders;
        std::shared_ptr<Table> table;
        std::vector<std::string> off_lines;
        std::map<std::pair<int, int>, Status::PlanRow> rows;
        std::vector<std::pair<int, int>> auto_q;  // kits with no plan or a stale one (a game update, an option change): planned after the publish
        int planned = 0, nothing_n = 0;  // kits from their plans; kits planned with nothing in the game to swap (not off, not a fault)
        double t_load0 = now_s();
        std::vector<SlotState> slots;
        GameFiles files(game_);
        std::string err;
        bool files_ok = false;
        if (!oodle_) {
            state = "error";
            message = "no Oodle decompressor in this process";
        } else if (!files.init(&err)) {
            state = "error";
            message = err;
        } else {
            files_ok = true;
        }
        if (!files_ok) {  // no plan can be made without the game's files: the rows asked for say why (and the worker does not spin)
            std::lock_guard<std::mutex> lk(m_);
            for (const auto& p : pending_plans_) {
                Status::PlanRow& row = plan_rows_[p.first];
                row.team = p.first.first;
                row.slot = p.first.second;
                row.state = "failed";
                row.reason = message;
            }
            pending_plans_.clear();
        }
        if (files_ok) {
            const Options base = get_options();
            const std::string sig = index_signature(files);
            if (pending) plan_pending(files, sig, base, gen);  // stage time: the kits asked for (seconds each)
            if (stop_.load() || killed_.load()) continue;     // the loop's top ends it
            t_load0 = now_s();
            {
                std::lock_guard<std::mutex> lk(m_);
                slots = slots_;
            }
            // the start path: the table from the kits' plans, nothing scanned or planned
            TableBuilder tb;
            std::vector<Plan> plans;
            std::vector<std::string> msgs;
            for (const SlotState& s : slots) {
                const auto k = std::make_pair(s.team, s.slot);
                Status::PlanRow row;
                row.team = s.team;
                row.slot = s.slot;
                const PlanKey key = make_plan_key(game_build, sig, s, staged_dir(s.team, s.slot), base);
                std::error_code ec;
                const auto mt = fs::last_write_time(plan_path(s.team, s.slot), ec);
                std::shared_ptr<SwapPlan> sp;
                std::string why, reason;
                auto lp = loaded_.find(k);
                if (!ec && lp != loaded_.end() && lp->second.mtime == mt && lp->second.plan->key.fields == key.fields) {
                    sp = lp->second.plan;
                } else {
                    auto np = std::make_shared<SwapPlan>();
                    if (load_swap_plan(plan_path(s.team, s.slot), key, *np, &why, &reason)) {
                        sp = np;
                        loaded_[k] = {np, mt};
                    } else {
                        loaded_.erase(k);
                    }
                }
                const std::string name = "team " + std::to_string(s.team) + " " + slot_folder_name(s.slot);
                if (!sp) {
                    row.state = why == "no plan" ? "none" : why.compare(0, 7, "damaged") == 0 ? "failed" : "stale";
                    row.reason = reason;
                    off_lines.push_back("kit swap: " + name + " is off: " + reason + " [" + why + "]");
                    if (auto_plan && row.state != "failed") auto_q.push_back(k);  // 4.0: planned in the background below (once per session), not left off
                } else if (!sp->plan.ok) {
                    row.state = plan_row_state(*sp);
                    nothing_n += row.state == "nothing";
                    row.reason = sp->plan.error;
                    row.seconds = sp->seconds;
                    msgs.push_back(name + ": " + sp->plan.error);
                } else {
                    row.state = "ready";
                    row.seconds = sp->seconds;
                    Plan p;
                    replay_plan(*sp, tb, p);
                    plans.push_back(std::move(p));
                    ++planned;
                    std::lock_guard<std::mutex> lk(m_);
                    plan_kinds_[s.team].insert(sp->kinds.begin(), sp->kinds.end());
                }
                rows[k] = row;
            }
            Assembled a = assemble_table(plans, tb);
            table = a.table;
            items = std::move(a.items);
            folders = std::move(a.folders);
            state = "ready";
            message = std::to_string(a.swapped) + " textures in the table, " + std::to_string(a.skipped) + " left to the game";
            const int off_n = int(slots.size()) - planned - nothing_n;
            if (off_n > 0) message += "; " + std::to_string(off_n) + (off_n == 1 ? " kit is" : " kits are") + " off until planned (see the Kits page)";
            for (const auto& m : msgs) message += "; " + m;
            if (table->size() == 0 && !slots.empty()) message += " (nothing matchable)";
            if (slots.empty()) {  // no kit switched on: no table at all (the hook recognises nothing), as before plans
                state = "off";
                message.clear();
                table.reset();
            }
        }
        if (generation_.load() != gen) continue;  // a newer request (a kit switched on, a plan asked for): publish once more
        // publish (under the lock) and say when
        const double now = now_s();
        std::string line, ready;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (!pending_plans_.empty()) continue;  // asked for while this pass ran
            state_ = state;
            message_ = message;
            build_skipped_ = false;
            items_ = items;
            folders_ = folders;
            swap_any_ = false;
            for (const auto& s : slots_) swap_any_ = swap_any_.load() || s.mode == Mode::Swap;
            table_cached_ = false;
            table_origin_ = table ? "from plans" : "";
            table_ms_ = (now - t_load0) * 1000.0;
            ready_after_ms_ = started_s_ >= 0 ? (now - started_s_) * 1000.0 : -1;
            ready_at_ms_ = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            cache_note_ = std::to_string(planned) + " of " + std::to_string(slots.size()) + " kits from their plans; the game's tocs were not read";
            for (const auto& r : rows) {
                Status::PlanRow& pr = plan_rows_[r.first];
                if (pr.state != "queued" && pr.state != "planning") pr = r.second;
            }
            if (table && !slots.empty()) {
                char b[200];
                std::snprintf(b, sizeof(b), "kit swap: table from plans (%zu blocks, %.0f ms; %.0f ms after start; %d of %zu kits)", table->size(), table_ms_, ready_after_ms_,
                              planned, slots.size());
                line = b;
                char r2[160];
                std::snprintf(r2, sizeof(r2), "kit swap: table ready (from plans, %zu blocks, %.0f ms after start)", table->size(), ready_after_ms_);
                ready = r2;
            }
        }
        publish(table);
        if (log_line) {
            try {
                if (!line.empty()) {
                    log_line(line);
                    log_line(ready);
                }
                for (const auto& l : off_lines) log_line(l);
            } catch (...) {
            }
        }
        // the kit index for the Kits page's folder list: kit_index.cache when it is there (milliseconds); never scanned at start
        if (files_ok) {
            bool have = false;
            {
                std::lock_guard<std::mutex> lk(m_);
                have = index_ != nullptr;
            }
            if (!have) {
                auto ni = std::make_shared<KitIndex>();
                if (load_index_cache(files, index_cache_path(), *ni)) {
                    std::lock_guard<std::mutex> lk(m_);
                    index_ = ni;
                    index_cached_ = true;
                    index_seconds_ = ni->seconds;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            // 4.0 (10-29): a kit switched on whose plan is missing or stale is planned now, in the background, and the table is published
            // once more; before, it stayed off until "Rebuild all kit plans" (52 of 65 kits after the 10-27 game update). Once per kit and
            // session: a plan that cannot be made does not loop
            for (const auto& k : auto_q) {
                if (!auto_planned_.insert(k).second) continue;
                bool found = false;
                for (const auto& p : pending_plans_) found = found || p.first == k;
                if (!found) pending_plans_.push_back({k, false});
                Status::PlanRow& row = plan_rows_[k];
                row.team = k.first;
                row.slot = k.second;
                row.state = "queued";
                row.reason.clear();
            }
            if (generation_.load() != gen || !pending_plans_.empty()) continue;
            running_ = false;
        }
        flush_log(true);
        return;
    }
}

int Runtime::on_block(const uint8_t* src, uint64_t srclen, uint8_t* dst, uint64_t dstlen) {
    const Table* t = table_.load(std::memory_order_acquire);
    if (!t || killed_.load(std::memory_order_relaxed) || !t->may_match(srclen, dstlen)) return 0;
    candidates_.fetch_add(1, std::memory_order_relaxed);
    const Entry* e = t->match(src, srclen, dstlen);
    if (!e) return 0;
    hits_.fetch_add(1, std::memory_order_relaxed);
    t->count_hit(*e);
    if (!t->swap_item(e->item) || !dst || !e->data) return 1;
    std::memcpy(dst, e->data, size_t(e->dsize));
    swapped_.fetch_add(1, std::memory_order_relaxed);
    return 2;
}

int Runtime::on_decompressed(const uint8_t* src, uint64_t srclen, uint8_t* dst, uint64_t dstlen, int64_t ret, uint32_t flags, int phase) {
    const Table* t = table_.load(std::memory_order_acquire);
    if (!t || killed_.load(std::memory_order_relaxed) || !src || srclen < 8 || srclen < t->min_csize() || srclen > t->max_csize() || dstlen == 0 || dstlen > 0x1000000) return 0;
    candidates_.fetch_add(1, std::memory_order_relaxed);
    const Entry* e = t->match(src, srclen, dstlen);
    const uint32_t i = (e || srclen >= 1024) ? diag_n_.fetch_add(1, std::memory_order_relaxed) : kDiag;  // small non-matching blocks (hotspot sizes) stay out of the ring
    if (i < kDiag) {
        uint64_t head;
        std::memcpy(&head, src, 8);
        diag_[i].a.store(srclen << 32 | (dstlen & 0xFFFFFFFFu), std::memory_order_relaxed);
        diag_[i].b.store(head, std::memory_order_relaxed);
        diag_[i].c.store(uint64_t(uint32_t(ret)) << 32 | uint64_t(flags & 0xFF) << 16 | uint64_t(phase & 0xFF) << 8 | (e ? 1u : 0u), std::memory_order_relaxed);
    }
    if (!e) return 0;
    hits_.fetch_add(1, std::memory_order_relaxed);
    t->count_hit(*e);
    if (flags & 1) with_cb_.fetch_add(1, std::memory_order_relaxed);
    if (phase != 3) other_phase_.fetch_add(1, std::memory_order_relaxed);
    if (ret != int64_t(dstlen)) {
        unserved_.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    if (!t->swap_item(e->item) || !dst || !e->data) return 1;
    std::memcpy(dst, e->data, size_t(e->dsize));
    swapped_.fetch_add(1, std::memory_order_relaxed);
    return 2;
}

Status Runtime::status() const {
    Status s;
    std::lock_guard<std::mutex> lk(m_);
    s.state = state_;
    s.message = message_;
    s.killed = killed_.load();
    s.hook = hook_;
    if (!hook_ && !hook_why_.empty()) s.message += (s.message.empty() ? "" : "; ") + hook_why_;
    s.calls = calls_.load();
    s.candidates = candidates_.load();
    s.hits = hits_.load();
    s.swapped = swapped_.load();
    s.unserved = unserved_.load();
    s.with_callback = with_cb_.load();
    s.other_phase = other_phase_.load();
    s.slots = slots_;
    s.index_seconds = index_seconds_;
    const Table* t = table_.load();
    if (t) {
        s.blocks = t->size();
        s.bytes = t->bytes();
    }
    s.items = items_;
    s.folders = folders_;
    for (auto& it : s.items) {
        const bool in = t && it.table_item != 0xFFFFFFFFu;
        it.hits = in ? t->hits(it.table_item) : 0;
        it.first_ms = in ? t->first_ms(it.table_item) : 0;
        it.last_ms = in ? t->last_ms(it.table_item) : 0;
    }
    s.index_cached = index_cached_;
    s.table_cached = table_cached_;
    s.table_ms = table_ms_;
    s.ready_after_ms = ready_after_ms_;
    s.ready_at_ms = ready_at_ms_;
    s.cache_note = cache_note_;
    s.table_origin = table_origin_;
    for (const auto& r : plan_rows_) {
        const bool active = std::any_of(slots_.begin(), slots_.end(), [&](const SlotState& x) { return x.team == r.first.first && x.slot == r.first.second; });
        if (active || r.second.state == "queued" || r.second.state == "planning" || r.second.state == "ready" || r.second.state == "failed" || r.second.state == "nothing") s.plans.push_back(r.second);
    }
    s.plans_queued = int(pending_plans_.size());
    s.plan_estimate_s = planning_.first >= 0 ? (last_plan_s_ > 0 ? last_plan_s_ : 20.0) : 0;
    s.scanning = scanning_;
    s.scan_done = progress_.done.load();
    s.scan_total = progress_.total.load();
    // per club and kit folder: what the match decompressed (replaced textures and watch-only ones)
    // (the same game texture can be registered by two kits' plans, as a watch-only item each: counted once, with the larger count)
    std::map<std::tuple<int, std::string, std::string>, uint64_t> uniq;
    for (const auto& it : s.items) {
        if (it.table_item == 0xFFFFFFFFu || it.role == "hotspots") continue;  // the hotspot asset is not a texture of a kit folder
        uint64_t& h = uniq[std::make_tuple(it.team, it.kind, it.res)];
        h = std::max(h, it.hits);
    }
    for (const auto& u : uniq) {
        KindSeen* k = nullptr;
        for (auto& x : s.kinds)
            if (x.team == std::get<0>(u.first) && x.kind == std::get<1>(u.first)) k = &x;
        if (!k) {
            s.kinds.push_back({});
            k = &s.kinds.back();
            k->team = std::get<0>(u.first);
            k->kind = std::get<1>(u.first);
        }
        ++k->textures;
        if (u.second) ++k->textures_seen;
        k->blocks_seen += u.second;
    }
    return s;
}

static std::string clock_text(uint64_t ms) {
    if (!ms) return "-";
    const time_t t = time_t(ms / 1000);
    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[40];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, int(ms % 1000));
    return b;
}

void Runtime::flush_log(bool force) {
    const double now = now_s();
    const uint64_t h = hits_.load(), c = calls_.load();
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!force && (now - last_log_ < 5.0 || (h == logged_hits_ && c == logged_calls_))) return;
        last_log_ = now;
        logged_hits_ = h;
        logged_calls_ = c;
    }
    const Status s = status();
    std::ostringstream o;
    o << "Turbo kit swap log (turbo_output\\kit_swap_log.txt)\nstate " << s.state << (s.killed ? " (kill switch kits3d_off.txt present)" : "") << ": " << s.message << "\n";
    o << "hook " << (s.hook ? "installed" : "NOT installed") << ", decompress calls " << s.calls << ", size candidates " << s.candidates << ", blocks recognised " << s.hits
      << ", blocks overwritten " << s.swapped << ", table " << s.blocks << " blocks (" << s.bytes << " bytes), index " << s.index_seconds << " s\n";
    o << "recognised but not overwritten (original returned another size): " << s.unserved << ", with an Oodle callback: " << s.with_callback << ", threadPhase != 3: " << s.other_phase << "\n";
    {
        const uint32_t n = std::min<uint32_t>(diag_n_.load(), kDiag);
        o << "first " << n << " decompress calls whose compressed size the table knows (srclen rawlen ret head8 flags(1 cb,2 decbuf,4 decmem) phase matched):\n";
        for (uint32_t i = 0; i < n; ++i) {
            const uint64_t a = diag_[i].a.load(), b = diag_[i].b.load(), c = diag_[i].c.load();
            char line[160];
            std::snprintf(line, sizeof(line), "  %u %u %d %016llx %u %u %u\n", unsigned(a >> 32), unsigned(a & 0xFFFFFFFFu), int(int32_t(c >> 32)), static_cast<unsigned long long>(b),
                          unsigned((c >> 16) & 0xFF), unsigned((c >> 8) & 0xFF), unsigned(c & 1));
            o << line;
        }
    }
    for (const auto& sl : s.slots)
        o << "kit team " << sl.team << " " << slot_folder_name(sl.slot) << " mode " << mode_name(sl.mode) << ", zip decals " << (sl.decals ? "on" : "off") << ", game's club badge "
          << (sl.keep_crest ? "kept" : "replaced by the zip's") << ", share within club " << (sl.share_club ? "on" : "off") << ", share with other clubs "
          << (sl.share_other ? "on" : "off") << "\n";
    o << "KITS (2.13: each kit folder is shown whole, the zip's or the game's own, never half a kit):\n";
    for (const auto& f : s.folders) {
        o << "  team " << f.team << " " << slot_folder_name(f.slot) << " " << f.kind << ": ";
        if (f.swapped) {
            o << "swapped";
            if (f.details_left) o << " (" << f.details_left << " detail map(s) stay the game's)";
            std::vector<KitUser> changed;
            for (const auto& u : f.shared)
                if (u.team != f.team) changed.push_back(u);
            if (!changed.empty()) o << "; also changes " << users_text(changed);
        } else {
            o << "LEFT TO THE GAME: " << f.role << ": " << f.reason;
        }
        o << "\n";
    }
    if (s.ready_at_ms) {
        char tb[200];
        std::snprintf(tb, sizeof(tb), "table: %s, %zu blocks, %.0f ms, published %s (%.0f ms after start)", s.table_origin.empty() ? "none" : s.table_origin.c_str(), s.blocks, s.table_ms,
                      clock_text(s.ready_at_ms).c_str(), s.ready_after_ms);
        o << tb << "; " << s.cache_note << "\n";
    }
    // 2.20: one line per kit: its swap plan (made once at stage time; the start only loads it)
    for (const auto& p : s.plans) {
        o << "plan team " << p.team << " " << slot_folder_name(p.slot) << ": " << p.state;
        if (p.state == "ready") {
            char b[64];
            std::snprintf(b, sizeof(b), " (planned in %.1f s)", p.seconds);
            o << b;
        }
        if (!p.reason.empty()) o << ": " << p.reason;
        o << "\n";
    }
    // 2.20: one line per club with a kit in the table: where the table came from, how many of its textures the table holds and how many the game
    // has loaded through it so far (the in-game check reads this line: club id + the count the plan writer printed before the boot)
    {
        std::map<int, std::array<uint64_t, 3>> by_team;  // textures in the table, of them loaded, blocks loaded
        std::map<int, uint64_t> first;
        for (const auto& it : s.items) {
            if (!it.swap || it.team < 0) continue;
            auto& c = by_team[it.team];
            ++c[0];
            if (it.hits) {
                ++c[1];
                c[2] += it.hits;
                auto f = first.find(it.team);
                if (f == first.end() || it.first_ms < f->second) first[it.team] = it.first_ms;
            }
        }
        const std::string origin = s.table_origin.empty() ? std::string("no table") : s.table_origin;
        for (const auto& t : by_team) {
            o << "served team " << t.first << " " << origin << ": " << t.second[1] << " of " << t.second[0] << " textures in the table loaded by the game, " << t.second[2]
              << " blocks";
            if (t.second[1]) o << " (first " << clock_text(first[t.first]) << ")";
            o << "\n";
        }
    }
    {
        const std::string idx = [&]() -> std::string {
            if (s.scanning) return "being scanned from the game's tocs while a kit is planned (once after a game update)";
            if (s.index_seconds > 0 && !s.index_cached) return "scanned from the game's tocs while a kit was planned";
            if (s.index_cached) return "from turbo_output\\kits\\kit_index.cache (the Kits page's folder list; the table does not need it)";
            return "not read (the table comes from the kits' plans)";
        }();
        o << "kit index: " << idx << "\n";
    }
    o << "KIT FOLDERS THE GAME LOADED (per club and kit folder: textures seen / registered, blocks decompressed; 0 = that folder was not used):\n";
    for (const auto& k : s.kinds) o << "  team " << k.team << " " << k.kind << ": " << k.textures_seen << " of " << k.textures << " textures, " << k.blocks_seen << " blocks\n";
    for (const auto& it : s.items)
        o << (it.swap ? "  in table  " : it.probe ? "  watch     " : "  left      ") << it.kind << "/" << it.res.substr(it.res.rfind('/') == std::string::npos ? 0 : it.res.rfind('/') + 1) << "  blocks " << it.blocks << "  seen "
          << it.hits << (it.hits ? "  first " + clock_text(it.first_ms) + " last " + clock_text(it.last_ms) : std::string()) << (it.copies > 1 ? "  x" + std::to_string(it.copies) + " copies" : std::string())
          << (it.reason.empty() ? "" : "  (" + it.reason + ")") << "\n";
    std::error_code ec;
    fs::create_directories(log_path().parent_path(), ec);
    std::ofstream f(log_path(), std::ios::binary | std::ios::trunc);
    f << o.str();
}

}  // namespace kitswap
}  // namespace turbo
