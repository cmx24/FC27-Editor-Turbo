// FC 27 LE Turbo GUI - Lua loader hook, platform-independent part (the Windows host is src/win/lua_loader_win.cpp).
//
// What it is for: the game compiles its Lua assets (fifarna player_kit.lua among them) with Lua 5.4's
// lua_load(L, reader, data, chunkname, mode), statically linked into FC27.exe. A detour on lua_load sees every compile;
// research in turbo_dev/research/c2_lua_loader/C2_LUA_LOADER.md (signatures read from build 6AB9813C).
//
// Two modes, both opt-in by file (turbo_output\):
//   * lua_loader_on.txt          LOG-ONLY: one line per lua_load call (caller, thread, chunkname, reader, size + md5 of the
//                                source for the two known readers, rc). Every call passes through unchanged.
//   * lua_loader_substitute.txt  (only together with lua_loader_on.txt) SUBSTITUTE: a source whose (size, md5) matches a row
//                                is replaced by the row's override file (turbo_output\lua_override\...). Text chunks only.
//   * hook_lua_load_off.txt      kill switch: checked before install and re-read every 250 ms; present = pure pass-through.
//   * faces3d_on.txt             product opt-in (written by the GUI): the same as lua_loader_on.txt + lua_loader_substitute.txt.
//   * faces3d_off.txt            kill switch like hook_lua_load_off.txt (also game_hooks_off.txt).
// Without lua_loader_on.txt / faces3d_on.txt nothing is scanned, hooked or logged.
//
// STOCK CAPTURE (faces3d, 2.9 WP-B): for a chunkname row on a reader whose live code bytes equal kOtherReaderExpect (the game's
// one-shot reader FC27.exe+0x46AE738 on 6AC07E31; 6AB9813C 0x1446B08C8: `cmp byte [rdx+8],0 / je / mov qword [r8],0 / xor eax,eax /
// ret / mov rax,[rdx] / mov byte [rdx+8],1 / mov rcx,[rax+20h] / mov [r8],rcx / mov rax,[rax+18h] / ret`, a leaf: no call, no Lua
// API, it cannot raise), the detour drains that reader itself (the lua_Reader contract: call until NULL or size 0), copies + md5s
// the bytes and then ALWAYS hands lua_load its own replay reader (the game's reader is consumed: calling it again yields an empty
// chunk). What the replay serves: the override, only when its first line `-- TURBO build <key> stock <md5> table <hash>` names the
// running build and the md5 just captured; otherwise the captured stock bytes (identical to what the game's reader returned). A
// failed override compile replays the captured stock bytes. Capture mode = LOG-ONLY: the first capture is published once
// (turbo_output\faces3d\stock_player_kit.cache, written by the watcher thread, tmp + rename); in SUBSTITUTE mode only when the md5
// equals the row's expected stock md5; never while mods\legacy\data\fifarna\lua\assets\player_kit.lua exists (LE would serve that
// file, and its length patch crashes: substitution is refused then).
//
// How the source is seen without changing what the game does (the "peek"):
//   * luaL_loadstring's reader getS: its data is {const char* s; uint32 size} (FC27 6AB9813C: mov eax,[rdx+8]); it is read
//     straight from the struct before the call, nothing is called. Substitution passes lua_load Turbo's own one-shot reader.
//   * the game's blob reader (loadfile / loadfileasync): its data is {Blob* blob; bool done}; the first call sets done and
//     returns blob->vf[0]() with size blob->vf[1](), later calls NULL. Calling it early and "restoring" done would make the
//     virtual calls run twice (not provably harmless), so it is not peeked: a second detour on the blob reader itself
//     records the (pointer, size) the ORIGINAL reader returned, at the moment the parser asked for it (same thread, same
//     order, same arguments). Substitution returns the override buffer from that detour instead; the done flag is already
//     set by the original, so the next call returns NULL exactly as before.
//   * any other reader: never called by Turbo (luaB_load's generic_reader can raise a Lua error), EXCEPT for a chunkname row
//     on a reader whose live bytes equal kOtherReaderExpect: that one is drained (STOCK CAPTURE above). Substitution there:
//     lua_load is called with the same L / chunkname / mode and Turbo's one-shot reader over the immutable override buffer
//     (per-call reader state on the detour's stack). Gates: header build + stock md5 (above), the override file is valid,
//     mode allows text, and the lua_load stack layout was checked at install (kLuaLoadTopCheck). If that compile fails
//     (rc != 0) the error message lua_load pushed is read and popped (L->top -= one slot, only when the stack depth grew by
//     exactly one slot) and lua_load is called again over the CAPTURED stock bytes; a per-row latch then serves the captured
//     stock directly on every later call. Launch L1 (6AC07E31): fifarna player_kit.lua comes through that reader
//     (FC27.exe+0x46AE738), chunkname "data/fifarna/lua/assets/player_kit.lua" (no '@'), 18 calls on 18 threads at boot;
//     Launch L2: the chunkname substitution compiled and ran in game (hub + Edit Players previews).
// The override cannot be compile-checked in C++ without a Lua state: validate it offline (Lua 5.4 / lua_sim, the bisect
// tooling) before deploying. Main chunk local limit: stock uses 193 of 200 locals.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/sigscan.h"

