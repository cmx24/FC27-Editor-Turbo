// FC 27 LE Turbo GUI - kits from FIFA Kit Creator (fifakitcreator.com) zip downloads: read, check, convert, stage.
//
// One kit zip (one kit of one club) holds 24 PNG textures named after the game's kit textures, the minikit, the kit's
// database settings and the decal placement:
//   jersey / shorts / socks  _color _coeff _normal _taa .png           (12, required)
//   brand_jersey / brand_shorts / crest / crest_shorts _color _normal _coeff .png   (12, optional: some zips have a
//                                                                       blank.png instead; the game's own decals stay)
//   minikit.png (the 2D kit the menus show), settings.json (teamkits fields), hotspots.hotspot (JSON decal placement)
// FC 27 keeps a kit's 3D textures as Frostbite assets under
//   content/character/kit/kit_<floor(teamid / 500) * 500>/<slug>_<teamid>/<kind>_<type>_0/{jersey,shorts,socks}/...
// Their sizes and formats (fc27_kit_spec) were read from the game's own texture headers (Arsenal home, contentsb.toc,
// 2026-10-12; turbo_output\KITS_IMPORT_RESEARCH.txt): colour BC1 sRGB, coeff BC1, normal BC5, taa BC7, decal colour BC3
// sRGB, decal normal BC5, decal coeff BC3, every one with its full mip chain. The game reads none of these files today:
// phase 1 stages them (DX10 DDS, same size / format / mips) in turbo_output\kits\<teamid>\<slot>\ with manifest.json for
// the in-match texture swap of phase 2. Platform independent (the Windows GUI and the Linux tests).
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "image.h"

