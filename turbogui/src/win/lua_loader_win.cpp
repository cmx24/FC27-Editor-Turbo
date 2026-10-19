// FC 27 LE Turbo GUI - Lua loader hook, Windows host (see lua_loader_win.h, core/lua_loader.h)
#include "lua_loader_win.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>

#include "MinHook.h"
#include "core/devops.h"
#include "core/faces3d.h"
#include "core/lua_loader.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
namespace ll = turbo::lualoader;

namespace {

std::atomic<bool> g_opted_in{false};
std::atomic<bool> g_uses_mh{false};
std::atomic<bool> g_installed{false};
std::atomic<bool> g_substitute{false};
std::string g_state = "starting";  // written by the loader thread only, before g_installed / g_done
std::atomic<bool> g_done{false};   // g_state is final
std::atomic<bool> g_legacy_refused{false};
std::atomic<bool> g_cache_written{false};
// 2.17: the saved list (players.json) the product build baked at this game start (faces3d_status: the card's marker restore)
std::mutex g_saved_m;
std::vector<int64_t> g_saved_ids;
bool g_saved_known = false;
std::string g_build;
uint64_t g_exe_base = 0, g_exe_size = 0;

// STOCK CAPTURE: the first publishable capture, handed from a game thread to the watcher (which writes the cache file)
std::mutex g_cap_m;
std::shared_ptr<const std::string> g_cap_bytes;
std::string g_cap_md5, g_cap_row, g_cache_error;
// reader_capturable: the verdict for the last reader address checked (one address in practice)
std::mutex g_rv_m;
uint64_t g_rv_addr = 0;
bool g_rv_ok = false;

ll::Engine* g_engine = nullptr;  // never freed: the detours read it for the lifetime of the process
ll::LineQueue g_lines(50000);
ll::LuaLoadFn o_lua_load = nullptr;
ll::Reader o_blob = nullptr;

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
int env_int(const wchar_t* name, int fallback) {
    wchar_t buf[32];
    DWORD n = GetEnvironmentVariableW(name, buf, 32);
    if (n == 0 || n >= 32) return fallback;
    return _wtoi(buf);
}

// ---------------------------------------------------------------- detours (x64: one calling convention)
int hk_lua_load(void* L, ll::Reader reader, void* data, const char* chunkname, const char* mode) {
    const uint64_t caller = reinterpret_cast<uint64_t>(__builtin_return_address(0));
    return g_engine->on_lua_load(o_lua_load, L, reader, data, chunkname, mode, caller);
}
const char* hk_blob(void* L, void* data, size_t* size) { return g_engine->on_blob_read(o_blob, L, data, size); }

// ---------------------------------------------------------------- env for the engine
bool readable_range(const void* p, size_t n) {
    uint64_t a = reinterpret_cast<uint64_t>(p);
    const uint64_t end = a + n;
    if (end < a) return false;
    while (a < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        const DWORD pr = mbi.Protect & 0xFF;
        if ((mbi.Protect & PAGE_GUARD) || pr == PAGE_NOACCESS || pr == PAGE_EXECUTE) return false;
        a = reinterpret_cast<uint64_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

std::string where(uint64_t addr) {
    char b[160];
    if (g_exe_base && addr >= g_exe_base && addr < g_exe_base + g_exe_size) {
        std::snprintf(b, sizeof(b), "FC27.exe+0x%llX", static_cast<unsigned long long>(addr - g_exe_base));
        return b;
    }
    HMODULE m = nullptr;
    if (addr && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(addr)), &m) &&
        m) {
        wchar_t path[MAX_PATH] = {0};
        GetModuleFileNameW(m, path, MAX_PATH);
        const wchar_t* base = path;
        for (const wchar_t* q = path; *q; ++q)
            if (*q == L'\\' || *q == L'/') base = q + 1;
        std::string name;
        for (const wchar_t* q = base; *q && name.size() < 60; ++q) name += *q < 128 ? char(*q) : '?';
        std::snprintf(b, sizeof(b), "%s+0x%llX", name.c_str(), static_cast<unsigned long long>(addr - reinterpret_cast<uint64_t>(m)));
        return b;
    }
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(addr));
    return b;
}

