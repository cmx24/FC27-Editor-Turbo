// FC 27 LE Turbo GUI - tactics channel E: your club's LIVE tactic record (the game's GameServices::CachedTeamSheetService),
// read and written in place on the game thread. Platform-independent part; src/win/team_sheet_win.cpp gives it the game's memory,
// the signatures and Turbo's game-thread dispatcher.
//
// Facts: turbo_output\TACTICS_CHANNEL_E_REPORT.txt (live probe 2026-10-11 on FC27.exe build 6AC07E31-2145C000, read-only), sections 2-4.
// Tags used below: [V] verified live on that build (or in its code bytes), [A] assessed, [U] unknown. Anything [U] is refused, never
// guessed: build-up / preset / role / focus LABELS (raw numbers only), presets and formations (not written in 2.2), the meaning of the
// undecoded record and slot fields (never touched).
//
// The chain (report 2.5-2.9, all [V]):
//   service svc (0x1418 bytes): [svc] = VT1, [svc+8] = VT2, u8 +0x1404 loaded, i32 +0x13A0 last team / +0x13A4 active sheet id (the
//     game's cache, never written by Turbo), eastl::hash_map<int team, eastl::vector<Sheet>> at +0x13B0: +0x13B8 bucket array,
//     u32 +0x13C0 bucket count, u32 +0x13C4 node count; node {+0 i32 key, +8 begin, +0x10 end, +0x28 next}, bucket = team % count
//   sheet element E (stride 0x290): +0 teamsheetid, +4 teamid, u8 +0xAC current flag, +0xC4 52 player ids, +0x1B0/+0x1B8 tactic
//     records, i32 +0x280 slot count (11), u8 +0x288 DIRTY, i32 +0x28C mode
//   record R (stride 0xF8): +0 name (EASTL string), u8 +0x80 activetactic, +0x84 presetid, +0xA4 defensivedepth, +0xB0 buildupplay,
//     +0xC0 formationfullnameid, +0xC4 / +0xD0 sourceformationid, +0xC8 formationaudioid, +0xD8 / +0xE0 the 11 slots
//   slot S (stride 0x48): +0 pid, +4 position, f32 +8 / +0xC x / y, i32 +0x30 role word = group << 12 | roleId << 6 | focus with the
//     mirrors +0x34 roleId and +0x38 focus (bits 3..5 of the word 0, verified on 11 of 11 slots)
//
// What Turbo does (report section 4): a GUARDED IN-PLACE WRITE, not a SetSheet call. For your sheet (mode -1, report 2.7) SetSheet is
// UpdateSheet = deep copy + dirty = 1 + the cache, so writing the scalar fields and setting dirty = 1 reaches the same end state without
// allocating anything. Written: buildupplay, defensivedepth, and per slot the three role values together; then the dirty flag. Never:
// formation ids, positions, offsets, names, takers, presets, the service cache. Every field is read back in the same game-thread job; a
// mismatch puts every field and the dirty flag back from the snapshot taken in that job and switches channel E off for the session.
//
// Gates (report 4.3): G1 opt-in turbo_output\call_team_sheet_on.txt, no call_team_sheet_off.txt / tactics_off.txt; G2 career hub only
// (Lua's match phase "hub", no save started in the last kSaveQuietSeconds, the located service belongs to this career load); G3 the
// exact build and every anchor consistent with the vtable; G4 the locate checks L1-L4; G5 SetSheet would not run vf 0xF8 for this sheet;
// G6 value ranges (preset / formation refused in 2.2).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
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
// tactic record
constexpr uint64_t kRecStride = 0xF8;
constexpr uint64_t kRecName = 0x00, kRecNameSize = 0x08, kRecActive = 0x80, kRecPreset = 0x84, kRecDepth = 0xA4, kRecBuildUp = 0xB0,
                   kRecFormation = 0xC0, kRecSourceFormation = 0xC4, kRecAudio = 0xC8, kRecSourceFormation2 = 0xD0, kRecSlotBegin = 0xD8,
                   kRecSlotEnd = 0xE0;
constexpr int kMaxRecords = 8;
// slot
constexpr uint64_t kSlotStride = 0x48;
constexpr uint64_t kSlotPid = 0x00, kSlotPosition = 0x04, kSlotX = 0x08, kSlotY = 0x0C, kSlotRole = 0x30, kSlotRoleId = 0x34, kSlotFocus = 0x38;
constexpr int kSlots = 11;
// value ranges (G6): the career schema's cm_mentalities ranges (turbo\le27\fc27_db_schema.json), which the game's save writes these into
constexpr int kBuildUpMin = 0, kBuildUpMax = 3, kDepthMin = 1, kDepthMax = 100;
constexpr int kRoleIdMax = 63, kFocusMax = 7;
constexpr int kRoleWordMax = 60000;  // cm_mentalities.pos{n}role max: a larger word could not be saved
// G2: a save started (Lua saw PREPARE_FOR_SAVE) less than this long ago refuses a write
constexpr long long kSaveQuietSeconds = 30;
// A job the game thread runs later than this after Apply writes nothing (the hub may have been left meanwhile)
constexpr double kJobDeadlineUs = 2e6;
// match-setup copy (report 2.12) [V]: roles at out+0x118, pids at out+0x168, team id at out+0x28
constexpr uint64_t kCopyRoles = 0x118, kCopyPids = 0x168, kCopyTeam = 0x28;

