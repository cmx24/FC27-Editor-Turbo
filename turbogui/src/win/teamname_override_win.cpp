// FC 27 LE Turbo GUI - live team names inside FC27.exe: the detour, the cached switches and the Service the App talks
// to (see teamname_override_win.h, core/teamname_override.h)
#include "teamname_override_win.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "core/gamethread.h"
#include "core/teamname_override.h"
#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

constexpr const char* kHook = "team_names";
constexpr uint64_t kLookupBodyBytes = 0x300;  // the guard's call site must lie this close past Lookup's entry (0x110)

// ---------------------------------------------------------------- what the detour reads (atomics only)
using LookupFn = int (*)(void*, void*, const char*, int);  // int LocImpl::Lookup(this, eastl::string* out, const char* key, int mode)
using AssignFn = void* (*)(void*, const char*);           // eastl::string& eastl::string::assign(this, const char*)
LookupFn g_orig_lookup = nullptr;
AssignFn g_assign = nullptr;
std::atomic<HookHandle> g_hook{nullptr};
std::atomic<bool> g_live{false};         // installed, no kill switch, a table with names
std::atomic<bool> g_feature_off{false};  // turbo_output\team_names_hook_off.txt (cached)
// Never destroyed: a detour may still read any table published during the session
tnames::SnapshotSlot* const g_slot = new tnames::SnapshotSlot();
tnames::Stats g_stats;

// ---------------------------------------------------------------- install / GUI state (never taken by the detour)
std::mutex g_mutex;
bool g_install_tried = false;
bool g_installed = false;
std::string g_off = "Turbo's game hooks did not start";  // why not installed
std::string g_reason;                                    // why off while installed (switch files), "" = on
std::string g_le_note;                                   // Live Editor's hook one level below (report only)
unsigned long long g_next_refresh = 0;
bool g_seen_feature_off = false;

fs::path out_dir() { return le_root() / "turbo_output"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

// Which module owns an address ("" when none: VirtualAlloc'ed code)
std::string module_of(uint64_t addr) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(addr)), &m) ||
        !m)
        return "";
    wchar_t path[MAX_PATH * 2] = {0};
    GetModuleFileNameW(m, path, MAX_PATH * 2);
    std::wstring w(path);
    const size_t slash = w.find_last_of(L"\\/");
    if (slash != std::wstring::npos) w = w.substr(slash + 1);
    std::string out;
    for (wchar_t c : w) out += c < 128 ? static_cast<char>(c) : '?';
    return out;
}

// The inline hook at a function's entry ("" = none), e.g. "jmp [rip] -> FCLiveEditor.DLL"
std::string foreign_hook_at(uint64_t addr) {
    ProcessMemory mem;
    uint8_t code[kInlineHookProbeBytes] = {0};
    if (!mem.read(addr, code, sizeof(code))) return "unreadable entry";
    uint64_t target = 0;
    const InlineHook h = detect_inline_hook(code, sizeof(code), addr, &target);
    if (h == InlineHook::None || h == InlineHook::Truncated) return "";
    if (h == InlineHook::JmpIndirect) {
        uint64_t slot = 0;
        if (mem.rd(target, slot)) target = slot;
    }
    const std::string owner = module_of(target);
    if (owner == "Turbo.dll") return "";  // MinHook's own jump (Turbo's hook)
    return std::string(inline_hook_name(h)) + " -> " + (owner.empty() ? "unnamed code" : owner);
}

// Upper case for the game's "_upper" requests: Windows' invariant-locale mapping (any script); the portable one when
// a conversion fails. Runs on the GUI thread when a table is built, never in the detour.
std::string upper_win(const std::string& s) {
    if (s.empty()) return s;
    const int wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (wn <= 0) return tnames::utf8_upper_basic(s);
    std::wstring w(static_cast<size_t>(wn), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), &w[0], wn);
    const int un = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, w.c_str(), wn, nullptr, 0, nullptr, nullptr, 0);
    if (un <= 0) return tnames::utf8_upper_basic(s);
    std::wstring u(static_cast<size_t>(un), L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, w.c_str(), wn, &u[0], un, nullptr, nullptr, 0) <= 0)
        return tnames::utf8_upper_basic(s);
    const int bn = WideCharToMultiByte(CP_UTF8, 0, u.data(), un, nullptr, 0, nullptr, nullptr);
    if (bn <= 0) return tnames::utf8_upper_basic(s);
    std::string out(static_cast<size_t>(bn), '\0');
    WideCharToMultiByte(CP_UTF8, 0, u.data(), un, &out[0], bn, nullptr, nullptr);
    return out;
}