// ---------------------------------------------------------------- the Lua 5.4 stack, for the chunkname fallback only
// Layout read from lua_load itself (kLuaLoadTopCheck, checked live before these are handed to the engine): L->top at +0x10,
// 16-byte slots. L->ci at +0x20 and ci->func at +0 (6AB9813C: 0x1446A0224 `mov rax,[rcx+20h] / mov rcx,[rcx+10h] / sub rcx,[rax]`)
// give a depth that does not move when the stack is reallocated. The tag byte of a TValue is at +8 (collectable bit 0x40);
// a string TString keeps shrlen at +0x0B, lnglen at +0x10 and the bytes at +0x18.
long long lua_stack_depth(void* L) {
    ProcessMemory mem;
    const uint64_t a = reinterpret_cast<uint64_t>(L);
    uint64_t top = 0, ci = 0, func = 0;
    if (!a || !mem.read(a + 0x10, &top, 8) || !mem.read(a + 0x20, &ci, 8) || !ci || !mem.read(ci, &func, 8)) return -1;
    if (top < func || top - func > (1ull << 30) || ((top - func) & 0xF) != 0) return -1;
    return static_cast<long long>(top - func);
}

std::string lua_top_message(void* L) {
    ProcessMemory mem;
    const uint64_t a = reinterpret_cast<uint64_t>(L);
    uint64_t top = 0, ts = 0;
    uint8_t tag = 0;
    if (!a || !mem.read(a + 0x10, &top, 8) || top < 0x10 || !mem.read(top - 0x10 + 8, &tag, 1)) return "(message unreadable)";
    const uint8_t v = tag & 0x3F;
    if (v != 0x04 && v != 0x14) return "(error object is not a string)";
    if (!mem.read(top - 0x10, &ts, 8) || !ts) return "(message unreadable)";
    uint64_t len = 0;
    if (v == 0x04) {
        uint8_t b = 0;
        if (!mem.read(ts + 0x0B, &b, 1)) return "(message unreadable)";
        len = b;
    } else if (!mem.read(ts + 0x10, &len, 8)) {
        return "(message unreadable)";
    }
    char buf[301] = {0};
    const size_t n = len < 300 ? size_t(len) : 300;
    if (n && !mem.read(ts + 0x18, buf, n)) return "(message unreadable)";
    return ll::quote_chunkname(buf, n, 300);
}

bool lua_pop1(void* L) {
    ProcessMemory mem;
    const uint64_t a = reinterpret_cast<uint64_t>(L);
    uint64_t top = 0;
    if (!a || !mem.read(a + 0x10, &top, 8) || top < 0x10) return false;
    // this thread runs lua_load on L right now: nobody else touches L->top
    *reinterpret_cast<volatile uint64_t*>(static_cast<uintptr_t>(a + 0x10)) = top - 0x10;
    return true;
}

// The live code at `reader` equals kOtherReaderExpect (exact bytes): only then does Turbo call that reader itself
bool reader_capturable(uint64_t reader) {
    {
        std::lock_guard<std::mutex> lk(g_rv_m);
        if (reader && reader == g_rv_addr) return g_rv_ok;
    }
    std::vector<uint8_t> eb;
    std::vector<bool> em;
    bool ok = false;
    uint8_t code[64] = {0};
    if (reader && turbo::parse_pattern(ll::kOtherReaderExpect, eb, em) && eb.size() <= sizeof(code) && ProcessMemory().read(reader, code, eb.size()))
        ok = std::memcmp(code, eb.data(), eb.size()) == 0;
    {
        std::lock_guard<std::mutex> lk(g_rv_m);
        g_rv_addr = reader;
        g_rv_ok = ok;
    }
    g_lines.push(std::string("lua_loader: reader ") + where(reader) +
                 (ok ? " has the verified one-shot reader bytes (35): stock capture on" : " is NOT the verified one-shot reader: no capture, pass-through"));
    return ok;
}

void save_capture(const std::string& row, std::shared_ptr<const std::string> bytes, const std::string& md5) {
    std::lock_guard<std::mutex> lk(g_cap_m);
    if (g_cap_bytes) return;
    g_cap_bytes = std::move(bytes);
    g_cap_md5 = md5;
    g_cap_row = row;
}

