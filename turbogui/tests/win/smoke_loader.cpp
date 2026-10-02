// FC 27 LE Turbo GUI - Windows smoke test for Turbo.dll (runs under Windows or Wine, no game needed).
//
//   smoke_loader.exe <LE folder> refuse   Turbo.dll alone: must refuse to start (no FCLiveEditor.DLL)
//   smoke_loader.exe <LE folder> start    stub FCLiveEditor.DLL first, then Turbo.dll: the GUI core must
//                                         start, publish its mailbox in bridge_dll.json and initialise it
//   smoke_loader.exe <LE folder> lua      no FCLiveEditor.DLL, but a fresh bridge_state.json (as written by
//                                         Turbo's Lua side right before it loads the DLL): must start
//
// <LE folder> holds FCLiveEditor.DLL (a stub built from stub_le.cpp, NOT Live Editor), turbo_config.json
// and turbo\Turbo.dll. Exit code 0 = pass.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

static std::string read_file(const std::wstring& p) {
    std::ifstream f(p.c_str(), std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool wait_for(const std::wstring& path, const char* needle, int ms) {
    for (int t = 0; t < ms; t += 100) {
        if (read_file(path).find(needle) != std::string::npos) return true;
        Sleep(100);
    }
    return false;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::printf("usage: smoke_loader <LE folder> refuse|start\n");
        return 2;
    }
    std::wstring le = argv[1];
    std::wstring mode = argv[2];
    std::wstring log = le + L"\\turbo_output\\turbo_gui.log";
    std::wstring dll = le + L"\\turbo\\Turbo.dll";
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++fails;
    };

    if (mode == L"refuse") {
        HMODULE h = LoadLibraryW(dll.c_str());
        check(h != nullptr, "Turbo.dll loads (all imports resolve)");
        check(h && GetProcAddress(h, "luaopen_turbo_gui") != nullptr, "exports luaopen_turbo_gui");
        check(wait_for(log, "FCLiveEditor.DLL is not loaded in this process", 5000), "refuses to start without Live Editor");
        check(read_file(log).find("hooks installed") == std::string::npos, "no hooks installed");
    } else {
        if (mode == L"lua") {
            // what Turbo's Lua side does right before package.loadlib: write a fresh bridge_state.json
            std::ofstream((le + L"\\turbo_output\\bridge_state.json").c_str()) << "{\"session\":\"smoke\",\"seq\":1}";
        } else {
            HMODULE stub = LoadLibraryW((le + L"\\FCLiveEditor.DLL").c_str());
            check(stub != nullptr, "stub FCLiveEditor.DLL loads");
        }
        HMODULE h = LoadLibraryW(dll.c_str());
        check(h != nullptr, "Turbo.dll loads");
        check(wait_for(le + L"\\turbo_output\\bridge_dll.json", "mailbox", 5000), "bridge_dll.json published");
        std::string j = read_file(le + L"\\turbo_output\\bridge_dll.json");
        size_t p = j.find("0x");
        unsigned long long addr = p == std::string::npos ? 0 : std::strtoull(j.c_str() + p + 2, nullptr, 16);
        check(addr != 0, "mailbox address parsed");
        if (addr) {
            MEMORY_BASIC_INFORMATION mbi{};
            bool committed = VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT;
            check(committed, "mailbox memory committed in this process");
            if (committed) {
                unsigned magic = 0, ver = 0;
                std::memcpy(&magic, reinterpret_cast<void*>(addr), 4);
                std::memcpy(&ver, reinterpret_cast<void*>(addr + 4), 4);
                check(magic == 0x4F425254u, "mailbox magic TRBO");
                check(ver == 1u, "mailbox version 1");
            }
        }
        check(j.find("\"gui_version\": \"0.2.0\"") != std::string::npos, "gui_version 0.2.0");
        // The D3D12 probe either installs the hooks (real GPU) or reports why it cannot (no D3D12, or no
        // display as in headless Wine: "probe window failed" / "could not create the probe swap chain"),
        // after which the overlay stays off ("overlay not started") and nothing is hooked.
        bool hooked = wait_for(log, "hooks installed", 8000);
        bool off = !hooked && wait_for(log, "overlay not started", 2000);
        check(hooked || off, hooked ? "D3D12 hooks installed" : "no D3D12/display here: overlay reports it and stays off");
        if (off) check(read_file(log).find("hooks installed") == std::string::npos, "nothing hooked when the overlay stays off");
    }
    std::printf("%s\n", fails == 0 ? "SMOKE PASS" : "SMOKE FAIL");
    std::fflush(stdout);
    // Exit without unloading: Turbo.dll keeps running threads by design (it is never unloaded in game)
    TerminateProcess(GetCurrentProcess(), fails == 0 ? 0 : 1);
    return fails == 0 ? 0 : 1;
}
