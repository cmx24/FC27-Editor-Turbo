// FC 27 LE Turbo GUI - match setup and gameplay switches inside FC27.exe (see match_setup_win.h, core/match_setup.h,
// docs/re/match_setup.md)
#include "match_setup_win.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>

#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

gv::Fns g_fns;
uint64_t g_sched = 0, g_standings = 0;  // the two FCE HandleMessage functions
bool g_installed = false;
std::string g_off;      // why the variables are off at install time ("" = resolved)
std::string g_fix_off;  // why result fixing is off at install time
std::mutex g_mutex;     // guards everything below
std::map<std::string, gv::Override> g_vars;
std::deque<msetup::VarResult> g_results;
std::string g_last;
long long g_runs = 0;
constexpr size_t kMaxResults = 32;

// result fixing: the table the hooks read (swapped whole), per-fixture counts, the hooks
std::shared_ptr<const mfix::FixTable> g_fix_table;  // std::atomic_load / atomic_store only
std::map<uint16_t, long long> g_fix_counts;          // guarded by g_mutex
std::atomic<long long> g_fix_applied{0}, g_fix_skipped{0};
std::atomic<bool> g_hooks_tried{false};
bool g_hooks_ok = false;  // guarded by g_mutex
std::string g_fix_last;   // guarded by g_mutex

using HandleFn = uint64_t (*)(void*, void*);
HandleFn g_orig_sched = nullptr, g_orig_standings = nullptr;

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

fs::path vars_off_path() { return le_root() / "turbo_output" / "call_gamevar_off.txt"; }
fs::path fix_off_path() { return le_root() / "turbo_output" / "call_match_fix_off.txt"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// A kill switch file, looked at no more than every 2 s (the UI asks every frame, the hooks on every result message)
struct Switch {
    std::atomic<long long> next{0};
    std::atomic<bool> on{false};
    bool present(const fs::path& p) {
        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now >= next.load()) {
            on = file_exists(p);
            next = now + 2000;
        }
        return on.load();
    }
};
Switch g_vars_switch, g_fix_switch;

// ---------------------------------------------------------------- the game call and the lock (game thread)
struct RealCaller : gv::Caller {
    using SetIntFn = void (*)(void*, const char*, int);
    bool set_int(const gv::Fns& fns, const std::string& name, int32_t value, std::string& err) override {
        if (!fns.set_int || !fns.object) {
            err = "function not resolved";
            return false;
        }
        auto fn = reinterpret_cast<SetIntFn>(static_cast<uintptr_t>(fns.set_int));
        fn(reinterpret_cast<void*>(static_cast<uintptr_t>(fns.object)), name.c_str(), value);
        return true;
    }
    // the game's writer protocol (0x140D9BCA8): the word is 0x01000000 with no reader and no writer; a writer takes
    // all of it at once. Turbo only takes it when it is free (compare-exchange), spinning a bounded while.
    bool try_lock(uint64_t lock) override {
        auto* w = reinterpret_cast<volatile LONG*>(static_cast<uintptr_t>(lock));
        for (int i = 0; i < 20000; ++i) {
            if (InterlockedCompareExchange(w, 0, LONG(gv::kLockFree)) == LONG(gv::kLockFree)) return true;
            if (i > 64) SwitchToThread();
            else YieldProcessor();
        }
        return false;
    }
    void unlock(uint64_t lock) override {
        auto* w = reinterpret_cast<volatile LONG*>(static_cast<uintptr_t>(lock));
        InterlockedExchangeAdd(w, LONG(gv::kLockFree));
    }
};

void remember(const msetup::VarResult& r) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last = std::string(r.ok ? "ok: " : "failed: ") + r.message;
    g_results.push_back(r);
    while (g_results.size() > kMaxResults) g_results.pop_front();
}

bool vars_ready_impl(std::string* why) {
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
    if (g_vars_switch.present(vars_off_path())) {
        if (why) *why = "kill switch turbo_output\\call_gamevar_off.txt is present";
        return false;
    }
    return true;
}

