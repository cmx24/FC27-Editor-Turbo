// FC 27 LE Turbo GUI - gameplay tuning (tactics overhaul track T3): Turbo changes the engine's gameplay tuning tables (the
// "attribulator" collections that gameplay mods edit) live in game memory, all inside Turbo (no mod manager). Platform-independent part;
// src/win/tuning_win.cpp gives it the game's memory, the signatures, the code scan and Turbo's game-thread dispatcher.
//
// DATA-DRIVEN. What can be tuned is a knob registry read from turbo\tuning\knobs_<build>.json (the package's
// turbo/package/turbo/tuning/knobs_6AC07E31.json). A knob shipped UNVERIFIED ("verified": false) reads "not located on this build" and
// nothing is ever written for it; only the live locator run (track T1, turbo_output\ATTRIBULATOR_LIVE.json) that confirms its group's
// type, field offset and instances in the running game sets verified. The entries come from turbo_output\TACTICS_MOD_CATALOGUE.txt
// (FC 27 type data: group, field, type, offset, FC 27's own value per database copy); nothing here guesses an address, and no gameplay
// mod's values are in it (ranges are generic bounds).
//
// A LOCATOR names how a knob's table is found in memory, re-chased inside every job. Kind "attrib" (the attribulator, catalogue
// sections 2 and 7) resolves by TYPE AND FIELD NAMES:
//   type      the reflected type ("AttribSchema_gp_positioning_zonal_defense"): its TypeInfoData in FC27.exe is found by name and must
//             have the file's signature, size and field count (L1); a knob's offset is the type's field of the knob's "field" name, and
//             must equal the offset the file gives
//   variants  one instance per attribulator database copy (base gameplayattribdb, databases/simulation = Authentic, ...): L3 through the
//             database runtime object named "db" (+family_slot -> the group object, +group_slot -> the instance), else L2 by the
//             instance's own Name at +0x18 (unique); each with the same vtable at +0, its TypeInfo at +8 ("head": "typeinfo") and its
//             Name re-checked in every job: ONE KNOB WRITES EVERY COPY together
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
// knobs file for this exact game build; G4 every knob located now (a knob not "verified" in the file is never located); G5 values
// (type, range); a read-back mismatch switches tuning off for the session. The job re-locates every knob, checks each still holds the
// value Apply was pressed for, that every copy of each knob is in the plan (base and simulation together or not at all), snapshots,
// writes, reads every knob back and puts EVERY knob back from the snapshot on a mismatch. In the service every copy is written only
// while it holds FC 27's own value of that copy ("default" / "copy_defaults" of the knobs file) or the value Turbo wrote last (anything
// else: another tool or mod owns it; refused). The values before Turbo's first write of a knob in this game session are the "game
// defaults" (Restore game defaults). Undo (turbo_output\tuning_undo.json) is bound to the career load it was written in. Undo and
// Restore game defaults put the game's values back: they need no opt-in and the kill switches do not block them. Attribulator data is
// not in the career save: values last for this game session.
//
// 2.4 (docs/TACTICS_OVERHAUL_DECISION.md 3.1, WP-A1):
//   * DETERMINISTIC LOCATE. With the knobs file's "attrib_manager" and the signature attrib_db_getter (get_current_gameplay_attribdb:
//     mov rcx,[rip+G]) an attrib locator is found WITHOUT a memory search: G -> mgr = [G] -> the current gameplay database
//     [[mgr+0]+8] and the prematch database [[mgr+8]+8] (each pointer slot masked & ~4) -> +family_slot the group object (its type
//     checked) -> +group_slot the instance. Every gameplay database (15) is enumerated once per game session from the heap slots that
//     hold the database type's TypeInfo (one background pass); the distinct instances of a group over all databases are its copies,
//     matched to the knobs file's variants by their Name. Identity per instance: +0 a vtable in FC27.exe (the RVA when the file gives
//     it), +8 its TypeInfo whose TypeInfoData has the locator's type name, signature, size and field count, +0x18 the variant's asset
//     path, the asset GUID. A mismatch refuses that group for the game session (logged). The name search of the 2.3 locator stays as
//     the cross-check on the first run per build (both must find the same instances: turbo_output\tuning_chain_check.json) and as
//     the fallback when the signature is missing. Log: "tuning: chain locate ok <n> groups <m> knobs <ms> ms".
//   * THE PROFILE turbo_output\tuning_profile.json {"on", "preset", "values"} (written by the UI only) is applied at three moments:
//     boot (the first tick after the locate, no career loaded: the main menu), career_load (once per career load, in its hub) and hub
//     (every time the hub is entered, never within 5 s of a prematch phase: Lua goes hub -> prematch -> hub 0.5 s -> match). A knob
//     is written at a moment its "when" names; in prematch / match only with "live": true. Kill switches: tuning_off.txt (every write),
//     tuning_profile_off.txt (the automatic moments), call_attrib_off.txt, tactics_off.txt. Log: "tuning: profile applied (<moment>)
//     <k> knobs (read back ok)". Restore FC 27 defaults writes FC 27's own values from the knobs file and switches the profile off.
//   * HONEST UI (WP-A2): a knob is PROVEN (a "proof" in the knobs file: the in-game session and its evidence; a slider), LOCATED
//     (read-only: located, effect not proven) or UNLOCATED (hidden, listed as not yet working).
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
inline const char* keep_file() { return "tuning_keep.json"; }       // 2.3's kept values: no longer read (replaced by the profile)
inline const char* profile_file() { return "tuning_profile.json"; } // the profile the automatic moments apply (written by the UI)
inline const char* profile_off_file() { return "tuning_profile_off.txt"; }  // kill switch of the automatic moments only
inline const char* chain_check_file() { return "tuning_chain_check.json"; } // the chain / name search cross-check of each build
inline const char* presets_dir() { return "tuning_presets"; }
// The knobs file names tried for a build key ("6AC07E31-2145C000"): knobs_6AC07E31-2145C000.json, then knobs_6AC07E31.json
std::vector<std::string> knobs_file_names(const std::string& build);

