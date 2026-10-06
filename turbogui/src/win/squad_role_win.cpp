// FC 27 LE Turbo GUI - "squad_role" game call inside FC27.exe (see squad_role_win.h, core/squad_role.h,
// docs/re/player_status_roles.md sections 3 and 7.2)
#include "squad_role_win.h"

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

roles::Fns g_fns;
bool g_installed = false;
std::string g_off;
std::mutex g_mutex;
std::string g_last;
std::atomic<long long> g_runs{0}, g_queued{0}, g_ok{0}, g_refused_busy{0}, g_created{0}, g_removed{0};
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
        log("game call squad_role: taking over a request that never ran (older than %lld s)", kBusyStaleMs / 1000);
        return true;
    }
    return false;
}

template <class F> F fn_at(uint64_t a) { return reinterpret_cast<F>(static_cast<uintptr_t>(a)); }

struct RealCaller : roles::Caller {
    using InTeamFn = uint8_t (*)(void*, int, int);
    using FindFn = void* (*)(void*, int);
    using AddFn = uint8_t (*)(void*, int, uint8_t);
    using RemoveFn = uint8_t (*)(void*, int);

    bool in_team(uint64_t dc, int pid, int team, bool& in, std::string& err) override {
        if (!g_fns.is_player_in_team) return (err = "function not resolved", false);
        in = (fn_at<InTeamFn>(g_fns.is_player_in_team)(reinterpret_cast<void*>(dc), pid, team) & 1) != 0;
        return true;
    }
    bool find(uint64_t sub, int pid, uint64_t& entry, std::string& err) override {
        if (!g_fns.find) return (err = "function not resolved", false);
        entry = reinterpret_cast<uint64_t>(fn_at<FindFn>(g_fns.find)(reinterpret_cast<void*>(sub), pid));
        return true;
    }
    // bool PlayerStatusManager::AddPlayer(psm, int pid, uint8 role) 0x147BE7BB4 (role in r8b: 1..5, or 0xFF = the game computes it)
    bool add_player(uint64_t psm, int pid, int role, bool& added, std::string& err) override {
        if (!g_fns.add_player) return (err = "function not resolved", false);
        added = (fn_at<AddFn>(g_fns.add_player)(reinterpret_cast<void*>(psm), pid, static_cast<uint8_t>(role)) & 1) != 0;
        return true;
    }
    // bool PlayerStatusData::Remove(sub = psm + 0x10, int pid) 0x147D90C80
    bool remove(uint64_t sub, int pid, bool& removed, std::string& err) override {
        if (!g_fns.remove) return (err = "function not resolved", false);
        removed = (fn_at<RemoveFn>(g_fns.remove)(reinterpret_cast<void*>(sub), pid) & 1) != 0;
        return true;
    }
};

roles::Result run_now(roles::Request req) {
    roles::Result r;
    std::string why;
    if (!squad_role_ready(&why)) {
        r.stage = "off";
        r.message = "squad_role: off (" + why + ")";
    } else {
        if (!req.image_base) req.image_base = game_image_base();
        if (!req.image_size) req.image_size = game_image_size();
        req.create_off = roles::create_killed(out_dir());
        ProcessMemory mem;
        RealCaller caller;
        r = roles::run(mem, caller, g_fns, req);
    }
    ++g_runs;
    if (r.ok) ++g_ok;
    if (r.created) ++g_created;
    if (r.removed) ++g_removed;
    log("game call squad_role(%s, player %d, role %d, comm %s): %s [%s] %s (role %lld, how %d, count %d, psm %s, entry %s%s%s)",
        roles::action_name(req.action), req.player, req.role, hex(req.comm).c_str(), r.ok ? "ok" : "failed", r.stage.c_str(), r.message.c_str(),
        static_cast<long long>(r.role), r.how, r.count, hex(r.psm).c_str(), hex(r.entry).c_str(), r.created ? ", created" : "",
        r.removed ? ", removed" : "");
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last = std::string(r.ok ? "ok: " : "failed: ") + r.message;
    }
    return r;
}

void publish(int32_t seq, const roles::Request& req, const roles::Result& r, int32_t status) {
    if (seq == 0) return;
    publish_call_result(seq, status, r.role, roles::out1(req, r), r.message);
}

}  // namespace

bool squad_role_ready(std::string* why) {
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
    if (roles::killed(out_dir())) {
        if (why) *why = std::string("kill switch turbo_output\\") + roles::kill_switch_name() + " is present";
        return false;
    }
    return true;
}

