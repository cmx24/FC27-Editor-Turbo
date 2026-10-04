// FC 27 LE Turbo GUI - voice swaps: the store, the table and the rewrite on raw query memory (see callname_voice.h)
#include "callname_voice.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

#include "nlohmann/json.hpp"

namespace turbo {
namespace voice {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// The 12 own-recording name events (docs/re/inmatch-callnames.md 1.2; ids = djb2-xor of the names, checked against
// docs/re/inmatch-callnames.json "name_events")
constexpr uint32_t kNameEvents[] = {
    0x9D0D5E6C,  // PLAYER_LOW_SIMPLE
    0x0810BC33,  // PLAYER_LOW_SIMPLE_LOC
    0x0A3EC8A2,  // PLAYER_LOW_LINK
    0xCFF93E8B,  // PLAYER_SPECULATIVE_SIMPLE
    0x167E9305,  // PLAYER_SPECULATIVE_LINK
    0xC845D38F,  // PLAYER_SPECULATIVE_RETURN
    0xD1CD7315,  // PLAYER_LONG_RUN
    0x12E90DCA,  // PLAYER_LONG_RUN_LOC
    0xDA9E4225,  // PLAYER_AND_CLUB
    0x7B2A7AE7,  // PLAYER_BANTER
    0xB012C838,  // PLAYER_BANTER_LOC
    0x232D915F,  // PLAYER_NAME_HIGH
};

constexpr size_t kMaxNameScan = 64;  // parameter names are short ("ptw_player_db_pID"); never scan further

// Unaligned-safe reads and writes of the game's memory (plain moves once compiled)
uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
int32_t rdi32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
uint8_t* rdptr(const uint8_t* p) {
    uint8_t* v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

void bump(std::atomic<uint64_t>& c) { c.fetch_add(1, std::memory_order_relaxed); }

std::string stamp_now() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);
    return stamp;
}

std::string str_of(const json& o, const char* key) {
    auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// A nullable int field: missing or null -> nullopt (true); an integer in [lo, INT32_MAX] -> the value (true); anything
// else -> false (the entry is malformed)
bool opt_int_of(const json& o, const char* key, int64_t lo, std::optional<int64_t>& out) {
    out.reset();
    auto it = o.find(key);
    if (it == o.end() || it->is_null()) return true;
    if (!it->is_number_integer()) return false;
    const int64_t v = it->get<int64_t>();
    if (v < lo || v > INT32_MAX) return false;
    out = v;
    return true;
}

bool entry_of(const json& e, Entry& out) {
    if (!e.is_object()) return false;
    auto pid = e.find("playerid");
    if (pid == e.end() || !pid->is_number_integer()) return false;
    out.playerid = pid->get<int64_t>();
    if (out.playerid <= 0 || out.playerid > INT32_MAX) return false;
    if (!opt_int_of(e, "voice_of", 0, out.voice_of)) return false;
    if (!opt_int_of(e, "kickoff", -1, out.kickoff)) return false;
    if (out.kickoff && *out.kickoff == 0) return false;      // -1 or a commentary id
    if (!out.voice_of && !out.kickoff) return false;         // changes nothing
    out.names_only = false;
    if (auto l = e.find("lines"); l != e.end() && !l->is_null()) {
        if (!l->is_string()) return false;
        const std::string lines = l->get<std::string>();
        if (lines == "names") out.names_only = true;
        else if (lines != "all") return false;
    }
    out.player = str_of(e, "player");
    out.from = str_of(e, "from");
    out.when = str_of(e, "when");
    return true;
}

// Sorted by pid; for equal pids the one that came last wins
template <typename T>
void sort_last_wins(std::vector<T>& v) {
    std::stable_sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.pid < b.pid; });
    std::vector<T> out;
    out.reserve(v.size());
    for (const T& x : v) {
        if (!out.empty() && out.back().pid == x.pid) out.back() = x;
        else out.push_back(x);
    }
    v.swap(out);
}

template <typename T>
const T* find_pid(const std::vector<T>& v, int32_t pid) {
    auto it = std::lower_bound(v.begin(), v.end(), pid, [](const T& a, int32_t p) { return a.pid < p; });
    return it != v.end() && it->pid == pid ? &*it : nullptr;
}

}  // namespace

// ---------------------------------------------------------------- store

void VoiceStore::upsert(const Entry& e) {
    for (Entry& x : entries)
        if (x.playerid == e.playerid) {
            x = e;
            return;
        }
    entries.push_back(e);
}

