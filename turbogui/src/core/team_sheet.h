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
// allocating anything. Written: Defensive Approach, Line Height and Build-Up Style together (the approach band-consistent with the line
// height), per slot the three role values together, then the dirty flag. 2.4 (docs/TACTICS_OVERHAUL_DECISION.md 3.2): no Edit Tactic
// working copy is written any more (E2 refuted, R4; the copy machinery stays for X3's branches, JobChecks::copy_targets, empty by default).
// Never: formation ids, positions, offsets, names, codes, takers, presets, the service cache, any game function (Turbo never triggers the
// team-sheet prompt or a save). Every field is read back in the same game-thread job; a mismatch puts every field and the dirty flag back
// from the snapshot taken in that job and switches channel E off for the session.
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
#include "t3db.h"

namespace turbo {
namespace tsheet {

// ------------------------------------------------------------------------------------------------ constants (report sections 2 / 3)
constexpr const char* kBuild = "6AC07E31-2145C000";  // the only build the layout was verified on [V]
// X 2.0 (Cassio 11-01: "save the order of the bench and reserves once and for all"): on another build whose team sheet signatures are all
// found and whose guards pass (check_anchors), channel E READS (the bench probe: is the layout the same?) but never writes: apply_plan
// and apply_bench refuse until the build is verified in game and added here.
inline bool build_verified(const std::string& build) { return build == kBuild; }
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
// Edit Tactic's working copies: inline records in the service (2026-10-12 dump of svc+0xB00..+0x1400) [V]. Read only since 2.4: writing
// them did not change Edit Tactic's screen (R4, 2026-10-13 19:34); they are never a copy target of the service's jobs
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
// A service the vtable-pair scan picked becomes "located" only when it passes check_service again this long (WALL time, Host::wall_us) after
// the scan (a freed copy keeps its header for a while, report 2.5); a scan that ran out of time or reached kScanMaxHits hits locates nothing
constexpr double kScanConfirmUs = 5e6;
constexpr size_t kScanMaxHits = 16;
// The role allow-list walk (every cached team's sheets): at most this many map nodes (64 hops a bucket, kMaxSheets a node), this long
constexpr int kAllowMaxNodes = 20000;
constexpr double kAllowBudgetUs = 10e6;
// match-setup copy (report 2.12) [V]: roles at out+0x118, pids at out+0x168, team id at out+0x28, positions at out+0x30 (2026-10-12 dev
// read of both copies: 0856_match_setup_copies.txt)
constexpr uint64_t kCopyRoles = 0x118, kCopyPids = 0x168, kCopyTeam = 0x28, kCopyPositions = 0x30;
constexpr int kMaxPosition = 27;
// The raw bytes the kick-off line carries: the style block of the copy (T2: out+0x150..+0x167, record +0x88..+0xBF converted byte-wise)
constexpr uint64_t kCopyStyleWindow = 0x150, kCopyStyleWindowLen = 0x18;
// The kick-off check (read-only), on Turbo's game_tick listener: 20 s of WALL time (Host::wall_us; Turbo's QPC clock ran ~3x in game) after
// Lua's phase became "match", then every 15 s while no copy is found, at most kKickoffMaxLooks looks (about 80 s): a copy made late is
// still read, and only the last look says "no match copy found ... (look 5 of 5)". Each match-setup copy is dumped from out+0x00: the 0x1A0
// bytes of the known layout and the 0x200 bytes after them; the scan's budget is scan_budget_s of wall time
constexpr double kKickoffDelayUs = 20e6, kKickoffRetryUs = 15e6;
constexpr int kKickoffMaxLooks = 5;
constexpr size_t kKickoffHead = 0x1A0, kKickoffTail = 0x200, kKickoffMaxCopies = 4;
inline const char* kickoff_dump_file() { return "channel_e_kickoff_dump.bin"; }
inline const char* kickoff_off_file() { return "channel_e_kickoff_off.txt"; }  // no kick-off check (it needs the opt-in and no kill switch besides)
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
    bool writable;            // Turbo may write this value (2.4: Build-Up Style 2 / 3 and Defensive Approach 3 stay refused until X3's Q5)
};
// Balanced 0, Counter 1 (both seen). Short Passing exists (IQ_BUILDUP_0..2, EA's notes, Live Editor's label list) but its stored value is
// 2 or 3 (the match's map of the stored value treats 2 like 0): refused until decoded.
const std::vector<EnumLabel>& build_up_styles();
// Deep 0, Balanced 1 (both seen), High 2, Aggressive 3: the order of the game's band code (minDepthThresholdFor...DefensiveStyle,
// IQ_DEFENSIVE_0..3) and EA's list; defaults 25 (seen at load), 50 (seen), 70 and 95 (EA). High is written (the other clubs' records hold
// 2 with lines 65-90 [V 2026-10-13]); Aggressive (3) is never seen in any record and stays refused (decision 3.2, Q5)
const std::vector<EnumLabel>& defensive_approaches();
const EnumLabel* build_up_style(int value);       // nullptr = unknown
const EnumLabel* defensive_approach(int value);
bool build_up_writable(int value);                 // known and writable
bool approach_writable(int value);
// The highest Line Height Turbo writes: the top of the highest band whose Defensive Approach is writable (90 while Aggressive is refused).
// Turbo keeps the approach band-consistent with the line height (the game's CPU records are [V]), so a line in a refused band is refused too.
int max_writable_line();
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
// 2.4: the cm_mentalities row is part of the write (no opt-in any more); this kill switch leaves it out of every job (Apply and Undo)
inline const char* db_row_off_file() { return "channel_e_db_off.txt"; }
inline const char* db_row_off_legacy_file() { return "channel_e_db_mirror_off.txt"; }  // 2.3's name of the same switch, still honoured
inline const char* db_off_file() { return "tactics_db_off.txt"; }                      // the database rows channel (leaves the row out too)
inline const char* undo_file() { return "channel_e_undo.json"; }

struct Switches {
    bool opt_in = false, off = false, master = false, find_off = false, db_row_off = false, db_row_off_legacy = false, db_off = false;
    // the kill switch that blocks a write ("" = none)
    std::string blocking() const;
    // why a job leaves the cm_mentalities row out ("" = it is written with the live record): a database switch. An Apply is stopped
    // altogether by blocking(); an undo puts the row back unless a database switch says no (it never touches the database then)
    std::string db_row_block() const;
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
    // 2.20 probe (cause B, read-only): every record of the club's current sheet as L4 saw it (records_probe below); "" when L4 was not reached
    std::string records_probe;
};
// 2.20 probe (brief 2.20-T-tactics cause B): one line with the n records at rb (at most kMaxRecords), read only:
//   "5 record(s), 1 with act 1 and src > 0, 1 with act 1; #0 act 1 src 10/10 fmt 27 preset 1 lh 25 b77 3 name '433' raw7C 00000000..."
// act = +0x80, src = +0xC4 / +0xD0, fmt = +0xC0, preset = +0x84, lh = +0xA4, b77 = +0x77, raw7C = the 12 bytes +0x7C..+0x87. A field that
// cannot be read prints "?". Appended to the L4 "active tactic records" mismatch and kept in Located::records_probe on a good read.
std::string records_probe(Memory& mem, uint64_t rb, int n);
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
// ------------------------------------------------------------------------------------------------ bench keep (2.15): the working copies
// P214 (research_20261017\BENCH_FINDINGS.md, dump evidence\2026-10-17\P214\bench\, LEICESTER 15:17): your club's MAP sheet keeps the order
// you saved; Team Management builds its own working copy when it OPENS, in the game's own order, shows that, and its Yes copies it into the
// map (and autosaves). So bench keep writes the map's bench / reserves order into the working copy right after it is built.
// The working copies: inline sheet elements of the service (each inside a 0x510-byte block starting 0x10 before it: i32 0 / 10 / 11 / 0),
// at svc+0x550 and svc+0xA60, laid out as a map element (kSheet* offsets). With Team Management open both held teamsheetid -1, your team,
// current 1, slot count 11, DIRTY 255, mode -1 and the screen's order; they differed only in their record vectors (+0x1B0 / +0x1B8 / +0x1C0,
// freshly allocated: a new build has new ones). Which of the two the screen draws is not known; bench keep writes +0x550 (the first, as Edit
// Tactic's edited copy is the first of its pair) and the probe logs both. Whether a copy stays "dirty 255" after Team Management closes is
// not known either: a build is told by its record vector changing.
constexpr std::array<uint64_t, 2> kTmCopyOffsets = {0x550, 0xA60};
constexpr uint64_t kTmCopyTarget = 0x550;
constexpr uint8_t kTmCopyDirty = 255;
struct TmCopy {
    uint64_t offset = 0;      // svc + offset
    bool readable = false;
    int32_t sheet_id = 0, team = 0, slot_count = 0, mode = 0;
    uint8_t current = 0, dirty = 0;
    uint64_t rec_begin = 0;   // its record vector: the build's identity
    std::array<int32_t, kSheetPidCount> ids{};
    // Team Management's working copy of club `user` is built: team == user, dirty 255, slot count 11, mode -1, current 1, readable
    bool live(int user) const;
    std::string label() const;  // "svc+0x550"
};
// Both working copies of the service, read-only (a copy that cannot be read is listed with readable = false)
std::vector<TmCopy> read_tm_copies(Memory& mem, uint64_t svc);

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
    std::array<int32_t, kSheetPidCount> sheet_ids{};  // the sheet's 52 player ids in the game's order (-1 = empty; the bench probe)
    std::vector<WorkCopy> copies;  // the four inline records of the service, in kWorkCopyOffsets order
    std::vector<TmCopy> tm_copies;  // 2.15: Team Management's working copies of your sheet (CoreService::read only; read_live leaves it empty)
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
// "" only for mode -1 (your club's own sheet). Any mode > -1 is refused (decision WP-B1); the active sheet of its mode with the reason that
// SetSheet would run vf 0xF8 for it
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
// ------------------------------------------------------------------------------------------------ the career database row (2.4)
// The cm_mentalities row of the live tactic is written in the SAME game-thread job as the map record (decision 3.2): what the game's own
// save writes from the map record (old build [V]) and what a career load reads, so the tactic persists the way the game's own does. The row
// is found on the render thread through Turbo's database layer (core/t3db.h: the table's layout is copied into the plan); the job re-checks
// the table header, the record, the row's identity and the owner rule (each field still holds the value it held when Apply was pressed,
// which is the live record's old value), snapshots, writes, reads back and rolls back the row together with the map record. Kill switches
// channel_e_db_off.txt / tactics_db_off.txt leave it out (the live record is still written; the log says so).
struct DbRowField {
    std::string name;  // "defensivedepth", "buildupplay", "pos9role"
    int64_t old_value = 0, new_value = 0;
};
struct DbRowCheck {
    std::string name;  // "teamid", "activetactic", "sourceformationid", "artificialkey", "playerid3", "position3", "code"
    Value value;       // what the row must hold in the job
};
struct DbRowWrite {
    Table table;       // the table as Turbo's database layer read it (header, record block, fields): a copy
    uint64_t rec = 0;  // the row
    int64_t key = -1;  // its artificialkey
    std::vector<DbRowCheck> checks;
    std::vector<DbRowField> fields;
    std::string label() const;  // "cm_mentalities artificialkey 0"
};
// The range Turbo writes a mirrored field in (generic, the schema's): defensivedepth 1..100, buildupplay 0..3, pos<n>role 0..60000. false = not
// a field Turbo writes
bool db_row_field_range(const std::string& name, int64_t& lo, int64_t& hi);
// In the job: the table still describes the same block, the record is valid, every check holds, every field holds its old value, every new
// value is in the field's range and the database's own. "" = it may be written
std::string check_db_row(Memory& mem, const DbRowWrite& row);

// The record a plan was made for: the job writes only when it still finds exactly this record with exactly these old values. A plan made
// from the editor holds map-record writes only; the job adds the copy targets' writes itself. An undo plan names both.
struct Plan {
    int user = 0, record_index = -1;
    int32_t sheet_id = 0, formation = 0, source_formation = 0;
    std::string record_name, code;  // code: the record's tactic code when the plan was made (the job refuses another)
    std::array<int32_t, kSlots> pids{};
    std::vector<FieldWrite> writes;
    bool undo = false;  // an undo: the dirty flag stays 1 (the game may have committed other changes since)
    long long load_gen = -1;  // the career load the plan was made in (-1 = the submitting context's)
    std::vector<DbRowWrite> rows;  // the career database row(s) written in the same job (cm_mentalities; X2's teams branch adds the teams row)
    std::string rows_note;         // why a row is not in the job ("" = the rows say it all)
};
// G6 and the diff against the live record: "" and the plan (possibly empty: nothing changes), else the refusal. When Line Height, Build-Up
// Style or Defensive Approach changes, all three are in the plan (written together; the unchanged ones with their live value). A new
// enum value must be known and writable (build_up_writable / approach_writable), the approach must be the line height's band (line_band:
// Turbo writes them band-consistent), a changed line height must lie in a band whose approach is writable (max_writable_line); a role /
// focus change needs its triple in `allow` (null or not built: refused)
std::string make_plan(const LiveTactic& now, const Request& req, Plan& out, const RoleAllowList* allow);

// The values the write jobs read from the game right before Turbo's first write of each field (map record and working copies), for one
// record in one career load. An undo may write back only these (never a value taken from the undo file alone).
// A field the game (Edit Tactic's Yes, a reload of the sheet) changed after Turbo's write and Turbo wrote again (Re-apply): the value the game
// put there is what the later write replaced, so it becomes the original (as merge_undo does for the undo file; a slot's three role values
// as one unit, keyed by its role word).
struct WriteHistory {
    int user = 0, record_index = -1;
    long long load_gen = -1;
    std::map<std::tuple<uint64_t, int, int>, int32_t> original;  // (copy, FieldKind, slot) -> value
    std::map<std::tuple<uint64_t, int, int>, int32_t> last;      // (copy, FieldKind, slot) -> the value Turbo's latest write put there
    // the career database rows the jobs wrote: (table, artificialkey, field) -> the value before Turbo's first write / Turbo's latest
    std::map<std::tuple<std::string, int64_t, std::string>, int64_t> db_original, db_last;
    // a non-undo job whose writes were attempted (its rows with their snapshot values as old values)
    void note(const Plan& p, const std::vector<FieldWrite>& written, const std::vector<DbRowWrite>& rows = {});
    void note(const Plan& p) { note(p, p.writes, p.rows); }
    // "" when every value the undo writes (live record and rows) is an original of this record and load
    std::string check_undo(const Plan& p) const;
};
// What apply_plan checks besides the live record (a missing one refuses what needs it)
struct JobChecks {
    const RoleAllowList* allow = nullptr;      // a non-undo role write needs its triple here
    const WriteHistory* history = nullptr;     // an undo needs its values here
    const std::atomic<bool>* cancel = nullptr; // set: nothing is written
    // The record copies written with the map record (ids of LiveTactic::copies). Null or empty = none: the 2.4 default (the E2 working copies
    // are never written). X3's branch (a) / (b) names the copy Edit Tactic loads from here; the tests drive the machinery with stand-ins.
    const std::set<uint64_t>* copy_targets = nullptr;
};
inline bool copy_target(const JobChecks& chk, uint64_t id) { return chk.copy_targets && chk.copy_targets->count(id) != 0; }
// The plan on its own against the live record: kinds, slots, ranges, known and writable enum values (a non-undo write), a non-undo write's
// three record fields together and band-consistent, the three role values of a slot together and consistent (word == role_word(live
// group, role id, focus)), the group unchanged; a non-undo role word in the allow-list; a copy only in an undo, only a copy target and
// only while it still holds this tactic; each row field in its range, an Apply's row field the mirror of a map-record write with the same
// new value; an undo's values (live and rows) in the history. "" = it may be written
std::string check_plan(const LiveTactic& now, const Plan& plan, const JobChecks& chk);
// The address of a field inside the located record or a matching working copy (0 when the slot is out of range or the copy does not match)
uint64_t field_address(const LiveTactic& now, const FieldWrite& w);
// The same fields for every copy target that holds this tactic (WorkCopy::matches and its id in `targets`; none when `targets` is null):
// the copy's own current values as old values, the plan's new values; a copy whose 11 slots do not hold the map record's players slot by
// slot (another team's or another line-up's tactic with the same code), or whose written slot is another position group, is left out
// (`note` says why). Only values that differ. `note` stays empty when there is no target.
std::vector<FieldWrite> copy_writes(const LiveTactic& now, const Plan& plan, std::string& note, const std::set<uint64_t>* targets);

// ------------------------------------------------------------------------------------------------ the write (game thread)
struct ApplyResult {
    bool ok = false;           // written and read back (or nothing to do)
    bool written = false;      // fields were changed in game memory and stayed
    bool undone = false;       // a read-back mismatched: everything was put back from the snapshot
    bool restore_failed = false;
    bool session_off = false;  // switch channel E off for the session (a mismatch)
    bool undo = false;         // the plan was an undo
    std::string stage, message;
    std::vector<FieldWrite> writes;  // what was written (old = the snapshot), the copy targets' writes included
    std::string copies;              // what happened to the copy targets ("" = none: the 2.4 default)
    int copies_written = 0;          // copies written by this job
    std::vector<DbRowWrite> rows;    // the database rows this job wrote or put back (old = the snapshot); empty when the plan had none
    std::string rows_note;           // the plan's reason a row is not in the job
    uint8_t dirty_before = 0, dirty_after = 0;
    LiveTactic after;
    // "channel E: team 95 record 0 approach 0->2 line height 25->90 build-up 0->1; cm_mentalities artificialkey 0 defensivedepth 25->90
    // buildupplay 0->1 (read back ok) [record now LH 90 BU 1 DA 2]"
    std::string log_line;
    double micros = 0.0;   // time the job took (the host fills it)
    int reads = 0;         // reads of game memory the job made (whole pages for the walk, single fields for the read-back)
};
// Report 4.4: re-run L1-L4, G5, the plan's record, code and old values, the copy targets that hold this tactic (copy_writes), the database
// rows (check_db_row), snapshot, write all of them, dirty = 1, read back; a mismatch restores every field (map record, copies, rows) and the
// dirty flag from the snapshot.
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

// ------------------------------------------------------------------------------------------------ bench keep (2.15): the write
// The write (Keeper decides when, Service::submit_bench queues it). ONE game-thread job: L1-L4 and G5 of the map again; the map still holds
// exactly `saved` (your saved order) with ids 0..10 = the 11 players of the active record's slots; the working copy at svc+0x550 is still
// the build the plan was made for (record vector `build`), live for your club with the map's sheet id, and holds exactly `before`; `saved`
// is `before` with ids 11..51 in another order (bench::shuffle_of: the same values, the empty places where they are, no player twice). Then:
// snapshot the copy's 52 ids, write the differing ids among 11..51, read all 52 back; a mismatch puts every id back from the snapshot and
// switches bench keep off for the session. Never ids 0..10, never a player added or removed, never the copy's dirty flag (255 marks it as
// Team Management's), never the map, the record, the service cache or a game function. Team Management is open by design (the copy is
// built only then); G2's other parts hold: the hub, no save started in the last kSaveQuietSeconds, the career load.
struct BenchPlan {
    int user = 0;
    long long load_gen = -1;  // the career load the plan was made in
    int32_t sheet_id = 0;
    uint64_t build = 0;       // the working copy's record vector begin when the plan was made
    std::array<int32_t, kSheetPidCount> saved{}, before{};  // saved = the map's ids (the order to write), before = the copy's ids now
};
struct BenchResult {
    bool ok = false;             // written and read back (or nothing to do)
    bool written = false;        // ids were changed in game memory and stayed
    bool undone = false;         // a read-back mismatched: everything was put back from the snapshot
    bool restore_failed = false;
    bool session_off = false;    // switch bench keep off for the session
    std::string stage, message;
    int moved = 0;               // places 11..51 written
    std::array<int32_t, kSheetPidCount> after{};  // the copy's 52 ids read back (written: the saved order)
    // "bench keep: team 95 sheet -1: your saved order written into Team Management's working copy svc+0x550, places 11..51 (2 places; the
    // starting XI untouched) (read back ok)"
    std::string log_line;
    double micros = 0.0;
    int reads = 0;
};
// The plan against the live read-out (the map, its starters) and the working copy (build, identity, ids, the permutation). "" = it may be
// written
std::string check_bench_plan(const LiveTactic& now, const TmCopy& copy, const BenchPlan& p);
// The job (game thread). Never throws
BenchResult apply_bench(Memory& game, uint64_t svc, const Anchors& a, const BenchPlan& p, const std::atomic<bool>* cancel = nullptr);
// The switch that blocks a bench keep write, read from turbo_output ("" = none): no bench_keep_on.txt, bench_keep_off.txt, tactics_off.txt,
// call_team_sheet_off.txt
std::string bench_switch_block(const std::filesystem::path& turbo_output);

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
    // the cm_mentalities row (2.4: written in the same game-thread job as the live record; "db_mirror" in the JSON, the 2.2 name)
    std::string mirror_state;  // "" none, "pending" (in the job), "written", "skipped" (left out: mirror_note), "restored"
    std::string mirror_note, mirror_table;
    int64_t mirror_row_key = -1;  // the row's artificialkey
    std::vector<MirrorField> mirror;  // old = the value before Turbo's first write, new = Turbo's
    std::string note;
    std::string to_json() const;
    static bool parse(const std::string& text, UndoRecord& out, std::string* err);
    static UndoRecord from_plan(const Plan& p);
};
bool load_undo(const std::filesystem::path& p, UndoRecord& out, std::string* err);  // false when absent or not an undo file
bool save_undo(const std::filesystem::path& p, const UndoRecord& u, std::string* err);
// An undo plan: the fields that still hold the value Turbo wrote get their old value back; a slot whose three role values do not all
// hold Turbo's values is left alone (listed in `skipped`); a working copy that no longer holds this tactic (another code, e.g. after an
// Edit Tactic save) or is not a copy target (`copy_targets`, none by default: 2.4 writes no working copy) is left alone too. "" and the
// plan, else why nothing can be put back
// Refused when the undo record is from another Lua session or career load (a reload throws the in-memory change away anyway)
std::string make_undo_plan(const LiveTactic& now, const UndoRecord& u, const std::string& session, long long load_gen, Plan& out,
                           std::vector<std::string>& skipped, const std::set<uint64_t>* copy_targets = nullptr);
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
// The cm_mentalities row check (render thread, when Apply is pressed): the row's playerid0..10 / position0..10 must be the live slots', its
// code (when the save has the field and get_str can read it) the live record's code, and every field about to be written must hold the
// live OLD value. get(name, value) reads one int field of the row (false = the field is missing). "" = it may be written, else why the row
// is left out (the live record is written alone)
std::string mirror_row_check(const std::function<bool(const std::string&, int64_t&)>& get, const LiveTactic& now,
                             const std::vector<FieldWrite>& writes,
                             const std::function<bool(const std::string&, std::string&)>& get_str = nullptr);