// G2: a save started (Lua saw PREPARE_FOR_SAVE) less than this long ago refuses a write
constexpr long long kSaveQuietSeconds = 30;
// A job the game thread runs later than this after Apply writes nothing; one not back after kJobLostUs was dropped
constexpr double kJobDeadlineUs = 2e6, kJobLostUs = 10e6;
constexpr int kMaxChain = 8, kMaxKnobs = 512;
// A slow anchor (an attribulator search, an inline pattern) that failed is searched again after this long, doubled after every further
// failure up to kAnchorRetryMaxUs (a whole-memory search must not run every minute while the attribulator is not loaded)
constexpr double kAnchorRetryUs = 60e6, kAnchorRetryMaxUs = 960e6;
double anchor_retry_us(int failures);  // 60 s, 120 s, 240 s, ... at most 16 min

// ------------------------------------------------------------------------------------------------ the registry
enum class ValueType { F32, I32, U8, Bool };
const char* type_name(ValueType t);  // "f32" "i32" "u8" "bool"
bool type_from_name(const std::string& s, ValueType& t);
size_t type_size(ValueType t);
enum class Applies { NextMatchSetup, Live, Unknown };
const char* applies_name(Applies a);  // "next match setup" "live" "unknown"
bool applies_from_name(const std::string& s, Applies& a);
// The groups the view shows, in order (a knob of another group is listed after them)
const std::vector<std::string>& standard_groups();  // Shape with / without the ball, Defensive line depth bands, Shape, Pressing, Marking, Attacking runs, ...
// The career phases (Lua's match phase, core/bridge.h) and the profile's apply moments
const std::vector<std::string>& phases();           // hub, prematch, match
const std::vector<std::string>& moments();          // boot, career_load, hub
// What a knob's "when" may name: the moments and the phases
const std::vector<std::string>& when_values();      // boot, career_load, hub, prematch, match

// The in-game proof of a knob's effect on a match (the P-sessions, docs/TACTICS_OVERHAUL_DECISION.md section 5): the session and the
// evidence it wrote ("EV/2026-10-14/TEST_25.md#width_in"). Adding a proof to the knobs file is a pure data change
struct Proof {
    std::string session, evidence, note;
    bool given() const { return !session.empty() && !evidence.empty(); }
};

