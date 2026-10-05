// FC 27 LE Turbo GUI - "player_move" game call inside FC27.exe (see player_move_win.h, core/player_move.h,
// docs/re/realtime_transfers.md)
#include "player_move_win.h"

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

pm::Fns g_fns;
bool g_installed = false;
std::string g_off;  // why the call is off at install time ("" = resolved)
std::mutex g_mutex;
std::string g_last;  // last outcome (Status tab)
std::atomic<long long> g_runs{0}, g_queued{0}, g_ok{0}, g_refused_busy{0};
// One call at a time: set when a request is accepted, cleared when it has run. A request that was dropped from the dispatcher's queue
// would leave it set for good, so a flag older than kBusyStaleMs is taken over.
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
        log("game call player_move: taking over a request that never ran (older than %lld s)", kBusyStaleMs / 1000);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- the real calls (game thread only)
struct RealCaller : pm::Caller {
    using InTeamFn = uint8_t (*)(void*, int, int);
    using CountsFn = void (*)(void*, int, int*, int*, int*);
    using LeagueFn = int (*)(void*, int);
    using InternationalFn = uint8_t (*)(int);
    using MovedFn = void (*)(void*, int, int, int);
    using AddContractFn = void (*)(void*, int, int, int, int, int, const void*, int);
    using ReleaseFn = int (*)(void*, int);

    bool in_team(uint64_t dc, int pid, int team, bool& in, std::string& err) override {
        if (!g_fns.is_player_in_team) {
            err = "function not resolved";
            return false;
        }
        in = (reinterpret_cast<InTeamFn>(static_cast<uintptr_t>(g_fns.is_player_in_team))(reinterpret_cast<void*>(dc), pid, team) & 1) != 0;
        return true;
    }
    bool squad_counts(uint64_t dc, int team, int& rows, int& loaned, int& pending, std::string& err) override {
        if (!g_fns.squad_counts) {
            err = "function not resolved";
            return false;
        }
        rows = loaned = pending = -1;
        reinterpret_cast<CountsFn>(static_cast<uintptr_t>(g_fns.squad_counts))(reinterpret_cast<void*>(dc), team, &rows, &loaned, &pending);
        return true;
    }
    bool league_of_team(uint64_t dc, int team, int& league, std::string& err) override {
        if (!g_fns.league_of_team) {
            err = "function not resolved";
            return false;
        }
        league = reinterpret_cast<LeagueFn>(static_cast<uintptr_t>(g_fns.league_of_team))(reinterpret_cast<void*>(dc), team);
        return true;
    }
    bool is_international(int league, bool& out, std::string& err) override {
        if (!g_fns.is_international) {
            err = "function not resolved";
            return false;
        }
        out = (reinterpret_cast<InternationalFn>(static_cast<uintptr_t>(g_fns.is_international))(league) & 1) != 0;
        return true;
    }
    bool player_moved(uint64_t team_util, int pid, int from, int to, std::string& err) override {
        if (!g_fns.player_moved) {
            err = "function not resolved";
            return false;
        }
        reinterpret_cast<MovedFn>(static_cast<uintptr_t>(g_fns.player_moved))(reinterpret_cast<void*>(team_util), pid, from, to);
        return true;
    }
    bool add_contract(uint64_t pcm, int pid, int team, int months, int wage, int k, uint64_t date, int status, std::string& err) override {
        if (!g_fns.add_contract) {
            err = "function not resolved";
            return false;
        }
        reinterpret_cast<AddContractFn>(static_cast<uintptr_t>(g_fns.add_contract))(reinterpret_cast<void*>(pcm), pid, team, months, wage, k,
                                                                                     reinterpret_cast<const void*>(date), status);
        return true;
    }
    bool release_player(uint64_t ctm, int pid, int& code, std::string& err) override {
        if (!g_fns.release_player) {
            err = "function not resolved";
            return false;
        }
        code = reinterpret_cast<ReleaseFn>(static_cast<uintptr_t>(g_fns.release_player))(reinterpret_cast<void*>(ctm), pid);
        return true;
    }
};

pm::Result run_now(pm::Request req) {
    pm::Result r;
    std::string why;
    if (!player_move_ready(&why)) {
        r.stage = "off";
        r.message = why;
    } else {
        if (!req.image_base) req.image_base = game_image_base();
        if (!req.image_size) req.image_size = game_image_size();
        ProcessMemory mem;
        RealCaller caller;
        r = pm::run(mem, caller, g_fns, req);
    }
    ++g_runs;
    if (r.ok) ++g_ok;
    log("game call player_move(%s, player %d, from %d, to %d, months %d, wage %d, comm %s): %s [%s] %s (from_ok %d, to_ok %d, user team %d, dc %s, team util %s)",
        pm::action_name(req.action), req.player, req.from, req.to, req.months, req.wage, hex(req.comm).c_str(), r.ok ? "ok" : "failed", r.stage.c_str(),
        r.message.c_str(), r.from_ok, r.to_ok, r.at.user_team, hex(r.at.dc).c_str(), hex(r.at.team_util).c_str());
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last = std::string(r.ok ? "ok: " : "failed: ") + r.message;
    }
    return r;
}

void publish(int32_t seq, const pm::Result& r, int32_t status) {
    if (seq == 0) return;
    publish_call_result(seq, status, r.from_ok, r.to_ok, r.message);
}

}  // namespace

// ---------------------------------------------------------------- public API
pm::Fns player_move_fns() { return g_fns; }

bool player_move_ready(std::string* why) {
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
    if (pm::killed(out_dir())) {
        if (why) *why = std::string("kill switch turbo_output\\") + pm::kill_switch_name() + " is present";
        return false;
    }
    return true;
}

