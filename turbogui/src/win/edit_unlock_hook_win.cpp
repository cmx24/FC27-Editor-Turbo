// FC 27 LE Turbo GUI - the game editors' in-memory fallback inside FC27.exe (see edit_unlock_hook_win.h)
#include "edit_unlock_hook_win.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo::edit_unlock;

namespace {

constexpr size_t kPathMax = 256;  // longer paths are not editor configs (the longest is ~70 characters)

// ---------------------------------------------------------------- what the detour reads (atomics / fixed buffers)
using LoadFn = void* (*)(void* self, void* out, void* path);  // Config* Load(this, Config* out, const eastl::string* path)
LoadFn g_orig = nullptr;
std::atomic<HookHandle> g_handle{nullptr};
std::atomic<bool> g_live_installed{false};
std::atomic<bool> g_set_enabled{true}, g_set_experimental{false}, g_set_hook{true};
std::atomic<uint32_t> g_set_off{0};
std::atomic<bool> g_kill{false};
std::atomic<unsigned long long> g_next_kill_check{0};
wchar_t g_kill_path[1024] = {0};  // filled once at install (no allocation in the detour)
KeepIds g_ids;                    // written once before the hook is enabled, read-only afterwards
HookCounters g_counters;

// ---------------------------------------------------------------- install / GUI state (never taken by the detour)
std::mutex g_mutex;
bool g_install_tried = false;
std::string g_off = "not installed yet";

fs::path out_dir() { return le_root() / "turbo_output"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

bool user_range(uintptr_t a, size_t n) { return a >= 0x10000 && a + n >= a && a + n <= 0x00007FFFFFFFFFFFull; }

// Bytes readable (write = and writable) from p, at most `cap`, through VirtualQuery (never faults, never allocates)
size_t readable_span(const void* p, size_t cap, bool write) {
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (!user_range(a, cap ? cap : 1)) return 0;
    size_t got = 0;
    while (got < cap) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(a + got), &mbi, sizeof(mbi))) break;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) break;
        const DWORD prot = mbi.Protect & 0xFF;
        const bool rw = prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_WRITECOPY ||
                        prot == PAGE_EXECUTE_WRITECOPY;
        const bool ro = rw || prot == PAGE_READONLY || prot == PAGE_EXECUTE_READ;
        if (write ? !rw : !ro) break;
        const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (end <= a + got) break;
        got += static_cast<size_t>(end - (a + got));
    }
    return got < cap ? got : cap;
}

// The walk's memory check: the parsed config lives in the game's heap (read-write)
bool readable_rw(const void* p, size_t n) { return n == 0 || readable_span(p, n, true) == n; }

// The kill switch file, re-read at most every 2 s (GetFileAttributesW on a fixed buffer: no allocation)
bool kill_now() {
    const unsigned long long now = GetTickCount64();
    unsigned long long next = g_next_kill_check.load();
    if (now >= next && g_next_kill_check.compare_exchange_strong(next, now + 2000) && g_kill_path[0])
        g_kill.store(GetFileAttributesW(g_kill_path) != INVALID_FILE_ATTRIBUTES);
    return g_kill.load();
}

// Copies the path argument (an eastl string: heap when byte +0xF has bit 7, else inline) into buf; true when it names
// an editor config Turbo unlocks. Taken before the original runs, while the caller's string is certainly alive.
bool copy_path(const void* str, char* buf, size_t& len) {
    len = 0;
    if (!str || readable_span(str, 16, false) < 16) return false;
    const uint8_t* s = static_cast<const uint8_t*>(str);
    const char* data = reinterpret_cast<const char*>(s);
    if (s[15] & 0x80) std::memcpy(&data, s, sizeof(data));
    const size_t avail = data ? readable_span(data, kPathMax - 1, false) : 0;
    while (len < avail && data[len]) {
        buf[len] = data[len];
        ++len;
    }
    if (len == avail) return false;  // no terminator where it may be read: not a path Turbo knows
    buf[len] = '\0';
    return context_index(buf, len) >= 0;
}

Gate gate_now() {
    Gate g;
    g.live = g_live_installed.load(std::memory_order_relaxed) && game_hook_live(g_handle.load(std::memory_order_acquire));
    g.kill_switch = kill_now();
    g.settings.enabled = g_set_enabled.load(std::memory_order_relaxed);
    g.settings.experimental = g_set_experimental.load(std::memory_order_relaxed);
    g.settings.hook = g_set_hook.load(std::memory_order_relaxed);
    g.settings.context_off = g_set_off.load(std::memory_order_relaxed);
    return g;
}

void* load_detour(void* self, void* out, void* path) {
    char buf[kPathMax];
    size_t len = 0;
    bool want = false;
    if (g_live_installed.load(std::memory_order_relaxed)) HOOK_BODY("edit_unlock", want = copy_path(path, buf, len));
    void* r = g_orig(self, out, path);  // the game parses the file exactly as always
    if (!want || !r || r != out) return r;
    // (no game_hook_called here: it takes Turbo's hook-table lock; the counters below are atomics)
    HOOK_BODY("edit_unlock", on_config_loaded(static_cast<uint8_t*>(r), buf, len, gate_now(), g_ids, &readable_rw, g_counters));
    return r;
}

