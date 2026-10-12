// FC 27 LE Turbo GUI - kits from FIFA Kit Creator zip downloads (see kit_import.h)
#include "kit_import.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

#include "nlohmann/json.hpp"
#include "reapply.h"

// declarations only: the implementations are compiled once, in image.cpp
#include "stb/stb_dxt.h"
#include "stb/stb_image.h"

namespace turbo {
namespace kits {

namespace fs = std::filesystem;
using nlohmann::json;

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
static void wr32(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) d[at + size_t(i)] = uint8_t(v >> (8 * i));
}

// ================================================================ zip
uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static bool zfail(std::string* err, const std::string& why) {
    if (err) *err = why;
    return false;
}

bool Zip::open(std::vector<uint8_t> bytes, std::string* err) {
    bytes_ = std::move(bytes);
    entries_.clear();
    const size_t n = bytes_.size();
    if (n < 22) return zfail(err, "not a zip file (too short)");
    if (n > kMaxFile) return zfail(err, "the zip is bigger than 512 MB");
    // end of central directory: the last record with its signature (a comment of up to 64 KB may follow it)
    size_t eocd = std::string::npos;
    const size_t lowest = n > 22 + 65535 ? n - 22 - 65535 : 0;
    for (size_t i = n - 22 + 1; i-- > lowest;) {
        if (rd32(&bytes_[i]) == 0x06054b50u && i + 22 + rd16(&bytes_[i + 20]) <= n) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) return zfail(err, "not a zip file (no end of central directory)");
    const uint8_t* e = &bytes_[eocd];
    const uint16_t disk = rd16(e + 4), cd_disk = rd16(e + 6), count = rd16(e + 10);
    const uint32_t cd_size = rd32(e + 12), cd_off = rd32(e + 16);
    if (disk != 0 || cd_disk != 0) return zfail(err, "split (multi-part) zips are not supported");
    if (count == 0xFFFF || cd_off == 0xFFFFFFFFu || cd_size == 0xFFFFFFFFu) return zfail(err, "ZIP64 archives are not supported");
    if (count > kMaxEntries) return zfail(err, "the zip holds more than " + std::to_string(kMaxEntries) + " files");
    if (uint64_t(cd_off) + cd_size > eocd) return zfail(err, "the zip is cut off (central directory outside the file)");
    size_t p = cd_off;
    for (uint16_t i = 0; i < count; ++i) {
        if (p + 46 > eocd || rd32(&bytes_[p]) != 0x02014b50u) return zfail(err, "the zip's central directory is damaged");
        const uint8_t* c = &bytes_[p];
        const uint16_t flags = rd16(c + 8), method = rd16(c + 10), nlen = rd16(c + 28), xlen = rd16(c + 30), clen = rd16(c + 32);
        ZipEntry z;
        z.method = method;
        z.crc = rd32(c + 16);
        z.csize = rd32(c + 20);
        z.size = rd32(c + 24);
        const uint32_t lho = rd32(c + 42);
        if (p + 46 + nlen + xlen + clen > eocd) return zfail(err, "the zip's central directory is damaged");
        z.name.assign(reinterpret_cast<const char*>(c + 46), nlen);
        p += 46 + size_t(nlen) + xlen + clen;
        std::replace(z.name.begin(), z.name.end(), '\\', '/');
        if (!z.name.empty() && z.name.back() == '/') continue;  // a folder
        if (flags & 1) return zfail(err, z.name + " is encrypted (password-protected zips are not supported)");
        if (z.csize == 0xFFFFFFFFu || z.size == 0xFFFFFFFFu || lho == 0xFFFFFFFFu) return zfail(err, "ZIP64 archives are not supported");
        if (z.name.empty() || z.name[0] == '/' || z.name.find(':') != std::string::npos || ("/" + z.name + "/").find("/../") != std::string::npos)
            return zfail(err, "unsafe file name in the zip: " + z.name);
        if (z.size > kMaxEntry) return zfail(err, z.name + " is bigger than 96 MB");
        if (uint64_t(lho) + 30 > n || rd32(&bytes_[lho]) != 0x04034b50u) return zfail(err, z.name + ": local header missing (damaged zip)");
        z.data = uint64_t(lho) + 30 + rd16(&bytes_[lho + 26]) + rd16(&bytes_[lho + 28]);
        if (z.data + z.csize > n) return zfail(err, z.name + " is cut off");
        entries_.push_back(std::move(z));
    }
    return true;
}

bool Zip::load_file(const fs::path& path, std::string* err) {
    std::error_code ec;
    const uintmax_t sz = fs::file_size(path, ec);
    if (ec) return zfail(err, "cannot read the file (" + ec.message() + ")");
    if (sz > kMaxFile) return zfail(err, "the zip is bigger than 512 MB");
    std::ifstream f(path, std::ios::binary);
    if (!f) return zfail(err, "cannot open the file");
    std::vector<uint8_t> b(static_cast<size_t>(sz));
    if (sz && !f.read(reinterpret_cast<char*>(b.data()), std::streamsize(sz))) return zfail(err, "cannot read the file");
    return open(std::move(b), err);
}

const ZipEntry* Zip::find(const std::string& name) const {
    const std::string want = lower(name);
    const ZipEntry* in_folder = nullptr;
    for (const ZipEntry& e : entries_) {
        const std::string n = lower(e.name);
        if (n == want) return &e;
        const size_t slash = n.find('/');
        if (!in_folder && slash != std::string::npos && n.find('/', slash + 1) == std::string::npos && n.substr(slash + 1) == want) in_folder = &e;
    }
    return in_folder;
}

bool Zip::read(const ZipEntry& e, std::vector<uint8_t>& out, std::string* err) const {
    out.clear();
    if (e.data + e.csize > bytes_.size()) return zfail(err, e.name + " is cut off");
    const uint8_t* src = bytes_.data() + e.data;
    if (e.method == 0) {
        if (e.csize != e.size) return zfail(err, e.name + ": stored size and file size differ (damaged zip)");
        out.assign(src, src + e.size);
    } else if (e.method == 8) {
        out.resize(static_cast<size_t>(e.size));
        if (e.size) {
            const int got = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(out.data()), int(out.size()), reinterpret_cast<const char*>(src),
                                                             int(e.csize));
            if (got < 0 || uint64_t(got) != e.size) return zfail(err, e.name + ": cannot unpack the data (damaged zip)");
        }
    } else {
        return zfail(err, e.name + ": compression method " + std::to_string(e.method) + " is not supported (stored and deflate only)");
    }
    if (crc32(out.data(), out.size()) != e.crc) return zfail(err, e.name + ": CRC mismatch (damaged zip)");
    return true;
}