// ---------------------------------------------------------------- the detour
int override_body(void* out, const tnames::KeyMatch& m, int mode, int r) {
    if (!game_hook_live(g_hook.load(std::memory_order_acquire))) return r;
    const tnames::Snapshot* s = g_slot->current();
    if (!s) return r;
    const char* text = s->lookup(m, mode == 0);  // mode 0: the game asked for "<key>_upper"
    if (!text) return r;
    g_assign(out, text);  // the game's own assign on the game's own string (Lookup writes `out` the same way)
    g_stats.given.fetch_add(1, std::memory_order_relaxed);
    return 1;  // found
}

int lookup_detour(void* self, void* out, const char* key, int mode) {
    // The key is matched BEFORE the call: it is the caller's string and could live in `out`, which the original rewrites
    tnames::KeyMatch m;
    bool want = false;
    if (g_live.load(std::memory_order_relaxed)) {
        g_stats.calls.fetch_add(1, std::memory_order_relaxed);
        want = out && tnames::match_key(key, m);
        if (want) g_stats.team_keys.fetch_add(1, std::memory_order_relaxed);
    }
    int r = g_orig_lookup(self, out, key, mode);  // the game, and Live Editor's hook below it, answer first
    if (!want) return r;
    HOOK_BODY(kHook, r = override_body(out, m, mode, r));
    return r;
}

// ---------------------------------------------------------------- switches (g_mutex held)
void update_live() {
    const tnames::Snapshot* s = g_slot->current();
    g_live.store(g_installed && !g_feature_off.load() && s && !s->empty(), std::memory_order_relaxed);
}

void update_reason() {
    if (!g_installed) {
        g_reason = g_off;
        return;
    }
    if (g_feature_off.load()) g_reason = "turned off by turbo_output\\team_names_hook_off.txt";
    else if (file_exists(out_dir() / "game_hooks_off.txt")) g_reason = "Turbo's game hooks are turned off (turbo_output\\game_hooks_off.txt)";
    else if (file_exists(out_dir() / "hook_team_names_off.txt")) g_reason = "turned off by turbo_output\\hook_team_names_off.txt";
    else if (!game_hook_live(g_hook.load())) g_reason = "the hook is not active";
    else g_reason.clear();
}

// The files, at most every 2 s (force: now)
void refresh(bool force) {
    game_hooks_refresh_switches();
    const unsigned long long now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!force && now < g_next_refresh) return;
    g_next_refresh = now + 2000;
    if (g_installed) {
        const bool off = file_exists(tnames::hook_off_path(out_dir()));
        g_feature_off.store(off);
        if (off != g_seen_feature_off)
            log("team names: %s", off ? "off (turbo_output\\team_names_hook_off.txt present)" : "on again (kill switch removed)");
        g_seen_feature_off = off;
    }
    update_reason();
    update_live();
}

bool same_table(const tnames::Snapshot& a, const tnames::Snapshot& b) {
    if (a.items.size() != b.items.size()) return false;
    for (size_t i = 0; i < a.items.size(); ++i) {
        if (a.items[i].teamid != b.items[i].teamid) return false;
        for (int k = 0; k < tnames::kKinds; ++k)
            if (a.items[i].text[k] != b.items[i].text[k]) return false;
    }
    return true;
}

// ---------------------------------------------------------------- the service
class TeamNamesService : public tnames::Service {
public:
    bool available() const override {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_installed && g_reason.empty() && !g_feature_off.load() && game_hook_live(g_hook.load());
    }

    std::string why_off() const override {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_installed) return g_off;
        if (!g_reason.empty()) return g_reason;
        if (g_feature_off.load()) return "turned off by turbo_output\\team_names_hook_off.txt";
        if (!game_hook_live(g_hook.load())) return "the hook is not active";
        return "";
    }

    void publish(const tnames::Store& st) override {
        tnames::Snapshot s = tnames::build_snapshot(st, &upper_win);
        std::lock_guard<std::mutex> lock(g_mutex);
        const tnames::Snapshot* cur = g_slot->current();
        if (cur && same_table(*cur, s)) return;  // nothing changed: no new copy
        const size_t n = s.items.size();
        g_slot->publish(std::move(s));
        update_live();
        log("team names: table published (%zu clubs)%s", n, g_installed ? "" : " (the hook is not installed: not shown until Live Editor's next start)");
    }

    tnames::StatsSnapshot stats() const override { return tnames::snapshot(g_stats); }
    void refresh_switches() override { refresh(false); }
};

TeamNamesService g_service;

