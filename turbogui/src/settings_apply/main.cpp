// FC 27 LE Turbo - turbo_settings_apply.exe: applies the game settings preset Turbo's Game settings tab staged, with the game CLOSED
// (core/game_settings.h: backup with md5s, temp file, CRC re-check, one Settings file, Undo). A console program next to Turbo.dll in
// <Live Editor>\turbo\. It never touches CmMgrC* (careers), Squads* or ProfileOptions*.
//
// usage: turbo_settings_apply.exe --status | --apply [--dry-run] | --undo [--force] [--dry-run]
//                                 | --write-markers <out_dir> [--set blocks|run] | --install-markers <out_dir> [--dry-run]
//        options: --le-root <Live Editor folder> (default: this exe's folder's parent), --settings-dir <folder> (default:
//                 %LOCALAPPDATA%\EA SPORTS FC 27\settings), --map <slider map json> (default: <le-root>\turbo\settings\slider_map_6AC07E31.json)
// Exit code 0 = done (or nothing to do), 1 = refused / failed, 2 = bad arguments. Every run is appended to turbo_output\settings_apply.log.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/game_settings.h"

namespace fs = std::filesystem;
using namespace turbo::gsettings;

static std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

static bool game_running() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return true;  // not known: treated as running (refused)
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"FC27.exe") == 0) found = true;
        } while (!found && Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static bool replace_file(const fs::path& from, const fs::path& to, std::string* err) {
    if (MoveFileExW(from.wstring().c_str(), to.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    if (err) *err = "MoveFileEx failed (error " + std::to_string(GetLastError()) + ")";
    return false;
}

static std::string stamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[40];
    std::snprintf(b, sizeof(b), "%04u%02u%02u-%02u%02u%02u-%03u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return b;
}

static void usage() {
    std::printf(
        "turbo_settings_apply.exe - FC 27 game settings presets (Turbo), with the game CLOSED\n"
        "  --status                         what the settings file holds (read-only)\n"
        "  --apply [--dry-run]              apply turbo_output\\settings_pending.json (staged in Turbo's Game settings tab)\n"
        "  --undo [--force] [--dry-run]     put the newest backup back (turbo_output\\settings_backup)\n"
        "  --write-markers <dir> [--set blocks|run]   write a marker COPY of the settings file + legend into <dir> (read-only on the game's folder)\n"
        "  --install-markers <dir> [--dry-run]        patch the settings file with that copy's markers (backup; --undo puts it back)\n"
        "  options: --le-root <folder>  --settings-dir <folder>  --map <slider map json>\n");
}

int wmain(int argc, wchar_t** argv) {
    std::vector<std::wstring> a(argv + 1, argv + argc);
    std::string cmd, set = "blocks";
    fs::path arg_dir, le_root, settings_dir, map;
    bool dry = false, force = false;
    for (size_t i = 0; i < a.size(); ++i) {
        const std::wstring& x = a[i];
        auto next = [&](fs::path& out) {
            if (i + 1 >= a.size()) return false;
            out = fs::path(a[++i]);
            return true;
        };
        if (x == L"--status" || x == L"--apply" || x == L"--undo") cmd = utf8(x.substr(2));
        else if (x == L"--write-markers" || x == L"--install-markers") {
            cmd = utf8(x.substr(2));
            if (!next(arg_dir)) { usage(); return 2; }
        } else if (x == L"--set" && i + 1 < a.size()) set = utf8(a[++i]);
        else if (x == L"--dry-run") dry = true;
        else if (x == L"--force") force = true;
        else if (x == L"--le-root") { if (!next(le_root)) { usage(); return 2; } }
        else if (x == L"--settings-dir") { if (!next(settings_dir)) { usage(); return 2; } }
        else if (x == L"--map") { if (!next(map)) { usage(); return 2; } }
        else { usage(); return 2; }
    }
    if (cmd.empty()) {
        usage();
        return 2;
    }
    if (le_root.empty()) {
        wchar_t self[MAX_PATH * 2];
        const DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
        le_root = fs::path(std::wstring(self, n)).parent_path().parent_path();  // <Live Editor>\turbo\turbo_settings_apply.exe
    }
    if (settings_dir.empty()) {
        const wchar_t* la = _wgetenv(L"LOCALAPPDATA");
        if (!la || !*la) {
            std::printf("Refused: LOCALAPPDATA is not set (give --settings-dir)\n");
            return 1;
        }
        settings_dir = fs::path(la) / L"EA SPORTS FC 27" / L"settings";
    }
    if (map.empty()) map = le_root / "turbo" / map_dir() / map_file();

    SliderMap m;
    std::string err;
    if (!load_map(map, m, &err)) {
        std::printf("Refused: slider map: %s\n", err.c_str());
        return 1;
    }
    Env env;
    env.settings_dir = settings_dir;
    env.out_dir = le_root / "turbo_output";
    env.game_running = game_running;
    env.replace = replace_file;
    env.stamp = stamp();
    env.dry_run = dry;

    Outcome o;
    if (cmd == "status") {
        const Snapshot s = read_current(settings_dir, m);
        std::printf("settings folder: %s\n", settings_dir.u8string().c_str());
        std::printf("FC 27 running: %s\n", game_running() ? "yes" : "no");
        if (!s.ok) {
            std::printf("not readable: %s\n", s.why.c_str());
            return 1;
        }
        std::printf("%s: %s, layout %s, CRC ok, md5 %s\n", s.name.c_str(), s.profile.c_str(), s.layout.c_str(), s.md5.c_str());
        for (const Region& r : m.regions) {
            std::printf("%-8s", r.id.c_str());
            for (const auto& v : s.slots)
                if (v.first >= r.first && v.first < r.first + r.count) std::printf(" %u", v.second);
            std::printf("\n");
        }
        int verified = 0;
        for (const Entry& e : m.entries) verified += e.writable() ? 1 : 0;
        std::printf("slider map: %zu entries, %d verified in game\n", m.entries.size(), verified);
        return 0;
    }
    if (cmd == "apply") o = apply_pending(env, m);
    else if (cmd == "undo") o = undo_last(env, force);
    else if (cmd == "write-markers") o = write_markers(settings_dir, arg_dir, m, set);
    else if (cmd == "install-markers") o = install_markers(env, m, arg_dir);
    for (const std::string& l : o.lines) std::printf("  %s\n", l.c_str());
    std::printf("%s\n", o.message.c_str());
    std::error_code ec;
    fs::create_directories(env.out_dir, ec);
    std::ofstream log(env.out_dir / "settings_apply.log", std::ios::app | std::ios::binary);
    log << env.stamp << " --" << cmd << (dry ? " --dry-run" : "") << (force ? " --force" : "") << ": " << o.message << "\r\n";
    for (const std::string& l : o.lines) log << "    " << l << "\r\n";
    return o.ok ? 0 : 1;
}
