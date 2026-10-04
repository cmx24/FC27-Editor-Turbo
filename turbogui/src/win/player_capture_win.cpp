// FC 27 LE Turbo GUI - miniface from the game's 3D model: the game-side part (docs/re/player_capture.md).
//
// The game's PlayerCaptureController renders up to six players from their 3D models and hands each picture to a
// callback (PlayerCapture_RequestStatic_B: players, slot callback, done callback, mode, extra). Turbo:
//   * resolves the controller's entry points from the signature table (core/sigscan.cpp, docs/re/player_capture.md);
//   * installs two guarded, pass-through hooks that only LEARN: pc_start (PlayerCaptureController::Start: the game's own
//     requests teach the 0x6C descriptor, mode / extra and are logged) and pc_slot (the per-picture handler: size and
//     format of the game's own pictures are logged);
//   * sends its own request on the game thread (game_hooks.h dispatcher) with delegates Turbo owns; the game calls them
//     back on its thread with the picture bytes, which are copied and decoded later on Turbo's thread (core/player_capture.h).
// Kill switches: turbo_output\player_capture_off.txt (feature off), the hook switches of game_hooks.h, and everything is
// off when the build is unknown. Nothing here runs unless the Miniface editor asks for a picture.
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>

#include "core/player_capture.h"
#include "game_hooks.h"
#include "host.h"
#include "ui/app.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo::capture;

// The game calls these back on its thread (defined below, after the job type)
void on_slot_invoker(int id, void* data, size_t size, const void* storage);
void on_done_invoker(const void* storage);

namespace {

constexpr size_t kMaxSliceBytes = 32u << 20;  // a picture the game hands over is ~190 KB; refuse anything absurd
constexpr double kAnswerTimeout = 20.0;       // seconds after the request ran on the game thread
constexpr int kMaxLoggedCaptures = 40;

using GetOrCreateFn = void* (*)();
using StaticBFn = void (*)(void* unused, const DescVector* players, const Delegate* on_slot, const Delegate* on_done, int mode, int extra);
using StartFn = void (*)(void* ctl, void* players);
using OnSlotFn = void (*)(void* ctl, int slot, long long size);

// resolved game addresses (0 = missing)
uint64_t g_get_or_create = 0, g_static_b = 0, g_start = 0, g_on_slot = 0;
uint64_t g_settings = 0, g_renderer = 0, g_hub = 0;  // the globals' addresses (pointers live there)
StartFn o_start = nullptr;
OnSlotFn o_on_slot = nullptr;
bool g_start_hooked = false, g_slot_hooked = false;
bool g_installed = false;
std::string g_install_note;

std::mutex g_mutex;  // template, jobs, results, counters
Template g_template;
std::atomic<void*> g_controller{nullptr};  // the controller once seen (Start hook or our own request)
std::atomic<bool> g_own_request{false};    // set on the game thread around our call into the game
int g_seen_game = 0, g_logged = 0, g_done = 0, g_failed = 0;
std::string g_last_format;

struct Job {
    uint64_t serial = 0;
    Request req;
    Plan plan;
    std::string label;
    // owned by the job for as long as the game may touch them (the job is never freed)
    PlayerDesc desc;
    DescVector vec;
    Delegate on_slot, on_done;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> ran{false};      // the game-thread part ran
    std::atomic<bool> submitted{false};
    std::atomic<bool> got_bytes{false};
    std::atomic<bool> finished{false};  // the game's done callback fired
    std::atomic<bool> failed{false};
    std::string error;
    std::vector<uint8_t> bytes;
    int32_t bytes_id = 0;
    std::chrono::steady_clock::time_point t_request, t_ran;
};

std::deque<std::unique_ptr<Job>> g_jobs;  // never freed: the game keeps a copy of our delegates' storage (= Job*)
Job* g_current = nullptr;                  // the request in flight (owned by g_jobs)
uint64_t g_serial = 0;
std::string g_dispatch;

fs::path capture_log_path() { return le_root() / "turbo_output" / "player_capture.log"; }

void capture_log(const std::string& line) {
    log("player capture: %s", line.c_str());
    std::error_code ec;
    fs::create_directories(le_root() / "turbo_output", ec);
    std::ofstream f(capture_log_path(), std::ios::app);
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char ts[32];
        std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        f << ts << line << "\n";
    }
}

bool feature_off() {
    std::error_code ec;
    return fs::exists(le_root() / "turbo_output" / "player_capture_off.txt", ec);
}

