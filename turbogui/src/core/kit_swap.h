// FC 27 LE Turbo GUI - 3D kits on the pitch (Turbo 2.6 "kits3d", phase 2 of the FIFA Kit Creator import): swap the game's kit
// texture data for the textures phase 1 staged, while the game decompresses them.
//
// How FC 27 loads a kit texture (read from the game's own files, 2026-10-14; tools turbo\tools\kits3d\kit_chunks.py):
//   * every kit texture is a Frostbite texture resource (RES type 6bde20ba, a 184-byte Oodle-compressed header with format id,
//     width, height, mip count, the chunk GUID and the mip sizes) plus ONE chunk: the tight mip chain, largest mip first, each
//     mip block-compressed (BC1 / BC3 / BC5 / BC7) exactly as the DDS files Turbo stages; no padding, no pitch.
//   * a chunk sits in a cas file as a chain of Oodle blocks (8-byte header: raw size BE, type 0x19, compressed size) of at most
//     256 KB raw each. The game imports OodleLZ_Decompress (oo2core_9_win64.dll) for them.
//   * the bundle (toc) lists the manifest, the EBX, the RES and the chunks as consecutive locations; the manifest names every
//     resource and chunk GUID. Kit assets live in content/character/kit/kit_<bucket>/<slug>_<teamid>/<kind>_<type>_<var>/<part>/...
//     (kind home / away / goalie_home / third / fourth; a second set loc2_<kind>_<40..44> exists for many clubs).
// This module resolves a club's kit textures to their chunks and blocks (no game file is written), checks every staged texture
// against the game's own header, and builds an immutable Table keyed by (compressed size, raw size, first bytes, FNV-1a 64 of the
// compressed block). The host's detour on OodleLZ_Decompress asks the Table after each decompress and, in swap mode, overwrites the
// output with the staged bytes of that block. Nothing here knows about the process: the Windows hook is win/kit_swap_win.cpp.
// Platform independent (g++ tests: tests/catalog/run_kitswap.sh).
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace turbo {
namespace kitswap {

uint64_t fnv1a64(const uint8_t* p, size_t n, uint64_t h = 0xCBF29CE484222325ULL);

// Oodle decompress one block: true when exactly dstlen bytes came out
using OodleFn = std::function<bool(const uint8_t* src, size_t srclen, uint8_t* dst, size_t dstlen)>;

// ---------------------------------------------------------------- container readers (pure functions on bytes)
// Data\layout.toc -> install package names by persistentIndex (8 hex digits)
bool parse_layout(const std::vector<uint8_t>& layout_toc, std::map<std::string, std::string>& packages, std::string* err = nullptr);

struct Loc {
    std::string cas;  // 16 hex digits: layer(4) package(8) file number(4)
    uint64_t off = 0;
    uint32_t size = 0;
    uint8_t flag = 0;
};
// The bundles of a toc: per bundle its locations (manifest, EBX..., RES..., chunks...); an empty vector = not inline
bool parse_toc_bundles(const std::vector<uint8_t>& toc, std::vector<std::vector<Loc>>& bundles, std::string* err = nullptr);

struct Block {
    uint32_t dsize = 0;       // raw size
    uint32_t csize = 0;       // compressed size (payload only)
    uint8_t ctype = 0;        // 0 stored, 0x11 / 0x15 / 0x19 Oodle
    uint64_t payload = 0;     // offset of the payload in the chunk bytes
};
// The block stream of a chunk; false when it does not end exactly at the end of the bytes
bool block_headers(const std::vector<uint8_t>& chunk, std::vector<Block>& out);
bool read_blocks(const std::vector<uint8_t>& chunk, const OodleFn& oodle, std::vector<uint8_t>& out, std::string* err = nullptr);

struct ManifestRes {
    std::string name;
    uint32_t size = 0;
    uint32_t type = 0;
};
struct ManifestChunk {
    std::string guid;  // 32 hex digits
    uint32_t logical = 0;
};
struct Manifest {
    std::vector<std::string> ebx;
    std::vector<ManifestRes> res;
    std::vector<ManifestChunk> chunks;
};
bool parse_manifest(const std::vector<uint8_t>& blob, Manifest& out, std::string* err = nullptr);

struct TexHeader {
    uint32_t fmt_id = 0;
    int w = 0, h = 0, mips = 0;
    std::string guid;
    std::vector<uint32_t> mip_sizes;
};
bool parse_texture_header(const std::vector<uint8_t>& h, TexHeader& out, std::string* err = nullptr);
constexpr uint32_t kTextureResType = 0x6BDE20BA;
// Frostbite render format id -> DXGI format (54 BC1, 55 BC1 sRGB, 60 BC3, 61 BC3 sRGB, 63 BC5, 66 BC7); 0 = not one of those
uint32_t dxgi_of_format(uint32_t fmt_id);
// Bytes of the tight mip chain of a w x h texture in a BC format (dxgi 71/72 BC1, 77/78 BC3, 83 BC5, 98/99 BC7)
std::vector<uint32_t> bc_mip_sizes(uint32_t dxgi, int w, int h, int mips);

// ---------------------------------------------------------------- the game's files (read only)
class GameFiles {
public:
    explicit GameFiles(std::filesystem::path game_root) : root_(std::move(game_root)) {}
    bool init(std::string* err = nullptr);  // loads Data\layout.toc
    std::filesystem::path root() const { return root_; }
    std::filesystem::path cas_path(const std::string& cas) const;  // empty when the package is unknown
    bool read(const std::string& cas, uint64_t off, uint64_t size, std::vector<uint8_t>& out);
    bool read_loc(const Loc& l, std::vector<uint8_t>& out) { return read(l.cas, l.off, l.size, out); }
    // The tocs under Data\Win32 and Patch\Win32 (relative names "Data/Win32/contentsb.toc")
    std::vector<std::string> list_tocs() const;
    bool read_file(const std::string& rel, std::vector<uint8_t>& out);

private:
    std::filesystem::path root_;
    std::map<std::string, std::string> packages_;
    std::map<std::string, std::unique_ptr<std::ifstream>> files_;
};

// ---------------------------------------------------------------- kit slots and names
// 0 home, 1 away, 2 goalkeeper, 3 third, 4 fourth for "home_0_0", "goalie_home_2_0", "loc2_away_41_0"...; -1 for anything else
int kind_slot(const std::string& kind);
// "jersey_95_0_0_color" -> "jersey_color", "brand_shorts_95_40_0_normal" -> "brand_shorts_normal"; "" when the name does not fit
std::string role_of(const std::string& leaf, int team);
struct KitName {
    std::string slug, kind, part, leaf;
    int team = -1;
    bool ok = false;
};
// "content/character/kit/kit_0/leicester_city_95/home_0_0/jersey/jersey_95_0_0_color"
KitName parse_kit_name(const std::string& path);

// ---------------------------------------------------------------- the index of every kit bundle (one pass over the tocs)
struct KitBundle {
    int toc = 0;          // index in KitIndex::tocs
    uint32_t bundle = 0;
    int team = -1;
    std::string kind;     // the first kit folder the bundle names
    uint32_t slot_mask = 0;  // bit s (0..4) = the bundle holds a kit folder of slot s (home, away, goalkeeper, third, fourth; loc2 variants too)
};
struct KitUser {
    int team = -1;
    int slot = -1;
    std::string kind;
};
struct KitIndex {
    std::vector<std::string> tocs;
    std::vector<KitBundle> bundles;
    // physical key "cas:offset:size" of every kit chunk -> who uses it
    std::unordered_map<std::string, std::vector<KitUser>> users;
    // every kit folder the game has for a club ("home_0_0", "loc2_home_40_0", "away_1_0", "goalie_home_2_0", "third_3_0"...): which slots exist
    std::map<int, std::set<std::string>> kinds;
    int bundles_seen = 0;
    int unreadable = 0;
    double seconds = 0;
    bool from_cache = false;
};
struct Progress {
    std::atomic<bool> cancel{false};
    std::atomic<int> done{0};
    std::atomic<int> total{0};
};
bool scan_kits(GameFiles& files, const OodleFn& oodle, KitIndex& out, Progress* progress = nullptr, std::string* err = nullptr);
std::string phys_key(const Loc& l);
// The index on disk (turbo_output\kits\kit_index.cache): valid while every toc (and layout.toc) has the same size and write time. A miss
// (no file, other game build, damaged file) is never an error: the caller scans again.
std::string index_signature(const GameFiles& files);
bool save_index_cache(const GameFiles& files, const KitIndex& idx, const std::filesystem::path& file);
bool load_index_cache(const GameFiles& files, const std::filesystem::path& file, KitIndex& out);

// ---------------------------------------------------------------- staged textures (phase 1: turbo_output\kits\<team>\<slot>)
struct StagedTexture {
    std::string role;  // "jersey_color"
    std::filesystem::path file;
    uint32_t dxgi = 0;
    int w = 0, h = 0, mips = 0;
    uint64_t bytes = 0;  // file size, header included
};
struct StagedKit {
    int team = -1;
    int slot = -1;
    std::vector<StagedTexture> textures;
    std::filesystem::path hotspots;  // the zip's hotspots.hotspot (JSON), empty when the kit has none
};
bool load_staged(const std::filesystem::path& dir, StagedKit& out, std::string* err = nullptr);
constexpr size_t kDdsHeader = 148;  // DX10 DDS: the mip data starts here

// ---------------------------------------------------------------- the block table
struct Entry {
    uint32_t csize = 0, dsize = 0;
    uint64_t head = 0;      // first 8 compressed bytes
    uint64_t fnv = 0;       // FNV-1a 64 of the whole compressed block
    const uint8_t* data = nullptr;  // replacement raw bytes (dsize of them)
    uint32_t item = 0;      // index of the plan item (hit counters)
    uint32_t alias_off = 0, alias_n = 0;  // other items whose block is byte-identical (the same replacement): their counters too
};
class Table {
public:
    // The entry whose compressed block `src` is (exact sizes, first 8 bytes, then the whole block's hash), else nullptr.
    // No allocation, no lock: called from the decompress detour on every game thread.
    const Entry* match(const uint8_t* src, uint64_t srclen, uint64_t dstlen) const;
    size_t size() const { return entries_.size(); }
    uint32_t items() const { return n_items_; }
    uint64_t hits(uint32_t item) const { return item < n_items_ ? hits_[item].load() : 0; }
    void count_hit(const Entry& e) const {
        bump(e.item);
        for (uint32_t i = 0; i < e.alias_n; ++i) bump(alias_pool_[e.alias_off + i]);
    }
    // Wall-clock time (ms since the Unix epoch) of the first / last block recognised for an item, 0 = never. Tells which game screen
    // (match load, kit select, hub) loaded which kit data.
    uint64_t first_ms(uint32_t item) const { return item < n_items_ ? first_ms_[item].load() : 0; }
    uint64_t last_ms(uint32_t item) const { return item < n_items_ ? last_ms_[item].load() : 0; }
    uint64_t bytes() const { return bytes_; }
    bool swap_item(uint32_t item) const { return item < item_swap_.size() && item_swap_[item]; }
    bool may_match(uint64_t srclen, uint64_t dstlen) const;  // cheap size pre-check
    uint32_t min_csize() const { return min_csize_; }
    uint32_t max_csize() const { return max_csize_; }

private:
    friend class TableBuilder;
    void bump(uint32_t i) const {
        if (i >= n_items_) return;
        hits_[i].fetch_add(1);
        const uint64_t t = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        uint64_t zero = 0;
        first_ms_[i].compare_exchange_strong(zero, t);
        last_ms_[i].store(t);
    }
    std::vector<Entry> entries_;
    std::vector<uint32_t> slots_;  // open addressing, 0 = empty, else index + 1
    uint32_t mask_ = 0;
    std::vector<std::shared_ptr<std::vector<uint8_t>>> storage_;
    std::unique_ptr<std::atomic<uint64_t>[]> hits_, first_ms_, last_ms_;
    std::vector<uint32_t> alias_pool_;
    std::vector<uint8_t> item_swap_;  // 1 = overwrite, 0 = observe only
    uint32_t n_items_ = 0;
    uint64_t bytes_ = 0;
    uint32_t min_csize_ = 0xFFFFFFFFu, max_csize_ = 0;
};

// ---------------------------------------------------------------- the plan for one (club, slot)
// 2.13: why a texture (and with it maybe a whole kit folder) stays the game's
enum class LeftBy {
    None,
    OtherClubs,    // another club's kit uses the same data: Options::share_other_clubs serves it (that club's kit then shows it too)
    ThisClub,      // this club's other kit slot uses the same chunk: Options::share_within_club serves it
    ThisClubData,  // a byte-identical block in another chunk of this club (no option serves a patched fragment of this club's kits)
    Clash,         // another active kit wants the same game data with other bytes
    Other,         // not in the zip, another size or format, uniform fill, unreadable...
};
// 2.13: never half a kit. The pictures that decide whether a kit folder is shown as the zip's: when one of them is not served, for any
// reason, the whole folder (jersey, shorts, socks, brand, hotspots) stays the game's. The coefficient / normal / taa maps are detail
// maps: one that cannot be served stays the game's and is counted (FolderVerdict::details_left). The game loads one kit folder at a
// time (home_0_0, or its alternative version loc2_home_40_0), so each folder is judged on its own.
bool kit_role(const std::string& role);
// jersey / shorts / socks coefficient, normal and taa maps (counted, never decisive)
bool detail_role(const std::string& role);
struct FolderVerdict {
    int team = -1, slot = -1;
    std::string kind;             // the game's kit folder: goalie_home_2_0, loc2_goalie_home_42_0, ...
    bool swapped = true;          // false: the whole folder stays the game's
    LeftBy why = LeftBy::None;    // left: the kind of reason (OtherClubs: Options::share_other_clubs would serve it)
    std::string role;             // left: the texture that decided it ("jersey_color")
    std::string reason;           // left: that texture's reason (plain words, for the log)
    std::vector<KitUser> shared;  // left: the kits that share the deciding texture; swapped: the other kits that change with it
    int details_left = 0;         // swapped: coefficient / normal / taa maps that stay the game's
};
struct PlanItem {
    std::string res;      // game resource name
    std::string kind;     // game kit folder (home_0_0, loc2_home_40_0, ...)
    std::string role;     // zip role, "" when the texture has none (weave arrays)
    bool swap = false;
    std::string reason;   // why not, or a note
    bool probe = false;   // 2.7: watch only (a texture of another kit folder of the club, counted when the game loads it, never replaced)
    int blocks = 0;
    uint64_t bytes = 0;   // raw bytes swapped
    uint32_t table_item = 0xFFFFFFFFu;  // the item number in the Table (hit counters), when swap
    LeftBy left_by = LeftBy::None;      // 2.13: why it is not served
    std::vector<KitUser> shared;        // 2.13: the other kits on the same data (same chunk or a byte-identical block): served, they change too
};
struct Options {
    bool include_decals = true;   // brand_* / crest_* textures of the zip
    // 2.7: keep the game's own club badge: the zip's crest_* / crest_shorts_* pictures are not served (the zip's decal is usually blank, which
    // removes the game's dynamic badge). brand_* (sponsor) pictures follow include_decals. Default on: a blank zip decal never removes the badge.
    bool keep_game_crest = true;
    // 2.7: a texture the game shares between this club's kits (home / away / goalkeeper / third use one chunk) is served too and then shows in
    // every kit that shares it. Off: such a texture stays the game's. Textures shared with ANOTHER club: share_other_clubs.
    bool share_within_club = false;
    // 2.13: a texture whose data another CLUB's kit uses too (the same chunk, or a byte-identical compressed block in that club's chunk: EA
    // reuses goalkeeper templates, e.g. Sevilla's and Aston Villa's goalie_home_2_0 jersey) is served as well, and that club's kit then shows
    // it too (the plan names every such kit: "also changes team 2 goalie_home_2_0"). Off: it stays the game's, and so does its whole kit
    // folder when it is a kit_role picture.
    bool share_other_clubs = false;
    // 2.7: also register the game's own blocks of the club's other kit folders (watch only, never replaced) so one match shows which kit
    // folders (home_0_0, loc2_home_40_0, away_1_0, ...) it really loads
    bool watch_club = true;
    uint32_t min_csize = 1024;    // blocks with a smaller compressed size are uniform fill: never matched
    bool census = true;           // the block census over every kit chunk (header hops over the whole game's kit data)
    bool apply_hotspots = true;   // also put the zip's name / number / sleeve-badge placement (hotspots.hotspot) into the game's hotspot asset
};
// The smallest compressed block accepted for a hotspot asset (a whole small EBX in one block; its raw bytes carry the club's own path, so
// no other asset can have the same bytes: all 4,851 kit hotspot assets of the install have distinct raw bytes, 494..979 compressed bytes)
constexpr uint32_t kMinEbxCsize = 256;

// ---------------------------------------------------------------- hotspots (name / number / sleeve-badge placement)
// FC 27 keeps a kit's decal placement in one small EBX asset per kit: content/character/kit/kit_<b>/<slug>/<kind>/hotspots_<team>_<type>_<var>
// (plus a *_hotspot_brt wrapper that only references it). Layout (read from the install, 4,851 assets): RIFF "EBX EBXD" container, the
// instance starts at 0x90 (a 25 in the u32 at 0x8C), 25 entries of 0x30 bytes: 4 floats (the rectangle X, Y, Z, W in jersey UV space,
// the same four numbers as the zip's Bounds), 16 bytes of self-relative pointers, a float rotation at +0x20, padding; behind them the
// string table starting with the asset's own path. Entry order: jersey 0 team, 1 numberback, 2 numberfront, 3 name, 4 namearccenter,
// 5 namearctangent, 6 armband, 7 brand, 8 weave_tiles, 9 effectParams, 10 decalEffectParams; shorts 11 team, 12 number, 13 brand,
// 14 weave_tiles, 15 effectParams, 16 decalEffectParams; socks 17..19; competition 20 LeftSleeveLong, 21 LeftSleeveShort,
// 22 RightSleeveLong, 23 RightSleeveShort, 24 Center. [V] the layout and the order of entries 1..5 and 20..23 against the Leicester asset
// and the zip's values; [A] shorts team / number (11 / 12) by the value ranges.
struct HotspotRect {
    float x = 0, y = 0, z = 0, w = 0, rot = 0;
};
using HotspotMap = std::map<std::string, HotspotRect>;  // "jersey/numberback" (lower case group/name)
// The zip's hotspots.hotspot JSON
bool parse_hotspots(const std::vector<uint8_t>& json_bytes, HotspotMap& out, std::string* err = nullptr);
// Patches the decompressed hotspot asset `ebx` (same size out) of the game resource `res_name` with the zip's rectangles. Rules: only the
// entries the zip names and the table above knows; a zip rectangle that is a point (X==Z and Y==W) never replaces the game's rectangle
// (the zip's numberfront is a point); an entry the game has switched off (all four floats negative, e.g. Center -1) stays off; an all-negative rectangle (the zip's "not used") is applied only to team / brand entries and only
// when `decals` is true (the zip bakes the badge into the jersey colour); non-finite or out-of-range values are refused. Returns false
// with `why` when the asset is not the expected layout (nothing is changed then); true with `changes` (one text per patched entry,
// may be empty = nothing differed).
bool patch_hotspot_ebx(std::vector<uint8_t>& ebx, const std::string& res_name, const HotspotMap& zip, bool decals, std::vector<std::string>* changes, std::string* why);
struct Plan {
    int team = -1, slot = -1;
    bool ok = false;
    std::string error;
    std::vector<PlanItem> items;
    int swapped = 0, skipped = 0, probes = 0;
    std::vector<std::string> notes;
    std::vector<FolderVerdict> folders;  // 2.13: every kit folder of the slot, the main ones first (loc2_* after)
};
// 2.13: the whole kit folder `kind` of `plan` stays the game's (by = the deciding item): every item of the folder that was served is not
// any more (probes stay). Returns the item numbers in the table those items had (the caller leaves them out of the table).
std::vector<uint32_t> leave_folder(Plan& plan, const std::string& kind, LeftBy why, const std::string& role, const std::string& reason,
                                   const std::vector<KitUser>& shared);
// "team 2 goalie_home_2_0, team 50 goalie_home_2_0"
std::string users_text(const std::vector<KitUser>& users);
// Builds the plan and appends the accepted blocks to `b` (see TableBuilder). Never throws.
class TableBuilder {
public:
    TableBuilder();
    ~TableBuilder();
    // Adds a plan's blocks; plan items are numbered by the builder (item index = running count over all plans)
    // data == nullptr: a watch-only item (its blocks carry no replacement and never conflict with a real one)
    uint32_t add_item(std::shared_ptr<std::vector<uint8_t>> data, bool swap);
    bool add_block(uint32_t item, uint32_t csize, uint32_t dsize, uint64_t head, uint64_t fnv, const uint8_t* data, std::string* conflict);
    // 2.13: the item's blocks stay out of every later finish() (a kit folder left whole after a clash); finish() can run again
    void exclude_item(uint32_t item);
    // dropped_items: the items that lost every block (the same compressed block wanted with other bytes: every item of such a group);
    // clash_with (2.13): for each dropped item, the other items that wanted the same block
    std::shared_ptr<Table> finish(std::vector<std::string>* dropped, std::set<uint32_t>* dropped_items = nullptr,
                                  std::map<uint32_t, std::set<uint32_t>>* clash_with = nullptr);
    uint32_t items() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};
Plan build_plan(GameFiles& files, const OodleFn& oodle, const KitIndex& index, const StagedKit& staged, const Options& opt, bool swap,
                TableBuilder& table, Progress* progress = nullptr);

// ---------------------------------------------------------------- the runtime (state, worker, hot path)
enum class Mode { Off, Observe, Swap };
const char* mode_name(Mode m);
struct SlotState {
    int team = -1;
    int slot = -1;
    Mode mode = Mode::Observe;
    // per kit (saved in swap_state.json, version 2; a version 1 file gets the defaults)
    bool decals = true;       // serve the zip's brand_* / crest_* pictures at all
    bool keep_crest = true;   // ... but not the crest_* ones (the game's club badge stays)
    bool share_club = false;  // serve textures the game shares with this club's other kits
    bool share_other = false; // 2.13: serve textures the game shares with other clubs' kits ("share_other_clubs"; a file without it: off)
};
struct ItemStatus {
    int team = -1, slot = -1;
    std::string res, kind, role, reason;
    bool swap = false;
    bool probe = false;   // watch only (another kit folder of the club)
    int copies = 1;       // identical left-to-the-game entries (same resource at several places) are listed once
    int blocks = 0;
    uint64_t hits = 0;
    uint64_t first_ms = 0, last_ms = 0;  // wall clock of the first / last recognised block (0 = never)
    uint32_t table_item = 0xFFFFFFFFu;
    LeftBy left_by = LeftBy::None;  // 2.13
    std::vector<KitUser> shared;    // 2.13: the other kits on the same data
};
// One kit folder of a club as the match used it: how many of its game textures (blocks) were decompressed so far
struct KindSeen {
    int team = -1;
    std::string kind;        // "home_0_0", "loc2_home_40_0", "away_1_0", or "home_0_0+away_1_0" for one chunk both use
    int textures = 0;        // textures registered (replaced + watch only)
    int textures_seen = 0;   // of them at least one block was decompressed
    uint64_t blocks_seen = 0;
};
struct Status {
    std::string state;       // "off", "building", "ready", "error"
    std::string message;
    bool killed = false;     // kill switch file present
    bool hook = false;       // the host installed the detour
    uint64_t calls = 0;      // decompress calls the detour saw
    uint64_t candidates = 0; // calls whose sizes the table could match (full hash computed)
    uint64_t hits = 0;       // blocks recognised
    uint64_t swapped = 0;    // blocks overwritten
    uint64_t unserved = 0;   // recognised, but the original returned something other than the raw size (not overwritten)
    uint64_t with_callback = 0;  // recognised, and the game passed an Oodle callback (it may read the output during the decode)
    uint64_t other_phase = 0;    // recognised, and threadPhase was not 3 (all phases)
    size_t blocks = 0;       // blocks in the table
    uint64_t bytes = 0;
    std::vector<SlotState> slots;
    std::vector<ItemStatus> items;
    std::vector<KindSeen> kinds;  // per club and kit folder, from the items
    std::vector<FolderVerdict> folders;  // 2.13: per active kit, every kit folder: shown whole as the zip's, or left whole to the game
    double index_seconds = 0;
    bool index_cached = false;    // the kit index came from turbo_output\kits\kit_index.cache (no scan of the game's tocs)
    int scan_done = 0, scan_total = 0;  // a running scan of the game's tocs (bundles)
    bool scanning = false;
};
class Runtime {
public:
    // le_root: the Live Editor folder (state and logs under turbo_output); game_root: the install; oodle: the game's own decompressor
    Runtime(std::filesystem::path le_root, std::filesystem::path game_root, OodleFn oodle);
    ~Runtime();
    void set_hook_installed(bool on, const std::string& why_not);
    // The host sets it: without turbo_output\kits3d_on.txt nothing is read, scanned or hooked (a kit left on in an old session stays inert)
    bool require_opt_in = false;
    // Runs at the start of every worker thread (the host lowers its priority so a scan does not compete with the game)
    std::function<void()> worker_init;
    // The host's installer of the decompress hook: called ONCE, the first time a kit is activated (or restored at start-up), so a
    // player who never uses the feature has no hook at all. false + reason = not installed (activation refused). Unset = no hook
    // needed (tests).
    std::function<bool(std::string* why)> ensure_hook;
    // Loads turbo_output\kits\swap_state.json and starts building the table for the slots in it (async)
    void start();
    // Activates a staged kit (turbo_output\kits\<team>\<slot>) in `mode`; async build. false + reason: nothing staged, unknown slot
    bool activate(int team, int slot, Mode mode, std::string* err);
    void deactivate(int team, int slot);
    // Per-kit choices (decals, keep the game's badge, share within the club, share with other clubs): saved, and the table is built again
    bool set_slot_options(int team, int slot, bool decals, bool keep_crest, bool share_club, bool share_other);
    bool slot_options(int team, int slot, bool* decals, bool* keep_crest, bool* share_club, bool* share_other = nullptr) const;
    // The kit folders the game has for a club (empty until the kit index exists)
    std::set<std::string> kinds_of(int team) const;
    // Builds the table again (the staged textures of an active kit changed)
    void rebuild() { rebuild_async(); }
    bool is_active(int team, int slot, Mode* mode = nullptr) const;
    Status status() const;
    // The kill switch: raised, it stops a running scan / build and the hot path; released, a build that was skipped because of it runs
    void set_killed(bool k);
    bool killed() const { return killed_.load(); }
    // The hot path (any thread, no lock, no allocation). 0 = not a kit block, 1 = recognised, 2 = recognised and overwritten.
    // Call it after the original decompress returned dstlen.
    int on_block(const uint8_t* src, uint64_t srclen, uint8_t* dst, uint64_t dstlen);
    void count_call() { calls_.fetch_add(1, std::memory_order_relaxed); }
    // The detour's whole job after the original returned `ret` (the number of raw bytes it wrote) for a block of `srclen` compressed
    // bytes: flags 1 = fpCallback was set, 2 = decBufBase, 4 = decoderMemory; phase = threadPhase. Records the first 256 calls whose
    // compressed size the table knows (what the game really passes: the log shows them) and does what on_block does when
    // ret == dstlen. Returns on_block's code (0 when nothing was served).
    int on_decompressed(const uint8_t* src, uint64_t srclen, uint8_t* dst, uint64_t dstlen, int64_t ret, uint32_t flags, int phase);
    // Writes turbo_output\kit_swap_log.txt when the counters changed (at most every 5 s unless force)
    void flush_log(bool force = false);
    // Waits for the worker (tests)
    void wait_idle() const;
    // The explicit opt-in: turbo_output\kits3d_on.txt. Without it the feature is invisible in the UI and the Oodle hook is never installed (default = no new hook).
    bool opted_in() const;
    std::filesystem::path state_path() const;
    std::filesystem::path log_path() const;
    std::filesystem::path index_cache_path() const;
    std::filesystem::path staged_dir(int team, int slot) const;
    // Tests: the table as published
    const Table* table() const { return table_.load(); }
    Options options;  // set before activating; the UI changes it through set_options (the worker copies it under the lock)
    void set_options(const Options& o) {
        std::lock_guard<std::mutex> lk(m_);
        options = o;
    }
    Options get_options() const {
        std::lock_guard<std::mutex> lk(m_);
        return options;
    }

private:
    bool hook_ready(std::string* err);
    void save_state_locked();
    void rebuild_async();
    void worker();
    void publish(std::shared_ptr<Table> t);
    std::filesystem::path le_, game_;
    OodleFn oodle_;
    mutable std::mutex m_;
    std::vector<SlotState> slots_;
    std::vector<ItemStatus> items_;
    std::vector<FolderVerdict> folders_;
    std::string state_ = "off", message_;
    bool hook_ = false;
    std::string hook_why_;
    double index_seconds_ = 0;
    std::shared_ptr<KitIndex> index_;
    std::thread worker_;
    std::atomic<int> generation_{0};
    bool running_ = false;
    std::atomic<bool> stop_{false};
    Progress progress_;
    std::atomic<bool> killed_{false};
    bool build_skipped_ = false;  // a build did not run because of the kill switch / missing opt-in (m_)
    bool index_cached_ = false;   // (m_)
    std::atomic<const Table*> table_{nullptr};
    std::atomic<bool> swap_any_{false};
    std::vector<std::shared_ptr<Table>> keep_;                 // published tables (m_)
    std::vector<std::pair<std::shared_ptr<Table>, double>> retired_;  // replaced tables, freed after a grace period (m_)
    std::atomic<uint64_t> calls_{0}, candidates_{0}, hits_{0}, swapped_{0}, unserved_{0}, with_cb_{0}, other_phase_{0};
    // first 256 candidate calls (compressed size known to the table), written lock-free by the detour, read by flush_log
    struct Diag {
        std::atomic<uint64_t> a{0}, b{0}, c{0};
    };
    static constexpr uint32_t kDiag = 256;
    std::unique_ptr<Diag[]> diag_;
    std::atomic<uint32_t> diag_n_{0};
    uint64_t logged_hits_ = 0, logged_calls_ = 0;
    double last_log_ = -100;
};

}  // namespace kitswap
}  // namespace turbo
