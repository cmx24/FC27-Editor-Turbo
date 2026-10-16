// FC 27 LE Turbo GUI - 3D kits on the pitch, Windows host (see kit_swap_win.h, core/kit_swap.h)
#include "kit_swap_win.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "core/kit_swap.h"
#include "core/oodle_dispatch.h"
#include "game_hooks.h"
#include "host.h"
#include "oodle_shared_win.h"
#include "ui/app.h"

namespace host {

namespace fs = std::filesystem;

namespace {

// The detour itself is the shared one (oodle_shared_win.cpp, core/oodle_dispatch.h): this file registers the kit swap's handler in its slot.
// The handler does what the 2.10 detour did after the original returned: count_call() on every call, and, when hook "kit_oodle" is live
// (hook_kit_oodle_off.txt / game_hooks_off.txt gate THIS handler only), Runtime::on_decompressed(comp, comp_len, raw, raw_len, ret, flags, phase)
// with every exception swallowed and counted under "kit_oodle".
std::atomic<HookHandle> g_handle{nullptr};
std::atomic<turbo::kitswap::Runtime*> g_rt{nullptr};  // set once the handler is registered (kit_swap_status reads it)
std::atomic<bool> g_installed{false};
std::string g_off = "not installed yet";
std::atomic<bool> g_watch{true};
std::string g_how;  // "installed by the 3D kit swap at <time>" / "attached to the detour the attrib EBX patch installed at <time>" (status)
turbo::oodle::KitBinding g_binding;

bool kit_live() { return game_hook_live(g_handle.load(std::memory_order_acquire)); }
void kit_error(const char* what) { game_hook_error("kit_oodle", what); }

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
    // The Oodle entry the Runtime uses for the game's own blocks: the ORIGINAL (the shared trampoline, whoever installed the detour; the
    // export itself only while no detour exists yet), so Turbo's reads never pass the detour and a block Turbo reads is never rewritten
    OodleFn oodle = [](const uint8_t* s, size_t n, uint8_t* d, size_t dn) { return oodle_shared_decompress(s, n, d, dn); };
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
        // One detour for the process: installed here through game_hooks (kit_oodle), or, when the attrib EBX patch installed it first, a
        // "kit_oodle" hook record attached to it (same refusals). The handler is registered right after: until then the detour only
        // calls the original (and the attrib handler, when it is there).
        std::string how;
        if (oodle_shared_install_for_kits(&how, &why)) {
            g_handle = game_hook_handle("kit_oodle");
            g_binding.rt = rt;
            g_binding.live = &kit_live;
            g_binding.error = &kit_error;
            if (!oodle_shared_register(turbo::oodle::kSlotKits, turbo::oodle::make_kit_handler(&g_binding))) {
                g_off = "the kit slot of the shared Oodle detour is taken";
                if (why_out) *why_out = g_off;
                return false;
            }
            g_how = how;  // before g_rt: kit_swap_status reads it only after it sees g_rt
            g_rt = rt;
            g_installed = true;
            g_off.clear();
            log("kit swap: Oodle hook %s", how.c_str());
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
    // 2.11: one Oodle detour for the kit swap and the attrib EBX patch; say who installed it and whether the attrib patch shares it
    v.push_back("Oodle hook: " + g_how + (oodle_shared_stats(turbo::oodle::kSlotAttrib).registered ? " | shared with the attrib EBX patch" : ""));
    return v;
}

}  // namespace host
