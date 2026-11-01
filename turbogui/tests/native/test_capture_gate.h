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
        CHECK(st.find("pump_held_locked();") != std::string::npos && st.find("g_gate.may_submit(real_seconds(), 1, &why)") != std::string::npos,
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
}

}  // namespace capture_gate_test

inline void test_capture_gate() { capture_gate_test::cases(); }
