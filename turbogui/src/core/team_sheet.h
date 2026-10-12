// FC 27 LE Turbo GUI - tactics channel E: your club's LIVE tactic record (the game's GameServices::CachedTeamSheetService),
// read and written in place on the game thread. Platform-independent part; src/win/team_sheet_win.cpp gives it the game's memory,
// the signatures, Turbo's game-thread dispatcher and the game_tick listener of the kick-off check.
//
// Facts: turbo_output\TACTICS_CHANNEL_E_REPORT.txt (live probe 2026-10-11 on FC27.exe build 6AC07E31-2145C000, read-only), sections 2-4,
// and turbo_output\TACTICS_LAYER1_FINDINGS_20261012.txt (the game's own Edit Tactic save decoded, 2026-10-12): which record fields Edit
// Tactic writes and the Edit Tactic working copies inside the service.
// Tags used below: [V] verified live on that build (or in its code bytes), [A] assessed, [U] unknown. Anything [U] is refused, never
// guessed: an enum value without a label (Build-Up Style 2 and 3), presets and formations (not written), the meaning of the undecoded
// record and slot fields (never touched).
//
// The chain (report 2.5-2.9 and findings 1-2, all [V]):
//   service svc (0x1418 bytes): [svc] = VT1, [svc+8] = VT2, u8 +0x1404 loaded, i32 +0x13A0 last team / +0x13A4 active sheet id (the
//     game's cache, never written by Turbo), eastl::hash_map<int team, eastl::vector<Sheet>> at +0x13B0: +0x13B8 bucket array,
//     u32 +0x13C0 bucket count, u32 +0x13C4 node count; node {+0 i32 key, +8 begin, +0x10 end, +0x28 next}, bucket = team % count.
//     Inline 0xF8-byte tactic records at svc+0xF80 / +0x1078 / +0x1170 / +0x1268: Edit Tactic's working copies (+0xF80 the edited
//     one, +0x1170 the one before, the other two empty templates; 2026-10-12 svc dump). Edit Tactic reads and writes these, not the map
//     record; leaving Team Management asks "save your changes to your team sheet?" and Yes copies the working copy into the map record
//     AND AUTOSAVES THE CAREER.
//   sheet element E (stride 0x290): +0 teamsheetid, +4 teamid, u8 +0xAC current flag, +0xC4 52 player ids, +0x1B0/+0x1B8 tactic
//     records, i32 +0x280 slot count (11), u8 +0x288 DIRTY, i32 +0x28C mode
//   record R (stride 0xF8): +0 name (EASTL string), +0x68 tactic code (12 printable chars + NUL; Edit Tactic's save makes a new one),
//     u8 +0x80 activetactic, +0x84 presetid, +0xA4 LINE HEIGHT (1..100), +0xB0 BUILD-UP STYLE, +0xB4 DEFENSIVE APPROACH,
//     +0xC0 formationfullnameid, +0xC4 / +0xD0 sourceformationid, +0xC8 formationaudioid, +0xD8 / +0xE0 the 11 slots
//   slot S (stride 0x48): +0 pid, +4 position, f32 +8 / +0xC x / y, i32 +0x30 role word = group << 12 | roleId << 6 | focus with the
//     mirrors +0x34 roleId and +0x38 focus (bits 3..5 of the word 0, verified on 11 of 11 slots)
//
// What Turbo does (report section 4): a GUARDED IN-PLACE WRITE, not a SetSheet call. For your sheet (mode -1, report 2.7) SetSheet is
// UpdateSheet = deep copy + dirty = 1 + the cache, so writing the scalar fields and setting dirty = 1 reaches the same end state without
// allocating anything. Written: Defensive Approach, Line Height and Build-Up Style together, per slot the three role values together,
// then the dirty flag; in the SAME job the same fields into every Edit Tactic working copy whose code is the map record's (none: map
// only, Edit Tactic shows its own copy until the next career load). Never: formation ids, positions, offsets, names, codes, takers,
// presets, the service cache, any game function (Turbo never triggers the team-sheet prompt or a save). Every field is read back in the
// same game-thread job; a mismatch puts every field and the dirty flag back from the snapshot taken in that job and switches channel E
// off for the session.
//
// Gates (report 4.3): G1 opt-in turbo_output\call_team_sheet_on.txt, no call_team_sheet_off.txt / tactics_off.txt; G2 career hub only
// (Lua's match phase "hub" only, Team Management not open; no save started in the last kSaveQuietSeconds; the located service belongs to
// this career load); G3 the exact build and every anchor consistent with the vtable; G4 the locate checks L1-L4; G5 SetSheet would not
// run vf 0xF8 for this sheet; G6 value ranges and known enum values (preset / formation refused).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "mem.h"
#include "memmap.h"

