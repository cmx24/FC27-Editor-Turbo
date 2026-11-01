// FC 27 LE Turbo - turbo_settings_apply.exe: applies the game settings preset Turbo's Game settings tab staged, with the game CLOSED
// (core/game_settings.h: backup with md5s, temp file, CRC re-check, one Settings file, Undo). A console program next to Turbo.dll in
// <Live Editor>\turbo\. It never touches CmMgrC* (careers), Squads* or ProfileOptions*.
//
// usage: turbo_settings_apply.exe --status | --apply [--dry-run] | --undo [--force] [--dry-run] [--evidence <dir>]
//                                 | --write-markers <out_dir> [--set blocks|run] | --install-markers <out_dir> [--dry-run]
//        --undo REFUSES while a game-written Settings* file (not the one Turbo wrote) exists that is not yet copied into the evidence dir;
//        with --evidence <dir> it copies that file there first (md5 verified), also with --force (2.10: --undo --force once deleted a
//        game-written Settings file without a copy).
//        options: --le-root <Live Editor folder> (default: this exe's folder's parent), --settings-dir <folder> (default:
//                 %LOCALAPPDATA%\EA SPORTS FC 27\settings), --map <slider map json> (default: <le-root>\turbo\settings\slider_map_6AC07E31.json)
// Exit code 0 = done (or nothing to do), 1 = refused / failed, 2 = bad arguments. Every run is appended to turbo_output\settings_apply.log.
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/game_settings.h"
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;

