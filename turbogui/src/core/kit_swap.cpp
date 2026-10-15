// FC 27 LE Turbo GUI - 3D kits on the pitch: resolver, plan, block table, runtime (see kit_swap.h)
#include "kit_swap.h"

#include <algorithm>
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
};
TableBuilder::TableBuilder() : d_(new Impl) {}
TableBuilder::~TableBuilder() = default;
uint32_t TableBuilder::items() const { return uint32_t(d_->storage.size()); }
uint32_t TableBuilder::add_item(std::shared_ptr<std::vector<uint8_t>> data, bool swap) {
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

static uint64_t mix(uint32_t cs, uint32_t ds, uint64_t head) {
    uint64_t h = head * 0x9E3779B97F4A7C15ULL;
    h ^= (uint64_t(cs) << 32 | ds) * 0xC2B2AE3D27D4EB4FULL;
    h ^= h >> 29;
    return h;
}

std::shared_ptr<Table> TableBuilder::finish(std::vector<std::string>* dropped, std::set<uint32_t>* dropped_items) {
    auto& recs = d_->recs;
    // identical key: the same replacement bytes = one entry serving both items; different bytes = a conflict: every block of both
    // items goes (a half-swapped texture is worse than none)
    std::set<uint32_t> poisoned;
    std::map<size_t, std::vector<uint32_t>> aliases;  // survivor record -> items of its byte-identical duplicates
    std::vector<size_t> order(recs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    auto key = [&](size_t i) { return std::make_tuple(recs[i].e.csize, recs[i].e.dsize, recs[i].e.head, recs[i].e.fnv); };
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return key(a) < key(b) || (key(a) == key(b) && a < b); });
    for (size_t i = 0; i < order.size();) {
        size_t j = i + 1;
        while (j < order.size() && key(order[j]) == key(order[i])) ++j;
        // the survivor of a group of identical blocks: the first one that carries replacement bytes (a watch-only record never wins over a real one)
        size_t first = order[i];
        for (size_t k = i; k < j; ++k)
            if (recs[order[k]].e.data) { first = order[k]; break; }
        for (size_t k = i; k < j; ++k) {
            const size_t oi = order[k];
            if (oi == first) continue;
            auto& other = recs[oi];
            if (!other.e.data || !recs[first].e.data || std::memcmp(recs[first].e.data, other.e.data, other.e.dsize) == 0) {
                other.dead = true;  // identical block, same replacement (or watch only): one entry, both counters
                if (other.e.item != recs[first].e.item) aliases[first].push_back(other.e.item);
            } else {
                poisoned.insert(recs[first].e.item);
                poisoned.insert(other.e.item);
            }
        }
        i = j;
    }
    if (dropped_items) *dropped_items = poisoned;
    if (dropped)
        for (uint32_t it : poisoned) dropped->push_back("item " + std::to_string(it) + ": the same compressed block is also wanted with other bytes");
    auto t = std::make_shared<Table>();
    t->n_items_ = uint32_t(d_->storage.size());
    t->hits_.reset(new std::atomic<uint64_t>[t->n_items_ ? t->n_items_ : 1]);
    for (uint32_t i = 0; i < t->n_items_; ++i) t->hits_[i].store(0);
    t->first_ms_.reset(new std::atomic<uint64_t>[t->n_items_ ? t->n_items_ : 1]);
    t->last_ms_.reset(new std::atomic<uint64_t>[t->n_items_ ? t->n_items_ : 1]);
    for (uint32_t i = 0; i < t->n_items_; ++i) {
        t->first_ms_[i].store(0);
        t->last_ms_[i].store(0);
    }
    t->item_swap_ = d_->swap;
    t->storage_ = d_->storage;
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
    uint32_t cap = 16;
    while (cap < t->entries_.size() * 2 + 2) cap <<= 1;
    t->slots_.assign(cap, 0);
    t->mask_ = cap - 1;
    for (size_t i = 0; i < t->entries_.size(); ++i) {
        const Entry& e = t->entries_[i];
        uint32_t s = uint32_t(mix(e.csize, e.dsize, e.head)) & t->mask_;
        while (t->slots_[s]) s = (s + 1) & t->mask_;
        t->slots_[s] = uint32_t(i + 1);
    }
    return t;
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
}  // namespace

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
            plan.error = "the game has no 3D kit textures for this club's slot (nothing to swap)";
            return plan;
        }
        // 2. check each against its staged texture and the sharing rules
        std::map<std::string, const StagedTexture*> by_role;
        for (const auto& s : staged.textures) by_role[s.role] = &s;
        std::vector<Cand> cands;
        std::map<size_t, std::shared_ptr<std::vector<uint8_t>>> data_of;
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
            // sharing: every user of this physical chunk must be this club's same slot
            const auto us = index.users.find(phys_key(t.loc));
            bool shared = false;
            std::string who;
            std::set<std::string> club_kinds;  // this club's OTHER kit slots that use the same chunk
            if (us != index.users.end())
                for (const KitUser& u : us->second) {
                    if (u.team != staged.team || u.slot < 0) { shared = true; who = "team " + std::to_string(u.team) + " " + u.kind; break; }
                    if (u.slot != staged.slot) club_kinds.insert(u.kind);
                }
            if (shared) { skip("the chunk is also used by " + who + ": left to the game"); continue; }
            std::string club_note;
            if (!club_kinds.empty()) {
                std::string list;
                for (const auto& k : club_kinds) list += (list.empty() ? "" : ", ") + k;
                if (!opt.share_within_club) { skip("the chunk is also used by this club's " + list + ": left to the game (the option 'also change this club's other kits that share a texture' serves it)"); continue; }
                club_note = "shared with this club's " + list + ": those kits show it too";
            }
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
                else if (b.csize < opt.min_csize) { ok = false; it.reason = "compressed block of " + std::to_string(b.csize) + " bytes is uniform fill, too small to recognise safely"; }
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
            cands.push_back(std::move(c));
        }
        // 3. the block census: no OTHER kit chunk may hold a byte-identical compressed block
        if (opt.census && !cands.empty()) {
            std::map<std::pair<uint32_t, uint32_t>, std::vector<std::pair<size_t, size_t>>> want;  // (csize,dsize) -> (cand, block)
            std::set<std::string> own;
            for (size_t ci = 0; ci < cands.size(); ++ci) {
                own.insert(cands[ci].own_key);
                for (size_t bi = 0; bi < cands[ci].blocks.size(); ++bi) want[{cands[ci].blocks[bi].csize, cands[ci].blocks[bi].dsize}].push_back({ci, bi});
            }
            uint32_t min_c = 0xFFFFFFFFu;
            for (const auto& w : want) min_c = std::min(min_c, w.first.first);
            std::set<size_t> bad;
            std::map<size_t, std::string> bad_why;
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
                            if (cands[cb.first].fnv[cb.second] == f) {
                                bad.insert(cb.first);
                                if (!bad_why.count(cb.first)) {
                                    const KitUser* u = kv.second.empty() ? nullptr : &kv.second[0];
                                    bad_why[cb.first] = "a byte-identical compressed block is also in another kit chunk" + (u ? " (team " + std::to_string(u->team) + " " + u->kind + ")" : std::string());
                                }
                            }
                    }
                    p += 8 + cs;
                }
            }
            plan.notes.push_back("block census: " + std::to_string(scanned) + " other kit chunks checked");
            for (const Cand& c : cands)
                if (!c.note.empty() && !bad.count(size_t(&c - cands.data()))) plan.items[c.plan_index].reason = c.note;
            for (size_t ci : bad) {
                PlanItem& it = plan.items[cands[ci].plan_index];
                it.swap = false;
                it.reason = bad_why[ci];
                ++plan.skipped;
                cands[ci].blocks.clear();
            }
        }
        // 4. the table
        for (Cand& c : cands) {
            if (c.blocks.empty()) continue;
            PlanItem& it = plan.items[c.plan_index];
            const uint32_t id = tb.add_item(data_of[c.plan_index], swap);
            uint64_t at = 0;
            const uint8_t* base = data_of[c.plan_index]->data();
            for (size_t b = 0; b < c.blocks.size(); ++b) {
                tb.add_block(id, c.blocks[b].csize, c.blocks[b].dsize, c.head[b], c.fnv[b], base + at, nullptr);
                at += c.blocks[b].dsize;
            }
            it.table_item = id;
            ++plan.swapped;
        }
        // 5. the hotspot asset(s): the zip's name / number / sleeve-badge placement
        if (opt.apply_hotspots && !staged.hotspots.empty()) {
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
                        plan.items.push_back(it);
                        ++plan.skipped;
                    };
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
        // item ids are the builder's: keep the plan's own numbering out of the table (hits are looked up by the Runtime)
        plan.ok = true;
    } catch (const std::exception& e) {
        plan.ok = false;
        plan.error = std::string("plan failed: ") + e.what();
    }
    return plan;
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
                             {"keep_game_crest", s.keep_crest}, {"share_within_club", s.share_club}});
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

