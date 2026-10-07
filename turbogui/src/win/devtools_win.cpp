// FC 27 LE Turbo GUI - dev service on Windows (core/devops.h): a background thread that answers
// turbo_output\turbo_dev_request.json in turbo_output\turbo_dev_result.json.
#include <windows.h>
#include <psapi.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "core/devops.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using nlohmann::json;

static std::vector<turbo::DevRegion> dev_regions() {
    std::vector<turbo::DevRegion> out;
    uint64_t addr = turbo::kMinPtr;
    MEMORY_BASIC_INFORMATION m{};
    while (addr < turbo::kMaxPtr && VirtualQuery(reinterpret_cast<LPCVOID>(addr), &m, sizeof(m)) == sizeof(m)) {
        uint64_t base = reinterpret_cast<uint64_t>(m.BaseAddress), size = m.RegionSize;
        if (size == 0) break;
        const DWORD p = m.Protect;
        bool readable = m.State == MEM_COMMIT && !(p & (PAGE_GUARD | PAGE_NOACCESS | PAGE_WRITECOMBINE)) &&
                        (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                              PAGE_EXECUTE_WRITECOPY));
        if (readable) {
            turbo::DevRegion r;
            r.start = base;
            r.end = base + size;
            r.writable = (p & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
            r.kind = m.Type == MEM_IMAGE ? "image" : m.Type == MEM_MAPPED ? "mapped" : "private";
            out.push_back(r);
        }
        if (base + size <= addr) break;
        addr = base + size;
    }
    return out;
}

static json dev_modules() {
    json arr = json::array();
    HMODULE mods[1024];
    DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need)) return arr;
    size_t n = std::min<size_t>(need / sizeof(HMODULE), 1024);
    for (size_t i = 0; i < n; ++i) {
        wchar_t wname[MAX_PATH] = {0};
        GetModuleFileNameW(mods[i], wname, MAX_PATH);
        MODULEINFO mi{};
        K32GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof(mi));
        char name[MAX_PATH * 3] = {0};
        WideCharToMultiByte(CP_UTF8, 0, fs::path(wname).filename().wstring().c_str(), -1, name, sizeof(name), nullptr, nullptr);
        char base[24];
        std::snprintf(base, sizeof(base), "0x%llX", static_cast<unsigned long long>(reinterpret_cast<uint64_t>(mi.lpBaseOfDll)));
        arr.push_back({{"name", name}, {"base", base}, {"size", static_cast<unsigned long long>(mi.SizeOfImage)}});
    }
    return arr;
}

struct EarlyWindow {
    DWORD pid;
    HWND hwnd;
};

static BOOL CALLBACK early_window(HWND hwnd, LPARAM lp) {
    auto* w = reinterpret_cast<EarlyWindow*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != w->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    RECT r{};
    if (!GetWindowRect(hwnd, &r) || r.right - r.left < 400 || r.bottom - r.top < 300) return TRUE;
    w->hwnd = hwnd;
    return FALSE;
}

// The window the overlay draws on; before the overlay has started (dev service started early, turbo_dev_early.txt), this
// process's visible, un-owned top-level window big enough to be the game window (the same rule as dllmain's wait)
static HWND dev_game_window() {
    if (HWND h = game_window()) return h;
    EarlyWindow w{GetCurrentProcessId(), nullptr};
    EnumWindows(early_window, reinterpret_cast<LPARAM>(&w));
    return w.hwnd;
}

// Key press for the game, only while the game window is in front (a key sent while another window has focus would go
// to that window, e.g. the Claude app)
static std::string dev_key(int vk, int ms, bool* sent) {
    *sent = false;
    HWND game = dev_game_window();
    if (!game) return "the game window is not known yet";
    HWND root = GetAncestor(game, GA_ROOT);
    if (GetForegroundWindow() != (root ? root : game)) return "the game window is not in front: key not sent";
    UINT sc = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    INPUT in[1]{};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = static_cast<WORD>(vk);
    in[0].ki.wScan = static_cast<WORD>(sc);
    in[0].ki.dwFlags = sc ? KEYEVENTF_SCANCODE : 0;
    if (SendInput(1, in, sizeof(INPUT)) != 1) return "SendInput failed";
    Sleep(static_cast<DWORD>(ms));
    in[0].ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, in, sizeof(INPUT));
    *sent = true;
    log("dev service: key 0x%02X pressed for the game for %d ms", vk, ms);
    return "";
}

static bool write_atomic(const fs::path& p, const std::string& text) {
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
    return !ec;
}

static DWORD WINAPI dev_thread(LPVOID) {
    fs::path out = le_root() / "turbo_output";
    fs::path req_path = out / "turbo_dev_request.json", res_path = out / "turbo_dev_result.json";
    static ProcessMemory mem;
    turbo::DevEnv env;
    env.mem = &mem;
    env.regions = dev_regions;
    env.key = dev_key;
    env.modules = dev_modules;
    env.sleep = [](int ms) { Sleep(static_cast<DWORD>(ms)); };
    env.clock = []() { return static_cast<double>(GetTickCount64()) / 1000.0; };
    env.out_dir = out;
    turbo::DevService svc(env);
    // A request left by an earlier game session is not run again
    json last_id = nullptr;
    {
        std::ifstream f(req_path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        json j = json::parse(ss.str(), nullptr, false);
        if (!j.is_discarded() && j.contains("id")) last_id = j["id"];
    }
    fs::file_time_type seen{};
    for (;;) {
        Sleep(250);
        std::error_code ec;
        auto t = fs::last_write_time(req_path, ec);
        if (ec || t == seen) continue;
        seen = t;
        std::ifstream f(req_path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        json j = json::parse(ss.str(), nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("id") || j["id"] == last_id) continue;
        last_id = j["id"];
        json r = svc.run(j);
        write_atomic(res_path, r.dump());
        log("dev service: request %s (%s) %s", j["id"].dump().c_str(), j.value("op", std::string("?")).c_str(),
            r.value("ok", false) ? "done" : ("failed: " + r.value("error", std::string())).c_str());
    }
    return 0;
}

// Called by the overlay once it runs, and earlier by dllmain when turbo_output\turbo_dev_early.txt exists: one thread only
void start_devtools() {
    static std::atomic<bool> started{false};
    if (started.load()) return;
    const char* off = std::getenv("TURBO_GUI_NO_DEVTOOLS");
    if (off && off[0] == '1') {
        log("dev service off (TURBO_GUI_NO_DEVTOOLS=1)");
        return;
    }
    std::error_code ec;
    if (fs::exists(le_root() / "turbo_output" / "turbo_dev_disable.txt", ec)) {
        log("dev service off (turbo_output\\turbo_dev_disable.txt)");
        return;
    }
    if (started.exchange(true)) return;
    HANDLE th = CreateThread(nullptr, 0, dev_thread, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
    log("dev service ready (turbo_output\\turbo_dev_request.json)");
}

}  // namespace host
