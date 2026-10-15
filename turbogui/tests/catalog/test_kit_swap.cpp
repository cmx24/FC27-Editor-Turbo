// 3D kits on the pitch (core/kit_swap.h): resolver, plan, block table, runtime. Everything is generated here: a synthetic "game"
// (layout.toc DbObject, a toc, cas files whose blocks use a stand-in for Oodle: csize == dsize, the payload is the raw data),
// never an EA file. Optional: `test_kit_swap --game "<FC 27 folder>" --team 95 --slot 0` reads the real install (read only), runs the
// scan and the plan with random stand-in textures of the game's own sizes and prints the result (no file is written in the game).
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/kit_swap.h"
#include "nlohmann/json.hpp"

using namespace turbo::kitswap;
namespace fs = std::filesystem;
using nlohmann::json;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    if (ok) ++g_pass;
    else {
        ++g_fail;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

// ---------------------------------------------------------------- byte helpers
static void be32(std::vector<uint8_t>& d, uint32_t v) {
    for (int i = 3; i >= 0; --i) d.push_back(uint8_t(v >> (8 * i)));
}
static void le32(std::vector<uint8_t>& d, uint32_t v) {
    for (int i = 0; i < 4; ++i) d.push_back(uint8_t(v >> (8 * i)));
}
static void put_le32(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) d[at + size_t(i)] = uint8_t(v >> (8 * i));
}
static void put_le16(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    d[at] = uint8_t(v);
    d[at + 1] = uint8_t(v >> 8);
}

// DbObject writer (just what layout.toc needs)
static void varint(std::vector<uint8_t>& d, uint64_t v) {
    while (v >= 0x80) {
        d.push_back(uint8_t(v | 0x80));
        v >>= 7;
    }
    d.push_back(uint8_t(v));
}
static void db_named(std::vector<uint8_t>& d, uint8_t type, const std::string& name) {
    d.push_back(type);
    d.insert(d.end(), name.begin(), name.end());
    d.push_back(0);
}
static std::vector<uint8_t> db_obj(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> v;
    varint(v, body.size() + 1);
    v.insert(v.end(), body.begin(), body.end());
    v.push_back(0);
    return v;
}
static std::vector<uint8_t> make_layout(uint32_t persistent, const std::string& pkg) {
    std::vector<uint8_t> chunk_body;
    db_named(chunk_body, 8, "persistentIndex");
    le32(chunk_body, persistent);
    db_named(chunk_body, 7, "name");
    varint(chunk_body, pkg.size() + 1);
    chunk_body.insert(chunk_body.end(), pkg.begin(), pkg.end());
    chunk_body.push_back(0);
    std::vector<uint8_t> chunk;  // unnamed object (type 0x82)
    chunk.push_back(0x82);
    auto cb = db_obj(chunk_body);
    chunk.insert(chunk.end(), cb.begin(), cb.end());
    std::vector<uint8_t> list;
    db_named(list, 1, "installChunks");
    auto lb = chunk;
    lb.push_back(0);
    varint(list, lb.size());
    list.insert(list.end(), lb.begin(), lb.end());
    std::vector<uint8_t> im;
    db_named(im, 2, "installManifest");
    auto ib = db_obj(list);
    im.insert(im.end(), ib.begin(), ib.end());
    std::vector<uint8_t> root;
    root.push_back(0x02);
    root.push_back(0);  // unnamed? the root is read with a name; use an empty name
    auto rb = db_obj(im);
    root.insert(root.end(), rb.begin(), rb.end());
    return root;
}

// ---------------------------------------------------------------- the synthetic game
static const char* kPkg = "superbundlelayout/installpackage_00";
static const uint32_t kPersistent = 0xAABBCCDDu;
static const char* kCasId = "0000aabbccdd0001";

struct GameBuilder {
    fs::path root;
    std::vector<uint8_t> cas;  // cas_01.cas content
    struct B {
        std::vector<std::string> ebx;
        struct R {
            std::string name;
            std::vector<uint8_t> hdr;  // decompressed texture header
            std::string guid;
        };
        std::vector<R> res;
        struct C {
            std::string guid;
            uint32_t logical;
            uint64_t off;
            uint32_t size;
        };
        std::vector<C> chunks;
    };
    std::vector<B> bundles;
    std::map<std::string, std::vector<uint8_t>> ebx_data;  // raw bytes of named EBX assets (others get 20 filler bytes)
   
    std::mt19937 rng{7};

    uint64_t append(const std::vector<uint8_t>& b) {
        const uint64_t at = cas.size();
        cas.insert(cas.end(), b.begin(), b.end());
        return at;
    }
    // one Oodle-style block stream; stand-in codec: csize == dsize (type 0x19)
    std::vector<uint8_t> blocks_of(const std::vector<uint8_t>& raw, size_t block) {
        std::vector<uint8_t> out;
        for (size_t at = 0; at < raw.size(); at += block) {
            const size_t n = std::min(block, raw.size() - at);
            be32(out, uint32_t(n));
            out.push_back(0x19);
            out.push_back(0x70);
            out.push_back(uint8_t(n >> 8));
            out.push_back(uint8_t(n));
            out.insert(out.end(), raw.begin() + std::ptrdiff_t(at), raw.begin() + std::ptrdiff_t(at + n));
        }
        return out;
    }
    std::vector<uint8_t> random_bytes(size_t n) {
        std::vector<uint8_t> v(n);
        for (auto& x : v) x = uint8_t(rng());
        return v;
    }
    static std::vector<uint8_t> tex_header(uint32_t fmt, int w, int h, int mips, const std::string& guid_hex, const std::vector<uint32_t>& sizes) {
        std::vector<uint8_t> hd(184, 0);
        put_le32(hd, 0x0C, fmt);
        put_le16(hd, 0x16, uint32_t(w));
        put_le16(hd, 0x18, uint32_t(h));
        hd[0x1E] = uint8_t(mips);
        for (int i = 0; i < 16; ++i) hd[0x28 + size_t(i)] = uint8_t(std::stoi(guid_hex.substr(size_t(i) * 2, 2), nullptr, 16));
        for (size_t i = 0; i < sizes.size(); ++i) put_le32(hd, 0x38 + i * 4, sizes[i]);
        return hd;
    }
    static std::string guid_of(int n) {
        char b[40];
        std::snprintf(b, sizeof(b), "%032x", n * 7919 + 13);
        return b;
    }
    // Adds a bundle with the given textures (name, format id, w, h, mips, raw data, chunk location override: when has_loc the
    // chunk is NOT written again (it points at an earlier chunk's location: physical sharing)).
    struct TexSpec {
        std::string name;
        uint32_t fmt;
        int w, h, mips;
        std::vector<uint8_t> raw;  // empty = random
        int share_with = -1;       // another TexSpec's chunk location (by registration order across the game)
        size_t block = 16384;
    };
    std::vector<std::array<uint64_t, 2>> chunk_locs;  // registration order
    int add_bundle(const std::vector<TexSpec>& texs, const std::string& extra_ebx = "") {
        B b;
        std::vector<std::array<uint64_t, 2>> lc;
        for (size_t i = 0; i < texs.size(); ++i) {
            const TexSpec& t = texs[i];
            const uint32_t dxgi = dxgi_of_format(t.fmt);
            const auto sizes = bc_mip_sizes(dxgi, t.w, t.h, t.mips);
            uint32_t total = 0;
            for (uint32_t s : sizes) total += s;
            std::vector<uint8_t> raw = t.raw.empty() ? random_bytes(total) : t.raw;
            const std::string g = guid_of(int(chunk_locs.size() + 1));
            uint64_t off, size;
            if (t.share_with >= 0) {
                off = chunk_locs[size_t(t.share_with)][0];
                size = chunk_locs[size_t(t.share_with)][1];
            } else {
                auto bl = blocks_of(raw, t.block);
                off = append(bl);
                size = bl.size();
            }
            chunk_locs.push_back({off, size});
            b.chunks.push_back({g, total, off, uint32_t(size)});
            b.res.push_back({t.name, tex_header(t.fmt, t.w, t.h, t.mips, g, sizes), g});
            b.ebx.push_back(t.name);
        }
        if (!extra_ebx.empty()) b.ebx.push_back(extra_ebx);
        bundles.push_back(std::move(b));
        return int(bundles.size() - 1);
    }
    // Writes layout.toc, the toc and the cas file
    void write() {
        fs::create_directories(root / "Data" / "Win32" / "superbundlelayout" / "installpackage_00");
        const auto lay = make_layout(kPersistent, kPkg);
        std::ofstream(root / "Data" / "layout.toc", std::ios::binary).write(reinterpret_cast<const char*>(lay.data()), std::streamsize(lay.size()));
        // bundle manifests and RES / chunk locations
        std::vector<std::vector<std::array<uint64_t, 2>>> L;
        for (const B& b : bundles) {
            std::vector<std::array<uint64_t, 2>> l;
            // manifest
            std::vector<uint8_t> strings;
            std::vector<uint32_t> ebx_off, res_off;
            for (const auto& n : b.ebx) {
                ebx_off.push_back(uint32_t(strings.size()));
                strings.insert(strings.end(), n.begin(), n.end());
                strings.push_back(0);
            }
            for (const auto& r : b.res) {
                res_off.push_back(uint32_t(strings.size()));
                strings.insert(strings.end(), r.name.begin(), r.name.end());
                strings.push_back(0);
            }
            const uint32_t ne = uint32_t(b.ebx.size()), nr = uint32_t(b.res.size()), nc = uint32_t(b.chunks.size()), total = ne + nr + nc;
            std::vector<uint8_t> m(4, 0);  // size prefix (unused)
            le32(m, kManifestMagicForTest());
            le32(m, total);
            le32(m, ne);
            le32(m, nr);
            le32(m, nc);
            const size_t so_at = m.size();
            le32(m, 0);  // strings offset, patched
            le32(m, 0);
            le32(m, 0);
            m.insert(m.end(), 20 * size_t(total), 0);  // sha1s
            for (uint32_t i = 0; i < ne; ++i) {
                le32(m, ebx_off[i]);
                le32(m, 300);
            }
            for (uint32_t i = 0; i < nr; ++i) {
                le32(m, res_off[i]);
                le32(m, 184);
            }
            for (uint32_t i = 0; i < nr; ++i) le32(m, 0x6BDE20BA);
            m.insert(m.end(), 24 * size_t(nr), 0);
            for (const auto& c : b.chunks) {
                for (int i = 0; i < 16; ++i) m.push_back(uint8_t(std::stoi(c.guid.substr(size_t(i) * 2, 2), nullptr, 16)));
                le32(m, 0);
                le32(m, c.logical);
            }
            put_le32(m, so_at, uint32_t(m.size() - 4));
            m.insert(m.end(), strings.begin(), strings.end());
            l.push_back({append(m), m.size()});
            for (uint32_t i = 0; i < ne; ++i) {  // EBX: 20 filler bytes, or the registered asset as one block
                const auto ed = ebx_data.find(b.ebx[i]);
                auto e = ed == ebx_data.end() ? random_bytes(20) : blocks_of(ed->second, 1 << 20);
                l.push_back({append(e), e.size()});
            }
            for (const auto& r : b.res) {  // RES: one block stream with the header
                auto bl = blocks_of(r.hdr, 1 << 20);
                l.push_back({append(bl), bl.size()});
            }
            for (const auto& c : b.chunks) l.push_back({c.off, c.size});
            L.push_back(std::move(l));
        }
        std::ofstream(root / "Data" / "Win32" / "superbundlelayout" / "installpackage_00" / "cas_01.cas", std::ios::binary)
            .write(reinterpret_cast<const char*>(cas.data()), std::streamsize(cas.size()));
        // toc
        std::vector<uint8_t> toc(0x22C, 0);
        const size_t base = 0x22C;
        std::vector<uint8_t> payload;  // after the header (60) and the bundle table
        const size_t table_off = 60;
        const size_t table_size = bundles.size() * 16;
        std::vector<uint8_t> bdata;
        std::vector<std::pair<uint32_t, uint64_t>> entries;  // (size, off relative to TOC_DATA)
        for (const auto& l : L) {
            std::vector<uint8_t> bd;
            const uint32_t cnt = uint32_t(l.size());
            for (int i = 0; i < 9; ++i) be32(bd, 0);
            const uint32_t flags_off = 36, p0 = 36 + cnt;
            bd.resize(36);
            std::vector<uint8_t> flags(cnt, 0), loc;
            flags[0] = 0x80;
            for (uint32_t i = 0; i < cnt; ++i) {
                if (i == 0)
                    for (int k = 0; k < 8; ++k) loc.push_back(uint8_t(std::stoi(std::string(kCasId).substr(size_t(k) * 2, 2), nullptr, 16)));
                be32(loc, uint32_t(l[i][0]));
                be32(loc, uint32_t(l[i][1]));
            }
            bd.insert(bd.end(), flags.begin(), flags.end());
            bd.insert(bd.end(), loc.begin(), loc.end());
            auto set = [&](size_t at, uint32_t v) {
                for (int i = 0; i < 4; ++i) bd[at + size_t(i)] = uint8_t(v >> (8 * (3 - i)));
            };
            set(8, flags_off);
            set(12, cnt);
            set(16, p0);
            entries.push_back({0x40000000u | uint32_t(bd.size()), table_off + table_size + bdata.size()});
            bdata.insert(bdata.end(), bd.begin(), bd.end());
        }
        std::vector<uint8_t> body;
        std::vector<uint32_t> h(15, 0);
        h[1] = uint32_t(table_off);
        h[2] = uint32_t(bundles.size());
        for (uint32_t v : h) be32(body, v);
        for (const auto& e : entries) {
            be32(body, 1);
            be32(body, e.first);
            be32(body, uint32_t(e.second >> 32));
            be32(body, uint32_t(e.second));
        }
        body.insert(body.end(), bdata.begin(), bdata.end());
        toc.insert(toc.end(), body.begin(), body.end());
        (void)base;
        std::ofstream(root / "Data" / "Win32" / "contentsb.toc", std::ios::binary).write(reinterpret_cast<const char*>(toc.data()), std::streamsize(toc.size()));
    }
    static uint32_t kManifestMagicForTest() { return 0x9D798ED6u; }
};