void write_pending_capture() {
    std::shared_ptr<const std::string> b;
    std::string md5, row;
    {
        std::lock_guard<std::mutex> lk(g_cap_m);
        if (!g_cap_bytes || g_cache_written.load() || !g_cache_error.empty()) return;
        b = g_cap_bytes;
        md5 = g_cap_md5;
        row = g_cap_row;
    }
    const fs::path p = out_dir() / ll::kStockCacheRel;
    std::string err;
    if (ll::write_file_atomic(p.u8string(), *b, &err)) {
        g_cache_written = true;
        log("lua_loader: stock %s cache written: turbo_output\\%s, %llu bytes, md5 %s", row.c_str(), ll::kStockCacheRel,
            static_cast<unsigned long long>(b->size()), md5.c_str());
    } else {
        std::lock_guard<std::mutex> lk(g_cap_m);
        g_cache_error = err;
        log("lua_loader: stock %s cache NOT written: %s", row.c_str(), err.c_str());
    }
}

std::string now_text() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char b[32];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return b;
}

// ---------------------------------------------------------------- the game image
struct Section {
    uint64_t start = 0, end = 0;
};

bool locate_exe(std::vector<Section>& secs, std::string& err) {
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) {
        err = "GetModuleHandle(NULL) failed";
        return false;
    }
    const uint8_t* base = reinterpret_cast<const uint8_t*>(exe);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) {
        err = "bad DOS header";
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        err = "not a 64-bit PE image";
        return false;
    }
    g_exe_base = reinterpret_cast<uint64_t>(base);
    g_exe_size = nt->OptionalHeader.SizeOfImage;
    g_build = turbo::build_key(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections && i < 96; ++i) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) || sec[i].Misc.VirtualSize == 0) continue;
        secs.push_back({g_exe_base + sec[i].VirtualAddress, g_exe_base + sec[i].VirtualAddress + sec[i].Misc.VirtualSize});
    }
    if (secs.empty()) {
        err = "the main module has no executable section";
        return false;
    }
    return true;
}

// Every signature over every executable section, from one copy of each section (unreadable pages read as zeros); a match in
// two sections counts as ambiguous, like game_hooks.cpp's scan_signature.
std::vector<turbo::SigResult> scan_all(const std::vector<turbo::Signature>& sigs, const std::vector<Section>& secs, std::vector<uint8_t>& buf) {
    std::vector<turbo::SigResult> total(sigs.size());
    for (size_t i = 0; i < sigs.size(); ++i) {
        total[i].name = sigs[i].name;
        total[i].state = turbo::SigState::Missing;
        total[i].error = "no match";
    }
    ProcessMemory mem;
    for (const auto& s : secs) {
        const size_t len = size_t(s.end - s.start);
        buf.resize(len);
        for (size_t off = 0; off < len; off += 0x10000) {
            const size_t n = len - off < 0x10000 ? len - off : 0x10000;
            if (!mem.read(s.start + off, buf.data() + off, n)) std::memset(buf.data() + off, 0, n);
        }
        for (size_t i = 0; i < sigs.size(); ++i) {
            if (total[i].state == turbo::SigState::Ambiguous || total[i].state == turbo::SigState::BadPattern) continue;
            turbo::SigResult r = turbo::resolve_signature(sigs[i], buf.data(), len, s.start);
            if (r.state == turbo::SigState::Found) {
                if (total[i].state == turbo::SigState::Found) {
                    total[i].state = turbo::SigState::Ambiguous;
                    total[i].error = "matches in more than one section";
                } else {
                    total[i] = r;
                }
            } else if (r.state == turbo::SigState::Ambiguous || r.state == turbo::SigState::BadPattern) {
                total[i] = r;
            }
        }
    }
    return total;
}

