// FC 27 LE Turbo GUI - Windows smoke test for Turbo.dll (runs under Windows or Wine, no game needed).
//
//   smoke_loader.exe <LE folder> refuse    Turbo.dll alone: must refuse to start (no FCLiveEditor.DLL)
//   smoke_loader.exe <LE folder> start     stub FCLiveEditor.DLL first, then Turbo.dll (game-window wait skipped, as
//                                          there is no game here): the GUI core must start, publish its mailbox in
//                                          bridge_dll.json with a live time stamp and initialise it
//   smoke_loader.exe <LE folder> lua       no FCLiveEditor.DLL, but a fresh bridge_state.json (as written by Turbo's
//                                          Lua side right before it loads the DLL): must start
//   smoke_loader.exe <LE folder> nowindow  stub FCLiveEditor.DLL, no game window (like loading Turbo.dll while the game
//                                          is still launching): the DLL must wait, then give up without hooking
//                                          anything, publishing a mailbox or leaving a crash flag
//   smoke_loader.exe <LE folder> disabled  kill switch file present: the overlay must stay off at once
//   smoke_loader.exe <LE folder> guard     crash flag "RETRY: ..." present (the last two starts did not finish): must
//                                          refuse to hook
//   smoke_loader.exe <LE folder> guardretry plain crash flag present (one start did not finish): must try once more (here:
//                                          no game window, so it gives up without touching anything and keeps the flag)
//   smoke_loader.exe <LE folder> guardexit crash guard held (test mode), then a CLEAN process exit: run_smoke.sh checks
//                                          that the flag is gone afterwards
//   smoke_loader.exe <LE folder> guardkill crash guard held (test mode), then the process is killed: the flag must stay
//   smoke_loader.exe <LE folder> launch    Turbo.dll loaded like lua\autorun does while the game starts (mode "launch"):
//                                          it must touch nothing while Live Editor's log has no "Initial setup done" for
//                                          this process (an earlier session's line and the launcher log do not count), and
//                                          go on as soon as that line is written
//   smoke_loader.exe <LE folder> launchlua same, but no Live Editor log: a bridge_state.json older than the DLL does not
//                                          count, a fresh one (Turbo's Lua side ran in game) starts it
//
// <LE folder> holds FCLiveEditor.DLL (a stub built from stub_le.cpp, NOT Live Editor), turbo_config.json
// and turbo\Turbo.dll. Exit code 0 = pass.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <sstream>
#include <string>

