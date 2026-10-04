// FC 27 LE Turbo GUI - voice swaps inside FC27.exe: the two detours, the cached switches, the observe log writer and
// the Service the App talks to (see callname_voice_win.h, core/callname_voice.h, core/callname_voice_host.h)
#include "callname_voice_win.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "commentary_audio_win.h"
#include "core/callname_voice.h"
#include "core/callname_voice_host.h"
#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

constexpr uint64_t kLogMaxBytes = 64ull << 20;  // the observe log stops growing here (a long play session)

// ---------------------------------------------------------------- what the detours read (atomics only)
using PreprocessFn = void (*)(void*, void*);         // void Preprocess(IQueryPreprocessor*, SpeechQuery*)
using GetCallnameFn = int (*)(void*, int, int);       // int GetCallname(SpeechEventHandler*, int playerid, int mode)
PreprocessFn g_orig_preprocess = nullptr;
GetCallnameFn g_orig_get_callname = nullptr;
std::atomic<HookHandle> g_voice_hook{nullptr};
std::atomic<HookHandle> g_kickoff_hook{nullptr};
std::atomic<bool> g_voice_live{false};
std::atomic<bool> g_kickoff_live{false};
std::atomic<bool> g_observe_live{false};
std::atomic<const voice::Table*> g_table{nullptr};
std::atomic<voice::LogRing*> g_ring{nullptr};  // created when observe mode is first seen, never freed
voice::Stats g_stats;
voice::FileSwitches g_switches;
thread_local voice::Guard t_guard;  // the double-pass guard (constant-initialised: no TLS constructor)

// ---------------------------------------------------------------- install / GUI state (never taken by a detour)
std::mutex g_mutex;
bool g_install_tried = false;
bool g_installed = false;                   // both hooks installed
std::string g_off = "hooks off";            // why not installed: "game build" / "hooks off"
std::vector<const voice::Table*> g_tables;  // every published table (a detour may still read an old one)
bool g_writer_started = false;
bool g_seen_feature_off = false, g_seen_observe = false;  // last logged switch states
std::atomic<uint64_t> g_lines_written{0}, g_lines_lost{0};

fs::path out_dir() { return le_root() / "turbo_output"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

// ---------------------------------------------------------------- the detours
// Observe mode: the query's "_pID" values before and after the rewrite, into the ring
void observe_query(uint8_t* q, const voice::Table& t, voice::LogRing& ring) {
    voice::LogEntry e;
    const uint8_t* pvs[voice::kLogPids] = {};
    if (!voice::observe_before(q, e, pvs)) {
        voice::process_query(q, t, t_guard, g_stats, false);
        return;
    }
    // Preprocess runs under the speech registry lock (one query at a time), so the counter's change is this query's
    const uint64_t skips = g_stats.guard_skips.load(std::memory_order_relaxed);
    voice::process_query(q, t, t_guard, g_stats, false);
    voice::observe_after(e, pvs);
    e.guard = g_stats.guard_skips.load(std::memory_order_relaxed) != skips ? 1 : 0;
    e.t_ms = GetTickCount64();
    e.tid = GetCurrentThreadId();
    ring.push(e);
}

void voice_body(uint8_t* q) {
    if (!game_hook_live(g_voice_hook.load(std::memory_order_acquire))) return;
    const voice::Table* t = g_table.load(std::memory_order_acquire);
    if (!t) return;
    const bool own = own_query_depth() > 0;  // Turbo's own audit queries: never rewritten, never logged
    voice::LogRing* ring = (!own && g_observe_live.load(std::memory_order_relaxed)) ? g_ring.load(std::memory_order_acquire) : nullptr;
    if (ring) observe_query(q, *t, *ring);
    else voice::process_query(q, *t, t_guard, g_stats, own);
}

void preprocess_detour(void* self, void* q) {
    g_orig_preprocess(self, q);  // the game's cm_sim / *_gID first, from the real player id (B keeps his own gender)
    if (!q || !g_voice_live.load(std::memory_order_relaxed)) return;
    HOOK_BODY("callname_voice", voice_body(static_cast<uint8_t*>(q)));
}

int kickoff_body(int pid, int game) {
    if (!game_hook_live(g_kickoff_hook.load(std::memory_order_acquire))) return game;
    const voice::Table* t = g_table.load(std::memory_order_acquire);
    const int out = t ? voice::kickoff_override(*t, pid, game, g_stats) : game;
    if (g_observe_live.load(std::memory_order_relaxed)) {
        if (voice::LogRing* ring = g_ring.load(std::memory_order_acquire)) {
            voice::LogEntry e;
            e.kind = voice::LogEntry::Kickoff;
            e.t_ms = GetTickCount64();
            e.tid = GetCurrentThreadId();
            e.pid = pid;
            e.game = game;
            e.has_override = t && t->kickoff(pid);
            e.override_id = out;
            ring->push(e);
        }
    }
    return out;
}

int get_callname_detour(void* self, int pid, int mode) {
    const int game = g_orig_get_callname(self, pid, mode);  // every side effect of the game's own computation kept
    if (!g_kickoff_live.load(std::memory_order_relaxed)) return game;
    int out = game;
    HOOK_BODY("callname_kickoff", out = kickoff_body(pid, game));
    return out;
}

// ---------------------------------------------------------------- switches (g_mutex held)
void update_live() {
    const voice::Table* t = g_table.load(std::memory_order_acquire);
    voice::SwitchState s;
    s.installed = g_installed;
    s.feature_off = g_switches.feature_off.load();
    s.observe = g_switches.observe.load() && g_ring.load() != nullptr;
    s.voices = t && !t->voices.empty();
    s.kickoffs = t && !t->kickoffs.empty();
    const voice::Live l = voice::live_from(s);
    g_observe_live.store(l.observe);
    g_voice_live.store(l.voice);
    g_kickoff_live.store(l.kickoff);
}

std::string stamp_now() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tmv);
    return b;
}

