// FC 27 LE Turbo GUI - the spoken set asked from the game's own audio service (docs/callnames.md section 5).
//
// The game's Create Player screen lists the commentary names of one letter by reading commentarynames and handing the
// ids to the audio service, which drops every id without a recording in the loaded bank (FC27.exe 1.0.140.64835,
// 0x1480B2128 -> [0x14C2A8590] -> GetCommentaryService 0x142A52420 -> service+0x40 (vcall 0x60) -> vcall 0xC0 ->
// FilterNames 0x1439074C8). FilterNames builds one SpeechQuery for the event PLAYER_NAME_FE with player_intensity = 2,
// sets surname_ID = id for every element of an eastl::vector<{int row, int commentaryid}> and asks
// CommentaryBridge::HasAudio (vcall 0xB8); elements that get "no" are erased in place. Turbo calls that same worker
// with its own vector (a batch of ids plus a canary id that can never have audio) on the game thread, batch by batch,
// one batch per frame. Player-specific recordings use the in-match path (0x14294A0F4: the events PLAYER_LOW_SIMPLE /
// PLAYER_LOW_LINK with player_db_pID = playerid), which no vector helper covers: Turbo builds that SpeechQuery itself
// with the game's own ctor / set-int / dtor helpers and a scratch-allocator scope, exactly as the game does inline.
//
// The bank is not bound everywhere: in the career hub the language db's event map is empty and every HasAudio answers
// "no" (docs/callnames.md section 5.6); the game binds PLAYER_NAME_FE on its Create Player screen and during a match,
// through anti-cheat-protected code Turbo does not call. So a build that gets "no" for every id fails (never cached),
// and a small *probe* (the same FilterNames call over a sample of ids, one step, quiet) is repeated by the SpokenWatch
// state machine below until the game answers "yes" somewhere; the full build then starts by itself and its result is
// cached for later sessions. The id list comes from the career database when it is connected, else from the cache
// Turbo writes while connected (turbo_output\callnames\ids.json) or from Lua's bridge_commentary.txt.
//
// This file holds what needs no game: the resolved function set, the pointer chain checks on turbo::Memory, the batch
// layout and its validation, the stepped build (bounded, time-budgeted work per tick), the probe sample, the id cache,
// the watcher and the service interface the Callname tab talks to. src/win/commentary_audio_win.cpp calls the game.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "commentary_bank.h"
#include "mem.h"

