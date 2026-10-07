// FC 27 LE Turbo GUI - match setup and gameplay switches (see match_setup.h, docs/re/match_setup.md)
#include "match_setup.h"

#include <cstring>

namespace turbo {

// ================================================================ game variables
namespace gv {

uint32_t hash(const std::string& name, uint32_t seed) {
    uint32_t h = seed;
    for (unsigned char c : name) h = h * 33u + c;
    return h * 33u;  // the terminating NUL: the game's loop adds the 0 before it tests it
}

const char* Fns::missing() const {
    if (!get_int) return "gamevar_get_int";
    if (!set_int) return "gamevar_set_int";
    if (!object) return "gamevar_object";
    if (!table_slot) return "gamevar_table_slot";
    if (!lock) return "gamevar_lock";
    if (!set_lock) return "gamevar_set_lock";
    return nullptr;
}

std::string Fns::inconsistent() const {
    if (table_slot != object + kObjTable) return "the store and GetInt's table global disagree";
    if (lock != set_lock) return "GetInt and SetInt use different locks";
    return "";
}

namespace {
bool power_of_two(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }

bool read_node(Memory& mem, uint64_t addr, Node& n) {
    uint8_t b[kNodeSize];
    if (!is_ptr(addr, 8) || !mem.read(addr, b, kNodeSize)) return false;
    n.addr = addr;
    std::memcpy(&n.hash, b + kNodeHash, 4);
    std::memcpy(&n.var, b + kNodeVar, 8);
    std::memcpy(&n.next, b + kNodeNext, 8);
    std::memcpy(&n.name, b + kNodeName, 8);
    return true;
}

// NUL-terminated string read in small steps (a name may end close to the end of a mapping); "" when unreadable
std::string read_name(Memory& mem, uint64_t addr) {
    std::string s;
    if (!is_ptr(addr)) return s;
    for (size_t off = 0; off < kMaxName; off += 8) {
        char b[8];
        if (!mem.read(addr + off, b, 8)) {
            for (size_t i = 0; i < 8; ++i) {  // the last bytes of a page
                char c = 0;
                if (!mem.read(addr + off + i, &c, 1)) return std::string();
                if (!c) return s;
                s += c;
            }
            continue;
        }
        for (char c : b) {
            if (!c) return s;
            s += c;
        }
    }
    return std::string();
}
}  // namespace

std::string locate(Memory& mem, uint64_t slot, Table& out) {
    out = Table();
    if (!is_ptr(slot, 8)) return "no table pointer global";
    uint64_t addr = mem.ptr(slot);
    if (!addr) return "the table pointer is not readable or not a pointer";
    uint8_t hdr[kTableSize];
    if (!mem.read(addr, hdr, kTableSize)) return "the table header is not readable";
    Table t;
    t.slot = slot;
    t.addr = addr;
    std::memcpy(&t.mask, hdr + kOffMask, 4);
    std::memcpy(&t.seed, hdr + kOffSeed, 4);
    std::memcpy(&t.pool, hdr + kOffPool, 8);
    std::memcpy(&t.buckets, hdr + kOffBuckets, 8);
    std::memcpy(&t.free_head, hdr + kOffFree, 8);
    const uint32_t n = t.mask + 1;
    if (!power_of_two(n) || n < kMinBuckets || n > kMaxBuckets) return "bucket count is not a power of two in range (mask " + std::to_string(t.mask) + ")";
    if (t.seed != kSeed) return "unexpected hash seed " + std::to_string(t.seed);
    if (!is_ptr(t.buckets, 8)) return "bucket array pointer is not a pointer";
    if (t.free_head && !is_ptr(t.free_head, 8)) return "free-list head is not a pointer";
    std::vector<uint8_t> heads;
    if (!mem.read_block(t.buckets, size_t(n) * 8, heads)) return "the bucket array is not readable";
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint64_t p = 0;
        std::memcpy(&p, heads.data() + size_t(i) * 8, 8);
        int chain = 0;
        while (p) {
            Node nd;
            if (!read_node(mem, p, nd)) return "bucket " + std::to_string(i) + ": a node is not readable";
            if ((nd.hash & t.mask) != i) return "bucket " + std::to_string(i) + ": a node's hash does not belong to it";
            if (nd.var && !is_ptr(nd.var, 4)) return "bucket " + std::to_string(i) + ": a node's value is not a pointer";
            if (++chain > kMaxChain) return "bucket " + std::to_string(i) + ": chain longer than " + std::to_string(kMaxChain);
            ++count;
            p = nd.next;
        }
    }
    t.count = count;
    out = t;
    return "";
}

bool validate(Memory& mem, const Table& t) {
    if (!t.ok()) return false;
    if (mem.ptr(t.slot) != t.addr) return false;
    uint32_t mask = 0, seed = 0;
    uint64_t buckets = 0;
    if (!mem.rd(t.addr + kOffMask, mask) || !mem.rd(t.addr + kOffSeed, seed) || !mem.rd(t.addr + kOffBuckets, buckets)) return false;
    return mask == t.mask && seed == t.seed && buckets == t.buckets;
}

bool find(Memory& mem, const Table& t, const std::string& name, Node& out, std::string& err) {
    err.clear();
    out = Node();
    if (!t.ok()) {
        err = "no table";
        return false;
    }
    const uint32_t h = hash(name, t.seed);
    uint64_t p = 0;
    if (!mem.rd(t.buckets + uint64_t(h & t.mask) * 8, p)) {
        err = "the bucket is not readable";
        return false;
    }
    int chain = 0;
    while (p) {
        Node nd;
        if (!read_node(mem, p, nd)) {
            err = "a node of the chain is not readable";
            return false;
        }
        if (nd.hash == h) {  // the game compares the hash only, like this
            out = nd;
            return true;
        }
        if (++chain > kMaxChain) {
            err = "chain too long";
            return false;
        }
        p = nd.next;
    }
    return false;
}

bool read_int(Memory& mem, const Node& n, int32_t& value, uint32_t* type) {
    if (!n.var || !is_ptr(n.var, 4)) return false;
    uint32_t ty = 0;
    if (!mem.rd(n.var + kVarType, ty) || !mem.rd(n.var + kVarValue, value)) return false;
    if (type) *type = ty;
    return true;
}

std::string arena_room(Memory& mem, uint64_t object, size_t name_len) {
    uint64_t begin = 0, end = 0, cur = 0;
    if (!mem.rd(object + kObjArenaBegin, begin) || !mem.rd(object + kObjArenaEnd, end) || !mem.rd(object + kObjArenaCur, cur))
        return "the store's arena is not readable";
    if (!is_ptr(begin) || !is_ptr(end) || !is_ptr(cur) || cur < begin || end < cur) return "the store's arena pointers are not consistent";
    // SetInt: the value first (cursor + 0x18 < end), then the name (len + 1 rounded up to 4, new cursor < end)
    const uint64_t need = kVarSize + ((uint64_t(name_len) + 1 + 3) & ~uint64_t(3));
    if (cur + need >= end) return "the store's arena is full (" + std::to_string(end - cur) + " bytes left)";
    return "";
}

std::string check_name(const std::string& name) {
    if (name.empty() || name.size() >= kMaxName) return "the name must be 1.." + std::to_string(kMaxName - 1) + " characters";
    for (char c : name)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/'))
            return "the name may only hold A-Z, 0-9, _ and / (the game stores names upper-case)";
    return "";
}

namespace {
// The store and its table, checked: "" and t, else the reason
std::string open_store(Memory& mem, const Fns& fns, Table& t) {
    if (const char* m = fns.missing()) return std::string("anchor ") + m + " is not resolved";
    std::string err = fns.inconsistent();
    if (!err.empty()) return err;
    uint8_t enabled = 0;
    if (!mem.rd(fns.object + kObjEnabled, enabled)) return "the variable store is not readable";
    if (!enabled) return "the game's variable store is switched off (its SetInt would do nothing)";
    uint32_t lockword = 0;
    if (!mem.rd(fns.lock, lockword)) return "the store's lock is not readable";
    err = locate(mem, fns.table_slot, t);
    if (!err.empty()) return "variable table: " + err;
    return "";
}

// Under the writer lock: store `value` at node+8 when the node still has hash `h` and var `expect`
std::string relink(Memory& mem, Caller& caller, const Fns& fns, uint64_t node, uint32_t h, uint64_t expect, uint64_t value) {
    if (!caller.try_lock(fns.lock)) return "the variable store is busy (its lock stayed taken): try again";
    std::string err;
    Node nd;
    if (!read_node(mem, node, nd)) err = "the node is not readable any more";
    else if (nd.hash != h) err = "the node changed under Turbo";
    else if (nd.var != expect) err = "the node's value changed under Turbo";
    else if (!mem.wr(node + kNodeVar, value)) err = "writing the node failed";
    caller.unlock(fns.lock);
    return err;
}
}  // namespace

std::string apply(Memory& mem, Caller& caller, const Fns& fns, const std::string& name, int32_t value, Override& ov) {
    std::string err = check_name(name);
    if (!err.empty()) return err;
    Table t;
    err = open_store(mem, fns, t);
    if (!err.empty()) return err;
    if (ov.name != name) {
        ov = Override();
        ov.name = name;
    }
    ov.hash = hash(name, t.seed);
    Node nd;
    const bool exists = find(mem, t, name, nd, err);
    if (!err.empty()) return "finding " + name + ": " + err;
    if (exists && nd.name) {
        const std::string stored = read_name(mem, nd.name);
        if (stored != name) return "the table holds another name with the same hash (" + stored + "): refused";
    }
    bool created = false, relinked = false;
    if (exists && nd.var) {
        uint32_t ty = 0;
        int32_t cur = 0;
        if (!read_int(mem, nd, cur, &ty)) return "the variable's value is not readable";
        if (ty != kTypeInt) return "the variable is not an integer (type " + std::to_string(ty) + "): refused";
        const bool ours = ov.node == nd.addr && ov.var == nd.var && !ov.game_declared;
        if (!ov.active && !ours) {
            ov.previous = cur;
            ov.game_declared = true;
        }
    } else if (exists) {
        // a node without a value: Turbo's own cleared entry gets its value object back (SetInt then updates it in
        // place); any other such node gets a new value from SetInt
        if (ov.saved_var && ov.node == nd.addr) {
            err = relink(mem, caller, fns, nd.addr, ov.hash, 0, ov.saved_var);
            if (!err.empty()) return err;
            relinked = true;
        } else {
            err = arena_room(mem, fns.object, name.size());
            if (!err.empty()) return err;
        }
        created = !ov.game_declared;
    } else {
        err = arena_room(mem, fns.object, name.size());
        if (!err.empty()) return err;
        // the insert takes a node from the free list and writes through it: an empty list would be a null write
        uint64_t next = 0;
        if (!t.free_head) return "the variable table has no free node left";
        if (!mem.rd(t.free_head + kNodeNext, next)) return "the table's free list is not readable";
        created = true;
    }
    if (!caller.set_int(fns, name, value, err)) {
        // the old value must not stay live: unlink the value object linked back above
        if (relinked) relink(mem, caller, fns, nd.addr, ov.hash, ov.saved_var, 0);
        return "SetInt: " + (err.empty() ? std::string("the call did not run") : err);
    }
    // read back
    Node after;
    if (!validate(mem, t) && !locate(mem, fns.table_slot, t).empty()) return "the variable table changed during the call";
    if (!find(mem, t, name, after, err) || !after.var) return "after SetInt the table does not hold " + name + (err.empty() ? "" : ": " + err);
    int32_t back = 0;
    uint32_t ty = 0;
    if (!read_int(mem, after, back, &ty) || ty != kTypeInt || back != value)
        return "after SetInt " + name + " reads " + std::to_string(back) + " (type " + std::to_string(ty) + "), not " + std::to_string(value);
    if (created) ov.game_declared = false;
    ov.node = after.addr;
    ov.var = after.var;
    ov.saved_var = 0;
    ov.value = value;
    ov.active = true;
    return "";
}

std::string clear(Memory& mem, Caller& caller, const Fns& fns, Override& ov) {
    if (!ov.active) return "";
    Table t;
    std::string err = open_store(mem, fns, t);
    if (!err.empty()) return err;
    Node nd;
    if (!find(mem, t, ov.name, nd, err) || nd.addr != ov.node) return "the variable's node is not where Turbo left it" + (err.empty() ? "" : ": " + err);
    if (ov.game_declared) {
        if (!caller.set_int(fns, ov.name, ov.previous, err)) return "SetInt: " + (err.empty() ? std::string("the call did not run") : err);
        int32_t back = 0;
        Node after;
        if (!find(mem, t, ov.name, after, err) || !read_int(mem, after, back) || back != ov.previous)
            return "after SetInt " + ov.name + " does not read its previous value " + std::to_string(ov.previous);
        ov.active = false;
        return "";
    }
    err = relink(mem, caller, fns, nd.addr, ov.hash, ov.var, 0);
    if (!err.empty()) return err;
    ov.saved_var = ov.var;
    ov.active = false;
    return "";
}

bool current(Memory& mem, const Fns& fns, const std::string& name, int32_t& value) {
    if (fns.missing() || !check_name(name).empty()) return false;
    Table t;
    if (!locate(mem, fns.table_slot, t).empty()) return false;
    Node nd;
    std::string err;
    uint32_t ty = 0;
    return find(mem, t, name, nd, err) && read_int(mem, nd, value, &ty) && ty == kTypeInt;
}

const std::vector<KnownVar>& known_vars() {
    // read sites: docs/re/match_setup.md section 2 (every one a GetInt call with the name as a literal)
    static const std::vector<KnownVar> k = {
        {"NEVER_INJURE", "Injuries off", "1 = no player gets injured in the played match (the match's injury setup reads it)",
         "at kick-off", 0, 1, 0},
        {"GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER", "Injury frequency (your team)",
         "0..100, replaces the Gameplay slider (also beyond the menu's range); cleared = the slider again", "at kick-off", 0, 100, -1},
        {"GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI", "Injury frequency (CPU team)", "0..100, replaces the slider", "at kick-off", 0, 100, -1},
        {"GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_USER", "Injury severity (your team)", "0..100, replaces the slider", "at kick-off", 0, 100, -1},
        {"GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_CPUAI", "Injury severity (CPU team)", "0..100, replaces the slider", "at kick-off", 0, 100, -1},
        // weather and time of day stay numbers: which number is which look is not verified in the game yet (docs/re/match_setup.md)
        {"OVERRIDE/WEATHER", "Weather", "a weather setting, 0 to 8, for the next matches (what each number looks like is not verified yet; -1 = the game decides)",
         "when a match is set up", -1, 8, -1},
        {"OVERRIDE/TOD", "Time of day", "a time-of-day setting: 0, 1, 3 or 4 (the game ignores other numbers; which is which is not verified yet; -1 = the game decides)",
         "when a match is set up", -1, 4, -1},
        {"OVERRIDE_MATCH_DIFFICULTY", "Difficulty", "0 Beginner up to 5 Legendary for the next matches (-1 = your setting)", "when a match is set up",
         -1, 5, -1},
        {"DISABLE_CPU_SUBSTITUTION", "CPU makes no substitutions", "1 = the CPU team's substitution AI stays idle", "during the match", 0, 1, 0},
    };
    return k;
}

const KnownVar* known(const std::string& name) {
    for (const KnownVar& v : known_vars())
        if (name == v.name) return &v;
    return nullptr;
}

}  // namespace gv

