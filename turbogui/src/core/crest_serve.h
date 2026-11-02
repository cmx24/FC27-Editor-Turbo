// FC 27 LE Turbo GUI - crest serve: modded legacy crests on the game's ASYNC legacy-file path (2.18; the platform-independent part of
// src/win/crest_serve_win.cpp; docs/re/legacy_async_crest.md).
//
// The problem (build 6AC07E31-2145C000): FC27's async legacy loaders (ChunkFileAsyncLoad = the career manager-avatar capture and the
// match transition, ChunkFileLoaderData, the script CallPromise loader) do TryFindFile(path, &desc) -> alloc buf(desc.size) ->
// BeginLoadAsync(&desc, _, desc.size, buf, path) (FC27+306C2AC) -> poll the request. For a file in mods\legacy, Live Editor's
// TryFindFile answers true and writes only desc.size (the size on disk): the chunk GUID / start / end are stale, so the request waits
// for a chunk that does not exist and the career start (37 %) or the match transition hangs. Only the sync LoadFile path is served
// by Live Editor.
//
// The fix (decoy, all request objects are the game's own): the BeginLoadAsync detour, for a path under one of the configured prefixes
// (default "data/ui/imgAssets/crest/") whose file exists in <LE folder>\mods\legacy with exactly the requested size, reads the file
// into the caller's buf and returns the ORIGINAL BeginLoadAsync of a small native legacy file (the decoy, resolved once through
// TryFindFile) into a Turbo scratch buffer. The game's own request completes; IsDone / Finish / release are the game's code; the
// callers take the size from their own desc (verified for all three) and the bytes from buf. Every other call goes to the
// original untouched.
//
// On a failed serve the detour returns NULL only to the two callers that test it (ChunkFileLoaderData, ChunkFileAsyncLoad: they free
// buf and report " failed to begin load"); the CallPromise loader dereferences the result at once, so it (and any unknown caller)
// gets the original call instead (today's behaviour).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "sigscan.h"