// Appends what the ring holds to callname_voice_log.txt every 5 s (the only reader of the ring)
void writer_loop() {
    std::string text;
    uint64_t dropped_seen = 0;
    bool header = false, full_logged = false;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        voice::LogRing* ring = g_ring.load(std::memory_order_acquire);
        if (!ring) continue;
        text.clear();
        const uint64_t dropped = ring->dropped();
        if (dropped != dropped_seen) {
            text += "# dropped " + std::to_string(dropped - dropped_seen) + "\n";
            dropped_seen = dropped;
        }
        const size_t lines = voice::drain_lines(*ring, text, voice::kRingSize * 4);
        if (text.empty()) continue;
        const fs::path file = voice::observe_log_path(out_dir());
        std::error_code ec;
        const uint64_t size = fs::exists(file, ec) ? fs::file_size(file, ec) : 0;
        if (!ec && size > kLogMaxBytes) {
            g_lines_lost += lines;
            if (!full_logged) log("callname voice: %s is above 64 MB: no longer written", file.string().c_str());
            full_logged = true;
            continue;
        }
        std::ofstream f(file, std::ios::binary | std::ios::app);
        if (!f) {
            g_lines_lost += lines;
            continue;
        }
        if (!header) {
            f << "# Turbo voice-swap observe log: session " << stamp_now() << ", game build " << game_build_key() << ", ring "
              << ring->size() << " (scripts/callname_voice_log.py)\n";
            header = true;
        }
        f << text;
        g_lines_written += lines;
    }
}

void start_observe() {
    if (!g_ring.load()) g_ring.store(new voice::LogRing(voice::kRingSize), std::memory_order_release);
    if (!g_writer_started) {
        g_writer_started = true;
        std::thread(writer_loop).detach();
    }
}

// The files, at most every 2 s (force: now)
void refresh(bool force) {
    game_hooks_refresh_switches();
    if (!g_switches.refresh(GetTickCount64(), out_dir(), &file_exists, force)) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool off = g_switches.feature_off.load(), observe = g_switches.observe.load();
    if (g_installed && observe) start_observe();
    if (g_installed && off != g_seen_feature_off)
        log("callname voice: %s", off ? "off (turbo_output\\callname_voice_off.txt present)" : "on again (kill switch removed)");
    if (g_installed && observe != g_seen_observe)
        log("callname voice: observe log %s", observe ? "on (turbo_output\\callname_voice_log.txt, every 5 s)" : "off");
    g_seen_feature_off = off;
    g_seen_observe = observe;
    update_live();
}

bool same_table(const voice::Table& a, const voice::Table& b) {
    if (a.voices.size() != b.voices.size() || a.kickoffs.size() != b.kickoffs.size()) return false;
    for (size_t i = 0; i < a.voices.size(); ++i)
        if (a.voices[i].pid != b.voices[i].pid || a.voices[i].to != b.voices[i].to || a.voices[i].names_only != b.voices[i].names_only)
            return false;
    for (size_t i = 0; i < a.kickoffs.size(); ++i)
        if (a.kickoffs[i].pid != b.kickoffs[i].pid || a.kickoffs[i].id != b.kickoffs[i].id) return false;
    return true;
}

// ---------------------------------------------------------------- the service
class VoiceService : public voice::Service {
public:
    bool available() const override { return why_off().empty(); }

