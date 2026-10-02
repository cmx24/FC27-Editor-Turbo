// FC 27 LE Turbo - TurboInjector.exe
// Fallback loader for Turbo.dll when Live Editor's Lua engine cannot load it (no package.loadlib).
// Loads Turbo.dll into FC27.exe only when Live Editor's FCLiveEditor.DLL is already inside that
// process, i.e. only in a game session that Live Editor started for offline play.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwchar>
#include <string>

static void pause_exit(int code) {
    std::printf("\nPress Enter to close...");
    std::fflush(stdout);
    std::getchar();
    std::exit(code);
}

static DWORD find_process(const wchar_t* exe) {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exe) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool has_module(DWORD pid, const wchar_t* name, bool* ok) {
    *ok = false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    *ok = true;
    bool found = false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, name) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// Minutes since Turbo's Lua side last wrote <LE>\turbo_output\bridge_state.json; -1 if missing
static double state_age_minutes(const std::wstring& le_root) {
    std::wstring p = le_root + L"\\turbo_output\\bridge_state.json";
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) return -1.0;
    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);
    ULARGE_INTEGER a, b;
    a.LowPart = fa.ftLastWriteTime.dwLowDateTime;
    a.HighPart = fa.ftLastWriteTime.dwHighDateTime;
    b.LowPart = now_ft.dwLowDateTime;
    b.HighPart = now_ft.dwHighDateTime;
    if (b.QuadPart < a.QuadPart) return 0.0;
    return static_cast<double>(b.QuadPart - a.QuadPart) / 600000000.0;  // 100 ns units -> minutes
}

int wmain() {
    std::printf("FC 27 LE Turbo - Turbo GUI loader\n\n");

    wchar_t self[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(sizeof(self) / sizeof(self[0])));
    std::wstring dir(self, n);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring dll = dir + L"\\Turbo.dll";
    std::wstring le_root = dir.substr(0, dir.find_last_of(L"\\/"));
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wprintf(L"Turbo.dll not found next to this program:\n  %ls\n", dll.c_str());
        pause_exit(1);
    }

    std::printf("Waiting for FC27.exe with Live Editor loaded (up to 3 minutes)...\n");
    DWORD pid = 0;
    bool le = false;
    int seen_game = 0;
    for (int i = 0; i < 180 && !le; ++i) {
        pid = find_process(L"FC27.exe");
        if (pid) {
            // Game up for 5 s without a listed Live Editor DLL, but Turbo's Lua side is running: go on
            double age = state_age_minutes(le_root);
            if (++seen_game > 5 && age >= 0.0 && age <= 15.0) break;
            bool ok = false;
            le = has_module(pid, L"FCLiveEditor.DLL", &ok);
            if (!ok) {
                std::printf("Cannot inspect FC27.exe (error %lu). Run this program as administrator.\n", GetLastError());
                pause_exit(2);
            }
            bool dummy = false;
            if (has_module(pid, L"Turbo.dll", &dummy)) {
                std::printf("Turbo GUI is already loaded. Press the show/hide key in game (default F8).\n");
                pause_exit(0);
            }
        }
        if (!le) Sleep(1000);
    }
    if (!pid) {
        std::printf("FC27.exe is not running. Start the game through Live Editor first.\n");
        pause_exit(3);
    }
    if (!le) {
        // Live Editor's DLL may not be listed as a module. Turbo's Lua side running inside Live Editor
        // (fresh bridge_state.json) is accepted instead; Turbo.dll checks the same thing.
        double age = state_age_minutes(le_root);
        if (age < 0.0 || age > 15.0) {
            std::printf("FC27.exe is running, but neither FCLiveEditor.DLL nor Turbo's Lua side was found.\n"
                        "Turbo only loads into a game started through Live Editor. Start the game through\n"
                        "Live Editor (Turbo's Lua side starts with it), then run this program again.\n");
            pause_exit(4);
        }
        std::printf("FCLiveEditor.DLL is not in FC27.exe's module list, but Turbo's Lua side ran %.0f minute(s) ago.\n", age);
    }

    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                  PROCESS_VM_WRITE | PROCESS_VM_READ,
                              FALSE, pid);
    if (!proc) {
        std::printf("Cannot open FC27.exe (error %lu). Run this program as administrator.\n", GetLastError());
        pause_exit(5);
    }
    size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(proc, remote, dll.c_str(), bytes, nullptr)) {
        std::printf("Cannot write into FC27.exe (error %lu).\n", GetLastError());
        CloseHandle(proc);
        pause_exit(6);
    }
    auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, load, remote, 0, nullptr);
    if (!th) {
        std::printf("Cannot start the loader thread (error %lu).\n", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        pause_exit(7);
    }
    WaitForSingleObject(th, 15000);
    DWORD code = 0;
    GetExitCodeThread(th, &code);
    CloseHandle(th);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    if (code == 0) {
        std::printf("LoadLibrary failed inside FC27.exe. See turbo_output\\turbo_gui.log.\n");
        pause_exit(8);
    }
    std::printf("Turbo GUI loaded. Press the show/hide key in game (default F8).\n");
    pause_exit(0);
    return 0;
}