namespace turbo {
namespace lualoader {

// Lua 5.4 C types as the game sees them (lua_State* and lua_Reader are opaque here)
using Reader = const char* (*)(void* L, void* data, size_t* size);
using LuaLoadFn = int (*)(void* L, Reader reader, void* data, const char* chunkname, const char* mode);

// ---------------------------------------------------------------- md5
std::string md5_hex(const uint8_t* p, size_t n);

// ---------------------------------------------------------------- signatures (read on build 6AB9813C; all four found live on 6AC07E31 in Launch L1)
// lua_load itself, luaL_loadstring (whose call must resolve to lua_load: the cross-check), luaL_loadstring's lea of getS
// (same pattern, rip operand at +29) and the game's blob reader. Patterns from C2_LUA_LOADER.md section 2.
extern const char* const kSigLuaLoad;          // "lua_load"
extern const char* const kSigLuaLoadXcheck;    // "lua_load_xcheck"  (rip at +50: call lua_load)
extern const char* const kSigLoadStringReader; // "lua_getS"         (rip at +29: lea rdx, getS)
extern const char* const kSigBlobReader;       // "lua_game_blob_reader"
std::vector<Signature> signatures();
// The bytes getS must start with (cmp dword [rdx+8],0 / jne / xor eax,eax / ret / mov eax,[rdx+8] / mov [r8],rax / mov rax,[rdx])
extern const char* const kGetSExpect;
// First bytes of lua_load as the pattern has them (mov rax,rsp / push rbx / sub rsp,50h)
extern const uint8_t kLuaLoadPrologue[8];
// lua_load right after luaD_protectedparser returns (6AB9813C +0x45): mov r11d,eax / test eax,eax / jne / mov rcx,[rbx+10h] /
// mov r10,[rcx-10h]: L->top at L+0x10, 16-byte stack slots. Required for the chunkname fallback (the pop of the error message).
extern const char* const kLuaLoadTopCheck;
extern const size_t kLuaLoadTopCheckOffset;

// True when the bytes look like someone's inline hook / patch: E9 / EB / FF 25 / CC / 48 B8 .. FF E0 / 68 .. C3
bool prologue_is_jump(const uint8_t* p, size_t n);

// Install decision from the scan results. read(addr, out, n) reads live code bytes (getS check, lua_load prologue).
struct Located {
    bool ok = false;
    uint64_t lua_load = 0;
    uint64_t gets = 0;         // 0 = not confirmed (loadstring sources are then logged without size/md5)
    uint64_t blob_reader = 0;  // 0 = not found (blob sources are then logged without size/md5, no substitution)
    bool top_layout_ok = false;  // kLuaLoadTopCheck matched: the chunkname fallback may pop the error message
    std::string why;           // reason for !ok, or notes for ok
};
Located decide_install(const std::vector<SigResult>& results, const std::function<bool(uint64_t, void*, size_t)>& read);

// ---------------------------------------------------------------- rows
struct Row {
    std::string name;      // "player_kit"
    uint64_t size = 0;     // stock source size
    std::string md5;       // stock source md5, lower-case hex
    std::string override_rel;  // relative to turbo_output: "lua_override\\player_kit.lua"
    // Optional: substitution BY CHUNKNAME for readers Turbo cannot measure ("other"). Exact text, one leading '@' tolerated on
    // either side; only when the running build key (sigscan build_key, "6AC07E31-2145C000") equals `build`.
    std::string chunkname;
    std::string build;
};
// Build the chunkname rows are gated on (the build Launch L1 saw the chunkname on)
extern const char* const kChunknameBuild;
std::vector<Row> default_rows();
// Matching row index or -1 (size + md5; rows without size/md5 never match here)
int match_row(const std::vector<Row>& rows, uint64_t size, const std::string& md5);
// True when the chunkname `got` (n bytes, no NUL) equals `want`, ignoring one leading '@' on either
bool chunkname_equal(const std::string& want, const char* got, size_t n);
// Row whose chunkname matches, or -1 (build not checked here)
int match_chunkname_row(const std::vector<Row>& rows, const char* got, size_t n);

// An override source, loaded once and never freed (the parser may read it at any time after it was handed out)
struct Override {
    const char* data = nullptr;
    size_t size = 0;
    std::string md5;
    std::string error;  // why it is not usable ("" = usable)
};
// Validates an override file's content: non-empty, not a binary chunk (0x1B 'L' 'u' 'a'), no NUL byte in the first 4 KB.
std::string override_refusal(const std::string& bytes);

// ---------------------------------------------------------------- faces3d: override header, capture, switches
// First line of an override: `-- TURBO build <buildkey> stock <md5> table <hash>` (CRLF or LF; single or repeated spaces).
// build = sigscan build key of the game it was made for, stock = md5 of the captured stock source it was made from, table = hash
// of the generator's gate table (logged, not gated).
struct OverrideHeader {
    bool ok = false;
    std::string build, stock, table;
    std::string error;  // why !ok
};
OverrideHeader parse_override_header(const std::string& bytes);
std::string make_override_header(const std::string& build, const std::string& stock_md5, const std::string& table);

// The game's one-shot reader (6AB9813C 0x1446B08C8 / 6AC07E31 FC27.exe+0x46AE738), exact bytes, no wildcard (the +0x18 / +0x20
// object offsets are the contract). The host compares the live bytes at the reader address before Turbo ever calls it.
extern const char* const kOtherReaderExpect;
// Cache of the captured stock source, relative to turbo_output
extern const char* const kStockCacheRel;     // "faces3d\\stock_player_kit.cache"
// The legacy file whose presence refuses substitution, relative to the LE root
extern const char* const kLegacyKitRel;      // "mods\\legacy\\data\\fifarna\\lua\\assets\\player_kit.lua"

// Text only a gate v4 override holds (core/faces3d.h template v4): the hook reports it as Faces3dStatus::live_looks
extern const char* const kLiveLooksMarker;  // "TURBO_GATE.band = function"
bool has_live_looks(const char* data, size_t size);

// Start decision from the switch files (all in turbo_output, the legacy file under the LE root)
struct StartSwitches {
    bool lua_on = false, lua_substitute = false, faces_on = false, faces_off = false, hook_off = false, game_hooks_off = false,
         legacy_present = false;
};
struct StartDecision {
    bool opted_in = false;      // scan + hook at all
    bool refused = false;       // opted in, but a kill switch is present: nothing installed
    bool substitute = false;    // SUBSTITUTE mode (after the legacy refusal)
    bool capture_mode = false;  // LOG-ONLY: the first capture is cached whatever its md5
    bool legacy_refused = false;  // substitution wanted but refused (legacy player_kit.lua present)
    std::string why;            // for refused / legacy_refused
};
StartDecision decide_start(const StartSwitches& s);

// Writes (on) or removes (off) turbo_output\faces3d_on.txt. Writing goes through `<file>.tmp` + rename. For the GUI.
bool set_faces3d_opt_in(const std::string& turbo_output_utf8, bool on, std::string* err);
// Writes `bytes` to `path_utf8` through `<path>.tmp` + rename (replacing an existing file); creates the parent folder.
bool write_file_atomic(const std::string& path_utf8, const std::string& bytes, std::string* err);

// For the Status tab / Appearance page (the host fills it: win/lua_loader_win.h faces3d_status())
struct Faces3dStatus {
    bool opted_in = false;       // lua_loader_on.txt or faces3d_on.txt at start
    bool installed = false;      // lua_load hooked
    bool substitute = false;     // SUBSTITUTE mode active
    bool legacy_refused = false; // substitution refused: mods\legacy player_kit.lua present
    bool killed = false;         // a kill switch is present now
    std::string running_build;
    bool header_ok = false;      // the override has a TURBO header
    bool build_key_ok = false;   // ... naming the running build
    std::string header_stock;    // the stock md5 the override was made from
    std::string header_table;    // its table hash (md5(players.json)[:12]): which list this game start runs
    bool live_looks = false;     // that override carries the 2.16 live band rule (kLiveLooksMarker): looks change without a restart
    long long captures = 0, served_stock = 0, substituted = 0, fallbacks = 0, capture_refused = 0, capture_mismatch = 0;
    uint64_t captured_size = 0;
    std::string captured_md5;    // "" until the first capture
    bool stock_matches_header = false;  // captured md5 == header stock
    bool cache_written = false;
    std::string last_error;      // last reason the override was not served, or a capture problem
};

// ---------------------------------------------------------------- the detour logic (host independent, testable)
enum class ReaderKind { Other, Blob, LoadString };
const char* reader_kind_name(ReaderKind k);

// True when `mode` (already copied, nullptr = Lua's NULL = "bt") allows text chunks
bool mode_allows_text(const char* mode);

// Printable, quoted, at most max_len characters ("..." appended when cut)
std::string quote_chunkname(const char* s, size_t n, size_t max_len);

struct CallRecord {
    uint64_t seq = 0;
    std::string time;      // HH:MM:SS.mmm of the call
    uint32_t thread = 0;
    std::string caller;    // "FC27.exe+0x89E1D7C"
    std::string reader;    // "FC27.exe+0x89E182C"
    ReaderKind kind = ReaderKind::Other;
    std::string chunk;     // quoted, or "(unreadable)" / "(null)"
    std::string mode;      // "(null)" or the text
    bool have_source = false;
    uint64_t size = 0;
    std::string md5;
    std::string note;      // e.g. "source unreadable", "binary chunk"
    bool substituted = false;
    bool by_chunkname = false;  // substituted on the chunkname alone (stock source unknown)
    std::string row;
    uint64_t new_size = 0;
    std::string new_md5;
    bool fallback = false;      // the override failed to compile; the original was loaded instead
    bool captured = false;      // the stock source was captured by draining the verified reader (size / md5 = that source)
    bool served_stock = false;  // captured, and the captured stock bytes were served (no override)
    int override_rc = 0;
    std::string override_msg;
    int rc = 0;
};
std::string format_line(const CallRecord& r);

// Callbacks the host gives the engine (tests give fakes)
struct Env {
    std::function<bool(uint64_t addr, void* out, size_t n)> read;  // safe copy, false when unreadable
    std::function<bool(const void* p, size_t n)> readable;          // the whole range is committed + readable
    std::function<std::string(uint64_t addr)> where;                // "FC27.exe+0x..." for an address
    std::function<uint32_t()> thread_id;
    std::function<std::string()> now;                               // "HH:MM:SS.mmm"
    std::function<void(const std::string& line)> emit;              // queue a log line (never blocks for long)
    // The chunkname fallback (all three, or chunkname substitution is refused): the host sets them only when the lua_load layout
    // check passed. depth = (L->top - L->ci->func) in bytes, -1 = unreadable; message = the error object at top-1 (quoted);
    // pop1 = L->top -= one slot.
    std::function<long long(void* L)> stack_depth;
    std::function<std::string(void* L)> top_message;
    std::function<bool(void* L)> pop1;
    // STOCK CAPTURE: true when the live code at `reader` equals kOtherReaderExpect (unset = never capture)
    std::function<bool(uint64_t reader)> reader_capturable;
    // Called at most once per row per process with the first publishable capture (the host hands it to its watcher thread; it
    // must not do file I/O on the calling game thread)
    std::function<void(const std::string& row, std::shared_ptr<const std::string> bytes, const std::string& md5)> save_capture;
};

// The capture settings (set once before the hooks are enabled)
struct CaptureConfig {
    bool enabled = false;         // drain + md5 chunkname rows on a verified reader
    bool capture_mode = false;    // LOG-ONLY: publish the first capture whatever its md5
    bool legacy_present = false;  // never substitute, never publish
};

class Engine {
public:
    explicit Engine(Env env) : env_(std::move(env)) {}
    // Set once before the hooks are enabled (not changed afterwards)
    // running_build: the game's build key (gates the chunkname rows; "" = no chunkname substitution)
    void configure(uint64_t gets, uint64_t blob_reader, bool substitute, std::vector<Row> rows, std::vector<Override> overrides,
                   std::string running_build = std::string(), CaptureConfig capture = CaptureConfig());
    // Read by every call (atomic): true = pure pass-through, nothing logged
    void set_killed(bool k) { killed_.store(k, std::memory_order_release); }
    bool killed() const { return killed_.load(std::memory_order_acquire); }

