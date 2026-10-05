// FC 27 LE Turbo GUI - "player_create" game call inside FC27.exe (see player_create_win.h, core/player_create.h,
// docs/re/created_players.md)
#include "player_create_win.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "game_calls_win.h"
#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

pc::Fns g_fns;
bool g_installed = false;
std::string g_off;  // why the call is off at install time ("" = resolved)
std::mutex g_mutex;
std::string g_last;  // last outcome (Status tab)
std::atomic<long long> g_runs{0}, g_queued{0}, g_ok{0}, g_refused_busy{0};
std::atomic<bool> g_busy{false};
std::atomic<long long> g_busy_since{0};
constexpr long long kBusyStaleMs = 60000;
// the game's own name for the allocation (the allocator may keep the pointer: a literal of this DLL, which is never unloaded)
const char kAllocName[] = "DataController::CreatePlayer";

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
        log("game call player_create: taking over a request that never ran (older than %lld s)", kBusyStaleMs / 1000);
        return true;
    }
    return false;
}

template <class F> F fn_at(uint64_t a) { return reinterpret_cast<F>(static_cast<uintptr_t>(a)); }

// ---------------------------------------------------------------- the real calls (game thread only)
struct RealCaller : pc::Caller {
    using InTeamFn = uint8_t (*)(void*, int, int);
    using CountsFn = void (*)(void*, int, int*, int*, int*);
    using LeagueFn = int (*)(void*, int);
    using InternationalFn = uint8_t (*)(int);
    using MovedFn = void (*)(void*, int, int, int);
    using AddContractFn = void (*)(void*, int, int, int, int, int, const void*, int);
    using ReleaseFn = int (*)(void*, int);
    using InitFn = void (*)(void*, int, const char*);
    using SetIntFn = void (*)(void*, const char*, int);
    using SetStrFn = void (*)(void*, const char*, const char*);
    using SelectFn = void (*)(void*, const char*);
    using WhereFn = void (*)(void*, const char*, uint8_t, int);
    using DestroyFn = void (*)(void*);
    using FreeFn = void (*)(void**);
    using ExecFn = uint8_t (*)(void*, void*, void**);
    using CountFn = int (*)(void*);
    using GetIntFn = int (*)(void*, int, const char*);
    using AllocFn = void* (*)(void*, uint64_t, const char*, int);
    using PostFn = void (*)(void*, int, void*);
    using InsertTpFn = int (*)(void*, int, int, int, int, uint8_t);

    // ---- op 11's calls
    bool in_team(uint64_t dc, int pid, int team, bool& in, std::string& err) override {
        if (!g_fns.move.is_player_in_team) return (err = "function not resolved", false);
        in = (fn_at<InTeamFn>(g_fns.move.is_player_in_team)(reinterpret_cast<void*>(dc), pid, team) & 1) != 0;
        return true;
    }
    bool squad_counts(uint64_t dc, int team, int& rows, int& loaned, int& pending, std::string& err) override {
        if (!g_fns.move.squad_counts) return (err = "function not resolved", false);
        rows = loaned = pending = -1;
        fn_at<CountsFn>(g_fns.move.squad_counts)(reinterpret_cast<void*>(dc), team, &rows, &loaned, &pending);
        return true;
    }
    bool league_of_team(uint64_t dc, int team, int& league, std::string& err) override {
        if (!g_fns.move.league_of_team) return (err = "function not resolved", false);
        league = fn_at<LeagueFn>(g_fns.move.league_of_team)(reinterpret_cast<void*>(dc), team);
        return true;
    }
    bool is_international(int league, bool& out, std::string& err) override {
        if (!g_fns.move.is_international) return (err = "function not resolved", false);
        out = (fn_at<InternationalFn>(g_fns.move.is_international)(league) & 1) != 0;
        return true;
    }
    bool player_moved(uint64_t team_util, int pid, int from, int to, std::string& err) override {
        if (!g_fns.move.player_moved) return (err = "function not resolved", false);
        fn_at<MovedFn>(g_fns.move.player_moved)(reinterpret_cast<void*>(team_util), pid, from, to);
        return true;
    }
    bool add_contract(uint64_t pcm, int pid, int team, int months, int wage, int k, uint64_t date, int status, std::string& err) override {
        if (!g_fns.move.add_contract) return (err = "function not resolved", false);
        fn_at<AddContractFn>(g_fns.move.add_contract)(reinterpret_cast<void*>(pcm), pid, team, months, wage, k, reinterpret_cast<const void*>(date), status);
        return true;
    }
    bool release_player(uint64_t, int, int&, std::string& err) override {
        err = "player_create never releases";
        return false;
    }

