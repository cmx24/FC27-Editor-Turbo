// FC 27 LE Turbo GUI - voice swaps: give a player another player's own recordings, or turn his own recording off and
// give him any generic callname, in matches only (turbo_dev/research/real_callnames_plan.md, docs/re/inmatch-callnames.md).
//
// Two guarded game hooks (src/win/callname_voice_win.cpp) read an immutable Table published here:
//   * Preprocess 0x1414A90C8 (pre-handler of every commentary line played or asked): after the original ran, every
//     single-value int parameter whose name contains "_pID" (player_db_pID, keeper_pID, pass_from_pID, ...) that holds
//     B is rewritten to voice(B).to (A's id, or 0 = own recording off), exactly as ParamValue::SetInt stores it.
//   * GetCallname 0x14294A0F4 (once per player at kick-off): returns kickoff(B).id when set (-1 = surname lines silent,
//     or a generic commentary id), else the game's own result.
// Nothing is written to the database or the save. The store is turbo_output\callnames\voice_swaps.json (all careers).
//
// This header is the CONTRACT between the core (callname_voice.cpp: offsets, process_query, Table, VoiceStore), the
// host (win/callname_voice_win.cpp: detours, switches, observe ring, Service) and the App/UI (ui_callnames.cpp,
// app.cpp). No Windows or game includes here.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace turbo {
namespace voice {

// ---------------------------------------------------------------- game layout (FC27.exe 1.0.140.64835, build key
// 6AB9813C-211EF000; checked by the layout-guard signatures before anything installs)
constexpr uint64_t kQueryCount = 0x18;          // u32 number of parameters
constexpr uint64_t kQueryParams = 0x20;         // ParamValue** array
constexpr uint64_t kQueryCtx = 0x78;            // event context
constexpr uint64_t kCtxGroup = 0x40;            // u32 group hash
constexpr uint64_t kCtxEventId = 0x44;          // u32 event id
constexpr uint32_t kGroupCommentaryDb = 0x515CA0C5;  // "CommentaryDbEvents"
constexpr uint64_t kPvValue = 0x00;             // i32 value
constexpr uint64_t kPvDesc = 0x30;              // descriptor*
constexpr uint64_t kPvIsSet = 0x44;             // u8 set flag (SetInt writes 1)
constexpr uint64_t kDescName = 0x20;            // const char* parameter name
constexpr uint64_t kDescType = 0x40;            // u32 1 = int
constexpr uint64_t kDescMulti = 0x44;           // u8 0 = single value
constexpr uint32_t kMaxParams = 64;             // a query never holds more; larger counts are skipped

// ---------------------------------------------------------------- store (turbo_output\callnames\voice_swaps.json)
// {"turbo_voice": 1, "entries": [{"playerid", "voice_of", "kickoff", "lines": "all"|"names", "player", "from", "when"}]}
struct Entry {
    int64_t playerid = 0;                 // B, the player whose commentary changes
    std::optional<int64_t> voice_of;      // A's player id (his recordings), 0 = own recording off, nullopt = unchanged
    std::optional<int64_t> kickoff;       // -1 = surname lines silent, a commentary id, nullopt = the game's rule
    bool names_only = false;              // "lines": "names" = only the 12 name events, else all *_pID lines
    std::string player;                   // display only
    std::string from;                     // display only ("Stanislav Lobotka", "Del Piero (generic)")
    std::string when;                     // "YYYY-MM-DD HH:MM"
};

struct VoiceStore {
    std::vector<Entry> entries;           // one per playerid, the last edit wins
    // A missing file is an empty store (true). A bad file is renamed to voice_swaps.json.bad-<stamp> and gives an
    // empty store (true, *err says so). Malformed entries are dropped with a note in *err. false (empty store, *err)
    // only when the file cannot be opened or a bad file cannot be set aside.
    bool load(const std::filesystem::path& file, std::string* err);
    bool save(const std::filesystem::path& file, std::string* err) const;   // atomic write (tmp + rename)
    void upsert(const Entry& e);
    bool forget(int64_t playerid);        // true when an entry was removed
    const Entry* find(int64_t playerid) const;
};

std::filesystem::path store_path(const std::filesystem::path& le_root);   // <LE>\turbo_output\callnames\voice_swaps.json
std::string entries_json(const VoiceStore& s);                            // the file's text (tests, save)
bool parse_entries_json(const std::string& text, VoiceStore& out, std::string* err);

// ---------------------------------------------------------------- the table the detours read (immutable once built)
struct Table {
    struct Voice { int32_t pid; int32_t to; bool names_only; };
    struct Kick { int32_t pid; int32_t id; };
    std::vector<Voice> voices;            // sorted by pid
    std::vector<Kick> kickoffs;           // sorted by pid
    const Voice* voice(int32_t pid) const;     // binary search; nullptr = none
    const Kick* kickoff(int32_t pid) const;
    bool empty() const { return voices.empty() && kickoffs.empty(); }
};
// voice_of set -> Voice{pid, to}; kickoff set -> Kick; a voice swap (voice_of > 0) with no kickoff gets kickoff -1
// (the game's own pattern for real-name players); voice_of == 0 with no kickoff keeps the game's rule.
Table build_table(const VoiceStore& s);

// ---------------------------------------------------------------- counters (atomics, read by the Status tab)
struct Stats {
    std::atomic<uint64_t> queries{0};       // Preprocess calls seen with a non-empty table
    std::atomic<uint64_t> rewrites{0};      // *_pID values rewritten
    std::atomic<uint64_t> guard_skips{0};   // second pre-handler pass over the same query skipped
    std::atomic<uint64_t> own_skips{0};     // Turbo's own audit queries left alone
    std::atomic<uint64_t> kickoffs{0};      // GetCallname results replaced
    std::atomic<uint64_t> bounded{0};       // queries skipped for a parameter count above kMaxParams
};
struct StatsSnapshot { uint64_t queries = 0, rewrites = 0, guard_skips = 0, own_skips = 0, kickoffs = 0, bounded = 0; };
StatsSnapshot snapshot(const Stats& s);

// ---------------------------------------------------------------- the rewrite, on raw query memory (testable)
// Names of the name events (PLAYER_LOW_SIMPLE, PLAYER_LOW_LINK, ...): is_name_event(event id) for names_only.
bool is_name_event(uint32_t event_id);
bool contains_pid(const char* name);     // the parameter name contains "_pID"
// Thread-local double-pass guard: the pre-runner may run Preprocess twice over the same query; a value already
// rewritten on this thread (same query, same ParamValue, holding the id it got) is not rewritten again, so a swap pair
// (A -> B, B -> 0) never chains. Keyed on more than the query pointer: a new query at a reused address is still
// rewritten, and so is a second pass where the original wrote the player's own id again. A mark is used once, so a new
// query at the same address that legitimately holds that id loses at most one line (counted in guard_skips).
struct Guard {
    struct Mark { const void* query = nullptr; const void* pv = nullptr; int32_t to = 0; };
    static constexpr int kMarks = 8;      // the last 8 rewrites on this thread
    Mark marks[kMarks];
    int next = 0;
    void reset() { for (Mark& m : marks) m = Mark{}; next = 0; }
    bool seen(const void* query, const void* pv, int32_t value);   // true (once) for a remembered rewrite
    void remember(const void* query, const void* pv, int32_t to);
};
// Rewrites the query in place per the table. Reads only what the original Preprocess just dereferenced. Returns the
// number of values rewritten. own_query = Turbo's own audit is running on this thread (nothing changes).
int process_query(uint8_t* query, const Table& t, Guard& g, Stats& st, bool own_query);
// GetCallname override: kickoff(pid) when set, else game_result.
int32_t kickoff_override(const Table& t, int32_t pid, int32_t game_result, Stats& st);

// ---------------------------------------------------------------- the host service the App talks to
// win/callname_voice_win.cpp implements it; tests use a fake. publish() copies the table into a new immutable
// object and swaps the atomic pointer (old tables are kept while the hooks are installed).
class Service {
public:
    virtual ~Service() = default;
    virtual bool available() const = 0;            // both hooks and the 3 layout guards resolved and installed
    virtual std::string why_off() const = 0;       // "game build", "kill switch", "hooks off", "" when available
    virtual void publish(const Table& t) = 0;
    virtual StatsSnapshot stats() const = 0;
    virtual bool observe_on() const = 0;           // turbo_output\callname_voice_log_on.txt present
    virtual void refresh_switches() = 0;           // GUI tick, every 2 s: kill-switch files -> cached atomics
};
// The win host creates one Service at Turbo start (next to install_speech_log) and hands it to the App
// (App::voice_service, a Service*; nullptr in native tests unless a test sets a fake = "Voice swaps are off").

}  // namespace voice
}  // namespace turbo