// ================================================================ slots
int slot_type(Slot s) { return s == Slot::Unknown ? -1 : static_cast<int>(s); }

Slot slot_of_type(int64_t type) {
    switch (type) {
        case 0: return Slot::Home;
        case 1: return Slot::Away;
        case 2: return Slot::Goalkeeper;
        case 3: return Slot::Third;
        case 4: return Slot::Fourth;
        default: return Slot::Unknown;
    }
}

const char* slot_folder(Slot s) {
    switch (s) {
        case Slot::Home: return "home";
        case Slot::Away: return "away";
        case Slot::Goalkeeper: return "goalkeeper";
        case Slot::Third: return "third";
        case Slot::Fourth: return "fourth";
        default: return "unknown";
    }
}

const char* slot_label(Slot s) {
    switch (s) {
        case Slot::Home: return "Home";
        case Slot::Away: return "Away";
        case Slot::Goalkeeper: return "Goalkeeper";
        case Slot::Third: return "Third";
        case Slot::Fourth: return "Fourth";
        default: return "(pick a slot)";
    }
}

const char* slot_kind(Slot s) {
    switch (s) {
        case Slot::Home: return "home";
        case Slot::Away: return "away";
        case Slot::Goalkeeper: return "goalie_home";
        case Slot::Third: return "third";
        case Slot::Fourth: return "fourth";
        default: return "";
    }
}

static Slot slot_word(const std::string& w) {
    if (w == "h" || w == "home") return Slot::Home;
    if (w == "a" || w == "away") return Slot::Away;
    if (w == "g" || w == "gk" || w == "goalkeeper" || w == "goalie" || w == "keeper") return Slot::Goalkeeper;
    if (w == "t" || w == "third" || w == "3rd") return Slot::Third;
    if (w == "f" || w == "fourth" || w == "4th") return Slot::Fourth;
    return Slot::Unknown;
}

static Slot slot_of_name(const std::string& stem0) {
    const std::string stem = lower(stem0);
    Slot s = slot_word(stem);
    if (s != Slot::Unknown) return s;
    const size_t cut = stem.find_last_of("_- .");
    // the last word only when it is a whole word ("ado_den_haag_away"); one-letter words need the full name ("h")
    if (cut != std::string::npos && stem.size() - cut - 1 > 1) return slot_word(stem.substr(cut + 1));
    return Slot::Unknown;
}

Slot slot_from_path(const fs::path& zip) {
    Slot s = slot_of_name(zip.stem().string());
    if (s != Slot::Unknown) return s;
    return slot_of_name(zip.parent_path().filename().string());
}

// ================================================================ texture spec
uint32_t dxgi_of(Tex t) {
    switch (t) {
        case Tex::BC1: return 71;
        case Tex::BC1_SRGB: return 72;
        case Tex::BC3: return 77;
        case Tex::BC3_SRGB: return 78;
        case Tex::BC5: return 83;
        case Tex::BC7: return 98;
    }
    return 0;
}

const char* tex_name(Tex t) {
    switch (t) {
        case Tex::BC1: return "BC1_UNORM";
        case Tex::BC1_SRGB: return "BC1_UNORM_SRGB";
        case Tex::BC3: return "BC3_UNORM";
        case Tex::BC3_SRGB: return "BC3_UNORM_SRGB";
        case Tex::BC5: return "BC5_UNORM";
        case Tex::BC7: return "BC7_UNORM";
    }
    return "?";
}

static size_t block_bytes(Tex t) { return t == Tex::BC1 || t == Tex::BC1_SRGB ? 8 : 16; }

int full_mips(int w, int h) {
    int n = 1, m = std::max(w, h);
    while (m > 1) {
        m >>= 1;
        ++n;
    }
    return n;
}

