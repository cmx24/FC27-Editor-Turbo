// FC 27 LE Turbo GUI - game-code hooks inside FC27.exe (see game_hooks.h and docs/re/game_thread.md)
#include "game_hooks.h"

#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#include "MinHook.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;

namespace {

struct ExecSection {
    std::string name;
    uint64_t start = 0, end = 0;  // [start, end) virtual addresses
};

struct HookRec {
    std::string name;
    void* target = nullptr;
    std::atomic<long long> calls{0};
    std::atomic<long long> errors{0};
    std::atomic<bool> killed{false};
    std::string note;
    bool active = false;
};

std::mutex g_mutex;  // state below (not the counters)
bool g_installed = false;
bool g_allowed = false;
std::string g_build;
std::string g_table_source;
std::string g_note;
uint64_t g_base = 0;
std::vector<ExecSection> g_sections;
turbo::SignatureTable g_table;
std::vector<turbo::SigResult> g_results;
std::deque<HookRec*> g_hooks;  // never freed: detours read them for the lifetime of the process

// kill switches (cached)
std::atomic<bool> g_global_off{false};
std::atomic<unsigned long long> g_next_switch_check{0};

// dispatcher
std::mutex g_queue_mutex;
std::deque<std::function<void()>> g_queue;
constexpr size_t kMaxQueue = 256;
std::atomic<bool> g_tick_hooked{false};
std::atomic<long long> g_ticks{0}, g_pumps{0}, g_ran{0};
std::atomic<uint32_t> g_game_tid{0};
std::atomic<bool> g_tick_logged{false};

fs::path out_dir() { return le_root() / "turbo_output"; }
fs::path global_off_path() { return out_dir() / "game_hooks_off.txt"; }
fs::path hook_off_path(const std::string& name) { return out_dir() / ("hook_" + name + "_off.txt"); }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// Re-reads the kill switch files at most every 2 s (called from detours: must be cheap)
void refresh_switches() {
    const unsigned long long now = GetTickCount64();
    unsigned long long next = g_next_switch_check.load();
    if (now < next) return;
    if (!g_next_switch_check.compare_exchange_strong(next, now + 2000)) return;
    g_global_off = file_exists(global_off_path());
    std::lock_guard<std::mutex> lock(g_mutex);
    for (HookRec* h : g_hooks) h->killed = file_exists(hook_off_path(h->name));
}

HookRec* find_hook(const char* name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (HookRec* h : g_hooks)
        if (h->name == name) return h;
    return nullptr;
}

bool in_exec_section(uint64_t addr) {
    for (const auto& s : g_sections)
        if (addr >= s.start && addr < s.end) return true;
    return false;
}

// The main module's PE header: build key and executable sections
bool locate_game(std::string& err) {
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) {
        err = "GetModuleHandle(NULL) failed";
        return false;
    }
    const uint8_t* base = reinterpret_cast<const uint8_t*>(exe);
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) {
        err = "bad DOS header";
        return false;
    }
    const IMAGE_NT_HEADERS64* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        err = "not a 64-bit PE image";
        return false;
    }
    g_base = reinterpret_cast<uint64_t>(base);
    g_build = turbo::build_key(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
    g_sections.clear();
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    const unsigned n = nt->FileHeader.NumberOfSections;
    for (unsigned i = 0; i < n && i < 96; ++i) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        if (sec[i].Misc.VirtualSize == 0) continue;
        ExecSection s;
        char name[9] = {0};
        std::memcpy(name, sec[i].Name, 8);
        s.name = name;
        s.start = g_base + sec[i].VirtualAddress;
        s.end = s.start + sec[i].Misc.VirtualSize;
        g_sections.push_back(s);
    }
    wchar_t path[MAX_PATH * 2] = {0};
    GetModuleFileNameW(exe, path, MAX_PATH * 2);
    log("game hooks: main module %ls at 0x%llX, build %s, %zu executable section(s)", path,
        static_cast<unsigned long long>(g_base), g_build.c_str(), g_sections.size());
    for (const auto& s : g_sections)
        log("game hooks:   section %-8s 0x%llX-0x%llX (%llu KB)", s.name.c_str(), static_cast<unsigned long long>(s.start),
            static_cast<unsigned long long>(s.end), static_cast<unsigned long long>((s.end - s.start) / 1024));
    return true;
}

