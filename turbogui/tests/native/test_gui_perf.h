// Native tests (2.16 gui/perf): what one Turbo frame costs, page by page, shown and hidden. Cassio 2026-10-17: "the mouse
// pointer lags like crazy. and the status screen lags so bad it seems to freeze". Turbo builds its frame inside the game's
// Present (overlay_dx12.cpp render_frame), so every millisecond and every file-system call a page makes is added to the
// game's frame, and while Turbo is shown the pointer is drawn by Dear ImGui at that frame rate.
// The harness opens every page of the shell (Advanced view: all of them), the Settings page with its Technical details and
// its log open, and the hidden overlay; it times each frame and, on Windows, counts the file-system calls THIS thread makes
// (ntdll NtCreateFile / NtOpenFile / NtQueryAttributesFile / NtQueryFullAttributesFile / NtQueryDirectoryFile(Ex) /
// NtReadFile / NtWriteFile, hooked with MinHook for the duration of the case). Periodic polls (the bridge files every
// 0.5 s, kill switches every 2 s, the catalog folder every 5 s) are allowed; per-frame file work is not: the budget is a
// share of frames that touch the file system at all. Included by test_main.cpp; gui_perf_test::ui_cases(app, ui, le).
#pragma once
#include <atomic>
#include <chrono>

#ifdef _WIN32
#include <windows.h>

#include "MinHook.h"
#endif

namespace gui_perf_test {

// ---- file-system call counter (this thread only)
enum FsOp { kNtCreate, kNtOpen, kNtAttr, kNtFullAttr, kNtDir, kNtDirEx, kNtRead, kNtWrite, kFsOps };
static const char* const kFsOpNames[kFsOps] = {"NtCreateFile", "NtOpenFile", "NtQueryAttributesFile", "NtQueryFullAttributesFile",
                                                "NtQueryDirectoryFile", "NtQueryDirectoryFileEx", "NtReadFile", "NtWriteFile"};
static std::atomic<unsigned long> g_tid{0};  // the counted thread (0 = counting off)
static std::atomic<long> g_fs[kFsOps];

static long fs_total() {
    long n = 0;
    for (auto& c : g_fs) n += c.load();
    return n;
}

#ifdef _WIN32
using P = void*;
using Nt2 = LONG(NTAPI*)(P, P);
using Nt6 = LONG(NTAPI*)(P, P, P, P, P, P);
using Nt9 = LONG(NTAPI*)(P, P, P, P, P, P, P, P, P);
using Nt10 = LONG(NTAPI*)(P, P, P, P, P, P, P, P, P, P);
using Nt11 = LONG(NTAPI*)(P, P, P, P, P, P, P, P, P, P, P);
static void* g_orig[kFsOps] = {};
static void* g_target[kFsOps] = {};

static inline void hit(FsOp op) {
    if (GetCurrentThreadId() == g_tid.load(std::memory_order_relaxed)) g_fs[op].fetch_add(1, std::memory_order_relaxed);
}
// x64: every argument is one 8-byte slot, so each detour forwards the exact count as pointers
static LONG NTAPI d_create(P a, P b, P c, P d, P e, P f, P g, P h, P i, P j, P k) {
    hit(kNtCreate);
    return reinterpret_cast<Nt11>(g_orig[kNtCreate])(a, b, c, d, e, f, g, h, i, j, k);
}
static LONG NTAPI d_open(P a, P b, P c, P d, P e, P f) {
    hit(kNtOpen);
    return reinterpret_cast<Nt6>(g_orig[kNtOpen])(a, b, c, d, e, f);
}
static LONG NTAPI d_attr(P a, P b) {
    hit(kNtAttr);
    return reinterpret_cast<Nt2>(g_orig[kNtAttr])(a, b);
}
static LONG NTAPI d_fullattr(P a, P b) {
    hit(kNtFullAttr);
    return reinterpret_cast<Nt2>(g_orig[kNtFullAttr])(a, b);
}
static LONG NTAPI d_dir(P a, P b, P c, P d, P e, P f, P g, P h, P i, P j, P k) {
    hit(kNtDir);
    return reinterpret_cast<Nt11>(g_orig[kNtDir])(a, b, c, d, e, f, g, h, i, j, k);
}
static LONG NTAPI d_direx(P a, P b, P c, P d, P e, P f, P g, P h, P i, P j) {
    hit(kNtDirEx);
    return reinterpret_cast<Nt10>(g_orig[kNtDirEx])(a, b, c, d, e, f, g, h, i, j);
}
static LONG NTAPI d_read(P a, P b, P c, P d, P e, P f, P g, P h, P i) {
    hit(kNtRead);
    return reinterpret_cast<Nt9>(g_orig[kNtRead])(a, b, c, d, e, f, g, h, i);
}
static LONG NTAPI d_write(P a, P b, P c, P d, P e, P f, P g, P h, P i) {
    hit(kNtWrite);
    return reinterpret_cast<Nt9>(g_orig[kNtWrite])(a, b, c, d, e, f, g, h, i);
}

// Hooks the ntdll file functions for this case; false when MinHook refused (the counts stay 0 then)
static bool fs_hooks_on(std::string* why) {
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        *why = std::string("MH_Initialize: ") + MH_StatusToString(st);
        return false;
    }
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    const char* names[kFsOps] = {"NtCreateFile", "NtOpenFile", "NtQueryAttributesFile", "NtQueryFullAttributesFile",
                                 "NtQueryDirectoryFile", "NtQueryDirectoryFileEx", "NtReadFile", "NtWriteFile"};
    void* detours[kFsOps] = {reinterpret_cast<void*>(&d_create), reinterpret_cast<void*>(&d_open), reinterpret_cast<void*>(&d_attr),
                             reinterpret_cast<void*>(&d_fullattr), reinterpret_cast<void*>(&d_dir), reinterpret_cast<void*>(&d_direx),
                             reinterpret_cast<void*>(&d_read), reinterpret_cast<void*>(&d_write)};
    int ok = 0;
    for (int i = 0; i < kFsOps; ++i) {
        void* t = nt ? reinterpret_cast<void*>(GetProcAddress(nt, names[i])) : nullptr;
        if (!t) continue;  // NtQueryDirectoryFileEx: Windows 10 1709 and later
        if (MH_CreateHook(t, detours[i], &g_orig[i]) != MH_OK) continue;
        g_target[i] = t;
        ++ok;
    }
    if (ok == 0 || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        *why = "no ntdll file function could be hooked";
        return false;
    }
    return true;
}
static void fs_hooks_off() {
    for (int i = 0; i < kFsOps; ++i)
        if (g_target[i]) {
            MH_DisableHook(g_target[i]);
            MH_RemoveHook(g_target[i]);
            g_target[i] = nullptr;
        }
}
#endif