const std::vector<TexSpec>& fc27_kit_spec() {
    // FC 27's own headers (Arsenal home, Data\Win32\contentsb.toc bundles 15416 / 15701 / 27645): format ids 55, 54, 63, 66,
    // 61, 60 with 0.5 / 1 bytes per pixel = BC1 sRGB, BC1, BC5, BC7, BC3 sRGB, BC3 (Frostbite RenderFormat order)
    static const std::vector<TexSpec> s = [] {
        std::vector<TexSpec> v;
        struct Part { const char* name; int w, h; };
        for (const Part& p : {Part{"jersey", 2048, 2048}, Part{"shorts", 1024, 512}, Part{"socks", 512, 256}}) {
            // the colour map is twice the size of the others on the jersey only (2048 vs 1024); shorts and socks use one size
            const bool jersey = std::string(p.name) == "jersey";
            const int cw = p.w, ch = p.h, ow = jersey ? p.w / 2 : p.w, oh = jersey ? p.h / 2 : p.h;
            v.push_back({std::string(p.name) + "_color", p.name, cw, ch, Tex::BC1_SRGB, false});
            v.push_back({std::string(p.name) + "_coeff", p.name, ow, oh, Tex::BC1, false});
            v.push_back({std::string(p.name) + "_normal", p.name, ow, oh, Tex::BC5, false});
            v.push_back({std::string(p.name) + "_taa", p.name, ow, oh, Tex::BC7, false});
        }
        for (const char* d : {"brand_jersey", "crest", "brand_shorts", "crest_shorts"}) {
            const std::string part = std::string(d).find("shorts") != std::string::npos ? "shorts" : "jersey";
            v.push_back({std::string(d) + "_color", part, 256, 256, Tex::BC3_SRGB, true});
            v.push_back({std::string(d) + "_normal", part, 256, 256, Tex::BC5, true});
            v.push_back({std::string(d) + "_coeff", part, 128, 128, Tex::BC3, true});
        }
        return v;
    }();
    return s;
}

std::string game_asset_path(const TexSpec& t, int64_t teamid, Slot s, const std::string& slug) {
    const size_t cut = t.role.rfind('_');
    const std::string base = cut == std::string::npos ? t.role : t.role.substr(0, cut);
    const std::string suffix = cut == std::string::npos ? std::string() : t.role.substr(cut + 1);
    const std::string id = std::to_string(teamid), type = std::to_string(slot_type(s));
    return "content/character/kit/kit_" + std::to_string(teamid / 500 * 500) + "/" + (slug.empty() ? std::string("<slug>") : slug) + "_" + id + "/" +
           slot_kind(s) + "_" + type + "_0/" + t.part + "/" + base + "_" + id + "_" + type + "_0_" + suffix;
}

std::string minikit_path(int64_t teamid, Slot s) {
    return "data/ui/imgAssets/kits/j" + std::to_string(slot_type(s)) + "_" + std::to_string(teamid) + "_0.dds";
}

// ================================================================ pixels
Rgba downscale_box(const Rgba& src, int w, int h) {
    Rgba out;
    if (src.empty() || w <= 0 || h <= 0 || src.w % w || src.h % h) return out;
    const int fx = src.w / w, fy = src.h / h;
    out.w = w;
    out.h = h;
    out.px.resize(size_t(w) * size_t(h) * 4);
    const uint32_t n = uint32_t(fx * fy);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint32_t acc[4] = {0, 0, 0, 0};
            for (int yy = 0; yy < fy; ++yy) {
                const uint8_t* s = src.at(x * fx, y * fy + yy);
                for (int xx = 0; xx < fx; ++xx, s += 4)
                    for (int c = 0; c < 4; ++c) acc[c] += s[c];
            }
            uint8_t* o = out.at(x, y);
            for (int c = 0; c < 4; ++c) o[c] = uint8_t((acc[c] + n / 2) / n);
        }
    return out;
}

// the next mip level: a 2 x 2 box (2 x 1 / 1 x 2 when a side is already 1)
static Rgba half_level(const Rgba& s) {
    Rgba o;
    o.w = std::max(1, s.w / 2);
    o.h = std::max(1, s.h / 2);
    o.px.resize(size_t(o.w) * size_t(o.h) * 4);
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x) {
            const int x0 = std::min(x * 2, s.w - 1), x1 = std::min(x * 2 + 1, s.w - 1);
            const int y0 = std::min(y * 2, s.h - 1), y1 = std::min(y * 2 + 1, s.h - 1);
            uint8_t* d = o.at(x, y);
            for (int c = 0; c < 4; ++c)
                d[c] = uint8_t((uint32_t(s.at(x0, y0)[c]) + s.at(x1, y0)[c] + s.at(x0, y1)[c] + s.at(x1, y1)[c] + 2) / 4);
        }
    return o;
}

// ================================================================ BC7 mode 6
static const int kW4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

static void put_bits(uint8_t* b, int& pos, uint32_t v, int n) {
    for (int i = 0; i < n; ++i, ++pos)
        if ((v >> i) & 1u) b[pos >> 3] = uint8_t(b[pos >> 3] | (1u << (pos & 7)));
}
static uint32_t get_bits(const uint8_t* b, int& pos, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i, ++pos) v |= uint32_t((b[pos >> 3] >> (pos & 7)) & 1u) << i;
    return v;
}

// 7-bit endpoint + shared p-bit closest to e (8-bit channels)
static void quantize_endpoint(const float e[4], int c7[4], int& p) {
    float best = 1e30f;
    for (int pp = 0; pp < 2; ++pp) {
        float err = 0.0f;
        int q[4];
        for (int c = 0; c < 4; ++c) {
            q[c] = std::clamp(int(std::lround((e[c] - float(pp)) / 2.0f)), 0, 127);
            const float d = float(q[c] * 2 + pp) - e[c];
            err += d * d;
        }
        if (err < best) {
            best = err;
            p = pp;
            for (int c = 0; c < 4; ++c) c7[c] = q[c];
        }
    }
}

// indices for the endpoints (8-bit after the p-bit); returns the squared error
static uint32_t pick_indices(const uint8_t px[16][4], const int e0[4], const int e1[4], int idx[16]) {
    int pal[16][4];
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 4; ++c) pal[i][c] = ((64 - kW4[i]) * e0[c] + kW4[i] * e1[c] + 32) >> 6;
    uint32_t total = 0;
    for (int t = 0; t < 16; ++t) {
        uint32_t best = ~0u;
        int bi = 0;
        for (int i = 0; i < 16; ++i) {
            uint32_t d = 0;
            for (int c = 0; c < 4; ++c) {
                const int k = pal[i][c] - px[t][c];
                d += uint32_t(k * k);
            }
            if (d < best) {
                best = d;
                bi = i;
            }
        }
        idx[t] = bi;
        total += best;
    }
    return total;
}

