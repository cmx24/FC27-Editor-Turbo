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
        if (r == raw_len && raw_len > 0 && comp_len > 0 && game_hook_live(g_handle.load(std::memory_order_acquire)))
            HOOK_BODY("kit_oodle", { rt->on_block(static_cast<const uint8_t*>(comp), uint64_t(comp_len), static_cast<uint8_t*>(raw), uint64_t(raw_len)); });
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
    std::string why;
    void* orig = nullptr;
    if (install_game_dll_export_hook("kit_oodle", L"oo2core_9_win64.dll", "OodleLZ_Decompress", reinterpret_cast<void*>(&detour), &orig, &why)) {
        g_orig = reinterpret_cast<DecompressFn>(orig);
        g_handle = game_hook_handle("kit_oodle");
        g_rt = rt;
        g_installed = true;
        g_off.clear();
        rt->set_hook_installed(true, "");
    } else {
        g_off = why;
        rt->set_hook_installed(false, "the Oodle hook is off: " + why);
    }
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
    log("kit swap: %s", g_installed.load() ? "Oodle hook installed (kill switch turbo_output\\kits3d_off.txt)" : ("off: " + g_off).c_str());
}

std::vector<std::string> kit_swap_status() {
    std::vector<std::string> v;
    turbo::kitswap::Runtime* rt = g_rt.load();
    if (!rt) {
        v.push_back("3D kit swap: off (" + (g_off.empty() ? std::string("hook not installed") : g_off) + ")");
        return v;
    }
    const auto s = rt->status();
    v.push_back("3D kit swap: " + s.state + (s.killed ? " [kill switch]" : "") + " | decompress calls " + std::to_string(s.calls) + " | blocks " + std::to_string(s.blocks) +
                " | recognised " + std::to_string(s.hits) + " | overwritten " + std::to_string(s.swapped));
    return v;
}

}  // namespace host
