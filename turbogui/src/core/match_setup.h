// FC 27 LE Turbo GUI - match setup and gameplay switches (platform-independent part; src/win/match_setup_win.cpp
// resolves the anchors, makes the game calls on the game thread and installs the result hooks).
//
// Two mechanisms, both proven in FC27.exe 1.0.140.64835 (docs/re/match_setup.md, static analysis + live reads):
//
// 1. Game variables (namespace gv). The game keeps one store of named tuning / debug integers ("GameVars", object
//    0x14D26E510): +0x00 u8 enabled, +0x18 the hash table, +0x20 / +0x28 / +0x30 begin / end / cursor of the arena the
//    values and names live in. Code reads them with GetInt(name, default) 0x140856DB4 under a reader-writer lock
//    (0x14C193230): when the table holds the name with a value the stored value wins, otherwise the caller's default.
//    The match code asks for OVERRIDE/WEATHER, OVERRIDE/TOD, OVERRIDE_MATCH_DIFFICULTY (default -1 = "not set"),
//    NEVER_INJURE, the GAMEPLAY_CUSTOMIZATION/INJURY_* sliders and DISABLE_CPU_SUBSTITUTION this way. None of them
//    exists in the live table (1408 nodes, none of those names), so Turbo adds them with the game's own
//    SetInt(store, name, value) 0x14154F384 (a game call on the game thread: it takes the writer lock, allocates the
//    value and the name in the store's arena and links a node from the table's free list). Before the call Turbo checks
//    the store, the table's shape, the arena room and the free list, because the game's insert writes through a null
//    pointer when either runs out. Clearing a variable Turbo added unlinks its value (node +0x08 = 0, under the writer
//    lock, the lock word taken with the game's own protocol): GetInt then returns the caller's default again; the value
//    object stays in the arena and is linked back on the next set (nothing leaks on set / clear cycles). A variable the
//    game itself declared is set back to its previous value with SetInt.
//    Table (the store's +0x18; "gamevar_get_int" +0x2E reads the same global): +0 u32 mask (buckets - 1), +4 u32 seed
//    5381, +0x10 node pool, +0x18 node* buckets[], +0x20 free-list head (next at node +0x10). Node {u32 hash; var*;
//    next*; name*}; var {u32 type (2 = int); i32 value at +8; +0x10 bound C++ variables}. Hash: djb2 over the name
//    INCLUDING its terminating NUL (h = 5381; for each byte and the 0: h = h * 33 + byte).
//
// 2. Result fixing (namespace mfix). Every match result reaches the competition engine (FCE) as an
//    FCEI::RequestUpdateMatchResult (type 0x2A: +0x10 type, +0x20 home goals, +0x24 away goals, +0x30 extra-time goals
//    {home, away}, +0x38 penalties {home, away} (-1 = none), +0x70 fixture id), whether the user played it or a
//    simulation produced it. The scheduling manager's HandleMessage (vtable 0x14B1854A8 slot 5, 0x148A4E80C) writes the
//    fixture's score from it; the standings manager's (vtable 0x14B1852E8 slot 5, 0x148A4E8FC) adds the outcome to the
//    two table rows. Turbo hooks both: when the request's fixture is in the fix table and the match ended in regular
//    time, the goals are rewritten before the game's code reads them, so fixture and table agree. Player statistics,
//    the match report and news keep the real match: documented limit.
//
// Everything here is pure (turbo::Memory and the Caller interface) so the native tests run it on synthetic memory.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mem.h"

