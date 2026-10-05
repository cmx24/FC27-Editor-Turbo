// FC 27 LE Turbo GUI - game calls inside FC27.exe (see game_calls_win.h, core/game_calls.h, docs/re/job_offer.md)
#include "game_calls_win.h"

#include <atomic>
#include <cstdio>
#include <mutex>

#include "game_hooks.h"
#include "host.h"
#include "reveal_win.h"
#include "manager_rules_win.h"
#include "standings_refresh_win.h"
#include "transfer_list_win.h"
#include "player_move_win.h"
#include "match_setup_win.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

std::atomic<uint64_t> g_jmm{0};
std::atomic<long long> g_jmm_events{0};
std::atomic<uint64_t> g_mailbox{0};
JobMarketFns g_fns;
bool g_installed = false;
std::string g_off;  // why the call is off at install time ("" = resolved)
std::mutex g_mutex;
std::string g_last;  // last outcome (Status tab)
std::atomic<long long> g_runs{0};

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

fs::path call_off_path() { return le_root() / "turbo_output" / "call_job_offer_off.txt"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// ---------------------------------------------------------------- capture hook
// JobMarketManager::HandleEvent(this, eventId, Event*): pass-through; afterwards the manager pointer is recorded
using HandleFn = void* (*)(void*, void*, void*, void*);
HandleFn o_handle = nullptr;

void* hk_handle(void* self, void* id, void* ev, void* d) {
    void* r = o_handle(self, id, ev, d);
    if (!game_hook_enabled("jmm_handle_event")) return r;
    game_hook_called("jmm_handle_event");
    HOOK_BODY("jmm_handle_event", {
        ++g_jmm_events;
        const uint64_t p = reinterpret_cast<uint64_t>(self);
        if (p != g_jmm.load() && is_ptr(p, 8)) {
            ProcessMemory mem;
            uint64_t vt = 0;
            if (mem.rd(p, vt) && (!g_fns.vtable || vt == g_fns.vtable)) {
                g_jmm = p;
                log("game calls: JobMarketManager seen at %s (event %lld)", hex(p).c_str(),
                    static_cast<long long>(reinterpret_cast<uintptr_t>(id) & 0xFFFFFFFF));
            }
        }
    });
    return r;
}

// ---------------------------------------------------------------- the real calls (game thread only)
struct RealCaller : GameCaller {
    using HasFn = uint8_t (*)(void*, int);
    using ApplyFn = void (*)(void*, int);
    using OfferFn = void (*)(void*, void*);
    using TodayFn = int (*)(void*);

    bool has_application(uint64_t jmm, int team, bool& out, std::string& err) override {
        if (!g_fns.has_application) {
            err = "function not resolved";
            return false;
        }
        auto fn = reinterpret_cast<HasFn>(static_cast<uintptr_t>(g_fns.has_application));
        out = (fn(reinterpret_cast<void*>(jmm), team) & 1) != 0;
        return true;
    }
    bool apply_for_job(uint64_t jmm, int team, std::string& err) override {
        if (!g_fns.apply_for_job) {
            err = "function not resolved";
            return false;
        }
        auto fn = reinterpret_cast<ApplyFn>(static_cast<uintptr_t>(g_fns.apply_for_job));
        fn(reinterpret_cast<void*>(jmm), team);
        return true;
    }
    bool make_offer(uint64_t jmm, uint64_t offer, std::string& err) override {
        if (!g_fns.make_offer) {
            err = "function not resolved";
            return false;
        }
        auto fn = reinterpret_cast<OfferFn>(static_cast<uintptr_t>(g_fns.make_offer));
        fn(reinterpret_cast<void*>(jmm), reinterpret_cast<void*>(offer));
        return true;
    }
    bool today(uint64_t jmm, int& yyyymmdd, std::string& err) override {
        if (!g_fns.today_int) {
            err = "function not resolved";
            return false;
        }
        ProcessMemory mem;
        uint64_t hub = mem.ptr(jmm + jmm::kHub);
        uint64_t cal = hub ? mem.chain(hub, {jmm::kHubCalendar, 0}) : 0;
        if (!cal || !mem.ptr(cal + jmm::kCalendarDate, 1)) {
            err = "calendar not readable";
            return false;
        }
        auto fn = reinterpret_cast<TodayFn>(static_cast<uintptr_t>(g_fns.today_int));
        yyyymmdd = fn(reinterpret_cast<void*>(cal + jmm::kCalendarDate));
        return true;
    }
};

JobOfferResult run_now(uint64_t jmm, int team) {
    JobOfferResult r;
    std::string why;
    if (!job_offer_ready(&why)) {
        r.stage = "off";
        r.message = why;
    } else {
        const uint64_t seen = g_jmm.load();
        if (jmm == 0) jmm = seen;
        if (jmm == 0) {
            r.stage = "validate";
            r.message = "the career's JobMarketManager is not known yet: load a Manager Career and let a day pass";
        } else if (seen != 0 && jmm != seen) {
            r.stage = "validate";
            r.message = "JobMarketManager mismatch: Lua found " + hex(jmm) + " but the game's events come from " + hex(seen);
        } else {
            ProcessMemory mem;
            RealCaller caller;
            JobOfferRequest req;
            req.jmm = jmm;
            req.team = team;
            r = job_offer_create(mem, caller, g_fns, req);
        }
    }
    ++g_runs;
    log("game call job_offer(team %d, jmm %s): %s [%s]%s", team, hex(jmm).c_str(), r.ok ? "ok" : "failed", r.stage.c_str(),
        r.message.empty() ? "" : (" " + r.message).c_str());
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last = std::string(r.ok ? "ok: " : "failed: ") + r.message;
    }
    return r;
}