// Scan one signature over every executable section (the match must be unique across all of them)
turbo::SigResult scan_signature(const turbo::Signature& s) {
    turbo::SigResult total;
    total.name = s.name;
    total.state = turbo::SigState::Missing;
    total.error = "no match";
    if (s.pattern.empty()) return turbo::resolve_signature(s, nullptr, 0, 0);
    ProcessMemory mem;
    std::vector<uint8_t> buf;
    for (const auto& sec : g_sections) {
        const size_t len = static_cast<size_t>(sec.end - sec.start);
        // The image section is readable in-process, but copy it through ReadProcessMemory so an unmapped page (the
        // protected parts of FC27.exe) fails instead of faulting. Pages that cannot be read are skipped in 64 KB steps.
        buf.resize(len);
        size_t got = 0;
        for (size_t off = 0; off < len; off += 0x10000) {
            size_t n = len - off < 0x10000 ? len - off : 0x10000;
            if (mem.read(sec.start + off, buf.data() + off, n)) got += n;
            else std::memset(buf.data() + off, 0, n);
        }
        turbo::SigResult r = turbo::resolve_signature(s, buf.data(), len, sec.start);
        if (r.state == turbo::SigState::BadPattern) return r;
        if (r.state == turbo::SigState::Ambiguous) return r;
        if (r.state == turbo::SigState::Found) {
            if (total.state == turbo::SigState::Found) {
                total.state = turbo::SigState::Ambiguous;
                total.hits = 2;
                total.error = "matches in more than one section";
                return total;
            }
            total = r;
        }
        (void)got;
    }
    return total;
}

bool load_table_file(const fs::path& p, std::string& err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        err = "cannot open";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    turbo::SignatureTable t;
    if (!turbo::parse_signature_table(ss.str(), t, err)) return false;
    if (t.build != g_build) {
        err = "the file is for build " + t.build + ", the game is " + g_build;
        return false;
    }
    g_table = std::move(t);
    return true;
}

// Writes a template next to the DLL when no table exists for this build, so a title update can be followed up
void write_template(const fs::path& p) {
    std::error_code ec;
    if (fs::exists(p, ec)) return;
    turbo::SignatureTable t;
    t.build = g_build;
    t.game = "FC27.exe";
    t.sigs.push_back({"game_tick", "", "none", 0, "fill in: per-frame game-thread function (docs/re/game_thread.md)"});
    fs::path tmp = p;
    tmp += ".tmp";
    std::ofstream f(tmp, std::ios::binary);
    if (!f) return;
    f << turbo::signature_table_json(t) << "\n";
    f.close();
    fs::rename(tmp, p, ec);
}

// ---------------------------------------------------------------- dispatcher
void drain_queue(const char* who) {
    g_game_tid = GetCurrentThreadId();
    for (;;) {
        std::function<void()> job;
        {
            std::lock_guard<std::mutex> lock(g_queue_mutex);
            if (g_queue.empty()) return;
            job = std::move(g_queue.front());
            g_queue.pop_front();
        }
        try {
            job();
            ++g_ran;
        } catch (const std::exception& e) {
            log("game thread (%s): queued job failed: %s", who, e.what());
        } catch (...) {
            log("game thread (%s): queued job failed: unknown exception", who);
        }
    }
}

// Generic pass-through detour for the per-frame function: the first four integer arguments and the integer return value
// are forwarded untouched; the queue is drained AFTER the original returned, so the original sees its arguments exactly
// as the game set them. (A target that takes floating-point arguments or more than four arguments must not be hooked
// through this detour: docs/re/game_thread.md.)
using TickFn = void* (*)(void*, void*, void*, void*);
TickFn o_tick = nullptr;

void* hk_tick(void* a, void* b, void* c, void* d) {
    void* r = o_tick(a, b, c, d);
    ++g_ticks;
    if (!game_hook_enabled("game_tick")) return r;
    game_hook_called("game_tick");
    if (!g_tick_logged.exchange(true)) log("game thread: game_tick hook runs on thread %lu", GetCurrentThreadId());
    HOOK_BODY("game_tick", { drain_queue("game_tick hook"); });
    return r;
}

}  // namespace

// ---------------------------------------------------------------- public API
bool game_hooks_allowed() { return g_installed && g_allowed && !g_global_off.load(); }
std::string game_build_key() { return g_build; }

uint64_t game_signature(const char* name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& r : g_results)
        if (r.name == name && r.state == turbo::SigState::Found) return r.address;
    return 0;
}

bool game_hook_enabled(const char* name) {
    refresh_switches();
    if (g_global_off.load()) return false;
    HookRec* h = find_hook(name);
    return h && !h->killed.load();
}