namespace turbo {
namespace tsheet {

// ------------------------------------------------------------------------------------------------ constants (report sections 2 / 3)
constexpr const char* kBuild = "6AC07E31-2145C000";  // the only build the layout was verified on [V]
constexpr uint32_t kServiceId = 0x113EA820;          // ENUM_djb2TeamSheetService_CLSS (Live Editor enums.lua) [V]
constexpr uint64_t kRegistryFindSlot = 0x38;         // registry vtable +0x38 = Find(registry, id): the call MCOPY makes [V-code]
// VT1 slots (report 2.2) [V]
constexpr int kVtActive = 23, kVtSet = 24, kVtUpdate = 182;
// service
constexpr uint64_t kSvcLastTeam = 0x13A0, kSvcActiveSheet = 0x13A4;  // the game's cache: read, never written
constexpr uint64_t kSvcBuckets = 0x13B8, kSvcBucketCount = 0x13C0, kSvcNodeCount = 0x13C4, kSvcLoaded = 0x1404;
constexpr uint64_t kSvcSize = 0x1418;
constexpr uint32_t kMaxBucketCount = 65521;  // L1: 1..65521 (887 seen, a prime)
// map node
constexpr uint64_t kNodeKey = 0x00, kNodeBegin = 0x08, kNodeEnd = 0x10, kNodeNext = 0x28;
constexpr int kMaxHops = 64;
// sheet element
constexpr uint64_t kSheetStride = 0x290;
constexpr uint64_t kSheetId = 0x000, kSheetTeam = 0x004, kSheetCurrent = 0x0AC, kSheetPids = 0x0C4, kSheetRecBegin = 0x1B0, kSheetRecEnd = 0x1B8,
                   kSheetSlotCount = 0x280, kSheetDirty = 0x288, kSheetMode = 0x28C;
constexpr int kSheetPidCount = 52, kMaxSheets = 32;
// tactic record (findings 1: the game's own Edit Tactic save changed exactly +0xA4 / +0xB0 / +0xB4 and the code) [V]
constexpr uint64_t kRecStride = 0xF8;
constexpr uint64_t kRecName = 0x00, kRecNameSize = 0x08, kRecCode = 0x68, kRecActive = 0x80, kRecPreset = 0x84, kRecLineHeight = 0xA4,
                   kRecBuildUp = 0xB0, kRecApproach = 0xB4, kRecFormation = 0xC0, kRecSourceFormation = 0xC4, kRecAudio = 0xC8,
                   kRecSourceFormation2 = 0xD0, kRecSlotBegin = 0xD8, kRecSlotEnd = 0xE0;
constexpr int kCodeLen = 12;  // the tactic code: 12 printable characters then NUL (both codes seen on 2026-10-12)
constexpr int kMaxRecords = 8;
// Edit Tactic's working copies: inline records in the service (2026-10-12 dump of svc+0xB00..+0x1400) [V]
constexpr std::array<uint64_t, 4> kWorkCopyOffsets = {0xF80, 0x1078, 0x1170, 0x1268};
// slot
constexpr uint64_t kSlotStride = 0x48;
constexpr uint64_t kSlotPid = 0x00, kSlotPosition = 0x04, kSlotX = 0x08, kSlotY = 0x0C, kSlotRole = 0x30, kSlotRoleId = 0x34, kSlotFocus = 0x38;
constexpr int kSlots = 11;
// value ranges (G6): the career schema's cm_mentalities ranges (turbo\le27\fc27_db_schema.json: buildupplay 0..3, defensivedepth 1..100),
// the 4 approaches of the game's text (IQ_DEFENSIVE_0..3). A non-undo write needs a KNOWN enum value besides (build_up_styles(),
// defensive_approaches()); an undo writes back only what the game held (the write history)
constexpr int kBuildUpMin = 0, kBuildUpMax = 3, kLineMin = 1, kLineMax = 100, kApproachMin = 0, kApproachMax = 3;
constexpr int kRoleIdMax = 63, kFocusMax = 7;
constexpr int kRoleWordMax = 60000;  // cm_mentalities.pos{n}role max: a larger word could not be saved
// G2: a save started (Lua saw PREPARE_FOR_SAVE) less than this long ago refuses a write
constexpr long long kSaveQuietSeconds = 30;
// A job the game thread runs later than this after Apply writes nothing (the hub may have been left meanwhile)
constexpr double kJobDeadlineUs = 2e6;
// A job not back after this long was dropped by the dispatcher (its queue keeps 256 jobs): channel E stops waiting for it (if it still
// runs, it is past kJobDeadlineUs and writes nothing)
constexpr double kJobLostUs = 10e6;
// The vtable-pair scan runs once per career load; after a failed locate the next try waits this long (or "Locate again"); a Find job the
// game thread never ran counts as failed after kLocateLostUs
constexpr double kLocateRetryUs = 60e6, kLocateLostUs = 90e6;
// A service the vtable-pair scan picked becomes "located" only when it passes check_service again this long after the scan (a freed copy
// keeps its header for a while, report 2.5); a scan that ran out of time or reached kScanMaxHits hits locates nothing
constexpr double kScanConfirmUs = 5e6;
constexpr size_t kScanMaxHits = 16;
// The role allow-list walk (every cached team's sheets): at most this many map nodes (64 hops a bucket, kMaxSheets a node), this long
constexpr int kAllowMaxNodes = 20000;
constexpr double kAllowBudgetUs = 10e6;
// match-setup copy (report 2.12) [V]: roles at out+0x118, pids at out+0x168, team id at out+0x28
constexpr uint64_t kCopyRoles = 0x118, kCopyPids = 0x168, kCopyTeam = 0x28;
// The kick-off check (read-only): 20 s after Lua's phase became "match" (once more 30 s later when no copy was found), on Turbo's game_tick
// listener; each match-setup copy is dumped from out+0x00: the 0x1A0 bytes of the known layout and the 0x200 bytes after them
constexpr double kKickoffDelayUs = 20e6, kKickoffRetryUs = 30e6;
constexpr size_t kKickoffHead = 0x1A0, kKickoffTail = 0x200, kKickoffMaxCopies = 4;
inline const char* kickoff_dump_file() { return "channel_e_kickoff_dump.bin"; }
inline const char* kickoff_off_file() { return "channel_e_kickoff_off.txt"; }  // no kick-off check (it needs the opt-in besides)
inline const char* kickoff_index_file() { return "channel_e_kickoff_dump.txt"; }

// ------------------------------------------------------------------------------------------------ the game's words (Edit Tactic)
// A value of Build-Up Style (record +0xB0, cm_mentalities.buildupplay) or Defensive Approach (record +0xB4, no cm_mentalities column).
// seen = shown by Edit Tactic for this value on 6AC07E31 (2026-10-12); a value missing from the list is unknown and never written.
struct EnumLabel {
    int value;
    const char* label;        // the game's word ("Counter")
    bool seen;
    int default_line;         // Defensive Approach: the Line Height Edit Tactic puts in when you pick it (-1 = not known)
    bool default_line_seen;   // that default was seen in Edit Tactic (else EA's notes)
    const char* source;
};
// Balanced 0, Counter 1 (both seen). Short Passing exists (IQ_BUILDUP_0..2, EA's notes, Live Editor's label list) but its stored value is
// 2 or 3 (the match's map of the stored value treats 2 like 0): refused until decoded.
const std::vector<EnumLabel>& build_up_styles();
// Deep 0, Balanced 1 (both seen), High 2, Aggressive 3: the order of the game's band code (minDepthThresholdFor...DefensiveStyle,
// IQ_DEFENSIVE_0..3) and EA's list; defaults 25 (seen at load), 50 (seen), 70 and 95 (EA)
const std::vector<EnumLabel>& defensive_approaches();
const EnumLabel* build_up_style(int value);       // nullptr = unknown
const EnumLabel* defensive_approach(int value);
std::string build_up_text(int value);              // "Counter (1)" / "unknown value 3"
std::string approach_text(int value);

// The role word (report 2.9) [V]
inline int32_t role_word(int group, int role_id, int focus) { return int32_t((group << 12) | (role_id << 6) | focus); }
inline int role_group(int32_t w) { return int(uint32_t(w) >> 12); }
inline int role_id_of(int32_t w) { return int((uint32_t(w) >> 6) & 0x3F); }
inline int focus_of(int32_t w) { return int(uint32_t(w) & 7); }
inline bool role_word_plain(int32_t w) { return w >= 0 && (w & 0x38) == 0; }  // bits 3..5 zero: the encoding seen on every slot
// The Line Height band the game's code maps a line height to (<=30 / 31-60 / 61-90 / >90, research [2].5 [V-code, old build]): the band
// of the approach of the same number (EA: Deep 1-30, Balanced 31-60, High 61-90, Aggressive 91-100)
int line_band(int line);
std::string line_band_text(int line);  // "band 0 (1-30)"

// ------------------------------------------------------------------------------------------------ files in turbo_output
inline const char* opt_in_file() { return "call_team_sheet_on.txt"; }
inline const char* kill_switch_file() { return "call_team_sheet_off.txt"; }
inline const char* master_switch_file() { return "tactics_off.txt"; }
inline const char* find_off_file() { return "call_team_sheet_find_off.txt"; }        // registry Find not called: the scan only
inline const char* mirror_off_file() { return "channel_e_db_mirror_off.txt"; }      // no cm_mentalities mirror
inline const char* mirror_on_file() { return "channel_e_db_mirror_on.txt"; }        // the cm_mentalities mirror is opt-in in 2.2
inline const char* db_off_file() { return "tactics_db_off.txt"; }                   // the database rows channel (stops the mirror too)
inline const char* undo_file() { return "channel_e_undo.json"; }

struct Switches {
    bool opt_in = false, off = false, master = false, find_off = false, mirror_off = false, db_off = false, mirror_on = false;
    // the kill switch that blocks a write ("" = none)
    std::string blocking() const;
    // why the cm_mentalities mirror (and its undo) does not run ("" = it may): not opted in, or one of its kill switches
    std::string mirror_block() const;
};
Switches read_switches(const std::filesystem::path& turbo_output);

// ------------------------------------------------------------------------------------------------ anchors (G3)
// Addresses from the signature table (core/sigscan.cpp, build 6AC07E31-2145C000 only)
struct Anchors {
    std::string build;
    uint64_t vt1 = 0, vt2 = 0;                  // teamsheet_vt1 / teamsheet_vt2
    uint64_t set = 0, set_body = 0;             // teamsheet_set, its layout guard teamsheet_set_body (same address)
    uint64_t update = 0, update_body = 0;       // teamsheet_update / teamsheet_update_body
    uint64_t active = 0, active_body = 0;       // teamsheet_active / teamsheet_active_body
    uint64_t registry = 0, registry_this = 0;   // service_registry (mov rax,[rip]) / service_registry_this (lea rcx,[rip])
    uint64_t image_base = 0, image_size = 0;    // FC27.exe (0 size = no range check: tests)
    const char* missing() const;                // the first signature that is not there
};
// The signature names, in the order Anchors lists them
const std::vector<const char*>& anchor_signatures();
// "" when the build is the verified one, every anchor is there, each layout guard sits where its function is, the registry's two
// operands agree and VT1[23] / [24] / [182] are GetActiveSheetId / SetSheet / UpdateSheet; else the first problem
std::string check_anchors(Memory& mem, const Anchors& a);
bool in_image(const Anchors& a, uint64_t addr);
// The registry's Find for the preferred locate path: [registry] = its vtable, [vtable + 0x38] = Find, both inside FC27.exe; 0 + why when not
uint64_t registry_find_fn(Memory& mem, const Anchors& a, std::string& why);

// ------------------------------------------------------------------------------------------------ locate (L1 - L4)
struct ServiceHeader {
    uint8_t loaded = 0;
    uint32_t bucket_count = 0, node_count = 0;
    uint64_t buckets = 0;
    int32_t last_team = 0, active_sheet = 0;
};
// L1: [svc] == VT1, [svc+8] == VT2, loaded == 1, bucket count 1..65521, node count 1..4 * bucket count, a bucket array. A VT-pair hit alone
// is not enough (report 2.5: a freed copy kept the pair for a while). "" = a live service
std::string check_service(Memory& mem, uint64_t svc, const Anchors& a, ServiceHeader* out = nullptr);
// The 8-aligned positions of the 16 bytes VT1 || VT2 in buf (whose first byte is at `base`), at most max_hits
std::vector<uint64_t> find_vtable_pairs(const uint8_t* buf, size_t len, uint64_t base, uint64_t vt1, uint64_t vt2, size_t max_hits);
// Exactly one of the scan hits passes check_service: "" and svc set; else why (none / more than one)
std::string pick_service(Memory& mem, const std::vector<uint64_t>& hits, const Anchors& a, uint64_t& svc);
// What the registry Find returned: the service itself or its second base (+8); "" and svc set when it passes check_service
std::string accept_find_result(Memory& mem, uint64_t returned, const Anchors& a, uint64_t& svc);
// L2's user: PSM = the career's manager slot 87 (count 1), [PSM] == psm_vtable, [PSM+8] == the manager table (its back pointer, as
// core/squad_role.cpp checks it), user club = i32 [PSM+0x10]
std::string user_from_psm(Memory& mem, uint64_t managers, uint64_t psm_vtable, int& user);

struct Located {
    uint64_t svc = 0, node = 0, vec_begin = 0, vec_end = 0, sheet = 0, rec_begin = 0, record = 0, slots = 0;
    int user = 0, sheets = 0, records = 0, record_index = -1, hops = 0;
    ServiceHeader header;
};
// L1-L4 (report 4.2). Fails closed: "layout mismatch (<check>)"
std::string locate(Memory& mem, uint64_t svc, int user, const Anchors& a, Located& out);

// ------------------------------------------------------------------------------------------------ the read-out
struct SlotView {
    int32_t pid = -1, position = -1, role = 0, role_id = 0, focus = 0;
    float x = 0.0f, y = 0.0f;
    int group() const { return role_group(role); }
};
// One of Edit Tactic's working copies (an inline record of the service, findings 2)
struct WorkCopy {
    uint64_t offset = 0, addr = 0, slots = 0;  // svc + offset; its slot vector
    std::string why;      // "" = a tactic record with a sane header: code, active flag, the map record's formation ids, 11 sane slots
    std::string code;     // its tactic code ("" when there is none: an empty template)
    bool matches = false; // why is "" and the code is the map record's: Turbo writes it together with the map record
    int32_t line_height = 0, build_up = 0, approach = 0;
    std::array<int32_t, kSlots> pids{}, roles{}, role_ids{}, focuses{};
    std::string label() const;  // "svc+0xF80"
};
struct LiveTactic {
    Located at;
    std::string g5;  // CoreService::read: check_g5 of this read ("" = pass)
    int32_t sheet_id = 0, mode = 0;
    uint8_t dirty = 0;
    std::string name;  // the record's name ("433"; "" when it could not be read: display only)
    std::string code;  // the record's tactic code ("" when it is not 12 printable characters: no working copy can match)
    int32_t preset = 0, line_height = 0, build_up = 0, approach = 0, formation = 0, source_formation = 0, audio = 0;
    std::array<SlotView, kSlots> slots{};
    std::vector<int32_t> squad;  // the sheet's player ids (not -1)
    std::vector<WorkCopy> copies;  // the four inline records of the service, in kWorkCopyOffsets order
    const WorkCopy* copy(uint64_t offset) const;  // nullptr = no such offset
    int matching_copies() const;
};
// The 12-character tactic code at rec + 0x68 ("" and why when it is not 12 printable characters followed by NUL)
std::string read_code(Memory& mem, uint64_t rec, std::string* why = nullptr);
// The inline records of the service, each checked against the map record (code, active flag, formation ids, slot vector). Read-only
std::vector<WorkCopy> read_work_copies(Memory& mem, uint64_t svc, const LiveTactic& map);
// locate + every field the UI shows, the working copies included. "" = read
std::string read_live(Memory& mem, uint64_t svc, int user, const Anchors& a, LiveTactic& out);

// G5 (report 4.3): the id GetActiveSheetId(mode, team) would return, by the same loop (first element of map[team] with +0x28C == mode
// and +0xAC != 0; -1 when none). Turbo walks it itself: the game's function would write the service cache. false = a sheet could not be
// read (G5 then refuses)
bool active_sheet_id(Memory& mem, const Located& at, int32_t mode, int32_t& id);
// "" when SetSheet would not run vf 0xF8 for this sheet either (mode -1, or the sheet is not the active one of its mode)
std::string check_g5(Memory& mem, const LiveTactic& lt);

// ------------------------------------------------------------------------------------------------ the role allow-list (G6)
// The (group, role id, focus) triples the game itself uses: every slot of every record (sourceformationid > 0) of every club's sheet in the
// service's map (841 clubs on 2026-10-11), read-only, a plain word whose mirrors +0x34 / +0x38 agree, a player in the slot. A role / focus
// edit is written only when its triple is in this list; without the list (not built yet, the walk failed) role and focus are not editable.
struct RoleAllowList {
    bool ok = false;
    std::string why;           // why it is not built ("" when ok)
    std::set<int32_t> words;   // the triples as role words
    int teams = 0, sheets = 0, records = 0, slots = 0, skipped = 0;
    // evidence only (never a gate): the Build-Up Style / Defensive Approach values of the same records, and the line heights per approach
    std::map<int, int> build_up_count, approach_count;
    std::map<int, std::pair<int, int>> approach_lines;  // approach -> (lowest, highest) line height
    bool has(int32_t word) const { return ok && words.count(word) != 0; }
    std::vector<int> role_ids(int group) const;              // the role ids seen in a group, ascending
    std::vector<int> focuses(int group, int role_id) const;  // the focuses seen with that role
    std::string summary() const;                             // "412 triples from 841 teams"
    std::string values_summary() const;                      // "approach 0 x412 (line 10-30), ...; build-up 0 x500, ..."
};
// Walks the whole map through a page cache (bounded: kAllowMaxNodes nodes, 64 hops a bucket, kAllowBudgetUs with a clock). Fails (ok
// false) on a bad service header, a walk that did not finish or no triple at all; a club whose sheet does not look right is skipped.
// exclude_team (your club) is left out: its sheet may hold values Turbo wrote.
RoleAllowList build_role_allow_list(Memory& mem, uint64_t svc, const Anchors& a, int exclude_team, const std::function<double()>& clock_us = nullptr);

// ------------------------------------------------------------------------------------------------ requests and plans (G6)
struct SlotChange {
    int slot = -1;
    int role_id = -1, focus = -1;  // -1 = keep
};
struct Request {
    int line_height = -1, build_up = -1, approach = -1;  // -1 = keep
    std::vector<SlotChange> slots;
    bool preset = false, formation = false;  // asked to change the preset / the formation: refused
};
enum class FieldKind { LineHeight, BuildUp, Approach, Role, RoleId, Focus };
// "lineheight" "buildupstyle" "defensiveapproach" "role" "roleid" "focus" (the undo file; 2.2's "defensivedepth" / "buildupplay" are read too)
const char* field_name(FieldKind k);
const char* field_label(FieldKind k);  // the game's words: "Line Height" "Build-Up Style" "Defensive Approach" "role word" "role id" "focus"
bool field_from_name(const std::string& s, FieldKind& k);
inline bool record_field(FieldKind k) { return k == FieldKind::LineHeight || k == FieldKind::BuildUp || k == FieldKind::Approach; }
struct FieldWrite {
    FieldKind kind = FieldKind::LineHeight;
    int slot = -1;  // -1 for the record fields
    int32_t old_value = 0, new_value = 0;
    uint64_t copy = 0;  // 0 = the map record; else the svc offset of the Edit Tactic working copy (kWorkCopyOffsets)
};
// The record a plan was made for: the job writes only when it still finds exactly this record with exactly these old values. A plan made
// from the editor holds map-record writes only; the job adds the working copies' writes itself. An undo plan names both.
struct Plan {
    int user = 0, record_index = -1;
    int32_t sheet_id = 0, formation = 0, source_formation = 0;
    std::string record_name, code;  // code: the record's tactic code when the plan was made (the job refuses another)
    std::array<int32_t, kSlots> pids{};
    std::vector<FieldWrite> writes;
    bool undo = false;  // an undo: the dirty flag stays 1 (the game may have committed other changes since)
    long long load_gen = -1;  // the career load the plan was made in (-1 = the submitting context's)
};
// G6 and the diff against the live record: "" and the plan (possibly empty: nothing changes), else the refusal. When Line Height, Build-Up
// Style or Defensive Approach changes, all three are in the plan (written together; the unchanged ones with their live value). A new
// enum value must be known (build_up_styles / defensive_approaches); a role / focus change needs its triple in `allow` (null or not built:
// refused)
std::string make_plan(const LiveTactic& now, const Request& req, Plan& out, const RoleAllowList* allow);

// The values the write jobs read from the game right before Turbo's first write of each field (map record and working copies), for one
// record in one career load. An undo may write back only these (never a value taken from the undo file alone).
struct WriteHistory {
    int user = 0, record_index = -1;
    long long load_gen = -1;
    std::map<std::tuple<uint64_t, int, int>, int32_t> original;  // (copy, FieldKind, slot) -> value
    void note(const Plan& p, const std::vector<FieldWrite>& written);  // a non-undo job whose writes were attempted
    void note(const Plan& p) { note(p, p.writes); }
    std::string check_undo(const Plan& p) const;  // "" when every value the undo writes is an original of this record and load
};
// What apply_plan checks besides the live record (a missing one refuses what needs it)
struct JobChecks {
    const RoleAllowList* allow = nullptr;      // a non-undo role write needs its triple here
    const WriteHistory* history = nullptr;     // an undo needs its values here
    const std::atomic<bool>* cancel = nullptr; // set: nothing is written
};
// The plan on its own against the live record: kinds, slots, ranges, known enum values (a non-undo write), the three role values of a slot
// together and consistent (word == role_word(live group, role id, focus)), the group unchanged; a non-undo role word in the allow-list; a
// working copy only in an undo and only while it still holds this tactic; an undo's values in the history. "" = it may be written
std::string check_plan(const LiveTactic& now, const Plan& plan, const JobChecks& chk);
// The address of a field inside the located record or a matching working copy (0 when the slot is out of range or the copy does not match)
uint64_t field_address(const LiveTactic& now, const FieldWrite& w);
// The same fields for every working copy that holds this tactic (WorkCopy::matches): the copy's own current values as old values, the plan's
// new values; a copy whose written slot holds another player or position group is left out (`note` says why). Only values that differ.
std::vector<FieldWrite> copy_writes(const LiveTactic& now, const Plan& plan, std::string& note);

// ------------------------------------------------------------------------------------------------ the write (game thread)
struct ApplyResult {
    bool ok = false;           // written and read back (or nothing to do)
    bool written = false;      // fields were changed in game memory and stayed
    bool undone = false;       // a read-back mismatched: everything was put back from the snapshot
    bool restore_failed = false;
    bool session_off = false;  // switch channel E off for the session (a mismatch)
    bool undo = false;         // the plan was an undo
    std::string stage, message;
    std::vector<FieldWrite> writes;  // what was written (old = the snapshot), the working copies' writes included
    std::string copies;              // what happened to Edit Tactic's working copies ("" = the plan wrote nothing a copy holds)
    int copies_written = 0;          // working copies written by this job
    uint8_t dirty_before = 0, dirty_after = 0;
    LiveTactic after;
    std::string log_line;  // "channel E: team 95 record 0 line height 25->90 approach 0->3 build-up 0->1; Edit Tactic copies ... (read back ok)"
    double micros = 0.0;   // time the job took (the host fills it)
    int reads = 0;         // reads of game memory the job made (whole pages for the walk, single fields for the read-back)
};
// Report 4.4: re-run L1-L4, G5, the plan's record, code and old values, the working copies that hold this tactic (copy_writes), snapshot,
// write all of them, dirty = 1, read back; a mismatch restores every field (map record and copies) and the dirty flag from the snapshot.
// The walks read the game through whole 4 KB pages (a handful of reads instead of several hundred: the job stays well under the report's
// 50 us); the read-back reads each written field from the game itself. Never throws.
ApplyResult apply_plan(Memory& mem, uint64_t svc, const Anchors& a, const Plan& plan, const JobChecks& chk = JobChecks());

// Reads served from whole 4 KB pages, each page read from the underlying memory once (at most 64 pages, then direct reads); a page that
// cannot be read falls back to a direct read of the asked bytes. A write goes straight through and drops every cached page.
class PageCache : public Memory {
public:
    explicit PageCache(Memory& m) : m_(m) {}
    bool read(uint64_t addr, void* out, size_t n) override;
    bool write(uint64_t addr, const void* in, size_t n) override;
    int reads() const { return reads_; }  // reads made on the underlying memory

private:
    struct Page {
        uint64_t base = 0;
        bool ok = false;
        std::vector<uint8_t> bytes;
    };
    const uint8_t* page(uint64_t base);
    Memory& m_;
    std::vector<Page> pages_;
    int reads_ = 0;
};
// "team 95 record 0 line height 25->90 build-up 0->1 approach 0->3 slot 9 role 38213->38214" (map record; unchanged fields left out), then
// "; Edit Tactic copies svc+0xF80, svc+0x1170: the same" when copies were written
std::string describe_writes(int user, int record_index, const std::vector<FieldWrite>& ws);

// ------------------------------------------------------------------------------------------------ the undo record (channel_e_undo.json)
struct MirrorField {
    std::string field;  // "defensivedepth", "buildupplay", "pos9role"
    int64_t old_value = 0, new_value = 0;
};
struct UndoRecord {
    std::string build, when, session, state;  // state: "pending" "written" "undone" "aborted" "restored" "failed"
    long long load_gen = -1;
    int team = 0, record_index = -1;
    std::string record_name, code;  // code: the map record's tactic code (the working copies are matched by it)
    std::string copies;             // what the job said about Edit Tactic's working copies
    int32_t sheet_id = 0, formation = 0, source_formation = 0;
    uint8_t dirty_before = 0;
    std::array<int32_t, kSlots> pids{};
    std::vector<FieldWrite> writes;
    // the cm_mentalities mirror (written on the GUI thread after the live write)
    std::string mirror_state;  // "" none, "written", "skipped", "failed", "restored"
    std::string mirror_note, mirror_table;
    int64_t mirror_row_key = -1;  // the row's artificialkey
    std::vector<MirrorField> mirror;
    std::string note;
    std::string to_json() const;
    static bool parse(const std::string& text, UndoRecord& out, std::string* err);
    static UndoRecord from_plan(const Plan& p);
};
bool load_undo(const std::filesystem::path& p, UndoRecord& out, std::string* err);  // false when absent or not an undo file
bool save_undo(const std::filesystem::path& p, const UndoRecord& u, std::string* err);
// An undo plan: the fields that still hold the value Turbo wrote get their old value back; a slot whose three role values do not all
// hold Turbo's values is left alone (listed in `skipped`); a working copy that no longer holds this tactic (another code, e.g. after an
// Edit Tactic save) is left alone too. "" and the plan, else why nothing can be put back
// Refused when the undo record is from another Lua session or career load (a reload throws the in-memory change away anyway)
std::string make_undo_plan(const LiveTactic& now, const UndoRecord& u, const std::string& session, long long load_gen, Plan& out,
                           std::vector<std::string>& skipped);
// "" when the undo record belongs to this Lua session and career load, else why it does not
std::string same_career(const UndoRecord& u, const std::string& session, long long load_gen);
// Report 4.5: fields Turbo wrote that no longer hold its value (Edit Tactic or the game wrote them since)
std::vector<std::string> reverted(const LiveTactic& now, const UndoRecord& u);
// A second Apply on the same record while the first one's values are still in place: the record keeps the values from before Turbo's
// FIRST write (a field the new plan writes again whose old value is the earlier write's new value gets the earlier old value; fields only
// the earlier write touched stay listed). Another club or record: `next` as it is.
// Not merged across Lua sessions or career loads (prev's session / load_gen against next's)
UndoRecord merge_undo(const UndoRecord& prev, const UndoRecord& next);
// After the job: its working-copy writes into the undo record (a copy written again keeps its value from before Turbo's first write)
void add_job_writes(UndoRecord& u, const std::vector<FieldWrite>& job);
// The cm_mentalities mirror's row check: the row's playerid0..10 / position0..10 must be the live slots', its code (when the save has the
// field and get_str can read it) the live record's code, and every field about to be written must hold the live OLD value. get(name,
// value) reads one int field of the row (false = the field is missing). "" = it may be written, else why it is skipped
std::string mirror_row_check(const std::function<bool(const std::string&, int64_t&)>& get, const LiveTactic& now,
                             const std::vector<FieldWrite>& writes,
                             const std::function<bool(const std::string&, std::string&)>& get_str = nullptr);
// The mirror's undo: the same switches as the mirror (sw.mirror_block) and the row (get reads its int fields) must still be the record's
// tactic: teamid == u.team, activetactic == 1, sourceformationid == u.source_formation. "" = it may be put back
std::string undo_mirror_check(const Switches& sw, const std::function<bool(const std::string&, int64_t&)>& get, const UndoRecord& u);
// The DB field a live write of the map record mirrors to: Build-Up Style -> "buildupplay", Line Height -> "defensivedepth" (the schema's
// 1..100), the role word -> "pos<slot>role"; "" for role id / focus (the word carries them), for the Defensive Approach (cm_mentalities
// has no column for it) and for every working-copy write
std::string mirror_field_name(const FieldWrite& w);
bool same_record(const UndoRecord& u, const LiveTactic& now);

// ------------------------------------------------------------------------------------------------ the kick-off check (read-only)
// The 44 bytes of the 11 role words in slot order (the pattern the match-setup copy carries at out+0x118)
std::vector<uint8_t> role_pattern(const std::array<int32_t, kSlots>& roles);
// Every 4-aligned position of the pattern in buf (first byte at `base`), at most max_hits
std::vector<uint64_t> find_bytes(const uint8_t* buf, size_t len, uint64_t base, const std::vector<uint8_t>& pat, size_t max_hits, size_t align);
// A hit is a match-setup copy (report 2.12) when the 11 pids sit 0x50 bytes later and the team id at hit - 0x118 + 0x28
bool is_match_copy(Memory& mem, uint64_t hit, int user, const std::array<int32_t, kSlots>& pids);
// What the kick-off check looks for: the live tactic as last read in the hub (or after Turbo's last job)
struct KickoffArm {
    bool armed = false;
    int user = 0;
    long long load_gen = -1;
    std::array<int32_t, kSlots> roles{}, pids{};
    int32_t line_height = 0, build_up = 0, approach = 0;
    std::string code, when;  // when: "read in the hub" / "after Turbo's write" (+ the clock)
    bool turbo_values = false;  // Turbo's last job wrote these values (and no undo since)
};
// One dumped match-setup copy: out = hit - 0x118, bytes = out+0x00 .. +0x1A0 + 0x200
struct KickoffCopy {
    uint64_t out = 0;
    std::vector<uint8_t> bytes;
};
// The 4-aligned offsets inside the dump where an i32 equals v, outside the known blocks (positions, x / y, the role words, the pids)
std::vector<size_t> kickoff_value_offsets(const std::vector<uint8_t>& bytes, int32_t v);
// The index file next to the dump: what was looked for, each copy's address, team, formation ids, roles, pids, and where the expected
// line height sits (offsets in every copy)
std::string kickoff_index(const KickoffArm& arm, const std::vector<KickoffCopy>& copies, size_t hits);

// ------------------------------------------------------------------------------------------------ gates
struct GateInput {
    // G1
    Switches sw;
    // G2
    bool bridge = false;              // Lua's bridge state is there
    bool in_cm = false;
    std::string match_phase;          // Lua's "hub" / "prematch" / "match" / "unknown"; "" = a Lua side without it (G2: "hub" only)
    bool team_mgmt = false;           // Lua saw ENTERING_TEAM_MANAGEMENT and no LEAVING_TEAM_MANAGEMENT since (G2: refused while open)
    long long save_age = -1;          // seconds since Lua saw a save start, -1 = none seen
    long long load_gen = -1, located_load_gen = -1;
    // G3 .. G6
    std::string anchors, locate, g5, plan;
    std::string session_off;          // a read-back mismatch switched channel E off for this session
    bool undo = false;                // an undo: G1's opt-in is not needed and the kill switches do not block it (it puts the game's values back)
};
struct Gate {
    std::string name, why;  // "G2 career hub", "" = pass
    bool pass() const { return why.empty(); }
};
struct Gates {
    std::vector<Gate> list;
    bool pass() const;
    std::string first() const;  // "G2 career hub: a match is being set up or played (Lua: prematch)"
};
Gates evaluate(const GateInput& in);

// ------------------------------------------------------------------------------------------------ the service the UI uses
struct Context {
    long long load_gen = -1;
    int user = 0;            // the bridge's user club
    uint64_t managers = 0;   // the career's manager table (bridge_state.json "managers"), for the PSM cross-check
    bool in_cm = false;
    bool allow_find = false; // the registry Find may be called (opt-in present, no kill switch, no call_team_sheet_find_off.txt)
    bool hub = false;        // G2 passes now as the UI sees it (the jobs on the game thread check the latest value)
    std::string phase;       // Lua's match phase as the UI sees it ("hub" / "prematch" / "match" / "unknown" / ""): the kick-off check
};

class Service {
public:
    virtual ~Service() = default;
    virtual std::string anchors_status() = 0;  // G3: "" = ready
    // L1-L4 and the read-out (read-only, the calling thread). Locates the service first when this career load has none yet (the
    // registry Find on the game thread, else a scan on a background thread): "locating ..." until then
    virtual bool read(const Context& ctx, LiveTactic& out, std::string& why) = 0;
    virtual std::string locate_status() = 0;   // "located 0x... (registry Find, load 3)" / "locating ..." / "not located: ..."
    virtual long long located_load_gen() = 0;  // the career load the located service belongs to (-1 = none)
    virtual void relocate() = 0;               // forget the located service (the next read finds it again)
    // The 4.4 job on the game thread. false + why when it cannot be queued
    virtual bool submit(const Context& ctx, const Plan& plan, std::string& why) = 0;
    virtual bool poll(ApplyResult& out) = 0;
    virtual bool busy() = 0;                   // a job is queued or running
    virtual std::string session_off() = 0;     // "" = on; else why channel E is off for this session
    // Report 4.5 / findings 3d: the kick-off check. Called on the game thread every frame (Turbo's game_tick listener) and from the UI's
    // tick as a fallback; cheap, never blocks (a try-lock). 20 s after Lua's phase became "match" (note_context) it looks for the
    // match-setup copy of the live tactic last read in the hub (read-only, background), dumps each copy to
    // turbo_output\channel_e_kickoff_dump.bin (+ .txt index) and logs the outcome either way; once more 30 s later when none was found
    virtual void kickoff_tick() = 0;
    virtual std::string kickoff_status() = 0;
    // The role allow-list of a career load (null until built), and its status line
    virtual std::shared_ptr<const RoleAllowList> role_allow(long long load_gen) = 0;
    virtual std::string allow_status() = 0;
    // The UI's latest view (every frame): the jobs compare their plan's career load and the hub flag with it
    virtual void note_context(const Context& ctx) = 0;
};

// What the host gives the platform-independent service (src/win/team_sheet_win.cpp; tests give a simulated game)
struct Host {
    Memory* mem = nullptr;
    Anchors anchors;
    uint64_t psm_vtable = 0;
    std::filesystem::path out_dir;  // turbo_output: the switch files are looked at again right before a job is queued
    // queue a job for the game thread ("" = no dispatcher: the job is dropped and submit refuses)
    std::function<std::string(std::function<void()>)> run_on_game_thread;
    // run a scan away from the game thread (tests: inline)
    std::function<void(std::function<void()>)> run_background;
    // the readable private regions to scan
    std::function<std::vector<Region>()> regions;
    // the registry's Find(registry, id) on the game thread (null = scan only)
    std::function<uint64_t(uint64_t fn, uint64_t registry, uint32_t id)> call_find;
    std::function<void(const std::string&)> log;
    std::function<double()> clock_us;  // microseconds (the job's duration)
    double scan_budget_s = 45.0;       // seconds per scan
};

class CoreService : public Service {
public:
    explicit CoreService(Host h);
    std::string anchors_status() override;
    bool read(const Context& ctx, LiveTactic& out, std::string& why) override;
    std::string locate_status() override;
    long long located_load_gen() override;
    void relocate() override;
    bool submit(const Context& ctx, const Plan& plan, std::string& why) override;
    bool poll(ApplyResult& out) override;
    bool busy() override;
    std::string session_off() override;
    void kickoff_tick() override;
    std::string kickoff_status() override;
    std::shared_ptr<const RoleAllowList> role_allow(long long load_gen) override;
    std::string allow_status() override;
    void note_context(const Context& ctx) override;