namespace turbo {

// ---------------------------------------------------------------- game variables
namespace gv {

constexpr uint32_t kSeed = 0x1505;  // djb2 seed the game's table carries at +4
// the store ("GameVars" object)
constexpr uint64_t kObjEnabled = 0x00, kObjTable = 0x18, kObjArenaBegin = 0x20, kObjArenaEnd = 0x28, kObjArenaCur = 0x30;
// the table
constexpr uint64_t kOffMask = 0x00, kOffSeed = 0x04, kOffPool = 0x10, kOffBuckets = 0x18, kOffFree = 0x20;
constexpr uint64_t kTableSize = 0x28;
constexpr uint64_t kNodeHash = 0x00, kNodeVar = 0x08, kNodeNext = 0x10, kNodeName = 0x18, kNodeSize = 0x20;
constexpr uint64_t kVarType = 0x00, kVarValue = 0x08, kVarSize = 0x18;
constexpr uint32_t kTypeInt = 2;            // SetInt writes type 2 (SetFloat 0x14154F324 writes 3)
constexpr uint32_t kLockFree = 0x01000000;  // the lock word with no reader and no writer
constexpr uint32_t kMinBuckets = 64, kMaxBuckets = 1u << 20;
constexpr int kMaxChain = 64;    // a bucket chain longer than this is not the table
constexpr size_t kMaxName = 96;  // longest name Turbo sets (the game's are < 64)
// signature offsets (core/sigscan.cpp)
constexpr uint64_t kRipGetIntLock = 0x1C, kRipGetIntTable = 0x2E, kRipSetIntLock = 0x15;

// djb2 as the game computes it, the terminating NUL included
uint32_t hash(const std::string& name, uint32_t seed = kSeed);

// Anchors resolved from the signature table (src/win/match_setup_win.cpp)
struct Fns {
    uint64_t get_int = 0;     // GetInt(name, default) 0x140856DB4 (proof of the hash and the lock)
    uint64_t set_int = 0;     // SetInt(store, name, value) 0x14154F384: the game call
    uint64_t object = 0;      // the store 0x14D26E510 (lea r13 in the injury setup 0x140FEBDB4)
    uint64_t table_slot = 0;  // the global GetInt reads the table from (0x14D26E528 = object + 0x18)
    uint64_t lock = 0;        // GetInt's lock word (0x14C193230)
    uint64_t set_lock = 0;    // SetInt's lock word (must be the same)
    // the first missing anchor, nullptr when all are there
    const char* missing() const;
    // anchors that disagree ("" = consistent): table_slot == object + 0x18 and lock == set_lock
    std::string inconsistent() const;
};

struct Table {
    uint64_t slot = 0;  // the global holding the table pointer
    uint64_t addr = 0;  // the table
    uint32_t mask = 0, seed = 0;
    uint64_t buckets = 0, pool = 0, free_head = 0;
    uint32_t count = 0;  // nodes seen by the validation walk (all buckets, bounded chains)
    bool ok() const { return addr != 0 && buckets != 0; }
};

struct Node {
    uint64_t addr = 0;
    uint32_t hash = 0;
    uint64_t var = 0, next = 0, name = 0;
};

// Read the table through its pointer slot: "" and `out` when it has the expected shape (mask + 1 a power of two in
// [64, 1M], seed 5381, bucket array readable, every node in the bucket its hash names, chains bounded), else the reason
std::string locate(Memory& mem, uint64_t slot, Table& out);
// Re-check a located table (same pointer, same mask / seed / buckets); false = locate again
bool validate(Memory& mem, const Table& t);
// The node of `name`: false when absent (err empty) or when the chain is unreadable / too long (err set)
bool find(Memory& mem, const Table& t, const std::string& name, Node& out, std::string& err);
// Value of a node's var (int32 at var+8); false when the node has no var or it is unreadable
bool read_int(Memory& mem, const Node& n, int32_t& value, uint32_t* type = nullptr);
// Room for one new value + name in the store's arena (exactly what SetInt allocates for a new entry): "" or the reason
std::string arena_room(Memory& mem, uint64_t object, size_t name_len);

// What the host does for the core: the game call and the writer lock (src/win/match_setup_win.cpp; tests fake it)
class Caller {
public:
    virtual ~Caller() = default;
    // SetInt(store, name, value) on the game thread. false + err when it could not run
    virtual bool set_int(const Fns& fns, const std::string& name, int32_t value, std::string& err) = 0;
    // Take / release the store's writer lock with the game's protocol (free = 0x01000000 -> 0; release adds it back).
    // try_lock gives up after a bounded spin; false = the lock stayed busy
    virtual bool try_lock(uint64_t lock) = 0;
    virtual void unlock(uint64_t lock) = 0;
};

// One variable Turbo manages
struct Override {
    std::string name;
    uint32_t hash = 0;
    uint64_t node = 0, var = 0;
    uint64_t saved_var = 0;      // the value object of a cleared Turbo entry (linked back on the next set)
    int32_t value = 0, previous = 0;
    bool game_declared = false;  // the game had the variable with a value: clear restores `previous`
    bool active = false;
};

// "" when the name is one Turbo may set: 1..95 characters of A-Z 0-9 _ / (the game stores names upper-case)
std::string check_name(const std::string& name);
// Set `name = value` through the game's SetInt after every check above; `ov` records what is needed to undo it (a
// record of the same name is reused). "" = done and read back, else the reason (nothing was called then)
std::string apply(Memory& mem, Caller& caller, const Fns& fns, const std::string& name, int32_t value, Override& ov);
// Undo one override: a Turbo entry is unlinked (GetInt returns the caller's default again), a game variable gets its
// previous value back. "" = done
std::string clear(Memory& mem, Caller& caller, const Fns& fns, Override& ov);
// Current value of `name` in the live table: false when absent / no value / unreadable
bool current(Memory& mem, const Fns& fns, const std::string& name, int32_t& value);

// The variables Turbo offers (each read site proven in docs/re/match_setup.md). The host sets no other name (2.4: the per-side
// GAMEPLAY_CUSTOMIZATION slider family of tactics channel A was removed: the match ignores it)
struct KnownVar {
    const char* name;
    const char* label;  // short, for the UI
    const char* what;   // one line for the user
    const char* when;   // when the game reads it
    int32_t min, max;   // range the UI allows (the game's own checks are documented)
    int32_t off;        // the value that means "the game decides" (also what the caller passes as default)
};
const std::vector<KnownVar>& known_vars();
const KnownVar* known(const std::string& name);

}  // namespace gv

// ---------------------------------------------------------------- result fixing
namespace mfix {

constexpr int32_t kRequestType = 0x2A;  // FCEI::RequestUpdateMatchResult
constexpr uint64_t kOffType = 0x10, kOffHome = 0x20, kOffAway = 0x24, kOffExtra = 0x30, kOffPens = 0x38, kOffFixture = 0x70;
constexpr int32_t kMaxFixture = 20000, kMaxGoals = 30;
// FCE manager vtables of build kRvaBuild (image-relative, core/sigscan.h): the hook only rewrites when `this` carries the
// expected one. On any other build they are not the managers' (6AC07E31 skipped every fixed result with "not the expected
// FCE manager"): there the manager is identified by its vtable's slot 5 holding the hooked HandleMessage itself.
constexpr uint64_t kRvaSchedulingVtable = 0xB1854A8, kRvaStandingsVtable = 0xB1852E8;
constexpr uint64_t kVtableSlotHandleMessage = 0x28;  // slot 5 (both managers, checked in the 6AB9813C image)
// Who may own a hooked HandleMessage: `vtable` (exact, 0 = not checked) and `handler` (the hooked function, signature-
// resolved: the object's vtable must hold it in slot 5; 0 = not checked). Both 0 = no check (tests).
// LIMITATION of the slot-5 check (2.2 review): when the game reaches the hooked HandleMessage by virtual dispatch through
// `self`'s vtable, slot 5 holds the hooked function by construction, so the check cannot fail for such a call. It only
// identifies the class because each of the two handlers is referenced by exactly one vtable: verified in the 6AB9813C image
// (0x148A4E80C only in 0x14B1854D0, 0x148A4E8FC only in 0x14B185310), NOT verified on 6AC07E31 (its exe is encrypted on
// disk). What still guards a rewrite there: the message must be a RequestUpdateMatchResult (type 0x2A) of a fixture the
// user fixed, regular time only, both goal ints read back after the write (apply_to_request), the kill switch
// turbo_output\call_match_fix_off.txt, and the log line of each rewrite / skip (PairLog: fixture and table disagreeing).
struct Owner {
    uint64_t vtable = 0, handler = 0;
};
// "" when `self` is such an owner, else why not
std::string check_owner(Memory& mem, uint64_t self, const Owner& owner);

struct Fix {
    uint16_t fixture = 0;
    int8_t home = 0, away = 0;
    std::string label;  // for the user ("Napoli v Roma, 22.08.2026"); may be empty
    long long applied = 0;  // results rewritten (the two managers each count once)
};

// The fixes in force. The host swaps whole tables under the hooks (one shared_ptr<const FixTable> read per message).
class FixTable {
public:
    std::string set(uint16_t fixture, int home, int away, const std::string& label = "");  // "" or the reason
    bool erase(uint16_t fixture);
    void clear() { fixes_.clear(); }
    const Fix* find(uint16_t fixture) const;
    Fix* find(uint16_t fixture);
    const std::vector<Fix>& all() const { return fixes_; }
    bool empty() const { return fixes_.empty(); }
    size_t size() const { return fixes_.size(); }

private:
    std::vector<Fix> fixes_;
};

struct Applied {
    uint16_t fixture = 0;
    int32_t old_home = 0, old_away = 0, home = 0, away = 0;
    bool changed = false;     // the goals were rewritten and read back (false = already equal, or skipped)
    std::string skipped;      // why a fixed fixture's result was left alone ("" = not skipped)
};

// Inspect one message the FCE manager `self` is about to handle. When it is a RequestUpdateMatchResult for a fixed
// fixture that ended in regular time, rewrite its goals. `owner` = who may own the handler (check_owner). Returns true
// when the message was a result for a fixed fixture (changed / skipped say what happened). Reads the type first; an
// unreadable or foreign message is left alone. 2.2 review: both goal ints are read back after the write; a failed write
// or a mismatch puts the played goals back (read back as well) and marks the result skipped ("... put back as played" or
// "... putting the played result back FAILED").
// `hold` not "" = leave a fixed result as played with that reason, nothing written (2.2 review: the host passes "the other
// result hook is off: left as played" while the partner hook is switched off, so fixture and table cannot disagree).
bool apply_to_request(Memory& mem, uint64_t self, const Owner& owner, uint64_t msg, const FixTable& fixes, Applied& out,
                      const std::string& hold = std::string());
// The same with the manager's exact vtable only (0 = not checked)
inline bool apply_to_request(Memory& mem, uint64_t self, uint64_t expected_vtable, uint64_t msg, const FixTable& fixes, Applied& out) {
    Owner o;
    o.vtable = expected_vtable;
    return apply_to_request(mem, self, o, msg, fixes, out);
}

// The two hooks of one result (2.2 review): the scheduling manager's HandleMessage writes the fixture's score, the
// standings manager's the table rows. When one rewrites a fixed result and the other leaves it as played, the fixture and
// the table disagree: note() returns the line the host logs (turbo_gui.log "result fixing: fixture N: ... disagree"), so
// result fixing on a new build can be judged from the log. Not thread-safe (the host holds its lock).
enum class Hook { Scheduling = 0, Standings = 1 };
class PairLog {
public:
    // One hook's outcome for `a.fixture` (an apply_to_request that returned true). "" until both hooks reported that
    // fixture; then the pair is forgotten and the line is returned when exactly one of them left the result as played.
    std::string note(Hook hook, const Applied& a);
    size_t waiting() const { return open_.size(); }  // fixtures seen by one hook only (bounded: the oldest are dropped)
    static constexpr size_t kMaxOpen = 64;

private:
    struct Entry {
        uint16_t fixture = 0;
        bool seen[2] = {false, false};
        Applied got[2];
    };
    std::vector<Entry> open_;
};

}  // namespace mfix

// ---------------------------------------------------------------- what the App uses (host implements, tests fake)
namespace msetup {

struct VarLine {
    std::string name;
    int32_t value = 0, previous = 0;
    bool active = false, game_declared = false;
};

struct VarResult {
    bool ok = false;
    std::string name, message;
    int32_t value = 0;
    bool cleared = false;
};

class Service {
public:
    virtual ~Service() = default;
    // Queue (or run on the game thread) `name = value` / clear. false + why when it cannot run at all (kill switch,
    // anchor missing); the outcome arrives through poll()
    virtual bool set_var(const std::string& name, int32_t value, std::string& why) = 0;
    virtual bool clear_var(const std::string& name, std::string& why) = 0;
    virtual bool clear_all(std::string& why) = 0;
    virtual bool poll(VarResult& out) = 0;
    virtual std::vector<VarLine> vars() = 0;  // overrides known to the host (active or cleared this session)
    // Result fixing: the hooks are installed on the first fix (opt-in); the new table is used from the next message on
    virtual bool fix(uint16_t fixture, int home, int away, const std::string& label, std::string& why) = 0;
    virtual bool unfix(uint16_t fixture) = 0;
    virtual void unfix_all() = 0;
    virtual std::vector<mfix::Fix> fixes() = 0;
    virtual long long fixes_applied() = 0;            // results rewritten so far
    virtual bool fixing_ready(std::string* why) = 0;  // the result hooks can be (or are) installed
    virtual bool vars_ready(std::string* why) = 0;    // the variable store is reachable
    virtual std::string status() = 0;                 // one line for the UI
};

}  // namespace msetup

}  // namespace turbo
