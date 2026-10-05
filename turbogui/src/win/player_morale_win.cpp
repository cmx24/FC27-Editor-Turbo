// FC 27 LE Turbo GUI - "player_morale" game call inside FC27.exe (see player_morale_win.h, core/player_morale.h,
// docs/re/player_status_roles.md section 4)
#include "player_morale_win.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

#include "game_calls_win.h"
#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

morale::Fns g_fns;
bool g_installed = false;
std::string g_off;
std::mutex g_mutex;
std::string g_last;
std::atomic<long long> g_runs{0}, g_queued{0}, g_ok{0}, g_refused_busy{0};
std::atomic<bool> g_busy{false};
std::atomic<long long> g_busy_since{0};
constexpr long long kBusyStaleMs = 60000;

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

fs::path out_dir() { return le_root() / "turbo_output"; }

bool take_gate() {
    bool expected = false;
    if (g_busy.compare_exchange_strong(expected, true)) {
        g_busy_since = now_ms();
        return true;
    }
    if (now_ms() - g_busy_since.load() > kBusyStaleMs) {
        g_busy_since = now_ms();
        log("game call player_morale: taking over a request that never ran (older than %lld s)", kBusyStaleMs / 1000);
        return true;
    }
    return false;
}

template <class F> F fn_at(uint64_t a) { return reinterpret_cast<F>(static_cast<uintptr_t>(a)); }

struct RealCaller : morale::Caller {
    using InTeamFn = uint8_t (*)(void*, int, int);
    using FindFn = void* (*)(void*, int);
    using LevelFn = int (*)(void*, int, int);
    using SetFn = void (*)(void*, void*, int);

    bool in_team(uint64_t dc, int pid, int team, bool& in, std::string& err) override {
        if (!g_fns.is_player_in_team) return (err = "function not resolved", false);
        in = (fn_at<InTeamFn>(g_fns.is_player_in_team)(reinterpret_cast<void*>(dc), pid, team) & 1) != 0;
        return true;
    }
    bool find_record(uint64_t store, int pid, uint64_t& rec, std::string& err) override {
        if (!g_fns.find_record) return (err = "function not resolved", false);
        rec = reinterpret_cast<uint64_t>(fn_at<FindFn>(g_fns.find_record)(reinterpret_cast<void*>(store), pid));
        return true;
    }
    bool level(uint64_t pmm, int total, int emotion, int& out, std::string& err) override {
        if (!g_fns.level) return (err = "function not resolved", false);
        out = fn_at<LevelFn>(g_fns.level)(reinterpret_cast<void*>(pmm), total, emotion);
        return true;
    }
    bool set_total(uint64_t pmm, uint64_t rec, int total, std::string& err) override {
        if (!g_fns.set_total) return (err = "function not resolved", false);
        fn_at<SetFn>(g_fns.set_total)(reinterpret_cast<void*>(pmm), reinterpret_cast<void*>(rec), total);
        return true;
    }
};

morale::Result run_now(morale::Request req) {
    morale::Result r;
    std::string why;
    if (!player_morale_ready(&why)) {
        r.stage = "off";
        r.message = "player_morale: off (" + why + ")";
    } else {
        if (!req.image_base) req.image_base = game_image_base();
        if (!req.image_size) req.image_size = game_image_size();
        ProcessMemory mem;
        RealCaller caller;
        r = morale::run(mem, caller, g_fns, req);
    }
    ++g_runs;
    if (r.ok) ++g_ok;
    log("game call player_morale(%s, player %d, value %d, comm %s): %s [%s] %s (total %lld, level %lld, target %d, records %d, pmm %s, rec %s)",
        morale::action_name(req.action), req.player, req.value, hex(req.comm).c_str(), r.ok ? "ok" : "failed", r.stage.c_str(), r.message.c_str(),
        static_cast<long long>(r.total), static_cast<long long>(r.level), r.target, r.records, hex(r.pmm).c_str(), hex(r.rec).c_str());
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last = std::string(r.ok ? "ok: " : "failed: ") + r.message;
    }
    return r;
}

void publish(int32_t seq, const morale::Result& r, int32_t status) {
    if (seq == 0) return;
    publish_call_result(seq, status, r.total, r.level, r.message);
}

}  // namespace

