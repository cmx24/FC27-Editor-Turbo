// FC 27 LE Turbo GUI - gameplay tuning (tactics overhaul track T3): Turbo changes the engine's gameplay tuning tables (the
// "attribulator" collections that gameplay mods edit) live in game memory, all inside Turbo (no mod manager). Platform-independent part;
// src/win/tuning_win.cpp gives it the game's memory, the signatures, the code scan and Turbo's game-thread dispatcher.
//
// DATA-DRIVEN. What can be tuned is a knob registry read from turbo\tuning\knobs_<build>.json (the package's
// turbo/package/turbo/tuning/knobs_6AC07E31.json). Every knob is shipped UNVERIFIED ("verified": false): it reads "not located on this
// build" and nothing is ever written for it until track T1 (the live locator) confirms its group, field, type and offset in the running
// game and sets verified. The entries come from turbo_output\TACTICS_MOD_CATALOGUE.txt (FC 27 type data: group, field, type, offset,
// FC 27's own value); nothing here guesses an address, and no gameplay mod's values are in it.
//
// A LOCATOR names how a knob's table is found in memory, re-chased inside every job. Kind "attrib" (the attribulator, catalogue
// sections 2 and 7) resolves by TYPE AND FIELD NAMES:
//   type      the reflected type ("AttribSchema_gp_positioning_zonal_defense"): its TypeInfoData in FC27.exe is found by name and must
//             have the file's signature, size and field count (L1); a knob's offset is the type's field of the knob's "field" name, and
//             must equal the offset the file gives
//   variants  one instance per attribulator database copy (base gameplayattribdb, databases/simulation = Authentic, ...), each found by
//             its asset name (the instance's Name at +0x18) with the same type pointer at +0 (L2): ONE KNOB WRITES EVERY COPY together
// Kind "chain":
//   anchor   a signature: a name in Turbo's signature table (core/sigscan.cpp) or an inline pattern of the knobs file (scanned once in
//            FC27.exe's code, unique match required, resolved like any signature)
//   deref    the anchor is a pointer slot (mov rax,[rip+x]): object 0 = [anchor], else object 0 = the anchor
//   chain    object k = [object k-1 + chain[k-1]] (each a pointer)
//   checks   header checks on any object of the chain (a vtable equal to a signature's address, a pointer, an integer or float in a range,
//            bytes): every check must pass, else "not located on this build (header check ...)"
//   add      the table's base = last object + add; a knob's address = base + its offset
// A "native" locator is code in the host (Host::natives, src/win/tuning_win.cpp) for a walk the chain cannot express (a hash map of
// collections); it is read-only and re-run in every job like a chain.
//
// WRITES (one game-thread job per Apply, every changed knob and every copy together): gates G1 opt-in turbo_output\tuning_on.txt, no
// tuning_off.txt / call_attrib_off.txt / tactics_off.txt; G2 a career phase every changed knob allows ("when", default the hub only; no save started in the last 30 s); G3 a
// knobs file for this exact game build; G4 every knob located now; G5 values (type, range); a read-back mismatch switches tuning off for
// the session. The job re-locates every knob, checks each still holds the value Apply was pressed for, snapshots, writes, reads every
// knob back and puts EVERY knob back from the snapshot on a mismatch. In the service a knob with a known FC 27 value is written only
// while it holds that value or the value Turbo wrote last (anything else: another tool or mod owns it). The values before Turbo's first write of a knob in this game
// session are the "game defaults" (Restore game defaults). Undo (turbo_output\tuning_undo.json) is bound to the career load it was
// written in. Undo and Restore game defaults put the game's values back: they need no opt-in and the kill switches do not block them.
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "mem.h"
#include "sigscan.h"