bool fixing_ready_impl(std::string* why) {
    if (!g_installed) {
        if (why) *why = "game calls are not installed";
        return false;
    }
    if (!game_hooks_allowed()) {
        if (why) *why = "game hooks are off for this game build (Status tab > Game hooks)";
        return false;
    }
    if (!g_fix_off.empty()) {
        if (why) *why = g_fix_off;
        return false;
    }
    if (g_fix_switch.present(fix_off_path())) {
        if (why) *why = "kill switch turbo_output\\call_match_fix_off.txt is present";
        return false;
    }
    return true;
}

// One variable job (game thread)
void run_var(const std::string& name, int32_t value, bool clear_it) {
    msetup::VarResult r;
    r.name = name;
    r.value = value;
    r.cleared = clear_it;
    std::string why;
    if (!vars_ready_impl(&why)) {
        r.message = name + ": " + why;
        remember(r);
        return;
    }
    ProcessMemory mem;
    RealCaller caller;
    std::string err;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        gv::Override& ov = g_vars[name];
        if (ov.name.empty()) ov.name = name;
        if (clear_it) {
            err = gv::clear(mem, caller, g_fns, ov);
            r.value = ov.game_declared ? ov.previous : 0;
        } else {
            err = gv::apply(mem, caller, g_fns, name, value, ov);
        }
        ++g_runs;
    }
    r.ok = err.empty();
    if (r.ok) r.message = clear_it ? name + " cleared (the game decides again)" : name + " = " + std::to_string(value);
    else r.message = name + ": " + err;
    log("game call gamevar %s %s: %s", clear_it ? "clear" : "set", clear_it ? name.c_str() : (name + " = " + std::to_string(value)).c_str(),
        r.ok ? "ok" : err.c_str());
    remember(r);
}

bool queue_var(const std::string& name, int32_t value, bool clear_it, std::string& why) {
    if (!vars_ready_impl(&why)) return false;
    if (!clear_it) {
        why = gv::check_name(name);
        if (!why.empty()) return false;
    }
    const uint32_t game = game_thread_id();
    if (game != 0 && GetCurrentThreadId() == game) {
        run_var(name, value, clear_it);
        return true;
    }
    const char* via = run_on_game_thread([name, value, clear_it]() { run_var(name, value, clear_it); });
    if (!via || !*via) log("game call gamevar %s queued: no game-thread dispatcher yet (runs when one is available)", name.c_str());
    return true;
}

// ---------------------------------------------------------------- result hooks (FCE's thread)
void rewrite(const char* hook, void* self, void* msg, uint64_t vtable_rva) {
    std::shared_ptr<const mfix::FixTable> fixes = std::atomic_load(&g_fix_table);
    if (!fixes || fixes->empty()) return;
    ProcessMemory mem;
    int32_t type = 0;
    if (!mem.rd(reinterpret_cast<uint64_t>(msg) + mfix::kOffType, type) || type != mfix::kRequestType) return;
    if (g_fix_switch.present(fix_off_path())) return;
    const uint64_t base = game_image_base();
    mfix::Applied a;
    if (!mfix::apply_to_request(mem, reinterpret_cast<uint64_t>(self), base ? base + vtable_rva : 0, reinterpret_cast<uint64_t>(msg),
                                *fixes, a))
        return;
    char line[320];
    if (!a.skipped.empty()) {
        ++g_fix_skipped;
        std::snprintf(line, sizeof(line), "%s: fixture %u left as played (%d-%d): %s", hook, unsigned(a.fixture), a.old_home, a.old_away,
                      a.skipped.c_str());
    } else {
        if (a.changed) ++g_fix_applied;
        std::snprintf(line, sizeof(line), "%s: fixture %u %d-%d -> %d-%d%s", hook, unsigned(a.fixture), a.old_home, a.old_away, a.home,
                      a.away, a.changed ? "" : " (already that score)");
    }
    log("result fixing %s", line);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (a.changed) ++g_fix_counts[a.fixture];
    g_fix_last = line;
}

uint64_t hk_sched(void* self, void* msg) {
    game_hook_called("fce_result_sched");
    if (game_hook_enabled("fce_result_sched")) HOOK_BODY("fce_result_sched", rewrite("scheduling", self, msg, mfix::kRvaSchedulingVtable));
    return g_orig_sched(self, msg);
}

uint64_t hk_standings(void* self, void* msg) {
    game_hook_called("fce_result_standings");
    if (game_hook_enabled("fce_result_standings")) HOOK_BODY("fce_result_standings", rewrite("standings", self, msg, mfix::kRvaStandingsVtable));
    return g_orig_standings(self, msg);
}