// The Apply's row fields (render thread): the fields the map record's writes mirror to (mirror_field_name), old = the row's value now (get),
// new = the write's new value; only those that differ (empty: the row already holds the values)
std::vector<DbRowField> apply_row_fields(const Plan& plan, const std::function<bool(const std::string&, int64_t&)>& get);
// The undo's row identity (render thread): the database switches (sw.db_row_block) and the row (get reads its int fields) must still be the
// record's tactic: teamid == u.team, activetactic == 1, sourceformationid == u.source_formation, artificialkey == u.mirror_row_key. "" = it
// may be put back
std::string undo_row_check(const Switches& sw, const std::function<bool(const std::string&, int64_t&)>& get, const UndoRecord& u);
// The undo's row fields: every field of the undo record's db_mirror whose row value (get) is still Turbo's goes back to its value from before
// Turbo's first write, when this undo plan puts its live field back OR the live record (now) no longer holds Turbo's value there (a job
// whose putting back failed can leave the row at Turbo's value while the live field is the game's); `left` names the others
std::vector<DbRowField> undo_row_fields(const UndoRecord& u, const Plan& undo_plan, const std::function<bool(const std::string&, int64_t&)>& get,
                                        std::vector<std::string>& left, const LiveTactic* now = nullptr);
// After a job: the rows it wrote into the undo record's db_mirror (state "written", table, artificialkey; a field written again keeps its
// value from before Turbo's first write); an undo's rows mark it "restored"
void add_job_rows(UndoRecord& u, const std::vector<DbRowWrite>& rows, bool undo);
// ------------------------------------------------------------------------------------------------ overwrite detection and Re-apply (2.4)
// After Team Management was open (Lua's team_mgmt flag back to false in the hub) the UI re-reads the record: the game's "Yes, save your
// team sheet" copies Edit Tactic's screen into it (and autosaves). "" when the live record still holds every value Turbo's write (the undo
// record, state "written") left, else the one-line prompt "the game replaced your tactic (LH 50 / Balanced / Balanced). Re-apply?" (LH /
// Build-Up Style / Defensive Approach as they are now). Never acted on automatically.
std::string replaced_prompt(const LiveTactic& now, const UndoRecord& u);
// Only for the record Turbo wrote: the same club, record index, formation ids and sheet (the tactic code may differ: Edit Tactic's save makes
// a new one). A slot that holds another player now does not count (Re-apply would leave it out).
bool same_tactic_record(const UndoRecord& u, const LiveTactic& now);
// The active tactic is another record than the one Turbo wrote: the card says so, with no Re-apply ("" = it is the same one)
std::string other_tactic_note(const LiveTactic& now, const UndoRecord& u);
// Re-apply: Turbo's last values onto the live record as a Request (make_plan applies G6 again): the three record fields as Turbo left them,
// each slot role Turbo wrote whose live slot holds the same player and position group (others are named in `skipped`)
Request reapply_request(const LiveTactic& now, const UndoRecord& u, std::vector<std::string>& skipped);
// "" when a Re-apply may be planned now: the undo record is of this Lua session and career load, of the live club and the same tactic record,
// and the record no longer holds Turbo's values; else why not (the click is refused and the prompt dropped)
std::string reapply_check(const LiveTactic& now, const UndoRecord& u, const std::string& session, long long load_gen);