// ---- one measured stretch of frames
struct Cost {
    std::string what;
    int frames = 0;
    double ms_avg = 0.0, ms_max = 0.0;
    long fs = 0;             // file-system calls of this thread over the stretch
    int fs_frames = 0;       // frames that made at least one
    long fs_max = 0;         // most in one frame
    long by_op[kFsOps] = {};
    long reports = 0;        // App::hook_report calls (the host built the game-hook report each time: ~25 files)
    long dir_calls() const { return by_op[kNtDir] + by_op[kNtDirEx]; }
    double fs_per_frame() const { return frames ? static_cast<double>(fs) / frames : 0.0; }
    double fs_frame_share() const { return frames ? static_cast<double>(fs_frames) / frames : 0.0; }
};

static long g_reports = 0;  // calls of the case's counting hook_report

static Cost measure(Ui& ui, const std::string& what, int frames) {
    Cost c;
    const long reports_before = g_reports;
    c.what = what;
    c.frames = frames;
    double total = 0.0;
    long op_before[kFsOps];
    for (int i = 0; i < kFsOps; ++i) op_before[i] = g_fs[i].load();
    for (int i = 0; i < frames; ++i) {
        const long before = fs_total();
        const auto t0 = std::chrono::steady_clock::now();
        ui.frame();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const long n = fs_total() - before;
        total += ms;
        c.ms_max = std::max(c.ms_max, ms);
        c.fs += n;
        if (n > 0) ++c.fs_frames;
        c.fs_max = std::max(c.fs_max, n);
    }
    for (int i = 0; i < kFsOps; ++i) c.by_op[i] = g_fs[i].load() - op_before[i];
    c.ms_avg = frames ? total / frames : 0.0;
    c.reports = g_reports - reports_before;
    std::string ops;
    for (int i = 0; i < kFsOps; ++i)
        if (c.by_op[i]) ops += fmt("%s%s %ld", ops.empty() ? "" : ", ", kFsOpNames[i], c.by_op[i]);
    std::printf("    PERF %-44s %6.3f ms/frame (max %7.3f) | fs calls %6.2f/frame, %3d of %d frames, max %ld in one | hook reports %ld%s%s\n",
                what.c_str(), c.ms_avg, c.ms_max, c.fs_per_frame(), c.fs_frames, c.frames, c.fs_max, c.reports, ops.empty() ? "" : " | ",
                ops.c_str());
    return c;
}

// A page the shell shows (Advanced view): area tab + page, a few frames to settle (first draws load files once)
static void open(App& app, Ui& ui, shell::Area a, shell::Page p) {
    app.go(a, p);
    ui.frames(30);
}

