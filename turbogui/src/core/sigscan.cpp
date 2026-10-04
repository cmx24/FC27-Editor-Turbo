// FC 27 LE Turbo GUI - signature scanning (see sigscan.h)
#include "sigscan.h"

#include <cstdio>
#include <cstring>

#include "devops.h"
#include "nlohmann/json.hpp"

namespace turbo {

using nlohmann::json;

const char* sig_state_name(SigState s) {
    switch (s) {
        case SigState::Found: return "found";
        case SigState::Missing: return "missing";
        case SigState::Ambiguous: return "ambiguous";
        case SigState::Skipped: return "skipped";
        case SigState::BadPattern: return "bad pattern";
        default: return "unknown";
    }
}

const Signature* SignatureTable::find(const std::string& name) const {
    for (const auto& s : sigs)
        if (s.name == name) return &s;
    return nullptr;
}

std::string build_key(uint32_t timestamp, uint32_t size_of_image) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%08X-%08X", static_cast<unsigned>(timestamp), static_cast<unsigned>(size_of_image));
    return buf;
}

bool parse_signature_table(const std::string& json_text, SignatureTable& out, std::string& err) {
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        err = "not a JSON object";
        return false;
    }
    SignatureTable t;
    t.build = j.value("build", std::string());
    t.game = j.value("game", std::string("FC27.exe"));
    if (t.build.empty()) {
        err = "\"build\" missing";
        return false;
    }
    if (j.contains("signatures")) {
        const json& sigs = j["signatures"];
        if (!sigs.is_object()) {
            err = "\"signatures\" must be an object";
            return false;
        }
        for (auto it = sigs.begin(); it != sigs.end(); ++it) {
            const json& v = it.value();
            if (!v.is_object()) {
                err = "signature " + it.key() + " must be an object";
                return false;
            }
            Signature s;
            s.name = it.key();
            s.pattern = v.value("pattern", std::string());
            s.resolve = v.value("resolve", std::string("none"));
            s.note = v.value("note", std::string());
            if (v.contains("offset")) {
                if (!v["offset"].is_number_integer()) {
                    err = "signature " + it.key() + ": offset must be an integer";
                    return false;
                }
                long long o = v["offset"].get<long long>();
                if (o < -4096 || o > 4096) {
                    err = "signature " + it.key() + ": offset out of range";
                    return false;
                }
                s.offset = static_cast<int>(o);
            }
            if (s.resolve != "none" && s.resolve != "rip") {
                err = "signature " + it.key() + ": resolve must be \"none\" or \"rip\"";
                return false;
            }
            if (!s.pattern.empty()) {
                std::vector<uint8_t> b;
                std::vector<bool> m;
                if (!parse_pattern(s.pattern, b, m)) {
                    err = "signature " + it.key() + ": malformed pattern";
                    return false;
                }
            }
            t.sigs.push_back(std::move(s));
        }
    }
    out = std::move(t);
    return true;
}

std::string signature_table_json(const SignatureTable& t) {
    json j;
    j["build"] = t.build;
    j["game"] = t.game;
    json sigs = json::object();
    for (const auto& s : t.sigs) {
        json v;
        v["pattern"] = s.pattern;
        v["resolve"] = s.resolve.empty() ? "none" : s.resolve;
        v["offset"] = s.offset;
        if (!s.note.empty()) v["note"] = s.note;
        sigs[s.name] = v;
    }
    j["signatures"] = sigs;
    return j.dump(2);
}