// The DB field a live write of the map record mirrors to: Build-Up Style -> "buildupplay", Line Height -> "defensivedepth" (the schema's
// 1..100), the role word -> "pos<slot>role"; "" for role id / focus (the word carries them), for the Defensive Approach (cm_mentalities
// has no column for it) and for every working-copy write
std::string mirror_field_name(const FieldWrite& w);
bool same_record(const UndoRecord& u, const LiveTactic& now);

// ------------------------------------------------------------------------------------------------ the kick-off check (read-only)
// 2.4 (decision 3.2): the check is the self-proof of the write. 20 s (Turbo's clock) after Lua's phase became "match" it finds the match-setup
// copies of your club's tactic (by the 11 player ids at out+0x168 or the 11 role words at out+0x118; team and positions checked), reads
// Line Height / Build-Up Style / Defensive Approach through the decode table below and logs exactly one line:
//   channel E kick-off: match copy team 95 LH 70 BU 1 DA 2 roles 11/11 = Turbo (ok) | <copies, the table's offsets, the raw window, the dump>
// "= Turbo (ok)" only when every copy holds the values Turbo's last write left (read back, this career load), all three fields decoded.
// Otherwise "= game (differs: ...)", "= game (the hub's values; no Turbo write in place)" or "= unknown (... not decoded ...)".
// TeamState (DefendingTeamStyle +0x230, Mentality +0x280: old-build type data) is not chained on this build (T2, kickoff_readout.py): not read.

