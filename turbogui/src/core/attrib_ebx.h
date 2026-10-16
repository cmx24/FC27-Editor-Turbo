// FC 27 LE Turbo GUI - attribulator EBX patch at load (Turbo 2.10 track T1 "attrib-ebx", docs: turbo_dev\research_20261015\PLAN_tactics_settings.md
// section 1 (b) and gate G-T1b).
//
// What it does: the game's gameplay attribulator groups (fifa/attribulator/gameplay/groups/<family>/<group>_runtime, one small EBX asset
// per group) are read from the install, decompressed, patched in memory with the values of turbo_output\attrib_preset.json (scalars,
// array elements, FloatCurve points) and registered, by their compressed block, in a kitswap::Table. The host's detour on
// OodleLZ_Decompress (win/attrib_ebx_win.cpp) asks the Runtime after each decompress and, in patch mode, overwrites the output with the
// patched bytes: the game instantiates the attribulator from bytes that already carry the values, the way a .fbmod does. In observe mode
// the blocks are only recognised and the first time each asset passes the hook is logged ("attrib block seen <time> <asset>").
//
// Layout (read from the install, build 6AC07E31-2145C000, 2026-10-15; every attrib EBX is ONE Oodle block, turbo_output\attrib_block_kinds.md):
//   RIFF <u32 size> "EBX\0" "EBXD" <u32 dsize> ... payload P = 0x20 ... "EFIX" <u32 size> {partition guid, type guids[], type sigs[],
//   exported, instance offsets dc[] (P-relative), pointer slots[] (P-relative), resource refs[], imports[] (32 bytes), import offsets[],
//   typeinfo offsets[], array / boxed / string offsets}. An instance starts with its type index (u16, into the type sig list); its
//   fields sit at P + dc + <field offset>, the SAME offsets as the in-memory group (docs/re/attrib_inventory_6AC07E31.json: lineWidthModifier
//   0x28 = 1.0, LineLengthModifierForBalanced 0x30 = 0.84 in ..._attackinglinelengthwidth_runtime) [V]. The asset's own path is the
//   CString at instance +0x18 (Name) [V]. Pointers, arrays and strings are 32-bit signed offsets relative to their own 8-byte slot; an
//   array's count is the u32 before its data [V]. A FloatCurve field is a pointer to another instance of the file whose Points array
//   slot is at +0x18; a point is 28 bytes: CurveType i32 +0, X f32 +4, two tangent floats, Y f32 +16, two more floats [V, against
//   gp_positioning_zonal_defense_playoffsides and the inventory's curve Y values].
// Pure (no Windows): native tests in tests/native/test_attrib_ebx.h.
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/kit_swap.h"

