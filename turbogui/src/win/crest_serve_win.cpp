// FC 27 LE Turbo GUI - crest serve inside FC27.exe (see crest_serve_win.h, core/crest_serve.h)
#include "crest_serve_win.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

#include "core/crest_serve.h"
#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
namespace cs = turbo::crest_serve;

namespace {

constexpr const char* kHook = "crest_serve";

using BeginLoadAsyncFn = void* (*)(void* desc, void* unused, uint64_t size, void* buf, const char* path);
using TryFindFileFn = bool (*)(const char* path, cs::Desc* out);

BeginLoadAsyncFn o_begin = nullptr;
TryFindFileFn g_try_find = nullptr;
uint64_t g_image = 0;
cs::Resolved g_res;         // set before the hook is installed, read-only afterwards
cs::Config g_cfg;           // idem
std::wstring g_mods;        // <LE folder>\mods\legacy (idem)
std::wstring g_off_file;    // <LE folder>\turbo_output\crest_serve_off.txt (idem)
std::atomic<HookHandle> g_handle{nullptr};

// The decoy: chosen once, on the first serve (the legacy file system is up by then), never written again
std::mutex g_decoy_mutex;
std::atomic<int> g_decoy_state{0};  // 0 = not tried, 1 = ready, 2 = failed (no serve possible)
alignas(16) cs::Desc g_decoy;
std::string g_decoy_path;
alignas(64) uint8_t g_scratch[cs::kScratchSize];  // the decoy's bytes land here and are never read

std::atomic<long long> g_serves{0}, g_failures{0}, g_passes{0};
std::mutex g_status_mutex;
std::string g_status = "crest serve: not installed yet";

void set_status(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_status_mutex);
    g_status = s;
}

