// FC 27 LE Turbo GUI - voice swaps: the testable part of the host (win/callname_voice_win.cpp): the cached switches
// the detours read, the observe ring and its log lines (docs/callnames.md section 12, scripts/callname_voice_log.py).
// No Windows or game includes: the host passes the clock, the thread id and the file check in.
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "callname_voice.h"

namespace turbo {
namespace voice {

// ---------------------------------------------------------------- cached switches
// The detours read atomics only; the files behind them are looked at on the GUI tick (at most every 2 s) and when a
// table is published. The global switches (game_hooks_off.txt, TURBO_GUI_NO_GAME_HOOKS=1) and the per-hook files
// (hook_callname_voice_off.txt, hook_callname_kickoff_off.txt) are game_hooks' own (game_hook_live).
std::filesystem::path feature_off_path(const std::filesystem::path& out_dir);   // callname_voice_off.txt
std::filesystem::path observe_on_path(const std::filesystem::path& out_dir);    // callname_voice_log_on.txt
std::filesystem::path observe_log_path(const std::filesystem::path& out_dir);   // callname_voice_log.txt

// True at most once per period (lock-free; the first call is due)
class Every {
public:
    explicit Every(uint64_t period_ms) : period_(period_ms) {}
    bool due(uint64_t now_ms);
    void force() { next_.store(0); }
private:
    uint64_t period_;
    std::atomic<uint64_t> next_{0};
};

struct FileSwitches {
    std::atomic<bool> feature_off{false};
    std::atomic<bool> observe{false};
    Every every{2000};
    // Re-reads both files when due (or forced); true when it did. exists() is the host's file check.
    bool refresh(uint64_t now_ms, const std::filesystem::path& out_dir, bool (*exists)(const std::filesystem::path&),
                 bool force = false);
};

struct SwitchState {
    bool installed = false;    // both hooks installed (every signature and layout guard resolved)
    bool feature_off = false;  // callname_voice_off.txt
    bool observe = false;      // callname_voice_log_on.txt (and the ring exists)
    bool voices = false;       // the published table has voice entries
    bool kickoffs = false;     // ... kick-off entries
};
struct Live {
    bool voice = false;    // the Preprocess detour does something
    bool kickoff = false;  // the GetCallname detour does something
    bool observe = false;  // both fill the ring
};
// installed, the feature switch off, and something to do (entries of that kind, or observe mode)
Live live_from(const SwitchState& s);

// ---------------------------------------------------------------- observe mode: one ring entry
constexpr uint32_t kRingSize = 4096;  // entries (a power of 2)
constexpr int kLogPids = 8;           // "_pID" parameters kept per line (the rest are counted: more=<n>)
constexpr int kLogName = 32;          // parameter name bytes kept, NUL included
// flags (OR over the line's "_pID" descriptors)
constexpr uint32_t kFlagValueList = 0x1;  // an int descriptor with an allowed-values list (u32 count at [desc+0x18]-4 not 0)
constexpr uint32_t kFlagOutsideOk = 0x2;  // [desc+0x45] != 0: values outside that list are accepted (SetInt 0x1407AFAB7)
constexpr uint32_t kFlagLeftOut = 0x4;    // a "_pID" parameter that is not a single-value int (never rewritten)
constexpr uint32_t kFlagBounded = 0x8;    // the parameter count is above kMaxParams (nothing read)

struct LogEntry {
    enum Kind : uint8_t { None = 0, Query = 1, Kickoff = 2 };
    uint8_t kind = None;
    uint8_t guard = 0;          // query: the double-pass guard skipped a rewrite
    uint8_t n = 0;              // query: pids kept
    bool has_surname = false;
    bool has_intensity = false;
    bool has_override = false;  // kick-off: the table replaced the game's result
    uint16_t more = 0;          // query: "_pID" parameters past kLogPids
    uint32_t tid = 0;
    uint32_t ev = 0;
    uint32_t flags = 0;
    int32_t surname = 0, intensity = 0;
    int32_t pid = 0, game = 0, override_id = 0;  // kick-off
    uint64_t t_ms = 0;
    uint64_t q = 0;
    struct Pid {
        char name[kLogName];
        int32_t before;
        int32_t after;
    } pids[kLogPids] = {};
};

// Reads a query the way the game's Preprocess left it (the same fields process_query reads, plus surname_ID,
// player_intensity and the "_pID" descriptors' +0x44 / +0x45 flags). false = not a CommentaryDb query (no line).
// pvs[i] receives the ParamValue of pids[i] for observe_after.
bool observe_before(const uint8_t* query, LogEntry& e, const uint8_t* pvs[kLogPids]);
// The same values after Turbo's rewrite
void observe_after(LogEntry& e, const uint8_t* const pvs[kLogPids]);
// One line of turbo_output\callname_voice_log.txt (no newline), the format scripts/callname_voice_log.py reads:
//   t=<ms> tid=<n> ev=0x<08X> q=0x<X> pid_before=<name>:<v>,... pid_after=... surname=<id|-> intensity=<n|->
//   flags=0x<X> guard=<0|1> [more=<n>]
//   t=<ms> kickoff pid=<n> game=<id> override=<id|-> tid=<n>
std::string format_entry(const LogEntry& e);

// Lock-free bounded ring (many writers: the detours; one reader: the writer thread). push never allocates, never
// locks, never blocks: a full ring drops the entry and counts it.
class LogRing {
public:
    explicit LogRing(uint32_t size = kRingSize);  // rounded up to a power of 2
    bool push(const LogEntry& e);
    bool pop(LogEntry& out);   // one reader only
    uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    uint32_t size() const { return mask_ + 1; }
private:
    struct Cell {
        std::atomic<uint64_t> seq{0};
        LogEntry e;
    };
    std::unique_ptr<Cell[]> cells_;
    uint32_t mask_ = 0;
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};
    alignas(64) std::atomic<uint64_t> dropped_{0};
};

// Pops up to max entries into text, one line each ('\n'); returns the number of lines
size_t drain_lines(LogRing& ring, std::string& text, size_t max);

}  // namespace voice
}  // namespace turbo