    // tests / status
    uint64_t located() ;
    int scans() const { return scans_; }
    int finds() const { return finds_; }

private:
    void start_locate(const Context& ctx);
    void finish_locate(long long gen, uint64_t svc, const std::string& how, const std::string& why);
    void scan_for_service(long long gen);
    void start_allow(long long gen, uint64_t svc);  // builds the role allow-list of a located service (background)
    void log(const std::string& s);
    double now() const { return h_.clock_us ? h_.clock_us() : 0.0; }

    Host h_;
    std::mutex mu_;
    std::string anchors_why_;
    bool anchors_checked_ = false;
    uint64_t svc_ = 0;
    long long svc_gen_ = -1;
    std::string how_, locate_why_;
    bool locating_ = false;
    long long locating_gen_ = -1;
    double locating_since_ = 0.0, failed_at_ = 0.0;  // clock_us of the running locate / of the last failed one
    long long failed_gen_ = -1;                       // the career load the last failed locate was for
    std::vector<ApplyResult> results_;
    bool busy_ = false;
    double busy_since_ = 0.0;
    int job_seq_ = 0;  // the job busy_ waits for (a lost job's late result does not clear a newer one)
    std::shared_ptr<std::atomic<bool>> cancel_;  // the queued job's cancel flag (set when it is given up or was not queued)
    std::string session_off_;
    std::string kickoff_;
    bool kickoff_running_ = false;
    // the kick-off check: what to look for (the last hub read / job), Lua's phase, when the check is due
    KickoffArm arm_;
    std::string phase_;              // the last phase note_context saw
    bool kick_pending_ = false;      // a check is due at kick_due_ (set when the phase became "match")
    double kick_due_ = 0.0;
    int kick_tries_ = 0;             // checks run for this match (a retry when the first found no copy)
    bool turbo_values_ = false;      // Turbo's last job wrote the live values (no undo since)
    void arm_from(const LiveTactic& lt, long long gen, const char* when);  // with mu_ held
    void run_kickoff(KickoffArm arm, int attempt);
    std::atomic<int> scans_{0}, finds_{0};
    // a service the scan picked, waiting for its kScanConfirmUs re-check
    uint64_t cand_svc_ = 0;
    long long cand_gen_ = -1;
    double cand_at_ = 0.0;
    // the role allow-list
    std::shared_ptr<const RoleAllowList> allow_;
    long long allow_gen_ = -1;
    bool allow_running_ = false;
    std::string allow_why_;
    // the originals the write jobs read (undo checks)
    WriteHistory history_;
    // the UI's latest context
    int user_ = 0;
    std::atomic<long long> cur_gen_{-1};
    std::atomic<bool> cur_hub_{false};
};

}  // namespace tsheet
}  // namespace turbo