namespace turbo {
namespace crest_serve {

// FC27's legacy-file descriptor (0x30 bytes): what TryFindFile fills and BeginLoadAsync reads
struct Desc {
    uint8_t guid[16];  // chunk GUID
    uint64_t start;
    uint64_t end;
    uint64_t offset;
    uint64_t size;  // +0x28
};
static_assert(sizeof(Desc) == 0x30, "FC27 legacy Desc is 0x30 bytes");

// ---------------------------------------------------------------- signatures (build 6AC07E31-2145C000, exact_build)
// BeginLoadAsync(Desc*, _, u64 size, void* buf, const char* path) -> Request*: the function's first 24 bytes (4 whole instructions
// in the first 15 bytes, no rip-relative operand; the next 5 bytes are the call to the legacy FS instance getter, left out)
inline constexpr const char* kBeginLoadAsyncPattern = "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 49 8B D9 49 8B F8 48 8B F1";
// ChunkFileLoaderData (FC27+1C3D53D): TryFindFile(r12, &desc) ... BeginLoadAsync(&desc, _, size, buf, path); then the null test
inline constexpr const char* kLoaderSitePattern =
    "48 8D 54 24 40 49 8B CC E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 49 8B 07 89 5C 24 28 C7 44 24 20 10 00 00 00 45 33 C9 4D 8B C4 48 8B "
    "54 24 68 49 8B CF FF 50 08 4C 8B E8 4C 89 64 24 20 4C 8B C8 4C 8B 44 24 68 48 8D 4C 24 40 E8 ?? ?? ?? ?? 48 89 44 24 38 4D 8B 17 "
    "49 8B CF 48 85 C0 74";
inline constexpr int kLoaderTryFindCall = 8;    // E8 -> TryFindFile
inline constexpr int kLoaderAsyncCall = 74;     // E8 -> BeginLoadAsync
inline constexpr int kLoaderAsyncReturn = 79;   // the return address of that call (mov [rsp+38],rax ... test rax,rax; je)
// The script CallPromise loader (FC27+46AD341): same shape; it passes the result to IsDone WITHOUT a null test
inline constexpr const char* kPromiseSitePattern =
    "48 8D 55 F0 48 8B CF E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 4C 8B 65 18 49 8B 07 89 5C 24 28 C7 44 24 20 10 00 00 00 45 33 C9 4C 8B "
    "C7 49 8B D4 49 8B CF FF 50 08 4C 8B E8 48 89 7C 24 20 4C 8B C8 4D 8B C4 48 8D 4D F0 E8 ?? ?? ?? ?? 48 8B F8 48 8B C8 E8";
inline constexpr int kPromiseTryFindCall = 7;
inline constexpr int kPromiseAsyncCall = 72;
inline constexpr int kPromiseAsyncReturn = 77;
// ChunkFileAsyncLoad (FC27+388879E, the capture / match path): BeginLoadAsync(&desc, _, [rsp+58], r14, path & ~3); then the null test
inline constexpr const char* kCaptureSitePattern =
    "48 8B 44 24 60 48 83 E0 FC 48 89 44 24 20 4D 8B CE 4C 8B 44 24 58 48 8D 4C 24 30 E8 ?? ?? ?? ?? 4C 8B E0 48 85 C0 75";
inline constexpr int kCaptureAsyncCall = 27;
inline constexpr int kCaptureAsyncReturn = 32;

// ---------------------------------------------------------------- any build (Turbo X 2.0.1)
// The cs_* entries are exact_build (6AC07E31), so on a later title update (6AD922D1, 10-27) the signature table has none of them and
// crest serve stayed off: modded crests then broke the match load again (Live Editor's stale async descriptor, section above). Now
// install finds the three call sites on the running build itself: each site first by its exact pattern above, else by the same
// instructions with the stack / frame displacements left open (a recompile may move a local; the instruction lengths and so every
// offset below stay the same). Whatever matched, only the cross-check decides: the three async calls go to ONE function whose first
// bytes are kBeginLoadAsyncPattern (BeginLoadAsync is taken from the calls, it needs no unique pattern of its own), both TryFindFile
// calls go to one function, and the three sites differ (check_resolved).
inline constexpr const char* kLoaderSiteRelaxed =
    "48 8D 54 24 ?? 49 8B CC E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 49 8B 07 89 5C 24 28 C7 44 24 20 10 00 00 00 45 33 C9 4D 8B C4 48 8B "
    "54 24 ?? 49 8B CF FF 50 08 4C 8B E8 4C 89 64 24 20 4C 8B C8 4C 8B 44 24 ?? 48 8D 4C 24 ?? E8 ?? ?? ?? ?? 48 89 44 24 ?? 4D 8B 17 "
    "49 8B CF 48 85 C0 74";
inline constexpr const char* kPromiseSiteRelaxed =
    "48 8D 55 ?? 48 8B CF E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 4C 8B 65 ?? 49 8B 07 89 5C 24 28 C7 44 24 20 10 00 00 00 45 33 C9 4C 8B "
    "C7 49 8B D4 49 8B CF FF 50 08 4C 8B E8 48 89 7C 24 20 4C 8B C8 4D 8B C4 48 8D 4D ?? E8 ?? ?? ?? ?? 48 8B F8 48 8B C8 E8";
inline constexpr const char* kCaptureSiteRelaxed =
    "48 8B 44 24 ?? 48 83 E0 FC 48 89 44 24 20 4D 8B CE 4C 8B 44 24 ?? 48 8D 4C 24 ?? E8 ?? ?? ?? ?? 4C 8B E0 48 85 C0 75";

// The bytes at BeginLoadAsync must be this function (checked before hooking)
bool is_begin_load_async(const uint8_t* code, size_t n);

// The resolved addresses install checks before hooking
struct Resolved {
    uint64_t begin_load_async = 0;
    uint64_t loader_tryfind = 0, loader_async = 0, loader_ret = 0;
    uint64_t promise_tryfind = 0, promise_async = 0, promise_ret = 0;
    uint64_t capture_async = 0, capture_ret = 0;
};
// "" when consistent: every address found, all three sites call BeginLoadAsync, both TryFindFile calls agree. Else why not.
std::string check_resolved(const Resolved& r);

// One unique-match scan of the game's code: Found (with .match) or why not (Missing / Ambiguous ...). The host's is
// game_scan_signature; tests pass a buffer scan.
using ScanFn = std::function<SigResult(const Signature&)>;
// Reads n bytes of the game's code at addr; false when unreadable
using ReadFn = std::function<bool(uint64_t addr, uint8_t* out, size_t n)>;
struct Located {
    Resolved res;       // every address, only when error is empty
    std::string how;    // "loader exact, promise exact, capture relaxed" (for the log)
    std::string error;  // "" = consistent (check_resolved passed and the bytes at BeginLoadAsync are the function)
};
// Finds the three sites (exact pattern, else relaxed), reads their calls (E8 rel32 at the documented offsets), takes BeginLoadAsync
// from the three async calls when they agree and its bytes are kBeginLoadAsyncPattern, then check_resolved.
Located locate(const ScanFn& scan, const ReadFn& read);

// ---------------------------------------------------------------- config (turbo_config.json "crest_serve")
struct Config {
    bool enabled = true;
    std::vector<std::string> prefixes{"data/ui/imgAssets/crest/"};
    std::string note;  // for the log
};
// {"crest_serve": {"enabled": true, "paths": ["data/ui/imgAssets/crest/"]}}; a wrong field keeps its default (err says which).
// A prefix must be a relative legacy path ending in '/', without "..", ':' or a leading '/' (refused ones are named in err).
Config parse_config(const std::string& json_text, std::string* err);

// ---------------------------------------------------------------- the per-call decision
// The legacy path relative to mods\legacy, '\' -> '/', or "" when it is not one Turbo may open: empty, absolute, a drive, "..",
// a NUL inside the first `max_len` bytes missing (unterminated), or longer than max_len.
std::string safe_relative(const char* path, size_t max_len = 512);
// Case-insensitive prefix match ('\' and '/' equal) of a raw path against the config; cheap, no allocation. Null path = false.
bool matches_prefix(const char* path, const Config& cfg);

enum class Decision {
    Original,  // not ours: call the original untouched
    Serve,     // read the mod file into buf, then the decoy request
};
struct Probe {
    bool killed = false;       // turbo_output\crest_serve_off.txt
    bool file_exists = false;  // <LE>\mods\legacy\<rel>
    uint64_t file_size = 0;
};
// Pure: the decision for one call. `why` gets a short reason when the answer is Original after the prefix matched (for the log).
// Serve needs: enabled, prefix match, a safe relative path, not killed, buf and size non-zero, the file present with size == the
// requested size (Live Editor wrote desc.size = the size on disk: a different size means Live Editor did not serve this file).
Decision decide(const Config& cfg, const char* path, const void* buf, uint64_t size, const Probe& probe, std::string* why);

// Which caller asked (by the return address), and whether it tests the result for NULL
enum class Caller { Loader, Promise, Capture, Other };
const char* caller_name(Caller c);
Caller classify_caller(const Resolved& r, uint64_t return_address);
inline bool caller_checks_null(Caller c) { return c == Caller::Loader || c == Caller::Capture; }

// Reads exactly `size` bytes of the file through `read_all` into `buf`, only after the whole read succeeded (buf untouched on
// failure). read_all(out) fills the file's bytes; false = unreadable. Returns false with `why` when unreadable or size differs.
bool fill_buffer(const std::function<bool(std::vector<uint8_t>&)>& read_all, void* buf, uint64_t size, std::string* why);

// ---------------------------------------------------------------- the decoy
// Small native legacy files seen with Result=true, is_modded=false on build 6AC07E31 (Live Editor log 2026-10-18), smallest first
const std::vector<std::string>& decoy_candidates();
inline constexpr uint64_t kScratchSize = 64 * 1024;  // the decoy's bytes land here (never read)
// A descriptor TryFindFile filled for a native file: GUID non-zero, 0 < size <= kScratchSize
bool decoy_desc_valid(const Desc& d, std::string* why);
// The first candidate that is not in mods\legacy (modded(rel)) and that try_find(rel, &desc) finds with a valid descriptor.
// Returns the index into `candidates`, or -1 (why lists each refusal).
int choose_decoy(const std::vector<std::string>& candidates, const std::function<bool(const std::string&)>& modded,
                 const std::function<bool(const std::string&, Desc*)>& try_find, Desc* out, std::string* why);

}  // namespace crest_serve
}  // namespace turbo