static std::string read_file(const std::wstring& p) {
    std::ifstream f(p.c_str(), std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

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

    const std::wstring flag = le + L"\\turbo_output\\turbo_gui_start.flag";
    const std::wstring mailbox_file = le + L"\\turbo_output\\bridge_dll.json";
    if (mode == L"start" || mode == L"lua") SetEnvironmentVariableW(L"TURBO_GUI_SKIP_WAIT", L"1");
    if (mode == L"nowindow" || mode == L"launch" || mode == L"launchlua" || mode == L"guardretry")
        SetEnvironmentVariableW(L"TURBO_GUI_WAIT_MS", L"1500");
    // What turbo_gui_load.lua writes right before it loads the DLL in game: no wait for Live Editor's setup
    if (mode == L"nowindow") std::ofstream((le + L"\\turbo_output\\turbo_gui_load.json").c_str()) << "{\"mode\":\"now\",\"time\":1}";

    if (mode == L"launch" || mode == L"launchlua") {
        HMODULE stub = LoadLibraryW((le + L"\\FCLiveEditor.DLL").c_str());
        check(stub != nullptr, "stub FCLiveEditor.DLL loads");
        const unsigned long long base = reinterpret_cast<unsigned long long>(stub);
        char ours[160], earlier[160];
        std::snprintf(ours, sizeof(ours), "20:24:09.779458\tINFO\tModule <FCLiveEditor.DLL> 0x%llX-0x%llX\n", base, base + 0xA6D000);
        std::snprintf(earlier, sizeof(earlier), "13:49:58.966124\tINFO\tModule <FCLiveEditor.DLL> 0x%llX-0x%llX\n", base, base + 0xA6D000);
        const std::wstring logs = le + L"\\Logs";
        const std::wstring lelog = logs + L"\\live_editor_02-10-2026.log";
        const std::wstring state = le + L"\\turbo_output\\bridge_state.json";
        if (mode == L"launch") {
            CreateDirectoryW(logs.c_str(), nullptr);
            // an earlier game session with the same module base that finished, another session, then this one (not done)
            std::ofstream(lelog.c_str(), std::ios::binary)
                << "13:49:58.965847\tINFO\tFC 27 Live Editor - v27.1.2\n" << earlier << "13:50:20.000000\tINFO\tInitial setup done\n"
                << "17:38:59.207163\tINFO\tModule <FCLiveEditor.DLL> 0x7FF8B36D0000-0x7FF8B413D000\n"
                << "17:39:20.000000\tINFO\tInitial setup done\n"
                << "20:24:09.779039\tINFO\tFC 27 Live Editor - v27.1.2\n" << ours
                << "20:24:09.993802\tINFO\t[LUA] Execute: C:\\FC 27 Live Editor\\lua\\autorun\\turbo_boot.lua\n";
            // the launcher's log is not Live Editor's in-game log, whatever it contains
            std::ofstream((logs + L"\\live_editor_launcher_02-10-2026.log").c_str(), std::ios::binary)
                << ours << "20:24:42.000000\tINFO\tInitial setup done\n";
        } else {
            std::ofstream(state.c_str()) << "{\"session\":\"previous game\",\"seq\":9}";  // older than the DLL: stale
        }
        std::ofstream((le + L"\\turbo_output\\turbo_gui_load.json").c_str()) << "{\"mode\":\"launch\",\"time\":1}";
        Sleep(50);
        HMODULE h = LoadLibraryW(dll.c_str());
        check(h != nullptr, "Turbo.dll loads");
        check(wait_for(log, "loaded while the game is starting", 5000), "launch mode: waits for Live Editor to finish setting up the game");
        Sleep(3000);
        std::string l = read_file(log);
        check(l.find("finished setting up") == std::string::npos && l.find("Lua side ran in game") == std::string::npos,
              mode == L"launch" ? "an earlier session's 'Initial setup done' and the launcher log do not count"
                                : "a bridge_state.json older than the DLL does not count");
        check(l.find("waiting for the game window") == std::string::npos, "no game-window search yet");
        check(l.find("probing") == std::string::npos && l.find("hooks") == std::string::npos && l.find("TurboProbe") == std::string::npos,
              "nothing probed or hooked yet");
        check(!exists(mailbox_file), "no mailbox published yet");
        check(!GetModuleHandleW(L"d3d12.dll") && !GetModuleHandleW(L"dxgi.dll") && !GetModuleHandleW(L"d3dcompiler_47.dll") &&
                  !GetModuleHandleW(L"dwmapi.dll") && !GetModuleHandleW(L"shell32.dll"),
              "loading Turbo.dll brought no Direct3D 12, DXGI, shader compiler, DWM or shell DLL into the process");
        if (mode == L"launch") {
            std::ofstream(lelog.c_str(), std::ios::binary | std::ios::app) << "20:24:42.331781\tINFO\tInitial setup done\n";
            check(wait_for(log, "Live Editor has finished setting up the game", 5000), "goes on once Live Editor reports Initial setup done");
        } else {
            std::ofstream(state.c_str()) << "{\"session\":\"this game\",\"seq\":1}";
            check(wait_for(log, "Turbo's Lua side ran in game", 5000), "goes on once Turbo's Lua side writes bridge_state.json in game");
        }
        check(wait_for(log, "waiting for the game window", 5000), "then waits for the game window as usual");
        check(wait_for(log, "overlay not started", 8000), "no game window here: gives up without hooking");
        check(read_file(log).find("hooks installed") == std::string::npos, "nothing hooked");
        check(!exists(flag), "no crash flag left behind");
        std::printf("%s\n", fails == 0 ? "SMOKE PASS" : "SMOKE FAIL");
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), fails == 0 ? 0 : 1);
        return 1;
    }

    if (mode == L"guardexit" || mode == L"guardkill") {
        SetEnvironmentVariableW(L"TURBO_GUI_SKIP_WAIT", L"1");
        SetEnvironmentVariableW(L"TURBO_GUI_TEST_HOLD_GUARD", L"1");
        HMODULE stub = LoadLibraryW((le + L"\\FCLiveEditor.DLL").c_str());
        check(stub != nullptr, "stub FCLiveEditor.DLL loads");
        HMODULE h = LoadLibraryW(dll.c_str());
        check(h != nullptr, "Turbo.dll loads");
        bool held = false;
        for (int t = 0; t < 8000 && !held; t += 100) {
            held = exists(flag);
            if (!held) Sleep(100);
        }
        check(held, "crash flag exists while the guard is held");
        std::printf("%s\n", fails == 0 ? "SMOKE PASS" : "SMOKE FAIL");
        std::fflush(stdout);
        if (mode == L"guardexit") ExitProcess(fails == 0 ? 0 : 1);  // clean exit: DLL_PROCESS_DETACH runs
        TerminateProcess(GetCurrentProcess(), fails == 0 ? 0 : 1);  // killed: nothing runs
        return 1;
    }
    if (mode == L"nowindow" || mode == L"disabled" || mode == L"guard" || mode == L"guardretry") {
        HMODULE stub = LoadLibraryW((le + L"\\FCLiveEditor.DLL").c_str());
        check(stub != nullptr, "stub FCLiveEditor.DLL loads");
        if (mode == L"disabled") std::ofstream((le + L"\\turbo_output\\turbo_gui_disable.txt").c_str()) << "off";
        if (mode == L"guard") std::ofstream(flag.c_str()) << "RETRY: hooks being installed (test) (process 1, tick 1)";
        if (mode == L"guardretry") {
            std::ofstream(flag.c_str()) << "hooks being installed (test) (process 1, tick 1)";
            std::ofstream((le + L"\\turbo_output\\turbo_gui_load.json").c_str()) << "{\"mode\":\"now\",\"time\":1}";
        }
        HMODULE h = LoadLibraryW(dll.c_str());
        check(h != nullptr, "Turbo.dll loads");
        const char* needle = mode == L"nowindow" ? "not starting" : mode == L"disabled" ? "kill switch present"
                           : mode == L"guard" ? "last two Turbo GUI starts did not finish" : "Trying once more";
        check(wait_for(log, needle, 10000), mode == L"nowindow" ? "waits for the game window, then gives up" :
                                            mode == L"disabled" ? "kill switch keeps the overlay off" :
                                            mode == L"guard" ? "crash guard refuses after two failed starts" :
                                                               "one failed start: tries once more");
        if (mode == L"nowindow" || mode == L"guardretry")
            check(wait_for(log, "overlay not started", 4000), "reports that the overlay is not started");
        check(read_file(log).find("hooks installed") == std::string::npos, "nothing hooked");
        check(read_file(log).find("probing Direct3D 12") == std::string::npos, "Direct3D 12 never probed");
        check(!exists(mailbox_file), "no mailbox published (the GUI never started)");
        if (mode == L"guard") check(exists(flag), "crash flag kept so the cause stays visible");
        else if (mode == L"guardretry")
            check(exists(flag) && read_file(flag).rfind("RETRY: ", 0) != 0,
                  "nothing was tried in the game: the flag stays as it was (still one retry left)");
        else check(!exists(flag), "no crash flag left behind");
    } else if (mode == L"refuse") {
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
                // readable-memory map for Turbo's Lua side: its address at mailbox +0x18, and it lists the mailbox itself
                unsigned long long map = 0;
                for (int t = 0; t < 50 && !map; ++t) {
                    std::memcpy(&map, reinterpret_cast<void*>(addr + 0x18), 8);
                    if (!map) Sleep(100);
                }
                check(map != 0, "readable-memory map address in the mailbox");
                if (map) {
                    unsigned mm = 0, seq = 0, count = 0;
                    std::memcpy(&mm, reinterpret_cast<void*>(map), 4);
                    std::memcpy(&seq, reinterpret_cast<void*>(map + 8), 4);
                    std::memcpy(&count, reinterpret_cast<void*>(map + 0x0C), 4);
                    check(mm == 0x4D4D5254u && seq % 2 == 0 && count > 0, "map magic TRMM, complete, not empty");
                    bool has_mb = false, has_null = false, sorted = true;
                    unsigned long long prev_end = 0;
                    for (unsigned i = 0; i < count; ++i) {
                        unsigned long long st = 0, en = 0;
                        std::memcpy(&st, reinterpret_cast<void*>(map + 0x20 + i * 16ull), 8);
                        std::memcpy(&en, reinterpret_cast<void*>(map + 0x28 + i * 16ull), 8);
                        if (st <= addr && addr + 0x2020 <= en) has_mb = true;
                        if (st < 0x10000) has_null = true;
                        if (st < prev_end || en <= st) sorted = false;
                        prev_end = en;
                    }
                    check(has_mb, "the map lists the mailbox as readable");
                    check(!has_null && sorted, "sorted, non-overlapping, nothing below 64 KB");
                    MEMORY_BASIC_INFORMATION fm{};
                    VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);  // a no-access region must never be listed
                    check(VirtualQuery(reinterpret_cast<void*>(map), &fm, sizeof(fm)) && fm.State == MEM_COMMIT, "map memory committed");
                }
            }
        }
        check(j.find("\"gui_version\": \"1.1.0\"") != std::string::npos, "gui_version 1.1.0");
        check(j.find("\"updated\":") != std::string::npos, "bridge_dll.json carries a live time stamp");
        // The D3D12 probe either installs the hooks (real GPU) or reports why it cannot (no D3D12, or no
        // display as in headless Wine: "probe window failed" / "could not create the probe swap chain"),
        // after which the overlay stays off ("overlay not started") and nothing is hooked.
        bool hooked = wait_for(log, "hooks installed", 8000);
        bool off = !hooked && wait_for(log, "overlay not started", 2000);
        check(hooked || off, hooked ? "D3D12 hooks installed" : "no D3D12/display here: overlay reports it and stays off");
        if (off) {
            check(read_file(log).find("hooks installed") == std::string::npos, "nothing hooked when the overlay stays off");
            check(!exists(flag), "a clean refusal leaves no crash flag");
        }
    }
    std::printf("%s\n", fails == 0 ? "SMOKE PASS" : "SMOKE FAIL");
    std::fflush(stdout);
    // Exit without unloading: Turbo.dll keeps running threads by design (it is never unloaded in game)
    TerminateProcess(GetCurrentProcess(), fails == 0 ? 0 : 1);
    return fails == 0 ? 0 : 1;
}