    // ---- the query layer: a Query (0x60 bytes) on this stack, never copied; Execute through the provider; the holder released
    struct Query {
        alignas(16) uint8_t bytes[0x100];
    };
    static bool begin(Query& q, int type, const std::string& table, std::string& err) {
        if (!g_fns.query_init || !g_fns.query_set_int || !g_fns.query_set_string || !g_fns.query_select || !g_fns.query_where_int || !g_fns.query_destroy ||
            !g_fns.result_free || !g_fns.provider_execute) {
            err = "query functions not resolved";
            return false;
        }
        std::memset(q.bytes, 0, sizeof(q.bytes));
        fn_at<InitFn>(g_fns.query_init)(q.bytes, type, table.c_str());
        return true;
    }
    static void set(Query& q, const pc::Row& cols) {
        for (const auto& v : cols) {
            if (v.is_string)
                fn_at<SetStrFn>(g_fns.query_set_string)(q.bytes, v.column.c_str(), v.s.c_str());
            else
                fn_at<SetIntFn>(g_fns.query_set_int)(q.bytes, v.column.c_str(), static_cast<int>(v.i));
        }
    }
    static void where(Query& q, const pc::Where& w) {
        for (const auto& c : w) fn_at<WhereFn>(g_fns.query_where_int)(q.bytes, c.first.c_str(), 0, c.second);
    }
    // Execute, then `use(holder)` while the result is open; the holder and the query are always released
    template <class U> static bool execute(uint64_t dc, Query& q, std::string& err, U use) {
        ProcessMemory mem;
        const uint64_t provider = mem.ptr(dc);
        void* holder = nullptr;
        bool ok = false;
        if (!provider) {
            err = "the DataController has no db provider";
        } else {
            fn_at<ExecFn>(g_fns.provider_execute)(reinterpret_cast<void*>(provider), q.bytes, &holder);
            if (!holder) {
                err = "the provider returned no result";
            } else {
                use(holder);
                ok = true;
            }
        }
        fn_at<FreeFn>(g_fns.result_free)(&holder);
        fn_at<DestroyFn>(g_fns.query_destroy)(q.bytes);
        return ok;
    }
    static int count_of(void* holder) {
        void** vt = *reinterpret_cast<void***>(holder);
        return reinterpret_cast<CountFn>(vt[1])(holder);
    }
    bool insert_row(uint64_t dc, const std::string& table, const pc::Row& cols, int& count, std::string& err) override {
        Query q;
        if (!begin(q, 4, table, err)) return false;
        set(q, cols);
        count = -1;
        return execute(dc, q, err, [&](void* h) { count = count_of(h); });
    }
    bool update_row(uint64_t dc, const std::string& table, const pc::Row& cols, const pc::Where& w, std::string& err) override {
        Query q;
        if (!begin(q, 3, table, err)) return false;
        set(q, cols);
        where(q, w);
        return execute(dc, q, err, [](void*) {});
    }
    bool select_ints(uint64_t dc, const std::string& table, const std::vector<std::string>& cols, const pc::Where& w, int& rows, std::vector<int64_t>& values,
                     std::string& err) override {
        Query q;
        if (!begin(q, 1, table, err)) return false;
        for (const auto& c : cols) fn_at<SelectFn>(g_fns.query_select)(q.bytes, c.c_str());
        where(q, w);
        rows = -1;
        values.clear();
        return execute(dc, q, err, [&](void* h) {
            rows = count_of(h);
            if (rows <= 0) return;
            void** vt = *reinterpret_cast<void***>(h);
            for (const auto& c : cols) values.push_back(reinterpret_cast<GetIntFn>(vt[2])(h, 0, c.c_str()));
        });
    }
    bool alloc_event(uint64_t size, uint64_t& ev, std::string& err) override {
        ProcessMemory mem;
        const uint64_t alloc = g_fns.event_allocator ? mem.ptr(g_fns.event_allocator) : 0, vt = alloc ? mem.ptr(alloc) : 0;
        uint64_t fn = 0;
        if (!vt || !mem.rd(vt + 0x10, fn) || !fn) return (err = "the event allocator is not readable", false);
        ev = reinterpret_cast<uint64_t>(fn_at<AllocFn>(fn)(reinterpret_cast<void*>(alloc), size, kAllocName, 0));
        return true;
    }
    bool post_event(uint64_t dispatcher, int id, uint64_t ev, std::string& err) override {
        if (!g_fns.post_event) return (err = "function not resolved", false);
        fn_at<PostFn>(g_fns.post_event)(reinterpret_cast<void*>(dispatcher), id, reinterpret_cast<void*>(ev));
        return true;
    }
    bool insert_team_player(uint64_t dc, int pid, int team, int jersey, int position, int suppress, int& ret, std::string& err) override {
        if (!g_fns.insert_team_player) return (err = "function not resolved", false);
        ret = fn_at<InsertTpFn>(g_fns.insert_team_player)(reinterpret_cast<void*>(dc), pid, team, jersey, position, static_cast<uint8_t>(suppress != 0));
        return true;
    }
};