// The role word (report 2.9) [V]
inline int32_t role_word(int group, int role_id, int focus) { return int32_t((group << 12) | (role_id << 6) | focus); }
inline int role_group(int32_t w) { return int(uint32_t(w) >> 12); }
inline int role_id_of(int32_t w) { return int((uint32_t(w) >> 6) & 0x3F); }
inline int focus_of(int32_t w) { return int(uint32_t(w) & 7); }
inline bool role_word_plain(int32_t w) { return w >= 0 && (w & 0x38) == 0; }  // bits 3..5 zero: the encoding seen on every slot
// Defensive depth band (report 1.3: <=30 / 31-60 / 61-90 / >90; the band NAMES are [A] and not shown)
int depth_band(int depth);
std::string depth_band_text(int depth);  // "band 0 (1-30)"

// ------------------------------------------------------------------------------------------------ files in turbo_output
inline const char* opt_in_file() { return "call_team_sheet_on.txt"; }
inline const char* kill_switch_file() { return "call_team_sheet_off.txt"; }
inline const char* master_switch_file() { return "tactics_off.txt"; }
inline const char* find_off_file() { return "call_team_sheet_find_off.txt"; }        // registry Find not called: the scan only
inline const char* mirror_off_file() { return "channel_e_db_mirror_off.txt"; }      // no cm_mentalities mirror
inline const char* db_off_file() { return "tactics_db_off.txt"; }                   // the database rows channel (stops the mirror too)
inline const char* undo_file() { return "channel_e_undo.json"; }

