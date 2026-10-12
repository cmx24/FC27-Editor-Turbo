// FC 27 LE Turbo GUI - match setup and gameplay switches (see match_setup.h, docs/re/match_setup.md)
#include "match_setup.h"

#include <cstdio>
#include <cstring>
#include <deque>

#include "gameplay_sliders.h"

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
    static const std::vector<KnownVar> k = [] {
        std::vector<KnownVar> v = {
            {"NEVER_INJURE", "Injuries off", "1 = no player gets injured in the played match (the match's injury setup reads it)",
             "at kick-off", 0, 1, 0, false},
            {"GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER", "Injury frequency (your team)",
             "0..100, replaces the Gameplay slider (also beyond the menu's range); cleared = the slider again", "at kick-off", 0, 100, -1, false},
            {"GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI", "Injury frequency (CPU team)", "0..100, replaces the slider", "at kick-off", 0, 100, -1,
             false},
            {"GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_USER", "Injury severity (your team)", "0..100, replaces the slider", "at kick-off", 0, 100, -1,
             false},
            {"GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_CPUAI", "Injury severity (CPU team)", "0..100, replaces the slider", "at kick-off", 0, 100, -1,
             false},
            // weather reads by Live Editor's names (core/field_labels.h "weather": 0 Clear .. 4 Overcast, 5..8 unnamed); time of day has no
            // names anywhere yet (core/user_text.h time_of_day_text says so; which number is which look is not verified in the game)
            {"OVERRIDE/WEATHER", "Weather",
             "the weather of the next matches: 0 Clear, 1 Partly Clear, 2 Rain, 3 Snow, 4 Overcast (5 to 8 have no name yet; -1 = the game decides)",
             "when a match is set up", -1, 8, -1, false},
            {"OVERRIDE/TOD", "Time of day",
             "a time-of-day setting: 0, 1, 3 or 4 (the game ignores other numbers; which is which is not verified yet; -1 = the game decides)",
             "when a match is set up", -1, 4, -1, false},
            {"OVERRIDE_MATCH_DIFFICULTY", "Difficulty", "0 Beginner up to 5 Legendary for the next matches (-1 = your setting)",
             "when a match is set up", -1, 5, -1, false},
            {"DISABLE_CPU_SUBSTITUTION", "CPU makes no substitutions", "1 = the CPU team's substitution AI stays idle", "during the match", 0, 1, 0,
             false},
        };
        // The per-side gameplay slider family (core/gameplay_sliders.h): 34 sliders x {USER, CPU}, 0..100, off = cleared (-1 is never
        // sent). Read site: the GameSettings reset 0x1442DC794 with GetInt(name, current slider byte) (research.json "gameplay-sliders").
        // UNTESTED: whether the value survives to kick-off is what the A/B match decides; nothing here was seen in a played match.
        static std::deque<std::string> store;  // stable storage for the generated strings (KnownVar holds const char*)
        auto keep = [](const std::string& s) {
            store.push_back(s);
            return store.back().c_str();
        };
        for (const gs::Slider& s : gs::family()) {
            for (gs::Side side : {gs::Side::User, gs::Side::Cpu}) {
                const std::string name = gs::var_name(s, side);
                bool already = false;  // INJURY_FREQUENCY_USER / INJURY_SEVERITY_USER are the proven entries above: one entry per name
                for (const KnownVar& e : v)
                    if (name == e.name) already = true;
                if (already) continue;
                KnownVar kv{};
                kv.name = keep(name);
                kv.label = keep(std::string(s.label) + " (" + gs::side_label(side) + ")");
                kv.what = keep(std::string("0..100, replaces the ") + gs::side_label(side) + " slider byte in the settings block when the game's "
                                                                                              "settings reset runs; " +
                               s.what + " UNTESTED: no played match has shown an effect yet.");
                kv.when = "at the settings reset (untested whether it survives to kick-off)";
                kv.min = gs::kMin;
                kv.max = gs::kMax;
                kv.off = gs::kOff;
                kv.untested = true;
                v.push_back(kv);
            }
        }
        return v;
    }();
    return k;
}