namespace turbo {
namespace caudio {

// ---------------------------------------------------------------- game-side layout (FC27.exe 1.0.140.64835)
constexpr uint64_t kServiceNames = 0x40;     // CommentaryService+0x40: the names object (what vcall 0x60 returns)
constexpr uint64_t kNamesInner = 0x08;       // names+0x08: the object FilterNames works on (null until the audio is up)
constexpr uint64_t kInnerAudio = 0x10;       // inner+0x10: the audio system (vcall 0xE0 = get bridge, 0x48 = event ctx)
constexpr uint64_t kServiceRefCount = 0x10;  // service+0x10: the reference GetCommentaryService takes, Release drops
constexpr uint64_t kVtRelease = 0x08;        // service vtable slot 1: Release (lock xadd -1 on +0x10)
constexpr uint64_t kVtGetNames = 0x60;       // service vtable slot 12: returns service+0x40
constexpr uint64_t kVtFilterNames = 0xC0;    // names vtable slot 24: forwards to FilterNames(names+0x08, vector)
constexpr uint64_t kVtAudioGetBridge = 0xE0; // audio system: GetSubsystem("CommentaryBridge")
constexpr uint64_t kVtAudioGetEvent = 0x48;  // audio system: GetEventContext("CommentaryDbEvents", event)
constexpr uint64_t kVtBridgeHasAudio = 0xB8; // CommentaryBridge: bool HasAudio(SpeechQuery*)
constexpr size_t kSpeechQuerySize = 0x100;   // the game's object is 0x84 bytes (ctor writes up to +0x80); Turbo gives it more
constexpr size_t kScratchScopeSize = 0x40;   // the scratch-allocator scope the query allocates its parameters from
constexpr int kPlayerIntensity = 2;          // the frontend always asks with player_intensity = 2
constexpr int kPlayerLowSimple = 1, kPlayerLowLink = 2;  // bits of a player's answer

// Functions, globals and string constants resolved by signature (core/sigscan.cpp built-in table). 0 = not found.
struct Fns {
    uint64_t registry = 0;        // the global slot holding the service registry pointer (0x14C2A8590): read [registry]
    uint64_t get_service = 0;     // void** GetCommentaryService(void** out, Registry*) 0x142A52420 (takes a reference)
    uint64_t filter_names = 0;    // void FilterNames(Inner*, Vector*) 0x1439074C8
    uint64_t service_vtable = 0;  // 0x14A8C6D70 (checked when known)
    uint64_t names_vtable = 0;    // 0x14A8C5F48 (checked when known)
    uint64_t query_ctor = 0;      // SpeechQuery* ctor(SpeechQuery*, Scope*, EventCtx*) 0x1407B0EC0
    uint64_t query_set_int = 0;   // void SetInt(SpeechQuery*, const char* name, int value) 0x1407B03E4
    uint64_t query_dtor = 0;      // void dtor(SpeechQuery*) 0x1407B0B6C
    uint64_t scope_ctor = 0;      // Scope* ctor(Scope*) 0x140670BF4 (scratch allocator scope, 0x40 bytes)
    uint64_t scope_dtor = 0;      // void dtor(Scope*) 0x14053A030
    // the game's own string constants (so every name reaches the audio system exactly as the game passes it)
    uint64_t s_bridge = 0;            // "CommentaryBridge"
    uint64_t s_db_events = 0;         // "CommentaryDbEvents"
    uint64_t s_player_name_fe = 0;    // "PLAYER_NAME_FE"
    uint64_t s_player_intensity = 0;  // "player_intensity"
    uint64_t s_surname_id = 0;        // "surname_ID"
    uint64_t s_player_low_simple = 0; // "PLAYER_LOW_SIMPLE"
    uint64_t s_player_low_link = 0;   // "PLAYER_LOW_LINK"
    uint64_t s_player_db_pid = 0;     // "player_db_pID"
    // First missing signature of the surname path (FilterNames) / of the player path (Turbo-built queries); nullptr = complete
    const char* missing_names() const;
    const char* missing_players() const;
};
// Signature names of the built-in table and the Fns field each one fills (the host resolves them in this order)
struct FnSignature {
    const char* signature;
    uint64_t Fns::*field;
};
const std::vector<FnSignature>& fn_signatures();

// ---------------------------------------------------------------- the pointer chain (reads only)
struct Chain {
    uint64_t service = 0, names = 0, inner = 0, audio = 0;
};
// From a service pointer GetCommentaryService returned: service+0x40 = names (vtable checked), names+0x08 = inner,
// inner+0x10 = audio system; every object must be readable and pointer-shaped. false (with err) otherwise.
bool resolve_chain(Memory& mem, uint64_t service, const Fns& f, Chain& out, std::string& err);

// ---------------------------------------------------------------- the name batch
// An element is what the game's list reader builds: low 32 bits = position in the batch, high 32 bits = commentary id
// (FilterNames reads the id at +4). The last element is the canary: an id that can never have audio (commentary ids
// are a 20-bit field, 900000..965000 is the name range). If it survives the filter did not run (no bridge, no inner).
constexpr int64_t kCanaryId = 999999;
constexpr size_t kBatchMax = 2048;
inline uint64_t batch_elem(uint32_t row, uint32_t id) { return (uint64_t(id) << 32) | row; }
inline uint32_t batch_row(uint64_t e) { return static_cast<uint32_t>(e & 0xFFFFFFFFu); }
inline int64_t batch_id(uint64_t e) { return static_cast<int64_t>(e >> 32); }
// ids + canary -> batch. ids outside 1..2^20-1 are skipped; at most kBatchMax ids are packed.
std::vector<uint64_t> pack_name_batch(const std::vector<int64_t>& ids);
// Elements [0, survivors) of the batch after the filter: the ids kept. false (with err) when the canary survived, an
// element is not the one packed at that row, or rows do not increase (the filter erases in place and keeps the order).
bool unpack_name_batch(const std::vector<uint64_t>& batch, size_t survivors, const std::vector<int64_t>& asked,
                       std::unordered_set<int64_t>& kept, std::string& err);

// ---------------------------------------------------------------- the calls (host: game thread; tests: fake)
class Caller {
public:
    virtual ~Caller() = default;
    // Runs the game's FilterNames over the packed batch in place; `survivors` receives how many elements are left.
    // false (with err) when the service / chain is not available: the build fails.
    virtual bool filter_names(std::vector<uint64_t>& batch, size_t& survivors, std::string& err) = 0;
    // For every player id: bit kPlayerLowSimple / kPlayerLowLink when that event has audio for player_db_pID = id.
    // false (with err) when the player path is not available: the build keeps the names and notes it.
    virtual bool player_audio(const std::vector<int64_t>& pids, std::vector<int>& flags, std::string& err) = 0;
};

struct BuildRequest {
    std::vector<int64_t> names;    // commentary ids to check (commentarynames + the ids playernames / playernamemap use)
    std::vector<int64_t> players;  // player ids to check for their own recordings
    size_t batch_start = 100, batch_min = 25, batch_max = 400;  // ids per tick, adapted to the time one tick took
    double tick_budget = 0.004;    // seconds a tick may spend; above it the batch halves, under half of it it doubles
    // A probe: a short check whether the bank is bound in the current screen (a sample of ids, one or two steps). Its
    // result is never cached and the host logs it only when the outcome changes.
    bool probe = false;
};

struct BuildResult {
    bool ok = false;
    bool cancelled = false;
    bool probe = false;                         // the request was a probe (BuildRequest::probe)
    bool unbound = false;                       // every name answered "no": the bank is not bound in this screen
    bool no_filter = false;                     // the canary survived: the game's filter did not run (no bridge)
    std::string note;                           // why not ok, or a summary
    std::string players_note;                   // why the player path gave nothing ("" = it ran)
    std::unordered_set<int64_t> names;          // commentary ids with audio
    std::unordered_map<int64_t, int> players;   // player id -> kPlayerLowSimple | kPlayerLowLink
    size_t names_checked = 0, players_checked = 0;
    size_t steps = 0;
    double seconds = 0.0;                       // game-thread time spent inside the calls
    double elapsed = 0.0;                       // wall time from the first to the last step
};

// The build, stepped on the game thread: every step processes one batch (names first, then players), measures the time
// the calls took and adapts the next batch. progress() / result() may be read from another thread.
class Build {
public:
    explicit Build(BuildRequest req, std::function<double()> clock = {});
    // One tick of work. Returns true while work remains (the host queues the next step for the next tick).
    bool step(Caller& c);
    void cancel() { cancel_ = true; }
    // Ends the build as failed (a step threw inside the game): the result carries `why`
    void abort(const std::string& why) { finish(false, why); }
    bool done() const { return done_.load(); }
    BuildResult result() const;
    std::string progress() const;  // "names 1200 / 4849 (310 with audio)" ...
    size_t batch() const { return batch_; }

private:
    void finish(bool ok, const std::string& note);
    BuildRequest req_;
    std::function<double()> clock_;
    mutable std::mutex m_;
    BuildResult r_;
    size_t names_pos_ = 0, players_pos_ = 0;
    size_t batch_;
    double started_ = -1.0;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> done_{false};
};

// ---------------------------------------------------------------- the service the Callname tab uses
struct ServiceStatus {
    bool installed = false;    // signatures resolved, hooks allowed (the feature exists at all)
    bool available = false;    // a build may start now (installed, no kill switch, not busy)
    bool players = false;      // the player path (Turbo-built queries) is resolved too
    std::string reason;        // why not available, or a short summary
    bool busy = false;
    std::string progress;      // while busy
    std::string dispatch;      // "hook" / "lua" / "" (how a step reaches the game thread)
    long long runs = 0;        // builds started
    std::string last;          // outcome of the last build
};
class Service {
public:
    virtual ~Service() = default;
    virtual ServiceStatus status() = 0;
    // Start a build; false (with err) when refused (unavailable, busy, nothing to check)
    virtual bool request(const BuildRequest& r, std::string* err) = 0;
    // The finished build, once, when one ended since the last poll
    virtual bool poll(BuildResult& out) = 0;
    virtual void cancel() = 0;
};

// The result as the Callname tab caches it (commentary_bank.h cache format, source kAudioSource)
constexpr const char* kAudioSource = "game audio service";
BankCapture to_capture(const BuildResult& r);

// ---------------------------------------------------------------- the probe sample
// Up to `spread` ids taken evenly over `ids` (sorted) plus up to `preferred_max` of `preferred` (ids whose
// commentarynames.commentarypreview flag says a preview clip exists in the launch bank), without duplicates. With a
// third of the names spoken, 48 spread ids miss a bound bank with a probability under 1e-8.
std::vector<int64_t> probe_sample(const std::vector<int64_t>& ids, const std::vector<int64_t>& preferred, size_t spread = 48,
                                  size_t preferred_max = 16);

// ---------------------------------------------------------------- the id list cache
// What a build asks about, written while the career database is connected so that a build can also run from the main
// menu's Create Player screen (no database): turbo_output\callnames\ids.json
struct IdCache {
    std::vector<int64_t> names;    // every commentary id (commentarynames + the ids playernames / playernamemap use)
    std::vector<int64_t> preview;  // the ids with commentarypreview = 1 (the probe asks these first)
    std::vector<int64_t> players;  // every player id (the PLAYER_LOW_* check)
    std::string when;              // "2026-10-04 02:10"
    std::string session;           // the Live Editor session the list was read in
    std::string source;            // where the list came from (for the UI)
    bool empty() const { return names.empty() && players.empty(); }
};
std::filesystem::path id_cache_path(const std::filesystem::path& le_root);
std::string id_cache_json(const IdCache& c);
// false (with err) when the text is not a cache written by Turbo or holds no commentary ids
bool parse_id_cache_json(const std::string& text, IdCache& out, std::string* err);
// Lua's bridge_commentary.txt ("#turbo-commentary <session> <count>", then "commentaryid<TAB>text" lines): the ids,
// sorted. false (with err) when the header is missing, the count does not match (a file being rewritten) or no id is
// in the name range.
bool parse_commentary_list(const std::string& text, std::vector<int64_t>& ids, std::string* session = nullptr,
                           std::string* err = nullptr);

// ---------------------------------------------------------------- the watcher
// Decides when the host should probe the game and when the full build should start, from what the GUI knows each
// frame (core state machine; the App runs it from its tick and starts the requests). States:
//   Idle     nothing to do: the spoken set is verified (list file or cache), or no service / no ids
//   Waiting  the set is not built: a probe is due every `interval` seconds (after an error the interval doubles up to
//            `max_interval`); a probe that answers "bound" moves to Building
//   Building the full build runs; ok -> Idle, "unbound" (the screen changed) -> Waiting, error -> Waiting (backoff)
class SpokenWatch {
public:
    enum class State { Idle, Waiting, Building };
    enum class Action { None, Probe, Build };
    struct Inputs {
        bool service = false;     // a service exists and is installed
        bool available = false;   // a request may start now (not busy, no kill switch)
        bool verified = false;    // the spoken set is verified (list file or a cache)
        bool have_ids = false;    // an id list is at hand (database, cache or Lua's list)
        std::string why_not;      // when !service / !have_ids: the reason for the line
    };
    double interval = 3.0, max_interval = 60.0;
    // One tick: what the App should start now (at most one request per tick)
    Action tick(double now, const Inputs& in);
    // A build or probe finished (every result goes through here, manual builds too)
    void on_result(double now, const BuildResult& r);
    // The App started a request (one the watcher asked for, or a manual build): the watcher waits for its result
    void started(double now, bool probe);
    // The host refused the request the watcher asked for (busy, kill switch): back off and try again later
    void refused(double now, const std::string& why);
    State state() const { return state_; }
    // One line for the Callname tab and the Status tab ("spoken set not built yet: open ... (last check 02:10:05: ...)")
    const std::string& line() const { return line_; }
    double next_probe() const { return next_probe_; }
    long long probes() const { return probes_; }
    long long bound_seen() const { return bound_seen_; }
    bool pending() const { return pending_; }
    std::string last_probe;        // outcome of the last probe ("not bound in this screen", "bound", an error)
    std::string last_probe_clock;  // clock text the App gives when a probe ends ("02:10:05"); "" = none yet
    std::string last_build;        // outcome of the last full build

private:
    State state_ = State::Idle;
    std::string line_;
    double next_probe_ = 0.0;
    double current_interval_ = 0.0;
    bool pending_ = false;      // a request was started and its result has not arrived
    bool pending_probe_ = false;
    long long probes_ = 0;
    long long bound_seen_ = 0;
};

}  // namespace caudio
}  // namespace turbo