    // The lua_load detour body: calls `orig` once and returns its result; twice only when a chunkname override failed to compile
    // (then the second call is the untouched original and its rc is returned).
    int on_lua_load(LuaLoadFn orig, void* L, Reader reader, void* data, const char* chunkname, const char* mode, uint64_t caller);
    // The blob reader detour body: always calls `orig` exactly once and returns its result, or (substitute mode, row match,
    // text chunk) the override buffer with its size.
    const char* on_blob_read(Reader orig, void* L, void* data, size_t* size);

    struct Stats {
        long long calls = 0, logged = 0, nested = 0, passthrough_killed = 0, blob_reads = 0, blob_orphans = 0, substituted = 0,
                  errors = 0, fallbacks = 0, captures = 0, served_stock = 0, capture_refused = 0, capture_mismatch = 0, published = 0;
    };
    Stats stats() const;
    // The engine's part of the Status struct (captures, md5, header of row 0, counts, last error); the host adds the rest
    void fill_status(Faces3dStatus& st) const;

private:
    struct Ctx;
    void fill_common(CallRecord& r, std::string& raw, bool& raw_ok, Reader reader, const char* chunkname, const char* mode, uint64_t caller);
    bool text_allowed(const char* mode);
    const Override* override_for(uint64_t size, const std::string& md5, int* row_out) const;
    void set_error(const std::string& e);
    void note_capture(uint64_t size, const std::string& md5);