std::vector<std::string> player_move_status() {
    std::vector<std::string> out;
    std::string why;
    char line[768];
    if (player_move_ready(&why)) {
        std::snprintf(line, sizeof(line),
                      "player_move: ready | player_moved %s, in_team %s, squad_counts %s, league %s / %s, add_contract %s, release %s | vtables pcm %s, tm %s, "
                      "um %s, ctm %s | runs %lld (ok %lld, queued %lld, busy %lld)",
                      hex(g_fns.player_moved).c_str(), hex(g_fns.is_player_in_team).c_str(), hex(g_fns.squad_counts).c_str(), hex(g_fns.league_of_team).c_str(),
                      hex(g_fns.is_international).c_str(), hex(g_fns.add_contract).c_str(), hex(g_fns.release_player).c_str(), hex(g_fns.pcm_vtable).c_str(),
                      hex(g_fns.tm_vtable).c_str(), hex(g_fns.um_vtable).c_str(), hex(g_fns.ctm_vtable).c_str(), g_runs.load(), g_ok.load(), g_queued.load(),
                      g_refused_busy.load());
    } else {
        std::snprintf(line, sizeof(line), "player_move: off (%s)", why.c_str());
    }
    out.push_back(line);
    if (why.empty()) {
        if (const char* m = g_fns.missing(pm::kActionRelease)) out.push_back(std::string("  release: off (signature ") + m + " was not found on this game build)");
        if (!g_fns.morale_vtable) out.push_back("  morale gate: not checked (signature morale_vtable was not found on this game build)");
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_last.empty()) out.push_back("  last: " + g_last);
    return out;
}

pm::Result player_move_request(pm::Request req, int32_t seq) {
    pm::Result r;
    std::string why;
    if (!player_move_ready(&why)) {
        r.stage = "off";
        r.message = why;
        publish(seq, r, kCallFailed);
        return r;
    }
    if (!req.bad_args.empty() || !pm::valid_action(req.action) || req.player <= 0) {
        r.stage = "validate";
        r.message = !req.bad_args.empty()    ? req.bad_args
                    : !pm::valid_action(req.action) ? "unknown player_move code " + std::to_string(req.action) + " (1 move, 2 release, 9 check only)"
                                                    : "player id must be a positive number";
        publish(seq, r, kCallFailed);
        return r;
    }
    if (!take_gate()) {
        ++g_refused_busy;
        r.stage = "busy";
        r.message = "another player_move call is still queued or running: wait for its result";
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
        pm::Result rr = run_now(req);
        g_busy = false;
        publish(seq, rr, rr.ok ? kCallOk : kCallFailed);
    });
    r.stage = "queued";
    r.message = std::string("queued for the game thread: it runs ") +
                (std::string(via) == "hook" ? "at the next game tick"
                 : std::string(via) == "lua" ? "on the next career-mode event (advance a day)"
                                            : "when the game-thread dispatcher is available");
    publish(seq, r, kCallQueued);
    log("game call player_move(%s, player %d, from %d, to %d) queued from thread %lu (game thread %lu, via %s)", pm::action_name(req.action), req.player,
        req.from, req.to, static_cast<unsigned long>(tid), static_cast<unsigned long>(game), via && *via ? via : "none yet");
    return r;
}

void install_player_move() {
    if (g_installed) return;
    g_installed = true;
    if (!game_hooks_allowed()) {
        g_off = "game hooks are off for this game build";
        log("game calls: player_move off (%s)", g_off.c_str());
        return;
    }
    g_fns.player_moved = game_signature("teamutil_player_moved");
    g_fns.is_player_in_team = game_signature("dc_is_player_in_team");
    g_fns.squad_counts = game_signature("dc_squad_counts");
    g_fns.league_of_team = game_signature("dc_get_league_of_team");
    g_fns.is_international = game_signature("is_international_league");
    g_fns.add_contract = game_signature("pcm_add_contract_record");
    g_fns.release_player = game_signature("ctm_release_player");
    g_fns.pcm_vtable = game_signature("pcm_vtable");
    g_fns.tm_vtable = game_signature("tm_vtable");
    g_fns.um_vtable = game_signature("um_vtable");
    g_fns.ctm_vtable = game_signature("ctm_vtable");
    g_fns.morale_vtable = game_signature("morale_vtable");
    g_fns.morale_handle_event = game_signature("morale_handle_event");
    if (const char* m = g_fns.missing(pm::kActionMove)) {
        g_off = std::string("signature ") + m + " was not found on this game build";
        log("game calls: player_move off (%s)", g_off.c_str());
        return;
    }
    log("game calls: player_move resolved (PlayerMoved %s, IsPlayerInTeam %s, SquadCounts %s, GetLeagueOfTeam %s, IsInternationalLeague %s, "
        "AddContractRecord %s, ReleasePlayer %s; vtables pcm %s, tm %s, um %s, ctm %s, morale %s / HandleEvent %s; kill switch turbo_output\\%s)",
        hex(g_fns.player_moved).c_str(), hex(g_fns.is_player_in_team).c_str(), hex(g_fns.squad_counts).c_str(), hex(g_fns.league_of_team).c_str(),
        hex(g_fns.is_international).c_str(), hex(g_fns.add_contract).c_str(), hex(g_fns.release_player).c_str(), hex(g_fns.pcm_vtable).c_str(),
        hex(g_fns.tm_vtable).c_str(), hex(g_fns.um_vtable).c_str(), hex(g_fns.ctm_vtable).c_str(), hex(g_fns.morale_vtable).c_str(),
        hex(g_fns.morale_handle_event).c_str(), pm::kill_switch_name());
    if (const char* m = g_fns.missing(pm::kActionRelease)) log("game calls: player_move release off (signature %s was not found; moves are unaffected)", m);
}

}  // namespace host