void game_hook_error(const char* name, const char* what) {
    HookRec* h = find_hook(name);
    if (h) {
        long long n = ++h->errors;
        if (n <= 5) log("game hook %s: exception caught in the detour: %s", name, what ? what : "?");
    } else {
        log("game hook %s: exception caught in the detour: %s", name, what ? what : "?");
    }
}

void game_hook_called(const char* name) {
    HookRec* h = find_hook(name);
    if (h) ++h->calls;
}

bool install_game_hook_at(const char* name, void* target, void* detour, void** original) {
    if (!name || !target || !detour || !original) {
        log("game hook %s: bad arguments", name ? name : "?");
        return false;
    }
    if (!game_hooks_allowed()) {
        log("game hook %s: not installed (%s)", name, g_note.c_str());
        return false;
    }
    if (!in_exec_section(reinterpret_cast<uint64_t>(target))) {
        log("game hook %s: target 0x%llX is outside FC27.exe's executable sections: refused", name,
            static_cast<unsigned long long>(reinterpret_cast<uint64_t>(target)));
        return false;
    }
    if (find_hook(name)) {
        log("game hook %s: already installed", name);
        return false;
    }
    if (file_exists(hook_off_path(name))) {
        log("game hook %s: kill switch turbo_output\\hook_%s_off.txt present: not installed", name, name);
        return false;
    }
    auto* h = new HookRec();
    h->name = name;
    h->target = target;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_hooks.push_back(h);  // registered before enabling: the detour may run at once
    }
    guard_hold("installing a game-code hook");
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st == MH_OK) st = MH_EnableHook(target);
    guard_release();
    if (st != MH_OK) {
        h->note = std::string("MinHook: ") + MH_StatusToString(st);
        log("game hook %s at 0x%llX: %s", name, static_cast<unsigned long long>(reinterpret_cast<uint64_t>(target)),
            h->note.c_str());
        if (st != MH_ERROR_ALREADY_CREATED) MH_RemoveHook(target);
        return false;
    }
    h->active = true;
    log("game hook %s installed at 0x%llX (kill switch: turbo_output\\hook_%s_off.txt)", name,
        static_cast<unsigned long long>(reinterpret_cast<uint64_t>(target)), name);
    return true;
}

bool install_game_hook(const char* name, const char* signature, void* detour, void** original) {
    if (!signature) return false;
    uint64_t addr = game_signature(signature);
    if (!addr) {
        log("game hook %s: signature %s is not available on build %s: not installed", name ? name : "?", signature,
            g_build.c_str());
        return false;
    }
    return install_game_hook_at(name, reinterpret_cast<void*>(addr), detour, original);
}

const char* run_on_game_thread(std::function<void()> fn) {
    if (!fn) return "";
    {
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        while (g_queue.size() >= kMaxQueue) g_queue.pop_front();
        g_queue.push_back(std::move(fn));
    }
    if (g_tick_hooked.load() && game_hook_enabled("game_tick")) return "hook";
    if (g_pumps.load() > 0) return "lua";
    return "";
}

bool game_thread_hooked() { return g_tick_hooked.load() && game_hook_enabled("game_tick"); }
uint32_t game_thread_id() { return g_game_tid.load(); }

turbo::HookReport game_hooks_report() {
    turbo::HookReport r;
    std::lock_guard<std::mutex> lock(g_mutex);
    r.build = g_build;
    r.table_source = g_table_source;
    r.enabled = g_installed && g_allowed && !g_global_off.load();
    r.note = g_note;
    r.signatures = g_results;
    for (HookRec* h : g_hooks) {
        turbo::HookStatus s;
        s.name = h->name;
        s.active = h->active;
        s.killed = h->killed.load();
        s.target = reinterpret_cast<uint64_t>(h->target);
        s.calls = h->calls.load();
        s.errors = h->errors.load();
        s.note = h->note;
        r.hooks.push_back(s);
    }
    r.dispatcher_hooked = g_tick_hooked.load();
    r.dispatcher_ticks = g_ticks.load();
    r.dispatcher_pumps = g_pumps.load();
    r.dispatcher_ran = g_ran.load();
    {
        std::lock_guard<std::mutex> ql(g_queue_mutex);
        r.queued = g_queue.size();
    }
    r.game_thread_id = g_game_tid.load();
    return r;
}