// Reads the game's 16-byte SSO string (flag bit 7 of byte +0xF = heap string: pointer at +0)
std::string read_game_string(uint64_t addr) {
    ProcessMemory mem;
    uint8_t raw[16];
    if (!mem.read(addr, raw, 16)) return "?";
    std::string s;
    if (raw[15] & 0x80) {
        uint64_t p = 0;
        std::memcpy(&p, raw, 8);
        char buf[96] = {0};
        if (!p || !mem.read(p, buf, sizeof(buf) - 1)) return "?";
        s = buf;
    } else {
        for (int i = 0; i < 15 && raw[i]; ++i) s += static_cast<char>(raw[i]);
    }
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = '.';
    return s;
}

// What the game-thread job does: ask the game for the picture
void run_request(Job* job) {
    job->ran = true;
    job->t_ran = std::chrono::steady_clock::now();
    if (job->cancelled) return;
    ProcessMemory mem;
    auto fail = [&](const std::string& why) {
        job->error = why;
        job->failed = true;
        job->finished = true;
        capture_log("request " + job->label + " refused: " + why);
    };
    uint64_t settings = 0, renderer = 0, hub = 0;
    if (!mem.read(g_settings, &settings, 8) || !mem.read(g_renderer, &renderer, 8) || !mem.read(g_hub, &hub, 8)) return fail("cannot read the game's capture globals");
    if (!renderer || !hub) return fail("the game's frontend renderer is not up (open a menu screen, e.g. the career hub)");
    uint8_t gate = 0;
    if (settings && (!mem.read(settings + 0x1F6, &gate, 1) || gate == 0)) return fail("the game's player-capture setting is off (settings byte +0x1F6 = 0)");
    void* ctl = nullptr;
    try {
        ctl = reinterpret_cast<GetOrCreateFn>(g_get_or_create)();
    } catch (...) {
        return fail("PlayerCaptureController_GetOrCreate threw");
    }
    if (!ctl) return fail("the game has no PlayerCaptureController");
    g_controller = ctl;
    int32_t state = -1;
    if (!mem.read(reinterpret_cast<uint64_t>(ctl), &state, 4)) return fail("cannot read the controller");
    if (state != 0) return fail("the game is capturing something else right now (controller state " + std::to_string(state) + "), try again in a moment");
    // our structures (kept alive by the job)
    job->desc = job->plan.desc;
    job->vec.begin = &job->desc;
    job->vec.end = &job->desc + 1;
    job->vec.cap = job->vec.end;
    job->vec.allocator = nullptr;
    job->on_slot = make_delegate(job, reinterpret_cast<void*>(&on_slot_invoker));
    job->on_done = make_delegate(job, reinterpret_cast<void*>(&on_done_invoker));
    capture_log("request " + job->label + ": id " + std::to_string(job->req.id) + ", mode " + std::to_string(job->plan.mode) + ", extra " +
                std::to_string(job->plan.extra) + " (" + job->plan.note + "); desc " + describe_desc(job->desc));
    g_own_request = true;
    try {
        reinterpret_cast<StaticBFn>(g_static_b)(nullptr, &job->vec, &job->on_slot, &job->on_done, job->plan.mode, job->plan.extra);
    } catch (...) {
        g_own_request = false;
        return fail("PlayerCapture_RequestStatic_B threw");
    }
    g_own_request = false;
    state = -1;
    mem.read(reinterpret_cast<uint64_t>(ctl), &state, 4);
    if (state != 1 && state != 2 && state != 3 && state != 4) return fail("the game did not start the capture (controller state " + std::to_string(state) + " after the request)");
    job->submitted = true;
    capture_log("request " + job->label + " submitted (controller state " + std::to_string(state) + ")");
}