// Install both hooks once (the first fix). Both or none: a single one would let fixture and table disagree
bool ensure_hooks(std::string& why) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_hooks_ok) return true;
    }
    if (g_hooks_tried.exchange(true)) {
        why = "the result hooks could not be installed (see turbo_gui.log)";
        return false;
    }
    bool a = install_game_hook_at("fce_result_sched", reinterpret_cast<void*>(static_cast<uintptr_t>(g_sched)), reinterpret_cast<void*>(&hk_sched),
                                  reinterpret_cast<void**>(&g_orig_sched));
    bool b = a && install_game_hook_at("fce_result_standings", reinterpret_cast<void*>(static_cast<uintptr_t>(g_standings)),
                                       reinterpret_cast<void*>(&hk_standings), reinterpret_cast<void**>(&g_orig_standings));
    if (!a || !b) {
        why = "the result hooks could not be installed (see turbo_gui.log)";
        log("result fixing: hooks not installed (scheduling %s, standings %s): fixes stay inactive", a ? "ok" : "failed", b ? "ok" : "failed");
        // a lone scheduling hook does nothing without a fix table entry; the table is cleared below so it stays idle
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hooks_ok = true;
    return true;
}

void publish_table(const mfix::FixTable& t) { std::atomic_store(&g_fix_table, std::make_shared<const mfix::FixTable>(t)); }

mfix::FixTable copy_table() {
    std::shared_ptr<const mfix::FixTable> cur = std::atomic_load(&g_fix_table);
    return cur ? *cur : mfix::FixTable();
}

struct HostService : msetup::Service {
    bool set_var(const std::string& name, int32_t value, std::string& why) override { return queue_var(name, value, false, why); }
    bool clear_var(const std::string& name, std::string& why) override { return queue_var(name, 0, true, why); }
    bool clear_all(std::string& why) override {
        std::vector<std::string> names;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (const auto& kv : g_vars)
                if (kv.second.active) names.push_back(kv.first);
        }
        for (const auto& n : names)
            if (!queue_var(n, 0, true, why)) return false;
        return true;
    }
    bool poll(msetup::VarResult& out) override {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_results.empty()) return false;
        out = g_results.front();
        g_results.pop_front();
        return true;
    }
    std::vector<msetup::VarLine> vars() override {
        std::vector<msetup::VarLine> out;
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& kv : g_vars) {
            msetup::VarLine l;
            l.name = kv.first;
            l.value = kv.second.value;
            l.previous = kv.second.previous;
            l.active = kv.second.active;
            l.game_declared = kv.second.game_declared;
            out.push_back(l);
        }
        return out;
    }
    bool fix(uint16_t fixture, int home, int away, const std::string& label, std::string& why) override {
        if (!fixing_ready_impl(&why)) return false;
        mfix::FixTable t = copy_table();
        why = t.set(fixture, home, away, label);
        if (!why.empty()) return false;
        if (!ensure_hooks(why)) return false;
        publish_table(t);
        log("result fixing: fixture %u set to %d-%d (%s)", unsigned(fixture), home, away, label.c_str());
        return true;
    }
    bool unfix(uint16_t fixture) override {
        mfix::FixTable t = copy_table();
        if (!t.erase(fixture)) return false;
        publish_table(t);
        log("result fixing: fixture %u removed", unsigned(fixture));
        return true;
    }
    void unfix_all() override {
        publish_table(mfix::FixTable());
        log("result fixing: every fix removed");
    }
    std::vector<mfix::Fix> fixes() override {
        std::vector<mfix::Fix> out = copy_table().all();
        std::lock_guard<std::mutex> lock(g_mutex);
        for (mfix::Fix& f : out) {
            auto it = g_fix_counts.find(f.fixture);
            f.applied = it == g_fix_counts.end() ? 0 : it->second;
        }
        return out;
    }
    long long fixes_applied() override { return g_fix_applied.load(); }
    bool fixing_ready(std::string* why) override { return fixing_ready_impl(why); }
    bool vars_ready(std::string* why) override { return vars_ready_impl(why); }
    std::string status() override {
        std::string s;
        for (const auto& l : match_setup_status()) s += (s.empty() ? "" : " | ") + l;
        return s;
    }
};

}  // namespace