// ---------------------------------------------------------------- built-in tables
// One table per game build Turbo knows. Patterns were checked for a unique match in the game image of that build
// (docs/re/game_thread.md). An entry with an empty pattern is a placeholder: its status is "skipped" and nothing is
// hooked for it, but the build still counts as known, so hooks installed by address (install_game_hook_at) are allowed.
static const SignatureTable kBuiltin[] = {
    {"6AB9813C-211EF000",  // FC27.exe as dumped 2026-10-03 (TimeDateStamp 0x6AB9813C, SizeOfImage 0x211EF000)
     "FC27.exe",
     {
         // MainLoop frame body (0x1459E2E7C on this build): runs once per frame on the thread the game's "MainLoop"
         // job runs on, void(MainLoop*, int64* dt). Hooked as the game-thread dispatcher (docs/re/game_thread.md s.3).
         {"game_tick",
          "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 79 60 48 8B F1 48 8B EA 48 8B 5F 10 48 8B CB "
          "FF 15 ?? ?? ?? ?? 48 8B 4F 10 E8",
          "none", 0, "MainLoop frame body: per-frame, game thread (docs/re/game_thread.md)"},
         // Career-event post entry PostEvent(dispatcher, int type, Event*) (0x14060124C), resolved through its call in
         // DataController::InsertTeamPlayer so Live Editor's inline hook on the function itself does not hide it.
         {"post_career_event", "4C 8B C0 48 8B CF E8 ?? ?? ?? ?? 48 8D 8C 24 90 00 00 00 E8 ?? ?? ?? ??", "rip", 6,
          "PostEvent(dispatcher, type, event): the entry Live Editor hooks for post__CareerModeEvent (docs/re/game_thread.md s.4)"},
         // Miniface from the 3D model (docs/re/player_capture.md, scripts/re/player_capture_signatures.json)
         {"PlayerCaptureController_GetOrCreate",
          "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 48 89 48 08 41 56 48 83 EC 60 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 65 48 8B 0C 25 58 00 00 00",
          "none", 0, "PlayerCaptureController* GetOrCreate() (0x1470E291C): the controller singleton"},
         {"PlayerCapture_RequestStatic_B",
          "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 80 00 00 00 49 8B F9 49 8B D8 48 8B F2 E8 ?? ?? ?? ?? 48 8B D7 48 8D 4C 24 60",
          "none", 0, "RequestStatic_B(unused, vector<PlayerDesc>*, Delegate* onSlot, Delegate* onDone, int mode, int extra) (0x1470DB908)"},
         {"PlayerCaptureController_Start",
          "48 89 5C 24 18 48 89 74 24 20 57 48 81 EC C0 04 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 B0 04 00 00 48 8B 05 ?? ?? ?? ??",
          "none", 0, "Start(this, const vector<PlayerDesc>*) (0x1470E49CC): hooked to learn the game's own requests"},
         {"PlayerCapture_Settings",
          "48 89 5C 24 18 48 89 74 24 20 57 48 81 EC C0 04 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 B0 04 00 00 48 8B 05 ?? ?? ?? ??",
          "rip", 0x24, "settings singleton pointer (0x14C1ED820): byte +0x1F6 gates Start"},
         {"PlayerCapture_ListenerHub",
          "48 89 5C 24 18 48 89 74 24 20 57 48 81 EC C0 04 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 B0 04 00 00 48 8B 05 ?? ?? ?? ??",
          "rip", 0xB4, "listener hub pointer (0x14C267B48): must be non-null"},
         {"PlayerCapture_Renderer",
          "48 89 5C 24 18 48 89 74 24 20 57 48 81 EC C0 04 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 B0 04 00 00 48 8B 05 ?? ?? ?? ??",
          "rip", 0x141, "capture renderer / message sender pointer (0x14C267B98): must be non-null"},
         {"PlayerCaptureStream_OnSlot",
          "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 30 48 8B D9 B8 08 AF 2F 00 B9 80 F8 02 00",
          "none", 0, "per-picture handler (this, int slot, size_t bytes) (0x1470F2A80): hooked to log the game's own pictures"},
     }},
};

const SignatureTable* builtin_signature_table(const std::string& build) {
    for (const auto& t : kBuiltin)
        if (t.build == build) return &t;
    return nullptr;
}

std::vector<std::string> builtin_builds() {
    std::vector<std::string> out;
    for (const auto& t : kBuiltin) out.push_back(t.build);
    return out;
}