// ---------------------------------------------------------------- learning hooks
void hk_start(void* ctl, void* vec) {
    if (!game_hook_enabled("pc_start")) {
        o_start(ctl, vec);
        return;
    }
    game_hook_called("pc_start");
    HOOK_BODY("pc_start", {
        ProcessMemory mem;
        g_controller = ctl;
        const bool ours = g_own_request.load();
        uint64_t begin = 0, end = 0;
        const uint64_t c = reinterpret_cast<uint64_t>(ctl), v = reinterpret_cast<uint64_t>(vec);
        if (mem.read(v, &begin, 8) && mem.read(v + 8, &end, 8) && end >= begin) {
            const size_t n = static_cast<size_t>((end - begin) / kPlayerDescSize);
            int32_t type = 0, mode = 0, extra = 0;
            mem.read(c + 0x18, &type, 4);
            mem.read(c + 0x40, &mode, 4);
            mem.read(c + 0x44, &extra, 4);
            std::string name = read_game_string(c + 0xA8);
            uint64_t nb = 0, ne = 0;
            mem.read(c + 0x48, &nb, 8);
            mem.read(c + 0x50, &ne, 8);
            std::string first_image = (nb && ne > nb) ? read_game_string(nb) : "";
            std::vector<PlayerDesc> descs;
            for (size_t i = 0; i < n && i < kMaxPlayersPerRequest; ++i) {
                PlayerDesc d;
                if (!mem.read(begin + i * kPlayerDescSize, d.b, kPlayerDescSize)) break;
                descs.push_back(d);
            }
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!ours) {
                ++g_seen_game;
                if (!descs.empty() && !g_template.learned) {
                    g_template.learned = true;
                    g_template.desc = descs[0];
                    g_template.count = static_cast<int>(n);
                    g_template.type = type;
                    g_template.mode = mode;
                    g_template.extra = extra;
                    g_template.name = name;
                    g_template.first_image = first_image;
                    g_template.source = "game";
                    g_template.seen_at_ms = GetTickCount64();
                }
            }
            if (g_logged < kMaxLoggedCaptures) {
                ++g_logged;
                capture_log(std::string(ours ? "turbo" : "game") + " capture: " + std::to_string(n) + " player(s), type " + std::to_string(type) +
                            ", mode " + std::to_string(mode) + ", extra " + std::to_string(extra) + ", name '" + name + "', first image '" +
                            first_image + "', thread " + std::to_string(GetCurrentThreadId()));
                for (size_t i = 0; i < descs.size(); ++i)
                    capture_log("  desc[" + std::to_string(i) + "] " + describe_desc(descs[i]) + "\n    " + hex_bytes(descs[i].b, kPlayerDescSize));
            }
        }
    });
    o_start(ctl, vec);
}

void hk_on_slot(void* ctl, int slot, long long size) {
    if (!game_hook_enabled("pc_slot")) {
        o_on_slot(ctl, slot, size);
        return;
    }
    game_hook_called("pc_slot");
    HOOK_BODY("pc_slot", {
        ProcessMemory mem;
        const uint64_t c = reinterpret_cast<uint64_t>(ctl);
        int32_t mode = 0;
        uint64_t buf = 0;
        mem.read(c + 0x40, &mode, 4);
        mem.read(c + 0x08, &buf, 8);
        const size_t stride = mode == 1 ? kSliceSizeMode1 : kSliceSizeMode0;
        uint8_t head[16] = {0};
        bool have = buf && slot >= 0 && slot < 6 && mem.read(buf + static_cast<uint64_t>(slot) * stride, head, 16);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_logged < kMaxLoggedCaptures * 6) {
            ++g_logged;
            capture_log("picture ready: slot " + std::to_string(slot) + ", " + std::to_string(size) + " bytes, mode " + std::to_string(mode) +
                        ", starts " + (have ? hex_bytes(head, 16) : std::string("?")));
        }
    });
    o_on_slot(ctl, slot, size);
}

}  // namespace

