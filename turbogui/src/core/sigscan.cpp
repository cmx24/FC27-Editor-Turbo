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
         // Job offers (docs/re/job_offer.md, scripts/re/job_offer_signatures.json; every pattern unique in the image)
         {"jmm_vtable",
          "48 89 5C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 48 8B 05 ?? ?? ?? ??", "rip",
          0x29, "JobMarketManager vtable: the lea rax,[rip+..] in the manager's constructor (0x147DB6088 -> 0x14B016428)"},
         {"jmm_handle_event",
          "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 4D 8B F8 4C 8B 41 08", "none", 0,
          "JobMarketManager::HandleEvent(this, eventId, Event*) 0x147DD232C: hooked to capture the manager pointer"},
         {"jmm_has_application",
          "44 8B 81 F8 08 00 00 4C 8B 89 F0 08 00 00 4C 63 D2 33 D2 49 8B C2 49 F7 F0 8B C2 49 8B 0C C1", "none", 0,
          "bool JobMarketManager::HasApplication(this, teamId) 0x147DD4FC4"},
         {"jmm_apply_for_job",
          "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 8B FA 48 8B D9 E8 ?? ?? ?? ??",
          "none", 0, "void JobMarketManager::ApplyForJob(this, teamId) 0x147DBBF64"},
         {"jmm_make_offer",
          "48 89 5C 24 08 57 48 83 EC 20 48 8B 41 08 4C 8B D9 48 8B DA 4C 8B 80 18 03 00 00 49 8B 08", "none", 0,
          "void JobMarketManager::MakeOffer(this, JobOffer*) 0x147DD5510"},
         {"calendar_today_int",
          "48 83 EC 28 83 CA FF E8 ?? ?? ?? ?? 3C 01 75 0C 6B 41 08 64 03 41 04 6B D0 64 03 11", "none", 0,
          "int TodayInt(CalendarDate*) 0x142AA5824: yyyymmdd of the career calendar"},
         {"speech_system_ptr", "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 F0 00 00 00 48 8D 54 24 20 48 8B 08 4C 8B 81 D8 00 00 00", "rip", 0,
          "global pointer to the SpeechSystem (0x14C27D590 on this build): [+0x50] the commentary event registry, [+0x58] the "
          "variation selector (docs/callnames.md section 6); read by the commentary-bank notes, not hooked"},
         // Spoken callnames through the game's audio service (docs/callnames.md section 5, core/commentary_audio.h).
         // The Create Player list reader 0x1480B2128 hands its id vector to the service: the call site is the anchor
         // for the registry global and the getter; the strings are resolved from the instructions that load them, so
         // the audio system sees the game's own constants.
         {"commentary_service_registry",
          "48 8B 15 ?? ?? ?? ?? 48 8D 4C 24 38 E8 ?? ?? ?? ?? 48 8B 4C 24 38 48 85 C9 74 ?? 48 8B 01 FF 50 60 48 85 C0 74 ?? 48 8B 08 48 8D 54 24 50 4C 8B",
          "rip", 0, "global slot holding the service registry pointer (0x14C2A8590): [slot] is the registry"},
         {"commentary_service_get",
          "48 8B 15 ?? ?? ?? ?? 48 8D 4C 24 38 E8 ?? ?? ?? ?? 48 8B 4C 24 38 48 85 C9 74 ?? 48 8B 01 FF 50 60 48 85 C0 74 ?? 48 8B 08 48 8D 54 24 50 4C 8B",
          "rip", 12, "void** GetCommentaryService(void** out, Registry*) 0x142A52420: registry vcall 0x40 (id 0xA621C80), entry vcall 0x18 (0xA621C86); takes a reference"},
         {"commentary_filter_names",
          "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 81 EC 00 01 00 00 48 8B D9 48 8B FA 48 8B 49 10 48 8D 15 ?? ?? ?? ?? 48 8B 01 FF 90 E0 00 00 00 4C 8B F0 48 85 C0",
          "none", 0, "void FilterNames(Inner*, eastl::vector<{int row, int commentaryid}>*) 0x1439074C8: PLAYER_NAME_FE / surname_ID per element, erases the silent ones"},
         {"commentary_str_bridge",
          "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 81 EC 00 01 00 00 48 8B D9 48 8B FA 48 8B 49 10 48 8D 15 ?? ?? ?? ?? 48 8B 01 FF 90 E0 00 00 00 4C 8B F0 48 85 C0",
          "rip", 0x22, "the string \"CommentaryBridge\" FilterNames passes to the audio system's vcall 0xE0"},
         {"commentary_str_player_name_fe",
          "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 81 EC 00 01 00 00 48 8B D9 48 8B FA 48 8B 49 10 48 8D 15 ?? ?? ?? ?? 48 8B 01 FF 90 E0 00 00 00 4C 8B F0 48 85 C0",
          "rip", 0x42, "the string \"PLAYER_NAME_FE\""},
         {"commentary_str_db_events",
          "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 81 EC 00 01 00 00 48 8B D9 48 8B FA 48 8B 49 10 48 8D 15 ?? ?? ?? ?? 48 8B 01 FF 90 E0 00 00 00 4C 8B F0 48 85 C0",
          "rip", 0x50, "the string \"CommentaryDbEvents\" (event category of the audio system's vcall 0x48)"},
         {"commentary_str_player_intensity",
          "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 81 EC 00 01 00 00 48 8B D9 48 8B FA 48 8B 49 10 48 8D 15 ?? ?? ?? ?? 48 8B 01 FF 90 E0 00 00 00 4C 8B F0 48 85 C0",
          "rip", 0x7F, "the string \"player_intensity\" (query parameter, always 2)"},
         {"commentary_str_surname_id",
          "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 81 EC 00 01 00 00 48 8B D9 48 8B FA 48 8B 49 10 48 8D 15 ?? ?? ?? ?? 48 8B 01 FF 90 E0 00 00 00 4C 8B F0 48 85 C0",
          "rip", 0x99, "the string \"surname_ID\" (query parameter = commentary id)"},
         {"speech_query_ctor",
          "40 53 48 83 EC 20 48 8B D9 48 8D 05 ?? ?? ?? ?? 33 C9 89 4B 08 48 89 03 48 8D 05 ?? ?? ?? ?? 48 89 53 10 89 4B 18 48 89 4B 20 48 89",
          "none", 0, "SpeechQuery* ctor(SpeechQuery*, ScratchScope*, EventCtx*) 0x1407B0EC0 (object 0x84 bytes)"},
         {"speech_query_set_int",
          "48 89 5C 24 08 57 48 83 EC 20 41 8B F8 48 8B D9 E8 ?? ?? ?? ?? 3B 43 18 73 ?? 8B D0 48 8B 43 20 48 8B 0C D0 48 85 C9 75 ?? 48 8B 5C",
          "none", 0, "void SpeechQuery::SetInt(this, const char* name, int) 0x1407B03E4: finds the parameter by name hash or adds it"},
         {"speech_query_dtor",
          "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 83 EC 20 33 F6 48 8D 05 ?? ?? ?? ?? 48 8D 79 20 48 8B D9 48 89 01 39 71 18 76 ?? 48",
          "none", 0, "void SpeechQuery::~SpeechQuery(this) 0x1407B0B6C"},
         {"scratch_scope_ctor",
          "40 53 48 83 EC 20 48 8D 05 ?? ?? ?? ?? C7 41 08 01 00 00 00 48 89 01 48 8B D9 C7 41 0C 01 00 00 00 48 C7 41 10 00 00 00 00 E8 ?? ?? ?? ?? 48 89 43 18",
          "none", 0, "ScratchScope* ctor(ScratchScope*) 0x140670BF4: 0x40-byte scope of the thread's scratch allocator (the query's parameters live in it)"},
         {"scratch_scope_dtor",
          "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 48 8B F9 48 8D 05 ?? ?? ?? ?? 48 89 01 48 83 79 38 00 75 ?? 4C 8B 77 28 8B",
          "none", 0, "void ~ScratchScope(this) 0x14053A030: rewinds the scratch allocator"},
         {"commentary_service_vtable",
          "48 8D 0D ?? ?? ?? ?? 48 89 53 18 48 89 0B 48 8D 05 ?? ?? ?? ?? 44 8D 75 10 48 89 43",
          "rip", 0, "CommentaryService vtable (0x14A8C6D70) from its constructor 0x1438A145C: slot 1 Release, slot 3 QueryInterface, slot 12 names object"},
         {"commentary_names_vtable",
          "48 8D 0D ?? ?? ?? ?? 48 89 68 08 48 89 08 EB ?? 48 8B C5 48 8B D6 48 89 43 40 49 8B CE E8",
          "rip", 0, "vtable of the names object the service keeps at +0x40 (0x14A8C5F48): slot 24 = the filter forwarder"},
         // the in-match / frontend per-player check 0x14294A0F4 (PLAYER_LOW_SIMPLE / PLAYER_LOW_LINK with player_db_pID):
         // only its string constants are needed, Turbo builds the queries itself
         {"commentary_str_player_low_simple",
          "49 8B 4D 08 4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8B 01 FF 50 48 48 8D 4D D0 4C 8B F8 E8 ?? ?? ?? ?? C7 45 28 00",
          "rip", 0x4, "the string \"PLAYER_LOW_SIMPLE\" (player-specific recordings event), from 0x14294A15B"},
         {"commentary_str_player_db_pid",
          "49 8B 4D 08 4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8B 01 FF 50 48 48 8D 4D D0 4C 8B F8 E8 ?? ?? ?? ?? C7 45 28 00",
          "rip", 0xC5, "the string \"player_db_pID\" (query parameter = player id)"},
         {"commentary_str_player_low_link",
          "49 8B 4D 08 4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8B 01 FF 50 48 48 8D 4D D0 4C 8B F8 E8 ?? ?? ?? ?? C7 45 28 00",
          "rip", 0xEF, "the string \"PLAYER_LOW_LINK\""},
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