static OodleFn stand_in_oodle() {
    return [](const uint8_t* src, size_t n, uint8_t* dst, size_t dn) {
        if (n != dn) return false;
        std::memcpy(dst, src, n);
        return true;
    };
}

// staged DDS: header + `data` (random) for a texture of the given format
static void stage_texture(const fs::path& dir, const std::string& role, uint32_t fmt, int w, int h, int mips, std::vector<json>& textures, std::mt19937& rng,
                          std::vector<uint8_t>* keep = nullptr) {
    const uint32_t dxgi = dxgi_of_format(fmt);
    const auto sizes = bc_mip_sizes(dxgi, w, h, mips);
    uint64_t total = 0;
    for (auto s : sizes) total += s;
    std::vector<uint8_t> f(kDdsHeader, 0);
    std::memcpy(f.data(), "DDS ", 4);
    put_le32(f, 4, 124);
    put_le32(f, 12, uint32_t(h));
    put_le32(f, 16, uint32_t(w));
    put_le32(f, 28, uint32_t(mips));
    std::memcpy(&f[84], "DX10", 4);
    put_le32(f, 128, dxgi);
    std::vector<uint8_t> data;
    data.resize(size_t(total));
    for (auto& x : data) x = uint8_t(rng());
    f.insert(f.end(), data.begin(), data.end());
    if (keep) *keep = data;
    fs::create_directories(dir);
    std::ofstream(dir / (role + ".dds"), std::ios::binary).write(reinterpret_cast<const char*>(f.data()), std::streamsize(f.size()));
    textures.push_back({{"role", role}, {"file", role + ".dds"}, {"dxgi", dxgi}, {"width", w}, {"height", h}, {"mips", mips}, {"bytes", f.size()}});
}

static void write_manifest(const fs::path& dir, int team, int slot, const std::vector<json>& textures, const std::string& hotspots = "") {
    json j = {{"turbo_kit_stage", 1}, {"teamid", team}, {"teamkittypetechid", slot}, {"textures", textures}, {"hotspots", hotspots}};
    std::ofstream(dir / "manifest.json") << j.dump(2);
}

static std::string P(const char* kind, const char* part, const char* leaf, int team = 95) {
    return std::string("content/character/kit/kit_0/testfc_") + std::to_string(team) + "/" + kind + "/" + part + "/" + leaf;
}

// ---------------------------------------------------------------- tests
static void test_names() {
    check(kind_slot("home_0_0") == 0 && kind_slot("away_1_0") == 1 && kind_slot("goalie_home_2_0") == 2 && kind_slot("third_3_0") == 3 && kind_slot("fourth_4_0") == 4, "kind_slot base kinds");
    check(kind_slot("loc2_home_40_0") == 0 && kind_slot("loc2_goalie_home_42_0") == 2 && kind_slot("loc2_fourth_44_0") == 4, "kind_slot loc2 kinds");
    check(kind_slot("referee_5_0") == -1 && kind_slot("home") == -1 && kind_slot("x_45_0") == -1, "kind_slot others");
    check(role_of("jersey_95_0_0_color", 95) == "jersey_color", "role jersey_color");
    check(role_of("brand_shorts_95_40_0_normal", 95) == "brand_shorts_normal", "role brand_shorts_normal");
    check(role_of("crest_shorts_95_0_0_coeff", 95) == "crest_shorts_coeff", "role crest_shorts_coeff");
    check(role_of("weave_jersey_95_0_0_coeff_array", 95) == "weave_jersey_coeff_array", "role weave array");
    check(role_of("jersey_96_0_0_color", 95).empty() && role_of("hotspots_95_0_0", 95).empty(), "role mismatches");
    const KitName k = parse_kit_name("content/character/kit/kit_0/leicester_city_95/home_0_0/jersey/jersey_95_0_0_color");
    check(k.ok && k.team == 95 && k.slug == "leicester_city_95" && k.kind == "home_0_0" && k.part == "jersey" && k.leaf == "jersey_95_0_0_color", "parse_kit_name");
    const KitName k2 = parse_kit_name("content/character/kit/kit_0/leicester_city_95/home_0_0/hotspots_95_0_0");
    check(k2.ok && k2.part.empty() && k2.leaf == "hotspots_95_0_0", "parse_kit_name without part");
    check(!parse_kit_name("content/character/head/x").ok, "parse_kit_name refuses non-kit names");
    const auto sz = bc_mip_sizes(72, 2048, 2048, 12);
    uint64_t s = 0;
    for (auto x : sz) s += x;
    check(s == 2796216 && sz.back() == 8, "BC1 2048 mip chain = the game's 2,796,216 bytes");
    uint64_t s2 = 0;
    for (auto x : bc_mip_sizes(98, 1024, 1024, 11)) s2 += x;
    check(s2 == 1398128, "BC7 1024 chain = 1,398,128");
    check(dxgi_of_format(55) == 72 && dxgi_of_format(63) == 83 && dxgi_of_format(66) == 98 && dxgi_of_format(1) == 0, "format ids");
}

static void test_blocks() {
    std::vector<uint8_t> raw(40000);
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = uint8_t(i * 7);
    GameBuilder g;
    auto bl = g.blocks_of(raw, 16384);
    std::vector<Block> bs;
    check(block_headers(bl, bs) && bs.size() == 3 && bs[0].dsize == 16384 && bs[2].dsize == 40000 - 32768 && bs[0].ctype == 0x19 && bs[1].payload == 8 + 16384 + 8, "block_headers");
    std::vector<uint8_t> out;
    check(read_blocks(bl, stand_in_oodle(), out, nullptr) && out == raw, "read_blocks with the stand-in codec");
    auto cut = bl;
    cut.pop_back();
    check(!block_headers(cut, bs), "a cut block stream is refused");
    check(!read_blocks(bl, nullptr, out, nullptr), "Oodle block without a decompressor is refused");
}

struct World {
    fs::path root, le;
    GameBuilder g;
    std::mt19937 rng{11};
    std::vector<uint8_t> jersey_color_data;
};

static World make_world(const fs::path& base) {
    World w;
    w.root = base / "game";
    w.le = base / "le";
    w.g.root = w.root;
    // chunk registration order: 0 jersey_color, 1 jersey_normal, 2 brand_normal(tiny csize), 3 jersey_coeff (shared phys with team 96)...
    using T = GameBuilder::TexSpec;
    // team 95 home jersey bundle: BC1 sRGB color 256x256 (8 mips), normal BC5 128x128, brand decal BC5 64x64 (tiny), coeff BC1 128x128 shared, taa BC7 128x128
    w.g.add_bundle({T{P("home_0_0", "jersey", "jersey_95_0_0_color"), 55, 256, 256, 9, {}, -1, 16384},  // 0
                    T{P("home_0_0", "jersey", "jersey_95_0_0_normal"), 63, 128, 128, 8, {}, -1, 8192},  // 1
                    T{P("home_0_0", "jersey", "brand_jersey_95_0_0_normal"), 63, 16, 16, 5, {}, -1, 8192},  // 2 (tiny: csize < 1024)
                    T{P("home_0_0", "jersey", "jersey_95_0_0_coeff"), 54, 128, 128, 8, {}, -1, 8192},  // 3 (shared with team 96)
                    T{P("home_0_0", "jersey", "jersey_95_0_0_taa"), 66, 128, 128, 8, {}, -1, 8192},  // 4 (census: same bytes in another chunk)
                    T{P("home_0_0", "jersey", "weave_jersey_95_0_0_coeff_array"), 63, 64, 64, 7, {}, -1, 8192}});  // 5 (no role)
    // the same club's loc2 variant: same chunks for jersey_color? no: its own jersey_color, but SHARED normal (same slot: allowed)
    w.g.add_bundle({T{P("loc2_home_40_0", "jersey", "jersey_95_40_0_color"), 55, 256, 256, 9, {}, -1, 16384},  // 6 own
                    T{P("loc2_home_40_0", "jersey", "jersey_95_40_0_normal"), 63, 128, 128, 8, {}, 1, 8192}});  // 7 shares chunk 1
    // team 96: shares team 95's jersey_coeff chunk physically, and holds a byte-identical copy of team 95's taa at another place
    // (the census case: needs the raw bytes of chunk 4: read back from the cas below)
    w.g.add_bundle({T{P("home_0_0", "jersey", "jersey_96_0_0_coeff", 96), 54, 128, 128, 8, {}, 3, 8192}});  // 8 shares chunk 3
    // team 95 away (another slot): own textures, must not be touched by a home import
    w.g.add_bundle({T{P("away_1_0", "jersey", "jersey_95_1_0_color"), 55, 256, 256, 9, {}, -1, 16384}});  // 9
    // team 97: a byte-identical copy of team 95's taa chunk bytes at a different physical place: add after capturing the raw bytes
    {
        // chunk 4's raw bytes are the cas bytes of its blocks: rebuild them from the builder's storage by reading the cas
        const auto& c = w.g.chunk_locs[4];
        std::vector<uint8_t> raw;
        std::vector<uint8_t> stream(w.g.cas.begin() + std::ptrdiff_t(c[0]), w.g.cas.begin() + std::ptrdiff_t(c[0] + c[1]));
        read_blocks(stream, stand_in_oodle(), raw, nullptr);
        T t{P("home_0_0", "jersey", "jersey_97_0_0_taa", 97), 66, 128, 128, 8, raw, -1, 8192};
        w.g.add_bundle({t});  // 10
    }
    // team 95 home: crest and crest_shorts normal share ONE chunk (the game stores them once); a zip with different pictures for the two
    // cannot be served by one block: both stay the game's
    w.g.add_bundle({T{P("home_0_0", "jersey", "crest_95_0_0_normal"), 63, 64, 64, 7, {}, -1, 8192},  // 11
                    T{P("home_0_0", "shorts", "crest_shorts_95_0_0_normal"), 63, 64, 64, 7, {}, 11, 8192}});  // 12 shares chunk 11
    w.g.write();
    return w;
}