pc::Result run_now(pc::Request req) {
    pc::Result r;
    std::string why;
    if (!player_create_ready(&why)) {
        r.stage = "off";
        r.message = "player_create: off (" + why + ")";
    } else {
        if (!req.image_base) req.image_base = game_image_base();
        if (!req.image_size) req.image_size = game_image_size();
        ProcessMemory mem;
        RealCaller caller;
        r = pc::run(mem, caller, g_fns, req);
    }
    ++g_runs;
    if (r.ok) ++g_ok;
    std::string calls;
    for (const auto& c : r.calls) calls += (calls.empty() ? "" : ", ") + c;
    log("game call player_create(%s, player %d, seq %d, team %d, months %d, wage %d, %llu players columns, %llu names, comm %s): %s [%s] %s (written 0x%X, in_team %d, "
        "dc %s; calls: %s)",
        pc::action_name(req.action), req.player, req.seq, req.payload.team, req.payload.months, req.payload.wage,
        static_cast<unsigned long long>(req.payload.players.size()), static_cast<unsigned long long>(req.payload.names.size()),
        hex(req.comm).c_str(), r.ok ? "ok" : "failed", r.stage.c_str(), r.message.c_str(), static_cast<unsigned>(r.written), r.in_team, hex(r.at.dc).c_str(),
        calls.c_str());
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last = std::string(r.ok ? "ok: " : "failed: ") + r.message;
    }
    return r;
}

void publish(int32_t seq, const pc::Result& r, int32_t status) {
    if (seq == 0) return;
    // out[0]: -1 = the call is off (nothing was called: Lua keeps its database path), else the written mask
    publish_call_result(seq, status, r.stage == "off" ? pc::kOutOff : static_cast<int64_t>(r.written), r.in_team, r.message);
}

}  // namespace

// ---------------------------------------------------------------- public API
pc::Fns player_create_fns() { return g_fns; }

bool player_create_ready(std::string* why) {
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
    if (!pc::opted_in(out_dir())) {
        if (why) *why = std::string("opt-in: create turbo_output\\") + pc::opt_in_name();
        return false;
    }
    if (pc::killed(out_dir())) {
        if (why) *why = std::string("kill switch turbo_output\\") + pc::kill_switch_name() + " is present";
        return false;
    }
    return true;
}

std::vector<std::string> player_create_status() {
    std::vector<std::string> out;
    std::string why;
    char line[768];
    if (player_create_ready(&why)) {
        std::snprintf(line, sizeof(line),
                      "player_create: ready (opted in) | query init %s, execute %s, insert_team_player %s, post_event %s, allocator %s, 0x3A vtable %s | runs %lld (ok "
                      "%lld, queued %lld, busy %lld)",
                      hex(g_fns.query_init).c_str(), hex(g_fns.provider_execute).c_str(), hex(g_fns.insert_team_player).c_str(), hex(g_fns.post_event).c_str(),
                      hex(g_fns.event_allocator).c_str(), hex(g_fns.inserted_vtable).c_str(), g_runs.load(), g_ok.load(), g_queued.load(), g_refused_busy.load());
    } else {
        std::snprintf(line, sizeof(line), "player_create: off (%s)", why.c_str());
    }
    out.push_back(line);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_last.empty()) out.push_back("  last: " + g_last);
    return out;
}

