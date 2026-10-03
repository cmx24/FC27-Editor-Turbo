#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#define STBI_MAX_DIMENSIONS 8192
#include "stb/stb_image.h"
#define STB_DXT_IMPLEMENTATION
#include "stb/stb_dxt.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace turbo {

static bool fail(std::string* err, const char* why) {
    if (err) *err = why;
    return false;
}

static uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
static uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

// ---------------------------------------------------------------- DDS decode
static void rgb565(uint16_t c, uint8_t out[3]) {
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = uint8_t((r << 3) | (r >> 2));
    out[1] = uint8_t((g << 2) | (g >> 4));
    out[2] = uint8_t((b << 3) | (b >> 2));
}

// colour part of a BC1/BC2/BC3 block into 16 RGBA texels; four_colour = BC2/BC3 (always 4-colour mode)
static void decode_colour_block(const uint8_t* b, uint8_t texels[16][4], bool four_colour) {
    uint16_t c0 = rd16(b), c1 = rd16(b + 2);
    uint8_t pal[4][4];
    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (four_colour || c0 > c1) {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
    }
    uint32_t idx = rd32(b + 4);
    for (int i = 0; i < 16; ++i) {
        const uint8_t* c = pal[(idx >> (2 * i)) & 3];
        std::memcpy(texels[i], c, 4);
    }
}

static void decode_bc3_alpha(const uint8_t* b, uint8_t texels[16][4]) {
    int a0 = b[0], a1 = b[1];
    int pal[8];
    pal[0] = a0;
    pal[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) pal[1 + i] = ((7 - i) * a0 + i * a1) / 7;
    } else {
        for (int i = 1; i <= 4; ++i) pal[1 + i] = ((5 - i) * a0 + i * a1) / 5;
        pal[6] = 0;
        pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) texels[i][3] = uint8_t(pal[(bits >> (3 * i)) & 7]);
}

static void decode_bc2_alpha(const uint8_t* b, uint8_t texels[16][4]) {
    for (int i = 0; i < 16; ++i) {
        int v = (b[i / 2] >> ((i & 1) * 4)) & 15;
        texels[i][3] = uint8_t(v * 17);
    }
}

static int mask_shift(uint32_t m) {
    if (!m) return 0;
    int s = 0;
    while (!(m & 1)) { m >>= 1; ++s; }
    return s;
}
static uint8_t mask_value(uint32_t px, uint32_t m) {
    if (!m) return 255;
    int s = mask_shift(m);
    uint32_t v = (px & m) >> s, max = m >> s;
    return uint8_t(max ? (v * 255 + max / 2) / max : 0);
}

