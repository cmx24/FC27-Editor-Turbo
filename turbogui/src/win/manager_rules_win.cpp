// FC 27 LE Turbo GUI - manager rules inside FC27.exe (see manager_rules_win.h, core/manager_rules.h, docs/re/manager_rules.md)
#include "manager_rules_win.h"

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

ManagerRulesFns g_fns;
bool g_installed = false;
std::string g_off;  // why the call is off at install time ("" = resolved)
std::atomic<uint64_t> g_com{0}, g_jsm{0};
// the last pointers Lua passed that validated (vtable + the career manager table's own slot): shown in the Status
// line while the game has not called UpdateJobSecurityScore / HandleEvent on them yet
std::atomic<uint64_t> g_com_checked{0}, g_jsm_checked{0};
std::atomic<bool> g_sack_hooked{false};
std::atomic<long long> g_com_updates{0}, g_jsm_events{0}, g_runs{0};
std::mutex g_mutex;          // g_unsack, g_last
UnsackableState g_unsack;    // off at every start: a saved career never comes back unsackable by itself
std::string g_last;

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

fs::path call_off_path() { return le_root() / "turbo_output" / "call_manager_rules_off.txt"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// ---------------------------------------------------------------- capture hooks (pass-through)
using Fn4 = void* (*)(void*, void*, void*, void*);
Fn4 o_jsm_handle = nullptr, o_com_update = nullptr, o_sack = nullptr;

void remember(std::atomic<uint64_t>& slot, uint64_t p, uint64_t vtable, const char* what) {
    if (p == slot.load() || !is_ptr(p, 8)) return;
    ProcessMemory mem;
    uint64_t vt = 0;
    if (mem.rd(p, vt) && vtable && vt == vtable) {
        slot = p;
        log("manager rules: %s seen at %s", what, hex(p).c_str());
    }
}

// JobSwitchManager::HandleEvent(this, eventId, Event*) (vtable slot 1)
void* hk_jsm_handle(void* self, void* id, void* ev, void* d) {
    void* r = o_jsm_handle(self, id, ev, d);
    if (!game_hook_enabled("jsm_handle_event")) return r;
    game_hook_called("jsm_handle_event");
    HOOK_BODY("jsm_handle_event", {
        ++g_jsm_events;
        remember(g_jsm, reinterpret_cast<uint64_t>(self), g_fns.jsm_vtable, "JobSwitchManager");
    });
    return r;
}

// ClubObjectivesManager::UpdateJobSecurityScore(this)
void* hk_com_update(void* self, void* b, void* c, void* d) {
    void* r = o_com_update(self, b, c, d);
    if (!game_hook_enabled("com_update_job_security")) return r;
    game_hook_called("com_update_job_security");
    HOOK_BODY("com_update_job_security", {
        ++g_com_updates;
        remember(g_com, reinterpret_cast<uint64_t>(self), g_fns.com_vtable, "ClubObjectivesManager");
    });
    return r;
}

// JobSwitchManager::SackManager(this): with unsackable on the original is NOT called (void function: both callers,
// HandleEvent's tail jump and the contract-ended handler, ignore the return value)
void* hk_sack(void* self, void* b, void* c, void* d) {
    if (!game_hook_enabled("jsm_sack_manager") || file_exists(call_off_path())) return o_sack(self, b, c, d);
    game_hook_called("jsm_sack_manager");
    bool refuse = false;
    HOOK_BODY("jsm_sack_manager", {
        const uint64_t jsm = reinterpret_cast<uint64_t>(self);
        remember(g_jsm, jsm, g_fns.jsm_vtable, "JobSwitchManager");
        ProcessMemory mem;
        std::string why;
        long long n = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            refuse = sack_should_refuse(mem, g_fns, jsm, g_unsack, why);
            n = g_unsack.refused;
        }
        log("manager rules: the game called JobSwitchManager::SackManager on %s: %s (%lld refused so far)", hex(jsm).c_str(), why.c_str(), n);
    });
    if (refuse) return nullptr;
    return o_sack(self, b, c, d);
}