namespace turbo {
namespace attrib {

// ---------------------------------------------------------------- the EBX container
struct EbxView {
    size_t payload = 0x20;      // P: instance, pointer and dc offsets are relative to it
    size_t data_end = 0;        // end of the EBXD chunk's data (20 + dsize)
    std::vector<uint32_t> type_sigs;
    std::vector<uint32_t> dc;   // instance offsets (P-relative)
    std::vector<uint32_t> ptrs; // pointer / array / string slots (P-relative)
    uint32_t exported = 0;
};
bool parse_ebx(const uint8_t* d, size_t n, EbxView& out, std::string* why);
// Absolute offsets of the instances whose type sig is `sig`
std::vector<size_t> instances_of(const uint8_t* d, size_t n, const EbxView& v, uint32_t sig);
// The instance's Name (CString at +0x18), "" when it has none or it does not fit
std::string instance_name(const uint8_t* d, size_t n, const EbxView& v, size_t inst);

// FloatCurve and FloatCurvePoint (see the layout above)
constexpr uint32_t kCurvePointsSlot = 0x18, kPointStride = 28, kPointX = 4, kPointY = 16;
constexpr uint32_t kNameSlot = 0x18;

// ---------------------------------------------------------------- one value to change
enum class Kind { F32, I32, U32, Bool, ArrF32, ArrI32, ArrU32, Curve };
bool kind_of(const std::string& s, Kind& k);  // "f32" "i32" "u32" "bool" "af32" "ai32" "au32" "curve"
const char* kind_name(Kind k);
// A path names the field and, for arrays and curves, the element: "lineWidthModifier", "ACTION_IntensityToSpeed[3]",
// "StopDistanceFn.Points[2].Y" (or .X). The field name is checked against nothing but the preset's own entry (the offset is the address).
struct PathSpec {
    std::string field;
    int index = -1;      // array element / curve point
    char axis = 0;       // 'X' / 'Y' for a curve point
};
bool parse_path(const std::string& path, Kind kind, PathSpec& out, std::string* why);

struct Patch {
    std::string knob;      // "shape.length_in" (for the log only)
    std::string asset;     // "fifa/attribulator/gameplay/groups/gp_positioningformation/gp_positioningformation_attackinglinelengthwidth_runtime"
    uint32_t type_sig = 0; // the schema's type signature (inventory "sig"): the instance must have it, and only one may
    std::string path;      // see PathSpec
    uint32_t offset = 0;   // field offset in the instance (inventory field offset == knobs file offset)
    Kind kind = Kind::F32;
    double value = 0;
    bool has_expect = false;
    double expect = 0;     // the install's value: anything else = another layout, nothing is written
};
// Applies every patch to the decompressed EBX `raw` (same size out). All or nothing: when one patch does not fit (no / several
// instances of the type, the asset's Name differs from `asset`, the slot is not a pointer slot, an index out of range, the current
// value is not `expect`, a non-finite value) nothing is changed and `why` says it. `changes`: one text per value ("path 0.84 -> 0.4").
// No clamping: the gate values (length 0.4, width 0.5) lie below the slider bounds on purpose.
bool patch_ebx(std::vector<uint8_t>& raw, const std::string& asset, const std::vector<Patch>& patches, std::vector<std::string>* changes, std::string* why);

// ---------------------------------------------------------------- the preset (turbo_output\attrib_preset.json)
enum class Mode { Observe, Patch };
struct Preset {
    std::string build;     // "6AC07E31-2145C000": another running build = nothing is done
    Mode mode = Mode::Patch;
    std::vector<Patch> patches;
    std::vector<std::string> assets() const;  // distinct, in file order
};
bool parse_preset(const std::string& json_text, Preset& out, std::string* why);

// ---------------------------------------------------------------- finding the assets in the install
struct AssetLoc {
    std::string toc;      // "Patch/Win32/fc/fcgame/fcgame.toc"
    kitswap::Loc loc;     // the EBX's location in the cas
};
// The tocs searched, Patch layer first
std::vector<std::string> attrib_tocs();
// Every copy of every named EBX in the tocs (Patch and Data). `found[i]` lists the copies of names[i].
bool find_assets(kitswap::GameFiles& files, const kitswap::OodleFn& oodle, const std::vector<std::string>& names, std::vector<std::vector<AssetLoc>>& found,
                 std::string* err);
// A small cache of find_assets (turbo_output\attrib_ebx_locs.json), valid while the tocs keep their size and write time
std::string tocs_signature(const kitswap::GameFiles& files);
bool save_locs(const std::filesystem::path& file, const std::string& sig, const std::vector<std::string>& names, const std::vector<std::vector<AssetLoc>>& found);
bool load_locs(const std::filesystem::path& file, const std::string& sig, const std::vector<std::string>& names, std::vector<std::vector<AssetLoc>>& found);

// ---------------------------------------------------------------- the table
struct ItemInfo {
    std::string asset;
    std::string toc;
    bool patch = false;           // the block is overwritten (else watch only)
    std::string what;             // the changes, or why the copy is watch only
    uint32_t csize = 0, dsize = 0;
    uint32_t table_item = 0xFFFFFFFFu;
};
// One copy of an asset: `stream` = its bytes in the cas (block headers included). Exactly one Oodle block is required. Patch mode: the
// decompressed bytes are patched and registered as a replacement; when the patch does not fit, the copy is registered watch only
// (observe line still written) and `what` says why. Observe mode: watch only. false = the copy cannot be registered at all.
bool add_asset(kitswap::TableBuilder& tb, const std::vector<uint8_t>& stream, const kitswap::OodleFn& oodle, const std::string& asset, const std::string& toc,
               const std::vector<Patch>& patches, Mode mode, ItemInfo& out, std::string* why);

// ---------------------------------------------------------------- the runtime (hot path + log)
class Runtime {
public:
    Runtime();
    // The table and what each item is; the table's items must be numbered like `items` (add_asset's table_item)
    void publish(std::shared_ptr<kitswap::Table> table, std::vector<ItemInfo> items);
    bool ready() const { return table_.load(std::memory_order_acquire) != nullptr; }
    // Detour side (any thread, no lock, no allocation): after the original returned `ret` raw bytes for `srclen` compressed ones.
    // 0 = not an attrib block, 1 = recognised (watch only, or the original returned another size), 2 = recognised and overwritten.
    int on_decompressed(const uint8_t* src, uint64_t srclen, uint8_t* dst, uint64_t dstlen, int64_t ret);
    void count_call() {
        calls_.fetch_add(1, std::memory_order_relaxed);
        if (!table_.load(std::memory_order_relaxed)) calls_before_.fetch_add(1, std::memory_order_relaxed);
    }
    void set_killed(bool k) { killed_.store(k); }
    bool killed() const { return killed_.load(); }
    // The lines not reported yet: "attrib block seen <YYYY-MM-DD HH:MM:SS.mmm> <asset> (<toc>; patched: ... | watch only: ...)", one per
    // item, the first time the game decompressed it
    std::vector<std::string> take_seen_lines();
    // turbo_output\attrib_ebx_log.txt
    std::string log_text(const std::string& head) const;
    uint64_t calls() const { return calls_.load(); }
    uint64_t hits() const { return hits_.load(); }
    uint64_t patched() const { return patched_.load(); }
    uint64_t published_ms() const { return published_ms_; }

private:
    std::atomic<const kitswap::Table*> table_{nullptr};
    std::shared_ptr<kitswap::Table> keep_;
    std::vector<ItemInfo> items_;  // written once, before table_ is published
    std::vector<uint8_t> reported_;
    mutable std::mutex m_;
    uint64_t published_ms_ = 0;
    std::atomic<bool> killed_{false};
    std::atomic<uint64_t> calls_{0}, calls_before_{0}, hits_{0}, patched_{0}, unserved_{0};
};
std::string clock_text(uint64_t ms_since_epoch);  // local "YYYY-MM-DD HH:MM:SS.mmm", "-" for 0

// The process's runtime (set by the host once; a later shared Oodle detour, e.g. the 3D kits' one, can call it too)
Runtime* active_runtime();
void set_active_runtime(Runtime* rt);

}  // namespace attrib
}  // namespace turbo