std::string read_file(const fs::path& p, bool& ok) {
    std::ifstream f(p, std::ios::binary);
    ok = bool(f);
    if (!ok) return std::string();
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void set_state(const std::string& s) { g_state = s; }

// The watcher: kill switch (every 250 ms) and the log lines of the detours
void watcher() {
    const fs::path off = out_dir() / "hook_lua_load_off.txt";
    const fs::path all_off = out_dir() / "game_hooks_off.txt";
    const fs::path faces_off = out_dir() / "faces3d_off.txt";
    bool was = g_engine->killed();
    for (;;) {
        const bool k = file_exists(off) || file_exists(all_off) || file_exists(faces_off);
        g_engine->set_killed(k);
        if (k != was) {
            log("lua_loader: kill switch %s: %s", k ? "present" : "removed", k ? "every lua_load call passes through untouched" : "logging again");
            was = k;
        }
        for (const auto& line : g_lines.take()) log("%s", line.c_str());
        write_pending_capture();
        static long long dropped_logged = 0;
        const long long d = g_lines.dropped();
        if (d != dropped_logged) {
            log("lua_loader: %lld log lines dropped (queue full)", d - dropped_logged);
            dropped_logged = d;
        }
        Sleep(250);
    }
}

void run() {
    const fs::path out = out_dir();
    ll::StartSwitches sw;
    sw.lua_on = file_exists(out / "lua_loader_on.txt");
    sw.lua_substitute = file_exists(out / "lua_loader_substitute.txt");
    sw.faces_on = file_exists(out / "faces3d_on.txt");
    sw.faces_off = file_exists(out / "faces3d_off.txt");
    sw.hook_off = file_exists(out / "hook_lua_load_off.txt");
    sw.game_hooks_off = file_exists(out / "game_hooks_off.txt");
    sw.legacy_present = file_exists(le_root() / ll::kLegacyKitRel);
    sw.research_file = file_exists(out / ll::kResearchFileSwitch);
    sw.log_only = file_exists(out / ll::kLogOnlySwitch);
    const ll::StartDecision dec = ll::decide_start(sw);
    if (!dec.opted_in) return;  // (2.16: always on unless refused below)
    g_opted_in = true;
    const bool substitute = dec.substitute;
    log("lua_loader: on (2.16: whenever Turbo is installed): %s (kill switches turbo_output\\hook_lua_load_off.txt, faces3d_off.txt, game_hooks_off.txt)",
        dec.in_memory ? "SUBSTITUTE mode, player_kit.lua v4 built in memory from the game's own file"
        : substitute  ? "SUBSTITUTE mode from the override file (lua_loader_research_file.txt)"
        : dec.capture_mode ? "LOG-ONLY mode (capture mode: the first stock capture is cached)"
                           : "LOG-ONLY mode");
    auto refuse = [](const std::string& why) {
        set_state("off: " + why);
        g_done = true;
        log("lua_loader: not installed: %s", why.c_str());
    };
    if (env_is(L"TURBO_GUI_DISABLE", L"1") || file_exists(out / "turbo_gui_disable.txt")) return refuse("Turbo GUI kill switch (turbo_gui_disable.txt)");
    if (env_is(L"TURBO_GUI_NO_GAME_HOOKS", L"1")) return refuse("TURBO_GUI_NO_GAME_HOOKS=1");
    if (dec.refused) return refuse(dec.why);
    if (dec.legacy_refused) {
        g_legacy_refused = true;
        log("lua_loader: %s", dec.why.c_str());
    }
    const fs::path flag = out / "lua_loader_start.flag";
    if (file_exists(flag))
        return refuse("turbo_output\\lua_loader_start.flag is left from an install that did not finish (the game may have crashed): delete it to try again");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    std::vector<Section> secs;
    std::string err;
    if (!locate_exe(secs, err)) return refuse(err);

    // Override sources (substitute mode only): read once, never freed
    std::vector<ll::Row> rows = ll::default_rows();
    std::vector<ll::Override> overrides(rows.size());
    // product mode: the saved list (players.json) is read here, never on a game thread; the build runs at the boot capture
    auto saved = std::make_shared<turbo::faces3d::Table>();
    auto crest = std::make_shared<turbo::faces3d::CrestList>();  // 2.20 K2: kits\crest2d.json, the shirt-crest list (same build)
    if (dec.in_memory) {
        std::string perr;
        if (!turbo::faces3d::load_players(out.u8string(), *saved, &perr)) {
            log("lua_loader: faces3d\\players.json unreadable (%s): the saved list is empty this start", perr.c_str());
            saved->clear();
        }
        {
            std::string cline;  // read here, with players.json, never on a game thread; unreadable = no list, one line
            *crest = turbo::faces3d::load_crest2d_for_boot(out.u8string(), cline);
            if (!cline.empty()) log("%s", cline.c_str());
        }
        {
            std::lock_guard<std::mutex> lk(g_saved_m);
            g_saved_ids.clear();
            for (const auto& kv : *saved) g_saved_ids.push_back(kv.first);
            g_saved_known = true;
        }
        for (auto& o : overrides) o.error = "built in memory";
    }
    if (substitute && !dec.in_memory) {
        for (size_t i = 0; i < rows.size(); ++i) {
            bool ok = false;
            std::string bytes = read_file(out / rows[i].override_rel, ok);
            ll::Override& o = overrides[i];
            o.error = ok ? ll::override_refusal(bytes) : "file missing";
            if (o.error.empty()) {
                const ll::OverrideHeader h = ll::parse_override_header(bytes);
                if (h.ok)
                    log("lua_loader: row %s override header: build %s (running %s: %s), stock %s, table %s", rows[i].name.c_str(), h.build.c_str(),
                        g_build.c_str(), h.build == g_build ? "ok" : "STALE, stock will be served", h.stock.c_str(), h.table.c_str());
                else
                    log("lua_loader: row %s override header: %s (stock will be served)", rows[i].name.c_str(), h.error.c_str());
                log("lua_loader: row %s live looks: %s", rows[i].name.c_str(),
                    ll::has_live_looks(bytes.data(), bytes.size()) ? "yes (3D hair & beard change without a restart)" : "no (older file)");
                char* keep = new char[bytes.size()];
                std::memcpy(keep, bytes.data(), bytes.size());
                o.data = keep;
                o.size = bytes.size();
                o.md5 = ll::md5_hex(reinterpret_cast<const uint8_t*>(keep), o.size);
                log("lua_loader: row %s (stock 0x%llX %s) -> turbo_output\\%s: 0x%llX bytes, md5 %s (must be validated offline with Lua 5.4 before use)",
                    rows[i].name.c_str(), static_cast<unsigned long long>(rows[i].size), rows[i].md5.c_str(), rows[i].override_rel.c_str(),
                    static_cast<unsigned long long>(o.size), o.md5.c_str());
            } else {
                log("lua_loader: row %s: turbo_output\\%s not used: %s", rows[i].name.c_str(), rows[i].override_rel.c_str(), o.error.c_str());
            }
        }
    }
    for (const auto& r : rows)
        if (!r.chunkname.empty())
            log("lua_loader: row %s: stock capture by chunkname \"%s\" on the verified one-shot reader (chunkname seen on %s; running %s)", r.name.c_str(),
                r.chunkname.c_str(), r.build.c_str(), g_build.c_str());

    const std::vector<turbo::Signature> sigs = ll::signatures();
    const std::vector<turbo::Signature> first(sigs.begin(), sigs.begin() + 1);  // lua_load alone until it shows up
    const int wait_ms = env_int(L"TURBO_LUA_LOADER_WAIT_MS", 60000);
    const DWORD t0 = GetTickCount();
    std::vector<uint8_t> buf;
    ll::Located at;
    std::string last_why;
    bool lua_load_seen = false;
    int attempts = 0;
    auto read = [](uint64_t a, void* o, size_t n) { return ProcessMemory().read(a, o, n); };
    log("lua_loader: scanning build %s for lua_load every 250 ms for up to %d s", g_build.c_str(), wait_ms / 1000);
    for (;;) {
        ++attempts;
        std::vector<turbo::SigResult> r1 = scan_all(first, secs, buf);
        if (r1[0].state == turbo::SigState::Found) {
            lua_load_seen = true;
            std::vector<turbo::SigResult> rs = scan_all(sigs, secs, buf);
            at = ll::decide_install(rs, read);
            if (at.ok) {
                for (const auto& r : rs)
                    log("lua_loader: signature %s: %s%s", r.name.c_str(), turbo::sig_state_name(r.state),
                        r.state == turbo::SigState::Found ? (" -> " + where(r.address)).c_str() : (" (" + r.error + ")").c_str());
                break;
            }
            if (at.why != last_why) {
                log("lua_loader: lua_load found but not installable yet: %s", at.why.c_str());
                last_why = at.why;
            }
        } else if (r1[0].state == turbo::SigState::Ambiguous) {
            return refuse("lua_load signature matches more than once on build " + g_build);
        }
        buf.clear();
        buf.shrink_to_fit();  // ~the code section: not kept while waiting
        if (GetTickCount() - t0 > DWORD(wait_ms)) {
            if (!lua_load_seen) {
                log("lua_loader: signature not found (build %s) after %d attempts in %lu ms: nothing installed", g_build.c_str(), attempts,
                    static_cast<unsigned long>(GetTickCount() - t0));
                set_state("off: signature not found (build " + g_build + ")");
                g_done = true;
                return;
            }
            return refuse("build " + g_build + ": " + last_why);
        }
        Sleep(250);
    }
    buf.clear();
    buf.shrink_to_fit();
    log("lua_loader: lua_load at %s after %d attempt(s), %lu ms%s%s", where(at.lua_load).c_str(), attempts, static_cast<unsigned long>(GetTickCount() - t0),
        at.why.empty() ? "" : "; ", at.why.c_str());
    if (file_exists(out / "hook_lua_load_off.txt") || file_exists(out / "game_hooks_off.txt"))
        return refuse("kill switch appeared while scanning");

    // MinHook: initialised here (the overlay accepts that later) or already by the overlay (a load into a running game)
    const MH_STATUS mi = MH_Initialize();
    if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) return refuse(std::string("MinHook init: ") + MH_StatusToString(mi));
    g_uses_mh = true;

    ll::Env env;
    env.read = [](uint64_t a, void* o, size_t n) { return ProcessMemory().read(a, o, n); };
    env.readable = readable_range;
    env.where = where;
    env.thread_id = []() { return uint32_t(GetCurrentThreadId()); };
    env.now = now_text;
    env.emit = [](const std::string& s) { g_lines.push(s); };
    env.reader_capturable = reader_capturable;
    env.save_capture = save_capture;
    if (dec.in_memory)
        env.build_override = [saved, crest](const std::string& row, const std::string& stock, std::string& note,
                                     std::string& why) -> std::shared_ptr<const std::string> {
            if (row != "player_kit") {
                why = "no in-memory build for row " + row;
                return nullptr;
            }
            return turbo::faces3d::build_live_override(stock, *saved, note, why, *crest);
        };
    if (at.top_layout_ok) {  // the fallback's pop is safe only on the layout checked in lua_load's own bytes
        env.stack_depth = lua_stack_depth;
        env.top_message = lua_top_message;
        env.pop1 = lua_pop1;
    }
    g_engine = new ll::Engine(env);

    {
        std::error_code ec;
        fs::create_directories(out, ec);
        if (FILE* f = _wfopen(flag.wstring().c_str(), L"w")) {
            std::fprintf(f, "lua_loader installing (process %lu)\n", static_cast<unsigned long>(GetCurrentProcessId()));
            std::fclose(f);
        }
    }
    // The blob reader first (pass-through until lua_load's detour gives it a context), then lua_load. The engine is configured
    // before lua_load's hook is enabled and never changed afterwards.
    uint64_t blob = at.blob_reader;
    if (blob) {
        void* target = reinterpret_cast<void*>(static_cast<uintptr_t>(blob));
        MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&hk_blob), reinterpret_cast<void**>(&o_blob));
        if (st == MH_OK) st = MH_EnableHook(target);
        if (st != MH_OK) {
            log("lua_loader: blob reader hook at %s failed (%s): file sources are not measured, no substitution", where(blob).c_str(), MH_StatusToString(st));
            if (st != MH_ERROR_ALREADY_CREATED) MH_RemoveHook(target);
            blob = 0;
        }
    }
    ll::CaptureConfig cap;
    cap.enabled = true;
    cap.capture_mode = dec.capture_mode;
    cap.legacy_present = sw.legacy_present;
    cap.build_in_memory = dec.in_memory;
    g_engine->configure(at.gets, blob, substitute, rows, overrides, g_build, cap);
    g_substitute = substitute;
    void* target = reinterpret_cast<void*>(static_cast<uintptr_t>(at.lua_load));
    MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&hk_lua_load), reinterpret_cast<void**>(&o_lua_load));
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) {
        if (st != MH_ERROR_ALREADY_CREATED) MH_RemoveHook(target);
        if (blob) {
            void* bt = reinterpret_cast<void*>(static_cast<uintptr_t>(blob));
            MH_DisableHook(bt);  // the trampoline stays allocated (MinHook keeps its buffer): a reader call in flight returns safely
        }
        DeleteFileW(flag.wstring().c_str());
        return refuse(std::string("MinHook on lua_load: ") + MH_StatusToString(st));
    }
    DeleteFileW(flag.wstring().c_str());
    g_installed = true;
    set_state(std::string(substitute ? "SUBSTITUTE" : "LOG-ONLY") + ": lua_load hooked at " + where(at.lua_load) +
              (blob ? ", blob reader at " + where(blob) : ", blob reader not hooked (not needed for player_kit.lua)") + (at.gets ? ", getS at " + where(at.gets) : ", getS unknown") +
              (at.top_layout_ok ? ", stack layout confirmed" : ", stack layout NOT confirmed (no chunkname substitution)") + ", build " + g_build);
    g_done = true;
    log("lua_loader: installed, %s", g_state.c_str());
    watcher();  // this thread keeps the kill switch and the log lines (never returns)
}

