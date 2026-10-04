// FC 27 LE Turbo GUI - "reveal player data" game call inside FC27.exe (see reveal_win.h, core/reveal.h,
// docs/re/development.md)
#include "reveal_win.h"

#include <atomic>
#include <cstdio>
#include <mutex>

#include "game_calls_win.h"
#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

std::atomic<uint64_t> g_pdrm{0};
std::atomic<long long> g_pdrm_events{0};
pdrm::Fns g_fns;
bool g_installed = false;
std::string g_off;  // why the call is off at install time ("" = resolved)
std::mutex g_mutex;
std::string g_last;  // last outcome (Status tab)
std::atomic<long long> g_runs{0}, g_queued{0};

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

fs::path call_off_path() { return le_root() / "turbo_output" / "call_reveal_off.txt"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// ---------------------------------------------------------------- capture hook
// PlayerDataRevealManager::HandleEvent(this, eventId, Event*): pass-through; afterwards the manager pointer is recorded
using HandleFn = void* (*)(void*, void*, void*, void*);
HandleFn o_handle = nullptr;

void* hk_handle(void* self, void* id, void* ev, void* d) {
    void* r = o_handle(self, id, ev, d);
    if (!game_hook_enabled("pdrm_handle_event")) return r;
    game_hook_called("pdrm_handle_event");
    HOOK_BODY("pdrm_handle_event", {
        ++g_pdrm_events;
        const uint64_t p = reinterpret_cast<uint64_t>(self);
        if (p != g_pdrm.load() && is_ptr(p, 8)) {
            ProcessMemory mem;
            uint64_t vt = 0;
            if (mem.rd(p, vt) && (!g_fns.vtable || vt == g_fns.vtable)) {
                g_pdrm = p;
                log("game calls: PlayerDataRevealManager seen at %s (event %lld)", hex(p).c_str(),
                    static_cast<long long>(reinterpret_cast<uintptr_t>(id) & 0xFFFFFFFF));
            }
        }
    });
    return r;
}

// ---------------------------------------------------------------- the real calls (game thread only)
struct RealCaller : pdrm::Caller {
    using RevealFn = void (*)(void*, int);
    using TodayFn = int (*)(void*);

    bool reveal_player(uint64_t p, int player, std::string& err) override {
        if (!g_fns.reveal_player) {
            err = "function not resolved";
            return false;
        }
        reinterpret_cast<RevealFn>(static_cast<uintptr_t>(g_fns.reveal_player))(reinterpret_cast<void*>(p), player);
        return true;
    }
    bool reveal_team(uint64_t p, int team, std::string& err) override {
        if (!g_fns.reveal_team) {
            err = "function not resolved";
            return false;
        }
        reinterpret_cast<RevealFn>(static_cast<uintptr_t>(g_fns.reveal_team))(reinterpret_cast<void*>(p), team);
        return true;
    }
    bool today(uint64_t p, int& yyyymmdd, std::string& err) override {
        if (!g_fns.today_int) {
            err = "function not resolved";
            return false;
        }
        ProcessMemory mem;
        const uint64_t hub = mem.ptr(p + pdrm::kHub);
        const uint64_t cal = hub ? mem.chain(hub, {pdrm::kHubCalendar, 0}) : 0;
        if (!cal || !mem.ptr(cal + pdrm::kCalendarDate, 1)) {
            err = "calendar not readable";
            return false;
        }
        yyyymmdd = reinterpret_cast<TodayFn>(static_cast<uintptr_t>(g_fns.today_int))(reinterpret_cast<void*>(cal + pdrm::kCalendarDate));
        return true;
    }
};

pdrm::Result run_now(pdrm::Request req) {
    pdrm::Result r;
    std::string why;
    if (!reveal_ready(&why)) {
        r.stage = "off";
        r.message = why;
    } else {
        std::string err;
        req.pdrm = pdrm::choose(req.pdrm, req.managers, g_pdrm.load(), err);
        if (req.pdrm == 0) {
            r.stage = "validate";
            r.message = err;
        } else {
            ProcessMemory mem;
            RealCaller caller;
            r = pdrm::reveal(mem, caller, g_fns, req);
        }
    }
    ++g_runs;
    log("game call reveal(%s %d, pdrm %s, managers %s): %s [%s] %s", req.mode == pdrm::Mode::Team ? "team" : "player", req.id, hex(req.pdrm).c_str(),
        hex(req.managers).c_str(), r.ok ? "ok" : "failed", r.stage.c_str(), r.message.c_str());
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last = std::string(r.ok ? "ok: " : "failed: ") + r.message;
    }
    return r;
}

void publish(int32_t seq, const pdrm::Result& r, int32_t status) {
    if (seq == 0) return;
    const int64_t out0 = r.points_after >= 0 ? r.points_after : r.records_after;
    publish_call_result(seq, status, out0, static_cast<int64_t>(r.records_before), r.message);
}

}  // namespace