const KnownVar* known(const std::string& name) {
    for (const KnownVar& v : known_vars())
        if (name == v.name) return &v;
    return nullptr;
}

std::vector<const KnownVar*> known_proven() {
    std::vector<const KnownVar*> out;
    for (const KnownVar& v : known_vars())
        if (!v.untested) out.push_back(&v);
    return out;
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

std::string check_owner(Memory& mem, uint64_t self, const Owner& owner) {
    if (!owner.vtable && !owner.handler) return "";
    uint64_t vt = 0;
    if (!is_ptr(self, 8) || !mem.rd(self, vt) || !is_ptr(vt, 8)) return "the handler's object is not readable";
    if (owner.vtable && vt != owner.vtable) return "the handler's object is not the expected FCE manager (another vtable)";
    if (owner.handler) {
        uint64_t fn = 0;
        if (!mem.rd(vt + kVtableSlotHandleMessage, fn) || fn != owner.handler)
            return "the handler's object is not the expected FCE manager (its vtable slot 5 is not the hooked HandleMessage)";
    }
    return "";
}

bool apply_to_request(Memory& mem, uint64_t self, const Owner& owner, uint64_t msg, const FixTable& fixes, Applied& out,
                      const std::string& hold) {
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
    const std::string who = check_owner(mem, self, owner);
    if (!who.empty()) {
        out.skipped = who;
        return true;
    }
    if (!mem.rd(msg + kOffHome, out.old_home) || !mem.rd(msg + kOffAway, out.old_away)) {
        out.skipped = "the result is not readable";
        return true;
    }
    if (!hold.empty()) {
        out.skipped = hold;
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
    // write both goal ints, read them back (2.2 review: an automatic write is read back); a failure puts the played goals back
    const bool wrote = mem.wr(msg + kOffHome, h) && mem.wr(msg + kOffAway, a);
    int32_t rh = -1, ra = -1;
    const bool back = wrote && mem.rd(msg + kOffHome, rh) && mem.rd(msg + kOffAway, ra);
    if (back && rh == h && ra == a) {
        out.changed = true;
        return true;
    }
    const std::string what = !wrote ? "writing the result failed" : !back ? "the result is not readable after the write"
                                                                         : "the result did not read back what Turbo wrote";
    int32_t ph = -1, pa = -1;
    const bool restored = mem.wr(msg + kOffHome, out.old_home) && mem.wr(msg + kOffAway, out.old_away) && mem.rd(msg + kOffHome, ph) &&
                          mem.rd(msg + kOffAway, pa) && ph == out.old_home && pa == out.old_away;
    out.skipped = what + (restored ? " (put back as played)" : " (putting the played result back FAILED)");
    return true;
}

std::string PairLog::note(Hook hook, const Applied& a) {
    const int k = hook == Hook::Scheduling ? 0 : 1;
    Entry* e = nullptr;
    for (Entry& x : open_)
        if (x.fixture == a.fixture) e = &x;
    if (!e) {
        if (open_.size() >= kMaxOpen) open_.erase(open_.begin());
        open_.push_back(Entry());
        e = &open_.back();
        e->fixture = a.fixture;
    }
    e->seen[k] = true;
    e->got[k] = a;
    if (!e->seen[0] || !e->seen[1]) return "";
    const Entry done = *e;
    open_.erase(open_.begin() + (e - open_.data()));
    const bool fixed0 = done.got[0].skipped.empty(), fixed1 = done.got[1].skipped.empty();
    if (fixed0 == fixed1) return "";
    const char* names[2] = {"the scheduling manager", "the standings manager"};
    const int r = fixed0 ? 0 : 1, s = 1 - r;  // the one that rewrote / the one that skipped
    const Applied& w = done.got[r];
    char score[96];
    std::snprintf(score, sizeof(score), "%d-%d -> %d-%d", int(w.old_home), int(w.old_away), int(w.home), int(w.away));
    return "fixture " + std::to_string(done.fixture) + ": " + names[r] + " rewrote it (" + score + (w.changed ? "" : ", already that score") +
           ") but " + names[s] + " left it as played (" + done.got[s].skipped + "): the fixture's score and the table now disagree";
}

}  // namespace mfix

}  // namespace turbo