// ---------------------------------------------------------------- the call (game thread only)
struct RealCaller : JobSecurityCaller {
    using UpdateFn = void (*)(void*);
    bool update_job_security(uint64_t com, std::string& err) override {
        if (!g_fns.update_score) {
            err = "function not resolved";
            return false;
        }
        reinterpret_cast<UpdateFn>(static_cast<uintptr_t>(g_fns.update_score))(reinterpret_cast<void*>(com));
        return true;
    }
};

ManagerRulesResult fail(const char* stage, const std::string& msg) {
    ManagerRulesResult r;
    r.stage = stage;
    r.message = msg;
    return r;
}

ManagerRulesResult run_now(int64_t sub, uint64_t addr, int64_t value, int64_t team) {
    std::string why;
    if (!manager_rules_ready(&why)) return fail("off", why);
    ProcessMemory mem;
    ManagerRulesRequest req;
    req.sub = sub;
    req.value = value;
    req.expect_team = static_cast<int>(team);
    if (sub == kMrUnsackable || sub == kMrFlags) {
        if (sub == kMrUnsackable && value != 0 && !g_sack_hooked.load())
            return fail("validate", "the SackManager hook is not installed (Status tab > Game hooks): unsackable cannot work");
        req.addr = addr ? addr : g_jsm.load();
        const uint64_t seen = g_jsm.load();
        if (addr && seen && addr != seen)
            return fail("validate", "JobSwitchManager mismatch: Lua found " + hex(addr) + " but the game's events come from " + hex(seen));
        std::lock_guard<std::mutex> lock(g_mutex);
        ManagerRulesResult r = unsackable_call(mem, g_fns, req, g_unsack);
        if (r.ok && req.addr) g_jsm_checked = req.addr;
        return r;
    }
    req.addr = addr ? addr : g_com.load();
    if (!req.addr) return fail("validate", "the career's ClubObjectivesManager is not known yet: load a Manager Career and let a day pass");
    const uint64_t seen = g_com.load();
    if (seen && req.addr != seen)
        return fail("validate", "ClubObjectivesManager mismatch: Lua found " + hex(req.addr) + " but the game's updates come from " + hex(seen));
    RealCaller game;
    ManagerRulesResult r = job_security_call(mem, game, g_fns, req);
    if (r.ok) g_com_checked = req.addr;
    return r;
}

// "0x.. (N score updates seen)" / "0x.. (Lua's, checked against manager slot 133; no game update seen yet)" / "not seen yet"
std::string seen_text(uint64_t seen, uint64_t checked, long long n, const char* what, int slot) {
    char b[200];
    if (seen)
        std::snprintf(b, sizeof(b), "%s (%lld %s seen)", hex(seen).c_str(), n, what);
    else if (checked)
        std::snprintf(b, sizeof(b), "%s (Lua's, checked against manager slot %d; no %s seen yet)", hex(checked).c_str(), slot, what);
    else
        std::snprintf(b, sizeof(b), "not seen yet (%lld %s seen; Lua's pointer is checked against manager slot %d)", n, what, slot);
    return b;
}

ManagerRulesResult run_logged(int64_t sub, uint64_t addr, int64_t value, int64_t team) {
    ManagerRulesResult r = run_now(sub, addr, value, team);
    ++g_runs;
    log("game call manager_rules(sub %lld, %s, value %lld): %s [%s] %s", static_cast<long long>(sub), hex(addr).c_str(),
        static_cast<long long>(value), r.ok ? "ok" : "failed", r.stage.c_str(), r.message.c_str());
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last = std::string(r.ok ? "ok: " : "failed: ") + r.message;
    return r;
}

}  // namespace

// ---------------------------------------------------------------- public API
ManagerRulesFns manager_rules_fns() { return g_fns; }
uint64_t com_seen() { return g_com.load(); }
uint64_t jsm_seen() { return g_jsm.load(); }
bool unsackable_on() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_unsack.on && !file_exists(call_off_path());
}

bool manager_rules_ready(std::string* why) {
    if (!g_installed) {
        if (why) *why = "manager rules are not installed";
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
        if (why) *why = "kill switch turbo_output\\call_manager_rules_off.txt is present";
        return false;
    }
    return true;
}