// ---------------------------------------------------------------- the game's callbacks (game thread)
void on_slot_invoker(int id, void* data, size_t size, const void* storage) {
    HOOK_BODY("pc_callback", {
        Job* job = static_cast<Job*>(delegate_context(storage));
        if (!job) return;
        if (job->cancelled || job->finished) return;
        if (!data || size == 0 || size > kMaxSliceBytes) {
            job->error = "the game handed over " + std::to_string(size) + " bytes";
            job->failed = true;
            return;
        }
        ProcessMemory mem;
        std::vector<uint8_t> bytes(size);
        if (!mem.read(reinterpret_cast<uint64_t>(data), bytes.data(), size)) {
            job->error = "cannot read the picture bytes";
            job->failed = true;
            return;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        job->bytes = std::move(bytes);
        job->bytes_id = id;
        job->got_bytes = true;
        uint8_t head[16] = {0};
        std::memcpy(head, job->bytes.data(), job->bytes.size() < 16 ? job->bytes.size() : 16);
        capture_log("picture for id " + std::to_string(id) + ": " + std::to_string(size) + " bytes, starts " + hex_bytes(head, 16));
    });
}

void on_done_invoker(const void* storage) {
    HOOK_BODY("pc_callback", {
        Job* job = static_cast<Job*>(delegate_context(storage));
        if (!job) return;
        job->finished = true;
        capture_log("capture done for " + job->label + (job->got_bytes ? "" : " (no picture was handed over)"));
    });
}

// ---------------------------------------------------------------- the service the editor uses
class GameCapture : public CaptureService {
public:
    Status status() override {
        Status s;
        s.installed = g_installed;
        std::lock_guard<std::mutex> lock(g_mutex);
        s.learned = g_template.learned;
        s.seen = g_seen_game;
        s.done = g_done;
        s.failed = g_failed;
        s.last_format = g_last_format;
        s.dispatch = g_dispatch;
        if (!g_installed) {
            s.reason = g_install_note;
            return s;
        }
        if (g_current) {
            s.busy = true;
            s.busy_label = g_current->label;
            auto now = std::chrono::steady_clock::now();
            s.busy_for = std::chrono::duration<double>(now - g_current->t_request).count();
            s.reason = g_current->ran ? "rendering..." : (g_dispatch == "hook" ? "queued" : "queued: runs on the next career-mode event (advance the calendar / open a screen)");
            return s;
        }
        ProcessMemory mem;
        uint64_t settings = 0, renderer = 0, hub = 0;
        if (!mem.read(g_settings, &settings, 8) || !mem.read(g_renderer, &renderer, 8) || !mem.read(g_hub, &hub, 8)) {
            s.reason = "cannot read the game's capture globals";
            return s;
        }
        if (!renderer || !hub) {
            s.reason = "the game's frontend renderer is not up (open a menu screen such as the career hub)";
            return s;
        }
        uint8_t gate = 1;
        if (settings && mem.read(settings + 0x1F6, &gate, 1) && gate == 0) {
            s.reason = "the game's player-capture setting is off (settings byte +0x1F6)";
            return s;
        }
        if (void* ctl = g_controller.load()) {
            int32_t state = 0;
            if (mem.read(reinterpret_cast<uint64_t>(ctl), &state, 4) && state != 0) {
                s.reason = "the game is capturing something itself (state " + std::to_string(state) + ")";
                return s;
            }
        }
        s.available = true;
        s.reason = g_template.learned ? "ready (descriptor learned from the game)" : "ready (default descriptor; the game has not captured anything yet)";
        return s;
    }

    bool request(const Request& r, std::string* err) override {
        if (!g_installed) {
            if (err) *err = g_install_note;
            return false;
        }
        if (r.id <= 0) {
            if (err) *err = "no id to render";
            return false;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_current) {
            if (err) *err = "a capture is already in flight (" + g_current->label + ")";
            return false;
        }
        auto job = std::make_unique<Job>();
        job->serial = ++g_serial;
        job->req = r;
        job->label = r.label.empty() ? ("id " + std::to_string(r.id)) : r.label;
        job->plan = plan_request(r, &g_template);
        job->t_request = std::chrono::steady_clock::now();
        Job* raw = job.get();
        g_jobs.push_back(std::move(job));
        // old jobs stay allocated (the game may still hold our delegates' storage, which is the Job pointer); only
        // their picture bytes are released
        for (auto& old : g_jobs)
            if (old.get() != raw && old->finished) {
                old->bytes.clear();
                old->bytes.shrink_to_fit();
            }
        g_current = raw;
        g_dispatch = run_on_game_thread([raw]() { run_request(raw); });
        capture_log("request " + raw->label + " queued (" + (g_dispatch.empty() ? "no game-thread dispatcher yet" : g_dispatch) + ")");
        return true;
    }

    bool poll(Result& out) override {
        Job* job = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_current) return false;
            Job* j = g_current;
            const auto now = std::chrono::steady_clock::now();
            bool timeout = j->ran && !j->finished && std::chrono::duration<double>(now - j->t_ran).count() > kAnswerTimeout;
            if (!(j->finished || j->failed || timeout)) return false;
            if (timeout && !j->failed) {
                j->failed = true;
                j->error = j->submitted ? "the game did not finish the capture in 20 s (is a menu screen open?)" : "the request did not run";
                j->cancelled = true;
            }
            job = j;
            g_current = nullptr;
        }
        out = Result();
        out.label = job->label;
        out.id = job->req.id;
        if (job->failed && !job->got_bytes) {
            out.ok = false;
            out.error = job->error.empty() ? "the capture failed" : job->error;
        } else if (!job->got_bytes) {
            out.ok = false;
            out.error = "the game finished without handing a picture over (check turbo_output\\player_capture.log)";
        } else {
            std::vector<uint8_t> bytes;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                bytes = job->bytes;
            }
            out.bytes = bytes.size();
            std::string fmt, err;
            if (decode_slice(bytes.data(), bytes.size(), out.image, &fmt, &err)) {
                out.ok = true;
                out.format = fmt;
            } else {
                out.ok = false;
                out.error = err;
                // keep the bytes for the integrator
                std::error_code ec;
                fs::path dump = le_root() / "turbo_output" / ("player_capture_" + std::to_string(job->req.id) + ".bin");
                std::ofstream f(dump, std::ios::binary);
                if (f) f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                out.error += " (bytes saved to " + dump.string() + ")";
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (out.ok) {
                ++g_done;
                g_last_format = out.format;
            } else {
                ++g_failed;
            }
            job->bytes.clear();
            job->bytes.shrink_to_fit();
        }
        capture_log("result for " + out.label + ": " + (out.ok ? "ok, " + out.format : "failed: " + out.error));
        return true;
    }

    void cancel() override {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_current) return;
        g_current->cancelled = true;
        capture_log("request " + g_current->label + " cancelled");
        g_current = nullptr;
    }

    const Template* learned() override { return g_template.learned ? &g_template : nullptr; }
};