// The resolved match address of a signature (0 when not found)
uint64_t signature_match(const char* name) {
    const HookReport r = game_hooks_report();
    for (const auto& s : r.signatures)
        if (s.name == name && s.state == SigState::Found) return s.match;
    return 0;
}

}  // namespace

// ---------------------------------------------------------------- public API
void install_team_names() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_install_tried) return;
        g_install_tried = true;
    }
    std::string off;
    // Report only: Live Editor's hook one level below (StrTab::GetString), never touched
    if (const uint64_t st = game_signature("loc_strtab_get")) {
        const std::string h = foreign_hook_at(st);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_le_note = h.empty() ? "loc_strtab_get " + hex(st) + " carries no inline hook (Live Editor's team names file not hooked there)"
                              : "Live Editor hooks loc_strtab_get " + hex(st) + " (" + h + "); Turbo answers above it";
    }
    if (!game_hooks_allowed()) {
        char env[8] = {};
        const bool hooks_off = (GetEnvironmentVariableA("TURBO_GUI_NO_GAME_HOOKS", env, sizeof(env)) > 0 && env[0] == '1') ||
                               file_exists(out_dir() / "game_hooks_off.txt");
        off = hooks_off ? "Turbo's game hooks are turned off (turbo_output\\game_hooks_off.txt or TURBO_GUI_NO_GAME_HOOKS=1)"
                        : "this game version is not one Turbo knows (game build " + game_build_key() + ")";
    }
    const char* sigs[] = {"loc_lookup", "eastl_string_assign_cstr", "loc_lookup_out_assign"};
    for (const char* s : sigs)
        if (off.empty() && !game_signature(s)) off = std::string("this game version is not supported (signature ") + s + " not found)";
    const uint64_t lookup = game_signature("loc_lookup"), assign = game_signature("eastl_string_assign_cstr");
    if (off.empty()) {
        // layout guard: Lookup's own "out = ..." call goes to that very assign, from inside Lookup
        const uint64_t site = signature_match("loc_lookup_out_assign");
        if (game_signature("loc_lookup_out_assign") != assign || site <= lookup || site - lookup > kLookupBodyBytes)
            off = "this game version is not supported (Lookup does not write its result through " + hex(assign) + ")";
    }
    if (off.empty()) {
        const std::string h = foreign_hook_at(lookup);
        if (!h.empty()) off = "another module already hooks the game's text lookup " + hex(lookup) + " (" + h + ")";
    }
    if (off.empty()) {
        g_assign = reinterpret_cast<AssignFn>(static_cast<uintptr_t>(assign));
        if (!install_game_hook(kHook, "loc_lookup", reinterpret_cast<void*>(&lookup_detour), reinterpret_cast<void**>(&g_orig_lookup)))
            off = "the hook could not be installed (see turbo_gui.log)";
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!off.empty()) {
            g_off = off;
            g_reason = off;
            log("team names: off (%s): renamed clubs show after Live Editor's next start", off.c_str());
            if (!g_le_note.empty()) log("team names: %s", g_le_note.c_str());
            return;
        }
        g_hook.store(game_hook_handle(kHook), std::memory_order_release);
        g_installed = true;
        g_off.clear();
    }
    refresh(true);
    std::lock_guard<std::mutex> lock(g_mutex);
    log("team names: installed (Lookup %s, assign %s; kill switch turbo_output\\team_names_hook_off.txt%s)", hex(lookup).c_str(),
        hex(assign).c_str(), g_feature_off.load() ? ", present: off" : "");
    if (!g_le_note.empty()) log("team names: %s", g_le_note.c_str());
}

tnames::Service* team_names_service() { return &g_service; }

std::vector<std::string> team_names_status() {
    std::vector<std::string> out;
    const std::string why = g_service.why_off();
    const tnames::StatsSnapshot s = tnames::snapshot(g_stats);
    const tnames::Snapshot* t = g_slot->current();
    const size_t clubs = t ? t->items.size() : 0;
    char line[512];
    if (why.empty())
        std::snprintf(line, sizeof(line), "team_names: on | %zu renamed clubs | lookups %llu | team-name keys %llu | names given %llu", clubs,
                      static_cast<unsigned long long>(s.calls), static_cast<unsigned long long>(s.team_keys),
                      static_cast<unsigned long long>(s.given));
    else
        std::snprintf(line, sizeof(line), "team_names: off (%s) | %zu renamed clubs", why.c_str(), clubs);
    out.push_back(line);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_le_note.empty()) out.push_back("  " + g_le_note);
    return out;
}

}  // namespace host