static StagedKit stage_home(World& w, bool with_shorts_color = false) {
    const fs::path dir = w.le / "turbo_output" / "kits" / "95" / "home";
    std::vector<json> tex;
    stage_texture(dir, "jersey_color", 55, 256, 256, 9, tex, w.rng, &w.jersey_color_data);
    stage_texture(dir, "jersey_normal", 63, 128, 128, 8, tex, w.rng);
    stage_texture(dir, "brand_jersey_normal", 63, 16, 16, 5, tex, w.rng);
    stage_texture(dir, "jersey_coeff", 54, 128, 128, 8, tex, w.rng);
    stage_texture(dir, "jersey_taa", 66, 128, 128, 8, tex, w.rng);
    if (with_shorts_color) stage_texture(dir, "shorts_color", 55, 64, 64, 7, tex, w.rng);
    stage_texture(dir, "crest_normal", 63, 64, 64, 7, tex, w.rng);
    stage_texture(dir, "crest_shorts_normal", 63, 64, 64, 7, tex, w.rng);
    write_manifest(dir, 95, 0, tex);
    StagedKit sk;
    std::string err;
    check(load_staged(dir, sk, &err), "load_staged: " + err);
    return sk;
}

static const PlanItem* item_named(const Plan& p, const std::string& leaf, const std::string& kind) {
    for (const auto& i : p.items)
        if (i.res.size() > leaf.size() && i.res.compare(i.res.size() - leaf.size() - 1, leaf.size() + 1, "/" + leaf) == 0 && i.kind == kind) return &i;
    return nullptr;
}

static void test_scan_and_plan(const fs::path& base) {
    World w = make_world(base / "scan");
    GameFiles files(w.root);
    std::string err;
    check(files.init(&err), "GameFiles::init (layout.toc): " + err);
    check(files.cas_path(kCasId).filename() == "cas_01.cas", "cas path from the layout package");
    KitIndex idx;
    check(scan_kits(files, stand_in_oodle(), idx, nullptr, &err), "scan_kits: " + err);
    check(idx.bundles.size() == 6 && idx.tocs.size() == 1, "6 kit bundles found, got " + std::to_string(idx.bundles.size()));
    int t95 = 0;
    for (const auto& b : idx.bundles) t95 += b.team == 95;
    check(t95 == 4, "team 95 has 4 bundles");
    // physical sharing: chunk 3 is used by team 95 and team 96
    Loc l3;
    l3.cas = kCasId;
    l3.off = w.g.chunk_locs[3][0];
    l3.size = uint32_t(w.g.chunk_locs[3][1]);
    const auto it = idx.users.find(phys_key(l3));
    check(it != idx.users.end() && it->second.size() == 2, "chunk 3 has two users");

    StagedKit sk = stage_home(w);
    Options opt;
    opt.min_csize = 1024;
    opt.keep_game_crest = false;  // the 2.6 behaviour: the zip's crest pictures are served
    opt.watch_club = false;
    TableBuilder tb;
    Plan plan = build_plan(files, stand_in_oodle(), idx, sk, opt, true, tb, nullptr);
    check(plan.ok, "plan ok: " + plan.error);
    for (const auto& i : plan.items) std::printf("    %-12s %-34s %s %s\n", i.kind.c_str(), i.res.substr(i.res.rfind('/') + 1).c_str(), i.swap ? "SWAP" : "left", i.reason.c_str());
    const PlanItem* jc = item_named(plan, "jersey_95_0_0_color", "home_0_0");
    check(jc && jc->swap && jc->blocks == 3, "jersey_color: swapped, 3 blocks (256x256 BC1 chain in 16 KB blocks)");
    const PlanItem* jn = item_named(plan, "jersey_95_0_0_normal", "home_0_0");
    check(jn && jn->swap, "jersey_normal swapped (shared only within the same club and slot)");
    const PlanItem* jn2 = item_named(plan, "jersey_95_40_0_normal", "loc2_home_40_0");
    check(jn2 && jn2->swap, "loc2 normal swapped too (same bytes, one entry)");
    const PlanItem* jc2 = item_named(plan, "jersey_95_40_0_color", "loc2_home_40_0");
    check(jc2 && jc2->swap, "loc2 jersey_color swapped too (same role, its own chunk)");
    const PlanItem* bn = item_named(plan, "brand_jersey_95_0_0_normal", "home_0_0");
    check(bn && !bn->swap && bn->reason.find("uniform fill") != std::string::npos, "brand normal: left (tiny compressed block)");
    const PlanItem* co = item_named(plan, "jersey_95_0_0_coeff", "home_0_0");
    check(co && !co->swap && co->reason.find("also used by team 96") != std::string::npos, "jersey_coeff: left (chunk shared with team 96)");
    const PlanItem* ta = item_named(plan, "jersey_95_0_0_taa", "home_0_0");
    check(ta && !ta->swap && ta->reason.find("byte-identical") != std::string::npos, "taa: left (identical compressed block in team 97's chunk)");
    const PlanItem* wv = item_named(plan, "weave_jersey_95_0_0_coeff_array", "home_0_0");
    check(wv && !wv->swap, "weave array: left");
    check(!item_named(plan, "jersey_95_1_0_color", "away_1_0"), "the away kit is not part of a home plan");
    check(!item_named(plan, "jersey_96_0_0_coeff", "home_0_0"), "team 96 is not part of team 95's plan");
}

static void test_table(const fs::path& base) {
    World w = make_world(base / "table");
    GameFiles files(w.root);
    std::string err;
    files.init(&err);
    KitIndex idx;
    scan_kits(files, stand_in_oodle(), idx, nullptr, &err);
    StagedKit sk = stage_home(w);
    Options opt;
    opt.keep_game_crest = false;
    opt.watch_club = false;
    TableBuilder tb;
    Plan plan = build_plan(files, stand_in_oodle(), idx, sk, opt, true, tb, nullptr);
    std::vector<std::string> dropped;
    auto table = tb.finish(&dropped);
    check(dropped.size() == 2, "the crest / crest_shorts conflict drops exactly those two items");
    // jersey_color (5 blocks), jersey_normal (home + loc2: one entry set), loc2 jersey_color (5 blocks, other bytes), normal 128x128 BC5 chain = 21856 B -> 2 blocks of 8192+..
    check(table->size() > 0, "table has entries: " + std::to_string(table->size()));
    // read the real compressed blocks of the home jersey_color chunk and look them up
    const auto& c = w.g.chunk_locs[0];
    std::vector<uint8_t> chunk(w.g.cas.begin() + std::ptrdiff_t(c[0]), w.g.cas.begin() + std::ptrdiff_t(c[0] + c[1]));
    std::vector<Block> bl;
    block_headers(chunk, bl);
    uint64_t at = 0;
    int found = 0;
    for (const Block& b : bl) {
        const Entry* e = table->match(&chunk[size_t(b.payload)], b.csize, b.dsize);
        if (e && std::memcmp(e->data, &w.jersey_color_data[size_t(at)], b.dsize) == 0) ++found;
        at += b.dsize;
    }
    check(found == int(bl.size()), "every block of the jersey_color chunk matches and its replacement is the staged bytes at the right offset");
    // one flipped byte, or other sizes: no match
    std::vector<uint8_t> flipped(chunk.begin() + std::ptrdiff_t(bl[0].payload), chunk.begin() + std::ptrdiff_t(bl[0].payload + bl[0].csize));
    flipped[flipped.size() / 2] ^= 1;
    check(!table->match(flipped.data(), flipped.size(), bl[0].dsize), "a flipped byte: no match");
    check(!table->match(&chunk[size_t(bl[0].payload)], bl[0].csize, bl[0].dsize + 1), "other raw size: no match");
    check(!table->match(&chunk[size_t(bl[0].payload)], bl[0].csize - 1, bl[0].dsize), "other compressed size: no match");
    // the away kit's color chunk (same sizes, other bytes) is not in the table
    const auto& ca = w.g.chunk_locs[9];
    std::vector<uint8_t> ach(w.g.cas.begin() + std::ptrdiff_t(ca[0]), w.g.cas.begin() + std::ptrdiff_t(ca[0] + ca[1]));
    std::vector<Block> ab;
    block_headers(ach, ab);
    check(!table->match(&ach[size_t(ab[0].payload)], ab[0].csize, ab[0].dsize), "the away kit's blocks are not matched");

    // builder: conflict and alias
    TableBuilder b2;
    std::vector<uint8_t> d1(64, 1), d2(64, 2), key(64, 9);
    auto s1 = std::make_shared<std::vector<uint8_t>>(d1), s2 = std::make_shared<std::vector<uint8_t>>(d1), s3 = std::make_shared<std::vector<uint8_t>>(d2), s4 = std::make_shared<std::vector<uint8_t>>(d1);
    const uint32_t i1 = b2.add_item(s1, true), i2 = b2.add_item(s2, true), i3 = b2.add_item(s3, true), i4 = b2.add_item(s4, true);
    uint64_t head;
    std::memcpy(&head, key.data(), 8);
    const uint64_t f = fnv1a64(key.data(), key.size());
    b2.add_block(i1, 64, 64, head, f, s1->data(), nullptr);
    b2.add_block(i2, 64, 64, head, f, s2->data(), nullptr);  // same key, same bytes: alias
    std::vector<uint8_t> key2(64, 7);
    std::memcpy(&head, key2.data(), 8);
    const uint64_t f2 = fnv1a64(key2.data(), key2.size());
    b2.add_block(i3, 64, 64, head, f2, s3->data(), nullptr);
    b2.add_block(i4, 64, 64, head, f2, s4->data(), nullptr);  // same key, other bytes: conflict, both items dropped
    std::vector<std::string> dr;
    auto t2 = b2.finish(&dr);
    check(t2->size() == 1 && dr.size() == 2, "conflicting items dropped, identical ones merged (size " + std::to_string(t2->size()) + ", dropped " + std::to_string(dr.size()) + ")");
    uint64_t kh;
    std::memcpy(&kh, key.data(), 8);
    const Entry* e = t2->match(key.data(), 64, 64);
    check(e != nullptr, "merged entry matches");
    if (e) {
        t2->count_hit(*e);
        check(t2->hits(i1) == 1 && t2->hits(i2) == 1, "the alias item counts the hit too");
    }
    (void)kh;
}