    Env env_;
    uint64_t gets_ = 0, blob_ = 0;
    bool substitute_ = false;
    std::vector<Row> rows_;
    std::vector<Override> overrides_;  // same index as rows_
    std::string build_;
    std::unique_ptr<std::atomic<bool>[]> failed_;  // per row: its override failed to compile once (then: original directly)
    std::unique_ptr<std::atomic<bool>[]> published_;  // per row: save_capture was called
    std::vector<OverrideHeader> headers_;            // per row, parsed from the override at configure
    std::vector<bool> live_;                         // per row: the override holds kLiveLooksMarker (set at configure)
    CaptureConfig capture_;
    std::atomic<bool> killed_{false};
    std::atomic<uint64_t> seq_{0};
    std::atomic<long long> calls_{0}, logged_{0}, nested_{0}, killed_calls_{0}, blob_reads_{0}, blob_orphans_{0}, substituted_{0},
        errors_{0}, fallbacks_{0}, captures_{0}, served_stock_{0}, capture_refused_{0}, capture_mismatch_{0}, published_count_{0};
    mutable std::mutex st_m_;  // captured_md5_ / captured_size_ / last_error_ (a few calls at boot, never on a hot path)
    std::string captured_md5_, last_error_;
    uint64_t captured_size_ = 0;
};

// Bounded line queue (detours push, a watcher thread drains into the Turbo log)
class LineQueue {
public:
    explicit LineQueue(size_t cap) : cap_(cap) {}
    void push(std::string s);
    std::vector<std::string> take();
    long long dropped() const { return dropped_.load(); }

private:
    std::mutex m_;
    std::vector<std::string> q_;
    size_t cap_;
    std::atomic<long long> dropped_{0};
};

}  // namespace lualoader
}  // namespace turbo