namespace turbo {
namespace tuning {

// ------------------------------------------------------------------------------------------------ files
constexpr const char* kKnobsSchema = "turbo-tuning-knobs/1";
inline const char* knobs_dir() { return "tuning"; }                 // <Live Editor>\turbo\tuning
inline const char* opt_in_file() { return "tuning_on.txt"; }        // turbo_output
inline const char* kill_switch_file() { return "tuning_off.txt"; }
inline const char* master_switch_file() { return "tactics_off.txt"; }  // every tactics channel
inline const char* attrib_switch_file() { return "call_attrib_off.txt"; }  // the attribulator writes (TACTICS_MOD_CATALOGUE section 8)
inline const char* undo_file() { return "tuning_undo.json"; }
inline const char* keep_file() { return "tuning_keep.json"; }       // the values written again at every career load (opt-in)
inline const char* presets_dir() { return "tuning_presets"; }
// The knobs file names tried for a build key ("6AC07E31-2145C000"): knobs_6AC07E31-2145C000.json, then knobs_6AC07E31.json
std::vector<std::string> knobs_file_names(const std::string& build);

// G2: a save started (Lua saw PREPARE_FOR_SAVE) less than this long ago refuses a write
constexpr long long kSaveQuietSeconds = 30;
// A job the game thread runs later than this after Apply writes nothing; one not back after kJobLostUs was dropped
constexpr double kJobDeadlineUs = 2e6, kJobLostUs = 10e6;
constexpr int kMaxChain = 8, kMaxKnobs = 512;
// A slow anchor (an attribulator search, an inline pattern) that failed is searched again after this long
constexpr double kAnchorRetryUs = 60e6;

// ------------------------------------------------------------------------------------------------ the registry
enum class ValueType { F32, I32, U8, Bool };
const char* type_name(ValueType t);  // "f32" "i32" "u8" "bool"
bool type_from_name(const std::string& s, ValueType& t);
size_t type_size(ValueType t);
enum class Applies { NextMatchSetup, Live, Unknown };
const char* applies_name(Applies a);  // "next match setup" "live" "unknown"
bool applies_from_name(const std::string& s, Applies& a);
// The groups the view shows, in order (a knob of another group is listed after them)
const std::vector<std::string>& standard_groups();  // Shape, Pressing, Marking, Attacking runs, Passing, Shooting, CPU AI, Goalkeeper, Roles
// The career phases a knob's "when" may name (Lua's match phase, core/bridge.h)
const std::vector<std::string>& phases();           // hub, prematch, match

struct Knob {
    std::string id, label, group, collection, field, unit, locator, notes;
    ValueType type = ValueType::F32;
    bool has_min = false, has_max = false, has_default = false;
    double min = 0, max = 0, def = 0;  // def: the shipped game value when known (display only; the live first snapshot rules)
    int64_t offset = -1;               // bytes from the locator's base; -1 = not known (placeholder)
    Applies applies = Applies::Unknown;
    std::vector<std::string> when{"hub"};  // phases a write is allowed in
    bool verified = false;             // T1 checked the knob on the file's build
    double step = 0;                   // slider step (0 = the type's)
    std::vector<std::string> below;    // knobs whose value this one must stay below (the line height bands do not overlap)
    bool allows(const std::string& phase) const;
    bool has_range() const { return has_min && has_max; }
    // why the knob is not complete in the file ("" = complete): placeholder, no locator, no offset
    std::string placeholder() const;
};

struct HeaderCheck {
    int object = 0;         // 0 = the anchor's object, k = the object after chain step k
    int64_t offset = 0;
    std::string kind;       // "vtable" "ptr" "u8_range" "u32_range" "i32_range" "f32_range" "bytes"
    std::string signature;  // vtable: the signature (Turbo's table) whose address the u64 must be
    double min = 0, max = 0;
    std::string bytes;      // bytes: hex, "??" allowed
    std::string note;
};

// One attribulator database copy of a group: its name ("base", "simulation") and the instance's asset name (its Name at +0x18)
struct Variant {
    std::string name, instance;
};

struct Locator {
    std::string name, kind = "chain";  // "chain" | "native" | "attrib"
    std::string signature;             // a name in Turbo's signature table ("" = the inline pattern, or none)
    Signature pattern;                 // inline pattern (pattern.pattern empty = none)
    bool deref = false;
    std::vector<int64_t> chain;
    int64_t add = 0;
    std::vector<HeaderCheck> checks;
    // attrib: the reflected type (L1) and the copies (L2)
    std::string type;
    uint32_t type_signature = 0, type_size = 0;
    int type_fields = -1;
    std::vector<Variant> variants;
    bool verified = false;
    std::string note;
    // why the locator cannot be used ("" = complete): placeholder, no anchor / type / variants
    std::string placeholder() const;
};

struct Registry {
    std::string build;   // the game build key the file is for
    std::string source;  // the file it was read from ("" = none)
    std::string note;
    int version = 0;
    std::vector<Knob> knobs;
    std::vector<Locator> locators;
    const Knob* knob(const std::string& id) const;
    const Locator* locator(const std::string& name) const;
    std::vector<std::string> groups() const;  // the standard groups that have knobs, then the others
    int placeholders() const;                 // knobs whose placeholder() is not ""
};
// Parses and validates a knobs file. A malformed file, an unknown type / applies / phase, a duplicate id, a bad range or a check of an
// unknown kind refuses the whole file (err). Incomplete knobs (placeholders) are fine. Keys starting with "_" are comments.
bool parse_registry(const std::string& text, Registry& out, std::string& err);
std::string registry_json(const Registry& r);  // the same format (tests, "how to add a knob")
// The knobs file of a build in `dir` (knobs_file_names); false + err when none is there or it does not parse or is for another build
bool load_registry(const std::filesystem::path& dir, const std::string& build, Registry& out, std::string& err);

// ------------------------------------------------------------------------------------------------ values
// "" when v is a value the knob takes: finite, an integer for i32 / u8, 0 or 1 for a switch, inside the range (a knob without a range
// cannot be written: "no value range")
std::string check_value(const Knob& k, double v);
double representable(const Knob& k, double v);  // what the game would hold: f32 rounding, integers rounded, switches 0 / 1
bool same_value(ValueType t, double a, double b);  // equal as stored (f32 bit pattern, integers)
std::string format_value(const Knob& k, double v, bool with_unit = true);  // "0.750 m", "12", "on"
bool read_value(Memory& mem, uint64_t addr, ValueType t, double& out);
bool write_value(Memory& mem, uint64_t addr, ValueType t, double v);

// ------------------------------------------------------------------------------------------------ reflected type data (FC27.exe)
// TypeInfoData (catalogue section 7 L1): +0 name, +0x08 u32 name hash, +0x0C u16 flags, +0x0E u16 size, +0x10 GUID, +0x30 u16 align,
// +0x32 u16 field count, +0x34 u32 signature, +0x40 -> FieldInfoData[count] (0x20 each: +0 name, +0x08 hash, +0x10 u32 offset,
// +0x18 TypeInfo*, whose +0 is the field type's TypeInfoData). An attribulator instance: +0 an image pointer (the same for every
// instance of a type), +0x18 its Name (the asset path)
constexpr uint64_t kTdName = 0x00, kTdSize = 0x0E, kTdFieldCount = 0x32, kTdSignature = 0x34, kTdFields = 0x40;
constexpr uint64_t kFiStride = 0x20, kFiName = 0x00, kFiOffset = 0x10, kFiType = 0x18;
constexpr uint64_t kInstName = 0x18;
constexpr int kMaxTypeFields = 4096;
struct FieldInfo {
    uint32_t offset = 0;
    std::string type;  // the field type's name ("Float32", "Int32", "Boolean", ...; "" = unreadable)
};
struct TypeLayout {
    std::string name;
    uint32_t signature = 0, size = 0;
    int fields = 0;
    std::map<std::string, FieldInfo> field;
};
// Parses the TypeInfoData at td. "" = parsed
std::string parse_type_info(Memory& mem, uint64_t td, TypeLayout& out);
// The knob type a reflected field type name stands for (false = a name Turbo does not map: no type check)
bool type_of_field(const std::string& field_type, ValueType& t);
// Search helpers for the host's memory scans (one chunk at a time): every position where one of `names` sits as a NUL-terminated
// string (ASCII, letter case ignored) -> (name index, address); every 8-aligned qword equal to one of `values` -> (value, slot address)
std::vector<std::pair<size_t, uint64_t>> find_cstrings(const uint8_t* buf, size_t len, uint64_t base, const std::vector<std::string>& names,
                                                       size_t max_hits);
std::vector<std::pair<uint64_t, uint64_t>> find_qwords(const uint8_t* buf, size_t len, uint64_t base, const std::set<uint64_t>& values, size_t max_hits);
bool same_name(const std::string& a, const std::string& b);  // ASCII, letter case ignored

// ------------------------------------------------------------------------------------------------ locate
using NativeLocator = std::function<std::string(Memory& mem, uint64_t& base)>;  // "" + base, else why
struct Lookup {
    std::function<uint64_t(const std::string&)> signature;  // Turbo's table: the resolved address, 0 = not found
    std::function<SigResult(const Signature&)> scan;        // the code scan for inline patterns (null = not available)
    std::map<std::string, NativeLocator> natives;
    // attrib: for each name, the 8-aligned qword slots that point at a NUL-terminated copy of it (letter case ignored), in FC27.exe's
    // image (in_image) or in the game's writable heap. Slow (whole memory): the host runs it away from the render thread
    std::function<std::map<std::string, std::vector<uint64_t>>(const std::vector<std::string>& names, bool in_image)> refs;
    uint64_t image_base = 0, image_size = 0;                // FC27.exe (size 0 = no range check: tests)
};
// One copy of a table: its variant ("" for a chain / native locator) and its base address
struct Copy {
    std::string variant;
    uint64_t base = 0;
};
// A locator's anchor, resolved once per process (the anchor is code or static data)
struct Anchor {
    bool ok = false, pending = false;  // pending: its inline pattern / its attribulator data is being searched
    uint64_t addr = 0;
    std::string why;
    // attrib: the type found by name (L1), the instances' common type pointer and one instance per variant (L2)
    TypeLayout layout;
    uint64_t type_ptr = 0;
    std::vector<Copy> copies;
};
using AnchorMap = std::map<std::string, Anchor>;
// The anchor of one locator from Turbo's signature table or an inline pattern (lk.scan, may be slow: the host runs it away from the
// render thread); a native locator needs no anchor (ok when the host has it)
Anchor resolve_anchor(Memory& mem, const Locator& l, const Lookup& lk);
inline bool slow_anchor(const Locator& l) { return l.kind == "attrib" || (l.kind == "chain" && l.signature.empty() && !l.pattern.pattern.empty()); }
// The chain from the anchor and every header check (attrib: every copy re-checked: its type pointer and its Name). "" + the copies'
// bases, else why (no "not located" prefix)
std::string chase(Memory& mem, const Locator& l, const Anchor& anchor, const Lookup& lk, std::vector<Copy>& bases, std::vector<uint64_t>* objects = nullptr);
// The knob's offset in its table: attrib = the type's field of the knob's field name, which must equal the file's offset and fit the
// type and the knob's type; else the file's offset. "" + off, else why
std::string field_offset(const Knob& k, const Locator& l, const Anchor& a, uint64_t& off);

struct CopyAt {
    std::string variant;
    uint64_t addr = 0;
    double value = 0;
};
struct KnobAt {
    std::vector<CopyAt> copies;  // one per variant (one for a chain / native locator)
    uint64_t addr = 0;           // the first copy's
    double value = 0;
};
std::string copy_key(const std::string& id, const std::string& copy);  // "shape.width_out@simulation" ("" copy: the id)
// The knob's address now and its value: the locator re-chased, the value read and inside the knob's range (a value outside it says the
// layout is wrong). "" + out, else "not located on this build (<why>)"
std::string locate_knob(Memory& mem, const Registry& reg, const Knob& k, const AnchorMap& anchors, const Lookup& lk, KnobAt& out);

struct KnobLive {
    std::string id;
    bool located = false;
    std::string why;   // why not located ("" when located)
    uint64_t addr = 0;
    double value = 0;       // the first copy's
    bool writable = false;  // located and the knob has a range
    std::vector<CopyAt> copies;
    const CopyAt* copy(const std::string& variant) const;
};

// ------------------------------------------------------------------------------------------------ gates
struct Switches {
    bool opt_in = false, off = false, master = false, attrib = false;
    std::string blocking() const;  // the kill switch file that blocks an Apply ("" = none)
};
Switches read_switches(const std::filesystem::path& turbo_output);

struct GateInput {
    Switches sw;
    bool bridge = false, in_cm = false;
    std::string match_phase;   // Lua's "hub" / "prematch" / "match" / "unknown" ("" = a Lua side without it)
    long long save_age = -1;   // seconds since Lua saw a save start (-1 = none)
    long long load_gen = -1;
    std::string registry;      // G3 ("" = a knobs file for this build)
    std::string locate;        // G4 ("" = every knob of the plan located)
    std::string values;        // G5 ("" = the plan's values are fine)
    std::string session_off;
    std::vector<const Knob*> knobs;  // the plan's knobs (their "when" for G2; none = the hub only)
    bool restore = false;      // Undo / Restore game defaults: no opt-in, the kill switches do not block, the session switch neither
};
struct Gate {
    std::string name, why;  // "" = pass
    bool pass() const { return why.empty(); }
};
struct Gates {
    std::vector<Gate> list;
    bool pass() const;
    std::string first() const;  // "G2 career phase: ..."
};
Gates evaluate(const GateInput& in);

// ------------------------------------------------------------------------------------------------ plans and the job
enum class PlanKind { Apply, Undo, Restore, Reapply };
const char* plan_kind_name(PlanKind k);  // "apply" "undo" "restore game defaults" "re-apply"
inline bool puts_back(PlanKind k) { return k == PlanKind::Undo || k == PlanKind::Restore; }
struct Write {
    std::string id;
    double old_value = 0, new_value = 0;
    std::string copy;  // the variant written ("" for a chain / native locator)
};
struct Plan {
    PlanKind kind = PlanKind::Apply;
    std::vector<Write> writes;
    long long load_gen = -1;  // the career load it was made in (-1 = the submitting context's)
    std::string session;
};
// The diff of the wanted values against the live ones: each wanted knob must be in the registry, located and writable, its value must
// pass check_value and keep the knob below the knobs it must stay below; it is written into every copy that does not hold it yet.
// "" + plan (possibly empty), else the first refusal
std::string make_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const std::map<std::string, double>& wanted, PlanKind kind,
                      Plan& out);

struct ApplyResult {
    PlanKind kind = PlanKind::Apply;
    bool ok = false, written = false, undone = false, restore_failed = false, session_off = false;
    std::string stage, message, log_line;
    std::vector<Write> writes;               // what was written (old = the snapshot)
    std::map<std::string, double> snapshot;  // copy_key -> the value the job read before writing (the first snapshot = the game default)
    double micros = 0;
    int reads = 0;
};
// Re-locate every knob, the plan's old values still in place, G5 again, snapshot, write, read every knob back; a mismatch puts EVERY
// knob back from the snapshot (and reads that back). Never throws. `cancel` set: nothing is written. owner_check: a knob with a known
// FC 27 value (its "default") is written only while it holds that value or the one Turbo wrote last (last_written, by copy_key)
struct JobOptions {
    const std::atomic<bool>* cancel = nullptr;
    bool owner_check = false;
    const std::map<std::string, double>* last_written = nullptr;
};
ApplyResult apply_plan(Memory& mem, const Registry& reg, const AnchorMap& anchors, const Lookup& lk, const Plan& plan,
                       const JobOptions& opt = JobOptions());
std::string describe_writes(const Registry& reg, const std::vector<Write>& ws);  // "Pressing intensity 0.500 -> 0.600, ..."

// ------------------------------------------------------------------------------------------------ the undo record (tuning_undo.json)
struct UndoRecord {
    std::string build, when, session, state, note;  // state: "pending" "written" "undone" "aborted" "restored" "failed"
    long long load_gen = -1;
    std::vector<Write> knobs;  // one per knob and copy: old = the value before Turbo's first write in this career load, new = Turbo's
    std::string to_json() const;
    static bool parse(const std::string& text, UndoRecord& out, std::string* err);
};
bool load_undo(const std::filesystem::path& p, UndoRecord& out, std::string* err);
bool save_undo(const std::filesystem::path& p, const UndoRecord& u, std::string* err);
// "" when the record belongs to this Lua session and career load
std::string same_career(const UndoRecord& u, const std::string& session, long long load_gen);
// A second Apply in the same career load keeps the old values of the first (merged by knob); another career load: `next` as it is
UndoRecord merge_undo(const UndoRecord& prev, const UndoRecord& next);
// The undo plan: knobs that still hold Turbo's value get their old value back; the others are listed in `skipped`. Refused (why) across
// career loads, when nothing is written in place or nothing can be put back
std::string make_undo_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const UndoRecord& u, const std::string& session,
                           long long load_gen, Plan& out, std::vector<std::string>& skipped);