// ---------------------------------------------------------------- hotspots
// A synthetic hotspot asset in the layout read from the real install: RIFF EBX container, 25 at 0x8C, 25 entries of 0x30 bytes from 0x90,
// the asset's own path right behind them.
static std::vector<uint8_t> make_hotspot_ebx(const std::string& path, std::mt19937& rng) {
    std::vector<uint8_t> d(0x540 + path.size() + 1 + 0x40, 0);
    std::memcpy(d.data(), "RIFF", 4);
    put_le32(d, 4, uint32_t(d.size() - 8));
    std::memcpy(&d[8], "EBX\0EBXD", 8);
    put_le32(d, 0x8C, 25);
    auto setf = [&](int i, float x, float y, float z, float w, float rot = 0.f) {
        float f[4] = {x, y, z, w};
        std::memcpy(&d[0x90 + size_t(i) * 0x30], f, 16);
        std::memcpy(&d[0x90 + size_t(i) * 0x30 + 0x20], &rot, 4);
        for (int k = 0; k < 8; ++k) d[0x90 + size_t(i) * 0x30 + 0x10 + size_t(k)] = uint8_t(rng());  // pointer-like bytes the patch must not touch
    };
    for (int i = 0; i < 25; ++i) setf(i, 0.1f, 0.1f, 0.2f, 0.2f);
    setf(0, 0.5437f, 0.575f, 0.6008f, 0.6262f);   // jersey team
    setf(1, 0.589f, 0.3959f, 0.4108f, 0.2317f);   // numberback
    setf(2, 0.4601f, 0.6943f, 0.54f, 0.7603f);    // numberfront (a rectangle in the game)
    setf(3, 0.582f, 0.4428f, 0.418f, 0.4143f);    // name
    setf(4, 0.5f, 0.2542f, 0.5f, 0.2542f);        // namearccenter (a point)
    setf(5, 0.5f, 0.4321f, 0.5f, 0.4321f);
    setf(7, -0.5f, -0.5f, -0.5f, -0.5f);          // jersey brand (not used)
    setf(11, 0.3379f, 0.8298f, 0.3926f, 0.9198f);
    setf(12, 0.5799f, 0.8016f, 0.6647f, 0.9144f);
    setf(20, 0.6808f, 0.4434f, 0.7512f, 0.5566f, 90.f);
    setf(24, -1.f, -1.f, -1.f, -1.f);
    std::memcpy(&d[0x540], path.c_str(), path.size() + 1);
    return d;
}
static float getf(const std::vector<uint8_t>& d, int entry, int k) {
    float f;
    std::memcpy(&f, &d[0x90 + size_t(entry) * 0x30 + size_t(k) * 4], 4);
    return f;
}
// the Leicester h.zip's hotspots (values as found in the zip), trimmed to what matters here
static const char* kZipHotspots = R"({"HotspotFormatVersion":1,"Hotspots":[
 {"Group":"jersey","Name":"numberback","Bounds":{"X":0.5803,"Y":0.3831,"Z":0.4197,"W":0.2224},"Rotation":0},
 {"Group":"jersey","Name":"numberfront","Bounds":{"X":0.5,"Y":0.6689,"Z":0.5,"W":0.6689},"Rotation":0},
 {"Group":"jersey","Name":"name","Bounds":{"X":0.5806,"Y":0.436,"Z":0.4194,"W":0.3921},"Rotation":0},
 {"Group":"jersey","Name":"namearccenter","Bounds":{"X":0.5,"Y":0.2509,"Z":0.5,"W":0.2509},"Rotation":0},
 {"Group":"shorts","Name":"number","Bounds":{"X":0.5859,"Y":0.708,"Z":0.6543,"W":0.8389},"Rotation":0},
 {"Group":"competition","Name":"LeftSleeveLong","Bounds":{"X":0.67431640625,"Y":0.4287109375,"Z":0.74267578125,"W":0.5498046875},"Rotation":90},
 {"Group":"competition","Name":"Center","Bounds":{"X":0.4915771484375,"Y":0.5814208984375,"Z":0.5084228515625,"W":0.5982666015625},"Rotation":0},
 {"Group":"jersey","Name":"team","Bounds":{"X":-0.5,"Y":-0.5,"Z":-0.5,"W":-0.5},"Rotation":0},
 {"Group":"jersey","Name":"brand","Bounds":{"X":-0.5,"Y":-0.5,"Z":-0.5,"W":-0.5},"Rotation":0}]})";

static int count_patched(const std::vector<std::string>& ch) {
    int n = 0;
    for (const auto& c : ch) n += c.find(" -> ") != std::string::npos;
    return n;
}