pc::Result player_create_request(pc::Request req, int32_t seq) {
    pc::Result r;
    std::string why;
    if (!player_create_ready(&why)) {
        r.stage = "off";
        r.message = "player_create: off (" + why + ")";
        publish(seq, r, kCallFailed);
        return r;
    }
    if (!req.bad_args.empty() || !pc::valid_action(req.action)) {
        r.stage = "validate";
        r.message = !req.bad_args.empty() ? req.bad_args : "unknown player_create code " + std::to_string(req.action) + " (1 create, 9 check only)";
        publish(seq, r, kCallFailed);
        return r;
    }
    // the payload Lua wrote for this request, and the database's columns: read now (the file belongs to this request number)
    pc::load_payload(out_dir(), req);
    if (!take_gate()) {
        ++g_refused_busy;
        r.stage = "busy";
        r.message = "another player_create call is still queued or running: wait for its result";
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
        pc::Result rr = run_now(req);
        g_busy = false;
        publish(seq, rr, rr.ok ? kCallOk : kCallFailed);
    });
    r.stage = "queued";
    r.message = std::string("queued for the game thread: it runs ") +
                (std::string(via) == "hook" ? "at the next game tick"
                 : std::string(via) == "lua" ? "on the next career-mode event (advance a day)"
                                            : "when the game-thread dispatcher is available");
    publish(seq, r, kCallQueued);
    log("game call player_create(%s, player %d, seq %d) queued from thread %lu (game thread %lu, via %s)", pc::action_name(req.action), req.player, req.seq,
        static_cast<unsigned long>(tid), static_cast<unsigned long>(game), via && *via ? via : "none yet");
    return r;
}

void install_player_create() {
    if (g_installed) return;
    g_installed = true;
    if (!game_hooks_allowed()) {
        g_off = "game hooks are off for this game build";
        log("game calls: player_create off (%s)", g_off.c_str());
        return;
    }
    g_fns.move.player_moved = game_signature("teamutil_player_moved");
    g_fns.move.is_player_in_team = game_signature("dc_is_player_in_team");
    g_fns.move.squad_counts = game_signature("dc_squad_counts");
    g_fns.move.league_of_team = game_signature("dc_get_league_of_team");
    g_fns.move.is_international = game_signature("is_international_league");
    g_fns.move.add_contract = game_signature("pcm_add_contract_record");
    g_fns.move.release_player = game_signature("ctm_release_player");
    g_fns.move.pcm_vtable = game_signature("pcm_vtable");
    g_fns.move.tm_vtable = game_signature("tm_vtable");
    g_fns.move.um_vtable = game_signature("um_vtable");
    g_fns.move.ctm_vtable = game_signature("ctm_vtable");
    g_fns.move.morale_vtable = game_signature("morale_vtable");
    g_fns.move.morale_handle_event = game_signature("morale_handle_event");
    g_fns.query_init = game_signature("db_query_init");
    g_fns.query_set_int = game_signature("db_query_set_int");
    g_fns.query_set_string = game_signature("db_query_set_string");
    g_fns.query_select = game_signature("db_query_select_field");
    g_fns.query_where_int = game_signature("db_query_where_int");
    g_fns.query_destroy = game_signature("db_query_destroy");
    g_fns.result_free = game_signature("db_result_free");
    g_fns.provider_execute = game_signature("db_provider_execute");
    g_fns.event_allocator = game_signature("event_allocator_global");
    g_fns.event_base_vtable = game_signature("event_base_vtable");
    g_fns.inserted_vtable = game_signature("player_inserted_event_vtable");
    g_fns.insert_team_player = game_signature("dc_insert_team_player");
    g_fns.post_event = game_signature("post_career_event");
    if (const char* m = g_fns.missing()) {
        g_off = std::string("signature ") + m + " was not found on this game build";
        log("game calls: player_create off (%s)", g_off.c_str());
        return;
    }
    log("game calls: player_create resolved (Query::Init %s, SetInt %s, SetString %s, Select %s, Where %s, ~Query %s, Release %s, Execute %s, allocator %s, "
        "event vtables %s / %s, InsertTeamPlayer %s, PostEvent %s); off until turbo_output\\%s exists (kill switch turbo_output\\%s)",
        hex(g_fns.query_init).c_str(), hex(g_fns.query_set_int).c_str(), hex(g_fns.query_set_string).c_str(), hex(g_fns.query_select).c_str(),
        hex(g_fns.query_where_int).c_str(), hex(g_fns.query_destroy).c_str(), hex(g_fns.result_free).c_str(), hex(g_fns.provider_execute).c_str(),
        hex(g_fns.event_allocator).c_str(), hex(g_fns.event_base_vtable).c_str(), hex(g_fns.inserted_vtable).c_str(), hex(g_fns.insert_team_player).c_str(),
        hex(g_fns.post_event).c_str(), pc::opt_in_name(), pc::kill_switch_name());
}

}  // namespace host