struct Knob {
    std::string id, label, group, collection, field, unit, locator, notes;
    ValueType type = ValueType::F32;
    bool has_min = false, has_max = false, has_default = false;
    double min = 0, max = 0, def = 0;  // def: FC 27's own value of every copy (the owner rule, Reset, presets' base)
    std::map<std::string, double> copy_defaults;  // FC 27's own value of a copy that differs from def ("simulation": 0)
    int64_t offset = -1;               // bytes from the locator's base; -1 = not known (placeholder)
    Applies applies = Applies::Unknown;
    std::vector<std::string> when{"hub"};  // the moments (boot, career_load, hub) and phases a write is allowed in
    bool live = false;                 // may also be written while a match is set up or played (gate X1 outcome 1)
    Proof proof;                       // the in-game proof of its effect (none = not proven: read-only in the view)
    bool slider = false;               // "slider": true in the file: offered as a slider; needs a proof (the package guard refuses it without)
    bool verified = false;             // T1 checked the knob on the file's build
    double step = 0;                   // slider step (0 = the type's)
    std::vector<std::string> below;    // knobs whose value this one must stay below (the line height bands do not overlap)
    // A phase: "hub" when `when` names it; "prematch" / "match" when `live` or `when` names it
    bool allows(const std::string& phase) const;
    bool allows_moment(const std::string& moment) const;  // `when` names the moment
    bool proven() const { return proof.given(); }         // a slider in the view (a "slider" without a proof is not one)
    bool has_range() const { return has_min && has_max; }
    // FC 27's own value of one copy ("" = a chain / native locator's only copy): copy_defaults, else def. false = not known
    bool game_value(const std::string& copy, double& v) const;
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

// One attribulator database copy of a group: its name ("base", "simulation"), the instance's asset name (its Name at +0x18) and, for
// L3, the Name of the database runtime object that points at it ("Fifa/Attribulator/Gameplay/gameplayattribdb"; "" = L2: the instance
// is found by its own Name, which must then be unique)
struct Variant {
    std::string name, instance, db;
    std::vector<uint8_t> guid;  // the asset's GUID in the 16 bytes before the instance (file: "guid": "1852d61d-8c0d-..."; empty = not checked)
};
// A GUID "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" as the 16 bytes in memory (the first three groups little-endian) and back
bool guid_bytes(const std::string& text, std::vector<uint8_t>& out);
std::string guid_text(const std::vector<uint8_t>& b);

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
    // attrib: an instance's head (the live run, ATTRIBULATOR_LIVE.json: +0 a vtable in FC27.exe, +8 its TypeInfo*, +0x18 its Name).
    // +0 is always a pointer into FC27.exe, the same for every copy; head "typeinfo" = +8 must be the type's TypeInfo (whose +0 is the
    // TypeInfoData L1 found); head_rva (not 0) = +0 exactly FC27.exe's base + head_rva (the vtable)
    std::string head;
    uint64_t head_rva = 0;
    // attrib L3 (variants with a "db"): the database runtime object (type db_type, found by its Name at +0x18 with its TypeInfo at +8)
    // holds the family's group object at +family_slot; the group object (type group_type when given) holds the instance at +group_slot.
    // Pointer slots carry tag bit 2 (masked). The chain picks the instance the game uses when several objects carry its name
    std::string db_type, group_type;
    uint32_t db_type_signature = 0, group_type_signature = 0;
    uint64_t family_slot = 0, group_slot = 0;
    bool l3() const;  // a variant names its database
    // the chain from the attribute manager can reach it (WP-A1): db_type, family_slot and group_slot are given
    bool chain_capable() const { return kind == "attrib" && !db_type.empty() && family_slot && group_slot; }
    bool verified = false;
    std::string note;
    // why the locator cannot be used ("" = complete): placeholder, no anchor / type / variants
    std::string placeholder() const;
};