static void test_hotspots(const fs::path& base) {
    std::mt19937 rng(5);
    const std::string path = "content/character/kit/kit_0/testfc_95/home_0_0/hotspots_95_0_0";
    const std::vector<uint8_t> orig = make_hotspot_ebx(path, rng);
    HotspotMap zip;
    std::string err;
    const std::string js = kZipHotspots;
    check(parse_hotspots(std::vector<uint8_t>(js.begin(), js.end()), zip, &err) && zip.size() == 9, "parse_hotspots: 9 entries: " + err);
    check(zip.count("jersey/numberback") && zip.count("competition/leftsleevelong"), "keys are group/name in lower case");
    check(!parse_hotspots(std::vector<uint8_t>{'x'}, zip, &err) && !parse_hotspots(std::vector<uint8_t>{'{', '}'}, zip, &err), "garbage / no Hotspots array refused");
    check(parse_hotspots(std::vector<uint8_t>(js.begin(), js.end()), zip, &err), "parse again");

    std::vector<uint8_t> e = orig;
    std::vector<std::string> ch;
    check(patch_hotspot_ebx(e, path, zip, true, &ch, &err), "patch ok: " + err);
    check(e.size() == orig.size(), "same size");
    check(getf(e, 1, 0) == 0.5803f && getf(e, 1, 1) == 0.3831f && getf(e, 1, 2) == 0.4197f && getf(e, 1, 3) == 0.2224f, "numberback rectangle patched (entry 1)");
    check(getf(e, 3, 0) == 0.5806f && getf(e, 3, 3) == 0.3921f, "name rectangle patched (entry 3)");
    check(getf(e, 12, 0) == 0.5859f && getf(e, 12, 3) == 0.8389f, "shorts number patched (entry 12)");
    check(getf(e, 11, 0) == 0.3379f, "shorts team (entry 11) untouched: the zip did not name it");
    check(getf(e, 2, 0) == getf(orig, 2, 0) && getf(e, 2, 1) == getf(orig, 2, 1), "numberfront: the zip's point does not replace the game's rectangle");
    check(getf(e, 4, 1) == 0.2509f && getf(e, 4, 0) == 0.5f, "namearccenter: a point replaces a point");
    check(getf(e, 20, 0) == 0.67431640625f && getf(e, 20, 1) == 0.4287109375f, "sleeve badge rectangle patched (entry 20)");
    check(getf(e, 24, 0) == -1.f && getf(e, 24, 3) == -1.f, "competition Center: the game has it off, the zip's rectangle does not switch it on (entry 24)");
    float rot;
    std::memcpy(&rot, &e[0x90 + 20 * 0x30 + 0x20], 4);
    check(rot == 90.f, "sleeve rotation kept at 90");
    check(getf(e, 0, 0) == -0.5f && getf(e, 7, 0) == -0.5f, "decals on: the zip's 'not used' team / brand applied");
    int stray = 0;
    for (size_t i = 0; i < e.size(); ++i) {
        if (e[i] == orig[i]) continue;
        bool allowed = false;
        for (int en : {0, 1, 3, 4, 12, 20})
            if (i >= 0x90 + size_t(en) * 0x30 && i < 0x90 + size_t(en) * 0x30 + 16) allowed = true;
        stray += !allowed;
    }
    check(stray == 0, "no byte outside the patched rectangles changed (pointer fields, strings, header)");
    check(count_patched(ch) == 6, "change list: one text per patched entry (6), got " + std::to_string(count_patched(ch)));
    // decals off: team / brand stay the game's
    std::vector<uint8_t> e2 = orig;
    check(patch_hotspot_ebx(e2, path, zip, false, nullptr, &err) && getf(e2, 0, 0) == 0.5437f && getf(e2, 7, 0) == -0.5f && getf(e2, 1, 0) == 0.5803f,
          "decals off: team / brand kept, numbers still patched");
    {   // decals off: the change list warns that the game's decal is drawn next to the zip's baked one (two badges); decals on: no such text
        std::vector<uint8_t> e2b = orig, e2c = orig;
        std::vector<std::string> chb, chc;
        patch_hotspot_ebx(e2b, path, zip, false, &chb, &err);
        patch_hotspot_ebx(e2c, path, zip, true, &chc, &err);
        auto has = [](const std::vector<std::string>& v, const std::string& k) { for (const auto& t : v) if (t.find(k) != std::string::npos) return true; return false; };
        check(has(chb, "jersey/team: zip says 'not used'") && has(chb, "baked into the picture") && has(chb, "second badge"), "decals off: the team entry's text explains the two-badge result");
        check(!has(chc, "baked into the picture") && has(chc, "jersey/team (0.5437"), "decals on: the team entry is switched off, no warning");
        check(count_patched(chb) < count_patched(chc) && !has(chb, "baked into the picture) ->"), "decals off: fewer rectangles patched (team kept), the warning text is not counted as a patch");
    }
    // idempotent
    std::vector<uint8_t> e3 = e;
    ch.clear();
    patch_hotspot_ebx(e3, path, zip, true, &ch, &err);
    check(e3 == e && count_patched(ch) == 0, "patching twice: no further change");
    // refusals leave the bytes alone
    std::vector<uint8_t> bad = orig;
    check(!patch_hotspot_ebx(bad, path + "x", zip, true, nullptr, &err) && bad == orig, "another asset path: refused, untouched");
    bad[0x8C] = 24;
    const std::vector<uint8_t> bad_copy = bad;
    check(!patch_hotspot_ebx(bad, path, zip, true, nullptr, &err) && bad == bad_copy, "other entry count: refused");
    std::vector<uint8_t> small(orig.begin(), orig.begin() + 0x500);
    check(!patch_hotspot_ebx(small, path, zip, true, nullptr, &err), "cut-off asset: refused");
    HotspotMap crazy;
    crazy["jersey/numberback"] = {5.f, 0.f, 0.f, 0.f, 0.f};
    crazy["jersey/name"] = {0.5f, 0.5f, 0.4f, 0.4f, 0.f};
    std::vector<uint8_t> e4 = orig;
    check(patch_hotspot_ebx(e4, path, crazy, true, nullptr, &err) && getf(e4, 1, 0) == getf(orig, 1, 0) && getf(e4, 3, 0) == 0.5f, "an out-of-range value is not written, the rest is");
    HotspotMap neg;
    neg["jersey/numberback"] = {-0.5f, -0.5f, -0.5f, -0.5f, 0.f};
    std::vector<uint8_t> e5 = orig;
    patch_hotspot_ebx(e5, path, neg, true, nullptr, &err);
    check(e5 == orig, "a number / name 'not used' is never applied");
    {
        HotspotMap same;
        same["jersey/numberback"] = {getf(orig, 1, 0), getf(orig, 1, 1), getf(orig, 1, 2), getf(orig, 1, 3), 0.f};
        std::vector<uint8_t> e6 = orig;
        std::vector<std::string> c6;
        check(patch_hotspot_ebx(e6, path, same, true, &c6, &err) && e6 == orig && c6.empty(), "equal hotspots: no change, empty list");
    }

    // --- the plan, the table and the runtime on a synthetic game
    using T = GameBuilder::TexSpec;
    World w;
    w.root = base / "hs" / "game";
    w.le = base / "hs" / "le";
    w.g.root = w.root;
    w.g.ebx_data[path] = orig;
    w.g.add_bundle({T{P("home_0_0", "jersey", "jersey_95_0_0_color"), 55, 64, 64, 7, {}, -1, 4096}}, path);
    const std::string other = "content/character/kit/kit_0/testfc_96/home_0_0/hotspots_96_0_0";
    w.g.ebx_data[other] = make_hotspot_ebx(other, rng);
    w.g.add_bundle({T{P("home_0_0", "jersey", "jersey_96_0_0_color", 96), 55, 64, 64, 7, {}, -1, 4096}}, other);
    w.g.write();
    const fs::path dir = w.le / "turbo_output" / "kits" / "95" / "home";
    std::vector<json> tex;
    stage_texture(dir, "jersey_color", 55, 64, 64, 7, tex, w.rng);
    std::ofstream(dir / "hotspots.hotspot", std::ios::binary) << js;
    write_manifest(dir, 95, 0, tex, "hotspots.hotspot");
    StagedKit sk;
    check(load_staged(dir, sk, &err) && !sk.hotspots.empty(), "load_staged finds the staged hotspots.hotspot: " + err);
    GameFiles files(w.root);
    files.init(&err);
    KitIndex idx;
    check(scan_kits(files, stand_in_oodle(), idx, nullptr, &err) && idx.bundles.size() == 2, "scan: two kit bundles");
    Options opt;
    TableBuilder tb;
    Plan plan = build_plan(files, stand_in_oodle(), idx, sk, opt, true, tb, nullptr);
    check(plan.ok, "plan ok: " + plan.error);
    const PlanItem* hi = item_named(plan, "hotspots_95_0_0", "home_0_0");
    check(hi && hi->swap && hi->role == "hotspots" && hi->blocks == 1 && hi->reason.find("patched") != std::string::npos, "the hotspot asset is a plan item and in the table: " + (hi ? hi->reason : "missing"));
    check(!item_named(plan, "hotspots_96_0_0", "home_0_0"), "another club's hotspot asset is not part of the plan");
    std::vector<std::string> dropped;
    auto table = tb.finish(&dropped);
    GameBuilder gb;
    const std::vector<uint8_t> stream = gb.blocks_of(orig, 1 << 20);  // the asset as stored in the cas
    std::vector<Block> bl;
    block_headers(stream, bl);
    const Entry* en = table->match(&stream[size_t(bl[0].payload)], bl[0].csize, bl[0].dsize);
    check(en != nullptr && en->dsize == orig.size(), "the hotspot block is recognised by (csize, dsize, hash)");
    if (en) check(std::vector<uint8_t>(en->data, en->data + en->dsize) == e, "its replacement is the patched asset");
    {
        Runtime rt(w.le, w.root, stand_in_oodle());
        rt.set_hook_installed(true, "");
        std::string e2s;
        check(rt.activate(95, 0, Mode::Swap, &e2s), "activate swap: " + e2s);
        rt.wait_idle();
        std::vector<uint8_t> out(orig.begin(), orig.end());
        check(rt.on_decompressed(&stream[size_t(bl[0].payload)], bl[0].csize, out.data(), bl[0].dsize, int64_t(bl[0].dsize), 0, 3) == 2 && out == e,
              "runtime: the game's decompressed hotspot asset is replaced by the patched one");
        const Status st = rt.status();
        bool timed = false;
        for (const auto& it : st.items)
            if (it.role == "hotspots" && it.hits == 1 && it.first_ms > 0 && it.last_ms >= it.first_ms) timed = true;
        check(timed, "the hotspot item shows its hit with a first / last time");
        rt.flush_log(true);
        std::ifstream lf(rt.log_path());
        std::string log((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
        check(log.find("hotspots_95_0_0") != std::string::npos && log.find("hotspot rectangle(s) patched") != std::string::npos && log.find("jersey/numberback (0.5890,") != std::string::npos,
              "the log lists the patched rectangles with old and new values");
        Options o = rt.get_options();
        o.apply_hotspots = false;
        rt.set_options(o);
        rt.rebuild();
        rt.wait_idle();
        std::vector<uint8_t> out2(orig.begin(), orig.end());
        check(rt.on_decompressed(&stream[size_t(bl[0].payload)], bl[0].csize, out2.data(), bl[0].dsize, int64_t(bl[0].dsize), 0, 3) == 0 && out2 == orig,
              "option off: the hotspot asset is not touched");
        rt.deactivate(95, 0);
        rt.wait_idle();
    }
}

static void test_runtime(const fs::path& base) {
    World w = make_world(base / "rt");
    StagedKit sk = stage_home(w);
    (void)sk;
    // the block bytes the game would decompress
    const auto& c = w.g.chunk_locs[0];
    std::vector<uint8_t> chunk(w.g.cas.begin() + std::ptrdiff_t(c[0]), w.g.cas.begin() + std::ptrdiff_t(c[0] + c[1]));
    std::vector<Block> bl;
    block_headers(chunk, bl);

    {
        Runtime rt(w.le, w.root, stand_in_oodle());
        rt.options.min_csize = 1024;
        rt.options.watch_club = false;
        rt.set_hook_installed(true, "");
        std::string err;
        check(!rt.activate(95, 3, Mode::Observe, &err) && !err.empty(), "activating a slot with nothing staged is refused: " + err);
        check(rt.activate(95, 0, Mode::Observe, &err), "activate observe: " + err);
        rt.wait_idle();
        check(rt.set_slot_options(95, 0, true, false, false), "per-kit options: zip crest served");
        rt.wait_idle();
        Status s = rt.status();
        check(s.state == "ready" && s.blocks > 0, "ready with blocks: " + s.state + " / " + s.message);
        check(fs::exists(rt.state_path()), "swap_state.json written");
        std::vector<uint8_t> out(bl[0].dsize, 0xEE);
        const uint8_t* src = &chunk[size_t(bl[0].payload)];
        check(rt.on_block(src, bl[0].csize, out.data(), bl[0].dsize) == 1 && out[0] == 0xEE, "observe: recognised, output untouched");
        s = rt.status();
        check(s.hits == 1 && s.swapped == 0, "observe counters");
        bool seen_item = false;
        for (const auto& it : s.items)
            if (it.res.find("jersey_95_0_0_color") != std::string::npos && it.hits == 1) seen_item = true;
        check(seen_item, "the item's seen counter is 1");
        check(rt.activate(95, 0, Mode::Swap, &err), "switch to swap");
        rt.wait_idle();
        std::vector<uint8_t> out2(bl[0].dsize, 0xEE);
        check(rt.on_block(src, bl[0].csize, out2.data(), bl[0].dsize) == 2, "swap: overwritten");
        check(std::memcmp(out2.data(), w.jersey_color_data.data(), bl[0].dsize) == 0, "the output is the staged bytes of block 0");
        rt.set_killed(true);
        std::vector<uint8_t> out3(bl[0].dsize, 0xEE);
        check(rt.on_block(src, bl[0].csize, out3.data(), bl[0].dsize) == 0 && out3[0] == 0xEE, "kill switch: nothing recognised, nothing written");
        rt.set_killed(false);
        check(rt.on_block(src, bl[0].csize, out3.data(), bl[0].dsize) == 2, "kill switch released");
        // a block of the same size that is not ours
        std::vector<uint8_t> other(src, src + bl[0].csize);
        other[10] ^= 0xFF;
        check(rt.on_block(other.data(), other.size(), out3.data(), bl[0].dsize) == 0, "an unknown block with the same sizes is left alone");
        // the detour's entry: what the game passes decides whether the swap can work
        {
            std::vector<uint8_t> o1(bl[0].dsize, 0xEE), o2(bl[0].dsize, 0xEE), o3(bl[0].dsize, 0xEE);
            check(rt.on_decompressed(src, bl[0].csize, o1.data(), bl[0].dsize, int64_t(bl[0].dsize), 0, 3) == 2 &&
                      std::memcmp(o1.data(), w.jersey_color_data.data(), bl[0].dsize) == 0,
                  "on_decompressed: full decode -> overwritten");
            const uint64_t before_unserved = rt.status().unserved;
            check(rt.on_decompressed(src, bl[0].csize, o2.data(), bl[0].dsize, int64_t(bl[0].dsize) - 5, 0, 3) == 1 && o2[0] == 0xEE &&
                      rt.status().unserved == before_unserved + 1,
                  "on_decompressed: the original returned another size -> recognised, counted, NOT overwritten");
            const uint64_t cb0 = rt.status().with_callback, ph0 = rt.status().other_phase;
            rt.on_decompressed(src, bl[0].csize, o3.data(), bl[0].dsize, int64_t(bl[0].dsize), 1, 1);
            check(rt.status().with_callback == cb0 + 1 && rt.status().other_phase == ph0 + 1, "on_decompressed: callback and phase counted");
            check(rt.on_decompressed(src, 4, o3.data(), 4, 4, 0, 3) == 0 && rt.on_decompressed(nullptr, bl[0].csize, o3.data(), bl[0].dsize, 0, 0, 3) == 0, "on_decompressed: tiny / null input ignored");
        }
        rt.flush_log(true);
        std::ifstream lf(rt.log_path());
        std::string log((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
        bool crest_left = false;
        for (const auto& it : s.items)
            if (it.res.find("crest_95_0_0_normal") != std::string::npos && !it.swap && it.reason.find("same data") != std::string::npos) crest_left = true;
        check(crest_left, "crest_normal / crest_shorts_normal: one game chunk, two different pictures -> both left to the game");
        check(log.find("blocks overwritten") != std::string::npos && log.find("jersey_95_0_0_color") != std::string::npos && log.find("first ") != std::string::npos &&
                  log.find("threadPhase != 3") != std::string::npos,
              "log file written, with the call ring");
    }
    {  // a new session restores the state and builds the table by itself
        Runtime rt(w.le, w.root, stand_in_oodle());
        rt.start();
        rt.wait_idle();
        Mode m = Mode::Off;
        check(rt.is_active(95, 0, &m) && m == Mode::Swap, "state restored: swap");
        check(rt.status().state == "ready" && rt.table() && rt.table()->size() > 0, "table rebuilt at start");
        rt.deactivate(95, 0);
        rt.wait_idle();
        check(!rt.is_active(95, 0) && rt.table() == nullptr && rt.status().state == "off", "undo: no table, state off");
        std::vector<uint8_t> out(bl[0].dsize, 0xEE);
        check(rt.on_block(&chunk[size_t(bl[0].payload)], bl[0].csize, out.data(), bl[0].dsize) == 0, "after undo nothing is recognised");
    }
    {
        Runtime rt(w.le, w.root, stand_in_oodle());
        rt.start();
        rt.wait_idle();
        check(!rt.is_active(95, 0) && rt.status().state == "off", "undo persisted across a restart");
    }
    {  // the hook is installed once, when a kit is first switched on; a refusal refuses the activation
        Runtime rt(w.le, w.root, stand_in_oodle());
        int installs = 0;
        bool allow = false;
        rt.ensure_hook = [&](std::string* why) {
            ++installs;
            if (!allow && why) *why = "test: not allowed";
            return allow;
        };
        std::string err;
        check(installs == 0, "no hook before a kit is switched on");
        check(!rt.activate(95, 0, Mode::Observe, &err) && err.find("not allowed") != std::string::npos && !rt.is_active(95, 0), "hook refused: the activation is refused, nothing saved: " + err);
        allow = true;
        check(rt.activate(95, 0, Mode::Observe, &err) && installs == 2, "hook installed on the next try");
        check(rt.activate(95, 0, Mode::Swap, &err) && installs == 2, "once installed it is not asked again");
        rt.wait_idle();
        rt.deactivate(95, 0);
        rt.wait_idle();
    }
    {  // a missing Oodle decompressor and a missing game are reported, never thrown
        Runtime rt(w.le, w.root, nullptr);
        std::string err;
        check(rt.activate(95, 0, Mode::Observe, &err), "activate without Oodle is accepted");
        rt.wait_idle();
        check(rt.status().state == "error", "state error without Oodle: " + rt.status().message);
        Runtime rt2(w.le, w.root / "nowhere", stand_in_oodle());
        rt2.activate(95, 0, Mode::Observe, &err);
        rt2.wait_idle();
        check(rt2.status().state == "error", "state error without a game");
    }
}


// ---------------------------------------------------------------- 2.7: every slot of any club
// Club 120 ("testfc_120"): home, away, goalkeeper, third and a loc2 away; the away jersey normal map is the home's chunk (a club-internal share);
// the goalkeeper's normal map is also club 121's (never served). Chunk registration order: see the list below.
struct World2 {
    fs::path root, le;
    GameBuilder g;
    std::mt19937 rng{23};
};
static World2 make_world2(const fs::path& base) {
    World2 w;
    w.root = base / "game";
    w.le = base / "le";
    w.g.root = w.root;
    using T = GameBuilder::TexSpec;
    const int C = 120;
    // 0 home jersey_color, 1 home jersey_normal, 2 home shorts_color, 3 home crest_color
    w.g.add_bundle({T{P("home_0_0", "jersey", "jersey_120_0_0_color", C), 55, 256, 256, 9, {}, -1, 16384}, T{P("home_0_0", "jersey", "jersey_120_0_0_normal", C), 63, 128, 128, 8, {}, -1, 8192},
                    T{P("home_0_0", "shorts", "shorts_120_0_0_color", C), 55, 128, 128, 8, {}, -1, 8192}, T{P("home_0_0", "jersey", "crest_120_0_0_color", C), 61, 64, 64, 7, {}, -1, 8192}});
    // 4 away jersey_color, 5 away jersey_normal (the home's chunk), 6 away shorts_color, 7 away crest_color
    w.g.add_bundle({T{P("away_1_0", "jersey", "jersey_120_1_0_color", C), 55, 256, 256, 9, {}, -1, 16384}, T{P("away_1_0", "jersey", "jersey_120_1_0_normal", C), 63, 128, 128, 8, {}, 1, 8192},
                    T{P("away_1_0", "shorts", "shorts_120_1_0_color", C), 55, 128, 128, 8, {}, -1, 8192}, T{P("away_1_0", "jersey", "crest_120_1_0_color", C), 61, 64, 64, 7, {}, -1, 8192}});
    // 8 goalkeeper jersey_color, 9 goalkeeper jersey_normal
    w.g.add_bundle({T{P("goalie_home_2_0", "jersey", "jersey_120_2_0_color", C), 55, 256, 256, 9, {}, -1, 16384}, T{P("goalie_home_2_0", "jersey", "jersey_120_2_0_normal", C), 63, 128, 128, 8, {}, -1, 8192}});
    // 10 third jersey_color
    w.g.add_bundle({T{P("third_3_0", "jersey", "jersey_120_3_0_color", C), 55, 256, 256, 9, {}, -1, 16384}});
    // 11 loc2 away jersey_color (its own chunk)
    w.g.add_bundle({T{P("loc2_away_41_0", "jersey", "jersey_120_41_0_color", C), 55, 256, 256, 9, {}, -1, 16384}});
    // 12 club 121: shares the goalkeeper's jersey_normal chunk
    w.g.add_bundle({T{P("home_0_0", "jersey", "jersey_121_0_0_normal", 121), 63, 128, 128, 8, {}, 9, 8192}});
    w.g.write();
    return w;
}
struct RoleSpec {
    const char* role;
    uint32_t fmt;
    int w, h, mips;
};
static const RoleSpec kJerseyColor = {"jersey_color", 55, 256, 256, 9}, kJerseyNormal = {"jersey_normal", 63, 128, 128, 8}, kShortsColor = {"shorts_color", 55, 128, 128, 8},
                      kCrestColor = {"crest_color", 61, 64, 64, 7};
static fs::path stage_kit(World2& w, int team, int slot, const char* folder, const std::vector<RoleSpec>& roles, std::map<std::string, std::vector<uint8_t>>* keep = nullptr) {
    const fs::path dir = w.le / "turbo_output" / "kits" / std::to_string(team) / folder;
    std::vector<json> tex;
    for (const RoleSpec& r : roles) {
        std::vector<uint8_t> data;
        stage_texture(dir, r.role, r.fmt, r.w, r.h, r.mips, tex, w.rng, &data);
        if (keep) (*keep)[r.role] = data;
    }
    write_manifest(dir, team, slot, tex);
    return dir;
}
static std::vector<uint8_t> chunk_bytes(const GameBuilder& g, size_t idx) {
    const auto& c = g.chunk_locs[idx];
    return std::vector<uint8_t>(g.cas.begin() + std::ptrdiff_t(c[0]), g.cas.begin() + std::ptrdiff_t(c[0] + c[1]));
}
// the first block of chunk `idx`: its compressed bytes and sizes
struct Blk {
    std::vector<uint8_t> src;
    uint32_t dsize = 0;
};
static Blk first_block(const GameBuilder& g, size_t idx) {
    const auto chunk = chunk_bytes(g, idx);
    std::vector<Block> bl;
    block_headers(chunk, bl);
    Blk b;
    b.src.assign(chunk.begin() + std::ptrdiff_t(bl[0].payload), chunk.begin() + std::ptrdiff_t(bl[0].payload + bl[0].csize));
    b.dsize = bl[0].dsize;
    return b;
}
static const PlanItem* item_of(const Plan& p, const std::string& leaf) {
    for (const auto& i : p.items)
        if (i.res.size() > leaf.size() && i.res.compare(i.res.size() - leaf.size() - 1, leaf.size() + 1, "/" + leaf) == 0) return &i;
    return nullptr;
}

static void test_any_club_slots(const fs::path& base) {
    World2 w = make_world2(base / "w2");
    GameFiles files(w.root);
    std::string err;
    files.init(&err);
    KitIndex idx;
    check(scan_kits(files, stand_in_oodle(), idx, nullptr, &err), "scan club 120: " + err);
    const auto& kinds = idx.kinds[120];
    check(kinds.count("home_0_0") && kinds.count("away_1_0") && kinds.count("goalie_home_2_0") && kinds.count("third_3_0") && kinds.count("loc2_away_41_0") && kinds.size() == 5,
          "the game's kit folders of club 120 are read from its names (home, away, goalkeeper, third, loc2 away), got " + std::to_string(kinds.size()));
    check(idx.kinds[121].size() == 1, "club 121 has one folder");
    uint32_t away_mask = 0;
    for (const auto& b : idx.bundles)
        if (b.team == 120 && b.kind == "away_1_0") away_mask = b.slot_mask;
    check(away_mask == (1u << 1), "bundle slot mask of the away bundle = slot 1");

    Options opt;
    opt.watch_club = false;
    // ---- away: jersey_color + shorts_color served, the normal map is the home's chunk (left), the crest stays the game's (default keep), loc2 away color served too
    std::map<std::string, std::vector<uint8_t>> away_data;
    stage_kit(w, 120, 1, "away", {kJerseyColor, kJerseyNormal, kShortsColor, kCrestColor}, &away_data);
    StagedKit away;
    check(load_staged(w.le / "turbo_output" / "kits" / "120" / "away", away, &err), "load away: " + err);
    {
        TableBuilder tb;
        Plan p = build_plan(files, stand_in_oodle(), idx, away, opt, true, tb, nullptr);
        check(p.ok, "away plan ok: " + p.error);
        const PlanItem* jc = item_of(p, "jersey_120_1_0_color");
        const PlanItem* jn = item_of(p, "jersey_120_1_0_normal");
        const PlanItem* sc = item_of(p, "shorts_120_1_0_color");
        const PlanItem* cc = item_of(p, "crest_120_1_0_color");
        const PlanItem* l2 = item_of(p, "jersey_120_41_0_color");
        check(jc && jc->swap && sc && sc->swap, "away: jersey_color and shorts_color served");
        check(l2 && l2->swap && l2->kind == "loc2_away_41_0", "away: the loc2 away folder is part of the away slot");
        check(jn && !jn->swap && jn->reason.find("used by this club's home_0_0") != std::string::npos, "away: the normal map shared with the home kit stays the game's: " + (jn ? jn->reason : "?"));
        check(cc && !cc->swap && cc->reason.find("club badge stays") != std::string::npos, "away: the crest picture is not served by default (the game's badge stays)");
        check(!item_of(p, "jersey_120_0_0_color") && !item_of(p, "jersey_120_2_0_color") && !item_of(p, "jersey_120_3_0_color"), "away plan: no item of the home / goalkeeper / third kits");
        check(p.probes == 0, "watch_club off: no watch-only items");
    }
    {   // the crest option off: the crest picture is served; the zip's decals off: neither crest nor brand
        Options o2 = opt;
        o2.keep_game_crest = false;
        TableBuilder tb;
        Plan p = build_plan(files, stand_in_oodle(), idx, away, o2, true, tb, nullptr);
        const PlanItem* cc = item_of(p, "crest_120_1_0_color");
        check(cc && cc->swap, "keep_game_crest off: the zip's crest picture is served");
        o2.include_decals = false;
        TableBuilder tb2;
        Plan p2 = build_plan(files, stand_in_oodle(), idx, away, o2, true, tb2, nullptr);
        const PlanItem* cc2 = item_of(p2, "crest_120_1_0_color");
        check(cc2 && !cc2->swap && cc2->reason.find("decals") != std::string::npos, "decals off: crest left");
        check(item_of(p2, "jersey_120_1_0_color")->swap, "decals off: the jersey is still served");
    }
    {   // share within the club: the away kit's normal map is served and the note says the home kit shows it too
        Options o2 = opt;
        o2.share_within_club = true;
        TableBuilder tb;
        Plan p = build_plan(files, stand_in_oodle(), idx, away, o2, true, tb, nullptr);
        const PlanItem* jn = item_of(p, "jersey_120_1_0_normal");
        check(jn && jn->swap && jn->reason.find("home_0_0") != std::string::npos && jn->reason.find("show it too") != std::string::npos, "share_within_club: served, with the note: " + (jn ? jn->reason : "?"));
        // a table over BOTH home and away with different normal maps: the shared chunk is wanted with two different pictures -> both dropped
        stage_kit(w, 120, 0, "home", {kJerseyColor, kJerseyNormal}, nullptr);
        StagedKit home;
        load_staged(w.le / "turbo_output" / "kits" / "120" / "home", home, &err);
        TableBuilder tb2;
        Plan ph = build_plan(files, stand_in_oodle(), idx, home, o2, true, tb2, nullptr);
        Plan pa = build_plan(files, stand_in_oodle(), idx, away, o2, true, tb2, nullptr);
        std::set<uint32_t> dropped;
        std::vector<std::string> dr;
        auto table = tb2.finish(&dr, &dropped);
        const PlanItem* hn = item_of(ph, "jersey_120_0_0_normal");
        const PlanItem* an = item_of(pa, "jersey_120_1_0_normal");
        check(hn && an && dropped.count(hn->table_item) && dropped.count(an->table_item), "home + away with different pictures for the shared chunk: both are dropped (no half-served kit)");
        check(!dropped.count(item_of(pa, "jersey_120_1_0_color")->table_item), "... the other textures of the away kit stay");
        // the same picture for both kits is fine (one entry, two counters)
        (void)table;
    }
    // ---- goalkeeper and third
    stage_kit(w, 120, 2, "goalkeeper", {kJerseyColor, kJerseyNormal}, nullptr);
    stage_kit(w, 120, 3, "third", {kJerseyColor}, nullptr);
    StagedKit gk, third;
    load_staged(w.le / "turbo_output" / "kits" / "120" / "goalkeeper", gk, &err);
    load_staged(w.le / "turbo_output" / "kits" / "120" / "third", third, &err);
    {
        TableBuilder tb;
        Plan p = build_plan(files, stand_in_oodle(), idx, gk, opt, true, tb, nullptr);
        const PlanItem* jc = item_of(p, "jersey_120_2_0_color");
        const PlanItem* jn = item_of(p, "jersey_120_2_0_normal");
        check(p.ok && jc && jc->swap, "goalkeeper: jersey_color served");
        check(jn && !jn->swap && jn->reason.find("team 121") != std::string::npos, "goalkeeper: the normal map another club uses stays the game's, even with sharing on: " + (jn ? jn->reason : "?"));
        Options o2 = opt;
        o2.share_within_club = true;
        TableBuilder tb2;
        Plan p2 = build_plan(files, stand_in_oodle(), idx, gk, o2, true, tb2, nullptr);
        check(!item_of(p2, "jersey_120_2_0_normal")->swap, "share_within_club never serves a texture another CLUB uses");
        TableBuilder tb3;
        Plan p3 = build_plan(files, stand_in_oodle(), idx, third, opt, true, tb3, nullptr);
        check(p3.ok && item_of(p3, "jersey_120_3_0_color") && item_of(p3, "jersey_120_3_0_color")->swap, "third: jersey_color served");
    }
    {   // a club without the slot: a clear plan error, nothing written
        StagedKit fourth = third;
        fourth.slot = 4;
        TableBuilder tb;
        Plan p = build_plan(files, stand_in_oodle(), idx, fourth, opt, true, tb, nullptr);
        check(!p.ok && p.error.find("no 3D kit textures") != std::string::npos, "a slot the game has no kit for: an error that says so: " + p.error);
    }
    // ---- watch only: the other kit folders of the club are registered without replacement data
    {
        Options o2;
        o2.min_csize = 1024;
        TableBuilder tb;
        Plan p = build_plan(files, stand_in_oodle(), idx, away, o2, true, tb, nullptr);
        check(p.ok && p.probes > 0, "watch_club on: watch-only items for the club's other kit folders: " + std::to_string(p.probes));
        const PlanItem* hp = item_of(p, "jersey_120_0_0_color");
        const PlanItem* tp = item_of(p, "jersey_120_3_0_color");
        const PlanItem* gp = item_of(p, "jersey_120_2_0_normal");
        check(hp && hp->probe && !hp->swap && tp && tp->probe, "home and third jersey colours are watched");
        check(!gp || !gp->probe, "a chunk another club uses is not watched (a hit would say nothing about this club)");
        auto table = tb.finish(nullptr);
        const Blk hb = first_block(w.g, 0), ab = first_block(w.g, 4);
        const Entry* he = table->match(hb.src.data(), hb.src.size(), hb.dsize);
        check(he && !table->swap_item(he->item) && he->data == nullptr, "a watched block is found but has no replacement and no swap flag");
        const Entry* ae = table->match(ab.src.data(), ab.src.size(), ab.dsize);
        check(ae && table->swap_item(ae->item) && ae->data, "the away block is the one that is replaced");
        check(table->bytes() > 0, "watch-only blocks add no replacement bytes");
    }
    {   // sharing on: a byte-identical block inside ANOTHER chunk of the same club is still left to the game (no patched fragment)
        World2 w3 = make_world2(base / "w2c");
        GameBuilder& g = w3.g;
        std::vector<uint8_t> raw;
        {
            const auto st = chunk_bytes(g, 0);  // home jersey_color raw bytes
            read_blocks(st, stand_in_oodle(), raw, nullptr);
        }
        using T = GameBuilder::TexSpec;
        g.add_bundle({T{P("third_3_0", "shorts", "shorts_120_3_0_color", 120), 55, 256, 256, 9, raw, -1, 16384}});
        g.write();
        GameFiles f3(w3.root);
        f3.init(nullptr);
        KitIndex i3;
        scan_kits(f3, stand_in_oodle(), i3, nullptr, nullptr);
        stage_kit(w3, 120, 0, "home", {kJerseyColor}, nullptr);
        StagedKit hk;
        load_staged(w3.le / "turbo_output" / "kits" / "120" / "home", hk, nullptr);
        Options o3;
        o3.watch_club = false;
        o3.share_within_club = true;
        TableBuilder tb3;
        Plan p3 = build_plan(f3, stand_in_oodle(), i3, hk, o3, true, tb3, nullptr);
        const PlanItem* hj = item_of(p3, "jersey_120_0_0_color");
        check(hj && !hj->swap && hj->reason.find("byte-identical") != std::string::npos, "identical block in another chunk of the club stays the game's even with sharing on: " + (hj ? hj->reason : "?"));
    }
    {   // a watch-only record with the same block as a real item never wins and never conflicts
        TableBuilder b;
        std::vector<uint8_t> key(64, 5), rep1(64, 1);
        auto real = std::make_shared<std::vector<uint8_t>>(rep1);
        const uint32_t probe = b.add_item(nullptr, false), item = b.add_item(real, true);
        uint64_t head;
        std::memcpy(&head, key.data(), 8);
        const uint64_t f = fnv1a64(key.data(), key.size());
        check(b.add_block(probe, 64, 64, head, f, nullptr, nullptr), "watch-only block accepted without data");
        check(!b.add_block(item, 64, 64, head, f, nullptr, nullptr), "a real item needs data");
        check(b.add_block(item, 64, 64, head, f, real->data(), nullptr), "real block");
        std::set<uint32_t> dropped;
        auto t = b.finish(nullptr, &dropped);
        const Entry* e = t->match(key.data(), 64, 64);
        check(dropped.empty() && t->size() == 1 && e && e->data && t->swap_item(e->item), "the real record survives, nothing is dropped");
        if (e) {
            t->count_hit(*e);
            check(t->hits(probe) == 1 && t->hits(item) == 1, "both counters tick");
        }
        // watch-only first, real second (order must not matter)
        TableBuilder b2;
        const uint32_t i2 = b2.add_item(real, true), p2 = b2.add_item(nullptr, false);
        b2.add_block(i2, 64, 64, head, f, real->data(), nullptr);
        b2.add_block(p2, 64, 64, head, f, nullptr, nullptr);
        auto t2 = b2.finish(nullptr, nullptr);
        const Entry* e2 = t2->match(key.data(), 64, 64);
        check(t2->size() == 1 && e2 && e2->data, "order does not matter");
    }
}

static void test_runtime_slots(const fs::path& base) {
    World2 w = make_world2(base / "w2rt");
    std::map<std::string, std::vector<uint8_t>> away_data, gk_data;
    stage_kit(w, 120, 1, "away", {kJerseyColor, kShortsColor}, &away_data);
    stage_kit(w, 120, 2, "goalkeeper", {kJerseyColor}, &gk_data);
    Runtime rt(w.le, w.root, stand_in_oodle());
    rt.options.min_csize = 1024;
    rt.set_hook_installed(true, "");
    std::string err;
    check(rt.activate(120, 1, Mode::Swap, &err), "activate away: " + err);
    check(rt.activate(120, 2, Mode::Observe, &err), "activate goalkeeper (observe): " + err);
    rt.wait_idle();
    Status s = rt.status();
    check(s.state == "ready", "two slots of club 120: ready: " + s.state + " / " + s.message);
    check(!rt.kinds_of(120).empty() && rt.kinds_of(120).count("third_3_0") && rt.kinds_of(7).empty(), "kinds_of(club) from the index");
    const Blk away_blk = first_block(w.g, 4), home_blk = first_block(w.g, 0), third_blk = first_block(w.g, 10), gk_blk = first_block(w.g, 8), l2_blk = first_block(w.g, 11);
    std::vector<uint8_t> out(away_blk.dsize, 0xEE);
    check(rt.on_block(away_blk.src.data(), away_blk.src.size(), out.data(), away_blk.dsize) == 2 && std::memcmp(out.data(), away_data["jersey_color"].data(), out.size()) == 0,
          "the away jersey block is overwritten with the away picture");
    std::vector<uint8_t> out2(gk_blk.dsize, 0xEE);
    check(rt.on_block(gk_blk.src.data(), gk_blk.src.size(), out2.data(), gk_blk.dsize) == 1 && out2[0] == 0xEE, "goalkeeper in observe mode: recognised, not overwritten (no leak between the slots)");
    std::vector<uint8_t> out3(home_blk.dsize, 0xEE), out4(third_blk.dsize, 0xEE);
    check(rt.on_block(home_blk.src.data(), home_blk.src.size(), out3.data(), home_blk.dsize) == 1 && out3[0] == 0xEE, "the home kit (not switched on) is only watched, never overwritten");
    check(rt.on_block(third_blk.src.data(), third_blk.src.size(), out4.data(), third_blk.dsize) == 1 && out4[0] == 0xEE, "the third kit is only watched");
    std::vector<uint8_t> out5(l2_blk.dsize, 0xEE);
    check(rt.on_block(l2_blk.src.data(), l2_blk.src.size(), out5.data(), l2_blk.dsize) == 2, "the loc2 away folder is overwritten too (same slot)");
    s = rt.status();
    auto seen = [&](const std::string& kind) -> uint64_t {
        for (const auto& k : s.kinds)
            if (k.team == 120 && k.kind == kind) return k.blocks_seen;
        return uint64_t(-1);
    };
    check(seen("away_1_0") == 1 && seen("home_0_0") == 1 && seen("third_3_0") == 1 && seen("goalie_home_2_0") == 1 && seen("loc2_away_41_0") == 1, "the per-kit-folder summary shows which folders the match loaded");
    rt.flush_log(true);
    std::ifstream lf(rt.log_path());
    std::string log((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
    check(log.find("KIT FOLDERS THE GAME LOADED") != std::string::npos && log.find("team 120 third_3_0: 1 of") != std::string::npos && log.find("team 120 home_0_0: 1 of") != std::string::npos &&
              log.find("kit index: scanned") != std::string::npos && log.find("zip decals on") != std::string::npos,
          "the log lists the kit folders the match loaded");
    check(fs::exists(rt.index_cache_path()), "the kit index was saved after the first scan");
    // per-kit options: persisted, a restart brings them back
    check(rt.set_slot_options(120, 1, false, false, true), "set_slot_options");
    rt.wait_idle();
    {
        Runtime r2(w.le, w.root, stand_in_oodle());
        r2.start();
        r2.wait_idle();
        bool d = true, k = true, sc = false;
        check(r2.slot_options(120, 1, &d, &k, &sc) && !d && !k && sc, "per-kit options survive a restart");
        check(r2.status().state == "ready" && r2.status().index_cached, "the restart reads the saved kit index instead of scanning the game");
        Mode m;
        check(r2.is_active(120, 2, &m) && m == Mode::Observe, "the goalkeeper's mode survives");
    }
    rt.deactivate(120, 1);
    rt.deactivate(120, 2);
    rt.wait_idle();
}

static void test_guards_and_cache(const fs::path& base) {
    World2 w = make_world2(base / "w2g");
    stage_kit(w, 120, 1, "away", {kJerseyColor}, nullptr);
    const fs::path state = w.le / "turbo_output" / "kits" / "swap_state.json";
    fs::create_directories(state.parent_path());
    {   // a version 1 state file (no options): defaults, and it still loads
        std::ofstream(state) << R"({"version":1,"kits":[{"team":120,"slot":"away","slot_type":1,"mode":"swap"}]})";
        Runtime rt(w.le, w.root, stand_in_oodle());
        rt.start();
        rt.wait_idle();
        bool d = false, k = false, sc = true;
        check(rt.slot_options(120, 1, &d, &k, &sc) && d && k && !sc, "a version 1 state file loads with the default options");
        check(rt.status().state == "ready", "... and builds: " + rt.status().state);
        rt.deactivate(120, 1);
        rt.wait_idle();
        fs::remove(rt.index_cache_path());
        fs::remove(rt.log_path());
    }
    std::ofstream(state) << R"({"version":2,"kits":[{"team":120,"slot":"away","slot_type":1,"mode":"swap","decals":true,"keep_game_crest":true,"share_within_club":false}]})";
    const fs::path opt_in = w.le / "turbo_output" / "kits3d_on.txt";
    {   // no opt-in file: a kit left on reads nothing, scans nothing, writes nothing
        Runtime rt(w.le, w.root, stand_in_oodle());
        rt.require_opt_in = true;
        int hook_calls = 0;
        rt.ensure_hook = [&](std::string*) { ++hook_calls; return true; };
        rt.start();
        rt.wait_idle();
        check(rt.status().state == "off" && rt.status().message.find("kits3d_on.txt") != std::string::npos, "no opt-in file: state off, says why: " + rt.status().message);
        check(rt.kinds_of(120).empty() && !fs::exists(rt.index_cache_path()) && !fs::exists(rt.log_path()), "no opt-in file: no scan, no cache, no log");
        std::string err;
        check(!rt.activate(120, 1, Mode::Swap, &err) && err.find("kits3d_on.txt") != std::string::npos, "no opt-in file: activation refused: " + err);
        check(hook_calls == 0, "no opt-in file: the hook was never asked for");
    }
    std::ofstream(opt_in) << "on";
    {   // the kill switch before start: no scan; released: the skipped build runs
        Runtime rt(w.le, w.root, stand_in_oodle());
        rt.require_opt_in = true;
        rt.ensure_hook = [&](std::string*) { return true; };
        rt.set_hook_installed(true, "");
        rt.set_killed(true);
        rt.start();
        rt.wait_idle();
        check(rt.status().state == "off" && rt.status().message.find("kits3d_off.txt") != std::string::npos && rt.kinds_of(120).empty() && !fs::exists(rt.index_cache_path()),
              "kill switch at start: no scan, no cache: " + rt.status().message);
        rt.set_killed(false);
        rt.wait_idle();
        check(rt.status().state == "ready" && fs::exists(rt.index_cache_path()), "kill switch released: the skipped build runs: " + rt.status().state);
        check(!rt.status().index_cached, "... as a scan (no cache yet)");
    }
    {   // a second session: the saved index is used; a changed toc invalidates it
        Runtime rt(w.le, w.root, stand_in_oodle());
        rt.require_opt_in = true;
        rt.start();
        rt.wait_idle();
        check(rt.status().state == "ready" && rt.status().index_cached, "second session: kit index from the saved copy");
    }
    {
        std::ofstream t(w.root / "Data" / "Win32" / "contentsb.toc", std::ios::binary | std::ios::app);
        t.put('\0');
    }
    {
        Runtime rt(w.le, w.root, stand_in_oodle());
        rt.require_opt_in = true;
        rt.start();
        rt.wait_idle();
        check(rt.status().state == "ready" && !rt.status().index_cached, "a changed toc (a game update): the index is scanned again");
    }
    {   // a damaged cache is a miss, never a failure
        std::ofstream bad(w.le / "turbo_output" / "kits" / "kit_index.cache", std::ios::binary | std::ios::trunc);
        bad << "not a cache";
    }
    {
        Runtime rt(w.le, w.root, stand_in_oodle());
        rt.start();
        rt.wait_idle();
        check(rt.status().state == "ready" && !rt.status().index_cached, "a damaged cache: scanned again");
        GameFiles gf(w.root);
        gf.init(nullptr);
        KitIndex a, b;
        scan_kits(gf, stand_in_oodle(), a, nullptr, nullptr);
        const fs::path cf = w.le / "cache_roundtrip.bin";
        check(save_index_cache(gf, a, cf) && load_index_cache(gf, cf, b) && b.bundles.size() == a.bundles.size() && b.users.size() == a.users.size() && b.kinds == a.kinds && b.from_cache,
              "index cache round trip");
        // truncated copies never load
        std::ifstream in(cf, std::ios::binary);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        bool any_loaded = false;
        for (size_t cut : {all.size() - 1, all.size() / 2, size_t(20), size_t(3)}) {
            std::ofstream(cf, std::ios::binary | std::ios::trunc).write(all.data(), std::streamsize(cut));
            KitIndex c;
            any_loaded = any_loaded || load_index_cache(gf, cf, c);
        }
        check(!any_loaded, "truncated cache files are refused");
    }
    // ---- the kill switch stops a build that is running (the plan reads the game's texture headers through the slow decompressor)
    {
        fs::remove(w.le / "turbo_output" / "kits" / "kit_index.cache");
        std::atomic<int> calls{0};
        OodleFn slow = [&](const uint8_t* src, size_t n, uint8_t* dst, size_t dn) {
            ++calls;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            return stand_in_oodle()(src, n, dst, dn);
        };
        Runtime rt(w.le, w.root, slow);
        rt.require_opt_in = true;
        rt.start();
        for (int i = 0; i < 200 && calls.load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(calls.load() > 0, "the build is running (slow decompressor)");
        rt.set_killed(true);
        rt.wait_idle();
        const int at_stop = calls.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        check(calls.load() == at_stop && rt.status().state == "off", "the kill switch stops a running build: " + std::to_string(at_stop) + " decompressions, state " + rt.status().state);
        check(rt.table() == nullptr, "... and publishes no table");
    }
    fs::remove(opt_in);
}

// ---------------------------------------------------------------- optional: the real install (read only)
static std::string g_real_hotspots;  // --hotspots FILE: the zip's hotspots.hotspot, staged beside the stand-in textures of the real-install smoke test
static bool g_real_share = false, g_real_zip_crest = false, g_real_nowatch = false;
static int real_game(const std::string& game, int team, int slot) {
    OodleFn oodle;
#ifdef _WIN32
    HMODULE m = LoadLibraryW((fs::path(game) / "oo2core_9_win64.dll").wstring().c_str());
    using Fn = int64_t (*)(const void*, int64_t, void*, int64_t, int, int, int, void*, int64_t, void*, void*, void*, int64_t, int);
    Fn fn = m ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "OodleLZ_Decompress"))) : nullptr;
    if (fn)
        oodle = [fn](const uint8_t* s, size_t n, uint8_t* d, size_t dn) { return fn(s, int64_t(n), d, int64_t(dn), 1, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, 3) == int64_t(dn); };
#endif
    if (!oodle) {
        std::printf("no Oodle (Windows only)\n");
        return 2;
    }
    GameFiles files(game);
    std::string err;
    if (!files.init(&err)) {
        std::printf("init: %s\n", err.c_str());
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();
    KitIndex idx;
    Progress pg;
    if (!scan_kits(files, oodle, idx, &pg, &err)) {
        std::printf("scan: %s\n", err.c_str());
        return 2;
    }
    std::printf("scan: %zu tocs, %d bundles read, %zu kit bundles, %zu kit chunks, %d unreadable, %.1f s\n", idx.tocs.size(), idx.bundles_seen, idx.bundles.size(), idx.users.size(),
                idx.unreadable, idx.seconds);
    // stand-in staged textures of the game's own sizes: gather the club's textures first
    fs::path tmp = fs::temp_directory_path() / "kitswap_real";
    fs::remove_all(tmp);
    // roles from a first plan with an empty staged set tells us each texture's header; simplest: a plan per role needs the sizes, so
    // build the staged set from the game's headers through a dry plan
    StagedKit empty;
    empty.team = team;
    empty.slot = slot;
    StagedTexture dummy;
    empty.textures.push_back(dummy);
    // the staged set: one random DDS per role found (the spec of the 24 roles with the game's sizes)
    struct R { const char* role; uint32_t fmt; int w, h, mips; };
    const R roles[] = {{"jersey_color", 55, 2048, 2048, 12}, {"jersey_coeff", 54, 1024, 1024, 11}, {"jersey_normal", 63, 1024, 1024, 11}, {"jersey_taa", 66, 1024, 1024, 11},
                       {"shorts_color", 55, 1024, 512, 11}, {"shorts_coeff", 54, 1024, 512, 11}, {"shorts_normal", 63, 1024, 512, 11}, {"shorts_taa", 66, 1024, 512, 11},
                       {"socks_color", 55, 512, 256, 10}, {"socks_coeff", 54, 512, 256, 10}, {"socks_normal", 63, 512, 256, 10}, {"socks_taa", 66, 512, 256, 10},
                       {"brand_jersey_color", 61, 256, 256, 9}, {"brand_jersey_normal", 63, 256, 256, 9}, {"brand_jersey_coeff", 60, 128, 128, 8},
                       {"crest_color", 61, 256, 256, 9}, {"crest_normal", 63, 256, 256, 9}, {"crest_coeff", 60, 128, 128, 8},
                       {"brand_shorts_color", 61, 256, 256, 9}, {"brand_shorts_normal", 63, 256, 256, 9}, {"brand_shorts_coeff", 60, 128, 128, 8},
                       {"crest_shorts_color", 61, 256, 256, 9}, {"crest_shorts_normal", 63, 256, 256, 9}, {"crest_shorts_coeff", 60, 128, 128, 8}};
    std::mt19937 rng(5);
    std::vector<json> tex;
    for (const R& r : roles) stage_texture(tmp, r.role, r.fmt, r.w, r.h, r.mips, tex, rng);
    if (!g_real_hotspots.empty()) fs::copy_file(g_real_hotspots, tmp / "hotspots.hotspot", fs::copy_options::overwrite_existing);
    write_manifest(tmp, team, slot, tex, g_real_hotspots.empty() ? "" : "hotspots.hotspot");
    StagedKit sk;
    if (!load_staged(tmp, sk, &err)) {
        std::printf("staged: %s\n", err.c_str());
        return 2;
    }
    Options opt;
    opt.share_within_club = g_real_share;
    opt.keep_game_crest = !g_real_zip_crest;
    opt.watch_club = !g_real_nowatch;
    {
        const fs::path cf = fs::temp_directory_path() / "kitswap_real_index.cache";
        KitIndex again;
        const auto tc = std::chrono::steady_clock::now();
        const bool saved = save_index_cache(files, idx, cf);
        const double save_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - tc).count();
        const auto tl = std::chrono::steady_clock::now();
        const bool loaded = load_index_cache(files, cf, again);
        std::error_code ec;
        std::printf("index cache: saved=%d (%.2f s, %llu bytes) loaded=%d in %.2f s, same bundles %d users %d\n", saved, save_s, static_cast<unsigned long long>(fs::file_size(cf, ec)), loaded,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - tl).count(), int(again.bundles.size() == idx.bundles.size()), int(again.users.size() == idx.users.size()));
        fs::remove(cf, ec);
        std::string ks;
        for (const auto& k : idx.kinds[team]) ks += k + " ";
        std::printf("kit folders of team %d: %s\n", team, ks.c_str());
    }
    TableBuilder tb;
    const auto t1 = std::chrono::steady_clock::now();
    Plan plan = build_plan(files, oodle, idx, sk, opt, false, tb, &pg);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    std::printf("plan: ok=%d error=%s swapped=%d left=%d in %.1f s (total %.1f s)\n", plan.ok, plan.error.c_str(), plan.swapped, plan.skipped, s,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    for (const auto& i : plan.items)
        std::printf("  %-18s %-34s %-5s blocks %2d %s\n", i.kind.c_str(), i.res.substr(i.res.rfind('/') + 1).c_str(), i.swap ? "SWAP" : i.probe ? "watch" : "left", i.blocks, i.reason.c_str());
    std::printf("watch-only items: %d\n", plan.probes);
    for (const auto& n : plan.notes) std::printf("  note: %s\n", n.c_str());
    std::vector<std::string> dropped;
    auto table = tb.finish(&dropped);
    std::printf("table: %zu blocks, %llu bytes, csize %u..%u, dropped %zu\n", table->size(), static_cast<unsigned long long>(table->bytes()), table->min_csize(), table->max_csize(), dropped.size());
    fs::remove_all(tmp);
    return plan.ok ? 0 : 1;
}

int main(int argc, char** argv) {
    std::string game;
    int team = 95, slot = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--game" && i + 1 < argc) game = argv[++i];
        else if (a == "--team" && i + 1 < argc) team = std::atoi(argv[++i]);
        else if (a == "--slot" && i + 1 < argc) slot = std::atoi(argv[++i]);
        else if (a == "--hotspots" && i + 1 < argc) g_real_hotspots = argv[++i];
        else if (a == "--share") g_real_share = true;
        else if (a == "--zip-crest") g_real_zip_crest = true;
        else if (a == "--no-watch") g_real_nowatch = true;
    }
    if (!game.empty()) return real_game(game, team, slot);
    const fs::path base = fs::temp_directory_path() / "turbo_kit_swap_test";
    fs::remove_all(base);
    fs::create_directories(base);
    test_names();
    test_blocks();
    test_scan_and_plan(base);
    test_table(base);
    test_runtime(base);
    test_hotspots(base);
    test_any_club_slots(base);
    test_runtime_slots(base);
    test_guards_and_cache(base);
    fs::remove_all(base);
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
