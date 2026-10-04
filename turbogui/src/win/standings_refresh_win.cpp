// FC 27 LE Turbo GUI - "standings_refresh" game call inside FC27.exe (see standings_refresh_win.h,
// core/standings_refresh.h, docs/re/standings-ui-path.md)
#include "standings_refresh_win.h"

#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>

#include "game_calls_win.h"
#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

svm::Fns g_fns;
bool g_installed = false;
std::string g_off;  // why the call is off at install time ("" = resolved)
std::mutex g_mutex;
std::string g_last;                 // last outcome (Status tab)
std::deque<svm::Result> g_results;  // outcomes not yet polled by the App (bounded)
std::atomic<long long> g_runs{0}, g_queued{0};
constexpr size_t kMaxResults = 16;

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

fs::path call_off_path() { return le_root() / "turbo_output" / "call_standings_refresh_off.txt"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// ---------------------------------------------------------------- the real calls (game thread only)
struct RealCaller : svm::Caller {
    using RefreshFn = void (*)(void*, int);
    using EventFn = void (*)(void*, int, void*);

    bool refresh_comp(uint64_t svm_ptr, int32_t comp, std::string& err) override {
        if (!g_fns.refresh_comp) {
            err = "function not resolved";
            return false;
        }
        auto fn = reinterpret_cast<RefreshFn>(static_cast<uintptr_t>(g_fns.refresh_comp));
        fn(reinterpret_cast<void*>(svm_ptr), comp);
        return true;
    }
    bool career_event(uint64_t svm_ptr, int32_t event_id, std::string& err) override {
        if (!g_fns.listener) {
            err = "function not resolved";
            return false;
        }
        auto fn = reinterpret_cast<EventFn>(static_cast<uintptr_t>(g_fns.listener));
        fn(reinterpret_cast<void*>(svm_ptr), event_id, nullptr);
        return true;
    }
};

void remember(const svm::Result& r) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last = std::string(r.ok ? "ok: " : "failed: ") + r.message;
    g_results.push_back(r);
    while (g_results.size() > kMaxResults) g_results.pop_front();
}

svm::Result run_now(svm::Request req) {
    svm::Result r;
    std::string why;
    if (!standings_refresh_ready(&why)) {
        r.stage = "off";
        r.message = why;
    } else {
        if (!req.image_base) req.image_base = game_image_base();
        ProcessMemory mem;
        RealCaller caller;
        r = svm::refresh(mem, caller, g_fns, req);
    }
    ++g_runs;
    log("game call standings_refresh(svm %s, managers %s, comm %s, ifce %s): %s [%s] %s", hex(req.svm).c_str(),
        hex(req.managers).c_str(), hex(req.comm).c_str(), hex(req.ifce).c_str(), r.ok ? "ok" : "failed", r.stage.c_str(),
        r.message.c_str());
    remember(r);
    return r;
}

void publish(int32_t seq, const svm::Result& r, int32_t status) {
    if (seq == 0) return;
    publish_call_result(seq, status, static_cast<int64_t>(r.refreshed), static_cast<int64_t>(r.keys.size()), r.message);
}

struct HostService : svm::RefreshService {
    bool request(const svm::Request& req, std::string& why) override {
        if (!standings_refresh_ready(&why)) return false;
        svm::Result r = standings_refresh_request(req, 0);
        if (r.stage == "off") {
            why = r.message;
            return false;
        }
        return true;
    }
    bool poll(svm::Result& out) override {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_results.empty()) return false;
        out = g_results.front();
        g_results.pop_front();
        return true;
    }
    std::string status() override {
        std::vector<std::string> lines = standings_refresh_status();
        std::string s;
        for (const auto& l : lines) s += (s.empty() ? "" : " | ") + l;
        return s;
    }
};

}  // namespace

// ---------------------------------------------------------------- public API
svm::Fns standings_refresh_fns() { return g_fns; }

bool standings_refresh_ready(std::string* why) {
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
        if (why) *why = "kill switch turbo_output\\call_standings_refresh_off.txt is present";
        return false;
    }
    return true;
}

std::vector<std::string> standings_refresh_status() {
    std::vector<std::string> out;
    std::string why;
    char line[512];
    if (standings_refresh_ready(&why))
        std::snprintf(line, sizeof(line),
                      "standings_refresh: ready | refresh_comp %s, listener %s, vtable %s, slot10 %s | runs %lld, queued %lld",
                      hex(g_fns.refresh_comp).c_str(), hex(g_fns.listener).c_str(), hex(g_fns.vtable).c_str(),
                      hex(g_fns.slot10).c_str(), g_runs.load(), g_queued.load());
    else
        std::snprintf(line, sizeof(line), "standings_refresh: off (%s)", why.c_str());
    out.push_back(line);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_last.empty()) out.push_back("  last: " + g_last);
    return out;
}

svm::Result standings_refresh_request(const svm::Request& req, int32_t seq) {
    svm::Result r;
    std::string why;
    if (!standings_refresh_ready(&why)) {
        r.stage = "off";
        r.message = why;
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
        svm::Result rr = run_now(req);
        publish(seq, rr, rr.ok ? kCallOk : kCallFailed);
    });
    r.stage = "queued";
    r.message = std::string("queued for the game thread: it runs ") +
                (std::string(via) == "hook" ? "at the next game tick"
                 : std::string(via) == "lua" ? "on the next career-mode event (advance a day)"
                                            : "when the game-thread dispatcher is available");
    publish(seq, r, kCallQueued);
    log("game call standings_refresh queued from thread %lu (game thread %lu, via %s)", static_cast<unsigned long>(tid),
        static_cast<unsigned long>(game), via && *via ? via : "none yet");
    return r;
}

std::shared_ptr<svm::RefreshService> standings_refresh_service() {
    static std::shared_ptr<svm::RefreshService> s = std::make_shared<HostService>();
    return s;
}

void install_standings_refresh() {
    if (g_installed) return;
    g_installed = true;
    if (!game_hooks_allowed()) {
        g_off = "game hooks are off for this game build";
        log("game calls: standings_refresh off (%s)", g_off.c_str());
        return;
    }
    g_fns.refresh_comp = game_signature("svm_refresh_comp");
    g_fns.listener = game_signature("svm_listener");
    g_fns.vtable = game_signature("svm_vtable");
    g_fns.slot10 = game_signature("svm_slot10");
    if (const char* m = g_fns.missing()) {
        g_off = std::string("signature ") + m + " was not found on this game build";
        log("game calls: standings_refresh off (%s)", g_off.c_str());
        return;
    }
    if (!g_fns.vtable) log("game calls: standings_refresh: svm_vtable not found, the built-in RVA 0x%llX is used",
                           static_cast<unsigned long long>(svm::kRvaVtable));
    log("game calls: standings_refresh resolved (refresh_comp %s, listener %s, vtable %s, slot10 %s; kill switch "
        "turbo_output\\call_standings_refresh_off.txt)",
        hex(g_fns.refresh_comp).c_str(), hex(g_fns.listener).c_str(), hex(g_fns.vtable).c_str(), hex(g_fns.slot10).c_str());
}

}  // namespace host
