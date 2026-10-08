// FC 27 LE Turbo GUI - signature scanning for game-code hooks (the platform-independent part of src/win/game_hooks.cpp).
//
// A signature is a byte pattern with "??" wildcards (turbo::parse_pattern, core/devops.h) that must match EXACTLY ONCE
// inside the game's executable sections. Optionally the match points at an instruction with a rip-relative operand
// (call/jmp rel32, mov/lea [rip+disp32], call/jmp [rip+disp32]) that is resolved to the real target.
//
// Signatures are keyed by the game build: build_key(TimeDateStamp, SizeOfImage) of FC27.exe's PE header, e.g.
// "6AB9813C-211EF000". A table comes from turbo\signatures_<build>.json:
//   {"build": "6AB9813C-211EF000", "game": "FC27.exe",
//    "signatures": {"game_tick": {"pattern": "48 89 5C 24 ?? 57 ...", "resolve": "rip"|"none", "offset": 0,
//                                 "expect": "48 8D 05", "hook_probe": false, "note": "..."}}}
// or from the built-in fallback table (builtin_signature_table). A build that is in no table (an EA title update) is
// scanned with the table of the newest built-in build: when EVERY signature with a pattern is found exactly once and
// every offset is checked (decide_adapt) that adapted table is used, else every game hook stays off.
// turbo_output\signature_adapt_off.txt turns adapting off.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace turbo {

enum class SigState { Unknown, Found, Missing, Ambiguous, Skipped, BadPattern };
const char* sig_state_name(SigState s);

struct Signature {
    std::string name;
    std::string pattern;   // "48 8B ?? 05"; empty = placeholder (Skipped)
    std::string resolve;   // "none" (default): address = match + offset; "rip": resolve the rip-relative instruction at match + offset
    int offset = 0;        // bytes added to the match before resolving (may be negative)
    std::string note;
    // The bytes ("??" allowed) that must be at match + offset, checked before resolving (a mismatch is BadPattern):
    // the instruction an offset outside the pattern points at, e.g. "48 8D 05" for lea rax,[rip+x]. Empty = none.
    std::string expect;
    // The address is only probed for another module's inline hook and used only when one is seen there
    // (career_event_dispatch, loc_strtab_get), so an offset outside the pattern needs no expect.
    bool hook_probe = false;
    // Positional like the rows of the built-in table: {name, pattern, resolve, offset, note[, expect[, hook_probe]]}
    Signature(std::string name_ = {}, std::string pattern_ = {}, std::string resolve_ = {}, int offset_ = 0,
              std::string note_ = {}, std::string expect_ = {}, bool hook_probe_ = false)
        : name(std::move(name_)),
          pattern(std::move(pattern_)),
          resolve(std::move(resolve_)),
          offset(offset_),
          note(std::move(note_)),
          expect(std::move(expect_)),
          hook_probe(hook_probe_) {}
};

struct SignatureTable {
    std::string build;  // build key the table is for
    std::string game;   // "FC27.exe"
    std::vector<Signature> sigs;
    const Signature* find(const std::string& name) const;
};

struct SigResult {
    std::string name;
    SigState state = SigState::Unknown;
    uint64_t match = 0;    // address of the pattern match (0 when not found)
    uint64_t address = 0;  // resolved address (0 when not found)
    size_t hits = 0;       // matches seen (2 = ambiguous)
    std::string error;
};

// "6AB9813C-211EF000"
std::string build_key(uint32_t timestamp, uint32_t size_of_image);

// Parses a signatures_<build>.json text. Returns false (and err) when malformed; a table with no signatures is fine.
bool parse_signature_table(const std::string& json_text, SignatureTable& out, std::string& err);
// Serialises a table in the same format (used to write a template next to the game's build key).
std::string signature_table_json(const SignatureTable& t);
// Built-in tables shipped inside Turbo.dll, keyed by build; nullptr when the build is unknown.
const SignatureTable* builtin_signature_table(const std::string& build);
std::vector<std::string> builtin_builds();  // oldest first
// The built-in table of the newest known build (the last of builtin_builds), the one auto-adapt starts from.
const SignatureTable* newest_builtin_table();