namespace turbo {
namespace kits {

// ---------------------------------------------------------------- zip (STORED and DEFLATE entries)
struct ZipEntry {
    std::string name;      // as stored ('/' separators)
    uint16_t method = 0;   // 0 stored, 8 deflate
    uint32_t crc = 0;
    uint64_t size = 0;     // uncompressed
    uint64_t csize = 0;    // compressed
    uint64_t data = 0;     // offset of the compressed bytes in the file
};
// CRC-32 (zip, PNG): crc32(p, n) of the whole buffer, or continued from an earlier value
uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0);

class Zip {
public:
    static constexpr uint64_t kMaxFile = uint64_t(512) << 20;   // bigger zips are refused (memory)
    static constexpr uint64_t kMaxEntry = uint64_t(96) << 20;   // one file inside, uncompressed
    static constexpr size_t kMaxEntries = 512;
    // Reads the central directory. Refused, with the reason: not a zip, cut off, ZIP64, encrypted entries, names with
    // ".." / absolute / backslash-drive paths, more than kMaxEntries entries, an entry over kMaxEntry.
    bool open(std::vector<uint8_t> bytes, std::string* err = nullptr);
    bool load_file(const std::filesystem::path& p, std::string* err = nullptr);
    const std::vector<ZipEntry>& entries() const { return entries_; }
    // The entry named `name` (letter case ignored, at the top of the zip or in one folder: "kit/jersey_color.png")
    const ZipEntry* find(const std::string& name) const;
    // Uncompressed bytes of an entry; false when the method is unknown, the data is cut off or its CRC is wrong
    bool read(const ZipEntry& e, std::vector<uint8_t>& out, std::string* err = nullptr) const;
    uint64_t file_size() const { return bytes_.size(); }
    uint32_t file_crc() const { return crc32(bytes_.data(), bytes_.size()); }

private:
    std::vector<uint8_t> bytes_;
    std::vector<ZipEntry> entries_;
};

// ---------------------------------------------------------------- kit slots
enum class Slot { Unknown = -1, Home = 0, Away = 1, Goalkeeper = 2, Third = 3, Fourth = 4 };
// teamkits.teamkittypetechid of the slot (the kit folders and minikits of FC 27 use the same number: home_0_0,
// away_1_0, goalie_home_2_0, third_3_0, fourth_4_0, j<type>_<teamid>_0.dds); -1 for Unknown
int slot_type(Slot s);
Slot slot_of_type(int64_t type);           // Unknown for other types
const char* slot_folder(Slot s);           // "home", "away", "goalkeeper", "third", "fourth" (turbo_output\kits\<id>\<slot>)
const char* slot_label(Slot s);            // "Home", "Away", "Goalkeeper", "Third", "Fourth"
const char* slot_kind(Slot s);             // the game's kit folder word: home, away, goalie_home, third, fourth
// The slot a zip is for, from its name: h / a / g / t / f, home / away / gk / goalkeeper / goalie / keeper / third /
// fourth, also as the last word of the name ("ado_den_haag_away.zip"); for a name that says nothing ("kit.zip"), the
// folder it is in ("ita\ASR\kit\gk\kit.zip"). Unknown otherwise ("3.zip").
Slot slot_from_path(const std::filesystem::path& zip);

// ---------------------------------------------------------------- textures
enum class Tex { BC1, BC1_SRGB, BC3, BC3_SRGB, BC5, BC7 };
uint32_t dxgi_of(Tex t);        // DXGI_FORMAT number (71, 72, 77, 78, 83, 98)
const char* tex_name(Tex t);    // "BC1_UNORM_SRGB", ...
int full_mips(int w, int h);    // levels down to 1 x 1 (2048 x 2048: 12)

struct TexSpec {
    std::string role;   // the zip's file name without ".png" ("jersey_color", "crest_shorts_normal")
    std::string part;   // the game's part folder: jersey, shorts, socks
    int w = 0, h = 0;   // the game's texture size
    Tex fmt = Tex::BC1;
    bool decal = false; // brand_* / crest_*: optional in a zip
};
// The 24 textures of an FC 27 kit, as the game ships them (see the header comment)
const std::vector<TexSpec>& fc27_kit_spec();
// "content/character/kit/kit_0/arsenal_1/home_0_0/jersey/jersey_1_0_0_color" (slug "" -> "<slug>")
std::string game_asset_path(const TexSpec& t, int64_t teamid, Slot s, const std::string& slug);
// "data/ui/imgAssets/kits/j<type>_<teamid>_0.dds": the minikit the menus show (a legacy file)
std::string minikit_path(int64_t teamid, Slot s);

// Pixels scaled down by an integer factor (box filter on straight alpha); w / h must divide the source size
Rgba downscale_box(const Rgba& src, int w, int h);
// DX10 DDS of `img` in format `fmt` with `mips` levels (each a straight-alpha box half of the one before; levels
// smaller than a block repeat their edge texels). Empty with err when the size is not a power-of-two chain or mips is
// out of range.
std::vector<uint8_t> encode_kit_dds(const Rgba& img, Tex fmt, int mips, std::string* err = nullptr);
struct KitDds {
    uint32_t dxgi = 0;
    int w = 0, h = 0, mips = 0;
    size_t data = 0;  // offset of level 0
};
// Reads a DX10 DDS written by encode_kit_dds (or the same layout) and decodes level 0 (BC1, BC3, BC5 as red / green,
// BC7 mode 6 blocks; other BC7 modes decode as magenta). False with the reason for anything else or a cut-off file.
bool decode_kit_dds(const std::vector<uint8_t>& bytes, Rgba& top, KitDds* info = nullptr, std::string* err = nullptr);
// One BC7 block (16 bytes) in mode 6 (one subset, RGBA 7.7.7.7 endpoints with p-bits, 4-bit indices) from 16 RGBA
// texels (row by row), and back (false for a block of another mode)
void encode_bc7_mode6_block(const uint8_t px[16][4], uint8_t out[16]);
bool decode_bc7_mode6_block(const uint8_t b[16], uint8_t out[16][4]);

// ---------------------------------------------------------------- settings.json
struct KitSettings {
    // The teamkits fields and values it sets, a colour as its three channels (teamcolorprimr, ...g, ...b)
    std::vector<std::pair<std::string, int64_t>> fields;
    // "#rrggbb" colours by prefix ("teamcolorprim") and the other keys as given, in file order (preview)
    std::vector<std::pair<std::string, std::string>> shown;
    std::vector<std::string> ignored;  // keys Turbo does not write (named in the preview)
};
// The colour prefixes (kit_colour_prefixes: teamcolorprim ... shortsnumbercolorter) as "#rrggbb"; kit_setting_fields
// (core/reapply.h): teamcolorprimpercent /
// teamcolorsecpercent / teamcolortertpercent 0..100 (number or text); jerseycollargeometrytype, jerseynamelayouttype,
// shortstyle as whole numbers 0..255. Any other key is ignored (listed). false with the reason for a value that is not
// what its key needs, a file that is not a JSON object, or a file with none of the keys.
bool parse_settings(const std::string& text, KitSettings& out, std::string* err = nullptr);
// "#234f8e" -> 0x23, 0x4f, 0x8e
bool parse_hex_colour(const std::string& s, uint8_t rgb[3]);

// ---------------------------------------------------------------- one kit zip, checked
struct KitPackage {
    std::filesystem::path source;
    Slot slot = Slot::Unknown;          // from the file name (slot_from_path); the user may pick another
    bool ok = false;                    // every check passed: it can be applied
    std::string error;                  // the first problem (what the panel shows), "" when ok
    std::vector<std::string> problems;  // every problem
    std::vector<std::string> notes;     // not a problem: decals left to the game, pictures scaled down, unknown files
    KitSettings settings;
    Rgba minikit;                       // decoded minikit.png
    Rgba thumb;                         // jersey_color.png at most 256 pixels per side (preview)
    bool decals = false;                // the zip carries the 12 decal textures
    bool hotspots = false;              // hotspots.hotspot is there and reads as JSON
    std::vector<std::string> roles;     // textures found (spec roles)
    uint64_t size = 0;                  // of the zip file
    uint32_t crc = 0;                   // CRC-32 of the zip file (the manifest names the source exactly)
};
// Reads one zip and checks everything: the 12 main textures, minikit.png and settings.json present; every PNG decodes;
// each texture is its spec size or 2 / 4 / 8 times it (then scaled down); the decals all present or all absent;
// settings.json valid. Never throws.
KitPackage load_kit_zip(const std::filesystem::path& zip, const std::vector<TexSpec>& spec = fc27_kit_spec());
// The kit zips of a team folder: *.zip directly inside, else one folder down (<folder>\home\kit.zip); sorted, at most 8
std::vector<std::filesystem::path> team_folder_zips(const std::filesystem::path& folder);

// ---------------------------------------------------------------- staging (for phase 2)
// <Live Editor>\turbo_output\kits
std::filesystem::path kits_dir(const std::filesystem::path& le_root);
// <Live Editor>\turbo_output\kits\<teamid>\<slot>
std::filesystem::path slot_dir(const std::filesystem::path& le_root, int64_t teamid, Slot s);

struct StageInfo {
    int64_t teamid = 0;
    std::string team;   // club name (manifest only)
    Slot slot = Slot::Unknown;
    std::string slug;   // the club's kit folder slug from the asset catalog ("" = not known: phase 2 looks it up)
    std::string when;   // "2026-10-12 14:00"
};
struct StageResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> files;  // written, file names
    uint64_t bytes = 0;
    std::filesystem::path dir;
};
// Converts every texture of the zip to its spec (size, format, full mips) and writes them with manifest.json (and a copy
// of hotspots.hotspot) into `dir`. All or nothing: the files are made in <dir>\.staging first and moved in only when
// every one was written; the previous staged files of that slot are replaced, other files in `dir` (import.json) stay.
// `cancel` (optional) is polled between textures.
StageResult stage_kit_textures(const std::filesystem::path& zip, const std::filesystem::path& dir, const StageInfo& info,
                               const std::vector<TexSpec>& spec = fc27_kit_spec(), const std::atomic<bool>* cancel = nullptr);
// FNV-1a 64 of bytes as 16 hex digits (the manifest's checksum)
std::string fnv1a64_hex(const std::vector<uint8_t>& b);

}  // namespace kits
}  // namespace turbo