bool Runtime::set_slot_options(int team, int slot, bool decals, bool keep_crest, bool share_club) {
    {
        std::lock_guard<std::mutex> lk(m_);
        bool found = false;
        for (auto& s : slots_)
            if (s.team == team && s.slot == slot) {
                s.decals = decals;
                s.keep_crest = keep_crest;
                s.share_club = share_club;
                found = true;
            }
        if (!found) return false;
        save_state_locked();
    }
    rebuild_async();
    return true;
}

bool Runtime::slot_options(int team, int slot, bool* decals, bool* keep_crest, bool* share_club) const {
    std::lock_guard<std::mutex> lk(m_);
    for (const auto& s : slots_)
        if (s.team == team && s.slot == slot) {
            if (decals) *decals = s.decals;
            if (keep_crest) *keep_crest = s.keep_crest;
            if (share_club) *share_club = s.share_club;
            return true;
        }
    return false;
}

std::set<std::string> Runtime::kinds_of(int team) const {
    std::lock_guard<std::mutex> lk(m_);
    if (!index_) return {};
    auto it = index_->kinds.find(team);
    return it == index_->kinds.end() ? std::set<std::string>() : it->second;
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
    rebuild_async();
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
    state_ = "building";
    message_ = "reading the game's kit data";
    if (running_) {
        progress_.cancel = true;  // the running build restarts with the new state
        return;
    }
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
            return;
        }
        if (killed_.load() || (require_opt_in && !opted_in())) {  // no scan, no table while the kill switch / a missing opt-in says so
            std::lock_guard<std::mutex> lk(m_);
            state_ = "off";
            message_ = killed_.load() ? "kill switch turbo_output\\kits3d_off.txt is present: nothing is read or swapped" : "turbo_output\\kits3d_on.txt is missing: nothing is read or swapped";
            build_skipped_ = true;
            running_ = false;
            return;
        }
        const int gen = generation_.load();
        progress_.cancel = false;
        progress_.done = 0;
        progress_.total = 0;
        std::vector<SlotState> slots;
        {
            std::lock_guard<std::mutex> lk(m_);
            slots = slots_;
        }
        std::string state, message;
        std::vector<ItemStatus> items;
        std::shared_ptr<Table> table;
        double idx_s = 0;
        if (slots.empty()) {
            state = "off";
        } else {
            GameFiles files(game_);
            std::string err;
            std::shared_ptr<KitIndex> index;
            {
                std::lock_guard<std::mutex> lk(m_);
                index = index_;
            }
            if (!oodle_) {
                state = "error";
                message = "no Oodle decompressor in this process";
            } else if (!files.init(&err)) {
                state = "error";
                message = err;
            } else {
                if (!index) {
                    auto ni = std::make_shared<KitIndex>();
                    if (load_index_cache(files, index_cache_path(), *ni)) {  // the game's tocs are unchanged since the last scan
                        index = ni;
                        std::lock_guard<std::mutex> lk(m_);
                        index_ = ni;
                        index_cached_ = true;
                    } else if (scan_kits(files, oodle_, *ni, &progress_, &err)) {
                        index = ni;
                        save_index_cache(files, *ni, index_cache_path());
                        std::lock_guard<std::mutex> lk(m_);
                        index_ = ni;
                        index_cached_ = false;
                    } else if (err != "cancelled") {
                        state = "error";
                        message = "reading the game's kit data failed: " + err;
                    }
                }
                if (index && state != "error") {
                    idx_s = index->seconds;
                    TableBuilder tb;
                    const Options base_opts = get_options();
                    int swapped = 0, skipped = 0;
                    std::vector<std::string> msgs;
                    for (const SlotState& s : slots) {
                        if (progress_.cancel.load()) break;
                        StagedKit sk;
                        std::string e;
                        if (!load_staged(staged_dir(s.team, s.slot), sk, &e)) {
                            msgs.push_back("team " + std::to_string(s.team) + " " + slot_folder_name(s.slot) + ": " + e);
                            continue;
                        }
                        Options opts = base_opts;
                        opts.include_decals = s.decals;
                        opts.keep_game_crest = s.keep_crest;
                        opts.share_within_club = s.share_club;
                        Plan pl = build_plan(files, oodle_, *index, sk, opts, s.mode == Mode::Swap, tb, &progress_);
                        if (!pl.ok) {
                            msgs.push_back("team " + std::to_string(s.team) + " " + slot_folder_name(s.slot) + ": " + pl.error);
                            continue;
                        }
                        swapped += pl.swapped;
                        skipped += pl.skipped;
                        for (const auto& it : pl.items) {
                            ItemStatus st;
                            st.team = s.team;
                            st.slot = s.slot;
                            st.res = it.res;
                            st.kind = it.kind;
                            st.role = it.role;
                            st.reason = it.reason;
                            st.swap = it.swap;
                            st.probe = it.probe;
                            st.blocks = it.blocks;
                            st.table_item = it.table_item;
                            if (!it.swap && !it.probe) {  // the same resource left to the game at several places (big clubs): one line
                                bool merged = false;
                                for (auto& o : items)
                                    if (!o.swap && !o.probe && o.team == st.team && o.slot == st.slot && o.res == st.res && o.reason == st.reason) { ++o.copies; merged = true; break; }
                                if (merged) continue;
                            }
                            items.push_back(st);
                        }
                    }
                    if (!progress_.cancel.load()) {
                        std::vector<std::string> dropped;
                        std::set<uint32_t> dropped_items;
                        table = tb.finish(&dropped, &dropped_items);
                        for (auto& it : items)
                            if (it.table_item != 0xFFFFFFFFu && dropped_items.count(it.table_item)) {
                                it.swap = false;
                                it.table_item = 0xFFFFFFFFu;
                                it.reason = "the game uses the same data for two of the zip's pictures (for example crest and crest_shorts): left to the game";
                                --swapped;
                                ++skipped;
                            }
                        dropped.clear();
                        state = "ready";
                        message = std::to_string(swapped) + " textures in the table, " + std::to_string(skipped) + " left to the game";
                        for (const auto& m : msgs) message += "; " + m;
                        for (const auto& d : dropped) message += "; " + d;
                        if (table->size() == 0) message += " (nothing matchable)";
                    }
                }
            }
        }
        if (generation_.load() != gen || progress_.cancel.load()) continue;  // a newer request: build again
        {
            std::lock_guard<std::mutex> lk(m_);
            state_ = state;
            message_ = message;
            index_seconds_ = idx_s;
            build_skipped_ = false;
            items_ = items;
            swap_any_ = false;
            for (const auto& s : slots_) swap_any_ = swap_any_.load() || s.mode == Mode::Swap;
        }
        publish(table);
        {
            std::lock_guard<std::mutex> lk(m_);
            if (generation_.load() != gen) continue;
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
    for (auto& it : s.items) {
        const bool in = t && it.table_item != 0xFFFFFFFFu;
        it.hits = in ? t->hits(it.table_item) : 0;
        it.first_ms = in ? t->first_ms(it.table_item) : 0;
        it.last_ms = in ? t->last_ms(it.table_item) : 0;
    }
    s.index_cached = index_cached_;
    s.scanning = running_ && !index_;
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
          << (sl.keep_crest ? "kept" : "replaced by the zip's") << ", share within club " << (sl.share_club ? "on" : "off") << "\n";
    o << "kit index: " << (s.index_cached ? "from turbo_output\\kits\\kit_index.cache" : "scanned from the game's tocs") << "\n";
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
