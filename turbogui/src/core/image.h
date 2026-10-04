// FC 27 LE Turbo GUI - pictures: decode (PNG, JPG, BMP, TGA, DDS), frame, clean the background, encode DDS (DXT5).
// Platform independent (used by the Windows GUI and by the Linux tests).
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace turbo {

// 8-bit RGBA, rows top to bottom
struct Rgba {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool empty() const { return w <= 0 || h <= 0 || px.size() != size_t(w) * size_t(h) * 4; }
    uint8_t* at(int x, int y) { return &px[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
    const uint8_t* at(int x, int y) const { return &px[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
};

// Largest picture Turbo decodes (pixels per side); bigger files are refused (memory)
constexpr int kMaxImageSide = 8192;

// Decode a file's bytes: DDS (DXT1 / DXT3 / DXT5, DX10 BC1-BC3, uncompressed 32/24-bit) or anything stb_image reads
// (PNG, JPG, BMP, TGA). Returns false with a reason.
bool decode_image(const std::vector<uint8_t>& bytes, Rgba& out, std::string* err = nullptr);
bool decode_dds(const std::vector<uint8_t>& bytes, Rgba& out, std::string* err = nullptr);
bool load_image_file(const std::filesystem::path& p, Rgba& out, std::string* err = nullptr);

// How a source picture is placed in a square target:
//   zoom 1 = the picture's shorter side fills the target (centre crop); > 1 zooms in; < 1 shows more (transparent around)
//   dx, dy = shift of the picture's centre in target widths (-1 .. 1; positive = picture moves right / down)
struct Framing {
    float zoom = 1.0f;
    float dx = 0.0f, dy = 0.0f;
};
// Render `src` into a size x size picture with bilinear filtering (box-filtered first when shrinking a lot)
Rgba frame_image(const Rgba& src, int size, const Framing& f);
// Scale to fit inside max_side x max_side, keeping the aspect ratio (no upscaling)
Rgba fit_image(const Rgba& src, int max_side);

// Make a plain background transparent: the colour of the picture's border (median of the edge pixels) is removed by a
// flood fill from the edges, with a soft edge. `tolerance` 0..255 (colour distance). Returns pixels made transparent.
size_t remove_plain_background(Rgba& img, int tolerance = 40);

// DDS file with one DXT5 surface (no mipmaps, like FC 27's own minifaces). Width and height must be multiples of 4.
std::vector<uint8_t> encode_dds_dxt5(const Rgba& img);

// The layout of an existing DDS file (a game crest, for example), so a replacement can be written the same way:
// same size, same pixel format, same number of mipmaps, same header style (legacy or DX10).
struct DdsFormat {
    enum class Pixel { BGRA8, RGBA8, BGRX8, BGR8, DXT1, DXT3, DXT5 };
    Pixel pixel = Pixel::BGRA8;
    int w = 0, h = 0;
    int mips = 1;
    bool dx10 = false;
    std::vector<uint8_t> header;  // the original file's header (128 or 148 bytes); empty = build a plain one
    const char* name() const;     // "BGRA8", "DXT5", ...
    bool compressed() const { return pixel == Pixel::DXT1 || pixel == Pixel::DXT3 || pixel == Pixel::DXT5; }
};
// Read the format of a DDS file. false with a reason for formats Turbo cannot write back.
bool parse_dds_format(const std::vector<uint8_t>& bytes, DdsFormat& out, std::string* err = nullptr);
// Write `img` (which must be f.w x f.h) as a DDS file in that format, with f.mips mip levels (box filtered).
// Sizes that are not multiples of 4 are allowed for the compressed formats (edge texels repeat, as DirectX does).
std::vector<uint8_t> encode_dds(const Rgba& img, const DdsFormat& f, std::string* err = nullptr);
// Half-size picture (2x2 box filter, premultiplied); odd sizes drop the last row / column, 1 stays 1
Rgba halve_image(const Rgba& src);

}  // namespace turbo