void install_player_capture(turbo::App& app) {
    app.capture = std::make_shared<GameCapture>();
    try {
        if (feature_off()) {
            g_install_note = "off: turbo_output\\player_capture_off.txt present";
            log("player capture: %s", g_install_note.c_str());
            return;
        }
        if (!game_hooks_allowed()) {
            g_install_note = "off: game hooks are off (" + game_hooks_report().note + ")";
            log("player capture: %s", g_install_note.c_str());
            return;
        }
        g_get_or_create = game_signature("PlayerCaptureController_GetOrCreate");
        g_static_b = game_signature("PlayerCapture_RequestStatic_B");
        g_start = game_signature("PlayerCaptureController_Start");
        g_on_slot = game_signature("PlayerCaptureStream_OnSlot");
        g_settings = game_signature("PlayerCapture_Settings");
        g_renderer = game_signature("PlayerCapture_Renderer");
        g_hub = game_signature("PlayerCapture_ListenerHub");
        std::string missing;
        auto need = [&](const char* n, uint64_t v) {
            if (!v) missing += std::string(missing.empty() ? "" : ", ") + n;
        };
        need("GetOrCreate", g_get_or_create);
        need("RequestStatic_B", g_static_b);
        need("Start", g_start);
        need("Settings", g_settings);
        need("Renderer", g_renderer);
        need("ListenerHub", g_hub);
        if (!missing.empty()) {
            g_install_note = "off: signature(s) not found on build " + game_build_key() + ": " + missing;
            log("player capture: %s", g_install_note.c_str());
            return;
        }
        g_start_hooked = install_game_hook("pc_start", "PlayerCaptureController_Start", reinterpret_cast<void*>(&hk_start), reinterpret_cast<void**>(&o_start));
        if (g_on_slot)
            g_slot_hooked = install_game_hook("pc_slot", "PlayerCaptureStream_OnSlot", reinterpret_cast<void*>(&hk_on_slot), reinterpret_cast<void**>(&o_on_slot));
        g_installed = true;
        g_install_note = std::string("installed (learning hook ") + (g_start_hooked ? "on" : "off") + ", picture hook " + (g_slot_hooked ? "on" : "off") + ")";
        log("player capture: %s; GetOrCreate 0x%llX, RequestStatic_B 0x%llX, settings 0x%llX, renderer 0x%llX, hub 0x%llX", g_install_note.c_str(),
            static_cast<unsigned long long>(g_get_or_create), static_cast<unsigned long long>(g_static_b), static_cast<unsigned long long>(g_settings),
            static_cast<unsigned long long>(g_renderer), static_cast<unsigned long long>(g_hub));
    } catch (const std::exception& e) {
        g_installed = false;
        g_install_note = std::string("off: ") + e.what();
        log("player capture: %s", g_install_note.c_str());
    } catch (...) {
        g_installed = false;
        g_install_note = "off: unknown error while installing";
        log("player capture: %s", g_install_note.c_str());
    }
}

}  // namespace host