// The knobs file's "attrib_manager" (WP-A1): how the attribulator databases are reached from the static manager slot G without a memory
// search. signature (Turbo's table) resolves to G; mgr = [G]; the current gameplay database = the pointer chain `current` from mgr (each
// step [obj + off] & ~4); the prematch database = [[mgr + prematch_slot] + handle_db] (when that slot does not hold it, every 8-aligned slot
// of the manager's first mgr_size bytes is tried and the one found is logged). A database is checked by its TypeInfo's TypeInfoData (type
// name, signature) and its Name at +0x18 (one of gameplay_dbs / prematch_dbs; empty list = any "Fifa/Attribulator/..." name).
struct AttribManager {
    bool present = false;
    std::string signature = "attrib_db_getter";
    std::vector<int64_t> current{0x0, 0x8};
    int64_t prematch_slot = 0x8, handle_db = 0x8, mgr_size = 0x240, index_at = 0x18;
    std::string gameplay_db_type, prematch_db_type;
    uint32_t gameplay_db_signature = 0, prematch_db_signature = 0;
    std::vector<std::string> gameplay_dbs, prematch_dbs;
    std::string note;
    bool prematch(const Locator& l) const { return !prematch_db_type.empty() && l.db_type == prematch_db_type; }
};

struct Registry {
    std::string build;   // the game build key the file is for
    std::string source;  // the file it was read from ("" = none)
    std::string note;
    int version = 0;
    AttribManager manager;
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
constexpr uint64_t kInstName = 0x18, kInstTypeInfo = 0x08, kInstGuidBefore = 0x10;
constexpr uint64_t kSlotTag = 4;  // tag bit 2 of the pointer slots in database / group runtime objects
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
    // The 8-aligned qword slots of the game's writable heap that hold exactly `value` (one pass, at most max_hits). Slow: the database
    // enumeration of the chain locate runs it once per game session, away from the render thread (null = not available)
    std::function<std::vector<uint64_t>(uint64_t value, size_t max_hits)> heap_qwords;
    uint64_t image_base = 0, image_size = 0;                // FC27.exe (size 0 = no range check: tests)
};
// One copy of a table: its variant ("" for a chain / native locator), its base address and (L3) the database runtime object and its Name
// (the chain locate: the database the copy was reached from; "" = the variant's "db")
struct Copy {
    std::string variant;
    uint64_t base = 0;
    uint64_t db = 0;
    std::string db_name;
};
// A locator's anchor, resolved once per process (the anchor is code or static data)
struct Anchor {
    bool ok = false, pending = false;  // pending: its inline pattern / its attribulator data is being searched
    bool refused = false;              // an identity mismatch: refused for this game session (never located again)
    std::string path;                  // attrib: "chain" (the attribute manager) or "search" (the name search)
    uint64_t addr = 0;
    std::string why;
    // attrib: the type found by name (L1: its TypeInfoData td), the instances' common type pointer and one instance per variant (L2)
    TypeLayout layout;
    uint64_t td = 0, type_ptr = 0;
    uint64_t db_td = 0, group_td = 0;  // L3: the database and group runtime types' TypeInfoData (0 = not checked)
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

// ------------------------------------------------------------------------------------------------ the chain from the attribute manager
// The attribulator databases the G walk found in this game session
struct AttribDb {
    uint64_t addr = 0;
    std::string name;
    bool prematch = false;
};
struct AttribDbs {
    uint64_t g = 0, mgr = 0, current = 0, prematch = 0;
    int64_t prematch_slot = -1;           // the manager slot that held the prematch handle (-1 = not found)
    int64_t seed_slot = -1;               // no current database yet: the manager slot whose handle gave a gameplay database (-1 = none)
    bool seed_slot_found() const { return seed_slot >= 0; }
    int32_t index = -1;                   // i32 at mgr + index_at (the game's current database index; diagnostics)
    std::string current_name;
    uint64_t gameplay_td = 0, prematch_td = 0, gameplay_ti = 0, gameplay_vt = 0;
    std::vector<AttribDb> dbs;            // the current gameplay database first, then the other gameplay ones, then the prematch one
    bool enumerated = false;              // every gameplay database of the manager's list was found (else: the current one only)
    std::string enum_why;                 // the enumeration's outcome ("15 of 15 gameplay databases", "... missing ...")
    std::string summary() const;          // one log line
};
// The walk G -> mgr -> the current gameplay database and the prematch database, each identity-checked (TypeInfo -> TypeInfoData name and
// signature, Name). "" + out; else why: transient = the manager or the database does not exist yet (the game is still starting: retry)
std::string walk_manager(Memory& mem, const AttribManager& m, const Lookup& lk, AttribDbs& out, bool& transient);
// Every gameplay database from the heap slots that hold the current database's TypeInfo (Lookup::heap_qwords): each with the current one's
// vtable and TypeInfo and a Name of the manager's list, each Name once (byte copies of a database that point at the same groups are one).
// Fills io.dbs / enumerated / enum_why; never fails the walk (the current database stays)
void enumerate_dbs(Memory& mem, const AttribManager& m, const Lookup& lk, const std::vector<uint64_t>& family_slots, AttribDbs& io);
// One attrib locator through the databases, no search: each database +family_slot -> the group object (type group_type) +group_slot ->
// the instance; the distinct instances matched to the variants by Name. Identity per instance as in the file header. refused = an
// identity mismatch (the group is refused for the session); else !ok = not reachable yet (a variant not found before the enumeration)
Anchor resolve_attrib_chain(Memory& mem, const AttribManager& m, const Locator& l, const AttribDbs& dbs, const Lookup& lk);

// The cross-check record (turbo_output\tuning_chain_check.json): per build, the locators whose chain copies equalled the name search's
struct ChainCheck {
    std::string build, when;
    std::set<std::string> agree;
    std::string to_json() const;
    static bool parse(const std::string& text, ChainCheck& out, std::string* err);
};
bool load_chain_check(const std::filesystem::path& p, ChainCheck& out, std::string* err);
bool save_chain_check(const std::filesystem::path& p, const ChainCheck& c, std::string* err);
// "" when the chain's and the name search's anchors hold the same instance for every variant, else what differs
std::string same_instances(const Anchor& chain, const Anchor& search);

// What the locate has reached (the automatic moments wait for `ready`)
struct LocateStatus {
    bool ready = false;   // nothing pending: every usable locator resolved (or refused), the chain's databases enumerated
    std::string path;     // "chain" (the attribute manager), "search" (the name search: fallback), "" (no attrib locator)
    int groups = 0, knobs = 0;  // verified locators located now, their verified knobs
    std::string why;      // not ready: what it waits for
};

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

// The view's state of a knob (WP-A2): PROVEN = a proof in the knobs file and located with a range (a slider); LOCATED = located, its
// effect not proven (a read-only row); UNLOCATED = not located on this build (hidden, listed as not yet working)
enum class KnobState { Proven, Located, Unlocated };
const char* knob_state_name(KnobState s);  // "proven" "located" "unlocated"
KnobState knob_state(const Knob& k, const KnobLive* live);

// ------------------------------------------------------------------------------------------------ gates
// The switch files (and the profile's "on"); `session` is the tab's tick for this game session (set by the caller)
struct Switches {
    bool opt_in = false, off = false, master = false, attrib = false;
    bool profile_off = false, profile_on = false, session = false;
    bool opted_in() const { return opt_in || profile_on || session; }  // G1 of a write from the view
    std::string blocking() const;  // the kill switch file that blocks an Apply ("" = none)
    std::string blocking_moments() const;  // ... and the automatic moments (tuning_profile_off.txt too)
};
Switches read_switches(const std::filesystem::path& turbo_output);

struct GateInput {
    Switches sw;
    bool bridge = false, in_cm = false;
    std::string match_phase;   // Lua's "hub" / "prematch" / "match" / "unknown" ("" = a Lua side without it)
    long long save_age = -1;   // seconds since Lua saw a save start (-1 = none)
    long long prematch_age = -1;  // seconds since Lua's last prematch event (-1 = none): a hub write waits kPrematchGuardSeconds
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
    std::string moment;       // the profile's automatic moment ("boot", "career_load", "hub"; "" = a click in the view)
};
// The diff of the wanted values against the live ones: each wanted knob must be in the registry, located and writable, its value must
// pass check_value and keep the knob below the knobs it must stay below. A key is a knob id (one value for every copy) or a copy_key
// ("id@simulation": that copy's value; then every copy of the knob needs its own key). COPIES GO TOGETHER: a knob any copy of which
// changes is written into EVERY copy (a copy that already holds its value is written with that same value), never into some of them.
// "" + plan (possibly empty), else the first refusal
std::string make_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const std::map<std::string, double>& wanted, PlanKind kind,
                      Plan& out);