bool VoiceStore::forget(int64_t playerid) {
    auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& x) { return x.playerid == playerid; });
    if (it == entries.end()) return false;
    entries.erase(it);
    return true;
}

const Entry* VoiceStore::find(int64_t playerid) const {
    for (const Entry& x : entries)
        if (x.playerid == playerid) return &x;
    return nullptr;
}

std::string entries_json(const VoiceStore& s) {
    json list = json::array();
    for (const Entry& e : s.entries) {
        json o = json::object();
        o["playerid"] = e.playerid;
        o["voice_of"] = e.voice_of ? json(*e.voice_of) : json(nullptr);
        o["kickoff"] = e.kickoff ? json(*e.kickoff) : json(nullptr);
        o["lines"] = e.names_only ? "names" : "all";
        o["player"] = e.player;
        o["from"] = e.from;
        o["when"] = e.when;
        list.push_back(o);
    }
    json j = json::object();
    j["turbo_voice"] = 1;
    j["entries"] = list;
    // error_handler::replace: a name with broken UTF-8 (a player name read from memory) must not throw
    return j.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

bool parse_entries_json(const std::string& text, VoiceStore& out, std::string* err) {
    out = VoiceStore{};
    size_t bad = 0;
    try {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            if (err) *err = "not valid JSON";
            return false;
        }
        auto ver = j.find("turbo_voice");
        if (ver == j.end() || !ver->is_number_integer() || ver->get<int64_t>() != 1) {
            if (err) *err = "not a Turbo voice swap file (no \"turbo_voice\": 1)";
            return false;
        }
        auto list = j.find("entries");
        if (list != j.end() && !list->is_null()) {
            if (!list->is_array()) {
                if (err) *err = "\"entries\" is not a list";
                return false;
            }
            for (const json& e : *list) {
                Entry x;
                if (!entry_of(e, x)) {
                    ++bad;
                    continue;
                }
                out.upsert(x);  // one per player: the last one wins
            }
        }
    } catch (const std::exception& e) {
        out = VoiceStore{};
        if (err) *err = std::string("cannot read the file: ") + e.what();
        return false;
    }
    if (bad && err) *err = std::to_string(bad) + (bad == 1 ? " bad entry dropped" : " bad entries dropped");
    return true;
}

fs::path store_path(const fs::path& le_root) { return le_root / "turbo_output" / "callnames" / "voice_swaps.json"; }

bool VoiceStore::load(const fs::path& file, std::string* err) {
    entries.clear();
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + file.string();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    f.close();
    std::string why;
    VoiceStore read;
    if (parse_entries_json(ss.str(), read, &why)) {
        entries = std::move(read.entries);
        if (!why.empty() && err) *err = file.filename().string() + ": " + why;
        return true;
    }
    // A bad file is set aside (never overwritten by the next save) and the store starts empty
    const std::string stamp = stamp_now();
    fs::path aside = file;
    aside += ".bad-" + stamp;
    for (int i = 2; fs::exists(aside, ec) && i < 1000; ++i) {
        aside = file;
        aside += ".bad-" + stamp + "_" + std::to_string(i);
    }
    ec.clear();
    fs::rename(file, aside, ec);
    if (ec) {
        if (err) *err = file.filename().string() + ": " + why + "; cannot set it aside: " + ec.message();
        return false;
    }
    if (err) *err = file.filename().string() + ": " + why + "; set aside as " + aside.filename().string();
    return true;
}