// The Restore game defaults plan: every knob copy with a game default (the first snapshot of this game session, by copy_key) that is
// located and holds another value gets the default back. "" + plan, else why
std::string make_restore_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const std::map<std::string, double>& defaults, Plan& out,
                              std::vector<std::string>& skipped);

// ------------------------------------------------------------------------------------------------ kept values (tuning_keep.json)
// Opt-in ("on"): the values are written again at the first hub of every career load (one job), for the located knobs of this build
struct KeepStore {
    bool on = false;
    std::string build, when;
    std::map<std::string, double> values;
    std::string to_json() const;
    static bool parse(const std::string& text, KeepStore& out, std::string* err);
};
bool load_keep(const std::filesystem::path& p, KeepStore& out, std::string* err);  // false when absent or unreadable
bool save_keep(const std::filesystem::path& p, const KeepStore& k, std::string* err);
// The re-apply plan of the kept values for this registry: knobs not in the registry / not located / out of range are skipped (listed)
std::string make_keep_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const KeepStore& k, Plan& out,
                           std::vector<std::string>& skipped);

// ------------------------------------------------------------------------------------------------ presets (turbo_output\tuning_presets)
struct PresetChange {
    std::string knob;
    bool relative = true;  // value = game default x (1 + pct / 100)
    double pct = 0, value = 0;
};
struct Preset {
    std::string id, name, note, author;  // author "Turbo" = Turbo's own (read-only in the view)
    std::vector<PresetChange> changes;
    std::filesystem::path file;          // where it was read from
    bool turbo() const { return author == "Turbo"; }
    std::string to_json() const;
    static bool parse(const std::string& text, Preset& out, std::string* err);
};
// Turbo's own presets: relative changes only (no real field values are known yet); Turbo's own design, no mod's value table
const std::vector<Preset>& builtin_presets();
std::string preset_file_name(const Preset& p);  // "turbo_high_line_press.json" (Turbo's own), "<safe name>.json"
// Writes Turbo's own presets into dir when a file is missing (an existing file is never overwritten). Returns how many were written
int ensure_builtin_presets(const std::filesystem::path& dir, std::string* err);
// Every *.json preset in dir, by name; a file that does not parse is listed in `errors`. Turbo's own come first
std::vector<Preset> load_presets(const std::filesystem::path& dir, std::vector<std::string>* errors);
// The values a preset gives: a relative change from the knob's base (base_of: its game default; false = not known), clamped to the
// knob's range and made representable; an absolute value must pass check_value. Knobs not in the registry, without a base or a range,
// switches with a relative change: skipped (listed)
struct PresetValues {
    std::map<std::string, double> values;
    std::vector<std::string> skipped;
};
PresetValues preset_values(const Preset& p, const Registry& reg, const std::function<bool(const std::string&, double&)>& base_of);
// A preset from the knobs that differ from their base: a relative change (rounded to 0.1 %) when the base is not 0, else the value
Preset preset_from_values(const std::string& name, const Registry& reg, const std::map<std::string, double>& values,
                          const std::function<bool(const std::string&, double&)>& base_of);