// The knob id and the copy of a copy_key ("shape.gap_dmf@simulation" -> "shape.gap_dmf", "simulation"; "shape.gap_dmf" -> it, "")
void split_copy_key(const std::string& key, std::string& id, std::string& copy);

struct ApplyResult {
    PlanKind kind = PlanKind::Apply;
    bool ok = false, written = false, undone = false, restore_failed = false, session_off = false;
    std::string moment;              // the plan's moment ("" = a click in the view)
    std::vector<std::string> lines;  // logged exactly as they are (the profile's "tuning: profile applied (...)" line)
    std::string stage, message, log_line;
    std::vector<Write> writes;               // what was written (old = the snapshot)
    std::map<std::string, double> snapshot;  // copy_key -> the value the job read before writing (the first snapshot = the game default)
    double micros = 0;
    int reads = 0;
};
// Re-locate every knob, the plan's old values still in place, G5 again, every copy of each knob in the plan (copies go together),
// snapshot, write, read every knob back; a mismatch puts EVERY knob back from the snapshot (and reads that back). Never throws. `cancel`
// set: nothing is written. owner_check: a copy is written only while it holds FC 27's own value (Knob::game_value) or the value Turbo
// wrote last (last_written, by copy_key); a copy whose FC 27 value the knobs file does not give is not written at all
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
// The undo plan: knobs whose every copy still holds Turbo's value get their old values back (copies go together: one copy changed by
// someone else leaves the whole knob alone); the others are listed in `skipped`. Refused (why) across career loads, when nothing is
// written in place or nothing can be put back
std::string make_undo_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const UndoRecord& u, const std::string& session,
                           long long load_gen, Plan& out, std::vector<std::string>& skipped);