void encode_bc7_mode6_block(const uint8_t px[16][4], uint8_t out[16]) {
    float mean[4] = {0, 0, 0, 0};
    for (int t = 0; t < 16; ++t)
        for (int c = 0; c < 4; ++c) mean[c] += px[t][c];
    for (float& m : mean) m /= 16.0f;
    float cov[4][4] = {};
    float lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
    for (int t = 0; t < 16; ++t) {
        float d[4];
        for (int c = 0; c < 4; ++c) {
            d[c] = px[t][c] - mean[c];
            lo[c] = std::min(lo[c], float(px[t][c]));
            hi[c] = std::max(hi[c], float(px[t][c]));
        }
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b) cov[a][b] += d[a] * d[b];
    }
    // principal axis by power iteration, started from the bounding box diagonal
    float axis[4];
    float len = 0.0f;
    for (int c = 0; c < 4; ++c) {
        axis[c] = hi[c] - lo[c];
        len += axis[c] * axis[c];
    }
    float e0f[4], e1f[4];
    if (len < 1e-6f) {
        for (int c = 0; c < 4; ++c) e0f[c] = e1f[c] = mean[c];
    } else {
        for (int it = 0; it < 8; ++it) {
            float v[4] = {0, 0, 0, 0};
            for (int a = 0; a < 4; ++a)
                for (int b = 0; b < 4; ++b) v[a] += cov[a][b] * axis[b];
            float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
            if (n < 1e-6f) break;
            for (int c = 0; c < 4; ++c) axis[c] = v[c] / n;
        }
        float n = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2] + axis[3] * axis[3]);
        for (float& a : axis) a /= n;
        float tmin = 1e30f, tmax = -1e30f;
        for (int t = 0; t < 16; ++t) {
            float s = 0.0f;
            for (int c = 0; c < 4; ++c) s += (px[t][c] - mean[c]) * axis[c];
            tmin = std::min(tmin, s);
            tmax = std::max(tmax, s);
        }
        for (int c = 0; c < 4; ++c) {
            e0f[c] = std::clamp(mean[c] + tmin * axis[c], 0.0f, 255.0f);
            e1f[c] = std::clamp(mean[c] + tmax * axis[c], 0.0f, 255.0f);
        }
    }
    auto build = [&](const float a[4], const float b[4], int c0[4], int c1[4], int& p0, int& p1, int idx[16]) {
        quantize_endpoint(a, c0, p0);
        quantize_endpoint(b, c1, p1);
        int e0[4], e1[4];
        for (int c = 0; c < 4; ++c) {
            e0[c] = c0[c] * 2 + p0;
            e1[c] = c1[c] * 2 + p1;
        }
        return pick_indices(px, e0, e1, idx);
    };
    int c0[4], c1[4], p0 = 0, p1 = 0, idx[16];
    uint32_t err = build(e0f, e1f, c0, c1, p0, p1, idx);
    // one least-squares refinement of the endpoints for the chosen indices
    if (err > 0) {
        double aa = 0, ab = 0, bb = 0, ax[4] = {0, 0, 0, 0}, bx[4] = {0, 0, 0, 0};
        for (int t = 0; t < 16; ++t) {
            const double w = kW4[idx[t]] / 64.0, v = 1.0 - w;
            aa += v * v;
            ab += v * w;
            bb += w * w;
            for (int c = 0; c < 4; ++c) {
                ax[c] += v * px[t][c];
                bx[c] += w * px[t][c];
            }
        }
        const double det = aa * bb - ab * ab;
        if (std::fabs(det) > 1e-9) {
            float r0[4], r1[4];
            for (int c = 0; c < 4; ++c) {
                r0[c] = float(std::clamp((ax[c] * bb - bx[c] * ab) / det, 0.0, 255.0));
                r1[c] = float(std::clamp((bx[c] * aa - ax[c] * ab) / det, 0.0, 255.0));
            }
            int d0[4], d1[4], q0 = 0, q1 = 0, jdx[16];
            const uint32_t err2 = build(r0, r1, d0, d1, q0, q1, jdx);
            if (err2 < err) {
                std::memcpy(c0, d0, sizeof(c0));
                std::memcpy(c1, d1, sizeof(c1));
                std::memcpy(idx, jdx, sizeof(idx));
                p0 = q0;
                p1 = q1;
            }
        }
    }
    // the first texel's index has its top bit implied 0: swap the endpoints when it is 8 or more
    if (idx[0] >= 8) {
        for (int c = 0; c < 4; ++c) std::swap(c0[c], c1[c]);
        std::swap(p0, p1);
        for (int& i : idx) i = 15 - i;
    }
    std::memset(out, 0, 16);
    int pos = 0;
    put_bits(out, pos, 1u << 6, 7);  // mode 6
    for (int c = 0; c < 4; ++c) {
        put_bits(out, pos, uint32_t(c0[c]), 7);
        put_bits(out, pos, uint32_t(c1[c]), 7);
    }
    put_bits(out, pos, uint32_t(p0), 1);
    put_bits(out, pos, uint32_t(p1), 1);
    put_bits(out, pos, uint32_t(idx[0]), 3);
    for (int t = 1; t < 16; ++t) put_bits(out, pos, uint32_t(idx[t]), 4);
}