std::string rva(uint64_t v) {
    char b[32];
    if (g_image && v >= g_image && v - g_image < 0x80000000ull)
        std::snprintf(b, sizeof(b), "FC27+%llX", static_cast<unsigned long long>(v - g_image));
    else
        std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

// <LE folder>\mods\legacy\<rel> ('/' -> '\')
std::wstring mod_file(const std::string& rel) {
    std::wstring w = widen(rel);
    for (wchar_t& c : w)
        if (c == L'/') c = L'\\';
    return g_mods + L"\\" + w;
}

bool file_present(const std::wstring& p, uint64_t* size) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return false;
    if (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    if (size) *size = (static_cast<uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    return true;
}

bool killed() { return GetFileAttributesW(g_off_file.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool read_file(const std::wstring& p, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart >= 0 && sz.QuadPart <= 64ll * 1024 * 1024;
    if (ok) {
        out.resize(static_cast<size_t>(sz.QuadPart));
        size_t done = 0;
        while (ok && done < out.size()) {
            DWORD got = 0;
            const DWORD want = static_cast<DWORD>(std::min<size_t>(out.size() - done, 1u << 20));
            ok = ReadFile(h, out.data() + done, want, &got, nullptr) && got > 0;
            done += got;
        }
    }
    CloseHandle(h);
    return ok;
}

// The decoy, once (under the mutex): the first candidate that is not modded and that TryFindFile finds with a valid descriptor
bool ensure_decoy() {
    const int st = g_decoy_state.load(std::memory_order_acquire);
    if (st != 0) return st == 1;
    std::lock_guard<std::mutex> lock(g_decoy_mutex);
    if (g_decoy_state.load() != 0) return g_decoy_state.load() == 1;
    std::string why;
    cs::Desc d;
    std::memset(&d, 0, sizeof(d));
    const auto& cands = cs::decoy_candidates();
    const int i = cs::choose_decoy(
        cands, [](const std::string& rel) { return file_present(mod_file(rel), nullptr); },
        [](const std::string& rel, cs::Desc* out) { return g_try_find && g_try_find(rel.c_str(), out); }, &d, &why);
    if (i < 0) {
        g_decoy_state.store(2, std::memory_order_release);
        log("crest serve: NO decoy, crests will not be served this session (%s)", why.c_str());
        return false;
    }
    g_decoy = d;
    g_decoy_path = cands[static_cast<size_t>(i)];
    g_decoy_state.store(1, std::memory_order_release);
    log("crest serve: decoy %s (size 0x%llX, start 0x%llX, end 0x%llX, offset 0x%llX)%s%s", g_decoy_path.c_str(),
        static_cast<unsigned long long>(d.size), static_cast<unsigned long long>(d.start), static_cast<unsigned long long>(d.end),
        static_cast<unsigned long long>(d.offset), why.empty() ? "" : "; skipped: ", why.c_str());
    return true;
}

// Throttled log of a crest-prefix call that is left to the original (native crests can be many)
void log_pass(const char* path, const std::string& why, cs::Caller c) {
    const long long n = ++g_passes;
    if (n <= 20 || n % 200 == 0)
        log("crest serve: original for %.200s (%s; caller %s; #%lld)", path, why.c_str(), cs::caller_name(c), n);
}

enum class Plan { Original, Decoy, Null };

// Everything before the call: decide, read the file into buf, make sure the decoy exists. Never calls game code except TryFindFile
// (through ensure_decoy).
Plan prepare(void* buf, uint64_t size, const char* path, uint64_t ret) {
    const cs::Caller caller = cs::classify_caller(g_res, ret);
    const std::string rel = cs::safe_relative(path);
    cs::Probe probe;
    if (!rel.empty()) {
        probe.killed = killed();
        probe.file_exists = !probe.killed && file_present(mod_file(rel), &probe.file_size);
    }
    std::string why;
    if (cs::decide(g_cfg, path, buf, size, probe, &why) != cs::Decision::Serve) {
        if (!why.empty()) log_pass(path, why, caller);
        return Plan::Original;
    }
    auto fail = [&](const std::string& w) {
        const long long n = ++g_failures;
        const bool null_ok = cs::caller_checks_null(caller);
        log("crest serve: FAILED %.200s (%s; caller %s, return %s): %s (#%lld)", path, w.c_str(), cs::caller_name(caller), rva(ret).c_str(),
            null_ok ? "NULL to the caller (it reports a failed load)" : "original call (this caller does not test for NULL)", n);
        return null_ok ? Plan::Null : Plan::Original;
    };
    if (!ensure_decoy()) return fail("no decoy");
    std::string rwhy;
    const std::wstring file = mod_file(rel);
    if (!cs::fill_buffer([&](std::vector<uint8_t>& out) { return read_file(file, out); }, buf, size, &rwhy)) return fail(rwhy);
    return Plan::Decoy;
}

// BeginLoadAsync(Desc*, _, size, buf, path): every async legacy load passes here; the cheap tests come first
__attribute__((noinline)) void* hk_begin_load_async(void* desc, void* unused, uint64_t size, void* buf, const char* path) {
    const uint64_t ret = reinterpret_cast<uint64_t>(__builtin_return_address(0));
    const HookHandle h = g_handle.load(std::memory_order_acquire);
    if (!h || !path || !g_cfg.enabled || !game_hook_live(h) || !cs::matches_prefix(path, g_cfg)) return o_begin(desc, unused, size, buf, path);
    Plan plan = Plan::Original;
    HOOK_BODY(kHook, { plan = prepare(buf, size, path, ret); });
    if (plan == Plan::Null) return nullptr;
    if (plan != Plan::Decoy) return o_begin(desc, unused, size, buf, path);
    void* req = o_begin(&g_decoy, unused, g_decoy.size, g_scratch, g_decoy_path.c_str());
    const long long n = ++g_serves;
    log("crest serve: served %.200s (%llu bytes from mods\\legacy; caller %s, return %s; decoy request %p) #%lld", path,
        static_cast<unsigned long long>(size), cs::caller_name(cs::classify_caller(g_res, ret)), rva(ret).c_str(), req, n);
    return req;
}

std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

std::string crest_serve_status() {
    std::lock_guard<std::mutex> lock(g_status_mutex);
    if (!g_handle.load()) return g_status;
    char b[160];
    std::snprintf(b, sizeof(b), "; %lld serve(s), %lld failure(s), %lld crest call(s) left to the game", g_serves.load(), g_failures.load(),
                  g_passes.load());
    return g_status + b;
}

void install_crest_serve() {
    auto off = [](const std::string& why) {
        set_status("crest serve: off (" + why + ")");
        log("crest serve: off (%s)", why.c_str());
    };
    try {
        g_off_file = (le_root() / "turbo_output" / "crest_serve_off.txt").wstring();
        g_mods = (le_root() / "mods" / "legacy").wstring();
        if (killed()) return off("turbo_output\\crest_serve_off.txt present");
        if (!game_hooks_allowed()) return off("game hooks are off: " + game_hooks_report().note);
        g_image = game_image_base();
        cs::Resolved r;
        r.begin_load_async = game_signature("cs_begin_load_async");
        r.loader_tryfind = game_signature("cs_loader_tryfind_call");
        r.loader_async = game_signature("cs_loader_async_call");
        r.loader_ret = game_signature("cs_loader_async_ret");
        r.promise_tryfind = game_signature("cs_promise_tryfind_call");
        r.promise_async = game_signature("cs_promise_async_call");
        r.promise_ret = game_signature("cs_promise_async_ret");
        r.capture_async = game_signature("cs_capture_async_call");
        r.capture_ret = game_signature("cs_capture_async_ret");
        const std::string bad = cs::check_resolved(r);
        if (!bad.empty()) return off(bad + " (build " + game_build_key() + "; verified on 6AC07E31 only)");
        uint8_t code[32] = {};
        ProcessMemory mem;
        if (!mem.read(r.begin_load_async, code, sizeof(code)) || !cs::is_begin_load_async(code, sizeof(code)))
            return off("the bytes at BeginLoadAsync " + rva(r.begin_load_async) + " are not the expected function (another hook?)");

        std::string err;
        g_cfg = cs::parse_config(read_text(le_root() / "turbo_config.json"), &err);
        if (!err.empty()) log("crest serve: turbo_config.json: %s", err.c_str());
        g_res = r;
        g_try_find = reinterpret_cast<TryFindFileFn>(r.loader_tryfind);
        if (!install_game_hook(kHook, "cs_begin_load_async", reinterpret_cast<void*>(&hk_begin_load_async), reinterpret_cast<void**>(&o_begin)))
            return off("the hook on BeginLoadAsync " + rva(r.begin_load_async) + " was refused (see the game hooks lines above)");
        g_handle.store(game_hook_handle(kHook), std::memory_order_release);
        set_status(std::string("crest serve: ") + (g_cfg.enabled ? "on" : "installed but OFF in turbo_config.json") + " (" + g_cfg.note + ")");
        log("crest serve: %s (%s); BeginLoadAsync %s, TryFindFile %s, callers ChunkFileLoaderData %s / CallPromise %s / ChunkFileAsyncLoad %s; "
            "mods folder %s; kill switches turbo_output\\crest_serve_off.txt, hook_crest_serve_off.txt",
            g_cfg.enabled ? "on" : "installed, OFF in turbo_config.json", g_cfg.note.c_str(), rva(r.begin_load_async).c_str(),
            rva(r.loader_tryfind).c_str(), rva(r.loader_ret).c_str(), rva(r.promise_ret).c_str(), rva(r.capture_ret).c_str(),
            (le_root() / "mods" / "legacy").string().c_str());
    } catch (const std::exception& e) {
        off(std::string("error while installing: ") + e.what());
    } catch (...) {
        off("unknown error while installing");
    }
}

}  // namespace host
