// FC 27 LE Turbo GUI - DLL entry. Loaded into FC27.exe by Turbo's Lua side (package.loadlib) or by
// TurboInjector.exe, only while Live Editor is running.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/le_log.h"
#include "host.h"
#include "nlohmann/json.hpp"

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

// ---------------------------------------------------------------- crash guard
static std::atomic<int> g_guard_holds{0};
static bool g_retry_mode = false;  // the previous start did not finish: this is the one retry
static fs::file_time_type g_load_time{};

fs::file_time_type load_time() { return g_load_time; }
// Plain buffer, filled when the flag file is created: DllMain's detach path must not touch C++ statics
wchar_t g_flag_w[1024] = {0};

static fs::path guard_path() { return g_root / "turbo_output" / "turbo_gui_start.flag"; }
static fs::path disable_path() { return g_root / "turbo_output" / "turbo_gui_disable.txt"; }

void guard_hold(const char* why) {
    if (g_guard_holds.fetch_add(1) != 0) return;
    std::error_code ec;
    fs::create_directories(g_root / "turbo_output", ec);
    std::wstring path = guard_path().wstring();
    FILE* f = _wfopen(path.c_str(), L"w");
    if (!f) return;
    if (path.size() < sizeof(g_flag_w) / sizeof(g_flag_w[0])) wcscpy_s(g_flag_w, path.c_str());
    std::fprintf(f, "%s%s (process %lu, tick %lu)\n", g_retry_mode ? "RETRY: " : "", why,
                 static_cast<unsigned long>(GetCurrentProcessId()), static_cast<unsigned long>(GetTickCount()));
    std::fclose(f);
}

void guard_release() {
    int n = g_guard_holds.load();
    while (n > 0 && !g_guard_holds.compare_exchange_weak(n, n - 1)) {}
    if (n == 1) DeleteFileW(guard_path().wstring().c_str());
}

// ---------------------------------------------------------------- waiting for the game
static int env_int(const wchar_t* name, int fallback) {
    wchar_t buf[32];
    DWORD n = GetEnvironmentVariableW(name, buf, 32);
    if (n == 0 || n >= 32) return fallback;
    return _wtoi(buf);
}

struct WindowSearch {
    DWORD pid;
    int w;
    int h;
};

// A visible, un-owned top-level window of this process that is big enough to be the game window
static BOOL CALLBACK enum_windows(HWND hwnd, LPARAM lp) {
    auto* ws = reinterpret_cast<WindowSearch*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ws->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w >= 400 && h >= 300) {
        ws->w = w;
        ws->h = h;
        return FALSE;
    }
    return TRUE;
}

// Nothing is probed or hooked until the game is really running: its window is up, Direct3D 12 and DXGI are loaded, and a
// settle time has passed. Returns false (nothing touched) if that does not happen in time.
static bool wait_for_game() {
    if (env_int(L"TURBO_GUI_SKIP_WAIT", 0) == 1) {
        log("TURBO_GUI_SKIP_WAIT is set: not waiting for the game window (test mode)");
        return true;
    }
    const int wait_ms = env_int(L"TURBO_GUI_WAIT_MS", 180000);
    const int settle_ms = env_int(L"TURBO_GUI_SETTLE_MS", 5000);
    log("waiting for the game window and Direct3D 12 (up to %d s); nothing is hooked yet", wait_ms / 1000);
    const DWORD t0 = GetTickCount();
    bool window_logged = false;
    for (;;) {
        WindowSearch ws{GetCurrentProcessId(), 0, 0};
        EnumWindows(enum_windows, reinterpret_cast<LPARAM>(&ws));
        bool has_window = ws.w > 0;
        bool has_d3d = GetModuleHandleW(L"d3d12.dll") != nullptr && GetModuleHandleW(L"dxgi.dll") != nullptr;
        if (has_window && has_d3d) {
            log("game window found (%dx%d), Direct3D 12 loaded; settling for %d ms", ws.w, ws.h, settle_ms);
            Sleep(static_cast<DWORD>(settle_ms));
            return true;
        }
        if (has_window && !window_logged) {
            window_logged = true;
            log("game window found (%dx%d); waiting for Direct3D 12", ws.w, ws.h);
        }
        if (GetTickCount() - t0 > static_cast<DWORD>(wait_ms)) {
            log("no game window with Direct3D 12 appeared within %d s; not starting", wait_ms / 1000);
            return false;
        }
        Sleep(500);
    }
}