bool decode_bc7_mode6_block(const uint8_t b[16], uint8_t out[16][4]) {
    if ((b[0] & 0x7F) != 0x40) return false;
    int pos = 7;
    int c0[4], c1[4];
    for (int c = 0; c < 4; ++c) {
        c0[c] = int(get_bits(b, pos, 7));
        c1[c] = int(get_bits(b, pos, 7));
    }
    const int p0 = int(get_bits(b, pos, 1)), p1 = int(get_bits(b, pos, 1));
    for (int t = 0; t < 16; ++t) {
        const int i = int(get_bits(b, pos, t == 0 ? 3 : 4));
        for (int c = 0; c < 4; ++c) {
            const int e0 = c0[c] * 2 + p0, e1 = c1[c] * 2 + p1;
            out[t][c] = uint8_t(((64 - kW4[i]) * e0 + kW4[i] * e1 + 32) >> 6);
        }
    }
    return true;
}

// ---- BC4 / BC5 decode (preview and tests)
static void decode_bc4(const uint8_t* b, uint8_t out[16]) {
    const int a0 = b[0], a1 = b[1];
    int pal[8];
    pal[0] = a0;
    pal[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
    } else {
        for (int i = 1; i < 5; ++i) pal[i + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
        pal[6] = 0;
        pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int t = 0; t < 16; ++t) out[t] = uint8_t(pal[(bits >> (3 * t)) & 7]);
}

// ================================================================ DDS
static void encode_block(Tex fmt, const uint8_t tex[16][4], uint8_t* dst) {
    switch (fmt) {
        case Tex::BC1:
        case Tex::BC1_SRGB: stb_compress_dxt_block(dst, &tex[0][0], 0, STB_DXT_HIGHQUAL); break;
        case Tex::BC3:
        case Tex::BC3_SRGB: stb_compress_dxt_block(dst, &tex[0][0], 1, STB_DXT_HIGHQUAL); break;
        case Tex::BC5: {
            uint8_t rg[32];
            for (int t = 0; t < 16; ++t) {
                rg[2 * t] = tex[t][0];
                rg[2 * t + 1] = tex[t][1];
            }
            stb_compress_bc5_block(dst, rg);
            break;
        }
        case Tex::BC7: encode_bc7_mode6_block(tex, dst); break;
    }
}

std::vector<uint8_t> encode_kit_dds(const Rgba& img, Tex fmt, int mips, std::string* err) {
    if (img.empty()) {
        if (err) *err = "no picture";
        return {};
    }
    if (mips < 1 || mips > full_mips(img.w, img.h)) {
        if (err) *err = "mip count " + std::to_string(mips) + " out of range for " + std::to_string(img.w) + " x " + std::to_string(img.h);
        return {};
    }
    const size_t bsz = block_bytes(fmt);
    std::vector<uint8_t> d(148, 0);
    std::memcpy(d.data(), "DDS ", 4);
    wr32(d, 4, 124);
    // CAPS | HEIGHT | WIDTH | PIXELFORMAT | MIPMAPCOUNT | LINEARSIZE
    wr32(d, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);
    wr32(d, 12, uint32_t(img.h));
    wr32(d, 16, uint32_t(img.w));
    wr32(d, 20, uint32_t(size_t((img.w + 3) / 4) * size_t((img.h + 3) / 4) * bsz));
    wr32(d, 28, uint32_t(mips));
    wr32(d, 76, 32);           // pixel format size
    wr32(d, 80, 0x4);          // DDPF_FOURCC
    std::memcpy(&d[84], "DX10", 4);
    wr32(d, 108, 0x1000 | (mips > 1 ? 0x400008u : 0u));  // TEXTURE (| MIPMAP | COMPLEX)
    wr32(d, 128, dxgi_of(fmt));
    wr32(d, 132, 3);           // TEXTURE2D
    wr32(d, 140, 1);           // array size
    Rgba level = img;
    for (int m = 0; m < mips; ++m) {
        if (m) level = half_level(level);
        const int bw = (level.w + 3) / 4, bh = (level.h + 3) / 4;
        const size_t at = d.size();
        d.resize(at + size_t(bw) * size_t(bh) * bsz);
        uint8_t tex[16][4];
        for (int by = 0; by < bh; ++by)
            for (int bx = 0; bx < bw; ++bx) {
                for (int t = 0; t < 16; ++t) {
                    const int x = std::min(bx * 4 + (t & 3), level.w - 1), y = std::min(by * 4 + (t >> 2), level.h - 1);
                    std::memcpy(tex[t], level.at(x, y), 4);
                }
                encode_block(fmt, tex, &d[at + (size_t(by) * size_t(bw) + size_t(bx)) * bsz]);
            }
    }
    return d;
}

bool decode_kit_dds(const std::vector<uint8_t>& bytes, Rgba& top, KitDds* info, std::string* err) {
    if (bytes.size() < 148 || std::memcmp(bytes.data(), "DDS ", 4) != 0 || rd32(&bytes[4]) != 124 || std::memcmp(&bytes[84], "DX10", 4) != 0)
        return zfail(err, "not a DX10 DDS file");
    KitDds k;
    k.h = int(rd32(&bytes[12]));
    k.w = int(rd32(&bytes[16]));
    k.mips = std::max(1, int(rd32(&bytes[28])));
    k.dxgi = rd32(&bytes[128]);
    k.data = 148;
    if (info) *info = k;
    if (k.w <= 0 || k.h <= 0 || k.w > kMaxImageSide || k.h > kMaxImageSide) return zfail(err, "DDS size out of range");
    if (k.dxgi == 71 || k.dxgi == 72 || k.dxgi == 77 || k.dxgi == 78) return decode_image(bytes, top, err);
    if (k.dxgi != 83 && k.dxgi != 98) return zfail(err, "DXGI format " + std::to_string(k.dxgi) + " is not one Turbo writes for kits");
    const int bw = (k.w + 3) / 4, bh = (k.h + 3) / 4;
    if (bytes.size() < k.data + size_t(bw) * size_t(bh) * 16) return zfail(err, "DDS data is cut off");
    top.w = k.w;
    top.h = k.h;
    top.px.assign(size_t(k.w) * size_t(k.h) * 4, 0);
    const uint8_t* p = &bytes[k.data];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx, p += 16) {
            uint8_t tex[16][4];
            if (k.dxgi == 83) {
                uint8_t r[16], g[16];
                decode_bc4(p, r);
                decode_bc4(p + 8, g);
                for (int t = 0; t < 16; ++t) {
                    tex[t][0] = r[t];
                    tex[t][1] = g[t];
                    tex[t][2] = 0;
                    tex[t][3] = 255;
                }
            } else if (!decode_bc7_mode6_block(p, tex)) {
                for (auto& t : tex) t[0] = 255, t[1] = 0, t[2] = 255, t[3] = 255;
            }
            for (int t = 0; t < 16; ++t) {
                const int x = bx * 4 + (t & 3), y = by * 4 + (t >> 2);
                if (x < k.w && y < k.h) std::memcpy(top.at(x, y), tex[t], 4);
            }
        }
    return true;
}