// ------------------------------------------------------------------------------------------------ the undo evidence guard (pure: no Windows call)
namespace turbo {
namespace settings_tool {

using namespace turbo::gsettings;

struct GuardResult {
    bool ok = true;
    std::string message;             // why refused ("" = fine)
    std::vector<std::string> lines;  // what was copied / would be copied
};

// Before --undo (with or without --force) replaces or removes a Settings* file: every Settings* file in the settings folder that is NOT the
// one the newest backup says Turbo wrote (name + md5_after) and NOT the backed-up original (md5_before) is game-written evidence. It must
// exist in `evidence_dir` (same md5) first. No evidence dir given -> refused; given -> the file is copied (md5 read back).
// dry_run copies nothing and reports. Nothing to protect (no backup, or only Turbo's file) -> ok.
inline GuardResult undo_evidence_guard(const fs::path& settings_dir, const fs::path& out_dir, const fs::path& evidence_dir, bool dry_run) {
    GuardResult g;
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (const auto& e : fs::directory_iterator(out_dir / backup_dir(), ec))
        if (e.is_directory(ec) && fs::exists(e.path() / "backup.json", ec)) dirs.push_back(e.path());
    if (dirs.empty()) return g;  // undo_last says "Nothing to undo"
    std::sort(dirs.begin(), dirs.end());
    std::vector<uint8_t> rb;
    nlohmann::json rec;
    if (read_bytes(dirs.back() / "backup.json", rb, 1u << 20)) rec = nlohmann::json::parse(std::string(rb.begin(), rb.end()), nullptr, false);
    auto str = [&](const char* k) { return rec.is_object() && rec.contains(k) && rec[k].is_string() ? rec[k].get<std::string>() : std::string(); };
    const std::string wrote_name = str("file"), md5_after = str("md5_after"), md5_before = str("md5_before");
    for (const auto& e : fs::directory_iterator(settings_dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string n = e.path().filename().u8string();
        std::string low = n;
        for (char& c : low) c = char(std::tolower((unsigned char)c));
        if (low.rfind("settings", 0) != 0) continue;
        std::vector<uint8_t> b;
        if (!read_bytes(e.path(), b)) {
            g.ok = false;
            g.message = "Refused: cannot read " + n + " to check it against the evidence folder";
            return g;
        }
        const std::string md5 = md5_hex(b);
        if (n == wrote_name && !md5_after.empty() && md5 == md5_after) continue;  // still the file Turbo wrote
        if (!md5_before.empty() && md5 == md5_before) continue;                    // the original itself
        // game-written: needs a verified copy in the evidence dir
        if (evidence_dir.empty()) {
            g.ok = false;
            g.message = "Refused: " + n + " (md5 " + md5 + ") was written by the game after Turbo's change and is not copied anywhere. --undo would replace it. "
                        "Run again with --evidence <folder> to copy it first (this holds for --force too).";
            return g;
        }
        std::vector<uint8_t> have;
        const fs::path same = evidence_dir / fs::u8path(n);
        bool covered = read_bytes(same, have) && md5_hex(have) == md5;
        if (!covered) {
            for (const auto& x : fs::directory_iterator(evidence_dir, ec)) {  // a copy under another name counts
                if (!x.is_regular_file(ec) || x.file_size(ec) != b.size()) continue;
                if (read_bytes(x.path(), have) && md5_hex(have) == md5) {
                    covered = true;
                    break;
                }
            }
        }
        if (covered) {
            g.lines.push_back(n + ": already in the evidence folder (md5 " + md5 + ")");
            continue;
        }
        if (dry_run) {
            g.lines.push_back(n + ": would be copied to " + evidence_dir.u8string());
            continue;
        }
        fs::path to = same;
        if (fs::exists(to, ec)) to = evidence_dir / fs::u8path(n + "." + md5.substr(0, 8));
        fs::create_directories(evidence_dir, ec);
        {
            std::ofstream f(to, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
        }
        std::vector<uint8_t> back;
        if (!read_bytes(to, back) || md5_hex(back) != md5) {
            g.ok = false;
            g.message = "Refused: the evidence copy of " + n + " failed (" + to.u8string() + "); nothing was changed";
            return g;
        }
        g.lines.push_back(n + ": copied to " + to.u8string() + " (md5 " + md5 + ")");
    }
    return g;
}

}  // namespace settings_tool
}  // namespace turbo

#ifndef TURBO_SETTINGS_APPLY_GUARD_ONLY  // the native tests include this file for the guard above
#include <windows.h>
#include <tlhelp32.h>

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
        "  --undo [--force] [--dry-run] [--evidence <dir>]  put the newest backup back (turbo_output\\settings_backup); REFUSED while a\n"
        "                                   game-written Settings* file is not copied into the evidence dir (--evidence copies it first, also with --force)\n"
        "  --write-markers <dir> [--set blocks|run]   write a marker COPY of the settings file + legend into <dir> (read-only on the game's folder)\n"
        "  --install-markers <dir> [--dry-run]        patch the settings file with that copy's markers (backup; --undo puts it back)\n"
        "  options: --le-root <folder>  --settings-dir <folder>  --map <slider map json>\n");
}

int wmain(int argc, wchar_t** argv) {
    std::vector<std::wstring> a(argv + 1, argv + argc);
    std::string cmd, set = "blocks";
    fs::path arg_dir, le_root, settings_dir, map, evidence;
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
        else if (x == L"--evidence") { if (!next(evidence)) { usage(); return 2; } }
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
        for (const std::string& l : status_lines(s, m)) std::printf("%s\n", l.c_str());  // 3.0.1: the Custom block too (GUI30H)
        int verified = 0, file_mapped = 0;
        for (const Entry& e : m.entries) {
            verified += e.writable() ? 1 : 0;
            file_mapped += e.file_mapped() ? 1 : 0;
        }
        std::printf("slider map: %zu entries, %d verified in game, %d file-mapped (written here, game closed)\n", m.entries.size(), verified, file_mapped);
        return 0;
    }
    if (cmd == "apply") o = apply_pending(env, m);
    else if (cmd == "undo") {
        const turbo::settings_tool::GuardResult gr = turbo::settings_tool::undo_evidence_guard(settings_dir, env.out_dir, evidence, dry);
        if (!gr.ok) {
            o.lines = gr.lines;
            o.message = gr.message;
        } else {
            o = undo_last(env, force);
            o.lines.insert(o.lines.begin(), gr.lines.begin(), gr.lines.end());
        }
    }
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
#endif  // TURBO_SETTINGS_APPLY_GUARD_ONLY