// The Restore game defaults plan: every knob with a game default for each of its copies (the first snapshot of this game session, by
// copy_key) that is located and of which a copy holds another value gets every copy's default back (copies go together). `only` (not
// empty): that knob only (the row's Reset). "" + plan, else why
std::string make_restore_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const std::map<std::string, double>& defaults, Plan& out,
                              std::vector<std::string>& skipped, const std::string& only = std::string());

// ------------------------------------------------------------------------------------------------ the profile (tuning_profile.json)
// {"on": true|false, "preset": "<name>|custom", "values": {"<knob id>": <value>}}: what the automatic moments apply (on), written by the
// UI only (Apply, a preset, the on switch, Restore FC 27 defaults). Keys starting with "_" are comments
struct Profile {
    bool on = false;
    std::string preset = "custom";
    std::map<std::string, double> values;
    std::string to_json() const;
    static bool parse(const std::string& text, Profile& out, std::string* err);
};
constexpr uintmax_t kMaxProfileBytes = 64 * 1024;  // a bigger profile file is not used (read every 2 s on the render thread)
bool load_profile(const std::filesystem::path& p, Profile& out, std::string* err);  // false when absent (err "") or unreadable / too big
bool save_profile(const std::filesystem::path& p, const Profile& pr, std::string* err);
// The plan of an automatic moment: every profile value whose knob is in the registry, located, writable and allowed at that moment
// (`when`), its value in range: EVERY copy of each, also a copy that already holds the value (the job writes it again and reads every
// one back, so "read back ok" covers the whole profile). The others are skipped (listed). Kind Reapply, the moment set
std::string make_profile_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const Profile& p, const std::string& moment,
                              Plan& out, std::vector<std::string>& skipped);
// Restore FC 27 defaults: FC 27's own value (the knobs file's default / copy_defaults) back into every copy of every located knob of
// which a copy holds another value; a knob without FC 27's value of a copy is skipped. Kind Restore
std::string make_vanilla_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, Plan& out, std::vector<std::string>& skipped);

