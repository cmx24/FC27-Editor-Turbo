// FC 27 LE Turbo GUI - attribulator EBX patch at load, Windows host (core/attrib_ebx.h).
//
// start_attrib_ebx_early() runs from DllMain (its own thread; nothing under the loader lock) so the decompress hook is in before the game
// reads its attribulator groups, if Turbo is loaded early enough: the log says when the hook went in, when the table was published and
// when each attrib asset was first decompressed ("attrib block seen <time> <asset>"), which is gate G-T1b's timing answer.
//
// Off unless turbo_output\attrib_preset.json exists (the values to write; mode "observe" only watches). Always off when one of these is
// present: turbo_output\attrib_ebx_off.txt (also honoured at run time: the handler then only counts), game_hooks_off.txt,
// hook_attrib_oodle_off.txt, env TURBO_GUI_NO_GAME_HOOKS=1. The preset's build must be the running FC27.exe's.
//
// Hook: OodleLZ_Decompress of oo2core_9_win64.dll (the game folder's own copy). Since 2.11 ONE detour serves this patch and the 3D kit swap
// (core/oodle_dispatch.h, win/oodle_shared_win.cpp): this file registers a handler in slot Attrib and installs the detour when it is first
// (early, from DllMain's thread, with the checks it always had: module in the game folder, no jump at the entry), or attaches to the one the
// kit swap installed. The detour always calls the original first and only then lets the Runtime overwrite a recognised block; with both
// features on, the kit handler runs first and a block it overwrote is never offered to this one.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>

#include "core/attrib_ebx.h"
#include "core/kit_swap.h"
#include "core/oodle_dispatch.h"
#include "host.h"
#include "oodle_shared_win.h"

namespace host {

namespace fs = std::filesystem;

namespace {

std::atomic<uint64_t> g_errors{0};
std::string g_head;  // the log's status line (written by the attrib thread only)
turbo::oodle::AttribBinding g_binding;  // the handler's context: lives as long as the process

fs::path out_dir() { return le_root() / "turbo_output"; }
bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}
bool env_is(const wchar_t* name, const wchar_t* value) {
    wchar_t buf[16] = {0};
    DWORD n = GetEnvironmentVariableW(name, buf, 16);
    return n > 0 && n < 16 && std::wcscmp(buf, value) == 0;
}
std::string now_text() { return turbo::attrib::clock_text(uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())); }

// "6AC07E31-2145C000": TimeDateStamp and SizeOfImage of FC27.exe (the key game_hooks uses)
std::string exe_build_key() {
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return "";
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return "";
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return "";
    char b[32];
    std::snprintf(b, sizeof(b), "%08X-%08X", static_cast<unsigned>(nt->FileHeader.TimeDateStamp), static_cast<unsigned>(nt->OptionalHeader.SizeOfImage));
    return b;
}

void write_log(turbo::attrib::Runtime* rt) {
    // with the 3D kit swap in the same detour its slot is named too (who installed it, what each side recognised / overwrote / skipped)
    const std::string shared = oodle_shared_stats(turbo::oodle::kSlotKits).registered ? "\n" + oodle_shared_summary() : std::string();
    const std::string text = rt->log_text(g_head + (g_errors.load() ? " | detour errors " + std::to_string(g_errors.load()) : std::string()) + shared);
    std::error_code ec;
    fs::create_directories(out_dir(), ec);
    std::ofstream f(out_dir() / "attrib_ebx_log.txt", std::ios::binary | std::ios::trunc);
    f << text;
}

void stop(const std::string& why) {
    g_head = "off: " + why;
    log("attrib ebx: %s", why.c_str());
}