struct Switches {
    bool opt_in = false, off = false, master = false, find_off = false, mirror_off = false, db_off = false;
    // the kill switch that blocks a write ("" = none)
    std::string blocking() const;
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
// L2's user: PSM = the career's manager slot 87 (count 1), [PSM] == psm_vtable, user club = i32 [PSM+0x10]
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
struct LiveTactic {
    Located at;
    std::string g5;  // CoreService::read: check_g5 of this read ("" = pass)
    int32_t sheet_id = 0, mode = 0;
    uint8_t dirty = 0;
    std::string name;  // the record's name ("433"; "" when it could not be read: display only)
    int32_t preset = 0, depth = 0, build_up = 0, formation = 0, source_formation = 0, audio = 0;
    std::array<SlotView, kSlots> slots{};
    std::vector<int32_t> squad;  // the sheet's player ids (not -1)
};
// locate + every field the UI shows. "" = read
std::string read_live(Memory& mem, uint64_t svc, int user, const Anchors& a, LiveTactic& out);

// G5 (report 4.3): the id GetActiveSheetId(mode, team) would return, by the same loop (first element of map[team] with +0x28C == mode
// and +0xAC != 0; -1 when none). Turbo walks it itself: the game's function would write the service cache.
int32_t active_sheet_id(Memory& mem, const Located& at, int32_t mode);
// "" when SetSheet would not run vf 0xF8 for this sheet either (mode -1, or the sheet is not the active one of its mode)
std::string check_g5(Memory& mem, const LiveTactic& lt);

// ------------------------------------------------------------------------------------------------ requests and plans (G6)
struct SlotChange {
    int slot = -1;
    int role_id = -1, focus = -1;  // -1 = keep
};
struct Request {
    int build_up = -1, depth = -1;  // -1 = keep
    std::vector<SlotChange> slots;
    bool preset = false, formation = false;  // asked to change the preset / the formation: refused in 2.2
};
enum class FieldKind { Depth, BuildUp, Role, RoleId, Focus };
const char* field_name(FieldKind k);                     // "defensivedepth" "buildupplay" "role" "roleid" "focus"
bool field_from_name(const std::string& s, FieldKind& k);
struct FieldWrite {
    FieldKind kind = FieldKind::Depth;
    int slot = -1;  // -1 for the record fields
    int32_t old_value = 0, new_value = 0;
};
// The record a plan was made for: the job writes only when it still finds exactly this record with exactly these old values
struct Plan {
    int user = 0, record_index = -1;
    int32_t sheet_id = 0, formation = 0, source_formation = 0;
    std::string record_name;
    std::array<int32_t, kSlots> pids{};
    std::vector<FieldWrite> writes;
    bool undo = false;  // an undo: the dirty flag stays 1 (the game may have committed other changes since)
};
// G6 and the diff against the live record: "" and the plan (possibly empty: nothing changes), else the refusal
std::string make_plan(const LiveTactic& now, const Request& req, Plan& out);
// The address of a plan field inside the located record (0 when the slot is out of range)
uint64_t field_address(const Located& at, const FieldWrite& w);

// ------------------------------------------------------------------------------------------------ the write (game thread)
struct ApplyResult {
    bool ok = false;           // written and read back (or nothing to do)
    bool written = false;      // fields were changed in game memory and stayed
    bool undone = false;       // a read-back mismatched: everything was put back from the snapshot
    bool restore_failed = false;
    bool session_off = false;  // switch channel E off for the session (a mismatch)
    bool undo = false;         // the plan was an undo
    std::string stage, message;
    std::vector<FieldWrite> writes;  // what was written (old = the snapshot)
    uint8_t dirty_before = 0, dirty_after = 0;
    LiveTactic after;
    std::string log_line;  // "channel E: team 95 record 0 build-up 0->2 depth 25->75 slot 9 role 38213->38214 (read back ok)"
    double micros = 0.0;   // time the job took (the host fills it)
};
// Report 4.4: re-run L1-L4, G5, the plan's record and old values, snapshot, write, dirty = 1, read back; a mismatch restores every field
// and the dirty flag from the snapshot. Never throws.
ApplyResult apply_plan(Memory& mem, uint64_t svc, const Anchors& a, const Plan& plan);
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
    std::string record_name;
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
// hold Turbo's values is left alone (listed in `skipped`). "" and the plan, else why nothing can be put back
std::string make_undo_plan(const LiveTactic& now, const UndoRecord& u, Plan& out, std::vector<std::string>& skipped);
// Report 4.5: fields Turbo wrote that no longer hold its value (Edit Tactic or the game wrote them since)
std::vector<std::string> reverted(const LiveTactic& now, const UndoRecord& u);
// A second Apply on the same record while the first one's values are still in place: the record keeps the values from before Turbo's
// FIRST write (a field the new plan writes again whose old value is the earlier write's new value gets the earlier old value; fields only
// the earlier write touched stay listed). Another club or record: `next` as it is.
UndoRecord merge_undo(const UndoRecord& prev, const UndoRecord& next);
bool same_record(const UndoRecord& u, const LiveTactic& now);

// ------------------------------------------------------------------------------------------------ the kick-off check (read-only)
// The 44 bytes of the 11 role words in slot order (the pattern the match-setup copy carries at out+0x118)
std::vector<uint8_t> role_pattern(const std::array<int32_t, kSlots>& roles);
// Every 4-aligned position of the pattern in buf (first byte at `base`), at most max_hits
std::vector<uint64_t> find_bytes(const uint8_t* buf, size_t len, uint64_t base, const std::vector<uint8_t>& pat, size_t max_hits, size_t align);
// A hit is a match-setup copy (report 2.12) when the 11 pids sit 0x50 bytes later and the team id at hit - 0x118 + 0x28
bool is_match_copy(Memory& mem, uint64_t hit, int user, const std::array<int32_t, kSlots>& pids);

// ------------------------------------------------------------------------------------------------ gates
struct GateInput {
    // G1
    Switches sw;
    // G2
    bool bridge = false;              // Lua's bridge state is there
    bool in_cm = false;
    std::string match_phase;          // Lua's "hub" / "prematch" / "match" / "unknown"; "" = a Lua side without it
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
    // Report 4.5: after kick-off, look for the 11 role words in the game's memory (read-only, background)
    virtual void kickoff_check(int user, const std::array<int32_t, kSlots>& roles, const std::array<int32_t, kSlots>& pids) = 0;
    virtual std::string kickoff_status() = 0;
};

// What the host gives the platform-independent service (src/win/team_sheet_win.cpp; tests give a simulated game)
struct Host {
    Memory* mem = nullptr;
    Anchors anchors;
    uint64_t psm_vtable = 0;
    std::filesystem::path out_dir;  // turbo_output: the kill switches are looked at again inside the job
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
    void kickoff_check(int user, const std::array<int32_t, kSlots>& roles, const std::array<int32_t, kSlots>& pids) override;
    std::string kickoff_status() override;

    // tests / status
    uint64_t located() ;
    int scans() const { return scans_; }
    int finds() const { return finds_; }

private:
    void start_locate(const Context& ctx);
    void finish_locate(long long gen, uint64_t svc, const std::string& how, const std::string& why);
    void scan_for_service(long long gen);
    void log(const std::string& s);

    Host h_;
    std::mutex mu_;
    std::string anchors_why_;
    bool anchors_checked_ = false;
    uint64_t svc_ = 0;
    long long svc_gen_ = -1;
    std::string how_, locate_why_;
    bool locating_ = false;
    long long locating_gen_ = -1;
    std::vector<ApplyResult> results_;
    bool busy_ = false;
    std::string session_off_;
    std::string kickoff_;
    bool kickoff_running_ = false;
    std::atomic<int> scans_{0}, finds_{0};
};

}  // namespace tsheet
}  // namespace turbo