// ------------------------------------------------------------------------------------------------ the service the view uses
struct Context {
    std::string session;
    long long load_gen = -1;
    std::string phase;   // Lua's match phase
    bool in_cm = false;
};

class Service {
public:
    virtual ~Service() = default;
    virtual std::shared_ptr<const Registry> registry() = 0;  // never null (an empty registry when no file is loaded)
    virtual std::string registry_status() = 0;               // "" = a knobs file for this build is loaded
    virtual bool reload(std::string& why) = 0;               // read the knobs file again (refused while a job runs)
    virtual std::string anchors_status() = 0;                // "2 of 3 locators resolved ..." / "resolving ..."
    virtual std::vector<KnobLive> read() = 0;                // every knob of the registry (read-only, the calling thread)
    virtual bool submit(const Context& ctx, const Plan& plan, std::string& why) = 0;  // the job on the game thread
    virtual bool poll(ApplyResult& out) = 0;
    virtual bool busy() = 0;
    virtual std::string session_off() = 0;                   // "" = on
    virtual std::map<std::string, double> defaults() = 0;    // the first snapshot of this game session, by copy_key
    virtual void note_context(const Context& ctx) = 0;       // every frame: the job compares its phase and career load with it
};

// What the host gives the platform-independent service (src/win/tuning_win.cpp; tests give a simulated table)
struct Host {
    Memory* mem = nullptr;
    std::string build;                           // the running game's build key
    std::filesystem::path knobs_dir;             // <Live Editor>\turbo\tuning
    std::filesystem::path out_dir;               // turbo_output: the switch files are looked at again right before a job is queued
    Lookup lookup;
    std::function<std::string(std::function<void()>)> run_on_game_thread;  // "" = not queued
    std::function<void(std::function<void()>)> run_background;            // inline-pattern scans (tests: inline)
    std::function<void(const std::string&)> log;
    std::function<double()> clock_us;
    std::string off;                             // set: the whole feature is off (e.g. game hooks off for this build)
};

