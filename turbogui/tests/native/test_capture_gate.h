// Turbo X 1.0.3 lane CG: the player-capture submit gate (core/player_capture.h SubmitGate) and its wiring in the Windows host
// (src/win/player_capture_win.cpp) and the overlay (src/win/overlay_dx12.cpp). Incident 2026-11-01: the game crashed 47 s after
// Live Editor's display re-initialised twice while a "Render all filtered heads" sweep sent a batch of 6 heads every ~0.7 s.
// Included by test_main.cpp after test_capture_watchdog.h (it reuses capture_watchdog_test::function_body).
#pragma once

#include <fstream>
#include <string>

#include "core/player_capture.h"

namespace capture_gate_test {
namespace pc = turbo::capture;

inline std::string read_src(const char* rel) {
    const fs::path p = fs::path(g_lua).parent_path().parent_path().parent_path() / "src" / rel;
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

inline void cases() {
    run_case("1.0.3 player capture gate: a display re-init pauses new requests for 120 frames AND 2 s, whichever ends later", [&] {
        pc::SubmitGate g;
        std::string why;
        CHECK(!g.paused() && g.may_submit(0.0, 6, &why), "open before any re-init");
        CHECK(g.display_reinit(10.0), "the first re-init starts a pause (logged \"paused\")");
        CHECK(g.paused() && !g.may_submit(10.0, 6, &why) && !g.may_submit(10.0, 1, nullptr), "paused: neither a batch nor a single request goes");
        CHECK(why.find("paused (display re-init)") != std::string::npos && why.find("0 of 120 frames") != std::string::npos, "the reason names the pause: " + why);
        // 120 frames in 1 s (a 120 fps game): still paused until 2 s passed
        bool resumed = false;
        for (int i = 1; i <= 120; ++i) resumed = g.frame(10.0 + i / 120.0) || resumed;
        CHECK(!resumed && g.paused(), "120 frames after 1 s: still paused (the 2 s are not over)");
        CHECK(g.frame(12.0) && !g.paused() && g.may_submit(12.0, 6, nullptr), "the first frame at 2 s resumes (logged \"resumed\")");
        // 2 s with only 30 frames (a stalled display): still paused until 120 frames
        CHECK(g.display_reinit(20.0), "a new re-init pauses again");
        for (int i = 1; i <= 30; ++i) g.frame(20.0 + i * 0.1);
        CHECK(g.paused(), "3 s but 30 frames: still paused");
        bool early = false;
        for (int i = 31; i < 120; ++i) early = g.frame(23.0 + i * 0.001) || early;
        CHECK(!early && g.paused() && g.frames_since_reinit() == 119, "119 frames: still paused");
        CHECK(g.frame(23.2) && !g.paused(), "frame 120 resumes");
        CHECK(g.reinits() == 2, "two re-inits counted");
    });

    run_case("1.0.3 player capture gate: a second re-init during the pause (the 16:04:09 / 16:04:39 pair) restarts the count", [&] {
        pc::SubmitGate g;
        CHECK(g.display_reinit(0.0), "first re-init: pause starts");
        for (int i = 1; i <= 100; ++i) g.frame(i / 60.0);
        CHECK(!g.display_reinit(1.7), "a re-init while paused does not log a second \"paused\"");
        CHECK(g.frames_since_reinit() == 0, "the frame count starts again");
        for (int i = 1; i < 120; ++i) g.frame(1.7 + i / 60.0);
        CHECK(g.paused(), "119 frames after the second re-init: still paused");
        CHECK(g.frame(1.7 + 120 / 60.0 + 0.01) && !g.paused(), "120 frames and 2 s after the LAST re-init: resumed");
    });

    run_case("1.0.3 player capture gate: the Windows host holds requests while paused and the overlay feeds the gate", [&] {
        const std::string win = read_src("win/player_capture_win.cpp");
        CHECK(!win.empty(), "player_capture_win.cpp read");
        const std::string req = capture_watchdog_test::function_body(win, "bool request(const Request& r, std::string* err) override");
        CHECK(req.find("g_gate.may_submit(") != std::string::npos && req.find("raw->held = true;") != std::string::npos &&
                  req.find("held: ") != std::string::npos,
              "request(): a request is HELD (accepted, not submitted) while the gate is closed");
        const size_t held = req.find("raw->held = true;"), disp = req.find("dispatch_locked(raw);");
        CHECK(held != std::string::npos && disp != std::string::npos && held < disp, "request(): the gate is checked before the game thread gets it");
        const std::string st = capture_watchdog_test::function_body(win, "Status status() override");
        CHECK(st.find("pump_held_locked();") != std::string::npos && st.find("g_gate.may_submit(real_seconds(), 2, &why)") != std::string::npos,
              "status(): a held request is pumped, and `available` stays false while paused (callers wait, nothing fails)");
        const std::string poll = capture_watchdog_test::function_body(win, "bool poll(Result& out) override");
        CHECK(poll.find("if (j->held) return false;") != std::string::npos, "poll(): a held request hands out nothing and has no watchdog");
        const std::string fr = capture_watchdog_test::function_body(win, "void player_capture_frame()");
        const std::string ri = capture_watchdog_test::function_body(win, "void player_capture_display_reinit(const char* what)");
        CHECK(ri.find("capture_log(std::string(\"paused (display re-init: \")") != std::string::npos, "the pause is logged \"player capture: paused (display re-init: ...)\"");
        CHECK(fr.find("capture_log(\"resumed (display stable for ") != std::string::npos && fr.find("pump_held_locked();") != std::string::npos,
              "the resume is logged \"player capture: resumed ...\" and sends a held request");
        CHECK(win.find("constexpr double kAnswerTimeout = 75.0;") != std::string::npos, "kAnswerTimeout unchanged (75 s)");

        const std::string ov = read_src("win/overlay_dx12.cpp");
        CHECK(!ov.empty(), "overlay_dx12.cpp read");
        const std::string ar = capture_watchdog_test::function_body(ov, "static HRESULT around_resize(");
        CHECK(ar.find("player_capture_display_reinit(what)") != std::string::npos, "ResizeBuffers / ResizeBuffers1 (around_resize) report a display re-init");
        CHECK(ov.find("[&] { return o_resize(sc, count, w, h, fmt, flags); }, \"ResizeBuffers\")") != std::string::npos &&
                  ov.find("}, \"ResizeBuffers1\")") != std::string::npos,
              "both resize hooks name themselves");
        const std::string rt = capture_watchdog_test::function_body(ov, "static bool retarget(IDXGISwapChain3* sc)");
        CHECK(rt.find("player_capture_display_reinit(\"new swap chain\")") != std::string::npos, "a replaced swap chain is a re-init too");
        const std::string rf = capture_watchdog_test::function_body(ov, "static void render_frame(IDXGISwapChain3* sc)");
        CHECK(rf.find("player_capture_frame();") != std::string::npos, "every presented frame feeds the gate");
    });

    run_case("1.0.3 player capture pacing: a batch waits 0.25 s after the previous request ended, a single request never", [&] {
        pc::SubmitGate g;
        std::string why;
        CHECK(g.may_submit(0.0, 6, &why), "no request yet: a batch goes");
        g.request_ended(10.0);
        CHECK(!g.may_submit(10.05, 6, &why) && why.find("pacing") != std::string::npos, "50 ms after a request ended (the log's gap): a batch waits: " + why);
        CHECK(g.may_submit(10.05, 1, nullptr), "a single request (Miniface editor, faces card) is never paced");
        CHECK(!g.may_submit(10.249, 2, nullptr) && g.may_submit(10.25, 2, nullptr), "the gap is 0.25 s");
        CHECK(pc::GateConfig().batch_gap_s == 0.25 && pc::GateConfig().settle_frames == 120 && pc::GateConfig().settle_s == 2.0, "defaults");
        // the log's cadence: render 0.64 s + gap -> ~0.9 s per batch of 6 instead of ~0.69 s (a fresh gate: no earlier end)
        g = pc::SubmitGate();
        double t = 0.0;
        int batches = 0;
        while (t < 60.0) {
            while (!g.may_submit(t, 6, nullptr)) t += 0.016;  // one frame at 60 fps
            t += 0.64;
            g.request_ended(t);
            ++batches;
        }
        CHECK(batches >= 60 && batches <= 70, "about 65 batches a minute at the paced rate (" + std::to_string(batches) + ")");
    });

    run_case("1.0.3 sweep cap: Render all stops after 600 heads and goes on only after Continue", [&] {
        pc::SweepCap c;
        CHECK(c.cap() == 600 && pc::kSweepCapHeads == 600, "600 heads per run");
        c.start();
        bool hit = false;
        int batches = 0;
        while (!c.held()) {
            const size_t n = std::min<size_t>(6, c.room());
            hit = c.sent(n) || hit;
            ++batches;
        }
        CHECK(hit && batches == 100 && c.sent_total() == 600 && c.room() == 0, "100 batches of 6, then held (" + std::to_string(batches) + ")");
        CHECK(!c.sent(0), "the cap is reported once");
        c.resume();
        CHECK(!c.held() && c.room() == 600, "Continue: the next 600");
        c.sent(4);
        CHECK(c.room() == 596, "counted");
        c.start();
        CHECK(c.sent_total() == 0 && c.room() == 600, "a new Render all starts at 0");
        pc::SweepCap small(2);
        small.start();
        CHECK(small.sent(2) && small.held(), "a cap of 2 holds after 2");
    });

    run_case("1.0.3 sweep cap and pacing: wired into the face chooser's Render all and the Windows host", [&] {
        const std::string ui = read_src("ui/ui_faces.cpp");
        CHECK(!ui.empty(), "ui_faces.cpp read");
        const std::string tick = capture_watchdog_test::function_body(ui, "void tick_faces3d(App& app)");
        CHECK(tick.find("g_r3d.cap.room()") != std::string::npos && tick.find("next_heads3d(app, max, room)") != std::string::npos,
              "tick_faces3d: the Render all queue gives at most the cap's room");
        CHECK(tick.find("g_r3d.cap.sent(swept)") != std::string::npos && tick.find("faces: Render all paused after ") != std::string::npos,
              "tick_faces3d: counts the sweep's heads sent and logs the pause once");
        const std::string bar = capture_watchdog_test::function_body(ui, "static void head3d_bar(App& app, const std::vector<const Face*>& rows)");
        CHECK(bar.find("if (!head3d_scanned(*f)) {") != std::string::npos && bar.find("if (head3d_scanned(*f) && (!f->game_head") != std::string::npos &&
                  bar.find("generic heads skipped") != std::string::npos,
              "owner 2026-11-01: Render all counts and queues scanned heads only, and says how many generic heads it skips");
        CHECK(ui.find("static bool head3d_scanned(const Face& f) { return f.real || f.game_head; }") != std::string::npos,
              "scanned = headclasscode 0 (Face::real) or a game head");
        CHECK(bar.find("face3d_cached(app, face_in_m(*f), head3d_id(*f))) continue;") != std::string::npos,
              "heads already rendered are skipped: a stopped sweep goes on where it stopped");
        CHECK(bar.find("Continue (next %zu heads)##r3dcont") != std::string::npos && bar.find("faces3d_sweep_continue()") != std::string::npos &&
                  bar.find("g_r3d.cap.start();") != std::string::npos,
              "the chooser: Render all starts a new cap, Continue goes on");
        const std::string win = read_src("win/player_capture_win.cpp");
        size_t ends = 0;
        for (size_t at = win.find("g_gate.request_ended(real_seconds());"); at != std::string::npos; at = win.find("g_gate.request_ended(real_seconds());", at + 1)) ++ends;
        CHECK(ends == 4, "the host records the end of a request where it leaves g_current: settle, poll (2), cancel (" + std::to_string(ends) + ")");
        const std::string st = capture_watchdog_test::function_body(win, "Status status() override");
        CHECK(st.find("g_gate.may_submit(real_seconds(), 2, &why)") != std::string::npos, "status(): not available within the gap after a request");
    });
}

}  // namespace capture_gate_test

inline void test_capture_gate() { capture_gate_test::cases(); }