// The tactic fields of the match-setup copy, ONE table (an offset changes here and nowhere else). Source: T2's static decode of the old build
// 6AB9813C (the converter 0x148060BE4 copies record +0x88..+0xBF byte-wise to out+0x150..+0x164: record +0xA4 Line Height -> out+0x158,
// +0xB0 Build-Up Style -> +0x15B, +0xB4 Defensive Approach -> +0x15C; the style consumer 0x1481446F0 reads +0x15B / +0x15C as bytes). The
// live build kept the old layout for team, positions, roles and pids (2026-10-12) [V]; these three are [A] on 6AC07E31 until a kick-off line
// that names Turbo's values proves them (X2's readout confirms them by code signature). known == false: not read ("?" and the raw window).
struct CopyField {
    const char* name;  // "LH" "BU" "DA"
    uint32_t offset;   // from out+0x00
    uint32_t width;    // bytes, little-endian, unsigned (1 = u8)
    bool known;
};
const std::vector<CopyField>& match_copy_fields();
const char* match_copy_fields_source();  // one line for the log: where the table comes from
// The 44 bytes of the 11 role words (or player ids) in slot order (the patterns the match-setup copy carries at out+0x118 / out+0x168)
std::vector<uint8_t> role_pattern(const std::array<int32_t, kSlots>& roles);
// Every 4-aligned position of the pattern in buf (first byte at `base`), at most max_hits
std::vector<uint64_t> find_bytes(const uint8_t* buf, size_t len, uint64_t base, const std::vector<uint8_t>& pat, size_t max_hits, size_t align);
// `out` is a match-setup copy of club `user`'s tactic: team id at out+0x28, the 11 pids at out+0x168, 11 positions 0..27 at out+0x30 with
// the goalkeeper (0) first
bool is_match_copy_at(Memory& mem, uint64_t out, int user, const std::array<int32_t, kSlots>& pids);
// A role-word hit (out = hit - 0x118) that is a match-setup copy
bool is_match_copy(Memory& mem, uint64_t hit, int user, const std::array<int32_t, kSlots>& pids);
// What the kick-off check looks for: the live tactic as last read in the hub (or after Turbo's last job)
struct KickoffArm {
    bool armed = false;
    int user = 0;
    long long load_gen = -1;
    std::array<int32_t, kSlots> roles{}, pids{};
    int32_t line_height = 0, build_up = 0, approach = 0;
    std::string code, when;  // when: "read in the hub" / "after Turbo's write" (+ the clock)
    bool turbo_values = false;  // these are the values Turbo's last write of this career load left (no undo, reload or other value since)
};
// One dumped match-setup copy: bytes = out+0x00 .. +0x1A0 + 0x200
struct KickoffCopy {
    uint64_t out = 0;
    std::vector<uint8_t> bytes;
};
// One copy decoded through match_copy_fields()
struct KickoffRead {
    uint64_t out = 0;
    int32_t team = 0;
    int lh = -1, bu = -1, da = -1;          // -1 = not decoded (field unknown, or outside the bytes)
    int roles_match = 0;                     // slots whose role word is the one looked for
    std::array<int32_t, kSlots> roles{};
    std::vector<uint8_t> window;             // out+kCopyStyleWindow .. +kCopyStyleWindowLen
    // the raw bytes at every table offset, known or not ("LH +0x158=46", "DA +0x15C=02 (not decoded)"): the line always carries them, so the
    // S-B1 check doubles as X2's readout even when a field is not decoded
    std::string raw;
    std::vector<size_t> lh_bytes;            // offsets where a byte equals the Line Height looked for (outside the known blocks), at most 6
    bool decoded() const { return lh >= 0 && bu >= 0 && da >= 0; }
};
KickoffRead decode_match_copy(const KickoffCopy& c, const KickoffArm& arm, const std::vector<CopyField>& fields = match_copy_fields());
// The verdict part: "= Turbo (ok)", "= game (differs: LH 25 not 95, roles 10/11)", "= game (the hub's values; no Turbo write in place)",
// "= unknown (LH not decoded: see the raw window)"; ok = the copies hold Turbo's values
std::string kickoff_verdict(const KickoffArm& arm, const std::vector<KickoffRead>& reads, bool& ok);
// The one kick-off line: "channel E kick-off: match copy team 95 LH 70 BU 1 DA 2 roles 11/11 = Turbo (ok)" + (details.empty() ? "" : " | " +
// details). reads must not be empty
std::string kickoff_line(const KickoffArm& arm, const std::vector<KickoffRead>& reads, const std::string& details);
// "4b 02 02 01 02 ..." (lower-case hex, space separated)
std::string window_hex(const std::vector<uint8_t>& b);
// The 4-aligned offsets inside the dump where an i32 equals v, outside the known blocks (positions, x / y, the role words, the pids)
std::vector<size_t> kickoff_value_offsets(const std::vector<uint8_t>& bytes, int32_t v);
// The offsets where a byte equals v (0..255) outside the known blocks: where a u8 field holding v may sit
std::vector<size_t> kickoff_byte_offsets(const std::vector<uint8_t>& bytes, int v);
// The index file next to the dump: what was looked for, each copy's address, team, formation ids, roles, pids, the decode, and where the
// expected line height sits (i32 and u8 offsets in every copy)
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
    bool bench_hub = false;  // 2.15 bench keep: G2 without its Team Management part (bench keep writes while it is open): hub, no save in 30 s, load
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
    // match-setup copy of the live tactic last read in the hub of this career load (read-only, background), dumps each copy to
    // turbo_output\channel_e_kickoff_dump.bin (+ .txt index) and logs the outcome either way; once more 30 s later when none was found
    virtual void kickoff_tick() = 0;
    virtual std::string kickoff_status() = 0;
    // The role allow-list of a career load (null until built), and its status line
    virtual std::shared_ptr<const RoleAllowList> role_allow(long long load_gen) = 0;
    virtual std::string allow_status() = 0;
    // The UI's latest view (every frame): the jobs compare their plan's career load and the hub flag with it
    virtual void note_context(const Context& ctx) = 0;
    // 2.15 bench keep (apply_bench on the game thread): one channel E job at a time with the tactic jobs (the same busy flag), its results
    // in their own queue (never poll()'s: those are the tactic card's). false + why when it cannot be queued
    virtual bool submit_bench(const Context& ctx, const BenchPlan& plan, std::string& why) {
        (void)ctx;
        (void)plan;
        why = "this channel E service has no bench keep";
        return false;
    }
    virtual bool poll_bench(BenchResult& out) {
        (void)out;
        return false;
    }
    virtual std::string bench_off() { return ""; }  // "" = on; else why bench keep is off for this session (a read-back mismatch)
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
    // wall-clock microseconds for the waits a user or the game paces: the kick-off check's schedule and scan budget, the scan pick's re-check
    // (null = clock_us). The Windows host gives the system clock: Turbo's QPC / steady clocks ran ~3x wall time in game (2026-10-13)
    std::function<double()> wall_us;
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
    bool submit_bench(const Context& ctx, const BenchPlan& plan, std::string& why) override;
    bool poll_bench(BenchResult& out) override;
    std::string bench_off() override;

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
    double wall() const { return h_.wall_us ? h_.wall_us() : now(); }
    std::string job_switch_block() const;  // the master / channel kill switch or a missing opt-in, read in the job ("" = none)
    bool timed() const { return h_.clock_us || h_.wall_us; }

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
    std::vector<BenchResult> bench_results_;  // 2.15 bench keep: its own queue
    std::string bench_off_;                   // a bench keep read-back mismatch switched it off for the session
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
    // what Turbo's last write left in place (armed; cleared by an undo): an arm is "Turbo's values" only while it reads exactly this, in
    // the same career load (a reload, Edit Tactic's Yes or the game put other values in otherwise)
    KickoffArm turbo_arm_;
    void arm_from(const LiveTactic& lt, long long gen, const char* when);  // with mu_ held
    // gen: the career load the UI saw when the check came due (an arm of another load is not looked for)
    void run_kickoff(KickoffArm arm, int attempt, long long gen);
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
    std::atomic<bool> cur_bench_hub_{false};  // 2.15: Context::bench_hub
};

}  // namespace tsheet
}  // namespace turbo