// ---------------------------------------------------------------- waiting for Live Editor (Turbo.dll loaded at game launch)
// turbo_output\turbo_gui_load.json is written by Turbo's Lua side right before it loads Turbo.dll: {"mode":"launch"} from
// lua\autorun while the game starts, {"mode":"now"} from turbo_gui_load.lua or a career-mode event (the game is running).
static bool loaded_now_by_lua() {
    std::error_code ec;
    fs::path p = g_root / "turbo_output" / "turbo_gui_load.json";
    auto t = fs::last_write_time(p, ec);
    if (ec) return false;
    auto age = fs::file_time_type::clock::now() - t;
    if (age > std::chrono::minutes(2) || age < -std::chrono::seconds(5)) return false;
    std::string text;
    if (FILE* f = _wfopen(p.wstring().c_str(), L"rb")) {
        char buf[512];
        size_t n = std::fread(buf, 1, sizeof(buf), f);
        text.assign(buf, n);
        std::fclose(f);
    }
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    return j.is_object() && j.contains("mode") && j["mode"].is_string() && j["mode"].get<std::string>() == "now";
}

// Up to the last 4 MB of a file Live Editor may still be writing (opened with full sharing)
static std::string read_shared_tail(const fs::path& p) {
    HANDLE h = CreateFileW(p.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    LARGE_INTEGER size{};
    std::string out;
    if (GetFileSizeEx(h, &size)) {
        const LONGLONG cap = 4LL * 1024 * 1024;
        LONGLONG start = size.QuadPart > cap ? size.QuadPart - cap : 0;
        LARGE_INTEGER pos{};
        pos.QuadPart = start;
        if (SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
            out.resize(static_cast<size_t>(size.QuadPart - start));
            DWORD got = 0;
            if (!out.empty() && (!ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &got, nullptr))) got = 0;
            out.resize(got);
        }
    }
    CloseHandle(h);
    return out;
}

