// FC 27 LE Turbo GUI - "standings_refresh" game call: make FC 27's Standings screen and Office tile show an edited
// live table (platform-independent part; src/win/standings_refresh_win.cpp resolves the functions and runs the call on
// the game thread).
//
// Why (docs/re/standings-ui-path.md): the Standings screen never asks the competition engine (FCE). It reads a cache,
// StandingsViewManager::mLiveStandings (career manager type id 108, "SVM"), an eastl::map<int compObjId,
// LiveStandings*> at svm+0x250 that the game rebuilds only from ResponseStandingsList replies it requests on career
// events 22 / 23 / 29 / 33 and on match days. A row Turbo writes into FCE::DataManager (core/fce_standings.h) is
// therefore correct but invisible until the next match day. The game's own refresh is
//   StandingsViewManager::RequestStandingsSync(svm, compObjId)   0x147da5310 on 1.0.140.64835 ("svm_refresh_comp")
// an immediate FCEI::RequestGetStandings (tag 'rmvs'): FCE answers inside the call, the SVM replaces
// mLiveStandings[compObjId] with a fresh, re-sorted clone of the live rows and posts the LiveTableUpdate career event
// the UI listens to. Turbo calls it for every key the map already holds (the ids the game itself requested with).
//
// The crash of 04-10-2026 (docs/re/standings-ui-path.md section 0): Turbo's "SVM vtable" was the StaffManager's
// (0x14B0160D8, the class constructed right before the SVM in the career code), so the type check picked the
// StaffManager (slot 107), refused the real SVM (slot 108, vtable 0x14975EA38), walked a non-map at staff+0x250 and
// ran the fallback (the SVM's career-event listener, event 29) on the StaffManager: EnterCriticalSection(staff+0x280)
// on bytes that are no critical section raised STATUS_INVALID_HANDLE. Hence the rules below:
//   * the SVM is the object in manager slot 108 (Live Editor's type id, confirmed by the game's own registration
//     code) and must carry the SVM vtable (from the SVM constructor, "svm_vtable") whose slot 1 is the listener
//     function Turbo resolved by signature ("svm_listener"): the vtable and the function identify each other;
//   * every word the game call dereferences is checked first: svm+0x08 == the manager table, the table's slot 108
//     holds the object, the critical section at +0x280 is initialised and free, the map nodes link back and hold
//     LiveStandings objects (their vtable), [[ctx+0x38]] is the FCE interface Turbo wrote to and its vtable slot 4
//     is the game's post function ("fce_iface_post"), the game allocator global ("svm_allocator") holds an object
//     with a vtable;
//   * the call never runs while the SimDayManager (type 103) is processing a day (state != 0) and at most one
//     refresh is in flight (OneShotGate);
//   * the full refresh through the listener (event 29 POST_LOAD_PREPARE) is opt-in (Request::allow_fallback, the
//     host turns it on only with turbo_output\call_standings_refresh_full.txt); by default an empty or unreadable
//     map is reported, nothing else is called.
// Every read goes through turbo::Memory; the calls are behind Caller so the sequence is tested on synthetic memory.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mem.h"