class CoreService : public Service {
public:
    explicit CoreService(Host h);
    std::shared_ptr<const Registry> registry() override;
    std::string registry_status() override;
    bool reload(std::string& why) override;
    std::string anchors_status() override;
    std::vector<KnobLive> read() override;
    bool submit(const Context& ctx, const Plan& plan, std::string& why) override;
    bool poll(ApplyResult& out) override;
    bool busy() override;
    std::string session_off() override;
    std::map<std::string, double> defaults() override;
    void note_context(const Context& ctx) override;
    std::map<std::string, double> last_written();  // tests: what Turbo wrote last, by copy_key

private:
    void load_locked();       // reads the knobs file (mu_ held)
    void ensure_anchors();    // resolves every locator's anchor once (inline patterns in the background)
    void log(const std::string& s);
    double now() const { return h_.clock_us ? h_.clock_us() : 0.0; }

    Host h_;
    std::mutex mu_;
    std::shared_ptr<const Registry> reg_;
    std::string reg_why_;
    AnchorMap anchors_;
    std::map<std::string, double> anchor_failed_at_;  // clock_us of a slow anchor's last failed search
    long long anchors_epoch_ = 0;  // bumps on reload: a scan of an older registry does not fill the new map
    std::vector<ApplyResult> results_;
    bool busy_ = false;
    double busy_since_ = 0;
    int job_seq_ = 0;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::string session_off_;
    std::map<std::string, double> defaults_;
    std::map<std::string, double> last_written_;
    std::string cur_phase_, cur_session_;
    long long cur_gen_ = -1;
};

}  // namespace tuning
}  // namespace turbo
