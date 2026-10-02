// FC 27 LE Turbo GUI - DLL entry. Loaded into FC27.exe by Turbo's Lua side (package.loadlib) or by
// TurboInjector.exe, only while Live Editor is running.
#include <windows.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

#include "host.h"

namespace fs = std::filesystem;

namespace host {

static HMODULE g_self = nullptr;
static fs::path g_root;
static std::mutex g_log_mutex;

fs::path le_root() { return g_root; }

void log(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::error_code ec;
    fs::create_directories(g_root / "turbo_output", ec);
    FILE* f = _wfopen((g_root / "turbo_output" / "turbo_gui.log").wstring().c_str(), L"a");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d  %s\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                 st.wSecond, st.wMilliseconds, msg);
    std::fclose(f);
}

bool ProcessMemory::read(uint64_t addr, void* out, size_t n) {
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr), out, n, &got)) return false;
    return got == n;
}

bool ProcessMemory::write(uint64_t addr, const void* in, size_t n) {
    SIZE_T done = 0;
    if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(addr), in, n, &done)) return false;
    return done == n;
}

// <LE>\turbo\Turbo.dll -> <LE>; falls back to the DLL's own folder
static fs::path find_root(HMODULE self) {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(self, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    fs::path dll = fs::path(std::wstring(buf, n));
    fs::path dir = dll.parent_path();
    for (fs::path cand : {dir, dir.parent_path()}) {
        std::error_code ec;
        if (fs::exists(cand / "turbo_config.json", ec) || fs::exists(cand / "FCLiveEditor.DLL", ec)) return cand;
    }
    return dir.parent_path();
}

// Turbo's Lua side writes bridge_state.json when Live Editor starts it (right before it loads
// Turbo.dll) and on career events. A recent file means Live Editor's Lua engine is running Turbo,
// even if Live Editor's DLL is not in the module list. TurboInjector.exe applies the same rule.
static bool lua_side_just_ran() {
    std::error_code ec;
    fs::path p = g_root / "turbo_output" / "bridge_state.json";
    auto t = fs::last_write_time(p, ec);
    if (ec) return false;
    auto age = fs::file_time_type::clock::now() - t;
    return age < std::chrono::minutes(15) && age > -std::chrono::seconds(5);
}

static DWORD WINAPI init_thread(LPVOID) {
    log("Turbo GUI loading in process %lu", static_cast<unsigned long>(GetCurrentProcessId()));
    if (!GetModuleHandleW(L"FCLiveEditor.DLL")) {
        if (!lua_side_just_ran()) {
            log("FCLiveEditor.DLL is not loaded in this process; Turbo GUI only runs together with Live Editor. Not starting.");
            return 0;
        }
        log("FCLiveEditor.DLL is not in the module list, but Turbo's Lua side just ran in Live Editor: starting.");
    }
    if (!start_overlay(g_self)) log("overlay not started");
    return 0;
}

}  // namespace host

// Dear ImGui assertion (see ui/turbo_imconfig.h): log the first ones, never abort the game
namespace turbo {
void imgui_assert_failed(const char* expr, const char* file, int line) {
    static int count = 0;
    if (count >= 20) return;
    ++count;
    const char* base = file;
    for (const char* p = file; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    host::log("ImGui check failed: %s (%s:%d)%s", expr, base, line, count == 20 ? " - further failures not logged" : "");
}
}  // namespace turbo

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        host::g_self = inst;
        DisableThreadLibraryCalls(inst);
        // Never unload: the hooks point into this module. Lua's package.loadlib frees its libraries when
        // a Lua state is closed, and that must not take Turbo.dll out from under the game.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&DllMain), &pinned);
        host::g_root = host::find_root(inst);
        HANDLE th = CreateThread(nullptr, 0, host::init_thread, nullptr, 0, nullptr);
        if (th) CloseHandle(th);
    }
    return TRUE;
}

// Lets Lua's package.loadlib(path, "luaopen_turbo_gui") succeed as well as loadlib(path, "*")
extern "C" __declspec(dllexport) int luaopen_turbo_gui(void*) { return 0; }