// ================================================================ settings.json
bool parse_hex_colour(const std::string& s0, uint8_t rgb[3]) {
    std::string s = s0;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i < s.size() && s[i] == '#') ++i;
    if (s.size() - i != 6) return false;
    for (int c = 0; c < 3; ++c) {
        int v = 0;
        for (int k = 0; k < 2; ++k) {
            const char ch = char(std::tolower(static_cast<unsigned char>(s[i + size_t(c) * 2 + size_t(k)])));
            int d;
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else return false;
            v = v * 16 + d;
        }
        rgb[c] = uint8_t(v);
    }
    return true;
}

// a whole number given as a JSON number or as text ("100")
static bool whole_number(const json& v, int64_t& out) {
    if (v.is_number_integer()) {
        out = v.get<int64_t>();
        return true;
    }
    if (v.is_number_float()) {
        const double d = v.get<double>();
        if (std::floor(d) != d || std::fabs(d) > 1e9) return false;
        out = int64_t(d);
        return true;
    }
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        size_t i = 0;
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        s = s.substr(i);
        if (s.empty() || s.size() > 9) return false;
        size_t k = (s[0] == '-' || s[0] == '+') ? 1 : 0;
        if (k == s.size()) return false;
        for (size_t j = k; j < s.size(); ++j)
            if (!std::isdigit(static_cast<unsigned char>(s[j]))) return false;
        out = std::stoll(s);
        return true;
    }
    return false;
}

static std::string json_text(const json& v) { return v.is_string() ? v.get<std::string>() : v.dump(); }

bool parse_settings(const std::string& text, KitSettings& out, std::string* err) {
    out = KitSettings{};
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return zfail(err, "not a JSON object");
    const auto& colours = kit_colour_prefixes();
    const auto& ints = kit_setting_fields();
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string key = it.key();
        const json& v = it.value();
        if (std::find(colours.begin(), colours.end(), key) != colours.end()) {
            uint8_t rgb[3];
            if (!v.is_string() || !parse_hex_colour(v.get<std::string>(), rgb)) return zfail(err, key + " is " + json_text(v) + " (not a #rrggbb colour)");
            static const char ch[3] = {'r', 'g', 'b'};
            for (int c = 0; c < 3; ++c) out.fields.emplace_back(key + ch[c], rgb[c]);
            char hex[8];
            std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
            out.shown.emplace_back(key, hex);
        } else if (std::find(ints.begin(), ints.end(), key) != ints.end()) {
            int64_t n = 0;
            const bool percent = key.size() > 7 && key.compare(key.size() - 7, 7, "percent") == 0;
            const int64_t hi = percent ? 100 : 255;
            if (!whole_number(v, n) || n < 0 || n > hi)
                return zfail(err, key + " is " + json_text(v) + " (not a whole number from 0 to " + std::to_string(hi) + ")");
            out.fields.emplace_back(key, n);
            out.shown.emplace_back(key, std::to_string(n));
        } else {
            out.ignored.push_back(key);
        }
    }
    if (out.fields.empty()) return zfail(err, "none of the kit settings Turbo knows (colours, collar, name layout, shorts style)");
    return true;
}

