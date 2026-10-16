// FC 27 LE Turbo GUI - the game's own settings file (Settings > Game Settings: match, player and the User / CPU sliders), read and patched
// OFFLINE with the game closed. Design: turbo_output\assessment_20261014\SETTINGS_PRESETS.md (2.8 v1 = the file route, applies at the next
// launch). Pure code: no Windows call, no ImGui (the tool src/settings_apply/main.cpp and the view ui/ui_game_settings.cpp share it).
//
// THE FILE %LOCALAPPDATA%\EA SPORTS FC 27\settings\Settings<yyyyMMddHHmmssfff> (772790 bytes, "Personal Settings 1"; the game names a new
// file at every save and keeps one):
//   +0   "FBCHUNKS" 01 00, u32 meta size 0x454, u32 payload size 0xBC650
//   +18  meta (the slot name at +0, the profile GUID at +0x40, the rest changes at every save)
//   +0x466 payload: "SaveType_Settin(gs)", u32 CRC32 at payload+16 = crc32(payload[48..]) (zlib's), u32 0xBC620 (= the CRC'd length) at
//         payload+0x30, the constant words 0x38943 0x537 0x2EE77 0xF3 at payload+0x74 (the same in every backup since 10-03; the words
//         around them change between saves, so they are not checked).
//   The sliders are a little-endian u32 array; the slider map (data: turbo\settings\slider_map_6AC07E31.json) gives its base (payload
//   offset of index 0), the candidate REGIONS (the 36-value run, block A, block B) and the ENTRIES (a slider's id -> index).
//
// RULES (all of them refuse, nothing is written):
//   * only a file named Settings + 17 digits, and only when it is the ONLY file of the folder whose name starts with "Settings";
//     CmMgrC* (careers), Squads* and ProfileOptions* are never opened for writing (is_protected_name);
//   * size, header, tag, CRC'd length or the constant words differ, or the stored CRC does not match;
//   * FC 27 is running (the host's game_running callback);
//   * a write outside the map's regions, or onto a slot whose value now is not 0..100 (the layout moved);
//   * a preset writes only VERIFIED entries (verified: true, an index): in 2.8 every entry is verified: false until the marker launch
//     proves it, so a preset writes nothing yet. The marker-test mode (write_markers / install_markers) is the one writer of unverified
//     slots, and only into a copy or with the same backup and Undo.
// THE WRITE (patch, never replace): only the given u32s change, the CRC is computed again, a temp file (~turbo_settings_write.tmp, never
// named Settings*) is written and read back, the original is backed up to turbo_output\settings_backup\<stamp>\ with md5s
// (backup.json), then the temp replaces the original under the SAME name. Undo puts the backup back when the file is still Turbo's (name
// and md5 as written), else only with force.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace turbo {
namespace gsettings {

// ------------------------------------------------------------------------------------------------ the file format
constexpr size_t kHeaderSize = 18, kMetaSize = 0x454, kPayloadSize = 0xBC650;
constexpr size_t kPayloadOff = kHeaderSize + kMetaSize;  // 0x466
constexpr size_t kFileSize = kPayloadOff + kPayloadSize;  // 772790
constexpr size_t kCrcAt = 16, kCrcFrom = 48;              // payload offsets
constexpr size_t kCrcLenAt = 0x30, kConstAt = 0x74;
constexpr uint8_t kCustomPrefix[5] = {1, 0, 0, 0, 1};  // the bytes right before the Custom block
constexpr uint32_t kConstWords[4] = {0x38943, 0x537, 0x2EE77, 0xF3};
inline const char* layout_name() { return "fbchunks-454-bc650"; }

uint32_t crc32(const uint8_t* p, size_t n);                  // zlib's crc32
std::string md5_hex(const uint8_t* p, size_t n);             // 32 lower-case hex digits
inline std::string md5_hex(const std::vector<uint8_t>& b) { return md5_hex(b.data(), b.size()); }

struct Check {
    bool ok = false;
    std::string why;           // the first failing check ("" = ok)
    std::string layout;        // layout_name() when the structure matches (also when only the CRC fails)
    std::string profile_name;  // "Personal Settings 1"
    uint32_t crc_stored = 0, crc_calc = 0;
};
Check validate(const std::vector<uint8_t>& b);  // read-only
void fix_crc(std::vector<uint8_t>& b);          // stores crc32(payload[48..]) at payload+16 (b must be kFileSize long)
// The u32 at payload offset `off` (false outside the payload)
bool get_u32(const std::vector<uint8_t>& b, size_t off, uint32_t& v);
bool set_u32(std::vector<uint8_t>& b, size_t off, uint32_t v);

// ------------------------------------------------------------------------------------------------ the folder
bool is_settings_name(const std::string& n);   // "Settings" + 17 digits (the profile settings save)
bool is_protected_name(const std::string& n);  // CmMgrC*, Squads* (also _1_Squads*), ProfileOptions*, Assets*: never written
struct Folder {
    bool ok = false;
    std::string why;
    std::vector<std::string> settings;  // every file whose name starts with "Settings" (any case)
    std::filesystem::path file;         // the one Settings file (ok only)
};
Folder scan(const std::filesystem::path& dir);
bool read_bytes(const std::filesystem::path& p, std::vector<uint8_t>& out, size_t max_bytes = 4u << 20);

// ------------------------------------------------------------------------------------------------ the slider map (data)
inline const char* map_dir() { return "settings"; }                       // <Live Editor>\turbo\settings
inline const char* map_file() { return "slider_map_6AC07E31.json"; }
struct Region {
    std::string id, label, candidate;  // "block_a", "Block A (44 values)", "CPU sliders?"
    int first = 0, count = 0;          // array indices
};
struct Entry {
    std::string id, label, group, note;  // group: match / player / user / cpu
    int index = -1;                      // u32 array index (the service-id view; NOT what the Custom screen shows): -1 = not mapped yet
    int custom_index = -1;               // byte index inside the 125-byte Custom block (GameplayTypeCustomData); -1 = not mapped
    int min = 0, max = 100;
    bool verified = false;               // proven in game (the marker launch); only then a preset writes it
    std::string proof;                   // where the proof is ("" = none)
    bool writable() const { return verified && (custom_index >= 0 || index >= 0); }
};
struct SliderMap {
    std::string build, layout, note;
    size_t base = 0;  // payload offset of index 0
    // The Custom block (2.10, TEST_31 G-S1): custom_count u8 values at payload offset custom_base (file 0xB8DDC), element k = setting id
    // 0x8C + k [A]; the five bytes before it must be 01 00 00 00 01. 0 = the map has none.
    size_t custom_base = 0;
    int custom_count = 0;
    size_t custom_offset_of(int index) const { return custom_base + size_t(index); }
    std::vector<Region> regions;
    std::vector<Entry> entries;
    const Entry* entry(const std::string& id) const;
    const Region* region_of(int index) const;
    size_t offset_of(int index) const { return base + 4u * size_t(index); }
    // Refused: not JSON, another layout, a base / region outside the payload, an id twice or not [a-z0-9._-], an unknown group, an index
    // outside every region, verified without an index, min/max outside 0..100
    static bool parse(const std::string& text, SliderMap& out, std::string* err);
};
bool load_map(const std::filesystem::path& file, SliderMap& out, std::string* err);
const char* group_label(const std::string& group);  // "Match", "Player", "Sliders: User", "Sliders: CPU"
const std::vector<std::string>& groups();           // match, player, user, cpu

// ------------------------------------------------------------------------------------------------ what the file holds now (read-only)
struct Snapshot {
    bool ok = false;
    std::string why;
    std::filesystem::path file;
    std::string name, md5, layout, profile;
    std::map<std::string, int> values;               // entry id -> value (mapped entries)
    std::vector<std::pair<int, uint32_t>> slots;     // every region index -> value (the candidate view)
    std::vector<std::pair<int, uint32_t>> custom_slots;  // every Custom block byte index -> value (empty when the map has no block)
};
Snapshot read_current(const std::filesystem::path& settings_dir, const SliderMap& m);

// ------------------------------------------------------------------------------------------------ presets (turbo_output\settings_presets)
inline const char* presets_dir() { return "settings_presets"; }
inline const char* pending_file() { return "settings_pending.json"; }  // turbo_output (outside the presets folder)
inline const char* backup_dir() { return "settings_backup"; }          // turbo_output
struct Preset {
    std::string name, note, created;
    std::string src_file, src_md5, src_layout;  // the file it was saved from
    std::map<std::string, int> values;          // slider id -> 0..100 (never a file offset)
    std::filesystem::path file;                 // where it was read from
    std::string to_json() const;
    static bool parse(const std::string& text, Preset& out, std::string* err);
};
std::string safe_name(const std::string& s);
std::string preset_file_name(const Preset& p);  // "<safe name>.json"
std::vector<Preset> load_presets(const std::filesystem::path& dir, std::vector<std::string>* errors);  // by name
std::string unique_preset_name(const std::vector<Preset>& have, const std::string& wanted);          // "Name", "Name 2" ...
bool same_name(const std::string& a, const std::string& b);                                           // case-insensitive
bool write_preset_file(const std::filesystem::path& dir, Preset& p, std::string* err);                // sets p.file
bool delete_preset_file(const Preset& p, std::string* err);
bool write_text(const std::filesystem::path& p, const std::string& text, std::string* err);           // .tmp + rename
// A preset of the VERIFIED entries' current values (nothing when no entry is verified: the caller says so)
Preset preset_from_snapshot(const std::string& name, const Snapshot& s, const SliderMap& m, const std::string& created);
// What staging a preset would write: the verified entries (clamped to min..max), the others listed as skipped
struct Plan {
    std::map<std::string, int> values;
    std::vector<std::string> skipped, clamped;
};
Plan plan_preset(const Preset& p, const SliderMap& m);

// The staged preset (the view writes it; the tool applies it with the game closed, then renames it settings_pending.applied_<stamp>.json)
struct Pending {
    std::string preset, staged, src_md5;
    std::map<std::string, int> values;  // verified ids only
    std::string to_json() const;
    static bool parse(const std::string& text, Pending& out, std::string* err);
};
// Writes turbo_output\settings_pending.json from the preset (the verified entries only); refused when none of its values is writable
bool stage_pending(const std::filesystem::path& out_dir, const Preset& p, const SliderMap& m, const std::string& stamp, std::string* msg);

// ------------------------------------------------------------------------------------------------ the writes (the tool, game closed)
struct Env {
    std::filesystem::path settings_dir, out_dir;      // the game's settings folder, turbo_output
    std::function<bool()> game_running;               // FC27.exe runs (null = not known: refused)
    // Replaces `to` with `from` (MoveFileExW REPLACE_EXISTING | WRITE_THROUGH in the tool); null = remove + rename
    std::function<bool(const std::filesystem::path& from, const std::filesystem::path& to, std::string* err)> replace;
    std::string stamp;                                // "20261014-221500-123" (the backup folder's name)
    bool dry_run = false;
};
struct Outcome {
    bool ok = false;
    std::string message;
    std::vector<std::string> lines;
    std::filesystem::path backup;  // the backup folder (a write only)
    std::string md5_before, md5_after;
};
struct Write {
    int index = 0;      // u32 array index, or the byte index inside the Custom block when `custom`
    uint32_t value = 0;
    std::string what;  // "cpu.sprint_speed" / "block_a[3]"
    bool custom = false;  // true: one byte of the Custom block
};
// The guarded patch (the rules above). `label` goes into backup.json
Outcome patch(const Env& env, const SliderMap& m, const std::vector<Write>& writes, const std::string& label);
// Applies turbo_output\settings_pending.json (verified entries only) and renames it settings_pending.applied_<stamp>.json
Outcome apply_pending(const Env& env, const SliderMap& m);
// Puts the newest backup back (refused when the file is no longer the one Turbo wrote, unless force)
Outcome undo_last(const Env& env, bool force);

// ------------------------------------------------------------------------------------------------ the marker test (one launch)
// set "blocks": block A = 1..44, block B = 56..99 (disjoint, never 50), the run untouched; set "run": the 36-value run = 1..36, the
// blocks untouched. write_markers writes a COPY (same file name) + markers.json + markers.txt (value -> region, slot, index) into out_dir;
// the settings folder is only read. install_markers patches the current file with the legend's writes (same backup and Undo as patch),
// only when it is still the file the copy was made from (name and md5)
std::vector<Write> marker_writes(const SliderMap& m, const std::string& set, std::string* err);
Outcome write_markers(const std::filesystem::path& settings_dir, const std::filesystem::path& out_dir, const SliderMap& m, const std::string& set);
Outcome install_markers(const Env& env, const SliderMap& m, const std::filesystem::path& marker_dir);

}  // namespace gsettings
}  // namespace turbo