// The keep-list ids: each attribute signature matched the game's own registration of that id (the imm32 is part of the
// pattern) and resolves to the name string, which must be exactly the expected name
bool resolve_keep_ids(std::string& why) {
    static const char* const kSigs[kKeptCount] = {"edit_attr_body_type", "edit_attr_gender", "edit_attr_preferred_position",
                                                  "edit_attr_team"};
    ProcessMemory mem;
    KeepIds ids;
    for (int i = 0; i < kKeptCount; ++i) {
        const uint64_t addr = game_signature(kSigs[i]);
        if (!addr) {
            why = std::string("signature ") + kSigs[i] + " was not found on this game build";
            return false;
        }
        char name[40] = {0};
        const std::string want = kept_at(i).name;
        if (!mem.read(addr, name, want.size() + 1) || std::memcmp(name, want.c_str(), want.size() + 1) != 0) {
            why = std::string("signature ") + kSigs[i] + " does not name " + want + " (the attribute table changed)";
            return false;
        }
        ids.ids[i] = kept_at(i).id;
    }
    g_ids = ids;
    return true;
}

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

class Service : public HookService {
public:
    void configure(const Settings& s) override {
        g_set_enabled.store(s.enabled);
        g_set_experimental.store(s.experimental);
        g_set_hook.store(s.hook);
        g_set_off.store(s.context_off);
    }
    std::vector<std::string> status() const override { return edit_unlock_hook_status(); }
    bool installed() const override { return g_live_installed.load(); }
};

Service g_service;

}  // namespace

void install_edit_unlock_hook() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_install_tried) return;
    g_install_tried = true;
    if (!game_hooks_allowed()) {
        char env[8] = {};
        const bool hooks_off = (GetEnvironmentVariableA("TURBO_GUI_NO_GAME_HOOKS", env, sizeof(env)) > 0 && env[0] == '1') ||
                               file_exists(out_dir() / "game_hooks_off.txt");
        g_off = hooks_off ? "hooks off" : "game build";
        log("game editors hook: off (%s): not installed", g_off.c_str());
        return;
    }
    // The loader and the layout guards: each one pins an offset or a stride the walk relies on
    const char* const sigs[] = {"edit_cfg_loader",     "edit_cfg_category_stride", "edit_cfg_category_items", "edit_cfg_item_vector",
                                "edit_cfg_item_children", "edit_cfg_item_flags",    "edit_cfg_item_ids",       "edit_cfg_item_stride"};
    for (const char* s : sigs)
        if (!game_signature(s)) {
            g_off = std::string("game build: signature ") + s + " was not found";
            log("game editors hook: off (%s): not installed", g_off.c_str());
            return;
        }
    std::string why;
    if (!resolve_keep_ids(why)) {
        g_off = "game build: " + why;
        log("game editors hook: off (%s): not installed", g_off.c_str());
        return;
    }
    const std::wstring kill = (out_dir() / "edit_unlock_hook_off.txt").wstring();
    if (kill.size() < sizeof(g_kill_path) / sizeof(g_kill_path[0])) {
        std::memcpy(g_kill_path, kill.c_str(), (kill.size() + 1) * sizeof(wchar_t));
        g_kill.store(GetFileAttributesW(g_kill_path) != INVALID_FILE_ATTRIBUTES);
    }
    if (!install_game_hook("edit_unlock", "edit_cfg_loader", reinterpret_cast<void*>(&load_detour),
                           reinterpret_cast<void**>(&g_orig))) {
        g_off = "hooks off: the edit_unlock hook was not installed";
        log("game editors hook: off (the edit_unlock hook was not installed)");
        return;
    }
    g_handle.store(game_hook_handle("edit_unlock"), std::memory_order_release);
    g_live_installed.store(true);
    g_off.clear();
    log("game editors hook: installed on the editor config loader %s (keep-list ids BODY_TYPE %d, GENDER %d, "
        "PREFERRED_POSITION %d, TEAM %d; kill switch turbo_output\\edit_unlock_hook_off.txt%s)",
        hex(game_signature("edit_cfg_loader")).c_str(), g_ids.ids[0], g_ids.ids[1], g_ids.ids[2], g_ids.ids[3],
        g_kill.load() ? ", present: pass-through" : "");
}

HookService* edit_unlock_hook_service() { return &g_service; }

std::vector<std::string> edit_unlock_hook_status() {
    StatusInput in;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        in.off_reason = g_off;
    }
    if (in.off_reason.empty() && !game_hook_live(g_handle.load())) in.off_reason = "hooks off: kill switch hook_edit_unlock_off.txt or game_hooks_off.txt";
    in.kill_switch = kill_now();
    in.settings.enabled = g_set_enabled.load();
    in.settings.experimental = g_set_experimental.load();
    in.settings.hook = g_set_hook.load();
    in.settings.context_off = g_set_off.load();
    return status_lines(in, g_counters);
}

}  // namespace host