bool player_morale_ready(std::string* why) {
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
    if (morale::killed(out_dir())) {
        if (why) *why = std::string("kill switch turbo_output\\") + morale::kill_switch_name() + " is present";
        return false;
    }
    return true;
}

std::vector<std::string> player_morale_status() {
    std::vector<std::string> out;
    std::string why;
    char line[512];
    if (player_morale_ready(&why))
        std::snprintf(line, sizeof(line), "player_morale: ready | find %s, set_total %s, level %s | runs %lld (ok %lld, queued %lld, busy %lld)",
                      hex(g_fns.find_record).c_str(), hex(g_fns.set_total).c_str(), g_fns.level ? hex(g_fns.level).c_str() : "not resolved (85 used)",
                      g_runs.load(), g_ok.load(), g_queued.load(), g_refused_busy.load());
    else
        std::snprintf(line, sizeof(line), "player_morale: off (%s)", why.c_str());
    out.push_back(line);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_last.empty()) out.push_back("  last: " + g_last);
    return out;
}

morale::Result player_morale_request(morale::Request req, int32_t seq) {
    morale::Result r;
    std::string why;
    if (!player_morale_ready(&why)) {
        r.stage = "off";
        r.message = "player_morale: off (" + why + ")";
        publish(seq, r, kCallFailed);
        return r;
    }
    if (!req.bad_args.empty() || !morale::valid_action(req.action)) {
        r.stage = "validate";
        r.message = !req.bad_args.empty() ? req.bad_args : "unknown player_morale code " + std::to_string(req.action);
        publish(seq, r, kCallFailed);
        return r;
    }
    if (!take_gate()) {
        ++g_refused_busy;
        r.stage = "busy";
        r.message = "another player_morale call is still queued or running: wait for its result";
        publish(seq, r, kCallFailed);
        return r;
    }
    const uint32_t tid = GetCurrentThreadId();
    const uint32_t game = game_thread_id();
    if (game != 0 && tid == game) {
        r = run_now(req);
        g_busy = false;
        publish(seq, r, r.ok ? kCallOk : kCallFailed);
        return r;
    }
    ++g_queued;
    const char* via = run_on_game_thread([req, seq]() {
        morale::Result rr = run_now(req);
        g_busy = false;
        publish(seq, rr, rr.ok ? kCallOk : kCallFailed);
    });
    r.stage = "queued";
    r.message = std::string("queued for the game thread: it runs ") +
                (std::string(via) == "hook" ? "at the next game tick"
                 : std::string(via) == "lua" ? "on the next career-mode event (advance a day)"
                                            : "when the game-thread dispatcher is available");
    publish(seq, r, kCallQueued);
    return r;
}

void install_player_morale() {
    if (g_installed) return;
    g_installed = true;
    if (!game_hooks_allowed()) {
        g_off = "game hooks are off for this game build";
        log("game calls: player_morale off (%s)", g_off.c_str());
        return;
    }
    g_fns.pmm_vtable = game_signature("pmm_vtable");
    g_fns.pmm_handle_event = game_signature("pmm_handle_event");
    g_fns.find_record = game_signature("pmm_find_record");
    g_fns.set_total = game_signature("pmm_set_total");
    g_fns.level = game_signature("pmm_get_level");
    g_fns.is_player_in_team = game_signature("dc_is_player_in_team");
    g_fns.um_vtable = game_signature("um_vtable");
    if (const char* m = g_fns.missing()) {
        g_off = std::string("signature ") + m + " was not found on this game build";
        log("game calls: player_morale off (%s)", g_off.c_str());
        return;
    }
    log("game calls: player_morale resolved (vtable %s, HandleEvent %s, Find %s, SetTotalMorale %s, level %s; kill switch turbo_output\\%s)",
        hex(g_fns.pmm_vtable).c_str(), hex(g_fns.pmm_handle_event).c_str(), hex(g_fns.find_record).c_str(), hex(g_fns.set_total).c_str(),
        g_fns.level ? hex(g_fns.level).c_str() : "NOT resolved: 85 is used", morale::kill_switch_name());
}

}  // namespace host