// ---------------------------------------------------------------- scanning
std::vector<uint64_t> scan_pattern(const uint8_t* buf, size_t len, uint64_t base, const std::vector<uint8_t>& bytes,
                                   const std::vector<bool>& mask, size_t max_hits) {
    std::vector<uint64_t> hits;
    const size_t n = bytes.size();
    if (!buf || n == 0 || n != mask.size() || len < n || max_hits == 0) return hits;
    size_t first = 0;
    while (first < n && !mask[first]) ++first;
    if (first == n) return hits;  // only wildcards: never meaningful
    const uint8_t fb = bytes[first];
    const uint8_t* p = buf + first;
    const uint8_t* end = buf + len - n + first + 1;  // last position where the first fixed byte may sit
    while (p < end) {
        const uint8_t* q = static_cast<const uint8_t*>(std::memchr(p, fb, static_cast<size_t>(end - p)));
        if (!q) break;
        const uint8_t* start = q - first;
        bool ok = true;
        for (size_t k = 0; k < n; ++k) {
            if (mask[k] && start[k] != bytes[k]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            hits.push_back(base + static_cast<uint64_t>(start - buf));
            if (hits.size() >= max_hits) break;
        }
        p = q + 1;
    }
    return hits;
}

static int32_t rd32(const uint8_t* p) {
    uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    return static_cast<int32_t>(v);
}

bool resolve_rip(const uint8_t* code, size_t len, uint64_t addr, uint64_t& target, std::string& err) {
    target = 0;
    if (!code || len < 2) {
        err = "instruction cut off";
        return false;
    }
    size_t i = 0;
    // legacy prefixes that may precede the opcode (66 operand size, F2/F3)
    while (i < len && (code[i] == 0x66 || code[i] == 0xF2 || code[i] == 0xF3)) ++i;
    bool rex = false;
    if (i < len && (code[i] & 0xF0) == 0x40) {
        rex = true;
        ++i;
    }
    if (i >= len) {
        err = "instruction cut off";
        return false;
    }
    const uint8_t op = code[i];
    // E8 call rel32 / E9 jmp rel32
    if ((op == 0xE8 || op == 0xE9) && !rex) {
        if (i + 5 > len) {
            err = "rel32 cut off";
            return false;
        }
        target = addr + i + 5 + static_cast<int64_t>(rd32(code + i + 1));
        return true;
    }
    // 0F 8x jcc rel32
    if (op == 0x0F && i + 1 < len && (code[i + 1] & 0xF0) == 0x80) {
        if (i + 6 > len) {
            err = "rel32 cut off";
            return false;
        }
        target = addr + i + 6 + static_cast<int64_t>(rd32(code + i + 2));
        return true;
    }
    // FF /2 call [rip+disp32], FF /4 jmp [rip+disp32]: the target is the pointer slot
    if (op == 0xFF && i + 1 < len && (code[i + 1] == 0x15 || code[i + 1] == 0x25)) {
        if (i + 6 > len) {
            err = "disp32 cut off";
            return false;
        }
        target = addr + i + 6 + static_cast<int64_t>(rd32(code + i + 2));
        return true;
    }
    // mov/lea/cmp/test/movsxd/mov imm with a rip-relative ModRM (mod=00, rm=101)
    const bool modrm_op = op == 0x8B || op == 0x8D || op == 0x89 || op == 0x39 || op == 0x3B || op == 0x63 || op == 0x85 ||
                          op == 0xC7 || op == 0x88 || op == 0x8A || op == 0x80 || op == 0x83 || op == 0x81;
    if (modrm_op && i + 1 < len) {
        const uint8_t modrm = code[i + 1];
        if ((modrm & 0xC7) != 0x05) {
            err = "ModRM is not rip-relative";
            return false;
        }
        size_t imm = 0;
        if (op == 0xC7 || op == 0x81) imm = 4;
        if (op == 0x80 || op == 0x83) imm = 1;
        if (op == 0xC7 && i > 0 && code[0] == 0x66) imm = 2;
        const size_t end = i + 2 + 4 + imm;
        if (end > len) {
            err = "disp32 cut off";
            return false;
        }
        target = addr + end + static_cast<int64_t>(rd32(code + i + 2));
        return true;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "opcode %02X not understood", op);
    err = buf;
    return false;
}

SigResult resolve_signature(const Signature& s, const uint8_t* buf, size_t len, uint64_t base) {
    SigResult r;
    r.name = s.name;
    if (s.pattern.empty()) {
        r.state = SigState::Skipped;
        r.error = s.note.empty() ? "placeholder (no pattern)" : s.note;
        return r;
    }
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
    if (!parse_pattern(s.pattern, bytes, mask)) {
        r.state = SigState::BadPattern;
        r.error = "malformed pattern";
        return r;
    }
    std::vector<uint64_t> hits = scan_pattern(buf, len, base, bytes, mask, 2);
    r.hits = hits.size();
    if (hits.empty()) {
        r.state = SigState::Missing;
        r.error = "no match";
        return r;
    }
    if (hits.size() > 1) {
        r.state = SigState::Ambiguous;
        r.error = "more than one match";
        return r;
    }
    r.match = hits[0];
    const int64_t at = static_cast<int64_t>(r.match - base) + s.offset;
    if (at < 0 || static_cast<uint64_t>(at) >= len) {
        r.state = SigState::BadPattern;
        r.error = "offset leaves the scanned range";
        return r;
    }
    if (s.resolve == "rip") {
        uint64_t t = 0;
        std::string err;
        size_t avail = len - static_cast<size_t>(at);
        if (!resolve_rip(buf + at, avail < 16 ? avail : 16, base + static_cast<uint64_t>(at), t, err)) {
            r.state = SigState::BadPattern;
            r.error = "rip-relative operand: " + err;
            return r;
        }
        r.address = t;
    } else {
        r.address = base + static_cast<uint64_t>(at);
    }
    r.state = SigState::Found;
    return r;
}

}  // namespace turbo