bool decode_dds(const std::vector<uint8_t>& d, Rgba& out, std::string* err) {
    if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) return fail(err, "not a DDS file");
    const uint8_t* h = d.data() + 4;
    if (rd32(h) != 124) return fail(err, "DDS header size is not 124");
    int height = int(rd32(h + 8)), width = int(rd32(h + 12));
    if (width <= 0 || height <= 0 || width > kMaxImageSide || height > kMaxImageSide) return fail(err, "DDS size out of range");
    const uint8_t* pf = h + 72;
    uint32_t pf_flags = rd32(pf + 4);
    uint32_t four = rd32(pf + 8);
    size_t off = 128;
    enum { NONE, BC1, BC2, BC3, RGBA32, BGRA32, MASKED } fmt = NONE;
    uint32_t bits = rd32(pf + 12), rm = rd32(pf + 16), gm = rd32(pf + 20), bm = rd32(pf + 24), am = rd32(pf + 28);
    if (pf_flags & 0x4) {
        if (four == 0x31545844u) fmt = BC1;          // DXT1
        else if (four == 0x32545844u || four == 0x33545844u) fmt = BC2;  // DXT2 / DXT3
        else if (four == 0x34545844u || four == 0x35545844u) fmt = BC3;  // DXT4 / DXT5
        else if (four == 0x30315844u) {                                  // DX10
            if (d.size() < 148) return fail(err, "DDS DX10 header is cut off");
            uint32_t dxgi = rd32(d.data() + 128);
            off = 148;
            if (dxgi == 70 || dxgi == 71 || dxgi == 72) fmt = BC1;
            else if (dxgi == 73 || dxgi == 74 || dxgi == 75) fmt = BC2;
            else if (dxgi == 76 || dxgi == 77 || dxgi == 78) fmt = BC3;
            else if (dxgi == 27 || dxgi == 28 || dxgi == 29) fmt = RGBA32;
            else if (dxgi == 87 || dxgi == 88 || dxgi == 90 || dxgi == 91) fmt = BGRA32;
            else return fail(err, "DDS DX10 format not supported (only BC1-BC3 and 8-bit RGBA)");
        } else {
            return fail(err, "DDS compression not supported (only DXT1, DXT3, DXT5)");
        }
    } else if ((pf_flags & 0x40) && (bits == 32 || bits == 24)) {
        fmt = MASKED;
    } else {
        return fail(err, "DDS pixel format not supported");
    }
    out.w = width;
    out.h = height;
    out.px.assign(size_t(width) * size_t(height) * 4, 0);
    if (fmt == BC1 || fmt == BC2 || fmt == BC3) {
        int bw = (width + 3) / 4, bh = (height + 3) / 4;
        size_t bsize = fmt == BC1 ? 8 : 16;
        size_t need = size_t(bw) * size_t(bh) * bsize;
        if (d.size() - off < need) return fail(err, "DDS data is cut off");
        const uint8_t* p = d.data() + off;
        uint8_t tex[16][4];
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx, p += bsize) {
                if (fmt == BC1) {
                    decode_colour_block(p, tex, false);
                } else {
                    decode_colour_block(p + 8, tex, true);
                    if (fmt == BC2) decode_bc2_alpha(p, tex);
                    else decode_bc3_alpha(p, tex);
                }
                for (int i = 0; i < 16; ++i) {
                    int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                    if (x < width && y < height) std::memcpy(out.at(x, y), tex[i], 4);
                }
            }
        }
        return true;
    }
    size_t bpp = (fmt == MASKED) ? bits / 8 : 4;
    size_t need = size_t(width) * size_t(height) * bpp;
    if (d.size() - off < need) return fail(err, "DDS data is cut off");
    const uint8_t* p = d.data() + off;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x, p += bpp) {
            uint8_t* o = out.at(x, y);
            if (fmt == RGBA32) {
                std::memcpy(o, p, 4);
            } else if (fmt == BGRA32) {
                o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = p[3];
            } else {
                uint32_t v = bpp == 4 ? rd32(p) : (uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16);
                o[0] = mask_value(v, rm);
                o[1] = mask_value(v, gm);
                o[2] = mask_value(v, bm);
                o[3] = (pf_flags & 0x1) ? mask_value(v, am) : 255;
            }
        }
    }
    return true;
}

bool decode_image(const std::vector<uint8_t>& bytes, Rgba& out, std::string* err) {
    out = Rgba();
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "DDS ", 4) == 0) return decode_dds(bytes, out, err);
    if (bytes.empty() || bytes.size() > 0x7FFFFFFF) return fail(err, "empty or too large file");
    int w = 0, h = 0, n = 0;
    if (!stbi_info_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n)) {
        if (err) *err = std::string("unknown picture format (") + (stbi_failure_reason() ? stbi_failure_reason() : "?") + ")";
        return false;
    }
    if (w <= 0 || h <= 0 || w > kMaxImageSide || h > kMaxImageSide) return fail(err, "picture is too large (max 8192 x 8192)");
    stbi_uc* data = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n, 4);
    if (!data) {
        if (err) *err = std::string("cannot read the picture (") + (stbi_failure_reason() ? stbi_failure_reason() : "?") + ")";
        return false;
    }
    out.w = w;
    out.h = h;
    out.px.assign(data, data + size_t(w) * size_t(h) * 4);
    stbi_image_free(data);
    return true;
}

bool load_image_file(const std::filesystem::path& p, Rgba& out, std::string* err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return fail(err, "cannot open the file");
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n <= 0 || n > 256ll * 1024 * 1024) return fail(err, "file is empty or larger than 256 MB");
    f.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(n));
    if (!f.read(reinterpret_cast<char*>(bytes.data()), n)) return fail(err, "cannot read the file");
    return decode_image(bytes, out, err);
}

// ---------------------------------------------------------------- resampling
// Premultiplied bilinear sample at (u, v) in source pixel coordinates (pixel centres at .5); outside = transparent
static void sample(const Rgba& s, float u, float v, float o[4]) {
    o[0] = o[1] = o[2] = o[3] = 0.0f;
    float x = u - 0.5f, y = v - 0.5f;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    float fx = x - float(x0), fy = y - float(y0);
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            int xx = x0 + i, yy = y0 + j;
            float wgt = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);
            if (wgt <= 0.0f) continue;
            if (xx < 0 || yy < 0 || xx >= s.w || yy >= s.h) {
                // edge pixels repeat up to half a pixel outside (no dark fringe), beyond that transparent
                if (u < 0.0f || v < 0.0f || u > float(s.w) || v > float(s.h)) continue;
                xx = std::clamp(xx, 0, s.w - 1);
                yy = std::clamp(yy, 0, s.h - 1);
            }
            const uint8_t* p = s.at(xx, yy);
            float a = p[3] / 255.0f;
            o[0] += wgt * p[0] * a;
            o[1] += wgt * p[1] * a;
            o[2] += wgt * p[2] * a;
            o[3] += wgt * a;
        }
    }
}