// State of this game session in Live Editor's log files (<LE>\Logs\live_editor_<date>.log, newest first)
static turbo::LeLogState live_editor_state() {
    HMODULE le = GetModuleHandleW(L"FCLiveEditor.DLL");
    if (!le) return turbo::LeLogState::NoSession;
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> logs;
    const auto now = fs::file_time_type::clock::now();
    for (fs::directory_iterator it(g_root / "Logs", ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        std::string name = p.filename().string();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name.rfind("live_editor_", 0) != 0 || name.rfind("live_editor_launcher", 0) == 0) continue;
        if (p.extension() != ".log" && p.extension() != ".LOG") continue;
        std::error_code tec;
        auto t = fs::last_write_time(p, tec);
        if (tec || now - t > std::chrono::hours(72)) continue;
        logs.emplace_back(t, p);
    }
    std::sort(logs.begin(), logs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    const uint64_t base = reinterpret_cast<uint64_t>(le);
    for (const auto& l : logs) {
        turbo::LeLogState st = turbo::le_log_state(read_shared_tail(l.second), base);
        if (st != turbo::LeLogState::NoSession) return st;
    }
    return turbo::LeLogState::NoSession;
}

static bool bridge_state_newer_than(fs::file_time_type t0) {
    std::error_code ec;
    auto t = fs::last_write_time(g_root / "turbo_output" / "bridge_state.json", ec);
    return !ec && t > t0;
}

// Turbo.dll loaded by lua\autorun while the game is starting: touch nothing (no window search, no Direct3D) until Live Editor
// reports that it has finished setting up the game (its own Direct3D 12 hooks are in place and the main menu is up), or
// until Turbo's Lua side runs in game. Loaded from turbo_gui_load.lua or a career event, the game is already running.
static void wait_for_live_editor() {
    if (env_int(L"TURBO_GUI_SKIP_WAIT", 0) == 1) return;
    if (loaded_now_by_lua()) {
        log("loaded by Turbo's Lua side while the game is running");
        return;
    }
    const auto t0 = fs::file_time_type::clock::now();
    const DWORD tick0 = GetTickCount();
    log("loaded while the game is starting: nothing is touched until Live Editor reports \"Initial setup done\" in "
        "Logs\\live_editor_<date>.log, or Turbo's Lua side runs in game");
    bool noted = false;
    for (;;) {
        turbo::LeLogState st = live_editor_state();
        if (st == turbo::LeLogState::Done) {
            log("Live Editor has finished setting up the game");
            return;
        }
        if (bridge_state_newer_than(t0)) {
            log("Turbo's Lua side ran in game (bridge_state.json updated)");
            return;
        }
        if (!noted && GetTickCount() - tick0 > 120000) {
            noted = true;
            log(st == turbo::LeLogState::NoSession
                    ? "Live Editor's log for this game session is not in Logs (log level above INFO?): the Turbo GUI starts "
                      "when you enter a career or run turbo_gui_load.lua"
                    : "Live Editor has not reported \"Initial setup done\" yet: still waiting (entering a career or running "
                      "turbo_gui_load.lua also starts the Turbo GUI)");
        }
        Sleep(1000);
    }
}

static DWORD WINAPI init_thread(LPVOID) {
    g_load_time = fs::file_time_type::clock::now();
    log("Turbo GUI loading in process %lu", static_cast<unsigned long>(GetCurrentProcessId()));
    std::error_code ec;
    if (fs::exists(disable_path(), ec) || env_int(L"TURBO_GUI_DISABLE", 0) == 1) {
        log("kill switch present (turbo_output\\turbo_gui_disable.txt): Turbo GUI stays off");
        return 0;
    }
    if (!GetModuleHandleW(L"FCLiveEditor.DLL")) {
        if (!lua_side_just_ran()) {
            log("FCLiveEditor.DLL is not loaded in this process; Turbo GUI only runs together with Live Editor. Not starting.");
            return 0;
        }
        log("FCLiveEditor.DLL is not in the module list, but Turbo's Lua side just ran in Live Editor: starting.");
    }
    if (fs::exists(guard_path(), ec)) {
        std::string why;
        if (FILE* f = _wfopen(guard_path().wstring().c_str(), L"r")) {
            char buf[256] = {0};
            if (std::fgets(buf, sizeof(buf), f)) why = buf;
            std::fclose(f);
        }
        while (!why.empty() && (why.back() == '\n' || why.back() == '\r')) why.pop_back();
        if (why.rfind("RETRY: ", 0) == 0) {
            log("the last two Turbo GUI starts did not finish (%s): the game may have crashed. Turbo GUI stays off. "
                "Delete turbo_output\\turbo_gui_start.flag to try again.", why.c_str());
            return 0;
        }
        g_retry_mode = true;
        log("the previous Turbo GUI start did not finish (%s): the game may have crashed or been closed. Trying once more; "
            "if this start does not finish either, Turbo GUI stays off.", why.empty() ? "no details" : why.c_str());
    }
    wait_for_live_editor();
    if (fs::exists(disable_path(), ec)) {
        log("kill switch present (turbo_output\\turbo_gui_disable.txt): Turbo GUI stays off");
        return 0;
    }
    if (!wait_for_game()) {
        log("overlay not started");
        return 0;
    }
    if (env_int(L"TURBO_GUI_TEST_HOLD_GUARD", 0) == 1) {  // test mode: lets the smoke test check how the flag behaves at exit
        guard_hold("test hold");
        log("test mode: crash guard held, overlay not started");
        return 0;
    }
    // start_overlay holds the crash guard itself around the work it does inside the game
    if (!start_overlay(g_self)) {
        guard_release();  // a clean refusal installed nothing: not a crash
        if (g_retry_mode) DeleteFileW(guard_path().wstring().c_str());  // the retry ended cleanly: the old flag is moot
        log("overlay not started");
    }
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

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_DETACH && reserved != nullptr && host::g_flag_w[0]) {
        // The process is exiting normally: a clean exit is not a crash (a killed or crashed process never gets here)
        DeleteFileW(host::g_flag_w);
    }
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