// A table file that an earlier Turbo package shipped for a build that is now built in (Turbo 2.0.1's
// turbo\signatures_6AC07E31-2145C000.json), recognised by signature_set_fingerprint: the package name ("Turbo 2.0.1"),
// else nullptr. Such a file is ignored so later fixes to the built-in table reach that install.
const char* superseded_package_table(const SignatureTable& t);
// FNV-1a 64 of the build and every (name, pattern, resolve, offset), sorted by name; notes, expect and order ignored
uint64_t signature_set_fingerprint(const SignatureTable& t);

// Whether the address a signature yields rests on checked bytes: an expect, a hook probe, or (no expect) an offset inside
// the pattern whose instruction ("rip": prefixes, REX, opcode, ModRM up to the operand) is fixed pattern bytes.
bool signature_offset_checked(const Signature& s);

// Auto-adapt for a build in no table: a copy of `from` for `build`, each note prefixed "adapted from <from.build>:".
SignatureTable adapt_signature_table(const SignatureTable& from, const std::string& build);
// All or nothing: adopt only when every signature with a pattern was found exactly once in the scan and its offset is
// checked (signature_offset_checked; else it counts as missing, "unverified offset"). Results are matched by name;
// placeholders do not count, a missing result counts as missing. reason: "N of N signatures found exactly once" or
// "K missing, M ambiguous of N (first: name (missing), ...)".
struct AdaptDecision {
    bool adopt = false;
    int found = 0, missing = 0, ambiguous = 0, skipped = 0;
    std::string reason;
};
AdaptDecision decide_adapt(const SignatureTable& t, const std::vector<SigResult>& results);

// Every match of (bytes, mask) in buf (its first byte lives at address `base`), at most max_hits of them.
std::vector<uint64_t> scan_pattern(const uint8_t* buf, size_t len, uint64_t base, const std::vector<uint8_t>& bytes,
                                   const std::vector<bool>& mask, size_t max_hits);

// Resolves the rip-relative operand of the instruction at `addr`, whose bytes are code[0..len). Understood:
// E8/E9 rel32, 0F 8x rel32, FF 15 / FF 25 [rip+disp32] (target = the pointer slot), and [REX] 8B/8D/89/39/3B/63/85/C7
// with a rip-relative ModRM. Returns false with a reason otherwise.
bool resolve_rip(const uint8_t* code, size_t len, uint64_t addr, uint64_t& target, std::string& err);

// Scan + resolve one signature over a buffer. Unique match required; "rip" resolution reads the instruction from buf.
SigResult resolve_signature(const Signature& s, const uint8_t* buf, size_t len, uint64_t base);

// Status of the game hooks for the Status tab (filled by the Windows host; see src/win/game_hooks.h)
struct HookStatus {
    std::string name;
    bool active = false;
    bool killed = false;  // per-hook kill switch file present
    uint64_t target = 0;
    long long calls = 0;
    long long errors = 0;
    std::string note;
};
struct HookReport {
    std::string build;           // build key of the running game ("" = not known)
    std::string table_source;    // "file", "built-in", "adapted from <build>" or "" (no table for this build)
    bool enabled = false;        // false: global kill switch, unknown build or not installed
    std::string note;            // why hooks are off, or the summary
    std::vector<SigResult> signatures;
    std::vector<HookStatus> hooks;
    bool dispatcher_hooked = false;  // game_tick hook active
    long long dispatcher_ticks = 0;  // game-thread ticks seen (hook)
    long long dispatcher_pumps = 0;  // pumps from Turbo's Lua side (career events and the synthetic trigger)
    long long dispatcher_ran = 0;    // queued jobs run
    long long dispatcher_failed = 0;   // queued jobs that threw
    long long dispatcher_dropped = 0;  // queued jobs dropped (queue full)
    size_t queued = 0;
    uint32_t game_thread_id = 0;     // thread the queue ran on last (0 = never)
    uint32_t tick_thread_id = 0;     // thread the game_tick hook runs on (0 = not seen yet)
    uint32_t pump_thread_id = 0;     // thread Turbo's Lua side pumps from (= where Live Editor runs Lua; 0 = not seen)
    // Prompt Lua commands (the synthetic career event, docs/re/game_thread.md s.4)
    std::string lua_trigger;         // state in words ("ready", "off: ...", "waiting ...")
    long long lua_triggers = 0;      // synthetic events sent
    long long lua_trigger_pumps = 0; // of those, how many made Turbo's Lua side pump (proof the handlers ran)
    // Game calls (core/game_calls.h): one status line per call, e.g. "job_offer: ready (JobMarketManager seen ...)"
    std::vector<std::string> calls;
};

}  // namespace turbo