    std::string why_off() const override {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_installed) return g_off;
        }
        if (!game_hook_live(g_voice_hook.load()) || !game_hook_live(g_kickoff_hook.load())) return "hooks off";
        if (g_switches.feature_off.load()) return "kill switch";
        return "";
    }

    void publish(const voice::Table& t) override {
        std::lock_guard<std::mutex> lock(g_mutex);
        const voice::Table* cur = g_table.load();
        if (cur && same_table(*cur, t)) return;  // nothing changed: no new copy
        const voice::Table* nt = new voice::Table(t);
        g_tables.push_back(nt);
        g_table.store(nt, std::memory_order_release);
        update_live();
        log("callname voice: table published (%zu voice swaps, %zu kick-off entries)", t.voices.size(), t.kickoffs.size());
    }

    voice::StatsSnapshot stats() const override { return voice::snapshot(g_stats); }
    bool observe_on() const override { return g_switches.observe.load(); }
    void refresh_switches() override { refresh(false); }
};

VoiceService g_service;

}  // namespace

// ---------------------------------------------------------------- public API
void install_callname_voice() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_install_tried) return;
    g_install_tried = true;
    if (!game_hooks_allowed()) {
        char env[8] = {};
        const bool hooks_off = (GetEnvironmentVariableA("TURBO_GUI_NO_GAME_HOOKS", env, sizeof(env)) > 0 && env[0] == '1') ||
                               file_exists(out_dir() / "game_hooks_off.txt");
        g_off = hooks_off ? "hooks off" : "game build";
        log("callname voice: off (%s): voice swaps are not installed", g_off.c_str());
        return;
    }
    const char* sigs[] = {"speech_query_preprocess", "commentary_get_callname", "speech_param_name_layout", "speech_param_get_int_layout",
                          "speech_param_set_int_store"};
    for (const char* s : sigs)
        if (!game_signature(s)) {
            g_off = "game build";
            log("callname voice: off (signature %s was not found on this game build): voice swaps are not installed", s);
            return;
        }
    if (!install_game_hook("callname_voice", "speech_query_preprocess", reinterpret_cast<void*>(&preprocess_detour),
                           reinterpret_cast<void**>(&g_orig_preprocess))) {
        g_off = "hooks off";
        log("callname voice: off (the callname_voice hook was not installed)");
        return;
    }
    g_voice_hook.store(game_hook_handle("callname_voice"), std::memory_order_release);
    if (!install_game_hook("callname_kickoff", "commentary_get_callname", reinterpret_cast<void*>(&get_callname_detour),
                           reinterpret_cast<void**>(&g_orig_get_callname))) {
        g_off = "hooks off";
        log("callname voice: off (the callname_kickoff hook was not installed; callname_voice stays pass-through)");
        return;
    }
    g_kickoff_hook.store(game_hook_handle("callname_kickoff"), std::memory_order_release);
    g_installed = true;
    g_off.clear();
    g_switches.refresh(GetTickCount64(), out_dir(), &file_exists, true);
    g_seen_feature_off = g_switches.feature_off.load();
    g_seen_observe = g_switches.observe.load();
    if (g_seen_observe) start_observe();
    update_live();
    log("callname voice: installed (Preprocess %s, GetCallname %s; kill switch turbo_output\\callname_voice_off.txt%s%s)",
        hex(game_signature("speech_query_preprocess")).c_str(), hex(game_signature("commentary_get_callname")).c_str(),
        g_seen_feature_off ? ", present: off" : "", g_seen_observe ? "; observe log on" : "");
}

voice::Service* callname_voice_service() { return &g_service; }

std::vector<std::string> callname_voice_status() {
    std::vector<std::string> out;
    const std::string why = g_service.why_off();
    const voice::StatsSnapshot s = voice::snapshot(g_stats);
    size_t voices = 0, kicks = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (const voice::Table* t = g_table.load()) {
            voices = t->voices.size();
            kicks = t->kickoffs.size();
        }
    }
    char line[512];
    if (why.empty())
        std::snprintf(line, sizeof(line),
                      "callname_voice: on | %zu voice swaps, %zu kick-off entries | queries %llu | lines changed %llu | kick-offs set %llu | "
                      "guard skips %llu | own skips %llu | bounded %llu",
                      voices, kicks, static_cast<unsigned long long>(s.queries), static_cast<unsigned long long>(s.rewrites),
                      static_cast<unsigned long long>(s.kickoffs), static_cast<unsigned long long>(s.guard_skips),
                      static_cast<unsigned long long>(s.own_skips), static_cast<unsigned long long>(s.bounded));
    else
        std::snprintf(line, sizeof(line), "callname_voice: off (%s)", why.c_str());
    out.push_back(line);
    if (voice::LogRing* ring = g_ring.load()) {
        std::snprintf(line, sizeof(line), "  observe log %s | lines written %llu | dropped %llu | not written %llu",
                      g_switches.observe.load() ? "on" : "off", static_cast<unsigned long long>(g_lines_written.load()),
                      static_cast<unsigned long long>(ring->dropped()), static_cast<unsigned long long>(g_lines_lost.load()));
        out.push_back(line);
    }
    return out;
}

}  // namespace host