// Halve the picture (2x2 box, premultiplied) until it is at most twice `target` on the longer side
static Rgba shrink_for(const Rgba& src, float scale) {
    Rgba cur = src;
    while (scale < 0.5f && cur.w >= 2 && cur.h >= 2) {
        Rgba nx;
        nx.w = cur.w / 2;
        nx.h = cur.h / 2;
        nx.px.resize(size_t(nx.w) * size_t(nx.h) * 4);
        for (int y = 0; y < nx.h; ++y) {
            for (int x = 0; x < nx.w; ++x) {
                float acc[4] = {0, 0, 0, 0};
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        const uint8_t* p = cur.at(2 * x + i, 2 * y + j);
                        float a = p[3] / 255.0f;
                        acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; acc[3] += a;
                    }
                uint8_t* o = nx.at(x, y);
                if (acc[3] > 0.0f) {
                    for (int k = 0; k < 3; ++k) o[k] = uint8_t(std::clamp(acc[k] / acc[3] + 0.5f, 0.0f, 255.0f));
                } else {
                    o[0] = o[1] = o[2] = 0;
                }
                o[3] = uint8_t(std::clamp(acc[3] / 4.0f * 255.0f + 0.5f, 0.0f, 255.0f));
            }
        }
        cur = std::move(nx);
        scale *= 2.0f;
    }
    return cur;
}

