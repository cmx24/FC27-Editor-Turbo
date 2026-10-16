// FC 27 LE Turbo GUI - the ONE detour on the game's Oodle decompressor, Windows host (see oodle_shared_win.h, core/oodle_dispatch.h)
#include "oodle_shared_win.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>

#include "MinHook.h"
#include "core/attrib_ebx.h"  // clock_text
#include "game_hooks.h"
#include "host.h"

namespace host {

namespace {

using turbo::oodle::DecompressFn;

DecompressFn g_orig = nullptr;  // the trampoline: MinHook stores it BEFORE it enables the hook, so the detour can run at once
turbo::oodle::Dispatcher g_disp;

std::mutex g_mutex;  // install state below; never taken inside the detour
std::atomic<bool> g_installed{false};
std::atomic<bool> g_uses_mh{false};
void* g_target = nullptr;
std::string g_installer, g_when;  // under g_mutex

int64_t shared_detour(const void* comp, int64_t comp_len, void* raw, int64_t raw_len, int fuzz, int crc, int verb, void* dec_base, int64_t dec_size, void* cb,
                      void* cb_user, void* dec_mem, int64_t dec_mem_size, int phase) {
    return g_disp.run(g_orig, comp, comp_len, raw, raw_len, fuzz, crc, verb, dec_base, dec_size, cb, cb_user, dec_mem, dec_mem_size, phase);
}

std::string now_text() {
    return turbo::attrib::clock_text(uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
}

constexpr const char* kKit = "3D kit swap";
constexpr const char* kAttrib = "attrib EBX patch";

}  // namespace

bool oodle_shared_register(int slot, const turbo::oodle::Handler& h) { return g_disp.add(slot, h); }
void oodle_shared_retire(int slot) { g_disp.retire(slot); }

bool oodle_shared_install_early(std::string* how, std::string* why) {
    namespace fs = std::filesystem;
    auto no = [&](const std::string& s) {
        if (why) *why = s;
        return false;
    };
    // the game loads the Oodle DLL itself; wait for it (never LoadLibrary: Turbo must not change what the game loads)
    HMODULE m = nullptr;
    for (int i = 0; i < 480 && !(m = GetModuleHandleW(L"oo2core_9_win64.dll")); ++i) Sleep(250);
    if (!m) return no("oo2core_9_win64.dll was not loaded within 120 s");
    {
        wchar_t mp[MAX_PATH * 2] = {0}, ep[MAX_PATH * 2] = {0};
        GetModuleFileNameW(m, mp, MAX_PATH * 2);
        GetModuleFileNameW(GetModuleHandleW(nullptr), ep, MAX_PATH * 2);
        if (fs::path(mp).parent_path() != fs::path(ep).parent_path()) return no("oo2core_9_win64.dll is not the game folder's copy");
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_installed.load()) {  // the 3D kit swap installed it first: attach (its jump at the entry is ours)
        if (how) *how = std::string("attached to the detour the ") + g_installer + " installed at " + g_when;
        return true;
    }
    void* target = reinterpret_cast<void*>(GetProcAddress(m, "OodleLZ_Decompress"));
    if (!target) return no("OodleLZ_Decompress is not exported");
    {
        const auto* b = static_cast<const uint8_t*>(target);
        if (b[0] == 0xE9 || b[0] == 0xEB || (b[0] == 0xFF && b[1] == 0x25) || (b[0] == 0x48 && b[1] == 0xB8))
            return no("OodleLZ_Decompress already starts with a jump (another hook): not hooked");
    }
    g_uses_mh = true;  // before the call: the overlay's own MH_Initialize may run at the same moment and must accept ALREADY_INITIALIZED
    const MH_STATUS mi = MH_Initialize();
    if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) return no(std::string("MinHook init: ") + MH_StatusToString(mi));
    MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&shared_detour), reinterpret_cast<void**>(&g_orig));
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) {
        if (st != MH_ERROR_ALREADY_CREATED) MH_RemoveHook(target);
        return no(std::string("MinHook on OodleLZ_Decompress: ") + MH_StatusToString(st));
    }
    g_target = target;
    g_installer = kAttrib;
    g_when = now_text();
    g_installed = true;
    if (how) *how = std::string("installed by the ") + kAttrib + " at " + g_when;
    return true;
}

bool oodle_shared_install_for_kits(std::string* how, std::string* why) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_installed.load()) {  // the attrib patch installed it first: a hook record for "kit_oodle" (same refusals), nothing in MinHook
        if (!attach_game_hook_record("kit_oodle", g_target, why)) return false;
        if (how) *how = std::string("attached to the detour the ") + g_installer + " installed at " + g_when;
        return true;
    }
    // MinHook stores the trampoline into g_orig BEFORE it enables the hook: the detour can run at once
    if (!install_game_dll_export_hook("kit_oodle", L"oo2core_9_win64.dll", "OodleLZ_Decompress", reinterpret_cast<void*>(&shared_detour), reinterpret_cast<void**>(&g_orig), why))
        return false;
    HMODULE m = GetModuleHandleW(L"oo2core_9_win64.dll");
    g_target = m ? reinterpret_cast<void*>(GetProcAddress(m, "OodleLZ_Decompress")) : nullptr;
    g_installer = kKit;
    g_when = now_text();
    g_installed = true;
    if (how) *how = std::string("installed by the ") + kKit + " at " + g_when;
    return true;
}

bool oodle_shared_decompress(const uint8_t* s, size_t n, uint8_t* d, size_t dn) {
    DecompressFn f = g_orig;  // the trampoline: Turbo's reads never pass the detour
    if (!f) {
        HMODULE m = GetModuleHandleW(L"oo2core_9_win64.dll");
        f = m ? reinterpret_cast<DecompressFn>(reinterpret_cast<void*>(GetProcAddress(m, "OodleLZ_Decompress"))) : nullptr;
    }
    return f && f(s, int64_t(n), d, int64_t(dn), 1, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, 3) == int64_t(dn);
}

bool oodle_shared_installed() { return g_installed.load(); }
bool oodle_shared_uses_minhook() { return g_uses_mh.load() || g_installed.load(); }

std::string oodle_shared_installer() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_installer;
}

turbo::oodle::SlotStats oodle_shared_stats(int slot) { return g_disp.stats(slot); }

std::string oodle_shared_summary() {
    std::string s;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        s = g_installed.load() ? "shared Oodle detour installed by the " + g_installer + " at " + g_when : std::string("shared Oodle detour not installed");
    }
    for (int i = 0; i < turbo::oodle::kSlots; ++i) {
        const auto st = g_disp.stats(i);
        s += std::string(" | ") + turbo::oodle::slot_name(i) + ": " + (st.registered ? "registered" : st.retired ? "retired" : "not registered");
        if (st.registered)
            s += " (recognised " + std::to_string(st.seen) + ", overwritten " + std::to_string(st.overwritten) + ", skipped " + std::to_string(st.skipped) + ", errors " +
                 std::to_string(st.errors) + ")";
    }
    return s;
}

}  // namespace host