// A played career's picture cache: Cassio's holds 24,675 files in 34 folders (1.8 GB, 2026-10-17). Empty .dds files under a
// folder no picture path uses; removed at the end.
constexpr int kSeedDirs = 30, kSeedFiles = 10000;
static fs::path seed_cache(App& app) {
    const fs::path root = app.legacy.cache_dir() / "data" / "ui" / "imgAssets" / "perfseed";
    const std::string r = fs::absolute(root).generic_string(), out = fs::absolute(g_out).generic_string();
    if (r.compare(0, out.size(), out) != 0) return fs::path();  // only ever inside this run's output folder
    for (int d = 0; d < kSeedDirs; ++d) fs::create_directories(root / ("d" + std::to_string(d)));
    for (int i = 0; i < kSeedFiles; ++i) std::ofstream((root / ("d" + std::to_string(i % kSeedDirs)) / ("f" + std::to_string(i) + ".dds")).string());
    return root;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    run_case("gui perf: per-frame cost of every page, shown and hidden (file-system calls on the render thread)", [&] {
        ui.frames(3);
        CHECK(app.connected(), "connected: " + app.db_error);
        const bool adv_was = app.prefs.advanced_mode;
        app.prefs.advanced_mode = true;
        app.visible = true;
        const fs::path seeded = seed_cache(app);
        CHECK(!seeded.empty() && app.legacy.cached_files() >= static_cast<size_t>(kSeedFiles), fmt("picture cache seeded (%zu files)", app.legacy.cached_files()));
        // the host's game-hook report (the Windows host builds it from ~25 kill-switch files and the game calls' locks): counted
        auto saved_report = app.hook_report;
        app.hook_report = [] {
            ++g_reports;
            HookReport r;
            r.build = "6AC07E31-2145C000";
            r.table_source = "built-in";
            r.enabled = true;
            r.signatures.push_back({"game_tick", SigState::Found, 0x140001000ULL, 0x140002000ULL, 1, ""});
            r.calls.push_back("job_offer: ready");
            return r;
        };
        bool counting = false;
#ifdef _WIN32
        std::string why;
        counting = fs_hooks_on(&why);
        CHECK(counting, "ntdll file functions hooked: " + why);
#endif
        for (auto& c : g_fs) c = 0;
#ifdef _WIN32
        g_tid = counting ? static_cast<unsigned long>(GetCurrentThreadId()) : 0;
#endif
        const int kFrames = 120;  // 2 s of the simulated clock: the 0.5 s bridge poll runs 4 times, the 2 s switches once
        std::vector<Cost> all;
        for (int ai = 0; ai < shell::kAreaCount; ++ai) {
            const shell::Area a = static_cast<shell::Area>(ai);
            if (!shell::area_visible(a, true)) continue;
            for (shell::Page p : shell::pages_of(a)) {
                if (!shell::page_visible(p, true)) continue;
                open(app, ui, a, p);
                all.push_back(measure(ui, std::string(shell::area_label(a)) + " > " + shell::page_label(p), kFrames));
            }
        }
        // the Settings page (the old Status tab) with everything open: Technical details (the game-hook report) and the log
        open(app, ui, shell::Area::Settings, shell::Page::Settings);
        const bool log_was = app.prefs.section_open("settings.log", false);
        app.prefs.set_section_open("settings.log", true);
        ui.frames(3);
        if (ui.find("Technical details")) ui.click("Technical details");
        ui.frames(5);
        const Cost settings_open = measure(ui, "Settings, Technical details + Log open", kFrames);
        all.push_back(settings_open);
        if (ui.find("Technical details")) ui.click("Technical details");  // closed again for the cases after this one
        app.prefs.set_section_open("settings.log", log_was);
        // hidden: the game's frames while Turbo is not shown (the App still ticks every frame)
        app.visible = false;
        ui.frames(10);
        const Cost hidden = measure(ui, "hidden (Turbo not shown)", kFrames);
        all.push_back(hidden);
        app.visible = true;
        ui.frames(3);
        g_tid = 0;
#ifdef _WIN32
        if (counting) fs_hooks_off();
#endif
        app.prefs.advanced_mode = adv_was;
        app.hook_report = saved_report;
        std::error_code ec;
        if (!seeded.empty()) fs::remove_all(seeded, ec);
        // the game-hook report: at most once a second however often the page is drawn (it was built twice per frame)
        CHECK(settings_open.reports <= 3, fmt("Settings with Technical details: %ld hook reports in %d frames", settings_open.reports, settings_open.frames));
        if (!counting) return;
        for (const Cost& c : all) {
            // a page touches the file system in at most 1 frame in 10 (the periodic polls), never every frame
            CHECK(c.fs_frame_share() <= 0.10,
                  fmt("%s: file-system calls in %d of %d frames (%.2f per frame); per-frame file work belongs on a worker or in a cache",
                      c.what.c_str(), c.fs_frames, c.frames, c.fs_per_frame()));
            // no directory walk in the frame: only the asset catalog's folder poll (every 5 s, one small folder)
            CHECK(c.dir_calls() <= 8, fmt("%s: %ld directory reads in %d frames (the picture cache walked in the frame?)", c.what.c_str(),
                                          c.dir_calls(), c.frames));
        }
    });
}

}  // namespace gui_perf_test