namespace turbo {
namespace svm {

// StandingsViewManager and manager-table layout, FC27.exe 1.0.140.64835 (docs/re/standings-ui-path.md, [H], verified in
// the live game on 04-10-2026)
constexpr int kTypeId = 108;      // ENUM_FCEGameModesFCECareerModeStandingsViewManager (the game stores the SVM there)
constexpr int kIfceTypeId = 1;    // IFCEInterface (the holder the career's requests go through)
constexpr int kSimDayTypeId = 103;  // SimDayManager: its state machine processes match days
constexpr int kMaxSlots = 200;    // manager table slots walked when looking for the holder of an object
constexpr uint64_t kCommManagersA = 0x20, kCommManagersB = 0x10;  // managers = [[comm+0x20]+0x10]
constexpr uint64_t kSlotSize = 0x20, kSlotType = 0x08, kSlotCount = 0x10, kSlotHolder = 0x18;
constexpr uint64_t kTypeFlag = 0x10;  // type descriptor +0x10 == 1 (Live Editor's walk checks it)
constexpr uint64_t kOffCtx = 0x08;        // manager table ("ctx")
constexpr uint64_t kOffPendingTags = 0x10;  // 27 u8 "request pending" flags (+0x18 is tag index 8, not an enable flag)
constexpr uint64_t kOffMap = 0x250;       // eastl::rbtree anchor: +0 rightmost, +8 leftmost, +0x10 root, +0x20 count
constexpr uint64_t kOffMapRoot = 0x260;
constexpr uint64_t kOffMapSize = 0x270;   // u32 node count (anchor+0x20)
constexpr uint64_t kOffCritSec = 0x280;   // CRITICAL_SECTION guarding the map (InitializeCriticalSectionAndSpinCount 0x100)
constexpr uint64_t kOffUserCopy = 0x488;  // LiveStandings* copy of the user's competition
constexpr uint64_t kOffFirstUpdate = 0x490;  // u8 "LiveTableFirstUpdate posted"
constexpr uint64_t kObjectSize = 0x4B8;   // allocation size in the manager factory (0x147b01b17)
constexpr uint64_t kNodeRight = 0x00, kNodeLeft = 0x08, kNodeParent = 0x10, kNodeKey = 0x20, kNodeValue = 0x28;
// CRITICAL_SECTION: +0 DebugInfo, +8 i32 LockCount (-1 = free), +0xC i32 RecursionCount, +0x10 OwningThread,
// +0x18 LockSemaphore, +0x20 SpinCount
constexpr uint64_t kCsLockCount = 0x08, kCsRecursion = 0x0C, kCsOwner = 0x10;
constexpr uint64_t kRvaVtable = 0x975EA38;          // StandingsViewManager vtable (ctor 0x147d9a700 writes it)
constexpr uint64_t kRvaLiveStandingsVtable = 0xB016148;  // the map values
constexpr uint64_t kVtableSlotListener = 0x08;  // vtable slot 1 = OnCareerEvent = "svm_listener"
constexpr uint64_t kCtxIfceHolder = 0x38;       // [ctx+0x38] = holder of manager slot 1 (0x20*1 + 0x18)
constexpr uint64_t kIfceVtablePost = 0x20;      // IFCEInterface vtable slot 4 = Post(request) ("fce_iface_post")
constexpr uint64_t kAllocVtableAlloc = 0x10;    // allocator vtable slot 2 = allocate(size, name, flags)
constexpr uint64_t kOffSimDayState = 0x14;      // SimDayManager state (0 = idle; 1..9 = processing a day)
constexpr size_t kMaxKeys = 64;                 // a career holds a handful of competitions; more = not the map
constexpr int32_t kEventPostLoadPrepare = 29;

// Game functions / anchors resolved by signature on the running build (0 = unknown)
struct Fns {
    uint64_t refresh_comp = 0;  // void (SVM*, int compObjId): immediate 'rmvs' RequestGetStandings
    uint64_t listener = 0;      // void (SVM*, int eventId, Event*): the SVM's career-event listener (vtable slot 1)
    uint64_t vtable = 0;        // SVM vtable (optional: the RVA + image base is used when 0)
    uint64_t iface_post = 0;    // IFCEInterface::Post (vtable slot 4), what refresh_comp calls (optional check)
    uint64_t allocator = 0;     // the game allocator global refresh_comp reads (optional check)
    // Name of the first required function that is missing, nullptr when all are there
    const char* missing() const;
};

// The calls into the game. The Windows host calls the resolved functions (game thread only); tests fake them.
class Caller {
public:
    virtual ~Caller() = default;
    virtual bool refresh_comp(uint64_t svm, int32_t comp, std::string& err) = 0;
    virtual bool career_event(uint64_t svm, int32_t event_id, std::string& err) = 0;
};

struct Request {
    uint64_t svm = 0;         // published SVM (bridge_state.json svm); 0 = locate through the manager table
    uint64_t managers = 0;    // published manager table (bridge_state.json managers); 0 = derive from comm
    uint64_t comm = 0;        // comm service (bridge_state.json comm_service): managers = [[comm+0x20]+0x10]
    uint64_t ifce = 0;        // FCE interface Turbo wrote to (bridge_state.json ifce); cross-checked, 0 = skip
    uint64_t image_base = 0;  // FC27.exe base for the vtable RVAs when Fns::vtable is 0; 0 = skip those checks
    uint64_t image_size = 0;  // FC27.exe SizeOfImage: function pointers must fall inside; 0 = skip the range check
    bool allow_fallback = false;  // call the listener (event 29) when the map is empty (opt-in: the game's full refresh)
    std::string label;        // what was edited (for the message); may be empty
};

struct Result {
    bool ok = false;
    std::string stage;    // where it stopped: "validate", "busy", "walk", "refresh", "fallback", "done" (host: "off", "queued")
    std::string message;  // for the user (toast / log / Lua)
    uint64_t svm = 0, managers = 0;
    std::vector<int32_t> keys;  // compObjIds found in the map
    int refreshed = 0;          // refresh_comp calls that ran
    bool fallback = false;      // the listener was called
};

// Manager table of the comm service: [[comm+0x20]+0x10]; 0 when unreadable
uint64_t manager_table(Memory& mem, uint64_t comm);
// Manager object of a type id from the table (instance count == 1, type flag == 1, holder -> object); 0 and `err`
// when the slot is empty or unreadable
uint64_t manager_at(Memory& mem, uint64_t managers, int type_id, std::string& err);
// The SVM: the object of slot 108 (the only place the game stores it). "" and `out` on success, else the reason. With
// `vtable` the object must carry it (the slot is never searched for another object: see the file comment).
std::string locate(Memory& mem, uint64_t managers, uint64_t& out, uint64_t vtable = 0);
// Validate an SVM pointer: readable to +0x4B8, vtable (0 = skip) whose slot 1 is `listener` (0 = skip), back-pointer to
// `managers` (0 = take the object's own), slot 108 holds the object, the critical section at +0x280 is initialised
// and free. "" = fine, else the reason
std::string validate(Memory& mem, uint64_t svm, uint64_t managers, uint64_t vtable, uint64_t listener);
// Validate what refresh_comp dereferences besides the SVM: [[ctx+0x38]] (the FCE interface) with Post in its vtable
// (`iface_post`, 0 = skip), the allocator global (`allocator`, 0 = skip). Function pointers must be inside the image
// when image_base/size are given. "" = fine
std::string validate_request_path(Memory& mem, uint64_t svm, uint64_t ifce_expected, uint64_t iface_post, uint64_t allocator,
                                  uint64_t image_base, uint64_t image_size);
// Keys of mLiveStandings in order (bounded in-order walk of the rbtree; every node's parent link, the ascending key
// order, the node count and the values' LiveStandings vtable (`live_vtable`, 0 = skip) are checked). "" = fine (an
// empty map gives no keys), else the inconsistency (keys cleared)
std::string map_keys(Memory& mem, uint64_t svm, std::vector<int32_t>& keys, uint64_t live_vtable = 0);
// "" when the SimDayManager (type 103) is idle (state 0), else why the game is busy (missing manager counts as busy)
std::string sim_busy(Memory& mem, uint64_t managers);

// The whole call (see the file comment). Never throws.
Result refresh(Memory& mem, Caller& call, const Fns& fns, const Request& req);

// One-shot gate: an edit arms a refresh, the call takes it once; nothing runs twice for one edit and nothing runs while
// a call is in flight. The host keeps one per call; tests drive it directly.
class OneShotGate {
public:
    // Arm for one run (true) unless a run is in flight (false, why)
    bool arm(const std::string& label, std::string& why);
    // Take the armed run: false (why) when nothing is armed or a run is in flight
    bool take(std::string& why);
    // The run finished (ok or not)
    void done();
    bool armed() const { return armed_; }
    bool in_flight() const { return running_; }
    const std::string& label() const { return label_; }

private:
    bool armed_ = false, running_ = false;
    std::string label_;
};

// What the App uses (the Windows host implements it over the game thread; tests fake it)
class RefreshService {
public:
    virtual ~RefreshService() = default;
    // Queue the refresh (or run it at once on the game thread). false + why when it cannot run at all (not installed,
    // kill switch, signatures missing, one already in flight); true = running / queued, the outcome arrives through poll()
    virtual bool request(const Request& req, std::string& why) = 0;
    // Outcomes finished since the last poll, oldest first
    virtual bool poll(Result& out) = 0;
    // One line for the Status tab
    virtual std::string status() = 0;
};

}  // namespace svm
}  // namespace turbo