bool VoiceStore::save(const fs::path& file, std::string* err) const {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f << entries_json(*this);
        f.close();  // a small file is written only here: a failed flush must not replace the good file
        if (!f) {
            fs::remove(tmp, ec);
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    ec.clear();
    fs::rename(tmp, file, ec);
    if (ec) {
        // Windows: a rename over a file another program holds open can fail; replace in two steps
        ec.clear();
        fs::remove(file, ec);
        ec.clear();
        fs::rename(tmp, file, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (err) *err = "cannot replace " + file.string();
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- table

const Table::Voice* Table::voice(int32_t pid) const { return find_pid(voices, pid); }

const Table::Kick* Table::kickoff(int32_t pid) const { return find_pid(kickoffs, pid); }

Table build_table(const VoiceStore& s) {
    Table t;
    for (const Entry& e : s.entries) {
        if (e.playerid <= 0 || e.playerid > INT32_MAX) continue;
        const int32_t pid = static_cast<int32_t>(e.playerid);
        // his own voice is no swap (a rewrite to the same id); out-of-range ids never reach the hooks
        const bool voiced = e.voice_of && *e.voice_of >= 0 && *e.voice_of <= INT32_MAX && *e.voice_of != e.playerid;
        if (voiced) t.voices.push_back({pid, static_cast<int32_t>(*e.voice_of), e.names_only});
        if (e.kickoff && *e.kickoff >= -1 && *e.kickoff != 0 && *e.kickoff <= INT32_MAX)
            t.kickoffs.push_back({pid, static_cast<int32_t>(*e.kickoff)});
        else if (!e.kickoff && voiced && *e.voice_of > 0)
            t.kickoffs.push_back({pid, -1});  // the game's own pattern for a player with his own recording
    }
    sort_last_wins(t.voices);
    sort_last_wins(t.kickoffs);
    return t;
}

StatsSnapshot snapshot(const Stats& s) {
    StatsSnapshot o;
    o.queries = s.queries.load(std::memory_order_relaxed);
    o.rewrites = s.rewrites.load(std::memory_order_relaxed);
    o.guard_skips = s.guard_skips.load(std::memory_order_relaxed);
    o.own_skips = s.own_skips.load(std::memory_order_relaxed);
    o.kickoffs = s.kickoffs.load(std::memory_order_relaxed);
    o.bounded = s.bounded.load(std::memory_order_relaxed);
    return o;
}

// ---------------------------------------------------------------- the rewrite

bool is_name_event(uint32_t event_id) {
    for (uint32_t id : kNameEvents)
        if (id == event_id) return true;
    return false;
}

bool contains_pid(const char* name) {
    if (!name) return false;
    for (size_t i = 0; i + 4 <= kMaxNameScan && name[i]; ++i)
        if (name[i] == '_' && name[i + 1] == 'p' && name[i + 2] == 'I' && name[i + 3] == 'D') return true;
    return false;
}

bool Guard::seen(const void* query, const void* pv, int32_t value) {
    for (Mark& m : marks)
        if (m.query && m.query == query && m.pv == pv && m.to == value) {
            m = Mark{};  // once: a new query at the same address holding the same value is rewritten next time
            return true;
        }
    return false;
}

void Guard::remember(const void* query, const void* pv, int32_t to) {
    marks[next] = Mark{query, pv, to};
    next = (next + 1) % kMarks;
}

int process_query(uint8_t* query, const Table& t, Guard& g, Stats& st, bool own_query) {
    if (!query || t.empty()) return 0;
    bump(st.queries);
    if (own_query) {
        bump(st.own_skips);
        return 0;
    }
    const uint8_t* ctx = rdptr(query + kQueryCtx);
    if (!ctx || rd32(ctx + kCtxGroup) != kGroupCommentaryDb) return 0;
    const uint32_t event_id = rd32(ctx + kCtxEventId);
    const uint32_t n = rd32(query + kQueryCount);
    if (n > kMaxParams) {
        bump(st.bounded);
        return 0;
    }
    const uint8_t* arr = rdptr(query + kQueryParams);
    if (!arr) return 0;
    int done = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t* pv = rdptr(arr + i * sizeof(void*));
        if (!pv) continue;
        const uint8_t* desc = rdptr(pv + kPvDesc);
        if (!desc) continue;
        if (!contains_pid(reinterpret_cast<const char*>(rdptr(desc + kDescName)))) continue;
        if (rd32(desc + kDescType) != 1 || desc[kDescMulti] != 0) continue;  // one int value, as SetInt stores it
        const int32_t v = rdi32(pv + kPvValue);
        if (v <= 0) continue;
        const Table::Voice* e = t.voice(v);
        if (!e) continue;
        if (e->names_only && !is_name_event(event_id)) continue;
        if (g.seen(query, pv, v)) {  // a second pass: v is the id this query already got, never chained
            bump(st.guard_skips);
            continue;
        }
        std::memcpy(pv + kPvValue, &e->to, 4);
        pv[kPvIsSet] = 1;
        if (e->to > 0) g.remember(query, pv, e->to);
        bump(st.rewrites);
        ++done;
    }
    return done;
}

int32_t kickoff_override(const Table& t, int32_t pid, int32_t game_result, Stats& st) {
    const Table::Kick* k = t.kickoff(pid);
    if (!k) return game_result;
    bump(st.kickoffs);
    return k->id;
}

}  // namespace voice
}  // namespace turbo
