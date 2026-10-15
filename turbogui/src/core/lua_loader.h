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
// Without lua_loader_on.txt nothing is scanned, hooked or logged.
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
//   * any other reader: pass-through untouched (luaB_load's generic_reader can raise a Lua error; never called by Turbo).
// The override cannot be compile-checked in C++ without a Lua state: validate it offline (Lua 5.4 / lua_sim, the bisect
// tooling) before deploying. Main chunk local limit: stock uses 193 of 200 locals.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
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

// ---------------------------------------------------------------- signatures (build 6AB9813C; 6AC07E31 unverified)
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

// True when the bytes look like someone's inline hook / patch: E9 / EB / FF 25 / CC / 48 B8 .. FF E0 / 68 .. C3
bool prologue_is_jump(const uint8_t* p, size_t n);

// Install decision from the scan results. read(addr, out, n) reads live code bytes (getS check, lua_load prologue).
struct Located {
    bool ok = false;
    uint64_t lua_load = 0;
    uint64_t gets = 0;         // 0 = not confirmed (loadstring sources are then logged without size/md5)
    uint64_t blob_reader = 0;  // 0 = not found (blob sources are then logged without size/md5, no substitution)
    std::string why;           // reason for !ok, or notes for ok
};
Located decide_install(const std::vector<SigResult>& results, const std::function<bool(uint64_t, void*, size_t)>& read);

// ---------------------------------------------------------------- rows
struct Row {
    std::string name;      // "player_kit"
    uint64_t size = 0;     // stock source size
    std::string md5;       // stock source md5, lower-case hex
    std::string override_rel;  // relative to turbo_output: "lua_override\\player_kit.lua"
};
std::vector<Row> default_rows();
// Matching row index or -1
int match_row(const std::vector<Row>& rows, uint64_t size, const std::string& md5);

// An override source, loaded once and never freed (the parser may read it at any time after it was handed out)
struct Override {
    const char* data = nullptr;
    size_t size = 0;
    std::string md5;
    std::string error;  // why it is not usable ("" = usable)
};
// Validates an override file's content: non-empty, not a binary chunk (0x1B 'L' 'u' 'a'), no NUL byte in the first 4 KB.
std::string override_refusal(const std::string& bytes);

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
    std::string row;
    uint64_t new_size = 0;
    std::string new_md5;
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
};

class Engine {
public:
    explicit Engine(Env env) : env_(std::move(env)) {}
    // Set once before the hooks are enabled (not changed afterwards)
    void configure(uint64_t gets, uint64_t blob_reader, bool substitute, std::vector<Row> rows, std::vector<Override> overrides);
    // Read by every call (atomic): true = pure pass-through, nothing logged
    void set_killed(bool k) { killed_.store(k, std::memory_order_release); }
    bool killed() const { return killed_.load(std::memory_order_acquire); }

    // The lua_load detour body: always calls `orig` exactly once and returns its result.
    int on_lua_load(LuaLoadFn orig, void* L, Reader reader, void* data, const char* chunkname, const char* mode, uint64_t caller);
    // The blob reader detour body: always calls `orig` exactly once and returns its result, or (substitute mode, row match,
    // text chunk) the override buffer with its size.
    const char* on_blob_read(Reader orig, void* L, void* data, size_t* size);

    struct Stats {
        long long calls = 0, logged = 0, nested = 0, passthrough_killed = 0, blob_reads = 0, blob_orphans = 0, substituted = 0,
                  errors = 0;
    };
    Stats stats() const;

private:
    struct Ctx;
    void fill_common(CallRecord& r, Reader reader, const char* chunkname, const char* mode, uint64_t caller);
    bool text_allowed(const char* mode);
    const Override* override_for(uint64_t size, const std::string& md5, int* row_out) const;

    Env env_;
    uint64_t gets_ = 0, blob_ = 0;
    bool substitute_ = false;
    std::vector<Row> rows_;
    std::vector<Override> overrides_;  // same index as rows_
    std::atomic<bool> killed_{false};
    std::atomic<uint64_t> seq_{0};
    std::atomic<long long> calls_{0}, logged_{0}, nested_{0}, killed_calls_{0}, blob_reads_{0}, blob_orphans_{0}, substituted_{0},
        errors_{0};
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