std::vector<std::string> manager_rules_status() {
    std::vector<std::string> out;
    std::string why;
    char line[512];
    UnsackableState st;
    std::string last;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        st = g_unsack;
        last = g_last;
    }
    if (manager_rules_ready(&why))
        std::snprintf(line, sizeof(line), "manager_rules: ready | ClubObjectivesManager %s, JobSwitchManager %s | unsackable %s (SackManager refused %lld, let through %lld) | runs %lld",
                      seen_text(g_com.load(), g_com_checked.load(), g_com_updates.load(), "score updates", mtab::kClubObjectives).c_str(),
                      seen_text(g_jsm.load(), g_jsm_checked.load(), g_jsm_events.load(), "events", mtab::kJobSwitch).c_str(), st.on ? "ON" : "off",
                      st.refused, st.passed, g_runs.load());
    else
        std::snprintf(line, sizeof(line), "manager_rules: off (%s)", why.c_str());
    out.push_back(line);
    if (!last.empty()) out.push_back("  last: " + last);
    return out;
}

ManagerRulesResult manager_rules_request(int64_t sub, uint64_t addr, int64_t value, int64_t team, int32_t seq) {
    ManagerRulesResult r;
    std::string why;
    if (!manager_rules_ready(&why)) {
        r.stage = "off";
        r.message = why;
        publish_call_result(seq, kCallFailed, 0, 0, r.message);
        return r;
    }
    const uint32_t tid = GetCurrentThreadId();
    const uint32_t game = game_thread_id();
    if (game != 0 && tid == game) {
        r = run_logged(sub, addr, value, team);
        publish_call_result(seq, r.ok ? kCallOk : kCallFailed, r.out0, r.out1, r.message);
        return r;
    }
    const char* via = run_on_game_thread([sub, addr, value, team, seq]() {
        ManagerRulesResult rr = run_logged(sub, addr, value, team);
        publish_call_result(seq, rr.ok ? kCallOk : kCallFailed, rr.out0, rr.out1, rr.message);
    });
    r.stage = "queued";
    r.message = std::string("queued for the game thread: it runs ") +
                (std::string(via) == "hook" ? "at the next game tick" : "on the next career-mode event (advance a day)");
    publish_call_result(seq, kCallQueued, 0, 0, r.message);
    log("game call manager_rules(sub %lld) queued from thread %lu (game thread %lu)", static_cast<long long>(sub),
        static_cast<unsigned long>(tid), static_cast<unsigned long>(game));
    return r;
}

void install_manager_rules() {
    if (g_installed) return;
    g_installed = true;
    if (!game_hooks_allowed()) {
        g_off = "game hooks are off for this game build";
        log("manager rules: off (%s)", g_off.c_str());
        return;
    }
    g_fns.com_vtable = game_signature("com_vtable");
    g_fns.jsm_vtable = game_signature("jsm_vtable");
    g_fns.update_score = game_signature("com_update_job_security");
    g_fns.sack_manager = game_signature("jsm_sack_manager");
    if (const char* m = g_fns.missing()) {
        g_off = std::string("signature ") + m + " was not found on this game build";
        log("manager rules: off (%s)", g_off.c_str());
        return;  // no hook either: the capture hooks check the vtables, the sack hook needs the whole set
    }
    log("manager rules: resolved (com_vtable %s, jsm_vtable %s, update_score %s, sack_manager %s)", hex(g_fns.com_vtable).c_str(),
        hex(g_fns.jsm_vtable).c_str(), hex(g_fns.update_score).c_str(), hex(g_fns.sack_manager).c_str());
    install_game_hook("jsm_handle_event", "jsm_handle_event", reinterpret_cast<void*>(&hk_jsm_handle), reinterpret_cast<void**>(&o_jsm_handle));
    install_game_hook("com_update_job_security", "com_update_job_security", reinterpret_cast<void*>(&hk_com_update),
                      reinterpret_cast<void**>(&o_com_update));
    g_sack_hooked = install_game_hook("jsm_sack_manager", "jsm_sack_manager", reinterpret_cast<void*>(&hk_sack), reinterpret_cast<void**>(&o_sack));
}

}  // namespace host