// ================================================================ one kit zip
KitPackage load_kit_zip(const fs::path& zip, const std::vector<TexSpec>& spec) {
    KitPackage pk;
    pk.source = zip;
    pk.slot = slot_from_path(zip);
    auto finish = [&pk]() {
        pk.ok = pk.problems.empty();
        if (!pk.ok) pk.error = pk.problems[0] + (pk.problems.size() > 1 ? " (and " + std::to_string(pk.problems.size() - 1) + " more)" : "");
        return pk;
    };
    try {
        Zip z;
        std::string err;
        if (!z.load_file(zip, &err)) {
            pk.problems.push_back(zip.filename().string() + ": " + err);
            return finish();
        }
        pk.size = z.file_size();
        pk.crc = z.file_crc();
        std::vector<uint8_t> bytes;
        // settings.json
        if (const ZipEntry* e = z.find("settings.json")) {
            if (!z.read(*e, bytes, &err)) pk.problems.push_back(err);
            else if (!parse_settings(std::string(bytes.begin(), bytes.end()), pk.settings, &err)) pk.problems.push_back("settings.json: " + err);
            else if (!pk.settings.ignored.empty()) {
                std::string list;
                for (const auto& k : pk.settings.ignored) list += (list.empty() ? "" : ", ") + k;
                pk.notes.push_back("settings.json: not written (Turbo does not know these keys): " + list);
            }
        } else {
            pk.problems.push_back("settings.json is missing");
        }
        // minikit.png
        if (const ZipEntry* e = z.find("minikit.png")) {
            if (!z.read(*e, bytes, &err)) pk.problems.push_back(err);
            else if (!decode_image(bytes, pk.minikit, &err)) pk.problems.push_back("minikit.png: " + err);
            else if (pk.minikit.w < 16 || pk.minikit.h < 16) pk.problems.push_back("minikit.png is smaller than 16 x 16");
        } else {
            pk.problems.push_back("minikit.png is missing");
        }
        // textures
        std::vector<std::string> decals_missing;
        int decals_found = 0, scaled = 0;
        for (const TexSpec& t : spec) {
            const ZipEntry* e = z.find(t.role + ".png");
            if (!e) {
                if (t.decal) decals_missing.push_back(t.role + ".png");
                else pk.problems.push_back(t.role + ".png is missing");
                continue;
            }
            Rgba img;
            if (!z.read(*e, bytes, &err)) {
                pk.problems.push_back(err);
                continue;
            }
            if (!decode_image(bytes, img, &err)) {
                pk.problems.push_back(t.role + ".png: " + err);
                continue;
            }
            const int f = img.w / t.w;
            if (!((f == 1 || f == 2 || f == 4 || f == 8) && img.w == t.w * f && img.h == t.h * f)) {
                pk.problems.push_back(t.role + ".png is " + std::to_string(img.w) + " x " + std::to_string(img.h) + "; FC 27's texture is " +
                                      std::to_string(t.w) + " x " + std::to_string(t.h) + " (a picture 2, 4 or 8 times that size is scaled down)");
                continue;
            }
            if (f > 1) ++scaled;
            if (t.decal) ++decals_found;
            pk.roles.push_back(t.role);
            if (t.role == "jersey_color") {
                const int side = std::max(img.w, img.h);
                int k = 1;
                while (side / (k * 2) >= 256 && img.w % (k * 2) == 0 && img.h % (k * 2) == 0) k *= 2;
                pk.thumb = k > 1 ? downscale_box(img, img.w / k, img.h / k) : img;
            }
        }
        int decals_total = 0;
        for (const TexSpec& t : spec) decals_total += t.decal;
        if (decals_found == 0 && decals_total > 0) {
            pk.notes.push_back("no brand / crest decal textures in the zip: the game's own decals stay");
        } else if (decals_found == decals_total) {
            pk.decals = true;
        } else if (!decals_missing.empty()) {
            std::string list;
            for (const auto& m : decals_missing) list += (list.empty() ? "" : ", ") + m;
            pk.problems.push_back("only " + std::to_string(decals_found) + " of the " + std::to_string(decals_total) +
                                  " decal textures (brand_* / crest_*) are in the zip; missing: " + list);
        }
        if (scaled) pk.notes.push_back(std::to_string(scaled) + (scaled == 1 ? " picture is" : " pictures are") + " bigger than FC 27's texture: scaled down");
        // hotspots.hotspot (decal placement, staged for phase 2)
        if (const ZipEntry* e = z.find("hotspots.hotspot")) {
            if (z.read(*e, bytes, &err)) {
                json h = json::parse(bytes.begin(), bytes.end(), nullptr, false);
                pk.hotspots = !h.is_discarded() && h.is_object();
            }
            if (!pk.hotspots) pk.notes.push_back("hotspots.hotspot does not read as JSON: not staged");
        } else {
            pk.notes.push_back("no hotspots.hotspot (decal placement) in the zip");
        }
        // anything else in the zip
        std::vector<std::string> known = {"settings.json", "minikit.png", "hotspots.hotspot", "blank.png"};
        for (const TexSpec& t : spec) known.push_back(lower(t.role) + ".png");
        std::string other;
        for (const ZipEntry& e : z.entries()) {
            std::string n = lower(e.name);
            const size_t slash = n.rfind('/');
            if (slash != std::string::npos) n = n.substr(slash + 1);
            if (std::find(known.begin(), known.end(), n) == known.end()) other += (other.empty() ? "" : ", ") + e.name;
        }
        if (!other.empty()) pk.notes.push_back("not used: " + other);
    } catch (const std::exception& e) {
        pk.problems.push_back(std::string("cannot read the zip: ") + e.what());
    }
    return finish();
}

std::vector<fs::path> team_folder_zips(const fs::path& folder) {
    std::vector<fs::path> out;
    std::error_code ec;
    auto zips_in = [&](const fs::path& d) {
        std::vector<fs::path> v;
        for (fs::directory_iterator it(d, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_regular_file(e2) && lower(it->path().extension().string()) == ".zip") v.push_back(it->path());
        }
        std::sort(v.begin(), v.end());
        return v;
    };
    out = zips_in(folder);
    if (out.empty()) {
        std::vector<fs::path> dirs;
        ec.clear();
        for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_directory(e2)) dirs.push_back(it->path());
        }
        std::sort(dirs.begin(), dirs.end());
        for (const auto& d : dirs)
            for (const auto& z : zips_in(d)) out.push_back(z);
    }
    if (out.size() > 8) out.resize(8);
    return out;
}

// ================================================================ staging
fs::path kits_dir(const fs::path& le_root) { return le_root / "turbo_output" / "kits"; }

fs::path slot_dir(const fs::path& le_root, int64_t teamid, Slot s) { return kits_dir(le_root) / std::to_string(teamid) / slot_folder(s); }

