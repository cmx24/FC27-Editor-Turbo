// FC 27 LE Turbo GUI - the spoken set asked from the game's audio service, inside FC27.exe (see commentary_audio_win.h,
// core/commentary_audio.h, docs/callnames.md section 5)
#include "commentary_audio_win.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>

#include "core/commentary_audio.h"
#include "game_hooks.h"
#include "host.h"
#include "ui/app.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;
using namespace turbo::caudio;

namespace {

Fns g_fns;
bool g_installed = false;
std::string g_off;          // why the call is off at install time ("" = the surname path is resolved)
std::string g_players_off;  // why the player path is off ("" = resolved)
std::atomic<long long> g_runs{0};
std::atomic<long long> g_steps{0};
std::atomic<long long> g_probes{0};
std::mutex g_mutex;
std::string g_last;        // outcome of the last build (Status tab)
std::string g_last_probe;  // outcome of the last probe (logged only when it changes: a probe runs every few seconds)

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

fs::path call_off_path() { return le_root() / "turbo_output" / "call_commentary_audio_off.txt"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// The kill switch is looked at once every 2 s (the Callname tab asks for the status every frame)
bool kill_switch_present() {
    static std::mutex m;
    static double checked = -10.0;
    static bool present = false;
    std::lock_guard<std::mutex> lock(m);
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - checked > 2.0) {
        present = file_exists(call_off_path());
        checked = now;
    }
    return present;
}

double clock_now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool call_ready(std::string* why) {
    if (!g_installed) {
        if (why) *why = "the audio-service call is not installed";
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
    if (kill_switch_present()) {
        if (why) *why = "kill switch turbo_output\\call_commentary_audio_off.txt is present";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- the game's objects (game thread only)
// The reference GetCommentaryService takes is dropped through the service's own Release slot when the guard dies
struct ServiceRef {
    ProcessMemory& mem;
    uint64_t service = 0;
    explicit ServiceRef(ProcessMemory& m) : mem(m) {}
    ~ServiceRef() { release(); }
    bool acquire(std::string& err) {
        const uint64_t reg = mem.ptr(g_fns.registry);
        if (!reg) {
            err = "the service registry pointer (" + hex(g_fns.registry) + ") is null";
            return false;
        }
        using GetFn = void** (*)(void**, void*);
        void* out = nullptr;
        reinterpret_cast<GetFn>(static_cast<uintptr_t>(g_fns.get_service))(&out, reinterpret_cast<void*>(reg));
        service = reinterpret_cast<uint64_t>(out);
        if (!service) {
            err = "the commentary service is not registered with the game's service registry";
            return false;
        }
        return true;
    }
    void release() {
        if (!service) return;
        const uint64_t vt = mem.ptr(service);
        const uint64_t fn = vt ? mem.ptr(vt + kVtRelease, 1) : 0;
        if (fn && (!g_fns.service_vtable || vt == g_fns.service_vtable))
            reinterpret_cast<void (*)(void*)>(static_cast<uintptr_t>(fn))(reinterpret_cast<void*>(service));
        service = 0;
    }
};

struct RealCaller : Caller {
    // eastl::vector<uint64_t> as FilterNames reads it: {begin, end, capacity, allocator}; only end is written (shrinks)
    struct Vec {
        uint64_t* begin;
        uint64_t* end;
        uint64_t* cap;
        void* allocator;
    };

    bool filter_names(std::vector<uint64_t>& batch, size_t& survivors, std::string& err) override {
        if (const char* m = g_fns.missing_names()) {
            err = std::string("signature ") + m + " is not resolved";
            return false;
        }
        if (batch.empty()) {
            survivors = 0;
            return true;
        }
        ProcessMemory mem;
        ServiceRef ref(mem);
        if (!ref.acquire(err)) return false;
        Chain ch;
        if (!resolve_chain(mem, ref.service, g_fns, ch, err)) return false;
        Vec v{batch.data(), batch.data() + batch.size(), batch.data() + batch.size(), nullptr};
        reinterpret_cast<void (*)(void*, void*)>(static_cast<uintptr_t>(g_fns.filter_names))(reinterpret_cast<void*>(ch.inner), &v);
        if (v.begin != batch.data() || v.end < v.begin || v.end > batch.data() + batch.size()) {
            err = "the game's filter moved the vector (begin " + hex(reinterpret_cast<uint64_t>(v.begin)) + ", end " +
                  hex(reinterpret_cast<uint64_t>(v.end)) + ")";
            return false;
        }
        survivors = static_cast<size_t>(v.end - v.begin);
        return true;
    }

    bool player_audio(const std::vector<int64_t>& pids, std::vector<int>& flags, std::string& err) override {
        flags.assign(pids.size(), 0);
        if (!g_players_off.empty()) {
            err = g_players_off;
            return false;
        }
        if (const char* m = g_fns.missing_players()) {
            err = std::string("signature ") + m + " is not resolved";
            return false;
        }
        if (pids.empty()) return true;
        ProcessMemory mem;
        ServiceRef ref(mem);
        if (!ref.acquire(err)) return false;
        Chain ch;
        if (!resolve_chain(mem, ref.service, g_fns, ch, err)) return false;
        using GetBridgeFn = void* (*)(void*, const char*);
        using GetEventFn = void* (*)(void*, const char*, const char*);
        using HasAudioFn = uint8_t (*)(void*, void*);
        using ScopeCtorFn = void* (*)(void*);
        using ScopeDtorFn = void (*)(void*);
        using QueryCtorFn = void* (*)(void*, void*, void*);
        using SetIntFn = void (*)(void*, const char*, int);
        using QueryDtorFn = void (*)(void*);
        const uint64_t avt = mem.ptr(ch.audio);
        const uint64_t f_bridge = avt ? mem.ptr(avt + kVtAudioGetBridge, 1) : 0;
        const uint64_t f_event = avt ? mem.ptr(avt + kVtAudioGetEvent, 1) : 0;
        if (!f_bridge || !f_event) {
            err = "the audio system's vtable slots 0xE0 / 0x48 are not readable";
            return false;
        }
        void* audio = reinterpret_cast<void*>(ch.audio);
        void* bridge = reinterpret_cast<GetBridgeFn>(static_cast<uintptr_t>(f_bridge))(audio, reinterpret_cast<const char*>(g_fns.s_bridge));
        if (!bridge) {
            err = "the audio system has no CommentaryBridge in this screen";
            return false;
        }
        const uint64_t bvt = mem.ptr(reinterpret_cast<uint64_t>(bridge));
        const uint64_t f_has = bvt ? mem.ptr(bvt + kVtBridgeHasAudio, 1) : 0;
        if (!f_has) {
            err = "the CommentaryBridge's HasAudio slot (0xB8) is not readable";
            return false;
        }
        auto get_event = reinterpret_cast<GetEventFn>(static_cast<uintptr_t>(f_event));
        auto has_audio = reinterpret_cast<HasAudioFn>(static_cast<uintptr_t>(f_has));
        auto scope_ctor = reinterpret_cast<ScopeCtorFn>(static_cast<uintptr_t>(g_fns.scope_ctor));
        auto scope_dtor = reinterpret_cast<ScopeDtorFn>(static_cast<uintptr_t>(g_fns.scope_dtor));
        auto query_ctor = reinterpret_cast<QueryCtorFn>(static_cast<uintptr_t>(g_fns.query_ctor));
        auto set_int = reinterpret_cast<SetIntFn>(static_cast<uintptr_t>(g_fns.query_set_int));
        auto query_dtor = reinterpret_cast<QueryDtorFn>(static_cast<uintptr_t>(g_fns.query_dtor));
        const struct {
            uint64_t name;
            int bit;
        } events[] = {{g_fns.s_player_low_simple, kPlayerLowSimple}, {g_fns.s_player_low_link, kPlayerLowLink}};
        int known = 0;
        for (const auto& ev : events) {
            void* ctx = get_event(audio, reinterpret_cast<const char*>(g_fns.s_db_events), reinterpret_cast<const char*>(ev.name));
            if (!ctx) continue;  // the event is not bound in this screen: the game's own check skips the context too, every answer is "no"
            ++known;
            // exactly the game's sequence (0x14294A0F4): scope, query(scope, ctx), player_intensity = 2, player_db_pID = id, HasAudio
            alignas(16) uint8_t scope[kScratchScopeSize];
            alignas(16) uint8_t query[kSpeechQuerySize];
            std::memset(scope, 0, sizeof(scope));
            std::memset(query, 0, sizeof(query));
            scope_ctor(scope);
            query_ctor(query, scope, ctx);
            set_int(query, reinterpret_cast<const char*>(g_fns.s_player_intensity), kPlayerIntensity);
            for (size_t i = 0; i < pids.size(); ++i) {
                if (pids[i] <= 0 || pids[i] > 0x7FFFFFFF) continue;
                set_int(query, reinterpret_cast<const char*>(g_fns.s_player_db_pid), static_cast<int>(pids[i]));
                if (has_audio(bridge, query) & 1) flags[i] |= ev.bit;
            }
            query_dtor(query);
            scope_dtor(scope);
        }
        if (known == 0) {
            err = "the events PLAYER_LOW_SIMPLE / PLAYER_LOW_LINK are not bound in this screen (a match binds them)";
            return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------- the service
class GameCommentaryAudio : public Service {
public:
    ServiceStatus status() override {
        ServiceStatus s;
        std::string why;
        s.installed = g_installed && g_off.empty() && game_hooks_allowed();
        s.players = s.installed && g_players_off.empty();
        s.available = call_ready(&why);
        s.reason = why;
        s.runs = g_runs.load();
        std::lock_guard<std::mutex> lock(m_);
        if (build_ && !build_->done()) {
            s.busy = true;
            s.available = false;
            s.progress = build_->progress();
            if (s.reason.empty()) s.reason = "building";
        }
        s.dispatch = dispatch_;
        {
            std::lock_guard<std::mutex> l2(g_mutex);
            s.last = g_last;
        }
        return s;
    }

    bool request(const BuildRequest& r, std::string* err) override {
        std::string why;
        if (!call_ready(&why)) {
            if (err) *err = why;
            return false;
        }
        if (r.names.empty() && r.players.empty()) {
            if (err) *err = "nothing to check: no commentary ids and no players";
            return false;
        }
        std::shared_ptr<Build> b;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (build_ && !build_->done()) {
                if (err) *err = "a build is already running (" + build_->progress() + ")";
                return false;
            }
            b = std::make_shared<Build>(r, clock_now);
            build_ = b;
        }
        if (r.probe) ++g_probes;
        else ++g_runs;
        const char* via = queue_step(b);
        {
            std::lock_guard<std::mutex> lock(m_);
            dispatch_ = via;
        }
        if (!r.probe)
            log("game call commentary_has_audio: build started (%zu commentary ids, %zu players, via %s)", r.names.size(), r.players.size(),
                std::string(via).empty() ? "nothing yet: queued" : via);
        return true;
    }

    bool poll(BuildResult& out) override {
        std::lock_guard<std::mutex> lock(m_);
        if (results_.empty()) return false;
        out = std::move(results_.front());
        results_.pop_front();
        return true;
    }

    void cancel() override {
        std::lock_guard<std::mutex> lock(m_);
        if (build_) build_->cancel();
    }

private:
    const char* queue_step(const std::shared_ptr<Build>& b) {
        return run_on_game_thread([this, b]() { step(b); });
    }

    void step(const std::shared_ptr<Build>& b) {
        bool more = false;
        std::string fail;
        try {
            RealCaller c;
            more = b->step(c);
        } catch (const std::exception& e) {
            fail = e.what();
        } catch (...) {
            fail = "unknown exception";
        }
        ++g_steps;
        if (!fail.empty()) {
            game_hook_error("commentary_has_audio", fail.c_str());
            b->abort("exception inside the audio-service call: " + fail);
            more = false;
        }
        if (more) {
            queue_step(b);
            return;
        }
        BuildResult r = b->result();
        if (r.probe) {
            // a probe (SpokenWatch): quiet, logged when its outcome changes
            const std::string outcome = r.ok ? "bound (" + std::to_string(r.names.size()) + " of " + std::to_string(r.names_checked) + " sample ids have audio)"
                                             : (r.unbound ? "not bound in this screen" : (r.no_filter ? "no commentary bridge in this screen" : "failed: " + r.note));
            std::lock_guard<std::mutex> l2(g_mutex);
            if (outcome != g_last_probe) {
                g_last_probe = outcome;
                log("game call commentary_has_audio: probe (%zu ids): %s", r.names_checked, outcome.c_str());
            }
        } else {
            log("game call commentary_has_audio: build %s: %s", r.ok ? "ok" : (r.cancelled ? "cancelled" : "failed"), r.note.c_str());
        }
        std::lock_guard<std::mutex> lock(m_);
        if (!r.probe) {
            std::lock_guard<std::mutex> l2(g_mutex);
            g_last = std::string(r.ok ? "ok: " : "failed: ") + r.note;
        }
        results_.push_back(std::move(r));
        if (results_.size() > 4) results_.pop_front();
    }

    std::mutex m_;
    std::shared_ptr<Build> build_;
    std::deque<BuildResult> results_;
    std::string dispatch_;
};

}  // namespace

// ---------------------------------------------------------------- public API
std::vector<std::string> commentary_audio_status() {
    std::vector<std::string> out;
    std::string why;
    char line[640];
    if (call_ready(&why))
        std::snprintf(line, sizeof(line),
                      "commentary_has_audio: ready | names: FilterNames %s, GetCommentaryService %s, registry slot %s | players: %s | "
                      "runs %lld | probes %lld | steps %lld",
                      hex(g_fns.filter_names).c_str(), hex(g_fns.get_service).c_str(), hex(g_fns.registry).c_str(),
                      g_players_off.empty() ? "ready (Turbo-built PLAYER_LOW_SIMPLE / PLAYER_LOW_LINK queries)" : ("off: " + g_players_off).c_str(),
                      g_runs.load(), g_probes.load(), g_steps.load());
    else
        std::snprintf(line, sizeof(line), "commentary_has_audio: off (%s)", why.c_str());
    out.push_back(line);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_last.empty()) out.push_back("  last: " + g_last);
    if (!g_last_probe.empty()) out.push_back("  last probe: " + g_last_probe);
    return out;
}

void install_commentary_audio(turbo::App& app) {
    if (!g_installed) {
        g_installed = true;
        if (!game_hooks_allowed()) {
            g_off = "game hooks are off for this game build";
            g_players_off = g_off;
            log("game calls: commentary_has_audio off (%s)", g_off.c_str());
        } else {
            for (const auto& fs_ : fn_signatures()) g_fns.*(fs_.field) = game_signature(fs_.signature);
            if (const char* m = g_fns.missing_names()) {
                g_off = std::string("signature ") + m + " was not found on this game build";
                log("game calls: commentary_has_audio off (%s)", g_off.c_str());
            } else {
                log("game calls: commentary_has_audio resolved (registry slot %s, GetCommentaryService %s, FilterNames %s, service vtable %s, "
                    "names vtable %s)",
                    hex(g_fns.registry).c_str(), hex(g_fns.get_service).c_str(), hex(g_fns.filter_names).c_str(),
                    hex(g_fns.service_vtable).c_str(), hex(g_fns.names_vtable).c_str());
            }
            if (const char* m = g_fns.missing_players()) {
                g_players_off = std::string("signature ") + m + " was not found on this game build";
                log("game calls: commentary_has_audio player path off (%s)", g_players_off.c_str());
            } else {
                log("game calls: commentary_has_audio player path resolved (query ctor %s, set-int %s, dtor %s, scope %s / %s)",
                    hex(g_fns.query_ctor).c_str(), hex(g_fns.query_set_int).c_str(), hex(g_fns.query_dtor).c_str(),
                    hex(g_fns.scope_ctor).c_str(), hex(g_fns.scope_dtor).c_str());
            }
        }
    }
    app.commentary_audio = std::make_shared<GameCommentaryAudio>();
}

}  // namespace host
