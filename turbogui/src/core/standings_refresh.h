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
// the UI listens to. Turbo calls it for every key the map already holds (the ids the game itself requested with; the
// row's compObjId is not necessarily a key). When the map cannot be walked it falls back to the SVM's career-event
// listener (svm, 29 POST_LOAD_PREPARE, nullptr) 0x147da0e10 ("svm_listener"): the game's own load-time full refresh.
//
// Locating the SVM: Turbo's Lua side publishes it in bridge_state.json ("svm", mem.manager(108)) with the manager
// table ("managers"); the DLL can also walk the table itself from the comm service (managers = [[comm+0x20]+0x10],
// slot = managers + 0x20*108: +0x10 instance count == 1, +0x18 holder -> svm). Before anything is called the object is
// validated: readable, vtable (lea in the SVM constructor, "svm_vtable", or the RVA below), svm+0x08 == managers, the
// table's slot 108 points back at it, and the FCE interface Turbo wrote to is the table's slot 1 (the career's).
// Every read goes through turbo::Memory; the calls are behind Caller so the sequence is tested on synthetic memory.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mem.h"

namespace turbo {
namespace svm {

// StandingsViewManager and manager-table layout, FC27.exe 1.0.140.64835 (docs/re/standings-ui-path.md, [H])
constexpr int kTypeId = 108;      // ENUM_FCEGameModesFCECareerModeStandingsViewManager
constexpr int kIfceTypeId = 1;    // IFCEInterface (the holder the career's requests go through)
constexpr uint64_t kCommManagersA = 0x20, kCommManagersB = 0x10;  // managers = [[comm+0x20]+0x10]
constexpr uint64_t kSlotSize = 0x20, kSlotType = 0x08, kSlotCount = 0x10, kSlotHolder = 0x18;
constexpr uint64_t kTypeFlag = 0x10;  // type descriptor +0x10 == 1 (Live Editor's walk checks it)
constexpr uint64_t kOffCtx = 0x08;        // manager table ("ctx")
constexpr uint64_t kOffEnabled = 0x18;    // u8: the screen feed (vtable slot 10) returns early when 0
constexpr uint64_t kOffMap = 0x250;       // eastl::rbtree anchor: +0 rightmost, +8 leftmost, +0x10 root
constexpr uint64_t kOffMapRoot = 0x260;
constexpr uint64_t kOffMapSize = 0x270;   // u32 node count (anchor+0x20)
constexpr uint64_t kOffCritSec = 0x280;   // CRITICAL_SECTION guarding the map (the game thread owns it: no lock needed)
constexpr uint64_t kOffLastField = 0x490; // u8 "LiveTableFirstUpdate posted": the object spans at least this far
constexpr uint64_t kNodeRight = 0x00, kNodeLeft = 0x08, kNodeParent = 0x10, kNodeKey = 0x20, kNodeValue = 0x28;
constexpr uint64_t kRvaVtable = 0xB0160D8;  // StandingsViewManager vtable (image-relative; used when no signature)
constexpr uint64_t kVtableSlot10 = 0x50;    // slot 10 = the Standings screen feed ("svm_slot10")
constexpr size_t kMaxKeys = 64;             // a career holds a handful of competitions; more = not the map
constexpr int32_t kEventPostLoadPrepare = 29;

// Game functions / anchors resolved by signature on the running build (0 = unknown)
struct Fns {
    uint64_t refresh_comp = 0;  // void (SVM*, int compObjId): immediate 'rmvs' RequestGetStandings
    uint64_t listener = 0;      // void (SVM*, int eventId, Event*): the SVM's career-event listener (fallback)
    uint64_t vtable = 0;        // SVM vtable (optional: the RVA + image base is used when 0)
    uint64_t slot10 = 0;        // the function the vtable's slot 10 must hold (optional second vtable check)
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
    uint64_t image_base = 0;  // FC27.exe base for the vtable RVA when Fns::vtable is 0; 0 = skip the vtable check
    bool allow_fallback = true;  // call the listener (event 29) when the map cannot be walked or is empty
    std::string label;        // what was edited (for the message); may be empty
};

struct Result {
    bool ok = false;
    std::string stage;    // where it stopped: "validate", "walk", "refresh", "fallback", "done" (host: "off", "queued")
    std::string message;  // for the user (toast / log / Lua)
    uint64_t svm = 0, managers = 0;
    std::vector<int32_t> keys;  // compObjIds found in the map
    int refreshed = 0;          // refresh_comp calls that ran
    bool fallback = false;      // the listener was called
    bool enabled = true;        // svm+0x18 (informational)
};

// Manager table of the comm service: [[comm+0x20]+0x10]; 0 when unreadable
uint64_t manager_table(Memory& mem, uint64_t comm);
// Manager object of a type id from the table (instance count == 1, type flag == 1, holder -> object); 0 and `err`
// when the slot is empty or unreadable
uint64_t manager_at(Memory& mem, uint64_t managers, int type_id, std::string& err);
// Locate the SVM through the table (manager_at(kTypeId)); "" and `out` on success, else the reason
std::string locate(Memory& mem, uint64_t managers, uint64_t& out);
// Validate an SVM pointer: readable to +0x490, vtable (0 = skip), slot10 (0 = skip), back-pointer to `managers`
// (0 = take the object's own and check that its slot 108 holds the object). "" = fine, else the reason
std::string validate(Memory& mem, uint64_t svm, uint64_t managers, uint64_t vtable, uint64_t slot10);
// Keys of mLiveStandings in order (bounded in-order walk of the rbtree; every node's parent link, the ascending key
// order and the node count are checked). "" = fine (an empty map gives no keys), else the inconsistency (keys cleared)
std::string map_keys(Memory& mem, uint64_t svm, std::vector<int32_t>& keys);

// The whole call (see the file comment). Never throws.
Result refresh(Memory& mem, Caller& call, const Fns& fns, const Request& req);

// What the App uses (the Windows host implements it over the game thread; tests fake it)
class RefreshService {
public:
    virtual ~RefreshService() = default;
    // Queue the refresh (or run it at once on the game thread). false + why when it cannot run at all (not installed,
    // kill switch, signatures missing); true = running / queued, the outcome arrives through poll()
    virtual bool request(const Request& req, std::string& why) = 0;
    // Outcomes finished since the last poll, oldest first
    virtual bool poll(Result& out) = 0;
    // One line for the Status tab
    virtual std::string status() = 0;
};

}  // namespace svm
}  // namespace turbo