std::string fnv1a64_hex(const std::vector<uint8_t>& b) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t c : b) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    char s[20];
    std::snprintf(s, sizeof(s), "%016llx", static_cast<unsigned long long>(h));
    return s;
}

static bool write_file(const fs::path& p, const std::vector<uint8_t>& b, std::string* err) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return zfail(err, "cannot write " + p.string());
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    if (!f) return zfail(err, "writing " + p.string() + " failed");
    return true;
}

StageResult stage_kit_textures(const fs::path& zip, const fs::path& dir, const StageInfo& info, const std::vector<TexSpec>& spec,
                               const std::atomic<bool>* cancel) {
    StageResult r;
    r.dir = dir;
    std::error_code ec;
    const fs::path tmp = dir / ".staging";
    auto bail = [&](const std::string& why) {
        r.ok = false;
        r.error = why;
        r.files.clear();
        std::error_code e2;
        fs::remove_all(tmp, e2);
        return r;
    };
    if (info.slot == Slot::Unknown) return bail("no kit slot");
    try {
        Zip z;
        std::string err;
        if (!z.load_file(zip, &err)) return bail(zip.filename().string() + ": " + err);
        fs::remove_all(tmp, ec);
        ec.clear();
        fs::create_directories(tmp, ec);
        if (ec) return bail("cannot create " + tmp.string() + ": " + ec.message());
        json textures = json::array();
        json kept = json::array();
        std::vector<uint8_t> bytes;
        for (const TexSpec& t : spec) {
            if (cancel && cancel->load()) return bail("cancelled");
            const ZipEntry* e = z.find(t.role + ".png");
            if (!e) {
                if (!t.decal) return bail(t.role + ".png is missing");
                kept.push_back(game_asset_path(t, info.teamid, info.slot, info.slug));
                continue;
            }
            Rgba img;
            if (!z.read(*e, bytes, &err)) return bail(err);
            if (!decode_image(bytes, img, &err)) return bail(t.role + ".png: " + err);
            const int srcw = img.w, srch = img.h;
            if (img.w != t.w || img.h != t.h) {
                Rgba s = downscale_box(img, t.w, t.h);
                if (s.empty() || img.w / t.w != img.h / t.h)
                    return bail(t.role + ".png is " + std::to_string(img.w) + " x " + std::to_string(img.h) + "; FC 27's texture is " +
                                std::to_string(t.w) + " x " + std::to_string(t.h));
                img = std::move(s);
            }
            const int mips = full_mips(t.w, t.h);
            std::vector<uint8_t> dds = encode_kit_dds(img, t.fmt, mips, &err);
            if (dds.empty()) return bail(t.role + ": " + err);
            const std::string file = t.role + ".dds";
            if (!write_file(tmp / file, dds, &err)) return bail(err);
            r.files.push_back(file);
            r.bytes += dds.size();
            textures.push_back({{"role", t.role}, {"file", file}, {"asset", game_asset_path(t, info.teamid, info.slot, info.slug)},
                                {"part", t.part}, {"format", tex_name(t.fmt)}, {"dxgi", dxgi_of(t.fmt)}, {"width", t.w}, {"height", t.h},
                                {"mips", mips}, {"bytes", dds.size()}, {"fnv1a64", fnv1a64_hex(dds)},
                                {"from", t.role + ".png " + std::to_string(srcw) + " x " + std::to_string(srch)}});
        }
        bool hotspots = false;
        if (const ZipEntry* e = z.find("hotspots.hotspot")) {
            if (z.read(*e, bytes, &err)) {
                json h = json::parse(bytes.begin(), bytes.end(), nullptr, false);
                if (!h.is_discarded() && h.is_object()) {
                    if (!write_file(tmp / "hotspots.hotspot", bytes, &err)) return bail(err);
                    r.files.push_back("hotspots.hotspot");
                    hotspots = true;
                }
            }
        }
        json m = {{"turbo_kit_stage", 1},
                  {"note", "Kit textures converted by Turbo from a FIFA Kit Creator zip, in FC 27's own size, format and mip count. The game "
                           "does not read these files: they wait for Turbo's in-match texture swap (phase 2), which checks every texture "
                           "against the game's own header before using it."},
                  {"teamid", info.teamid}, {"team", info.team}, {"slot", slot_folder(info.slot)}, {"teamkittypetechid", slot_type(info.slot)},
                  {"variation", 0}, {"slug", info.slug}, {"when", info.when}, {"source", zip.string()}, {"source_bytes", z.file_size()},
                  {"source_crc32", z.file_crc()}, {"textures", textures}, {"game_textures_kept", kept},
                  {"hotspots", hotspots ? "hotspots.hotspot" : ""}};
        const std::string mt = m.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
        if (!write_file(tmp / "manifest.json", std::vector<uint8_t>(mt.begin(), mt.end()), &err)) return bail(err);
        r.files.push_back("manifest.json");
        // in place: the previous staged files of this slot go, the new ones move in (import.json and anything else stays)
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string n = lower(it->path().filename().string());
            std::error_code e2;
            if (it->is_regular_file(e2) && (it->path().extension() == ".dds" || n == "manifest.json" || n == "hotspots.hotspot")) fs::remove(it->path(), e2);
        }
        ec.clear();
        for (const std::string& f : r.files) {
            fs::rename(tmp / f, dir / f, ec);
            if (ec) return bail("cannot move " + f + " into " + dir.string() + ": " + ec.message());
        }
        fs::remove_all(tmp, ec);
        r.ok = true;
    } catch (const std::exception& e) {
        return bail(std::string("conversion failed: ") + e.what());
    }
    return r;
}

}  // namespace kits
}  // namespace turbo