void publish(int32_t seq, const JobOfferResult& r, int32_t status) {
    publish_call_result(seq, status, r.sent_day, r.wage, r.message);
}

}  // namespace

// ---------------------------------------------------------------- public API
void publish_call_result(int32_t seq, int32_t status, int64_t out0, int64_t out1, const std::string& text) {
    const uint64_t mb = g_mailbox.load();
    if (!mb) return;
    ProcessMemory mem;
    if (!write_call_result(mem, mb, seq, status, out0, out1, text))
        log("game call: cannot write the result into the mailbox call block");
}

uint64_t jmm_seen() { return g_jmm.load(); }
long long jmm_events() { return g_jmm_events.load(); }
JobMarketFns job_market_fns() { return g_fns; }

bool job_offer_ready(std::string* why) {
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
        if (why) *why = "kill switch turbo_output\\call_job_offer_off.txt is present";
        return false;
    }
    return true;
}

std::vector<std::string> game_calls_status() {
    std::vector<std::string> out;
    std::string why;
    char line[512];
    if (job_offer_ready(&why))
        std::snprintf(line, sizeof(line),
                      "job_offer: ready | JobMarketManager %s (%lld events seen) | has_application %s, apply_for_job %s, "
                      "make_offer %s, today %s | runs %lld",
                      g_jmm.load() ? hex(g_jmm.load()).c_str() : "not seen yet", g_jmm_events.load(),
                      hex(g_fns.has_application).c_str(), hex(g_fns.apply_for_job).c_str(), hex(g_fns.make_offer).c_str(),
                      hex(g_fns.today_int).c_str(), g_runs.load());
    else
        std::snprintf(line, sizeof(line), "job_offer: off (%s)", why.c_str());
    out.push_back(line);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_last.empty()) out.push_back("  last: " + g_last);
    }
    for (const auto& l : standings_refresh_status()) out.push_back(l);
    for (const auto& l : transfer_list_status()) out.push_back(l);
    for (const auto& l : player_move_status()) out.push_back(l);
    for (const auto& l : reveal_status()) out.push_back(l);
    for (const auto& l : manager_rules_status()) out.push_back(l);
    for (const auto& l : match_setup_status()) out.push_back(l);
    return out;
}

JobOfferResult job_offer_request(uint64_t jmm, int team, int32_t seq) {
    JobOfferResult r;
    std::string why;
    if (!job_offer_ready(&why)) {
        r.stage = "off";
        r.message = why;
        publish(seq, r, kCallFailed);
        return r;
    }
    if (team <= 0) {
        r.stage = "validate";
        r.message = "team id must be a positive number";
        publish(seq, r, kCallFailed);
        return r;
    }
    const uint32_t tid = GetCurrentThreadId();
    const uint32_t game = game_thread_id();
    if (game != 0 && tid == game) {
        r = run_now(jmm, team);
        publish(seq, r, r.ok ? kCallOk : kCallFailed);
        return r;
    }
    const char* via = run_on_game_thread([jmm, team, seq]() {
        JobOfferResult rr = run_now(jmm, team);
        publish(seq, rr, rr.ok ? kCallOk : kCallFailed);
    });
    r.stage = "queued";
    r.message = std::string("queued for the game thread: it runs ") +
                (std::string(via) == "hook" ? "at the next game tick" : "on the next career-mode event (advance a day)");
    publish(seq, r, kCallQueued);
    log("game call job_offer(team %d) queued from thread %lu (game thread %lu)", team, static_cast<unsigned long>(tid),
        static_cast<unsigned long>(game));
    return r;
}