std::vector<std::string> squad_role_status() {
    std::vector<std::string> out;
    std::string why;
    char line[512];
    if (squad_role_ready(&why)) {
        std::string create = roles::create_killed(out_dir()) ? std::string("off (kill switch ") + roles::create_kill_switch_name() + ")"
                             : !g_fns.add_player          ? std::string("off (psm_add_player not found)")
                                                          : "on";
        std::snprintf(line, sizeof(line),
                      "squad_role: ready | find %s, add_player %s, remove %s | create missing entries: %s | runs %lld (ok %lld, created %lld, "
                      "removed %lld, queued %lld, busy %lld)",
                      hex(g_fns.find).c_str(), g_fns.add_player ? hex(g_fns.add_player).c_str() : "not resolved",
                      g_fns.remove ? hex(g_fns.remove).c_str() : "not resolved", create.c_str(), g_runs.load(), g_ok.load(), g_created.load(),
                      g_removed.load(), g_queued.load(), g_refused_busy.load());
    } else
        std::snprintf(line, sizeof(line), "squad_role: off (%s)", why.c_str());
    out.push_back(line);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_last.empty()) out.push_back("  last: " + g_last);
    return out;
}

roles::Result squad_role_request(roles::Request req, int32_t seq) {
    roles::Result r;
    std::string why;
    if (!squad_role_ready(&why)) {
        r.stage = "off";
        r.message = "squad_role: off (" + why + ")";
        publish(seq, req, r, kCallFailed);
        return r;
    }
    if (!req.bad_args.empty() || !roles::valid_action(req.action)) {
        r.stage = "validate";
        r.message = !req.bad_args.empty() ? req.bad_args : "unknown squad_role code " + std::to_string(req.action);
        publish(seq, req, r, kCallFailed);
        return r;
    }
    if (!take_gate()) {
        ++g_refused_busy;
        r.stage = "busy";
        r.message = "another squad_role call is still queued or running: wait for its result";
        publish(seq, req, r, kCallFailed);
        return r;
    }
    const uint32_t tid = GetCurrentThreadId();
    const uint32_t game = game_thread_id();
    if (game != 0 && tid == game) {
        r = run_now(req);
        g_busy = false;
        publish(seq, req, r, r.ok ? kCallOk : kCallFailed);
        return r;
    }
    ++g_queued;
    const char* via = run_on_game_thread([req, seq]() {
        roles::Result rr = run_now(req);
        g_busy = false;
        publish(seq, req, rr, rr.ok ? kCallOk : kCallFailed);
    });
    r.stage = "queued";
    r.message = std::string("queued for the game thread: it runs ") +
                (std::string(via) == "hook" ? "at the next game tick"
                 : std::string(via) == "lua" ? "on the next career-mode event (advance a day)"
                                            : "when the game-thread dispatcher is available");
    publish(seq, req, r, kCallQueued);
    return r;
}

void install_squad_role() {
    if (g_installed) return;
    g_installed = true;
    if (!game_hooks_allowed()) {
        g_off = "game hooks are off for this game build";
        log("game calls: squad_role off (%s)", g_off.c_str());
        return;
    }
    g_fns.psm_vtable = game_signature("psm_vtable");
    g_fns.psm_handle_event = game_signature("psm_handle_event");
    g_fns.find = game_signature("psm_get_entry");
    g_fns.is_player_in_team = game_signature("dc_is_player_in_team");
    g_fns.um_vtable = game_signature("um_vtable");
    g_fns.add_player = game_signature("psm_add_player");
    g_fns.remove = game_signature("psm_remove_entry");
    if (const char* m = g_fns.missing()) {
        g_off = std::string("signature ") + m + " was not found on this game build";
        log("game calls: squad_role off (%s)", g_off.c_str());
        return;
    }
    log("game calls: squad_role resolved (vtable %s, HandleEvent %s, Find %s, AddPlayer %s, Remove %s; kill switches turbo_output\\%s, turbo_output\\%s)",
        hex(g_fns.psm_vtable).c_str(), hex(g_fns.psm_handle_event).c_str(), hex(g_fns.find).c_str(),
        g_fns.add_player ? hex(g_fns.add_player).c_str() : "NOT resolved: missing entries are counted only",
        g_fns.remove ? hex(g_fns.remove).c_str() : "NOT resolved: stale entries stay", roles::kill_switch_name(), roles::create_kill_switch_name());
}

}  // namespace host