DWORD WINAPI early_thread(LPVOID) {
    try {
        run();
    } catch (const std::exception& e) {
        log("lua_loader: unexpected error, not installed: %s", e.what());
    } catch (...) {
        log("lua_loader: unexpected error, not installed");
    }
    return 0;
}

}  // namespace

void start_lua_loader_early() {
    HANDLE th = CreateThread(nullptr, 0, early_thread, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
}

bool lua_loader_uses_minhook() { return g_uses_mh.load(); }

std::vector<std::string> lua_loader_status() {
    std::vector<std::string> v;
    if (!g_opted_in.load()) return v;
    std::string line = "Lua loader hook: ";
    line += g_done.load() ? g_state : std::string("scanning for lua_load");
    if (g_installed.load() && g_engine) {
        const auto s = g_engine->stats();
        line += " | calls " + std::to_string(s.calls) + " | logged " + std::to_string(s.logged) + " | blob reads " + std::to_string(s.blob_reads) +
                " | substituted " + std::to_string(s.substituted) + " | fallbacks " + std::to_string(s.fallbacks) + " | errors " + std::to_string(s.errors) + (g_engine->killed() ? " [kill switch]" : "");
    }
    v.push_back(line);
    const turbo::lualoader::Faces3dStatus f = faces3d_status();
    if (f.installed) {
        std::string l2 = "Faces 3D (player_kit.lua): ";
        l2 += f.captured_md5.empty() ? std::string("stock not captured yet") : "stock " + std::to_string(f.captured_size) + " B md5 " + f.captured_md5;
        l2 += f.legacy_refused ? " | substitution REFUSED (mods\\legacy player_kit.lua present)" : f.substitute ? " | substitute" : " | log-only";
        if (f.in_memory)
            l2 += f.built ? " | v4 built in memory" : f.built_error.empty() ? " | v4 not built yet" : " | stock served: " + f.built_error;
        else if (f.substitute)
            l2 += std::string(" | override header ") +
                  (!f.header_ok ? "missing/invalid" : !f.build_key_ok ? "other build" : (f.stock_matches_header || f.captured_md5.empty()) ? "ok" : "STALE");
        l2 += " | captures " + std::to_string(f.captures) + " | served stock " + std::to_string(f.served_stock) + " | substituted " + std::to_string(f.substituted) +
              " | fallbacks " + std::to_string(f.fallbacks) + (f.cache_written ? " | cache written" : "");
        if (!f.last_error.empty()) l2 += " | last: " + f.last_error;
        v.push_back(l2);
    }
    return v;
}

turbo::lualoader::Faces3dStatus faces3d_status() {
    turbo::lualoader::Faces3dStatus st;
    st.opted_in = g_opted_in.load();
    st.installed = g_installed.load() && g_engine;
    if (st.installed) g_engine->fill_status(st);
    st.legacy_refused = st.legacy_refused || g_legacy_refused.load();
    st.cache_written = g_cache_written.load();
    {
        std::lock_guard<std::mutex> lk(g_cap_m);
        if (!g_cache_error.empty() && st.last_error.empty()) st.last_error = "cache: " + g_cache_error;
    }
    {
        std::lock_guard<std::mutex> lk(g_saved_m);
        st.saved_known = g_saved_known;
        st.saved_ids = g_saved_ids;
    }
    return st;
}

}  // namespace host