void install_game_calls(uint64_t mailbox) {
    g_mailbox = mailbox;
    if (g_installed) return;
    g_installed = true;
    // player_move (player_move_win.cpp): the game's own club change / release / contract record; needs the signature table only
    install_player_move();
    if (!game_hooks_allowed()) {
        g_off = "game hooks are off for this game build";
        log("game calls: off (%s)", g_off.c_str());
        return;
    }
    g_fns.vtable = game_signature("jmm_vtable");
    g_fns.has_application = game_signature("jmm_has_application");
    g_fns.apply_for_job = game_signature("jmm_apply_for_job");
    g_fns.make_offer = game_signature("jmm_make_offer");
    g_fns.today_int = game_signature("calendar_today_int");
    if (const char* m = g_fns.missing()) {
        g_off = std::string("signature ") + m + " was not found on this game build";
        log("game calls: job_offer off (%s)", g_off.c_str());
    } else {
        log("game calls: job_offer resolved (vtable %s, has_application %s, apply_for_job %s, make_offer %s, today %s)",
            hex(g_fns.vtable).c_str(), hex(g_fns.has_application).c_str(), hex(g_fns.apply_for_job).c_str(),
            hex(g_fns.make_offer).c_str(), hex(g_fns.today_int).c_str());
    }
    // Capture hook (pass-through): records the JobMarketManager the game passes to HandleEvent
    install_game_hook("jmm_handle_event", "jmm_handle_event", reinterpret_cast<void*>(&hk_handle),
                      reinterpret_cast<void**>(&o_handle));
}

}  // namespace host

extern "C" __declspec(dllexport) int turbo_game_call(void*) {
    using namespace host;
    HOOK_BODY("turbo_game_call", {
        const uint64_t mb = g_mailbox.load();
        if (mb) {
            ProcessMemory mem;
            turbo::GameCallBlock b;
            if (turbo::read_call_block(mem, mb, b)) {
                if (b.op == turbo::kCallOpJobOffer) {
                    job_offer_request(static_cast<uint64_t>(b.args[0]), static_cast<int>(b.args[1]), b.seq);
                } else if (b.op == turbo::kCallOpStandingsRefresh) {
                    turbo::svm::Request req;
                    req.svm = static_cast<uint64_t>(b.args[0]);
                    req.managers = static_cast<uint64_t>(b.args[1]);
                    req.comm = static_cast<uint64_t>(b.args[2]);
                    req.ifce = static_cast<uint64_t>(b.args[3]);
                    req.label = "Lua";
                    standings_refresh_request(req, b.seq);
                } else if (b.op == turbo::kCallOpTransferList) {
                    turbo::tl::Request req;
                    req.action = static_cast<int>(b.args[0]);
                    req.player = static_cast<int>(b.args[1]);
                    req.comm = static_cast<uint64_t>(b.args[2]);
                    req.club = static_cast<int>(b.args[3]);
                    transfer_list_request(req, b.seq);
                } else if (b.op == turbo::kCallOpPlayerMove) {
                    player_move_request(turbo::pm::request_from_args(b.args), b.seq);
                } else if (b.op == turbo::kCallOpReveal) {
                    turbo::pdrm::Request req;
                    req.pdrm = static_cast<uint64_t>(b.args[0]);
                    req.mode = b.args[1] == 1 ? turbo::pdrm::Mode::Team : turbo::pdrm::Mode::Player;
                    req.id = static_cast<int>(b.args[2]);
                    req.managers = static_cast<uint64_t>(b.args[3]);
                    reveal_request(req, b.seq);
                } else if (b.op == turbo::kCallOpManagerRules) {
                    manager_rules_request(b.args[0], static_cast<uint64_t>(b.args[1]), b.args[2], b.args[3], b.seq);
                } else {
                    turbo::JobOfferResult r;
                    r.message = "unknown game call op " + std::to_string(b.op);
                    publish(b.seq, r, turbo::kCallFailed);
                }
            }
        }
    });
    return 0;
}