// ================================================================ result fixing
namespace mfix {

std::string FixTable::set(uint16_t fixture, int home, int away, const std::string& label) {
    if (fixture >= kMaxFixture) return "fixture id out of range";
    if (home < 0 || away < 0 || home > kMaxGoals || away > kMaxGoals) return "goals must be 0.." + std::to_string(kMaxGoals);
    if (Fix* f = find(fixture)) {
        f->home = int8_t(home);
        f->away = int8_t(away);
        if (!label.empty()) f->label = label;
        return "";
    }
    Fix f;
    f.fixture = fixture;
    f.home = int8_t(home);
    f.away = int8_t(away);
    f.label = label;
    fixes_.push_back(f);
    return "";
}

bool FixTable::erase(uint16_t fixture) {
    for (size_t i = 0; i < fixes_.size(); ++i)
        if (fixes_[i].fixture == fixture) {
            fixes_.erase(fixes_.begin() + long(i));
            return true;
        }
    return false;
}

const Fix* FixTable::find(uint16_t fixture) const {
    for (const Fix& f : fixes_)
        if (f.fixture == fixture) return &f;
    return nullptr;
}

Fix* FixTable::find(uint16_t fixture) {
    for (Fix& f : fixes_)
        if (f.fixture == fixture) return &f;
    return nullptr;
}

namespace {
// The game's "is set" test of a {home, away} pair (0x144046D18): both halves != -1
bool pair_set(Memory& mem, uint64_t at, bool& ok) {
    int32_t a = -1, b = -1;
    ok = mem.rd(at, a) && mem.rd(at + 4, b);
    return a != -1 && b != -1;
}
}  // namespace

bool apply_to_request(Memory& mem, uint64_t self, uint64_t expected_vtable, uint64_t msg, const FixTable& fixes, Applied& out) {
    out = Applied();
    if (fixes.empty() || !is_ptr(msg, 8)) return false;
    int32_t type = 0;
    if (!mem.rd(msg + kOffType, type) || type != kRequestType) return false;
    int32_t fixture = -1;
    if (!mem.rd(msg + kOffFixture, fixture) || fixture < 0 || fixture >= kMaxFixture) return false;
    const Fix* f = fixes.find(uint16_t(fixture));
    if (!f) return false;
    out.fixture = uint16_t(fixture);
    out.home = f->home;
    out.away = f->away;
    if (expected_vtable) {
        uint64_t vt = 0;
        if (!mem.rd(self, vt) || vt != expected_vtable) {
            out.skipped = "the handler's object is not the expected FCE manager";
            return true;
        }
    }
    if (!mem.rd(msg + kOffHome, out.old_home) || !mem.rd(msg + kOffAway, out.old_away)) {
        out.skipped = "the result is not readable";
        return true;
    }
    bool ok1 = false, ok2 = false;
    const bool extra = pair_set(mem, msg + kOffExtra, ok1), pens = pair_set(mem, msg + kOffPens, ok2);
    if (!ok1 || !ok2) {
        out.skipped = "the result is not readable";
        return true;
    }
    if (extra || pens) {
        out.skipped = "the match went to extra time or penalties: left as played";
        return true;
    }
    if (out.old_home == out.home && out.old_away == out.away) return true;
    const int32_t h = f->home, a = f->away;
    if (!mem.wr(msg + kOffHome, h) || !mem.wr(msg + kOffAway, a)) {
        out.skipped = "writing the result failed";
        return true;
    }
    out.changed = true;
    return true;
}

}  // namespace mfix

}  // namespace turbo