// ------------------------------------------------------------------------------------------------ the automatic moments
enum class Moment { None, Boot, CareerLoad, Hub };
const char* moment_name(Moment m);  // "" "boot" "career_load" "hub"
// A hub write waits this long after Lua's last prematch event (hub -> prematch -> hub 0.5 s -> match at kick-off)
constexpr long long kPrematchGuardSeconds = 5;
// "" when no prematch was seen in the last kPrematchGuardSeconds (prematch_t: Lua's os.time() of it, 0 = none; wall: now)
std::string prematch_guard(long long wall, long long prematch_t);
struct MomentInput {
    // A WALL clock in seconds with sub-second resolution (the view: std::chrono::system_clock). Never the GUI clock App::now: that is
    // QueryPerformanceCounter, and in game it ran about 3x wall time on 10-13 (a "5 s" wait was ~1.7 s real); the 5 s prematch window
    // must be 5 real seconds
    double now = 0;
    long long wall = 0;          // os time (seconds): Lua's prematch_t is os.time()
    bool bridge = false, in_cm = false;
    std::string phase, session;
    long long load_gen = -1;
    long long prematch_t = 0;
    bool profile_on = false;     // the profile says on and tuning_profile_off.txt is absent
    long long profile_rev = 0;   // changes when the profile file changes (a change in the hub applies it there)
    bool ready = false;          // LocateStatus::ready
    bool busy = false;           // a tuning job is queued or running
};
// Which moment is due (pure logic; the view's tick feeds it every frame). boot: once per game session, when ready and no career is
// loaded (a career seen first skips it: its career_load covers it). career_load: once per Lua session and career load, in its hub.
// hub: every entry into the hub (and right after a career_load, and when the profile changes or is switched on in the hub), never
// within kPrematchGuardSeconds of a prematch phase (Lua's stamp and the tracker's own sight). Nothing while the profile is off, the
// locate is not ready or a job is busy. fired() after the moment's job was queued (or found nothing to do)
class MomentTracker {
public:
    Moment next(const MomentInput& in);
    void fired(Moment m, const MomentInput& in);
    bool boot_done() const { return boot_done_; }
    bool hub_pending() const { return hub_pending_; }
    const std::string& load_done() const { return load_done_; }

private:
    void observe(const MomentInput& in);
    bool boot_done_ = false, hub_pending_ = false, was_on_ = false, seen_ = false;
    std::string load_done_, last_phase_;
    long long last_rev_ = 0;
    double prematch_seen_ = -1e18;
};
// The job's own gate of a moment (re-checked on the game thread): boot = no career loaded; career_load / hub = the hub, no prematch in the
// last kPrematchGuardSeconds, no save in the last kSaveQuietSeconds; every knob of the plan allows the moment. "" = pass
std::string moment_gate(const std::string& moment, bool in_cm, const std::string& phase, long long prematch_age, long long save_age,
                        const std::vector<const Knob*>& knobs);

// ------------------------------------------------------------------------------------------------ presets (turbo_output\tuning_presets)
struct PresetChange {
    std::string knob;
    bool relative = true;  // value = game default x (1 + pct / 100)
    double pct = 0, value = 0;
};
struct Preset {
    std::string id, name, note, author;  // author "Turbo" = Turbo's own (read-only in the view)
    bool fc27 = false;                   // "fc27": true = every knob at FC 27's own value first, the changes on top
    std::vector<PresetChange> changes;
    std::filesystem::path file;          // where it was read from
    bool turbo() const { return author == "Turbo"; }
    std::string to_json() const;
    static bool parse(const std::string& text, Preset& out, std::string* err);
};
// Turbo's own presets ("FC 27 default", "Turbo compact", "Turbo expansive"): FC 27's values and relative changes from them only;
// Turbo's own generic numbers, no mod's value table
const std::vector<Preset>& builtin_presets();
std::string preset_file_name(const Preset& p);  // "turbo_high_line_press.json" (Turbo's own), "<safe name>.json"
// Writes Turbo's own presets into dir when a file is missing (an existing file is never overwritten). Returns how many were written
int ensure_builtin_presets(const std::filesystem::path& dir, std::string* err);
// Every *.json preset in dir, by name; a file that does not parse is listed in `errors`. Turbo's own come first
std::vector<Preset> load_presets(const std::filesystem::path& dir, std::vector<std::string>* errors);
// The values a preset gives: a relative change from the knob's base (base_of: its game default; false = not known), clamped to the
// knob's range and made representable; an absolute value must pass check_value; "fc27": every knob with FC 27's value and a range at
// that value. Knobs not in the registry, without a base or a range, switches with a relative change, and (usable given) knobs it refuses
// ("not proven yet"): skipped (listed)
struct PresetValues {
    std::map<std::string, double> values;
    std::vector<std::string> skipped;
};
PresetValues preset_values(const Preset& p, const Registry& reg, const std::function<bool(const std::string&, double&)>& base_of,
                           const std::function<std::string(const Knob&)>& usable = nullptr);