std::shared_ptr<msetup::Service> match_setup_service() {
    static std::shared_ptr<msetup::Service> s = std::make_shared<HostService>();
    return s;
}

std::vector<std::string> match_setup_status() {
    std::vector<std::string> out;
    std::string why;
    char line[640];
    size_t active = 0;
    long long runs = 0;
    std::string last, fix_last;
    bool hooks = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& kv : g_vars) active += kv.second.active ? 1 : 0;
        runs = g_runs;
        last = g_last;
        fix_last = g_fix_last;
        hooks = g_hooks_ok;
    }
    if (vars_ready_impl(&why))
        std::snprintf(line, sizeof(line), "gamevar: ready | store %s, table %s, SetInt %s, lock %s | %zu override(s) active, %lld run(s)",
                      hex(g_fns.object).c_str(), hex(g_fns.table_slot).c_str(), hex(g_fns.set_int).c_str(), hex(g_fns.lock).c_str(), active, runs);
    else
        std::snprintf(line, sizeof(line), "gamevar: off (%s)", why.c_str());
    out.push_back(line);
    if (!last.empty()) out.push_back("  last: " + last);
    std::shared_ptr<const mfix::FixTable> t = std::atomic_load(&g_fix_table);
    if (fixing_ready_impl(&why))
        std::snprintf(line, sizeof(line), "match_fix: %s | scheduling %s, standings %s | %zu fix(es), %lld result(s) rewritten, %lld left as played",
                      hooks ? "hooks active" : "ready (hooks are installed on the first fix)", hex(g_sched).c_str(), hex(g_standings).c_str(),
                      t ? t->size() : size_t(0), g_fix_applied.load(), g_fix_skipped.load());
    else
        std::snprintf(line, sizeof(line), "match_fix: off (%s)", why.c_str());
    out.push_back(line);
    if (!fix_last.empty()) out.push_back("  last: " + fix_last);
    return out;
}

void install_match_setup() {
    if (g_installed) return;
    g_installed = true;
    if (!game_hooks_allowed()) {
        g_off = g_fix_off = "game hooks are off for this game build";
        log("game calls: gamevar / match_fix off (%s)", g_off.c_str());
        return;
    }
    g_fns.get_int = game_signature("gamevar_get_int");
    g_fns.set_int = game_signature("gamevar_set_int");
    g_fns.object = game_signature("gamevar_object");
    g_fns.table_slot = game_signature("gamevar_table_slot");
    g_fns.lock = game_signature("gamevar_lock");
    g_fns.set_lock = game_signature("gamevar_set_lock");
    if (const char* m = g_fns.missing()) g_off = std::string("signature ") + m + " was not found on this game build";
    else g_off = g_fns.inconsistent();
    if (g_off.empty()) {
        ProcessMemory mem;
        gv::Table t;
        std::string err = gv::locate(mem, g_fns.table_slot, t);
        if (err.empty())
            log("game calls: gamevar resolved (store %s, table %s with %u entries, SetInt %s, lock %s; kill switch "
                "turbo_output\\call_gamevar_off.txt)",
                hex(g_fns.object).c_str(), hex(t.addr).c_str(), t.count, hex(g_fns.set_int).c_str(), hex(g_fns.lock).c_str());
        else
            log("game calls: gamevar resolved, the table is not readable yet (%s): it is checked again before every call", err.c_str());
    } else {
        log("game calls: gamevar off (%s)", g_off.c_str());
    }
    g_sched = game_signature("fce_sched_handle_message");
    g_standings = game_signature("fce_standings_handle_message");
    if (!g_sched || !g_standings) {
        g_fix_off = std::string("signature ") + (!g_sched ? "fce_sched_handle_message" : "fce_standings_handle_message") +
                    " was not found on this game build";
        log("game calls: match_fix off (%s)", g_fix_off.c_str());
    } else {
        log("game calls: match_fix resolved (scheduling HandleMessage %s, standings HandleMessage %s; hooks installed on the first fix; kill "
            "switch turbo_output\\call_match_fix_off.txt)",
            hex(g_sched).c_str(), hex(g_standings).c_str());
    }
}

}  // namespace host
