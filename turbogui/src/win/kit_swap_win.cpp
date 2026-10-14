// FC 27 LE Turbo GUI - 3D kits on the pitch, Windows host (see kit_swap_win.h, core/kit_swap.h)
#include "kit_swap_win.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "core/kit_swap.h"
#include "game_hooks.h"
#include "host.h"
#include "ui/app.h"

namespace host {

namespace fs = std::filesystem;

namespace {

// int64 OodleLZ_Decompress(const void* comp, int64 compLen, void* raw, int64 rawLen, int fuzzSafe, int checkCRC, int verbosity,
//                          void* decBufBase, int64 decBufSize, void* fpCallback, void* callbackUserData, void* decoderMemory,
//                          int64 decoderMemorySize, int threadPhase)
using DecompressFn = int64_t (*)(const void*, int64_t, void*, int64_t, int, int, int, void*, int64_t, void*, void*, void*, int64_t, int);

DecompressFn g_orig = nullptr;
std::atomic<turbo::kitswap::Runtime*> g_rt{nullptr};
std::atomic<HookHandle> g_handle{nullptr};
std::atomic<bool> g_installed{false};
std::string g_off = "not installed yet";
std::atomic<bool> g_watch{true};

int64_t detour(const void* comp, int64_t comp_len, void* raw, int64_t raw_len, int fuzz, int crc, int verb, void* dec_base, int64_t dec_size, void* cb,
               void* cb_user, void* dec_mem, int64_t dec_mem_size, int phase) {
    const int64_t r = g_orig(comp, comp_len, raw, raw_len, fuzz, crc, verb, dec_base, dec_size, cb, cb_user, dec_mem, dec_mem_size, phase);
    turbo::kitswap::Runtime* rt = g_rt.load(std::memory_order_acquire);
    if (rt) {
        rt->count_call();
        if (game_hook_live(g_handle.load(std::memory_order_acquire)))
            HOOK_BODY("kit_oodle", {
                rt->on_decompressed(static_cast<const uint8_t*>(comp), comp_len > 0 ? uint64_t(comp_len) : 0, static_cast<uint8_t*>(raw), raw_len > 0 ? uint64_t(raw_len) : 0, r,
                                    (cb ? 1u : 0u) | (dec_base ? 2u : 0u) | (dec_mem ? 4u : 0u), phase);
            });
    }
    return r;
}

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

}  // namespace

void install_kit_swap(turbo::App& app) {
    using namespace turbo::kitswap;
    static bool tried = false;
    if (tried) return;
    tried = true;
    wchar_t exe[MAX_PATH * 2] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    const fs::path game = fs::path(exe).parent_path();
    // The Oodle entry the Runtime uses for the game's own blocks: the ORIGINAL (the trampoline), so Turbo's reads never pass the detour
    OodleFn oodle = [](const uint8_t* s, size_t n, uint8_t* d, size_t dn) {
        DecompressFn f = g_orig;
        if (!f) {
            HMODULE m = GetModuleHandleW(L"oo2core_9_win64.dll");
            f = m ? reinterpret_cast<DecompressFn>(reinterpret_cast<void*>(GetProcAddress(m, "OodleLZ_Decompress"))) : nullptr;
        }
        return f && f(s, int64_t(n), d, int64_t(dn), 1, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, 3) == int64_t(dn);
    };
    auto* rt = new Runtime(le_root(), game, oodle);  // lives as long as the process
    app.kit_swap = rt;
    // Without turbo_output\\kits3d_on.txt a kit left on in an old session reads nothing, scans nothing and installs no hook
    rt->require_opt_in = true;
    // The reading of the game's kit data (and the plan) runs below normal priority so it does not compete with the game's own loading
    rt->worker_init = []() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); };
    rt->set_killed(file_exists(le_root() / "turbo_output" / "kits3d_off.txt"));  // before start(): the kill switch also stops the first build
    // The hook goes in when a kit is first switched on (or one was left on last session), never for a player who does not use it
    rt->ensure_hook = [rt](std::string* why_out) {
        std::string why;
        if (!rt->opted_in()) {
            g_off = "opt-in file turbo_output\\kits3d_on.txt is missing: no hook is installed";
            if (why_out) *why_out = g_off;
            return false;
        }
        if (file_exists(le_root() / "turbo_output" / "kits3d_off.txt")) {
            // a kit left on must not bring the hook back at the next start while the kill switch is there (crash recovery)
            g_off = "turbo_output\\kits3d_off.txt is present: the hook is not installed (delete it, then switch the kit on again)";
            if (why_out) *why_out = g_off;
            return false;
        }
        // MinHook stores the trampoline into g_orig BEFORE it enables the hook: the detour can run at once
        if (install_game_dll_export_hook("kit_oodle", L"oo2core_9_win64.dll", "OodleLZ_Decompress", reinterpret_cast<void*>(&detour), reinterpret_cast<void**>(&g_orig), &why)) {
            g_handle = game_hook_handle("kit_oodle");
            g_rt = rt;
            g_installed = true;
            g_off.clear();
            return true;
        }
        g_off = why;
        if (why_out) *why_out = why;
        return false;
    };
    rt->start();
    // the kill switch file and the log, every 2 s, even while the window is hidden
    std::thread([rt]() {
        const fs::path off = le_root() / "turbo_output" / "kits3d_off.txt";
        while (g_watch.load()) {
            rt->set_killed(file_exists(off));
            rt->flush_log(false);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }).detach();
    log("kit swap: ready (the Oodle hook is installed when a kit is first switched on; kill switch turbo_output\\kits3d_off.txt)");
}

std::vector<std::string> kit_swap_status() {
    std::vector<std::string> v;
    turbo::kitswap::Runtime* rt = g_rt.load();
    if (!rt) {
        v.push_back(std::string("3D kit swap: no kit switched on, no hook installed") + (g_off.empty() ? "" : " (" + g_off + ")"));
        return v;
    }
    const auto s = rt->status();
    v.push_back("3D kit swap: " + s.state + (s.killed ? " [kill switch]" : "") + " | decompress calls " + std::to_string(s.calls) + " | blocks " + std::to_string(s.blocks) +
                " | recognised " + std::to_string(s.hits) + " | overwritten " + std::to_string(s.swapped));
    return v;
}

}  // namespace host