// A preset from the knobs that differ from their base: a relative change (rounded to 0.1 %) when the base is not 0, else the value
Preset preset_from_values(const std::string& name, const Registry& reg, const std::map<std::string, double>& values,
                          const std::function<bool(const std::string&, double&)>& base_of);

// ------------------------------------------------------------------------------------------------ the service the view uses
struct Context {
    std::string session;
    long long load_gen = -1;
    std::string phase;   // Lua's match phase
    bool in_cm = false;
    long long prematch_t = 0, save_t = 0;  // Lua's os.time() of its last prematch event / save start (0 = none)
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
    virtual LocateStatus locate_status() = 0;                // advances the locate (cheap); the automatic moments wait for ready
    virtual void set_session_opt_in(bool on) = 0;            // the tab's tick: writes from the view allowed in this game session
    virtual bool session_opt_in() = 0;
};

// What the host gives the platform-independent service (src/win/tuning_win.cpp; tests give a simulated table)
struct Host {
    Memory* mem = nullptr;
    std::string build;                           // the running game's build key
    std::filesystem::path knobs_dir;             // <Live Editor>\turbo\tuning
    std::filesystem::path out_dir;               // turbo_output: the switch files are looked at again right before a job is queued
    Lookup lookup;
    std::function<std::string(std::function<void()>)> run_on_game_thread;  // "" = not queued
    std::function<void(std::function<void()>)> run_background;            // inline-pattern scans, the chain locate (tests: inline)
    std::function<void(const std::string&)> log;
    std::function<double()> clock_us;
    std::function<long long()> wall_s;           // os time in seconds (Lua's stamps); null = std::time
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
    LocateStatus locate_status() override;
    void set_session_opt_in(bool on) override;
    bool session_opt_in() override;
    std::map<std::string, double> last_written();  // tests: what Turbo wrote last, by copy_key
    AttribDbs databases();                         // what the chain locate's G walk found (tests, the status line)

private:
    void load_locked();       // reads the knobs file (mu_ held)
    void ensure_anchors();    // resolves every locator's anchor once (inline patterns, the attribulator in the background)
    bool chain_mode_locked(const Registry& reg);  // the registry has an attrib_manager and the signature resolves (mu_ held)
    // runs a background locate job; an exception switches tuning off for the session (logged), never escapes the thread
    void guard_locate(long long epoch, const Registry& reg, const std::function<void()>& fn);
    void log(const std::string& s);
    double now() const { return h_.clock_us ? h_.clock_us() : 0.0; }
    long long wall() const;

    Host h_;
    std::mutex mu_;
    std::shared_ptr<const Registry> reg_;
    std::string reg_why_;
    AnchorMap anchors_;
    std::map<std::string, double> anchor_failed_at_;  // clock_us of a slow anchor's last failed search
    std::map<std::string, int> anchor_failures_;      // failed searches in a row (the retry backs off)
    long long anchors_epoch_ = 0;  // bumps on reload: a scan of an older registry does not fill the new map
    // the chain locate (WP-A1): 0 = due (at chain_next_), 1 = running in the background, 2 = done for this registry
    int chain_stage_ = 0;
    double chain_next_ = 0, chain_started_ = 0;
    int chain_failures_ = 0;
    std::string chain_why_, chain_logged_why_, fallback_logged_;
    AttribDbs dbs_;
    std::set<std::string> refused_;  // attrib locators refused for this game session (identity mismatch, the cross-check disagreed)
    std::string locate_failed_;      // a background locate threw: tuning off for the session
    std::vector<ApplyResult> results_;
    bool busy_ = false;
    double busy_since_ = 0;
    int job_seq_ = 0;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::string session_off_;
    bool session_opt_in_ = false;
    std::map<std::string, double> defaults_;
    std::map<std::string, double> last_written_;
    std::string cur_phase_, cur_session_;
    long long cur_gen_ = -1, cur_prematch_t_ = 0, cur_save_t_ = 0;
    bool cur_in_cm_ = false;
};

}  // namespace tuning
}  // namespace turbo