// ---------------------------------------------------------------- public API
uint64_t pdrm_seen() { return g_pdrm.load(); }
long long pdrm_events() { return g_pdrm_events.load(); }
pdrm::Fns reveal_fns() { return g_fns; }

bool reveal_ready(std::string* why) {
    if (!g_installed) {
        if (why) *why = "game calls are not installed";
        return false;
    }
    if (!game_hooks_allowed()) {
        if (why) *why = "game hooks are off for this game build (Status tab > Game hooks)";
        return false;
    }
    if (!g_off.empty()) {
        if (why) *why = g_off;
        return false;
    }
    if (file_exists(call_off_path())) {
        if (why) *why = "kill switch turbo_output\\call_reveal_off.txt is present";
        return false;
    }
    return true;
}

std::vector<std::string> reveal_status() {
    std::vector<std::string> out;
    std::string why;
    char line[640];
    if (reveal_ready(&why))
        std::snprintf(line, sizeof(line),
                      "reveal: ready | PlayerDataRevealManager %s (%lld events seen) | reveal_player %s, reveal_team %s, vtable %s, today %s | runs %lld, "
                      "queued %lld",
                      g_pdrm.load() ? hex(g_pdrm.load()).c_str() : "not seen yet", g_pdrm_events.load(), hex(g_fns.reveal_player).c_str(),
                      hex(g_fns.reveal_team).c_str(), hex(g_fns.vtable).c_str(), hex(g_fns.today_int).c_str(), g_runs.load(), g_queued.load());
    else
        std::snprintf(line, sizeof(line), "reveal: off (%s)", why.c_str());
    out.push_back(line);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_last.empty()) out.push_back("  last: " + g_last);
    return out;
}

pdrm::Result reveal_request(pdrm::Request req, int32_t seq) {
    pdrm::Result r;
    std::string why;
    if (!reveal_ready(&why)) {
        r.stage = "off";
        r.message = why;
        publish(seq, r, kCallFailed);
        return r;
    }
    if (req.id <= 0) {
        r.stage = "validate";
        r.message = std::string(req.mode == pdrm::Mode::Team ? "team" : "player") + " id must be a positive number";
        publish(seq, r, kCallFailed);
        return r;
    }
    const uint32_t tid = GetCurrentThreadId();
    const uint32_t game = game_thread_id();
    if (game != 0 && tid == game) {
        r = run_now(req);
        publish(seq, r, r.ok ? kCallOk : kCallFailed);
        return r;
    }
    ++g_queued;
    const char* via = run_on_game_thread([req, seq]() {
        pdrm::Result rr = run_now(req);
        publish(seq, rr, rr.ok ? kCallOk : kCallFailed);
    });
    r.stage = "queued";
    r.message = std::string("queued for the game thread: it runs ") +
                (std::string(via) == "hook"  ? "at the next game tick"
                 : std::string(via) == "lua" ? "on the next career-mode event (advance a day)"
                                             : "when the game-thread dispatcher is available");
    publish(seq, r, kCallQueued);
    log("game call reveal(%s %d) queued from thread %lu (game thread %lu, via %s)", req.mode == pdrm::Mode::Team ? "team" : "player", req.id,
        static_cast<unsigned long>(tid), static_cast<unsigned long>(game), via && *via ? via : "none yet");
    return r;
}

void install_reveal() {
    if (g_installed) return;
    g_installed = true;
    if (!game_hooks_allowed()) {
        g_off = "game hooks are off for this game build";
        log("game calls: reveal off (%s)", g_off.c_str());
        return;
    }
    g_fns.vtable = game_signature("pdrm_vtable");
    g_fns.reveal_player = game_signature("pdrm_reveal_player");
    g_fns.reveal_team = game_signature("pdrm_reveal_team");
    g_fns.today_int = game_signature("calendar_today_int");
    if (const char* m = g_fns.missing()) {
        g_off = std::string("signature ") + m + " was not found on this game build";
        log("game calls: reveal off (%s)", g_off.c_str());
    } else {
        log("game calls: reveal resolved (vtable %s, reveal_player %s, reveal_team %s, today %s; kill switch turbo_output\\call_reveal_off.txt)",
            hex(g_fns.vtable).c_str(), hex(g_fns.reveal_player).c_str(), hex(g_fns.reveal_team).c_str(), hex(g_fns.today_int).c_str());
    }
    // Capture hook (pass-through): records the PlayerDataRevealManager the game passes to HandleEvent
    install_game_hook("pdrm_handle_event", "pdrm_handle_event", reinterpret_cast<void*>(&hk_handle), reinterpret_cast<void**>(&o_handle));
}

}  // namespace host