Rgba frame_image(const Rgba& src0, int size, const Framing& f) {
    Rgba out;
    if (size <= 0 || src0.empty()) return out;
    out.w = out.h = size;
    out.px.assign(size_t(size) * size_t(size) * 4, 0);
    float zoom = std::clamp(f.zoom, 0.05f, 20.0f);
    // pixels of the source per target pixel
    float base = float(std::min(src0.w, src0.h)) / float(size);
    float step = base / zoom;
    Rgba src = shrink_for(src0, 1.0f / step);
    float k = float(src.w) / float(src0.w);  // shrink factor
    float cx = src0.w * 0.5f - f.dx * size * step, cy = src0.h * 0.5f - f.dy * size * step;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float u = cx + (x + 0.5f - size * 0.5f) * step, v = cy + (y + 0.5f - size * 0.5f) * step;
            float o[4];
            sample(src, u * k, v * k, o);
            uint8_t* p = out.at(x, y);
            if (o[3] > 0.0f) {
                for (int c = 0; c < 3; ++c) p[c] = uint8_t(std::clamp(o[c] / o[3] + 0.5f, 0.0f, 255.0f));
            }
            p[3] = uint8_t(std::clamp(o[3] * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
    return out;
}

Rgba fit_image(const Rgba& src, int max_side) {
    if (src.empty() || max_side <= 0) return Rgba();
    int longer = std::max(src.w, src.h);
    if (longer <= max_side) return src;
    float s = float(max_side) / float(longer);
    int w = std::max(1, int(std::lround(src.w * s))), h = std::max(1, int(std::lround(src.h * s)));
    Rgba small = shrink_for(src, s);
    Rgba out;
    out.w = w;
    out.h = h;
    out.px.assign(size_t(w) * size_t(h) * 4, 0);
    float sx = float(small.w) / float(w), sy = float(small.h) / float(h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float o[4];
            sample(small, (x + 0.5f) * sx, (y + 0.5f) * sy, o);
            uint8_t* p = out.at(x, y);
            if (o[3] > 0.0f)
                for (int c = 0; c < 3; ++c) p[c] = uint8_t(std::clamp(o[c] / o[3] + 0.5f, 0.0f, 255.0f));
            p[3] = uint8_t(std::clamp(o[3] * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
    return out;
}

// ---------------------------------------------------------------- background
size_t remove_plain_background(Rgba& img, int tolerance) {
    if (img.empty() || img.w < 3 || img.h < 3) return 0;
    tolerance = std::clamp(tolerance, 1, 255);
    // background colour = per-channel median of the opaque border pixels
    std::vector<int> ch[3];
    auto add = [&](int x, int y) {
        const uint8_t* p = img.at(x, y);
        if (p[3] < 128) return;
        for (int c = 0; c < 3; ++c) ch[c].push_back(p[c]);
    };
    for (int x = 0; x < img.w; ++x) { add(x, 0); add(x, img.h - 1); }
    for (int y = 1; y < img.h - 1; ++y) { add(0, y); add(img.w - 1, y); }
    if (ch[0].empty()) return 0;
    int bg[3];
    for (int c = 0; c < 3; ++c) {
        std::nth_element(ch[c].begin(), ch[c].begin() + long(ch[c].size() / 2), ch[c].end());
        bg[c] = ch[c][ch[c].size() / 2];
    }
    auto dist = [&](const uint8_t* p) {
        int d = 0;
        for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(int(p[c]) - bg[c]));
        return d;
    };
    std::vector<uint8_t> seen(size_t(img.w) * size_t(img.h), 0);
    std::vector<int> stack;
    auto push = [&](int x, int y) {
        size_t i = size_t(y) * size_t(img.w) + size_t(x);
        if (seen[i]) return;
        const uint8_t* p = img.at(x, y);
        if (p[3] == 0 || dist(p) <= tolerance) {
            seen[i] = 1;
            stack.push_back(int(i));
        }
    };
    for (int x = 0; x < img.w; ++x) { push(x, 0); push(x, img.h - 1); }
    for (int y = 0; y < img.h; ++y) { push(0, y); push(img.w - 1, y); }
    while (!stack.empty()) {
        int i = stack.back();
        stack.pop_back();
        int x = i % img.w, y = i / img.w;
        if (x > 0) push(x - 1, y);
        if (x + 1 < img.w) push(x + 1, y);
        if (y > 0) push(x, y - 1);
        if (y + 1 < img.h) push(x, y + 1);
    }
    size_t n = 0;
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            size_t i = size_t(y) * size_t(img.w) + size_t(x);
            uint8_t* p = img.at(x, y);
            if (seen[i]) {
                if (p[3] != 0) ++n;
                p[3] = 0;
                continue;
            }
            // soft edge: pixels next to removed ones fade with their closeness to the background colour
            bool edge = (x > 0 && seen[i - 1]) || (x + 1 < img.w && seen[i + 1]) || (y > 0 && seen[i - size_t(img.w)]) ||
                        (y + 1 < img.h && seen[i + size_t(img.w)]);
            if (edge) {
                int d = dist(p);
                float keep = std::clamp(float(d - tolerance) / float(tolerance), 0.35f, 1.0f);
                p[3] = uint8_t(p[3] * keep);
            }
        }
    }
    return n;
}

// ---------------------------------------------------------------- DDS encode
static void put32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[off + size_t(i)] = uint8_t(x >> (8 * i));
}

std::vector<uint8_t> encode_dds_dxt5(const Rgba& img) {
    std::vector<uint8_t> out;
    if (img.empty() || img.w % 4 || img.h % 4) return out;
    int bw = img.w / 4, bh = img.h / 4;
    out.assign(128 + size_t(bw) * size_t(bh) * 16, 0);
    std::memcpy(out.data(), "DDS ", 4);
    // Same header as FC 27's own minifaces: flags CAPS|HEIGHT|WIDTH|PIXELFORMAT, one mip, DXT5, caps TEXTURE
    put32(out, 4, 124);
    put32(out, 8, 0x1007);
    put32(out, 12, uint32_t(img.h));
    put32(out, 16, uint32_t(img.w));
    put32(out, 28, 1);
    put32(out, 76, 32);
    put32(out, 80, 0x4);
    std::memcpy(out.data() + 84, "DXT5", 4);
    put32(out, 108, 0x1000);
    uint8_t* dst = out.data() + 128;
    uint8_t block[64];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            for (int j = 0; j < 4; ++j) std::memcpy(block + j * 16, img.at(bx * 4, by * 4 + j), 16);
            // fully transparent texels: their colour is never seen, so they get the block's average visible colour and
            // do not pull the colour endpoints away from the visible texels
            int sum[3] = {0, 0, 0}, vis = 0;
            for (int i = 0; i < 16; ++i)
                if (block[i * 4 + 3] != 0) {
                    for (int c = 0; c < 3; ++c) sum[c] += block[i * 4 + c];
                    ++vis;
                }
            for (int i = 0; i < 16; ++i)
                if (block[i * 4 + 3] == 0)
                    for (int c = 0; c < 3; ++c) block[i * 4 + c] = uint8_t(vis ? sum[c] / vis : 0);
            stb_compress_dxt_block(dst, block, 1, STB_DXT_HIGHQUAL);
            dst += 16;
        }
    }
    return out;
}

}  // namespace turbo
