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
//                                 "note": "..."}}}
// or from the built-in fallback table (builtin_signature_table). A build that is in no table disables every game hook.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
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
std::vector<std::string> builtin_builds();

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
    std::string table_source;    // "file", "built-in" or "" (no table for this build)
    bool enabled = false;        // false: global kill switch, unknown build or not installed
    std::string note;            // why hooks are off, or the summary
    std::vector<SigResult> signatures;
    std::vector<HookStatus> hooks;
    bool dispatcher_hooked = false;  // game_tick hook active
    long long dispatcher_ticks = 0;  // game-thread ticks seen (hook) + Lua pumps
    long long dispatcher_pumps = 0;  // pumps from Turbo's Lua side (career events)
    long long dispatcher_ran = 0;    // queued jobs run
    size_t queued = 0;
    uint32_t game_thread_id = 0;     // thread the queue ran on last (0 = never)
};

}  // namespace turbo
