// The synthetic "game" of the 3D kit swap tests (core/kit_swap.h): a layout.toc DbObject, one toc, one cas file whose blocks use a
// stand-in for Oodle (csize == dsize, the payload is the raw data), and staged DDS textures. Never an EA file. Shared by the catalog runner
// (tests/catalog/test_kit_swap.cpp) and the native suite (tests/native/test_kit_share.h). Header only.
#pragma once
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "core/kit_swap.h"
#include "nlohmann/json.hpp"

namespace kitworld {
namespace ks = turbo::kitswap;
namespace fs = std::filesystem;
using nlohmann::json;

// ---------------------------------------------------------------- byte helpers
inline void be32(std::vector<uint8_t>& d, uint32_t v) {
    for (int i = 3; i >= 0; --i) d.push_back(uint8_t(v >> (8 * i)));
}
inline void le32(std::vector<uint8_t>& d, uint32_t v) {
    for (int i = 0; i < 4; ++i) d.push_back(uint8_t(v >> (8 * i)));
}
inline void put_le32(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) d[at + size_t(i)] = uint8_t(v >> (8 * i));
}
inline void put_le16(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    d[at] = uint8_t(v);
    d[at + 1] = uint8_t(v >> 8);
}

// DbObject writer (just what layout.toc needs)
inline void varint(std::vector<uint8_t>& d, uint64_t v) {
    while (v >= 0x80) {
        d.push_back(uint8_t(v | 0x80));
        v >>= 7;
    }
    d.push_back(uint8_t(v));
}
inline void db_named(std::vector<uint8_t>& d, uint8_t type, const std::string& name) {
    d.push_back(type);
    d.insert(d.end(), name.begin(), name.end());
    d.push_back(0);
}
inline std::vector<uint8_t> db_obj(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> v;
    varint(v, body.size() + 1);
    v.insert(v.end(), body.begin(), body.end());
    v.push_back(0);
    return v;
}
inline std::vector<uint8_t> make_layout(uint32_t persistent, const std::string& pkg) {
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
inline const char* kPkg = "superbundlelayout/installpackage_00";
inline const uint32_t kPersistent = 0xAABBCCDDu;
inline const char* kCasId = "0000aabbccdd0001";

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
            const uint32_t dxgi = ks::dxgi_of_format(t.fmt);
            const auto sizes = ks::bc_mip_sizes(dxgi, t.w, t.h, t.mips);
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

inline ks::OodleFn stand_in_oodle() {
    return [](const uint8_t* src, size_t n, uint8_t* dst, size_t dn) {
        if (n != dn) return false;
        std::memcpy(dst, src, n);
        return true;
    };
}

// staged DDS: header + `data` (random) for a texture of the given format
inline void stage_texture(const fs::path& dir, const std::string& role, uint32_t fmt, int w, int h, int mips, std::vector<json>& textures, std::mt19937& rng,
                          std::vector<uint8_t>* keep = nullptr) {
    const uint32_t dxgi = ks::dxgi_of_format(fmt);
    const auto sizes = ks::bc_mip_sizes(dxgi, w, h, mips);
    uint64_t total = 0;
    for (auto s : sizes) total += s;
    std::vector<uint8_t> f(ks::kDdsHeader, 0);
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

inline void write_manifest(const fs::path& dir, int team, int slot, const std::vector<json>& textures, const std::string& hotspots = "") {
    json j = {{"turbo_kit_stage", 1}, {"teamid", team}, {"teamkittypetechid", slot}, {"textures", textures}, {"hotspots", hotspots}};
    std::ofstream(dir / "manifest.json") << j.dump(2);
}

inline std::string P(const char* kind, const char* part, const char* leaf, int team = 95) {
    return std::string("content/character/kit/kit_0/testfc_") + std::to_string(team) + "/" + kind + "/" + part + "/" + leaf;
}

}  // namespace kitworld