DWORD WINAPI attrib_thread(LPVOID) {
    using namespace turbo::attrib;
    const fs::path out = out_dir();
    const fs::path preset_file = out / "attrib_preset.json";
    if (!file_exists(preset_file)) return 0;  // nothing to do, nothing logged: the feature is not in use
    if (file_exists(out / "attrib_ebx_off.txt")) return stop("turbo_output\\attrib_ebx_off.txt present: no hook"), 0;
    if (file_exists(out / "game_hooks_off.txt") || file_exists(out / "hook_attrib_oodle_off.txt") || env_is(L"TURBO_GUI_NO_GAME_HOOKS", L"1"))
        return stop("game hooks are switched off (game_hooks_off.txt, hook_attrib_oodle_off.txt or TURBO_GUI_NO_GAME_HOOKS): no hook"), 0;

    Preset preset;
    {
        std::ifstream f(preset_file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::string why;
        if (!parse_preset(text, preset, &why)) return stop(why), 0;
    }
    const std::string build = exe_build_key();
    if (build != preset.build) return stop("the preset is for build " + preset.build + ", the game is " + (build.empty() ? "unknown" : build)), 0;

    const fs::path game = fs::path(std::wstring([] {
                                       wchar_t e[MAX_PATH * 2] = {0};
                                       GetModuleFileNameW(nullptr, e, MAX_PATH * 2);
                                       return std::wstring(e);
                                   }()))
                              .parent_path();

    auto* rt = new Runtime();  // lives as long as the process: the detour reads it
    rt->set_killed(file_exists(out / "attrib_ebx_off.txt"));
    // the handler goes in BEFORE the detour (the trampoline is stored by MinHook before it enables the hook, and the Runtime passes everything
    // until its table exists); a failed install retires the slot
    g_binding.rt = rt;
    g_binding.errors = &g_errors;
    if (!oodle_shared_register(turbo::oodle::kSlotAttrib, turbo::oodle::make_attrib_handler(&g_binding))) return stop("the attrib slot of the shared Oodle detour is taken"), 0;
    std::string how, why_not;
    if (!oodle_shared_install_early(&how, &why_not)) {
        oodle_shared_retire(turbo::oodle::kSlotAttrib);
        return stop(why_not), 0;
    }
    set_active_runtime(rt);
    const std::string hook_time = now_text();
    const bool attached = how.rfind("attached", 0) == 0;
    log("attrib ebx: OodleLZ_Decompress hooked at %s (%s; build %s, mode %s, %zu value(s) in the preset)", hook_time.c_str(), how.c_str(), build.c_str(),
        preset.mode == Mode::Patch ? "patch" : "observe", preset.patches.size());

    // the table: below normal priority, the game is loading at the same time
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    // Turbo's own reads: the original (the shared trampoline), never the detour
    turbo::kitswap::OodleFn oodle = [](const uint8_t* s, size_t n, uint8_t* d, size_t dn) { return oodle_shared_decompress(s, n, d, dn); };
    turbo::kitswap::GameFiles files(game);
    std::string err;
    if (!files.init(&err)) return stop("hook in, but the game's layout.toc cannot be read: " + err + " (nothing recognised)"), 0;
    const std::vector<std::string> names = preset.assets();
    std::vector<std::vector<AssetLoc>> found;
    // the expected blocks' sizes: calls that match them before the table exists are counted (decompressed too early vs never)
    auto expect_sizes = [&]() {
        std::vector<std::pair<uint32_t, uint32_t>> sizes;
        for (const auto& v : found)
            for (const auto& a : v) {
                std::vector<uint8_t> s;
                std::vector<turbo::kitswap::Block> bl;
                if (files.read_loc(a.loc, s) && turbo::kitswap::block_headers(s, bl) && bl.size() == 1) sizes.push_back({bl[0].csize, bl[0].dsize});
            }
        rt->set_expected_sizes(sizes);
    };
    // where the copies are: the preset's known locations (checked), else the cache, else a search of the tocs
    std::string where = "searched in the tocs";
    bool located = false;
    if (known_locations(preset, names, found)) {
        located = true;
        for (size_t i = 0; i < found.size() && located; ++i)
            for (const AssetLoc& a : found[i]) {
                std::vector<uint8_t> s;
                std::string why;
                if (!files.read_loc(a.loc, s) || !verify_asset(s, oodle, names[i], &why)) {
                    log("attrib ebx: the preset's location of %s is stale (%s): searching the tocs", names[i].c_str(), why.empty() ? "unreadable" : why.c_str());
                    located = false;
                    break;
                }
            }
        if (located) where = "the preset's known locations (verified)";
    }
    const std::string sig = tocs_signature(files);
    const fs::path cache = out / "attrib_ebx_locs.json";
    if (!located && load_locs(cache, sig, names, found)) {
        located = true;
        where = "turbo_output\\attrib_ebx_locs.json";
    }
    if (!located) {
        if (!find_assets(files, oodle, names, found, &err)) return stop("hook in, but the attrib assets cannot be found: " + err), 0;
        save_locs(cache, sig, names, found);
    }
    expect_sizes();
    turbo::kitswap::TableBuilder tb;
    std::vector<ItemInfo> items;
    std::vector<std::string> notes;
    for (size_t i = 0; i < names.size(); ++i) {
        if (found[i].empty()) notes.push_back("not in the install: " + names[i]);
        for (const AssetLoc& a : found[i]) {
            std::vector<uint8_t> stream;
            ItemInfo it;
            std::string why;
            if (!files.read_loc(a.loc, stream)) {
                notes.push_back("cannot read " + names[i] + " (" + a.toc + ")");
                continue;
            }
            if (!add_asset(tb, stream, oodle, names[i], a.toc, preset.patches, preset.mode, it, &why)) {
                notes.push_back(names[i] + " (" + a.toc + "): " + why);
                continue;
            }
            items.push_back(it);
        }
    }
    std::vector<std::string> dropped;
    std::set<uint32_t> dropped_items;
    auto table = tb.finish(&dropped, &dropped_items);
    for (auto& it : items)
        if (dropped_items.count(it.table_item)) {
            it.what += " (DROPPED: the same block is wanted with other bytes)";
            it.patch = false;
        }
    int patching = 0;
    for (const auto& it : items) patching += it.patch ? 1 : 0;
    rt->publish(table, items);
    g_head = std::string(attached ? "hook attached " : "hook installed ") + hook_time + " (" + how + ")" + ", build " + build + ", mode " + (preset.mode == Mode::Patch ? "patch" : "observe") + ", assets " +
             std::to_string(names.size()) + ", copies registered " + std::to_string(items.size()) + " (" + std::to_string(patching) + " patched), locations " +
             where + ", table published " + now_text();
    for (const auto& n : notes) g_head += "\nnote: " + n;
    log("attrib ebx: table published at %s: %zu cop(ies), %d patched; decompress calls before it: see attrib_ebx_log.txt", now_text().c_str(), items.size(), patching);
    for (const auto& it : items) log("attrib ebx: %s %s (%s): %s", it.patch ? "patch" : "watch", it.asset.c_str(), it.toc.c_str(), it.what.c_str());
    for (const auto& n : notes) log("attrib ebx: note: %s", n.c_str());

    // the kill switch and the log, every 2 s, for the life of the process
    uint64_t last_calls = ~0ull, last_hits = ~0ull;
    for (;;) {
        rt->set_killed(file_exists(out / "attrib_ebx_off.txt"));
        for (const auto& line : rt->take_seen_lines()) log("%s", line.c_str());
        if (rt->calls() != last_calls || rt->hits() != last_hits) {
            last_calls = rt->calls();
            last_hits = rt->hits();
            write_log(rt);
        }
        Sleep(2000);
    }
}

}  // namespace

void start_attrib_ebx_early() {
    HANDLE th = CreateThread(nullptr, 0, attrib_thread, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
}

bool attrib_ebx_uses_minhook() { return oodle_shared_uses_minhook(); }

}  // namespace host