void install_game_hooks() {
    try {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_installed) return;
        g_installed = true;
        g_allowed = false;
        wchar_t env[8];
        if (GetEnvironmentVariableW(L"TURBO_GUI_NO_GAME_HOOKS", env, 8) > 0 && env[0] == L'1') {
            g_note = "off: TURBO_GUI_NO_GAME_HOOKS=1";
            log("game hooks: %s", g_note.c_str());
            return;
        }
        if (file_exists(global_off_path())) {
            g_global_off = true;
            g_note = "off: turbo_output\\game_hooks_off.txt present";
            log("game hooks: %s", g_note.c_str());
            return;
        }
        std::string err;
        if (!locate_game(err)) {
            g_note = "off: " + err;
            log("game hooks: %s", g_note.c_str());
            return;
        }
        if (g_sections.empty()) {
            g_note = "off: the main module has no executable section";
            log("game hooks: %s", g_note.c_str());
            return;
        }
        // Signature table: file next to Turbo.dll first, then the built-in one
        const fs::path file = le_root() / "turbo" / ("signatures_" + g_build + ".json");
        if (file_exists(file)) {
            if (load_table_file(file, err)) {
                g_table_source = "file";
                log("game hooks: signature table %s (%zu signatures)", file.string().c_str(), g_table.sigs.size());
            } else {
                log("game hooks: %s ignored: %s", file.string().c_str(), err.c_str());
            }
        }
        if (g_table_source.empty()) {
            const turbo::SignatureTable* b = turbo::builtin_signature_table(g_build);
            if (b) {
                g_table = *b;
                g_table_source = "built-in";
                log("game hooks: built-in signature table for build %s (%zu signatures)", g_build.c_str(), g_table.sigs.size());
            }
        }
        if (g_table_source.empty()) {
            std::string known;
            for (const auto& b : turbo::builtin_builds()) known += (known.empty() ? "" : ", ") + b;
            g_note = "off: game build " + g_build + " is not in any signature table (known: " + known +
                     "); a title update needs turbo\\signatures_" + g_build + ".json";
            log("game hooks: %s", g_note.c_str());
            write_template(file);
            return;
        }
        // Scan every signature (unique match required)
        g_results.clear();
        int found = 0, missing = 0, ambiguous = 0, skipped = 0;
        const DWORD t0 = GetTickCount();
        for (const auto& s : g_table.sigs) {
            turbo::SigResult r = scan_signature(s);
            switch (r.state) {
                case turbo::SigState::Found: ++found; break;
                case turbo::SigState::Missing: ++missing; break;
                case turbo::SigState::Ambiguous: ++ambiguous; break;
                case turbo::SigState::Skipped: ++skipped; break;
                default: ++missing; break;
            }
            if (r.state == turbo::SigState::Found)
                log("game hooks: signature %s: found, match 0x%llX -> 0x%llX", s.name.c_str(),
                    static_cast<unsigned long long>(r.match), static_cast<unsigned long long>(r.address));
            else
                log("game hooks: signature %s: %s (%s)", s.name.c_str(), turbo::sig_state_name(r.state), r.error.c_str());
            g_results.push_back(std::move(r));
        }
        log("game hooks: %d found, %d missing, %d ambiguous, %d skipped in %lu ms", found, missing, ambiguous, skipped,
            static_cast<unsigned long>(GetTickCount() - t0));
        g_allowed = true;
        g_note = "build " + g_build + " known (" + g_table_source + " table): " + std::to_string(found) + " of " +
                 std::to_string(g_table.sigs.size()) + " signatures found";
    } catch (const std::exception& e) {
        g_allowed = false;
        g_note = std::string("off: ") + e.what();
        log("game hooks: %s", g_note.c_str());
        return;
    } catch (...) {
        g_allowed = false;
        g_note = "off: unknown error while scanning";
        log("game hooks: %s", g_note.c_str());
        return;
    }
    // The dispatcher hook (outside the lock: install_game_hook_at takes it)
    uint64_t tick = game_signature("game_tick");
    if (tick) {
        if (install_game_hook_at("game_tick", reinterpret_cast<void*>(tick), reinterpret_cast<void*>(&hk_tick),
                                 reinterpret_cast<void**>(&o_tick))) {
            g_tick_hooked = true;
            log("game thread dispatcher: prompt (game_tick hook)");
        }
    }
    if (!g_tick_hooked.load())
        log("game thread dispatcher: queued work runs on the next career-mode event (Turbo's Lua side calls turbo_game_pump)");
}

}  // namespace host

extern "C" __declspec(dllexport) int turbo_game_pump(void*) {
    using namespace host;
    long long n = ++g_pumps;
    if (n == 1) log("game thread: Turbo's Lua side pumps the dispatcher from thread %lu (career-mode events)", GetCurrentThreadId());
    HOOK_BODY("turbo_game_pump", { drain_queue("Lua pump"); });
    return 0;
}
